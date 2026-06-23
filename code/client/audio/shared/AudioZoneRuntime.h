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

#include "AudioZoneFormat.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace fnql_audiozones_runtime {

namespace azfmt = fnql_audiozones;

constexpr float kAudioZonePortalBlendDistance = azfmt::kDefaultPortalBlendDistance;
constexpr float kAudioZonePortalMaxBlend = azfmt::kDefaultPortalMaximumBlend;
constexpr float kAudioZonePortalMinimumBlend = azfmt::kDefaultPortalMinimumBlend;

struct Vec3f {
	float v[3] = { 0.0f, 0.0f, 0.0f };
};

inline float ClampFloat( float value, float minimum, float maximum ) {
	if ( value < minimum ) {
		return minimum;
	}
	if ( value > maximum ) {
		return maximum;
	}
	return value;
}

inline float SmoothStep( float value ) {
	value = ClampFloat( value, 0.0f, 1.0f );
	return value * value * ( 3.0f - 2.0f * value );
}

inline float DistancePointToBounds( const float *origin, const Vec3f &mins, const Vec3f &maxs ) {
	if ( origin == nullptr ) {
		return std::numeric_limits<float>::max();
	}

	float distanceSquared = 0.0f;
	for ( int i = 0; i < 3; ++i ) {
		float distance = 0.0f;
		if ( origin[i] < mins.v[i] ) {
			distance = mins.v[i] - origin[i];
		} else if ( origin[i] > maxs.v[i] ) {
			distance = origin[i] - maxs.v[i];
		}
		distanceSquared += distance * distance;
	}
	return std::sqrt( distanceSquared );
}

struct AudioZonePortal {
	std::uint32_t targetZone = 0;
	Vec3f mins;
	Vec3f maxs;
	float openness = 0.0f;
	float blendDistance = azfmt::kDefaultPortalBlendDistance;
	float minimumBlend = azfmt::kDefaultPortalMinimumBlend;
	float maximumBlend = azfmt::kDefaultPortalMaximumBlend;
	std::uint8_t blendCurve = static_cast<std::uint8_t>( azfmt::PortalBlendCurve::Smooth );

	float CurveProximity( float proximity ) const {
		proximity = ClampFloat( proximity, 0.0f, 1.0f );
		switch ( static_cast<azfmt::PortalBlendCurve>( blendCurve ) ) {
		case azfmt::PortalBlendCurve::Linear:
			return proximity;
		case azfmt::PortalBlendCurve::EaseIn:
			return proximity * proximity;
		case azfmt::PortalBlendCurve::EaseOut: {
			const float inverse = 1.0f - proximity;
			return 1.0f - inverse * inverse;
		}
		case azfmt::PortalBlendCurve::Smooth:
		default:
			return SmoothStep( proximity );
		}
	}

	float Blend( const float *origin ) const {
		if ( openness <= 0.0f || origin == nullptr ) {
			return 0.0f;
		}

		const float distance = DistancePointToBounds( origin, mins, maxs );
		if ( distance >= blendDistance ) {
			return 0.0f;
		}

		const float proximity = 1.0f - ( distance / blendDistance );
		const float influence = CurveProximity( proximity ) * ClampFloat( openness, 0.0f, 1.0f );
		if ( influence <= minimumBlend ) {
			return 0.0f;
		}
		return ClampFloat( influence, 0.0f, maximumBlend );
	}

	float Influence( const float *origin ) const {
		return Blend( origin );
	}
};

struct AudioZone {
	Vec3f mins;
	Vec3f maxs;
	int presetIndex = 0;
	float reverbGain = 1.0f;
	float occlusionMultiplier = 1.0f;
	float directLF = 1.0f;
	float directHF = 1.0f;
	float wetLF = 1.0f;
	float wetHF = 1.0f;
	int transitionMs = static_cast<int>( azfmt::kDefaultTransitionMs );
	int priority = 0;
	std::uint8_t materialClass = static_cast<std::uint8_t>( azfmt::MaterialClass::Unknown );
	std::uint8_t flags = 0;
	std::string name;
	std::vector<AudioZonePortal> portals;

	bool Contains( const float *origin ) const {
		return origin != nullptr &&
			origin[0] >= mins.v[0] && origin[0] <= maxs.v[0] &&
			origin[1] >= mins.v[1] && origin[1] <= maxs.v[1] &&
			origin[2] >= mins.v[2] && origin[2] <= maxs.v[2];
	}

