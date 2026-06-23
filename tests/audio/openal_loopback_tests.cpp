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

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>

#include "../../code/client/audio/openal/OpenALCompat.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

constexpr int kSkipExitCode = 77;
constexpr int kSampleRate = 48000;
constexpr int kRenderFrames = 8192;
constexpr int kToneFrames = kRenderFrames * 2;
constexpr float kPi = 3.14159265358979323846f;

class DynamicLibrary {
public:
	~DynamicLibrary() {
		Close();
	}

	bool Open() {
#if defined(_WIN32)
		const char *names[] = { "OpenAL32.dll", "soft_oal.dll" };
#elif defined(__APPLE__)
		const char *names[] = {
			"libopenal.1.dylib",
			"libopenal.dylib",
			"/System/Library/Frameworks/OpenAL.framework/OpenAL"
		};
#else
		const char *names[] = { "libopenal.so.1", "libopenal.so" };
#endif

		for ( const char *name : names ) {
#if defined(_WIN32)
			handle_ = LoadLibraryA( name );
#else
			handle_ = dlopen( name, RTLD_NOW | RTLD_LOCAL );
#endif
			if ( handle_ != nullptr ) {
				name_ = name;
				return true;
			}
		}

		return false;
	}

	void Close() {
		if ( handle_ == nullptr ) {
			return;
		}
#if defined(_WIN32)
		FreeLibrary( handle_ );
#else
		dlclose( handle_ );
#endif
		handle_ = nullptr;
		name_.clear();
	}

	void *Symbol( const char *name ) const {
		if ( handle_ == nullptr || name == nullptr ) {
			return nullptr;
		}
#if defined(_WIN32)
		return reinterpret_cast<void *>( GetProcAddress( handle_, name ) );
#else
		return dlsym( handle_, name );
#endif
	}

	const std::string &Name() const {
		return name_;
	}

private:
#if defined(_WIN32)
	HMODULE handle_ = nullptr;
#else
	void *handle_ = nullptr;
#endif
	std::string name_;
};

struct OpenALSymbols {
	decltype(&::alcGetError) alcGetError = nullptr;
	decltype(&::alcGetString) alcGetString = nullptr;
	decltype(&::alcGetIntegerv) alcGetIntegerv = nullptr;
	decltype(&::alcIsExtensionPresent) alcIsExtensionPresent = nullptr;
	decltype(&::alcGetProcAddress) alcGetProcAddress = nullptr;
	decltype(&::alcCreateContext) alcCreateContext = nullptr;
	decltype(&::alcMakeContextCurrent) alcMakeContextCurrent = nullptr;
	decltype(&::alcDestroyContext) alcDestroyContext = nullptr;
	decltype(&::alcCloseDevice) alcCloseDevice = nullptr;

	decltype(&::alGetError) alGetError = nullptr;
	decltype(&::alGetString) alGetString = nullptr;
	decltype(&::alGetProcAddress) alGetProcAddress = nullptr;
	decltype(&::alIsExtensionPresent) alIsExtensionPresent = nullptr;
	decltype(&::alGenBuffers) alGenBuffers = nullptr;
	decltype(&::alDeleteBuffers) alDeleteBuffers = nullptr;
	decltype(&::alBufferData) alBufferData = nullptr;
	decltype(&::alGenSources) alGenSources = nullptr;
	decltype(&::alDeleteSources) alDeleteSources = nullptr;
	decltype(&::alSourcei) alSourcei = nullptr;
	decltype(&::alSourcef) alSourcef = nullptr;
	decltype(&::alSource3f) alSource3f = nullptr;
	decltype(&::alSourcePlay) alSourcePlay = nullptr;
	decltype(&::alSourceStop) alSourceStop = nullptr;
	decltype(&::alListener3f) alListener3f = nullptr;
	decltype(&::alListenerfv) alListenerfv = nullptr;
	decltype(&::alDistanceModel) alDistanceModel = nullptr;

	LPALCLOOPBACKOPENDEVICESOFT alcLoopbackOpenDeviceSOFT = nullptr;
	LPALCISRENDERFORMATSUPPORTEDSOFT alcIsRenderFormatSupportedSOFT = nullptr;
	LPALCRENDERSAMPLESSOFT alcRenderSamplesSOFT = nullptr;

	LPALGENFILTERS alGenFilters = nullptr;
	LPALDELETEFILTERS alDeleteFilters = nullptr;
	LPALFILTERI alFilteri = nullptr;
	LPALFILTERF alFilterf = nullptr;
};

template<typename T>
bool LoadSymbol( const DynamicLibrary &library, T &target, const char *name ) {
	target = reinterpret_cast<T>( library.Symbol( name ) );
	if ( target == nullptr ) {
		std::printf( "missing OpenAL symbol: %s\n", name );
		return false;
	}
	return true;
}

template<typename T>
T LoadExtension( const DynamicLibrary &library, const OpenALSymbols &al, ALCdevice *device, const char *name ) {
	void *symbol = nullptr;
	if ( al.alcGetProcAddress != nullptr ) {
		symbol = al.alcGetProcAddress( device, name );
		if ( symbol == nullptr ) {
			symbol = al.alcGetProcAddress( nullptr, name );
		}
	}
	if ( symbol == nullptr && al.alGetProcAddress != nullptr ) {
		symbol = al.alGetProcAddress( name );
	}
	if ( symbol == nullptr ) {
		symbol = library.Symbol( name );
	}
	return reinterpret_cast<T>( symbol );
}

