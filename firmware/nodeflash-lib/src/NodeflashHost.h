// NodeflashHost.h - ESP32 side of nodeflash: update STM32 node firmware over
// RS-485 from inside any ESP32 program (or the standalone gateway).
//
// Three layers, use what you need:
//
//   Transport   how bytes reach the bus. EspUartTransport drives an ESP32 UART
//               in RS-485 half-duplex mode; implement Transport yourself if your
//               program already owns the bus UART.
//   Host        the update engine: scan, info, enterBootloader, update, run.
//               Use it directly to update nodes from your own code (an image
//               from LittleFS, HTTP, ...).
//   Console     the USB line protocol the nodeflash PC tool speaks ("NF ..."
//               commands). Add it to your program and the PC tool can scan and
//               update nodes through it, next to your own console commands.
//
// Minimal standalone gateway:
//   nodeflash::EspUartTransport bus;
//   nodeflash::Host host(bus);
//   nodeflash::Console console(host, Serial);
//   void setup() { Serial.begin(460800); bus.begin(UART_NUM_1, 17, 18, 4); console.banner(); }
//   void loop()  { console.poll(); }
//
// Inside a program that has its own console and bus traffic:
//   console.onBusAcquire(pauseMyBusTask);  console.onBusRelease(resumeMyBusTask);
//   ... in your line handler:  if (console.handleLine(line)) return;   // it was an "NF ..." line
#pragma once
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <driver/uart.h>
#include "bus_packet.h"
#include "boot_protocol.h"

namespace nodeflash {

constexpr const char* HOST_VERSION = "1.0";

// ---------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------
class Transport {
 public:
  virtual ~Transport() {}
  virtual void write(const uint8_t* data, uint16_t len) = 0;            // returns after the last bit left
  virtual int read(uint8_t* buf, int max, uint32_t timeoutMs) = 0;      // bytes read (0 on timeout)
  virtual void flushInput() = 0;
};

class EspUartTransport : public Transport {
 public:
  // Installs the UART driver and sets RS-485 half-duplex mode (DE driven by the UART's RTS pin).
  void begin(uart_port_t port, int txPin, int rxPin, int dePin, uint32_t baud = BUS_BAUD);
  // Uses a UART driver your program has already installed and configured.
  void attach(uart_port_t port) { port_ = port; }
  void write(const uint8_t* data, uint16_t len) override;
  int read(uint8_t* buf, int max, uint32_t timeoutMs) override;
  void flushInput() override;

 private:
  uart_port_t port_ = UART_NUM_1;
};

// ---------------------------------------------------------------------------
// Host
// ---------------------------------------------------------------------------
struct NodeInfo {
  uint8_t  id = 0;
  bool     inApp = false;       // true: the application answered; false: the bootloader
  uint8_t  verMajor = 0, verMinor = 0;
  bool     appValid = false;    // bootloader's view (always true when inApp)
  uint32_t appSize = 0, appCrc = 0;
};

enum class Result : uint8_t {
  Ok, NoAnswer, NotLinked, BadImage, EraseFailed, WriteFailed, VerifyFailed, RunRefused, AppSilent, Stopped,
};
const char* resultText(Result r);

// progress(nodeId, percent, ctx) is called about every 10 %.
using ProgressFn = void (*)(uint8_t node, uint8_t percent, void* ctx);

class Host {
 public:
  explicit Host(Transport& t) : bus_(t) {}

  bool info(uint8_t node, NodeInfo& out, uint32_t timeoutMs = 200);
  // Fills out[] with the nodes that answer between from and to. Returns how many.
  int scan(uint8_t from, uint8_t to, NodeInfo* out, int maxOut, uint32_t timeoutMs = 30);
  Result enterBootloader(uint8_t node);
  Result run(uint8_t node);
  // Whole update: into the bootloader, erase, write, verify, start, check it answers.
  // stopAfterChunks > 0 cuts it off on purpose (test of the recovery path).
  Result update(uint8_t node, const uint8_t* image, uint32_t size, ProgressFn progress = nullptr,
                void* ctx = nullptr, uint32_t stopAfterChunks = 0);
  // Checks an image the same way the bootloader will (built for 0x08002000).
  static bool imageOk(const uint8_t* image, uint32_t size) { return bootCheckImageFull(image, size) == BOOT_OK; }

  const char* lastDetail() const { return detail_; }
  NodeInfo lastInfo() const { return lastInfo_; }

 private:
  bool transact(uint8_t node, uint8_t cmd, const void* payload, uint16_t len, uint32_t timeoutMs);
  bool status(uint8_t node, uint8_t cmd, const void* payload, uint16_t len, uint32_t timeoutMs, BootReply& r);
  Result fail(Result r, const char* fmt, ...);

  Transport& bus_;
  FrameParser parser_;
  uint8_t tx_[BOOT_CHUNK + 16];
  char detail_[96] = "";
  NodeInfo lastInfo_;
};

// ---------------------------------------------------------------------------
// Console: the line protocol the nodeflash PC tool speaks. Every command starts
// with "NF " so it can share a serial console with your own commands; every
// answer line starts with a keyword (NODE, SCAN, DONE, ERR, ...).
// ---------------------------------------------------------------------------
class Console {
 public:
  Console(Host& host, Stream& io) : host_(host), io_(io) {}
  void onBusAcquire(void (*fn)()) { acquire_ = fn; }   // e.g. pause your own bus traffic
  void onBusRelease(void (*fn)()) { release_ = fn; }
  void banner();
  // For programs with their own console: pass every complete line; returns true if it was "NF ...".
  // During "NF u" it reads the image bytes from the same stream itself.
  bool handleLine(const char* line);
  // For programs without a console of their own: reads the stream and handles lines.
  void poll();

 private:
  bool receiveImage(uint8_t node, uint32_t size, uint32_t crc, uint8_t*& image);
  Host& host_;
  Stream& io_;
  void (*acquire_)() = nullptr;
  void (*release_)() = nullptr;
  char line_[96];
  uint8_t len_ = 0;
};

}  // namespace nodeflash
#endif
