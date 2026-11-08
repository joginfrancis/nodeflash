# nodeflash

Program and update STM32 nodes on an RS-485 bus from a PC, through an ESP32.

```
PC ──USB──> ESP32 host ──RS-485──> node 1, node 2, ... node N   (updates, ~2 s per node)
PC ──ST-Link──> one node                                         (first time only)
```

- **PC tool:** interactive menu, command line, Python API and an MCP server for AI agents.
- **Safe updates:** a small bootloader on each node. A cut-off update leaves the
  node in its bootloader, still reachable. Wrong images are refused three times:
  on the PC, on the ESP32 and on the node.
- **Libraries, not lock-in:** link `nodeflash_client.h` into any STM32
  application to make it updatable, and `NodeflashHost.h` into any ESP32
  program to make it the update host. Your applications stay in their own
  projects.

Reference hardware: STM32G070RB nodes, SP3485 transceivers, ESP32-S3 host,
1 Mbaud. Other boards: see [docs/porting.md](docs/porting.md).

## Quick start

You need Python 3.11+. For first-time node setup, an ST-Link plus OpenOCD
(installed with PlatformIO) or STM32CubeProgrammer.

```
git clone https://github.com/joginfrancis/nodeflash.git
cd nodeflash
nodeflash.cmd                      # Windows (PowerShell / cmd)
./nodeflash.sh                     # Linux, macOS, Git Bash
```

The first run sets up a private Python environment (`.venv`). With no
arguments you get the interactive menu: it finds the ESP32 host, scans the bus,
lists the nodes and asks which to update (`all`, or e.g. `3,7-9`) and with
which file.

**First setup, once per piece of hardware:**

```
nodeflash gateway-flash --port COM6                                   # ESP32-S3 becomes the host
nodeflash factory --node 1 --app firmware/bin/example_app/app_v1.bin  # node 1: bootloader + app (ST-Link)
nodeflash scan                                                        # node 1: application v1.0 running
nodeflash update --image firmware/bin/example_app/app_v2.bin --nodes 1
```

## Commands

| Task | Command |
| --- | --- |
| What's connected | `nodeflash devices` |
| Nodes on the bus | `nodeflash scan [--range 1-20]` |
| Update with one file | `nodeflash update --image app.bin --nodes 7` |
| Update, one file per node | `nodeflash update --image "build/node{id:02d}.bin" --nodes 1-20` |
| Update a linked project | `nodeflash update --project path/to/app [--nodes 3,7] [--no-build]` |
| Link a project | `nodeflash link path/to/app` (writes `nodeflash.toml`) |
| Is this file right? | `nodeflash check app.bin` |
| First time, through the ST-Link | `nodeflash factory --node 7 --app app.bin` |
| Flash the ESP32-S3 host | `nodeflash gateway-flash --port COM6` |
| A node whose new app doesn't answer | `nodeflash rescue --node 7` (power-cycle it meanwhile) |
| Start app / back to bootloader | `nodeflash run --node 7`, `nodeflash enter --node 7` |
| MCP server for AI agents | `nodeflash mcp` |

`-y` skips confirmations, `--json` gives machine-readable output, `--port`
picks a host when there are several.

## Make your application updatable

1. In the STM32 application's `platformio.ini`:
   ```ini
   board_build.flash_offset = 0x2000
   board_upload.offset_address = 0x08002000
   lib_deps = symlink://<path to nodeflash>/firmware/nodeflash-lib
   ```
2. Pass each bus packet addressed to the node to `nodeflashHandle()`:
   ```cpp
   #include "nodeflash_client.h"
   if (parser.feed(b, millis()) && parser.dest == NODE_ID)
     if (!nodeflashHandle(parser.cmd, NODE_ID, FW_MAJOR, FW_MINOR, sendOnBus)) myCommands();
   ```
3. `nodeflash link path/to/app`, then `nodeflash update --project path/to/app`.

Details, the ESP32 host library, the Python API and the MCP server:
[docs/integration.md](docs/integration.md).

## Documentation

| | |
| --- | --- |
| [docs/integration.md](docs/integration.md) | linking STM32 applications and ESP32 programs, `nodeflash.toml`, Python API, MCP |
| [docs/bootloader.md](docs/bootloader.md) | flash layout, start-up decision, why updates are safe, image checks |
| [docs/protocol.md](docs/protocol.md) | RS-485 packets, commands and payloads, USB console protocol |
| [docs/porting.md](docs/porting.md) | other pins, boards, bus speeds, hosts |
| [docs/troubleshooting.md](docs/troubleshooting.md) | error messages and what to do |
| [CHANGELOG.md](CHANGELOG.md) | versions |

## Repository layout

| Path | What |
| --- | --- |
| `nodeflash.cmd`, `nodeflash.sh` | launchers (set up `.venv` on first run) |
| `nodeflash/` | Python package: CLI, menu, API, MCP server |
| `firmware/nodeflash-lib/` | library: `NodeflashHost.h` (ESP32), `nodeflash_client.h` (STM32 app), `bus_packet.h`, `boot_protocol.h` |
| `firmware/bin/` | prebuilt bootloaders `boot01..20.bin`, ESP32-S3 host (merged, flash at 0x0), example app; `MANIFEST.txt` |
| `firmware/src/` | sources: bootloader (bare metal), gateway, example application |
| `tools/build_firmware.py` | rebuilds `firmware/bin` (needs PlatformIO) |
| `tests/` | Python tests (`python -m pytest`) |
| `Dockerfile` | Linux only: Docker Desktop on Windows/macOS can't pass USB serial or ST-Link devices through |

## Licence

MIT, see [LICENSE](LICENSE). The CMSIS headers in
`firmware/src/bootloader/cmsis/` are Apache-2.0 (Arm, STMicroelectronics); see
the licence files in that folder.
