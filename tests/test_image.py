"""Image checks and helpers (no hardware needed)."""
import os
import struct
import zlib

import pytest

from nodeflash.image import APP_START, RECORD_MAGIC, check_image, crc32, valid_record

BIN = os.path.join(os.path.dirname(__file__), "..", "firmware", "bin")


def read(*parts):
    return open(os.path.join(BIN, *parts), "rb").read()


@pytest.mark.parametrize("name", ["app_v1.bin", "app_v2.bin"])
def test_example_apps_are_accepted(name):
    assert check_image(name, read("example_app", name)) is None


def test_bootloader_file_is_refused():
    problem = check_image("boot01.bin", read("bootloader", "boot01.bin"))
    assert problem and "reset address" in problem


def test_image_built_for_0x08000000_is_refused():
    # Vector table that looks fine (reset address inside the app range) but
    # literal pools pointing into the first 8 KB, like a normal non-offset build.
    body = struct.pack("<II", 0x20009000, APP_START + 0x101) + struct.pack("<I", 0x08001234) * 4
    body += b"\x00" * (0x200 - len(body))
    problem = check_image("x.bin", body)
    assert problem and "bootloader area" in problem


def test_elf_and_hex_are_refused():
    data = read("example_app", "app_v1.bin")
    assert "not .elf" in check_image("app.elf", data)
    assert "not .elf" in check_image("app.hex", data)


def test_stack_pointer_outside_ram_is_refused():
    data = bytearray(read("example_app", "app_v1.bin"))
    data[0:4] = struct.pack("<I", 0x30000000)
    assert "stack pointer" in check_image("x.bin", bytes(data))


def test_too_small_and_too_big():
    assert check_image("x.bin", b"\x00" * 4)
    assert check_image("x.bin", b"\x00" * (0x1D800 + 8))


def test_crc32_matches_zlib():
    data = b"nodeflash"
    assert crc32(data) == zlib.crc32(data) & 0xFFFFFFFF


def test_valid_record_layout():
    data = read("example_app", "app_v1.bin")
    magic, size, crc, check = struct.unpack("<IIII", valid_record(data))
    assert magic == RECORD_MAGIC and check == (~RECORD_MAGIC & 0xFFFFFFFF)
    assert size == len(data) and crc == crc32(data)


def test_manifest_matches_files():
    for line in open(os.path.join(BIN, "MANIFEST.txt")):
        parts = line.split()   # path, size, "bytes", "CRC-32", crc
        path, size, crc = parts[0], int(parts[1]), parts[-1]
        data = read(*path.split("/"))
        assert len(data) == size, path
        assert f"{crc32(data):08X}" == crc, path
