/*
===========================================================================
Copyright (C) 2026

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
===========================================================================
*/

#pragma once

#include <cstdint>

namespace fnql_audiozones {

constexpr std::uint8_t kMagic[4] = { 'F', 'Q', 'A', 'Z' };
constexpr std::uint32_t kLegacyVersion = 1;
constexpr std::uint32_t kMetadataVersion = 2;
constexpr std::uint32_t kPortalTuningVersion = 3;
constexpr std::uint32_t kVersion = kPortalTuningVersion;
constexpr std::uint32_t kMaxZones = 1024;
constexpr std::uint32_t kMaxZonePortals = 16;
constexpr std::uint32_t kMaxNameBytes = 63;
constexpr std::uint32_t kMaxFileBytes = 1024u * 1024u;
constexpr std::uint32_t kDefaultTransitionMs = 650;
constexpr float kDefaultPortalBlendDistance = 192.0f;
constexpr float kMinimumPortalBlendDistance = 1.0f;
constexpr float kMaximumPortalBlendDistance = 4096.0f;
constexpr float kDefaultPortalMinimumBlend = 0.02f;
constexpr float kDefaultPortalMaximumBlend = 0.45f;

enum class Preset : std::uint32_t {
	SmallRoom = 0,
	Room = 1,
	StoneRoom = 2,
	Hallway = 3,
	Hall = 4,
	Outdoors = 5,
	Underwater = 6,
	Count
};

enum class MaterialClass : std::uint8_t {
	Unknown = 0,
	Neutral = 1,
	Stone = 2,
	Metal = 3,
	Liquid = 4,
	Sky = 5,
	Soft = 6,
	Count
};

enum ZoneFlags : std::uint8_t {
	ZoneFlagGenerated = 1u << 0u,
	ZoneFlagOutdoor = 1u << 1u,
	ZoneFlagUnderwater = 1u << 2u
};

enum class PortalBlendCurve : std::uint8_t {
	Smooth = 0,
	Linear = 1,
	EaseIn = 2,
	EaseOut = 3,
	Count
};

constexpr const char *kPresetNames[] = {
	"small-room",
	"room",
	"stone-room",
	"hallway",
	"hall",
	"outdoors",
	"underwater"
};

constexpr const char *kMaterialClassNames[] = {
	"unknown",
	"neutral",
	"stone",
	"metal",
	"liquid",
	"sky",
	"soft"
};

constexpr const char *kPortalBlendCurveNames[] = {
	"smooth",
	"linear",
	"ease-in",
	"ease-out"
};

} // namespace fnql_audiozones
