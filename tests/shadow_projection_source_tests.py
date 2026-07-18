from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_text(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(text: str, needle: str, label: str, failures: list[str]) -> None:
    if needle not in text:
        failures.append(f"missing {label}: {needle}")


def require_order(text: str, needles: list[str], label: str, failures: list[str]) -> None:
    position = 0
    for needle in needles:
        found = text.find(needle, position)
        if found < 0:
            failures.append(f"missing ordered {label}: {needle}")
            return
        position = found + len(needle)


def section(text: str, start: str, end: str, label: str, failures: list[str]) -> str:
    start_index = text.find(start)
    if start_index < 0:
        failures.append(f"missing section start {label}: {start}")
        return ""
    end_index = text.find(end, start_index)
    if end_index < 0:
        failures.append(f"missing section end {label}: {end}")
        return text[start_index:]
    return text[start_index:end_index]


def check_vulkan_api_shadow_contracts(
    label: str, renderer_root: str, failures: list[str]
) -> None:
    required_paths = (
        f"{renderer_root}/vk.c",
        f"{renderer_root}/tr_backend.c",
        f"{renderer_root}/shaders/csm_shadow.frag",
        f"{renderer_root}/shaders/light_frag.tmpl",
        f"{renderer_root}/shaders/spirv/shader_data.c",
    )
    missing_paths = [path for path in required_paths if not (ROOT / path).is_file()]
    if missing_paths:
        for path in missing_paths:
            failures.append(f"missing {label} shadow source: {path}")
        return

    vk = read_text(f"{renderer_root}/vk.c")
    backend = read_text(f"{renderer_root}/tr_backend.c")
    csm_frag = read_text(f"{renderer_root}/shaders/csm_shadow.frag")
    light_frag = read_text(f"{renderer_root}/shaders/light_frag.tmpl")

    dlight_projection = section(
        backend,
        "static void RB_SetDlightShadowProjectionZ",
        "static void RB_BuildDlightShadowView",
        f"{label} dynamic-light shadow projection",
        failures,
    )
    require_order(
        dlight_projection,
        [
            "#ifdef USE_REVERSED_DEPTH",
            "viewParms->projectionMatrix[10] = zNear / depth;",
            "viewParms->projectionMatrix[14] = zFar * zNear / depth;",
            "#else",
            "viewParms->projectionMatrix[10] = -zFar / depth;",
            "viewParms->projectionMatrix[14] = -zFar * zNear / depth;",
        ],
        f"{label} zero-to-one dynamic shadow depth projection",
        failures,
    )
    require(
        backend,
        "shadowParms->viewportY = atlasHeight - atlasY - atlasFaceSize;",
        f"{label} shadow atlas plan-to-viewParms Y conversion",
        failures,
    )

    viewport = section(
        vk,
        "static void get_viewport_rect",
        "static void get_viewport(",
        f"{label} API viewport conversion",
        failures,
    )
    require(
        viewport,
        "r->offset.y = vk.renderHeight - (backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight) * vk.renderScaleY;",
        f"{label} lower-left viewParms to top-left VkViewport conversion",
        failures,
    )

    mvp = section(
        vk,
        "static void get_mvp_transform",
        "void vk_clear_color",
        f"{label} MVP conversion",
        failures,
    )
    require_order(
        mvp,
        [
            "Com_Memcpy( proj, p, 64 );",
            "proj[1] = -p[1];",
            "proj[5] = -p[5];",
            "proj[9] = -p[9];",
            "proj[13] = -p[13];",
            "myGlMultMatrix( vk_world.modelview_transform, proj, mvp );",
        ],
        f"{label} full clip-Y projection row inversion",
        failures,
    )

    depth_clear = section(
        vk,
        "static void vk_clear_depth_internal",
        "void vk_clear_depth( qboolean clear_stencil )",
        f"{label} depth clear convention",
        failures,
    )
    require_order(
        depth_clear,
        [
            "if ( vk.renderPassIndex == RENDER_PASS_CSM_SHADOW ) {",
            "attachment.clearValue.depthStencil.depth = 1.0f;",
            "#ifdef USE_REVERSED_DEPTH",
            "attachment.clearValue.depthStencil.depth = 0.0f;",
            "#else",
            "attachment.clearValue.depthStencil.depth = 1.0f;",
        ],
        f"{label} CSM forward clear and dynamic reversed clear",
        failures,
    )

    depth_compare = section(
        vk,
        "depth_stencil_state.depthWriteEnable = (state_bits & GLS_DEPTHMASK_TRUE) ? VK_TRUE : VK_FALSE;",
        "depth_stencil_state.depthBoundsTestEnable",
        f"{label} depth compare convention",
        failures,
    )
    require_order(
        depth_compare,
        [
            "if ( renderPassIndex == RENDER_PASS_CSM_SHADOW ) {",
            "VK_COMPARE_OP_LESS_OR_EQUAL",
            "#ifdef USE_REVERSED_DEPTH",
            "VK_COMPARE_OP_GREATER_OR_EQUAL",
        ],
        f"{label} CSM forward compare and dynamic reversed compare",
        failures,
    )

    csm_projection = section(
        backend,
        "static void RB_SetCSMShadowProjection",
        "static void RB_BuildCSMShadowView",
        f"{label} CSM producer projection",
        failures,
    )
    require(
        csm_projection,
        "shadowParms->projectionMatrix[13] = -( cascade->bounds[1][2] + cascade->bounds[0][2] ) / extentZ;",
        f"{label} CSM orthographic vertical offset",
        failures,
    )

    require_order(
        csm_frag,
        [
            "vec2 texel = 1.0 / vec2(textureSize(csm_shadow_texture, 0));",
            "float atlas_x = csmSplitAtlas.z + light_coord.y * csmSplitAtlas.w;",
            "float atlas_x_min = csmSplitAtlas.z + texel.x;",
            "float atlas_x_max = csmSplitAtlas.z + csmSplitAtlas.w - texel.x;",
            "clamp(atlas_x, atlas_x_min, atlas_x_max),",
            "clamp(1.0 - light_coord.z, texel.y, 1.0 - texel.y));",
        ],
        f"{label} CSM receiver atlas mapping",
        failures,
    )
    require(
        light_frag,
        "vec2 atlasUV = (vec2(column, row) * faceSize + localUV * faceSize) / atlasSize;",
        f"{label} dynamic-light top-left atlas row sampling",
        failures,
    )
    require_order(
        light_frag,
        [
            "float texelInset = 1.0 / tileSize;",
            "localUV = clamp(localUV, vec2(texelInset), vec2(1.0 - texelInset));",
            "vec2 atlasUV = (depthFadeScale.xy + localUV * tileSize) / atlasSize;",
        ],
        f"{label} spot atlas interior clamp",
        failures,
    )
    require(
        light_frag,
        "float receiverDepth = mix(forwardDepth, reversedDepth, depthFadeBias.x);",
        f"{label} dynamic-light depth convention mode",
        failures,
    )
    require(
        read_text(f"{renderer_root}/shaders/spirv/shader_data.c"),
        "const unsigned char csm_shadow_frag_spv",
        f"embedded {label} CSM fragment SPIR-V",
        failures,
    )


def main() -> int:
    failures: list[str] = []
    check_vulkan_api_shadow_contracts("RTX", "code/rendererrtx", failures)
    vk = read_text("code/renderervk/vk.c")
    vk_backend = read_text("code/renderervk/tr_backend.c")
    vk_csm_frag = read_text("code/renderervk/shaders/csm_shadow.frag")
    vk_light_frag = read_text("code/renderervk/shaders/light_frag.tmpl")
    glx_backend = read_text("code/renderer/tr_backend.c")
    glx_init = read_text("code/renderer/tr_init.c")
    glx_arb = read_text("code/renderer/tr_arb.c")

    glx_dlight_projection = section(
        glx_backend,
        "static void RB_SetDlightShadowProjectionZ",
        "static void RB_BuildDlightShadowView",
        "GLx dynamic-light shadow projection",
        failures,
    )
    require_order(
        glx_dlight_projection,
        [
            "viewParms->projectionMatrix[10] = -( zFar + zNear ) / depth;",
            "viewParms->projectionMatrix[14] = -2.0f * zFar * zNear / depth;",
        ],
        "GLx forward OpenGL shadow depth projection",
        failures,
    )
    require(glx_init, "qglClearDepth( 1.0f );", "GLx default depth clear", failures)
    require(glx_init, "qglDepthFunc( GL_LEQUAL );", "GLx default depth compare", failures)
    require(
        glx_backend,
        "shadowParms->viewportY = atlasHeight - atlasY - atlasFaceSize;",
        "GLx shadow atlas plan-to-viewport Y conversion",
        failures,
    )

    vk_dlight_projection = section(
        vk_backend,
        "static void RB_SetDlightShadowProjectionZ",
        "static void RB_BuildDlightShadowView",
        "Vulkan dynamic-light shadow projection",
        failures,
    )
    require_order(
        vk_dlight_projection,
        [
            "#ifdef USE_REVERSED_DEPTH",
            "viewParms->projectionMatrix[10] = zNear / depth;",
            "viewParms->projectionMatrix[14] = zFar * zNear / depth;",
            "#else",
            "viewParms->projectionMatrix[10] = -zFar / depth;",
            "viewParms->projectionMatrix[14] = -zFar * zNear / depth;",
        ],
        "Vulkan zero-to-one dynamic shadow depth projection",
        failures,
    )
    require(
        vk_backend,
        "shadowParms->viewportY = atlasHeight - atlasY - atlasFaceSize;",
        "Vulkan shadow atlas plan-to-viewParms Y conversion",
        failures,
    )

    viewport = section(
        vk,
        "static void get_viewport_rect",
        "static void get_viewport(",
        "Vulkan API viewport conversion",
        failures,
    )
    require(
        viewport,
        "r->offset.y = vk.renderHeight - (backEnd.viewParms.viewportY + backEnd.viewParms.viewportHeight) * vk.renderScaleY;",
        "Vulkan lower-left viewParms to top-left VkViewport conversion",
        failures,
    )

    mvp = section(
        vk,
        "static void get_mvp_transform",
        "void vk_clear_color",
        "Vulkan MVP conversion",
        failures,
    )
    require_order(
        mvp,
        [
            "Com_Memcpy( proj, p, 64 );",
            "proj[1] = -p[1];",
            "proj[5] = -p[5];",
            "proj[9] = -p[9];",
            "proj[13] = -p[13];",
            "myGlMultMatrix( vk_world.modelview_transform, proj, mvp );",
        ],
        "full clip-Y projection row inversion before Vulkan MVP upload",
        failures,
    )

    vk_depth_clear = section(
        vk,
        "static void vk_clear_depth_internal",
        "void vk_clear_depth( qboolean clear_stencil )",
        "Vulkan depth clear convention",
        failures,
    )
    require_order(
        vk_depth_clear,
        [
            "if ( vk.renderPassIndex == RENDER_PASS_CSM_SHADOW ) {",
            "attachment.clearValue.depthStencil.depth = 1.0f;",
            "#ifdef USE_REVERSED_DEPTH",
            "attachment.clearValue.depthStencil.depth = 0.0f;",
            "#else",
            "attachment.clearValue.depthStencil.depth = 1.0f;",
        ],
        "Vulkan CSM forward clear and dynamic reversed clear",
        failures,
    )

    vk_depth_compare = section(
        vk,
        "depth_stencil_state.depthWriteEnable = (state_bits & GLS_DEPTHMASK_TRUE) ? VK_TRUE : VK_FALSE;",
        "depth_stencil_state.depthBoundsTestEnable",
        "Vulkan depth compare convention",
        failures,
    )
    require_order(
        vk_depth_compare,
        [
            "if ( renderPassIndex == RENDER_PASS_CSM_SHADOW ) {",
            "VK_COMPARE_OP_LESS_OR_EQUAL",
            "#ifdef USE_REVERSED_DEPTH",
            "VK_COMPARE_OP_GREATER_OR_EQUAL",
        ],
        "Vulkan CSM forward compare and dynamic reversed compare",
        failures,
    )

    csm_projection = section(
        vk_backend,
        "static void RB_SetCSMShadowProjection",
        "static void RB_BuildCSMShadowView",
        "Vulkan CSM producer projection",
        failures,
    )
    require(
        csm_projection,
        "shadowParms->projectionMatrix[13] = -( cascade->bounds[1][2] + cascade->bounds[0][2] ) / extentZ;",
        "CSM orthographic vertical offset",
        failures,
    )

    require_order(
        vk_csm_frag,
        [
            "vec2 texel = 1.0 / vec2(textureSize(csm_shadow_texture, 0));",
            "float atlas_x = csmSplitAtlas.z + light_coord.y * csmSplitAtlas.w;",
            "float atlas_x_min = csmSplitAtlas.z + texel.x;",
            "float atlas_x_max = csmSplitAtlas.z + csmSplitAtlas.w - texel.x;",
            "clamp(atlas_x, atlas_x_min, atlas_x_max),",
            "clamp(1.0 - light_coord.z, texel.y, 1.0 - texel.y));",
        ],
        "Vulkan CSM receiver atlas mapping",
        failures,
    )
    require(
        glx_arb,
        '"MOV uv.y, lightCoord.z; \\n"',
        "GLx CSM receiver atlas mapping",
        failures,
    )
    require(
        glx_arb,
        '"SUB local.y, shadowAtlas.w, local.y; \\n"',
        "GLx dynamic-light lower-left atlas row sampling",
        failures,
    )
    require(
        glx_arb,
        '"SLT occ.z, depthTap.x, occ.y; \\n"',
        "GLx forward dynamic-light depth compare",
        failures,
    )
    require(
        vk_light_frag,
        "vec2 atlasUV = (vec2(column, row) * faceSize + localUV * faceSize) / atlasSize;",
        "Vulkan dynamic-light top-left atlas row sampling",
        failures,
    )
    require_order(
        vk_light_frag,
        [
            "float texelInset = 1.0 / tileSize;",
            "localUV = clamp(localUV, vec2(texelInset), vec2(1.0 - texelInset));",
            "vec2 atlasUV = (depthFadeScale.xy + localUV * tileSize) / atlasSize;",
        ],
        "Vulkan spot atlas interior clamp",
        failures,
    )
    require_order(
        vk_light_frag,
        [
            "float texelInset = 1.0 / faceSize;",
            "localUV = clamp(localUV, vec2(texelInset), vec2(1.0 - texelInset));",
            "vec2 atlasUV = (vec2(column, row) * faceSize + localUV * faceSize) / atlasSize;",
        ],
        "Vulkan point atlas interior clamp",
        failures,
    )
    require(
        vk_light_frag,
        "float receiverDepth = mix(forwardDepth, reversedDepth, depthFadeBias.x);",
        "Vulkan dynamic-light depth convention mode",
        failures,
    )
    require(
        read_text("code/renderervk/shaders/spirv/shader_data.c"),
        "const unsigned char csm_shadow_frag_spv",
        "embedded Vulkan CSM fragment SPIR-V",
        failures,
    )

    if failures:
        print("Shadow projection source contract violations:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("Checked shadow projection source contract.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
