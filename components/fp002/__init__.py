import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import uart, binary_sensor, text_sensor, sensor
from esphome.const import (
    CONF_ID,
    CONF_TRIGGER_ID,
    DEVICE_CLASS_CONNECTIVITY,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

CODEOWNERS = ["@robertrebor"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "text_sensor", "sensor"]
MULTI_CONF = True

fp002_ns = cg.esphome_ns.namespace("fp002")
Fp002Component = fp002_ns.class_("Fp002Component", cg.Component, uart.UARTDevice)

Fp002ReadyTrigger = fp002_ns.class_("Fp002ReadyTrigger", automation.Trigger.template())
Fp002FaceRecognizedTrigger = fp002_ns.class_(
    "Fp002FaceRecognizedTrigger", automation.Trigger.template(cg.uint16, cg.std_string, cg.bool_)
)
Fp002FaceRejectedTrigger = fp002_ns.class_(
    "Fp002FaceRejectedTrigger", automation.Trigger.template(cg.uint8)
)
Fp002EnrollSuccessTrigger = fp002_ns.class_(
    "Fp002EnrollSuccessTrigger", automation.Trigger.template(cg.uint16, cg.std_string, cg.bool_)
)
Fp002EnrollFailedTrigger = fp002_ns.class_(
    "Fp002EnrollFailedTrigger", automation.Trigger.template(cg.uint8)
)
Fp002DeleteSuccessTrigger = fp002_ns.class_(
    "Fp002DeleteSuccessTrigger", automation.Trigger.template(cg.uint16)
)
Fp002DeleteFailedTrigger = fp002_ns.class_(
    "Fp002DeleteFailedTrigger", automation.Trigger.template(cg.uint16, cg.uint8)
)
Fp002DeleteAllSuccessTrigger = fp002_ns.class_(
    "Fp002DeleteAllSuccessTrigger", automation.Trigger.template()
)

CONF_AUTO_VERIFY = "auto_verify"
CONF_VERIFY_TIMEOUT = "verify_timeout"
CONF_ENROLL_TIMEOUT = "enroll_timeout"
CONF_COOLDOWN = "cooldown"
CONF_FACE_RECOGNIZED_BINARY_SENSOR = "face_recognized_binary_sensor"
CONF_RECOGNIZED_USER_NAME = "recognized_user_name"
CONF_RECOGNIZED_USER_ID = "recognized_user_id"
CONF_REGISTERED_USER_IDS = "registered_user_ids"
CONF_REGISTERED_USER_COUNT = "registered_user_count"
CONF_MODULE_READY = "module_ready"
CONF_REGISTERED_PALM_USER_IDS = "registered_palm_user_ids"
CONF_REGISTERED_PALM_USER_COUNT = "registered_palm_user_count"
CONF_RECOGNIZED_USER_ADMIN = "recognized_user_admin"
CONF_ENROLL_STATUS = "enroll_status"
CONF_ENROLL_PROGRESS = "enroll_progress"
CONF_LAST_EVENT = "last_event"
CONF_ON_READY = "on_ready"
CONF_ON_FACE_RECOGNIZED = "on_face_recognized"
CONF_ON_FACE_REJECTED = "on_face_rejected"
CONF_ON_ENROLL_SUCCESS = "on_enroll_success"
CONF_ON_ENROLL_FAILED = "on_enroll_failed"
CONF_ON_DELETE_SUCCESS = "on_delete_success"
CONF_ON_DELETE_FAILED = "on_delete_failed"
CONF_ON_DELETE_ALL_SUCCESS = "on_delete_all_success"

# UART settings are checked at config validation time (replaces the C++
# check_uart_settings(), removed in ESPHome 2027.3).
FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "fp002", baud_rate=115200, require_tx=True, require_rx=True
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Fp002Component),
            cv.Optional(CONF_AUTO_VERIFY, default=True): cv.boolean,
            # Module-side timeout for a single verify attempt, 1-255 seconds
            # (see MID_VERIFY "timeout" parameter in the manual).
            cv.Optional(CONF_VERIFY_TIMEOUT, default=5): cv.int_range(min=1, max=255),
            # Default timeout for MID_ENROLL_SINGLE, used unless a specific call
            # to enroll_user() overrides it.
            cv.Optional(CONF_ENROLL_TIMEOUT, default=15): cv.int_range(min=1, max=255),
            # How long to wait after a command finishes before starting the next
            # auto-verify attempt.
            cv.Optional(CONF_COOLDOWN, default="2s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_FACE_RECOGNIZED_BINARY_SENSOR): binary_sensor.binary_sensor_schema(),
            cv.Optional(CONF_RECOGNIZED_USER_NAME): text_sensor.text_sensor_schema(),
            cv.Optional(CONF_RECOGNIZED_USER_ID): sensor.sensor_schema(
                icon="mdi:account-key",
                accuracy_decimals=0,
            ),
            cv.Optional(CONF_REGISTERED_USER_IDS): text_sensor.text_sensor_schema(
                icon="mdi:account-multiple"
            ),
            cv.Optional(CONF_REGISTERED_USER_COUNT): sensor.sensor_schema(
                icon="mdi:account-multiple",
                accuracy_decimals=0,
            ),
            # Palm-vein users (ids 501-800), refreshed right after the face list.
            cv.Optional(CONF_REGISTERED_PALM_USER_IDS): text_sensor.text_sensor_schema(
                icon="mdi:hand-back-right"
            ),
            cv.Optional(CONF_REGISTERED_PALM_USER_COUNT): sensor.sensor_schema(
                icon="mdi:hand-back-right",
                accuracy_decimals=0,
            ),
            # Admin flag of the last recognized user (set at enroll time).
            cv.Optional(CONF_RECOGNIZED_USER_ADMIN): binary_sensor.binary_sensor_schema(
                icon="mdi:shield-account"
            ),
            # True once the module has answered (NID_READY or status probe).
            cv.Optional(CONF_MODULE_READY): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            # Human-readable enroll progress, e.g. "Step 3/5: face left", "Done: name (ID 5)".
            cv.Optional(CONF_ENROLL_STATUS): text_sensor.text_sensor_schema(
                icon="mdi:account-clock"
            ),
            # Number of captured directions during an interactive enroll (0-5).
            cv.Optional(CONF_ENROLL_PROGRESS): sensor.sensor_schema(
                icon="mdi:progress-check",
                accuracy_decimals=0,
            ),
            # Last thing that happened: recognized / rejected / enrolled / deleted ...
            cv.Optional(CONF_LAST_EVENT): text_sensor.text_sensor_schema(
                icon="mdi:history"
            ),
            cv.Optional(CONF_ON_READY): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(Fp002ReadyTrigger)}
            ),
            cv.Optional(CONF_ON_FACE_RECOGNIZED): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(Fp002FaceRecognizedTrigger)}
            ),
            cv.Optional(CONF_ON_FACE_REJECTED): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(Fp002FaceRejectedTrigger)}
            ),
            cv.Optional(CONF_ON_ENROLL_SUCCESS): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(Fp002EnrollSuccessTrigger)}
            ),
            cv.Optional(CONF_ON_ENROLL_FAILED): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(Fp002EnrollFailedTrigger)}
            ),
            cv.Optional(CONF_ON_DELETE_SUCCESS): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(Fp002DeleteSuccessTrigger)}
            ),
            cv.Optional(CONF_ON_DELETE_FAILED): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(Fp002DeleteFailedTrigger)}
            ),
            cv.Optional(CONF_ON_DELETE_ALL_SUCCESS): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(Fp002DeleteAllSuccessTrigger)}
            ),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    cg.add(var.set_auto_verify(config[CONF_AUTO_VERIFY]))
    cg.add(var.set_verify_timeout(config[CONF_VERIFY_TIMEOUT]))
    cg.add(var.set_enroll_timeout(config[CONF_ENROLL_TIMEOUT]))
    cg.add(var.set_cooldown_ms(config[CONF_COOLDOWN].total_milliseconds))

    if CONF_FACE_RECOGNIZED_BINARY_SENSOR in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_FACE_RECOGNIZED_BINARY_SENSOR])
        cg.add(var.set_face_recognized_binary_sensor(sens))

    if CONF_RECOGNIZED_USER_NAME in config:
        sens = await text_sensor.new_text_sensor(config[CONF_RECOGNIZED_USER_NAME])
        cg.add(var.set_recognized_user_name_text_sensor(sens))

    if CONF_RECOGNIZED_USER_ID in config:
        sens = await sensor.new_sensor(config[CONF_RECOGNIZED_USER_ID])
        cg.add(var.set_recognized_user_id_sensor(sens))

    if CONF_REGISTERED_USER_IDS in config:
        sens = await text_sensor.new_text_sensor(config[CONF_REGISTERED_USER_IDS])
        cg.add(var.set_registered_user_ids_text_sensor(sens))

    if CONF_REGISTERED_USER_COUNT in config:
        sens = await sensor.new_sensor(config[CONF_REGISTERED_USER_COUNT])
        cg.add(var.set_registered_user_count_sensor(sens))

    if CONF_REGISTERED_PALM_USER_IDS in config:
        sens = await text_sensor.new_text_sensor(config[CONF_REGISTERED_PALM_USER_IDS])
        cg.add(var.set_registered_palm_user_ids_text_sensor(sens))

    if CONF_REGISTERED_PALM_USER_COUNT in config:
        sens = await sensor.new_sensor(config[CONF_REGISTERED_PALM_USER_COUNT])
        cg.add(var.set_registered_palm_user_count_sensor(sens))

    if CONF_RECOGNIZED_USER_ADMIN in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_RECOGNIZED_USER_ADMIN])
        cg.add(var.set_recognized_user_admin_binary_sensor(sens))

    if CONF_MODULE_READY in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_MODULE_READY])
        cg.add(var.set_module_ready_binary_sensor(sens))

    if CONF_ENROLL_STATUS in config:
        sens = await text_sensor.new_text_sensor(config[CONF_ENROLL_STATUS])
        cg.add(var.set_enroll_status_text_sensor(sens))

    if CONF_ENROLL_PROGRESS in config:
        sens = await sensor.new_sensor(config[CONF_ENROLL_PROGRESS])
        cg.add(var.set_enroll_progress_sensor(sens))

    if CONF_LAST_EVENT in config:
        sens = await text_sensor.new_text_sensor(config[CONF_LAST_EVENT])
        cg.add(var.set_last_event_text_sensor(sens))

    for conf in config.get(CONF_ON_READY, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_FACE_RECOGNIZED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger,
            [(cg.uint16, "user_id"), (cg.std_string, "user_name"), (cg.bool_, "admin")],
            conf,
        )

    for conf in config.get(CONF_ON_FACE_REJECTED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint8, "reason")], conf)

    for conf in config.get(CONF_ON_ENROLL_SUCCESS, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(
            trigger,
            [(cg.uint16, "user_id"), (cg.std_string, "user_name"), (cg.bool_, "admin")],
            conf,
        )

    for conf in config.get(CONF_ON_ENROLL_FAILED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint8, "reason")], conf)

    for conf in config.get(CONF_ON_DELETE_SUCCESS, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint16, "user_id")], conf)

    for conf in config.get(CONF_ON_DELETE_FAILED, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint16, "user_id"), (cg.uint8, "reason")], conf)

    for conf in config.get(CONF_ON_DELETE_ALL_SUCCESS, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
