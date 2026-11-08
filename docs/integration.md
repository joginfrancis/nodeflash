# Integrating nodeflash

Two sides can link the library `firmware/nodeflash-lib`:

- **STM32 application** (`nodeflash_client.h`): becomes updatable over the bus.
- **ESP32 program** (`NodeflashHost.h`): becomes an update host, so the PC tool
  (or your own code) can update nodes through it.

The library is a PlatformIO library (`library.json`) and an Arduino library
(`library.properties`). With PlatformIO, point at it from your project:

```ini
lib_deps = symlink://<path to nodeflash>/firmware/nodeflash-lib
```

## STM32 application

### 1. Build above the bootloader

```ini
board_build.flash_offset = 0x2000
board_upload.offset_address = 0x08002000
lib_deps = symlink://<path to nodeflash>/firmware/nodeflash-lib
```

`flash_offset` makes the STM32 Arduino core link at `0x08002000` and set the
vector table there. `offset_address` keeps a direct ST-Link upload from
overwriting the bootloader.

### 2. Answer INFO and ENTER

```cpp
#include <Arduino.h>            // or any CMSIS device header, first
#include "nodeflash_client.h"   // brings bus_packet.h and boot_protocol.h

static FrameParser parser;

static void sendOnBus(const uint8_t* data, uint16_t len) {
  // drive DE high, send, wait for the last stop bit, DE low
}

void loop() {
  int b;
  while ((b = busRead()) >= 0) {
    if (!parser.feed(b, millis())) continue;
    if (parser.dest == NODE_ID &&
        nodeflashHandle(parser.cmd, NODE_ID, FW_MAJOR, FW_MINOR, sendOnBus, beforeUpdate))
      continue;                 // a nodeflash command, already answered
    myCommands(parser);         // everything else is yours
  }
}
```

`nodeflashHandle()` returns `false` for anything outside `0x30`-`0x3F`. It
ignores ERASE/WRITE/VERIFY/RUN (bootloader only), so a stray command can't
touch a running node. `beforeUpdate` (optional) runs just before the restart,
for example to switch LEDs off or flush a log.

`firmware/src/example_app` is a complete application: 64 MHz clock, DMA
RS-485 receive, watchdog, LED blink by version.

### 3. Link the project to the PC tool

```
nodeflash link path/to/app
```

writes `nodeflash.toml` and tells you if step 1 is missing:

```toml
[app]
name  = "my node app"
image = ".pio/build/node{id:02d}/firmware.bin"   # one file for all, or {id} / {id:02d} per node
build = "pio run -e node{id:02d}"               # optional, runs before uploading
nodes = "1-20"

[gateway]
port = "auto"                                   # or e.g. "COM6", "/dev/ttyUSB0"
```

Then `nodeflash update --project path/to/app` (optionally `--nodes 3,7`,
`--no-build`).

**Node IDs.** If the ID is built into the application (one build per node), use
a `{id}` pattern. One file for several nodes is only right if the application
reads its ID at run time; the tool asks before doing that.

## ESP32 program

`NodeflashHost.h` has three layers:

| Class | Role |
| --- | --- |
| `Transport` / `EspUartTransport` | bytes to and from the bus. `begin()` installs an ESP32 UART in RS-485 half-duplex mode (DE on the RTS pin); `attach()` uses a UART your program already set up; or implement `Transport` yourself |
| `Host` | the update engine: `scan()`, `info()`, `enterBootloader()`, `update()`, `run()`, `imageOk()` |
| `Console` | the `NF ...` line protocol of the PC tool, on any `Stream` |

### Standalone gateway

```cpp
#include <NodeflashHost.h>

nodeflash::EspUartTransport bus;
nodeflash::Host host(bus);
nodeflash::Console console(host, Serial);

void setup() {
  Serial.setRxBufferSize(16384);
  Serial.begin(460800);
  bus.begin(UART_NUM_1, /*tx*/ 17, /*rx*/ 18, /*de*/ 4);
  console.banner();
}
void loop() { console.poll(); }
```

### Inside a program that owns the bus

```cpp
nodeflash::EspUartTransport bus;
nodeflash::Host host(bus);
nodeflash::Console console(host, Serial);

void setup() {
  Serial.setRxBufferSize(16384);
  Serial.begin(460800);           // the PC tool talks at 460800
  myBusBegin();                   // your code installs UART1 ...
  bus.attach(UART_NUM_1);         // ... nodeflash uses it too
  console.onBusAcquire([] { pauseMyBusTask(); });   // no frames/polls while nodeflash works
  console.onBusRelease([] { resumeMyBusTask(); });
}

void onConsoleLine(const char* line) {          // your own console's line handler
  if (console.handleLine(line)) return;         // "NF ..." lines belong to nodeflash
  ...
}
```

During `NF u` the console reads the image bytes from the same stream itself
and blocks until the update ends (about 2 s per node). The image is held in
PSRAM when available, otherwise in internal RAM (up to 118 KB).

### Updating from your own code

```cpp
// image from LittleFS, HTTP, ...
if (!nodeflash::Host::imageOk(img, size)) return;
pauseMyBusTask();
auto r = host.update(7, img, size, [](uint8_t node, uint8_t pct, void*) {
  Serial.printf("node %u: %u%%\n", node, pct);
});
resumeMyBusTask();
Serial.println(nodeflash::resultText(r));   // "ok", "no answer", "verify failed", ...
```

## Python API

```python
import nodeflash

nodeflash.devices()                    # ports, hosts found, ST-Link tools
nodeflash.scan(port="auto")            # [{id, mode, version, ...}]
nodeflash.update_nodes("3,7", image="app.bin", same_image_ok=True)
nodeflash.update_nodes("1-20", project="path/to/app")
nodeflash.factory(7, "app.bin")        # ST-Link
nodeflash.rescue(7)

with nodeflash.open_gateway() as gw:   # lower level
    gw.scan(1, 20)
    gw.update(7, open("app.bin", "rb").read(), progress=lambda n, p: print(n, p))
```

`update_nodes` updates one node at a time, never stops at the first failure,
and returns one result per node (`ok`, `ms`, `version` or `error`).

## MCP server

```
.venv/Scripts/python -m pip install "mcp>=1.2"      # Windows (.venv/bin/python on Linux/macOS)
claude mcp add nodeflash -- <path to nodeflash>/.venv/Scripts/python -m nodeflash mcp
```

| Tool | Changes firmware? |
| --- | --- |
| `list_devices`, `scan_nodes`, `check_image` | no |
| `update_nodes`, `factory_flash` | yes (descriptions say so, so clients ask first) |
| `rescue_node` | puts a node into its bootloader |
| `link_project` | writes `nodeflash.toml` |

Works with the `mcp` package 1.x and 2.x.
