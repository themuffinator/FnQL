#if defined (__linux__) // ALSA sound path

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>

#include "../client/audio/snd_local.h"
#include "../qcommon/q_shared.h"
#include "unix_raii.h"

#define USE_SPINLOCK

#define BUFFER_SIZE (64*1024)
#define NUM_PERIODS 3
#define PERIOD_TIME 20000 // wishable latency

/* engine variables */

extern cvar_t *s_khz;
extern cvar_t *s_device;

/* pthreads  private variables */

#ifdef USE_ALSA_STATIC

#ifdef USE_SPINLOCK
#define	_pthread_spin_init pthread_spin_init
#define	_pthread_spin_destroy pthread_spin_destroy
#define	_pthread_spin_lock pthread_spin_lock
#define	_pthread_spin_unlock pthread_spin_unlock
#else // mutex
#define _pthread_mutex_init pthread_mutex_init
#define _pthread_mutex_destroy pthread_mutex_destroy
#define	_pthread_mutex_lock pthread_mutex_lock
#define	_pthread_mutex_unlock pthread_mutex_unlock
#endif

#define	_pthread_join pthread_join
#define	_pthread_create pthread_create
#define	_pthread_exit pthread_exit

#define	_snd_strerror snd_strerror
#define	_snd_pcm_open snd_pcm_open
#define	_snd_pcm_drain snd_pcm_drain
#define	_snd_pcm_drop snd_pcm_drop
#define	_snd_pcm_close snd_pcm_close
#define	_snd_pcm_hw_params_sizeof snd_pcm_hw_params_sizeof
#define	_snd_pcm_sw_params_sizeof snd_pcm_sw_params_sizeof
#define	_snd_pcm_hw_params_any snd_pcm_hw_params_any
#define _snd_async_add_pcm_handler snd_async_add_pcm_handler
#define _snd_async_handler_get_pcm snd_async_handler_get_pcm
#define	_snd_pcm_hw_params_set_rate_resample snd_pcm_hw_params_set_rate_resample
#define	_snd_pcm_hw_params_set_access snd_pcm_hw_params_set_access
#define	_snd_pcm_hw_params_set_format snd_pcm_hw_params_set_format
#define	_snd_pcm_hw_params_set_channels snd_pcm_hw_params_set_channels
#define	_snd_pcm_hw_params_set_rate_near snd_pcm_hw_params_set_rate_near
#define	_snd_pcm_hw_params_set_period_time_near snd_pcm_hw_params_set_period_time_near
#define	_snd_pcm_hw_params_set_periods snd_pcm_hw_params_set_periods
#define	_snd_pcm_hw_params_set_periods_near snd_pcm_hw_params_set_periods_near
#define	_snd_pcm_hw_params_get_period_size_min snd_pcm_hw_params_get_period_size_min
#define	_snd_pcm_hw_params snd_pcm_hw_params
#define	_snd_pcm_sw_params_current snd_pcm_sw_params_current
#define	_snd_pcm_sw_params_set_start_threshold snd_pcm_sw_params_set_start_threshold
#define	_snd_pcm_sw_params_set_stop_threshold snd_pcm_sw_params_set_stop_threshold
#define	_snd_pcm_sw_params_set_avail_min snd_pcm_sw_params_set_avail_min
#define	_snd_pcm_sw_params snd_pcm_sw_params
#define	_snd_pcm_start snd_pcm_start
#define	_snd_pcm_prepare snd_pcm_prepare
#define	_snd_pcm_resume snd_pcm_resume
#define	_snd_pcm_wait snd_pcm_wait
#define	__snd_pcm_state snd_pcm_state
#define	_snd_pcm_avail snd_pcm_avail
#define	_snd_pcm_avail_update snd_pcm_avail_update
#define	_snd_pcm_mmap_begin snd_pcm_mmap_begin
#define	_snd_pcm_mmap_commit snd_pcm_mmap_commit
#define _snd_pcm_writei snd_pcm_writei

#else

/* pthreads private variables */

static fnql::posix::ScopedLibrary t_lib;

#ifdef USE_SPINLOCK
static int (*_pthread_spin_init)(pthread_spinlock_t *lock, int pshared);
static int (*_pthread_spin_destroy)(pthread_spinlock_t *lock);
static int (*_pthread_spin_lock)(pthread_spinlock_t *lock);
static int (*_pthread_spin_unlock)(pthread_spinlock_t *lock);
#else
static int (*_pthread_mutex_init)(pthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr);
static int (*_pthread_mutex_destroy)(pthread_mutex_t *mutex);
static int (*_pthread_mutex_lock)(pthread_mutex_t *mutex);
static int (*_pthread_mutex_unlock)(pthread_mutex_t *mutex);
#endif
static int (*_pthread_join)(pthread_t __th, void **__thread_return);
static int (*_pthread_create)(pthread_t *thread, const pthread_attr_t *attr, void *(*func) (void *), void *arg);
static void (*_pthread_exit)(void *retval);

/* alsa private variables */

static fnql::posix::ScopedLibrary a_lib;

