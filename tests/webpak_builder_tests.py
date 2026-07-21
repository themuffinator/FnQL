from __future__ import annotations

import struct
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from scripts import build_webpak


def decode_datapack(path: Path) -> dict[str, bytes]:
    encoded = path.read_bytes()
    version, resource_count, encoding = struct.unpack_from("<IIB", encoded, 0)
    if version != 4 or encoding != 0:
        raise ValueError("unexpected DataPack header")

    entries = [
        struct.unpack_from("<HI", encoded, 9 + index * 6)
        for index in range(resource_count + 1)
    ]
    payloads = {
        resource_id: encoded[offset : entries[index + 1][1]]
        for index, (resource_id, offset) in enumerate(entries[:-1])
    }

    manifest = payloads[0]
    manifest_length, manifest_version = struct.unpack_from("<II", manifest, 0)
    if manifest_length != len(manifest) - 4 or manifest_version != 7:
        raise ValueError("unexpected QL manifest header")

    resources: dict[str, bytes] = {}
    cursor = 8
    pending_path: str | None = None
    while cursor < len(manifest) - 8:
        _tag, linked_resource_id, character_count = struct.unpack_from(
            "<III", manifest, cursor
        )
        cursor += 12
        byte_count = character_count * 2
        virtual_path = manifest[cursor : cursor + byte_count].decode("utf-16le")
        cursor = (cursor + byte_count + 3) & ~3
        if pending_path is not None:
            resources[pending_path] = payloads[linked_resource_id]
        pending_path = virtual_path

    _tag, final_resource_id = struct.unpack_from("<II", manifest, cursor)
    if pending_path is not None:
        resources[pending_path] = payloads[final_resource_id]
    return resources


class WebPakBuilderTests(unittest.TestCase):
    def test_settings_tab_follows_retail_navigation_contract(self) -> None:
        script = (
            ROOT / "code" / "client" / "webui" / "fnql-settings.js"
        ).read_text(encoding="utf-8")
        style = (
            ROOT / "code" / "client" / "webui" / "css" / "fnql-settings.css"
        ).read_text(encoding="utf-8")

        self.assertIn("nav.appendChild(tab);", script)
        self.assertIn("else if (tab.nextSibling)", script)
        self.assertNotIn("nav.insertBefore(tab", script)
        self.assertIn("clearRetailActiveTabs(nav);", script)
        self.assertIn("tab.className = 'button fnql-settings-tab active';", script)
        self.assertIn("retailTab.classList.add('active');", script)
        self.assertIn("deactivate(node);", script)
        self.assertIn("if (currentRoot && currentRoot !== root)", script)
        self.assertIn(
            "window.addEventListener('hashchange', function () {\n      deactivate();",
            script,
        )
        self.assertNotIn("is-active", script)
        self.assertNotIn(".fnql-settings-tab", style)

    def test_project_overlay_is_sparse_and_round_trips(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "fnql-web.pak"
            resource_count, byte_count = build_webpak.build_webpak(
                ROOT / "code" / "client" / "webui", output
            )
            encoded_length = len(output.read_bytes())
            decoded = decode_datapack(output)

        self.assertEqual(resource_count, 3)
        self.assertEqual(byte_count, encoded_length)
        self.assertEqual(
            set(decoded),
            {"index.html", "fnql-settings.js", "css/fnql-settings.css"},
        )
        self.assertIn(b'<script src="bundle.js"></script>', decoded["index.html"])
        self.assertIn(b"patchLegacyVideoSettings", decoded["fnql-settings.js"])
        self.assertIn(b"settingsSignature", decoded["fnql-settings.js"])
        self.assertIn(
            b"panel.__fnqlSettingsSignature !== settingsSignature(cvarCache())",
            decoded["fnql-settings.js"],
        )
        self.assertIn(b"window.addEventListener('hashchange'", decoded["fnql-settings.js"])
        self.assertIn(b"window.setInterval(attach, 1000)", decoded["fnql-settings.js"])
        self.assertIn(b"nav.appendChild(tab);", decoded["fnql-settings.js"])
        self.assertIn(
            b"tab.className = 'button fnql-settings-tab active';",
            decoded["fnql-settings.js"],
        )
        self.assertNotIn(b"is-active", decoded["fnql-settings.js"])
        self.assertNotIn(b".fnql-settings-tab", decoded["css/fnql-settings.css"])
        source_paths = {
            resource.virtual_path
            for resource in build_webpak.collect_resources(
                ROOT / "code" / "client" / "webui"
            )
        }
        self.assertNotIn("bundle.js", source_paths)

    def test_output_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            first = Path(tmp) / "first.pak"
            second = Path(tmp) / "second.pak"
            source_root = ROOT / "code" / "client" / "webui"
            build_webpak.build_webpak(source_root, first)
            build_webpak.build_webpak(source_root, second)
            self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_retail_bundle_and_unsafe_names_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            source_root = Path(tmp)
            (source_root / "bundle.js").write_text("retail", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "retail web.pak content"):
                build_webpak.collect_resources(source_root)

        with tempfile.TemporaryDirectory() as tmp:
            source_root = Path(tmp)
            (source_root / "Upper.js").write_text("unsafe", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "lowercase"):
                build_webpak.collect_resources(source_root)


if __name__ == "__main__":
    unittest.main()
