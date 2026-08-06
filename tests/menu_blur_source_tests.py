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

    def test_settings_menu_exposes_the_control(self) -> None:
        """The cvar has to be reachable without the console: a slider on a live
        settings route, and published in the snapshot the overlay reads."""
        script = read_text("code/client/webui/fnql-settings.js")
        snapshot = read_text("code/client/cl_webui.cpp")
        main = read_text("code/client/cl_main.cpp")

        self.assertIn(
            "{ name: 'cl_menuBlur', title: 'In-Game Menu Soft Focus', "
            "type: 'range', min: 0, max: 1, step: 0.05,",
            script,
        )
        # Without this the overlay renders the row but never learns the value.
        self.assertIn('"cl_menuBlur",', snapshot)

        # The row lives on a route the retail settings UI actually navigates to,
        # not in an orphaned group that nothing appends.
        groups = script[script.index("var SECTION_GROUPS = {"):]
        self.assertLess(groups.index("game: ["), groups.index("'cl_menuBlur'"))
        self.assertIn("'game': 'game',", script)

        # A slider that cannot reach the cvar's bounds, or that overshoots them,
        # is the regression worth catching - the two ranges have to agree.
        self.assertIn('Cvar_CheckRange( cl_menuBlur, "0", "1", CV_FLOAT );', main)

    def test_only_an_in_game_menu_triggers_it(self) -> None:
        """Gameplay must never be softened, and a fullscreen menu has no scene
        behind it to soften."""
        scrn = read_text("code/client/cl_scrn.cpp")

