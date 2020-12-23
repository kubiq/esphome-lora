#include "nextion_binarysensor.h"
#include "esphome/core/util.h"
#include "esphome/core/log.h"

namespace esphome {
namespace nextion {

static const char *TAG = "nextion_binarysensor";

void NextionBinarySensor::process_bool(std::string variable_name, bool on) {
  if (!this->nextion_->is_setup())
    return;

  if (this->variable_name_.empty())  // This is a touch component
    return;

  if (this->variable_name_ == variable_name) {
    this->publish_state(on);
    ESP_LOGD(TAG, "Processed binarysensor \"%s\" state %s", variable_name.c_str(), state ? "ON" : "OFF");
  }
}

void NextionBinarySensor::process_touch(uint8_t page_id, uint8_t component_id, bool on) {
  if (this->page_id_ == page_id && this->component_id_ == component_id) {
    this->publish_state(on);
  }
}

void NextionBinarySensor::update() {
  if (!this->nextion_->is_setup())
    return;

  if (this->variable_name_.empty())  // This is a touch component
    return;

  this->nextion_->add_to_get_queue(this);
}

void NextionBinarySensor::set_state(bool state, bool publish, bool send_to_nextion) {
  if (!this->nextion_->is_setup())
    return;

  if (this->variable_name_.empty())  // This is a legacy touch component
    return;

  if (send_to_nextion) {
    if (this->nextion_->is_sleeping() || !this->visible_) {
      this->needs_to_send_update_ = true;
    } else {
      this->needs_to_send_update_ = false;
      this->nextion_->add_no_result_to_queue_with_set(this, (int) state);
    }
  }

  if (publish) {
    this->publish_state(state);
  } else {
    this->state = state;
    this->has_state_ = true;
  }

  this->update_component();

  if (this->nextion_->is_test_debug())
    ESP_LOGD(TAG, "Wrote state for sensor \"%s\" state %s", this->variable_name_.c_str(), state ? "ON" : "OFF");
}

}  // namespace nextion
}  // namespace esphome
