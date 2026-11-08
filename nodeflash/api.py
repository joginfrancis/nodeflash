"""nodeflash Python API: the same operations the CLI and the MCP server use.

    import nodeflash
    with nodeflash.open_gateway() as gw:          # finds the ESP32 host by itself
        print(gw.scan())                          # nodes on the bus
    nodeflash.update_nodes([3, 7], image="app.bin", same_image_ok=True)
    nodeflash.update_nodes("1-20", project="path/to/app")   # uses its nodeflash.toml
"""
import os
from typing import Callable, Dict, List, Optional, Union

from . import project as projmod
from .gateway import Gateway, GatewayError, find_gateways, list_ports
from .image import check_image
from .stlink import factory_flash, tools_available

Ids = Union[str, List[int]]


class NodeflashError(Exception):
    pass


def _ids(ids: Ids) -> List[int]:
    return projmod.parse_ids(ids) if isinstance(ids, str) else sorted(set(int(i) for i in ids))


def devices(log=lambda s: None) -> Dict:
    """Serial ports, which of them are nodeflash hosts, and the ST-Link tools available."""
    ports = list_ports()
    hosts = []
    for g in find_gateways(log=log):
        hosts.append({"port": g.port, "version": g.version, "bus_baud": g.bus_baud})
        g.close()
    return {
        "ports": [{"port": p.device, "description": p.description, "kind": p.kind} for p in ports],
        "hosts": hosts,
        "stlink_tools": tools_available(),
    }


def open_gateway(port: str = "auto", log=lambda s: None) -> Gateway:
    if port and port != "auto":
        g = Gateway(port, log)
        if not g.identify():
            g.close()
            raise NodeflashError(f"{port}: no nodeflash host answers (flash the gateway, or add the library to your ESP32 program)")
        return g
    found = find_gateways(log=log)
    if not found:
        raise NodeflashError("no nodeflash host found on any USB serial port")
    for g in found[1:]:
        g.close()
    return found[0]


def scan(port: str = "auto", first: int = 1, last: int = 20) -> List[Dict]:
    with open_gateway(port) as g:
        return [n.__dict__ for n in g.scan(first, last)]


def resolve_images(node_ids: List[int], image: Optional[str] = None, project: Optional[str] = None,
                   build: bool = True, same_image_ok: bool = False, log=print) -> Dict[int, str]:
    """node -> image file, from a project's nodeflash.toml, a {id} pattern, or one file."""
    if project:
        p = projmod.load(project)
        if build:
            p.build_for(node_ids, log)
        return p.images(node_ids)
    if not image:
        raise NodeflashError("give an image file (or pattern with {id}) or a project")
    if "{id" in image:
        paths = {i: image.format(id=i) for i in node_ids}
    else:
        if len(node_ids) > 1 and not same_image_ok:
            raise NodeflashError("one image for several nodes: only right if the application reads its node ID at run time. "
                                 "Use a {id} pattern, a project, or confirm (same_image_ok / --same-image)")
        paths = {i: image for i in node_ids}
    for i, path in paths.items():
        if not os.path.exists(path):
            raise NodeflashError(f"image for node {i} not found: {path}")
        problem = check_image(path, open(path, "rb").read())
        if problem:
            raise NodeflashError(f"image for node {i} ({path}): {problem}")
    return paths


def update_nodes(node_ids: Ids, image: Optional[str] = None, project: Optional[str] = None, port: str = "auto",
                 build: bool = True, same_image_ok: bool = False,
                 progress: Optional[Callable[[int, int], None]] = None, log=print) -> List[Dict]:
    """Updates the nodes one at a time. Returns one result per node; never stops at the first failure."""
    node_ids = _ids(node_ids)
    if project and port == "auto":
        port = projmod.load(project).port
    images = resolve_images(node_ids, image, project, build, same_image_ok, log)
    results = []
    with open_gateway(port, log) as g:
        for i in node_ids:
            data = open(images[i], "rb").read()
            log(f"node {i}: {os.path.basename(images[i])}, {len(data)} bytes")
            try:
                r = g.update(i, data, progress)
                r.update(ok=True, image=images[i])
                log(f"node {i}: done in {r['ms'] / 1000:.1f} s, application v{r.get('version')} running")
            except GatewayError as e:
                r = {"node": i, "ok": False, "error": str(e), "image": images[i]}
                log(f"node {i}: FAILED - {e}")
                if "new application does not answer" in str(e):
                    log(f"  (if the image was built for another node ID, recover with: nodeflash rescue {i})")
            results.append(r)
    return results


def rescue(node_id: int, port: str = "auto", seconds: int = 30, log=print) -> Dict:
    with open_gateway(port, log) as g:
        log(f"power-cycle node {node_id} now (listening for its bootloader for {seconds} s) ...")
        return g.rescue(node_id, seconds).__dict__


def factory(node_id: int, app: str, log=print) -> Dict:
    return factory_flash(node_id, app, log=log)


def link(project_dir: str, **kw) -> str:
    return projmod.link(project_dir, **kw)
