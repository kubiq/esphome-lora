#pragma once
#include "esphome/core/defines.h"

namespace esphome {
namespace nextion {

enum NextionQueueType {
  SENSOR = 0,
  BINARY_SENSOR = 1,
  SWITCH = 2,
  TEXT_SENSOR = 3,
  WAVEFORM_SENSOR = 4,
  NO_RESULT = 5,
};

static const char *NextionQueueTypeStrings[] = {"SENSOR",      "BINARY_SENSOR",   "SWITCH",
                                                "TEXT_SENSOR", "WAVEFORM_SENSOR", "NO_RESULT"};

class NextionComponentBase;

class NextionComponentBase {
 public:
 virtual ~NextionComponentBase() = default;

  void set_variable_name(std::string variable_name, std::string variable_name_to_send = "") {
    variable_name_ = variable_name;
    if (variable_name_to_send.empty()) {
      variable_name_to_send_ = variable_name_;
    } else {
      variable_name_to_send_ = variable_name_to_send;
    }
  }

  uint8_t get_component_id() { return this->component_id_; }
  void set_component_id(uint8_t component_id) { component_id_ = component_id; }

  uint8_t get_wave_channel_id() { return this->wave_chan_id_; }
  void set_wave_channel_id(uint8_t wave_chan_id) { this->wave_chan_id_ = wave_chan_id; }

  std::vector<uint8_t> get_wave_buffer() { return this->wave_buffer_; }
  size_t get_wave_buffer_size() { return this->wave_buffer_.size(); }

  std::string get_variable_name() { return this->variable_name_; }
  std::string get_variable_name_to_send() { return this->variable_name_to_send_; }
  virtual NextionQueueType get_queue_type() { return NextionQueueType::NO_RESULT; }
  virtual std::string get_queue_type_string() { return NextionQueueTypeStrings[this->get_queue_type()]; }
  virtual void set_state_from_int(int state_value, bool publish, bool send_to_nextion){};
  virtual void set_state_from_string(std::string state_value, bool publish, bool send_to_nextion){};

 protected:
  std::string variable_name_;
  std::string variable_name_to_send_;

  uint8_t component_id_ = 0;
  uint8_t wave_chan_id_ = UINT8_MAX;
  std::vector<uint8_t> wave_buffer_;
};
}  // namespace nextion
}  // namespace esphome
