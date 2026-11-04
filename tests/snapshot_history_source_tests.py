"""Integration gates for the native-tested snapshot history contract."""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class SnapshotHistorySourceTests(unittest.TestCase):
    def test_native_and_vm_imports_share_history_validation(self):
        source = (ROOT / "code/client/cl_cgame.cpp").read_text(encoding="utf-8")
        for name in ("CL_GetSnapshot", "CL_GetRetailNativeSnapshot"):
            body = source.split(f"static qboolean {name}(", 1)[1].split("\n}", 1)[0]
            self.assertIn("CL_FindSnapshot( snapshotNumber )", body)
        lookup = source.split("static const clSnapshot_t *CL_FindSnapshot(", 1)[1].split("\n}", 1)[0]
        self.assertIn("fnql::snapshot::Lookup(", lookup)
        self.assertIn("LookupStatus::Future", lookup)
        self.assertIn("Com_Error( ERR_DROP", lookup)

    def test_entity_order_is_checked_before_any_merge_writes(self):
        source = (ROOT / "code/client/cl_parse.cpp").read_text(encoding="utf-8")
        body = source.split("static void CL_ParsePacketEntities(", 1)[1].split("\n}", 1)[0]
        guard = body.index("fnql::snapshot::EntityFollows(")
        self.assertLess(body.index("if ( newnum == (MAX_GENTITIES-1) )"), guard)
        self.assertLess(guard, body.index("previousNumber = newnum;"))
        self.assertLess(guard, body.index("CL_DeltaEntity("))
        self.assertIn("newSnap.areabytes < 0 ||", source)

    def test_all_entity_ring_consumers_avoid_signed_cursor_arithmetic(self):
        for name in ("cl_parse.cpp", "cl_cgame.cpp", "cl_main.cpp"):
            source = (ROOT / "code/client" / name).read_text(encoding="utf-8")
            self.assertIsNone(re.search(r"parseEntitiesNum\s*(?:\+|-|%)", source), name)
            self.assertIn("fnql::snapshot::EntityIndex(", source)
        parser = (ROOT / "code/client/cl_parse.cpp").read_text(encoding="utf-8")
        self.assertIn("fnql::net::NextCounter( cl.parseEntitiesNum )", parser)


if __name__ == "__main__":
    unittest.main()
