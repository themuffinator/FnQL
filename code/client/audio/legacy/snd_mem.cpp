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

/*****************************************************************************
 * name:		snd_mem.c
 *
 * desc:		sound caching
 *
 * $Archive: /MissionPack/code/client/snd_mem.c $
 *
 *****************************************************************************/

extern "C" {
#include "../snd_local.h"
#include "../codecs/snd_codec.h"
}

#include "../../client_cpp.h"

#include <algorithm>
#include <memory>
#include <new>

using fnql::ReadUnaligned;
using fnql::ScopedTempMemory;
using fnql::WriteUnaligned;

namespace {

constexpr char kDefaultComSoundMegs[] = "8";

sndBuffer *buffer = nullptr;
std::unique_ptr<sndBuffer[]> bufferStorage;
sndBuffer *freelist = nullptr;
int inUse = 0;
int totalInUse = 0;
std::unique_ptr<short[]> sfxScratchStorage;

sndBuffer *GetFreeLink( const sndBuffer *chunk ) {
	return ReadUnaligned<sndBuffer *>( chunk );
}

void SetFreeLink( sndBuffer *chunk, sndBuffer *next ) {
	WriteUnaligned( chunk, next );
}

/*
================
ResampleSfx

resample / decimate to the current source rate
================
*/
int ResampleSfx( sfx_t *sfx, int channels, int inrate, int inwidth, int samples, byte *data, qboolean compressed ) {
	static_cast<void>( compressed );

	const float stepscale = static_cast<float>( inrate ) / dma.speed; // this is usually 0.5, 1, or 2
	const int outcount = static_cast<int>( samples / stepscale );
	int srcsample = 0;
	int samplefrac = 0;
	const int fracstep = static_cast<int>( stepscale * 256 * channels );
	sndBuffer *chunk = sfx->soundData;

	for ( int i = 0; i < outcount; i++ ) {
		srcsample += samplefrac >> 8;
		samplefrac &= 255;
		samplefrac += fracstep;
		for ( int j = 0; j < channels; j++ ) {
			int sample;
			if ( inwidth == 2 ) {
				sample = reinterpret_cast<const short *>( data )[srcsample + j];
			} else {
				sample = ( static_cast<int>( static_cast<unsigned char>( data[srcsample + j] ) ) - 128 ) << 8;
			}

			const int part = ( i * channels + j ) & ( SND_CHUNK_SIZE - 1 );
			if ( part == 0 ) {
				sndBuffer *newchunk = SND_malloc();
				if ( chunk == nullptr ) {
					sfx->soundData = newchunk;
				} else {
					chunk->next = newchunk;
				}
				chunk = newchunk;
			}

			chunk->sndChunk[part] = static_cast<short>( sample );
		}
	}

	return outcount;
}

/*
================
ResampleSfxRaw

resample / decimate to the current source rate
================
*/
int ResampleSfxRaw( short *sfx, int channels, int inrate, int inwidth, int samples, byte *data ) {
	const float stepscale = static_cast<float>( inrate ) / dma.speed; // this is usually 0.5, 1, or 2
	const int outcount = static_cast<int>( samples / stepscale );
	int srcsample = 0;
	int samplefrac = 0;
	const int fracstep = static_cast<int>( stepscale * 256 * channels );

	for ( int i = 0; i < outcount; i++ ) {
		srcsample += samplefrac >> 8;
		samplefrac &= 255;
		samplefrac += fracstep;
		for ( int j = 0; j < channels; j++ ) {
			int sample;
			if ( inwidth == 2 ) {
				sample = LittleShort( reinterpret_cast<const short *>( data )[srcsample + j] );
			} else {
				sample = ( static_cast<int>( static_cast<unsigned char>( data[srcsample + j] ) ) - 128 ) << 8;
			}
			sfx[i * channels + j] = static_cast<short>( sample );
		}
	}
	return outcount;
}

} // namespace

extern "C" {
short *sfxScratchBuffer = nullptr;
sfx_t *sfxScratchPointer = nullptr;
int sfxScratchIndex = 0;
}

extern "C" void SND_free( sndBuffer *v ) {
	SetFreeLink( v, freelist );
	freelist = v;
	inUse += sizeof( sndBuffer );
	totalInUse -= sizeof( sndBuffer ); // -EC-
}

extern "C" sndBuffer *SND_malloc( void ) {
	while ( freelist == nullptr ) {
		S_FreeOldestSound();
	}

	inUse -= sizeof( sndBuffer );
	totalInUse += sizeof( sndBuffer );

	sndBuffer *v = freelist;
	freelist = GetFreeLink( freelist );
	v->next = nullptr;
	return v;
}