static const char *(*_snd_strerror)(int errnum);
static int (*_snd_pcm_open)(snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream, int mode);
static int (*_snd_pcm_drain)(snd_pcm_t *pcm);
static int (*_snd_pcm_drop)(snd_pcm_t *pcm);
static int (*_snd_pcm_close)(snd_pcm_t *pcm);
static size_t (*_snd_pcm_hw_params_sizeof)(void);
static size_t (*_snd_pcm_sw_params_sizeof)(void);
static snd_pcm_t* (*_snd_async_handler_get_pcm)(snd_async_handler_t *handler);
static int (*_snd_async_add_pcm_handler)(snd_async_handler_t **handler, snd_pcm_t *pcm, snd_async_callback_t callback, void *private_data);
static int (*_snd_pcm_hw_params_any)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
static int (*_snd_pcm_hw_params_set_rate_resample)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val);
static int (*_snd_pcm_hw_params_set_access)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_access_t _access);
static int (*_snd_pcm_hw_params_set_format)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_format_t val);
static int (*_snd_pcm_hw_params_set_channels)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val);
static int (*_snd_pcm_hw_params_set_rate_near)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
static int (*_snd_pcm_hw_params_set_period_time_near)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
static int (*_snd_pcm_hw_params_set_periods)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val, int dir);
static int (*_snd_pcm_hw_params_set_periods_near)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
static int (*_snd_pcm_hw_params_get_period_size_min)(const snd_pcm_hw_params_t *params, snd_pcm_uframes_t *frames, int *dir);
static int (*_snd_pcm_hw_params)(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
static int (*_snd_pcm_sw_params_current)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params);
static int (*_snd_pcm_sw_params_set_start_threshold)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val);
static int (*_snd_pcm_sw_params_set_stop_threshold)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val);
static int (*_snd_pcm_sw_params_set_avail_min)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val);
static int (*_snd_pcm_sw_params)(snd_pcm_t *pcm, snd_pcm_sw_params_t *params);
static int (*_snd_pcm_start)(snd_pcm_t *pcm);
static int (*_snd_pcm_prepare)(snd_pcm_t *pcm);
static int (*_snd_pcm_resume)(snd_pcm_t *pcm);
static int (*_snd_pcm_wait)(snd_pcm_t *pcm, int timeout);
static snd_pcm_state_t (*__snd_pcm_state)(snd_pcm_t *pcm);
static snd_pcm_sframes_t (*_snd_pcm_avail)(snd_pcm_t *pcm);
static snd_pcm_sframes_t (*_snd_pcm_avail_update)(snd_pcm_t *pcm);
static int (*_snd_pcm_mmap_begin)(snd_pcm_t *pcm, const snd_pcm_channel_area_t **areas, snd_pcm_uframes_t *offset, snd_pcm_uframes_t *frames);
static snd_pcm_sframes_t (*_snd_pcm_mmap_commit)(snd_pcm_t *pcm, snd_pcm_uframes_t offset, snd_pcm_uframes_t frames);
static snd_pcm_sframes_t (*_snd_pcm_writei)(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size);

typedef struct {
	void **symbol;
	const char *name;
} sym_t;

sym_t t_list[] = {
#ifdef USE_SPINLOCK
	{ (void**)&_pthread_spin_init, "pthread_spin_init" },
	{ (void**)&_pthread_spin_destroy, "pthread_spin_destroy" },
	{ (void**)&_pthread_spin_lock, "pthread_spin_lock" },
	{ (void**)&_pthread_spin_unlock, "pthread_spin_unlock" },
#else
	{ (void**)&_pthread_mutex_init, "pthread_mutex_init" },
	{ (void**)&_pthread_mutex_destroy, "pthread_mutex_destroy" },
	{ (void**)&_pthread_mutex_lock, "pthread_mutex_lock" },
	{ (void**)&_pthread_mutex_unlock, "pthread_mutex_unlock" },
#endif
	{ (void**)&_pthread_join, "pthread_join" },
	{ (void**)&_pthread_create, "pthread_create" },
	{ (void**)&_pthread_exit, "pthread_exit" }
};

sym_t a_list[] = {
	{ (void**)&_snd_strerror, "snd_strerror" },
	{ (void**)&_snd_pcm_open, "snd_pcm_open" },
	{ (void**)&_snd_pcm_drain, "snd_pcm_drain" },
	{ (void**)&_snd_pcm_drop, "snd_pcm_drop" },
	{ (void**)&_snd_pcm_close, "snd_pcm_close" },
	{ (void**)&_snd_pcm_hw_params_sizeof, "snd_pcm_hw_params_sizeof" },
	{ (void**)&_snd_pcm_sw_params_sizeof, "snd_pcm_sw_params_sizeof" },
	{ (void**)&_snd_async_handler_get_pcm, "snd_async_handler_get_pcm" },
	{ (void**)&_snd_async_add_pcm_handler, "snd_async_add_pcm_handler" },
	{ (void**)&_snd_pcm_hw_params_any, "snd_pcm_hw_params_any" },
	{ (void**)&_snd_pcm_hw_params_set_rate_resample, "snd_pcm_hw_params_set_rate_resample" },
	{ (void**)&_snd_pcm_hw_params_set_access, "snd_pcm_hw_params_set_access" },
	{ (void**)&_snd_pcm_hw_params_set_format, "snd_pcm_hw_params_set_format" },
	{ (void**)&_snd_pcm_hw_params_set_channels, "snd_pcm_hw_params_set_channels" },
	{ (void**)&_snd_pcm_hw_params_set_rate_near, "snd_pcm_hw_params_set_rate_near" },
	{ (void**)&_snd_pcm_hw_params_set_period_time_near, "snd_pcm_hw_params_set_period_time_near" },
	{ (void**)&_snd_pcm_hw_params_set_periods, "snd_pcm_hw_params_set_periods" },
	{ (void**)&_snd_pcm_hw_params_set_periods_near, "snd_pcm_hw_params_set_periods_near" },
	{ (void**)&_snd_pcm_hw_params_get_period_size_min, "snd_pcm_hw_params_get_period_size_min" },
	{ (void**)&_snd_pcm_hw_params, "snd_pcm_hw_params" },
	{ (void**)&_snd_pcm_sw_params_current, "snd_pcm_sw_params_current" },
	{ (void**)&_snd_pcm_sw_params_set_start_threshold, "snd_pcm_sw_params_set_start_threshold" },
	{ (void**)&_snd_pcm_sw_params_set_stop_threshold, "snd_pcm_sw_params_set_stop_threshold" },
	{ (void**)&_snd_pcm_sw_params_set_avail_min, "snd_pcm_sw_params_set_avail_min" },
	{ (void**)&_snd_pcm_sw_params, "snd_pcm_sw_params" },
	{ (void**)&_snd_pcm_start, "snd_pcm_start" },
	{ (void**)&_snd_pcm_prepare, "snd_pcm_prepare" },
	{ (void**)&_snd_pcm_resume, "snd_pcm_resume" },
	{ (void**)&_snd_pcm_wait, "snd_pcm_wait" },
	{ (void**)&__snd_pcm_state, "snd_pcm_state" },
	{ (void**)&_snd_pcm_avail, "snd_pcm_avail" },
	{ (void**)&_snd_pcm_avail_update, "snd_pcm_avail_update" },
	{ (void**)&_snd_pcm_mmap_begin, "snd_pcm_mmap_begin" },
	{ (void**)&_snd_pcm_mmap_commit, "snd_pcm_mmap_commit" },
	{ (void**)&_snd_pcm_writei, "snd_pcm_writei" }
};

