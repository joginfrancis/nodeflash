# nodeflash

Program and update STM32 nodes on an RS-485 bus from a PC, through an ESP32.
One folder holds everything: the PC tool (command line, interactive menu,
Python API, MCP server for AI agents), the prebuilt firmware, and the libraries
an application links in to become updatable.

```
PC ──USB──> ESP32 (gateway, or your own ESP32 program with the library) ──RS-485──> nodes 1..20
PC ──ST-Link──> one node, first time only (bootloader + application)
```

## Quick start

Requirements: Python 3.11 or newer. For first-time node setup: an ST-Link and
either OpenOCD (comes with PlatformIO) or STM32CubeProgrammer.

```
nodeflash.cmd            (Windows; nodeflash.sh on Linux, macOS, Git Bash)
```

The first run creates a private Python environment in `.venv`. With no
arguments you get the interactive menu: it finds the ESP32 host, scans the bus,
lists the nodes and asks which to update (all, or e.g. `3,7-9`) and with what.

| Task | Command |
| --- | --- |
| What's connected | `nodeflash devices` |
| Nodes on the bus | `nodeflash scan` |
| Put the gateway on an ESP32-S3 | `nodeflash gateway-flash --port COM6` |
| New board, first time (ST-Link) | `nodeflash factory --node 7 --app app.bin` |
| Update one node | `nodeflash update --image app.bin --nodes 7` |
| Update all, one file per node | `nodeflash update --image "build/node{id:02d}.bin" --nodes 1-20` |
| Update from a linked project | `nodeflash update --project path/to/app` |
| Node whose new app doesn't answer | `nodeflash rescue --node 7` (power-cycle it while this runs) |
| Is this file right? | `nodeflash check app.bin` |
| Start the app / back to bootloader | `nodeflash run --node 7` / `nodeflash enter --node 7` |

Every update: the image is checked on the PC, again on the ESP32 and again by
the node; the node restarts into its bootloader, the application area is erased,
written in 256-byte pieces (each acknowledged), checked with CRC-32, marked
valid and started; the tool then checks the new application answers. About
2 s per node for a 25 KB application. Nodes are updated one at a time, so at
most one is ever half-done; a cut-off update leaves that node in its bootloader,
still reachable, and simply needs running again.

## Linking your application

Nothing of your application lives in this bundle. To make an STM32 application
updatable:

1. **Build it for 0x08002000** (above the 8 KB bootloader). In its `platformio.ini`:
   ```ini
   board_build.flash_offset = 0x2000
   board_upload.offset_address = 0x08002000
   lib_deps = symlink://<path to nodeflash>/firmware/nodeflash-lib
   ```
2. **Answer nodeflash's two commands.** Hand each valid bus packet addressed to
   the node to `nodeflashHandle()` (from `nodeflash_client.h`); it answers
   "info" and restarts into the bootloader on "enter". Commands 0x30-0x3F are
   reserved for nodeflash; everything else stays yours.
   ```cpp
   #include <Arduino.h>
   #include "nodeflash_client.h"
   ...
   if (parser.feed(b, millis()) && parser.dest == NODE_ID)
     if (!nodeflashHandle(parser.cmd, NODE_ID, FW_MAJOR, FW_MINOR, sendOnBus)) myCommands();
   ```
   The bus framing is in `bus_packet.h` (`FrameParser`, `buildFrame`).
   `firmware/src/example_app` is a complete example.
3. **Link the project:** `nodeflash link path/to/app` writes `nodeflash.toml`
   (where the images are, how to build them, which node IDs) and tells you if
   step 1 is missing. Then `nodeflash update --project path/to/app`.

```toml
[app]
name  = "my node app"
image = ".pio/build/node{id:02d}/firmware.bin"   # one file, or one per node with {id} / {id:02d}
build = "pio run -e node{id:02d}"               # optional, run before uploading
nodes = "1-20"
[gateway]
port = "auto"
```

**Node IDs.** If your application has its node ID built in (one build per
node), use a `{id}` pattern so each node gets its own file. Sending one file to
several nodes is only right if the application reads its ID at run time; the
tool asks before doing that. If a node gets an image built for another ID, the
update reports "new application does not answer" and the board now answers to
the other ID: run `nodeflash rescue --node N` and power-cycle the node, then
update it with the right file.

## Adding the host to your own ESP32 program