bool LoadBaseSymbols( const DynamicLibrary &library, OpenALSymbols &al ) {
	bool ok = true;

	ok = LoadSymbol( library, al.alcGetError, "alcGetError" ) && ok;
	ok = LoadSymbol( library, al.alcGetString, "alcGetString" ) && ok;
	ok = LoadSymbol( library, al.alcGetIntegerv, "alcGetIntegerv" ) && ok;
	ok = LoadSymbol( library, al.alcIsExtensionPresent, "alcIsExtensionPresent" ) && ok;
	ok = LoadSymbol( library, al.alcGetProcAddress, "alcGetProcAddress" ) && ok;
	ok = LoadSymbol( library, al.alcCreateContext, "alcCreateContext" ) && ok;
	ok = LoadSymbol( library, al.alcMakeContextCurrent, "alcMakeContextCurrent" ) && ok;
	ok = LoadSymbol( library, al.alcDestroyContext, "alcDestroyContext" ) && ok;
	ok = LoadSymbol( library, al.alcCloseDevice, "alcCloseDevice" ) && ok;

	ok = LoadSymbol( library, al.alGetError, "alGetError" ) && ok;
	ok = LoadSymbol( library, al.alGetString, "alGetString" ) && ok;
	ok = LoadSymbol( library, al.alGetProcAddress, "alGetProcAddress" ) && ok;
	ok = LoadSymbol( library, al.alIsExtensionPresent, "alIsExtensionPresent" ) && ok;
	ok = LoadSymbol( library, al.alGenBuffers, "alGenBuffers" ) && ok;
	ok = LoadSymbol( library, al.alDeleteBuffers, "alDeleteBuffers" ) && ok;
	ok = LoadSymbol( library, al.alBufferData, "alBufferData" ) && ok;
	ok = LoadSymbol( library, al.alGenSources, "alGenSources" ) && ok;
	ok = LoadSymbol( library, al.alDeleteSources, "alDeleteSources" ) && ok;
	ok = LoadSymbol( library, al.alSourcei, "alSourcei" ) && ok;
	ok = LoadSymbol( library, al.alSourcef, "alSourcef" ) && ok;
	ok = LoadSymbol( library, al.alSource3f, "alSource3f" ) && ok;
	ok = LoadSymbol( library, al.alSourcePlay, "alSourcePlay" ) && ok;
	ok = LoadSymbol( library, al.alSourceStop, "alSourceStop" ) && ok;
	ok = LoadSymbol( library, al.alListener3f, "alListener3f" ) && ok;
	ok = LoadSymbol( library, al.alListenerfv, "alListenerfv" ) && ok;
	ok = LoadSymbol( library, al.alDistanceModel, "alDistanceModel" ) && ok;

	return ok;
}

const char *ALCErrorName( ALCenum error ) {
	switch ( error ) {
	case ALC_NO_ERROR:
		return "ALC_NO_ERROR";
	case ALC_INVALID_DEVICE:
		return "ALC_INVALID_DEVICE";
	case ALC_INVALID_CONTEXT:
		return "ALC_INVALID_CONTEXT";
	case ALC_INVALID_ENUM:
		return "ALC_INVALID_ENUM";
	case ALC_INVALID_VALUE:
		return "ALC_INVALID_VALUE";
	case ALC_OUT_OF_MEMORY:
		return "ALC_OUT_OF_MEMORY";
	default:
		return "ALC_UNKNOWN_ERROR";
	}
}

const char *ALErrorName( ALenum error ) {
	switch ( error ) {
	case AL_NO_ERROR:
		return "AL_NO_ERROR";
	case AL_INVALID_NAME:
		return "AL_INVALID_NAME";
	case AL_INVALID_ENUM:
		return "AL_INVALID_ENUM";
	case AL_INVALID_VALUE:
		return "AL_INVALID_VALUE";
	case AL_INVALID_OPERATION:
		return "AL_INVALID_OPERATION";
	case AL_OUT_OF_MEMORY:
		return "AL_OUT_OF_MEMORY";
	default:
		return "AL_UNKNOWN_ERROR";
	}
}

const char *HrtfStatusName( ALCint status ) {
	switch ( status ) {
	case ALC_HRTF_DISABLED_SOFT:
		return "disabled";
	case ALC_HRTF_ENABLED_SOFT:
		return "enabled";
	case ALC_HRTF_DENIED_SOFT:
		return "denied";
	case ALC_HRTF_REQUIRED_SOFT:
		return "required";
	case ALC_HRTF_HEADPHONES_DETECTED_SOFT:
		return "headphones-detected";
	case ALC_HRTF_UNSUPPORTED_FORMAT_SOFT:
		return "unsupported-format";
	default:
		return "unknown";
	}
}

class TestRunner {
public:
	void Pass( const char *name ) {
		std::printf( "[PASS] %s\n", name );
	}

	void Skip( const char *name, const char *reason ) {
		std::printf( "[SKIP] %s: %s\n", name, reason );
	}

	void Check( bool condition, const char *name, const char *detail ) {
		if ( condition ) {
			std::printf( "[PASS] %s\n", name );
		} else {
			std::printf( "[FAIL] %s: %s\n", name, detail );
			++failures_;
		}
	}

	int Failures() const {
		return failures_;
	}

private:
	int failures_ = 0;
};

double Rms( const std::vector<float> &samples, int channel, int channels ) {
	double sum = 0.0;
	size_t count = 0;
	if ( channels <= 0 ) {
		return 0.0;
	}
	for ( size_t i = 0; i + static_cast<size_t>( channels ) <= samples.size(); i += static_cast<size_t>( channels ) ) {
		if ( channel >= 0 && channel < channels ) {
			const float sample = samples[i + static_cast<size_t>( channel )];
			sum += static_cast<double>( sample ) * sample;
			++count;
		} else if ( channel < 0 ) {
			for ( int c = 0; c < channels; ++c ) {
				const float sample = samples[i + static_cast<size_t>( c )];
				sum += static_cast<double>( sample ) * sample;
				++count;
			}
		}
	}
	return count > 0 ? std::sqrt( sum / static_cast<double>( count ) ) : 0.0;
}

double Rms( const std::vector<float> &samples, int channel ) {
	return Rms( samples, channel, 2 );
}

std::vector<short> MakeMonoTone( float frequency, float amplitude = 12000.0f ) {
	std::vector<short> pcm( static_cast<size_t>( kToneFrames ) );
	for ( int i = 0; i < kToneFrames; ++i ) {
		const float phase = 2.0f * kPi * frequency * static_cast<float>( i ) / static_cast<float>( kSampleRate );
		pcm[static_cast<size_t>( i )] = static_cast<short>( std::sin( phase ) * amplitude );
	}
	return pcm;
}

std::vector<short> MakeLeftOnlyStereoTone( float frequency ) {
	std::vector<short> pcm( static_cast<size_t>( kToneFrames ) * 2u );
	for ( int i = 0; i < kToneFrames; ++i ) {
		const float phase = 2.0f * kPi * frequency * static_cast<float>( i ) / static_cast<float>( kSampleRate );
		pcm[static_cast<size_t>( i ) * 2u] = static_cast<short>( std::sin( phase ) * 12000.0f );
		pcm[static_cast<size_t>( i ) * 2u + 1u] = 0;
	}
	return pcm;
}

