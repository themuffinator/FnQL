from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]

PURE_HEADERS = (
    "code/renderercommon/tr_glx_public.h",
    "code/rendererglx/glx_types.h",
    "code/rendererglx/glx_caps_logic.h",
    "code/rendererglx/glx_color_math.h",
    "code/rendererglx/glx_material_key.h",
    "code/rendererglx/glx_post_output_reference.h",
    "code/rendererglx/glx_post_shader_plan.h",
    "code/rendererglx/glx_post_shader_source.h",
    "code/rendererglx/glx_render_ir.h",
    "code/rendererglx/glx_stream_logic.h",
    "code/rendererglx/glx_static_world_logic.h",
)

MODULE_BRIDGE_HEADERS = (
    "code/renderercommon/tr_glx_api.h",
    "code/renderercommon/tr_glx_bridge.h",
    "code/rendererglx/glx_module.h",
)

RENDERER_FACADE_HEADERS = (
    "code/renderer/tr_glx_compat.h",
)

DRAW_OWNERSHIP_SOURCE_GLOB = "code/renderer/*.c"
DRAW_CALL_RE = re.compile(r"\bqglDraw(?:Elements|Arrays)\s*\(")

PURE_BANNED_INCLUDE_FRAGMENTS = (
    "../renderer/",
    "renderer/qgl.h",
    "qcommon.h",
    "tr_public.h",
    "qgl.h",
    "glx_local.h",
    "glx_module.h",
    "glx_caps.h",
    "glx_material.h",
    "glx_stream.h",
    "glx_static_world.h",
    "glx_profiler.h",
    "glx_postprocess.h",
)

MODULE_BANNED_INCLUDE_FRAGMENTS = (
    "../renderer/",
    "renderer/qgl.h",
    "qcommon.h",
    "tr_public.h",
    "qgl.h",
    "glx_local.h",
)

PURE_BANNED_TYPE_PATTERNS = (
    r"\bGLenum\b",
    r"\bGLuint\b",
    r"\bGLint\b",
    r"\bGLsizei\b",
    r"\bGLchar\b",
    r"\bGLfloat\b",
    r"\bGLvoid\b",
    r"\bglconfig_t\b",
    r"\brefimport_t\b",
    r"\brefexport_t\b",
    r"\brefShutdownCode_t\b",
    r"\bcvar_t\b",
    r"\bshader_t\b",
    r"\bshaderStage_t\b",
    r"\bmsurface_t\b",
)

MODULE_BANNED_TYPE_PATTERNS = (
    r"\bGLenum\b",
    r"\bGLuint\b",
    r"\bGLint\b",
    r"\bGLsizei\b",
    r"\bGLchar\b",
    r"\bGLfloat\b",
    r"\bGLvoid\b",
    r"\brefShutdownCode_t\b",
    r"\bcvar_t\b",
    r"\bshader_t\b",
    r"\bshaderStage_t\b",
    r"\bmsurface_t\b",
)

FACADE_BANNED_INCLUDE_FRAGMENTS = (
    "../rendererglx/",
    "rendererglx/",
)


def scan_header(
    path: Path,
    banned_include_fragments: tuple[str, ...],
    banned_type_patterns: tuple[str, ...],
) -> list[str]:
    failures: list[str] = []
    text = path.read_text(encoding="utf-8")

    for line_no, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if stripped.startswith("#include"):
            for fragment in banned_include_fragments:
                if fragment in stripped:
                    failures.append(f"{path}:{line_no}: banned include dependency: {stripped}")

        if "qgl" in line:
            failures.append(f"{path}:{line_no}: banned qgl reference: {stripped}")

        for pattern in banned_type_patterns:
            if re.search(pattern, line):
                failures.append(f"{path}:{line_no}: banned renderer/OpenGL type: {stripped}")

    return failures


def scan_includes_only(path: Path, banned_include_fragments: tuple[str, ...]) -> list[str]:
    failures: list[str] = []
    text = path.read_text(encoding="utf-8")

    for line_no, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if not stripped.startswith("#include"):
            continue
        for fragment in banned_include_fragments:
            if fragment in stripped:
                failures.append(f"{path}:{line_no}: banned include dependency: {stripped}")

    return failures


def scan_glx_draw_ownership(path: Path) -> list[str]:
    failures: list[str] = []
    renderer_glx_stack: list[bool | None] = []
    text = path.read_text(encoding="utf-8")

    for line_no, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if re.match(r"#\s*ifdef\s+RENDERER_GLX\b", stripped):
            renderer_glx_stack.append(True)
            continue
        if re.match(r"#\s*ifndef\s+RENDERER_GLX\b", stripped):
            renderer_glx_stack.append(False)
            continue
        if stripped.startswith("#if"):
            renderer_glx_stack.append(None)
            continue
        if stripped.startswith("#else") and renderer_glx_stack:
            if renderer_glx_stack[-1] is True:
                renderer_glx_stack[-1] = False
            elif renderer_glx_stack[-1] is False:
                renderer_glx_stack[-1] = True
            continue
        if stripped.startswith("#endif") and renderer_glx_stack:
            renderer_glx_stack.pop()
            continue

        if not DRAW_CALL_RE.search(line):
            continue

        renderer_glx_state = next(
            (state for state in reversed(renderer_glx_stack) if state is not None),
            None,
        )
        if renderer_glx_state is not False:
            failures.append(
                f"{path}:{line_no}: direct qgl draw is reachable in RENDERER_GLX: {stripped}"
            )

    return failures


def main() -> int:
    failures: list[str] = []

    for relative in PURE_HEADERS:
        path = ROOT / relative
        if not path.exists():
            failures.append(f"{path}: missing pure GLx header")
            continue
        failures.extend(scan_header(path, PURE_BANNED_INCLUDE_FRAGMENTS, PURE_BANNED_TYPE_PATTERNS))

    for relative in MODULE_BRIDGE_HEADERS:
        path = ROOT / relative
        if not path.exists():
            failures.append(f"{path}: missing GLx module bridge header")
            continue
        failures.extend(scan_header(path, MODULE_BANNED_INCLUDE_FRAGMENTS, MODULE_BANNED_TYPE_PATTERNS))

    for relative in RENDERER_FACADE_HEADERS:
        path = ROOT / relative
        if not path.exists():
            failures.append(f"{path}: missing GLx renderer facade header")
            continue
        failures.extend(scan_includes_only(path, FACADE_BANNED_INCLUDE_FRAGMENTS))

    draw_source_count = 0
    for path in sorted(ROOT.glob(DRAW_OWNERSHIP_SOURCE_GLOB)):
        draw_source_count += 1
        failures.extend(scan_glx_draw_ownership(path))

    if failures:
        print("GLx boundary violations:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1

    print(
        f"Checked {len(PURE_HEADERS)} pure GLx headers and "
        f"{len(MODULE_BRIDGE_HEADERS)} module bridge headers; "
        f"{len(RENDERER_FACADE_HEADERS)} renderer facade header no longer includes rendererglx; "
        f"{draw_source_count} renderer sources keep raw qgl draws out of RENDERER_GLX."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
