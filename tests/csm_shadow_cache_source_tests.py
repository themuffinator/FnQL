from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(text: str, needle: str, label: str, failures: list[str]) -> None:
    if needle not in text:
        failures.append(f"missing {label}: {needle}")


def reject(text: str, needle: str, label: str, failures: list[str]) -> None:
    if needle in text:
        failures.append(f"unexpected {label}: {needle}")


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


def check_backend(
    label: str,
    backend_path: str,
    header_path: str,
    mark_rendered: str,
    atlas_ready: str,
    failures: list[str],
) -> None:
    backend = (ROOT / backend_path).read_text(encoding="utf-8")
    header = (ROOT / header_path).read_text(encoding="utf-8")
    end_render = "FBO_EndCSMShadowAtlas();" if label == "OpenGL" else "vk_end_csm_shadow_render_pass();"

    signature = section(
        backend,
        "static qboolean RB_CSMShadowCacheSignature",
        "static void RB_SetCSMShadowProjection",
        f"{label} cache signature",
        failures,
    )
    render = section(
        backend,
        "static void RB_RenderCSMShadowAtlas",
        "static void RB_CSMShadowReceiverPass",
        f"{label} atlas render",
        failures,
    )
    receiver_gate = section(
        backend,
        "static void RB_CSMShadowReceiverPass",
        "static void RB_SetDlightShadowCasterEntity",
        f"{label} receiver gate",
        failures,
    )

    for counter in (
        "c_csmShadowAtlasCacheHits",
        "c_csmShadowAtlasCacheMisses",
        "c_csmShadowAtlasCacheUncacheable",
        "c_csmShadowAtlasMsec",
    ):
        require(header, f"int\t\t{counter};", f"{label} backend counter {counter}", failures)

    require(signature, "RB_DlightShadowCacheHashUInt( hash, (unsigned int)tr.csm.cascadeCount )", f"{label} cascade count hash", failures)
    require(signature, "RB_DlightShadowCacheHashUInt( hash, (unsigned int)tr.csm.resolution )", f"{label} cascade resolution hash", failures)
    require(signature, "RB_DlightShadowCacheHashFloat( hash, cascade->bounds[0][j] )", f"{label} cascade min bounds hash", failures)
    require(signature, "RB_DlightShadowCacheHashFloat( hash, cascade->bounds[1][j] )", f"{label} cascade max bounds hash", failures)
    require(signature, "RB_DlightShadowCacheHashFloat( hash, cascade->axis[0][j] )", f"{label} cascade forward axis hash", failures)
    require(signature, "RB_DlightShadowCacheHashFloat( hash, cascade->axis[1][j] )", f"{label} cascade side axis hash", failures)
    require(signature, "RB_DlightShadowCacheHashFloat( hash, cascade->axis[2][j] )", f"{label} cascade up axis hash", failures)
    require(
        backend,
        "static float RB_CSMShadowCacheQuantizeDepth( float value, float texelSize )",
        f"{label} light-depth bound quantize helper",
        failures,
    )
    require(backend, "return floorf( value / texelSize );", f"{label} light-depth bound quantize floor", failures)
    require(
        signature,
        "RB_CSMShadowCacheQuantizeDepth( cascade->bounds[0][0], cascade->texelSize )",
        f"{label} quantized min light-depth bound hash",
        failures,
    )
    require(
        signature,
        "RB_CSMShadowCacheQuantizeDepth( cascade->bounds[1][0], cascade->texelSize )",
        f"{label} quantized max light-depth bound hash",
        failures,
    )
    reject(
        signature,
        "RB_DlightShadowCacheHashFloat( hash, cascade->bounds[0][0] )",
        f"{label} raw min light-depth bound hash defeats the atlas cache",
        failures,
    )
    reject(
        signature,
        "RB_DlightShadowCacheHashFloat( hash, cascade->bounds[1][0] )",
        f"{label} raw max light-depth bound hash defeats the atlas cache",
        failures,
    )
    require_order(
        signature,
        [
            "if ( shader->numDeforms > 0 ) {",
            "return qfalse;",
        ],
        f"{label} deformed surfaces make cache unusable",
        failures,
    )
    require(signature, "RB_DlightShadowCacheHashPtr( hash, drawSurf->surface )", f"{label} surface identity hash", failures)
    require(signature, "RB_DlightShadowCacheHashUInt( hash, (unsigned int)ent->e.frame )", f"{label} entity frame hash", failures)
    require(signature, "RB_DlightShadowCacheHashFloat( hash, ent->e.origin[axis] )", f"{label} entity origin hash", failures)
    require(signature, "RB_DlightShadowCacheHashFloat( hash, ent->e.axis[2][axis] )", f"{label} entity orientation hash", failures)

    require_order(
        render,
        [
            "cacheable = RB_CSMShadowCacheSignature( drawSurfs, numDrawSurfs, &signature );",
            "if ( cacheable && rb_csmShadowCache.valid &&",
            "backEnd.pc.c_csmShadowAtlasCacheHits++;",
            mark_rendered,
            "R_ShadowManagerPublishCSMAtlas( &tr.shadowManager,",
            f"{atlas_ready}, generation );",
            "return;",
            "if ( cacheable ) {",
            "backEnd.pc.c_csmShadowAtlasCacheMisses++;",
            "} else {",
            "backEnd.pc.c_csmShadowAtlasCacheUncacheable++;",
        ],
        f"{label} cache hit/miss/uncacheable accounting",
        failures,
    )
    require_order(
        render,
        [
            end_render,
            "R_ShadowManagerPublishCSMAtlas( &tr.shadowManager,",
            f"{atlas_ready}, generation );",
            "if ( cacheable && surfaces > 0 ) {",
            "rb_csmShadowCache.valid = qtrue;",
            "rb_csmShadowCache.generation = generation;",
            "rb_csmShadowCache.signature = signature;",
            "} else {",
            "rb_csmShadowCache.valid = qfalse;",
        ],
        f"{label} cache publication and invalidation",
        failures,
    )
    require(
        receiver_gate,
        "( !R_ShadowManagerPassScheduled( &tr.shadowManager,\n\t\t\t\t\tSHADOW_MANAGER_PASS_CSM_RECEIVER ) ||\n\t\t\t\t!tr.shadowManager.csmAtlasPublication.published )",
        f"{label} manager publication receiver gate",
        failures,
    )
    require(render, "startMsec = ri.Milliseconds();", f"{label} CSM atlas timer start", failures)
    require(render, "backEnd.pc.c_csmShadowAtlasMsec += ri.Milliseconds() - startMsec;", f"{label} CSM atlas elapsed counter", failures)


