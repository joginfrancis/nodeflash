"""Factory flashing over an ST-Link: bootloader + application + "valid" record in one step.

This is the only ST-Link step a node needs. Afterwards it is updated over RS-485.
Uses OpenOCD (from PlatformIO, PATH, or NODEFLASH_OPENOCD) or, if that isn't
found, STM32CubeProgrammer's CLI (PATH or NODEFLASH_CUBEPROG).
"""
import os
import shutil
import subprocess
import tempfile

from .image import APP_START, RECORD_ADDR, check_image, crc32, valid_record
from .paths import FIRMWARE_BIN

BOOT_MAX = APP_START - 0x08000000


class FlashError(Exception):
    pass


def bootloader_for(node_id: int) -> str:
    p = os.path.join(FIRMWARE_BIN, "bootloader", f"boot{node_id:02d}.bin")
    if not os.path.exists(p):
        raise FlashError(f"no prebuilt bootloader for node {node_id} ({p})")
    return p


def find_openocd():
    exe = os.environ.get("NODEFLASH_OPENOCD")
    if exe and os.path.exists(exe):
        return exe, None
    core = os.environ.get("PLATFORMIO_CORE_DIR", os.path.join(os.path.expanduser("~"), ".platformio"))
    pio = os.path.join(core, "packages", "tool-openocd")
    for name in ("openocd.exe", "openocd"):
        p = os.path.join(pio, "bin", name)
        if os.path.exists(p):
            return p, os.path.join(pio, "openocd", "scripts")
    p = shutil.which("openocd")
    return (p, None) if p else (None, None)


def find_cubeprog():
    exe = os.environ.get("NODEFLASH_CUBEPROG")
    if exe and os.path.exists(exe):
        return exe
    for name in ("STM32_Programmer_CLI", "STM32_Programmer_CLI.exe"):
        p = shutil.which(name)
        if p:
            return p
    default = r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
    return default if os.path.exists(default) else None


def tools_available():
    ocd, _ = find_openocd()
    return {"openocd": ocd, "stm32cubeprogrammer": find_cubeprog()}


def factory_flash(node_id: int, app_path: str, boot_path: str = None, log=print) -> dict:
    boot_path = boot_path or bootloader_for(node_id)
    boot = open(boot_path, "rb").read()
    app = open(app_path, "rb").read()
    if len(boot) > BOOT_MAX:
        raise FlashError(f"bootloader is {len(boot)} bytes, more than its {BOOT_MAX}-byte slot")
    problem = check_image(app_path, app)
    if problem:
        raise FlashError(f"{app_path}: {problem}")

    fd, rec_path = tempfile.mkstemp(suffix=".bin")
    os.write(fd, valid_record(app))
    os.close(fd)
    parts = [(boot_path, 0x08000000), (app_path, APP_START), (rec_path, RECORD_ADDR)]
    log(f"bootloader (node {node_id}) {len(boot)} bytes -> 0x08000000")
    log(f"application {len(app)} bytes, CRC {crc32(app):08X} -> 0x{APP_START:08X}")
    try:
        ocd, scripts = find_openocd()
        cube = find_cubeprog()
        if ocd:
            _openocd(ocd, scripts, parts)
            tool = "openocd"
        elif cube:
            _cubeprog(cube, parts)
            tool = "stm32cubeprogrammer"
        else:
            raise FlashError("neither OpenOCD nor STM32CubeProgrammer found (see README, 'Requirements')")
    finally:
        os.remove(rec_path)
    log(f"flashed and verified with {tool}")
    return {"node": node_id, "app_bytes": len(app), "app_crc": f"{crc32(app):08X}", "tool": tool}


def _tcl(p):
    return "{" + os.path.abspath(p).replace("\\", "/") + "}"


def _openocd(exe, scripts, parts):
    cmds = ["init", "reset halt"]
    for path, addr in parts:
        cmds += [f"flash write_image erase {_tcl(path)} 0x{addr:08X}", f"verify_image {_tcl(path)} 0x{addr:08X}"]
    cmds += ["reset run", "shutdown"]
    args = [exe] + (["-s", scripts] if scripts else []) + ["-f", "interface/stlink.cfg", "-f", "target/stm32g0x.cfg"]
    for c in cmds:
        args += ["-c", c]
    r = subprocess.run(args, capture_output=True, text=True)
    out = r.stdout + r.stderr
    if r.returncode != 0 or out.count("verified") < len(parts):
        tail = "\n".join(out.strip().splitlines()[-8:])
        raise FlashError(f"OpenOCD failed:\n{tail}")


def _cubeprog(exe, parts):
    args = [exe, "-c", "port=SWD", "mode=UR"]
    for path, addr in parts:
        args += ["-w", path, f"0x{addr:08X}", "-v"]
    args += ["-rst"]
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        raise FlashError("STM32CubeProgrammer failed:\n" + "\n".join((r.stdout + r.stderr).strip().splitlines()[-8:]))
