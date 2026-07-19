from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class WebPakSourceTests(unittest.TestCase):
    def test_webpak_bridge_supports_external_datapak_without_vendoring(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webpak.cpp").read_text(encoding="utf-8")
        format_header = (ROOT / "code" / "client" / "webpak_format.h").read_text(encoding="utf-8")

        self.assertIn("CL_WebDataPak_LoadFile", source)
        self.assertIn("InspectDataPack", source)
        self.assertIn("parsed.version == 4u", format_header)
        self.assertIn("parsed.version == 5u", format_header)
        self.assertIn('"web.pak"', source)
        self.assertIn('"fs_homepath", "fs_basepath", "fs_steampath"', source)
        self.assertIn("retail-assets-external", source)
        self.assertNotIn("FS_LoadPackExplicit", source)
        self.assertNotIn("awesomium.dll", source)

    def test_datapak_layout_is_bounded_before_engine_table_access(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webpak.cpp").read_text(encoding="utf-8")
        format_header = (ROOT / "code" / "client" / "webpak_format.h").read_text(encoding="utf-8")
        tests = (ROOT / "tests" / "webpak_format_tests.cpp").read_text(encoding="utf-8")
        meson = (ROOT / "meson.build").read_text(encoding="utf-8")

        self.assertIn("InspectDataPack( dataPak.buffer", source)
        self.assertIn("DataPackCheckedMultiply", format_header)
        self.assertIn("offset < parsed.payloadOffset", format_header)
        self.assertIn("UnsortedResourceIds", format_header)
        self.assertIn("InvalidAliasTarget", format_header)
        self.assertIn("AcceptsRetailV4Shape", tests)
        self.assertIn("AcceptsV5AliasShape", tests)
        self.assertIn("RejectsTruncatedHeadersAndTables", tests)
        self.assertIn("RejectsUnsafeAliasTables", tests)
        self.assertIn("fnql_webpak_format", meson)

    def test_datapak_manifest_index_is_validated_and_binary_searchable(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webpak.cpp").read_text(encoding="utf-8")

        self.assertIn("nextResourceId > UINT16_MAX", source)
        self.assertIn("CL_WebDataPak_FindEntryIndex( dataPak, (uint16_t)nextResourceId )", source)
        self.assertIn("std::sort( dataPak->paths", source)
        self.assertIn("std::lower_bound( dataPak->paths", source)
        self.assertIn("cl_webPakVersion", source)
        self.assertIn("cl_webPakResourceCount", source)

    def test_sparse_fnql_overlay_precedes_external_retail_pack(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webpak.cpp").read_text(encoding="utf-8")

        self.assertIn('"fnql-web.pak"', source)
        self.assertIn("cl_fnqlWebDataPak", source)
        self.assertIn('CL_WebPak_BuildStandalonePath( Sys_Pwd(), "fnql-web.pak"', source)
        overlay_fetch = source.index("CL_WebDataPak_Fetch( &cl_fnqlWebDataPak")
        retail_fetch = source.index("CL_WebDataPak_Fetch( &cl_webDataPak", overlay_fetch)
        loose_fetch = source.index("CL_WebPak_ReadLooseFallback", retail_fetch)
        self.assertLess(overlay_fetch, retail_fetch)
        self.assertLess(retail_fetch, loose_fetch)
        self.assertIn("fnql-overlay-retail-fallback", source)
        self.assertIn("cl_fnqlWebPakLoaded", source)

    def test_launcher_requests_strip_protocol_and_reject_unsafe_paths(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webpak.cpp").read_text(encoding="utf-8")

        self.assertIn('trustedPrefix[] = "asset://ql/"', source)
        self.assertIn("CL_WEB_PAK_MAX_BYTES", source)
        self.assertIn("CL_WEB_RESOURCE_MAX_BYTES", source)
        self.assertIn("static qboolean CL_WebPak_IsSteamDataSourceRequest", source)
        self.assertIn('!Q_stricmpn( virtualPath, "steam://", 8 )', source)
        self.assertIn("CL_WebPak_IsSteamDataSourceRequest( virtualPath )", source)
        self.assertIn("normalizedSource[readIndex] != '?'", source)
        self.assertIn('normalized[0] == \'/\'', source)
        self.assertIn('strstr( normalized, ".." )', source)
        self.assertIn("CL_LauncherRequestData", source)
        self.assertIn("CL_WebRequestResolve", source)
        self.assertIn("FS_FOpenFileRead", source)

    def test_resource_bridge_exposes_steam_datasource_diagnostics(self) -> None:
        source = (ROOT / "code" / "client" / "cl_webpak.cpp").read_text(encoding="utf-8")
        webui = (ROOT / "code" / "client" / "cl_webui.cpp").read_text(encoding="utf-8")

        self.assertIn("ui_resourceBridgeSteamDataSourceSubset", source)
        self.assertIn("avatar-only SteamDataSource", source)
        self.assertIn("ui_resourceBridgeSteamDataSourceNativeGap", source)
        self.assertIn("missing non-avatar SteamDataSource owner", source)
        self.assertIn("ui_resourceBridgeSteamDataSourceFallbackOwner", source)
        self.assertIn("QLResourceInterceptor launcher/web fallback", source)
        self.assertIn('"ui_resourceBridgeProvider"', webui)
        self.assertIn('"ui_resourceBridgeSteamDataSourceSubset"', webui)
        self.assertIn('"ui_resourceBridgeSteamDataSourceNativeGap"', webui)
        self.assertIn('"ui_resourceBridgeSteamDataSourceFallbackOwner"', webui)

    def test_client_lifecycle_initializes_and_releases_webpak(self) -> None:
        client = (ROOT / "code" / "client" / "cl_main.cpp").read_text(encoding="utf-8")

        self.assertIn("CL_WebPak_Init();\n\tCL_WebHost_BootstrapAwesomiumMenu();", client)
        self.assertIn("CL_WebHost_Shutdown();\n\tCL_WebPak_Shutdown();", client)

    def test_build_manifests_include_webpak_client_source(self) -> None:
        expected = "code/client/cl_webpak.cpp"

        self.assertIn(expected, (ROOT / "meson.build").read_text(encoding="utf-8"))
        self.assertIn("$(B)/client/cl_webpak.o", (ROOT / "Makefile").read_text(encoding="utf-8"))
        self.assertIn(r"..\..\client\cl_webpak.cpp", (ROOT / "code" / "win32" / "msvc2017" / "fnql.vcxproj").read_text(encoding="utf-8"))
        self.assertIn(r"..\..\client\cl_webpak.cpp", (ROOT / "code" / "win32" / "msvc2005" / "fnql.vcproj").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
