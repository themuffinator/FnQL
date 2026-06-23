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

#ifndef SDL_FUNCTION_POINTER_IS_VOID_POINTER
#define SDL_FUNCTION_POINTER_IS_VOID_POINTER 1
#endif

#include <SDL3/SDL.h>

#include "../qcommon/q_shared.h"
#include "../client/audio/snd_local.h"
#include "../client/client.h"
#include "sdl_raii.h"

qboolean snd_inited = qfalse;

extern cvar_t *s_khz;
cvar_t *s_sdlBits;
cvar_t *s_sdlChannels;
cvar_t *s_sdlDevSamps;
cvar_t *s_sdlMixSamps;

static int dmapos = 0;
static int dmasize = 0;
static fnql::sdl::ScopedAudioStream sdlPlaybackStream;
static fnql::sdl::ScopedMutex sdlAudioMutex;

#if defined USE_VOIP
#define USE_SDL_AUDIO_CAPTURE

static fnql::sdl::ScopedAudioStream sdlCaptureStream;
static cvar_t *s_sdlCapture;
static float sdlMasterGain = 1.0f;
#endif

static const struct
{
	uint16_t enumFormat;
	const char *stringFormat;
} formatToStringTable[] =
{
	{ SDL_AUDIO_U8,     "SDL_AUDIO_U8" },
	{ SDL_AUDIO_S8,     "SDL_AUDIO_S8" },
	{ SDL_AUDIO_S16LE,  "SDL_AUDIO_S16LE" },
	{ SDL_AUDIO_S16BE,  "SDL_AUDIO_S16BE" },
	{ SDL_AUDIO_F32LE,  "SDL_AUDIO_F32LE" },
	{ SDL_AUDIO_F32BE,  "SDL_AUDIO_F32BE" }
};

static const int formatToStringTableSize = ARRAY_LEN( formatToStringTable );

/*
===============
SNDDMA_DefaultDeviceSamples
===============
*/
static int SNDDMA_DefaultDeviceSamples( int freq )
{
	if ( freq <= 11025 )
		return 256;
	if ( freq <= 22050 )
		return 512;
	if ( freq <= 44100 )
		return 1024;
	return 2048;
}

/*
===============
SNDDMA_SetSampleFramesHint
===============
*/
static void SNDDMA_SetSampleFramesHint( int sampleFrames )
{
	char value[16];

	Com_sprintf( value, sizeof( value ), "%d", sampleFrames );
	SDL_SetHint( SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, value );
}

/*
===============
SNDDMA_QueueSilence
===============
*/
static void SNDDMA_QueueSilence( SDL_AudioStream *stream, int len )
{
	Uint8 silence[4096];

	SDL_memset( silence, 0, sizeof( silence ) );

	while ( len > 0 )
	{
		int chunk = len;

		if ( chunk > (int) sizeof( silence ) )
			chunk = sizeof( silence );

		if ( !SDL_PutAudioStreamData( stream, silence, chunk ) )
			return;

		len -= chunk;
	}
}

#ifdef USE_SDL_AUDIO_CAPTURE
/*
===============
SNDDMA_ApplyMasterGain
===============
*/
static void SNDDMA_ApplyMasterGain( Uint8 *stream, int len, float gain )
{
	if ( gain == 1.0f )
		return;

	if ( dma.isfloat && dma.samplebits == 32 )
	{
		float *ptr = (float *) stream;
		const int count = len / (int) sizeof( *ptr );
		int i;

		for ( i = 0; i < count; i++, ptr++ )
			*ptr *= gain;
	}
	else if ( dma.samplebits == 16 )
	{
		Sint16 *ptr = (Sint16 *) stream;
		const int count = len / (int) sizeof( *ptr );
		int i;

		for ( i = 0; i < count; i++, ptr++ )
			*ptr = (Sint16) ( ( (float) *ptr ) * gain );
	}
	else if ( dma.samplebits == 8 )
	{
		Uint8 *ptr = (Uint8 *) stream;
		const int count = len / (int) sizeof( *ptr );
		int i;

		for ( i = 0; i < count; i++, ptr++ )
			*ptr = (Uint8) ( ( (float) *ptr ) * gain );
	}
}
#endif

