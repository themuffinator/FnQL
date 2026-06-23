from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class TgaLoaderSourceTests(unittest.TestCase):
    def test_tga_header_uses_explicit_little_endian_reads(self) -> None:
        source = (ROOT / "code" / "renderercommon" / "tr_image_tga.c").read_text(
            encoding="utf-8"
        )
        header_start = source.index("targa_header.id_length = buf_p[0];")
        header_end = source.index("buf_p += 18;", header_start)
        header_block = source[header_start:header_end]

        self.assertIn("static unsigned short R_TGAReadLittleShort", source)
        self.assertIn("targa_header.width = R_TGAReadLittleShort( &buf_p[12] );", header_block)
        self.assertIn("targa_header.height = R_TGAReadLittleShort( &buf_p[14] );", header_block)
        self.assertNotIn("memcpy(&targa_header", header_block)
        self.assertNotIn("LittleShort(targa_header", header_block)


if __name__ == "__main__":
    unittest.main()
