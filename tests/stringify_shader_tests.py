from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT_PATH = ROOT / "scripts" / "stringify_shader.py"

spec = importlib.util.spec_from_file_location("stringify_shader", SCRIPT_PATH)
assert spec is not None
stringify_shader = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = stringify_shader
assert spec.loader is not None
spec.loader.exec_module(stringify_shader)


class StringifyShaderTests(unittest.TestCase):
    def test_sanitizes_symbol_name_from_shader_filename(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            shader = root / "1 bloom-extra.frag.glsl"
            output = root / "shader.c"
            shader.write_text('vec4 color = vec4("x");\n', encoding="utf-8")

            stringify_shader.stringify_shader(shader, output)
            generated = output.read_text(encoding="utf-8")

        self.assertIn("const char *fallbackShader__1_bloom_extra_frag =", generated)
        self.assertIn('\\"x\\"', generated)

    def test_rejects_same_input_and_output_path(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            shader = Path(tmp) / "shader.glsl"
            shader.write_text("void main() {}\n", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "different"):
                stringify_shader.stringify_shader(shader, shader)

            self.assertEqual(shader.read_text(encoding="utf-8"), "void main() {}\n")

    def test_preserves_trailing_shader_line_whitespace(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            shader = root / "shader.glsl"
            output = root / "shader.c"
            shader.write_text("vec3 color;   \n", encoding="utf-8")

            stringify_shader.stringify_shader(shader, output)
            generated = output.read_text(encoding="utf-8")

        self.assertIn('"vec3 color;   \\n"', generated)


if __name__ == "__main__":
    unittest.main()
