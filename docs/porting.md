# Porting to other boards

The prebuilt firmware is for the reference hardware below. Anything else means
rebuilding with your settings (`tools/build_firmware.py` rebuilds everything).

## Reference hardware

| Part | Setting | Where to change it |
| --- | --- | --- |
| Node MCU | STM32G070RB (128 KB flash, 36 KB RAM, 2 KB pages) | see "Other STM32 parts" |
| Node bus UART | USART3, TX/RX swapped: TX on PB0, RX on PB2 (AF4) | `hwBegin()` in `firmware/src/bootloader/src/main.cpp`; your application's own driver |
| Node DE + /RE | PB1 | `hwBegin()`, `busWrite()` in the bootloader |
| Node log | USART2 on PA2/PA3, 115200 | `hwBegin()` |
| Node LED | PB12 (blinks fast in the bootloader) | `main()` loop in the bootloader |
| Transceiver | SP3485, DE and /RE tied | any half-duplex RS-485 transceiver |
| Host | ESP32-S3 (any ESP32 works for the library) | `firmware/src/gateway/platformio.ini` |
| Host pins | UART1: TX 17, RX 18, DE 4 | build flags `GW_TX_PIN`, `GW_RX_PIN`, `GW_DE_PIN` |
| Bus speed | 1 Mbaud | `-DBUS_BAUD_HZ=...` in every firmware |

**If your board doesn't swap TX/RX:** remove `USART_CR2_SWAP` in the
bootloader's `hwBegin()` and swap the two pin set-ups (PB2 is USART3's TX pin
and PB0 its RX pin without the swap).

## Bus speed

Every firmware on the bus must use the same `BUS_BAUD_HZ`. The bootloader runs
on the 16 MHz internal oscillator, so 1 Mbaud is its maximum (exact divider
16). A faster setting fails to build with a clear message; to go faster, give
the bootloader a PLL clock and restore the reset clock before starting the
application.

## Other STM32 parts

The bootloader is written against the CMSIS registers of the STM32G0. For
another G0 part, check:

- the flash size and page size (`BOOT_RECORD_ADDR`, `BOOT_PAGE_SIZE` in `boot_protocol.h`),
- the RAM end (`BOOT_RAM_END`, `BOOT_HANDOFF_ADDR` = RAM end - 16, and `boot.ld`),
- the device header (`firmware/src/bootloader/cmsis/`, `-DSTM32G070xx` in `platformio.ini`),
- the DMAMUX request number for your UART's RX (`0x36` is USART3_RX on the G0).

Other families (F0, L0, G4, ...) need their own flash routines (erase and
program steps differ) and possibly a different vector table size.

## Node IDs

Bootloaders are built per ID (`-DNODE_ID=n`, envs `boot01`...`boot20`). Add
envs for more nodes (addresses up to 247 work on the bus).

## A different host

Anything that can send the packets in [protocol.md](protocol.md) can update
nodes. On an ESP32, implement `nodeflash::Transport` for your bus driver and
reuse `Host`. The PC tool needs the `NF ...` console from
`nodeflash::Console`.
