import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RENDERERS = ("code/renderer", "code/renderervk", "code/rendererrtx")


def read_text(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


def section(source, start, end):
    start_index = source.index(start)
    end_index = source.index(end, start_index)
    return source[start_index:end_index]


def stable_projection_far(submitted_radius, applied_scale):
    projection_far = 64.0
    while projection_far < submitted_radius and projection_far < 131072.0:
        projection_far *= 2.0
    projection_far *= applied_scale
    return min(131072.0, max(64.0, projection_far))


class DlightShadowStabilityTests(unittest.TestCase):
    def test_retail_jitter_bands_have_stable_conservative_projection(self):
        for base_radius, unscaled_far in ((200, 256.0), (300, 512.0)):
            for scale in (0.1, 0.5, 1.0):
                with self.subTest(base_radius=base_radius, scale=scale):
                    projection_values = {
                        stable_projection_far(radius, scale)
                        for radius in range(base_radius, base_radius + 32)
                    }
                    self.assertEqual(
                        projection_values,
                        {max(64.0, unscaled_far * scale)},
                    )
                    projection_far = projection_values.pop()
                    for radius in range(base_radius, base_radius + 32):
                        self.assertGreaterEqual(
                            projection_far,
                            radius * scale,
                        )

    def test_shared_projection_helper_quantizes_before_dlight_scale(self):
        helper = read_text("code/renderercommon/tr_dlight_shadow.h")

        self.assertIn("rand() & 31", helper)
        self.assertIn(
            "R_DlightShadowExactProjectionFarForRadius",
            helper,
        )
        self.assertIn("projectionFar = 64.0f;", helper)
        self.assertIn(
            "while ( projectionFar < submittedRadius && "
            "projectionFar < (float)WORLD_SIZE )",
            helper,
        )
        self.assertIn(
            "appliedScale >= (float)WORLD_SIZE / projectionFar",
            helper,
        )
        self.assertIn("projectionFar *= appliedScale;", helper)
        self.assertIn("projectionFar = (float)WORLD_SIZE;", helper)

        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                header = read_text(f"{renderer}/tr_local.h")
                scene = read_text(f"{renderer}/tr_scene.c")
                call = scene.index(
                    "R_DlightShadowProjectionFarForRadius( "
                    "intensity, r_dlightScale->value )"
                )
                scale = scene.index(
                    "intensity *= r_dlightScale->value;",
                    call,
                )

                self.assertIn(
                    '#include "../renderercommon/tr_dlight_shadow.h"',
                    header,
                )
                self.assertLess(call, scale)
                self.assertIn("dl->radius = intensity;", scene[scale:])
                self.assertIn(
                    "dl->shadowProjectionFar = shadowProjectionFar;",
                    scene[scale:],
                )
                self.assertEqual(
                    scene.count(
                        "R_DlightShadowExactProjectionFarForRadius( "
                        "dl->radius )"
                    ),
                    2,
                )

    def test_planner_uses_sphere_validity_and_stable_range(self):
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                main = read_text(f"{renderer}/tr_main.c")
                receiver_count = section(
                    main,
                    "static int R_CountDlightShadowReceivers(",
                    "static qboolean R_DlightShadowInputValid(",
                )
                validity = section(
                    main,
                    "static qboolean R_DlightShadowInputValid(",
                    "static float R_DlightShadowPriority(",
                )
                priority = section(
                    main,
                    "static float R_DlightShadowPriority(",
                    "static void R_ShadowManagerStorePointLightRecord(",
                )
                record = section(
                    main,
                    "static void R_ShadowManagerStorePointLightRecord(",
                    "static void R_ShadowManagerAddPointCandidate(",
                )
                planner = section(
                    main,
                    "static void R_PlanDlightShadows(",
                    "#endif // USE_PMLIGHT",
                )

                self.assertIn(
                    "surf->flags & LSF_SHADOW_CASTER_ONLY",
                    receiver_count,
                )
                self.assertIn(
                    "R_DlightShadowFloatIsFinite( dl->radius )",
                    validity,
                )
                self.assertIn(
                    "R_DlightShadowFloatIsFinite( dl->shadowProjectionFar )",
                    validity,
                )
                self.assertIn(
                    "dl->shadowProjectionFar < dl->radius",
                    validity,
                )
                self.assertNotIn("R_TransformModelToClip", validity)
                self.assertNotIn("clip[3]", validity)
                self.assertIn(
                    "radius2 = Square( dl->shadowProjectionFar );",
                    priority,
                )
                self.assertNotIn("shadowReceiverCount", priority)
                self.assertIn(
                    "record->projectionFar = dl->shadowProjectionFar;",
                    record,
                )
                self.assertIn(
                    "if ( !R_DlightShadowInputValid( dl ) )",
                    planner,
                )
                self.assertIn(
                    "receivers = R_CountDlightShadowReceivers( dl );",
                    planner,
                )

    def test_producer_and_receivers_share_plan_projection_far(self):
        receiver_paths = (
            "code/renderer/tr_arb.c",
            "code/renderervk/tr_shade.c",
            "code/rendererrtx/tr_shade.c",
        )

        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                backend = read_text(f"{renderer}/tr_backend.c")
                self.assertIn(
                    "shadowParms->zFar = plan ? plan->projectionFar : "
                    "dl->shadowProjectionFar;",
                    backend,
                )

        for path in receiver_paths:
            with self.subTest(path=path):
                receiver = read_text(path)
                self.assertIn(
                    "zFar = plan ? plan->projectionFar : "
                    "dl->shadowProjectionFar;",
                    receiver,
                )
                self.assertIn("if ( zFar <= zNear )", receiver)

    def test_world_casters_are_collected_independently_of_camera_visibility(self):
        caster_source = read_text(
            "code/renderercommon/tr_dlight_shadow_casters.h"
        )

        self.assertIn(
            "R_CollectDlightShadowWorldCasters_r",
            caster_source,
        )
        self.assertIn(
            "R_CollectDlightShadowWorldCasters_r( tr.world->nodes",
            caster_source,
        )
        self.assertIn(
            "shadowLight.radius = plan->projectionFar;",
            caster_source,
        )
        self.assertIn("LSF_SHADOW_CASTER_ONLY", caster_source)
        self.assertIn("pmShadowMask", caster_source)
        self.assertIn("shader->numDeforms > 0", caster_source)
        self.assertIn("stage->stateBits & GLS_ATEST_BITS", caster_source)
        self.assertIn("r_nocurves->integer", caster_source)
        self.assertNotIn("visframe", caster_source)
        self.assertNotIn("vcVisible", caster_source)
        self.assertNotIn("viewParms.frustum", caster_source)
        self.assertIn(
            "tr.refdef.numLitSurfs = planStartLitSurfCount;",
            caster_source,
        )
        self.assertIn("planStartTail->next = NULL;", caster_source)
        self.assertIn(
            "tr.pc.c_lit_surfs - addedCount",
            caster_source,
        )
        self.assertIn(
            "R_DiscardDlightShadowPlansFrom( i );",
            caster_source,
        )
        self.assertIn(
            "c_dlightShadowSkippedCasterOverflow",
            caster_source,
        )

        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                world = read_text(f"{renderer}/tr_world.c")
                backend = read_text(f"{renderer}/tr_backend.c")
                lighting = section(
                    backend,
                    "static void RB_RenderLitSurfList(",
                    "static void RB_LightingPass(",
                )

                self.assertIn(
                    '#include "../renderercommon/'
                    'tr_dlight_shadow_casters.h"',
                    world,
                )
                self.assertIn(
                    "litSurf->flags & LSF_SHADOW_CASTER_ONLY",
                    lighting,
                )
                self.assertIn(
                    "!( litSurf->flags & LSF_SHADOW_CASTER_ONLY )",
                    backend,
                )

    def test_entity_casters_use_a_canonical_camera_independent_pass(self):
        caster_source = read_text(
            "code/renderercommon/tr_dlight_shadow_casters.h"
        )

        self.assertIn(
            "R_CollectDlightShadowEntityCasters",
            caster_source,
        )
        self.assertIn("ent->e.reType != RT_MODEL", caster_source)
        self.assertIn(
            "RF_NOSHADOW | RF_FIRST_PERSON | RF_DEPTHHACK",
            caster_source,
        )
        self.assertIn(
            "context.cullLight = shadowLight;",
            caster_source,
        )
        self.assertIn(
            "R_CollectDlightShadowEntityCasters( &context )",
            caster_source,
        )
        self.assertIn(
            "R_AddDlightShadowEntityCasterSurface",
            caster_source,
        )
        self.assertIn(
            "R_DlightShadowEntityBoundsCulled",
            caster_source,
        )
        self.assertIn("for ( corner = 0; corner < 8; corner++ )", caster_source)
        self.assertIn("ent->e.axis[0]", caster_source)
        self.assertIn("ent->e.axis[1]", caster_source)
        self.assertIn("ent->e.axis[2]", caster_source)
        self.assertIn(
            "R_DlightShadowEntityTransformValid",
            caster_source,
        )
        self.assertIn(
            "!R_DlightShadowFloatIsFinite( modelRadiusSquared )",
            caster_source,
        )
        self.assertIn(
            "!R_DlightShadowFloatIsFinite( distanceSquared )",
            caster_source,
        )
        self.assertNotIn("R_CullLocalBox", caster_source)
        self.assertLess(
            caster_source.index(
                "R_CollectDlightShadowWorldCasters_r( tr.world->nodes"
            ),
            caster_source.index(
                "R_CollectDlightShadowEntityCasters( &context )"
            ),
        )

        model_files = (
            "tr_mesh.c",
            "tr_animation.c",
            "tr_model_iqm.c",
        )
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                header = read_text(f"{renderer}/tr_local.h")
                main = read_text(f"{renderer}/tr_main.c")
                world = read_text(f"{renderer}/tr_world.c")
                backend = read_text(f"{renderer}/tr_backend.c")

                self.assertIn(
                    "dlight_t\t*targetLight;" if renderer != "code/renderervk"
                    else "dlight_t *targetLight;",
                    header,
                )
                self.assertIn("dlight_t", header)
                self.assertIn("cullLight;", header)
                self.assertIn("qboolean", header)
                self.assertIn("overflow;", header)
                self.assertIn(
                    "R_DlightShadowFrontendCasterAllowed",
                    header,
                )
                self.assertIn(
                    "R_AddDlightShadowEntityCasterSurface",
                    header,
                )
                self.assertIn(
                    "R_DlightShadowEntityBoundsCulled",
                    header,
                )

                for call in (
                    "R_AddMD3Surfaces( ent, NULL );",
                    "R_MDRAddAnimSurfaces( ent, NULL );",
                    "R_AddIQMSurfaces( ent, NULL );",
                    "R_AddBrushModelSurfaces( ent, NULL );",
                ):
                    self.assertIn(call, main)

                self.assertIn(
                    "R_DlightShadowEntityBoundsCulled",
                    world,
                )
                self.assertIn(
                    "R_AddDlightShadowEntityCasterSurface",
                    world,
                )
                self.assertIn(
                    "for ( i = 0; i < bmodel->numSurfaces; i++ )",
                    world,
                )

                signature = section(
                    backend,
                    "static qboolean RB_DlightShadowCacheSignature(",
                    "static int RB_DlightShadowCacheSlot(",
                )
                caster_render = section(
                    backend,
                    "static int RB_RenderDlightShadowCasters(",
                    "static qboolean RB_BuildSpotShadowView(",
                )
                for producer in (signature, caster_render):
                    self.assertIn(
                        "!( litSurf->flags & "
                        "LSF_SHADOW_CASTER_ONLY )",
                        producer,
                    )

                for filename in model_files:
                    model = read_text(f"{renderer}/{filename}")
                    self.assertIn(
                        "R_DlightShadowEntityBoundsCulled",
                        model,
                    )
                    self.assertIn(
                        "R_AddDlightShadowEntityCasterSurface",
                        model,
                    )
                    self.assertNotIn(
                        "LSF_SHADOW_CASTER_ONLY",
                        model,
                    )

                mesh = read_text(f"{renderer}/tr_mesh.c")
                animation = read_text(f"{renderer}/tr_animation.c")
                self.assertRegex(mesh, r"lod = \w+ \? 0 : R_ComputeLOD")
                self.assertIn("lodnum = 0", animation)

    def test_portal_views_own_dlights_and_restore_parent_shadow_plans(self):
        shared = read_text("code/renderercommon/tr_portal_plan.h")

        self.assertIn("plan->viewParms.dlights = NULL;", shared)
        self.assertIn("plan->viewParms.num_dlights = 0;", shared)
        self.assertIn(
            "available = (int)ARRAY_LEN( backEndData->dlights ) - "
            "r_numdlights;",
            shared,
        )
        self.assertIn(
            "plan->viewParms.dlights = "
            "&backEndData->dlights[r_numdlights];",
            shared,
        )
        self.assertIn(
            "shadowManager_t parentShadowManager = tr.shadowManager;",
            shared,
        )
        self.assertIn("tr.shadowManager = parentShadowManager;", shared)
        self.assertIn("tr.csm = parentCsm;", shared)
        self.assertIn(
            "tr.shadowCorrectnessDebug = parentCorrectnessDebug;",
            shared,
        )

        rtx = read_text("code/rendererrtx/tr_main.c")
        self.assertIn("newParms.dlights = NULL;", rtx)
        self.assertIn("newParms.num_dlights = 0;", rtx)
        self.assertIn("oldShadowManager = tr.shadowManager;", rtx)
        self.assertIn("tr.shadowManager = oldShadowManager;", rtx)
        self.assertIn("tr.csm = oldCsm;", rtx)

        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                main = read_text(f"{renderer}/tr_main.c")
                generate = section(
                    main,
                    "static void R_GenerateDrawSurfs( void )",
                    "void R_RenderView(",
                )

                self.assertIn(
                    "for ( i = 0; i < tr.viewParms.num_dlights; i++ )",
                    generate,
                )
                self.assertIn(
                    "tr.viewParms.dlights[i].head = "
                    "tr.viewParms.dlights[i].tail = NULL;",
                    generate,
                )
                self.assertIn(
                    "for ( i = 0; i < tr.viewParms.num_dlights; ++i )",
                    main,
                )

    def test_static_shadow_cache_hashes_only_producer_relevant_state(self):
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                backend = read_text(f"{renderer}/tr_backend.c")
                signature = section(
                    backend,
                    "static qboolean RB_DlightShadowCacheSignature(",
                    "static int RB_DlightShadowCacheSlot(",
                )

                self.assertIn("uint64_t hash;", signature)
                self.assertIn("dl->origin[i]", signature)
                self.assertIn("projectionFar", signature)
                self.assertIn("atlasFaceSize", signature)
                self.assertIn(
                    "r_dlightShadowCasterDepthBias",
                    signature,
                )
                self.assertIn(
                    "r_dlightShadowCasterSlopeBias",
                    signature,
                )
                self.assertIn(
                    "r_dlightShadowCasterNormalBias",
                    signature,
                )
                self.assertIn("r_nocull", signature)
                self.assertIn("r_lodCurveError", signature)
                self.assertIn("r_shadowCorrectness", signature)
                self.assertIn("tr.world ? tr.world->baseName", signature)
                self.assertIn(
                    "RB_DlightShadowEffectiveShader( shader )",
                    signature,
                )
                self.assertIn(
                    "(uint32_t)casterShader->cullType",
                    signature,
                )
                self.assertIn("litSurf->surface", signature)
                self.assertNotIn("dl->radius", signature)
                self.assertNotIn("dl->color", signature)
                self.assertNotIn("dl->additive", signature)
                self.assertNotIn("shadowReceiverCount", signature)
                self.assertLess(
                    signature.index(
                        "!RB_DlightShadowEntityCasterAllowed"
                    ),
                    signature.index(
                        "if ( entityNum != REFENTITYNUM_WORLD )"
                    ),
                )
                self.assertLess(
                    signature.index(
                        "!RB_DlightShadowCasterAllowed"
                    ),
                    signature.index(
                        "if ( entityNum != REFENTITYNUM_WORLD )"
                    ),
                )

    def test_caster_material_culling_and_cube_seams_are_consistent(self):
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                header = read_text(f"{renderer}/tr_local.h")
                backend = read_text(f"{renderer}/tr_backend.c")
                shade = read_text(f"{renderer}/tr_shade.c")
                surface = read_text(f"{renderer}/tr_surface.c")
                allowed = section(
                    backend,
                    "static const shader_t *RB_DlightShadowEffectiveShader(",
                    "static qboolean RB_DlightShadowMD3Bounds(",
                )
                caster_render = section(
                    backend,
                    "static int RB_RenderDlightShadowCasters(",
                    "static qboolean RB_BuildSpotShadowView(",
                )

                self.assertIn(
                    "shader->remappedShader ? "
                    "shader->remappedShader : shader",
                    allowed,
                )
                self.assertIn(
                    "shader = RB_DlightShadowEffectiveShader( shader );",
                    allowed,
                )
                self.assertIn(
                    "casterShader = "
                    "RB_DlightShadowEffectiveShader( shader );",
                    caster_render,
                )
                self.assertIn(
                    "tess.shadowCasterCullType = "
                    "casterShader->cullType;",
                    caster_render,
                )
                self.assertNotIn(
                    "tess.shadowCasterCullType = shader->cullType;",
                    caster_render,
                )

                backend_allowed = section(
                    backend,
                    "static qboolean RB_DlightShadowCasterAllowed(",
                    "static qboolean RB_DlightShadowMD3Bounds(",
                )

                self.assertIn(
                    "shader->numDeforms > 0 || "
                    "RB_ShadowShaderHasAlphaTest( shader )",
                    backend_allowed,
                )
                self.assertNotIn("shader->lightingStage", backend_allowed)
                self.assertIn(
                    "#define RB_DLIGHT_SHADOW_FACE_CULL_BASE_EPSILON "
                    "0.125f",
                    backend,
                )
                self.assertIn(
                    "0.75f * R_ShadowClampCasterNormalBias(",
                    backend,
                )
                self.assertIn(
                    "plane->dist - cullEpsilon",
                    backend,
                )
                self.assertIn(
                    "backEnd.viewParms.passFlags & VPF_DLIGHT_SHADOW",
                    surface,
                )
                self.assertIn(
                    "lodError = r_lodCurveError->value;",
                    surface,
                )
                self.assertIn(
                    "cullType_t\tshadowCasterCullType;",
                    header,
                )
                self.assertIn(
                    "state->shadowCasterCullType = "
                    "tess.shadowCasterCullType;",
                    surface,
                )
                self.assertIn(
                    "tess.shadowCasterCullType = "
                    "state->shadowCasterCullType;",
                    surface,
                )

                if renderer == "code/renderer":
                    self.assertIn(
                        "GL_Cull( tess.shadowCasterCullType );",
                        shade,
                    )
                else:
                    self.assertIn(
                        "def.face_culling = tess.csmCasterPass ?",
                        shade,
                    )
                    self.assertIn(
                        "CT_TWO_SIDED : tess.shadowCasterCullType;",
                        shade,
                    )

    def test_spot_shadow_batches_own_effective_cull_and_cache_state(self):
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                backend = read_text(f"{renderer}/tr_backend.c")
                spot_render = section(
                    backend,
                    "static int RB_RenderSpotShadowCasters(",
                    "typedef struct {\n\tqboolean valid;",
                )
                spot_cache = section(
                    backend,
                    "static qboolean RB_SpotShadowCacheSignature(",
                    "static void RB_RenderSpotShadowAtlas(",
                )

                self.assertIn(
                    "casterShader = "
                    "RB_DlightShadowEffectiveShader( shader );",
                    spot_render,
                )
                self.assertIn(
                    "currentCullType != casterShader->cullType",
                    spot_render,
                )
                self.assertIn(
                    "tess.shadowCasterCullType = "
                    "casterShader->cullType;",
                    spot_render,
                )
                self.assertIn(
                    "casterShader = "
                    "RB_DlightShadowEffectiveShader( shader );",
                    spot_cache,
                )
                self.assertIn(
                    "(unsigned int)casterShader->cullType",
                    spot_cache,
                )
                for cvar in (
                    "r_dlightShadowCasterDepthBias",
                    "r_dlightShadowCasterSlopeBias",
                    "r_dlightShadowCasterNormalBias",
                    "r_nocull",
                    "r_lodCurveError",
                ):
                    self.assertIn(cvar, spot_cache)

                if renderer == "code/renderer":
                    self.assertIn(
                        "GL_Cull( casterShader->cullType );",
                        spot_render,
                    )
                    self.assertIn(
                        "VBO_DrawDepthCasterItem( shader, itemIndex )",
                        spot_render,
                    )

    def test_glx_point_receiver_bias_remains_in_world_units(self):
        glx = read_text("code/rendererglx/glx_module.cpp")
        point_shadow = section(
            glx,
            '"    vec3 shadowVec = -dnLV;\\n"',
            '"    float face;\\n"',
        )

        self.assertIn(
            '"    float receiverNDotL = '
            'clamp(abs(dot(nn, lv)), 0.0, 1.0);\\n"',
            point_shadow,
        )
        self.assertIn(
            '"    float bias = u_DlightShadow.w * '
            '(0.125 + 0.375 * receiverSlope);\\n"',
            point_shadow,
        )
        self.assertIn(
            '"    float texelBias = max(2.0 * major / '
            'u_ShadowAtlas.x, 0.125);\\n"',
            point_shadow,
        )
        self.assertIn(
            '"    float receiver = max(major - min(bias, texelBias), '
            'u_ShadowDepth.z);\\n"',
            point_shadow,
        )
        self.assertNotIn("u_DlightShadow.w * major", point_shadow)

    def test_muzzle_flash_light_adjustment_is_narrow_and_renderer_consistent(self):
        helper = read_text(
            "code/renderercommon/tr_muzzle_flash_light.h"
        )

        self.assertIn('static const char suffix[] = "_flash.md3";', helper)
        self.assertIn(
            "ent->reType != RT_MODEL || ent->hModel <= 0",
            helper,
        )
        self.assertIn(
            "VectorNormalize2( ent->axis[0], forward ) <= 0.0001f",
            helper,
        )
        self.assertIn(
            "Square( R_MUZZLE_FLASH_LIGHT_MATCH_RADIUS )",
            helper,
        )
        self.assertLess(
            helper.index("candidate = r_muzzleFlashLightCandidate;"),
            helper.index(
                "VectorLengthSquared( delta ) >",
            ),
        )
        self.assertIn(
            "R_ClearMuzzleFlashLightCandidate();",
            helper[
                helper.index(
                    "candidate = r_muzzleFlashLightCandidate;"
                ):
            ],
        )
        self.assertIn(
            "VectorMA( adjustedOrigin, offset, candidate.forward, "
            "adjustedOrigin );",
            helper,
        )

        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                scene = read_text(f"{renderer}/tr_scene.c")
                init = read_text(f"{renderer}/tr_init.c")
                header = read_text(f"{renderer}/tr_local.h")

                self.assertIn(
                    '#include "../renderercommon/'
                    'tr_muzzle_flash_light.h"',
                    scene,
                )
                self.assertIn(
                    "R_TrackMuzzleFlashLightEntity( ent );",
                    scene,
                )
                self.assertIn(
                    "R_ClearMuzzleFlashLightCandidate();",
                    section(
                        scene,
                        "void RE_ClearScene( void )",
                        "RE_AddPolyToScene",
                    ),
                )
                self.assertEqual(
                    scene.count(
                        "R_PrepareMuzzleFlashLight( org, adjustedOrigin )"
                    ),
                    2,
                )
                self.assertIn(
                    "dl->shadowEligible = shadowEligible;",
                    scene,
                )
                self.assertIn(
                    'ri.Cvar_Get( "r_muzzleFlashDlightOffset", "8", '
                    "CVAR_ARCHIVE_ND )",
                    init,
                )
                self.assertIn(
                    'ri.Cvar_Get( "r_muzzleFlashDlightShadows", "1", '
                    "CVAR_ARCHIVE_ND )",
                    init,
                )
                self.assertIn(
                    "extern cvar_t\t*r_muzzleFlashDlightOffset;",
                    header,
                )
                self.assertIn(
                    "extern cvar_t\t*r_muzzleFlashDlightShadows;",
                    header,
                )

                if renderer == "code/rendererrtx":
                    self.assertIn(
                        "dl->castsRtShadows = shadowEligible;",
                        scene,
                    )

    def test_debug_snapshot_keeps_primary_view_telemetry(self):
        for renderer in RENDERERS:
            with self.subTest(renderer=renderer):
                header = read_text(f"{renderer}/tr_local.h")
                backend = read_text(f"{renderer}/tr_backend.c")
                commands = read_text(f"{renderer}/tr_cmds.c")

                self.assertIn(
                    "shadowManager_t\tshadowManagerDebug;",
                    header,
                )
                self.assertIn(
                    "backEnd.shadowManagerDebug = tr.shadowManager;",
                    backend,
                )
                self.assertIn(
                    "if ( backEnd.shadowManagerDebug.planned )",
                    commands,
                )
                self.assertIn(
                    "tr.shadowManager = backEnd.shadowManagerDebug;",
                    commands,
                )


if __name__ == "__main__":
    unittest.main()
