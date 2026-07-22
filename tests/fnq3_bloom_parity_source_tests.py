from __future__ import annotations

import hashlib
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FNQ3_REFERENCE = "915807ea27edd9adf05a283040d6e51d40c66c27"


def normalized_text(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8").replace("\r\n", "\n")


def digest(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def source_slice(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    return source[begin : source.index(end, begin)]


def spirv_bytes(renderer: str, array_name: str) -> bytes:
    source = normalized_text(f"code/{renderer}/shaders/spirv/shader_data.c")
    match = re.search(
        rf"const unsigned char {array_name}\[(\d+)\] = \{{(.*?)\n\}};",
        source,
        re.DOTALL,
    )
    if not match:
        raise AssertionError(f"missing embedded shader array: {renderer}/{array_name}")
    payload = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", match.group(2)))
    if len(payload) != int(match.group(1)):
        raise AssertionError(f"embedded shader length mismatch: {renderer}/{array_name}")
    return payload


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

    def test_fnq3_final_output_contract_and_rtx_extension_are_pinned(self) -> None:
        exact_files = {
            "code/rendererglx/glx_post_shader.cpp":
                "36e6aaa78b85ff0f36be90fed8747a2759ba08abf2f76899a55dc09408ebeaae",
            "code/rendererglx/glx_post_shader_plan.h":
                "33cf4a2920ebb85462a04d4e951bb031db6bed12f9edbf8d13e4f2cc9ca4120e",
            "code/renderervk/shaders/gamma.frag":
                "e4cf09c9732c464859c5b57098216e4f5d5bf9124ba23affb2f4c3679cb4e817",
        }
        for path, expected_hash in exact_files.items():
            with self.subTest(path=path):
                self.assertEqual(
                    expected_hash,
                    digest(normalized_text(path)),
                    f"final-output contract drifted from FnQ3 {FNQ3_REFERENCE}",
                )

        arb = normalized_text("code/renderer/tr_arb.c")
        exact_arb_slices = (
            (
                "static void FBO_SetOutputTransformParams(",
                "static void FBO_SetFramebufferSrgb(",
                "36829ca47ee79bbf3ceac6b130fdb005367cafdb645585e452a7d6746c487150",
            ),
            (
                "static const char *gammaFP = {",
                "static char *ARB_BuildBloomProgram(",
                "478ddcafc22db9e9e0bf4923c01514a293f48dc3f4e3923769b328eaa702c219",
            ),
            (
                "static const char *blend2gammaFP = {",
                "static void RenderQuad(",
                "0c7be6d0d7f3b2b0a8e2e68f3d469b514dfab42dcc408913d2ac86bd6b10b3d4",
            ),
        )
        for start, end, expected_hash in exact_arb_slices:
            with self.subTest(slice=start):
                self.assertEqual(expected_hash, digest(source_slice(arb, start, end)))

        # Vulkan is byte-for-byte FnQ3. RTX retains FnQL's renderer extension;
        # pin its QL-free output shader as a separate non-regression contract.
        embedded = {
            ("renderervk", "gamma_frag_spv"):
                "1fc2d4ff27a0d7beef6fa663d064bb78b4cdb6d23742652f8c1b5873fcccaa46",
            ("rendererrtx", "gamma_frag_spv"):
                "121dcbd24af0e0e8e37bda587ae9cb12f81254013b8a1d85c4a1ea009efacf04",
        }
        for (renderer, array_name), expected_hash in embedded.items():
            with self.subTest(renderer=renderer, shader=array_name):
                self.assertEqual(expected_hash, hashlib.sha256(
                    spirv_bytes(renderer, array_name)
                ).hexdigest())

        vulkan = normalized_text("code/renderervk/vk.c")
        self.assertIn("VkSpecializationMapEntry spec_entries[46];", vulkan)
        self.assertIn("spec_entries[45].constantID = 45;", vulkan)
        rtx = normalized_text("code/rendererrtx/vk.c")
        self.assertIn("VkSpecializationMapEntry spec_entries[45];", rtx)
        self.assertNotIn("VK_FRAG_SPEC_FIELD( 45,", rtx)

    def test_ql_postprocess_pipeline_is_absent(self) -> None:
        engine_root = ROOT / "code"
        forbidden_fingerprints = (
            "RBPP_",
            "RC_BLOOM_POST_PROCESS",
            "RetailBloomPostProcessCommand",
            "bloomPostProcessCommand_t",
            "R_QLUpdateRendererCvars",
            "R_QLRetailContrast",
            "retailContrast",
            "retail_contrast",
            '"r_floatingPointFBOs"',
            '"r_enablePostProcess"',
            '"r_enableBloom"',
            '"r_enableColorCorrect"',
            '"r_postProcessActive"',
            '"r_bloomActive"',
            '"r_colorCorrectActive"',
            '"r_bloomPasses"',
            '"r_bloomIntensity"',
            '"r_bloomBrightThreshold"',
            '"r_bloomBlurScale"',
            '"r_bloomBlurRadius"',
            '"r_bloomBlurFalloff"',
            '"r_bloomSaturation"',
            '"r_bloomSceneIntensity"',
            '"r_bloomSceneSaturation"',
            '"r_contrast"',
            '"r_qlRetailPostProcessBridge"',
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
