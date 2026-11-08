"""Flashing the standalone gateway firmware onto an ESP32-S3 (esptool)."""
import os
import subprocess
import sys

from .paths import GATEWAY_BIN


class EspFlashError(Exception):
    pass


def _esptool_cmd():
    try:
        import esptool  # noqa: F401  (pip install esptool)
        return [sys.executable, "-m", "esptool"]
    except ImportError:
        core = os.environ.get("PLATFORMIO_CORE_DIR", os.path.join(os.path.expanduser("~"), ".platformio"))
        p = os.path.join(core, "packages", "tool-esptoolpy", "esptool.py")
        if os.path.exists(p):
            return [sys.executable, p]
    raise EspFlashError("esptool not found: pip install esptool")


def flash_gateway(port: str, image: str = GATEWAY_BIN, log=print) -> dict:
    if not os.path.exists(image):
        raise EspFlashError(f"gateway image not found: {image}")
    args = _esptool_cmd() + ["--chip", "esp32s3", "--port", port, "--baud", "921600",
                             "write_flash", "0x0", image]
    log(f"flashing gateway ({os.path.getsize(image)} bytes) to {port} ...")
    r = subprocess.run(args, capture_output=True, text=True)
    out = r.stdout + r.stderr
    if r.returncode != 0 or "Hash of data verified" not in out:
        raise EspFlashError("esptool failed:\n" + "\n".join(out.strip().splitlines()[-8:]))
    log("gateway flashed and verified")
    return {"port": port, "bytes": os.path.getsize(image)}