/*
===============
SNDDMA_AudioCallback
===============
*/
static void SNDDMA_AudioCallback( void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount )
{
	Uint8 mixbuf[4096];
	int bytesPerSample;
	int bytesPerFrame;

	(void) userdata;
	(void) total_amount;

	if ( additional_amount <= 0 )
		return;

	bytesPerSample = dma.samplebits / 8;
	bytesPerFrame = bytesPerSample * (int) dma.channels;

	if ( !snd_inited || !sdlAudioMutex || !dma.buffer || bytesPerSample <= 0 || bytesPerFrame <= 0 || dmasize <= 0 )
	{
		SNDDMA_QueueSilence( stream, additional_amount );
		return;
	}

	while ( additional_amount > 0 )
	{
		int chunk = additional_amount;
		int remaining;
		int dstOffset = 0;
#ifdef USE_SDL_AUDIO_CAPTURE
		float masterGain;
#endif

		if ( chunk > (int) sizeof( mixbuf ) )
			chunk = sizeof( mixbuf );

		chunk -= chunk % bytesPerFrame;
		if ( chunk <= 0 )
			chunk = bytesPerFrame;
		if ( chunk > additional_amount )
			chunk = additional_amount;

		remaining = chunk;

		{
			fnql::sdl::ScopedMutexLock lock( sdlAudioMutex.get() );
#ifdef USE_SDL_AUDIO_CAPTURE
			masterGain = sdlMasterGain;
#endif

			while ( remaining > 0 )
			{
				const int pos = dmapos * bytesPerSample;
				int copy;

				if ( dmapos >= dma.samples )
				{
					dmapos = 0;
					continue;
				}

				copy = dmasize - pos;
				if ( copy > remaining )
					copy = remaining;

				memcpy( mixbuf + dstOffset, dma.buffer + pos, copy );

				dmapos += copy / bytesPerSample;
				if ( dmapos >= dma.samples )
					dmapos = 0;

				dstOffset += copy;
				remaining -= copy;
			}
		}

#ifdef USE_SDL_AUDIO_CAPTURE
		SNDDMA_ApplyMasterGain( mixbuf, chunk, masterGain );
#endif

		if ( !SDL_PutAudioStreamData( stream, mixbuf, chunk ) )
			return;

		additional_amount -= chunk;
	}
}

/*
===============
SNDDMA_PrintAudiospec
===============
*/
static void SNDDMA_PrintAudiospec( const char *str, const SDL_AudioSpec *spec, int sampleFrames )
{
	const char *fmt = NULL;
	int i;

	Com_Printf( "%s:\n", str );

	for ( i = 0; i < formatToStringTableSize; i++ )
	{
		if ( spec->format == formatToStringTable[i].enumFormat )
			fmt = formatToStringTable[i].stringFormat;
	}

	if ( fmt )
		Com_Printf( "  Format:   %s\n", fmt );
	else
		Com_Printf( "  Format:   " S_COLOR_RED "UNKNOWN\n" );

	Com_Printf( "  Freq:     %d\n", (int) spec->freq );
	Com_Printf( "  Frames:   %d\n", sampleFrames );
	Com_Printf( "  Channels: %d\n", (int) spec->channels );
}


static int SNDDMA_KHzToHz( int khz )
{
	switch ( khz )
	{
		default:
		case 22: return 22050;
		case 48: return 48000;
		case 44: return 44100;
		case 11: return 11025;
		case  8: return  8000;
	}
}


