// RS-485 on USART3 with TX/RX swapped: TX PB0 (to SP3485 DI), RX PB2 (from RO),
// DE (+ /RE) PB1, as wired on the reference board (the chip's default PB0 = RX
// is the other way round, hence the swap).
//
// Receive: DMA writes every byte into a circular buffer, so reception keeps
// going while the NeoPixel library has interrupts off.
// Transmit: blocking, only when polled; DE goes high for the frame and drops
// after the transmission-complete flag, so the last byte is not cut off.
#pragma once
#include <stdint.h>

void rs485Begin(uint32_t baud);
int  rs485Read();                                 // next received byte, or -1 if none
uint16_t rs485Pending();                          // bytes received but not yet read
void rs485Write(const uint8_t* data, uint16_t len);
