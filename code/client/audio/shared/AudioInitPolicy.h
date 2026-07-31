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

#ifndef FNQL_AUDIO_INIT_POLICY_H
#define FNQL_AUDIO_INIT_POLICY_H

#include <cstddef>
#include <limits>

namespace fnql_audio_init {

inline bool DmaBufferByteCount( int samples, int sampleBits, std::size_t &byteCount )
{
	byteCount = 0;
	if ( samples <= 0 || ( sampleBits != 8 && sampleBits != 16 && sampleBits != 32 ) ) {
		return false;
	}

	const std::size_t bytesPerSample = static_cast<std::size_t>( sampleBits / 8 );
	const std::size_t sampleCount = static_cast<std::size_t>( samples );
	if ( sampleCount > ( std::numeric_limits<std::size_t>::max )() / bytesPerSample ) {
		return false;
	}

	byteCount = sampleCount * bytesPerSample;
	return true;
}

inline bool DmaConfigurationIsUsable(
	int channels,
	int sampleBits,
	bool isFloat,
	int speed,
	int samples,
	int fullSamples,
	std::size_t &byteCount )
{
	byteCount = 0;
	if ( channels < 1 || channels > 2 || speed <= 0 || samples <= 0 ||
		fullSamples <= 0 || samples % channels != 0 ||
		fullSamples != samples / channels ) {
		return false;
	}

	if ( isFloat ) {
		if ( sampleBits != 32 ) {
			return false;
		}
	} else if ( sampleBits != 8 && sampleBits != 16 ) {
		return false;
	}

	return DmaBufferByteCount( samples, sampleBits, byteCount );
}

inline bool ScaleFrameCount(
	int frames,
	int sourceRate,
	int targetRate,
	int maximumFrames,
	int &scaledFrames )
{
	scaledFrames = 0;
	if ( frames <= 0 || sourceRate <= 0 || targetRate <= 0 || maximumFrames <= 0 ) {
		return false;
	}

	const long long numerator =
		static_cast<long long>( frames ) * static_cast<long long>( targetRate );
	const long long converted =
		( numerator + static_cast<long long>( sourceRate ) - 1 ) /
		static_cast<long long>( sourceRate );
	if ( converted <= 0 ) {
		return false;
	}

	scaledFrames = static_cast<int>(
		converted > maximumFrames ? maximumFrames : converted );
	return true;
}

} // namespace fnql_audio_init

#endif // FNQL_AUDIO_INIT_POLICY_H
