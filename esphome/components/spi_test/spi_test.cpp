#include "spi_test.h"
#include "esphome/core/log.h"

namespace esphome {
namespace spi_test {

static const char *TAG = "spi_test";

float SPI_TEST::get_setup_priority() const { return setup_priority::HARDWARE; }

void SPI_TEST::setup() {
  ESP_LOGCONFIG(TAG, "Setting up spi_test");
  this->spi_setup();
}

void SPI_TEST::dump_config() {
  ESP_LOGCONFIG(TAG, "SPI_TEST:");
  LOG_PIN("  CS Pin:", this->cs_);
}

bool msb = true;
void SPI_TEST::loop() {
  uint16_t to_write = 0x01;
  ESP_LOGD(TAG, "sending 0x%.4X : MSB %s", to_write, TRUEFALSE(msb));
  this->enable();
  this->write_byte16(to_write, msb);
  this->disable();
  msb = !msb;
}

}  // namespace spi_test
}  // namespace esphome
