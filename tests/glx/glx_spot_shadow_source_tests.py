from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def require(text: str, needle: str, label: str, failures: list[str]) -> None:
    if needle not in text:
        failures.append(f"missing {label}: {needle}")


def main() -> int:
    failures: list[str] = []
    glx_module = (ROOT / "code/rendererglx/glx_module.cpp").read_text(encoding="utf-8")
    tr_arb = (ROOT / "code/renderer/tr_arb.c").read_text(encoding="utf-8")
    gl_tr_scene = (ROOT / "code/renderer/tr_scene.c").read_text(encoding="utf-8")
    gl_tr_main = (ROOT / "code/renderer/tr_main.c").read_text(encoding="utf-8")
    vk_tr_scene = (ROOT / "code/renderervk/tr_scene.c").read_text(encoding="utf-8")
    vk_tr_main = (ROOT / "code/renderervk/tr_main.c").read_text(encoding="utf-8")
    vk_light_frag = (ROOT / "code/renderervk/shaders/light_frag.tmpl").read_text(
        encoding="utf-8"
    )

    require(
        glx_module,
        'bool spotShadow = u_ShadowFilter.z > 0.5;',
        "GLX GLSL spot-shadow mode flag",
        failures,
    )
    require(
        glx_module,
        'vec3 lightToFrag = -rawLV;',
        "GLX GLSL spot projection input",
        failures,
    )
    require(
        glx_module,
        'vec2 atlasPx = vec2(u_ShadowAtlas.x + uv.x * tileSize,',
        "GLX GLSL spot atlas projection",
        failures,
    )
    require(
        glx_module,
        'float inset = 1.0 / tileSize;',
        "GLX GLSL spot atlas texel inset",
        failures,
    )
    require(
        glx_module,
        'uv = clamp(uv, vec2(inset), vec2(1.0 - inset));',
        "GLX GLSL spot atlas interior clamp",
        failures,
    )
    require(
        glx_module,
        "float coneRadius2 = dot(uv, uv);",
        "GLX GLSL circular cone gate",
        failures,
    )
    require(
        glx_module,
        'shadowFactor = dlightShadowFactor(dnLV, v_LightVector, nn, lv);',
        "GLX GLSL raw light vector handoff",
        failures,
    )
    require(
        tr_arb,
        "static qboolean GLX_SpotShadowParams",
        "OpenGL spot-shadow uniform setup",
        failures,
    )
    require(
        tr_arb,
        "shadowFilter[2] = 1.0f;",
        "OpenGL spot-shadow mode upload",
        failures,
    )
    require(
        tr_arb,
        "FBO_BindSpotShadowTexture( 2 );",
        "OpenGL spot atlas texture binding",
        failures,
    )
    require(
        vk_light_frag,
        "float coneRadius2 = dot(projected, projected);",
        "Vulkan GLSL circular cone gate",
        failures,
    )
    for label, source in (
        ("OpenGL", gl_tr_main),
        ("Vulkan", vk_tr_main),
    ):
        require(
            source,
            "static int R_ShadowSpotEffectiveTileSize",
            f"{label} per-spot effective tile sizing",
            failures,
        )
        require(
            source,
            "return MIN( tileSize, atlasLayout->tileSize );",
            f"{label} per-spot tile cap",
            failures,
        )
        require(
            source,
            "record->atlasX = cellX + ( atlasLayout->tileSize - tileSize ) / 2;",
            f"{label} centered per-spot atlas X",
            failures,
        )
        require(
            source,
            "record->atlasY = cellY + ( atlasLayout->tileSize - tileSize ) / 2;",
            f"{label} centered per-spot atlas Y",
            failures,
        )
        require(
            source,
            "record->atlasTileSize = tileSize;",
            f"{label} per-spot atlas tile assignment",
            failures,
        )
        require(
            source,
            "usedPixels += (int64_t)plan->atlasTileSize * plan->atlasTileSize;",
            f"{label} per-spot atlas fill accounting",
            failures,
        )
        require(
            source,
            "static int R_ShadowSpotSurfaceRequestedTileSize",
            f"{label} surfacelight spot LOD helper",
            failures,
        )
        require(
            source,
            "importance = Com_Clamp( 0.0f, 1.0f, hemisphereWeight ) *",
            f"{label} surfacelight spot importance weighting",
            failures,
        )
        require(
            source,
            "tileSize = MIN( tileSize, 128 );",
            f"{label} surfacelight spot low-detail tile",
            failures,
        )
        require(
            source,
            "tileSize = MAX( tileSize, 512 );",
            f"{label} surfacelight spot promoted tile",
            failures,
        )
        require(
            source,
            "requestedTileSize = R_ShadowSpotSurfaceRequestedTileSize( proxy,",
            f"{label} surfacelight spot per-proxy tile request",
            failures,
        )
        require(
            source,
            "casterRadius = ( proxy->shadowCasterRadius > 0.0f ) ?",
            f"{label} surfacelight spot bounded caster radius",
            failures,
        )
        require(
            source,
            "outerAngle = ( proxy->shadowConeAngle > 0.0f ) ?",
            f"{label} surfacelight spot per-proxy cone angle",
            failures,
        )
        require(
            source,
            "proxy->origin, proxy->normal, proxy->color, casterRadius, proxy->intensity",
            f"{label} surfacelight spot candidate uses caster radius",
            failures,
        )
        require(
            source,
            "R_ShadowManagerAddSpotCandidate( SHADOW_SPOT_SOURCE_STATIC_MAP, i,",
            f"{label} static sidecar spot atlas candidate",
            failures,
        )
        require(
            source,
            "light->innerAngle, light->outerAngle, light->resolution, priority",
            f"{label} static sidecar spot cone and resolution",
            failures,
        )
        require(
            source,
            "manager->spotStaticPlanCount++;",
            f"{label} static sidecar spot plan accounting",
            failures,
        )

    for label, source in (
        ("OpenGL", gl_tr_scene),
        ("Vulkan", vk_tr_scene),
    ):
        require(
            source,
            "RE_AddLinearLightToScene( light->origin, end, light->radius,",
            f"{label} static sidecar spot preview promotion",
            failures,
        )
        require(
            source,
            "dl->shadowSpotSource = SHADOW_SPOT_SOURCE_STATIC_MAP;",
            f"{label} static sidecar spot source identity",
            failures,
        )
        require(
            source,
            "dl->shadowSpotSourceIndex = bestIndex;",
            f"{label} static sidecar spot source index",
            failures,
        )

    if failures:
        print("Spot shadow source contract violations:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("Checked spotlight shadow source contract.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