def check_glx_gpu_pass(failures: list[str]) -> None:
    public = (ROOT / "code/renderercommon/tr_glx_public.h").read_text(encoding="utf-8")
    profiler = (ROOT / "code/rendererglx/glx_profiler.cpp").read_text(encoding="utf-8")
    backend = (ROOT / "code/renderer/tr_backend.c").read_text(encoding="utf-8")
    render = section(
        backend,
        "static void RB_RenderCSMShadowAtlas",
        "static void RB_CSMShadowReceiverPass",
        "OpenGL CSM atlas render for GLX timing",
        failures,
    )

    require(public, "#define GLX_GPU_PASS_CSM_SHADOW 15", "GLX CSM GPU pass enum", failures)
    require(public, "#define GLX_GPU_PASS_COUNT 16", "GLX GPU pass count includes CSM", failures)
    require(profiler, 'return "csm-shadow-atlas";', "GLX CSM GPU pass name", failures)
    require_order(
        render,
        [
            "startMsec = ri.Milliseconds();",
            "GLX_CompatBeginGpuPassTimer( GLX_GPU_PASS_CSM_SHADOW );",
            "GLX_CompatEndGpuPassTimer( GLX_GPU_PASS_CSM_SHADOW );",
            "backEnd.pc.c_csmShadowAtlasMsec += ri.Milliseconds() - startMsec;",
        ],
        "OpenGL CSM CPU/GPU timer order",
        failures,
    )


def check_debug(label: str, path: str, failures: list[str]) -> None:
    source = (ROOT / path).read_text(encoding="utf-8")
    cascade_call = (
        'R_PrintCSMCascadeDebug( csm, "glx", qfalse, qfalse, qfalse, qfalse );'
        if label == "OpenGL"
        else 'R_PrintCSMCascadeDebug( csm, "vulkan", qtrue, qtrue, qtrue, qtrue );'
    )
    debug = section(
        source,
        '"csm shadows sky:%s',
        "backEnd.pc.c_csmShadowReceiverEntitySurfaces );",
        f"{label} debug print",
        failures,
    )
    require(debug, "cache h/m/u:%i/%i/%i", f"{label} cache debug label", failures)
    require(debug, "depth center:%.0f %.0f %.0f %.0f", f"{label} light-depth center debug label", failures)
    require(debug, "depthCenter[0], depthCenter[1], depthCenter[2], depthCenter[3]", f"{label} light-depth center debug args", failures)
    require(debug, "cpu:%ims", f"{label} CSM CPU debug label", failures)
    require(debug, "backEnd.pc.c_csmShadowAtlasCacheHits", f"{label} cache hit debug arg", failures)
    require(debug, "backEnd.pc.c_csmShadowAtlasCacheMisses", f"{label} cache miss debug arg", failures)
    require(debug, "backEnd.pc.c_csmShadowAtlasCacheUncacheable", f"{label} cache uncacheable debug arg", failures)
    require(debug, "backEnd.pc.c_csmShadowAtlasMsec", f"{label} CSM CPU debug arg", failures)
    require(source, "static void R_PrintCSMCascadeDebug", f"{label} per-cascade debug helper", failures)
    require(
        source,
        '"csm cascade backend:%s index:%i split:%.0f..%.0f',
        f"{label} per-cascade debug label",
        failures,
    )
    require(
        source,
        "sample-y:%s clip-y:%s depth:forward ndc:%s clear:1.0 compare:lequal",
        f"{label} CSM convention debug label",
        failures,
    )
    require(source, cascade_call, f"{label} backend-specific CSM convention debug call", failures)


def main() -> int:
    failures: list[str] = []
    check_backend(
        "OpenGL",
        "code/renderer/tr_backend.c",
        "code/renderer/tr_local.h",
        "FBO_MarkCSMShadowAtlasRendered();",
        "FBO_CSMShadowsReady()",
        failures,
    )
    check_backend(
        "Vulkan",
        "code/renderervk/tr_backend.c",
        "code/renderervk/tr_local.h",
        "vk_mark_csm_shadow_atlas_rendered();",
        "vk_csm_shadow_atlas_ready()",
        failures,
    )
    check_backend(
        "RTX",
        "code/rendererrtx/tr_backend.c",
        "code/rendererrtx/tr_local.h",
        "vk_mark_csm_shadow_atlas_rendered();",
        "vk_csm_shadow_atlas_ready()",
        failures,
    )
    check_debug("OpenGL", "code/renderer/tr_cmds.c", failures)
    check_debug("Vulkan", "code/renderervk/tr_cmds.c", failures)
    check_debug("RTX", "code/rendererrtx/tr_cmds.c", failures)
    check_glx_gpu_pass(failures)

    if failures:
        print("CSM shadow cache source contract violations:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("Checked CSM shadow cache source contract.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
