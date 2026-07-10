from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class QLServicesSourceTests(unittest.TestCase):
    def test_all_renderers_share_the_bounded_v47_contract(self) -> None:
        for relative in (
            "code/renderer/tr_bsp.c",
            "code/renderer2/tr_bsp.c",
            "code/renderervk/tr_bsp.c",
        ):
            source = (ROOT / relative).read_text(encoding="utf-8")
            self.assertIn('../qcommon/ql_bsp.h', source)
            self.assertIn("QLBSP_ReadAdvertisementLump", source)
            self.assertIn("QLBSP_AdvertisementLumpShapeValid", source)
            self.assertIn("header->ident != BSP_IDENT", source)
            self.assertIn("QLBSP_ParseAdvertisementModel", source)
            self.assertNotIn("atoi( in->model", source)

    def test_zmq_is_external_default_off_and_failure_latched(self) -> None:
        source = (ROOT / "code/server/sv_zmq.cpp").read_text(encoding="utf-8")
        endpoint = (ROOT / "code/server/zmq_endpoint.hpp").read_text(encoding="utf-8")
        self.assertIn('Cvar_Get( "zmq_rcon_enable", "0", CVAR_INIT )', source)
        self.assertIn('Cvar_Get( "zmq_stats_enable", "0", CVAR_INIT )', source)
        self.assertIn('Cvar_Get( "zmq_library", "", CVAR_INIT | CVAR_PROTECTED | CVAR_PRIVATE )', source)
        self.assertIn("IsAbsoluteLibraryPath", endpoint)
        self.assertIn("set absolute zmq_library path", source)
        self.assertIn("runtime.rconAttempted", source)
        self.assertIn("runtime.statsAttempted", source)
        self.assertIn("refusing non-loopback %s endpoint without a password", source)
        self.assertIn('Cvar_Get( "zmq_allow_insecure_remote", "0", CVAR_INIT )', source)
        self.assertIn("PLAIN authentication is not encrypted", source)
        self.assertIn("could not enforce RCON authentication", source)
        self.assertIn("could not enforce stats authentication", source)
        self.assertIn("struct RconPeer", source)
        self.assertIn("peer->requestDelimiter", source)
        self.assertIn("requestDelimiter &&", source)
        self.assertIn("runtime.api.errorNumber() == EINTR", source)
        self.assertIn("retaining the runtime library safely", source)
        self.assertIn("BuildTcpEndpoint", source)
        self.assertIn("hadExtraFields", source)
        self.assertIn("hadExtraFrames", source)
        self.assertIn("ResetSocketsAfterFramingFailure", source)
        self.assertIn("normalized.push_back( '[' )", endpoint)
        self.assertIn("A single colon is a forbidden embedded port", endpoint)

    def test_opaque_retail_json_objects_are_never_dereferenced(self) -> None:
        source = (ROOT / "code/server/sv_zmq.cpp").read_text(encoding="utf-8")
        self.assertIn("Retail passes an opaque Json::Value-like object", source)
        self.assertIn('(void)report;\n\tPublish( "MATCH_REPORT", nullptr );', source)
        self.assertIn('(void)payload;\n\tPublish( eventName, nullptr );', source)
        self.assertNotIn("static_cast<const char *>( payload )", source)

    def test_platform_services_are_explicit_unavailable_capabilities(self) -> None:
        source = (ROOT / "code/server/sv_platform.cpp").read_text(encoding="utf-8")
        self.assertIn("constexpr unsigned int QL_STEAM_APP_ID = 282440u", source)
        self.assertIn("SV_PLATFORM_CAPABILITY_STEAM_GAME_SERVER", source)
        self.assertIn("compatibility-disabled (no Steam GameServer owner)", source)
        self.assertNotIn("SteamAPI_Init", source)

    def test_retail_connect_response_preserves_retail_and_fnql_shapes(self) -> None:
        source = (ROOT / "code/server/sv_client.cpp").read_text(encoding="utf-8")
        self.assertIn("cl_proto == QL_RETAIL_PROTOCOL_VERSION && !longstr", source)
        self.assertIn('NET_OutOfBandPrint( NS_SERVER, from, "connectResponse" );', source)
        self.assertIn('"connectResponse %d %d", challenge, sv_proto', source)
        self.assertIn('"connectResponse %d", challenge', source)

    def test_collision_loader_validates_ql_advertisement_contract(self) -> None:
        source = (ROOT / "code/qcommon/cm_load.c").read_text(encoding="utf-8")
        self.assertIn("header.ident != BSP_IDENT", source)
        self.assertIn("QLBSP_ReadAdvertisementLump", source)
        self.assertIn("QLBSP_AdvertisementLumpShapeValid", source)


if __name__ == "__main__":
    unittest.main()
