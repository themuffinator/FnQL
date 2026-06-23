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

#include "AudioZoneRuntime.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace azfmt = fnql_audiozones;
namespace azrt = fnql_audiozones_runtime;

using Bytes = std::vector<std::uint8_t>;

struct PortalSpec {
	std::uint32_t targetZone = 0;
	float mins[3] = { 0.0f, 0.0f, 0.0f };
	float maxs[3] = { 0.0f, 0.0f, 0.0f };
	float openness = 1.0f;
	float blendDistance = azfmt::kDefaultPortalBlendDistance;
	float minimumBlend = azfmt::kDefaultPortalMinimumBlend;
	float maximumBlend = azfmt::kDefaultPortalMaximumBlend;
	std::uint8_t blendCurve = static_cast<std::uint8_t>( azfmt::PortalBlendCurve::Smooth );
};

struct ZoneSpec {
	float mins[3] = { 0.0f, 0.0f, 0.0f };
	float maxs[3] = { 128.0f, 128.0f, 128.0f };
	std::uint32_t preset = static_cast<std::uint32_t>( azfmt::Preset::Room );
	float reverbGain = 1.0f;
	float occlusionMultiplier = 1.0f;
	float directLF = 1.0f;
	float directHF = 1.0f;
	float wetLF = 1.0f;
	float wetHF = 1.0f;
	std::uint32_t transitionMs = azfmt::kDefaultTransitionMs;
	std::int32_t priority = 0;
	std::string name = "zone";
	std::uint8_t materialClass = static_cast<std::uint8_t>( azfmt::MaterialClass::Unknown );
	std::uint8_t flags = 0;
	std::vector<PortalSpec> portals;
};

bool Check( bool condition, const char *test, int line, const char *expression ) {
	if ( condition ) {
		return true;
	}
	std::fprintf( stderr, "%s:%d: check failed: %s\n", test, line, expression );
	return false;
}

#define CHECK( expression ) do { if ( !Check( ( expression ), __func__, __LINE__, #expression ) ) return false; } while ( 0 )

bool Near( float a, float b, float epsilon = 0.001f ) {
	return std::fabs( a - b ) <= epsilon;
}

void AppendU8( Bytes &bytes, std::uint8_t value ) {
	bytes.push_back( value );
}

void AppendU16( Bytes &bytes, std::uint16_t value ) {
	bytes.push_back( static_cast<std::uint8_t>( value & 0xffu ) );
	bytes.push_back( static_cast<std::uint8_t>( ( value >> 8u ) & 0xffu ) );
}

void AppendU32( Bytes &bytes, std::uint32_t value ) {
	bytes.push_back( static_cast<std::uint8_t>( value & 0xffu ) );
	bytes.push_back( static_cast<std::uint8_t>( ( value >> 8u ) & 0xffu ) );
	bytes.push_back( static_cast<std::uint8_t>( ( value >> 16u ) & 0xffu ) );
	bytes.push_back( static_cast<std::uint8_t>( ( value >> 24u ) & 0xffu ) );
}

void AppendI32( Bytes &bytes, std::int32_t value ) {
	AppendU32( bytes, static_cast<std::uint32_t>( value ) );
}

void AppendF32( Bytes &bytes, float value ) {
	std::uint32_t bits = 0;
	std::memcpy( &bits, &value, sizeof( bits ) );
	AppendU32( bytes, bits );
}

void AppendVec3( Bytes &bytes, const float value[3] ) {
	AppendF32( bytes, value[0] );
	AppendF32( bytes, value[1] );
	AppendF32( bytes, value[2] );
}

