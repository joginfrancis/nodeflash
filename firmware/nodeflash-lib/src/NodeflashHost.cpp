#if defined(ARDUINO_ARCH_ESP32)
#include "NodeflashHost.h"
#include <esp_heap_caps.h>
#include <stdarg.h>

namespace nodeflash {

// ---------------------------------------------------------------------------
// EspUartTransport
// ---------------------------------------------------------------------------
void EspUartTransport::begin(uart_port_t port, int txPin, int rxPin, int dePin, uint32_t baud) {
  port_ = port;
  uart_config_t cfg = {};
  cfg.baud_rate = (int)baud;
  cfg.data_bits = UART_DATA_8_BITS;
  cfg.parity = UART_PARITY_DISABLE;
  cfg.stop_bits = UART_STOP_BITS_1;
  cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uart_driver_install(port_, 2048, 0, 0, nullptr, 0);
  uart_param_config(port_, &cfg);
  uart_set_pin(port_, txPin, rxPin, dePin, UART_PIN_NO_CHANGE);
  uart_set_mode(port_, UART_MODE_RS485_HALF_DUPLEX);
  uart_set_rx_timeout(port_, 1);
}

void EspUartTransport::write(const uint8_t* data, uint16_t len) {
  uart_write_bytes(port_, (const char*)data, len);
  uart_wait_tx_done(port_, pdMS_TO_TICKS(50));
}

int EspUartTransport::read(uint8_t* buf, int max, uint32_t timeoutMs) {
  int n = uart_read_bytes(port_, buf, max, pdMS_TO_TICKS(timeoutMs));
  return n < 0 ? 0 : n;
}

void EspUartTransport::flushInput() { uart_flush_input(port_); }

// ---------------------------------------------------------------------------
// Host
// ---------------------------------------------------------------------------
const char* resultText(Result r) {
  switch (r) {
    case Result::Ok: return "ok";
    case Result::NoAnswer: return "no answer";
    case Result::NotLinked: return "application not linked to nodeflash";
    case Result::BadImage: return "image not built for 0x08002000";
    case Result::EraseFailed: return "erase failed";
    case Result::WriteFailed: return "write failed";
    case Result::VerifyFailed: return "verify failed";
    case Result::RunRefused: return "run refused";
    case Result::AppSilent: return "new application does not answer";
    case Result::Stopped: return "stopped on purpose";
  }
  return "?";
}

Result Host::fail(Result r, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(detail_, sizeof(detail_), fmt, ap);
  va_end(ap);
  return r;
}

bool Host::transact(uint8_t node, uint8_t cmd, const void* payload, uint16_t len, uint32_t timeoutMs) {
  bus_.flushInput();
  parser_.reset();
  bus_.write(tx_, buildFrame(tx_, node, cmd, (const uint8_t*)payload, len));
  uint32_t t0 = millis();
  uint8_t buf[64];
  while (millis() - t0 < timeoutMs) {
    int got = bus_.read(buf, sizeof(buf), 1);
    for (int i = 0; i < got; i++)
      if (parser_.feed(buf[i], millis()) && parser_.dest == ADDR_MASTER && parser_.cmd == (cmd | 0x80)) return true;
  }
  return false;
}

bool Host::status(uint8_t node, uint8_t cmd, const void* payload, uint16_t len, uint32_t timeoutMs, BootReply& r) {
  r = BootReply{0xFF, 0};
  if (!transact(node, cmd, payload, len, timeoutMs) || parser_.len != sizeof(BootReply)) return false;
  memcpy(&r, parser_.payload, sizeof(BootReply));
  return true;
}

bool Host::info(uint8_t node, NodeInfo& out, uint32_t timeoutMs) {
  if (!transact(node, BOOT_CMD_INFO, nullptr, 0, timeoutMs) || parser_.len != sizeof(BootInfo)) return false;
  BootInfo bi;
  memcpy(&bi, parser_.payload, sizeof(bi));
  out.id = node;
  out.inApp = bi.mode == BOOT_MODE_APP;
  out.verMajor = bi.verMajor;
  out.verMinor = bi.verMinor;
  out.appValid = bi.appValid;
  out.appSize = bi.appSize;
  out.appCrc = bi.appCrc;
  lastInfo_ = out;
  return true;
}

int Host::scan(uint8_t from, uint8_t to, NodeInfo* out, int maxOut, uint32_t timeoutMs) {
  int found = 0;
  for (int id = from; id <= to && found < maxOut; id++)
    if (info((uint8_t)id, out[found], timeoutMs)) found++;
  return found;
}

Result Host::enterBootloader(uint8_t node) {
  NodeInfo ni;
  if (!info(node, ni)) return fail(Result::NoAnswer, "node %u does not answer", node);
  if (!ni.inApp) return Result::Ok;
  BootReply r;
  if (!status(node, BOOT_CMD_ENTER, nullptr, 0, 200, r))
    return fail(Result::NotLinked, "node %u application did not accept 'enter bootloader'", node);
  for (int tries = 0; tries < 20; tries++) {  // the bootloader checks the application CRC first
    delay(100);
    if (info(node, ni, 100) && !ni.inApp) return Result::Ok;
  }
  return fail(Result::NoAnswer, "node %u did not come back in the bootloader", node);
}

Result Host::run(uint8_t node) {
  BootReply r;
  if (!status(node, BOOT_CMD_RUN, nullptr, 0, 2000, r))
    return fail(Result::NoAnswer, "node %u no answer to run (application already running?)", node);
  if (r.status != BOOT_OK) return fail(Result::RunRefused, "node %u has no valid application", node);
  return Result::Ok;
}

Result Host::update(uint8_t node, const uint8_t* image, uint32_t size, ProgressFn progress, void* ctx,
                    uint32_t stopAfterChunks) {
  detail_[0] = 0;
  if (!imageOk(image, size)) return fail(Result::BadImage, "image not built for 0x%08lX", (unsigned long)BOOT_APP_START);
  Result res = enterBootloader(node);
  if (res != Result::Ok) return res;

  BootReply r;
  if (!status(node, BOOT_CMD_ERASE, &size, 4, 5000, r) || r.status != BOOT_OK)
    return fail(Result::EraseFailed, "node %u erase failed (status %u)", node, r.status);

  uint8_t chunk[4 + BOOT_CHUNK];
  uint32_t chunks = 0;
  int lastDecile = -1;
  for (uint32_t off = 0; off < size; off += BOOT_CHUNK) {
    if (stopAfterChunks && chunks >= stopAfterChunks)
      return fail(Result::Stopped, "node %u stopped on purpose after %lu chunks", node, (unsigned long)chunks);
    uint32_t n = size - off < BOOT_CHUNK ? size - off : BOOT_CHUNK;
    uint32_t padded = (n + 7) & ~7u;
    memcpy(chunk, &off, 4);
    memset(chunk + 4, 0xFF, padded);
    memcpy(chunk + 4, image + off, n);
    bool ok = false;
    for (int attempt = 0; attempt < 3 && !ok; attempt++)
      ok = status(node, BOOT_CMD_WRITE, chunk, 4 + padded, 100, r) && r.status == BOOT_OK && r.value == off;
    if (!ok) return fail(Result::WriteFailed, "node %u write failed at offset %lu", node, (unsigned long)off);
    chunks++;
    int pct = (int)((off + n) * 100 / size);
    if (progress && pct / 10 != lastDecile) { lastDecile = pct / 10; progress(node, pct, ctx); }
  }

  uint32_t v[2] = {size, bootCrc32(0, image, size)};
  if (!status(node, BOOT_CMD_VERIFY, v, 8, 3000, r) || r.status != BOOT_OK)
    return fail(Result::VerifyFailed, "node %u verify failed (status %u, node CRC %08lX)", node, r.status,
                (unsigned long)r.value);
  res = run(node);
  if (res != Result::Ok) return res;
  NodeInfo ni;
  for (int tries = 0; tries < 10; tries++) {
    delay(100);
    if (info(node, ni, 100) && ni.inApp) return Result::Ok;
  }
  return fail(Result::AppSilent, "node %u new application does not answer (is it linked to nodeflash?)", node);
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------
static void printNode(Stream& io, const NodeInfo& n) {
  if (n.inApp)
    io.printf("NODE %u APP %u.%u\n", n.id, n.verMajor, n.verMinor);
  else
    io.printf("NODE %u BOOT %u.%u %s %lu %08lX\n", n.id, n.verMajor, n.verMinor, n.appValid ? "VALID" : "INVALID",
              (unsigned long)n.appSize, (unsigned long)n.appCrc);
}

void Console::banner() {
  io_.printf("NODEFLASH HOST %s BUS %lu\n", HOST_VERSION, (unsigned long)BUS_BAUD);
}

bool Console::receiveImage(uint8_t node, uint32_t size, uint32_t crc, uint8_t*& image) {
  image = nullptr;
  if (size < 8 || size > BOOT_APP_MAX) { io_.printf("ERR %u bad size\n", node); return false; }
  image = (uint8_t*)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!image) image = (uint8_t*)malloc(size);
  if (!image) { io_.printf("ERR %u out of memory for %lu bytes\n", node, (unsigned long)size); return false; }
  io_.println("READY");
  uint32_t got = 0, last = millis();
  while (got < size && millis() - last < 3000) {
    int a = io_.available();
    if (a > 0) { got += io_.readBytes(image + got, min((uint32_t)a, size - got)); last = millis(); }
    else delay(1);
  }
  if (got < size) { io_.printf("ERR %u only %lu of %lu bytes arrived\n", node, (unsigned long)got, (unsigned long)size); return false; }
  if (bootCrc32(0, image, size) != crc) { io_.printf("ERR %u image CRC mismatch on the USB link\n", node); return false; }
  if (!Host::imageOk(image, size)) { io_.printf("ERR %u image not built for 0x%08lX\n", node, (unsigned long)BOOT_APP_START); return false; }
  return true;
}

struct ProgressCtx { Stream* io; };
static void printProgress(uint8_t node, uint8_t pct, void* ctx) {
  static_cast<ProgressCtx*>(ctx)->io->printf("PROGRESS %u %u\n", node, pct);
}

bool Console::handleLine(const char* line) {
  if (strncmp(line, "NF ", 3) != 0 && strcmp(line, "NF") != 0) return false;
  const char* p = line[2] ? line + 3 : line + 2;
  while (*p == ' ') p++;
  char cmd = *p ? *p++ : '?';
  unsigned long a = 0, b = 0, c = 0, d = 0;
  int args = sscanf(p, "%lu %lu %lx %lu", &a, &b, &c, &d);

  if (cmd == '?') { banner(); return true; }
  if (acquire_) acquire_();
  NodeInfo ni;
  switch (cmd) {
    case 's': {
      if (args < 2) { a = 1; b = 20; }
      NodeInfo found[32];
      int n = host_.scan(a, b, found, 32);
      for (int i = 0; i < n; i++) printNode(io_, found[i]);
      io_.printf("SCAN %d\n", n);
      break;
    }
    case 'i':
      if (host_.info(a, ni)) printNode(io_, ni); else io_.printf("NONE %lu\n", a);
      break;
    case 'b':
      if (host_.enterBootloader(a) == Result::Ok) io_.printf("OK BOOT %lu\n", a);
      else io_.printf("ERR %lu %s\n", a, host_.lastDetail());
      break;
    case 'r':
      if (host_.run(a) == Result::Ok) io_.printf("OK RUN %lu\n", a);
      else io_.printf("ERR %lu %s\n", a, host_.lastDetail());
      break;
    case 'u': {  // NF u <id> <size> <crc hex> [stop]
      uint8_t* image = nullptr;
      if (args >= 3 && receiveImage(a, b, c, image)) {
        uint32_t t0 = millis();
        ProgressCtx pc{&io_};
        Result r = host_.update(a, image, b, printProgress, &pc, args >= 4 ? d : 0);
        if (r == Result::Ok) {
          if (host_.info(a, ni)) printNode(io_, ni);
          io_.printf("DONE %lu %lu\n", a, (unsigned long)(millis() - t0));
        } else {
          io_.printf("ERR %lu %s\n", a, host_.lastDetail()[0] ? host_.lastDetail() : resultText(r));
        }
      } else if (args < 3) {
        io_.printf("ERR %lu usage: NF u <id> <size> <crc hex> [stop]\n", a);
      }
      free(image);
      break;
    }
    case 'w': {  // NF w <id> <seconds>: rescue. Ask the node for info every few ms; a bootloader
                 // that hears it in its 100 ms start-up window stays. Power-cycle the node meanwhile.
      uint32_t until = millis() + (args >= 2 ? b : 30) * 1000UL;
      bool got = false;
      while (!got && (int32_t)(millis() - until) < 0) got = host_.info(a, ni, 15) && !ni.inApp;
      if (got) { printNode(io_, ni); io_.printf("OK RESCUE %lu\n", a); }
      else io_.printf("ERR %lu no bootloader answered (power-cycle the node while this runs)\n", a);
      break;
    }
    default:
      io_.printf("ERR 0 unknown command '%c'\n", cmd);
      break;
  }
  if (release_) release_();
  return true;
}

void Console::poll() {
  while (io_.available()) {
    char c = io_.read();
    if (c != '\n' && c != '\r') { if (len_ < sizeof(line_) - 1) line_[len_++] = c; continue; }
    if (!len_) continue;
    line_[len_] = 0;
    len_ = 0;
    if (!handleLine(line_)) io_.println("ERR 0 commands start with NF (try: NF ?)");
  }
}

}  // namespace nodeflash
#endif
