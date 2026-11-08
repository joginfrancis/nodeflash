"""nodeflash command line. Run without arguments for the interactive menu.

    nodeflash                              interactive: find hardware, scan, choose what to update
    nodeflash devices                      USB serial ports, nodeflash hosts, ST-Link tools
    nodeflash scan [--port COM6] [--range 1-20]
    nodeflash update --project DIR [--nodes 1-20] [--no-build]
    nodeflash update --image app.bin --nodes 3 [--same-image]
    nodeflash update --image "build/node{id:02d}.bin" --nodes all
    nodeflash factory --node 7 --app app.bin        ST-Link: bootloader + app (first time only)
    nodeflash gateway-flash --port COM6             put the standalone gateway on an ESP32-S3
    nodeflash rescue --node 7                       recover a node whose app doesn't answer
    nodeflash run --node 7 | enter --node 7         start the app / restart into the bootloader
    nodeflash link DIR                              link an application project (writes nodeflash.toml)
    nodeflash check app.bin                         is this image built for the bootloader?
    nodeflash mcp                                   run as an MCP server (stdio) for AI agents
"""
import argparse
import json
import os
import sys

from . import __version__
from . import api
from .esp import EspFlashError, flash_gateway
from .gateway import GatewayError
from .image import check_image, crc32
from .project import ProjectError, parse_ids
from .stlink import FlashError


