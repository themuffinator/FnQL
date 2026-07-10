from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_repo_file(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class QuakeLiveDemoSourceTests(unittest.TestCase):
    def test_retail_protocol_is_in_the_demo_registry(self) -> None:
        common = read_repo_file("code/qcommon/common.c")
        qcommon = read_repo_file("code/qcommon/qcommon.h")

        self.assertIn("QL_RETAIL_PROTOCOL_VERSION,\n\t0", common)
        self.assertIn("qboolean Com_DemoProtocolSupported( int protocol )", common)
        self.assertIn(
            "#define\tQL_RETAIL_PROTOCOL_VERSION\tNETCHAN_QL_RETAIL_PROTOCOL_VERSION",
            qcommon,
        )

    def test_demo_paths_use_the_shared_registry(self) -> None:
        client = read_repo_file("code/client/cl_main.cpp")

        self.assertIn("static bool CL_ParseDemoProtocolExtension", client)
        self.assertIn("static bool CL_DemoProtocolSupported", client)
        self.assertIn("static bool CL_TryDemoProtocol", client)
        self.assertIn(
            "com_protocol->integer == QL_RETAIL_PROTOCOL_VERSION", client
        )
        self.assertIn("CL_DemoProtocolSupported( protocol )", client)
        self.assertNotIn("version > NEW_PROTOCOL_VERSION", client)

    def test_demo_playback_uses_the_bounded_envelope_reader(self) -> None:
        client = read_repo_file("code/client/cl_main.cpp")
        stream = read_repo_file("code/client/demo_stream.hpp")

        self.assertIn('#include "demo_stream.hpp"', client)
        self.assertGreaterEqual(client.count("fnql::demo::ReadEnvelope("), 2)
        self.assertIn("invalid demo message length %d", client)
        for status in (
            "Message",
            "EndOfStreamTrailer",
            "TruncatedHeader",
            "TruncatedPayload",
            "NegativeLength",
            "OversizeLength",
        ):
            self.assertIn(status, stream)

    def test_retail_client_sideband_is_connection_profile_gated(self) -> None:
        cl_input = read_repo_file("code/client/cl_input.cpp")
        sv_client = read_repo_file("code/server/sv_client.cpp")

        self.assertIn(
            "clc.netchan.wireProfile == NETCHAN_WIRE_QL_RETAIL", cl_input
        )
        self.assertIn(
            "cl->netchan.wireProfile == NETCHAN_WIRE_QL_RETAIL", sv_client
        )

    def test_compat_suffix_does_not_downgrade_protocol_91(self) -> None:
        cl_main = read_repo_file("code/client/cl_main.cpp")
        sv_client = read_repo_file("code/server/sv_client.cpp")

        self.assertIn(
            "com_protocolCompat &&\n\t\t\t\tnegotiatedProtocol != QL_RETAIL_PROTOCOL_VERSION",
            cl_main,
        )
        self.assertIn(
            "com_protocolCompat && cl_proto != QL_RETAIL_PROTOCOL_VERSION",
            sv_client,
        )


if __name__ == "__main__":
    unittest.main()