#endif

qboolean alsa_used = qfalse; /* will be checked in oss engine */

static pthread_t thread;
#ifdef USE_SPINLOCK
static pthread_spinlock_t lock;
#else
static pthread_mutex_t mutex;
#endif

static qboolean snd_inited = qfalse;
static qboolean sync_initialized = qfalse;
static qboolean thread_started = qfalse;

/* we will use static dma buffer */
static unsigned char buffer[ BUFFER_SIZE ];
static unsigned int periods;
static unsigned int period_time;  // wishable latency
static snd_pcm_t *handle;


static std::atomic_bool snd_loop{ false };
static std::atomic_bool snd_async{ false };

static snd_pcm_uframes_t period_size;
static snd_pcm_sframes_t buffer_pos;	// buffer position, in mono samples
static int buffer_sz;					// buffers size, in bytes
static int frame_sz;					// frame size, in bytes

static void async_proc( snd_async_handler_t *ahandler );
static void thread_proc_mmap( void );
static void thread_proc_direct( void );
static void *thread_proc_mmap_entry( void * )
{
	thread_proc_mmap();
	return NULL;
}

static void *thread_proc_direct_entry( void * )
{
	thread_proc_direct();
	return NULL;
}


void Snd_Memset( void* dest, const int val, const size_t count )
{
    Com_Memset( dest, val, count );
}


void SNDDMA_BeginPainting( void )
{

}


void SNDDMA_Submit( void )
{

}


static void UnloadLibs( void )
{
#ifndef USE_ALSA_STATIC
	a_lib.reset();
	t_lib.reset();
#endif
}

typedef enum {
	SND_MODE_ASYNC,
	SND_MODE_MMAP,
	SND_MODE_DIRECT
} smode_t;

