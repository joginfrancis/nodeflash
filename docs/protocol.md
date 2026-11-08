# Protocol

nodeflash uses two links:

- **RS-485 bus** between the host (ESP32) and the nodes (STM32): binary packets.
- **USB serial** between the PC tool and the host: text lines.

## RS-485 packets

Defined in `firmware/nodeflash-lib/src/bus_packet.h`.

```
[0xAA] [DEST] [CMD] [LEN lo] [LEN hi] [PAYLOAD x LEN] [CRC lo] [CRC hi]
```

| Field | Meaning |
| --- | --- |
| `0xAA` | start of packet |
| `DEST` | `0x01`-`0xF7` a node, `0xFF` broadcast, `0x00` the host (replies) |
| `CMD` | command; a reply uses `CMD | 0x80` |
| `LEN` | payload length, little endian, at most 512 |
| `CRC` | CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over DEST, CMD, LEN and payload, little endian |

The receiver counts `LEN` payload bytes, so `0xAA` inside a payload is never
taken for a start. A packet is dropped if its CRC is wrong, if `LEN` is over
512, or if the line is silent for more than 3 ms in the middle of a packet;
the receiver then waits for the next `0xAA`. Only the host starts a
conversation. A node answers only commands addressed to its own ID, never a
broadcast, so replies can't collide.

Your application can use the same framing (`FrameParser`, `buildFrame`) for its
own commands. **Commands `0x30`-`0x3F` are reserved for nodeflash.**

### nodeflash commands

Defined in `firmware/nodeflash-lib/src/boot_protocol.h`. All multi-byte values
are little endian. Every command is addressed to one node and answered.

| CMD | Name | Payload | Answered by | Reply (CMD \| 0x80) |
| --- | --- | --- | --- | --- |
| `0x30` | INFO | none | application and bootloader | `BootInfo` |
| `0x31` | ENTER | none | application (bootloader: just acknowledges) | `BootReply`, then the application restarts into the bootloader |
| `0x32` | ERASE | u32 image size | bootloader | `BootReply` (value = pages erased) |
| `0x33` | WRITE | u32 offset + data (multiple of 8 bytes, at most 256) | bootloader | `BootReply` (value = offset written) |
| `0x34` | VERIFY | u32 size + u32 CRC-32 | bootloader | `BootReply` (value = CRC the node computed) |
| `0x35` | RUN | none | bootloader | `BootReply`, then the application starts |

`BootInfo` (21 bytes):

| Bytes | Field |
| --- | --- |
| 1 | mode: 0 = bootloader, 1 = application |
| 1 | node ID |
| 1 + 1 | version major, minor (of whichever program answers) |
| 1 | application valid (bootloader's view; 1 when the application answers) |
| 4 | application size |
| 4 | application CRC-32 |
| 4 | application start address (the application reports its vector table address) |
| 4 | maximum application size |

`BootReply` (5 bytes): u8 status + u32 value.

| Status | Meaning |
| --- | --- |
| 0 | OK |
| 1 | bad length |
| 2 | bad address or size |
| 3 | flash erase/write failed |
| 4 | CRC mismatch |
| 5 | no valid application (RUN refused) |
| 6 | image not built for the application address |

CRC-32 is the zlib/IEEE one (reflected 0xEDB88320, init and final XOR
0xFFFFFFFF), the same as Python's `zlib.crc32`.

### Update sequence

What `Host::update()` (and so the PC tool) sends, with the timeouts it uses:

| Step | Command | Timeout | Notes |
| --- | --- | --- | --- |
| 1 | INFO | 200 ms | which program answers |
| 2 | ENTER (if the application answered) | 200 ms | then INFO every 100 ms until the bootloader answers (up to 2 s) |
| 3 | ERASE size | 5 s | erases the valid record first, then the pages needed |
| 4 | WRITE offset+256 bytes, repeated | 100 ms each, 3 tries | last piece padded with 0xFF to a multiple of 8 |
| 5 | VERIFY size+CRC | 3 s | node checks CRC and image; marks it valid on success |
| 6 | RUN | 2 s | bootloader starts the application |
| 7 | INFO until the application answers | up to 1 s | proves the new application runs and is linked |

A scan sends INFO to each address with a 30 ms timeout.

## USB console (PC tool <-> host)

Implemented by `nodeflash::Console` (`NodeflashHost.h`), 460800 baud, one
command per line. Commands start with `NF ` so they can share a console with
an application's own commands. Answer lines start with a keyword.

| Command | Answer |
| --- | --- |
| `NF ?` | `NODEFLASH HOST <version> BUS <baud>` |
| `NF s <first> <last>` | `NODE ...` per node, then `SCAN <count>` |
| `NF i <id>` | `NODE <id> APP <maj>.<min>` or `NODE <id> BOOT <maj>.<min> VALID\|INVALID <size> <crc hex>`, or `NONE <id>` |
| `NF b <id>` | `OK BOOT <id>` |
| `NF r <id>` | `OK RUN <id>` |
| `NF w <id> <seconds>` | rescue: `NODE ...` + `OK RESCUE <id>` when the bootloader is caught |
| `NF u <id> <size> <crc hex> [stop]` | `READY`, then the PC sends `size` raw bytes; then `PROGRESS <id> <percent>` lines, `NODE ...`, `DONE <id> <ms>` |

Any failure: `ERR <id> <reason>`. Informational lines: `LOG ...`. `stop`
(chunks) cuts an update off on purpose, to test recovery.