void AppendZoneRecord( Bytes &bytes, const ZoneSpec &zone, std::uint32_t version ) {
	AppendVec3( bytes, zone.mins );
	AppendVec3( bytes, zone.maxs );
	AppendU32( bytes, zone.preset );
	AppendF32( bytes, zone.reverbGain );
	AppendF32( bytes, zone.occlusionMultiplier );
	AppendF32( bytes, zone.directLF );
	AppendF32( bytes, zone.directHF );
	AppendF32( bytes, zone.wetLF );
	AppendF32( bytes, zone.wetHF );
	AppendU32( bytes, zone.transitionMs );
	AppendI32( bytes, zone.priority );
	AppendU8( bytes, static_cast<std::uint8_t>( zone.name.size() ) );
	bytes.insert( bytes.end(), zone.name.begin(), zone.name.end() );

	if ( version >= azfmt::kMetadataVersion ) {
		AppendU8( bytes, zone.materialClass );
		AppendU8( bytes, zone.flags );
		AppendU16( bytes, static_cast<std::uint16_t>( zone.portals.size() ) );
		for ( const PortalSpec &portal : zone.portals ) {
			AppendU32( bytes, portal.targetZone );
			AppendVec3( bytes, portal.mins );
			AppendVec3( bytes, portal.maxs );
			AppendF32( bytes, portal.openness );
			if ( version >= azfmt::kPortalTuningVersion ) {
				AppendF32( bytes, portal.blendDistance );
				AppendF32( bytes, portal.minimumBlend );
				AppendF32( bytes, portal.maximumBlend );
				AppendU8( bytes, portal.blendCurve );
			}
		}
	}
}

Bytes MakeSidecar( std::uint32_t version, const std::vector<ZoneSpec> &zones ) {
	Bytes bytes;
	bytes.insert( bytes.end(), std::begin( azfmt::kMagic ), std::end( azfmt::kMagic ) );
	AppendU32( bytes, version );
	AppendU32( bytes, static_cast<std::uint32_t>( zones.size() ) );
	for ( const ZoneSpec &zone : zones ) {
		AppendZoneRecord( bytes, zone, version );
	}
	return bytes;
}

bool ParseZones( const Bytes &bytes, std::vector<azrt::AudioZone> &zones, std::string &error ) {
	return azrt::ParseAudioZoneBinary( bytes.data(), bytes.size(), zones, error );
}

bool ParseLegacySidecarPreservesCompatibilityDefaults() {
	ZoneSpec zone;
	zone.mins[0] = 128.0f;
	zone.mins[1] = 64.0f;
	zone.mins[2] = 32.0f;
	zone.maxs[0] = 0.0f;
	zone.maxs[1] = 0.0f;
	zone.maxs[2] = 0.0f;
	zone.reverbGain = 5.0f;
	zone.occlusionMultiplier = 5.0f;
	zone.directHF = 2.0f;
	zone.wetHF = -1.0f;
	zone.transitionMs = 50000u;
	zone.name = "legacy";

	std::vector<azrt::AudioZone> zones;
	std::string error;
	CHECK( ParseZones( MakeSidecar( azfmt::kLegacyVersion, { zone } ), zones, error ) );
	CHECK( zones.size() == 1u );
	CHECK( zones[0].name == "legacy" );
	CHECK( Near( zones[0].mins.v[0], 0.0f ) );
	CHECK( Near( zones[0].maxs.v[0], 128.0f ) );
	CHECK( Near( zones[0].reverbGain, 4.0f ) );
	CHECK( Near( zones[0].occlusionMultiplier, 4.0f ) );
	CHECK( Near( zones[0].directHF, 1.0f ) );
	CHECK( Near( zones[0].wetHF, 0.0f ) );
	CHECK( zones[0].transitionMs == 10000 );
	CHECK( zones[0].materialClass == static_cast<std::uint8_t>( azfmt::MaterialClass::Unknown ) );
	CHECK( zones[0].flags == 0 );
	CHECK( zones[0].portals.empty() );

	const float inside[3] = { 8.0f, 8.0f, 8.0f };
	CHECK( azrt::FindAudioZone( zones, inside ) == &zones[0] );
	return true;
}

bool SelectsPriorityThenSmallerVolume() {
	std::vector<azrt::AudioZone> zones( 3 );
	zones[0].name = "large";
	zones[0].mins = { { 0.0f, 0.0f, 0.0f } };
	zones[0].maxs = { { 512.0f, 512.0f, 512.0f } };
	zones[0].priority = 0;
	zones[1].name = "small";
	zones[1].mins = { { 128.0f, 128.0f, 128.0f } };
	zones[1].maxs = { { 256.0f, 256.0f, 256.0f } };
	zones[1].priority = 0;
	zones[2].name = "priority";
	zones[2].mins = { { 0.0f, 0.0f, 0.0f } };
	zones[2].maxs = { { 512.0f, 512.0f, 512.0f } };
	zones[2].priority = 1;

	const float sharedPoint[3] = { 160.0f, 160.0f, 160.0f };
	CHECK( azrt::FindAudioZone( { zones[0], zones[1] }, sharedPoint )->name == "small" );
	CHECK( azrt::FindAudioZone( zones, sharedPoint )->name == "priority" );
	return true;
}

