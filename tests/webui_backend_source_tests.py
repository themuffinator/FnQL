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

    def test_legacy_facade_delegates_without_loading_awesomium(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn('#include "webui_backend.hpp"', source)
        self.assertIn("fnql::webui::ClientBackendHost().Start", source)
        self.assertIn("host.Navigate", source)
        self.assertIn("host.CopySurface", source)
        self.assertIn("CL_WebUI_BackendRequestResource", source)
        self.assertIn("CL_WebUI_BackendReleaseResource", source)
        self.assertIn('Cvar_Get( "cl_webuiEnable", "0", CVAR_ARCHIVE_ND )', source)
        self.assertNotIn("LoadLibraryA(", source)
        self.assertNotIn("GetProcAddress(", source)
        self.assertNotIn("_Awe_", source)

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

    def test_native_ui_ownership_waits_for_a_real_presenter(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(
            encoding="utf-8"
        )
        drawable = source[
            source.index("qboolean CL_WebHost_HasDrawableSurface") :
            source.index("static qboolean CL_WebHost_CanDispatchLiveEvent")
        ]

        self.assertIn("host.Status().HasSurface()", drawable)
        self.assertIn("Do not claim overlay", drawable)
        self.assertIn("return qfalse;", drawable)

    def test_runtime_plan_separates_observations_and_blockers(self) -> None:
        plan = (ROOT / "docs" / "fnql" / "WEBUI_BACKEND.md").read_text(
            encoding="utf-8"
        )

        self.assertIn("source observations, not claims from a live retail probe", plan)
        self.assertIn("Retail behavior remains authoritative", plan)
        self.assertIn("Remaining runtime work", plan)
        self.assertIn("renderer-neutral dynamic RGBA", plan)


if __name__ == "__main__":
    unittest.main()
