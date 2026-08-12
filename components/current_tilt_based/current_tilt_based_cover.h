#pragma once

#include "esphome/components/cover/cover.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include <cfloat>

namespace esphome {
namespace current_tilt_based {

class CurrentTiltBasedCover : public cover::Cover, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void set_tilt_step_size(float step_size) { this->tilt_step_size_ = step_size; }
  float get_setup_priority() const override;

  Trigger<> *get_stop_trigger() const { return this->stop_trigger_; }

  Trigger<> *get_open_trigger() const { return this->open_trigger_; }
  void set_open_sensor(sensor::Sensor *open_sensor) { this->open_sensor_ = open_sensor; }
  void set_open_moving_current_threshold(float open_moving_current_threshold) {
    this->open_moving_current_threshold_ = open_moving_current_threshold;
  }
  void set_open_obstacle_current_threshold(float open_obstacle_current_threshold) {
    this->open_obstacle_current_threshold_ = open_obstacle_current_threshold;
  }
  void set_open_duration(uint32_t open_duration) { this->open_duration_ = open_duration; }

  Trigger<> *get_close_trigger() const { return this->close_trigger_; }
  void set_close_sensor(sensor::Sensor *close_sensor) { this->close_sensor_ = close_sensor; }
  void set_close_moving_current_threshold(float close_moving_current_threshold) {
    this->close_moving_current_threshold_ = close_moving_current_threshold;
  }
  void set_tilt_duration(uint32_t tilt_duration) { this->tilt_duration_ = tilt_duration; }
  void set_interlock_wait_time(uint32_t interlock_wait_time) { this->interlock_wait_time_ = interlock_wait_time; }
  void set_dir_change_dead_time(uint32_t dir_change_dead_time) { this->dir_change_dead_time_ = dir_change_dead_time; }
  

  void set_close_obstacle_current_threshold(float close_obstacle_current_threshold) {
    this->close_obstacle_current_threshold_ = close_obstacle_current_threshold;
  }
  void set_close_duration(uint32_t close_duration) { this->close_duration_ = close_duration; }

  void set_max_duration(uint32_t max_duration) { this->max_duration_ = max_duration; }
  void set_obstacle_rollback(float obstacle_rollback) { this->obstacle_rollback_ = obstacle_rollback; }

  void set_malfunction_detection(bool malfunction_detection) { this->malfunction_detection_ = malfunction_detection; }
  void set_start_sensing_delay(uint32_t start_sensing_delay) { this->start_sensing_delay_ = start_sensing_delay; }

  Trigger<> *get_malfunction_trigger() const { return this->malfunction_trigger_; }

  cover::CoverTraits get_traits() override;

 protected:
  void control(const cover::CoverCall &call) override;
  void stop_prev_trigger_();

  bool is_at_target_() const;
  bool is_opening_() const;
  bool is_opening_blocked_() const;
  bool is_closing_() const;
  bool is_closing_blocked_() const;
  bool is_initial_delay_finished_() const;

  void direction_idle_(float new_position = FLT_MAX);
  void start_direction_(cover::CoverOperation dir);

  void recompute_position_();

  Trigger<> *stop_trigger_{new Trigger<>()};

  sensor::Sensor *open_sensor_{nullptr};
  Trigger<> *open_trigger_{new Trigger<>()};
  float open_moving_current_threshold_;
  float open_obstacle_current_threshold_{FLT_MAX};
  uint32_t open_duration_;

  sensor::Sensor *close_sensor_{nullptr};
  Trigger<> *close_trigger_{new Trigger<>()};
  float close_moving_current_threshold_;
  float close_obstacle_current_threshold_{FLT_MAX};
  uint32_t close_duration_;

  uint32_t tilt_duration_;
  uint32_t interlock_wait_time_;
  uint32_t dir_change_dead_time_;
  bool dir_change_dead_time_active_{false};

  cover::CoverOperation delayed_interlock_dir_{cover::COVER_OPERATION_IDLE};
  cover::CoverOperation last_moving_dir_{cover::COVER_OPERATION_IDLE};

  uint32_t max_duration_{UINT32_MAX};
  bool malfunction_detection_{true};
  Trigger<> *malfunction_trigger_{new Trigger<>()};
  uint32_t start_sensing_delay_;
  float obstacle_rollback_;


  Trigger<> *prev_command_trigger_{nullptr};
  uint32_t last_recompute_time_{0};
  uint32_t start_dir_time_{0};
  uint32_t end_dir_open_time_{0};
  uint32_t end_dir_close_time_{0};
  uint32_t last_publish_time_{0};
  float target_position_{0};
  float target_tilt_{0};

 private:
  void validate_timing_state_();
  void reset_timing_state_();

  uint32_t malfunction_grace_period_{2000};  // 2 seconds grace period
  float malfunction_current_threshold_{0.1};  // Minimum current to consider as malfunction (100mA)
  
  // mutex-like flag to prevent timing conflicts
  bool timing_update_in_progress_{false};

  bool is_malfunction_grace_period_active_() const;
  void set_malfunction_grace_period(uint32_t grace_period) { this->malfunction_grace_period_ = grace_period; }
  void set_malfunction_current_threshold(float threshold) { this->malfunction_current_threshold_ = threshold; }

  bool state_is_valid_{true};
  uint32_t last_state_save_time_{0};
  float last_known_good_position_{0.5f};
  float last_known_good_tilt_{0.5f};

  void save_state_if_needed_();
  void validate_and_restore_state_();
  bool is_position_calculation_reliable_() const;

  float tilt_step_size_{0.1f};  // 10% tilt steps by default
  bool last_tilt_call_was_step_{false};  // Track if last call was a step
  
};

}  // namespace current_tilt_based
}  // namespace esphome