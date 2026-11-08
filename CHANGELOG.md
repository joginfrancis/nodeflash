# Changelog

## 1.0.0 - 2026-11-08

First release.

- Bootloader 1.1 for STM32G070RB (bare metal, 4.4 KB): RS-485 update commands,
  CRC-32 valid record, 100 ms start-up window, image checks, cached
  application state for fast scans. Prebuilt for node IDs 1-20.
- `nodeflash-lib`: `NodeflashHost.h` (ESP32 host: transport, update engine,
  USB console) and `nodeflash_client.h` (STM32 application side).
- Standalone ESP32-S3 gateway, prebuilt as one merged image.
- PC tool: interactive menu, CLI (`devices`, `scan`, `update`, `factory`,
  `gateway-flash`, `rescue`, `run`, `enter`, `link`, `check`, `mcp`), Python
  API, MCP server (mcp 1.x and 2.x).
- Application linking through `nodeflash.toml`.
- Tested on hardware: factory setup, scan (1-20 in about 1.2 s), updates
  (about 2 s per 25 KB node), cut-off updates, wrong images, wrong node ID
  and rescue.
