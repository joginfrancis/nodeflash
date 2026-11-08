"""nodeflash as an MCP server, so an AI agent (Claude Code, Claude Desktop, ...) can
list the hardware, scan the bus and update nodes.

    pip install "mcp>=1.2"          (optional dependency)
    nodeflash mcp                   (stdio transport)

Claude Code:  claude mcp add nodeflash -- python -m nodeflash mcp
Every tool that changes a node's firmware says so in its description, so the
agent's client asks the user before it runs.
"""
import io
from contextlib import redirect_stdout

from . import api
from .image import check_image as _check


def main():
    try:
        from mcp.server.mcpserver import MCPServer as Server  # mcp 2.x
    except ImportError:
        try:
            from mcp.server.fastmcp import FastMCP as Server  # mcp 1.x
        except ImportError:
            raise SystemExit('the MCP server needs the "mcp" package: pip install "mcp>=1.2"')

    mcp = Server("nodeflash")

    def captured(fn, *args, **kw):
        """Runs fn with its log lines collected (stdout belongs to the MCP protocol)."""
        lines = []
        kw.setdefault("log", lines.append)
        buf = io.StringIO()
        with redirect_stdout(buf):
            result = fn(*args, **kw)
        return {"result": result, "log": lines + buf.getvalue().splitlines()}

    @mcp.tool()
    def list_devices() -> dict:
        """List USB serial ports, the nodeflash hosts (ESP32) that answer, and available ST-Link tools. Read-only."""
        return api.devices()

    @mcp.tool()
    def scan_nodes(port: str = "auto", first: int = 1, last: int = 20) -> list:
        """Scan the RS-485 bus and list each node that answers: application running (with version) or
        bootloader (with whether a valid application is stored). Read-only."""
        return api.scan(port, first, last)

    @mcp.tool()
    def check_image(path: str) -> dict:
        """Check whether a .bin file is built for the node bootloader (0x08002000). Read-only."""
        data = open(path, "rb").read()
        problem = _check(path, data)
        return {"ok": problem is None, "problem": problem, "bytes": len(data)}

    @mcp.tool()
    def update_nodes(nodes: str, image: str = "", project: str = "", port: str = "auto",
                     build: bool = True, same_image_ok: bool = False) -> dict:
        """CHANGES FIRMWARE: write a new application into the given nodes over RS-485, one at a time.
        nodes: e.g. "1-20" or "3,7". Give either image (a .bin, or a pattern with {id} / {id:02d}) or
        project (an application folder with nodeflash.toml). same_image_ok must be true to send one
        file to several nodes (only right if the application reads its node ID at run time)."""
        return captured(api.update_nodes, nodes, image or None, project or None, port, build, same_image_ok)

    @mcp.tool()
    def rescue_node(node: int, port: str = "auto", seconds: int = 30) -> dict:
        """Catch a node's bootloader while a person power-cycles that node (for a node whose application
        does not answer, e.g. after an image built for another node ID). Tell the user to power-cycle it."""
        return captured(api.rescue, node, port, seconds)

    @mcp.tool()
    def factory_flash(node: int, app: str) -> dict:
        """CHANGES FIRMWARE, ERASES THE NODE: through the ST-Link, write the bootloader for this node ID,
        the application and its valid record. First-time setup of a board only."""
        return captured(api.factory, node, app)

    @mcp.tool()
    def link_project(project_dir: str, nodes: str = "1-20") -> dict:
        """Write nodeflash.toml into an application project so it can be uploaded with update_nodes.
        Also reports the platformio.ini lines the project still needs."""
        return captured(api.link, project_dir, nodes=nodes)

    mcp.run()


if __name__ == "__main__":
    main()
