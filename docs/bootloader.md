# Bootloader

`firmware/src/bootloader`: bare metal (no Arduino, no HAL, no interrupts),
about 4.4 KB of an 8 KB slot. Prebuilt per node ID in
`firmware/bin/bootloader/boot01.bin` ... `boot20.bin`.

## Flash layout (STM32G070RB, 128 KB, 2 KB pages)

```
0x08000000  bootloader          8 KB   pages 0-3     flashed once with the ST-Link
0x08002000  application         up to 118 KB         replaced by updates
0x0801F800  "valid" record      last page            magic, size, CRC-32, ~magic
```

## What happens at reset

1. **Did the application ask for the bootloader?** Before restarting, an
   application that got ENTER writes `0xB007104D` and its complement at
   `0x20008FF0` (top of RAM, outside the bootloader's own RAM). If present:
   clear it and stay.
2. **Is there a valid application?** The record must be present, the image
   must be built for `0x08002000` (see "Image checks" below) and the CRC-32
   over the application must match the record. If not: stay.
3. **Listen for 100 ms.** An update command (0x30-0x3F) for this node in that
   window keeps the bootloader running. Normal application traffic does not.
4. **Start the application:** switch off the bootloader's peripherals, point
   the vector table at `0x08002000`, load its stack pointer and jump. The
   application starts from the reset clock (HSI 16 MHz), as after a reset.

While it stays, the bootloader blinks PB12 fast, logs to USART2 (115200) and
answers nodeflash commands on the bus.

The CRC check takes about 0.2 s per 25 KB at 16 MHz. The result is kept in
RAM and recomputed only after ERASE or VERIFY, so INFO answers at once.

## Why a failed update can't brick a node

- **ERASE wipes the valid record first.** An update cut off at any point leaves
  no valid application, so after any reset the node stays in the bootloader
  and can be updated again.
- **The bootloader is never written over the bus.** Only the ST-Link changes it.
- **Every piece is acknowledged** and resent up to 3 times; the whole image is
  checked with CRC-32 before it's marked valid.
- **A crash in the bootloader resets it** (fault handler), and it keeps the
  watchdog refreshed, so a watchdog the application started can't reset it
  mid-update.
- **A wrong application can be rescued.** If an application doesn't answer
  (wrong node ID built in, crashes, not linked), power-cycle the node while
  `nodeflash rescue --node N` runs: the bootloader hears INFO in its 100 ms
  window and stays.

## Image checks

An image is accepted only if:

- its first word (initial stack pointer) is inside RAM (0x20000400-0x20009000, word aligned),
- its second word (reset address) is odd (Thumb) and inside the application,
- **no word in the image points into 0x08000000-0x08001FFF.**

The last rule catches the commonest mistake, an application built for
`0x08000000` (without `board_build.flash_offset = 0x2000`): such an image is
full of addresses of its own first 8 KB, while one built for `0x08002000`
never points into the bootloader. Its reset address alone can't tell: in a
normal build it often falls inside the application range.

The PC tool, the ESP32 host and the bootloader all apply these rules.

## Limits

- Bus speed: the bootloader runs at 16 MHz, so at most 1 Mbaud (the USART needs
  16 clocks per bit). A faster `BUS_BAUD_HZ` fails to build on purpose.
- The node ID is built in; one file per ID (`boot01..20.bin`).
- The flash page holding the valid record (the last one) is not available to
  the application.
