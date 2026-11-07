import esphome.codegen as cg
from esphome import automation
import esphome.config_validation as cv
from esphome.components import light, time
from esphome.const import (
    CONF_DEFAULT_TRANSITION_LENGTH,
    CONF_ESPHOME,
    CONF_PROJECT,
    CONF_VERSION,
    CONF_GAMMA_CORRECT,
    CONF_OUTPUT_ID,
    CONF_TRIGGER_ID,
)

from esphome.core import CORE

from . import PROJECT_NAME, REPO_URL, Quntis0213Light, RemoteTrigger, source_version

CONF_CE_PIN = "ce_pin"
CONF_CS_PIN = "cs_pin"
CONF_SPI_CLK_PIN = "spi_clk_pin"
CONF_SPI_MOSI_PIN = "spi_mosi_pin"
CONF_SPI_MISO_PIN = "spi_miso_pin"
CONF_CHANNEL = "channel"
CONF_DEVICE_ADDRESS = "device_address"
CONF_REMOTE_ID = "remote_id"
CONF_MIN_LEVEL = "min_level"
CONF_ON_REMOTE = "on_remote"
CONF_TIME_ID = "time_id"

CONFIG_SCHEMA = (
    light.LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(Quntis0213Light),
            cv.Optional(CONF_DEFAULT_TRANSITION_LENGTH, default="0s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_GAMMA_CORRECT, default=1.0): cv.positive_float,
            cv.Required(CONF_CE_PIN): cv.int_range(min=0, max=48),
            cv.Required(CONF_CS_PIN): cv.int_range(min=0, max=48),
            cv.Required(CONF_SPI_CLK_PIN): cv.int_range(min=0, max=48),
            cv.Required(CONF_SPI_MOSI_PIN): cv.int_range(min=0, max=48),
            cv.Required(CONF_SPI_MISO_PIN): cv.int_range(min=0, max=48),
            cv.Optional(CONF_CHANNEL, default=80): cv.int_range(min=0, max=125),
            # Both values can instead be learned from the remote on the device.
            # Scrambled address bytes, as they appear on air after the 71 0F 55 sync.
            cv.Inclusive(CONF_DEVICE_ADDRESS, "remote"): cv.All(
                cv.ensure_list(cv.hex_uint8_t), cv.Length(min=5, max=5)
            ),
            # First four decoded payload bytes.
            cv.Inclusive(CONF_REMOTE_ID, "remote"): cv.All(
                cv.ensure_list(cv.hex_uint8_t), cv.Length(min=4, max=4)
            ),
            # The remote's lowest level is 0x1A. The tested lamp stays steady down to 0x03, about 1 %.
            cv.Optional(CONF_MIN_LEVEL, default=0x03): cv.All(cv.hex_uint8_t, cv.Range(min=1, max=0xFF)),
            # A clock for following the time of day, usually "platform: homeassistant".
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            # Runs once per press on the remote, with the button's name in "button".
            cv.Optional(CONF_ON_REMOTE): automation.validate_automation(
                {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(RemoteTrigger)}
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    cg.add_library("SPI", None)
    cg.add_library("nrf24/RF24", "1.6.2")

    # Home Assistant shows the project version on the device page. A YAML "project:" wins, as in
    # the ready-made firmware, whose version comes from the GitHub build.
    project = CORE.config[CONF_ESPHOME].get(CONF_PROJECT)
    version = project[CONF_VERSION] if project and project[CONF_VERSION] != "dev" else source_version()
    if project is None:
        cg.add_define("ESPHOME_PROJECT_NAME", PROJECT_NAME)
        cg.add_define("ESPHOME_PROJECT_VERSION", version)
        cg.add_define("ESPHOME_PROJECT_VERSION_30", version[:29])
    cg.add_define("QUNTIS_VERSION", version)
    cg.add_define("QUNTIS_REPO_URL", REPO_URL)

    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await light.register_light(var, config)

    cg.add(var.set_pins(
        config[CONF_CE_PIN],
        config[CONF_CS_PIN],
        config[CONF_SPI_CLK_PIN],
        config[CONF_SPI_MOSI_PIN],
        config[CONF_SPI_MISO_PIN],
    ))
    cg.add(var.set_channel(config[CONF_CHANNEL]))
    cg.add(var.set_min_level(config[CONF_MIN_LEVEL]))
    if CONF_TIME_ID in config:
        clock = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(clock))
    for conf in config.get(CONF_ON_REMOTE, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "button")], conf)
    if CONF_DEVICE_ADDRESS in config:
        cg.add(var.set_device_address(config[CONF_DEVICE_ADDRESS]))
        cg.add(var.set_device_id(config[CONF_REMOTE_ID]))