bool ParsesVersion2MetadataAndPortalBlend() {
	ZoneSpec room;
	room.maxs[0] = 512.0f;
	room.maxs[1] = 128.0f;
	room.maxs[2] = 128.0f;
	room.name = "room";
	room.materialClass = static_cast<std::uint8_t>( azfmt::MaterialClass::Metal );
	room.flags = azfmt::ZoneFlagGenerated | azfmt::ZoneFlagOutdoor | 0x80u;
	room.portals.push_back( { 1u, { 512.0f, 0.0f, 0.0f }, { 512.0f, 128.0f, 128.0f }, 1.0f } );

	ZoneSpec hall;
	hall.mins[0] = 512.0f;
	hall.maxs[0] = 1024.0f;
	hall.maxs[1] = 128.0f;
	hall.maxs[2] = 128.0f;
	hall.name = "hall";
	hall.preset = static_cast<std::uint32_t>( azfmt::Preset::Hallway );
	hall.materialClass = static_cast<std::uint8_t>( azfmt::MaterialClass::Soft );
	hall.flags = azfmt::ZoneFlagGenerated;

	std::vector<azrt::AudioZone> zones;
	std::string error;
	CHECK( ParseZones( MakeSidecar( azfmt::kMetadataVersion, { room, hall } ), zones, error ) );
	CHECK( zones.size() == 2u );
	CHECK( zones[0].materialClass == static_cast<std::uint8_t>( azfmt::MaterialClass::Metal ) );
	CHECK( ( zones[0].flags & azfmt::ZoneFlagGenerated ) != 0 );
	CHECK( ( zones[0].flags & azfmt::ZoneFlagOutdoor ) != 0 );
	CHECK( ( zones[0].flags & 0x80u ) == 0 );
	CHECK( zones[0].portals.size() == 1u );
	CHECK( zones[0].portals[0].targetZone == 1u );
	CHECK( Near( zones[0].portals[0].blendDistance, azrt::kAudioZonePortalBlendDistance ) );
	CHECK( Near( zones[0].portals[0].minimumBlend, azrt::kAudioZonePortalMinimumBlend ) );
	CHECK( Near( zones[0].portals[0].maximumBlend, azrt::kAudioZonePortalMaxBlend ) );
	CHECK( zones[0].portals[0].blendCurve == static_cast<std::uint8_t>( azfmt::PortalBlendCurve::Smooth ) );

	const float nearPortal[3] = { 500.0f, 64.0f, 64.0f };
	const azrt::AudioZonePortalBlend nearBlend = azrt::FindAudioZonePortalBlend( zones, zones[0], nearPortal );
	CHECK( nearBlend.target == &zones[1] );
	CHECK( Near( nearBlend.blend, azrt::kAudioZonePortalMaxBlend ) );

	const float farFromPortal[3] = { 32.0f, 64.0f, 64.0f };
	const azrt::AudioZonePortalBlend farBlend = azrt::FindAudioZonePortalBlend( zones, zones[0], farFromPortal );
	CHECK( farBlend.target == nullptr );
	CHECK( Near( farBlend.blend, 0.0f ) );
	return true;
}

