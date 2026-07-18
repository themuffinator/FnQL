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
    light_object = section(
        source,
        "static qboolean R_StaticMapLightsParseLightObject",
        "static qboolean R_StaticMapLightsParseLightsArray",
        f"{label} light object parser",
        failures,
    )
    light_array = section(
        source,
        "static qboolean R_StaticMapLightsParseLightsArray",
        "static qboolean R_ParseStaticMapLights",
        f"{label} lights array parser",
        failures,
    )
    top_parser = section(
        source,
        "static qboolean R_ParseStaticMapLights",
        "static void R_LoadStaticMapLightsForWorld",
        f"{label} top parser",
        failures,
    )
    loader = section(
        source,
        "static void R_LoadStaticMapLightsForWorld",
        "void R_StaticMapLightsReload_f",
        f"{label} sidecar loader",
        failures,
    )

    require(light_object, "light->castsShadows = qtrue;", f"{label} shadow default", failures)
    require(light_object, "light->designerPriority = 1.0f;", f"{label} priority default", failures)
    require(light_object, "light->resolution = 256;", f"{label} resolution default", failures)
    require(light_object, "light->outerAngle = 45.0f;", f"{label} outer angle default", failures)
    require(light_object, "unsupportedType = qtrue;", f"{label} unsupported type flag", failures)
    require(light_object, "*skipReason = 1;", f"{label} unsupported skip reason", failures)
    require(light_object, "*skipReason = 2;", f"{label} invalid skip reason", failures)
    require(light_object, "light->resolution = (int)Com_Clamp( 64.0f, 1024.0f, (float)light->resolution );", f"{label} resolution clamp", failures)
    require(light_object, "light->innerAngle = Com_Clamp( 0.0f, light->outerAngle, light->innerAngle );", f"{label} cone clamp", failures)
    require(light_object, "} else if ( !R_StaticMapLightsSkipValue( p ) ) {", f"{label} forward-compatible object key skip", failures)
    require_order(
        light_object,
        [
            "if ( !hasOrigin || light->designerPriority <= 0.0f ) {",
            "*skipReason = 2;",
            "if ( light->type == MAP_LIGHT_SPOT && !hasDirection ) {",
            "*skipReason = 2;",
        ],
        f"{label} invalid light rejection order",
        failures,
    )

    require(light_array, "tr.staticMapLights.skippedOverflow++;", f"{label} overflow counter", failures)
    require(light_array, 'R_StaticMapLightsSkipCompound( p, "{" )', f"{label} overflow object skip", failures)
    require(light_array, "tr.staticMapLights.skippedUnsupported++;", f"{label} unsupported counter", failures)
    require(light_array, "tr.staticMapLights.skippedInvalid++;", f"{label} invalid counter", failures)
    require(light_array, "tr.staticMapLights.lights[tr.staticMapLights.count++] = light;", f"{label} accepted light store", failures)
    require_order(
        light_array,
        [
            "if ( tr.staticMapLights.count >= MAX_STATIC_MAP_LIGHTS ) {",
            "tr.staticMapLights.skippedOverflow++;",
            "} else {",
            "if ( accepted ) {",
            "} else if ( skipReason == 1 ) {",
            "tr.staticMapLights.skippedUnsupported++;",
            "} else {",
            "tr.staticMapLights.skippedInvalid++;",
        ],
        f"{label} parser counter flow",
        failures,
    )

    require(top_parser, 'if ( strcmp( token, "{" ) != 0 ) {', f"{label} malformed root reject", failures)
    require(top_parser, 'if ( !R_StaticMapLightsExpectToken( &p, ":" ) ) {', f"{label} malformed key reject", failures)
    require(top_parser, 'if ( !R_StaticMapLightsParseLightsArray( &p ) ) {', f"{label} malformed lights array reject", failures)
    require(top_parser, "} else if ( !R_StaticMapLightsSkipValue( &p ) ) {", f"{label} forward-compatible root key skip", failures)

    require(loader, "tr.staticMapLights.loaded = qtrue;", f"{label} loaded flag before parse", failures)
    require(loader, "tr.staticMapLights.parseFailed = qtrue;", f"{label} parse-failed flag", failures)
    require(loader, "tr.staticMapLights.count = 0;", f"{label} parse-failed count reset", failures)
    require(loader, "WARNING: failed to parse static map lights file %s", f"{label} parse warning", failures)


def main() -> int:
    failures: list[str] = []
    for label, relative_path in (
        ("OpenGL", "code/renderer/tr_bsp.c"),
        ("Vulkan", "code/renderervk/tr_bsp.c"),
        ("RTX", "code/rendererrtx/tr_bsp.c"),
    ):
        check_source(label, relative_path, failures)

    if failures:
        print("Static map light sidecar source contract violations:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print("Checked static map light sidecar source contract.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