/*
===============
SNDDMA_Init
===============
*/
qboolean SNDDMA_Init( void )
{
	SDL_AudioSpec desired;
	SDL_AudioSpec obtained;
	SDL_AudioDeviceID playbackDevice;
	const char *audioDriver;
	int sampleFrames;
	int tmp;
	fnql::sdl::ScopedAudioStream playbackStream;
	fnql::sdl::ScopedMutex audioMutex;
	fnql::sdl::ScopedMemory<byte> dmaBuffer;
#ifdef USE_SDL_AUDIO_CAPTURE
	fnql::sdl::ScopedAudioStream captureStream;
#endif

	if ( snd_inited )
		return qtrue;

	s_sdlBits = Cvar_Get( "s_sdlBits", "16", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( s_sdlBits, "8", "16", CV_INTEGER );
	Cvar_SetDescription( s_sdlBits, "Bits per-sample to request for SDL3 audio output (possible options: 8 or 16). When set to 0 it uses 16." );

	s_sdlChannels = Cvar_Get( "s_sdlChannels", "2", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( s_sdlChannels, "1", "2", CV_INTEGER );
	Cvar_SetDescription( s_sdlChannels, "Number of audio channels to request for SDL3 audio output. The Quake 3 audio mixer only supports mono and stereo. Additional channels are silent." );

	s_sdlDevSamps = Cvar_Get( "s_sdlDevSamps", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_SetDescription( s_sdlDevSamps, "Number of sample frames to provide to the SDL3 audio output device. When set to 0 it picks a value based on s_khz." );

	s_sdlMixSamps = Cvar_Get( "s_sdlMixSamps", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_SetDescription( s_sdlMixSamps, "Number of audio samples for Quake 3's audio mixer when using SDL3 audio output." );

	Com_Printf( "SDL_InitSubSystem( SDL_INIT_AUDIO )... " );

	if ( !SDL_InitSubSystem( SDL_INIT_AUDIO ) )
	{
		Com_Printf( "FAILED (%s)\n", SDL_GetError() );
		return qfalse;
	}

	Com_Printf( "OK\n" );
	fnql::sdl::ScopedSubSystem audioSubsystem( SDL_INIT_AUDIO, true );

	audioDriver = SDL_GetCurrentAudioDriver();
	Com_Printf( "SDL audio driver is \"%s\".\n", audioDriver ? audioDriver : "unknown" );

	SDL_zero( desired );
	SDL_zero( obtained );

	desired.freq = SNDDMA_KHzToHz( s_khz->integer );
	if ( desired.freq == 0 )
		desired.freq = 22050;

	tmp = s_sdlBits->integer;
	if ( tmp < 16 )
		tmp = 8;

	desired.format = ( tmp == 16 ) ? SDL_AUDIO_S16 : SDL_AUDIO_U8;
	desired.channels = s_sdlChannels->integer;

	sampleFrames = s_sdlDevSamps->integer ? s_sdlDevSamps->integer : SNDDMA_DefaultDeviceSamples( desired.freq );
	SNDDMA_SetSampleFramesHint( sampleFrames );

	playbackStream.reset( SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, SNDDMA_AudioCallback, NULL ) );
	if ( !playbackStream )
	{
		Com_Printf( "SDL_OpenAudioDeviceStream() failed: %s\n", SDL_GetError() );
		return qfalse;
	}

	playbackDevice = SDL_GetAudioStreamDevice( playbackStream.get() );
	if ( playbackDevice == 0 || !SDL_GetAudioDeviceFormat( playbackDevice, &obtained, &sampleFrames ) )
	{
		Com_Printf( "SDL_GetAudioDeviceFormat() failed: %s\n", SDL_GetError() );
		return qfalse;
	}

	SNDDMA_PrintAudiospec( "SDL_AudioSpec", &obtained, sampleFrames );

	tmp = s_sdlMixSamps->integer;
	if ( !tmp )
		tmp = ( sampleFrames * obtained.channels ) * 10;

	tmp -= tmp % obtained.channels;
	tmp = log2pad( tmp, 1 );

	audioMutex.reset( SDL_CreateMutex() );
	dmapos = 0;
	dma.samplebits = SDL_AUDIO_BITSIZE( obtained.format );
	dma.isfloat = SDL_AUDIO_ISFLOAT( obtained.format );
	dma.channels = obtained.channels;
	dma.samples = tmp;
	dma.fullsamples = dma.samples / dma.channels;
	dma.submission_chunk = 1;
	dma.speed = obtained.freq;
	dma.driver = "SDL3";
	dmasize = dma.samples * ( dma.samplebits / 8 );
	dmaBuffer.reset( static_cast<byte *>( calloc( 1, dmasize ) ) );

	if ( !audioMutex || !dmaBuffer )
	{
		Com_Printf( "Failed to allocate SDL audio state.\n" );

		dma.buffer = NULL;
		dma.driver = NULL;
		dmapos = dmasize = 0;
		return qfalse;
	}

#ifdef USE_SDL_AUDIO_CAPTURE
	s_sdlCapture = Cvar_Get( "s_sdlCapture", "1", CVAR_ARCHIVE | CVAR_LATCH );
	Cvar_SetDescription( s_sdlCapture, "Set to 1 to enable SDL3 audio capture." );

	if ( audioDriver && Q_stricmp( audioDriver, "pulseaudio" ) == 0 )
	{
		Com_Printf( "SDL audio capture support disabled for pulseaudio (https://bugzilla.libsdl.org/show_bug.cgi?id=4087)\n" );
	}
	else if ( !s_sdlCapture->integer )
	{
		Com_Printf( "SDL audio capture support disabled by user ('+set s_sdlCapture 1' to enable)\n" );
	}
#if USE_MUMBLE
	else if ( cl_useMumble->integer )
	{
		Com_Printf( "SDL audio capture support disabled for Mumble support\n" );
	}
#endif
	else
	{
		SDL_AudioSpec spec;

		SDL_zero( spec );
		spec.freq = 48000;
		spec.format = SDL_AUDIO_S16;
		spec.channels = 1;

		captureStream.reset( SDL_OpenAudioDeviceStream( SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, NULL, NULL ) );
		Com_Printf( "SDL capture device %s.\n", captureStream ? "opened" : "failed to open" );
	}

	sdlMasterGain = 1.0f;
#endif

	sdlPlaybackStream = std::move( playbackStream );
	sdlAudioMutex = std::move( audioMutex );
	dma.buffer = dmaBuffer.release();
#ifdef USE_SDL_AUDIO_CAPTURE
	sdlCaptureStream = std::move( captureStream );
#endif
	audioSubsystem.release();

	Com_Printf( "Starting SDL audio callback...\n" );
	if ( !SDL_ResumeAudioStreamDevice( sdlPlaybackStream.get() ) )
	{
		Com_Printf( "SDL_ResumeAudioStreamDevice() failed: %s\n", SDL_GetError() );
		SNDDMA_Shutdown();
		return qfalse;
	}

	Com_Printf( "SDL audio initialized.\n" );
	snd_inited = qtrue;
	return qtrue;
}


/*
===============
SNDDMA_GetDMAPos
===============
*/
int SNDDMA_GetDMAPos( void )
{
	int pos;

	fnql::sdl::ScopedMutexLock lock( sdlAudioMutex.get() );

	if ( dmapos >= dma.samples )
		dmapos = 0;
	pos = dmapos;

	return pos;
}


/*
===============
SNDDMA_Shutdown
===============
*/
void SNDDMA_Shutdown( void )
{
	snd_inited = qfalse;

	if ( sdlPlaybackStream )
	{
		Com_Printf( "Closing SDL audio playback device...\n" );
		sdlPlaybackStream.reset();
		Com_Printf( "SDL audio playback device closed.\n" );
	}

#ifdef USE_SDL_AUDIO_CAPTURE
	if ( sdlCaptureStream )
	{
		Com_Printf( "Closing SDL audio capture device...\n" );
		sdlCaptureStream.reset();
		Com_Printf( "SDL audio capture device closed.\n" );
	}
#endif

	sdlAudioMutex.reset();

	SDL_QuitSubSystem( SDL_INIT_AUDIO );
	std::free( dma.buffer );
	dma.buffer = NULL;
	dma.driver = NULL;
	dmapos = 0;
	dmasize = 0;
	Com_Printf( "SDL audio shut down.\n" );
}


/*
===============
SNDDMA_Submit

Send sound to device if buffer isn't really the dma buffer
===============
*/
void SNDDMA_Submit( void )
{
	if ( sdlAudioMutex )
		SDL_UnlockMutex( sdlAudioMutex.get() );
}


/*
===============
SNDDMA_BeginPainting
===============
*/
void SNDDMA_BeginPainting( void )
{
	if ( sdlAudioMutex )
		SDL_LockMutex( sdlAudioMutex.get() );
}


#ifdef USE_VOIP
void SNDDMA_StartCapture( void )
{
#ifdef USE_SDL_AUDIO_CAPTURE
	if ( sdlCaptureStream )
	{
		SDL_ClearAudioStream( sdlCaptureStream.get() );
		SDL_ResumeAudioStreamDevice( sdlCaptureStream.get() );
	}
#endif
}


int SNDDMA_AvailableCaptureSamples( void )
{
#ifdef USE_SDL_AUDIO_CAPTURE
	if ( sdlCaptureStream )
	{
		const int available = SDL_GetAudioStreamAvailable( sdlCaptureStream.get() );
		return available > 0 ? ( available / 2 ) : 0;
	}
#endif
	return 0;
}


void SNDDMA_Capture( int samples, byte *data )
{
#ifdef USE_SDL_AUDIO_CAPTURE
	if ( sdlCaptureStream )
	{
		const int requested = samples * 2;
		const int received = SDL_GetAudioStreamData( sdlCaptureStream.get(), data, requested );
		const int offset = MAX( received, 0 );

		if ( offset < requested )
			SDL_memset( data + offset, '\0', requested - offset );

		return;
	}
#endif

	SDL_memset( data, '\0', samples * 2 );
}

void SNDDMA_StopCapture( void )
{
#ifdef USE_SDL_AUDIO_CAPTURE
	if ( sdlCaptureStream )
		SDL_PauseAudioStreamDevice( sdlCaptureStream.get() );
#endif
}

void SNDDMA_MasterGain( float val )
{
#ifdef USE_SDL_AUDIO_CAPTURE
	fnql::sdl::ScopedMutexLock lock( sdlAudioMutex.get() );

	sdlMasterGain = val;
#else
	(void) val;
#endif
}
#endif
