"""Talking to a nodeflash host (the standalone gateway, or any ESP32 program
that includes the nodeflash library) over its USB serial port."""
import re
import time
from dataclasses import dataclass
from typing import Callable, List, Optional

import serial
import serial.tools.list_ports

from .image import crc32

BAUD = 460800
BANNER = re.compile(r"NODEFLASH (?:HOST|GATEWAY) (\S+) BUS (\d+)")
NODE = re.compile(r"NODE (\d+) (APP|BOOT) (\d+)\.(\d+)(?: (VALID|INVALID) (\d+) ([0-9A-Fa-f]+))?")

# USB-serial chips used on ESP32 boards; ST-Link (0x0483) is never probed.
ESP_USB_VIDS = {0x10C4: "CP210x", 0x1A86: "CH340", 0x0403: "FTDI", 0x303A: "Espressif USB"}
STLINK_VID = 0x0483


@dataclass
class Node:
    id: int
    mode: str               # "app" or "bootloader"
    version: str
    app_valid: bool = True
    app_size: int = 0
    app_crc: int = 0

    def describe(self):
        if self.mode == "app":
            return f"node {self.id:2d}: application v{self.version} running"
        state = f"valid application {self.app_size} bytes CRC {self.app_crc:08X}" if self.app_valid else "no valid application"
        return f"node {self.id:2d}: BOOTLOADER v{self.version}, {state}"


@dataclass
class PortInfo:
    device: str
    description: str
    kind: str               # "esp32?", "st-link", "other"
    vid: Optional[int] = None


class GatewayError(Exception):
    pass


def list_ports() -> List[PortInfo]:
    out = []
    for p in serial.tools.list_ports.comports():
        if p.vid == STLINK_VID:
            kind = "st-link"
        elif p.vid in ESP_USB_VIDS:
            kind = "esp32?"
        else:
            kind = "other"
        out.append(PortInfo(p.device, p.description or "", kind, p.vid))
    return out


class Gateway:
    """One nodeflash host on a serial port."""

    def __init__(self, port: str, log: Callable[[str], None] = lambda s: None):
        self.port = port
        self.log = log
        s = serial.Serial()
        s.port = port
        s.baudrate = BAUD
        s.timeout = 0.05
        s.dtr = False   # set before opening: keeps the ESP32 from resetting on open
        s.rts = False
        s.open()
        self.s = s
        self.version = None
        self.bus_baud = None
        self._buf = b""

    # -- low level ---------------------------------------------------------
    def close(self):
        self.s.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _lines(self, timeout):
        end = time.time() + timeout
        while time.time() < end:
            self._buf += self.s.read(4096)
            while b"\n" in self._buf:
                line, self._buf = self._buf.split(b"\n", 1)
                text = line.decode(errors="replace").strip()
                if text:
                    yield text
                    end = max(end, time.time() + 0.5)  # keep going while lines arrive

    def command(self, cmd: str, until: Callable[[str], bool], timeout: float) -> List[str]:
        self.s.write(f"NF {cmd}\n".encode())
        got = []
        for line in self._lines(timeout):
            got.append(line)
            if line.startswith("LOG"):
                self.log(line[4:])
            if until(line):
                return got
        raise GatewayError(f"{self.port}: no complete answer to 'NF {cmd}' (got {got[-3:]})")

    # -- discovery ---------------------------------------------------------
    def identify(self, timeout=3.0) -> bool:
        """True if a nodeflash host answers on this port (waits out an ESP32 reset)."""
        end = time.time() + timeout
        while time.time() < end:
            self.s.reset_input_buffer()
            self._buf = b""
            self.s.write(b"\nNF ?\n")
            for line in self._lines(0.4):
                m = BANNER.search(line)
                if m:
                    self.version, self.bus_baud = m.group(1), int(m.group(2))
                    return True
        return False

    # -- node operations ---------------------------------------------------
    @staticmethod
    def _node(line) -> Optional[Node]:
        m = NODE.match(line)
        if not m:
            return None
        nid, mode, ma, mi, valid, size, crc = m.groups()
        if mode == "APP":
            return Node(int(nid), "app", f"{ma}.{mi}")
        return Node(int(nid), "bootloader", f"{ma}.{mi}", valid == "VALID", int(size), int(crc, 16))

    def scan(self, first=1, last=20) -> List[Node]:
        lines = self.command(f"s {first} {last}", lambda l: l.startswith("SCAN"), 2 + 0.1 * (last - first + 1))
        return [n for n in map(self._node, lines) if n]

    def info(self, node: int) -> Optional[Node]:
        lines = self.command(f"i {node}", lambda l: l.startswith(("NODE", "NONE")), 3)
        return self._node(lines[-1])

    def enter_bootloader(self, node: int):
        lines = self.command(f"b {node}", lambda l: l.startswith(("OK", "ERR")), 6)
        if lines[-1].startswith("ERR"):
            raise GatewayError(lines[-1].split(" ", 2)[-1])

    def run(self, node: int):
        lines = self.command(f"r {node}", lambda l: l.startswith(("OK", "ERR")), 4)
        if lines[-1].startswith("ERR"):
            raise GatewayError(lines[-1].split(" ", 2)[-1])

    def rescue(self, node: int, seconds=30) -> Node:
        lines = self.command(f"w {node} {seconds}", lambda l: l.startswith(("OK", "ERR")), seconds + 5)
        if lines[-1].startswith("ERR"):
            raise GatewayError(lines[-1].split(" ", 2)[-1])
        return next(n for n in map(self._node, lines) if n)

    def update(self, node: int, data: bytes, progress: Callable[[int, int], None] = None, stop_after=0) -> dict:
        """Writes data into node. Returns {"node", "ms", "version"}; raises GatewayError on failure."""
        t0 = time.time()
        self.s.write(f"NF u {node} {len(data)} {crc32(data):08x} {stop_after}\n".encode())
        for line in self._lines(5):
            if line == "READY":
                break
            if line.startswith("ERR"):
                raise GatewayError(line.split(" ", 2)[-1])
        else:
            raise GatewayError("host did not get ready for the image")
        self.s.write(data)
        result = {"node": node}
        for line in self._lines(40):
            if line.startswith("PROGRESS") and progress:
                progress(node, int(line.split()[2]))
            elif line.startswith("LOG"):
                self.log(line[4:])
            elif line.startswith("NODE"):
                n = self._node(line)
                result["version"] = n.version if n else None
            elif line.startswith("DONE"):
                result["ms"] = int(line.split()[2])
                result["seconds_total"] = round(time.time() - t0, 2)
                return result
            elif line.startswith("ERR"):
                raise GatewayError(line.split(" ", 2)[-1])
        raise GatewayError("update timed out")


def find_gateways(ports: Optional[List[str]] = None, log=lambda s: None) -> List[Gateway]:
    """Opens every likely ESP32 port (or the given ones) and keeps those that answer as nodeflash hosts."""
    if ports is None:
        ports = [p.device for p in list_ports() if p.kind == "esp32?"]
    found = []
    for port in ports:
        try:
            g = Gateway(port, log)
        except serial.SerialException as e:
            log(f"{port}: can't open ({e})")
            continue
        if g.identify():
            found.append(g)
        else:
            g.close()
    return found
