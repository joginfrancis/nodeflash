"""Linking an application project to nodeflash: nodeflash.toml.

    [app]
    name   = "my node app"
    image  = ".pio/build/node{id:02d}/firmware.bin"   # one file for all nodes, or {id} / {id:02d} per node
    build  = "pio run -e node{id:02d}"               # optional, run before uploading ({id} allowed)
    nodes  = "1-20"                                   # node IDs this application is for

    [gateway]
    port = "auto"                                     # or e.g. "COM6"
"""
import os
import re
import subprocess
import tomllib
from dataclasses import dataclass, field
from typing import Dict, List, Optional

from .image import check_image
from .paths import FIRMWARE_LIB

CONFIG_NAME = "nodeflash.toml"


class ProjectError(Exception):
    pass


def parse_ids(text: str) -> List[int]:
    """'1-5,7, 9' -> [1, 2, 3, 4, 5, 7, 9]"""
    ids = set()
    for part in re.split(r"[,\s]+", str(text).strip()):
        if not part:
            continue
        if "-" in part:
            a, b = part.split("-", 1)
            ids.update(range(int(a), int(b) + 1))
        else:
            ids.add(int(part))
    bad = [i for i in ids if not 1 <= i <= 247]
    if bad:
        raise ProjectError(f"node IDs must be 1..247: {bad}")
    return sorted(ids)


@dataclass
class Project:
    root: str
    name: str
    image: str
    build: Optional[str]
    nodes: List[int]
    port: str = "auto"
    raw: Dict = field(default_factory=dict)

    @property
    def per_node(self) -> bool:
        return "{id" in self.image

    def image_for(self, node_id: int) -> str:
        return os.path.normpath(os.path.join(self.root, self.image.format(id=node_id)))

    def build_for(self, node_ids: List[int], log=print):
        if not self.build:
            return
        done = set()
        for i in node_ids:
            cmd = self.build.format(id=i)
            if cmd in done:
                continue
            done.add(cmd)
            log(f"build: {cmd}")
            r = subprocess.run(cmd, shell=True, cwd=self.root, capture_output=True, text=True)
            if r.returncode != 0:
                raise ProjectError(f"build failed: {cmd}\n" + "\n".join((r.stdout + r.stderr).strip().splitlines()[-10:]))

    def images(self, node_ids: List[int]) -> Dict[int, str]:
        """node -> image path, every image checked."""
        out = {}
        for i in node_ids:
            p = self.image_for(i)
            if not os.path.exists(p):
                raise ProjectError(f"image for node {i} not found: {p} (build first?)")
            problem = check_image(p, open(p, "rb").read())
            if problem:
                raise ProjectError(f"image for node {i} ({p}): {problem}")
            out[i] = p
        return out


def load(path: str) -> Project:
    cfg_path = path if path.endswith(".toml") else os.path.join(path, CONFIG_NAME)
    if not os.path.exists(cfg_path):
        raise ProjectError(f"{cfg_path} not found: run 'nodeflash link {path}' first")
    with open(cfg_path, "rb") as f:
        raw = tomllib.load(f)
    app = raw.get("app", {})
    if "image" not in app:
        raise ProjectError(f"{cfg_path}: [app] image = ... is required")
    return Project(root=os.path.dirname(os.path.abspath(cfg_path)), name=app.get("name", os.path.basename(os.path.dirname(os.path.abspath(cfg_path)))),
                   image=app["image"], build=app.get("build"), nodes=parse_ids(app.get("nodes", "1-20")),
                   port=raw.get("gateway", {}).get("port", "auto"), raw=raw)


PIO_LINES = """\
; --- nodeflash: build for 0x08002000 (above the bootloader) and use the client library
board_build.flash_offset = 0x2000
board_upload.offset_address = 0x08002000
lib_deps = symlink://{lib}
"""

TEMPLATE = """\
# nodeflash.toml - links this application to the nodeflash update tool.
# Upload with:  nodeflash update --project <this folder> [--nodes 1-20]

[app]
name  = "{name}"
# One file for all nodes, or {{id}} / {{id:02d}} for one file per node (node ID built in).
image = "{image}"
# Optional: command run before uploading ({{id}} allowed). Remove to upload existing files.
build = "{build}"
# Node IDs this application is for.
nodes = "{nodes}"

[gateway]
port = "auto"   # or e.g. "COM6"
"""


def link(root: str, image: str = None, build: str = None, nodes: str = "1-20", name: str = None, log=print) -> str:
    """Writes nodeflash.toml into an application project and checks its platformio.ini."""
    root = os.path.abspath(root)
    if not os.path.isdir(root):
        raise ProjectError(f"{root} is not a folder")
    ini = os.path.join(root, "platformio.ini")
    envs = []
    if os.path.exists(ini):
        text = open(ini, encoding="utf-8", errors="replace").read()
        envs = re.findall(r"^\[env:([^\]]+)\]", text, re.M)
        if "flash_offset" not in text or "0x2000" not in text:
            try:
                lib = os.path.relpath(FIRMWARE_LIB, root).replace(os.sep, "/")
            except ValueError:  # Windows: application and nodeflash on different drives
                lib = FIRMWARE_LIB.replace(os.sep, "/")
            log("platformio.ini is not built for the bootloader yet. Add to its [env] section:\n")
            log(PIO_LINES.format(lib=lib))
            log("and hand bus packets to nodeflashHandle() (see firmware/nodeflash-lib/src/nodeflash_client.h).\n")
    if image is None:
        # Per-node builds look like node01..node20: 3+ envs, same prefix, numbered from 1.
        numbered = [re.match(r"^(.*?)(\d+)$", e) for e in envs]
        numbered = [m for m in numbered if m]
        prefixes = {m.group(1) for m in numbered}
        id_envs = [m.group(0) for m in numbered] if len(prefixes) == 1 and len(numbered) >= 3 and \
            1 in {int(m.group(2)) for m in numbered} else []
        if id_envs:
            prefix = re.sub(r"\d+$", "", id_envs[0])
            width = len(re.search(r"(\d+)$", id_envs[0]).group(1))
            spec = "{id:0%dd}" % width if width > 1 else "{id}"
            image = f".pio/build/{prefix}{spec}/firmware.bin"
            build = build or f"pio run -e {prefix}{spec}"
        elif envs:
            image = f".pio/build/{envs[0]}/firmware.bin"
            build = build or f"pio run -e {envs[0]}"
        else:
            image = "firmware.bin"
    cfg = os.path.join(root, CONFIG_NAME)
    with open(cfg, "w", encoding="utf-8") as f:
        f.write(TEMPLATE.format(root=root, name=name or os.path.basename(root), image=image, build=build or "", nodes=nodes))
    log(f"wrote {cfg}")
    return cfg
