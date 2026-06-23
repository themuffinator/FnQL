/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2005 Stuart Dalton (badcdev@gmail.com)

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
#include "../client.h"
#include "codecs/snd_codec.h"
#include "snd_local.h"
#include "snd_public.h"
}

#include "../client_cpp.h"

#include <cstdlib>

using fnql::ToQboolean;

extern "C" {
cvar_t *s_volume;
cvar_t *s_musicVolume;
cvar_t *s_doppler;
cvar_t *s_muteWhenMinimized;
cvar_t *s_muteWhenUnfocused;
cvar_t *s_backend;
cvar_t *s_backendActive;
cvar_t *s_alDevice;
cvar_t *s_alHrtf;
cvar_t *s_alHrtfId;
cvar_t *s_alOutputMode;
cvar_t *s_alDistanceModel;
cvar_t *s_alFrequency;
cvar_t *s_alRefresh;
cvar_t *s_alMonoSources;
cvar_t *s_alStereoSources;
cvar_t *s_alOutputLimiter;
cvar_t *s_alSpatializeStereo;
}

namespace {
static soundInterface_t si;
}

/*
=================
S_ValidateInterface
=================
*/
static bool S_ValidSoundInterface( const soundInterface_t *s )
{
	if( !s->Shutdown ) return false;
	if( !s->StartSound ) return false;
	if( !s->StartLocalSound ) return false;
	if( !s->StartBackgroundTrack ) return false;
	if( !s->StopBackgroundTrack ) return false;
	if( !s->RawSamples ) return false;
	if( !s->StopAllSounds ) return false;
	if( !s->ClearLoopingSounds ) return false;
	if( !s->AddLoopingSound ) return false;
	if( !s->AddRealLoopingSound ) return false;
	if( !s->StopLoopingSound ) return false;
	if( !s->Respatialize ) return false;
	if( !s->UpdateEntityPosition ) return false;
	if( !s->Update ) return false;
	if( !s->DisableSounds ) return false;
	if( !s->BeginRegistration ) return false;
	if( !s->RegisterSound ) return false;
	if( !s->ClearSoundBuffer ) return false;
	if( !s->SoundInfo ) return false;
	if( !s->SoundList ) return false;

	return true;
}


/*
=================
S_StartSound
=================
*/
extern "C" void S_StartSound( vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx )
{
	if( si.StartSound ) {
		si.StartSound( origin, entnum, entchannel, sfx );
	}
}


/*
=================
S_StartLocalSound
=================
*/
extern "C" void S_StartLocalSound( sfxHandle_t sfx, int channelNum )
{
	if( si.StartLocalSound ) {
		si.StartLocalSound( sfx, channelNum );
	}
}


/*
=================
S_StartBackgroundTrack
=================
*/
extern "C" void S_StartBackgroundTrack( const char *intro, const char *loop )
{
	if( si.StartBackgroundTrack ) {
		si.StartBackgroundTrack( intro, loop );
	}
}


/*
=================
S_StopBackgroundTrack
=================
*/
extern "C" void S_StopBackgroundTrack( void )
{
	if( si.StopBackgroundTrack ) {
		si.StopBackgroundTrack( );
	}
}


/*
=================
S_RawSamples
=================
*/
extern "C" void S_RawSamples (int samples, int rate, int width, int channels,
		   const byte *data, float volume)
{
	if( si.RawSamples ) {
		si.RawSamples( samples, rate, width, channels, data, volume );
	}
}


/*
=================
S_StopAllSounds
=================
*/
extern "C" void S_StopAllSounds( void )
{
	if( si.StopAllSounds ) {
		si.StopAllSounds();
	}
}


/*
=================
S_ClearLoopingSounds
=================
*/
extern "C" void S_ClearLoopingSounds( qboolean killall )
{
	if( si.ClearLoopingSounds ) {
		si.ClearLoopingSounds( killall );
	}
}


/*
=================
S_AddLoopingSound
=================
*/
extern "C" void S_AddLoopingSound( int entityNum, const vec3_t origin,
		const vec3_t velocity, sfxHandle_t sfx )
{
	if( si.AddLoopingSound ) {
		si.AddLoopingSound( entityNum, origin, velocity, sfx );
	}
}


/*
=================
S_AddRealLoopingSound
=================
*/
extern "C" void S_AddRealLoopingSound( int entityNum, const vec3_t origin,
		const vec3_t velocity, sfxHandle_t sfx )
{
	if( si.AddRealLoopingSound ) {
		si.AddRealLoopingSound( entityNum, origin, velocity, sfx );
	}
}