static qboolean setup_ALSA( smode_t mode )
{
	snd_async_handler_t *ahandler;
	snd_pcm_hw_params_t *hwparams;
	snd_pcm_sw_params_t *swparams;
	std::unique_ptr<unsigned char[]> hwparamsStorage;
	std::unique_ptr<unsigned char[]> swparamsStorage;
	size_t hwparamsSize;
	size_t swparamsSize;
	constexpr size_t kMaximumAlsaParameterBytes = 1024 * 1024;
	unsigned int speed, rrate;
	int err, dir = 0, bps, channels;
	qboolean use_mmap;

	if ( snd_inited == qtrue )
	{
		return qtrue;
	}

	alsa_used = qfalse;
	snd_async.store( false, std::memory_order_release );
	snd_loop.store( false, std::memory_order_release );
	sync_initialized = qfalse;
	thread_started = qfalse;
	handle = NULL;
	use_mmap = qfalse;

#ifndef USE_ALSA_STATIC
	if ( !t_lib )
	{
		t_lib.reset( fnql::posix::LoadLibraryFallback( "libpthread.so.0", "libpthread.so" ) );
		if ( !t_lib )
		{
			Com_Printf( "Error loading pthread library, disabling ALSA support.\n" );
			return qfalse;
		}
	}

	if ( !fnql::posix::LoadSymbols( t_lib.get(), t_list, "pthread", fnql::posix::SymbolLoadFailure::Support ) )
	{
		UnloadLibs();
		return qfalse;
	}

	if ( !a_lib )
	{
		a_lib.reset( fnql::posix::LoadLibraryFallback( "libasound.so.2", "libasound.so" ) );
		if ( !a_lib )
		{
			Com_Printf( "Error loading ALSA library.\n" );
			goto __fail;
		}
	}

	if ( !fnql::posix::LoadSymbols( a_lib.get(), a_list, "ALSA", fnql::posix::SymbolLoadFailure::Support ) )
	{
		goto __fail;
	}
#endif

	err = _snd_pcm_open( &handle, s_device->string, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK );
	if ( err < 0 )
	{
		Com_Printf( "Playback device open error: %s\n", _snd_strerror( err ) );
		goto __fail;
	}

	hwparamsSize = _snd_pcm_hw_params_sizeof();
	swparamsSize = _snd_pcm_sw_params_sizeof();
	if ( hwparamsSize == 0 || hwparamsSize > kMaximumAlsaParameterBytes ||
		swparamsSize == 0 || swparamsSize > kMaximumAlsaParameterBytes )
	{
		Com_Printf( "ALSA returned invalid parameter storage sizes (%lu hardware, %lu software)\n",
			static_cast<unsigned long>( hwparamsSize ),
			static_cast<unsigned long>( swparamsSize ) );
		goto __fail;
	}
	hwparamsStorage.reset( new ( std::nothrow ) unsigned char[hwparamsSize] );
	swparamsStorage.reset( new ( std::nothrow ) unsigned char[swparamsSize] );
	if ( !hwparamsStorage || !swparamsStorage )
	{
		Com_Printf( "Could not allocate ALSA parameter storage\n" );
		goto __fail;
	}
	hwparams = reinterpret_cast<snd_pcm_hw_params_t *>( hwparamsStorage.get() );
	swparams = reinterpret_cast<snd_pcm_sw_params_t *>( swparamsStorage.get() );

	err = _snd_pcm_hw_params_any( handle, hwparams );
	if ( err < 0 )
	{
		Com_Printf( "Broken configuration for playback: " \
			"no configurations available: %s\n", _snd_strerror( err ) );
		goto __fail;
	}

	switch ( mode )
	{
		case SND_MODE_ASYNC:
					err = _snd_async_add_pcm_handler( &ahandler, handle, async_proc, NULL );
					if ( err < 0 )
						goto __fail;
					/* set the interleaved read/write format */
					err = _snd_pcm_hw_params_set_access( handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED );
					snd_async.store( true, std::memory_order_release );
					break;
		case SND_MODE_MMAP:
					err = _snd_pcm_hw_params_set_access( handle, hwparams, SND_PCM_ACCESS_MMAP_INTERLEAVED );
					use_mmap = qtrue;
					break;
		case SND_MODE_DIRECT:
					err = _snd_pcm_hw_params_set_access( handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED );
					break;
	}

	if ( err < 0 )
	{
		Com_Printf( "Access type not available for playback: %s\n",
			_snd_strerror( err ) );
		goto __fail;
	}

	/* set hw resampling */
	err = _snd_pcm_hw_params_set_rate_resample( handle, hwparams, 1 );
	if ( err < 0 )
	{
		Com_Printf( "Resampling setup failed for playback: %s\n",
			_snd_strerror( err ) );
		goto __fail;
	}

	/* set the sample format */
	bps = 16;
	err = _snd_pcm_hw_params_set_format( handle, hwparams, SND_PCM_FORMAT_S16 );
	if ( err < 0 )
	{
		bps = 8;
		err = _snd_pcm_hw_params_set_format( handle, hwparams, SND_PCM_FORMAT_S8 );
	}
	if ( err < 0 )
	{
		Com_Printf( "Sample format not available for playback: " \
			"%s\n", _snd_strerror( err ) );
		goto __fail;
	}

	channels = 2;
	/* set the count of channels */
	err = _snd_pcm_hw_params_set_channels( handle, hwparams, channels );
	if ( err < 0 )
	{
		channels = 1;
		err = _snd_pcm_hw_params_set_channels( handle, hwparams, channels );
	}
	
	if ( err < 0 )
	{
		err = _snd_pcm_hw_params_set_channels( handle, hwparams, channels );
		Com_Printf( "Channels count (%i) not available for playbacks: %s\n",
			channels, _snd_strerror( err ) );
		goto __fail;
	}

	switch ( s_khz->integer )
	{
		case 48: speed = 48000; break;
		case 44: speed = 44100; break;
		case 11: speed = 11025; break;
		case  8: speed =  8000; break;
		case 22:
		default: speed = 22050; break;
	};

	rrate = speed;
	err = _snd_pcm_hw_params_set_rate_near( handle, hwparams, &rrate, 0 );
	if ( err < 0 )
	{
		Com_Printf("Rate %iHz not available for playback: %s\n",
			speed, _snd_strerror( err ) );
		goto __fail;
	}
	if ( rrate != speed )
	{
		Com_Printf( "ALSA adjusted playback rate from %u Hz to %u Hz\n",
			speed, rrate );
		speed = rrate;
	}

	/* set the period time */
	period_time = PERIOD_TIME;
	err = _snd_pcm_hw_params_set_period_time_near( handle, hwparams,
		&period_time, &dir );
	if ( err < 0 )
	{
		Com_Printf( "Unable to set period time %i for playback: %s\n",
			period_time, _snd_strerror( err ) );
		goto __fail;
	}

	periods = NUM_PERIODS;
	err = _snd_pcm_hw_params_set_periods_near( handle, hwparams, &periods, &dir );
	if ( err < 0 )
	{
		Com_Printf( "Unable to set periods (%i): %s",
			periods, _snd_strerror( err ) );
		goto __fail;
	}

	/* get period size */
	err = _snd_pcm_hw_params_get_period_size_min( hwparams, &period_size, &dir );
	if ( err < 0 )
	{
		Com_Printf( "Unable to get period size for playback: %s\n",
			_snd_strerror( err ) );
		goto __fail;
	}
	if ( period_size == 0 || period_size > BUFFER_SIZE / static_cast<unsigned int>( channels * ( bps / 8 ) ) )
	{
		Com_Printf( "ALSA returned invalid period size %lu\n",
			static_cast<unsigned long>( period_size ) );
		goto __fail;
	}

	/* write the parameters to device */
	err = _snd_pcm_hw_params( handle, hwparams );
	if ( err < 0 )
	{
		Com_Printf( "Unable to set hw params for playback: %s\n",
			_snd_strerror( err ) );
		goto __fail;
	}

	err = _snd_pcm_sw_params_current( handle, swparams );
	if ( err < 0 )
	{
		Com_Printf( "Unable to determine current swparams for playback: %s\n",
			_snd_strerror( err ) );
		goto __fail;
	}

	err = _snd_pcm_sw_params_set_start_threshold( handle, swparams, 1 /*period_size*/ );
	if ( err < 0 )
	{
		Com_Printf( "Unable to set start threshold mode for playback: %s\n",
			_snd_strerror( err ) );
		goto __fail;
	}

	/* disable XRUN */
	err = _snd_pcm_sw_params_set_stop_threshold( handle, swparams, -1 );
	if ( err < 0 )
	{
		Com_Printf( "Unable to set stop threshold for playback: " \
			"%s\n", _snd_strerror( err ) );
		goto __fail;
	}

	err = _snd_pcm_sw_params_set_avail_min( handle, swparams, period_size );
	if ( err < 0 )
	{
		Com_Printf( "Unable to set avail min for playback: %s\n",
			_snd_strerror( err ) );
		goto __fail;
	}

	err = _snd_pcm_sw_params( handle, swparams );
	if ( err < 0 )
	{
		Com_Printf( "Unable to set sw params for playback: %s\n",
			_snd_strerror( err ) );
		goto __fail;
	}

#ifdef INT
	Com_Printf( "period_time=%i\n", period_time );
	Com_Printf( "period_size=%i\n", (int)period_size );
#endif

	dma.isfloat = qfalse;
	dma.channels = channels;
	dma.speed = speed;
	dma.samples = sizeof( buffer ) * 8 / bps;
	dma.fullsamples = dma.samples / dma.channels;
	dma.samplebits = bps;
	dma.submission_chunk = 1;
	dma.buffer = buffer;

	buffer_pos = 0; // in monosamples
	buffer_sz = dma.samples * dma.samplebits / 8; // buffer size, in bytes
	frame_sz = dma.channels * dma.samplebits / 8; // frame size, in bytes

	memset( buffer, 0, sizeof( buffer ) );

#ifdef USE_SPINLOCK
	err = _pthread_spin_init( &lock, 0 );
#else
	err = _pthread_mutex_init( &mutex, NULL );
#endif
	if ( err != 0 )
	{
		Com_Printf( "Error creating sound synchronization primitive (%i)\n", err );
		goto __fail;
	}
	sync_initialized = qtrue;

	if ( snd_async.load( std::memory_order_acquire ) )
	{
		const snd_pcm_sframes_t written = _snd_pcm_writei( handle, dma.buffer, period_size * 2 );
		if ( written < 0 )
		{
			Com_Printf( S_COLOR_YELLOW "ALSA initial write error: %s\n",
				_snd_strerror( static_cast<int>( written ) ) );
			goto __fail;
		}
		if ( static_cast<snd_pcm_uframes_t>( written ) != period_size * 2 )
		{
			Com_Printf( S_COLOR_YELLOW "ALSA initial write error: written %li expected %lu\n",
				static_cast<long>( written ), static_cast<unsigned long>( period_size * 2 ) );
			goto __fail;
		}
		if ( __snd_pcm_state( handle ) == SND_PCM_STATE_PREPARED )
		{
			err = _snd_pcm_start( handle );
			if ( err < 0 )
			{
				Com_Printf( "ALSA start error: %s\n", _snd_strerror( err ) );
				goto __fail;
			}
		}
	}
	else
	{
		snd_loop.store( true, std::memory_order_release );
	
		if ( use_mmap )
			err = _pthread_create( &thread, NULL, thread_proc_mmap_entry, NULL );
		else
			err = _pthread_create( &thread, NULL, thread_proc_direct_entry, NULL );

		if ( err != 0 )
		{
			Com_Printf( "Error creating sound thread (%i)\n", err );
			snd_loop.store( false, std::memory_order_release );
			goto __fail;
		}
		thread_started = qtrue;
	}

	snd_inited = qtrue;
	alsa_used = qtrue;
	return qtrue;

__fail:
	if ( thread_started )
	{
		snd_loop.store( false, std::memory_order_release );
		_pthread_join( thread, NULL );
		thread_started = qfalse;
	}
	if ( sync_initialized )
	{
#ifdef USE_SPINLOCK
		_pthread_spin_destroy( &lock );
#else
		_pthread_mutex_destroy( &mutex );
#endif
		sync_initialized = qfalse;
	}
	if ( handle != NULL )
	{
		_snd_pcm_close( handle );
		handle = NULL;
	}
	snd_inited = qfalse;
	snd_loop.store( false, std::memory_order_release );
	snd_async.store( false, std::memory_order_release );
	Com_Memset( &dma, 0, sizeof( dma ) );
	UnloadLibs();
	alsa_used = qfalse;
	return qfalse;
}


