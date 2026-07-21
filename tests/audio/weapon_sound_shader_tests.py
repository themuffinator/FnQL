from __future__ import annotations

import hashlib
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BASEQ3_SHADER_PATH = ROOT / "pkg" / "baseq3" / "sound" / "fnql-weapon-sounds.sndshd"
MISSIONPACK_SHADER_PATH = ROOT / "pkg" / "missionpack" / "sound" / "fnql-weapon-sounds.sndshd"
Q3A_WEAPON_SOUNDS = {
    "sound/weapons/bfg/bfg_fire.wav",
    "sound/weapons/bfg/bfg_hum.wav",
    "sound/weapons/change.wav",
    "sound/weapons/grenade/grenlf1a.wav",
    "sound/weapons/grenade/hgrenb1a.wav",
    "sound/weapons/grenade/hgrenb2a.wav",
    "sound/weapons/lightning/lg_fire.wav",
    "sound/weapons/lightning/lg_hit.wav",
    "sound/weapons/lightning/lg_hit2.wav",
    "sound/weapons/lightning/lg_hit3.wav",
    "sound/weapons/lightning/lg_hum.wav",
    "sound/weapons/machinegun/buletby1.wav",
    "sound/weapons/machinegun/machgf1b.wav",
    "sound/weapons/machinegun/machgf2b.wav",
    "sound/weapons/machinegun/machgf3b.wav",
    "sound/weapons/machinegun/machgf4b.wav",
    "sound/weapons/machinegun/ric1.wav",
    "sound/weapons/machinegun/ric2.wav",
    "sound/weapons/machinegun/ric3.wav",
    "sound/weapons/melee/fstatck.wav",
    "sound/weapons/melee/fsthum.wav",
    "sound/weapons/melee/fstrun.wav",
    "sound/weapons/noammo.wav",
    "sound/weapons/plasma/hyprbf1a.wav",
    "sound/weapons/plasma/lasfly.wav",
    "sound/weapons/plasma/plasmx1a.wav",
    "sound/weapons/railgun/railgf1a.wav",
    "sound/weapons/railgun/rg_hum.wav",
    "sound/weapons/rocket/rockfly.wav",
    "sound/weapons/rocket/rocklf1a.wav",
    "sound/weapons/rocket/rocklx1a.wav",
    "sound/weapons/shotgun/sshotf1b.wav",
}

Q3A_WEAPON_FIRE_SOUNDS = {
    "sound/weapons/bfg/bfg_fire.wav",
    "sound/weapons/grenade/grenlf1a.wav",
    "sound/weapons/lightning/lg_fire.wav",
    "sound/weapons/machinegun/machgf1b.wav",
    "sound/weapons/machinegun/machgf2b.wav",
    "sound/weapons/machinegun/machgf3b.wav",
    "sound/weapons/machinegun/machgf4b.wav",
    "sound/weapons/melee/fstatck.wav",
    "sound/weapons/plasma/hyprbf1a.wav",
    "sound/weapons/railgun/railgf1a.wav",
    "sound/weapons/rocket/rocklf1a.wav",
    "sound/weapons/shotgun/sshotf1b.wav",
}

MISSIONPACK_WEAPON_FIRE_SOUNDS = {
    "sound/weapons/bfg/bfg_fire.wav",
    "sound/weapons/grenade/grenlf1a.wav",
    "sound/weapons/lightning/lg_fire.wav",
    "sound/weapons/machinegun/machgf1b.wav",
    "sound/weapons/machinegun/machgf2b.wav",
    "sound/weapons/machinegun/machgf3b.wav",
    "sound/weapons/machinegun/machgf4b.wav",
    "sound/weapons/melee/fstatck.wav",
    "sound/weapons/nailgun/wnalfire.wav",
    "sound/weapons/plasma/hyprbf1a.wav",
    "sound/weapons/proxmine/wstbfire.wav",
    "sound/weapons/railgun/railgf1a.wav",
    "sound/weapons/rocket/rocklf1a.wav",
    "sound/weapons/shotgun/sshotf1b.wav",
    "sound/weapons/vulcan/wvulfire.wav",
    "sound/weapons/vulcan/vulcanf1b.wav",
    "sound/weapons/vulcan/vulcanf2b.wav",
    "sound/weapons/vulcan/vulcanf3b.wav",
    "sound/weapons/vulcan/vulcanf4b.wav",
}

MISSIONPACK_WEAPON_HIT_SOUNDS = {
    "sound/weapons/grenade/hgrenb1a.wav",
    "sound/weapons/grenade/hgrenb2a.wav",
    "sound/weapons/lightning/lg_hit.wav",
    "sound/weapons/lightning/lg_hit2.wav",
    "sound/weapons/lightning/lg_hit3.wav",
    "sound/weapons/machinegun/ric1.wav",
    "sound/weapons/machinegun/ric2.wav",
    "sound/weapons/machinegun/ric3.wav",
    "sound/weapons/nailgun/wnalimpd.wav",
    "sound/weapons/nailgun/wnalimpl.wav",
    "sound/weapons/nailgun/wnalimpm.wav",
    "sound/weapons/plasma/plasmx1a.wav",
    "sound/weapons/proxmine/wstbexpl.wav",
    "sound/weapons/proxmine/wstbimpd.wav",
    "sound/weapons/proxmine/wstbimpl.wav",
    "sound/weapons/proxmine/wstbimpm.wav",
    "sound/weapons/rocket/rocklx1a.wav",
    "sound/weapons/vulcan/wvulimpd.wav",
    "sound/weapons/vulcan/wvulimpl.wav",
    "sound/weapons/vulcan/wvulimpm.wav",
}