/*
=================
S_StopLoopingSound
=================
*/
extern "C" void S_StopLoopingSound( int entityNum )
{
	if( si.StopLoopingSound ) {
		si.StopLoopingSound( entityNum );
	}
}


/*
=================
S_Respatialize
=================
*/
extern "C" void S_Respatialize( int entityNum, const vec3_t origin,
		vec3_t axis[3], int inwater )
{
	if( si.Respatialize ) {
		si.Respatialize( entityNum, origin, axis, inwater );
	}
}


/*
=================
S_UpdateEntityPosition
=================
*/
extern "C" void S_UpdateEntityPosition( int entityNum, const vec3_t origin )
{
	if( si.UpdateEntityPosition ) {
		si.UpdateEntityPosition( entityNum, origin );
	}
}


/*
=================
S_Update
=================
*/
extern "C" void S_Update( int msec )
{
	if ( si.Update ) {
		si.Update( msec );
	}
}


/*
=================
S_DisableSounds
=================
*/
extern "C" void S_DisableSounds( void )
{
	if( si.DisableSounds ) {
		si.DisableSounds();
	}
}


/*
=================
S_BeginRegistration
=================
*/
extern "C" void S_BeginRegistration( void )
{
	if ( si.BeginRegistration ) {
		si.BeginRegistration();
	}
}


/*
=================
S_RegisterSound
=================
*/
extern "C" sfxHandle_t	S_RegisterSound( const char *sample, qboolean compressed )
{
	if ( !sample || !*sample ) {
		Com_Printf( "NULL sound\n" );
		return 0;
	}

	if( si.RegisterSound ) {
		return si.RegisterSound( sample, compressed );
	} else {
		return 0;
	}
}


/*
=================
S_ClearSoundBuffer
=================
*/
extern "C" void S_ClearSoundBuffer( void )
{
	if( si.ClearSoundBuffer ) {
		si.ClearSoundBuffer();
	}
}


/*
=================
S_SoundInfo
=================
*/
static void S_SoundInfo( void )
{
	if( si.SoundInfo ) {
		si.SoundInfo();
	}
}


/*
=================
S_SoundList
=================
*/
static void S_SoundList( void )
{
	if( si.SoundList ) {
		si.SoundList();
	}
}

extern "C" qboolean S_GetSpatialAudioDebugInfo( spatialAudioDebugInfo_t *info )
{
	if ( info ) {
		*info = {};
	}

	if ( si.GetSpatialDebugInfo ) {
		return si.GetSpatialDebugInfo( info );
	}

	return qfalse;
}

static void S_AlDebugDump_f( void )
{
	if ( si.DumpSpatialDebug ) {
		si.DumpSpatialDebug();
		return;
	}

	Com_Printf( "Spatial audio debug dump unavailable for backend '%s'\n",
		( s_backendActive != nullptr ) ? s_backendActive->string : "none" );
}

static void S_AlListDevices_f( void )
{
	S_OpenAL_ListDevices();
}

static void S_AlListHrtfs_f( void )
{
	S_OpenAL_ListHrtfs();
}

static void S_AlConfigHints_f( void )
{
	S_OpenAL_ConfigHints();
}

static void S_AlRecoverDevice_f( void )
{
	bool force = false;

	if ( Cmd_Argc() > 1 ) {
		const char *arg = Cmd_Argv( 1 );
		force = ( !Q_stricmp( arg, "force" ) || !Q_stricmp( arg, "true" ) ||
			!Q_stricmp( arg, "yes" ) || std::atoi( arg ) != 0 );
	}

	S_OpenAL_RecoverDevice( ToQboolean( force ) );
}

//=============================================================================

/*
=================
S_Play_f
=================
*/
static void S_Play_f( void ) {
	int 		i;
	int			c;
	sfxHandle_t	h;

	if( !si.RegisterSound || !si.StartLocalSound ) {
		return;
	}

	c = Cmd_Argc();

	if( c < 2 ) {
		Com_Printf ("Usage: play <sound filename> [sound filename] [sound filename] ...\n");
		return;
	}

	for( i = 1; i < c; i++ ) {
		h = si.RegisterSound( Cmd_Argv(i), qfalse );

		if( h ) {
			si.StartLocalSound( h, CHAN_LOCAL_SOUND );
		}
	}
}


/*
=================
S_Music_f
=================
*/
static void S_Music_f( void ) {
	int		c;

	if( !si.StartBackgroundTrack ) {
		return;
	}

	c = Cmd_Argc();

	if ( c == 2 ) {
		si.StartBackgroundTrack( Cmd_Argv(1), nullptr );
	} else if ( c == 3 ) {
		si.StartBackgroundTrack( Cmd_Argv(1), Cmd_Argv(2) );
	} else {
		Com_Printf ("Usage: music <musicfile> [loopfile]\n");
		return;
	}

}