	float Volume() const {
		return ( maxs.v[0] - mins.v[0] ) * ( maxs.v[1] - mins.v[1] ) * ( maxs.v[2] - mins.v[2] );
	}
};

struct AudioZonePortalBlend {
	const AudioZone *target = nullptr;
	float blend = 0.0f;
};

inline void NormalizeAudioZoneBounds( AudioZone &zone ) {
	for ( int i = 0; i < 3; ++i ) {
		if ( zone.mins.v[i] > zone.maxs.v[i] ) {
			std::swap( zone.mins.v[i], zone.maxs.v[i] );
		}
	}
}

inline void NormalizeAudioZonePortalBounds( AudioZonePortal &portal ) {
	for ( int i = 0; i < 3; ++i ) {
		if ( portal.mins.v[i] > portal.maxs.v[i] ) {
			std::swap( portal.mins.v[i], portal.maxs.v[i] );
		}
	}
}

inline bool ReadAudioZoneU8( const std::uint8_t *&cursor, const std::uint8_t *end, std::uint8_t &out ) {
	if ( cursor == nullptr || end == nullptr || cursor >= end || cursor > end ) {
		return false;
	}
	out = *cursor++;
	return true;
}

inline void NormalizeAudioZonePortalTuning( AudioZonePortal &portal ) {
	portal.openness = ClampFloat( portal.openness, 0.0f, 1.0f );
	portal.blendDistance = ClampFloat( portal.blendDistance, azfmt::kMinimumPortalBlendDistance, azfmt::kMaximumPortalBlendDistance );
	portal.minimumBlend = ClampFloat( portal.minimumBlend, 0.0f, 1.0f );
	portal.maximumBlend = ClampFloat( portal.maximumBlend, 0.0f, 1.0f );
	if ( portal.minimumBlend > portal.maximumBlend ) {
		std::swap( portal.minimumBlend, portal.maximumBlend );
	}
}

inline bool IsSupportedAudioZoneVersion( std::uint32_t version ) {
	return version == azfmt::kLegacyVersion ||
		version == azfmt::kMetadataVersion ||
		version == azfmt::kPortalTuningVersion;
}

inline bool ReadAudioZoneU16( const std::uint8_t *&cursor, const std::uint8_t *end, std::uint16_t &out ) {
	if ( cursor == nullptr || end == nullptr || cursor > end || end - cursor < 2 ) {
		return false;
	}
	out = static_cast<std::uint16_t>( cursor[0] ) |
		( static_cast<std::uint16_t>( cursor[1] ) << 8u );
	cursor += 2;
	return true;
}

inline bool ReadAudioZoneU32( const std::uint8_t *&cursor, const std::uint8_t *end, std::uint32_t &out ) {
	if ( cursor == nullptr || end == nullptr || cursor > end || end - cursor < 4 ) {
		return false;
	}
	out = static_cast<std::uint32_t>( cursor[0] ) |
		( static_cast<std::uint32_t>( cursor[1] ) << 8u ) |
		( static_cast<std::uint32_t>( cursor[2] ) << 16u ) |
		( static_cast<std::uint32_t>( cursor[3] ) << 24u );
	cursor += 4;
	return true;
}

inline bool ReadAudioZoneI32( const std::uint8_t *&cursor, const std::uint8_t *end, std::int32_t &out ) {
	std::uint32_t value = 0;
	if ( !ReadAudioZoneU32( cursor, end, value ) ) {
		return false;
	}
	out = static_cast<std::int32_t>( value );
	return true;
}

inline bool ReadAudioZoneF32( const std::uint8_t *&cursor, const std::uint8_t *end, float &out ) {
	std::uint32_t bits = 0;
	if ( !ReadAudioZoneU32( cursor, end, bits ) ) {
		return false;
	}
	std::memcpy( &out, &bits, sizeof( out ) );
	return std::isfinite( out );
}

inline bool ReadAudioZoneVec3( const std::uint8_t *&cursor, const std::uint8_t *end, Vec3f &out ) {
	return ReadAudioZoneF32( cursor, end, out.v[0] ) &&
		ReadAudioZoneF32( cursor, end, out.v[1] ) &&
		ReadAudioZoneF32( cursor, end, out.v[2] );
}

