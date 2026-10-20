#pragma once

#include <vector>
#include <string>

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/uart/uart.h"

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif

namespace esphome {
namespace fp002 {

// Protocol constants, taken from the GROW FP002 Facial and Palm Vein
// Recognition Module User Manual (Hangzhou Grow Technology Co., Ltd, V1.01).

static const uint8_t FP002_SYNC1 = 0xEF;
static const uint8_t FP002_SYNC2 = 0xAA;

// Top-level frame MsgID (byte right after the sync word)
static const uint8_t FP002_FRAME_REPLY = 0x00;
static const uint8_t FP002_FRAME_NOTE = 0x01;

// Command IDs (sent host -> module, echoed back as "mid" inside a REPLY)
static const uint8_t MID_RESET = 0x10;
static const uint8_t MID_GETSTATUS = 0x11;
static const uint8_t MID_VERIFY = 0x12;
static const uint8_t MID_ENROLL = 0x13;          // interactive, 5-direction enroll
static const uint8_t MID_ENROLL_SINGLE = 0x1D;
static const uint8_t MID_DELUSER_FACE = 0x20;    // delete a single face user
static const uint8_t MID_DELALL = 0x21;
static const uint8_t MID_GET_ALL_USERID = 0x24;
static const uint8_t MID_GET_VERSION = 0x30;
static const uint8_t MID_LED_CONTROL = 0x90;
static const uint8_t MID_DELUSER_PALM = 0x80;    // delete a single palm-vein user

// NOTE ids (sent unsolicited module -> host)
static const uint8_t NID_READY = 0x00;
static const uint8_t NID_FACE_STATE = 0x01;
static const uint8_t NID_UNKNOWNERROR = 0x02;
static const uint8_t NID_OTA_DONE = 0x03;
static const uint8_t NID_EYE_STATE = 0x04;

// Result codes
static const uint8_t MR_SUCCESS = 0x00;
static const uint8_t MR_FAILED4_TIMEOUT = 0x0D;  // used as our own host-side watchdog result too

// MID_GETSTATUS status byte
static const uint8_t MS_STANDBY = 0x00;

// face_direction codes for MID_ENROLL (section 4.2 of the manual). Sent one at a
// time, host -> module; the module's REPLY echoes back a cumulative bitmask of
// which directions have been captured so far (Figure 10).
static const uint8_t FACE_DIRECTION_MIDDLE = 0x01;
static const uint8_t FACE_DIRECTION_RIGHT = 0x02;
static const uint8_t FACE_DIRECTION_LEFT = 0x04;
static const uint8_t FACE_DIRECTION_DOWN = 0x08;
static const uint8_t FACE_DIRECTION_UP = 0x10;

// MID_LED_CONTROL color codes.
static const uint8_t FP002_LED_GREEN = 0x00;
static const uint8_t FP002_LED_RED = 0x01;
static const uint8_t FP002_LED_WHITE = 0x02;

// What command we're currently waiting on a REPLY for. Only one command is
// ever "in flight" at a time - this keeps the auto-verify loop from
// stepping on an enroll/delete/list request the user triggered from HA,
// and vice versa.
enum class Fp002PendingCmd {
  NONE,
  VERIFY,
  ENROLL,
  DELETE_USER,
  DELETE_ALL,
  REFRESH_LIST,
  RESET,
  GET_STATUS,
};

class Fp002Component : public Component, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

  void set_auto_verify(bool auto_verify) { this->auto_verify_ = auto_verify; }
  void set_verify_timeout(uint8_t seconds) { this->verify_timeout_ = seconds; }
  void set_cooldown_ms(uint32_t cooldown_ms) { this->cooldown_ms_ = cooldown_ms; }
  void set_enroll_timeout(uint8_t seconds) { this->enroll_timeout_ = seconds; }

#ifdef USE_BINARY_SENSOR
  void set_face_recognized_binary_sensor(binary_sensor::BinarySensor *bs) {
    this->face_recognized_binary_sensor_ = bs;
  }
#endif
#ifdef USE_TEXT_SENSOR
  void set_recognized_user_name_text_sensor(text_sensor::TextSensor *ts) {
    this->recognized_user_name_text_sensor_ = ts;
  }
  void set_registered_user_ids_text_sensor(text_sensor::TextSensor *ts) {
    this->registered_user_ids_text_sensor_ = ts;
  }
#endif
#ifdef USE_SENSOR
  void set_recognized_user_id_sensor(sensor::Sensor *s) { this->recognized_user_id_sensor_ = s; }
  void set_registered_user_count_sensor(sensor::Sensor *s) { this->registered_user_count_sensor_ = s; }
#endif

