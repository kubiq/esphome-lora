
#include "nextion.h"
#include "esphome/core/util.h"
#include "esphome/core/log.h"

namespace esphome {
namespace nextion {
static const char *TAG = "nextion_upload";

#if defined(USE_TFT_UPLOAD) && (defined(USE_ETHERNET) || defined(USE_WIFI))

// Followed guide
// https://unofficialnextion.com/t/nextion-upload-protocol-v1-2-the-fast-one/1044/2

int Nextion::upload_by_chunks_(int range_start) {
  int range_end = range_start + this->transfer_buffer_size_ - 1;
  if (range_end > this->tft_size_)
    range_end = this->tft_size_;

  HTTPClient http;

  bool begin_status = false;
#ifdef ARDUINO_ARCH_ESP32
  begin_status = http.begin(this->tft_url_.c_str());
#endif
#ifdef ARDUINO_ARCH_ESP8266
#ifndef CLANG_TIDY
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(3);
  begin_status = http.begin(*this->get_wifi_client_(), this->tft_url_.c_str());
#endif
#endif

  if (!begin_status) {
    ESP_LOGD(TAG, "upload_by_chunks_: connection failed");
    return false;
  }

  char range_header[64];
  sprintf(range_header, "bytes=%d-%d", range_start, range_end);
  http.addHeader("Range", range_header);

  ESP_LOGD(TAG, "Requesting range: %s", range_header);

  int tries = 1;
  int code = http.GET();
  delay(200);  // NOLINT
  while (code != 200 && code != 206 && tries <= 5) {
    ESP_LOGD(TAG, "upload_by_chunks_ retrying (%d/5)", tries);
    delay(500);  // NOLINT Needs a decent delay and since we will be rebooting this shouldnt be an issue.
    code = http.GET();
    ++tries;
  }

  if (tries > 5) {
    http.end();
    return -1;
  }

  // Upload the received byte Stream to the nextion
  uint32_t result = this->upload_send_stream_(*http.getStreamPtr(), range_end - range_start);
  http.end();

  return result > 0 ? result : range_end + 1;
}

uint32_t Nextion::upload_send_stream_(Stream &my_file, int range) {
#if defined ESP8266
  yield();
#endif

  // ESP_LOGD(TAG, "upload_send_stream_ start range %d", range);

  size_t size;
  int sent = 0;
  int sent_tmp = 0;
  while (sent < range) {
    size = my_file.available();
    if (!size)
      continue;

    int c = my_file.readBytes(transfer_buffer_,
                              ((size > this->transfer_buffer_size_) ? this->transfer_buffer_size_ : size));
    // ESP_LOGD(TAG, "upload_send_stream_ size %zu sent_packets_ %d c %d", size, this->sent_packets_,c);

    for (uint16_t i = 0; i < c; i++) {
      this->write_byte(transfer_buffer_[i]);
      ++sent;
      ++sent_tmp;

      --this->content_length_;
      //++this->sent_packets_;

      if (sent_tmp == 4096) {
        sent_tmp = 0;

        if (!this->upload_first_chunk_sent_) {
          this->upload_first_chunk_sent_ = true;
          delay(1000);  // NOLINT
        }

        String string = String("");
        this->recv_ret_string_(string, 2048, true);
        if (string[0] == 0x08) {
          uint32_t next_location = 0;
          for (int i = 0; i < 4; ++i) {
            next_location += static_cast<uint8_t>(string[i + 1]) << (8 * i);
          }
          if (next_location != 0) {
            ESP_LOGD(TAG, "Nextion reported new range %d", next_location);
            this->content_length_ = this->tft_size_ - next_location;
            return next_location;
          }
        }
      }
    }
  }
  // ESP_LOGD(TAG, "upload_send_stream_ jumped to 0");
  return 0;
}
void Nextion::upload_tft() {
  if (this->is_updating_) {
    ESP_LOGD(TAG, "Currently updating");
    return;
  }

  if (!network_is_connected()) {
    ESP_LOGD(TAG, "network is not connected");
    return;
  }

  this->is_updating_ = true;

  HTTPClient http;
  bool begin_status = false;
#ifdef ARDUINO_ARCH_ESP32
  begin_status = http.begin(this->tft_url_.c_str());
#endif
#ifdef ARDUINO_ARCH_ESP8266
#ifndef CLANG_TIDY
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(3);
  begin_status = http.begin(*this->get_wifi_client_(), this->tft_url_.c_str());
#endif
#endif

  if (!begin_status) {
    this->is_updating_ = false;
    ESP_LOGD(TAG, "connection failed");
    delete this->transfer_buffer_;
    return;
  } else {
    ESP_LOGD(TAG, "Connected");
  }
  http.addHeader("Range", "bytes=0-255");
  const char *header_names[] = {"Content-Range"};
  http.collectHeaders(header_names, 1);
  ESP_LOGD(TAG, "Requesting URL: %s", this->tft_url_.c_str());

  http.setReuse(true);
  // try up to 5 times. DNS sometimes needs a second try or so
  int tries = 0;
  int code = http.GET();
  delay(100);  // NOLINT

  while (code != 200 && code != 206 && tries < 5) {
    delay(500);  // NOLINT
    code = http.GET();
    ++tries;
  }

  // OK or Partial Content
  if (code == 200 || code == 206) {
    String content_range_string = http.header("Content-Range");
    content_range_string.remove(0, 12);
    this->content_length_ = content_range_string.toInt();
    this->tft_size_ = content_length_;

    http.end();  // End this HTTP call because we read all the data
    delay(2);
    ESP_LOGD(TAG, "%s", content_range_string.c_str());

    if (this->content_length_ < 4096) {
      ESP_LOGE(TAG, "Failed to get file size");
      this->upload_end_();
    }
    ESP_LOGD(TAG, "Updating Nextion...");
    // The Nextion will ignore the update command if it is sleeping
    this->soft_reset();
    delay(500);  // NOLINT
    this->sleep(false);
    this->set_backlight_brightness(1.0);
    delay(500);  // NOLINT

    // Flush serial
    this->flush();

    char command[128];
    // Tells the Nextion the content length of the tft file and baud rate it will be sent at
    // Once the Nextion accepts the command it will wait until the file is successfully uploaded
    // If it fails for any reason a power cycle of the display will be needed
    sprintf(command, "whmi-wris %d,%d,1", this->content_length_, this->parent_->get_baud_rate());
    this->send_command_(command);

    String response = String("");
    this->recv_ret_string_(response, 2000, true);  // This can take some time to return

    // The Nextion display will, if it's ready to accept data, send a 0x05 byte.
    if (response.indexOf(0x05) != -1) {
      ESP_LOGD(TAG, "preparation for tft update done");
    } else {
      ESP_LOGD(TAG, "preparation for tft update failed %d \"%s\"", response[0], response.c_str());
      // this->sent_packets_ = 0;
      this->is_updating_ = false;
      return;
    }
    // We send 4096 bytes to the Nextion so get x 4096 chunkss

    // int chunk = int(((ESP.getFreeHeap()) * .40) / 4096);  // 40% for the transfer buffer
    int chunk = 1;
    if (ESP.getFreeHeap() > 90112)
      chunk = int((ESP.getFreeHeap() - 65536) / 8192);
    uint32_t chunk_size = chunk * 4096;

    chunk_size = chunk_size > 65536 ? 65536 : chunk_size;

    if (this->transfer_buffer_ == nullptr) {
      ESP_LOGD(TAG, "upload_send_stream_ allocating %d buffer", chunk_size);
      this->transfer_buffer_ = new uint8_t[chunk_size];
      if (!this->transfer_buffer_) {  // Try a smaller size
        ESP_LOGD(TAG, "upload_send_stream_ could not allocate buffer size: %d trying 4096 instead", chunk_size);
        chunk_size = 4096;

        ESP_LOGD(TAG, "upload_send_stream_ allocating %d buffer", chunk_size);
        this->transfer_buffer_ = new uint8_t[chunk_size];

        if (!this->transfer_buffer_)
          return;
      }
      this->transfer_buffer_size_ = chunk_size;
    }

    ESP_LOGD(TAG, "Updating tft from \"%s\" with a file size of %d using %d chunksize", this->tft_url_.c_str(),
             this->content_length_, this->transfer_buffer_size_);
    ESP_LOGD(TAG, "Heap Size %d", ESP.getFreeHeap());

    int result = 0;
    while (this->content_length_ > 0) {
      result = this->upload_by_chunks_(result);
      ESP_LOGD(TAG, "Heap Size %d", ESP.getFreeHeap());
      // ESP_LOGD(TAG, "content_length %d this->sent_packets_ %d", this->content_length_, this->sent_packets_);
    }
    ESP_LOGD(TAG, "Succesfully updated Nextion!");

    this->upload_end_();
  }
}

void Nextion::upload_end_() {
  this->soft_reset();
  ESP.restart();
}

#ifdef ARDUINO_ARCH_ESP8266
WiFiClient *Nextion::get_wifi_client_() {
  if (this->tft_url_.compare(0, 6, "https:") == 0) {
    if (this->wifi_client_secure_ == nullptr) {
      this->wifi_client_secure_ = new BearSSL::WiFiClientSecure();
      this->wifi_client_secure_->setInsecure();
      this->wifi_client_secure_->setBufferSizes(512, 512);
    }
    return this->wifi_client_secure_;
  }

  if (this->wifi_client_ == nullptr) {
    this->wifi_client_ = new WiFiClient();
  }
  return this->wifi_client_;
}
#endif

#else
void Nextion::upload_tft() { ESP_LOGW(TAG, "tft_url, WIFI or Ethernet components are needed. Cannot upload."); }
#endif
}  // namespace nextion
}  // namespace esphome
