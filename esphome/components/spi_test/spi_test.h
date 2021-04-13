#pragma once

#include "esphome/core/component.h"
#include "esphome/core/esphal.h"
#include "esphome/components/spi/spi.h"

namespace esphome {
namespace spi_test {

class SPI_TEST : public Component,
                 public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                       spi::DATA_RATE_75KHZ> {
 public:
  SPI_TEST() = default;

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;
  void loop() override;

 protected:
};

}  // namespace spi_test
}  // namespace esphome
