import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, web_server_base, wifi
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import CONF_ID, CONF_LIGHT_ID
from esphome.core import coroutine_with_priority, CoroPriority

# web_server is what makes Home Assistant show the "Visit" link. It also reserves the sockets
# a browser needs. QuntisWeb answers the requests in its place.
DEPENDENCIES = ["web_server", "wifi"]
AUTO_LOAD = ["quntis_0213", "web_server_base"]

quntis_web_ns = cg.esphome_ns.namespace("quntis_web")
QuntisWeb = quntis_web_ns.class_("QuntisWeb", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(QuntisWeb),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
        cv.Required(CONF_LIGHT_ID): cv.use_id(light.LightState),
    }
).extend(cv.COMPONENT_SCHEMA)


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