MISSIONPACK_WEAPON_SOUNDS = MISSIONPACK_WEAPON_FIRE_SOUNDS | MISSIONPACK_WEAPON_HIT_SOUNDS


def shader_blocks(path: Path = BASEQ3_SHADER_PATH) -> dict[str, str]:
    text = path.read_text(encoding="utf-8")
    return {
        match.group("name"): match.group("body")
        for match in re.finditer(
            r"\bsound\s+(?P<name>\S+)\s*\{(?P<body>.*?)\}",
            text,
            flags=re.DOTALL,
        )
    }


def shader_samples(path: Path = BASEQ3_SHADER_PATH) -> set[str]:
    samples: set[str] = set()
    for block in shader_blocks(path).values():
        samples.update(
            line.strip().lower()
            for line in block.splitlines()
            if line.strip().lower().startswith("sound/weapons/")
        )
    return samples


def shader_blocks_by_sample(path: Path = BASEQ3_SHADER_PATH) -> dict[str, str]:
    by_sample: dict[str, str] = {}
    for block in shader_blocks(path).values():
        for line in block.splitlines():
            sample = line.strip().lower()
            if sample.startswith("sound/weapons/"):
                by_sample[sample] = block
    return by_sample


def value_for(block: str, key: str) -> float:
    match = re.search(rf"^\s*{re.escape(key)}\s+([-+]?\d+(?:\.\d+)?)", block, flags=re.MULTILINE)
    if not match:
        raise AssertionError(f"missing {key}")
    return float(match.group(1))


class WeaponSoundShaderTests(unittest.TestCase):
    def test_shipped_tuning_matches_fnq3_reference_exactly(self) -> None:
        expected = {
            BASEQ3_SHADER_PATH:
                "c2ed183785e01f227c4e59b5843f54f59b6bed1e5c4be569b39aae9971a61e29",
            MISSIONPACK_SHADER_PATH:
                "2f6fc1fb76414194d32ea4bba8f64d1ad4c3898a7e808e1959bc1762a77cb86e",
        }

        for path, expected_hash in expected.items():
            with self.subTest(path=path):
                text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
                fnq3_text = text.replace("FnQL", "FnQuake3").replace("fnql_", "fnq3_")
                self.assertEqual(
                    expected_hash,
                    hashlib.sha256(fnq3_text.encode("utf-8")).hexdigest(),
                )

    def test_shader_covers_all_standard_q3a_weapon_effects(self) -> None:
        self.assertEqual(shader_samples(BASEQ3_SHADER_PATH), Q3A_WEAPON_SOUNDS)

    def test_weapon_effect_shaders_are_punchier_and_travel_further(self) -> None:
        by_sample = shader_blocks_by_sample(BASEQ3_SHADER_PATH)

        for sample in sorted(Q3A_WEAPON_SOUNDS):
            with self.subTest(sample=sample):
                block = by_sample[sample]
                self.assertGreater(value_for(block, "volumeDb"), 0.0)
                self.assertGreater(value_for(block, "maxDistance"), 1330.0)
                self.assertGreater(value_for(block, "shakes"), 0.0)

    def test_standard_weapon_firing_samples_have_explicit_sound_shaders(self) -> None:
        by_sample: dict[str, str] = {}
        for shader_name, block in shader_blocks(BASEQ3_SHADER_PATH).items():
            for line in block.splitlines():
                sample = line.strip().lower()
                if sample in Q3A_WEAPON_FIRE_SOUNDS:
                    by_sample[sample] = shader_name

        self.assertEqual(set(by_sample), Q3A_WEAPON_FIRE_SOUNDS)
        for sample, shader_name in sorted(by_sample.items()):
            with self.subTest(sample=sample):
                self.assertRegex(shader_name, r"(fire|attack)")

    def test_missionpack_shader_covers_weapon_firing_and_hits(self) -> None:
        self.assertEqual(shader_samples(MISSIONPACK_SHADER_PATH), MISSIONPACK_WEAPON_SOUNDS)

    def test_missionpack_weapon_shaders_are_punchier_and_travel_further(self) -> None:
        by_sample = shader_blocks_by_sample(MISSIONPACK_SHADER_PATH)

        for sample in sorted(MISSIONPACK_WEAPON_SOUNDS):
            with self.subTest(sample=sample):
                block = by_sample[sample]
                self.assertGreater(value_for(block, "volumeDb"), 0.0)
                self.assertGreater(value_for(block, "maxDistance"), 1330.0)
                self.assertGreater(value_for(block, "shakes"), 0.0)

    def test_missionpack_firing_and_hit_samples_have_explicit_sound_shaders(self) -> None:
        by_sample: dict[str, str] = {}
        for shader_name, block in shader_blocks(MISSIONPACK_SHADER_PATH).items():
            for line in block.splitlines():
                sample = line.strip().lower()
                if sample in MISSIONPACK_WEAPON_SOUNDS:
                    by_sample[sample] = shader_name

        self.assertEqual(set(by_sample), MISSIONPACK_WEAPON_SOUNDS)
        for sample, shader_name in sorted(by_sample.items()):
            with self.subTest(sample=sample):
                if sample in MISSIONPACK_WEAPON_FIRE_SOUNDS:
                    self.assertRegex(shader_name, r"(fire|attack)")
                else:
                    self.assertRegex(shader_name, r"(hit|ricochet)")


if __name__ == "__main__":
    unittest.main()