bool ParsesVersion3PerPortalTuning() {
	ZoneSpec room;
	room.maxs[0] = 512.0f;
	room.maxs[1] = 128.0f;
	room.maxs[2] = 128.0f;
	room.name = "room";

	PortalSpec tunedPortal;
	tunedPortal.targetZone = 1u;
	tunedPortal.mins[0] = 512.0f;
	tunedPortal.mins[1] = 0.0f;
	tunedPortal.mins[2] = 0.0f;
	tunedPortal.maxs[0] = 512.0f;
	tunedPortal.maxs[1] = 128.0f;
	tunedPortal.maxs[2] = 128.0f;
	tunedPortal.openness = 1.0f;
	tunedPortal.blendDistance = 64.0f;
	tunedPortal.minimumBlend = 0.0f;
	tunedPortal.maximumBlend = 1.0f;
	tunedPortal.blendCurve = static_cast<std::uint8_t>( azfmt::PortalBlendCurve::EaseOut );
	room.portals.push_back( tunedPortal );

	ZoneSpec hall;
	hall.mins[0] = 512.0f;
	hall.maxs[0] = 1024.0f;
	hall.maxs[1] = 128.0f;
	hall.maxs[2] = 128.0f;
	hall.name = "hall";

	std::vector<azrt::AudioZone> zones;
	std::string error;
	CHECK( ParseZones( MakeSidecar( azfmt::kVersion, { room, hall } ), zones, error ) );
	CHECK( zones.size() == 2u );
	CHECK( zones[0].portals.size() == 1u );
	CHECK( Near( zones[0].portals[0].blendDistance, 64.0f ) );
	CHECK( Near( zones[0].portals[0].minimumBlend, 0.0f ) );
	CHECK( Near( zones[0].portals[0].maximumBlend, 1.0f ) );
	CHECK( zones[0].portals[0].blendCurve == static_cast<std::uint8_t>( azfmt::PortalBlendCurve::EaseOut ) );

	const float quarterProximity[3] = { 464.0f, 64.0f, 64.0f };
	const azrt::AudioZonePortalBlend tunedBlend = azrt::FindAudioZonePortalBlend( zones, zones[0], quarterProximity );
	CHECK( tunedBlend.target == &zones[1] );
	CHECK( Near( tunedBlend.blend, 0.4375f ) );

	const float outsideDistance[3] = { 448.0f, 64.0f, 64.0f };
	const azrt::AudioZonePortalBlend farBlend = azrt::FindAudioZonePortalBlend( zones, zones[0], outsideDistance );
	CHECK( farBlend.target == nullptr );
	CHECK( Near( farBlend.blend, 0.0f ) );
	return true;
}

bool RejectsInvalidSidecars() {
	std::vector<azrt::AudioZone> zones;
	std::string error;

	Bytes unsupported;
	unsupported.insert( unsupported.end(), std::begin( azfmt::kMagic ), std::end( azfmt::kMagic ) );
	AppendU32( unsupported, 99u );
	AppendU32( unsupported, 1u );
	CHECK( !ParseZones( unsupported, zones, error ) );
	CHECK( error.find( "unsupported version" ) != std::string::npos );

	ZoneSpec badMaterial;
	badMaterial.materialClass = static_cast<std::uint8_t>( azfmt::MaterialClass::Count );
	CHECK( !ParseZones( MakeSidecar( azfmt::kVersion, { badMaterial } ), zones, error ) );
	CHECK( error == "unknown zone material class" );

	ZoneSpec badPortal;
	badPortal.portals.push_back( { 3u, { 0.0f, 0.0f, 0.0f }, { 0.0f, 128.0f, 128.0f }, 1.0f } );
	ZoneSpec target;
	target.name = "target";
	CHECK( !ParseZones( MakeSidecar( azfmt::kVersion, { badPortal, target } ), zones, error ) );
	CHECK( error == "invalid zone portal record" );

	ZoneSpec badCurve;
	PortalSpec invalidCurve;
	invalidCurve.targetZone = 1u;
	invalidCurve.blendCurve = static_cast<std::uint8_t>( azfmt::PortalBlendCurve::Count );
	badCurve.portals.push_back( invalidCurve );
	CHECK( !ParseZones( MakeSidecar( azfmt::kVersion, { badCurve, target } ), zones, error ) );
	CHECK( error == "invalid zone portal record" );
	return true;
}

struct TestCase {
	const char *name;
	bool ( *run )();
};

} // namespace

int main() {
	const TestCase tests[] = {
		{ "ParseLegacySidecarPreservesCompatibilityDefaults", ParseLegacySidecarPreservesCompatibilityDefaults },
		{ "SelectsPriorityThenSmallerVolume", SelectsPriorityThenSmallerVolume },
		{ "ParsesVersion2MetadataAndPortalBlend", ParsesVersion2MetadataAndPortalBlend },
		{ "ParsesVersion3PerPortalTuning", ParsesVersion3PerPortalTuning },
		{ "RejectsInvalidSidecars", RejectsInvalidSidecars }
	};

	for ( const TestCase &test : tests ) {
		if ( !test.run() ) {
			std::fprintf( stderr, "%s failed\n", test.name );
			return 1;
		}
		std::printf( "%s passed\n", test.name );
	}
	return 0;
}
