// RS-485 bootloader test for the node (STM32G070RB). Bare metal, no HAL, no
// interrupts; runs on the reset clock (HSI 16 MHz) so the application starts
// from the same clock state as after a reset.
//
// On reset:
//   - the application asked for the bootloader (word at BOOT_HANDOFF_ADDR) -> stay
//   - no valid application (record missing or CRC wrong)                    -> stay
//   - otherwise listen 100 ms; a packet for this node in that time          -> stay
//   - else start the application at 0x08002000
// While staying: PB12 blinks fast, the console (COM5, 115200) logs, and the
// node answers BOOT_CMD_* packets on the RS-485 bus (PB0 TX / PB2 RX swapped,
// DE on PB1), at the wall's bus speed.
#include <stdint.h>
#include <string.h>
#include "stm32g070xx.h"
#include "bus_packet.h"
#include "boot_protocol.h"

#ifndef NODE_ID
#define NODE_ID 1
#endif
constexpr uint8_t  BL_MAJOR = 1, BL_MINOR = 1;  // 1.1: cached state, so INFO answers in microseconds
constexpr uint32_t CLK_HZ = 16000000;
constexpr uint32_t LISTEN_MS = 100;
constexpr uint16_t RX_BUF_SIZE = 1024;

// The USART needs at least 16 clocks per bit (BRR >= 16). At 16 MHz that caps
// the bus at 1 Mbaud; a faster BUS_BAUD must fail here, not silently on the wall.
static_assert(CLK_HZ / BUS_BAUD >= 16, "bootloader at 16 MHz can't run this bus speed: raise its clock first");
static_assert(CLK_HZ % BUS_BAUD == 0 || CLK_HZ / BUS_BAUD >= 100, "bus speed not exact at 16 MHz");

// ---------------------------------------------------------------------------
// Time, watchdog, console
// ---------------------------------------------------------------------------
static uint32_t ms = 0;
static void tick() {
  if (SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk) ms++;
  IWDG->KR = 0xAAAA;  // refresh; harmless when the watchdog was never started
}

static void logChar(char c) {
  while (!(USART2->ISR & USART_ISR_TXE_TXFNF)) {}
  USART2->TDR = c;
}
static void logStr(const char* s) { while (*s) { if (*s == '\n') logChar('\r'); logChar(*s++); } }
static void logHex(uint32_t v) {
  logStr("0x");
  for (int i = 28; i >= 0; i -= 4) logChar("0123456789ABCDEF"[(v >> i) & 0xF]);
}
static void logDec(uint32_t v) {
  char b[11]; int i = 10; b[i] = 0;
  do { b[--i] = '0' + v % 10; v /= 10; } while (v);
  logStr(&b[i]);
}
static void logFlush() { while (!(USART2->ISR & USART_ISR_TC)) {} }

// ---------------------------------------------------------------------------
// Hardware setup
// ---------------------------------------------------------------------------
static uint8_t  rxBuf[RX_BUF_SIZE];
static uint16_t rxTail = 0;

static void setPinMode(GPIO_TypeDef* g, uint8_t pin, uint32_t mode, uint32_t af) {
  g->MODER = (g->MODER & ~(3UL << (pin * 2))) | (mode << (pin * 2));
  volatile uint32_t& afr = g->AFR[pin >> 3];
  afr = (afr & ~(0xFUL << ((pin & 7) * 4))) | (af << ((pin & 7) * 4));
}