  void add_on_ready_callback(std::function<void()> &&cb) { this->ready_callback_.add(std::move(cb)); }
  void add_on_face_recognized_callback(std::function<void(uint16_t, std::string, bool)> &&cb) {
    this->face_recognized_callback_.add(std::move(cb));
  }
  void add_on_face_rejected_callback(std::function<void(uint8_t)> &&cb) {
    this->face_rejected_callback_.add(std::move(cb));
  }
  void add_on_enroll_success_callback(std::function<void(uint16_t, std::string, bool)> &&cb) {
    this->enroll_success_callback_.add(std::move(cb));
  }
  void add_on_enroll_failed_callback(std::function<void(uint8_t)> &&cb) {
    this->enroll_failed_callback_.add(std::move(cb));
  }
  void add_on_delete_success_callback(std::function<void(uint16_t)> &&cb) {
    this->delete_success_callback_.add(std::move(cb));
  }
  void add_on_delete_failed_callback(std::function<void(uint16_t, uint8_t)> &&cb) {
    this->delete_failed_callback_.add(std::move(cb));
  }
  void add_on_delete_all_success_callback(std::function<void()> &&cb) {
    this->delete_all_success_callback_.add(std::move(cb));
  }
  void add_on_user_list_updated_callback(std::function<void()> &&cb) {
    this->user_list_updated_callback_.add(std::move(cb));
  }

  /// Manually (re)start a single face/palm-vein verify attempt. Called automatically
  /// on a loop when auto_verify is enabled (the default) - you normally don't need to
  /// call this yourself unless auto_verify is turned off.
  void start_verify();

  /// Enroll a new user. By default this drives the module's interactive 5-direction
  /// enroll (MID_ENROLL: center, right, left, down, up) for better recognition
  /// accuracy than a single photo - the module's status LED blinks green once per
  /// captured direction and 3x on completion (red 2x on failure), so point the
  /// person at the camera and have them slowly follow the blinks/turn as prompted
  /// by whatever you wire to on_enroll_success/on_enroll_failed.
  /// Palm-vein users always use the single-frame command (MID_ENROLL_SINGLE) since
  /// the module doesn't support interactive enroll for palm vein. Set `quick=true`
  /// to use single-frame enroll for a face too (faster, one photo, less robust).
  void enroll_user(const std::string &user_name, bool admin = false, bool palm_vein = false, bool quick = false,
                    uint8_t timeout = 0);

  /// Delete one registered user by id. `palm_vein` must match how they were enrolled
  /// (face ids are 1-500, palm vein ids are 501-800 per the manual).
  void delete_user(uint16_t user_id, bool palm_vein = false);

  /// Delete users in bulk. type: 0=all, 1=all face users, 2=all palm-vein users,
  /// 3=all "general" users. `begin_id`/`end_id` are only used when you call the
  /// overload below with type=4 (range delete).
  void delete_all_users(uint8_t type = 0);
  void delete_user_range(uint16_t begin_id, uint16_t end_id);

  /// Ask the module for the list of currently registered user ids and refresh the
  /// registered_user_ids / registered_user_count entities. Called automatically
  /// after boot and after every successful enroll/delete.
  void refresh_user_list(bool palm_vein = false);

  /// Send MID_RESET - aborts whatever the module is currently doing.
  void reset_module();

 protected:
  enum class ParseState {
    WAIT_SYNC1,
    WAIT_SYNC2,
    WAIT_MSGID,
    WAIT_SIZE_H,
    WAIT_SIZE_L,
    WAIT_DATA,
    WAIT_CHECKSUM,
  } state_{ParseState::WAIT_SYNC1};

