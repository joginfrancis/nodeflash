#include <Arduino.h>
#include "rs485.h"

// USART3's pins as the core names them. On the node board the SP3485's RO is
// wired to PB2 and DI to PB0 (found 2026-11-08), so CR2.SWAP below exchanges
// them: the USART transmits on PB0 and receives on PB2.
static constexpr PinName  PIN_RX = PB_0;
static constexpr PinName  PIN_TX = PB_2;
static constexpr PinName  PIN_DE = PB_1;   // DE and /RE tied together
static constexpr uint16_t RX_BUF_SIZE = 1024;  // ~20 ms of bus traffic at 500 kbaud

// DMA1 channel 1, routed to USART3 RX through DMAMUX channel 0.
#define RX_DMA        DMA1_Channel1
#define RX_DMAMUX     DMAMUX1_Channel0

static uint8_t  rxBuf[RX_BUF_SIZE];
static uint16_t rxTail = 0;

void rs485Begin(uint32_t baud) {
  pinMode(pinNametoDigitalPin(PIN_DE), OUTPUT);
  digitalWriteFast(PIN_DE, LOW);  // listen

  // Alternate functions for USART3 from the core's pin map.
  pinmap_pinout(PIN_RX, PinMap_UART_RX);
  pinmap_pinout(PIN_TX, PinMap_UART_TX);

  __HAL_RCC_USART3_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  USART3->CR1 = 0;
  USART3->CR2 = USART_CR2_SWAP;  // TX on PB0 (to DI), RX on PB2 (from RO)
  USART3->BRR = HAL_RCC_GetPCLK1Freq() / baud;  // 64 MHz / 500 k = 128
  USART3->CR3 = USART_CR3_DMAR | USART_CR3_OVRDIS;

  RX_DMA->CCR = 0;
  RX_DMAMUX->CCR = DMA_REQUEST_USART3_RX;
  RX_DMA->CPAR = (uint32_t)&USART3->RDR;
  RX_DMA->CMAR = (uint32_t)rxBuf;
  RX_DMA->CNDTR = RX_BUF_SIZE;
  RX_DMA->CCR = DMA_CCR_MINC | DMA_CCR_CIRC | DMA_CCR_PL_1 | DMA_CCR_EN;

  USART3->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

uint16_t rs485Pending() {
  uint16_t head = RX_BUF_SIZE - RX_DMA->CNDTR;
  if (head >= RX_BUF_SIZE) head = 0;
  return (head + RX_BUF_SIZE - rxTail) % RX_BUF_SIZE;
}

int rs485Read() {
  uint16_t head = RX_BUF_SIZE - RX_DMA->CNDTR;  // where DMA writes next
  if (head >= RX_BUF_SIZE) head = 0;
  if (rxTail == head) return -1;
  uint8_t b = rxBuf[rxTail];
  rxTail = (rxTail + 1) % RX_BUF_SIZE;
  return b;
}

void rs485Write(const uint8_t* data, uint16_t len) {
  digitalWriteFast(PIN_DE, HIGH);  // drive the bus (also disables our receiver)
  for (uint16_t i = 0; i < len; i++) {
    while (!(USART3->ISR & USART_ISR_TXE_TXFNF)) {}
    USART3->TDR = data[i];
  }
  while (!(USART3->ISR & USART_ISR_TC)) {}  // last stop bit has left the pin
  digitalWriteFast(PIN_DE, LOW);
}