static void hwBegin() {
  RCC->IOPENR |= RCC_IOPENR_GPIOAEN | RCC_IOPENR_GPIOBEN;
  RCC->APBENR1 |= RCC_APBENR1_USART2EN | RCC_APBENR1_USART3EN;
  RCC->AHBENR |= RCC_AHBENR_DMA1EN;
  (void)RCC->AHBENR;

  // DE low (listen) before the transceiver pins become active
  GPIOB->BRR = 1u << 1;
  setPinMode(GPIOB, 1, 1, 0);   // PB1 DE + /RE, output
  setPinMode(GPIOB, 12, 1, 0);  // PB12 LED, output
  setPinMode(GPIOB, 0, 2, 4);   // PB0 USART3 (TX after swap)
  setPinMode(GPIOB, 2, 2, 4);   // PB2 USART3 (RX after swap)
  setPinMode(GPIOA, 2, 2, 1);   // PA2 USART2 TX -> ST-Link COM port
  setPinMode(GPIOA, 3, 2, 1);   // PA3 USART2 RX

  USART2->CR1 = 0;
  USART2->BRR = CLK_HZ / 115200;
  USART2->CR1 = USART_CR1_TE | USART_CR1_UE;

  USART3->CR1 = 0;
  USART3->CR2 = USART_CR2_SWAP;
  USART3->BRR = CLK_HZ / BUS_BAUD;  // 16 at 1 Mbaud
  USART3->CR3 = USART_CR3_DMAR | USART_CR3_OVRDIS;

  DMA1_Channel1->CCR = 0;
  DMAMUX1_Channel0->CCR = 0x36;  // USART3 RX request
  DMA1_Channel1->CPAR = (uint32_t)&USART3->RDR;
  DMA1_Channel1->CMAR = (uint32_t)rxBuf;
  DMA1_Channel1->CNDTR = RX_BUF_SIZE;
  DMA1_Channel1->CCR = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_PL_1 | DMA_CCR_EN;
  USART3->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

  SysTick->LOAD = CLK_HZ / 1000 - 1;
  SysTick->VAL = 0;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk;
}

static int busRead() {
  uint16_t head = RX_BUF_SIZE - DMA1_Channel1->CNDTR;
  if (head >= RX_BUF_SIZE) head = 0;
  if (rxTail == head) return -1;
  uint8_t b = rxBuf[rxTail];
  rxTail = (rxTail + 1) % RX_BUF_SIZE;
  return b;
}

static void busWrite(const uint8_t* d, uint16_t n) {
  GPIOB->BSRR = 1u << 1;  // DE high
  for (uint16_t i = 0; i < n; i++) {
    while (!(USART3->ISR & USART_ISR_TXE_TXFNF)) {}
    USART3->TDR = d[i];
  }
  while (!(USART3->ISR & USART_ISR_TC)) {}
  GPIOB->BRR = 1u << 1;   // DE low
}

static void reply(uint8_t cmd, const void* payload, uint16_t len) {
  static uint8_t out[64];
  uint16_t n = buildFrame(out, ADDR_MASTER, cmd, (const uint8_t*)payload, len);
  busWrite(out, n);
}

static void replyStatus(uint8_t cmd, uint8_t status, uint32_t value) {
  BootReply r{status, value};
  reply(cmd | 0x80, &r, sizeof(r));
}

// ---------------------------------------------------------------------------
// Flash
// ---------------------------------------------------------------------------
constexpr uint32_t FLASH_ERRS = FLASH_SR_OPERR | FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR |
                                FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR | FLASH_SR_FASTERR;

static void flashWait() { while (FLASH->SR & (FLASH_SR_BSY1 | FLASH_SR_CFGBSY)) {} }

static void flashUnlock() {
  if (FLASH->CR & FLASH_CR_LOCK) { FLASH->KEYR = 0x45670123; FLASH->KEYR = 0xCDEF89AB; }
}
static void flashLock() { FLASH->CR |= FLASH_CR_LOCK; }

static bool flashErasePage(uint32_t addr) {
  uint32_t page = (addr - BOOT_FLASH_BASE) / BOOT_PAGE_SIZE;
  flashWait();
  FLASH->SR = FLASH_ERRS | FLASH_SR_EOP;
  FLASH->CR = (FLASH->CR & ~FLASH_CR_PNB) | FLASH_CR_PER | (page << FLASH_CR_PNB_Pos);
  FLASH->CR |= FLASH_CR_STRT;
  flashWait();
  FLASH->CR &= ~FLASH_CR_PER;
  tick();
  if (FLASH->SR & FLASH_ERRS) return false;
  for (uint32_t a = addr; a < addr + BOOT_PAGE_SIZE; a += 4)
    if (*(volatile uint32_t*)a != 0xFFFFFFFF) return false;
  return true;
}

