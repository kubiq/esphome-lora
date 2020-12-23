#pragma once
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "nextion_component.h"
#include "nextion_base.h"

namespace esphome {
namespace nextion {
class NextionTextSensor;

class NextionTextSensor : public NextionComponent, public text_sensor::TextSensor, public PollingComponent {
 public:
  NextionTextSensor(NextionBase *nextion) { this->nextion_ = nextion; }
  void update() override;

  void on_state_changed(std::string state);

  void process_text(std::string variable_name, std::string text_value);
  void set_state(std::string state, bool publish = true, bool send_to_nextion = true);
  void send_state_to_nextion() override { this->set_state(this->state, false); };
  NextionQueueType get_queue_type() override { return NextionQueueType::TEXT_SENSOR; }
  void set_state_from_int(int state_value, bool publish, bool send_to_nextion) override {}
  void set_state_from_string(std::string state_value, bool publish, bool send_to_nextion) override {
    this->set_state(state_value, publish, send_to_nextion);
  }
};
}  // namespace nextion
}  // namespace esphome
