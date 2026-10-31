import esphome.codegen as cg
from esphome.components import light

DEPENDENCIES = []
AUTO_LOAD = ["light"]

quntis_0213_ns = cg.esphome_ns.namespace("quntis_0213")
Quntis0213Light = quntis_0213_ns.class_(
    "Quntis0213Light", light.LightOutput, cg.Component
)
