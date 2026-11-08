"""Rebuild the prebuilt firmware shipped in firmware/bin/ (for maintainers; users don't need this).

    python tools/build_firmware.py

Needs PlatformIO. Produces:
    firmware/bin/bootloader/boot01.bin ... boot20.bin   STM32 bootloader per node ID
    firmware/bin/gateway/gateway-esp32s3.bin            ESP32-S3 gateway, merged (flash at 0x0)
    firmware/bin/example_app/app_v1.bin, app_v2.bin     example application (node 1)
    firmware/bin/MANIFEST.txt                           sizes and CRCs
"""
import os
import shutil
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "firmware", "src")
BIN = os.path.join(ROOT, "firmware", "bin")
PIO_CORE = os.environ.get("PLATFORMIO_CORE_DIR", os.path.join(os.path.expanduser("~"), ".platformio"))


def run(args, cwd):
    print("  $", " ".join(args))
    r = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-3000:], r.stderr[-3000:])
        sys.exit(f"failed in {cwd}")


def main():
    for d in ("bootloader", "gateway", "example_app"):
        os.makedirs(os.path.join(BIN, d), exist_ok=True)

    print("bootloader x 20")
    envs = [f"boot{i:02d}" for i in range(1, 21)]
    run(["pio", "run"] + sum((["-e", e] for e in envs), []), os.path.join(SRC, "bootloader"))
    for e in envs:
        shutil.copy(os.path.join(SRC, "bootloader", ".pio", "build", e, "firmware.bin"),
                    os.path.join(BIN, "bootloader", f"{e}.bin"))

    print("gateway")
    gw = os.path.join(SRC, "gateway")
    run(["pio", "run"], gw)
    b = os.path.join(gw, ".pio", "build", "gateway")
    boot_app0 = os.path.join(PIO_CORE, "packages", "framework-arduinoespressif32", "tools", "partitions", "boot_app0.bin")
    esptool = os.path.join(PIO_CORE, "packages", "tool-esptoolpy", "esptool.py")
    run([sys.executable, esptool, "--chip", "esp32s3", "merge_bin", "-o",
         os.path.join(BIN, "gateway", "gateway-esp32s3.bin"), "--flash_mode", "dio", "--flash_size", "16MB",
         "0x0", os.path.join(b, "bootloader.bin"), "0x8000", os.path.join(b, "partitions.bin"),
         "0xe000", boot_app0, "0x10000", os.path.join(b, "firmware.bin")], gw)

    print("example app")
    run(["pio", "run"], os.path.join(SRC, "example_app"))
    for v in ("app_v1", "app_v2"):
        shutil.copy(os.path.join(SRC, "example_app", ".pio", "build", v, "firmware.bin"),
                    os.path.join(BIN, "example_app", f"{v}.bin"))

    lines = []
    for dirpath, _, files in sorted(os.walk(BIN)):
        for f in sorted(files):
            if f.endswith(".bin"):
                p = os.path.join(dirpath, f)
                d = open(p, "rb").read()
                lines.append(f"{os.path.relpath(p, BIN).replace(os.sep, '/'):40s} {len(d):8d} bytes  CRC-32 {zlib.crc32(d) & 0xFFFFFFFF:08X}")
    open(os.path.join(BIN, "MANIFEST.txt"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