// Writes n bytes (multiple of 8) at a double-word aligned address.
static bool flashWrite(uint32_t addr, const uint8_t* d, uint32_t n) {
  for (uint32_t i = 0; i < n; i += 8) {
    uint32_t lo, hi;
    memcpy(&lo, d + i, 4);
    memcpy(&hi, d + i + 4, 4);
    flashWait();
    FLASH->SR = FLASH_ERRS | FLASH_SR_EOP;
    FLASH->CR |= FLASH_CR_PG;
    *(volatile uint32_t*)(addr + i) = lo;
    *(volatile uint32_t*)(addr + i + 4) = hi;
    flashWait();
    FLASH->CR &= ~FLASH_CR_PG;
    if (FLASH->SR & FLASH_ERRS) return false;
  }
  return memcmp((const void*)addr, d, n) == 0;
}

struct Record { uint32_t magic, size, crc, check; };
constexpr uint32_t RECORD_MAGIC = 0x41505056;  // "APPV"

static uint32_t flashCrc(uint32_t size) {
  uint32_t crc = 0;
  for (uint32_t off = 0; off < size; off += 1024) {
    uint32_t n = size - off < 1024 ? size - off : 1024;
    crc = bootCrc32(crc, (const uint8_t*)(BOOT_APP_START + off), n);
    tick();
  }
  return crc;
}

static bool appValid(uint32_t* size = nullptr, uint32_t* crc = nullptr) {
  const Record* r = (const Record*)BOOT_RECORD_ADDR;
  if (size) *size = 0;
  if (crc) *crc = 0;
  if (r->magic != RECORD_MAGIC || r->check != ~RECORD_MAGIC) return false;
  if (r->size < 8 || r->size > BOOT_APP_MAX) return false;
  if (size) *size = r->size;
  if (crc) *crc = r->crc;
  if (bootCheckImageFull((const uint8_t*)BOOT_APP_START, r->size) != BOOT_OK) return false;
  return flashCrc(r->size) == r->crc;
}

// The CRC over the application takes ~0.2 s per 25 KB at 16 MHz, so the result
// is kept and only recomputed when flash changes. INFO then answers at once,
// which lets the gateway scan 20 addresses with a short timeout.
struct AppState { bool valid; uint32_t size, crc; };
static AppState app;
static void refreshAppState() { app.valid = appValid(&app.size, &app.crc); }