extern "C" void SND_setup( void ) {
	cvar_t *cv;
	int scs, sz;
	static int old_scs = -1;

	cv = Cvar_Get( "com_soundMegs", kDefaultComSoundMegs, CVAR_LATCH | CVAR_ARCHIVE );
	Cvar_CheckRange( cv, "1", "512", CV_INTEGER );
	Cvar_SetDescription( cv, "Amount of memory (RAM) assigned to the sound buffer (in MB)." );

	scs = ( cv->integer * 12 * dma.speed ) / 22050;
	scs *= 128;

	sz = scs * sizeof( sndBuffer );

	if ( old_scs != scs ) {
		bufferStorage.reset();
		buffer = nullptr;
		old_scs = scs;
	}

	if ( buffer == nullptr ) {
		bufferStorage.reset( new ( std::nothrow ) sndBuffer[ scs ] );
		buffer = bufferStorage.get();
	}

	if ( buffer == nullptr ) {
		Com_Error( ERR_FATAL, "Error allocating %i bytes for sound buffer", sz );
	} else {
		std::fill_n( buffer, scs, sndBuffer{} );
	}

	const int scratchSamples = SND_CHUNK_SIZE * 4;
	sz = scratchSamples * sizeof( short );

	if ( sfxScratchBuffer == nullptr ) {
		sfxScratchStorage.reset( new ( std::nothrow ) short[ scratchSamples ] );
		sfxScratchBuffer = sfxScratchStorage.get();
	}

	if ( sfxScratchBuffer == nullptr ) {
		Com_Error( ERR_FATAL, "Error allocating %i bytes for sfxScratchBuffer", sz );
	} else {
		std::fill_n( sfxScratchBuffer, scratchSamples, 0 );
	}

	sfxScratchPointer = nullptr;

	inUse = scs * sizeof( sndBuffer );
	totalInUse = 0; // -EC-

	sndBuffer *p = buffer;
	sndBuffer *q = p + scs;
	while ( --q > p ) {
		SetFreeLink( q, q - 1 );
	}

	SetFreeLink( q, nullptr );
	freelist = p + scs - 1;

	Com_Printf( "Sound memory manager started\n" );
}

extern "C" void SND_shutdown( void ) {
	sfxScratchStorage.reset();
	sfxScratchBuffer = nullptr;
	bufferStorage.reset();
	buffer = nullptr;
}

//=============================================================================

/*
==============
S_LoadSound

The filename may be different than sfx->name in the case
of a forced fallback of a player specific sound
==============
*/
extern "C" qboolean S_LoadSound( sfx_t *sfx ) {
	snd_info_t info;

	ScopedTempMemory dataStorage( S_CodecLoad( sfx->soundName, &info ) );
	auto *data = dataStorage.as<byte>();
	if ( !data ) {
		return qfalse;
	}

	if ( info.width == 1 ) {
		Com_DPrintf( S_COLOR_YELLOW "WARNING: %s is a 8 bit audio file\n", sfx->soundName );
	}

	if ( info.rate != 22050 ) {
		Com_DPrintf( S_COLOR_YELLOW "WARNING: %s is not a 22kHz audio file\n", sfx->soundName );
	}

	ScopedTempMemory sampleStorage = ScopedTempMemory::Allocate( info.samples * sizeof( short ) * 2 );
	auto *samples = sampleStorage.as<short>();

	sfx->lastTimeUsed = s_soundtime + 1; // Com_Milliseconds()+1

	// Each of these compression schemes works, but 16-bit quality is nicer and
	// with a local install we can rely on the sound memory manager to page sound.
	if ( info.channels == 1 && sfx->soundCompressed == qtrue ) {
		sfx->soundCompressionMethod = 1;
		sfx->soundData = nullptr;
		sfx->soundLength = ResampleSfxRaw( samples, info.channels, info.rate, info.width, info.samples, data + info.dataofs );
		S_AdpcmEncodeSound( sfx, samples );
#if 0
	} else if ( info.channels == 1 && info.samples > ( SND_CHUNK_SIZE * 16 ) && info.width > 1 ) {
		sfx->soundCompressionMethod = 3;
		sfx->soundData = nullptr;
		sfx->soundLength = ResampleSfxRaw( samples, info.channels, info.rate, info.width, info.samples, data + info.dataofs );
		encodeMuLaw( sfx, samples );
	} else if ( info.channels == 1 && info.samples > ( SND_CHUNK_SIZE * 6400 ) && info.width > 1 ) {
		sfx->soundCompressionMethod = 2;
		sfx->soundData = nullptr;
		sfx->soundLength = ResampleSfxRaw( samples, info.channels, info.rate, info.width, info.samples, data + info.dataofs );
		encodeWavelet( sfx, samples );
#endif
	} else {
		sfx->soundCompressionMethod = 0;
		sfx->soundData = nullptr;
		sfx->soundLength = ResampleSfx( sfx, info.channels, info.rate, info.width, info.samples, data + info.dataofs, qfalse );
	}

	sfx->soundChannels = info.channels;

	return qtrue;
}

extern "C" void S_DisplayFreeMemory( void ) {
	Com_Printf( "%d bytes free sound buffer memory, %d total used\n", inUse, totalInUse );
}
