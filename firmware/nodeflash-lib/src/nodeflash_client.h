// nodeflash_client.h - makes any node application updatable over RS-485.
//
// What an application needs (see README "Linking your application"):
//   1. Build for 0x08002000, above the bootloader. In platformio.ini:
//        board_build.flash_offset = 0x2000
//        board_upload.offset_address = 0x08002000
//        build_flags = ... -I<path to nodeflash>/firmware/include
//   2. Hand every valid bus packet addressed to this node to nodeflashHandle().
//      It answers "info" and, on "enter", restarts into the bootloader. Every
//      other command is left to the application (returns false).
//
//   #include <Arduino.h>            // (or any CMSIS device header) first
//   #include "nodeflash_client.h"
//   ...
//   if (parser.feed(b, millis()) && (parser.dest == MY_ID))
//     if (!nodeflashHandle(parser.cmd, MY_ID, FW_MAJOR, FW_MINOR, sendBytes)) handleMyCommand();
//
// Packets use bus_packet.h framing; commands 0x30-0x3F are reserved for nodeflash.
#pragma once
#include <stdint.h>
#include "bus_packet.h"
#include "boot_protocol.h"

// send(data, len) must transmit the bytes on the RS-485 bus (drive DE, wait
// for the last bit, release DE). beforeReset, if given, runs just before the
// restart into the bootloader (switch LEDs off, flush logs, ...).
inline bool nodeflashHandle(uint8_t cmd, uint8_t nodeId, uint8_t verMajor, uint8_t verMinor,
                            void (*send)(const uint8_t* data, uint16_t len), void (*beforeReset)() = nullptr) {
  if ((cmd & 0xF0) != 0x30) return false;
  uint8_t out[48];
  switch (cmd) {
    case BOOT_CMD_INFO: {
      BootInfo bi{};
      bi.mode = BOOT_MODE_APP;
      bi.nodeId = nodeId;
      bi.verMajor = verMajor;
      bi.verMinor = verMinor;
      bi.appValid = 1;
      bi.appStart = SCB->VTOR;
      bi.appMax = BOOT_APP_MAX;
      send(out, buildFrame(out, ADDR_MASTER, BOOT_CMD_INFO | 0x80, (const uint8_t*)&bi, sizeof(bi)));
      return true;
    }
    case BOOT_CMD_ENTER: {
      BootReply r{BOOT_OK, 0};
      send(out, buildFrame(out, ADDR_MASTER, BOOT_CMD_ENTER | 0x80, (const uint8_t*)&r, sizeof(r)));
      if (beforeReset) beforeReset();
      __disable_irq();
      volatile uint32_t* handoff = (volatile uint32_t*)BOOT_HANDOFF_ADDR;
      handoff[0] = BOOT_HANDOFF_MAGIC;
      handoff[1] = ~BOOT_HANDOFF_MAGIC;
      __DSB();
      NVIC_SystemReset();
      return true;  // not reached
    }
    default:
      return true;  // erase/write/verify/run belong to the bootloader: never act on them here
  }
}
