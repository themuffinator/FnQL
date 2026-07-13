from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class WebUiBackendSourceTests(unittest.TestCase):
    def test_typed_backend_boundary_is_versioned_and_default_null(self) -> None:
        header = (ROOT / "code" / "client" / "webui_backend.hpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("kBackendInterfaceVersion", header)
        self.assertIn("enum class Lifecycle", header)
        self.assertIn("class Backend", header)
        self.assertIn("class NullBackend final", header)
        self.assertIn("class BackendHost final", header)
        self.assertIn("BackendResult InstallBackend( Backend &backend )", header)
        self.assertIn("SurfaceMismatch", header)
        self.assertIn("Rgba8Premultiplied", header)
        self.assertIn("struct HostServices", header)
        self.assertIn("RequestResourceFn", header)

    def test_legacy_facade_delegates_to_the_platform_adapter(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn('#include "webui_backend.hpp"', source)
        self.assertIn("fnql::webui::ClientBackendHost().Start", source)
        self.assertIn("host.Navigate", source)
        self.assertIn("host.CopySurface", source)
        self.assertIn("CL_WebUI_BackendRequestResource", source)
        self.assertIn("CL_WebUI_BackendReleaseResource", source)
        self.assertIn("InstallRetailAwesomiumBackend", source)
        self.assertIn('"1",\n#else\n\t\t"0",', source)
        self.assertNotIn("LoadLibraryA(", source)
        self.assertNotIn("GetProcAddress(", source)
        self.assertNotIn("_Awe_", source)

        adapter = (
            ROOT / "code" / "client" / "awesomium_backend_win32.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn("class RetailAwesomiumBackend final", adapter)
        self.assertIn("LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR", adapter)
        self.assertIn("GetProcAddress( module_, name )", adapter)
        self.assertIn("_Awe_WebCore_Initialize@4", adapter)
        self.assertIn("newDataPakSource( webPakPath_.c_str() )", adapter)
        self.assertIn("FNQL_WEBUI_VERBOSE_LOG", adapter)
        self.assertIn("if ( module_ )", adapter)
        self.assertIn("webPakPath_.clear();", adapter)
        self.assertNotIn("#include <Awesomium/", adapter)

    def test_native_requests_are_origin_locked_and_losslessly_decoded(self) -> None:
        header = (ROOT / "code" / "client" / "webui_backend.hpp").read_text(
            encoding="utf-8"
        )
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn('kTrustedNavigationPrefix = "asset://ql/"', header)
        self.assertIn("IsTrustedNavigationUrl", source)
        self.assertIn("malformed UTF-16", source)
        self.assertIn("exceeds the UTF-8 bridge buffer", source)
        self.assertNotIn("codepoint <= 0 || codepoint > 255", source)

    def test_native_ui_ownership_requires_the_live_surface_presenter(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(
            encoding="utf-8"
        )
        drawable = source[
            source.index("qboolean CL_WebHost_HasDrawableSurface") :
            source.index("static qboolean CL_WebHost_CanDispatchLiveEvent")
        ]

        self.assertIn("host.Status().HasSurface()", drawable)
        self.assertIn("!re.DrawWebUISurface", drawable)
        self.assertIn("return qtrue;", drawable)

        public_renderer = (
            ROOT / "code" / "renderercommon" / "tr_public.h"
        ).read_text(encoding="utf-8")
        self.assertIn("DrawWebUISurface", public_renderer)

        for renderer in ("renderer", "renderer2", "renderervk"):
            with self.subTest(renderer=renderer):
                backend = (
                    ROOT / "code" / renderer / "tr_backend.c"
                ).read_text(encoding="utf-8")
                init = (ROOT / "code" / renderer / "tr_init.c").read_text(
                    encoding="utf-8"
                )
                self.assertIn("void RE_DrawWebUISurface", backend)
                self.assertIn(
                    "re.DrawWebUISurface = RE_DrawWebUISurface;", init
                )

    def test_retail_datapak_starts_before_renderer_dimensions_exist(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("#define CL_WEB_BOOTSTRAP_WIDTH 1280", source)
        self.assertIn("#define CL_WEB_BOOTSTRAP_HEIGHT 720", source)
        self.assertIn("static char *CL_WebHost_AllocateStartupBridgeScript", source)
        self.assertIn(
            "const size_t required = static_cast<size_t>( CL_WEB_STARTUP_SCRIPT_LENGTH ) +",
            source,
        )
        self.assertIn(
            "required > static_cast<size_t>( ( std::numeric_limits<int>::max )() )",
            source,
        )
        self.assertIn("Z_Malloc( static_cast<int>( required ) )", source)
        self.assertIn(
            "CL_WebHost_BuildStartupBridgeScript( script, required, factoryJson );",
            source,
        )
        self.assertIn(
            "? cls.glconfig.vidWidth : CL_WEB_BOOTSTRAP_WIDTH", source
        )
        self.assertIn(
            "? cls.glconfig.vidHeight : CL_WEB_BOOTSTRAP_HEIGHT", source
        )
        bootstrap = source[
            source.index("void CL_WebHost_BootstrapAwesomiumMenu") :
            source.index("qboolean CL_WebHost_HasLiveView")
        ]
        self.assertIn("CL_WebHost_OpenRequestedURL", bootstrap)
        self.assertNotIn("cls.glconfig.vidWidth <= 0", bootstrap)

    def test_legacy_stop_refresh_cannot_abort_the_live_document(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(
            encoding="utf-8"
        )
        handler = source[
            source.index("static void CL_Web_StopRefresh_f") :
            source.index("static void CL_Web_DumpSurface_f")
        ]

        self.assertIn("ignored for the live WebUI document", handler)
        self.assertNotIn("CL_Awesomium_Stop", handler)

    def test_retail_adapter_is_built_without_bundling_runtime_files(self) -> None:
        meson = (ROOT / "meson.build").read_text(encoding="utf-8")
        makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
        project = (
            ROOT / "code" / "win32" / "msvc2017" / "fnql.vcxproj"
        ).read_text(encoding="utf-8")

        self.assertIn("awesomium_backend_win32.cpp", meson)
        self.assertIn("awesomium_backend_win32.o", makefile)
        self.assertIn("awesomium_backend_win32.cpp", project)
        for runtime in (
            "awesomium.dll",
            "awesomium_process.exe",
            "avcodec-53.dll",
            "avformat-53.dll",
            "avutil-51.dll",
        ):
            with self.subTest(runtime=runtime):
                self.assertNotIn(runtime, meson)

    def test_runtime_plan_separates_observations_and_blockers(self) -> None:
        plan = (ROOT / "docs" / "fnql" / "WEBUI_BACKEND.md").read_text(
            encoding="utf-8"
        )

        self.assertIn("Verified retail runtime observations", plan)
        self.assertIn("Retail behavior remains authoritative", plan)
        self.assertIn("Runtime validation", plan)
        self.assertIn("arbitrary-size RGBA", plan)


if __name__ == "__main__":
    unittest.main()
