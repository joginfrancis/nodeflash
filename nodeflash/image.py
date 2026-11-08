"""Shared checks for node application images (used by the whole nodeflash tool).

Same rules as bootCheckImage() in common/boot_protocol.h, so a wrong file is
refused on the PC, again on the ESP32, and again by the node's bootloader.
"""
import struct
import zlib

APP_START = 0x08002000
RECORD_ADDR = 0x0801F800
APP_MAX = RECORD_ADDR - APP_START
RAM_START, RAM_END = 0x20000000, 0x20009000
RECORD_MAGIC = 0x41505056  # "APPV"


def check_image(path, data):
    """Returns None if data looks like a node application built for APP_START, else the reason."""
    if path.lower().endswith((".elf", ".hex")):
        return "send the .bin file, not .elf or .hex"
    if len(data) < 8 or len(data) > APP_MAX:
        return f"size {len(data)} bytes is outside 8..{APP_MAX}"
    sp, reset = struct.unpack_from("<II", data)
    if not (RAM_START + 1024 <= sp <= RAM_END) or sp & 3:
        return f"stack pointer 0x{sp:08X} is not in RAM: not a node firmware"
    entry = reset & ~1
    if not reset & 1 or not (APP_START + 8 <= entry < APP_START + len(data)):
        return (f"reset address 0x{reset:08X} is outside 0x{APP_START:08X}.. : "
                "built without board_build.flash_offset = 0x2000 (or the bootloader itself)?")
    # An image built for 0x08000000 is full of addresses of its own first 8 KB;
    # one built for APP_START never points into the bootloader.
    words = struct.unpack_from(f"<{len(data) // 4}I", data)
    refs = sum(1 for w in words if 0x08000000 <= w < APP_START)
    if refs:
        return (f"{refs} addresses point into the bootloader area (0x08000000-0x{APP_START - 1:08X}): "
                "built for 0x08000000, without board_build.flash_offset = 0x2000?")
    return None


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def valid_record(data):
    """The 16-byte "application valid" record the bootloader writes after a good update."""
    return struct.pack("<IIII", RECORD_MAGIC, len(data), crc32(data), ~RECORD_MAGIC & 0xFFFFFFFF)
