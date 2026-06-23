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

#include <algorithm>

namespace fnql_audio_occlusion {

constexpr float kMinimumDistance = 64.0f;
constexpr float kProbeFanDistance = 96.0f;
constexpr float kProbeSpreadScale = 0.04f;
constexpr float kProbeSpreadMinimum = 10.0f;
constexpr float kProbeSpreadMaximum = 28.0f;
constexpr float kPartialOcclusionBias = 0.18f;
constexpr float kCenterBlockedMinimum = 0.55f;

constexpr float kDirectAttenuation = 0.58f;
constexpr float kDirectGainMinimum = 0.18f;
constexpr float kDirectHFCut = 0.92f;
constexpr float kDirectHFMinimum = 0.08f;
constexpr float kWetBoost = 0.28f;
constexpr float kWetHFCut = 0.42f;
constexpr float kWetHFMinimum = 0.08f;

inline float Clamp01( float value ) {
	return ( std::max )( 0.0f, ( std::min )( value, 1.0f ) );
}

inline bool DistanceCanOcclude( float distance ) {
	return distance >= kMinimumDistance;
}

inline bool UsesProbeFan( float distance ) {
	return distance > kProbeFanDistance;
}

inline float ProbeSpreadForDistance( float distance ) {
	return ( std::max )( kProbeSpreadMinimum,
		( std::min )( distance * kProbeSpreadScale, kProbeSpreadMaximum ) );
}

inline float OcclusionFromProbeHits( int blocked, int total, bool centerBlocked ) {
	if ( blocked <= 0 || total <= 0 ) {
		return 0.0f;
	}

	float occlusion = Clamp01( static_cast<float>( blocked ) / static_cast<float>( total ) );
	if ( blocked < total ) {
		occlusion = kPartialOcclusionBias + occlusion * ( 1.0f - kPartialOcclusionBias );
	}
	if ( centerBlocked ) {
		occlusion = ( std::max )( occlusion, kCenterBlockedMinimum );
	}
	return Clamp01( occlusion );
}

inline float ApplyStrength( float occlusion, float strength ) {
	return Clamp01( Clamp01( occlusion ) * ( std::max )( strength, 0.0f ) );
}

inline float DirectGain( float occlusion ) {
	return ( std::max )( kDirectGainMinimum, 1.0f - Clamp01( occlusion ) * kDirectAttenuation );
}

inline float DirectHF( float environmentDirectHF, float occlusion ) {
	return ( std::max )( kDirectHFMinimum,
		( std::min )( Clamp01( environmentDirectHF ) * ( 1.0f - Clamp01( occlusion ) * kDirectHFCut ), 1.0f ) );
}

inline float WetGain( float environmentWetGain, float distanceMix, float occlusion ) {
	return Clamp01( Clamp01( environmentWetGain ) * ( 0.35f + Clamp01( distanceMix ) * 0.65f ) +
		Clamp01( occlusion ) * kWetBoost );
}

inline float WetHF( float environmentWetHF, float occlusion ) {
	return ( std::max )( kWetHFMinimum,
		( std::min )( Clamp01( environmentWetHF ) * ( 1.0f - Clamp01( occlusion ) * kWetHFCut ), 1.0f ) );
}

} // namespace fnql_audio_occlusion
