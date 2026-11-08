// Example application linked to nodeflash: updatable over RS-485. Runs at 0x08002000, above the
// bootloader. Two builds (env app_v1 / app_v2) differ only in APP_VERSION so an
// update is easy to see:
//   v1: PB12 blinks slowly (1 Hz)      v2: PB12 blinks fast (5 Hz)
// The console (USART2, the ST-Link virtual COM port, 115200) prints the version every 2 s.
//
// Like the real node it uses the 64 MHz HSE clock, the DMA RS-485 receiver and
// a 500 ms watchdog, so the test covers starting from the bootloader with all
// of those. On the bus it answers BOOT_CMD_INFO, and on BOOT_CMD_ENTER it
// resets into the bootloader.
#include <Arduino.h>
#include <IWatchdog.h>
#include "STM32G070ClockConfig.h"
#include "nodeflash_client.h"
#include "rs485.h"

#ifndef APP_VERSION
#define APP_VERSION 1
#endif
#ifndef NODE_ID
#define NODE_ID 1
#endif

static FrameParser parser;

static void busSend(const uint8_t* data, uint16_t len) { rs485Write(data, len); }

static void beforeUpdate() {
  Serial.println("update requested: restarting into the bootloader");
  Serial.flush();
}

static void handle() {
  if (parser.dest != NODE_ID && parser.dest != ADDR_BROADCAST) return;
  // nodeflash commands (info, enter bootloader) first; everything else is the app's.
  if (parser.dest == NODE_ID && nodeflashHandle(parser.cmd, NODE_ID, APP_VERSION, 0, busSend, beforeUpdate)) return;
  // ... the application's own commands would go here (polls, frames, config) ...
}

void setup() {
  pinMode(PB12, OUTPUT);
  Serial.begin(115200);
  rs485Begin(BUS_BAUD);
  IWatchdog.begin(500000);
  Serial.printf("\r\nAPP v%u node %u started, vector table at 0x%08lX, clock %lu MHz\r\n", APP_VERSION, NODE_ID,
                (unsigned long)SCB->VTOR, (unsigned long)(SystemCoreClock / 1000000));
}

void loop() {
  IWatchdog.reload();
  int b;
  while ((b = rs485Read()) >= 0)
    if (parser.feed((uint8_t)b, millis())) handle();

  static uint32_t lastBlink = 0, lastPrint = 0;
  uint32_t now = millis();
  const uint32_t half = APP_VERSION == 1 ? 500 : 100;
  if (now - lastBlink >= half) { lastBlink = now; digitalWrite(PB12, !digitalRead(PB12)); }
  if (now - lastPrint >= 2000) {
    lastPrint = now;
    Serial.printf("APP v%u running, uptime %lu s\r\n", APP_VERSION, (unsigned long)(now / 1000));
  }
}
