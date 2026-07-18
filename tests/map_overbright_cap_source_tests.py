from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def shift_rgb(rgb: tuple[int, int, int], shift: int, cap: int) -> tuple[int, int, int]:
    if shift >= 0:
        shifted = tuple(channel << shift for channel in rgb)
    else:
        shifted = tuple(channel >> -shift for channel in rgb)

    maximum = max(shifted)
    if maximum <= 255:
        return shifted
    return tuple(channel * cap // maximum for channel in shifted)


class MapOverbrightCapSourceTests(unittest.TestCase):
    def test_retail_default_and_bounded_chroma_normalization(self) -> None:
        self.assertEqual(shift_rgb((200, 100, 50), 2, 255), (255, 127, 63))
        self.assertEqual(shift_rgb((200, 100, 50), 2, 128), (128, 64, 32))
        self.assertEqual(shift_rgb((200, 100, 50), 2, 0), (0, 0, 0))
        self.assertEqual(shift_rgb((10, 20, 30), 0, 64), (10, 20, 30))

    def test_all_renderers_register_the_retail_latched_cloud_cvar(self) -> None:
        registration = (
            'ri.Cvar_Get( "r_mapOverBrightCap", "255", '
            'CVAR_ARCHIVE | CVAR_LATCH | CVAR_VM_CREATED | CVAR_CLOUD )'
        )
        for renderer in ("renderer", "renderervk", "rendererrtx"):
            init = read(f"code/{renderer}/tr_init.c")
            local = read(f"code/{renderer}/tr_local.h")
            self.assertIn("cvar_t\t*r_mapOverBrightCap;", init)
            self.assertIn("extern\tcvar_t\t*r_mapOverBrightCap;", local)
            self.assertIn(registration, init)
            self.assertIn(
                'ri.Cvar_CheckRange( r_mapOverBrightCap, "0", "255", CV_FLOAT );',
                init,
            )

    def test_byte_lightmap_and_lightgrid_paths_use_the_configured_cap(self) -> None:
        for renderer in ("renderer", "renderervk", "rendererrtx"):
            bsp = read(f"code/{renderer}/tr_bsp.c")
            self.assertIn(
                "const int\tcap = r_mapOverBrightCap ? r_mapOverBrightCap->integer : 255;",
                bsp,
            )
            self.assertIn("r = r * cap / max;", bsp)
            self.assertIn("g = g * cap / max;", bsp)
            self.assertIn("b = b * cap / max;", bsp)
            self.assertNotIn("r = r * 255 / max;", bsp)


if __name__ == "__main__":
    unittest.main()
