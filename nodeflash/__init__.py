"""nodeflash: program and update STM32 nodes on an RS-485 bus through an ESP32 host.

Python API (the CLI and the MCP server are built on the same calls):
    nodeflash.devices()                                  serial ports, hosts found, ST-Link tools
    nodeflash.scan(port="auto", first=1, last=20)        nodes on the bus
    nodeflash.update_nodes("1-20", project="path")       update nodes one at a time
    nodeflash.factory(node_id, app_bin)                  ST-Link: bootloader + app in one step
    nodeflash.rescue(node_id)                            catch a node's bootloader after a power cycle
    nodeflash.link(project_dir)                          write nodeflash.toml into an application
    nodeflash.open_gateway() -> Gateway                  lower level: scan/info/update/run on one host
"""
__version__ = "1.0.0"

from .api import NodeflashError, devices, factory, link, open_gateway, rescue, scan, update_nodes  # noqa: F401
from .gateway import Gateway, GatewayError, Node  # noqa: F401
from .image import check_image  # noqa: F401
