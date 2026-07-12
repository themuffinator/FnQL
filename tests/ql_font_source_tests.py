from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class QLFontSourceTests(unittest.TestCase):
    def test_retail_faces_and_fallback_order_are_explicit(self) -> None:
        source = read("code/renderercommon/tr_font.c")
        for path in (
            "fonts/handelgothic.ttf",
            "fonts/notosans-regular.ttf",
            "fonts/droidsansmono.ttf",
            "fonts/droidsansfallbackfull.ttf",
        ):
            self.assertIn(path, source)
        self.assertIn("requestedFace = QL_ResolveHostFaceIndex( requestedFace );", source)
        self.assertIn("QL_AppendHostFaceIndex( chain, &count, requestedFace );", source)
        self.assertIn("QL_AppendHostFaceIndex( chain, &count, 1 );", source)
        self.assertIn("QL_AppendHostFaceIndex( chain, &count, 3 );", source)
        self.assertIn("qlHostFonts.faces[4].face ? 4 : 3", source)

    def test_host_text_service_is_exported_by_every_renderer(self) -> None:
        public = read("code/renderercommon/tr_public.h")
        self.assertIn("REF_API_VERSION\t\t11", public)
        self.assertIn("(*DrawScaledText)", public)
        self.assertIn("(*MeasureScaledText)", public)
        self.assertIn("(*GetScaledFontMetrics)", public)
        for path in (
            "code/renderer/tr_init.c",
            "code/renderer2/tr_init.c",
            "code/renderervk/tr_init.c",
        ):
            source = read(path)
            self.assertIn("re.DrawScaledText = RE_DrawScaledText;", source)
            self.assertIn("re.MeasureScaledText = RE_MeasureScaledText;", source)
            self.assertIn("re.GetScaledFontMetrics = RE_GetScaledFontMetrics;", source)

    def test_client_preserves_limit_and_in_out_extent_semantics(self) -> None:
        screen = read("code/client/cl_scrn.cpp")
        cgame = read("code/client/cl_cgame.cpp")
        ui = read("code/client/cl_ui.cpp")
        self.assertIn("scale, int limit, float *outMaxX", screen)
        self.assertIn("scale, limit, maxX", cgame)
        self.assertIn("scale, limit, maxX", ui)
        self.assertIn("re.GetScaledFontMetrics( fontHandle, scale", screen)

    def test_native_measure_imports_write_the_retail_five_float_bounds_packet(self) -> None:
        bridge = read("code/client/ql_font_bridge.hpp")
        self.assertIn("bounds[ 0 ] = left", bridge)
        self.assertIn("bounds[ 2 ] = left + width", bridge)
        self.assertIn("bounds[ 4 ] = ascent", bridge)
        for path in ("code/client/cl_ui.cpp", "code/client/cl_cgame.cpp"):
            source = read(path)
            self.assertIn("fnql::font::WriteMeasureBounds( outLeft, left, width, height )", source)
            self.assertNotIn("&width, &height, outLeft", source)

    def test_retained_atlas_and_system_fallback_are_bounded(self) -> None:
        source = read("code/renderercommon/tr_font.c")
        self.assertIn("#define QL_HOST_ATLAS_INITIAL_WIDTH 512", source)
        self.assertIn("#define QL_HOST_ATLAS_INITIAL_HEIGHT 512", source)
        self.assertIn("#define QL_HOST_ATLAS_MAX_WIDTH 2048", source)
        self.assertIn("#define QL_HOST_ATLAS_MAX_HEIGHT 1024", source)
        self.assertIn("QL_CreateOrResizeHostAtlas", source)
        self.assertIn("QL_ResetHostAtlas", source)
        self.assertIn("QL_RefreshHostGlyphAtlasBindings", source)
        self.assertIn("QL_FlushHostAtlasCommands", source)
        self.assertIn('Q_strncpyz( name, "*fontstash"', source)
        self.assertIn("length > 32 * 1024 * 1024", source)
        self.assertIn("#if defined( _WIN32 )", source)
        self.assertIn("QL_LoadWindowsFallbackFace", source)

    def test_retail_font_atlas_debug_view_is_cross_backend(self) -> None:
        public = read("code/renderercommon/tr_public.h")
        screen = read("code/client/cl_scrn.cpp")
        self.assertIn("GetFontAtlasDebugShader", public)
        self.assertIn('Cvar_Get( "r_debugFontAtlas", "0", CVAR_TEMP )', screen)
        self.assertIn("SCR_DrawFontAtlasDebug", screen)
        for path in (
            "code/renderer/tr_init.c",
            "code/renderer2/tr_init.c",
            "code/renderervk/tr_init.c",
        ):
            self.assertIn("re.GetFontAtlasDebugShader = RE_GetFontAtlasDebugShader;", read(path))

    def test_console_prefers_the_retail_mono_ttf_with_a_safe_fallback(self) -> None:
        source = read("code/client/cl_console.cpp")
        self.assertIn("CONSOLE_HOST_FONT_MONO = 2", source)
        self.assertIn("RETAIL_CONSOLE_CHAR_WIDTH = 12.0f", source)
        self.assertIn("RETAIL_CONSOLE_CHAR_HEIGHT = 24.0f", source)
        self.assertIn("RETAIL_CONSOLE_REFERENCE_HEIGHT = 768.0f", source)
        self.assertIn("RETAIL_CONSOLE_SCALE_UNIT = 0.5f", source)
        self.assertIn('Cvar_Get( "con_scale", "1"', source)
        self.assertIn("Con_UpdateTtfFontAvailability", source)
        self.assertIn("Con_DrawConsoleLineText", source)
        self.assertIn("Con_MeasureHostText", source)
        self.assertIn("static int console_char_width", source)
        self.assertIn("smallchar_width = RoundToInt( SMALLCHAR_WIDTH * legacyCharScale )", source)
        self.assertIn("legacyCharScale = cls.con_factor", source)
        self.assertNotIn("legacyCharScale = scale * cls.con_factor", source)
        self.assertIn("re.DrawScaledText", source)
        self.assertIn("ch >= 32 || ch == 10 || ch == 11", source)
        self.assertIn("cls.charSetShader", source)
        self.assertNotIn("SCR_DrawSmallChar( cl_conXOffset", source)
        self.assertNotIn("SCR_DrawSmallString( xf + wf", source)
        self.assertNotIn("g_console_field_width = ((cls.glconfig.vidWidth / smallchar_width))", read("code/client/cl_main.cpp"))

    def test_client_large_text_and_chat_use_the_retail_host_font_lane(self) -> None:
        screen = read("code/client/cl_scrn.cpp")
        console = read("code/client/cl_console.cpp")
        keys = read("code/client/cl_keys.cpp")
        self.assertIn("constexpr int hostFontMono = 2", screen)
        self.assertIn("hostScale = size * yscale", screen)
        self.assertIn("RE_DrawScaledText( screenX, screenY, string, hostFontMono", screen)
        self.assertIn("QL_FontDecodeUtf8", screen)
        self.assertIn("QL_FontColorEscape", screen)
        self.assertIn('chat_team ? "say team:" : "say:"', console)
        self.assertIn("con_chatBackgroundColor", console)
        self.assertIn("Con_DrawInputText( &chatField", console)
        self.assertNotIn("Field_BigDraw( &chatField", console)
        self.assertIn("key_overstrikeMode ? '_' : '|'", keys)
        self.assertIn("420, 8, string.data()", screen)
        self.assertIn('SCR_DrawStringExt( 9, 477, 8, "REC"', screen)
        self.assertIn('re.RegisterShaderNoMip( "icons/record" )', read("code/client/cl_main.cpp"))

    def test_explicit_small_client_text_retains_the_bitmap_lane(self) -> None:
        screen = read("code/client/cl_scrn.cpp")
        self.assertIn("void SCR_DrawSmallStringExt", screen)
        self.assertIn("SCR_DrawSmallChar", screen)

    def test_client_edit_fields_do_not_split_utf8_codepoints(self) -> None:
        keys = read("code/client/cl_keys.cpp")
        self.assertIn("Field_IsUtf8ContinuationByte", keys)
        self.assertIn("Field_DeletePreviousCharacter", keys)
        self.assertIn("Field_DeleteCurrentCharacter", keys)
        self.assertIn("Field_AdvanceCursor", keys)
        self.assertIn("Field_RetreatCursor", keys)
        self.assertIn("case K_KP_DEL:", keys)
        self.assertIn("case K_KP_RIGHTARROW:", keys)
        self.assertIn("case K_KP_LEFTARROW:", keys)

        bridge = read("code/client/ql_font_bridge.hpp")
        console = read("code/client/cl_console.cpp")
        self.assertIn("GetUtf8FieldWindow", bridge)
        self.assertIn("PreviousUtf8Boundary", bridge)
        self.assertIn("NextUtf8Boundary", bridge)
        self.assertIn("Con_MeasureInputPrefix", console)
        self.assertIn("fnql::font::PreviousUtf8Boundary", console)
        self.assertIn("fnql::font::NextUtf8Boundary", console)
        self.assertNotIn("str[ 0 ] = '<'", console)

    def test_host_sizes_use_retail_ascender_descender_semantics(self) -> None:
        source = read("code/renderercommon/tr_font.c")
        helper = read("code/renderercommon/ql_font_text.h")
        self.assertIn("QL_FontFaceCharSize26Dot6", source)
        self.assertIn("QL_FontFaceMetric", source)
        self.assertIn("face->ascender - face->descender", source)
        self.assertIn("slot->metrics.horiAdvance", source)
        self.assertIn("Host text sizes describe the ascender-to-descender span", helper)

    def test_classic_font_registration_keeps_face_identity_and_safe_fallbacks(self) -> None:
        source = read("code/renderercommon/tr_font.c")
        self.assertIn("#define MAX_FONTS 16", source)
        self.assertIn("R_BuildClassicFontCacheName", source)
        self.assertIn('fonts/fontImage_%s_%i.dat', source)
        self.assertIn("R_BuildClassicFontPageName", source)
        self.assertIn("R_BindClassicFontCacheShaders", source)
        self.assertIn("i = GLYPH_START; i <= GLYPH_END", source)
        self.assertIn("R_RegisterClassicFontFallback", source)
        for alias in (
            '"fonts/font"',
            '"fonts/bigfont"',
            '"fonts/smallfont"',
            '"fonts/monofont"',
            '"normal"',
            '"sans"',
            '"mono"',
            '"sans-fallback"',
            '"sans-windows-fallback"',
        ):
            self.assertIn(alias, source)


if __name__ == "__main__":
    unittest.main()