inline bool ParseAudioZoneBinary( const std::uint8_t *data, std::size_t length, std::vector<AudioZone> &zones, std::string &error ) {
	if ( data == nullptr || length < 12u ) {
		error = "truncated header";
		return false;
	}
	if ( length > static_cast<std::size_t>( azfmt::kMaxFileBytes ) ) {
		error = "file is too large";
		return false;
	}
	if ( std::memcmp( data, azfmt::kMagic, sizeof( azfmt::kMagic ) ) != 0 ) {
		error = "bad magic";
		return false;
	}

	const std::uint8_t *cursor = data + sizeof( azfmt::kMagic );
	const std::uint8_t *end = data + length;
	std::uint32_t version = 0;
	std::uint32_t zoneCount = 0;
	if ( !ReadAudioZoneU32( cursor, end, version ) || !ReadAudioZoneU32( cursor, end, zoneCount ) ) {
		error = "truncated header";
		return false;
	}
	if ( !IsSupportedAudioZoneVersion( version ) ) {
		error = "unsupported version " + std::to_string( version );
		return false;
	}
	if ( zoneCount > azfmt::kMaxZones ) {
		error = "too many zones";
		return false;
	}
	if ( zoneCount == 0 ) {
		error = "no zones";
		return false;
	}

	zones.clear();
	zones.reserve( zoneCount );
	for ( std::uint32_t i = 0; i < zoneCount; ++i ) {
		AudioZone zone;
		std::uint32_t presetIndex = 0;
		std::uint32_t transitionMs = 0;
		std::int32_t priority = 0;
		std::uint8_t nameLength = 0;
		if ( !ReadAudioZoneVec3( cursor, end, zone.mins ) ||
			!ReadAudioZoneVec3( cursor, end, zone.maxs ) ||
			!ReadAudioZoneU32( cursor, end, presetIndex ) ||
			!ReadAudioZoneF32( cursor, end, zone.reverbGain ) ||
			!ReadAudioZoneF32( cursor, end, zone.occlusionMultiplier ) ||
			!ReadAudioZoneF32( cursor, end, zone.directLF ) ||
			!ReadAudioZoneF32( cursor, end, zone.directHF ) ||
			!ReadAudioZoneF32( cursor, end, zone.wetLF ) ||
			!ReadAudioZoneF32( cursor, end, zone.wetHF ) ||
			!ReadAudioZoneU32( cursor, end, transitionMs ) ||
			!ReadAudioZoneI32( cursor, end, priority ) ||
			!ReadAudioZoneU8( cursor, end, nameLength ) ) {
			error = "truncated zone record";
			return false;
		}
		if ( presetIndex >= static_cast<std::uint32_t>( azfmt::Preset::Count ) ) {
			error = "unknown environment preset index";
			return false;
		}
		if ( nameLength == 0 || nameLength > azfmt::kMaxNameBytes || cursor > end || static_cast<std::size_t>( end - cursor ) < nameLength ) {
			error = "invalid zone name length";
			return false;
		}

		NormalizeAudioZoneBounds( zone );
		if ( zone.mins.v[0] == zone.maxs.v[0] || zone.mins.v[1] == zone.maxs.v[1] || zone.mins.v[2] == zone.maxs.v[2] ) {
			error = "zero-volume zone bounds";
			return false;
		}
		zone.presetIndex = static_cast<int>( presetIndex );
		zone.reverbGain = ClampFloat( zone.reverbGain, 0.0f, 4.0f );
		zone.occlusionMultiplier = ClampFloat( zone.occlusionMultiplier, 0.0f, 4.0f );
		zone.directLF = ClampFloat( zone.directLF, 0.0f, 1.0f );
		zone.directHF = ClampFloat( zone.directHF, 0.0f, 1.0f );
		zone.wetLF = ClampFloat( zone.wetLF, 0.0f, 1.0f );
		zone.wetHF = ClampFloat( zone.wetHF, 0.0f, 1.0f );
		zone.transitionMs = transitionMs > 10000u ? 10000 : static_cast<int>( transitionMs );
		zone.priority = static_cast<int>( priority );
		zone.name.assign( reinterpret_cast<const char *>( cursor ), nameLength );
		cursor += nameLength;

		if ( version >= azfmt::kMetadataVersion ) {
			std::uint8_t materialClass = 0;
			std::uint8_t flags = 0;
			std::uint16_t portalCount = 0;
			if ( !ReadAudioZoneU8( cursor, end, materialClass ) ||
				!ReadAudioZoneU8( cursor, end, flags ) ||
				!ReadAudioZoneU16( cursor, end, portalCount ) ) {
				error = "truncated zone metadata";
				return false;
			}
			if ( materialClass >= static_cast<std::uint8_t>( azfmt::MaterialClass::Count ) ) {
				error = "unknown zone material class";
				return false;
			}
			if ( portalCount > azfmt::kMaxZonePortals ) {
				error = "too many zone portals";
				return false;
			}
			zone.materialClass = materialClass;
			zone.flags = static_cast<std::uint8_t>( flags & ( azfmt::ZoneFlagGenerated | azfmt::ZoneFlagOutdoor | azfmt::ZoneFlagUnderwater ) );
			zone.portals.reserve( portalCount );
			for ( std::uint16_t portal = 0; portal < portalCount; ++portal ) {
				AudioZonePortal zonePortal;
				if ( !ReadAudioZoneU32( cursor, end, zonePortal.targetZone ) ||
					!ReadAudioZoneVec3( cursor, end, zonePortal.mins ) ||
					!ReadAudioZoneVec3( cursor, end, zonePortal.maxs ) ||
					!ReadAudioZoneF32( cursor, end, zonePortal.openness ) ) {
					error = "truncated zone portal record";
					return false;
				}
				if ( version >= azfmt::kPortalTuningVersion ) {
					if ( !ReadAudioZoneF32( cursor, end, zonePortal.blendDistance ) ||
						!ReadAudioZoneF32( cursor, end, zonePortal.minimumBlend ) ||
						!ReadAudioZoneF32( cursor, end, zonePortal.maximumBlend ) ||
						!ReadAudioZoneU8( cursor, end, zonePortal.blendCurve ) ) {
						error = "truncated zone portal tuning";
						return false;
					}
				}
				if ( zonePortal.targetZone >= zoneCount ||
					!std::isfinite( zonePortal.openness ) ||
					!std::isfinite( zonePortal.blendDistance ) ||
					!std::isfinite( zonePortal.minimumBlend ) ||
					!std::isfinite( zonePortal.maximumBlend ) ||
					zonePortal.blendCurve >= static_cast<std::uint8_t>( azfmt::PortalBlendCurve::Count ) ) {
					error = "invalid zone portal record";
					return false;
				}
				NormalizeAudioZonePortalBounds( zonePortal );
				NormalizeAudioZonePortalTuning( zonePortal );
				zone.portals.push_back( zonePortal );
			}
		}
		zones.push_back( zone );
	}

	if ( cursor != end ) {
		error = "trailing bytes";
		return false;
	}
	return true;
}