The ESP32 side is a library too (`firmware/nodeflash-lib/src/NodeflashHost.h`),
so your own ESP32 program can do what the standalone gateway does.

```ini
lib_deps = symlink://<path to nodeflash>/firmware/nodeflash-lib
```

Standalone (the whole gateway, `firmware/src/gateway`):
```cpp
#include <NodeflashHost.h>
nodeflash::EspUartTransport bus;
nodeflash::Host host(bus);
nodeflash::Console console(host, Serial);
void setup() { Serial.begin(460800); bus.begin(UART_NUM_1, 17, 18, 4); console.banner(); }
void loop()  { console.poll(); }
```

Inside a program that already owns the bus and has its own serial console:
```cpp
nodeflash::EspUartTransport bus;      // bus.attach(UART_NUM_1) if your code installed the UART driver
nodeflash::Host host(bus);
nodeflash::Console console(host, Serial);

void setup() {
  ...
  bus.attach(UART_NUM_1);
  console.onBusAcquire([] { pauseMyBusTask(); });   // stop your frames/polls during nodeflash work
  console.onBusRelease([] { resumeMyBusTask(); });
}
// in your line handler: lines starting with "NF " belong to nodeflash
if (console.handleLine(line)) return;
```
The PC tool then finds your program like the gateway (the serial port must run
at 460800 baud). Use `Host` directly to update nodes from your own code, e.g.
an image downloaded over Wi-Fi:
```cpp
if (nodeflash::Host::imageOk(img, size)) {
  auto r = host.update(7, img, size, [](uint8_t node, uint8_t pct, void*) { Serial.printf("%u%%\n", pct); });
  Serial.println(nodeflash::resultText(r));
}
```

## Python API and MCP server

```python
import nodeflash
print(nodeflash.devices())
print(nodeflash.scan())
nodeflash.update_nodes("3,7", image="app.bin", same_image_ok=True)
nodeflash.update_nodes("1-20", project="path/to/app")
```

For AI agents (Claude Code, Claude Desktop, other MCP clients):
```
.venv\Scripts\python -m pip install "mcp>=1.2"
claude mcp add nodeflash -- <path>\.venv\Scripts\python -m nodeflash mcp
```
Tools: `list_devices`, `scan_nodes`, `check_image` (read-only), `update_nodes`,
`rescue_node`, `factory_flash`, `link_project`. The ones that change firmware
say so in their description so the client asks before running them.

## Docker

Only useful on Linux: Docker Desktop on Windows and macOS can't pass USB serial
ports or the ST-Link into a container. See `Dockerfile` for the commands.

## What's in the folder

| Path | What |
| --- | --- |
| `nodeflash.cmd`, `nodeflash.sh` | launchers (set up `.venv` on first run) |
| `nodeflash/` | the Python package: CLI, menu, API, MCP server |
| `firmware/bin/` | prebuilt: bootloader per node ID (`boot01..20.bin`), ESP32-S3 gateway (merged, flash at 0x0), example app; `MANIFEST.txt` lists CRCs |
| `firmware/nodeflash-lib/` | the library: `NodeflashHost.h` (ESP32), `nodeflash_client.h` (STM32 app), `bus_packet.h`, `boot_protocol.h` |
| `firmware/src/` | sources: bootloader (bare metal, 4.4 KB), gateway, example app |
| `tools/build_firmware.py` | rebuilds everything in `firmware/bin` (needs PlatformIO) |

## Defaults and limits

- STM32G070RB nodes: bootloader 0x08000000 (8 KB), application 0x08002000 (up to 118 KB), valid record in the last page (0x0801F800).
- Node RS-485: USART3 with TX/RX swapped (TX PB0, RX PB2), DE on PB1. Bootloader console: USART2 (PA2/PA3) at 115200. Board-specific: change them in `firmware/src/bootloader/src/main.cpp`.
- Bus 1 Mbaud (the bootloader at 16 MHz can't go faster: building for more fails on purpose). Change with `-DBUS_BAUD_HZ` in every firmware.
- Gateway pins: TX 17, RX 18, DE 4 (build flags `GW_TX_PIN`, `GW_RX_PIN`, `GW_DE_PIN`). USB console 460800 baud.
- The bootloader's node ID is built in (`boot01..20.bin`); it's flashed once with `factory`.
- The bootloader itself is never updated over the bus. Changing it needs the ST-Link.
