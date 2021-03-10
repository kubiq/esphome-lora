#include "nextion.h"
#include "esphome/core/util.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <regex>
namespace esphome {
namespace nextion {

static const char *TAG = "nextion";

void Nextion::setup() {
  this->is_setup_ = false;
  this->ignore_is_setup_ = true;
  this->send_command_("bkcmd=0");

  this->send_command_("sleep=0");  // bogus command. needed sometimes after updating
  delay(1500);                     // NOLINT

  this->send_command_("bodkdkdkds=22");  // bogus command. needed sometimes after updating
  delay(100);                            // NOLINT
  this->flush();
  this->send_command_("connect");
  delay(250);  // NOLINT

  String response = String("");
  this->recv_ret_string_(response, 500, false);
  if (response.indexOf(F("comok")) == -1) {
    ESP_LOGD(TAG, "display doesn't accept the first connect request %s", response.c_str());
    for (int i = 0; i < response.length(); i++) {
      ESP_LOGD(TAG, "response %d 0x%02X %c", i, response[i], response[i]);
    }
  } else {
    sscanf(response.c_str(), "%*64[^,],%*64[^,],%64[^,],%64[^,],%*64[^,],%64[^,],%64[^,]", device_model_,
           firmware_version_, serial_number_, flash_size_);
  }

  this->send_command_("bkcmd=3");  // Always, returns 0x00 to 0x23 result of serial command.

  this->set_backlight_brightness(this->brightness_);
  this->goto_page("0");

  if (this->touch_sleep_timeout_ != 0) {
    this->set_touch_sleep_timeout(this->touch_sleep_timeout_);
  }

  if (this->wake_up_page_ != -1) {
    this->set_wake_up_page(this->wake_up_page_);
  }
  this->ignore_is_setup_ = false;
}

void Nextion::dump_config() {
  ESP_LOGCONFIG(TAG, "Nextion:");
  ESP_LOGCONFIG(TAG, "  Baud Rate:        %d", this->parent_->get_baud_rate());
  ESP_LOGCONFIG(TAG, "  Device Model:     %s", this->device_model_);
  ESP_LOGCONFIG(TAG, "  Firmware Version: %s", this->firmware_version_);
  ESP_LOGCONFIG(TAG, "  Serial Number:    %s", this->serial_number_);
  ESP_LOGCONFIG(TAG, "  Flash Size:       %s", this->flash_size_);
  ESP_LOGCONFIG(TAG, "  Wake On Touch:    %s", this->auto_wake_on_touch_ ? "True" : "False");

  if (this->touch_sleep_timeout_ != 0) {
    ESP_LOGCONFIG(TAG, "  Touch Timeout:       %d", this->touch_sleep_timeout_);
  }

  if (this->wake_up_page_ != -1) {
    ESP_LOGCONFIG(TAG, "  Wake Up Page :       %d", this->wake_up_page_);
  }
}

float Nextion::get_setup_priority() const { return setup_priority::DATA; }
void Nextion::update() {
  if (!this->is_setup()) {
    return;
  }
  if (this->writer_.has_value()) {
    (*this->writer_)(*this);
  }
}

void Nextion::add_sleep_state_callback(std::function<void(bool)> &&callback) {
  this->sleep_callback_.add(std::move(callback));
}

void Nextion::add_wake_state_callback(std::function<void(bool)> &&callback) {
  this->wake_callback_.add(std::move(callback));
}

void Nextion::update_all_components() {
  if ((!this->is_setup() && !this->ignore_is_setup_) || this->is_sleeping())
    return;

  for (auto *binarysensortype : this->binarysensortype_) {
    binarysensortype->update_component();
  }
  for (auto *sensortype : this->sensortype_) {
    sensortype->update_component();
  }
  for (auto *switchtype : this->switchtype_) {
    switchtype->update_component();
  }
  for (auto *textsensortype : this->textsensortype_) {
    textsensortype->update_component();
  }
}

bool Nextion::send_command_printf(const char *format, ...) {
  if ((!this->is_setup() && !this->ignore_is_setup_) || this->is_sleeping())
    return false;

  char buffer[256];
  va_list arg;
  va_start(arg, format);
  int ret = vsnprintf(buffer, sizeof(buffer), format, arg);
  va_end(arg);
  if (ret <= 0) {
    ESP_LOGW(TAG, "Building command for format '%s' failed!", format);
    return false;
  }
  this->add_no_result_to_queue_("send_command_printf");
  this->send_command_(buffer);

  return true;
}

void Nextion::send_command_no_ack(const char *command) {
  if ((!this->is_setup() && !this->ignore_is_setup_) || this->is_sleeping())
    return;

  this->add_no_result_to_queue_("send_command_no_ack");
  this->send_command_(command);
}

void Nextion::send_command_(const char *command) {
  if (!this->ignore_is_setup_ && !this->is_setup()) {
    return;
  }

#ifdef PROTOCOL_LOG
  ESP_LOGD(TAG, "send_command %s", command);
#endif

  this->write_str(command);
  const uint8_t to_send[3] = {0xFF, 0xFF, 0xFF};
  this->write_array(to_send, sizeof(to_send));
}

#ifdef PROTOCOL_LOG
void Nextion::print_queue_members_() {
  ESP_LOGD(TAG, "print_queue_members_ size %zu", this->nextion_queue_.size());
  ESP_LOGD(TAG, "*******************************************");
  for (auto *i : this->nextion_queue_) {
    if (i == nullptr) {
      ESP_LOGD(TAG, "Nextion queue is null");
    } else {
      ESP_LOGD(TAG, "Nextion queue type: %d:%s , name: %s", i->get_queue_type(), i->get_queue_type_string().c_str(),
               i->get_variable_name().c_str());
    }
  }
  ESP_LOGD(TAG, "*******************************************");
}
#endif

void Nextion::loop() {
  if (this->is_updating_)
    return;

  this->process_nextion_commands_();
}

// nextion.tech/instruction-set/
bool Nextion::process_nextion_commands_() {
  uint8_t d;

  while (this->available()) {
    read_byte(&d);
    this->command_data_ += d;
    ESP_LOGD(TAG, "Available 0x%02X", d);
  }

#if defined ESP8266
  yield();
#endif

  std::string delimiter;
  delimiter.append(3, 255);

  size_t to_process_length = 0;
  std::string to_process;

 ESP_LOGD(TAG, "Loop Start print_queue_members_ size %zu", this->nextion_queue_.size());

  while ((to_process_length = this->command_data_.find(delimiter)) != std::string::npos) {
#ifdef PROTOCOL_LOG
    this->print_queue_members_();
#endif

    this->nextion_event_ = this->command_data_[0];
    to_process_length -= 1;

    to_process = this->command_data_.substr(1, to_process_length);

    switch (this->nextion_event_) {
      case 0x00:  // instruction sent by user has failed
        ESP_LOGW(TAG, "Nextion reported invalid instruction!");
        break;
      case 0x01:  // instruction sent by user was successful

#ifdef PROTOCOL_LOG
        ESP_LOGD(TAG, "instruction sent by user was successful");
        ESP_LOGD(TAG, "this->nextion_queue_.empty() %s", this->nextion_queue_.empty() ? "True" : "False");
#endif

        if (!this->nextion_queue_.empty() &&
            this->nextion_queue_.front()->get_queue_type() == NextionQueueType::NO_RESULT) {
          NextionComponentBase *nextion_queue = this->nextion_queue_.front();

#ifdef PROTOCOL_LOG
          ESP_LOGD(TAG, "Removing %s from the queue", nextion_queue->get_variable_name().c_str());
#endif
          this->nextion_queue_.pop_front();
          delete nextion_queue;

          if (!this->is_setup_) {
            if (this->nextion_queue_.empty()) {
              ESP_LOGD(TAG, "Nextion is setup");
              this->is_setup_ = true;
            }
          }
        } else {
          ESP_LOGE(TAG, "Queue is empty!");
        }

        break;
      case 0x02:  // invalid Component ID or name was used

        if (!this->nextion_queue_.empty()) {
          NextionComponentBase *nextion_queue = this->nextion_queue_.front();

#ifdef PROTOCOL_LOG
          ESP_LOGD(TAG, "Removing %s from the queue", nextion_queue->get_variable_name().c_str());
#endif
          ESP_LOGW(TAG, "Nextion reported component ID \"%s\" invalid!", nextion_queue->get_variable_name().c_str());
          this->nextion_queue_.pop_front();
          if (nextion_queue->get_queue_type() == NextionQueueType::NO_RESULT) {
            delete nextion_queue;
          }

        } else {
          ESP_LOGE(TAG, "Nextion reported component ID invalid but queue is empty!");
        }

        break;
      case 0x03:  // invalid Page ID or name was used
        ESP_LOGW(TAG, "Nextion reported page ID invalid!");
        break;
      case 0x04:  // invalid Picture ID was used
        ESP_LOGW(TAG, "Nextion reported picture ID invalid!");
        break;
      case 0x05:  // invalid Font ID was used
        ESP_LOGW(TAG, "Nextion reported font ID invalid!");
        break;
      case 0x06:  // File operation fails
        ESP_LOGW(TAG, "Nextion File operation fail!");
        break;
      case 0x09:  // Instructions with CRC validation fails their CRC check
        ESP_LOGW(TAG, "Nextion Instructions with CRC validation fails their CRC check!");
        break;
      case 0x11:  // invalid Baud rate was used
        ESP_LOGW(TAG, "Nextion reported baud rate invalid!");
        break;
      case 0x12:  // invalid Waveform ID or Channel # was used

        if (!this->nextion_queue_.empty()) {
          int index = 0;
          int found = -1;
          for (auto &i : this->nextion_queue_) {
            if (i->get_queue_type() == NextionQueueType::WAVEFORM_SENSOR) {
#ifdef PROTOCOL_LOG
              ESP_LOGD(TAG, "Removing waveform from queue with component id %d and waveform id %d",
                       i->get_component_id(), i->get_wave_channel_id());
#endif
              ESP_LOGW(TAG, "Nextion reported invalid Waveform ID %d or Channel # %d was used!", i->get_component_id(),
                       i->get_wave_channel_id());
              found = index;
              break;
            }
            ++index;
          }

          if (found != -1) {
            this->nextion_queue_.erase(this->nextion_queue_.begin() + found);
          } else {
            ESP_LOGW(
                TAG,
                "Nextion reported invalid Waveform ID or Channel # was used but no waveform sensor in queue found!");
          }
        }
        break;
      case 0x1A:  // variable name invalid

        if (!this->nextion_queue_.empty()) {
          NextionComponentBase *nextion_queue = this->nextion_queue_.front();

#ifdef PROTOCOL_LOG
          ESP_LOGD(TAG, "Removing %s from the queue", nextion_queue->get_variable_name().c_str());
#endif
          ESP_LOGW(TAG, "Nextion reported variable name \"%s\" invalid!", nextion_queue->get_variable_name().c_str());

          this->nextion_queue_.pop_front();
          if (nextion_queue->get_queue_type() == NextionQueueType::NO_RESULT) {
            delete nextion_queue;
          }

        } else {
          ESP_LOGE(TAG, "Nextion reported variable name invalid but queue is empty!");
        }

        break;
      case 0x1B:  // variable operation invalid
        ESP_LOGW(TAG, "Nextion reported variable operation invalid!");
        break;
      case 0x1C:  // failed to assign
        ESP_LOGW(TAG, "Nextion reported failed to assign variable!");
        break;
      case 0x1D:  // operate EEPROM failed
        ESP_LOGW(TAG, "Nextion reported operating EEPROM failed!");
        break;
      case 0x1E:  // parameter quantity invalid
        ESP_LOGW(TAG, "Nextion reported parameter quantity invalid!");
        break;
      case 0x1F:  // IO operation failed
        ESP_LOGW(TAG, "Nextion reported component I/O operation invalid!");
        break;
      case 0x20:  // undefined escape characters
        ESP_LOGW(TAG, "Nextion reported undefined escape characters!");
        break;
      case 0x23:  // too long variable name
        ESP_LOGW(TAG, "Nextion reported too long variable name!");

        if (!this->nextion_queue_.empty()) {
          auto nextion_queue = this->nextion_queue_.front();
#ifdef PROTOCOL_LOG
          ESP_LOGD(TAG, "Removing %s from the queue", nextion_queue->get_variable_name().c_str());
#endif
          this->nextion_queue_.pop_front();
          if (nextion_queue->get_queue_type() == NextionQueueType::NO_RESULT) {
            delete nextion_queue;
          }
        }

        break;
      case 0x24:  //  Serial Buffer overflow occurs
        ESP_LOGW(TAG, "Nextion reported Serial Buffer overflow!");
        break;
      case 0x65: {  // touch event return data
        if (to_process_length != 3) {
          ESP_LOGW(TAG, "Touch event data is expecting 3, received %d", to_process_length);

          break;
        }
        uint8_t page_id = to_process[0];
        uint8_t component_id = to_process[1];
        uint8_t touch_event = to_process[2];  // 0 -> release, 1 -> press
        ESP_LOGD(TAG, "Got touch page=%u component=%u type=%s", page_id, component_id,
                 touch_event ? "PRESS" : "RELEASE");
        for (auto *touch : this->touch_) {
          touch->process_touch(page_id, component_id, touch_event != 0);
        }
        break;
      }
      case 0x67:
      case 0x68: {  // touch coordinate data

        if (to_process_length != 5) {
          ESP_LOGW(TAG, "Touch coordinate data is expecting 5, received %d", to_process_length);
          ESP_LOGW(TAG, "%s", to_process.c_str());
          break;
        }

        uint16_t x = (uint16_t(to_process[0]) << 8) | to_process[1];
        uint16_t y = (uint16_t(to_process[2]) << 8) | to_process[3];
        uint8_t touch_event = to_process[4];  // 0 -> release, 1 -> press
        ESP_LOGD(TAG, "Got touch at x=%u y=%u type=%s", x, y, touch_event ? "PRESS" : "RELEASE");
        break;
      }
      case 0x66:  // sendme page id

      //  0x70 0x61 0x62 0x31 0x32 0x33 0xFF 0xFF 0xFF
      //  Returned when using get command for a string.
      //  Each byte is converted to char.
      //  data: ab123
      case 0x70:  // string variable data return
      {
        if (this->nextion_queue_.empty()) {
          ESP_LOGW(TAG, "ERROR: Received string return but the queue is empty");
          break;
        }

        if (to_process_length == 0) {
          ESP_LOGE(TAG, "ERROR: Received string return but no data!");
          break;
        }

        // command_data_[this->command_data_length_] = 0x00;

        // std::string buffer(reinterpret_cast<char const *>(command_data_), this->command_data_length_);

        auto nextion_queue = this->nextion_queue_.front();

#ifdef PROTOCOL_LOG
        ESP_LOGD(TAG, "Received get_string response: \"%s\" for component id: %s, type: %s", to_process.c_str(),
                 nextion_queue->get_variable_name().c_str(), nextion_queue->get_queue_type_string().c_str());
#endif
        if (nextion_queue->get_queue_type() != NextionQueueType::TEXT_SENSOR) {
          ESP_LOGE(TAG, "ERROR: Received string return but next in queue \"%s\" is not a text sensor",
                   nextion_queue->get_variable_name().c_str());
          break;
        }

        nextion_queue->set_state_from_string(to_process, true, false);
        this->nextion_queue_.pop_front();
        if (nextion_queue->get_queue_type() == NextionQueueType::NO_RESULT) {
          delete nextion_queue;
        }
        break;
      }
        //  0x71 0x01 0x02 0x03 0x04 0xFF 0xFF 0xFF
        //  Returned when get command to return a number
        //  4 byte 32-bit value in little endian order.
        //  (0x01+0x02*256+0x03*65536+0x04*16777216)
        //  data: 67305985
      case 0x71:  // numeric variable data return
      {
        if (this->nextion_queue_.empty()) {
          ESP_LOGE(TAG, "ERROR: Received numeric return but the queue is empty");
          break;
        }

        if (to_process_length == 0) {
          ESP_LOGE(TAG, "ERROR: Received numeric return but no data!");
          break;
        }

        int dataindex = 0;

        int value = 0;

        for (int i = 0; i < to_process_length; ++i) {
          value += to_process[i] << (8 * i);
          ++dataindex;
        }

        // if the length is < 4 than its a negative. 2s complement conversion is needed and
        // fill in any missing bytes and then flip the bits
        if (dataindex < 4) {
          for (int i = dataindex; i < 4; ++i) {
            value += 255 << (8 * i);
          }
        }

        auto nextion_queue = this->nextion_queue_.front();

#ifdef PROTOCOL_LOG
        ESP_LOGD(TAG, "Received numeric return for variable %s, queue type %d:%s, value %d",
                 nextion_queue->get_variable_name().c_str(), nextion_queue->get_queue_type(),
                 nextion_queue->get_queue_type_string().c_str(), value);
#endif
        if (nextion_queue->get_queue_type() != NextionQueueType::SENSOR &&
            nextion_queue->get_queue_type() != NextionQueueType::BINARY_SENSOR &&
            nextion_queue->get_queue_type() != NextionQueueType::SWITCH) {
          ESP_LOGE(TAG, "ERROR: Received numeric return but next in queue \"%s\" is not a valid sensor",
                   nextion_queue->get_variable_name().c_str());
          break;
        }

        nextion_queue->set_state_from_int(value, true, false);
        this->nextion_queue_.pop_front();

        break;
      }

      case 0x86: {  // device automatically enters into sleep mode
        ESP_LOGD(TAG, "Received Nextion entering sleep automatically");
        this->sleep_callback_.call(true);
        this->set_is_sleeping_(true);
        break;
      }
      case 0x87:  // device automatically wakes up
      {
        ESP_LOGD(TAG, "Received Nextion leaves sleep automatically");
        this->wake_callback_.call(true);
        this->set_is_sleeping_(false);
        this->all_components_send_state_();
        break;
      }
      case 0x88:  // system successful start up
      {
        ESP_LOGD(TAG, "system successful start up %d", to_process_length);
        break;
      }
      case 0x89:  // start SD card upgrade
      // Data from nextion is
      // 0x90 - Start
      // variable length of 0x70 return formatted data (bytes) that contain the variable name: prints "temp1",0
      // 00 - NULL
      // 00/01 - Single byte for on/off
      // FF FF FF - End
      case 0x90: {  // Switched component
        std::string variable_name;
        uint8_t variable_name_end = 0;
        uint8_t index = 0;

        // Get variable name
        for (index = 0; index < to_process_length; ++index) {
          if (to_process[index] == 0) {  // First Null
            variable_name_end = index;
            break;
          }
          variable_name += to_process[index];
        }
        if (variable_name_end == 0) {
          break;
        }
        ++index;

#ifdef PROTOCOL_LOG
        ESP_LOGD(TAG, "Got Switch variable_name=%s value=%d", variable_name.c_str(), to_process[0] != 0);
#endif

        for (auto *switchtype : this->switchtype_) {
          switchtype->process_bool(variable_name, to_process[index] != 0);
        }
        break;
      }
      // Data from nextion is
      // 0x91 - Start
      // variable length of 0x70 return formatted data (bytes) that contain the variable name: prints "temp1",0
      // 00 - NULL
      // variable length of 0x71 return data: prints temp1.val,0
      // FF FF FF - End
      case 0x91: {  // Sensor component
        std::string variable_name;
        uint8_t variable_name_end = 0;
        uint8_t index = 0;

        // Get variable name
        for (index = 0; index < to_process_length; ++index) {
          if (to_process[index] == 0) {  // First Null
            variable_name_end = index;
            break;
          }
          variable_name += to_process[index];
        }
        if (variable_name_end == 0) {
          // invalid_command_data_length = true;
          break;
        }

        int value = 0;
        int dataindex = 0;
        for (int i = 0; i < to_process_length - index - 1; ++i) {
          value += to_process[i + index + 1] << (8 * i);
          ++dataindex;
        }

        // if the length is < 4 than its a negative.
        // fill in any missing bytes
        if (dataindex < 4) {
          for (int i = dataindex; i < 4; ++i) {
            value += 255 << (8 * i);
          }
        }

#ifdef PROTOCOL_LOG
        ESP_LOGD(TAG, "Got sensor variable_name=%s value=%d", variable_name.c_str(), value);
#endif

        for (auto *sensor : this->sensortype_) {
          sensor->process_sensor(variable_name, value);
        }
        break;
      }
      // Data from nextion is
      // 0x92 - Start
      // variable length of 0x70 return formatted data (bytes) that contain the variable name: prints "temp1",0
      // 00 - NULL
      // variable length of 0x70 return formatted data (bytes) that contain the text prints temp1.txt,0
      // 00 - NULL
      // FF FF FF - End
      case 0x92: {  // Text Sensor Component
        std::string variable_name;
        std::string text_value;
        uint8_t variable_name_end = 0;
        uint8_t index = 0;

        // Get variable name
        for (index = 0; index < to_process_length; ++index) {
          if (to_process[index] == 0) {  // First Null
            variable_name_end = index;
            break;
          }
          variable_name[index] = to_process[index];
        }
        if (variable_name_end == 0) {
          // invalid_command_data_length = true;
          break;
        }

        variable_name_end = 0;

        for (int i = index + 1; i < to_process_length; ++i) {
          if (to_process[i] == 0) {  // Second Null
            variable_name_end = index;
            break;
          }
          text_value += to_process[i];
        }
        if (variable_name_end == 0) {
          // invalid_command_data_length = true;
          break;
        }
#ifdef PROTOCOL_LOG
        ESP_LOGD(TAG, "Got Text Sensor variable_name=%s value=%s", variable_name.c_str(), text_value.c_str());
#endif
        for (auto *textsensortype : this->textsensortype_) {
          textsensortype->process_text(variable_name, text_value);
        }
        break;
      }
      // Data from nextion is
      // 0x90 - Start
      // variable length of 0x70 return formatted data (bytes) that contain the variable name: prints "temp1",0
      // 00 - NULL
      // 00/01 - Single byte for on/off
      // FF FF FF - End
      case 0x93: {  // Binary Sensor component
        char variable_name[64];
        uint8_t variable_name_end = 0;
        uint8_t index = 0;

        // Get variable name
        for (index = 0; index < to_process_length; ++index) {
          variable_name[index] = to_process[index];
          if (to_process[index] == 0) {  // First Null
            variable_name_end = index;
            break;
          }
        }
        if (variable_name_end == 0) {
          // invalid_command_data_length = true;
          break;
        }
        ++index;

#ifdef PROTOCOL_LOG
        ESP_LOGD(TAG, "Got Binary Sensor variable_name=%s value=%d", variable_name, to_process[index] != 0);
#endif
        for (auto *binarysensortype : this->binarysensortype_) {
          binarysensortype->process_bool(&variable_name[0], to_process[index] != 0);
        }
        break;
      }
      case 0xFD: {  // data transparent transmit finished
        ESP_LOGD(TAG, "Nextion reported data transmit finished!");
        break;
      }
      case 0xFE: {  // data transparent transmit ready
        ESP_LOGD(TAG, "Nextion reported ready for transmit!");

        int index = 0;
        int found = -1;
        for (auto &i : this->nextion_queue_) {
          if (i->get_queue_type() == NextionQueueType::WAVEFORM_SENSOR) {
            this->write_array(i->get_wave_buffer().data(), static_cast<int>(i->get_wave_buffer_size()));
#ifdef PROTOCOL_LOG
            ESP_LOGD(TAG, "Nextion sending waveform data for component id %d and waveform id %d", i->get_component_id(),
                     i->get_wave_channel_id());
#endif
            if (i->get_wave_buffer().size() <= 255) {
              i->get_wave_buffer().clear();  // faster than the below
            } else {
              i->get_wave_buffer().erase(i->get_wave_buffer().begin(),
                                         i->get_wave_buffer().begin() + i->get_wave_buffer_size());
            }
            found = index;
            break;
          }
          ++index;
        }

        if (found == -1) {
          ESP_LOGE(TAG, "No waveforms in queue to send data!");
          break;
        } else {
          this->nextion_queue_.erase(this->nextion_queue_.begin() + found);
        }
        break;
      }
      default:
        ESP_LOGW(TAG, "Received unknown event from nextion: 0x%02X", this->nextion_event_);
        break;
    }

#ifdef PROTOCOL_LOG
    ESP_LOGD(TAG, "nextion loop end");
#endif

    this->command_data_.erase(0, to_process_length + delimiter.length() + 1);
  }


   ESP_LOGD(TAG, "Loop End print_queue_members_ size %zu", this->nextion_queue_.size());
  return false;
}

void Nextion::set_nextion_sensor_state(int queue_type, std::string name, float state) {
  this->set_nextion_sensor_state(static_cast<NextionQueueType>(queue_type), name, state);
}

void Nextion::set_nextion_sensor_state(NextionQueueType queue_type, std::string name, float state) {
  if (queue_type < 0 || queue_type > 2)
    return;

#ifdef PROTOCOL_LOG
  ESP_LOGD(TAG, "Received state for variable %s, state %lf", name.c_str(), state);
#endif
  switch (queue_type) {
    case NextionQueueType::SENSOR: {
      for (auto *sensor : this->sensortype_) {
        if (name == sensor->get_variable_name()) {
          sensor->set_state(state, true, true);
          break;
        }
      }
      break;
    }
    case NextionQueueType::BINARY_SENSOR: {
      for (auto *sensor : this->binarysensortype_) {
        if (name == sensor->get_variable_name()) {
          sensor->set_state(state != 0, true, true);
          break;
        }
      }
      break;
    }
    case NextionQueueType::TEXT_SENSOR: {
      break;
    }
    case NextionQueueType::NO_RESULT: {
      break;
    }
    case NextionQueueType::WAVEFORM_SENSOR: {
      break;
    }
    case NextionQueueType::SWITCH: {
      for (auto *sensor : this->switchtype_) {
        if (name == sensor->get_variable_name()) {
          sensor->set_state(state != 0, true, true);
          break;
        }
      }
      break;
    }
  }
}

void Nextion::set_nextion_text_state(std::string name, std::string state) {
  ESP_LOGD(TAG, "Received state for variable %s, state %s", name.c_str(), state.c_str());

  for (auto *sensor : this->textsensortype_) {
    if (name == sensor->get_variable_name()) {
      sensor->set_state(state, true, true);
      break;
    }
  }
}

void Nextion::all_components_send_state_(bool ignore_needs_update) {
  for (auto *binarysensortype : this->binarysensortype_) {
    if (ignore_needs_update || binarysensortype->get_needs_to_send_update())
      binarysensortype->send_state_to_nextion();
  }
  for (auto *sensortype : this->sensortype_) {
    if ((ignore_needs_update || sensortype->get_needs_to_send_update()) && sensortype->get_wave_chan_id() == 0)
      sensortype->send_state_to_nextion();
  }
  for (auto *switchtype : this->switchtype_) {
    if (ignore_needs_update || switchtype->get_needs_to_send_update())
      switchtype->send_state_to_nextion();
  }
  for (auto *textsensortype : this->textsensortype_) {
    if (ignore_needs_update || textsensortype->get_needs_to_send_update())
      textsensortype->send_state_to_nextion();
  }
}

void Nextion::update_components_by_prefix(std::string page) {
  for (auto *binarysensortype : this->binarysensortype_) {
    if (binarysensortype->get_variable_name().rfind(page, 0) == 0)
      binarysensortype->update_component_settings(true);
  }
  for (auto *sensortype : this->sensortype_) {
    if (sensortype->get_variable_name().rfind(page, 0) == 0)
      sensortype->update_component_settings(true);
  }
  for (auto *switchtype : this->switchtype_) {
    if (switchtype->get_variable_name().rfind(page, 0) == 0)
      switchtype->update_component_settings(true);
  }
  for (auto *textsensortype : this->textsensortype_) {
    if (textsensortype->get_variable_name().rfind(page, 0) == 0)
      textsensortype->update_component_settings(true);
  }
}

uint16_t Nextion::recv_ret_string_(String &response, uint32_t timeout, bool recv_flag) {
#if defined ESP8266
  delay(1);
#endif

  uint16_t ret = 0;
  uint8_t c = 0;
  uint8_t nr_of_ff_bytes = 0;
  long start;
  bool exit_flag = false;
  bool ff_flag = false;

  start = millis();

  while (millis() - start <= timeout) {
    while (this->available()) {
      this->read_byte(&c);
      //       App.feed_wdt();
      // #if defined ESP8266
      //       yield();
      // #endif

      if (c == 0xFF)
        nr_of_ff_bytes++;
      else {
        nr_of_ff_bytes = 0;
        ff_flag = false;
      }

      if (nr_of_ff_bytes >= 3)
        ff_flag = true;

      response += (char) c;

      if (recv_flag) {
        if (response.indexOf(0x05) != -1) {
          exit_flag = true;
        }
      }
    }
    if (exit_flag || ff_flag) {
      break;
    }
  }

  if (ff_flag)
    response = response.substring(0, response.length() - 3);  // Remove last 3 0xFF

  ret = response.length();
  return ret;
}

/**
 * @brief
 *
 * @param variable_name Name for the queue
 */
void Nextion::add_no_result_to_queue_(std::string name) {
  nextion::NextionComponentBase *nextion_queue = new nextion::NextionComponentBase;
  nextion_queue->set_variable_name(name);

  this->nextion_queue_.push_back(nextion_queue);

#ifdef PROTOCOL_LOG
  ESP_LOGD(TAG, "Add to queue type: NORESULT component %s", nextion_queue->get_variable_name().c_str());
#endif
}

/**
 * @brief
 *
 * @param variable_name Variable name for the queue
 * @param command
 */
void Nextion::add_no_result_to_queue_with_command_(std::string variable_name, std::string command) {
  if (!this->is_setup() && !this->ignore_is_setup_)
    return;

  this->add_no_result_to_queue_(variable_name);

  if (!command.empty()) {
    this->send_command_(command.c_str());
  }
}

/**
 * @brief Sends a formatted command to the nextion
 *
 * @param variable_name Variable name for the queue
 * @param format The printf-style command format, like "vis %s,0"
 * @param ... The format arguments
 */
void Nextion::add_no_result_to_queue_with_printf_(std::string variable_name, const char *format, ...) {
  if ((!this->is_setup() && !this->ignore_is_setup_) || this->is_sleeping())
    return;

  char buffer[256];
  va_list arg;
  va_start(arg, format);
  int ret = vsnprintf(buffer, sizeof(buffer), format, arg);
  va_end(arg);
  if (ret <= 0) {
    ESP_LOGW(TAG, "Building command for format '%s' failed!", format);
    return;
  }

  this->add_no_result_to_queue_with_command_(variable_name, buffer);
}

/**
 * @brief
 *
 * @param variable_name Variable name for the queue
 * @param variable_name_to_send Variable name for the left of the command
 * @param state_value Value to set
 * @param is_sleep_safe The command is safe to send when the Nextion is sleeping
 */

void Nextion::add_no_result_to_queue_with_set(NextionComponentBase *component, int state_value) {
  this->add_no_result_to_queue_with_set(component->get_variable_name(), component->get_variable_name_to_send(),
                                        state_value);
}

void Nextion::add_no_result_to_queue_with_set(std::string variable_name, std::string variable_name_to_send,
                                              int state_value) {
  this->add_no_result_to_queue_with_set_internal_(variable_name, variable_name_to_send, state_value);
}

void Nextion::add_no_result_to_queue_with_set_internal_(std::string variable_name, std::string variable_name_to_send,
                                                        int state_value, bool is_sleep_safe) {
  if ((!this->is_setup() && !this->ignore_is_setup_) || (!is_sleep_safe && this->is_sleeping()))
    return;

  this->add_no_result_to_queue_with_printf_(variable_name, "%s=%d", variable_name_to_send.c_str(), state_value);
}
/**
 * @brief
 *
 * @param variable_name Variable name for the queue
 * @param variable_name_to_send Variable name for the left of the command
 * @param state_value Sting value to set
 * @param is_sleep_safe The command is safe to send when the Nextion is sleeping
 */

void Nextion::add_no_result_to_queue_with_set(NextionComponentBase *component, std::string state_value) {
  this->add_no_result_to_queue_with_set(component->get_variable_name(), component->get_variable_name_to_send(),
                                        state_value);
}
void Nextion::add_no_result_to_queue_with_set(std::string variable_name, std::string variable_name_to_send,
                                              std::string state_value) {
  this->add_no_result_to_queue_with_set_internal_(variable_name, variable_name_to_send, state_value);
}

void Nextion::add_no_result_to_queue_with_set_internal_(std::string variable_name, std::string variable_name_to_send,
                                                        std::string state_value, bool is_sleep_safe) {
  if ((!this->is_setup() && !this->ignore_is_setup_) || (!is_sleep_safe && this->is_sleeping()))
    return;

  this->add_no_result_to_queue_with_printf_(variable_name, "%s=\"%s\"", variable_name_to_send.c_str(),
                                            state_value.c_str());
}

void Nextion::add_to_get_queue(NextionComponentBase *component) {
  if ((!this->is_setup() && !this->ignore_is_setup_))
    return;

  this->nextion_queue_.push_back(component);

#ifdef PROTOCOL_LOG
  ESP_LOGD(TAG, "Add to queue type: %s component %s", component->get_queue_type_string().c_str(),
           component->get_variable_name().c_str());
#endif

  char command[64];
  sprintf(command, "get %s", component->get_variable_name_to_send().c_str());
  this->send_command_(command);
}

/**
 * @brief Add addt command to the queue
 *
 * @param component_id The waveform component id
 * @param wave_chan_id The waveform channel to send it to
 * @param buffer_to_send The buffer size
 * @param buffer_size The buffer data
 */
void Nextion::add_addt_command_to_queue(NextionComponentBase *component) {
  if ((!this->is_setup() && !this->ignore_is_setup_) || this->is_sleeping())
    return;

  this->nextion_queue_.push_back(component);

  char command[64];
  sprintf(command, "addt %d,%u,%zu", component->get_component_id(), component->get_wave_channel_id(),
          component->get_wave_buffer_size());
  this->send_command_(command);
}

void Nextion::set_writer(const nextion_writer_t &writer) { this->writer_ = writer; }

void Nextion::set_wait_for_ack(bool wait_for_ack) { ESP_LOGW(TAG, "This command is depreciated"); }

}  // namespace nextion
}  // namespace esphome
