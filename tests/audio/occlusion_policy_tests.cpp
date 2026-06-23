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

#include "AudioOcclusion.h"

#include <cmath>
#include <cstdio>

namespace {

namespace occ = fnql_audio_occlusion;

bool Check( bool condition, const char *test, int line, const char *expression ) {
	if ( condition ) {
		return true;
	}
	std::fprintf( stderr, "%s:%d: check failed: %s\n", test, line, expression );
	return false;
}

#define CHECK( expression ) do { if ( !Check( ( expression ), __func__, __LINE__, #expression ) ) return false; } while ( 0 )

bool Near( float a, float b, float epsilon = 0.0001f ) {
	return std::fabs( a - b ) <= epsilon;
}

bool DistancePolicyIgnoresNearFieldAndSpreadsFarSources() {
	CHECK( !occ::DistanceCanOcclude( occ::kMinimumDistance - 0.1f ) );
	CHECK( occ::DistanceCanOcclude( occ::kMinimumDistance ) );
	CHECK( !occ::UsesProbeFan( occ::kProbeFanDistance ) );
	CHECK( occ::UsesProbeFan( occ::kProbeFanDistance + 0.1f ) );
	CHECK( Near( occ::ProbeSpreadForDistance( 128.0f ), occ::kProbeSpreadMinimum ) );
	CHECK( Near( occ::ProbeSpreadForDistance( 2048.0f ), occ::kProbeSpreadMaximum ) );
	return true;
}

bool CenterObstructionStaysAudibleInThePolicy() {
	const float centerOnly = occ::OcclusionFromProbeHits( 1, 5, true );
	const float sideOnly = occ::OcclusionFromProbeHits( 1, 5, false );

	CHECK( centerOnly >= occ::kCenterBlockedMinimum );
	CHECK( centerOnly > sideOnly );
	CHECK( occ::DirectGain( centerOnly ) <= 0.70f );
	CHECK( occ::DirectHF( 0.95f, centerOnly ) < 0.50f );
	return true;
}

bool FullOcclusionHasAGainOnlyFallback() {
	const float directGain = occ::DirectGain( 1.0f );
	const float directHF = occ::DirectHF( 0.95f, 1.0f );

	CHECK( directGain <= 0.45f );
	CHECK( directGain >= occ::kDirectGainMinimum );
	CHECK( directHF <= 0.10f );
	CHECK( directHF >= occ::kDirectHFMinimum );
	return true;
}

bool PartialOcclusionDoesNotBecomeBinary() {
	const float partial = occ::OcclusionFromProbeHits( 2, 5, false );
	CHECK( partial > 0.40f );
	CHECK( partial < 0.60f );
	CHECK( occ::DirectGain( partial ) > occ::DirectGain( 1.0f ) );
	CHECK( occ::DirectGain( partial ) < occ::DirectGain( 0.0f ) );
	return true;
}

bool StrengthAndWetRoutingClampPredictably() {
	const float partial = occ::OcclusionFromProbeHits( 2, 5, false );
	CHECK( Near( occ::ApplyStrength( partial, 0.0f ), 0.0f ) );
	CHECK( Near( occ::ApplyStrength( partial, 2.0f ), 1.0f ) );
	CHECK( occ::WetGain( 0.10f, 0.25f, 1.0f ) > occ::WetGain( 0.10f, 0.25f, 0.0f ) );
	CHECK( occ::WetHF( 0.90f, 1.0f ) < occ::WetHF( 0.90f, 0.0f ) );
	return true;
}

struct TestCase {
	const char *name;
	bool ( *run )();
};

} // namespace

int main() {
	const TestCase tests[] = {
		{ "DistancePolicyIgnoresNearFieldAndSpreadsFarSources", DistancePolicyIgnoresNearFieldAndSpreadsFarSources },
		{ "CenterObstructionStaysAudibleInThePolicy", CenterObstructionStaysAudibleInThePolicy },
		{ "FullOcclusionHasAGainOnlyFallback", FullOcclusionHasAGainOnlyFallback },
		{ "PartialOcclusionDoesNotBecomeBinary", PartialOcclusionDoesNotBecomeBinary },
		{ "StrengthAndWetRoutingClampPredictably", StrengthAndWetRoutingClampPredictably }
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
