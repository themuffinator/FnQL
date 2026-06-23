from __future__ import annotations

import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
import shutil


BSP_HEADER_LUMPS = 17
BSP_HEADER_SIZE = 8 + BSP_HEADER_LUMPS * 8
BSP_LUMP_SHADERS = 1
BSP_LUMP_LEAFS = 4
BSP_LUMP_LEAF_SURFACES = 5
BSP_LUMP_LEAF_BRUSHES = 6
BSP_LUMP_BRUSHES = 8
BSP_LUMP_BRUSH_SIDES = 9
BSP_LUMP_SURFACES = 13


def i32(value: int) -> bytes:
    return struct.pack("<i", value)


def fixed_qpath(name: str) -> bytes:
    encoded = name.encode("ascii")
    if len(encoded) >= 64:
        raise ValueError("qpath is too long for the test BSP")
    return encoded + b"\0" * (64 - len(encoded))


def leaf_record(
    cluster: int,
    area: int,
    mins: tuple[int, int, int],
    maxs: tuple[int, int, int],
    first_leaf_surface: int,
    num_leaf_surfaces: int,
) -> bytes:
    return b"".join(
        [
            i32(cluster),
            i32(area),
            i32(mins[0]),
            i32(mins[1]),
            i32(mins[2]),
            i32(maxs[0]),
            i32(maxs[1]),
            i32(maxs[2]),
            i32(first_leaf_surface),
            i32(num_leaf_surfaces),
            i32(0),
            i32(0),
        ]
    )


def surface_record(shader_index: int) -> bytes:
    surface = bytearray(104)
    surface[0:4] = i32(shader_index)
    surface[8:12] = i32(1)
    return bytes(surface)


def build_bsp(
    shaders: list[str],
    leaves: list[tuple[int, int, tuple[int, int, int], tuple[int, int, int], int]],
) -> bytes:
    shader_lump = b"".join(fixed_qpath(name) + i32(0) + i32(0) for name in shaders)
    leaf_lump = bytearray()
    leaf_surfaces = bytearray()
    surfaces = bytearray()

    for cluster, area, mins, maxs, shader_index in leaves:
        surface_index = len(surfaces) // 104
        first_leaf_surface = len(leaf_surfaces) // 4
        surfaces.extend(surface_record(shader_index))
        leaf_surfaces.extend(i32(surface_index))
        leaf_lump.extend(leaf_record(cluster, area, mins, maxs, first_leaf_surface, 1))

    lump_data: dict[int, bytes] = {
        BSP_LUMP_SHADERS: shader_lump,
        BSP_LUMP_LEAFS: bytes(leaf_lump),
        BSP_LUMP_LEAF_SURFACES: bytes(leaf_surfaces),
        BSP_LUMP_LEAF_BRUSHES: b"",
        BSP_LUMP_BRUSHES: b"",
        BSP_LUMP_BRUSH_SIDES: b"",
        BSP_LUMP_SURFACES: bytes(surfaces),
    }
    lumps: list[tuple[int, int]] = []
    body = bytearray()
    offset = BSP_HEADER_SIZE
    for index in range(BSP_HEADER_LUMPS):
        payload = lump_data.get(index, b"")
        lumps.append((offset, len(payload)))
        body.extend(payload)
        offset += len(payload)

    header = bytearray()
    header.extend(b"IBSP")
    header.extend(i32(46))
    for lump_offset, lump_length in lumps:
        header.extend(i32(lump_offset))
        header.extend(i32(lump_length))
    return bytes(header + body)


def build_minimal_bsp(shader_name: str) -> bytes:
    return build_bsp([shader_name], [(0, 0, (0, 0, 0), (128, 128, 128), 0)])


def resolve_tool() -> Path:
    if len(sys.argv) < 2:
        raise AssertionError("missing fnql-audiozonesc path")
    tool = Path(sys.argv[1])
    if tool.parent == Path("."):
        local_tool = Path.cwd() / tool.name
        if local_tool.exists():
            return local_tool
        found = shutil.which(tool.name)
        if found:
            return Path(found)
    return tool


class AudioZoneMaterialMapTests(unittest.TestCase):
    def generate_and_dump(self, bsp_bytes: bytes, material_map: str | None = None) -> str:
        tool = resolve_tool()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bsp = root / "generated.bsp"
            bsp.write_bytes(bsp_bytes)
            output = root / "generated.azb"
            command = [str(tool), "--from-bsp", "-o", str(output)]
            if material_map is not None:
                material_map_path = root / "audio-materials.txt"
                material_map_path.write_text(material_map, encoding="utf-8")
                command.extend(["--material-map", str(material_map_path)])
            command.append(str(bsp))

            generated = subprocess.run(
                command,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)

            dumped = subprocess.run(
                [str(tool), "--dump", str(output)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
            self.assertEqual(dumped.returncode, 0, dumped.stderr)
            return dumped.stdout

    def test_material_map_overrides_shader_classification(self) -> None:
        tool = resolve_tool()

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            bsp = root / "material_map.bsp"
            bsp.write_bytes(build_minimal_bsp("textures/custom/mystery_panel"))
            material_map = root / "audio-materials.txt"
            material_map.write_text(
                "textures/custom/* metal preset hallway flag outdoor weight 8\n",
                encoding="utf-8",
            )
            output = root / "material_map.azb"

            generated = subprocess.run(
                [
                    str(tool),
                    "--from-bsp",
                    "--material-map",
                    str(material_map),
                    "-o",
                    str(output),
                    str(bsp),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            self.assertIn("using 1 material rule", generated.stdout)

            dumped = subprocess.run(
                [str(tool), "--dump", str(output)],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
            self.assertEqual(dumped.returncode, 0, dumped.stderr)
            self.assertIn("audiozones version 3", dumped.stdout)
            self.assertIn("preset hallway", dumped.stdout)
            self.assertIn("material metal", dumped.stdout)
            self.assertIn("flags 3", dumped.stdout)

    def test_split_leaf_classification_uses_aggregate_room_bounds(self) -> None:
        dumped = self.generate_and_dump(
            build_bsp(
                ["textures/plain/wall"],
                [
                    (0, 0, (0, 0, 0), (64, 512, 128), 0),
                    (0, 0, (64, 0, 0), (512, 512, 128), 0),
                ],
            )
        )

        self.assertIn("audiozones version 3, zones 1", dumped)
        self.assertIn("preset room", dumped)
        self.assertNotIn("preset hallway", dumped)

    def test_adjacent_split_zones_merge_across_clusters(self) -> None:
        dumped = self.generate_and_dump(
            build_bsp(
                ["textures/plain/wall"],
                [
                    (0, 0, (0, 0, 0), (256, 512, 128), 0),
                    (1, 0, (256, 0, 0), (512, 512, 128), 0),
                ],
            )
        )

        self.assertIn("audiozones version 3, zones 1", dumped)
        self.assertIn("preset room", dumped)

    def test_doorway_connected_room_and_hallway_stay_separate_with_portal(self) -> None:
        dumped = self.generate_and_dump(
            build_bsp(
                ["textures/plain/wall"],
                [
                    (0, 0, (0, 0, 0), (512, 512, 128), 0),
                    (1, 0, (512, 192, 0), (896, 320, 128), 0),
                ],
            )
        )

        self.assertIn("audiozones version 3, zones 2", dumped)
        self.assertIn("preset room", dumped)
        self.assertIn("preset hallway", dumped)
        self.assertEqual(dumped.count("  portal target "), 2)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