std::vector<short> MakeSurround51Tone() {
	constexpr int kChannels = 6;
	std::vector<short> pcm( static_cast<size_t>( kToneFrames ) * static_cast<size_t>( kChannels ), 0 );
	for ( int i = 0; i < kToneFrames; ++i ) {
		const float t = static_cast<float>( i ) / static_cast<float>( kSampleRate );
		pcm[static_cast<size_t>( i ) * kChannels + 0u] = static_cast<short>( std::sin( 2.0f * kPi * 440.0f * t ) * 7000.0f );
		pcm[static_cast<size_t>( i ) * kChannels + 1u] = static_cast<short>( std::sin( 2.0f * kPi * 660.0f * t ) * 7000.0f );
		pcm[static_cast<size_t>( i ) * kChannels + 2u] = static_cast<short>( std::sin( 2.0f * kPi * 550.0f * t ) * 5000.0f );
		pcm[static_cast<size_t>( i ) * kChannels + 4u] = static_cast<short>( std::sin( 2.0f * kPi * 330.0f * t ) * 4000.0f );
		pcm[static_cast<size_t>( i ) * kChannels + 5u] = static_cast<short>( std::sin( 2.0f * kPi * 770.0f * t ) * 4000.0f );
	}
	return pcm;
}

std::vector<short> MakeSingleChannelTone( int channels, int activeChannel, float frequency ) {
	std::vector<short> pcm( static_cast<size_t>( kToneFrames ) * static_cast<size_t>( channels ), 0 );
	if ( channels <= 0 || activeChannel < 0 || activeChannel >= channels ) {
		return pcm;
	}

	for ( int i = 0; i < kToneFrames; ++i ) {
		const float phase = 2.0f * kPi * frequency * static_cast<float>( i ) / static_cast<float>( kSampleRate );
		pcm[static_cast<size_t>( i ) * static_cast<size_t>( channels ) + static_cast<size_t>( activeChannel )] =
			static_cast<short>( std::sin( phase ) * 12000.0f );
	}
	return pcm;
}

bool CheckAL( const OpenALSymbols &al, TestRunner &runner, const char *name ) {
	const ALenum error = al.alGetError();
	if ( error == AL_NO_ERROR ) {
		return true;
	}
	runner.Check( false, name, ALErrorName( error ) );
	return false;
}

