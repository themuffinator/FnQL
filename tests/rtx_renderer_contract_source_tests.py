from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8", errors="strict")


def cvar_default(source: str, name: str) -> str:
    match = re.search(
        rf'ri\.Cvar_Get\(\s*"{re.escape(name)}"\s*,\s*"([^"]+)"', source
    )
    if match is None:
        raise AssertionError(f"renderer does not register {name}")
    return match.group(1)


def source_section(source: str, start: str, end: str) -> str:
    start_index = source.index(start)
    return source[start_index : source.index(end, start_index)]


def source_section_last(source: str, start: str, end: str) -> str:
    """Select a definition when the same signature also has a prototype."""
    start_index = source.rindex(start)
    return source[start_index : source.index(end, start_index)]


class RtxRendererContractSourceTests(unittest.TestCase):
    _LIGHT_SAFETY_MARKER = re.compile(
        r"(?:finite|isfinite|valid|safe|bounded|within[_A-Za-z]*range)",
        re.IGNORECASE,
    )

    def assert_light_validation_precedes_append(
        self,
        block: str,
        append_anchor: str,
        subjects: tuple[str, ...],
        label: str,
    ) -> None:
        append = block.index(append_anchor)
        prefix = block[:append]
        marker = self._LIGHT_SAFETY_MARKER.search(prefix)

        self.assertIsNotNone(marker, f"{label} has no finite/range validation")
        for subject in subjects:
            self.assertIn(
                subject,
                prefix,
                f"{label} does not validate {subject} before reserving a GPU slot",
            )

        assert marker is not None
        rejection = re.search(r"(?:continue|return)\b[^;]*;", prefix[marker.start() :])
        self.assertIsNotNone(
            rejection,
            f"{label} validation does not reject the light before the append",
        )

    def test_rtx_is_part_of_the_supported_three_renderer_contract(self) -> None:
        options = read("meson_options.txt")
        meson = read("meson.build")
        cmake = read("CMakeLists.txt")
        makefile = read("Makefile")
        workflow = read(".github/workflows/vulkan-verification.yml")

        self.assertIn("choices: ['glx', 'vk', 'rtx']", options)
        renderers_option = options[
            options.index("option('renderers'") : options.index("option('renderer-default'")
        ]
        self.assertIn("value: ['glx', 'vk', 'rtx']", renderers_option)
        self.assertIn("renderer_prefix + '_rtx_' + renderer_arch", meson)
        self.assertIn("renderer_static_rtx", meson)
        self.assertIn("-DRENDERER_RTX", meson)
        self.assertIn("renderer_default == 'rtx'", meson)

        # The static RTX verification target links the complete Linux client,
        # including its native ALSA backend, rather than only the renderer.
        self.assertIn("libasound2-dev", workflow)

        focused_ci = source_section(
            workflow,
            "- name: Configure focused Meson Vulkan-family build",
            "- name: Configure static RTX client build",
        )
        self.assertIn("-Dbuild-client=true", focused_ci)
        self.assertIn("-Drenderer-default=${{ matrix.renderer }}", focused_ci)
        self.assertIn(
            '"fnql_${{ matrix.renderer }}_x86_64"',
            focused_ci,
        )

        # Static clients also provide ABI-level host-font fallbacks. Every
        # supported build frontend must keep the renderer's implementations
        # private so the two definitions cannot collide at final link time.
        for symbol in ("RE_DrawScaledText", "RE_MeasureScaledText"):
            remap = f"{symbol}=R_RTX_{symbol.removeprefix('RE_')}"
            self.assertIn(f"-D{remap}", meson)
            self.assertIn(remap, cmake)
            self.assertIn(f"-D{remap}", makefile)

        static_ci = source_section(
            workflow,
            "- name: Configure static RTX client build",
            "  vulkan-gate-plans:",
        )
        self.assertIn("-Dbuild-client=true", static_ci)
        self.assertIn(
            "meson compile -C .tmp/meson-rtx-static-verification fnql.x86_64",
            static_ci,
        )

    def test_rtx_exports_the_complete_quake_live_renderer_api_13_surface(self) -> None:
        public = read("code/renderercommon/tr_public.h")
        init = read("code/rendererrtx/tr_init.c")
        vulkan_init = read("code/renderervk/tr_init.c")
        common_font = read("code/renderercommon/tr_font.c")
        stash = read("code/renderercommon/tr_font_stash.c")

        self.assertIn("#define\tREF_API_VERSION\t\t13", public)
        self.assertIn("Com_Memset( &re, 0, sizeof( re ) );", init)
        exports = (
            "re.AdvertisementBridge_UpdateLoadingViewParameters = AdvertisementBridge_UpdateLoadingViewParameters;",
            "re.DrawWebUISurface = RE_DrawWebUISurface;",
            "re.RegisterShaderFromRGBA = RE_RegisterShaderFromRGBA;",
            "re.DrawScaledText = RE_DrawScaledText;",
            "re.MeasureScaledText = RE_MeasureScaledText;",
            "re.GetScaledFontMetrics = RE_GetScaledFontMetrics;",
            "re.GetFontAtlasDebugShader = RE_GetFontAtlasDebugShader;",
        )
        for export in exports:
            self.assertIn(export, init)

        assignment_pattern = re.compile(r"\bre\.([A-Za-z_][A-Za-z0-9_]*)\s*=")
        self.assertEqual(
            set(assignment_pattern.findall(init)),
            set(assignment_pattern.findall(vulkan_init)),
            "RTX must export the complete current FnQL renderer ABI, not only the new API-13 entries",
        )

        self.assertIn("R_InitHostFonts();", common_font)
        self.assertIn("R_DoneHostFonts();", common_font)
        self.assertIn("defined( RENDERER_RTX )", stash)

    def test_quake_live_bsp_47_and_advertisements_are_first_class(self) -> None:
        bsp = read("code/rendererrtx/tr_bsp.c")
        world = read("code/rendererrtx/tr_world.c")
        scene = read("code/rendererrtx/tr_scene.c")
        init = read("code/rendererrtx/tr_init.c")
        public = read("code/renderercommon/tr_public.h")

        self.assertIn('"../qcommon/ql_bsp.h"', bsp)
        self.assertIn("header->version != BSP_VERSION_QL", bsp)
        self.assertIn("QLBSP_LumpRangeValid", bsp)
        self.assertIn("QLBSP_ReadAdvertisementLump", bsp)
        self.assertIn("QLBSP_AdvertisementLumpShapeValid", bsp)
        self.assertIn("R_LoadAdvertisements( &qlAdvertisementsLump );", bsp)
        self.assertIn("void R_UpdateAdvertisements( void )", world)
        self.assertIn("advertisement->cellId", world)
        self.assertIn("AdvertisementBridge_GetCellDisplayState", public)
        self.assertIn("AdvertisementBridge_GetCellLabel", public)
        self.assertIn("AdvertisementBridge_UpdateLoadingViewParameters", scene)
        self.assertIn('ri.Cmd_AddCommand( "advertlist", R_AdvertisementList_f );', init)
        self.assertIn('ri.Cmd_RemoveCommand( "advertlist" );', init)

    def test_rtx_keeps_ql_view_flags_and_does_not_reintroduce_fov_correction(self) -> None:
        renderer_sources = "\n".join(
            path.read_text(encoding="utf-8", errors="ignore")
            for path in (ROOT / "code" / "rendererrtx").glob("*.[ch]")
        )
        main = read("code/rendererrtx/tr_main.c")

        self.assertIn("RDF_NOFIRSTPERSON", main)
        self.assertNotIn("r_fovCorrection", renderer_sources)
        self.assertNotIn("RDF_NOFOVCORRECTION", renderer_sources)

    def test_new_presentation_and_rt_features_are_conservative_by_default(self) -> None:
        init = read("code/rendererrtx/tr_init.c")
        expected_defaults = {
            "r_globalFog": "0",
            "r_fogMode": "0",
            # FnQ3's RTX path enables its raster HDR staging by default; this
            # is distinct from enabling ray tracing or HDR display output.
            "r_hdr": "1",
            "r_liquid": "0",
            "r_surfaceLightProxies": "0",
            "rtx_rt_mode": "0",
            "rtx_rt_require": "0",
            "rtx_rt_async_overlap": "0",
            "rtx_rt_dynamic_blas": "0",
            "rtx_rt_material_heuristics": "0",
        }
        for cvar, expected in expected_defaults.items():
            self.assertEqual(cvar_default(init, cvar), expected, cvar)

    def test_global_fog_sidecar_is_not_touched_when_feature_is_disabled(self) -> None:
        bsp = read("code/rendererrtx/tr_bsp.c")
        loader = bsp[bsp.index("static void R_LoadGlobalFog") : bsp.index("static void R_LoadEntities")]
        gate = loader.index("!r_globalFog || !r_globalFog->integer")
        read_file = loader.index("ri.FS_ReadFile")
        self.assertLess(gate, read_file)
        self.assertIn("GLOBAL_FOG_SIDECAR_MAX_BYTES", loader)
        self.assertIn("int expectedSize;", loader)
        self.assertIn("int loadedSize;", loader)
        self.assertIn("loadedSize != expectedSize", loader)

    def test_webui_upload_is_exported_and_bounded_before_byte_arithmetic(self) -> None:
        backend = read("code/rendererrtx/tr_backend.c")
        init = read("code/rendererrtx/tr_init.c")
        block = backend[
            backend.index("void RE_DrawWebUISurface") : backend.index("RB_DebugPolygon")
        ]
        bounds = block.index("cols > 4096 || rows > 4096")
        multiplication = block.index("cols * rows * 4")
        self.assertLess(bounds, multiplication)
        self.assertIn("vk_upload_image_data", block)
        self.assertIn("re.DrawWebUISurface = RE_DrawWebUISurface;", init)

    def test_rt_buffers_do_not_confuse_vulkan_allocation_padding_with_capacity(self) -> None:
        header = read("code/rendererrtx/vk.h")
        vk = read("code/rendererrtx/vk.c")
        create_start = vk.index("static qboolean vk_rt_create_buffer( VkDeviceSize size", 1000)
        create_start = vk.index("static qboolean vk_rt_create_buffer( VkDeviceSize size", create_start + 1)
        create = vk[create_start : vk.index("static void vk_rt_destroy_buffer", create_start)]
        ensure_start = vk.index("static qboolean vk_rt_ensure_buffer_capacity", create_start)
        ensure = vk[ensure_start : vk.index("static qboolean vk_rt_upload_buffer_data", ensure_start)]
        upload_start = vk.index("static qboolean vk_rt_upload_buffer_data", ensure_start)
        upload = vk[upload_start : vk.index("static qboolean vk_rt_upload_material_buffer", upload_start)]

        self.assertIn("VkDeviceSize capacity;", header)
        self.assertIn("VkDeviceSize allocationSize;", header)
        self.assertIn("outBuffer->capacity = size;", create)
        self.assertIn("outBuffer->allocationSize = memoryRequirements.size;", create)
        self.assertNotIn("outBuffer->capacity = memoryRequirements.size;", create)
        self.assertIn("buffer->capacity >= allocSize", ensure)
        self.assertIn("uploadSize > buffer->capacity", upload)
        self.assertIn("flushSize > buffer->allocationSize", upload)

    def test_rt_geometry_growth_is_checked_and_bad_indices_are_rejected_by_triple(self) -> None:
        vk = read("code/rendererrtx/vk.c")
        growth_start = vk.index("static qboolean vk_rt_cpu_array_growth")
        growth_end = vk.index(
            "static qboolean vk_rt_cpu_geometry_reserve_materials", growth_start
        )
        growth = vk[growth_start:growth_end]
        triangles_start = vk.index(
            "static qboolean vk_rt_append_surface_triangles_geometry", 1000
        )
        triangles_start = vk.index(
            "static qboolean vk_rt_append_surface_triangles_geometry",
            triangles_start + 1,
        )
        triangles = vk[
            triangles_start : vk.index("static qboolean vk_rt_append_poly_geometry", triangles_start)
        ]

        self.assertIn("(size_t)INT_MAX / elementSize", growth)
        self.assertIn("additionalVertices > UINT32_MAX - geometry->numVertices", growth)
        self.assertIn("additionalIndices > UINT32_MAX - geometry->numIndices", growth)
        self.assertIn("vk_rt_checked_u32_multiply", vk)
        self.assertGreaterEqual(vk.count("vk_rt_checked_u32_multiply("), 7)

        self.assertIn("completeIndexCount = surface->numIndexes - surface->numIndexes % 3", triangles)
        self.assertGreaterEqual(triangles.count("i += 3"), 2)
        validation = triangles.index("surface->indexes[i + 0] < 0")
        append = triangles.index("vk_rt_cpu_geometry_add_triangle")
        self.assertLess(validation, append)
        self.assertIn("surface->indexes[i + 1]", triangles[validation:append])
        self.assertIn("surface->indexes[i + 2]", triangles[validation:append])

    def test_static_light_sidecar_is_bounded_strict_and_not_recursive(self) -> None:
        bsp = read("code/rendererrtx/tr_bsp.c")
        parser = bsp[bsp.index("#define STATIC_MAP_LIGHTS_SIDECAR_MAX_BYTES") : bsp.index("void R_StaticMapLightsReload_f")]
        loader = parser[parser.index("static void R_LoadStaticMapLightsForWorld") :]

        self.assertIn("STATIC_MAP_LIGHTS_SIDECAR_MAX_BYTES ( 256 * 1024 )", parser)
        self.assertIn("STATIC_MAP_LIGHTS_MAX_TOKENS 65536", parser)
        self.assertIn("STATIC_MAP_LIGHTS_MAX_DEPTH 32", parser)
        self.assertIn("char closers[STATIC_MAP_LIGHTS_MAX_DEPTH]", parser)
        self.assertIn("strtod( token, &end )", parser)
        self.assertIn("strtol( token, &end, 10 )", parser)
        self.assertIn("*end != '\\0'", parser)
        self.assertIn("errno == ERANGE", parser)
        self.assertIn("parsed >= -(double)FLT_MAX && parsed <= (double)FLT_MAX", parser)
        self.assertIn("R_StaticMapLightsMarkField", parser)
        self.assertIn("tr.staticMapLights.version != 1", parser)
        self.assertIn(
            "return ( *token || s_staticMapLightsTokenCount >= STATIC_MAP_LIGHTS_MAX_TOKENS ) ?",
            parser,
        )

        self.assertIn("expectedSize = ri.FS_ReadFile( filename, NULL );", loader)
        self.assertIn("loadedSize = ri.FS_ReadFile( filename, &buffer.v );", loader)
        self.assertIn("loadedSize != expectedSize", loader)
        self.assertIn("memchr( buffer.c, '\\0', (size_t)loadedSize )", loader)

    def test_rt_dlights_keep_raster_shadow_state_and_independent_rt_policy(self) -> None:
        header = read("code/rendererrtx/tr_local.h")
        scene = read("code/rendererrtx/tr_scene.c")
        dlight = source_section(
            header,
            "typedef struct dlight_s {",
            "} dlight_t;",
        )

        self.assertIn("qboolean castsRtShadows;", dlight)
        for raster_field in (
            "qboolean shadowEligible;",
            "qboolean shadowPlanned;",
            "int shadowIndex;",
            "int shadowAtlasBaseFace;",
            "int shadowAtlasFaceSize;",
            "int shadowAtlasX[DLIGHT_SHADOW_FACES];",
            "int shadowAtlasY[DLIGHT_SHADOW_FACES];",
            "int shadowReceiverCount;",
            "float shadowPriority;",
            "float shadowPriorityMultiplier;",
            "int shadowSpotSource;",
            "int shadowSpotSourceIndex;",
        ):
            self.assertIn(raster_field, dlight)

        dynamic = source_section(
            scene,
            "void RE_AddDynamicLightToScene",
            "void RE_AddLinearLightToScene",
        )
        linear = source_section(
            scene,
            "void RE_AddLinearLightToScene",
            "void RE_AddAdditiveLightToScene",
        )
        for label, block in (("point", dynamic), ("linear", linear)):
            with self.subTest(light=label):
                self.assertIn("dl->castsRtShadows = qtrue;", block)
                self.assertIn("dl->shadowEligible = qtrue;", block)
                self.assertIn("dl->shadowPlanned = qfalse;", block)
                self.assertIn("dl->shadowIndex = -1;", block)
                self.assertIn("dl->shadowPriorityMultiplier = 1.0f;", block)

        static_lights = source_section(
            scene,
            "static void R_AddStaticMapLightsToScene",
            "static float R_SurfaceLightProxy",
        )
        self.assertIn("dl->castsRtShadows = light->castsShadows;", static_lights)
        self.assertIn("dl->shadowSpotSource = SHADOW_SPOT_SOURCE_STATIC_MAP;", static_lights)
        self.assertIn("dl->shadowEligible = ( light->type == MAP_LIGHT_POINT", static_lights)
        self.assertIn("r_staticLightShadows && r_staticLightShadows->integer", static_lights)
        self.assertIn("dl->shadowPriorityMultiplier = light->designerPriority", static_lights)

    def test_raster_shadow_planning_is_unconditional_and_precedes_rt_trace(self) -> None:
        main = read("code/rendererrtx/tr_main.c")
        backend = read("code/rendererrtx/tr_backend.c")
        generate = source_section(
            main,
            "static void R_GenerateDrawSurfs( void )",
            "R_RenderView",
        )

        self.assertIn("R_AddEntitySurfaces();", generate)
        self.assertIn("R_PlanFrameShadows();", generate)
        entity_surfaces = generate.index("R_AddEntitySurfaces();")
        shadow_plan = generate.index("R_PlanFrameShadows();")
        self.assertLess(entity_surfaces, shadow_plan)
        self.assertNotIn(
            "rtx_rt_",
            generate,
            "raster shadow planning must still run when rtx_rt_mode is 0",
        )

        draw = source_section(
            backend,
            "static const void *RB_DrawSurfs( const void *data )",
            "static const void *RB_DrawBuffer( const void *data )",
        )
        ordered = (
            "tr.shadowManager = cmd->shadowManager;",
            "RB_RunScheduledShadowManagerPreMainPasses( cmd->drawSurfs, cmd->numDrawSurfs );",
            "RB_BeginDrawingView();",
            "RB_RenderDrawSurfList(",
            "vk_rt_trace_frame();",
        )
        for token in ordered:
            self.assertIn(token, draw)
        positions = [draw.index(token) for token in ordered]
        self.assertEqual(positions, sorted(positions))

    def test_authored_world_sun_state_drives_csm_without_losing_rt_inputs(self) -> None:
        header = read("code/rendererrtx/tr_local.h")
        bsp = read("code/rendererrtx/tr_bsp.c")
        main = read("code/rendererrtx/tr_main.c")

        self.assertIn("} worldSun_t;", header)
        for field_type, field_name in (
            ("worldSun_t", "worldSun"),
            ("vec3_t", "sunLight"),
            ("vec3_t", "sunColor"),
            ("float", "sunIntensity"),
            ("qboolean", "sunParmsValid"),
            ("vec3_t", "sunDirection"),
        ):
            self.assertRegex(header, rf"\b{field_type}\s+{field_name}\s*;")

        clear = source_section(
            bsp,
            "static void R_ClearWorldSun( void )",
            "static void R_SetWorldSunFromShader(",
        )
        authored = source_section(
            bsp,
            "static void R_SetWorldSunFromShader(",
            "static void R_ClearStaticMapLights(",
        )
        self.assertIn("Com_Memset( &tr.worldSun", clear)
        self.assertIn("tr.sunParmsValid = qfalse;", clear)
        self.assertIn("tr.worldSun.valid = qtrue;", authored)
        self.assertIn("VectorCopy( tr.worldSun.light, tr.sunLight );", authored)
        self.assertIn("VectorCopy( tr.worldSun.color, tr.sunColor );", authored)
        self.assertIn("tr.sunIntensity = tr.worldSun.intensity;", authored)
        self.assertIn("tr.sunParmsValid = qtrue;", authored)

        planner = source_section(
            main,
            "static void R_PlanCascadedShadows( void )",
            "/*\n=============\nR_PlaneForSurface",
        )
        self.assertIn("!tr.worldSun.valid", planner)
        self.assertIn("!tr.sunParmsValid", planner)
        self.assertIn("tr.sunIntensity <= 0.0f", planner)
        self.assertIn("tr.worldSun.lightDirection", planner)
        self.assertIn("tr.sunColor", planner)

    def test_rt_gpu_light_inputs_are_finite_and_bounded_before_append(self) -> None:
        vk = read("code/rendererrtx/vk.c")

        surface = source_section_last(
            vk,
            "static uint32_t vk_rt_append_surface_lights(",
            "static qboolean vk_rt_update_light_buffer(",
        )
        with self.subTest(input="surface proxy"):
            self.assert_light_validation_precedes_append(
                surface,
                "dst = &lights[count++]",
                ("proxy->origin", "proxy->color", "proxy->normal", "proxy->radius"),
                "surface proxy light",
            )

        upload = source_section_last(
            vk,
            "static qboolean vk_rt_update_light_buffer( void )",
            "static void vk_rt_destroy_as(",
        )
        gameplay = source_section(
            upload,
            "for ( i = 0; i < backEnd.refdef.num_dlights",
            "count = vk_rt_append_surface_lights",
        )
        with self.subTest(input="gameplay dlight"):
            self.assert_light_validation_precedes_append(
                gameplay,
                "dst = &lights[count++]",
                ("dl->origin", "dl->color", "dl->origin2", "radius"),
                "gameplay dynamic light",
            )

        cache = source_section_last(
            vk,
            "static void vk_rt_rebuild_world_light_cache( void )",
            "static uint32_t vk_rt_append_world_entity_lights(",
        )
        accepted_entity = cache[cache.index("if ( !isLight || !hasOrigin )") :]
        with self.subTest(input="world entity"):
            self.assert_light_validation_precedes_append(
                accepted_entity,
                "s_vkRtWorldLightCache.count++",
                ("origin", "color", "radiusValue"),
                "world entity light",
            )

        sun = source_section_last(
            vk,
            "static void vk_rt_resolve_sun_params(",
            "static void vk_rt_reset_world_light_cache(",
        )
        raw_sun = sun.index("VectorCopy( tr.sunDirection, outDirection )")
        safety = self._LIGHT_SAFETY_MARKER.search(sun, raw_sun)
        with self.subTest(input="sun"):
            self.assertIsNotNone(
                safety,
                "authored/fallback sun direction and color are not sanitized after input",
            )
            self.assertIn("outDirection", sun[safety.start() :] if safety else "")
            self.assertIn("outColor", sun[safety.start() :] if safety else "")

        bounds = source_section_last(
            vk,
            "static void vk_rt_estimate_world_bounds(",
            "static void vk_rt_resolve_sun_params(",
        )
        combine = bounds.index("VectorAdd( mins, maxs, center )")
        with self.subTest(input="world bounds"):
            self.assertIsNotNone(
                self._LIGHT_SAFETY_MARKER.search(bounds[:combine]),
                "malformed BSP bounds reach fallback-sun arithmetic without validation",
            )
            self.assertIn("bounds[0]", bounds[:combine])
            self.assertIn("bounds[1]", bounds[:combine])

    def test_world_entity_duplicate_classname_uses_the_last_value(self) -> None:
        vk = read("code/rendererrtx/vk.c")
        cache = source_section_last(
            vk,
            "static void vk_rt_rebuild_world_light_cache( void )",
            "static uint32_t vk_rt_append_world_entity_lights(",
        )
        classname = source_section(
            cache,
            'if ( !Q_stricmp( keyName, "classname" ) ) {',
            'if ( !Q_stricmp( keyName, "origin" ) ) {',
        )

        direct_overwrite = re.search(
            r"isLight\s*=\s*[^;]*(?:lightJunior|Q_stricmp)[^;]*;",
            classname,
            re.DOTALL,
        )
        reset_before_set = re.search(
            r"isLight\s*=\s*qfalse\s*;.*?"
            r"if\s*\(.*?(?:lightJunior|Q_stricmp).*?\).*?"
            r"isLight\s*=\s*qtrue\s*;",
            classname,
            re.DOTALL,
        )
        self.assertTrue(
            direct_overwrite or reset_before_set,
            "a later non-light classname must clear an earlier light classification",
        )

    def test_hybrid_frame_has_complementary_base_overlay_and_fail_closed_modes(self) -> None:
        backend = read("code/rendererrtx/tr_backend.c")
        vk = read("code/rendererrtx/vk.c")

        self.assertIn("RB_DRAWSURFS_RT_BASE", backend)
        self.assertIn("RB_DRAWSURFS_RT_OVERLAY", backend)
        self.assertIn("vk_rt_primary_view_eligible()", backend)
        self.assertIn("preserving the complete raster frame", vk)
        self.assertIn(
            "rtx_rt_require 1 primary-view dispatch or scene-color copy failed; refusing silent raster fallback",
            backend,
        )

    def test_native_material_ownership_is_conservative_for_retail_content(self) -> None:
        init = read("code/rendererrtx/tr_init.c")
        shader = read("code/rendererrtx/tr_shader.c")
        vk = read("code/rendererrtx/vk.c")

        self.assertEqual(cvar_default(init, "rtx_rt_material_heuristics"), "0")
        native = shader[
            shader.index("qboolean R_RtShaderNativeSupported") : shader.index(
                "shader_t *R_FindShaderByName"
            )
        ]
        self.assertIn("lightmapBundleCount == 0", native)
        self.assertIn("current RT vertex/material ABI carries only base UVs", native)

        material_start = vk.index("static void vk_rt_translate_shader_to_material")
        material_start = vk.index(
            "static void vk_rt_translate_shader_to_material", material_start + 1
        )
        material = vk[
            material_start : vk.index(
                "static qboolean vk_rt_cpu_geometry_find_or_add_material_ex",
                material_start,
            )
        ]
        shader_heuristics = material.index("if ( useHeuristics )")
        stage_semantics = material.index("for ( i = 0; i < shader->numUnfoggedPasses; i++ )")
        self.assertLess(shader_heuristics, material.index("SURF_METALSTEPS"))
        self.assertLess(material.index("SURF_METALSTEPS"), stage_semantics)
        self.assertLess(shader_heuristics, material.index('shader->name, "metal"'))
        self.assertLess(material.index('shader->name, "metal"'), stage_semantics)
        self.assertIn("if ( useHeuristics && !lightmap && image && image->imgName )", material)
        self.assertIn("vk_rt_consume_cvar_modified( rtx_rt_material_heuristics )", vk)

        poly_start = vk.index("static qboolean vk_rt_poly_is_mark_decal")
        poly_start = vk.index("static qboolean vk_rt_poly_is_mark_decal", poly_start + 1)
        poly = vk[
            poly_start : vk.index("static qboolean vk_rt_extract_dynamic_geometry", poly_start)
        ]
        self.assertIn("!rtx_rt_material_heuristics", poly)
        self.assertLess(poly.index("!rtx_rt_material_heuristics"), poly.index('shader->name, "decal"'))

    def test_screenshot_capture_preserves_fnql_metadata_watermark_and_safety_contracts(
        self,
    ) -> None:
        init = read("code/rendererrtx/tr_init.c")
        screenshot = init[
            init.index("void RB_TakeScreenshotPNG")
            : init.index("const void *RB_TakeVideoFrameCmd")
        ]
        levelshot = init[
            init.index("void RB_TakeLevelShot")
            : init.index("static void R_ScheduleLevelShot")
        ]
        command = init[
            init.index("static void R_ScreenShot_f")
            : init.index("//============================================================================")
        ]

        self.assertIn('renderercommon/tr_levelshot.h"', init)
        self.assertIn("R_LevelshotCheckedRgbBytes", levelshot)
        self.assertIn(
            "R_LevelshotFloatIsFinite( r_levelshotDownscale->value )", init
        )
        self.assertGreaterEqual(
            screenshot.count("R_ApplyScreenshotWatermark("), 4
        )
        self.assertGreaterEqual(
            screenshot.count("R_WriteScreenshotViewpos("), 4
        )
        self.assertEqual(cvar_default(init, "r_screenshotWriteViewpos"), "0")
        self.assertEqual(cvar_default(init, "r_screenshotCaptureMode"), "0")
        self.assertIn(
            'r_screenshotWatermark = ri.Cvar_Get( "r_screenshotWatermark", ""',
            init,
        )
        self.assertIn("captureMode %s", init)
        self.assertIn("captureColorSpace %s", init)
        self.assertIn("return \"sdr-srgb\";", init)
        self.assertIn("R_WarnExplicitHdrScreenshotCapture();", command)
        self.assertIn("R_ExpandScreenshotPattern", init)
        self.assertIn("R_ValidScreenshotBaseName", command)
        self.assertIn("if ( ri.PublishGameScreenshot && checkname[0] )", command)
        self.assertIn(
            "ri.PublishGameScreenshot( checkname, checkname );", command
        )
        self.assertIn(
            "screenshot cubemap capture is not yet available with the ", command
        )
        self.assertIn("no files were written", command)

    def test_raster_fallback_keeps_enhanced_flare_wal_and_liquid_vbo_parity(
        self,
    ) -> None:
        flares = read("code/rendererrtx/vk_flares.c")
        image = read("code/rendererrtx/tr_image.c")
        vbo = read("code/rendererrtx/vk_vbo.c")

        flare_helper = source_section(
            flares,
            "static void RB_AddLensFlareSprites",
            "static void RB_RenderFlare",
        )
        flare_render = source_section(
            flares,
            "static void RB_RenderFlare",
            "void RB_RenderFlares",
        )
        self.assertIn('../renderercommon/tr_lens_flare.h"', flares)
        self.assertIn("r_flares->integer < 2", flare_helper)
        self.assertIn("tr.lensFlareShader", flare_helper)
        self.assertIn("R_LensFlareEdgeAttenuation", flare_helper)
        self.assertIn("R_LensFlareSpriteCount", flare_helper)
        self.assertIn("R_LensFlareSpritePosition", flare_helper)
        self.assertEqual(flare_helper.count("R_LensFlareSpriteColor"), 3)
        flare_call = "RB_AddLensFlareSprites( f, size, color, fogFactors );"
        self.assertIn(flare_call, flare_render)
        self.assertLess(
            flare_render.index("RB_EndSurface();"),
            flare_render.index(flare_call),
        )

        loaders = source_section(
            image,
            "static const imageExtToLoaderMap_t imageLoaders[]",
            "static const int numImageLoaders",
        )
        self.assertIn('{ "wal",  R_LoadWAL }', loaders)

        static_shader = source_section(
            vbo,
            "static qboolean isStaticShader",
            "static void VBO_AddGeometry",
        )
        liquid_rejection = static_shader.index("if ( r_fbo && r_fbo->integer")
        cached_acceptance = static_shader.index("if ( shader->isStaticShader )")
        self.assertLess(liquid_rejection, cached_acceptance)
        self.assertIn("r_liquid->integer > 0", static_shader)
        self.assertIn("shader->sort >= SS_FOG", static_shader)
        self.assertIn("R_LiquidShaderSupported( shader )", static_shader)
        self.assertIn(
            "R_LiquidContentsEnabled( shader->contentFlags, r_liquid->integer )",
            static_shader,
        )
        self.assertIn(
            "return qfalse;",
            static_shader[liquid_rejection:cached_acceptance],
        )

    def test_maintainer_docs_link_the_retail_rtx_smoke_gate(self) -> None:
        build = read("BUILD.md")
        readme = read("README.md")
        technical = read("docs/fnql/TECHNICAL.md")
        template = read("docs/templates/README.md.in")
        install_template = read("docs/templates/install-readme.html.in")
        contract = read("docs/fnql/RTX_RENDERER.md")
        release = read("scripts/release.py")

        self.assertIn("exactly three renderer modules", build)
        self.assertIn("-Drenderers=glx,vk,rtx", build)
        self.assertIn("docs/fnql/RTX_RENDERER.md", template)
        self.assertIn("docs/fnql/RTX_RENDERER.md", readme)
        self.assertIn("docs/fnql/RTX_RENDERER.md", install_template)
        self.assertIn("scripts/rtx_runtime_smoke.py", technical)
        self.assertIn('ROOT / "docs" / "fnql" / "RTX_RENDERER.md"', release)
        self.assertIn("supported three-renderer set", contract)
        self.assertIn("r_fullscreen 0", contract)
        self.assertIn("Steam app ID `282440`", contract)
        self.assertIn("A plan-only manifest", contract)
        self.assertIn(
            "rejects that subcommand without writing files", contract
        )


if __name__ == "__main__":
    unittest.main()
