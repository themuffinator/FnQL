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
// snd_local.h -- private sound definitions


#include "../../qcommon/q_shared.h"
#include "../../qcommon/qcommon.h"
#include "snd_public.h"

#ifdef __cplusplus
extern "C" {
#endif

#define	PAINTBUFFER_SIZE		4096					// this is in samples

#define SND_CHUNK_SIZE			1024					// samples
#define SND_CHUNK_SIZE_FLOAT	(SND_CHUNK_SIZE/2)		// floats
#define SND_CHUNK_SIZE_BYTE		(SND_CHUNK_SIZE*2)		// floats

typedef struct portable_samplepair_t portable_samplepair_t;
typedef struct adpcm_state_t adpcm_state_t;
typedef struct sndBuffer sndBuffer;
typedef struct sfx_t sfx_t;
typedef struct dma_t dma_t;
typedef struct loopSound_t loopSound_t;
typedef struct channel_t channel_t;
typedef struct wavinfo_t wavinfo_t;
typedef struct soundInterface_t soundInterface_t;

struct portable_samplepair_t {
	int			left;	// the final values will be clamped to +/- 0x00ffff00 and shifted down
	int			right;
};

struct adpcm_state_t {
	short	sample;		/* Previous output value */
	char	index;		/* Index into stepsize table */
};

struct sndBuffer {
	short					sndChunk[SND_CHUNK_SIZE];
	sndBuffer				*next;
	int						size;
	adpcm_state_t			adpcm;
};

struct sfx_t {
	sndBuffer		*soundData;
	qboolean		defaultSound;			// couldn't be loaded, so use buzz
	qboolean		inMemory;				// not in Memory
	qboolean		soundCompressed;		// not in Memory
	int				soundCompressionMethod;
	int 			soundLength;
	int				soundChannels;
	char 			soundName[MAX_QPATH];
	int				lastTimeUsed;
	sfx_t			*next;
};

struct dma_t {
	unsigned int channels;
	unsigned int samples;				// mono samples in buffer
	int			fullsamples;			// samples with all channels in buffer (samples divided by channels)
	int			submission_chunk;		// don't mix less than this #
	int			samplebits;
	int			isfloat;
	int			speed;
	byte		*buffer;
	const char	*driver;
};

extern byte *dma_buffer2;

#define START_SAMPLE_IMMEDIATE	0x7fffffff

#define MAX_DOPPLER_SCALE 50.0f //arbitrary

struct loopSound_t {
	vec3_t		origin;
	vec3_t		velocity;
	sfx_t		*sfx;
	int			mergeFrame;
	qboolean	active;
	qboolean	kill;
	qboolean	doppler;
	float		dopplerScale;
	float		oldDopplerScale;
	int			framenum;
};

struct channel_t {
	int			allocTime;
	int			startSample;	// START_SAMPLE_IMMEDIATE = set immediately on next mix
	int			entnum;			// to allow overriding a specific sound
	int			entchannel;		// to allow overriding a specific sound
	int			leftvol;		// 0-255 volume after spatialization
	int			rightvol;		// 0-255 volume after spatialization
	int			master_vol;		// 0-255 volume before spatialization
	float		dopplerScale;
	float		oldDopplerScale;
	vec3_t		origin;			// only use if fixed_origin is set
	qboolean	fixed_origin;	// use origin instead of fetching entnum's origin
	sfx_t		*thesfx;		// sfx structure
	qboolean	doppler;
};


#define WAV_FORMAT_PCM			0x0001
#define WAVE_FORMAT_IEEE_FLOAT	0x0003

struct wavinfo_t {
	int			format;
	int			rate;
	int			width;
	int			channels;
	int			samples;
	int			dataofs;		// chunk starts this many bytes from file start
};

// Interface between Q3 sound "api" and the sound backend
struct soundInterface_t {
	void (*Shutdown)(void);
	void (*StartSound)( const vec3_t origin, int entnum, int entchannel, sfxHandle_t sfx, float volume );
	void (*StartLocalSound)( sfxHandle_t sfx, int channelNum, float volume );
	void (*StartBackgroundTrack)( const char *intro, const char *loop );
	void (*StopBackgroundTrack)( void );
	void (*UpdateBackgroundTrack)( void );
	void (*RawSamples)(int samples, int rate, int width, int channels, const byte *data, float volume);
	void (*AddVoiceSamples)( int clientNum, int samples, int rate, const short *data );
	void (*StopAllSounds)( void );
	void (*ClearLoopingSoundsFrame)( void );
	void (*ClearLoopingSounds)( qboolean killall );
	void (*AddLoopingSound)( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx );
	void (*AddRealLoopingSound)( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfx );
	void (*StopLoopingSound)(int entityNum );
	void (*Respatialize)( int entityNum, const vec3_t origin, vec3_t axis[3], int inwater );
	void (*UpdateEntityPosition)( int entityNum, const vec3_t origin );
	void (*Update)( int msec );
	void (*DisableSounds)( void );
	void (*BeginRegistration)( void );
	sfxHandle_t (*RegisterSound)( const char *sample, qboolean compressed );
	void (*ClearSoundBuffer)( void );
	void (*SoundInfo)( void );
	void (*SoundList)( void );
	qboolean (*GetSpatialDebugInfo)( spatialAudioDebugInfo_t *info );
	void (*DumpSpatialDebug)( void );
};


/*
====================================================================

  SYSTEM SPECIFIC FUNCTIONS

====================================================================
*/

// initializes cycling through a DMA buffer and returns information on it
qboolean SNDDMA_Init(void);

// gets the current DMA position
int		SNDDMA_GetDMAPos(void);

// shutdown the DMA xfer.
void	SNDDMA_Shutdown(void);

void	SNDDMA_BeginPainting (void);

void	SNDDMA_Submit(void);

//====================================================================

#define	MAX_CHANNELS			96

extern	channel_t   s_channels[MAX_CHANNELS];
extern	channel_t   loop_channels[MAX_CHANNELS];
extern	int		numLoopChannels;

extern	int		s_soundtime;
extern	int		s_paintedtime;
extern	int		s_rawend;
extern	vec3_t	listener_forward;
extern	vec3_t	listener_right;
extern	vec3_t	listener_up;
extern	dma_t	dma;

#define	MAX_RAW_SAMPLES	16384
extern	portable_samplepair_t	s_rawsamples[MAX_RAW_SAMPLES];

extern cvar_t *s_volume;
extern cvar_t *s_musicVolume;
extern cvar_t *s_voiceVolume;
extern cvar_t *s_voiceStep;
extern cvar_t *s_doppler;
extern cvar_t *s_pvs;
extern cvar_t *s_muteWhenUnfocused;
extern cvar_t *s_muteWhenMinimized;
extern cvar_t *s_khz;
extern cvar_t *s_backend;
extern cvar_t *s_backendActive;
extern cvar_t *s_alDevice;
extern cvar_t *s_alHrtf;
extern cvar_t *s_alHrtfId;
extern cvar_t *s_alOutputMode;
extern cvar_t *s_alDistanceModel;
extern cvar_t *s_alFrequency;
extern cvar_t *s_alRefresh;
extern cvar_t *s_alMonoSources;
extern cvar_t *s_alStereoSources;
extern cvar_t *s_alOutputLimiter;
extern cvar_t *s_alSpatializeStereo;

extern cvar_t *s_testsound;

qboolean S_LoadSound( sfx_t *sfx );

void		SND_free(sndBuffer *v);
sndBuffer*	SND_malloc( void );
void		SND_setup( void );
void		SND_shutdown( void );

void S_PaintChannels(int endtime);
void S_PaintVoiceSamples( int starttime, int endtime, portable_samplepair_t *paintbuffer );
qboolean S_OriginInPVS( const vec3_t listener, const vec3_t origin );

// spatializes a channel
void S_Spatialize(channel_t *ch);

// adpcm functions
int  S_AdpcmMemoryNeeded( const wavinfo_t *info );
void S_AdpcmEncodeSound( sfx_t *sfx, short *samples );
void S_AdpcmGetSamples(sndBuffer *chunk, short *to);

// wavelet function

#define SENTINEL_MULAW_ZERO_RUN 127
#define SENTINEL_MULAW_FOUR_BIT_RUN 126

void S_FreeOldestSound( void );

#define	NXStream byte

void encodeWavelet(sfx_t *sfx, short *packets);
void decodeWavelet( sndBuffer *stream, short *packets);

void encodeMuLaw( sfx_t *sfx, short *packets);
extern short mulawToShort[256];

extern short *sfxScratchBuffer;
extern sfx_t *sfxScratchPointer;
extern int	   sfxScratchIndex;

qboolean S_Base_Init( soundInterface_t *si );
qboolean S_OpenAL_Init( soundInterface_t *si );
void S_OpenAL_ListDevices( void );
void S_OpenAL_ListHrtfs( void );
void S_OpenAL_ConfigHints( void );
qboolean S_OpenAL_RecoverDevice( qboolean force );

#ifdef __cplusplus
}
#endif
