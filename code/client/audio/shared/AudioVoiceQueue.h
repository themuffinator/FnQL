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
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fnql_audio_voice {

// Retail QL mixes at most five simultaneous remote speakers. Keeping this
// bound avoids letting voice traffic consume the gameplay source budget.
constexpr int kMaxLanes = 5;
constexpr int kLaneSampleCapacity = 0x4000;
constexpr int kLaneReuseDelayMs = 500;

struct LaneActivity {
	int clientNum = -1;
	bool queued = false;
	int lastPacketMs = -kLaneReuseDelayMs;
};

inline void ResetLane( LaneActivity &lane ) {
	lane = LaneActivity();
}

inline std::int32_t ElapsedMilliseconds( int nowMs, int thenMs ) {
	return static_cast<std::int32_t>( static_cast<std::uint32_t>( nowMs ) -
		static_cast<std::uint32_t>( thenMs ) );
}

// Prefer a speaker's existing lane, then an unassigned idle lane, then the
// oldest idle lane after the reuse grace period. Busy lanes are never stolen.
inline int SelectLane( const LaneActivity *lanes, int laneCount, int clientNum, int nowMs ) {
	if ( lanes == nullptr || laneCount <= 0 || clientNum < 0 ) {
		return -1;
	}

	for ( int i = 0; i < laneCount; ++i ) {
		if ( lanes[i].clientNum == clientNum ) {
			return i;
		}
	}

	for ( int i = 0; i < laneCount; ++i ) {
		if ( lanes[i].clientNum < 0 && !lanes[i].queued ) {
			return i;
		}
	}

	int oldest = -1;
	std::int32_t oldestAge = -1;
	for ( int i = 0; i < laneCount; ++i ) {
		const std::int32_t age = ElapsedMilliseconds( nowMs, lanes[i].lastPacketMs );
		if ( lanes[i].queued || age < kLaneReuseDelayMs ) {
			continue;
		}
		if ( oldest < 0 || age > oldestAge ) {
			oldest = i;
			oldestAge = age;
		}
	}
	return oldest;
}

inline int ResampledFrameCount( int sourceFrames, int sourceRate, int outputRate ) {
	if ( sourceFrames <= 0 || sourceRate <= 0 || outputRate <= 0 ) {
		return 0;
	}
	const std::int64_t scaled = static_cast<std::int64_t>( sourceFrames ) * outputRate;
	const std::int64_t frames = ( scaled + sourceRate - 1 ) / sourceRate;
	return static_cast<int>( ( std::min )( frames,
		static_cast<std::int64_t>( std::numeric_limits<int>::max() ) ) );
}

// Linear conversion is used only by the legacy DMA fallback. OpenAL receives
// the original packet rate and uses the runtime's higher-quality resampler.
inline int ResampleMonoPCM16( const short *source, int sourceFrames, int sourceRate,
	short *output, int outputCapacity, int outputRate ) {
	if ( source == nullptr || output == nullptr || sourceFrames <= 0 ||
		sourceRate <= 0 || outputRate <= 0 || outputCapacity <= 0 ) {
		return 0;
	}

	const int outputFrames = ( std::min )(
		ResampledFrameCount( sourceFrames, sourceRate, outputRate ), outputCapacity );
	if ( sourceRate == outputRate ) {
		std::copy_n( source, ( std::min )( sourceFrames, outputFrames ), output );
		return ( std::min )( sourceFrames, outputFrames );
	}

	for ( int i = 0; i < outputFrames; ++i ) {
		const std::int64_t numerator = static_cast<std::int64_t>( i ) * sourceRate;
		const int base = static_cast<int>( numerator / outputRate );
		const int next = ( std::min )( base + 1, sourceFrames - 1 );
		const std::int64_t remainder = numerator % outputRate;
		const std::int64_t firstWeight = outputRate - remainder;
		const std::int64_t mixed =
			static_cast<std::int64_t>( source[( std::min )( base, sourceFrames - 1 )] ) * firstWeight +
			static_cast<std::int64_t>( source[next] ) * remainder;
		output[i] = static_cast<short>( mixed / outputRate );
	}
	return outputFrames;
}

} // namespace fnql_audio_voice
