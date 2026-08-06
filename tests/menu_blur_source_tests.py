"""Structural gates for the in-game menu soft focus.

The effect spans a client trigger, a shared sampling plan, and three renderer
backends. The unit tests in tests/menu_blur_tests.cpp cover the plan's maths;
these gates cover what maths alone cannot: that each backend actually drives the
shared plan rather than reinventing a blur, and that the pieces which have to
agree numerically still reference the same constants.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

VULKAN_BACKENDS = ("code/renderervk", "code/rendererrtx")


def read_text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    """Return the brace-balanced body of the function opening with `signature`."""
    match = re.search(re.escape(signature) + r"\s*\{", source)
    if not match:
        raise AssertionError("Missing function %r" % signature)

    depth = 1
    for index in range(match.end(), len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.end():index]
    raise AssertionError("Unterminated function %r" % signature)


class SharedPlanTests(unittest.TestCase):
    def test_plan_is_owned_once_and_shared(self) -> None:
        policy = read_text("code/renderercommon/tr_menu_blur.h")

        self.assertIn("qboolean R_MenuBlur_Plan(", policy)
        self.assertIn("float R_MenuBlur_TapSpacing(", policy)
        self.assertIn("#define MENU_BLUR_PASS_COUNT 4", policy)
        self.assertIn("#define MENU_BLUR_LEVEL_DIVISOR 4", policy)
        self.assertIn("#define MENU_BLUR_SIGMA_FRACTION", policy)
        self.assertIn("#define MENU_BLUR_KERNEL_VARIANCE_BINOMIAL6", policy)
        self.assertIn("#define MENU_BLUR_KERNEL_VARIANCE_LINEAR3", policy)

        # Every backend must reach the plan through the shared header.
        for path in (
            "code/renderer/tr_arb.c",
            "code/renderervk/vk.c",
            "code/rendererrtx/vk.c",
        ):
            source = read_text(path)
            with self.subTest(path=path):
                self.assertIn('#include "../renderercommon/tr_menu_blur.h"', source)
                self.assertIn("R_MenuBlur_Plan(", source)
                self.assertIn("R_MenuBlur_TapSpacing(", source)

    def test_backends_pass_their_own_kernel_variance(self) -> None:
        """Backends agree on sigma, not on tap offsets: GL runs a 6-tap binomial
        kernel and the Vulkan pair a 3-tap linear one, so each converts the plan's
        sigma with its own variance constant."""
        gl = read_text("code/renderer/tr_arb.c")
        self.assertIn("MENU_BLUR_KERNEL_VARIANCE_BINOMIAL6", gl)
        self.assertNotIn("MENU_BLUR_KERNEL_VARIANCE_LINEAR3", gl)

        for base in VULKAN_BACKENDS:
            source = read_text(base + "/vk.c")
            with self.subTest(base=base):
                self.assertIn("MENU_BLUR_KERNEL_VARIANCE_LINEAR3", source)
                self.assertIn("MENU_BLUR_LINEAR3_TAP", source)
                self.assertNotIn("MENU_BLUR_KERNEL_VARIANCE_BINOMIAL6", source)


class ClientTriggerTests(unittest.TestCase):
    def test_single_cvar_gates_the_effect(self) -> None:
        main = read_text("code/client/cl_main.cpp")
        header = read_text("code/client/client.h")

        self.assertIn('cl_menuBlur = Cvar_Get( "cl_menuBlur", "1", CVAR_ARCHIVE );', main)
        self.assertIn('Cvar_CheckRange( cl_menuBlur, "0", "1", CV_FLOAT );', main)
        self.assertIn("extern\tcvar_t\t*cl_menuBlur;", header)

        # The binned depth-of-field path must be gone everywhere, not renamed in
        # one place and left behind in another.
        for path in (
            "code/client/cl_main.cpp",
            "code/client/client.h",
            "code/client/cl_scrn.cpp",
            "code/client/cl_webui.cpp",
            "code/client/webui/fnql-settings.js",
            "code/renderercommon/tr_public.h",
            "code/renderer/tr_arb.c",
            "code/renderer/tr_backend.c",
            "code/renderer/tr_cmds.c",
            "code/renderer/tr_local.h",
            "code/renderervk/tr_cmds.c",
            "code/rendererrtx/tr_cmds.c",
        ):
            with self.subTest(path=path):
                self.assertNotIn("menuDepthOfField", read_text(path))
                self.assertNotIn("MenuDepthOfField", read_text(path))

    def test_only_an_in_game_menu_triggers_it(self) -> None:
        """Gameplay must never be softened, and a fullscreen menu has no scene
        behind it to soften."""
        scrn = read_text("code/client/cl_scrn.cpp")

        self.assertIn("SCR_UpdateMenuBlurStrength(\n\t\tuiVisible && !uiFullscreen && cls.state == CA_ACTIVE );", scrn)
        self.assertIn("if ( menuBlurStrength > 0.0f && re.DrawMenuBlur ) {", scrn)
        self.assertIn("re.DrawMenuBlur( menuBlurStrength );", scrn)

        # The ramp is wall-clock paced, so the pull into focus takes the same
        # time regardless of frame rate.
        self.assertIn("kMenuBlurFadeMsec", scrn)
        self.assertIn("delta = previousTime ? cls.realtime - previousTime : 0;", scrn)
        self.assertIn("if ( delta < 0 || delta > 1000 ) {", scrn)

    def test_refexport_carries_the_strength(self) -> None:
        public = read_text("code/renderercommon/tr_public.h")
        self.assertIn("void\t(*DrawMenuBlur)( float strength );", public)

        for path in ("code/renderer", "code/renderervk", "code/rendererrtx"):
            init = read_text(path + "/tr_init.c")
            with self.subTest(path=path):
                self.assertIn("re.DrawMenuBlur = RE_DrawMenuBlur;", init)


class BackendPipelineTests(unittest.TestCase):
    def test_every_backend_queues_a_render_command(self) -> None:
        """The effect samples the finished frame, so it cannot run from the
        frontend: it has to be a command that executes in draw order, after the
        HUD and before the menu."""
        for base in ("code/renderer", "code/renderervk", "code/rendererrtx"):
            local = read_text(base + "/tr_local.h")
            cmds = read_text(base + "/tr_cmds.c")
            backend = read_text(base + "/tr_backend.c")
            with self.subTest(base=base):
                self.assertIn("} menuBlurCommand_t;", local)
                self.assertIn("RC_MENU_BLUR,", local)
                self.assertIn("cmd->commandId = RC_MENU_BLUR;", cmds)
                self.assertIn("case RC_MENU_BLUR:", backend)
                self.assertIn("data = RB_MenuBlur(data);", backend)
                # The queued geometry has to land on the target before it can be
                # read back as the blur source.
                self.assertIn("RB_EndSurface();", backend)

    def test_gl_backend_builds_the_pyramid(self) -> None:
        gl = read_text("code/renderer/tr_arb.c")

        self.assertIn("static frameBuffer_t menuBlurBuffers[ 3 ];", gl)
        body = function_body(gl, "void FBO_MenuBlur( float strength )")
        # Two exact halvings, not one 4:1 step.
        self.assertEqual(body.count("GL_COLOR_BUFFER_BIT, GL_LINEAR );"), 2)
        self.assertIn("FBO_MenuBlurPass( &menuBlurBuffers[1], &menuBlurBuffers[2],", body)
        self.assertIn("for ( pass = 0; pass < plan.passCount; ++pass )", body)
        # The kernel the plan's variance constant describes.
        self.assertIn("#define MENU_BLUR_GL_TAPS 6", gl)
        # Every pyramid buffer is released on renderer shutdown.
        for index in range(3):
            self.assertIn("FBO_Clean(&menuBlurBuffers[%d]);" % index, gl)

    def test_vulkan_backends_build_the_pyramid(self) -> None:
        for base in VULKAN_BACKENDS:
            source = read_text(base + "/vk.c")
            header = read_text(base + "/vk.h")
            with self.subTest(base=base):
                self.assertIn("#define VK_NUM_MENU_BLUR_IMAGES 3", header)
                # The pool is fixed size; the pyramid has to be budgeted in it.
                self.assertIn("VK_NUM_MENU_BLUR_IMAGES+VK_NUM_BLOOM_PASSES*2", header)
                self.assertIn("qboolean vk_menu_blur( float strength );", header)
                self.assertIn("qboolean vk_menu_blur( float strength )", source)
                self.assertIn("vk_create_menu_blur_attachments( usage );", source)
                self.assertIn("vk_menu_blur_begin_level( 0 );", source)
                self.assertIn("vk_menu_blur_begin_level( 1 );", source)
                self.assertIn("vk_menu_blur_begin_level( 2 );", source)
                self.assertIn("vk_begin_main_render_pass_load();", source)
                # Descriptor pool budget must cover the pyramid too.
                self.assertIn("VK_NUM_MENU_BLUR_IMAGES + ( 1 + VK_NUM_BLOOM_PASSES * 2 )", source)

    def test_vulkan_backends_release_everything_they_create(self) -> None:
        """A leaked render pass or pipeline survives vid_restart and then points
        at a destroyed attachment."""
        for base in VULKAN_BACKENDS:
            source = read_text(base + "/vk.c")
            with self.subTest(base=base):
                self.assertIn("qvkDestroyRenderPass( vk.device, vk.render_pass.menu_blur, NULL );", source)
                self.assertIn("qvkDestroyFramebuffer( vk.device, vk.framebuffers.menu_blur[n], NULL );", source)
                for name in ( "half", "level", "composite" ):
                    self.assertIn(
                        "qvkDestroyPipeline( vk.device, vk.menu_blur_%s_pipeline, NULL );" % name,
                        source,
                    )
                # The images themselves, however each backend spells the release.
                self.assertRegex(
                    source,
                    r"(vk_destroy_image_and_view\( &vk\.menu_blur_image\[i\]"
                    r"|qvkDestroyImage\( vk\.device, vk\.menu_blur_image\[i\], NULL \))",
                )
                self.assertIn("vk.menu_blur_width[i] = 0;", source)

    def test_vulkan_shader_matches_the_tabulated_kernel(self) -> None:
        """The shader's weights and tap positions are what
        MENU_BLUR_KERNEL_VARIANCE_LINEAR3 describes. If one moves without the
        other, the Vulkan backends quietly stop matching GL."""
        for base in VULKAN_BACKENDS:
            shader = read_text(base + "/shaders/menu_blur.frag")
            blob = read_text(base + "/shaders/spirv/shader_data.c")
            with self.subTest(base=base):
                self.assertIn("( 6.0 / 16.0 )", shader)
                self.assertEqual(shader.count("( 5.0 / 16.0 )"), 2)
                self.assertIn("tex_coord0 + offset", shader)
                self.assertIn("tex_coord0 - offset", shader)
                self.assertIn("layout(push_constant) uniform Push", shader)
                # A zero offset has to degenerate to a plain resample, which is
                # what the downsample and composite steps rely on.
                self.assertIn("vec2 offset = pc.params.xy;", shader)
                # The compiled blob has to be checked in beside the source.
                self.assertIn("const unsigned char menu_blur_frag_spv[", blob)
                self.assertIn("vk.modules.menu_blur_fs = SHADER_MODULE( menu_blur_frag_spv );",
                    read_text(base + "/vk.c"))

    def test_shader_sources_are_identical_across_the_vulkan_backends(self) -> None:
        first = read_text(VULKAN_BACKENDS[0] + "/shaders/menu_blur.frag")
        second = read_text(VULKAN_BACKENDS[1] + "/shaders/menu_blur.frag")
        self.assertEqual(first, second)

    def test_backends_explain_why_they_declined(self) -> None:
        """The predecessor of this effect failed silently, which made an
        unsupported configuration look identical to a disabled cvar."""
        self.assertIn("FBO_MenuBlurDecline(", read_text("code/renderer/tr_arb.c"))
        for base in VULKAN_BACKENDS:
            source = read_text(base + "/vk.c")
            with self.subTest(base=base):
                self.assertIn("vk_menu_blur_decline(", source)
                self.assertIn('ri.Printf( PRINT_DEVELOPER, "Menu soft focus unavailable: %s\\n", reason );', source)


if __name__ == "__main__":
    unittest.main()