/*
=================
S_StopMusic_f
=================
*/
static void S_StopMusic_f( void )
{
	if ( !si.StopBackgroundTrack )
		return;

	si.StopBackgroundTrack();
}


//=============================================================================

/*
=================
S_Init
=================
*/
extern "C" void S_Init( void )
{
	cvar_t		*cv;
	bool		started = false;

	Com_Printf( "------ Initializing Sound ------\n" );

	s_volume = Cvar_Get( "s_volume", "0.8", CVAR_ARCHIVE );
	Cvar_CheckRange( s_volume, "0", "1", CV_FLOAT );
	Cvar_SetDescription( s_volume, "Sets master volume for all game audio." );
	s_musicVolume = Cvar_Get( "s_musicVolume", "0.25", CVAR_ARCHIVE );
	Cvar_CheckRange( s_musicVolume, "0", "1", CV_FLOAT );
	Cvar_SetDescription( s_musicVolume, "Sets volume for in-game music only." );
	s_doppler = Cvar_Get( "s_doppler", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_doppler, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_doppler, "Enables doppler effect on moving projectiles." );
	s_muteWhenUnfocused = Cvar_Get( "s_muteWhenUnfocused", "1", CVAR_ARCHIVE );
	Cvar_CheckRange( s_muteWhenUnfocused, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_muteWhenUnfocused, "Mutes all audio while game window is unfocused." );
	s_muteWhenMinimized = Cvar_Get( "s_muteWhenMinimized", "1", CVAR_ARCHIVE );
	Cvar_CheckRange( s_muteWhenMinimized, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_muteWhenMinimized, "Mutes all audio while game is minimized." );
	s_backend = Cvar_Get( "s_backend", "openal", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_SetDescription( s_backend, "Selects the sound backend to initialize.\n"
		"Available backends:\n"
		"  " S_COLOR_CYAN "openal" S_COLOR_WHITE " - default client backend with OpenAL spatial audio\n"
		"  " S_COLOR_CYAN "legacy" S_COLOR_WHITE " - original software mixer and platform device fallback\n" );
	s_backendActive = Cvar_Get( "s_backendActive", "none", CVAR_ROM );
	Cvar_SetDescription( s_backendActive, "Reports the currently active sound backend." );
	s_alDevice = Cvar_Get( "s_alDevice", "", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_SetDescription( s_alDevice, "Selects the OpenAL playback device when s_backend is set to openal.\n"
		"Leave blank to use the system default device." );
	s_alHrtf = Cvar_Get( "s_alHrtf", "auto", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_SetDescription( s_alHrtf, "Requests OpenAL Soft HRTF rendering for stereo/headphone output. Requires snd_restart.\n"
		"Available values:\n"
		"  " S_COLOR_CYAN "auto" S_COLOR_WHITE " - let OpenAL decide based on device/output\n"
		"  " S_COLOR_CYAN "on" S_COLOR_WHITE " - request HRTF and report if the device denies it\n"
		"  " S_COLOR_CYAN "off" S_COLOR_WHITE " - request non-HRTF rendering" );
	s_alHrtfId = Cvar_Get( "s_alHrtfId", "", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_SetDescription( s_alHrtfId, "Preferred OpenAL Soft HRTF name or numeric index. Leave blank for the device default. Requires snd_restart." );
	s_alOutputMode = Cvar_Get( "s_alOutputMode", "auto", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_SetDescription( s_alOutputMode, "Requests the OpenAL Soft output mode. Requires snd_restart.\n"
		"Available values: auto, headphones, speakers, surround, quad, 5.1, 6.1, 7.1." );
	s_alDistanceModel = Cvar_Get( "s_alDistanceModel", "inverse_clamped", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_SetDescription( s_alDistanceModel, "Requests the OpenAL distance attenuation model for world sounds. Requires snd_restart.\n"
		"Available values: none, inverse, inverse_clamped, linear, linear_clamped, exponent, exponent_clamped." );
	s_alFrequency = Cvar_Get( "s_alFrequency", "48000", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( s_alFrequency, "8000", "192000", CV_INTEGER );
	Cvar_SetDescription( s_alFrequency, "Requests the OpenAL mix frequency in Hz. Requires snd_restart." );
	s_alRefresh = Cvar_Get( "s_alRefresh", "100", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( s_alRefresh, "20", "1000", CV_INTEGER );
	Cvar_SetDescription( s_alRefresh, "Requests the OpenAL context refresh rate in Hz. Requires snd_restart." );
	s_alMonoSources = Cvar_Get( "s_alMonoSources", "64", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( s_alMonoSources, "16", "256", CV_INTEGER );
	Cvar_SetDescription( s_alMonoSources, "Requests the number of OpenAL mono/3D source slots. Requires snd_restart." );
	s_alStereoSources = Cvar_Get( "s_alStereoSources", "8", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( s_alStereoSources, "0", "64", CV_INTEGER );
	Cvar_SetDescription( s_alStereoSources, "Requests the number of OpenAL stereo source slots. Requires snd_restart." );
	s_alOutputLimiter = Cvar_Get( "s_alOutputLimiter", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( s_alOutputLimiter, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_alOutputLimiter, "Requests OpenAL Soft output limiting when the device supports it. Requires snd_restart." );
	s_alSpatializeStereo = Cvar_Get( "s_alSpatializeStereo", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( s_alSpatializeStereo, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_alSpatializeStereo, "Allows two-channel world samples to use OpenAL positional routing instead of the authored direct path. Requires snd_restart." );
	Cvar_Set( "s_backendActive", "none" );

	cv = Cvar_Get( "s_initsound", "1", 0 );
	Cvar_SetDescription( cv, "Whether or not to startup the sound system." );
	if ( !cv->integer ) {
		Com_Printf( "Sound disabled.\n" );
	} else {

		S_CodecInit();

		Cmd_AddCommand( "play", S_Play_f );
		Cmd_AddCommand( "music", S_Music_f );
		Cmd_AddCommand( "stopmusic", S_StopMusic_f );
		Cmd_AddCommand( "s_list", S_SoundList );
		Cmd_AddCommand( "s_stop", S_StopAllSounds );
		Cmd_AddCommand( "s_info", S_SoundInfo );
		Cmd_AddCommand( "s_alDebugDump", S_AlDebugDump_f );
		Cmd_AddCommand( "s_alListDevices", S_AlListDevices_f );
		Cmd_AddCommand( "s_alListHrtfs", S_AlListHrtfs_f );
		Cmd_AddCommand( "s_alConfigHints", S_AlConfigHints_f );
		Cmd_AddCommand( "s_alRecoverDevice", S_AlRecoverDevice_f );

		if ( !started && !Q_stricmp( s_backend->string, "openal" ) ) {
			started = S_OpenAL_Init( &si );
			if ( started ) {
				Cvar_Set( "s_backendActive", "openal" );
			} else {
				Com_Printf( S_COLOR_YELLOW "WARNING: OpenAL backend failed to initialize, falling back to legacy backend.\n" );
			}
		} else if ( !started && Q_stricmp( s_backend->string, "legacy" ) != 0 ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: unknown s_backend '%s', falling back to legacy backend.\n", s_backend->string );
		}

		if ( !started ) {
			started = S_Base_Init( &si );
			if ( started ) {
				Cvar_Set( "s_backendActive", "legacy" );
			}
		}

		if ( started ) {
			if( !S_ValidSoundInterface( &si ) ) {
				Com_Error( ERR_FATAL, "Sound interface invalid" );
			}

			S_SoundInfo();
			Com_Printf( "Sound initialization successful.\n" );
		} else {
			Cvar_Set( "s_backendActive", "none" );
			Com_Printf( "Sound initialization failed.\n" );
		}
	}

	Com_Printf( "--------------------------------\n");
}


/*
=================
S_Shutdown
=================
*/
extern "C" void S_Shutdown( void )
{
	if ( si.StopAllSounds ) {
		si.StopAllSounds();
	}

	if ( si.Shutdown ) {
		si.Shutdown();
	}

	si = {};
	Cvar_Set( "s_backendActive", "none" );

	Cmd_RemoveCommand( "play" );
	Cmd_RemoveCommand( "music");
	Cmd_RemoveCommand( "stopmusic");
	Cmd_RemoveCommand( "s_list" );
	Cmd_RemoveCommand( "s_stop" );
	Cmd_RemoveCommand( "s_info" );
	Cmd_RemoveCommand( "s_alDebugDump" );
	Cmd_RemoveCommand( "s_alListDevices" );
	Cmd_RemoveCommand( "s_alListHrtfs" );
	Cmd_RemoveCommand( "s_alConfigHints" );
	Cmd_RemoveCommand( "s_alRecoverDevice" );

	S_CodecShutdown();

	cls.soundStarted = qfalse;
}