bool ConfigureFilter( const OpenALSymbols &al, ALuint filter, ALenum type, float gainLF, float gainHF ) {
	switch ( type ) {
	case AL_FILTER_LOWPASS:
		al.alFilteri( filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
		al.alFilterf( filter, AL_LOWPASS_GAIN, 1.0f );
		al.alFilterf( filter, AL_LOWPASS_GAINHF, gainHF );
		break;
	case AL_FILTER_HIGHPASS:
		al.alFilteri( filter, AL_FILTER_TYPE, AL_FILTER_HIGHPASS );
		al.alFilterf( filter, AL_HIGHPASS_GAIN, 1.0f );
		al.alFilterf( filter, AL_HIGHPASS_GAINLF, gainLF );
		break;
	case AL_FILTER_BANDPASS:
		al.alFilteri( filter, AL_FILTER_TYPE, AL_FILTER_BANDPASS );
		al.alFilterf( filter, AL_BANDPASS_GAIN, 1.0f );
		al.alFilterf( filter, AL_BANDPASS_GAINLF, gainLF );
		al.alFilterf( filter, AL_BANDPASS_GAINHF, gainHF );
		break;
	default:
		return false;
	}
	return true;
}

bool ConfigureListener( const OpenALSymbols &al, TestRunner &runner, const char *name ) {
	const ALfloat orientation[6] = { 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f };
	al.alListener3f( AL_POSITION, 0.0f, 0.0f, 0.0f );
	al.alListener3f( AL_VELOCITY, 0.0f, 0.0f, 0.0f );
	al.alListenerfv( AL_ORIENTATION, orientation );
	return CheckAL( al, runner, name );
}

void DestroyLoopbackContext( const OpenALSymbols &al, ALCcontext *&context );

bool CreateLoopbackContext( const OpenALSymbols &al, ALCdevice *device, ALCenum renderChannels,
	bool includeHrtfRequest, ALCint hrtfRequest, ALCcontext *&context, TestRunner &runner, const char *name ) {
	context = nullptr;
	std::vector<ALCint> attributes = {
		ALC_FORMAT_CHANNELS_SOFT, renderChannels,
		ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT,
		ALC_FREQUENCY, kSampleRate,
		ALC_REFRESH, 100,
		ALC_MONO_SOURCES, 16,
		ALC_STEREO_SOURCES, 4
	};
	if ( includeHrtfRequest ) {
		attributes.push_back( ALC_HRTF_SOFT );
		attributes.push_back( hrtfRequest );
	}
	attributes.push_back( 0 );

	al.alcGetError( device );
	context = al.alcCreateContext( device, attributes.data() );
	if ( context == nullptr ) {
		std::printf( "[FAIL] %s: could not create loopback context (%s)\n", name, ALCErrorName( al.alcGetError( device ) ) );
		runner.Check( false, name, "loopback context creation failed" );
		return false;
	}
	if ( al.alcMakeContextCurrent( context ) != ALC_TRUE ) {
		std::printf( "[FAIL] %s: could not make context current (%s)\n", name, ALCErrorName( al.alcGetError( device ) ) );
		al.alcDestroyContext( context );
		context = nullptr;
		runner.Check( false, name, "loopback context activation failed" );
		return false;
	}

	if ( !ConfigureListener( al, runner, name ) ) {
		DestroyLoopbackContext( al, context );
		return false;
	}
	return true;
}

void DestroyLoopbackContext( const OpenALSymbols &al, ALCcontext *&context ) {
	if ( context != nullptr ) {
		al.alcMakeContextCurrent( nullptr );
		al.alcDestroyContext( context );
		context = nullptr;
	}
}

double RenderMonoTone( const OpenALSymbols &al, ALCdevice *device, float frequency, float distance,
	ALenum distanceModel, ALenum filterType, float filterGainLF, float filterGainHF, TestRunner &runner ) {
	std::vector<short> pcm = MakeMonoTone( frequency );
	std::vector<float> rendered( static_cast<size_t>( kRenderFrames ) * 2u, 0.0f );
	ALuint buffer = 0;
	ALuint source = 0;
	ALuint filter = 0;

	al.alGetError();
	al.alGenBuffers( 1, &buffer );
	al.alBufferData( buffer, AL_FORMAT_MONO16, pcm.data(), static_cast<ALsizei>( pcm.size() * sizeof( short ) ), kSampleRate );
	al.alGenSources( 1, &source );
	if ( al.alGetError() != AL_NO_ERROR || buffer == 0 || source == 0 ) {
		runner.Check( false, "mono render setup", "could not create buffer/source" );
		if ( source != 0 ) {
			al.alDeleteSources( 1, &source );
		}
		if ( buffer != 0 ) {
			al.alDeleteBuffers( 1, &buffer );
		}
		return 0.0;
	}

	al.alDistanceModel( distanceModel );
	al.alSourcei( source, AL_BUFFER, static_cast<ALint>( buffer ) );
	al.alSourcei( source, AL_SOURCE_RELATIVE, AL_FALSE );
	al.alSourcef( source, AL_GAIN, 0.85f );
	al.alSourcef( source, AL_REFERENCE_DISTANCE, 1.0f );
	al.alSourcef( source, AL_MAX_DISTANCE, 64.0f );
	al.alSourcef( source, AL_ROLLOFF_FACTOR, 1.0f );
	al.alSource3f( source, AL_POSITION, 0.0f, 0.0f, -distance );

	if ( filterType != AL_FILTER_NULL && al.alGenFilters != nullptr ) {
		al.alGenFilters( 1, &filter );
		if ( filter != 0 && ConfigureFilter( al, filter, filterType, filterGainLF, filterGainHF ) ) {
			al.alSourcei( source, AL_DIRECT_FILTER, static_cast<ALint>( filter ) );
		}
	}

	if ( !CheckAL( al, runner, "mono render source configuration" ) ) {
		if ( filter != 0 ) {
			al.alDeleteFilters( 1, &filter );
		}
		al.alDeleteSources( 1, &source );
		al.alDeleteBuffers( 1, &buffer );
		return 0.0;
	}

	al.alSourcePlay( source );
	al.alcRenderSamplesSOFT( device, rendered.data(), kRenderFrames );
	al.alSourceStop( source );
	if ( filter != 0 ) {
		al.alDeleteFilters( 1, &filter );
	}
	al.alDeleteSources( 1, &source );
	al.alDeleteBuffers( 1, &buffer );
	CheckAL( al, runner, "mono render cleanup" );

	return Rms( rendered, -1 );
}

ALint DirectChannelMode( bool directChannelsAvailable, bool directChannelsRemixAvailable ) {
	if ( !directChannelsAvailable ) {
		return AL_FALSE;
	}
	return directChannelsRemixAvailable ? AL_REMIX_UNMATCHED_SOFT : AL_TRUE;
}

struct SpeakerLayout {
	const char *name;
	int channels;
	ALCenum alcChannels;
	ALenum alBufferFormat;
	bool requiresMcFormats;
};

std::vector<double> RenderDirectChannelProbe( const OpenALSymbols &al, ALCdevice *device, const SpeakerLayout &layout,
	int activeChannel, bool directChannelsAvailable, bool directChannelsRemixAvailable, TestRunner &runner ) {
	std::vector<short> pcm = MakeSingleChannelTone( layout.channels, activeChannel, 440.0f + 110.0f * static_cast<float>( activeChannel ) );
	std::vector<float> rendered( static_cast<size_t>( kRenderFrames ) * static_cast<size_t>( layout.channels ), 0.0f );
	std::vector<double> rms( static_cast<size_t>( layout.channels ), 0.0 );
	ALuint buffer = 0;
	ALuint source = 0;

	al.alGetError();
	al.alGenBuffers( 1, &buffer );
	al.alBufferData( buffer, layout.alBufferFormat, pcm.data(), static_cast<ALsizei>( pcm.size() * sizeof( short ) ), kSampleRate );
	al.alGenSources( 1, &source );
	al.alSourcei( source, AL_BUFFER, static_cast<ALint>( buffer ) );
	al.alSourcei( source, AL_SOURCE_RELATIVE, AL_TRUE );
	al.alSourcef( source, AL_GAIN, 0.85f );
	al.alSource3f( source, AL_POSITION, 0.0f, 0.0f, 0.0f );
	if ( directChannelsAvailable ) {
		al.alSourcei( source, AL_DIRECT_CHANNELS_SOFT, DirectChannelMode( directChannelsAvailable, directChannelsRemixAvailable ) );
	}

	const std::string configName = std::string( layout.name ) + " direct probe source configuration";
	if ( !CheckAL( al, runner, configName.c_str() ) ) {
		if ( source != 0 ) {
			al.alDeleteSources( 1, &source );
		}
		if ( buffer != 0 ) {
			al.alDeleteBuffers( 1, &buffer );
		}
		return rms;
	}

	al.alSourcePlay( source );
	al.alcRenderSamplesSOFT( device, rendered.data(), kRenderFrames );
	al.alSourceStop( source );
	al.alDeleteSources( 1, &source );
	al.alDeleteBuffers( 1, &buffer );
	const std::string cleanupName = std::string( layout.name ) + " direct probe cleanup";
	CheckAL( al, runner, cleanupName.c_str() );

	for ( int channel = 0; channel < layout.channels; ++channel ) {
		rms[static_cast<size_t>( channel )] = Rms( rendered, channel, layout.channels );
	}
	return rms;
}

void SpeakerLayoutValidationSuite( const OpenALSymbols &al, ALCdevice *device, bool multiChannelFormatsAvailable,
	bool directChannelsAvailable, bool directChannelsRemixAvailable, TestRunner &runner ) {
	const SpeakerLayout layouts[] = {
		{ "stereo", 2, ALC_STEREO_SOFT, AL_FORMAT_STEREO16, false },
		{ "quad", 4, ALC_QUAD_SOFT, AL_FORMAT_QUAD16, true },
		{ "5.1", 6, ALC_5POINT1_SOFT, AL_FORMAT_51CHN16, true },
		{ "6.1", 7, ALC_6POINT1_SOFT, AL_FORMAT_61CHN16, true },
		{ "7.1", 8, ALC_7POINT1_SOFT, AL_FORMAT_71CHN16, true }
	};

	for ( const SpeakerLayout &layout : layouts ) {
		const std::string testPrefix = std::string( layout.name ) + " speaker layout";
		if ( layout.requiresMcFormats && !multiChannelFormatsAvailable ) {
			runner.Skip( testPrefix.c_str(), "AL_EXT_MCFORMATS unavailable" );
			continue;
		}
		if ( al.alcIsRenderFormatSupportedSOFT( device, kSampleRate, layout.alcChannels, ALC_FLOAT_SOFT ) != ALC_TRUE ) {
			runner.Skip( testPrefix.c_str(), "loopback render format unsupported" );
			continue;
		}

		ALCcontext *layoutContext = nullptr;
		const std::string contextName = testPrefix + " context";
		if ( !CreateLoopbackContext( al, device, layout.alcChannels, false, ALC_FALSE, layoutContext, runner, contextName.c_str() ) ) {
			continue;
		}

		bool audible = true;
		bool ordered = true;
		bool isolated = true;
		double loudestObserved = 0.0;
		double worstLeakageRatio = 0.0;

		for ( int active = 0; active < layout.channels; ++active ) {
			const std::vector<double> rms = RenderDirectChannelProbe( al, device, layout, active,
				directChannelsAvailable, directChannelsRemixAvailable, runner );
			const auto loudestIt = std::max_element( rms.begin(), rms.end() );
			const int loudest = static_cast<int>( std::distance( rms.begin(), loudestIt ) );
			const double activeRms = rms[static_cast<size_t>( active )];
			double loudestLeak = 0.0;
			for ( int channel = 0; channel < layout.channels; ++channel ) {
				if ( channel != active ) {
					loudestLeak = ( std::max )( loudestLeak, rms[static_cast<size_t>( channel )] );
				}
			}

			loudestObserved = ( std::max )( loudestObserved, activeRms );
			if ( activeRms <= 0.01 ) {
				audible = false;
			}
			if ( loudest != active ) {
				ordered = false;
			}
			if ( activeRms > 0.0 ) {
				worstLeakageRatio = ( std::max )( worstLeakageRatio, loudestLeak / activeRms );
				if ( loudestLeak > activeRms * 0.12 ) {
					isolated = false;
				}
			}
		}

		std::printf( "%s layout: peak active RMS %.6f, worst leakage ratio %.3f\n",
			layout.name, loudestObserved, worstLeakageRatio );
		runner.Check( audible, ( testPrefix + " active channels audible" ).c_str(), "one or more channels rendered too quietly" );
		if ( directChannelsAvailable ) {
			runner.Check( ordered, ( testPrefix + " channel order" ).c_str(), "loudest rendered channel did not match input channel" );
			runner.Check( isolated, ( testPrefix + " direct isolation" ).c_str(), "direct-channel output leaked too much into other channels" );
		} else {
			runner.Skip( ( testPrefix + " direct isolation" ).c_str(), "AL_SOFT_direct_channels unavailable" );
		}

		DestroyLoopbackContext( al, layoutContext );
	}
}

std::array<double, 2> RenderStereoLeftOnly( const OpenALSymbols &al, ALCdevice *device, bool directChannelsAvailable, bool directChannelsRemixAvailable, TestRunner &runner ) {
	std::vector<short> pcm = MakeLeftOnlyStereoTone( 1000.0f );
	std::vector<float> rendered( static_cast<size_t>( kRenderFrames ) * 2u, 0.0f );
	ALuint buffer = 0;
	ALuint source = 0;

	al.alGetError();
	al.alGenBuffers( 1, &buffer );
	al.alBufferData( buffer, AL_FORMAT_STEREO16, pcm.data(), static_cast<ALsizei>( pcm.size() * sizeof( short ) ), kSampleRate );
	al.alGenSources( 1, &source );
	al.alSourcei( source, AL_BUFFER, static_cast<ALint>( buffer ) );
	al.alSourcei( source, AL_SOURCE_RELATIVE, AL_TRUE );
	al.alSourcef( source, AL_GAIN, 0.85f );
	al.alSource3f( source, AL_POSITION, 0.0f, 0.0f, 0.0f );
	if ( directChannelsAvailable ) {
		al.alSourcei( source, AL_DIRECT_CHANNELS_SOFT, DirectChannelMode( directChannelsAvailable, directChannelsRemixAvailable ) );
	}
	if ( !CheckAL( al, runner, "stereo render source configuration" ) ) {
		if ( source != 0 ) {
			al.alDeleteSources( 1, &source );
		}
		if ( buffer != 0 ) {
			al.alDeleteBuffers( 1, &buffer );
		}
		return { 0.0, 0.0 };
	}

	al.alSourcePlay( source );
	al.alcRenderSamplesSOFT( device, rendered.data(), kRenderFrames );
	al.alSourceStop( source );
	al.alDeleteSources( 1, &source );
	al.alDeleteBuffers( 1, &buffer );
	CheckAL( al, runner, "stereo render cleanup" );

	return { Rms( rendered, 0 ), Rms( rendered, 1 ) };
}

void RenderSilenceTest( const OpenALSymbols &al, ALCdevice *device, TestRunner &runner ) {
	std::vector<float> rendered( 1024u * 2u, 0.0f );
	al.alcRenderSamplesSOFT( device, rendered.data(), 1024 );
	const double rms = Rms( rendered, -1 );
	runner.Check( rms < 0.000001, "idle loopback silence", "loopback rendered non-silent idle output" );
}

void HrtfStatusTest( const OpenALSymbols &al, ALCdevice *device, bool hrtfAvailable, TestRunner &runner ) {
	if ( !hrtfAvailable ) {
		runner.Skip( "HRTF status query", "ALC_SOFT_HRTF unavailable" );
		return;
	}

	ALCint status = -1;
	al.alcGetError( device );
	al.alcGetIntegerv( device, ALC_HRTF_STATUS_SOFT, 1, &status );
	const ALCenum error = al.alcGetError( device );
	if ( error == ALC_NO_ERROR ) {
		std::printf( "HRTF status: %s (%d)\n", HrtfStatusName( status ), status );
		runner.Pass( "HRTF status query" );
	} else {
		std::printf( "[FAIL] HRTF status query: %s\n", ALCErrorName( error ) );
		runner.Check( false, "HRTF status query", "ALC_HRTF_STATUS_SOFT query failed" );
	}
}

void HrtfModeSwitchTest( const OpenALSymbols &al, ALCdevice *device, bool hrtfAvailable, TestRunner &runner ) {
	if ( !hrtfAvailable ) {
		runner.Skip( "HRTF mode switch", "ALC_SOFT_HRTF unavailable" );
		return;
	}
	if ( al.alcIsRenderFormatSupportedSOFT( device, kSampleRate, ALC_STEREO_SOFT, ALC_FLOAT_SOFT ) != ALC_TRUE ) {
		runner.Skip( "HRTF mode switch", "stereo float loopback unsupported" );
		return;
	}

	struct HrtfRequest {
		const char *name;
		ALCint request;
	};
	const HrtfRequest requests[] = {
		{ "off", ALC_FALSE },
		{ "on", ALC_TRUE }
	};

	for ( const HrtfRequest &request : requests ) {
		ALCcontext *context = nullptr;
		const std::string contextName = std::string( "HRTF " ) + request.name + " loopback context";
		if ( !CreateLoopbackContext( al, device, ALC_STEREO_SOFT, true, request.request, context, runner, contextName.c_str() ) ) {
			continue;
		}

		ALCint status = -1;
		al.alcGetError( device );
		al.alcGetIntegerv( device, ALC_HRTF_STATUS_SOFT, 1, &status );
		const ALCenum error = al.alcGetError( device );
		std::printf( "HRTF %s request status: %s (%d)\n", request.name, HrtfStatusName( status ), status );
		const std::string queryName = std::string( "HRTF " ) + request.name + " status query";
		runner.Check( error == ALC_NO_ERROR, queryName.c_str(), "ALC_HRTF_STATUS_SOFT query failed" );
		if ( error == ALC_NO_ERROR ) {
			const std::string modeName = std::string( "HRTF " ) + request.name + " mode honored";
			if ( request.request == ALC_FALSE ) {
				runner.Check( status == ALC_HRTF_DISABLED_SOFT || status == ALC_HRTF_REQUIRED_SOFT,
					modeName.c_str(), "non-HRTF context request did not report disabled/required status" );
			} else {
				runner.Check( status == ALC_HRTF_ENABLED_SOFT || status == ALC_HRTF_HEADPHONES_DETECTED_SOFT ||
						status == ALC_HRTF_REQUIRED_SOFT,
					modeName.c_str(), "HRTF context request was denied or unsupported" );
			}
		}

		DestroyLoopbackContext( al, context );
	}
}

void DistanceModelTest( const OpenALSymbols &al, ALCdevice *device, TestRunner &runner ) {
	const double nearRms = RenderMonoTone( al, device, 1000.0f, 1.0f, AL_INVERSE_DISTANCE_CLAMPED, AL_FILTER_NULL, 1.0f, 1.0f, runner );
	const double farRms = RenderMonoTone( al, device, 1000.0f, 16.0f, AL_INVERSE_DISTANCE_CLAMPED, AL_FILTER_NULL, 1.0f, 1.0f, runner );
	std::printf( "distance RMS: near %.6f, far %.6f\n", nearRms, farRms );
	runner.Check( nearRms > 0.01, "distance model near output", "near source rendered too quietly" );
	runner.Check( farRms < nearRms * 0.35, "distance model attenuation", "far source was not attenuated enough" );
}

void StereoDirectTest( const OpenALSymbols &al, ALCdevice *device, bool directChannelsAvailable, bool directChannelsRemixAvailable, TestRunner &runner ) {
	const std::array<double, 2> rms = RenderStereoLeftOnly( al, device, directChannelsAvailable, directChannelsRemixAvailable, runner );
	std::printf( "stereo RMS: left %.6f, right %.6f\n", rms[0], rms[1] );
	runner.Check( rms[0] > 0.01, "stereo direct left output", "left channel rendered too quietly" );
	if ( directChannelsAvailable ) {
		runner.Check( rms[1] < rms[0] * 0.10, "stereo direct channel isolation", "right channel leaked too much from left-only stereo input" );
	} else {
		runner.Skip( "stereo direct channel isolation", "AL_SOFT_direct_channels unavailable" );
	}
}

void MultiChannelBufferTest( const OpenALSymbols &al, ALCdevice *device, bool multiChannelFormatsAvailable, bool directChannelsAvailable, bool directChannelsRemixAvailable, TestRunner &runner ) {
	if ( !multiChannelFormatsAvailable ) {
		runner.Skip( "5.1 buffer acceptance", "AL_EXT_MCFORMATS unavailable" );
		return;
	}

	std::vector<short> pcm = MakeSurround51Tone();
	std::vector<float> rendered( static_cast<size_t>( kRenderFrames ) * 2u, 0.0f );
	ALuint buffer = 0;
	ALuint source = 0;

	al.alGetError();
	al.alGenBuffers( 1, &buffer );
	al.alBufferData( buffer, AL_FORMAT_51CHN16, pcm.data(), static_cast<ALsizei>( pcm.size() * sizeof( short ) ), kSampleRate );
	al.alGenSources( 1, &source );
	al.alSourcei( source, AL_BUFFER, static_cast<ALint>( buffer ) );
	al.alSourcei( source, AL_SOURCE_RELATIVE, AL_TRUE );
	al.alSourcef( source, AL_GAIN, 0.75f );
	al.alSource3f( source, AL_POSITION, 0.0f, 0.0f, 0.0f );
	if ( directChannelsAvailable ) {
		al.alSourcei( source, AL_DIRECT_CHANNELS_SOFT, DirectChannelMode( directChannelsAvailable, directChannelsRemixAvailable ) );
	}
	if ( !CheckAL( al, runner, "5.1 buffer source configuration" ) ) {
		if ( source != 0 ) {
			al.alDeleteSources( 1, &source );
		}
		if ( buffer != 0 ) {
			al.alDeleteBuffers( 1, &buffer );
		}
		return;
	}

	al.alSourcePlay( source );
	al.alcRenderSamplesSOFT( device, rendered.data(), kRenderFrames );
	al.alSourceStop( source );
	al.alDeleteSources( 1, &source );
	al.alDeleteBuffers( 1, &buffer );
	CheckAL( al, runner, "5.1 buffer cleanup" );

	const double rms = Rms( rendered, -1 );
	std::printf( "5.1 downmix/render RMS: %.6f\n", rms );
	runner.Check( rms > 0.01, "5.1 buffer renders", "5.1 test buffer rendered too quietly" );
}

struct EncodedSoundFieldLayout {
	const char *name;
	int channels;
	ALenum alBufferFormat;
	bool available;
	const char *extensionName;
};

void EncodedSoundFieldBufferTest( const OpenALSymbols &al, bool uhjAvailable, bool bFormatAvailable, TestRunner &runner ) {
	std::printf( "encoded sound-field extensions: UHJ %s, B-Format %s\n",
		uhjAvailable ? "available" : "unavailable",
		bFormatAvailable ? "available" : "unavailable" );

	const EncodedSoundFieldLayout layouts[] = {
		{ "UHJ 2-channel", 2, AL_FORMAT_UHJ2CHN16_SOFT, uhjAvailable, "AL_SOFT_UHJ" },
		{ "UHJ 3-channel", 3, AL_FORMAT_UHJ3CHN16_SOFT, uhjAvailable, "AL_SOFT_UHJ" },
		{ "UHJ 4-channel", 4, AL_FORMAT_UHJ4CHN16_SOFT, uhjAvailable, "AL_SOFT_UHJ" },
		{ "B-Format 2D", 3, AL_FORMAT_BFORMAT2D_16, bFormatAvailable, "AL_EXT_BFORMAT" },
		{ "B-Format 3D", 4, AL_FORMAT_BFORMAT3D_16, bFormatAvailable, "AL_EXT_BFORMAT" }
	};

	for ( const EncodedSoundFieldLayout &layout : layouts ) {
		const std::string testName = std::string( layout.name ) + " buffer acceptance";
		if ( !layout.available ) {
			runner.Skip( testName.c_str(), layout.extensionName );
			continue;
		}

		const std::vector<short> pcm = MakeSingleChannelTone( layout.channels, 0, 440.0f );
		ALuint buffer = 0;
		al.alGetError();
		al.alGenBuffers( 1, &buffer );
		al.alBufferData( buffer, layout.alBufferFormat, pcm.data(), static_cast<ALsizei>( pcm.size() * sizeof( short ) ), kSampleRate );
		const ALenum uploadError = al.alGetError();
		const bool accepted = ( uploadError == AL_NO_ERROR );
		runner.Check( accepted, testName.c_str(), "encoded sound-field buffer upload failed" );
		if ( buffer != 0 ) {
			al.alDeleteBuffers( 1, &buffer );
		}
		if ( accepted ) {
			CheckAL( al, runner, ( std::string( layout.name ) + " buffer cleanup" ).c_str() );
		}
	}
}

void FilterMatrixTest( const OpenALSymbols &al, ALCdevice *device, bool efxAvailable, TestRunner &runner ) {
	if ( !efxAvailable || al.alGenFilters == nullptr || al.alDeleteFilters == nullptr ||
		al.alFilteri == nullptr || al.alFilterf == nullptr ) {
		runner.Skip( "EFX filter matrix", "ALC_EXT_EFX filter functions unavailable" );
		return;
	}

	const double highUnfiltered = RenderMonoTone( al, device, 8000.0f, 1.0f, AL_NONE, AL_FILTER_NULL, 1.0f, 1.0f, runner );
	const double highLowPassed = RenderMonoTone( al, device, 8000.0f, 1.0f, AL_NONE, AL_FILTER_LOWPASS, 1.0f, 0.08f, runner );
	const double lowUnfiltered = RenderMonoTone( al, device, 160.0f, 1.0f, AL_NONE, AL_FILTER_NULL, 1.0f, 1.0f, runner );
	const double lowHighPassed = RenderMonoTone( al, device, 160.0f, 1.0f, AL_NONE, AL_FILTER_HIGHPASS, 0.08f, 1.0f, runner );
	const double bandLow = RenderMonoTone( al, device, 160.0f, 1.0f, AL_NONE, AL_FILTER_BANDPASS, 0.10f, 0.10f, runner );
	const double bandMid = RenderMonoTone( al, device, 1000.0f, 1.0f, AL_NONE, AL_FILTER_BANDPASS, 0.10f, 0.10f, runner );
	const double bandHigh = RenderMonoTone( al, device, 8000.0f, 1.0f, AL_NONE, AL_FILTER_BANDPASS, 0.10f, 0.10f, runner );

	std::printf( "filter RMS: high %.6f -> lowpass %.6f, low %.6f -> highpass %.6f, band low/mid/high %.6f %.6f %.6f\n",
		highUnfiltered, highLowPassed, lowUnfiltered, lowHighPassed, bandLow, bandMid, bandHigh );
	runner.Check( highLowPassed < highUnfiltered * 0.70, "low-pass preset attenuates highs", "low-pass filter did not reduce high tone enough" );
	runner.Check( lowHighPassed < lowUnfiltered * 0.70, "high-pass preset attenuates lows", "high-pass filter did not reduce low tone enough" );
	runner.Check( bandMid > bandLow * 1.35 && bandMid > bandHigh * 1.35, "band-pass preset favors mid band", "band-pass filter did not favor mid frequencies" );
}

} // namespace

