#include "current_tilt_based_cover.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cfloat>

namespace esphome {
namespace current_tilt_based {

static const char *const TAG = "current_tilt_based.cover";

using namespace esphome::cover;

CoverTraits CurrentTiltBasedCover::get_traits() {
  auto traits = CoverTraits();
  traits.set_supports_position(true);
  if (this->tilt_duration_ > 0) {
    traits.set_supports_tilt(true);
  }
  traits.set_is_assumed_state(false);
  return traits;
}

void CurrentTiltBasedCover::control(const CoverCall &call) {
  if (call.get_stop()) {
    this->delayed_interlock_dir_ = COVER_OPERATION_IDLE;
    this->direction_idle_();
    return;
  }
  
  if ((this->delayed_interlock_dir_ == COVER_OPERATION_IDLE) && (!this->dir_change_dead_time_active_)) {
    
    // Handle position commands (existing logic)
    if (call.get_position().has_value()) {
      auto pos = *call.get_position();
      if (pos == this->position) {
        return;
      }
      
      auto requested_op = pos < this->position ? COVER_OPERATION_CLOSING : COVER_OPERATION_OPENING;
      
      if (this->current_operation != COVER_OPERATION_IDLE && this->current_operation != requested_op) {
        ESP_LOGD(TAG, "'%s' - Direction change requested while moving. Stopping only.", this->name_.c_str());
        this->direction_idle_();
        return;
      } 
      
      if (this->current_operation == COVER_OPERATION_IDLE) {
        ESP_LOGD(TAG, "'%s' - Starting movement from idle: pos=%.3f -> %.3f", 
                 this->name_.c_str(), this->position, pos);
        this->target_position_ = pos;
        this->start_direction_(requested_op);
      } else {
        ESP_LOGD(TAG, "'%s' - Updating target: %.3f -> %.3f", 
                 this->name_.c_str(), this->target_position_, pos);
        this->target_position_ = pos;
      }
    }
    
    // Handle tilt commands with stepping
    if (this->tilt_duration_ > 0 && call.get_tilt().has_value()) {
      auto requested_tilt = *call.get_tilt();
      
      if (requested_tilt == this->tilt) {
        return;  // Already at target
      }
      
      // TILT STEPPING LOGIC
      float target_tilt;
      bool is_step_command = false;
      
      // Detect if this is a "full close/open" command that should be stepped
      if ((requested_tilt == 0.0f && this->tilt > 0.1f) || 
          (requested_tilt == 1.0f && this->tilt < 0.9f)) {
        
        // This looks like a step command from Home Assistant UI
        is_step_command = true;
        
        if (requested_tilt == 0.0f) {
          // "Close tilt" button - step down
          target_tilt = clamp(this->tilt - this->tilt_step_size_, 0.0f, 1.0f);
          ESP_LOGD(TAG, "'%s' - Tilt step down: %.3f -> %.3f (step=%.3f)", 
                   this->name_.c_str(), this->tilt, target_tilt, this->tilt_step_size_);
        } else {
          // "Open tilt" button - step up  
          target_tilt = clamp(this->tilt + this->tilt_step_size_, 0.0f, 1.0f);
          ESP_LOGD(TAG, "'%s' - Tilt step up: %.3f -> %.3f (step=%.3f)", 
                   this->name_.c_str(), this->tilt, target_tilt, this->tilt_step_size_);
        }
      } else {
        // This is a specific tilt position command (from slider or ESPHome buttons)
        target_tilt = requested_tilt;
        ESP_LOGD(TAG, "'%s' - Tilt to specific position: %.3f -> %.3f", 
                 this->name_.c_str(), this->tilt, target_tilt);
      }
      
      // Check for direction change
      auto requested_op = target_tilt < this->tilt ? COVER_OPERATION_CLOSING : COVER_OPERATION_OPENING;
      
      if (this->current_operation != COVER_OPERATION_IDLE && this->current_operation != requested_op) {
        ESP_LOGD(TAG, "'%s' - Tilt direction change requested while moving. Stopping only.", this->name_.c_str());
        this->direction_idle_();
        return;
      }
      
      // Proceed with tilt movement only if idle or same direction
      if (this->current_operation == COVER_OPERATION_IDLE) {
        float pos_dur;
        if (requested_op == COVER_OPERATION_CLOSING) {
          pos_dur = this->close_duration_;
        } else {
          pos_dur = this->open_duration_;
        }
        
        // Safety checks
        if (pos_dur <= 0 || this->tilt_duration_ <= 0) {
          ESP_LOGE(TAG, "'%s' - Invalid duration values", this->name_.c_str());
          return;
        }
        
        // Calculate target position based on tilt change
        float tilt_delta = target_tilt - this->tilt;
        float position_delta = (tilt_delta * this->tilt_duration_) / pos_dur;
        float new_target = this->position + position_delta;
        
        this->target_position_ = clamp(new_target, 0.0F, 1.0F);
        
        ESP_LOGD(TAG, "'%s' - Tilt control: current_tilt=%.3f, target_tilt=%.3f, target_pos=%.3f%s", 
                 this->name_.c_str(), this->tilt, target_tilt, this->target_position_,
                 is_step_command ? " (stepped)" : "");
        
        this->start_direction_(requested_op);
        this->last_moving_dir_ = requested_op;
        this->last_tilt_call_was_step_ = is_step_command;
      }
    }
  }
}

void CurrentTiltBasedCover::setup() {
  auto restore = this->restore_state_();
  if (restore.has_value()) {
    restore->apply(this);
    
    // Validate restored state
    if (this->position < 0.0f || this->position > 1.0f ||
        this->tilt < 0.0f || this->tilt > 1.0f) {
      ESP_LOGW(TAG, "'%s' - Restored state invalid, using defaults", this->name_.c_str());
      this->position = 0.5f;
      this->tilt = 0.5f;
    }
    
    ESP_LOGI(TAG, "'%s' - Restored state: pos=%.3f, tilt=%.3f", 
             this->name_.c_str(), this->position, this->tilt);
  } else {
    this->position = 0.5f;
    this->tilt = 0.5f;
    ESP_LOGI(TAG, "'%s' - Using default state: pos=%.3f, tilt=%.3f", 
             this->name_.c_str(), this->position, this->tilt);
  }
  
  // Initialize state tracking
  this->last_known_good_position_ = this->position;
  this->last_known_good_tilt_ = this->tilt;
  this->state_is_valid_ = true;
}

void CurrentTiltBasedCover::loop() {
  // Prevent re-entry during timing updates
  if (this->timing_update_in_progress_) {
    return;
  }
  
  this->timing_update_in_progress_ = true;
  
  const uint32_t now = millis();
  
  // Validate state periodically
  static uint32_t last_validation = 0;
  if (now - last_validation > 5000) {  // Every 5 seconds
    this->validate_and_restore_state_();
    last_validation = now;
  }

  // Handle idle state with delayed operations
  if (this->current_operation == COVER_OPERATION_IDLE) {
    auto delayed_interlock_dir = this->delayed_interlock_dir_;
    switch (delayed_interlock_dir) {
      case COVER_OPERATION_OPENING:
        if (this->end_dir_close_time_ == 0 ||
            (now - this->end_dir_close_time_) >= this->interlock_wait_time_) {
          this->start_direction_(delayed_interlock_dir);
          this->timing_update_in_progress_ = false;
          return;
        }
        break;

      case COVER_OPERATION_CLOSING:
        if (this->end_dir_open_time_ == 0 ||
            (now - this->end_dir_open_time_) >= this->interlock_wait_time_) {
          this->start_direction_(delayed_interlock_dir);
          this->timing_update_in_progress_ = false;
          return;
        }
        break;
        
      default:
        this->timing_update_in_progress_ = false;
        return;
    }
    this->save_state_if_needed_();
    this->timing_update_in_progress_ = false;
    return;
  }

  // Handle direction change dead time
  if (this->dir_change_dead_time_active_) {
    if ((now - this->start_dir_time_) >= this->dir_change_dead_time_) {
      this->dir_change_dead_time_active_ = false;
      
      // If we have a delayed operation waiting, start it now
      if (this->delayed_interlock_dir_ != COVER_OPERATION_IDLE) {
        ESP_LOGD(TAG, "'%s' - Direction change dead time finished, starting delayed operation: %d", 
                this->name_.c_str(), this->delayed_interlock_dir_);
        
        auto delayed_op = this->delayed_interlock_dir_;
        this->delayed_interlock_dir_ = COVER_OPERATION_IDLE;  // Clear it first
        this->start_direction_(delayed_op);
      } else {
        // Reset timing references after dead time
        this->start_dir_time_ = now;
        this->last_recompute_time_ = now;
      }
    } else {
      this->timing_update_in_progress_ = false;
      return;
    }
  }

  // Check for max duration timeout (with safety margin)
  if (this->max_duration_ != UINT32_MAX &&
      (now - this->start_dir_time_) >= this->max_duration_) {
    ESP_LOGD(TAG, "'%s' - Max duration reached. Stopping cover.", this->name_.c_str());
    this->direction_idle_();
    this->timing_update_in_progress_ = false;
    return;
  }

  // Operation-specific logic
  if (this->current_operation == COVER_OPERATION_OPENING) {
    // Improved malfunction detection with grace period and threshold
    if (this->malfunction_detection_ && 
        !this->is_malfunction_grace_period_active_() &&  // Wait for grace period
        this->is_closing_() && 
        this->close_sensor_->get_state() > this->malfunction_current_threshold_) {  // Above noise threshold
      
      this->direction_idle_();
      this->malfunction_trigger_->trigger();
      ESP_LOGI(TAG, "'%s' - Malfunction detected during opening. Close current: %.3fA (threshold: %.3fA)", 
               this->name_.c_str(), this->close_sensor_->get_state(), this->malfunction_current_threshold_);
               
    } else if (this->is_opening_blocked_()) {
      ESP_LOGD(TAG, "'%s' - Obstacle detected during opening.", this->name_.c_str());
      this->direction_idle_();
      if (this->obstacle_rollback_ != 0) {
        this->set_timeout("rollback", 300, [this]() {
          ESP_LOGD(TAG, "'%s' - Rollback.", this->name_.c_str());
          this->target_position_ = clamp(this->position - this->obstacle_rollback_, 0.0F, 1.0F);
          this->start_direction_(COVER_OPERATION_CLOSING);
        });
      }
    } else if (this->is_initial_delay_finished_() && !this->is_opening_()) {
      auto dur = (now - this->start_dir_time_) / 1e3f;
      ESP_LOGD(TAG, "'%s' - Open position reached. Took %.1fs.", this->name_.c_str(), dur);
      this->direction_idle_(COVER_OPEN);
    }
    
  } else if (this->current_operation == COVER_OPERATION_CLOSING) {
    // Improved malfunction detection for closing
    if (this->malfunction_detection_ && 
        !this->is_malfunction_grace_period_active_() &&  // Wait for grace period
        this->is_opening_() && 
        this->open_sensor_->get_state() > this->malfunction_current_threshold_) {  // Above noise threshold
        
      this->direction_idle_();
      this->malfunction_trigger_->trigger();
      ESP_LOGI(TAG, "'%s' - Malfunction detected during closing. Open current: %.3fA (threshold: %.3fA)", 
               this->name_.c_str(), this->open_sensor_->get_state(), this->malfunction_current_threshold_);
               
    } else if (this->is_closing_blocked_()) {
      ESP_LOGD(TAG, "'%s' - Obstacle detected during closing.", this->name_.c_str());
      this->direction_idle_();
      if (this->obstacle_rollback_ != 0) {
        this->set_timeout("rollback", 300, [this]() {
          ESP_LOGD(TAG, "'%s' - Rollback.", this->name_.c_str());
          this->target_position_ = clamp(this->position + this->obstacle_rollback_, 0.0F, 1.0F);
          this->start_direction_(COVER_OPERATION_OPENING);
        });
      }
    } else if (this->is_initial_delay_finished_() && !this->is_closing_()) {
      auto dur = (now - this->start_dir_time_) / 1e3f;
      ESP_LOGD(TAG, "'%s' - Close position reached. Took %.1fs.", this->name_.c_str(), dur);
      this->direction_idle_(COVER_CLOSED);
    }
  }

  // Recompute position (with timing protection)
  this->recompute_position_();

  // Check if target reached
  if (this->current_operation != COVER_OPERATION_IDLE && this->is_at_target_()) {
    this->direction_idle_();
  }

  // Publish state periodically (with timing protection)
  if (this->current_operation != COVER_OPERATION_IDLE &&
      (now - this->last_publish_time_) >= 1000) {
    this->last_moving_dir_ = this->current_operation;
    this->publish_state(false);
    this->last_publish_time_ = now;
  }
  
  this->timing_update_in_progress_ = false;
}

void CurrentTiltBasedCover::direction_idle_(float new_position) {
  ESP_LOGD(TAG, "'%s' - Stopping operation. Current pos=%.3f, tilt=%.3f", 
           this->name_.c_str(), this->position, this->tilt);
  
  // CRITICAL FIX: Only override position if we have a definitive end state
  if (new_position != FLT_MAX) {
    // This is a definitive end position (COVER_OPEN or COVER_CLOSED)
    this->position = new_position;
    this->tilt = new_position;
    ESP_LOGD(TAG, "'%s' - Set definitive position: %.3f", this->name_.c_str(), new_position);
  } else {
    // This is a STOP command - keep the calculated position!
    // Make sure we have the most up-to-date calculated position
    this->recompute_position_();
    
    // Validate the calculated position is reasonable
    if (this->position < 0.0f || this->position > 1.0f) {
      ESP_LOGW(TAG, "'%s' - Invalid calculated position %.3f, clamping", 
               this->name_.c_str(), this->position);
      this->position = clamp(this->position, 0.0f, 1.0f);
    }
    
    if (this->tilt < 0.0f || this->tilt > 1.0f) {
      ESP_LOGW(TAG, "'%s' - Invalid calculated tilt %.3f, clamping", 
               this->name_.c_str(), this->tilt);
      this->tilt = clamp(this->tilt, 0.0f, 1.0f);
    }
    
    ESP_LOGD(TAG, "'%s' - Keeping calculated position: pos=%.3f, tilt=%.3f", 
             this->name_.c_str(), this->position, this->tilt);
  }
  
  // Stop the operation
  this->start_direction_(COVER_OPERATION_IDLE);
  
  // Save the state immediately when stopping - ESPHome handles persistence automatically
  this->last_known_good_position_ = this->position;
  this->last_known_good_tilt_ = this->tilt;
  this->last_state_save_time_ = millis();
  
  // Publish the final state (ESPHome automatically handles persistence)
  this->publish_state();
}

void CurrentTiltBasedCover::dump_config() {
  LOG_COVER("", "Current Tilt Based Cover", this);
  LOG_SENSOR("  ", "Open Sensor", this->open_sensor_);
  ESP_LOGCONFIG(TAG, "  Open moving current threshold: %.3fA", this->open_moving_current_threshold_);
  if (this->open_obstacle_current_threshold_ != FLT_MAX) {
    ESP_LOGCONFIG(TAG, "  Open obstacle current threshold: %.3fA", this->open_obstacle_current_threshold_);
  }
  ESP_LOGCONFIG(TAG, "  Open Duration: %.1fs", this->open_duration_ / 1e3f);
  
  LOG_SENSOR("  ", "Close Sensor", this->close_sensor_);
  ESP_LOGCONFIG(TAG, "  Close moving current threshold: %.3fA", this->close_moving_current_threshold_);
  if (this->close_obstacle_current_threshold_ != FLT_MAX) {
    ESP_LOGCONFIG(TAG, "  Close obstacle current threshold: %.3fA", this->close_obstacle_current_threshold_);
  }
  ESP_LOGCONFIG(TAG, "  Close Duration: %.1fs", this->close_duration_ / 1e3f);
  ESP_LOGCONFIG(TAG, "  Tilt Duration: %.1fs", this->tilt_duration_ / 1e3f);
  ESP_LOGCONFIG(TAG, "  Obstacle Rollback: %.1f%%", this->obstacle_rollback_ * 100);
  
  if (this->max_duration_ != UINT32_MAX) {
    ESP_LOGCONFIG(TAG, "  Maximum duration: %.1fs", this->max_duration_ / 1e3f);
  }
  ESP_LOGCONFIG(TAG, "  Start sensing delay: %.1fs", this->start_sensing_delay_ / 1e3f);
  ESP_LOGCONFIG(TAG, "  Malfunction detection: %s", YESNO(this->malfunction_detection_));
  ESP_LOGCONFIG(TAG, "  Malfunction grace period: %.1fs", this->malfunction_grace_period_ / 1e3f);
  ESP_LOGCONFIG(TAG, "  Malfunction current threshold: %.3fA", this->malfunction_current_threshold_);
}

float CurrentTiltBasedCover::get_setup_priority() const { return setup_priority::DATA; }
void CurrentTiltBasedCover::stop_prev_trigger_() {
  if (this->prev_command_trigger_ != nullptr) {
    this->prev_command_trigger_->stop_action();
    this->prev_command_trigger_ = nullptr;
  }
}

bool CurrentTiltBasedCover::is_opening_() const {
  return this->open_sensor_->get_state() > this->open_moving_current_threshold_;
}

bool CurrentTiltBasedCover::is_opening_blocked_() const {
  if (this->open_obstacle_current_threshold_ == FLT_MAX) {
    return false;
  }
  return this->open_sensor_->get_state() > this->open_obstacle_current_threshold_;
}

bool CurrentTiltBasedCover::is_closing_() const {
  return this->close_sensor_->get_state() > this->close_moving_current_threshold_;
}

bool CurrentTiltBasedCover::is_closing_blocked_() const {
  if (this->close_obstacle_current_threshold_ == FLT_MAX) {
    return false;
  }
  return this->close_sensor_->get_state() > this->close_obstacle_current_threshold_;
}

bool CurrentTiltBasedCover::is_initial_delay_finished_() const {
  return millis() - this->start_dir_time_ > this->start_sensing_delay_;
}

bool CurrentTiltBasedCover::is_at_target_() const {
  switch (this->current_operation) {
    case COVER_OPERATION_OPENING:
      if (this->target_position_ == COVER_OPEN) {
        if (!this->is_initial_delay_finished_())  // During initial delay, state is assumed
          return false;
        return !this->is_opening_();
      }
      return this->position >= this->target_position_;
    case COVER_OPERATION_CLOSING:
      if (this->target_position_ == COVER_CLOSED) {
        if (!this->is_initial_delay_finished_())  // During initial delay, state is assumed
          return false;
        return !this->is_closing_();
      }
      return this->position <= this->target_position_;
    case COVER_OPERATION_IDLE:
    default:
      return true;
  }
}

void CurrentTiltBasedCover::start_direction_(CoverOperation dir) {
  if (dir == this->current_operation)
    return;

  const uint32_t now = millis();
  
  // Recompute position before changing direction to maintain consistency
  this->recompute_position_();
  
  Trigger<> *trig = nullptr;
  
  switch (dir) {
    case COVER_OPERATION_IDLE:
      trig = this->stop_trigger_;
      this->delayed_interlock_dir_ = dir;
      // Record end times atomically
      if (this->current_operation == COVER_OPERATION_OPENING) {
        this->end_dir_open_time_ = now;
      } else if (this->current_operation == COVER_OPERATION_CLOSING) {
        this->end_dir_close_time_ = now;
      }
      break;

    case COVER_OPERATION_OPENING:
      // Wraparound-safe interlock check
      if (this->end_dir_close_time_ == 0 ||
          (now - this->end_dir_close_time_) >= this->interlock_wait_time_) {
        trig = this->open_trigger_;
        this->delayed_interlock_dir_ = COVER_OPERATION_IDLE;  // Clear delayed operation
      } else {
        this->delayed_interlock_dir_ = dir;
        return;
      }
      break;

    case COVER_OPERATION_CLOSING:
      // Wraparound-safe interlock check
      if (this->end_dir_open_time_ == 0 ||
          (now - this->end_dir_open_time_) >= this->interlock_wait_time_) {
        trig = this->close_trigger_;
        this->delayed_interlock_dir_ = COVER_OPERATION_IDLE;  // Clear delayed operation
      } else {
        this->delayed_interlock_dir_ = dir;
        return;
      }
      break;
      
    default:
      this->delayed_interlock_dir_ = dir;
      return;
  }

  // Update state atomically
  this->current_operation = dir;
  
  // Execute the trigger
  this->stop_prev_trigger_();
  if (trig != nullptr) {
    trig->trigger();
    this->prev_command_trigger_ = trig;
  }

  // Set timing references atomically
  this->start_dir_time_ = now;
  this->last_recompute_time_ = now;
}

void CurrentTiltBasedCover::recompute_position_() {
  if (this->current_operation == COVER_OPERATION_IDLE)
    return;

  const uint32_t now = millis();
  
  // Prevent computation if timing is inconsistent
  if (now < this->last_recompute_time_) {
    ESP_LOGW(TAG, "'%s' - Timing rollback detected, skipping position update", this->name_.c_str());
    this->last_recompute_time_ = now;
    return;
  }
  
  const uint32_t time_diff = now - this->last_recompute_time_;
  
  // Skip tiny time differences to avoid jitter
  if (time_diff < 10) {  // Less than 10ms
    return;
  }
  
  // Detect and handle large time jumps
  if (time_diff > 5000) {  // More than 5 seconds
    ESP_LOGW(TAG, "'%s' - Large time jump detected (%ums), skipping update", 
             this->name_.c_str(), time_diff);
    this->last_recompute_time_ = now;
    return;
  }

  float dir;
  float pos_dur;
  float tilt_dur = this->tilt_duration_;
  
  switch (this->current_operation) {
    case COVER_OPERATION_OPENING:
      dir = 1.0F;
      pos_dur = this->open_duration_;
      break;
    case COVER_OPERATION_CLOSING:
      dir = -1.0F;
      pos_dur = this->close_duration_;
      break;
    default:
      return;
  }

  // Safety checks
  if (pos_dur <= 0 || tilt_dur <= 0) {
    ESP_LOGE(TAG, "'%s' - Invalid duration values", this->name_.c_str());
    return;
  }

  // Store previous values for validation
  float prev_position = this->position;
  float prev_tilt = this->tilt;

  // Update positions
  this->position += dir * time_diff / pos_dur;
  this->tilt += dir * time_diff / tilt_dur;

  // Clamp to valid ranges
  this->position = clamp(this->position, 0.0f, 1.0f);
  this->tilt = clamp(this->tilt, 0.0f, 1.0f);

  // Validate the change is reasonable (not jumping too much)
  float pos_change = abs(this->position - prev_position);
  float tilt_change = abs(this->tilt - prev_tilt);
  
  if (pos_change > 0.1f || tilt_change > 0.1f) {  // More than 10% change in one update
    ESP_LOGW(TAG, "'%s' - Large position jump detected: pos_change=%.3f, tilt_change=%.3f", 
             this->name_.c_str(), pos_change, tilt_change);
    // Don't apply the change if it's too large
    this->position = prev_position;
    this->tilt = prev_tilt;
    this->last_recompute_time_ = now;
    return;
  }

  // Update timing reference
  this->last_recompute_time_ = now;
  
  // Mark state as valid if position calculation is reliable
  this->state_is_valid_ = this->is_position_calculation_reliable_();
}

void CurrentTiltBasedCover::validate_timing_state_() {
  const uint32_t now = millis();
  bool state_corrupted = false;
  
  // Check for timing inconsistencies
  if (this->last_recompute_time_ > now) {
    ESP_LOGW(TAG, "'%s' - last_recompute_time_ is in the future, resetting", this->name_.c_str());
    state_corrupted = true;
  }
  
  if (this->start_dir_time_ > now) {
    ESP_LOGW(TAG, "'%s' - start_dir_time_ is in the future, resetting", this->name_.c_str());
    state_corrupted = true;
  }
  
  if (this->end_dir_open_time_ > now) {
    ESP_LOGW(TAG, "'%s' - end_dir_open_time_ is in the future, resetting", this->name_.c_str());
    state_corrupted = true;
  }
  
  if (this->end_dir_close_time_ > now) {
    ESP_LOGW(TAG, "'%s' - end_dir_close_time_ is in the future, resetting", this->name_.c_str());
    state_corrupted = true;
  }
  
  // Check for unreasonable time gaps (more than 1 hour indicates overflow or corruption)
  if (this->current_operation != COVER_OPERATION_IDLE) {
    if (now - this->start_dir_time_ > 3600000) {  // 1 hour
      ESP_LOGW(TAG, "'%s' - Operation running for over 1 hour, likely timing corruption", this->name_.c_str());
      state_corrupted = true;
    }
  }
  
  if (state_corrupted) {
    this->reset_timing_state_();
  }
}

void CurrentTiltBasedCover::reset_timing_state_() {
  const uint32_t now = millis();
  
  ESP_LOGW(TAG, "'%s' - Resetting timing state due to corruption", this->name_.c_str());
  
  this->last_recompute_time_ = now;
  this->start_dir_time_ = now;
  this->last_publish_time_ = now;
  
  // Reset interlock times to allow immediate operation
  this->end_dir_open_time_ = 0;
  this->end_dir_close_time_ = 0;
  
  // Reset timing flags
  this->dir_change_dead_time_active_ = false;
  this->timing_update_in_progress_ = false;
}

bool CurrentTiltBasedCover::is_malfunction_grace_period_active_() const {
  return millis() - this->start_dir_time_ < this->malfunction_grace_period_;
}

void CurrentTiltBasedCover::save_state_if_needed_() {
  const uint32_t now = millis();
  
  // Save state every 2 seconds during operation, immediately when stopping
  bool should_save = (this->current_operation == COVER_OPERATION_IDLE && 
                     now - this->last_state_save_time_ > 2000) ||
                    (this->current_operation != COVER_OPERATION_IDLE && 
                     now - this->last_state_save_time_ > 2000);
                     
  if (should_save && this->state_is_valid_) {
    // Only save if we have reasonable values
    if (this->position >= 0.0f && this->position <= 1.0f &&
        this->tilt >= 0.0f && this->tilt <= 1.0f) {
      
      this->last_known_good_position_ = this->position;
      this->last_known_good_tilt_ = this->tilt;
      this->last_state_save_time_ = now;
      
      // ESPHome handles state persistence automatically when we publish
      this->publish_state(false);  // Don't force update, just ensure persistence
      
      ESP_LOGD(TAG, "'%s' - State saved: pos=%.3f, tilt=%.3f", 
               this->name_.c_str(), this->position, this->tilt);
    }
  }
}

void CurrentTiltBasedCover::validate_and_restore_state_() {
  // Check if current state is reasonable
  if (this->position < 0.0f || this->position > 1.0f ||
      this->tilt < 0.0f || this->tilt > 1.0f) {
    
    ESP_LOGW(TAG, "'%s' - Invalid state detected: pos=%.3f, tilt=%.3f. Restoring last known good state.", 
             this->name_.c_str(), this->position, this->tilt);
             
    this->position = this->last_known_good_position_;
    this->tilt = this->last_known_good_tilt_;
    this->state_is_valid_ = true;
    this->publish_state();
  }
}

bool CurrentTiltBasedCover::is_position_calculation_reliable_() const {
  // Position calculation is reliable if:
  // 1. We've been moving for at least the sensing delay
  // 2. The timing is consistent
  // 3. We're not in a malfunction state
  
  const uint32_t now = millis();
  
  if (this->current_operation == COVER_OPERATION_IDLE) {
    return true;  // Always reliable when idle
  }
  
  if (now - this->start_dir_time_ < this->start_sensing_delay_) {
    return false;  // Too early to trust position
  }
  
  if (now - this->last_recompute_time_ > 5000) {
    return false;  // Timing is inconsistent
  }
  
  return true;
}



}  // namespace current_tilt_based
}  // namespace esphome