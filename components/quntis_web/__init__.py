import gzip
import logging
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light, web_server_base, wifi
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
from esphome.const import CONF_ID, CONF_LIGHT_ID, __version__
from esphome.core import CORE, coroutine_with_priority, CoroPriority

# web_server is what makes Home Assistant show the "Visit" link. It also reserves the sockets
# a browser needs. QuntisWeb answers the requests in its place.
DEPENDENCIES = ["web_server", "wifi"]
AUTO_LOAD = ["quntis_0213", "web_server_base"]

_LOGGER = logging.getLogger(__name__)

# The WiFi scan relies on ESPHome internals, which can change between releases.
TESTED_VERSION = "2026.10"

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
    if not __version__.startswith(TESTED_VERSION):
        _LOGGER.warning(
            "quntis_web was tested with ESPHome %s.x, not %s. If the build fails, use "
            "ESPHome %s or report the error on the project's GitHub page.",
            TESTED_VERSION,
            __version__,
            TESTED_VERSION,
        )
    base = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    var = cg.new_Pvariable(config[CONF_ID], base)
    await cg.register_component(var, config)

    # The page is sent compressed: about a third of its size, so it loads faster.
    source = (Path(__file__).parent / "page.h").read_text(encoding="utf-8")
    html = source[source.index('R"QUNTIS(') + len('R"QUNTIS(') : source.index(')QUNTIS"')]
    page = gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0)
    cg.add_global(
        cg.RawStatement(
            "extern const uint8_t QUNTIS_PAGE_GZ[] = {"
            + ",".join(str(b) for b in page)
            + f"}};\nextern const size_t QUNTIS_PAGE_GZ_SIZE = {len(page)};"
        )
    )

    light_state = await cg.get_variable(config[CONF_LIGHT_ID])
    cg.add(var.set_light(light_state))

    # A password changed on the page replaces this one, until this one changes.
    if (auth := CORE.config.get("web_server", {}).get("auth")) is not None:
        cg.add(var.set_yaml_password(auth["password"]))

    # The page lists networks while connected, and reads them from the web server task.
    wifi.request_wifi_scan_results()
    wifi.request_wifi_scan_results_lock()
