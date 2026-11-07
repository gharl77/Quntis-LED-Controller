import subprocess
from pathlib import Path

import esphome.codegen as cg
from esphome import automation
from esphome.components import light

DEPENDENCIES = []
AUTO_LOAD = ["light"]

quntis_0213_ns = cg.esphome_ns.namespace("quntis_0213")
Quntis0213Light = quntis_0213_ns.class_(
    "Quntis0213Light", light.LightOutput, cg.Component
)
PROJECT_NAME = "gharl77.quntis-0213"
REPO_URL = "https://github.com/gharl77/Quntis-LED-Controller"


def source_version() -> str:
    """The version of these files: a release such as 2026.11.2, or the release and commits after it."""
    try:
        result = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty", "--match", "v[0-9]*"],
            cwd=Path(__file__).parent,
            capture_output=True,
            text=True,
            timeout=5,
            check=True,
        )
    except (OSError, subprocess.SubprocessError):
        return "unknown"
    return result.stdout.strip().removeprefix("v") or "unknown"


RemoteTrigger = quntis_0213_ns.class_(
    "RemoteTrigger", automation.Trigger.template(cg.std_string)
)
