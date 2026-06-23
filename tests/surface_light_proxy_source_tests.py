from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


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


def check_source(label: str, relative_path: str, failures: list[str]) -> None:
    source = (ROOT / relative_path).read_text(encoding="utf-8")
    header = (ROOT / relative_path.replace("tr_bsp.c", "tr_local.h")).read_text(
        encoding="utf-8"
    )

    triangle_info = section(
        source,
        "static qboolean R_SurfaceLightTriangleInfo",
        "static void R_SurfaceLightAccumulateTriangle",
        f"{label} triangle info",
        failures,
    )
    accumulate = section(
        source,
        "static void R_SurfaceLightAccumulateTriangle",
        "static float R_SurfaceLightProxyRadius",
        f"{label} triangle accumulation",
        failures,
    )
    projection = section(
        source,
        "static surfaceLightProxyProjection_t R_SurfaceLightProxyProjection",
        "static qboolean R_AddSurfaceLightProxy",
        f"{label} projection selection",
        failures,
    )
    add_proxy = section(
        source,
        "static qboolean R_AddSurfaceLightProxy",
        "static qboolean R_SurfaceLightBeginSubdivision",
        f"{label} proxy creation",
        failures,
    )
    begin_subdivision = section(
        source,
        "static qboolean R_SurfaceLightBeginSubdivision",
        "static void R_SurfaceLightSubdivisionAddPoint",
        f"{label} subdivision start",
        failures,
    )
    finalize_subdivision = section(
        source,
        "static qboolean R_SurfaceLightFinalizeSubdivision",
        "static int R_SurfaceLightSubdivisionBucketForCentroid",
        f"{label} subdivision finalization",
        failures,
    )
    bucket = section(
        source,
        "static int R_SurfaceLightSubdivisionBucketForCentroid",
        "static void R_SurfaceLightAccumulateTriangleForSubdivision",
        f"{label} subdivision bucket selection",
        failures,
    )
    bucket_accumulate = section(
        source,
        "static void R_SurfaceLightAccumulateTriangleForSubdivision",
        "static qboolean R_AddSurfaceLightBucketedProxies",
        f"{label} subdivision accumulation",
        failures,
    )
    bucketed_proxies = section(
        source,
        "static qboolean R_AddSurfaceLightBucketedProxies",
        "static qboolean R_BuildSurfaceLightFaceProxy",
        f"{label} bucketed proxy emission",
        failures,
    )
    face = section(
        source,
        "static qboolean R_BuildSurfaceLightFaceProxy",
        "static qboolean R_BuildSurfaceLightGridProxy",
        f"{label} face proxy build",
        failures,
    )
    grid = section(
        source,
        "static qboolean R_BuildSurfaceLightGridProxy",
        "static qboolean R_BuildSurfaceLightTriProxy",
        f"{label} grid proxy build",
        failures,
    )
    tri = section(
        source,
        "static qboolean R_BuildSurfaceLightTriProxy",
        "static void R_BuildSurfaceLightProxyForSurface",
        f"{label} triangle-soup proxy build",
        failures,
    )
    dispatch = section(
        source,
        "static void R_BuildSurfaceLightProxyForSurface",
        "static void R_BuildSurfaceLightProxiesForWorld",
        f"{label} surface dispatch",
        failures,
    )

    require(header, "float\t\tarea;", f"{label} proxy area field", failures)
    require(header, "float\t\tfootprintRadius;", f"{label} proxy footprint radius field", failures)
    require(header, "float\t\tshadowCasterRadius;", f"{label} proxy shadow caster radius field", failures)
    require(header, "float\t\tshadowConeAngle;", f"{label} proxy shadow cone angle field", failures)
    require(header, "vec3_t\t\tnormal;", f"{label} proxy normal field", failures)
    require(header, "int\t\t\tsubdividedSurfaces;", f"{label} subdivision surface counter", failures)
    require(header, "int\t\t\tsubdivisionProxies;", f"{label} subdivision proxy counter", failures)
    require(source, "#define SURFACELIGHT_PROXY_SUBDIVIDE_MIN_SIZE 64.0f", f"{label} subdivision minimum size", failures)
    require(source, "#define SURFACELIGHT_PROXY_SUBDIVIDE_MAX_SIZE 1024.0f", f"{label} subdivision maximum size", failures)
    require(source, "#define SURFACELIGHT_PROXY_SUBDIVIDE_MAX_AXIS 4", f"{label} subdivision axis cap", failures)
    require(
        source,
        "#define SURFACELIGHT_PROXY_SUBDIVIDE_MAX_BUCKETS ( SURFACELIGHT_PROXY_SUBDIVIDE_MAX_AXIS * SURFACELIGHT_PROXY_SUBDIVIDE_MAX_AXIS )",
        f"{label} subdivision bucket cap",
        failures,
    )
    require(source, "vec3_t boundsMins;", f"{label} accumulator bounds mins", failures)
    require(source, "vec3_t boundsMaxs;", f"{label} accumulator bounds maxs", failures)
    require(source, "VectorSet( accum->boundsMins, 999999.0f, 999999.0f, 999999.0f );", f"{label} accumulator bounds min initialization", failures)
    require(source, "VectorSet( accum->boundsMaxs, -999999.0f, -999999.0f, -999999.0f );", f"{label} accumulator bounds max initialization", failures)
    require(source, "static void R_SurfaceLightAccumTriangleBounds", f"{label} triangle bounds accumulation helper", failures)
    require(source, "static float R_SurfaceLightProxyFootprintRadius", f"{label} proxy footprint radius helper", failures)
    require(source, "static float R_SurfaceLightProxyShadowCasterRadius", f"{label} proxy shadow caster radius helper", failures)
    require(source, "static float R_SurfaceLightProxyShadowConeAngle", f"{label} proxy shadow cone helper", failures)

    require(triangle_info, "CrossProduct( edge0, edge1, cross );", f"{label} normal from triangle cross product", failures)
    require(triangle_info, "if ( doubleArea <= 0.0f )", f"{label} degenerate triangle rejection", failures)
    require(triangle_info, "*area = doubleArea * 0.5f;", f"{label} triangle area calculation", failures)
    require(triangle_info, "VectorScale( centroid, 1.0f / 3.0f, centroid );", f"{label} triangle centroid average", failures)
    require(accumulate, "VectorMA( accum->centroidAccum, area, centroid, accum->centroidAccum );", f"{label} area-weighted centroid accumulation", failures)
    require(accumulate, "VectorAdd( accum->normalAccum, cross, accum->normalAccum );", f"{label} accumulated geometric normal", failures)
    require(accumulate, "R_SurfaceLightAccumTriangleBounds( accum, a, b, c );", f"{label} accumulated triangle bounds", failures)
    require(accumulate, "accum->area += area;", f"{label} accumulated area", failures)
    require(source, "return Com_Clamp( 16.0f, 2048.0f, MAX( radius, areaRadius ) );", f"{label} bounded proxy footprint radius", failures)
    require(source, "radius = Com_Clamp( 128.0f, 2048.0f, radius );", f"{label} bounded shadow caster radius", failures)
    require(source, "radius = MIN( radius, proxyRadius );", f"{label} caster radius respects light radius", failures)
    require(source, "return Com_Clamp( 35.0f, 70.0f, angle );", f"{label} bounded shadow cone angle", failures)

    require(projection, "if ( area <= Square( 96.0f ) )", f"{label} point projection threshold", failures)
    require(projection, "return SURFACE_LIGHT_PROXY_SPOT;", f"{label} large planar spot projection", failures)
    require(add_proxy, "if ( accum->area <= 1.0f )", f"{label} tiny-area rejection", failures)
    require(add_proxy, "if ( VectorNormalize2( accum->normalAccum, normal ) <= 0.0f )", f"{label} zero-normal rejection", failures)
    require(add_proxy, "VectorScale( accum->centroidAccum, 1.0f / accum->area, origin );", f"{label} stable emitter centroid", failures)
    require(add_proxy, "proxy->projection = R_SurfaceLightProxyProjection( accum->area );", f"{label} projection stored from area", failures)
    require(add_proxy, "proxy->area = accum->area;", f"{label} proxy area stored", failures)
    require(add_proxy, "proxy->footprintRadius = R_SurfaceLightProxyFootprintRadius( accum, normal );", f"{label} proxy footprint radius stored", failures)
    require(add_proxy, "proxy->shadowCasterRadius = R_SurfaceLightProxyShadowCasterRadius( shader,", f"{label} proxy shadow caster radius stored", failures)
    require(add_proxy, "proxy->shadowConeAngle = R_SurfaceLightProxyShadowConeAngle(", f"{label} proxy shadow cone angle stored", failures)
    require(add_proxy, "VectorCopy( normal, proxy->normal );", f"{label} normalized proxy normal stored", failures)
    require(add_proxy, "offset = Com_Clamp( 8.0f, 64.0f, proxy->radius * 0.05f );", f"{label} bounded normal offset", failures)
    require(add_proxy, "VectorMA( origin, offset, normal, proxy->origin );", f"{label} emitter origin offset along normal", failures)
    require(add_proxy, "tr.surfaceLightProxies.spotProjectionCount++;", f"{label} spot projection accounting", failures)
    require(add_proxy, "tr.surfaceLightProxies.skippedOverflow++;", f"{label} overflow accounting", failures)

    require(begin_subdivision, "shader->surfaceLightSubdivide <= 0.0f", f"{label} subdivision opt-in", failures)
    require(begin_subdivision, "accum->area <= Square( SURFACELIGHT_PROXY_SUBDIVIDE_MIN_SIZE )", f"{label} small surface subdivision bypass", failures)
    require(begin_subdivision, "MakeNormalVectors( normal, subdiv->axis[0], subdiv->axis[1] );", f"{label} subdivision axes from normal", failures)
    require(begin_subdivision, "subdiv->targetSize = Com_Clamp( SURFACELIGHT_PROXY_SUBDIVIDE_MIN_SIZE,", f"{label} subdivision target clamp start", failures)
    require(begin_subdivision, "SURFACELIGHT_PROXY_SUBDIVIDE_MAX_SIZE, shader->surfaceLightSubdivide );", f"{label} subdivision target clamp end", failures)
    require(finalize_subdivision, "cells = (int)ceilf( span / subdiv->targetSize );", f"{label} projected footprint sizing", failures)
    require(finalize_subdivision, "cells > SURFACELIGHT_PROXY_SUBDIVIDE_MAX_AXIS", f"{label} subdivision axis cap enforced", failures)
    require(finalize_subdivision, "subdiv->cellSpan[axis] = ( span > 1.0f ) ? ( span / (float)cells ) : subdiv->targetSize;", f"{label} stable cell span", failures)
    require(finalize_subdivision, "return ( subdiv->cells[0] * subdiv->cells[1] > 1 ) ? qtrue : qfalse;", f"{label} single-cell fallback", failures)
    require(bucket, "cell[axis] = (int)floorf( coord );", f"{label} projected centroid bucket", failures)
    require(bucket, "cell[axis] = subdiv->cells[axis] - 1;", f"{label} bucket upper clamp", failures)
    require(bucket_accumulate, "bucket = R_SurfaceLightSubdivisionBucketForCentroid( subdiv, centroid );", f"{label} triangle bucket assignment", failures)
    require(bucket_accumulate, "R_SurfaceLightAccumTriangleBounds( accum, a, b, c );", f"{label} bucket triangle bounds accumulation", failures)
    require(bucket_accumulate, "accum->area += area;", f"{label} bucket area accumulation", failures)
    require(bucketed_proxies, "if ( activeBuckets <= 1 )", f"{label} inactive bucket fallback", failures)
    require(bucketed_proxies, "return R_AddSurfaceLightProxy( surfaceIndex, shader, total );", f"{label} total proxy fallback", failures)
    require(bucketed_proxies, "tr.surfaceLightProxies.subdividedSurfaces++;", f"{label} subdivided surface accounting", failures)
    require(bucketed_proxies, "tr.surfaceLightProxies.subdivisionProxies++;", f"{label} subdivision proxy accounting", failures)

    for local_label, local_section in (
        ("face", face),
        ("grid", grid),
        ("triangle-soup", tri),
    ):
        require(local_section, "R_ClearSurfaceLightProxyAccum( &accum );", f"{label} {local_label} clears accumulator", failures)
        require(local_section, "R_SurfaceLightBeginSubdivision( shader, &accum, &subdiv );", f"{label} {local_label} subdivision hook", failures)
        require(local_section, "R_SurfaceLightFinalizeSubdivision( &subdiv );", f"{label} {local_label} subdivision finalize", failures)
        require(local_section, "R_AddSurfaceLightBucketedProxies( surfaceIndex, shader, &accum, buckets, bucketCount );", f"{label} {local_label} bucketed proxy emission", failures)
        require(local_section, "return R_AddSurfaceLightProxy( surfaceIndex, shader, &accum );", f"{label} {local_label} direct proxy fallback", failures)

    require(face, "VectorCopy( face->plane.normal, accum.normalAccum );", f"{label} face plane-normal fallback", failures)
    require(face, "if ( !face || face->numPoints <= 0 || face->numIndices < 3 )", f"{label} invalid face rejection", failures)
    require(grid, "if ( !grid || grid->width < 2 || grid->height < 2 )", f"{label} invalid grid rejection", failures)
    require(grid, "VectorAdd( accum.normalAccum, grid->verts[i].normal, accum.normalAccum );", f"{label} grid vertex-normal fallback", failures)
    require(tri, "if ( !tri || tri->numVerts <= 0 || tri->numIndexes < 3 )", f"{label} invalid triangle-soup rejection", failures)
    require(tri, "VectorAdd( accum.normalAccum, tri->verts[i].normal, accum.normalAccum );", f"{label} triangle-soup vertex-normal fallback", failures)
    require_order(
        dispatch,
        [
            "tr.surfaceLightProxies.sourceSurfaces++;",
            "if ( shader->isSky || ( shader->surfaceFlags & SURF_SKY ) ) {",
            "tr.surfaceLightProxies.skippedSky++;",
            "surfaceType = surf->data;",
            "if ( !surfaceType || *surfaceType == SF_SKIP ) {",
            "tr.surfaceLightProxies.skippedInvalid++;",
        ],
        f"{label} sky and invalid surface rejection order",
        failures,
    )
    require(dispatch, "case SF_FACE:", f"{label} face dispatch", failures)
    require(dispatch, "case SF_GRID:", f"{label} grid dispatch", failures)
    require(dispatch, "case SF_TRIANGLES:", f"{label} triangle dispatch", failures)
    require(dispatch, "tr.surfaceLightProxies.skippedInvalid++;", f"{label} unknown surface rejection", failures)


def main() -> int:
    failures: list[str] = []
    for label, relative_path in (
        ("OpenGL", "code/renderer/tr_bsp.c"),
        ("Vulkan", "code/renderervk/tr_bsp.c"),
    ):
        check_source(label, relative_path, failures)

    if failures:
        print("Surface light proxy source contract violations:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("Checked surfacelight proxy source contract.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