qboolean SNDDMA_Init( void )
{
	//Com_Printf( "...trying ASYNC mode\n" );
	//if ( !setup_ALSA( SND_MODE_ASYNC ) )
	{
		Com_Printf( "...trying MMAP mode\n" );
		if ( !setup_ALSA( SND_MODE_MMAP ) )
		{
			Com_Printf( "...trying DIRECT mode\n" );
			if ( !setup_ALSA( SND_MODE_DIRECT ) )
			{
				Com_Printf( "...ALSA setup failed\n" );
				return qfalse;
			}
		}
	}
	return qtrue;
}


void SNDDMA_Shutdown( void )
{
	if ( snd_inited == qfalse )
		return;

	if ( !snd_async.load( std::memory_order_acquire ) )
	{
		snd_loop.store( false, std::memory_order_release );

		/* wait for thread loop exit */
		if ( thread_started ) {
			_pthread_join( thread, NULL );
			thread_started = qfalse;
		}
	}
	else
	{
		_snd_pcm_drain( handle );
	}

	snd_async.store( false, std::memory_order_release );
	snd_inited = qfalse;

	_snd_pcm_close( handle );
	handle = NULL;

	if ( sync_initialized ) {
#ifdef USE_SPINLOCK
		_pthread_spin_destroy( &lock );
#else
		_pthread_mutex_destroy( &mutex );
#endif
		sync_initialized = qfalse;
	}

	UnloadLibs();
	alsa_used = qfalse;
}

#define CASE_STR(x) case (x): s = #x; break;

static void print_state( snd_pcm_state_t state )
{
	const char *s;

	switch( state )
	{
	CASE_STR(SND_PCM_STATE_OPEN);
	CASE_STR(SND_PCM_STATE_SETUP);
	CASE_STR(SND_PCM_STATE_PREPARED);
	CASE_STR(SND_PCM_STATE_RUNNING);
	CASE_STR(SND_PCM_STATE_XRUN);
	CASE_STR(SND_PCM_STATE_DRAINING);
	CASE_STR(SND_PCM_STATE_PAUSED);
	CASE_STR(SND_PCM_STATE_SUSPENDED);
	CASE_STR(SND_PCM_STATE_DISCONNECTED);
	default: s = "SND_PCM_STATE_PRIVATE1"; break;
	};

	Com_Printf( "%s\n", s );
}

