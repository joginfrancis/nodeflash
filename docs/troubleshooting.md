# Troubleshooting

| Symptom | Likely cause | What to do |
| --- | --- | --- |
| `no nodeflash host found on any USB serial port` | The ESP32 doesn't run the gateway (or your program without `Console`), or its USB chip isn't one the tool probes (CP210x, CH340, FTDI, Espressif USB) | `nodeflash gateway-flash --port COMx`, or give the port: `--port COMx` |
| Host found, `scan` lists no nodes | Bus wiring (A/B swapped, no common ground, no termination/bias), bus speed mismatch, node without bootloader, or an application not linked to nodeflash | Check `NF ?` reports the bus speed your nodes use; flash a node with `nodeflash factory`; check the node's log (USART2) |
| `image ... refused: N addresses point into the bootloader area` | Application built for 0x08000000 | Add `board_build.flash_offset = 0x2000` (see [integration.md](integration.md)) |
| `refused: reset address ... is outside` | Same, or the file is a bootloader, or not a firmware | Send the application's `firmware.bin` |
| `send the .bin file, not .elf or .hex` | Wrong file type | Use `.pio/build/<env>/firmware.bin` |
| `application did not accept 'enter bootloader'` | The running application doesn't call `nodeflashHandle()` | Link the application; for this one time, power-cycle the node during `nodeflash rescue --node N` |
| `new application does not answer (is it linked to nodeflash?)` | The new image doesn't answer at this address: built for another node ID, not linked, or crashing at start | `nodeflash scan` (a board answering at another ID shows there); `nodeflash rescue --node N`, power-cycle the node, then update with the right file |
| `write failed at offset ...` | Bus errors (noise, termination, ground) | Fix the wiring; the update can simply be run again |
| `verify failed (status 4 ...)` | Data corrupted on the way | Run again; check the bus |
| `verify failed (status 6 ...)` | The node itself refused the image as not built for 0x08002000 | As for "refused" above |
| A node shows `BOOTLOADER ... no valid application` | An update was cut off, or the node is new | Update it; nothing is broken |
| Opening the port resets the ESP32 | DTR/RTS auto-reset on the board | The tool keeps DTR/RTS low; if your board still resets, the tool waits for it (about 1-2 s) |
| `OpenOCD failed` during `factory` | ST-Link not connected, target unpowered, SWD pins in use | Check the ST-Link connection and the node's power; try STM32CubeProgrammer (used automatically if OpenOCD isn't found, or set `NODEFLASH_OPENOCD` / `NODEFLASH_CUBEPROG`) |
| `esptool not found` | Running without the launcher's environment | Use `nodeflash.cmd`/`nodeflash.sh`, or `pip install esptool` |
| Windows: `'C:\...' is not recognized` when calling `nodeflash.cmd` from Git Bash with quoted arguments | cmd.exe quoting | Use `nodeflash.sh` in Git Bash, or call `nodeflash.cmd` from PowerShell/cmd |

**Node log.** The bootloader logs every step on USART2 (PA2/PA3, 115200), on
Nucleo-style boards the ST-Link's virtual COM port: start-up decision, erase
time, write progress, CRC result, "starting application".
