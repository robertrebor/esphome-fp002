#include "fp002.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome {
namespace fp002 {

static const char *const TAG = "fp002";

void Fp002Component::setup() {
  // The module takes ~250ms to boot and then sends an unsolicited NOTE
  // (NID_READY) once it is able to accept commands - nothing to send here.
  ESP_LOGCONFIG(TAG, "Setting up FP002, waiting for module ready notification...");
}

void Fp002Component::loop() {
  while (this->available()) {
    uint8_t c;
    if (!this->read_byte(&c))
      break;

    switch (this->state_) {
      case ParseState::WAIT_SYNC1:
        if (c == FP002_SYNC1)
          this->state_ = ParseState::WAIT_SYNC2;
        break;

      case ParseState::WAIT_SYNC2:
        if (c == FP002_SYNC2) {
          this->state_ = ParseState::WAIT_MSGID;
        } else if (c != FP002_SYNC1) {
          this->state_ = ParseState::WAIT_SYNC1;
        }
        break;

      case ParseState::WAIT_MSGID:
        this->frame_msg_id_ = c;
        this->state_ = ParseState::WAIT_SIZE_H;
        break;

      case ParseState::WAIT_SIZE_H:
        this->data_size_ = static_cast<uint16_t>(c) << 8;
        this->state_ = ParseState::WAIT_SIZE_L;
        break;

      case ParseState::WAIT_SIZE_L:
        this->data_size_ |= c;
        this->data_buf_.clear();
        this->data_buf_.reserve(this->data_size_);
        this->state_ = this->data_size_ > 0 ? ParseState::WAIT_DATA : ParseState::WAIT_CHECKSUM;
        break;

      case ParseState::WAIT_DATA:
        this->data_buf_.push_back(c);
        if (this->data_buf_.size() >= this->data_size_)
          this->state_ = ParseState::WAIT_CHECKSUM;
        break;

      case ParseState::WAIT_CHECKSUM: {
        // Checksum = XOR of everything after the sync word (msg id, size hi/lo, data)
        uint8_t expected = this->frame_msg_id_;
        expected ^= static_cast<uint8_t>(this->data_size_ >> 8);
        expected ^= static_cast<uint8_t>(this->data_size_ & 0xFF);
        for (uint8_t b : this->data_buf_)
          expected ^= b;

        if (expected == c) {
          this->handle_frame_();
        } else {
          ESP_LOGW(TAG, "Checksum mismatch (got 0x%02X, expected 0x%02X) - dropping frame", c, expected);
        }
        this->state_ = ParseState::WAIT_SYNC1;
        break;
      }
    }
  }

  // Watchdog: if a command was sent but never got a REPLY, don't get stuck forever.
  if (this->pending_cmd_ != Fp002PendingCmd::NONE &&
      millis() - this->command_started_ms_ > this->command_timeout_ms_) {
    uint8_t timed_out_mid = this->pending_reply_mid_;
    ESP_LOGW(TAG, "Timed out waiting for a reply to command 0x%02X, giving up on it", timed_out_mid);
    this->pending_cmd_ = Fp002PendingCmd::NONE;
    this->last_command_done_ms_ = millis();
    // An enroll (single-frame or one step of an interactive enroll) that never got
    // a reply would otherwise leave interactive_enroll_active_ stuck true forever,
    // blocking auto_verify indefinitely - make sure it's reported as a failure.
    if (timed_out_mid == MID_ENROLL || timed_out_mid == MID_ENROLL_SINGLE) {
      this->interactive_enroll_active_ = false;
      this->start_led_blinks_(FP002_LED_RED, 2);
      this->enroll_failed_callback_.call(MR_FAILED4_TIMEOUT);
    }
  }

  this->update_led_blinks_();

  // Interactive enroll step sequencer: once any LED feedback for the previous
  // direction has finished playing and nothing else is in flight, send the next
  // direction. (The first direction is sent directly from enroll_user().)
  if (this->interactive_enroll_active_ && !this->led_blink_active_ &&
      this->pending_cmd_ == Fp002PendingCmd::NONE) {
    this->send_enroll_step_();
  }

  // Auto-verify loop: as soon as the module is ready and idle (no enroll/delete/list
  // request in flight), (re)start listening for a face/palm.
  if (this->auto_verify_ && this->ready_ && this->pending_cmd_ == Fp002PendingCmd::NONE &&
      !this->interactive_enroll_active_ && millis() - this->last_command_done_ms_ > this->cooldown_ms_) {
    this->start_verify();
  }
}

void Fp002Component::dump_config() {
  ESP_LOGCONFIG(TAG, "FP002 Facial/Palm Vein Recognition Module:");
  ESP_LOGCONFIG(TAG, "  Auto verify: %s", YESNO(this->auto_verify_));
  ESP_LOGCONFIG(TAG, "  Verify timeout: %us", this->verify_timeout_);
  ESP_LOGCONFIG(TAG, "  Enroll timeout: %us", this->enroll_timeout_);
  ESP_LOGCONFIG(TAG, "  Cooldown between attempts: %ums", this->cooldown_ms_);
  this->check_uart_settings(115200);
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Face Recognized", this->face_recognized_binary_sensor_);
#endif
#ifdef USE_TEXT_SENSOR
  LOG_TEXT_SENSOR("  ", "Recognized User Name", this->recognized_user_name_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Registered User Ids", this->registered_user_ids_text_sensor_);
#endif
#ifdef USE_SENSOR
  LOG_SENSOR("  ", "Recognized User Id", this->recognized_user_id_sensor_);
  LOG_SENSOR("  ", "Registered User Count", this->registered_user_count_sensor_);
#endif
}

void Fp002Component::append_name_(std::vector<uint8_t> &out, const std::string &name) {
  char buf[32];
  memset(buf, 0, sizeof(buf));
  strncpy(buf, name.c_str(), sizeof(buf) - 1);
  out.insert(out.end(), buf, buf + sizeof(buf));
}

void Fp002Component::write_frame_(uint8_t mid, const std::vector<uint8_t> &data) {
  uint16_t size = data.size();
  uint8_t checksum = mid;
  checksum ^= static_cast<uint8_t>(size >> 8);
  checksum ^= static_cast<uint8_t>(size & 0xFF);
  for (uint8_t b : data)
    checksum ^= b;

  this->write_byte(FP002_SYNC1);
  this->write_byte(FP002_SYNC2);
  this->write_byte(mid);
  this->write_byte(static_cast<uint8_t>(size >> 8));
  this->write_byte(static_cast<uint8_t>(size & 0xFF));
  if (!data.empty())
    this->write_array(data.data(), data.size());
  this->write_byte(checksum);
}

bool Fp002Component::send_command_(uint8_t mid, const std::vector<uint8_t> &data, Fp002PendingCmd kind,
                                    uint32_t timeout_ms) {
  if (this->pending_cmd_ != Fp002PendingCmd::NONE) {
    ESP_LOGW(TAG, "Module is busy (command 0x%02X in flight), ignoring new command 0x%02X", this->pending_reply_mid_,
              mid);
    return false;
  }
  if (!this->ready_ && mid != MID_RESET) {
    ESP_LOGW(TAG, "Module is not ready yet, ignoring command 0x%02X", mid);
    return false;
  }

  this->write_frame_(mid, data);

  this->pending_cmd_ = kind;
  this->pending_reply_mid_ = mid;
  this->command_started_ms_ = millis();
  this->command_timeout_ms_ = timeout_ms;
  return true;
}

void Fp002Component::send_led_(uint8_t color, bool on) {
  // Module's switch bit is inverted versus what you'd expect: 0x00 = on, 0x01 = off.
  std::vector<uint8_t> data = {color, static_cast<uint8_t>(on ? 0x00 : 0x01)};
  this->write_frame_(MID_LED_CONTROL, data);
}

void Fp002Component::start_led_blinks_(uint8_t color, uint8_t count) {
  if (count == 0)
    return;
  this->led_blink_color_ = color;
  this->led_blinks_target_ = count;
  this->led_blinks_done_ = 0;
  this->led_blink_active_ = true;
  this->led_on_now_ = true;
  this->send_led_(color, true);
  this->led_next_toggle_ms_ = millis() + LED_BLINK_ON_MS;
}

void Fp002Component::update_led_blinks_() {
  if (!this->led_blink_active_)
    return;
  if (millis() < this->led_next_toggle_ms_)
    return;

  if (this->led_on_now_) {
    this->send_led_(this->led_blink_color_, false);
    this->led_on_now_ = false;
    this->led_blinks_done_++;
    if (this->led_blinks_done_ >= this->led_blinks_target_) {
      this->led_blink_active_ = false;
      return;
    }
    this->led_next_toggle_ms_ = millis() + LED_BLINK_OFF_MS;
  } else {
    this->send_led_(this->led_blink_color_, true);
    this->led_on_now_ = true;
    this->led_next_toggle_ms_ = millis() + LED_BLINK_ON_MS;
  }
}

void Fp002Component::start_verify() {
  std::vector<uint8_t> data = {0x00, this->verify_timeout_};  // pd_rightaway (deprecated), timeout
  ESP_LOGV(TAG, "Sending MID_VERIFY (timeout=%us)", this->verify_timeout_);
  this->send_command_(MID_VERIFY, data, Fp002PendingCmd::VERIFY,
                       (static_cast<uint32_t>(this->verify_timeout_) + 5) * 1000UL);
}

void Fp002Component::enroll_user(const std::string &user_name, bool admin, bool palm_vein, bool quick,
                                  uint8_t timeout) {
  uint8_t use_timeout = timeout > 0 ? timeout : this->enroll_timeout_;
  this->pending_enroll_name_ = user_name;
  this->pending_enroll_admin_ = admin;

  // Palm vein has no interactive mode in the manual - it's always single-frame.
  // `quick` opts a face enroll into single-frame too.
  if (palm_vein || quick) {
    std::vector<uint8_t> data;
    data.push_back(admin ? 1 : 0);
    append_name_(data, user_name);
    data.push_back(palm_vein ? 0xFD : 0x00);
    data.push_back(use_timeout);

    ESP_LOGI(TAG, "Enrolling new %s user '%s' single-frame (admin=%s, timeout=%us)",
              palm_vein ? "palm-vein" : "face", user_name.c_str(), admin ? "yes" : "no", use_timeout);

    this->send_command_(MID_ENROLL_SINGLE, data, Fp002PendingCmd::ENROLL,
                         (static_cast<uint32_t>(use_timeout) + 5) * 1000UL);
    return;
  }

  // Interactive 5-direction enroll (the new default for face users): one
  // MID_ENROLL command per direction, each waiting for its own REPLY, with a
  // green LED blink after each captured direction and 3 blinks on completion.
  ESP_LOGI(TAG, "Enrolling new face user '%s' interactively (admin=%s, %us/direction)", user_name.c_str(),
            admin ? "yes" : "no", use_timeout);
  this->interactive_enroll_active_ = true;
  this->enroll_direction_index_ = 0;
  this->enroll_step_timeout_s_ = use_timeout;
  this->send_enroll_step_();
}

void Fp002Component::send_enroll_step_() {
  static const uint8_t DIRECTIONS[NUM_ENROLL_DIRECTIONS] = {
      FACE_DIRECTION_MIDDLE, FACE_DIRECTION_RIGHT, FACE_DIRECTION_LEFT, FACE_DIRECTION_DOWN, FACE_DIRECTION_UP,
  };
  static const char *const DIRECTION_NAMES[NUM_ENROLL_DIRECTIONS] = {"center", "right", "left", "down", "up"};

  if (this->enroll_direction_index_ >= NUM_ENROLL_DIRECTIONS) {
    // Shouldn't happen (the 5th successful REPLY carries a real user id and
    // clears interactive_enroll_active_), but don't loop forever if it does.
    ESP_LOGW(TAG, "Interactive enroll ran past 5 directions without finishing, aborting");
    this->interactive_enroll_active_ = false;
    this->enroll_failed_callback_.call(MR_FAILED4_TIMEOUT);
    return;
  }

  uint8_t direction = DIRECTIONS[this->enroll_direction_index_];
  std::vector<uint8_t> data;
  data.push_back(this->pending_enroll_admin_ ? 1 : 0);
  append_name_(data, this->pending_enroll_name_);
  data.push_back(direction);
  data.push_back(this->enroll_step_timeout_s_);

  ESP_LOGD(TAG, "Interactive enroll: capture direction %u/5 (%s)", this->enroll_direction_index_ + 1,
            DIRECTION_NAMES[this->enroll_direction_index_]);
  this->send_command_(MID_ENROLL, data, Fp002PendingCmd::ENROLL,
                       (static_cast<uint32_t>(this->enroll_step_timeout_s_) + 5) * 1000UL);
}

void Fp002Component::delete_user(uint16_t user_id, bool palm_vein) {
  uint8_t mid = palm_vein ? MID_DELUSER_PALM : MID_DELUSER_FACE;
  std::vector<uint8_t> data = {
      static_cast<uint8_t>(user_id >> 8),
      static_cast<uint8_t>(user_id & 0xFF),
      static_cast<uint8_t>(palm_vein ? 1 : 0),
  };
  ESP_LOGI(TAG, "Deleting %s user id %u", palm_vein ? "palm-vein" : "face", user_id);
  this->pending_delete_user_id_ = user_id;
  this->pending_delete_is_palm_ = palm_vein;
  this->send_command_(mid, data, Fp002PendingCmd::DELETE_USER, 2000);
}

void Fp002Component::delete_all_users(uint8_t type) {
  ESP_LOGI(TAG, "Deleting users (type=%u: 0=all,1=face,2=palm,3=general)", type);
  std::vector<uint8_t> data = {type, 0, 0, 0, 0};
  this->send_command_(MID_DELALL, data, Fp002PendingCmd::DELETE_ALL, 3000);
}

void Fp002Component::delete_user_range(uint16_t begin_id, uint16_t end_id) {
  ESP_LOGI(TAG, "Deleting users in range [%u, %u]", begin_id, end_id);
  std::vector<uint8_t> data = {
      4,  // type = 4: range delete
      static_cast<uint8_t>(begin_id >> 8),
      static_cast<uint8_t>(begin_id & 0xFF),
      static_cast<uint8_t>(end_id >> 8),
      static_cast<uint8_t>(end_id & 0xFF),
  };
  this->send_command_(MID_DELALL, data, Fp002PendingCmd::DELETE_ALL, 3000);
}

void Fp002Component::refresh_user_list(bool palm_vein) {
  std::vector<uint8_t> data = {static_cast<uint8_t>(palm_vein ? 2 : 1)};  // short bitmap format
  ESP_LOGD(TAG, "Requesting registered %s user list", palm_vein ? "palm-vein" : "face");
  this->send_command_(MID_GET_ALL_USERID, data, Fp002PendingCmd::REFRESH_LIST, 3000);
}

void Fp002Component::reset_module() {
  ESP_LOGD(TAG, "Sending MID_RESET");
  this->pending_cmd_ = Fp002PendingCmd::NONE;  // abandon whatever we were waiting on
  this->send_command_(MID_RESET, {}, Fp002PendingCmd::RESET, 2000);
}

void Fp002Component::handle_frame_() {
  if (this->frame_msg_id_ == FP002_FRAME_REPLY) {
    if (this->data_buf_.size() < 2) {
      ESP_LOGW(TAG, "REPLY frame too short (%u bytes)", (unsigned) this->data_buf_.size());
      return;
    }
    uint8_t mid = this->data_buf_[0];
    uint8_t result = this->data_buf_[1];
    this->handle_reply_(mid, result, this->data_buf_.data() + 2, this->data_buf_.size() - 2);
  } else if (this->frame_msg_id_ == FP002_FRAME_NOTE) {
    if (this->data_buf_.empty()) {
      ESP_LOGW(TAG, "NOTE frame too short");
      return;
    }
    uint8_t nid = this->data_buf_[0];
    this->handle_note_(nid, this->data_buf_.data() + 1, this->data_buf_.size() - 1);
  } else {
    ESP_LOGW(TAG, "Unknown top-level frame id 0x%02X", this->frame_msg_id_);
  }
}

void Fp002Component::handle_note_(uint8_t nid, const uint8_t *data, size_t len) {
  switch (nid) {
    case NID_READY: {
      ESP_LOGI(TAG, "FP002 module reports ready");
      this->ready_ = true;
      this->last_command_done_ms_ = millis();
      this->ready_callback_.call();
      this->refresh_user_list(false);  // populate the registered-user list right away
      break;
    }
    case NID_FACE_STATE:
      if (len >= 1) {
        ESP_LOGV(TAG, "Face/palm state update: 0x%02X", data[0]);
      }
      break;
    case NID_EYE_STATE:
      if (len >= 1) {
        ESP_LOGV(TAG, "Eye state update: 0x%02X", data[0]);
      }
      break;
    case NID_OTA_DONE:
      ESP_LOGI(TAG, "OTA update finished (result=0x%02X)", len >= 1 ? data[0] : 0xFF);
      break;
    case NID_UNKNOWNERROR:
      ESP_LOGW(TAG, "Module reported an unknown error via NOTE");
      break;
    default:
      ESP_LOGV(TAG, "Unhandled NOTE nid=0x%02X len=%u", nid, (unsigned) len);
      break;
  }
}

void Fp002Component::handle_reply_(uint8_t mid, uint8_t result, const uint8_t *data, size_t len) {
  // MID_VERIFY -------------------------------------------------------------
  if (mid == MID_VERIFY) {
    this->pending_cmd_ = Fp002PendingCmd::NONE;
    this->last_command_done_ms_ = millis();

    // Return data on success: user_id(2, MSB first), user_name(32), admin(1),
    // unlockStatus(1) = 36 bytes total.
    if (result == MR_SUCCESS && len >= 36) {
      uint16_t user_id = (static_cast<uint16_t>(data[0]) << 8) | data[1];
      size_t name_len = strnlen(reinterpret_cast<const char *>(data + 2), 32);
      std::string user_name(reinterpret_cast<const char *>(data + 2), name_len);
      bool admin = data[34] != 0;
      uint8_t unlock_status = data[35];

      ESP_LOGI(TAG, "Face/palm recognized: id=%u name='%s' admin=%s unlock_status=0x%02X", user_id,
                user_name.c_str(), admin ? "yes" : "no", unlock_status);

#ifdef USE_BINARY_SENSOR
      if (this->face_recognized_binary_sensor_ != nullptr)
        this->face_recognized_binary_sensor_->publish_state(true);
#endif
#ifdef USE_TEXT_SENSOR
      if (this->recognized_user_name_text_sensor_ != nullptr)
        this->recognized_user_name_text_sensor_->publish_state(user_name);
#endif
#ifdef USE_SENSOR
      if (this->recognized_user_id_sensor_ != nullptr)
        this->recognized_user_id_sensor_->publish_state(user_id);
#endif
      this->face_recognized_callback_.call(user_id, user_name, admin);
    } else {
      ESP_LOGD(TAG, "Verify finished without a match (result=0x%02X)", result);
#ifdef USE_BINARY_SENSOR
      if (this->face_recognized_binary_sensor_ != nullptr)
        this->face_recognized_binary_sensor_->publish_state(false);
#endif
      this->face_rejected_callback_.call(result);
    }
    return;
  }

  // MID_ENROLL_SINGLE --------------------------------------------------------
  if (mid == MID_ENROLL_SINGLE) {
    // Return data: user_id_heb(1), user_id_leb(1), face_direction/status(1).
    if (len >= 2) {
      uint16_t user_id = (static_cast<uint16_t>(data[0]) << 8) | data[1];
      if (result == MR_SUCCESS && user_id == 0xFFFF) {
        // Only expected for the interactive multi-direction flow, but handle it
        // defensively: this isn't final yet, keep waiting for the real reply.
        ESP_LOGD(TAG, "Enroll in progress...");
        this->command_started_ms_ = millis();  // extend the watchdog
        return;
      }
      this->pending_cmd_ = Fp002PendingCmd::NONE;
      this->last_command_done_ms_ = millis();

      if (result == MR_SUCCESS) {
        ESP_LOGI(TAG, "Enrolled '%s' successfully as user id %u", this->pending_enroll_name_.c_str(), user_id);
        this->start_led_blinks_(FP002_LED_GREEN, 3);
        this->enroll_success_callback_.call(user_id, this->pending_enroll_name_, this->pending_enroll_admin_);
        this->refresh_user_list(false);
      } else {
        ESP_LOGW(TAG, "Enroll of '%s' failed (result=0x%02X)", this->pending_enroll_name_.c_str(), result);
        this->start_led_blinks_(FP002_LED_RED, 2);
        this->enroll_failed_callback_.call(result);
      }
    } else {
      this->pending_cmd_ = Fp002PendingCmd::NONE;
      this->last_command_done_ms_ = millis();
      ESP_LOGW(TAG, "Enroll reply too short (result=0x%02X)", result);
      this->start_led_blinks_(FP002_LED_RED, 2);
      this->enroll_failed_callback_.call(result);
    }
    return;
  }

  // MID_ENROLL - one direction of the interactive 5-direction enroll ---------
  if (mid == MID_ENROLL) {
    this->pending_cmd_ = Fp002PendingCmd::NONE;
    this->last_command_done_ms_ = millis();

    if (result != MR_SUCCESS || len < 3) {
      ESP_LOGW(TAG, "Interactive enroll of '%s' failed on direction %u/5 (result=0x%02X)",
                this->pending_enroll_name_.c_str(), this->enroll_direction_index_ + 1, result);
      this->interactive_enroll_active_ = false;
      this->start_led_blinks_(FP002_LED_RED, 2);
      this->enroll_failed_callback_.call(result);
      return;
    }

    uint16_t user_id = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    uint8_t face_bitmap = data[2];

    if (user_id != 0xFFFF) {
      // All 5 directions captured - the module just created the user.
      ESP_LOGI(TAG, "Enrolled '%s' successfully as user id %u (interactive, bitmap=0x%02X)",
                this->pending_enroll_name_.c_str(), user_id, face_bitmap);
      this->interactive_enroll_active_ = false;
      this->start_led_blinks_(FP002_LED_GREEN, 3);
      this->enroll_success_callback_.call(user_id, this->pending_enroll_name_, this->pending_enroll_admin_);
      this->refresh_user_list(false);
      return;
    }

    // One more direction captured, more still to go - blink once, then the
    // loop() step-sequencer sends the next direction once the blink is done.
    ESP_LOGD(TAG, "Interactive enroll: direction %u/5 captured (bitmap=0x%02X)", this->enroll_direction_index_ + 1,
              face_bitmap);
    this->enroll_direction_index_++;
    this->start_led_blinks_(FP002_LED_GREEN, 1);
    return;
  }

  // MID_DELUSER (either face 0x20 or palm-vein 0x80) ------------------------
  if (mid == MID_DELUSER_FACE || mid == MID_DELUSER_PALM) {
    this->pending_cmd_ = Fp002PendingCmd::NONE;
    this->last_command_done_ms_ = millis();

    if (result == MR_SUCCESS) {
      ESP_LOGI(TAG, "Deleted user id %u", this->pending_delete_user_id_);
      this->delete_success_callback_.call(this->pending_delete_user_id_);
      this->refresh_user_list(false);
    } else {
      ESP_LOGW(TAG, "Delete of user id %u failed (result=0x%02X)", this->pending_delete_user_id_, result);
      this->delete_failed_callback_.call(this->pending_delete_user_id_, result);
    }
    return;
  }

  // MID_DELALL ---------------------------------------------------------------
  if (mid == MID_DELALL) {
    this->pending_cmd_ = Fp002PendingCmd::NONE;
    this->last_command_done_ms_ = millis();

    if (result == MR_SUCCESS) {
      ESP_LOGI(TAG, "Bulk delete completed");
      this->delete_all_success_callback_.call();
      this->refresh_user_list(false);
    } else {
      ESP_LOGW(TAG, "Bulk delete failed (result=0x%02X)", result);
      this->delete_failed_callback_.call(0, result);
    }
    return;
  }

  // MID_GET_ALL_USERID ---------------------------------------------------------
  if (mid == MID_GET_ALL_USERID) {
    this->pending_cmd_ = Fp002PendingCmd::NONE;
    this->last_command_done_ms_ = millis();

    if (result == MR_SUCCESS && len >= 2) {
      uint16_t reported_count = (static_cast<uint16_t>(data[0]) << 8) | data[1];
      std::string ids_csv;
      uint32_t found = 0;
      for (size_t byte_idx = 2; byte_idx < len; byte_idx++) {
        uint8_t b = data[byte_idx];
        for (uint8_t bit = 0; bit < 8; bit++) {
          if (b & (1 << bit)) {
            uint32_t user_id = (byte_idx - 2) * 8 + bit + 1;
            if (!ids_csv.empty())
              ids_csv += ",";
            ids_csv += std::to_string(user_id);
            found++;
          }
        }
      }
      ESP_LOGI(TAG, "Registered users: %u (module reported %u): %s", found, reported_count,
                ids_csv.empty() ? "(none)" : ids_csv.c_str());
#ifdef USE_TEXT_SENSOR
      if (this->registered_user_ids_text_sensor_ != nullptr)
        this->registered_user_ids_text_sensor_->publish_state(ids_csv);
#endif
#ifdef USE_SENSOR
      if (this->registered_user_count_sensor_ != nullptr)
        this->registered_user_count_sensor_->publish_state(found);
#endif
      this->user_list_updated_callback_.call();
    } else {
      ESP_LOGW(TAG, "Failed to get registered user list (result=0x%02X)", result);
    }
    return;
  }

  // MID_RESET / MID_GETSTATUS / MID_GET_VERSION / MID_LED_CONTROL ------------
  if (mid == MID_RESET) {
    this->pending_cmd_ = Fp002PendingCmd::NONE;
    this->last_command_done_ms_ = millis();
    ESP_LOGD(TAG, "Module acknowledged reset (result=0x%02X)", result);
    return;
  }
  if (mid == MID_GETSTATUS) {
    if (len >= 1)
      ESP_LOGD(TAG, "Module status: 0x%02X", data[0]);
    return;
  }
  if (mid == MID_GET_VERSION) {
    size_t vlen = strnlen(reinterpret_cast<const char *>(data), len);
    std::string version(reinterpret_cast<const char *>(data), vlen);
    ESP_LOGI(TAG, "Module version: %s", version.c_str());
    return;
  }
  if (mid == MID_LED_CONTROL) {
    ESP_LOGV(TAG, "Module acknowledged LED control (result=0x%02X)", result);
    return;
  }

  ESP_LOGV(TAG, "Unhandled REPLY for mid=0x%02X result=0x%02X len=%u", mid, result, (unsigned) len);
}

}  // namespace fp002
}  // namespace esphome
