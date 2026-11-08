"""nodeflash.toml linking and node ID parsing (no hardware needed)."""
import os

import pytest

from nodeflash import project
from nodeflash.project import ProjectError, parse_ids


def test_parse_ids():
    assert parse_ids("1-3, 7,9") == [1, 2, 3, 7, 9]
    assert parse_ids("5") == [5]
    with pytest.raises(ProjectError):
        parse_ids("0-3")


def write_ini(tmp_path, envs, offset=True):
    lines = ["[env]", "board = nucleo_g070rb"]
    if offset:
        lines.append("board_build.flash_offset = 0x2000")
    for e in envs:
        lines += [f"[env:{e}]", "build_flags = -DX"]
    (tmp_path / "platformio.ini").write_text("\n".join(lines))


def test_link_guesses_per_node_builds(tmp_path):
    write_ini(tmp_path, [f"node{i:02d}" for i in range(1, 21)])
    project.link(str(tmp_path), log=lambda s: None)
    p = project.load(str(tmp_path))
    assert p.per_node and p.nodes == list(range(1, 21))
    assert p.image_for(7).replace(os.sep, "/").endswith(".pio/build/node07/firmware.bin")
    assert p.build.format(id=7) == "pio run -e node07"


def test_link_does_not_mistake_versions_for_ids(tmp_path):
    write_ini(tmp_path, ["app_v1", "app_v2"])
    project.link(str(tmp_path), log=lambda s: None)
    p = project.load(str(tmp_path))
    assert not p.per_node
    assert p.image.endswith("app_v1/firmware.bin")


def test_link_warns_without_offset(tmp_path):
    write_ini(tmp_path, ["app"], offset=False)
    said = []
    project.link(str(tmp_path), log=said.append)
    assert any("flash_offset" in s for s in said)


def test_missing_image_is_reported(tmp_path):
    write_ini(tmp_path, ["app"])
    project.link(str(tmp_path), log=lambda s: None)
    with pytest.raises(ProjectError, match="not found"):
        project.load(str(tmp_path)).images([1])
