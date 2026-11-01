import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, web_server_base, wifi
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import CONF_ID, CONF_LIGHT_ID
from esphome.core import coroutine_with_priority, CoroPriority

DEPENDENCIES = ["wifi"]
AUTO_LOAD = ["quntis_0213", "socket", "web_server_base"]

quntis_web_ns = cg.esphome_ns.namespace("quntis_web")
QuntisWeb = quntis_web_ns.class_("QuntisWeb", cg.Component)


def _consume_web_sockets(config):
    # A browser keeps several connections open. Without this reservation they use up the
    # ESP32's sockets, and new connections fail, including Home Assistant's.
    from esphome.components import socket

    socket.consume_sockets(5, "quntis_web")(config)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(QuntisWeb),
            cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
            cv.Required(CONF_LIGHT_ID): cv.use_id(light.LightState),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _consume_web_sockets,
)


@coroutine_with_priority(CoroPriority.WEB)
async def to_code(config):
    base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    var = cg.new_Pvariable(config[CONF_ID], base)
    await cg.register_component(var, config)

    light_state = await cg.get_variable(config[CONF_LIGHT_ID])
    cg.add(var.set_light(light_state))

    # The page lists networks while connected, and reads them from the web server task.
    wifi.request_wifi_scan_results()
    wifi.request_wifi_scan_results_lock()
