from __future__ import annotations

import json
import os
import re
import sys
import unittest
import zipfile
from collections import Counter
from pathlib import Path
from typing import Any


RETAIL_PATH_ENV = "FNQL_RETAIL_QL_PATH"
FACTORY_MEMBER = "scripts/factories.txt"
FACTORY_SIZE_LIMIT = 0x8000

# Observed-current-build facts from the legitimate retail Steam installation.
# This probe intentionally stores only the compatibility contract, never assets.
OBSERVED_STOCK_FACTORIES = (
    ("_training", "ffa"),
    ("_rj", "ffa"),
    ("_sj", "ffa"),
    ("actf", "ctf"),
    ("ad", "ad"),
    ("ca", "ca"),
    ("ctf", "ctf"),
    ("dom", "dom"),
    ("duel", "duel"),
    ("oneflag", "oneflag"),
    ("ffa", "ffa"),
    ("ft", "ft"),
    ("har", "har"),
    ("ictf", "ctf"),
    ("iffa", "ffa"),
    ("ift", "ft"),
    ("infected", "rr"),
    ("quadhog", "ffa"),
    ("race", "race"),
    ("rr", "rr"),
    ("tdm", "tdm"),
    ("vca", "ca"),
)
OBSERVED_HIDDEN_FACTORY_IDS = ("_training", "_rj", "_sj")


class RetailProbeConfigurationError(ValueError):
    pass


def locate_retail_root() -> Path | None:
    configured = os.environ.get(RETAIL_PATH_ENV, "").strip()
    if configured:
        root = Path(configured).expanduser()
        if not root.is_dir():
            raise RetailProbeConfigurationError(
                f"{RETAIL_PATH_ENV} points to {root!s}, but that directory does not "
                "exist; set it to the Quake Live installation root containing baseq3"
            )
        return root

    if os.name == "nt":
        program_files_x86 = Path(
            os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
        )
        standard_root = (
            program_files_x86
            / "Steam"
            / "steamapps"
            / "common"
            / "Quake Live"
        )
        if standard_root.is_dir():
            return standard_root

    return None


def reject_non_json_constant(value: str) -> None:
    raise ValueError(f"non-JSON numeric constant {value!r}")


def reject_duplicate_object_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON object key {key!r}")
        result[key] = value
    return result


def load_factory_member(pak_path: Path) -> bytes:
    try:
        # ZipFile.read returns the one member in memory; nothing is extracted or copied.
        with zipfile.ZipFile(pak_path, mode="r") as archive:
            try:
                return archive.read(FACTORY_MEMBER)
            except KeyError as exc:
                raise AssertionError(
                    f"{pak_path} does not contain {FACTORY_MEMBER}; verify the retail "
                    "installation and Steam build before changing this contract"
                ) from exc
    except zipfile.BadZipFile as exc:
        raise AssertionError(
            f"{pak_path} is not a readable PK3/ZIP archive: {exc}"
        ) from exc
    except OSError as exc:
        raise AssertionError(
            f"Could not read retail archive {pak_path}: {exc}. Check file permissions "
            "or verify the installation through Steam."
        ) from exc


def parse_factory_json(raw: bytes, source: Path) -> Any:
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise AssertionError(
            f"{source}:{FACTORY_MEMBER} is not strict UTF-8 at byte {exc.start}"
        ) from exc

    try:
        return json.loads(
            text,
            parse_constant=reject_non_json_constant,
            object_pairs_hook=reject_duplicate_object_keys,
        )
    except (json.JSONDecodeError, ValueError) as exc:
        raise AssertionError(
            f"{source}:{FACTORY_MEMBER} is not strict JSON: {exc}"
        ) from exc


class RetailFactoryProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        try:
            root = locate_retail_root()
        except RetailProbeConfigurationError as exc:
            raise AssertionError(str(exc)) from exc

        if root is None:
            raise unittest.SkipTest(
                f"retail Quake Live not found; set {RETAIL_PATH_ENV} to enable the "
                "read-only factory compatibility probe"
            )

        cls.retail_root = root
        cls.pak_path = root / "baseq3" / "pak00.pk3"
        cls.server_cfg_path = root / "baseq3" / "server.cfg"
        if not cls.pak_path.is_file():
            raise AssertionError(
                f"retail root {root} is missing baseq3/pak00.pk3; verify "
                f"{RETAIL_PATH_ENV} and the Steam installation"
            )

        cls.factory_raw = load_factory_member(cls.pak_path)
        cls.factories = parse_factory_json(cls.factory_raw, cls.pak_path)

    def test_factory_file_observed_size_and_strict_array_root(self) -> None:
        self.assertLess(
            len(self.factory_raw),
            FACTORY_SIZE_LIMIT,
            "Observed-current retail scripts/factories.txt exceeds the recovered "
            f"loader bound: got {len(self.factory_raw):#x}, expected < "
            f"{FACTORY_SIZE_LIMIT:#x}. Verify the Steam build before updating FnQL.",
        )
        self.assertIsInstance(
            self.factories,
            list,
            "Observed-current retail scripts/factories.txt must have an array root; "
            f"got {type(self.factories).__name__}",
        )

    def test_factory_objects_have_the_observed_stock_schema(self) -> None:
        self.assertIsInstance(
            self.factories,
            list,
            "Cannot validate factory objects because the JSON root is not an array",
        )

        ids: list[str] = []
        for index, definition in enumerate(self.factories):
            self.assertIsInstance(
                definition,
                dict,
                f"Factory array element {index} must be an object, got "
                f"{type(definition).__name__}",
            )

            missing = sorted(
                {"id", "title", "basegt", "cvars", "author", "description"}
                - definition.keys()
            )
            self.assertFalse(
                missing,
                f"Factory element {index} is missing required stock fields: {missing}",
            )

            factory_id = definition["id"]
            for field in ("id", "title", "basegt", "author", "description"):
                self.assertIsInstance(
                    definition[field],
                    str,
                    f"Factory {factory_id!r} field {field!r} must be a string, got "
                    f"{type(definition[field]).__name__}",
                )
            for field in ("id", "title", "basegt"):
                self.assertTrue(
                    definition[field],
                    f"Factory element {index} has an empty required field {field!r}",
                )

            cvars = definition["cvars"]
            self.assertIsInstance(
                cvars,
                dict,
                f"Factory {factory_id!r} field 'cvars' must be an object, got "
                f"{type(cvars).__name__}",
            )
            for cvar_name, cvar_value in cvars.items():
                self.assertIsInstance(
                    cvar_name,
                    str,
                    f"Factory {factory_id!r} has a non-string cvar key",
                )
                self.assertIsInstance(
                    cvar_value,
                    str,
                    f"Observed stock factory {factory_id!r} cvar {cvar_name!r} must "
                    f"remain a JSON string, got {type(cvar_value).__name__}",
                )
            ids.append(factory_id)

        duplicate_ids = sorted(
            factory_id
            for factory_id, count in Counter(ids).items()
            if count > 1
        )
        self.assertFalse(
            duplicate_ids,
            f"Observed stock factory IDs must be unique; duplicates: {duplicate_ids}",
        )

    def test_observed_current_stock_ids_order_and_basegts(self) -> None:
        self.assertIsInstance(
            self.factories,
            list,
            "Cannot compare the observed stock catalog because the root is not an array",
        )

        actual: list[tuple[str, str]] = []
        for index, definition in enumerate(self.factories):
            if not isinstance(definition, dict):
                self.fail(
                    f"Cannot compare the stock catalog: element {index} is not an object"
                )
            factory_id = definition.get("id")
            basegt = definition.get("basegt")
            if not isinstance(factory_id, str) or not isinstance(basegt, str):
                self.fail(
                    f"Cannot compare the stock catalog: element {index} has invalid "
                    "id/basegt types"
                )
            actual.append((factory_id, basegt))

        expected = list(OBSERVED_STOCK_FACTORIES)
        expected_map = dict(expected)
        actual_map = dict(actual)
        missing = sorted(expected_map.keys() - actual_map.keys())
        unexpected = sorted(actual_map.keys() - expected_map.keys())
        changed = {
            factory_id: (expected_map[factory_id], actual_map[factory_id])
            for factory_id in sorted(expected_map.keys() & actual_map.keys())
            if expected_map[factory_id] != actual_map[factory_id]
        }
        order_matches = actual == expected
        self.assertEqual(
            actual,
            expected,
            "Observed-current retail factory catalog changed: "
            f"count={len(actual)} (expected 22), missing={missing}, "
            f"unexpected={unexpected}, basegt_changes={changed}, "
            f"order_matches={order_matches}. Confirm the Steam build and retail "
            "behavior before updating this regression contract.",
        )

    def test_observed_hidden_factory_ids(self) -> None:
        self.assertIsInstance(
            self.factories,
            list,
            "Cannot inspect hidden factories because the root is not an array",
        )
        actual_hidden = [
            definition.get("id")
            for definition in self.factories
            if isinstance(definition, dict)
            and isinstance(definition.get("id"), str)
            and definition["id"].startswith("_")
        ]
        self.assertEqual(
            actual_hidden,
            list(OBSERVED_HIDDEN_FACTORY_IDS),
            "Observed-current hidden factory IDs changed. These remain loadable but "
            "are omitted from retail's public valid-factory list; verify retail "
            "behavior before changing FnQL filtering.",
        )

    def test_loose_server_cfg_documents_factory_host_wiring(self) -> None:
        self.assertTrue(
            self.server_cfg_path.is_file(),
            f"Retail install is missing loose {self.server_cfg_path}; use Steam's "
            "file verification before updating this contract",
        )
        try:
            server_cfg = self.server_cfg_path.read_text(encoding="utf-8-sig")
        except OSError as exc:
            self.fail(
                f"Could not read retail {self.server_cfg_path}: {exc}. Check file "
                "permissions or verify the installation through Steam."
            )
        except UnicodeDecodeError as exc:
            self.fail(
                f"Retail {self.server_cfg_path} is not UTF-8-compatible at byte "
                f"{exc.start}"
            )

        lowered = server_cfg.lower()
        self.assertIn(
            ".factories",
            lowered,
            "Observed-current retail server.cfg no longer documents plural "
            "scripts/*.factories supplements; verify the Steam build.",
        )
        self.assertRegex(
            lowered,
            r"\bserverstartup\b",
            "Observed-current retail server.cfg no longer documents serverstartup",
        )
        self.assertIn(
            "factory is required",
            lowered,
            "Observed-current retail server.cfg no longer states that an explicit "
            "factory is required for a chosen map",
        )
        self.assertRegex(
            server_cfg,
            re.compile(
                r'^\s*(?:(?://|#)\s*)?set\s+serverstartup\s+'
                r'"map\s+[^\s"]+\s+[^\s"]+"\s*$',
                re.IGNORECASE | re.MULTILINE,
            ),
            "Observed-current retail server.cfg must show map+factory startup "
            "syntax such as: set serverstartup \"map <map> <factory>\"",
        )


def main() -> None:
    meson_mode = "--meson" in sys.argv
    if meson_mode:
        sys.argv.remove("--meson")
        try:
            root = locate_retail_root()
        except RetailProbeConfigurationError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            raise SystemExit(1) from exc
        if root is None:
            print(
                f"SKIP: retail Quake Live not found; set {RETAIL_PATH_ENV} to run "
                "the read-only factory probe"
            )
            raise SystemExit(77)

    unittest.main()


if __name__ == "__main__":
    main()
