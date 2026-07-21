from __future__ import annotations

import hashlib
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FNQ3_REFERENCE = "915807ea27edd9adf05a283040d6e51d40c66c27"


def normalized_text(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8").replace("\r\n", "\n")


def digest(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


class FnQ3BloomParitySourceTests(unittest.TestCase):
    """Pin the bloom implementation to the current FnQ3 reference."""

    def test_glx_bloom_implementation_matches_fnq3(self) -> None:
        arb = normalized_text("code/renderer/tr_arb.c")
        start = arb.index("qboolean FBO_Bloom(")
        end = arb.index("void R_BloomScreen(", start)

        self.assertEqual(
            "6183bf711c1b90f13378a7448da16265f2cac0a95bc3de818233176c7b1a0918",
            digest(arb[start:end]),
            f"FBO_Bloom drifted from FnQ3 {FNQ3_REFERENCE}",
        )

        expected = {
            "code/rendererglx/glx_postprocess.cpp":
                "2eef135b46432384de08fb2d1f6c11ceb04047b2cd9f69e3cecc650b19f9dc45",
            "code/rendererglx/glx_postprocess.h":
                "ab51ae91bb72365bb4a62e4f3ab35ebf032777fb96bfb3af9823819bc918a774",
        }
        for path, expected_hash in expected.items():
            with self.subTest(path=path):
                self.assertEqual(expected_hash, digest(normalized_text(path)))

    def test_vulkan_bloom_implementation_matches_fnq3(self) -> None:
        vulkan = normalized_text("code/renderervk/vk.c")
        start = vulkan.index("qboolean vk_bloom(")
        self.assertEqual(
            "ee4a2926a1abf84d3838766e627a93c5885ee3dcabdec4efcdfd8a8d01d75bbc",
            digest(vulkan[start:]),
            f"vk_bloom drifted from FnQ3 {FNQ3_REFERENCE}",
        )

    def test_rtx_bloom_is_fnq3_with_the_fnq3_vulkan_highlight_guard(self) -> None:
        rtx = normalized_text("code/rendererrtx/vk.c")
        start = rtx.index("qboolean vk_bloom(")
        bloom = rtx[start:]
        fnq3_rtx_binding = (
            "\t\tqvkCmdBindPipeline( vk.cmd->command_buffer, "
            "VK_PIPELINE_BIND_POINT_GRAPHICS, vk.bloom_blend_pipeline );"
        )
        fnq3_vulkan_parity_binding = (
            "\t\tqvkCmdBindPipeline( vk.cmd->command_buffer,\n"
            "\t\t\tVK_PIPELINE_BIND_POINT_GRAPHICS,\n"
            "\t\t\tR_BloomProtectHighlightsActive() &&\n"
            "\t\t\t\tvk.bloom_blend_cel_pipeline != VK_NULL_HANDLE ?\n"
            "\t\t\t\t\tvk.bloom_blend_cel_pipeline : vk.bloom_blend_pipeline );"
        )

        self.assertEqual(1, bloom.count(fnq3_vulkan_parity_binding))
        fnq3_rtx_bloom = bloom.replace(
            fnq3_vulkan_parity_binding,
            fnq3_rtx_binding,
        )
        self.assertEqual(
            "6c197147fe11cedd616869236e77a3aec0b5683c0d3d1822d04ec74a71d3cc68",
            digest(fnq3_rtx_bloom),
            "RTX vk_bloom drifted beyond the highlight guard already used by FnQ3 Vulkan",
        )

    def test_vulkan_family_bloom_shaders_match_fnq3(self) -> None:
        expected = {
            "bloom.frag":
                "827670bde1dbe2e93a2a658344ee5551f5e08f651e4bd37e45fa902357ff13aa",
            "blur.frag":
                "599937271bc05392f3926c5b9f4f801d828faef22c347380a536617c77d0197d",
            "blend.frag":
                "0f5d11e26e64493e664c6946b0d9edb088476d63c8fe70faa5dc27d9d23991de",
        }
        for renderer in ("renderervk", "rendererrtx"):
            for filename, expected_hash in expected.items():
                path = f"code/{renderer}/shaders/{filename}"
                with self.subTest(path=path):
                    self.assertEqual(expected_hash, digest(normalized_text(path)))

    def test_ql_bloom_pipeline_is_absent(self) -> None:
        engine_root = ROOT / "code"
        forbidden_fingerprints = (
            "RBPP_",
            "RC_BLOOM_POST_PROCESS",
            "RetailBloomPostProcessCommand",
            "bloomPostProcessCommand_t",
            '"r_enableBloom"',
            '"r_bloomActive"',
            '"r_bloomPasses"',
            '"r_bloomIntensity"',
            '"r_bloomBrightThreshold"',
            '"r_bloomBlurScale"',
            '"r_bloomBlurRadius"',
            '"r_bloomBlurFalloff"',
            '"r_bloomSaturation"',
            '"r_bloomSceneIntensity"',
            '"r_bloomSceneSaturation"',
        )
        source_suffixes = {
            ".c", ".cc", ".cpp", ".cxx", ".h", ".hpp",
            ".frag", ".vert", ".comp", ".geom", ".glsl",
            ".rgen", ".rchit", ".rmiss",
        }

        for path in engine_root.rglob("*"):
            if not path.is_file() or path.suffix not in source_suffixes:
                continue
            source = path.read_text(encoding="utf-8")
            for fingerprint in forbidden_fingerprints:
                with self.subTest(path=path.relative_to(ROOT), fingerprint=fingerprint):
                    self.assertNotIn(fingerprint, source)

    def test_renderer_bloom_defaults_match_fnq3(self) -> None:
        glx = normalized_text("code/renderer/tr_init.c")
        vulkan = normalized_text("code/renderervk/tr_init.c")
        rtx = normalized_text("code/rendererrtx/tr_init.c")

        for registration in (
            'ri.Cvar_Get( "r_fbo", "1"',
            'ri.Cvar_Get( "r_hdr", "0"',
            'ri.Cvar_Get( "r_bloom", "0"',
        ):
            self.assertIn(registration, glx)

        for name, renderer in (("vk", vulkan), ("rtx", rtx)):
            with self.subTest(renderer=name):
                for registration in (
                    'ri.Cvar_Get( "r_fbo", "1"',
                    'ri.Cvar_Get( "r_hdr", "1"',
                    'ri.Cvar_Get( "r_bloom", "1"',
                ):
                    self.assertIn(registration, renderer)

    def test_desktop_dynamic_light_default_matches_fnq3(self) -> None:
        client = normalized_text("code/client/cl_cgame.cpp")
        self.assertIn('Cvar_Get( "r_dlightMode", "2", 0 )', client)

        for path in (
            "code/renderer/tr_init.c",
            "code/renderervk/tr_init.c",
            "code/rendererrtx/tr_init.c",
        ):
            source = normalized_text(path)
            with self.subTest(path=path):
                self.assertIn(
                    'r_dlightMode = ri.Cvar_Get( "r_dlightMode", "2", CVAR_ARCHIVE );',
                    source,
                )
                self.assertIn(
                    'r_dlightMode = ri.Cvar_Get( "r_dlightMode", "0", CVAR_ARCHIVE );',
                    source,
                )


if __name__ == "__main__":
    unittest.main()
