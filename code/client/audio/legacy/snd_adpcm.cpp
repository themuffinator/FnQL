/***********************************************************
Copyright 1992 by Stichting Mathematisch Centrum, Amsterdam, The
Netherlands.

                        All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted,
provided that the above copyright notice appear in all copies and that
both that copyright notice and this permission notice appear in
supporting documentation, and that the names of Stichting Mathematisch
Centrum or CWI not be used in advertising or publicity pertaining to
distribution of the software without specific, written prior permission.

STICHTING MATHEMATISCH CENTRUM DISCLAIMS ALL WARRANTIES WITH REGARD TO
THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS, IN NO EVENT SHALL STICHTING MATHEMATISCH CENTRUM BE LIABLE
FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

******************************************************************/

/*
** Intel/DVI ADPCM coder/decoder.
**
** The algorithm for this coder was taken from the IMA Compatibility Project
** proceedings, Vol 2, Number 2; May 1992.
**
** Version 1.2, 18-Dec-92.
*/

extern "C" {
#include "../snd_local.h"
}

#include <algorithm>
#include <array>

namespace {

/* Intel ADPCM step variation table */
constexpr std::array<int, 16> kIndexTable = {{
	-1, -1, -1, -1, 2, 4, 6, 8,
	-1, -1, -1, -1, 2, 4, 6, 8,
}};

constexpr std::array<int, 89> kStepSizeTable = {{
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
	19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
	50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
	130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
	337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
	876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
	2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
	5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
}};

constexpr int kMinSample = -32768;
constexpr int kMaxSample = 32767;
constexpr int kMaxIndex = static_cast<int>( kStepSizeTable.size() ) - 1;

void AdpcmEncode( const short indata[], char outdata[], int len, adpcm_state_t *state ) {
	const short *inp = indata;
	auto *outp = reinterpret_cast<unsigned char *>( outdata );
	int valpred = state->sample;
	int index = state->index;
	int step = kStepSizeTable[index];
	int outputbuffer = 0;
	int bufferstep = 1;

	for ( ; len > 0; len-- ) {
		const int val = *inp++;

		int diff = val - valpred;
		const int sign = ( diff < 0 ) ? 8 : 0;
		if ( sign ) {
			diff = -diff;
		}

		int delta = 0;
		int vpdiff = step >> 3;

		if ( diff >= step ) {
			delta = 4;
			diff -= step;
			vpdiff += step;
		}
		step >>= 1;
		if ( diff >= step ) {
			delta |= 2;
			diff -= step;
			vpdiff += step;
		}
		step >>= 1;
		if ( diff >= step ) {
			delta |= 1;
			vpdiff += step;
		}

		valpred += sign ? -vpdiff : vpdiff;
		valpred = std::clamp( valpred, kMinSample, kMaxSample );

		delta |= sign;

		index += kIndexTable[delta];
		index = std::clamp( index, 0, kMaxIndex );
		step = kStepSizeTable[index];

		if ( bufferstep ) {
			outputbuffer = ( delta << 4 ) & 0xf0;
		} else {
			*outp++ = static_cast<unsigned char>( ( delta & 0x0f ) | outputbuffer );
		}
		bufferstep = !bufferstep;
	}

	if ( !bufferstep ) {
		*outp++ = static_cast<unsigned char>( outputbuffer );
	}

	state->sample = static_cast<short>( valpred );
	state->index = static_cast<char>( index );
}

void AdpcmDecode( const char indata[], short *outdata, int len, adpcm_state_t *state ) {
	const auto *inp = reinterpret_cast<const unsigned char *>( indata );
	int outp = 0;
	int valpred = state->sample;
	int index = state->index;
	int step = kStepSizeTable[index];
	int inputbuffer = 0;
	int bufferstep = 0;

	for ( ; len > 0; len-- ) {
		int delta;

		if ( bufferstep ) {
			delta = inputbuffer & 0xf;
		} else {
			inputbuffer = *inp++;
			delta = ( inputbuffer >> 4 ) & 0xf;
		}
		bufferstep = !bufferstep;

		index += kIndexTable[delta];
		index = std::clamp( index, 0, kMaxIndex );

		const int sign = delta & 8;
		delta &= 7;

		int vpdiff = step >> 3;
		if ( delta & 4 ) {
			vpdiff += step;
		}
		if ( delta & 2 ) {
			vpdiff += step >> 1;
		}
		if ( delta & 1 ) {
			vpdiff += step >> 2;
		}

		valpred += sign ? -vpdiff : vpdiff;
		valpred = std::clamp( valpred, kMinSample, kMaxSample );

		step = kStepSizeTable[index];

		outdata[outp++] = static_cast<short>( valpred );
	}

	state->sample = static_cast<short>( valpred );
	state->index = static_cast<char>( index );
}

} // namespace

/*
====================
S_AdpcmMemoryNeeded

Returns the amount of memory (in bytes) needed to store the samples in our internal ADPCM format.
====================
*/
extern "C" int S_AdpcmMemoryNeeded( const wavinfo_t *info ) {
	const float scale = static_cast<float>( info->rate ) / dma.speed;
	const int scaledSampleCount = static_cast<int>( info->samples / scale );
	const int sampleMemory = scaledSampleCount / 2;

	int blockCount = scaledSampleCount / PAINTBUFFER_SIZE;
	if ( scaledSampleCount % PAINTBUFFER_SIZE ) {
		blockCount++;
	}

	const int headerMemory = blockCount * sizeof( adpcm_state_t );

	return sampleMemory + headerMemory;
}

/*
====================
S_AdpcmGetSamples
====================
*/
extern "C" void S_AdpcmGetSamples( sndBuffer *chunk, short *to ) {
	adpcm_state_t state;

	state.index = chunk->adpcm.index;
	state.sample = chunk->adpcm.sample;

	const auto *out = reinterpret_cast<const char *>( chunk->sndChunk );
	AdpcmDecode( out, to, SND_CHUNK_SIZE_BYTE * 2, &state );
}

/*
====================
S_AdpcmEncodeSound
====================
*/
extern "C" void S_AdpcmEncodeSound( sfx_t *sfx, short *samples ) {
	adpcm_state_t state;
	int inOffset = 0;
	int count = sfx->soundLength;
	sndBuffer *chunk = nullptr;

	state.index = 0;
	state.sample = samples[0];

	while ( count ) {
		int n = count;
		if ( n > SND_CHUNK_SIZE_BYTE * 2 ) {
			n = SND_CHUNK_SIZE_BYTE * 2;
		}

		sndBuffer *newchunk = SND_malloc();
		if ( sfx->soundData == nullptr ) {
			sfx->soundData = newchunk;
		} else if ( chunk != nullptr ) {
			chunk->next = newchunk;
		}
		chunk = newchunk;

		chunk->adpcm.index = state.index;
		chunk->adpcm.sample = state.sample;

		auto *out = reinterpret_cast<char *>( chunk->sndChunk );
		AdpcmEncode( samples + inOffset, out, n, &state );

		inOffset += n;
		count -= n;
	}
}