<<<<<<< Updated upstream
        self.assertIn(
            "SCR_UpdateMenuBlurStrength(\n"
            "\t\tuiVisible && !uiFullscreen && cls.state == CA_ACTIVE );",
=======
        # The request carries the same browserSuppressUiRefresh term the menu's
        # own draw does. Without it the whole pyramid ran every frame while the
        # browser owned the surface, with nothing sharp drawn over it.
        self.assertIn(
            "SCR_UpdateMenuBlurStrength(\n"
            "\t\tuiVisible && !uiFullscreen && !browserSuppressUiRefresh\n"
            "\t\t&& cls.state == CA_ACTIVE );",
>>>>>>> Stashed changes
            scrn,
        )
        self.assertIn("if ( menuBlurStrength > 0.0f && re.DrawMenuBlur ) {", scrn)
        self.assertIn("re.DrawMenuBlur( menuBlurStrength );", scrn)

        # The ramp is wall-clock paced, so the pull into focus takes the same
        # time regardless of frame rate.
        self.assertIn("kMenuBlurFadeMsec", scrn)
        self.assertIn("delta = previousTime ? cls.realtime - previousTime : 0;", scrn)
        self.assertIn("if ( delta < 0 || delta > 1000 ) {", scrn)
        # A discarded delta must HOLD, not snap. Snapping defeated the fade on
        # the stereo second eye, on zero-millisecond frames, and on exactly the
        # hitch the clamp above discards - where a snap is most visible.
        self.assertNotIn("if ( delta <= 0 ) {\n\t\tstrength = target;", scrn)
        # A non-finite cvar value passes Com_Clamp and latches strength to NaN
        # for the session; the ramp can never recover from it.
        self.assertIn("cl_menuBlur->value == cl_menuBlur->value", scrn)

    def test_backends_require_a_live_frame_not_a_sticky_index(self) -> None:
        """vk.renderPassIndex is sticky state, not a recording flag:
        vk_end_render_pass leaves it set and vk_end_frame re-arms it to
        RENDER_PASS_MAIN after the command buffer is ended and submitted. A
        mid-frame drain therefore leaves it reading MAIN on a dead command
        buffer, and ending a render pass there faults the device."""
        for base in VULKAN_BACKENDS:
            body = function_body(
                read_text(base + "/vk.c"), "qboolean vk_menu_blur( float strength )"
            )
            with self.subTest(base=base):
                self.assertIn("vk.frame_count == 0 ||", body)
                self.assertIn(
                    'vk_menu_blur_decline( "no scene render pass is open on this frame" );',
                    body,
                )
                # The old proxy must not come back: it masked this by accident,
                # and it is what kept every 2D-only screen from being softened.
                self.assertNotIn("!backEnd.doneSurfaces", body)

        # renderervk tracks the recording pass directly, so it tests both halves
        # the way vk_capture_liquid_scene does for the same end/resume trick.
        vk_body = function_body(
            read_text("code/renderervk/vk.c"), "qboolean vk_menu_blur( float strength )"
        )
        self.assertIn("vk_current_render_pass != vk.render_pass.main &&", vk_body)

    def test_vulkan_backends_restore_the_mvp_push_constant(self) -> None:
        """vk_menu_blur_draw pushes through vk.pipeline_layout_post_process,
        which is not push-constant compatible with vk.pipeline_layout, so the
        64-byte MVP is undefined afterwards. This is the only post-process
        detour that runs while backEnd.projection2D is already set, so the next
        RB_StretchPic skips RB_SetGL2D and nothing else re-pushes it."""
        for base in VULKAN_BACKENDS:
            body = function_body(
                read_text(base + "/vk.c"), "qboolean vk_menu_blur( float strength )"
            )
            with self.subTest(base=base):
                self.assertIn("vk_update_mvp( NULL );", body)
                self.assertIn("vk.cmd->last_pipeline = VK_NULL_HANDLE;", body)
                self.assertIn(
                    "MIN( VK_DESC_COUNT, vk.maxBoundDescriptorSets ) - 1;", body
                )

    def test_gl_resolves_multisample_only_after_every_decline(self) -> None:
        """Consuming blitMSfbo disarms the frame's only multisample resolve and
        switches the draw target, so a later decline stranded the rest of the
        frame in a buffer that is never blitted."""
        body = function_body(
            read_text("code/renderer/tr_arb.c"), "void FBO_MenuBlur( float strength )"
        )
        resolve = body.index("FBO_BlitMS( qfalse );")
        for decline in (
            '"no readable scene framebuffer"',
            '"the scene framebuffer is not sampleable"',
            '"the soft-focus pyramid could not be allocated"',
        ):
            with self.subTest(decline=decline):
                self.assertLess(body.index(decline), resolve)
        # FBO_Create and FBO_Clean both leave framebuffer 0 bound, so the
        # allocation decline has to hand the caller's target back or every later
        # UI draw lands in the back buffer and is erased by FBO_PostProcess.
        self.assertIn("FBO_Bind( GL_FRAMEBUFFER, source->fbo );", body)

    def test_vulkan_backends_destroy_the_shader_module(self) -> None:
        """Created unconditionally beside the other post-process modules; it was
        the only one with no matching destroy, so vid_restart leaked one."""
        for base in VULKAN_BACKENDS:
            source = read_text(base + "/vk.c")
            with self.subTest(base=base):
                self.assertIn(
                    "qvkDestroyShaderModule(vk.device, vk.modules.menu_blur_fs, NULL);",
                    source,
                )

    def test_only_one_layer_is_requested(self) -> None:
        """The connection dialog, the level-loading screen and the console are
        all 2D-only frames. Requesting the effect there faulted the device: the
        backends need a 3D pass this frame to have something to read back. The
        in-game menu is the only layer with a scene behind it."""
        body = function_body(
            read_text("code/client/cl_scrn.cpp"),
            "static void SCR_DrawScreenField( stereoFrame_t stereoFrame )",
        )

        self.assertEqual(body.count("re.DrawMenuBlur("), 1)
        self.assertEqual(body.count("SCR_UpdateMenuBlurStrength("), 1)
        for absent in (
            "connectBlurStrength",
            "consoleBlurStrength",
            "KEYCATCH_CONSOLE",
            # Nothing may finish bloom on the client's behalf either: that moves
            # when the frame resolves, and the console does it for itself.
            "re.FinishBloom",
        ):
            with self.subTest(absent=absent):
                self.assertNotIn(absent, body)

    def test_backends_require_a_3d_pass(self) -> None:
        """backEnd.doneSurfaces is the only signal a backend has that the render
        target holds a scene this composite may read back. Dropping it to soften
        the 2D-only connect and loading screens faulted the device on the first
        such frame, so all three keep it."""
        gl = function_body(
            read_text("code/renderer/tr_arb.c"), "void FBO_MenuBlur( float strength )"
        )
        self.assertIn(
            "if ( !backEnd.doneSurfaces || backEnd.framePostProcessed "
            "|| ri.CL_IsMinimized() ) {",
            gl,
        )

        for base in VULKAN_BACKENDS:
            body = function_body(
                read_text(base + "/vk.c"), "qboolean vk_menu_blur( float strength )"
            )
            with self.subTest(base=base):
                self.assertIn(
                    "if ( !backEnd.doneSurfaces || ri.CL_IsMinimized() ) {", body
                )

    def test_cgame_overlays_stay_sharp(self) -> None:
        """cgame draws the scene, the HUD, and its own overlays in one
        CG_DRAW_ACTIVE_FRAME call, so any request queued after it would soften
        the overlay itself rather than the frame behind it."""
        body = function_body(
            read_text("code/client/cl_scrn.cpp"),
            "static void SCR_DrawScreenField( stereoFrame_t stereoFrame )",
        )

        trigger = body.index("SCR_UpdateMenuBlurStrength(")
        end = body.index(";", trigger)
        self.assertNotIn("KEYCATCH_CGAME", body[trigger:end])

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

    def test_vulkan_backends_null_the_pipeline_cache(self) -> None:
        """vk_menu_blur_draw binds descriptor set 0 through
        vk.pipeline_layout_post_process, and binding via an incompatible layout
        disturbs the sets bound for vk.pipeline_layout. Nulling the cache is what
        forces the next draw to rebind them; handing the frame's pipeline back
        instead lets that draw run with disturbed sets and faults the device."""
        for base in VULKAN_BACKENDS:
            body = function_body(
                read_text(base + "/vk.c"), "qboolean vk_menu_blur( float strength )"
            )
            with self.subTest(base=base):
                self.assertIn("vk.cmd->last_pipeline = VK_NULL_HANDLE;", body)
                self.assertNotIn("restore_pipeline", body)

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