// ---------------------------------------------------------------------------
// Start the application
// ---------------------------------------------------------------------------
static void jumpToApp() {
  logStr("starting application\n");
  logFlush();
  SysTick->CTRL = 0;
  SysTick->VAL = 0;
  USART3->CR1 = 0;
  USART2->CR1 = 0;
  DMA1_Channel1->CCR = 0;
  DMAMUX1_Channel0->CCR = 0;
  RCC->APBRSTR1 |= RCC_APBRSTR1_USART2RST | RCC_APBRSTR1_USART3RST;
  RCC->APBRSTR1 &= ~(RCC_APBRSTR1_USART2RST | RCC_APBRSTR1_USART3RST);
  RCC->AHBRSTR |= RCC_AHBRSTR_DMA1RST;
  RCC->AHBRSTR &= ~RCC_AHBRSTR_DMA1RST;
  RCC->APBENR1 &= ~(RCC_APBENR1_USART2EN | RCC_APBENR1_USART3EN);
  RCC->AHBENR &= ~RCC_AHBENR_DMA1EN;
  // GPIO is left as it is: DE stays low (listening) until the application takes it.

  SCB->VTOR = BOOT_APP_START;
  uint32_t sp = *(const uint32_t*)BOOT_APP_START;
  uint32_t pc = *(const uint32_t*)(BOOT_APP_START + 4);
  __asm volatile("msr msp, %0\n\tbx %1\n" : : "r"(sp), "r"(pc));
  while (true) {}
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
static FrameParser parser;
static bool stay = false;
static uint32_t writes = 0;

static uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

static void handle() {
  if (parser.dest != NODE_ID) return;
  // Only update commands keep the bootloader running; normal wall traffic (polls,
  // frames) during the start-up window must not hold a node in the bootloader.
  if ((parser.cmd & 0xF0) != 0x30) return;
  stay = true;
  const uint8_t* p = parser.payload;
  switch (parser.cmd) {
    case BOOT_CMD_INFO: {
      BootInfo bi{};
      bi.mode = BOOT_MODE_BOOTLOADER; bi.nodeId = NODE_ID;
      bi.verMajor = BL_MAJOR; bi.verMinor = BL_MINOR;
      bi.appValid = app.valid;  // BootInfo is packed: assign, never pass &bi.field to a uint32_t*
      bi.appSize = app.size;
      bi.appCrc = app.crc;
      bi.appStart = BOOT_APP_START; bi.appMax = BOOT_APP_MAX;
      reply(BOOT_CMD_INFO | 0x80, &bi, sizeof(bi));
      break;
    }
    case BOOT_CMD_ENTER:
      replyStatus(BOOT_CMD_ENTER, BOOT_OK, 0);
      break;
    case BOOT_CMD_ERASE: {
      if (parser.len != 4) { replyStatus(BOOT_CMD_ERASE, BOOT_ERR_LEN, 0); break; }
      uint32_t size = rd32(p);
      if (size == 0 || size > BOOT_APP_MAX) { replyStatus(BOOT_CMD_ERASE, BOOT_ERR_ADDR, size); break; }
      uint32_t t0 = ms;
      flashUnlock();
      bool ok = flashErasePage(BOOT_RECORD_ADDR);  // invalidate first: a cut-off update stays here
      uint32_t pages = (size + BOOT_PAGE_SIZE - 1) / BOOT_PAGE_SIZE;
      for (uint32_t i = 0; ok && i < pages; i++) ok = flashErasePage(BOOT_APP_START + i * BOOT_PAGE_SIZE);
      flashLock();
      app = AppState{false, 0, 0};
      writes = 0;
      logStr("erase "); logDec(pages); logStr(" pages: "); logStr(ok ? "ok, " : "FAILED, ");
      logDec(ms - t0); logStr(" ms\n");
      replyStatus(BOOT_CMD_ERASE, ok ? BOOT_OK : BOOT_ERR_FLASH, pages);
      break;
    }
    case BOOT_CMD_WRITE: {
      uint16_t n = parser.len - 4;
      uint32_t off = rd32(p);
      if (parser.len < 12 || n > BOOT_CHUNK || n % 8) { replyStatus(BOOT_CMD_WRITE, BOOT_ERR_LEN, off); break; }
      if (off % 8 || off + n > BOOT_APP_MAX) { replyStatus(BOOT_CMD_WRITE, BOOT_ERR_ADDR, off); break; }
      flashUnlock();
      bool ok = flashWrite(BOOT_APP_START + off, p + 4, n);
      flashLock();
      if (ok && (++writes % 32) == 0) { logStr("written "); logDec(off + n); logStr(" bytes\n"); }
      if (!ok) { logStr("write FAILED at "); logHex(off); logStr("\n"); }
      replyStatus(BOOT_CMD_WRITE, ok ? BOOT_OK : BOOT_ERR_FLASH, off);
      break;
    }
    case BOOT_CMD_VERIFY: {
      if (parser.len != 8) { replyStatus(BOOT_CMD_VERIFY, BOOT_ERR_LEN, 0); break; }
      uint32_t size = rd32(p), want = rd32(p + 4);
      if (size < 8 || size > BOOT_APP_MAX) { replyStatus(BOOT_CMD_VERIFY, BOOT_ERR_ADDR, 0); break; }
      uint32_t t0 = ms;
      uint32_t got = flashCrc(size);
      const uint32_t* vec = (const uint32_t*)BOOT_APP_START;
      uint8_t image = bootCheckImageFull((const uint8_t*)BOOT_APP_START, size);
      if (got == want && image != BOOT_OK) {
        logStr("verify: CRC ok but the image is not built for "); logHex(BOOT_APP_START);
        logStr(" (stack "); logHex(vec[0]); logStr(", reset "); logHex(vec[1]); logStr("): NOT marked valid\n");
        replyStatus(BOOT_CMD_VERIFY, BOOT_ERR_IMAGE, got);
        break;
      }
      bool ok = got == want;
      if (ok) {
        Record r{RECORD_MAGIC, size, got, ~RECORD_MAGIC};
        flashUnlock();
        ok = flashErasePage(BOOT_RECORD_ADDR) && flashWrite(BOOT_RECORD_ADDR, (const uint8_t*)&r, sizeof(r));
        flashLock();
      }
      refreshAppState();
      logStr("verify "); logDec(size); logStr(" bytes, CRC "); logHex(got);
      logStr(ok ? " matches, marked valid (" : " WRONG ("); logDec(ms - t0); logStr(" ms)\n");
      replyStatus(BOOT_CMD_VERIFY, got != want ? BOOT_ERR_CRC : ok ? BOOT_OK : BOOT_ERR_FLASH, got);
      break;
    }
    case BOOT_CMD_RUN:
      if (!app.valid) { replyStatus(BOOT_CMD_RUN, BOOT_ERR_NO_APP, 0); logStr("run refused: no valid application\n"); break; }
      replyStatus(BOOT_CMD_RUN, BOOT_OK, 0);
      jumpToApp();
      break;
    default:
      break;  // unknown commands are ignored
  }
}

static uint32_t rxCount = 0;

static void pollBus() {
  int b;
  while ((b = busRead()) >= 0) {
    rxCount++;
    if (parser.feed((uint8_t)b, ms)) handle();
  }
}

int main() {
  volatile uint32_t* handoff = (volatile uint32_t*)BOOT_HANDOFF_ADDR;
  bool requested = handoff[0] == BOOT_HANDOFF_MAGIC && handoff[1] == ~BOOT_HANDOFF_MAGIC;
  handoff[0] = 0;
  handoff[1] = 0;

  hwBegin();
  logStr("\nBOOT "); logDec(BL_MAJOR); logStr("."); logDec(BL_MINOR);
  logStr(" node "); logDec(NODE_ID); logStr("\n");

  refreshAppState();
  bool valid = app.valid;
  if (valid) { logStr("application "); logDec(app.size); logStr(" bytes, CRC "); logHex(app.crc); logStr(" ok\n"); }
  else logStr("no valid application\n");

  if (requested) { stay = true; logStr("update requested by the application\n"); }
  if (valid && !stay) {
    while (ms < LISTEN_MS && !stay) { tick(); pollBus(); }
    if (!stay) jumpToApp();
  }
  logStr("staying in the bootloader, waiting for the master\n");

  uint32_t blink = 0;
  while (true) {
    tick();
    pollBus();
    if (ms - blink >= 100) { blink = ms; GPIOB->ODR ^= 1u << 12; }
    static uint32_t lastReport = 0, lastRx = 0, lastCrc = 0;
    if (ms - lastReport >= 1000) {
      lastReport = ms;
      if (parser.crcErrors != lastCrc) {
        logStr("bus: "); logDec(rxCount - lastRx); logStr(" bytes in the last second, CRC errors ");
        logDec(parser.crcErrors); logStr("\n");
        lastRx = rxCount; lastCrc = parser.crcErrors;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Start-up: vector table and reset handler (no interrupts are used)
// ---------------------------------------------------------------------------
extern "C" {
extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
void Reset_Handler();
void Default_Handler() { NVIC_SystemReset(); }  // any fault: restart rather than hang

void Reset_Handler() {
  uint32_t* src = &_sidata;
  for (uint32_t* d = &_sdata; d < &_edata;) *d++ = *src++;
  for (uint32_t* d = &_sbss; d < &_ebss;) *d++ = 0;
  main();
  while (true) {}
}

__attribute__((section(".isr_vector"), used))
void (*const vectors[48])() = {
    (void (*)())&_estack, Reset_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
};
}