static int xrun_recovery( snd_pcm_t *handle, int err )
{
	if ( err == -EPIPE ) /* underrun */
	{
		err = _snd_pcm_prepare( handle );
		if ( err < 0 )
		{
			fprintf( stderr, "Can't recovery from underrun, prepare failed: %s\n",
				_snd_strerror( err ) );
			return err;
		}
		return 0;
	}
	else if ( err == -ESTRPIPE )
	{
		int tries = 0;
		struct timespec req;
		req.tv_sec = period_time / 1000000;
		req.tv_nsec = ( period_time % 1000000 ) * 1000;

		/* wait until the suspend flag is released */
		while ( ( err = _snd_pcm_resume( handle ) ) == -EAGAIN )
		{
			nanosleep( &req, NULL );
			if ( tries++ >= 16 )
			{
				break;
			}
		}
		if ( err < 0 )
		{
			err = _snd_pcm_prepare( handle );
			if ( err < 0 )
			{
				fprintf( stderr, "Can't recovery from suspend, prepare failed: %s\n",
					_snd_strerror( err ) );
				return err;
			}
		}
		return 0;
	}
//	Com_Printf( "error: %i\n", err );
	return err;
}


static int restore_transfer( void )
{
	snd_pcm_state_t state;
	int err;

	state = __snd_pcm_state( handle );

	if ( state == SND_PCM_STATE_XRUN )
	{
		//print_state( state );
		err = xrun_recovery( handle, -EPIPE );
		if ( err < 0 )
		{
			fprintf( stderr, "XRUN recovery failed: %s\n",
				_snd_strerror( err ) );
			return err;
		}
		buffer_pos = 0;
	}
	else if ( state == SND_PCM_STATE_SUSPENDED )
	{
		print_state( state );
		err = xrun_recovery( handle, -ESTRPIPE );
		if ( err < 0 )
		{
			fprintf( stderr, "SUSPEND recovery failed: %s\n",
				_snd_strerror( err ) );
			return err;
		}
		buffer_pos = 0;
	}
	return 0;
}


/*
==============
SNDDMA_GetDMAPos
return the current sample position (in mono samples read)
inside the recirculating dma buffer, so the mixing code will know
how many sample are required to fill it up.
===============
*/
int SNDDMA_GetDMAPos( void )
{
	int samples;

	if ( snd_inited == qfalse )
		return 0;

#ifdef USE_SPINLOCK
	_pthread_spin_lock( &lock );
#else
	_pthread_mutex_lock( &mutex );
#endif

	if ( dma.samples )
		samples = (buffer_pos) % dma.samples;
	else
		samples = 0;

#ifdef USE_SPINLOCK
	_pthread_spin_unlock( &lock );
#else
	_pthread_mutex_unlock( &mutex );
#endif

	return samples;
}


static void thread_proc_mmap( void )
{
	const snd_pcm_channel_area_t *areas;
	snd_pcm_sframes_t commitres;
	snd_pcm_uframes_t frames;
	snd_pcm_uframes_t offset;
	snd_pcm_sframes_t avail;
	snd_pcm_state_t state;
	unsigned char *addr;
	int sz0, sz1;
	int err, p;
	pid_t thread_id;

	// adjust thread priority
	thread_id = syscall( SYS_gettid );
	setpriority( PRIO_PROCESS, thread_id, -10 );

	while ( snd_loop.load( std::memory_order_acquire ) )
	{
		if ( restore_transfer() < 0 )
			break;

		_snd_pcm_wait( handle, static_cast<int>( ( period_time + 999 ) / 1000 ) );
		avail = _snd_pcm_avail_update( handle );

		if ( avail < 0 )
			continue;

		state = __snd_pcm_state( handle );

		if ( state == SND_PCM_STATE_PREPARED )
		{
			_snd_pcm_start( handle );
			avail = _snd_pcm_avail( handle ); // sync with hardware
			buffer_pos = 0;
		}

		if ( avail <= 0 )
			continue;

#ifdef USE_SPINLOCK
		_pthread_spin_lock( &lock );
#else
		_pthread_mutex_lock( &mutex );
#endif
		frames = avail;

		err = _snd_pcm_mmap_begin( handle, &areas, &offset, &frames );

		if ( err < 0 )
		{
			if ( (err = xrun_recovery( handle, err ) ) < 0 )
			{
				fprintf( stderr, "MMAP begin error: %s\n",
					_snd_strerror( err ) );
#ifdef USE_SPINLOCK
				_pthread_spin_unlock( &lock );
#else
				_pthread_mutex_unlock( &mutex );
#endif
				continue;
			}
		}

		addr = static_cast<unsigned char *>( areas[0].addr );
		addr += offset * frame_sz;
		sz0 = frames * frame_sz;

		p = buffer_pos * (dma.samplebits / 8);
		while ( sz0 > 0 )
		{
			sz1 = sz0;
			if ( p + sz1 > buffer_sz )
				sz1 = buffer_sz - p;
			memcpy( addr, dma.buffer + p, sz1 );
			p = (p + sz1) % buffer_sz;
			addr += sz1;
			sz0 -= sz1;
		}
		buffer_pos = p / ( dma.samplebits / 8 );

		commitres = _snd_pcm_mmap_commit( handle, offset, frames );
		if ( commitres < 0 || static_cast<snd_pcm_uframes_t>( commitres ) != frames )
		{
			if ( ( err = xrun_recovery( handle, commitres >= 0 ? -EPIPE : commitres ) ) < 0 )
			{
				fprintf( stderr, "MMAP commit error: %s\n", _snd_strerror( err ) );
#ifdef USE_SPINLOCK
				_pthread_spin_unlock( &lock );
#else
				_pthread_mutex_unlock( &mutex );
#endif
				break;
			}
		}
#ifdef USE_SPINLOCK
		_pthread_spin_unlock( &lock );
#else
		_pthread_mutex_unlock( &mutex );
#endif
	}

	_snd_pcm_drop( handle );

	_pthread_exit( 0 );
}


