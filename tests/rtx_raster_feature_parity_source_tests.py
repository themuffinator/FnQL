from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8", errors="strict")


def cvar_default(source: str, name: str) -> str | None:
    match = re.search(
        rf'ri\.Cvar_Get\(\s*"{re.escape(name)}"\s*,\s*"([^"]*)"', source
    )
    return match.group(1) if match else None


def function_body(source: str, name: str) -> str:
    signature = re.compile(
        rf"^[^\n;]*\b{re.escape(name)}\s*\([^;{{]*\)\s*\{{",
        re.MULTILINE | re.DOTALL,
    )
    match = signature.search(source)
    if match is None:
        return ""

    open_brace = source.find("{", match.start())
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start() : index + 1]
    return source[match.start() :]


def without_if_zero_blocks(source: str) -> str:
    output: list[str] = []
    disabled_depth = 0
    for line in source.splitlines():
        directive = line.lstrip()
        if disabled_depth:
            if directive.startswith(("#if", "#ifdef", "#ifndef")):
                disabled_depth += 1
            elif directive.startswith("#endif"):
                disabled_depth -= 1
            continue
        if re.match(r"#if\s+0(?:\s|$)", directive):
            disabled_depth = 1
            continue
        output.append(line)
    return "\n".join(output)


class RtxRasterFeatureParitySourceTests(unittest.TestCase):
    def assert_marker(self, source: str, marker: str, label: str) -> None:
        self.assertTrue(
            marker in source,
            f"{label}: missing source contract {marker!r}",
        )

    def assert_markers(self, source: str, markers: tuple[str, ...], label: str) -> None:
        for marker in markers:
            with self.subTest(contract=label, marker=marker):
                self.assert_marker(source, marker, label)

    def assert_cvar_defaults(
        self, source: str, defaults: dict[str, str], label: str
    ) -> None:
        for name, expected in defaults.items():
            with self.subTest(contract=label, cvar=name):
                actual = cvar_default(source, name)
                self.assertEqual(
                    actual,
                    expected,
                    f"{label}: {name} default is {actual!r}, expected {expected!r}",
                )

    def assert_ordered(self, source: str, markers: tuple[str, ...], label: str) -> None:
        positions: list[int] = []
        for marker in markers:
            with self.subTest(contract=label, marker=marker):
                position = source.find(marker)
                self.assertGreaterEqual(
                    position,
                    0,
                    f"{label}: missing ordered source contract {marker!r}",
                )
                positions.append(position)
        if len(positions) == len(markers) and all(position >= 0 for position in positions):
            with self.subTest(contract=label, order=markers):
                self.assertEqual(positions, sorted(positions), label)

    def test_model_cel_shading_and_player_highlights_are_end_to_end(self) -> None:
        init = read("code/rendererrtx/tr_init.c")
        header = read("code/rendererrtx/tr_local.h")
        shader = read("code/rendererrtx/tr_shader.c")
        shade = read("code/rendererrtx/tr_shade.c")
        shade_calc = read("code/rendererrtx/tr_shade_calc.c")
        shadows = read("code/rendererrtx/tr_shadows.c")

        self.assert_cvar_defaults(
            init,
            {
                "r_celShading": "0",
                "r_celShadingModelShadows": "1",
                "r_celShadingSteps": "4",
                "r_celOutline": "1",
                "r_celViewWeapon": "1",
                "r_celOutlineColor": "0 0 0 255",
            },
            "cel controls preserve current FnQL defaults",
        )
        self.assert_markers(
            header,
            (
                "enemyRimShader",
                "enemyOutlineShader",
                "RB_EnemyRimTessEnd",
                "RB_EnemyOutlineTessEnd",
                "RB_CelOutlineTessEnd",
                "R_CelQuantizeModelLighting",
            ),
            "cel/player declarations",
        )
        self.assert_markers(
            shader,
            ("<fnql_enemy_rim>", "<fnql_enemy_outline>"),
            "player highlight shaders",
        )
        self.assert_markers(
            shade,
            (
                "tess.shader == tr.enemyRimShader",
                "RB_EnemyRimTessEnd();",
                "tess.shader == tr.enemyOutlineShader",
                "RB_EnemyOutlineTessEnd();",
                "RB_CelOutlineTessEnd();",
            ),
            "cel/player draw dispatch",
        )
        self.assert_markers(
            shade_calc,
            ("R_CelQuantizeModelLighting",),
            "cel lighting quantization is used by shade calculations",
        )
        self.assert_markers(
            shadows,
            (
                "R_CelShadingActive",
                "R_CelOutlineActive",
                "RB_EnemyRimTessEnd",
                "RB_EnemyOutlineTessEnd",
                "RB_CelOutlineTessEnd",
            ),
            "cel/player pass implementation",
        )

    def test_world_outline_runs_after_opaque_geometry_and_protects_bloom(self) -> None:
        init = read("code/rendererrtx/tr_init.c")
        header = read("code/rendererrtx/tr_local.h")
        cmds = read("code/rendererrtx/tr_cmds.c")
        backend = read("code/rendererrtx/tr_backend.c")
        shadows = read("code/rendererrtx/tr_shadows.c")
        vk_header = read("code/rendererrtx/vk.h")
        vk = read("code/rendererrtx/vk.c")
        shader_data = read("code/rendererrtx/shaders/spirv/shader_data.c")

        self.assert_cvar_defaults(
            init,
            {
                "r_celShadingWorld": "0",
                "r_celShadingWorldWidth": "2.0",
                "r_celShadingWorldAlpha": "1.0",
                "r_celShadingWorldDepthThreshold": "0.0015",
            },
            "world cel controls preserve current FnQL defaults",
        )
        with self.subTest(contract="world-outline shader source is retained"):
            self.assertTrue(
                (ROOT / "code/rendererrtx/shaders/world_outline.frag").is_file(),
                "RTX must retain the source shader, not only generated bytes",
            )
        self.assert_markers(
            shader_data,
            ("world_outline_frag_spv",),
            "world-outline SPIR-V is embedded",
        )
        self.assert_markers(
            vk_header,
            ("world_outline_fs", "world_outline_pipeline", "vk_draw_world_cel_outline"),
            "world-outline Vulkan resources",
        )
        opaque_boundary = backend.find("shader->sort > SS_OPAQUE")
        outline_call = backend.find("RB_DrawWorldCelOutlineForScene", opaque_boundary)
        with self.subTest(contract="world outline follows the opaque boundary"):
            self.assertGreaterEqual(
                opaque_boundary,
                0,
                "opaque-sort boundary is missing from the RTX draw loop",
            )
            self.assertGreater(
                outline_call,
                opaque_boundary,
                "world outlines must snapshot only completed opaque BSP depth",
            )
        self.assert_markers(
            vk,
            ("void vk_draw_world_cel_outline",),
            "world-outline Vulkan draw implementation",
        )

        self.assert_markers(
            header,
            ("bloomProtectHighlights", "R_BloomProtectHighlightsActive"),
            "bloom highlight state",
        )
        self.assert_markers(
            cmds,
            ("backEnd.bloomProtectHighlights = qfalse",),
            "bloom protection resets per scene",
        )
        self.assert_markers(
            shadows,
            ("backEnd.bloomProtectHighlights = qtrue",),
            "cel/player passes activate bloom protection",
        )
        bloom = function_body(vk, "vk_bloom")
        self.assert_markers(
            bloom,
            (
                "R_BloomProtectHighlightsActive()",
                "bloom_blend_cel_pipeline",
                "bloom_blend_pipeline",
            ),
            "bloom selects its cel-preserving composite only when needed",
        )

    def test_motion_blur_has_complete_resources_and_default_off_controls(self) -> None:
        init = read("code/rendererrtx/tr_init.c")
        header = read("code/rendererrtx/tr_local.h")
        cmds = read("code/rendererrtx/tr_cmds.c")
        vk_header = read("code/rendererrtx/vk.h")
        vk = read("code/rendererrtx/vk.c")
        shader_data = read("code/rendererrtx/shaders/spirv/shader_data.c")

        self.assert_cvar_defaults(
            init,
            {"r_motionBlur": "0", "r_motionBlurStrength": "0.25"},
            "motion blur remains opt-in",
        )
        with self.subTest(contract="motion-blur shader source is retained"):
            self.assertTrue(
                (ROOT / "code/rendererrtx/shaders/motion_blur.frag").is_file(),
                "RTX must retain the motion-blur shader source",
            )
        self.assert_markers(
            shader_data,
            ("motion_blur_frag_spv",),
            "motion-blur SPIR-V is embedded",
        )
        self.assert_markers(
            vk,
            ('../renderercommon/tr_motion_blur.h"',),
            "RTX shares the bounded motion-blur camera math",
        )
        self.assert_markers(
            header,
            ("doneMotionBlur", "r_motionBlur", "r_motionBlurStrength"),
            "motion-blur renderer state",
        )
        self.assert_markers(
            cmds,
            ("backEnd.doneMotionBlur = qfalse",),
            "motion blur resets for each scene",
        )
        self.assert_markers(
            vk_header,
            (
                "vk_motion_blur",
                "motion_blur_image",
                "motion_blur_descriptor",
                "motion_blur_pipeline",
                "motion_blur_copy_pipeline",
            ),
            "motion-blur Vulkan resources",
        )

    def test_motion_blur_follows_world_fog_and_continues_successful_rt_frames(self) -> None:
        backend = read("code/rendererrtx/tr_backend.c")
        vk = read("code/rendererrtx/vk.c")
        draw_surfs = function_body(backend, "RB_DrawSurfs")
        motion = function_body(vk, "vk_motion_blur")

        self.assert_ordered(
            draw_surfs,
            ("vk_draw_global_fog();", "backEnd.doneSurfaces = qtrue", "vk_motion_blur();"),
            "world/global fog precedes blur and HUD remains outside it",
        )
        call = draw_surfs.find("vk_motion_blur();")
        if call >= 0:
            guard_window = draw_surfs[max(0, call - 400) : call]
            with self.subTest(contract="successful RT composition is not suppressed"):
                self.assertTrue(
                    "renderPassIndex" not in guard_window
                    or "RENDER_PASS_POST_BLOOM" in guard_window,
                    "the backend must not suppress blur after a successful RT composition",
                )
        self.assert_markers(
            motion,
            (
                "RENDER_PASS_MAIN",
                "RENDER_PASS_POST_BLOOM",
                "vk_begin_post_bloom_render_pass",
                "R_MotionBlur_Calculate",
                "backEnd.doneMotionBlur = qtrue",
            ),
            "motion blur accepts and resumes RTX post-bloom composition",
        )

    def test_scene_linear_color_grading_and_crt_are_final_pass_features(self) -> None:
        init = read("code/rendererrtx/tr_init.c")
        gamma = read("code/rendererrtx/shaders/gamma.frag")
        vk = read("code/rendererrtx/vk.c")

        self.assert_cvar_defaults(
            init,
            {
                "r_colorGrade": "0",
                "r_colorGradeLift": "0 0 0",
                "r_colorGradeGamma": "1 1 1",
                "r_colorGradeGain": "1 1 1",
                "r_colorGradeLUT": "",
                "r_crt": "0",
                "r_crtAmount": "1.0",
            },
            "grading and CRT remain opt-in",
        )
        self.assert_markers(
            gamma,
            (
                "colorGradeLut",
                "colorGradeMode",
                "applyColorGrade",
                "applyToneMap",
                "crtMode",
                "applyCRT",
            ),
            "final shader exposes grading and CRT",
        )
        resolve = function_body(gamma, "resolvePostColor")
        main = function_body(gamma, "main")
        self.assert_ordered(
            resolve,
            ("applyColorGrade", "applyToneMap"),
            "scene-linear grading precedes tone mapping",
        )
        self.assert_ordered(
            main,
            ("resolvePostColor", "applyCRT"),
            "CRT runs after the shared final color transform",
        )
        gamma_bind = function_body(vk, "vk_bind_gamma_descriptor_sets")
        self.assert_markers(
            gamma_bind,
            ("vk.color_descriptor", "vk_color_grade_lut_descriptor"),
            "final pass binds scene color and LUT",
        )
        end_frame = function_body(vk, "vk_end_frame")
        self.assert_ordered(
            end_frame,
            (
                "vk_push_post_process_constants();",
                "vk_bind_gamma_descriptor_sets();",
                "qvkCmdDraw",
            ),
            "final-pass runtime constants and descriptors precede drawing",
        )

    def test_native_hdr_is_opt_in_capability_checked_and_falls_back_to_sdr(self) -> None:
        init = read("code/rendererrtx/tr_init.c")
        header = read("code/rendererrtx/vk.h")
        vk = read("code/rendererrtx/vk.c")

        self.assert_cvar_defaults(
            init,
            {
                "r_hdrDisplay": "0",
                "r_outputBackend": "0",
                "r_outputAllowExperimentalLinuxHDR": "0",
            },
            "native HDR is explicit and conservative",
        )
        self.assert_markers(
            header,
            ("hdrDisplayActive", "hdrMetadata", "displayOutput", "outputRequest", "outputBackend"),
            "native HDR capability state",
        )
        defaults = function_body(vk, "vk_init_display_output_defaults")
        self.assert_markers(
            defaults,
            ("ROUTPUT_BACKEND_SDR_SRGB", "hdrHeadroom = 1.0f", "sdrWhiteNits = 203.0f"),
            "display query failure starts from safe SDR defaults",
        )
        query = function_body(vk, "vk_query_display_output")
        self.assert_markers(
            query,
            ("vk_init_display_output_defaults", "ri.GLimp_QueryDisplayOutput"),
            "display output query has a fallback",
        )
        select = function_body(vk, "vk_select_surface_format")
        self.assert_ordered(
            select,
            (
                "vk.outputBackend = ROUTPUT_BACKEND_SDR_SRGB",
                "vk.hdrDisplayActive = qfalse",
                "vk_output_request_wants_hdr10()",
                "!r_fbo->integer",
                "!vk.swapchainColorspace",
                "vk_find_hdr10_surface_format",
                "vk.hdrDisplayActive = qtrue",
            ),
            "HDR activates only after all required capabilities pass",
        )
        with self.subTest(contract="rejected HDR requests report SDR fallback"):
            self.assertGreaterEqual(
                select.count("using SDR presentation"),
                3,
                "each rejected HDR capability should report an honest SDR fallback",
            )
        metadata = function_body(vk, "vk_set_hdr_metadata")
        self.assert_markers(
            metadata,
            ("vk.hdrDisplayActive", "vk.hdrMetadata", "qvkSetHdrMetadataEXT"),
            "HDR metadata is submitted only on an active supported path",
        )

    def test_alpha_to_coverage_is_registered_default_off_and_null_safe(self) -> None:
        init = without_if_zero_blocks(read("code/rendererrtx/tr_init.c"))
        header = read("code/rendererrtx/tr_local.h")
        vk = read("code/rendererrtx/vk.c")
        generic = without_if_zero_blocks(read("code/rendererrtx/shaders/gen_frag.tmpl"))
        lighting = without_if_zero_blocks(read("code/rendererrtx/shaders/light_frag.tmpl"))

        self.assert_cvar_defaults(
            init,
            {"r_ext_alpha_to_coverage": "0"},
            "alpha-to-coverage remains opt-in",
        )
        with self.subTest(contract="alpha-to-coverage declaration is active"):
            declaration = re.search(
                r"(?m)^extern\s+cvar_t\s*\*r_ext_alpha_to_coverage\s*;",
                header,
            )
            self.assertIsNotNone(
                declaration,
                "r_ext_alpha_to_coverage must have an active extern declaration",
            )
        pipeline_guard = re.search(
            r"if\s*\(\s*r_ext_alpha_to_coverage\s*&&\s*"
            r"r_ext_alpha_to_coverage->integer",
            vk,
        )
        with self.subTest(contract="alpha-to-coverage cvar is null-safe"):
            self.assertIsNotNone(
                pipeline_guard,
                "optional A2C state must be checked before dereferencing its cvar",
            )
        for label, shader in (("generic", generic), ("lighting", lighting)):
            with self.subTest(shader=label):
                missing = tuple(
                    marker
                    for marker in (
                        "CorrectAlpha",
                        "alpha_to_coverage != 0",
                        "base.a = CorrectAlpha",
                    )
                    if marker not in shader
                )
                self.assertFalse(
                    missing,
                    f"{label} alpha-test shader is missing A2C contracts {missing!r}",
                )

    def test_bloom_default_matches_fnq3(self) -> None:
        init = read("code/rendererrtx/tr_init.c")
        self.assertEqual(
            cvar_default(init, "r_bloom"),
            "1",
            "the RTX renderer must retain FnQ3's bloom default",
        )


if __name__ == "__main__":
    unittest.main()