  uint8_t frame_msg_id_{0};
  uint16_t data_size_{0};
  std::vector<uint8_t> data_buf_;

  bool ready_{false};

  Fp002PendingCmd pending_cmd_{Fp002PendingCmd::NONE};
  uint8_t pending_reply_mid_{0};
  uint32_t command_started_ms_{0};
  uint32_t command_timeout_ms_{5000};
  uint32_t last_command_done_ms_{0};

  // One command that arrived while another was still in flight (typically the
  // background auto-verify). It's sent from loop() as soon as the module is idle;
  // if the command in flight is a verify, that verify is aborted with MID_RESET
  // first so HA-triggered enroll/delete requests aren't silently dropped.
  struct QueuedCommand {
    bool active{false};
    uint8_t mid{0};
    std::vector<uint8_t> data;
    Fp002PendingCmd kind{Fp002PendingCmd::NONE};
    uint32_t timeout_ms{0};
  } queued_;

  // NID_READY is only sent when the module itself powers up. If only the ESP32
  // restarts (OTA, reset button) while the module stays powered, it never comes,
  // so we probe with MID_GETSTATUS until the module answers.
  uint32_t last_ready_probe_ms_{0};
  static const uint32_t READY_PROBE_INTERVAL_MS = 3000;

  // Context carried from the request to when its REPLY arrives.
  uint16_t pending_delete_user_id_{0};
  bool pending_delete_is_palm_{false};
  std::string pending_enroll_name_;
  bool pending_enroll_admin_{false};

  // Interactive (5-direction) enroll state. interactive_enroll_active_ stays true
  // for the whole multi-step flow, including the gaps between steps while the LED
  // is blinking - it's what keeps auto_verify from stepping on an enroll in
  // progress and what the loop() step-sequencer checks to send the next direction.
  bool interactive_enroll_active_{false};
  uint8_t enroll_direction_index_{0};
  uint8_t enroll_step_timeout_s_{15};
  static const uint8_t NUM_ENROLL_DIRECTIONS = 5;

  // Non-blocking LED blink feedback (MID_LED_CONTROL), used for enroll progress/
  // success/failure. Fire-and-forget: doesn't go through pending_cmd_, so it can
  // run in the gaps between enroll commands without the "module busy" guard.
  bool led_blink_active_{false};
  uint8_t led_blink_color_{0};
  uint8_t led_blinks_target_{0};
  uint8_t led_blinks_done_{0};
  bool led_on_now_{false};
  uint32_t led_next_toggle_ms_{0};
  static const uint32_t LED_BLINK_ON_MS = 150;
  static const uint32_t LED_BLINK_OFF_MS = 150;

  bool auto_verify_{true};
  uint8_t verify_timeout_{5};
  uint8_t enroll_timeout_{15};
  uint32_t cooldown_ms_{2000};

  void handle_frame_();
  void handle_reply_(uint8_t mid, uint8_t result, const uint8_t *data, size_t len);
  void handle_note_(uint8_t nid, const uint8_t *data, size_t len);

  /// Common "module is ready" handling for NID_READY and a successful status probe.
  void mark_ready_();
  /// Abort a background MID_VERIFY with MID_RESET (used when a queued command is waiting).
  void abort_verify_();

  /// Low-level frame writer (sync word, mid, size, data, checksum) - shared by
  /// send_command_() and send_led_(). Does not touch pending_cmd_ tracking.
  void write_frame_(uint8_t mid, const std::vector<uint8_t> &data);

  /// Returns false (and logs) without sending anything if another command is
  /// still in flight.
  bool send_command_(uint8_t mid, const std::vector<uint8_t> &data, Fp002PendingCmd kind, uint32_t timeout_ms);
  static void append_name_(std::vector<uint8_t> &out, const std::string &name);

  /// Send one direction of the interactive enroll (uses enroll_direction_index_).
  void send_enroll_step_();

