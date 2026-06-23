/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

extern "C" {
#include "../snd_local.h"
}

#include <algorithm>
#include <array>

namespace {

constexpr double kDaub4C0 = 0.4829629131445341;
constexpr double kDaub4C1 = 0.8365163037378079;
constexpr double kDaub4C2 = 0.2241438680420134;
constexpr double kDaub4C3 = -0.1294095225512604;
constexpr int kWaveletWorkspaceSize = 4097;
constexpr int kMuLawTableSize = 256;

/* The number of bits required by each value. */
constexpr std::array<unsigned char, kMuLawTableSize> kNumBits = {{
	0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
	6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
	8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
}};

bool madeTable = false;
[[maybe_unused]] int NXStreamCount;

void Daub4( float b[], unsigned long n, int isign ) {
	std::array<float, kWaveletWorkspaceSize> wksp{};

	auto a = [b]( unsigned long index ) -> float & {
		return b[index - 1];
	};

	if ( n < 4 ) {
		return;
	}

	unsigned long nh = n >> 1;
	const unsigned long nh1 = nh + 1;

	if ( isign >= 0 ) {
		unsigned long i = 1;
		unsigned long j = 1;
		for ( ; j <= n - 3; j += 2, i++ ) {
			wksp[i] = kDaub4C0 * a( j ) + kDaub4C1 * a( j + 1 ) + kDaub4C2 * a( j + 2 ) + kDaub4C3 * a( j + 3 );
			wksp[i + nh] = kDaub4C3 * a( j ) - kDaub4C2 * a( j + 1 ) + kDaub4C1 * a( j + 2 ) - kDaub4C0 * a( j + 3 );
		}
		wksp[i] = kDaub4C0 * a( n - 1 ) + kDaub4C1 * a( n ) + kDaub4C2 * a( 1 ) + kDaub4C3 * a( 2 );
		wksp[i + nh] = kDaub4C3 * a( n - 1 ) - kDaub4C2 * a( n ) + kDaub4C1 * a( 1 ) - kDaub4C0 * a( 2 );
	} else {
		wksp[1] = kDaub4C2 * a( nh ) + kDaub4C1 * a( n ) + kDaub4C0 * a( 1 ) + kDaub4C3 * a( nh1 );
		wksp[2] = kDaub4C3 * a( nh ) - kDaub4C0 * a( n ) + kDaub4C1 * a( 1 ) - kDaub4C2 * a( nh1 );
		for ( unsigned long i = 1, j = 3; i < nh; i++ ) {
			wksp[j++] = kDaub4C2 * a( i ) + kDaub4C1 * a( i + nh ) + kDaub4C0 * a( i + 1 ) + kDaub4C3 * a( i + nh1 );
			wksp[j++] = kDaub4C3 * a( i ) - kDaub4C0 * a( i + nh ) + kDaub4C1 * a( i + 1 ) - kDaub4C2 * a( i + nh1 );
		}
	}

	for ( unsigned long i = 1; i <= n; i++ ) {
		a( i ) = wksp[i];
	}
}

void WaveletTransform( float a[], unsigned long n, int isign ) {
	const unsigned long inverseStartLength = n / 4;
	if ( n < inverseStartLength ) {
		return;
	}

	if ( isign >= 0 ) {
		for ( unsigned long nn = n; nn >= inverseStartLength; nn >>= 1 ) {
			Daub4( a, nn, isign );
		}
	} else {
		for ( unsigned long nn = inverseStartLength; nn <= n; nn <<= 1 ) {
			Daub4( a, nn, isign );
		}
	}
}

byte MuLawEncode( short s ) {
	byte sign = ( s < 0 ) ? 0 : 0x80;

	if ( s < 0 ) {
		s = -s;
	}
	unsigned long adjusted = static_cast<unsigned long>( s ) << ( 16 - sizeof( short ) * 8 );
	adjusted += 128L + 4L;
	if ( adjusted > 32767 ) {
		adjusted = 32767;
	}

	const byte exponent = static_cast<byte>( kNumBits[( adjusted >> 7 ) & 0xff] - 1 );
	const byte mantissa = static_cast<byte>( ( adjusted >> ( exponent + 3 ) ) & 0xf );
	return static_cast<byte>( ~( sign | ( exponent << 4 ) | mantissa ) );
}

short MuLawDecode( byte uLaw ) {
	uLaw = ~uLaw;
	const byte exponent = ( uLaw >> 4 ) & 0x7;
	const byte mantissa = ( uLaw & 0xf ) + 16;
	const signed long adjusted = ( mantissa << ( exponent + 3 ) ) - 128 - 4;

	return static_cast<short>( ( uLaw & 0x80 ) ? adjusted : -adjusted );
}

void EnsureMuLawTable() {
	if ( madeTable ) {
		return;
	}

	for ( int i = 0; i < kMuLawTableSize; i++ ) {
		mulawToShort[i] = MuLawDecode( static_cast<byte>( i ) );
	}
	madeTable = true;
}

[[maybe_unused]] void NXPutc( NXStream *stream, char out ) {
	stream[NXStreamCount++] = out;
}

} // namespace

