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

#include "AudioVoiceQueue.h"

#include <array>
#include <cstdio>
#include <limits>

namespace {

namespace voice = fnql_audio_voice;

bool Check( bool condition, const char *test, int line, const char *expression ) {
	if ( condition ) {
		return true;
	}
	std::fprintf( stderr, "%s:%d: check failed: %s\n", test, line, expression );
	return false;
}

#define CHECK( expression ) do { if ( !Check( ( expression ), __func__, __LINE__, #expression ) ) return false; } while ( 0 )

bool ExistingSpeakerKeepsItsLane() {
	std::array<voice::LaneActivity, voice::kMaxLanes> lanes{};
	for ( voice::LaneActivity &lane : lanes ) {
		voice::ResetLane( lane );
	}
	lanes[3].clientNum = 17;
	lanes[3].queued = true;
	CHECK( voice::SelectLane( lanes.data(), static_cast<int>( lanes.size() ), 17, 1000 ) == 3 );
	return true;
}

bool BusySpeakerAudioIsNeverStolen() {
	std::array<voice::LaneActivity, voice::kMaxLanes> lanes{};
	for ( int i = 0; i < static_cast<int>( lanes.size() ); ++i ) {
		lanes[static_cast<size_t>( i )].clientNum = i;
		lanes[static_cast<size_t>( i )].queued = true;
		lanes[static_cast<size_t>( i )].lastPacketMs = 0;
	}
	CHECK( voice::SelectLane( lanes.data(), static_cast<int>( lanes.size() ), 63, 5000 ) == -1 );
	return true;
}

bool IdleLaneReuseHonoursGraceAndAge() {
	std::array<voice::LaneActivity, voice::kMaxLanes> lanes{};
	for ( int i = 0; i < static_cast<int>( lanes.size() ); ++i ) {
		lanes[static_cast<size_t>( i )].clientNum = i;
		lanes[static_cast<size_t>( i )].queued = false;
		lanes[static_cast<size_t>( i )].lastPacketMs = 800 + i * 10;
	}
	CHECK( voice::SelectLane( lanes.data(), static_cast<int>( lanes.size() ), 63, 1000 ) == -1 );
	lanes[2].lastPacketMs = 100;
	lanes[4].lastPacketMs = 200;
	CHECK( voice::SelectLane( lanes.data(), static_cast<int>( lanes.size() ), 63, 1000 ) == 2 );
	return true;
}

bool LaneAgeSurvivesMillisecondClockWrap() {
	std::array<voice::LaneActivity, 2> lanes{};
	lanes[0].clientNum = 1;
	lanes[0].lastPacketMs = std::numeric_limits<int>::max() - 100;
	lanes[1].clientNum = 2;
	lanes[1].lastPacketMs = std::numeric_limits<int>::max() - 50;
	const int afterWrap = std::numeric_limits<int>::min() + 600;
	CHECK( voice::ElapsedMilliseconds( afterWrap, lanes[0].lastPacketMs ) >
		voice::ElapsedMilliseconds( afterWrap, lanes[1].lastPacketMs ) );
	CHECK( voice::SelectLane( lanes.data(), static_cast<int>( lanes.size() ), 63, afterWrap ) == 0 );
	return true;
}

bool ResamplerPreservesIdentityAndEndpoints() {
	const std::array<short, 4> input = { -30000, -10000, 10000, 30000 };
	std::array<short, 8> output{};
	CHECK( voice::ResampleMonoPCM16( input.data(), 4, 22050, output.data(), 8, 22050 ) == 4 );
	CHECK( output[0] == input[0] && output[3] == input[3] );
	output.fill( 0 );
	CHECK( voice::ResampleMonoPCM16( input.data(), 4, 22050, output.data(), 8, 44100 ) == 8 );
	CHECK( output[0] == input[0] );
	CHECK( output[1] > input[0] && output[1] < input[1] );
	CHECK( output[7] == input[3] );
	return true;
}

bool ResamplerBoundsMalformedOrHugeInput() {
	std::array<short, 2> input{};
	std::array<short, 2> output{};
	CHECK( voice::ResampleMonoPCM16( nullptr, 2, 22050, output.data(), 2, 48000 ) == 0 );
	CHECK( voice::ResampleMonoPCM16( input.data(), 2, 0, output.data(), 2, 48000 ) == 0 );
	CHECK( voice::ResampledFrameCount( std::numeric_limits<int>::max(), 1,
		std::numeric_limits<int>::max() ) == std::numeric_limits<int>::max() );
	CHECK( voice::ResampleMonoPCM16( input.data(), 2, 22050, output.data(), 1, 48000 ) == 1 );
	return true;
}

struct TestCase {
	const char *name;
	bool ( *run )();
};

} // namespace

int main() {
	const TestCase tests[] = {
		{ "ExistingSpeakerKeepsItsLane", ExistingSpeakerKeepsItsLane },
		{ "BusySpeakerAudioIsNeverStolen", BusySpeakerAudioIsNeverStolen },
		{ "IdleLaneReuseHonoursGraceAndAge", IdleLaneReuseHonoursGraceAndAge },
		{ "LaneAgeSurvivesMillisecondClockWrap", LaneAgeSurvivesMillisecondClockWrap },
		{ "ResamplerPreservesIdentityAndEndpoints", ResamplerPreservesIdentityAndEndpoints },
		{ "ResamplerBoundsMalformedOrHugeInput", ResamplerBoundsMalformedOrHugeInput }
	};
	for ( const TestCase &test : tests ) {
		if ( !test.run() ) {
			return 1;
		}
	}
	std::printf( "voice queue policy tests passed (%zu cases)\n", sizeof( tests ) / sizeof( tests[0] ) );
	return 0;
}