static void thread_proc_direct( void )
{
	snd_pcm_uframes_t size;
	snd_pcm_uframes_t pos;
	snd_pcm_sframes_t avail, x;
	snd_pcm_state_t state;
	pid_t thread_id;
	int err;

	// adjust thread priority
	thread_id = syscall( SYS_gettid );
	setpriority( PRIO_PROCESS, thread_id, -10 );

	/* buffer size in full samples */
	size = dma.samples / dma.channels;

	while ( snd_loop.load( std::memory_order_acquire ) )
	{
		if ( restore_transfer() < 0 )
			break;

		_snd_pcm_wait( handle, static_cast<int>( ( period_time + 999 ) / 1000 ) );
		avail = _snd_pcm_avail_update( handle );

		if ( avail < 0 )
			continue;

		if ( !snd_loop.load( std::memory_order_acquire ) )
			break;

		state = __snd_pcm_state( handle );

		if ( state == SND_PCM_STATE_PREPARED )
		{
			_snd_pcm_start( handle );
			avail = _snd_pcm_avail( handle ); // sync with hardware
			//_snd_pcm_writei( handle, dma.buffer, size );
			buffer_pos = 0;
		}

		if ( avail <= 0 )
			continue;

#ifdef USE_SPINLOCK
		_pthread_spin_lock( &lock );
#else
		_pthread_mutex_lock( &mutex );
#endif

		// buffer position in full samples
		pos = buffer_pos / dma.channels;

		while ( avail > 0 ) {
			x = avail;
			if ( pos + x > size ) {
				x = size - pos;
			}
			err = _snd_pcm_writei( handle, dma.buffer + pos * frame_sz, x );
			if ( err >= 0 ) {
				pos = (pos + x) % size;
				avail -= x;
			} else {
				avail = 0;
			}
		}

		// buffer pos in mono samples again
		buffer_pos = pos * dma.channels;

#ifdef USE_SPINLOCK
		_pthread_spin_unlock( &lock );
#else
		_pthread_mutex_unlock( &mutex );
#endif
	}

	_snd_pcm_drop( handle );

	_pthread_exit( 0 );
}


static void async_proc( snd_async_handler_t *ahandler )
{
	snd_pcm_sframes_t avail;
	snd_pcm_sframes_t x;
	snd_pcm_uframes_t pos;
	snd_pcm_uframes_t size;
	int err;

	if ( !snd_async.load( std::memory_order_acquire ) || !dma.samples )
		return;

	if ( restore_transfer() < 0 )
		return;

	size = dma.samples / dma.channels;

	while ( ( avail = _snd_pcm_avail_update( handle ) ) >= 0 &&
		static_cast<snd_pcm_uframes_t>( avail ) >= period_size )
	{
#ifdef USE_SPINLOCK
		_pthread_spin_lock( &lock );
#else
		_pthread_mutex_lock( &mutex );
#endif
		pos = buffer_pos / dma.channels; // buffer position in full samples

		while ( avail >= 0 && static_cast<snd_pcm_uframes_t>( avail ) >= period_size )
		{
			if ( static_cast<snd_pcm_uframes_t>( avail ) > period_size )
				x = static_cast<snd_pcm_sframes_t>( period_size );
			else
				x = avail;

			if ( pos + x > size )
				x = size - pos;

			err = _snd_pcm_writei( handle, dma.buffer + pos * frame_sz, x );
			if ( err >= 0 )
			{
				pos = (pos + x) % size;
				avail -= x;
			}
			else
			{
				fprintf( stderr, "ALSA write error: %s\n", _snd_strerror( err ) );
				break;
			}
		}

		buffer_pos = pos * dma.channels; // buffer pos in mono samples again

#ifdef USE_SPINLOCK
		_pthread_spin_unlock( &lock );
#else
		_pthread_mutex_unlock( &mutex );
#endif
	}
}

#else // legacy OSS code path

#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <limits>
#ifdef __linux__
#include <linux/soundcard.h>
#endif
#ifdef __FreeBSD__
#include <sys/soundcard.h>
#endif
#include <stdio.h>

#include "../client/audio/snd_local.h"
#include "../qcommon/q_shared.h"
#include "unix_raii.h"

static qboolean snd_inited = qfalse;
static fnql::posix::ScopedFd audio_fd;
static int map_size;

static cvar_t *snddevice;

/* Some devices may work only with 48000 */
static int tryrates[] = { 22050, 11025, 44100, 48000, 8000 };

void Snd_Memset( void* dest, const int val, const size_t count )
{
	Com_Memset( dest, val, count );
}