def _progress(node, pct):
    bar = "#" * (pct // 5)
    print(f"\r  node {node:2d} [{bar:<20}] {pct:3d}%", end="" if pct < 100 else "\n", flush=True)


def _ids_arg(text, default="1-20"):
    return parse_ids(default if text in (None, "all") else text)


def cmd_devices(a):
    d = api.devices(log=lambda s: print("  " + s))
    if a.json:
        print(json.dumps(d, indent=2))
        return
    print("USB serial ports:")
    for p in d["ports"] or [{"port": "-", "description": "none", "kind": ""}]:
        print(f"  {p['port']:8s} {p['kind']:8s} {p['description']}")
    print("nodeflash hosts:")
    for h in d["hosts"] or [{"port": "none found", "version": "", "bus_baud": ""}]:
        print(f"  {h['port']}  v{h['version']}  bus {h['bus_baud']} baud" if h["version"] else f"  {h['port']}")
    t = d["stlink_tools"]
    print(f"ST-Link tools: OpenOCD {'found' if t['openocd'] else 'not found'}, "
          f"STM32CubeProgrammer {'found' if t['stm32cubeprogrammer'] else 'not found'}")


def cmd_scan(a):
    first, last = (parse_ids(a.range)[0], parse_ids(a.range)[-1])
    with api.open_gateway(a.port) as g:
        print(f"host on {g.port} (v{g.version}, bus {g.bus_baud} baud), scanning {first}-{last} ...")
        nodes = g.scan(first, last)
    if a.json:
        print(json.dumps([n.__dict__ for n in nodes], indent=2))
        return
    for n in nodes:
        print("  " + n.describe())
    print(f"{len(nodes)} node(s) found")


def _confirm(prompt, yes):
    if yes:
        return True
    return input(f"{prompt} [y/N] ").strip().lower() in ("y", "yes")


def cmd_update(a):
    if a.nodes:
        ids = _ids_arg(a.nodes)
    elif a.project:
        from .project import load
        ids = load(a.project).nodes
    else:
        ids = parse_ids("1-20")
    images = api.resolve_images(ids, a.image, a.project, build=not a.no_build, same_image_ok=True, log=print)
    distinct = set(images.values())
    print(f"update {len(ids)} node(s): {', '.join(map(str, ids))}")
    for p in sorted(distinct):
        d = open(p, "rb").read()
        print(f"  {p}: {len(d)} bytes, CRC {crc32(d):08X}")
    if len(ids) > 1 and len(distinct) == 1 and not a.same_image:
        print("  NOTE: the same file goes to every node. That's only right if the application reads its node ID\n"
              "        at run time; an image with a built-in ID makes the other nodes answer to the wrong address.")
        if not _confirm("Send the same image to all of them?", a.yes):
            return 1
    elif not _confirm("Go ahead?", a.yes):
        return 1
    results = api.update_nodes(ids, a.image, a.project, a.port, build=False, same_image_ok=True,
                               progress=_progress, log=print)
    ok = sum(r["ok"] for r in results)
    print(f"{ok} of {len(results)} node(s) updated")
    if a.json:
        print(json.dumps(results, indent=2))
    return 0 if ok == len(results) else 2


def cmd_factory(a):
    if not _confirm(f"Flash bootloader for node {a.node} + {a.app} through the ST-Link (erases the node)?", a.yes):
        return 1
    api.factory(a.node, a.app)


def cmd_gateway_flash(a):
    if not _confirm(f"Flash the nodeflash gateway onto the ESP32-S3 on {a.port} (replaces its program)?", a.yes):
        return 1
    flash_gateway(a.port)


def cmd_rescue(a):
    n = api.rescue(a.node, a.port, a.seconds)
    print(f"caught: node {n['id']} is in its bootloader. Update it now with the right image.")


def cmd_run(a):
    with api.open_gateway(a.port) as g:
        g.run(a.node)
        print(f"node {a.node}: application started")


def cmd_enter(a):
    with api.open_gateway(a.port) as g:
        g.enter_bootloader(a.node)
        print(f"node {a.node}: in the bootloader")


def cmd_link(a):
    api.link(a.dir, image=a.image, build=a.build, nodes=a.nodes or "1-20")


def cmd_check(a):
    bad = 0
    for path in a.images:
        d = open(path, "rb").read()
        problem = check_image(path, d)
        print(f"{path}: {'OK' if not problem else 'REFUSED - ' + problem} ({len(d)} bytes, CRC {crc32(d):08X})")
        bad += bool(problem)
    return 1 if bad else 0


def cmd_mcp(a):
    from .mcp_server import main as mcp_main
    mcp_main()


# ---------------------------------------------------------------------------
# Interactive menu
# ---------------------------------------------------------------------------
def interactive():
    print(f"nodeflash {__version__} - looking for hardware ...")
    d = api.devices(log=lambda s: print("  " + s))
    if not d["hosts"]:
        print("No nodeflash host found on any USB serial port.")
        esp = [p["port"] for p in d["ports"] if p["kind"] == "esp32?"]
        if esp and _confirm(f"Flash the standalone gateway onto the ESP32 on {esp[0]}?", False):
            flash_gateway(esp[0])
            d = api.devices()
        if not d["hosts"]:
            return 1
    port = d["hosts"][0]["port"]
    if len(d["hosts"]) > 1:
        for k, h in enumerate(d["hosts"], 1):
            print(f"  {k}) {h['port']}")
        port = d["hosts"][int(input("Which host? ") or 1) - 1]["port"]

    while True:
        with api.open_gateway(port) as g:
            print(f"\nhost on {port} (bus {g.bus_baud} baud). Scanning nodes 1-20 ...")
            nodes = g.scan(1, 20)
        for n in nodes:
            print("  " + n.describe())
        if not nodes:
            print("  no nodes answered")
        print("\n  u) update nodes    p) update from a linked project    r) rescue a node\n"
              "  s) scan again      q) quit")
        choice = input("> ").strip().lower()
        if choice == "q":
            return 0
        if choice == "s":
            continue
        if choice == "r":
            api.rescue(int(input("node ID: ")), port)
            continue
        if choice in ("u", "p"):
            present = ",".join(str(n.id) for n in nodes) or "1-20"
            which = input(f"nodes (all = {present}, or e.g. 3,7-9): ").strip() or "all"
            ids = parse_ids(present if which == "all" else which)
            try:
                if choice == "p":
                    proj = input("project folder (with nodeflash.toml): ").strip().strip('"')
                    images = api.resolve_images(ids, project=proj, log=print)
                    img_arg, proj_arg = None, proj
                else:
                    img = input("image file (.bin; {id} or {id:02d} for one file per node): ").strip().strip('"')
                    same = "{id" not in img and len(ids) > 1
                    if same and not _confirm("Same file for every node (only right if the app reads its ID at run time)?", False):
                        continue
                    images = api.resolve_images(ids, image=img, same_image_ok=True, log=print)
                    img_arg, proj_arg = img, None
            except (api.NodeflashError, ProjectError, OSError) as e:
                print(f"  {e}")
                continue
            for i in ids:
                print(f"  node {i} <- {images[i]}")
            if _confirm("Go ahead?", False):
                res = api.update_nodes(ids, img_arg, proj_arg, port, build=False, same_image_ok=True,
                                       progress=_progress, log=print)
                print(f"{sum(r['ok'] for r in res)} of {len(res)} node(s) updated")


def main(argv=None):
    ap = argparse.ArgumentParser(prog="nodeflash", description="Program and update STM32 nodes over RS-485.")
    ap.add_argument("--version", action="version", version=f"nodeflash {__version__}")
    sub = ap.add_subparsers(dest="cmd")

    p = sub.add_parser("devices", help="list USB serial ports, nodeflash hosts and ST-Link tools")
    p.add_argument("--json", action="store_true")
    p.set_defaults(fn=cmd_devices)

    p = sub.add_parser("scan", help="list the nodes on the bus")
    p.add_argument("--port", default="auto")
    p.add_argument("--range", default="1-20")
    p.add_argument("--json", action="store_true")
    p.set_defaults(fn=cmd_scan)

    p = sub.add_parser("update", help="update nodes over RS-485")
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--project", help="application folder with nodeflash.toml")
    g.add_argument("--image", help="image .bin, or pattern with {id} / {id:02d}")
    p.add_argument("--nodes", help="e.g. 1-20, 3,7 or all (default: the project's nodes, else 1-20)")
    p.add_argument("--port", default="auto")
    p.add_argument("--no-build", action="store_true", help="don't run the project's build command")
    p.add_argument("--same-image", action="store_true", help="one file for several nodes is intended")
    p.add_argument("-y", "--yes", action="store_true", help="don't ask")
    p.add_argument("--json", action="store_true")
    p.set_defaults(fn=cmd_update)

    p = sub.add_parser("factory", help="ST-Link: bootloader + application, first time only")
    p.add_argument("--node", type=int, required=True)
    p.add_argument("--app", required=True)
    p.add_argument("-y", "--yes", action="store_true")
    p.set_defaults(fn=cmd_factory)

    p = sub.add_parser("gateway-flash", help="flash the standalone gateway onto an ESP32-S3")
    p.add_argument("--port", required=True)
    p.add_argument("-y", "--yes", action="store_true")
    p.set_defaults(fn=cmd_gateway_flash)

    p = sub.add_parser("rescue", help="catch a node's bootloader during a power cycle")
    p.add_argument("--node", type=int, required=True)
    p.add_argument("--port", default="auto")
    p.add_argument("--seconds", type=int, default=30)
    p.set_defaults(fn=cmd_rescue)

    for name, fn, hlp in (("run", cmd_run, "start the application from the bootloader"),
                          ("enter", cmd_enter, "restart a node's application into the bootloader")):
        p = sub.add_parser(name, help=hlp)
        p.add_argument("--node", type=int, required=True)
        p.add_argument("--port", default="auto")
        p.set_defaults(fn=fn)

    p = sub.add_parser("link", help="link an application project (writes nodeflash.toml)")
    p.add_argument("dir")
    p.add_argument("--image")
    p.add_argument("--build")
    p.add_argument("--nodes")
    p.set_defaults(fn=cmd_link)

    p = sub.add_parser("check", help="check images are built for the bootloader")
    p.add_argument("images", nargs="+")
    p.set_defaults(fn=cmd_check)

    p = sub.add_parser("mcp", help="run as an MCP server (stdio) for AI agents")
    p.set_defaults(fn=cmd_mcp)

    a = ap.parse_args(argv)
    try:
        if not a.cmd:
            return interactive()
        return a.fn(a) or 0
    except (api.NodeflashError, GatewayError, ProjectError, FlashError, EspFlashError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\ncancelled")
        return 130


if __name__ == "__main__":
    sys.exit(main())
