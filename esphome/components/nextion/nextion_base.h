#pragma once
#include "esphome/core/color.h"
#include "nextion_component_base.h"
namespace esphome {
namespace nextion {

class NextionBase;

static const uint8_t LOOP_TIMEOUT_MS = 200;

class NextionBase {
 public:
  virtual void add_no_result_to_queue_with_set(NextionComponentBase *component, int state_value) = 0;
  virtual void add_no_result_to_queue_with_set(std::string variable_name, std::string variable_name_to_send,
                                               int state_value) = 0;

  virtual void add_no_result_to_queue_with_set(NextionComponentBase *component, std::string state_value) = 0;
  virtual void add_no_result_to_queue_with_set(std::string variable_name, std::string variable_name_to_send,
                                               std::string state_value) = 0;

  virtual void add_addt_command_to_queue(NextionComponentBase *component) = 0;

  virtual void add_to_get_queue(NextionComponentBase *component) = 0;

  virtual void set_component_background_color(const char *component, Color color) = 0;
  virtual void set_component_pressed_background_color(const char *component, Color color) = 0;
  virtual void set_component_font_color(const char *component, Color color) = 0;
  virtual void set_component_pressed_font_color(const char *component, Color color) = 0;
  virtual void set_component_font(const char *component, uint8_t font_id) = 0;

  virtual void show_component(const char *component) = 0;
  virtual void hide_component(const char *component) = 0;

  bool is_sleeping() { return this->is_sleeping_; }
  bool is_setup() { return this->is_setup_; }

 protected:
  void set_is_sleeping_(bool is_sleeping) { this->is_sleeping_ = is_sleeping; }

  bool is_setup_ = false;
  bool is_sleeping_ = false;

};  // namespace nextion

}  // namespace nextion
}  // namespace esphome