int main() {
	TestRunner runner;
	DynamicLibrary library;
	OpenALSymbols al;

	if ( !library.Open() ) {
		std::printf( "[SKIP] OpenAL loopback tests: could not load OpenAL library\n" );
		return kSkipExitCode;
	}
	std::printf( "OpenAL library: %s\n", library.Name().c_str() );

	if ( !LoadBaseSymbols( library, al ) ) {
		std::printf( "[FAIL] OpenAL loopback tests: missing required base symbols\n" );
		return 1;
	}

	al.alcLoopbackOpenDeviceSOFT = LoadExtension<LPALCLOOPBACKOPENDEVICESOFT>( library, al, nullptr, "alcLoopbackOpenDeviceSOFT" );
	if ( al.alcLoopbackOpenDeviceSOFT == nullptr ) {
		std::printf( "[SKIP] OpenAL loopback tests: ALC_SOFT_loopback unavailable\n" );
		return kSkipExitCode;
	}

	ALCdevice *device = al.alcLoopbackOpenDeviceSOFT( nullptr );
	if ( device == nullptr ) {
		std::printf( "[SKIP] OpenAL loopback tests: could not open loopback device\n" );
		return kSkipExitCode;
	}

	al.alcIsRenderFormatSupportedSOFT = LoadExtension<LPALCISRENDERFORMATSUPPORTEDSOFT>( library, al, device, "alcIsRenderFormatSupportedSOFT" );
	al.alcRenderSamplesSOFT = LoadExtension<LPALCRENDERSAMPLESSOFT>( library, al, device, "alcRenderSamplesSOFT" );
	if ( al.alcIsRenderFormatSupportedSOFT == nullptr || al.alcRenderSamplesSOFT == nullptr ) {
		std::printf( "[SKIP] OpenAL loopback tests: loopback render functions unavailable\n" );
		al.alcCloseDevice( device );
		return kSkipExitCode;
	}

	if ( al.alcIsRenderFormatSupportedSOFT( device, kSampleRate, ALC_STEREO_SOFT, ALC_FLOAT_SOFT ) != ALC_TRUE ) {
		std::printf( "[SKIP] OpenAL loopback tests: stereo float %d Hz loopback unsupported\n", kSampleRate );
		al.alcCloseDevice( device );
		return kSkipExitCode;
	}

	const bool hrtfAvailable = al.alcIsExtensionPresent( device, "ALC_SOFT_HRTF" ) == ALC_TRUE;
	std::vector<ALCint> attributes = {
		ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
		ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT,
		ALC_FREQUENCY, kSampleRate,
		ALC_REFRESH, 100,
		ALC_MONO_SOURCES, 16,
		ALC_STEREO_SOURCES, 4
	};
	if ( hrtfAvailable ) {
		attributes.push_back( ALC_HRTF_SOFT );
		attributes.push_back( ALC_TRUE );
	}
	attributes.push_back( 0 );

	ALCcontext *context = al.alcCreateContext( device, attributes.data() );
	if ( context == nullptr && hrtfAvailable ) {
		std::printf( "HRTF loopback context request failed; retrying without HRTF\n" );
		attributes = {
			ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
			ALC_FORMAT_TYPE_SOFT, ALC_FLOAT_SOFT,
			ALC_FREQUENCY, kSampleRate,
			ALC_REFRESH, 100,
			ALC_MONO_SOURCES, 16,
			ALC_STEREO_SOURCES, 4,
			0
		};
		context = al.alcCreateContext( device, attributes.data() );
	}
	if ( context == nullptr ) {
		std::printf( "[FAIL] OpenAL loopback tests: could not create loopback context (%s)\n", ALCErrorName( al.alcGetError( device ) ) );
		al.alcCloseDevice( device );
		return 1;
	}
	if ( al.alcMakeContextCurrent( context ) != ALC_TRUE ) {
		std::printf( "[FAIL] OpenAL loopback tests: could not make context current (%s)\n", ALCErrorName( al.alcGetError( device ) ) );
		al.alcDestroyContext( context );
		al.alcCloseDevice( device );
		return 1;
	}

	const ALchar *vendor = al.alGetString( AL_VENDOR );
	const ALchar *renderer = al.alGetString( AL_RENDERER );
	const ALchar *version = al.alGetString( AL_VERSION );
	std::printf( "OpenAL runtime: vendor=%s renderer=%s version=%s\n",
		vendor != nullptr ? vendor : "unknown",
		renderer != nullptr ? renderer : "unknown",
		version != nullptr ? version : "unknown" );

	ConfigureListener( al, runner, "listener setup" );

	const bool efxAvailable = al.alcIsExtensionPresent( device, "ALC_EXT_EFX" ) == ALC_TRUE ||
		al.alIsExtensionPresent( "AL_EXT_EFX" ) == AL_TRUE;
	const bool directChannelsAvailable = al.alIsExtensionPresent( "AL_SOFT_direct_channels" ) == AL_TRUE;
	const bool directChannelsRemixAvailable = al.alIsExtensionPresent( "AL_SOFT_direct_channels_remix" ) == AL_TRUE;
	const bool multiChannelFormatsAvailable = al.alIsExtensionPresent( "AL_EXT_MCFORMATS" ) == AL_TRUE;
	const bool uhjAvailable = al.alIsExtensionPresent( "AL_SOFT_UHJ" ) == AL_TRUE;
	const bool bFormatAvailable = al.alIsExtensionPresent( "AL_EXT_BFORMAT" ) == AL_TRUE;
	if ( efxAvailable ) {
		al.alGenFilters = LoadExtension<LPALGENFILTERS>( library, al, device, "alGenFilters" );
		al.alDeleteFilters = LoadExtension<LPALDELETEFILTERS>( library, al, device, "alDeleteFilters" );
		al.alFilteri = LoadExtension<LPALFILTERI>( library, al, device, "alFilteri" );
		al.alFilterf = LoadExtension<LPALFILTERF>( library, al, device, "alFilterf" );
	}

	RenderSilenceTest( al, device, runner );
	HrtfStatusTest( al, device, hrtfAvailable, runner );
	DistanceModelTest( al, device, runner );
	StereoDirectTest( al, device, directChannelsAvailable, directChannelsRemixAvailable, runner );
	MultiChannelBufferTest( al, device, multiChannelFormatsAvailable, directChannelsAvailable, directChannelsRemixAvailable, runner );
	EncodedSoundFieldBufferTest( al, uhjAvailable, bFormatAvailable, runner );
	FilterMatrixTest( al, device, efxAvailable, runner );

	DestroyLoopbackContext( al, context );
	HrtfModeSwitchTest( al, device, hrtfAvailable, runner );
	SpeakerLayoutValidationSuite( al, device, multiChannelFormatsAvailable, directChannelsAvailable, directChannelsRemixAvailable, runner );
	al.alcCloseDevice( device );

	if ( runner.Failures() != 0 ) {
		std::printf( "OpenAL loopback tests failed: %d\n", runner.Failures() );
		return 1;
	}

	std::printf( "OpenAL loopback tests passed\n" );
	return 0;
}