qboolean SNDDMA_Init( void )
{
	cvar_t *sndbits;
	cvar_t *sndspeed;
	cvar_t *sndchannels;

	struct audio_buf_info info;
	int rc;
	int fmt;
	int tmp;
	int i;
	int caps;

	if (snd_inited)
		return qtrue;

	sndbits = Cvar_Get("sndbits", "16", CVAR_ARCHIVE_ND | CVAR_LATCH);
	Cvar_SetDescription( sndbits, "Bit resolution." );
	sndspeed = Cvar_Get("sndspeed", "0", CVAR_ARCHIVE_ND | CVAR_LATCH);
	sndchannels = Cvar_Get("sndchannels", "2", CVAR_ARCHIVE_ND |  CVAR_LATCH);
	Cvar_SetDescription( sndchannels, "Number of channels." );
	snddevice = Cvar_Get("snddevice", "/dev/dsp", CVAR_ARCHIVE_ND | CVAR_LATCH);

	map_size = 0;

	// open /dev/dsp, confirm capability to mmap, and get size of dma buffer
	if (!audio_fd) {
		audio_fd.reset( open(snddevice->string, O_RDWR) );

		if (!audio_fd) {
			perror(snddevice->string);
			Com_Printf("Could not open %s\n", snddevice->string);
			return qfalse;
		}
	}

	if (ioctl(audio_fd.get(), SNDCTL_DSP_GETCAPS, &caps) == -1) {
		perror(snddevice->string);
		Com_Printf("Sound driver too old\n");
		audio_fd.reset();
		return qfalse;
	}

	if (!(caps & DSP_CAP_TRIGGER) || !(caps & DSP_CAP_MMAP)) {
		Com_Printf("Sorry but your soundcard can't do this\n");
		audio_fd.reset();
		return qfalse;
	}

	/* SNDCTL_DSP_GETOSPACE moved to be called later */

	// set sample bits & speed
	dma.samplebits = (int)sndbits->value;
	if (dma.samplebits != 16 && dma.samplebits != 8) {
		ioctl(audio_fd.get(), SNDCTL_DSP_GETFMTS, &fmt);
		if (fmt & AFMT_S16_LE) 
			dma.samplebits = 16;
		else if (fmt & AFMT_U8) 
			dma.samplebits = 8;
	}

	dma.speed = (int)sndspeed->value;
	if (!dma.speed) {
		for (i=0 ; i<sizeof(tryrates)/4 ; i++)
			if (!ioctl(audio_fd.get(), SNDCTL_DSP_SPEED, &tryrates[i])) 
				break;
		dma.speed = tryrates[i];
	}

	dma.channels = (int)sndchannels->value;
	if (dma.channels < 1 || dma.channels > 2)
		dma.channels = 2;

	/*  mmap() call moved forward */

	tmp = 0;
	if (dma.channels == 2)
		tmp = 1;
	rc = ioctl(audio_fd.get(), SNDCTL_DSP_STEREO, &tmp);
	if (rc < 0) {
		perror(snddevice->string);
		Com_Printf("Could not set %s to stereo=%d", snddevice->string, dma.channels);
		audio_fd.reset();
		return qfalse;
	}

	if (tmp)
		dma.channels = 2;
	else
		dma.channels = 1;

	rc = ioctl(audio_fd.get(), SNDCTL_DSP_SPEED, &dma.speed);
	if (rc < 0) {
		perror(snddevice->string);
		Com_Printf("Could not set %s speed to %d", snddevice->string, dma.speed);
		audio_fd.reset();
		return qfalse;
	}

	if (dma.samplebits == 16) {
		rc = AFMT_S16_LE;
		rc = ioctl(audio_fd.get(), SNDCTL_DSP_SETFMT, &rc);
		if (rc < 0) {
			perror(snddevice->string);
			Com_Printf("Could not support 16-bit data.  Try 8-bit.\n");
			audio_fd.reset();
			return qfalse;
		}
	} else if (dma.samplebits == 8) {
		rc = AFMT_U8;
		rc = ioctl(audio_fd.get(), SNDCTL_DSP_SETFMT, &rc);
		if (rc < 0) {
			perror(snddevice->string);
			Com_Printf("Could not support 8-bit data.\n");
			audio_fd.reset();
			return qfalse;
		}
	} else {
		perror(snddevice->string);
		Com_Printf("%d-bit sound not supported.", dma.samplebits);
		audio_fd.reset();
		return qfalse;
	}

	if (ioctl(audio_fd.get(), SNDCTL_DSP_GETOSPACE, &info)==-1) {
		perror("GETOSPACE");
		Com_Printf("Um, can't do GETOSPACE?\n");
		audio_fd.reset();
		return qfalse;
	}

	if ( info.fragstotal <= 0 || info.fragsize <= 0 ||
		info.fragstotal > ( std::numeric_limits<int>::max )() / info.fragsize )
	{
		Com_Printf( "Sound driver returned an invalid DMA buffer layout\n" );
		audio_fd.reset();
		return qfalse;
	}
	map_size = info.fragstotal * info.fragsize;
	dma.samples = map_size / (dma.samplebits/8);
	dma.fullsamples = dma.samples / dma.channels;
	dma.submission_chunk = 1;

	// memory map the dma buffer

	// TTimo 2001/10/08 added PROT_READ to the mmap
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=371
	// checking Alsa bug, doesn't allow dma alloc with PROT_READ?

	if (dma.buffer == NULL)
		dma.buffer = (unsigned char *) mmap(NULL, map_size, PROT_WRITE|PROT_READ,
			MAP_FILE|MAP_SHARED, audio_fd.get(), 0);

	if (dma.buffer == MAP_FAILED)
	{
		Com_Printf("Could not mmap dma buffer PROT_WRITE|PROT_READ\n");
		Com_Printf("trying mmap PROT_WRITE (with associated better compatibility / less performance code)\n");
		dma.buffer = (unsigned char *) mmap(NULL, map_size, PROT_WRITE,
			MAP_FILE|MAP_SHARED, audio_fd.get(), 0);
	}

	if (dma.buffer == MAP_FAILED) {
		perror(snddevice->string);
		Com_Printf("Could not mmap %s\n", snddevice->string);
		dma.buffer = NULL;
		audio_fd.reset();
		return qfalse;
	}

	// toggle the trigger & start her up
	tmp = 0;
	rc  = ioctl(audio_fd.get(), SNDCTL_DSP_SETTRIGGER, &tmp);
	if (rc < 0) {
		perror(snddevice->string);
		Com_Printf("Could not toggle.\n");
		munmap(dma.buffer, map_size);
		audio_fd.reset();
		return qfalse;
	}

	tmp = PCM_ENABLE_OUTPUT;
	rc = ioctl(audio_fd.get(), SNDCTL_DSP_SETTRIGGER, &tmp);
	if (rc < 0) {
		perror(snddevice->string);
		Com_Printf("Could not toggle.\n");
		munmap(dma.buffer, map_size);
		audio_fd.reset();
		return qfalse;
	}

	snd_inited = qtrue;
	return qtrue;
}


int SNDDMA_GetDMAPos( void )
{
	struct count_info count;

	if (!snd_inited)
		return 0;

	if (ioctl(audio_fd.get(), SNDCTL_DSP_GETOPTR, &count) == -1)
	{
		perror(snddevice->string);
		Com_Printf("Uh, sound dead.\n");
		audio_fd.reset();
		snd_inited = qfalse;
		return 0;
	}

	return count.ptr / (dma.samplebits / 8);
}


void SNDDMA_Shutdown( void )
{
	if (dma.buffer)
	{
		munmap(dma.buffer, map_size);
		dma.buffer = NULL;
	}

	audio_fd.reset();

	snd_inited = qfalse;
}


/*
==============
SNDDMA_Submit

Send sound to device if buffer isn't really the dma buffer
===============
*/
void SNDDMA_Submit( void )
{
}


void SNDDMA_BeginPainting( void )
{
}

#endif // !defined (__linux__)