  /// Fire-and-forget MID_LED_CONTROL. on=true turns the LED on (module's on/off
  /// bit is inverted: 0x00=on, 0x01=off - handled here so callers don't need to
  /// remember that).
  void send_led_(uint8_t color, bool on);
  /// Start a non-blocking blink sequence: `count` on/off cycles of `color`,
  /// advanced from loop() via update_led_blinks_().
  void start_led_blinks_(uint8_t color, uint8_t count);
  void update_led_blinks_();

#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *face_recognized_binary_sensor_{nullptr};
#endif
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *recognized_user_name_text_sensor_{nullptr};
  text_sensor::TextSensor *registered_user_ids_text_sensor_{nullptr};
#endif
#ifdef USE_SENSOR
  sensor::Sensor *recognized_user_id_sensor_{nullptr};
  sensor::Sensor *registered_user_count_sensor_{nullptr};
#endif

  CallbackManager<void()> ready_callback_;
  CallbackManager<void(uint16_t, std::string, bool)> face_recognized_callback_;
  CallbackManager<void(uint8_t)> face_rejected_callback_;
  CallbackManager<void(uint16_t, std::string, bool)> enroll_success_callback_;
  CallbackManager<void(uint8_t)> enroll_failed_callback_;
  CallbackManager<void(uint16_t)> delete_success_callback_;
  CallbackManager<void(uint16_t, uint8_t)> delete_failed_callback_;
  CallbackManager<void()> delete_all_success_callback_;
  CallbackManager<void()> user_list_updated_callback_;
};

class Fp002ReadyTrigger : public Trigger<> {
 public:
  explicit Fp002ReadyTrigger(Fp002Component *parent) {
    parent->add_on_ready_callback([this]() { this->trigger(); });
  }
};

// Arguments: user_id, user_name, admin
class Fp002FaceRecognizedTrigger : public Trigger<uint16_t, std::string, bool> {
 public:
  explicit Fp002FaceRecognizedTrigger(Fp002Component *parent) {
    parent->add_on_face_recognized_callback(
        [this](uint16_t user_id, std::string user_name, bool admin) { this->trigger(user_id, user_name, admin); });
  }
};

// Argument: result/reason code, see the manual's "MR_..." table (section 3.2.1)
class Fp002FaceRejectedTrigger : public Trigger<uint8_t> {
 public:
  explicit Fp002FaceRejectedTrigger(Fp002Component *parent) {
    parent->add_on_face_rejected_callback([this](uint8_t reason) { this->trigger(reason); });
  }
};

// Arguments: newly assigned user_id, user_name, admin
class Fp002EnrollSuccessTrigger : public Trigger<uint16_t, std::string, bool> {
 public:
  explicit Fp002EnrollSuccessTrigger(Fp002Component *parent) {
    parent->add_on_enroll_success_callback(
        [this](uint16_t user_id, std::string user_name, bool admin) { this->trigger(user_id, user_name, admin); });
  }
};

class Fp002EnrollFailedTrigger : public Trigger<uint8_t> {
 public:
  explicit Fp002EnrollFailedTrigger(Fp002Component *parent) {
    parent->add_on_enroll_failed_callback([this](uint8_t reason) { this->trigger(reason); });
  }
};

class Fp002DeleteSuccessTrigger : public Trigger<uint16_t> {
 public:
  explicit Fp002DeleteSuccessTrigger(Fp002Component *parent) {
    parent->add_on_delete_success_callback([this](uint16_t user_id) { this->trigger(user_id); });
  }
};

class Fp002DeleteFailedTrigger : public Trigger<uint16_t, uint8_t> {
 public:
  explicit Fp002DeleteFailedTrigger(Fp002Component *parent) {
    parent->add_on_delete_failed_callback(
        [this](uint16_t user_id, uint8_t reason) { this->trigger(user_id, reason); });
  }
};

class Fp002DeleteAllSuccessTrigger : public Trigger<> {
 public:
  explicit Fp002DeleteAllSuccessTrigger(Fp002Component *parent) {
    parent->add_on_delete_all_success_callback([this]() { this->trigger(); });
  }
};

}  // namespace fp002
}  // namespace esphome