inline const AudioZone *FindAudioZone( const std::vector<AudioZone> &zones, const float *origin ) {
	const AudioZone *best = nullptr;
	float bestVolume = 0.0f;
	for ( const AudioZone &zone : zones ) {
		if ( !zone.Contains( origin ) ) {
			continue;
		}
		const float volume = zone.Volume();
		if ( best == nullptr ||
			zone.priority > best->priority ||
			( zone.priority == best->priority && volume < bestVolume ) ) {
			best = &zone;
			bestVolume = volume;
		}
	}
	return best;
}

inline AudioZonePortalBlend FindAudioZonePortalBlend( const std::vector<AudioZone> &zones, const AudioZone &zone, const float *origin ) {
	AudioZonePortalBlend result;
	float bestInfluence = 0.0f;
	for ( const AudioZonePortal &portal : zone.portals ) {
		if ( portal.targetZone >= zones.size() ) {
			continue;
		}
		const AudioZone &target = zones[portal.targetZone];
		if ( &target == &zone ) {
			continue;
		}
		const float blend = portal.Blend( origin );
		if ( blend > bestInfluence ) {
			bestInfluence = blend;
			result.target = &target;
		}
	}

	if ( result.target != nullptr && bestInfluence > 0.0f ) {
		result.blend = bestInfluence;
	} else {
		result.target = nullptr;
		result.blend = 0.0f;
	}
	return result;
}

} // namespace fnql_audiozones_runtime
