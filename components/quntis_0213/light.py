import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import CONF_DEFAULT_TRANSITION_LENGTH, CONF_GAMMA_CORRECT, CONF_OUTPUT_ID

from . import Quntis0213Light

CONF_CE_PIN = "ce_pin"
CONF_CS_PIN = "cs_pin"
CONF_SPI_CLK_PIN = "spi_clk_pin"
CONF_SPI_MOSI_PIN = "spi_mosi_pin"
CONF_SPI_MISO_PIN = "spi_miso_pin"
CONF_CHANNEL = "channel"
CONF_DEVICE_ADDRESS = "device_address"
CONF_REMOTE_ID = "remote_id"

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
            # Scrambled address bytes, as they appear on air after the 71 0F 55 sync.
            cv.Required(CONF_DEVICE_ADDRESS): cv.All(
                cv.ensure_list(cv.hex_uint8_t), cv.Length(min=5, max=5)
            ),
            # First four decoded payload bytes.
            cv.Required(CONF_REMOTE_ID): cv.All(
                cv.ensure_list(cv.hex_uint8_t), cv.Length(min=4, max=4)
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    cg.add_library("SPI", None)
    cg.add_library("nrf24/RF24", "1.6.2")

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
    cg.add(var.set_device_address(config[CONF_DEVICE_ADDRESS]))
    cg.add(var.set_device_id(config[CONF_REMOTE_ID]))
