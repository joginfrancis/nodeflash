// boot_protocol.h - firmware update over RS-485 (test, 2026-11-08).
//
// Uses the normal bus packet ([0xAA][DEST][CMD][LEN][payload][CRC-16]) from
// wall_protocol.h with these extra commands. Every command is addressed to one
// node and answered (CMD | 0x80), so the master always knows where it stands.
//
// Flash layout on the STM32G070RB (128 KB, 2 KB pages):
//   0x08000000  bootloader, 8 KB (pages 0-3), flashed once with the ST-Link
//   0x08002000  application, up to 118 KB (pages 4-62)
//   0x0801F800  last page: "application valid" record (size + CRC-32)
#pragma once
#include <stdint.h>

constexpr uint32_t BOOT_FLASH_BASE  = 0x08000000;
constexpr uint32_t BOOT_PAGE_SIZE   = 2048;
constexpr uint32_t BOOT_APP_START   = 0x08002000;
constexpr uint32_t BOOT_RECORD_ADDR = 0x0801F800;
constexpr uint32_t BOOT_APP_MAX     = BOOT_RECORD_ADDR - BOOT_APP_START;  // 120 KB - 2 KB
constexpr uint16_t BOOT_CHUNK       = 256;   // data bytes per write command (multiple of 8)

// The application asks for the bootloader by writing this word at the top of
// RAM and resetting. The bootloader keeps that word out of its own RAM.
constexpr uint32_t BOOT_HANDOFF_ADDR  = 0x20008FF0;
constexpr uint32_t BOOT_HANDOFF_MAGIC = 0xB007104DUL;

enum : uint8_t {
  BOOT_CMD_INFO   = 0x30,  // -> BootInfo (both bootloader and application answer)
  BOOT_CMD_ENTER  = 0x31,  // application: answer, then reset into the bootloader
  BOOT_CMD_ERASE  = 0x32,  // u32 image size; erases the record page and the pages needed
  BOOT_CMD_WRITE  = 0x33,  // u32 offset + data (multiple of 8, up to BOOT_CHUNK)
  BOOT_CMD_VERIFY = 0x34,  // u32 size + u32 CRC-32; on a match writes the "valid" record
  BOOT_CMD_RUN    = 0x35,  // start the application if it is valid
};

enum : uint8_t {
  BOOT_OK = 0, BOOT_ERR_LEN = 1, BOOT_ERR_ADDR = 2, BOOT_ERR_FLASH = 3,
  BOOT_ERR_CRC = 4, BOOT_ERR_NO_APP = 5,
  BOOT_ERR_IMAGE = 6,  // not built for 0x08002000 (wrong file or missing flash offset)
};

constexpr uint32_t BOOT_RAM_START = 0x20000000;
constexpr uint32_t BOOT_RAM_END   = 0x20009000;  // 36 KB

// Checks the first two words of an image (initial stack pointer, reset address)
// so a file built for 0x08000000, or not a firmware at all, is never marked
// valid. The same rule is in update.py.
inline uint8_t bootCheckImage(uint32_t sp, uint32_t reset, uint32_t size) {
  if (sp < BOOT_RAM_START + 1024 || sp > BOOT_RAM_END || (sp & 3)) return BOOT_ERR_IMAGE;
  if (!(reset & 1)) return BOOT_ERR_IMAGE;  // Thumb code: lowest bit set
  uint32_t entry = reset & ~1UL;
  if (entry < BOOT_APP_START + 8 || entry >= BOOT_APP_START + size) return BOOT_ERR_IMAGE;
  return BOOT_OK;
}

// The vector check alone isn't enough: an image built for 0x08000000 that is
// bigger than 8 KB usually has its reset address inside the application range
// too (seen 2026-11-08 with the normal node firmware). But such an image is
// full of addresses of its own first 8 KB (literal pools, function pointers),
// while an image built for 0x08002000 never points into the bootloader.
// Returns how many 32-bit words of the image point into 0x08000000-0x08001FFF.
inline uint32_t bootCountBootloaderRefs(const uint8_t* image, uint32_t size) {
  uint32_t count = 0;
  for (uint32_t i = 0; i + 4 <= size; i += 4) {
    uint32_t w = (uint32_t)image[i] | (uint32_t)image[i + 1] << 8 | (uint32_t)image[i + 2] << 16 |
                 (uint32_t)image[i + 3] << 24;
    if (w >= 0x08000000 && w < BOOT_APP_START) count++;
  }
  return count;
}

inline uint8_t bootCheckImageFull(const uint8_t* image, uint32_t size) {
  if (size < 8) return BOOT_ERR_IMAGE;
  const uint32_t sp = (uint32_t)image[0] | (uint32_t)image[1] << 8 | (uint32_t)image[2] << 16 | (uint32_t)image[3] << 24;
  const uint32_t rv = (uint32_t)image[4] | (uint32_t)image[5] << 8 | (uint32_t)image[6] << 16 | (uint32_t)image[7] << 24;
  if (bootCheckImage(sp, rv, size) != BOOT_OK) return BOOT_ERR_IMAGE;
  return bootCountBootloaderRefs(image, size) == 0 ? BOOT_OK : BOOT_ERR_IMAGE;
}

enum : uint8_t { BOOT_MODE_BOOTLOADER = 0, BOOT_MODE_APP = 1 };

#pragma pack(push, 1)
struct BootInfo {
  uint8_t  mode;          // BOOT_MODE_*
  uint8_t  nodeId;
  uint8_t  verMajor, verMinor;  // of whatever is answering
  uint8_t  appValid;      // bootloader's view: record present and CRC matches
  uint32_t appSize, appCrc;
  uint32_t appStart, appMax;
};
struct BootReply {        // reply to ERASE / WRITE / VERIFY / RUN / ENTER
  uint8_t  status;        // BOOT_OK or BOOT_ERR_*
  uint32_t value;         // WRITE: offset written; VERIFY: CRC the node computed
};
#pragma pack(pop)

// CRC-32 (zlib / IEEE: reflected 0xEDB88320, init and final XOR 0xFFFFFFFF).
inline uint32_t bootCrc32(uint32_t crc, const uint8_t* p, uint32_t n) {
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (uint8_t i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
  }
  return ~crc;
}