extern "C" {
short mulawToShort[256];
}

extern "C" void encodeWavelet( sfx_t *sfx, short *packets ) {
	std::array<float, kWaveletWorkspaceSize> wksp{};
	sndBuffer *chunk = nullptr;

	EnsureMuLawTable();

	int samples = sfx->soundLength;
	while ( samples > 0 ) {
		int size = samples;
		if ( size > ( SND_CHUNK_SIZE * 2 ) ) {
			size = ( SND_CHUNK_SIZE * 2 );
		}

		if ( size < 4 ) {
			size = 4;
		}

		sndBuffer *newchunk = SND_malloc();
		if ( sfx->soundData == nullptr ) {
			sfx->soundData = newchunk;
		} else if ( chunk != nullptr ) {
			chunk->next = newchunk;
		}
		chunk = newchunk;

		for ( int i = 0; i < size; i++ ) {
			wksp[i] = *packets;
			packets++;
		}
		WaveletTransform( wksp.data(), size, 1 );

		auto *out = reinterpret_cast<byte *>( chunk->sndChunk );
		for ( int i = 0; i < size; i++ ) {
			const float temp = std::clamp( wksp[i], -32768.0f, 32767.0f );
			out[i] = MuLawEncode( static_cast<short>( temp ) );
		}

		chunk->size = size;
		samples -= size;
	}
}

extern "C" void decodeWavelet( sndBuffer *chunk, short *to ) {
	std::array<float, kWaveletWorkspaceSize> wksp{};
	const int size = chunk->size;

	auto *out = reinterpret_cast<byte *>( chunk->sndChunk );
	for ( int i = 0; i < size; i++ ) {
		wksp[i] = mulawToShort[out[i]];
	}

	WaveletTransform( wksp.data(), size, -1 );

	if ( !to ) {
		return;
	}

	for ( int i = 0; i < size; i++ ) {
		to[i] = static_cast<short>( wksp[i] );
	}
}

extern "C" void encodeMuLaw( sfx_t *sfx, short *packets ) {
	sndBuffer *chunk = nullptr;
	int samples = sfx->soundLength;
	int grade = 0;

	EnsureMuLawTable();

	while ( samples > 0 ) {
		int size = samples;
		if ( size > ( SND_CHUNK_SIZE * 2 ) ) {
			size = ( SND_CHUNK_SIZE * 2 );
		}

		sndBuffer *newchunk = SND_malloc();
		if ( sfx->soundData == nullptr ) {
			sfx->soundData = newchunk;
		} else if ( chunk != nullptr ) {
			chunk->next = newchunk;
		}
		chunk = newchunk;

		auto *out = reinterpret_cast<byte *>( chunk->sndChunk );
		for ( int i = 0; i < size; i++ ) {
			int sample = packets[0] + grade;
			sample = std::clamp( sample, -32768, 32767 );
			out[i] = MuLawEncode( static_cast<short>( sample ) );
			grade = sample - mulawToShort[out[i]];
			packets++;
		}
		chunk->size = size;
		samples -= size;
	}
}

extern "C" void decodeMuLaw( sndBuffer *chunk, short *to ) {
	const int size = chunk->size;
	auto *out = reinterpret_cast<byte *>( chunk->sndChunk );

	for ( int i = 0; i < size; i++ ) {
		to[i] = mulawToShort[out[i]];
	}
}
