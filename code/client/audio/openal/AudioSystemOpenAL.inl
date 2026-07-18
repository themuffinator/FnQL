// Included by AudioSystem.cpp inside its private implementation namespace.
// OpenAL dynamic loading, device/context management, EFX, and stream buffers.

class OpenALLoader {
public:
	bool Load();
	void Unload();
	bool Ready() const { return handle_ != nullptr; }
	const std::string &LibraryName() const { return libraryName_; }

#define FNQL_AL_SYMBOLS(X) \
	X(alGetError) \
	X(alGetString) \
	X(alGetIntegerv) \
	X(alIsExtensionPresent) \
	X(alGetProcAddress) \
	X(alGenBuffers) \
	X(alDeleteBuffers) \
	X(alBufferData) \
	X(alGenSources) \
	X(alDeleteSources) \
	X(alSourcePlay) \
	X(alSourceStop) \
	X(alSourcePause) \
	X(alSourcei) \
	X(alSource3i) \
	X(alSourcef) \
	X(alSource3f) \
	X(alSourceQueueBuffers) \
	X(alSourceUnqueueBuffers) \
	X(alGetSourcei) \
	X(alListenerf) \
	X(alListener3f) \
	X(alListenerfv) \
	X(alDistanceModel) \
	X(alDopplerFactor) \
	X(alSpeedOfSound)

#define FNQL_ALC_SYMBOLS(X) \
	X(alcOpenDevice) \
	X(alcCloseDevice) \
	X(alcCreateContext) \
	X(alcDestroyContext) \
	X(alcMakeContextCurrent) \
	X(alcGetError) \
	X(alcGetIntegerv) \
	X(alcIsExtensionPresent) \
	X(alcGetProcAddress) \
	X(alcGetString)

#define FNQL_DECLARE_SYMBOL(name) decltype(&::name) name = nullptr;
	FNQL_AL_SYMBOLS(FNQL_DECLARE_SYMBOL)
	FNQL_ALC_SYMBOLS(FNQL_DECLARE_SYMBOL)
#undef FNQL_DECLARE_SYMBOL

private:
	void *handle_ = nullptr;
	std::string libraryName_;
};

bool OpenALLoader::Load() {
	if ( handle_ != nullptr ) {
		return true;
	}

#if defined(_WIN32)
	std::vector<std::string> candidateLibraries;
	const char *binaryPath = Sys_Pwd();
	if ( binaryPath != nullptr && binaryPath[0] != '\0' ) {
		const std::string binaryDir = binaryPath;
		candidateLibraries.push_back( binaryDir + "\\OpenAL32.dll" );
		candidateLibraries.push_back( binaryDir + "\\soft_oal.dll" );
#if defined(_M_ARM64)
		// No bundled ARM64 OpenAL runtime is staged currently.
#elif defined(_WIN64)
		candidateLibraries.push_back( binaryDir + "\\..\\..\\..\\openal\\windows\\x64\\OpenAL32.dll" );
#else
		candidateLibraries.push_back( binaryDir + "\\..\\..\\..\\openal\\windows\\x86\\OpenAL32.dll" );
#endif
	}
	// Prefer a discoverable OpenAL Soft runtime (its conventional install name
	// is soft_oal.dll) before the legacy Creative router that ships as the
	// system OpenAL32.dll on most Windows installs. The router's "Generic
	// Software" driver has no HRTF and only a subset of EFX.
	candidateLibraries.push_back( "soft_oal.dll" );
	candidateLibraries.push_back( "OpenAL32.dll" );
#elif defined(__APPLE__)
	const char *candidateLibraries[] = {
		"/System/Library/Frameworks/OpenAL.framework/OpenAL",
		"OpenAL.framework/OpenAL",
		"libopenal.1.dylib",
		"libopenal.dylib",
		nullptr
	};
#else
	const char *candidateLibraries[] = { "libopenal.so.1", "libopenal.so", nullptr };
#endif

#if defined(_WIN32)
	for ( const std::string &candidate : candidateLibraries ) {
		handle_ = Sys_LoadLibrary( candidate.c_str() );
		if ( handle_ != nullptr ) {
			libraryName_ = candidate;
			break;
		}
	}
#else
	for ( int i = 0; candidateLibraries[i] != nullptr; ++i ) {
		handle_ = Sys_LoadLibrary( candidateLibraries[i] );
		if ( handle_ != nullptr ) {
			libraryName_ = candidateLibraries[i];
			break;
		}
	}
#endif

	if ( handle_ == nullptr ) {
		return false;
	}

	bool ok = true;

#define FNQL_LOAD_SYMBOL(name) \
	name = reinterpret_cast<decltype(name)>( Sys_LoadFunction( handle_, #name ) ); \
	ok = ok && ( name != nullptr );
	FNQL_AL_SYMBOLS(FNQL_LOAD_SYMBOL)
	FNQL_ALC_SYMBOLS(FNQL_LOAD_SYMBOL)
#undef FNQL_LOAD_SYMBOL

	if ( !ok ) {
		Unload();
	}

	return ok;
}

void OpenALLoader::Unload() {
	if ( handle_ != nullptr ) {
		Sys_UnloadLibrary( handle_ );
	}
	handle_ = nullptr;
	libraryName_.clear();

#define FNQL_CLEAR_SYMBOL(name) name = nullptr;
	FNQL_AL_SYMBOLS(FNQL_CLEAR_SYMBOL)
	FNQL_ALC_SYMBOLS(FNQL_CLEAR_SYMBOL)
#undef FNQL_CLEAR_SYMBOL
}

static std::vector<std::string> SplitALCStringList( const ALCchar *list ) {
	std::vector<std::string> values;
	const char *cursor = reinterpret_cast<const char *>( list );
	if ( cursor == nullptr ) {
		return values;
	}

	while ( cursor[0] != '\0' ) {
		values.emplace_back( cursor );
		cursor += std::strlen( cursor ) + 1;
	}

	return values;
}

static bool LoaderHasALCExtension( const OpenALLoader &loader, ALCdevice *device, const char *extensionName, bool includeDeviceLess ) {
	if ( !loader.Ready() || extensionName == nullptr || extensionName[0] == '\0' ) {
		return false;
	}

	if ( loader.alcIsExtensionPresent != nullptr ) {
		if ( loader.alcIsExtensionPresent( device, extensionName ) == ALC_TRUE ) {
			return true;
		}
		if ( includeDeviceLess && loader.alcIsExtensionPresent( nullptr, extensionName ) == ALC_TRUE ) {
			return true;
		}
	}

	if ( loader.alcGetString != nullptr ) {
		const ALCchar *extensions = loader.alcGetString( device, ALC_EXTENSIONS );
		if ( ExtensionListContains( reinterpret_cast<const char *>( extensions ), extensionName ) ) {
			return true;
		}
		if ( includeDeviceLess ) {
			extensions = loader.alcGetString( nullptr, ALC_EXTENSIONS );
			if ( ExtensionListContains( reinterpret_cast<const char *>( extensions ), extensionName ) ) {
				return true;
			}
		}
	}

	return false;
}

static bool LoaderQueryALCInt( const OpenALLoader &loader, ALCdevice *device, ALCenum param, ALCint &value ) {
	if ( !loader.Ready() || loader.alcGetIntegerv == nullptr || device == nullptr ) {
		return false;
	}

	loader.alcGetError( device );
	ALCint queried = 0;
	loader.alcGetIntegerv( device, param, 1, &queried );
	if ( loader.alcGetError( device ) != ALC_NO_ERROR ) {
		return false;
	}

	value = queried;
	return true;
}

static void *LoaderGetALCProcAddress( const OpenALLoader &loader, ALCdevice *device, const char *name ) {
	if ( !loader.Ready() || name == nullptr || name[0] == '\0' ) {
		return nullptr;
	}

	void *proc = nullptr;
	if ( loader.alcGetProcAddress != nullptr ) {
		proc = loader.alcGetProcAddress( device, name );
	}
	if ( proc == nullptr && loader.alGetProcAddress != nullptr ) {
		proc = loader.alGetProcAddress( name );
	}
	return proc;
}

static void PrintHrtfSpecifierList( const std::vector<std::string> &specifiers, const char *requestedHrtfId ) {
	if ( specifiers.empty() ) {
		Com_Printf( "Available HRTFs: none reported\n" );
		return;
	}

	int requestedNumericId = -1;
	const bool hasRequestedNumericId = ParseNonNegativeInt( requestedHrtfId, requestedNumericId );
	Com_Printf( "Available HRTFs: %d\n", static_cast<int>( specifiers.size() ) );
	for ( size_t i = 0; i < specifiers.size(); ++i ) {
		const bool requestedByNumber = hasRequestedNumericId && requestedNumericId == static_cast<int>( i );
		const bool requestedByName = !StringIsBlank( requestedHrtfId ) && !Q_stricmp( requestedHrtfId, specifiers[i].c_str() );
		Com_Printf( "  %c [%d] %s\n",
			( requestedByNumber || requestedByName ) ? '*' : ' ',
			static_cast<int>( i ),
			specifiers[i].c_str() );
	}
}
static std::vector<std::string> EnumerateHrtfSpecifiers( const OpenALLoader &loader, ALCdevice *device, LPALCGETSTRINGISOFT alcGetStringiSOFT, ALCint &hrtfCount ) {
	std::vector<std::string> specifiers;
	hrtfCount = 0;

	ALCint queriedCount = 0;
	if ( LoaderQueryALCInt( loader, device, ALC_NUM_HRTF_SPECIFIERS_SOFT, queriedCount ) ) {
		hrtfCount = ClampInt( queriedCount, 0, 1024 );
	}

	if ( alcGetStringiSOFT == nullptr ) {
		return specifiers;
	}

	for ( ALCint i = 0; i < hrtfCount; ++i ) {
		const ALCchar *specifier = alcGetStringiSOFT( device, ALC_HRTF_SPECIFIER_SOFT, i );
		if ( specifier != nullptr ) {
			specifiers.emplace_back( reinterpret_cast<const char *>( specifier ) );
		}
	}

	return specifiers;
}

static void PrintOpenALDeviceList( const char *activeDeviceName ) {
	OpenALLoader loader;
	Com_Printf( "----- OpenAL Playback Devices -----\n" );
	if ( !loader.Load() ) {
		Com_Printf( "OpenAL library: unavailable\n" );
		Com_Printf( "-----------------------------------\n" );
		return;
	}

	const bool hasEnumerateAll = LoaderHasALCExtension( loader, nullptr, "ALC_ENUMERATE_ALL_EXT", true );
	const bool hasEnumeration = hasEnumerateAll || LoaderHasALCExtension( loader, nullptr, "ALC_ENUMERATION_EXT", true );
	const ALCenum deviceListParam = hasEnumerateAll ? ALC_ALL_DEVICES_SPECIFIER : ALC_DEVICE_SPECIFIER;
	const ALCenum defaultParam = hasEnumerateAll ? ALC_DEFAULT_ALL_DEVICES_SPECIFIER : ALC_DEFAULT_DEVICE_SPECIFIER;
	const ALCchar *defaultDevice = loader.alcGetString( nullptr, defaultParam );
	const std::vector<std::string> devices = hasEnumeration ? SplitALCStringList( loader.alcGetString( nullptr, deviceListParam ) ) : std::vector<std::string>();
	const char *requestedDevice = CvarStringOrDefault( s_alDevice, "default" );

	Com_Printf( "OpenAL library: %s\n", loader.LibraryName().empty() ? "unknown" : loader.LibraryName().c_str() );
	Com_Printf( "Enumeration: %s%s\n",
		AvailableUnavailable( hasEnumeration ),
		hasEnumerateAll ? " (all devices)" : "" );
	Com_Printf( "Requested device: %s\n", requestedDevice );
	Com_Printf( "Default device: %s\n", defaultDevice != nullptr ? reinterpret_cast<const char *>( defaultDevice ) : "unknown" );
	Com_Printf( "Active device: %s\n", StringIsBlank( activeDeviceName ) ? "none" : activeDeviceName );

	if ( devices.empty() ) {
		Com_Printf( "Devices: none reported\n" );
	} else {
		Com_Printf( "Devices: %d\n", static_cast<int>( devices.size() ) );
		for ( size_t i = 0; i < devices.size(); ++i ) {
			const bool isRequested = !StringIsBlank( s_alDevice != nullptr ? s_alDevice->string : nullptr ) && !Q_stricmp( s_alDevice->string, devices[i].c_str() );
			const bool isDefault = defaultDevice != nullptr && !Q_stricmp( reinterpret_cast<const char *>( defaultDevice ), devices[i].c_str() );
			const bool isActive = !StringIsBlank( activeDeviceName ) && !Q_stricmp( activeDeviceName, devices[i].c_str() );
			Com_Printf( "  %c [%d] %s%s%s%s\n",
				( isRequested || isActive ) ? '*' : ' ',
				static_cast<int>( i ),
				devices[i].c_str(),
				isDefault ? " [default]" : "",
				isRequested ? " [requested]" : "",
				isActive ? " [active]" : "" );
		}
	}

	Com_Printf( "-----------------------------------\n" );
	loader.Unload();
}

static void PrintOpenALHrtfListForRequestedDevice() {
	OpenALLoader loader;
	Com_Printf( "----- OpenAL HRTFs -----\n" );
	if ( !loader.Load() ) {
		Com_Printf( "OpenAL library: unavailable\n" );
		Com_Printf( "------------------------\n" );
		return;
	}

	const std::string requestedDevice = ( s_alDevice != nullptr ) ? SafeString( s_alDevice->string ) : std::string();
	bool usingDefaultFallback = false;
	ALCdevice *device = loader.alcOpenDevice( requestedDevice.empty() ? nullptr : requestedDevice.c_str() );
	if ( device == nullptr && !requestedDevice.empty() ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: requested OpenAL device '%s' could not be opened for HRTF listing; trying default\n", requestedDevice.c_str() );
		device = loader.alcOpenDevice( nullptr );
		usingDefaultFallback = ( device != nullptr );
	}

	if ( device == nullptr ) {
		Com_Printf( "OpenAL device: unavailable\n" );
		Com_Printf( "------------------------\n" );
		loader.Unload();
		return;
	}

	const ALCchar *activeDevice = loader.alcGetString( device, ALC_DEVICE_SPECIFIER );
	const bool hasHrtf = LoaderHasALCExtension( loader, device, "ALC_SOFT_HRTF", false );
	Com_Printf( "OpenAL library: %s\n", loader.LibraryName().empty() ? "unknown" : loader.LibraryName().c_str() );
	Com_Printf( "Requested device: %s\n", requestedDevice.empty() ? "default" : requestedDevice.c_str() );
	Com_Printf( "Opened device: %s%s\n",
		activeDevice != nullptr ? reinterpret_cast<const char *>( activeDevice ) : "unknown",
		usingDefaultFallback ? " [default fallback]" : "" );
	Com_Printf( "ALC_SOFT_HRTF: %s\n", AvailableUnavailable( hasHrtf ) );

	if ( hasHrtf ) {
		LPALCGETSTRINGISOFT alcGetStringiSOFT = reinterpret_cast<LPALCGETSTRINGISOFT>( LoaderGetALCProcAddress( loader, device, "alcGetStringiSOFT" ) );
		ALCint hrtfCount = 0;
		const std::vector<std::string> specifiers = EnumerateHrtfSpecifiers( loader, device, alcGetStringiSOFT, hrtfCount );
		Com_Printf( "HRTF count: %d\n", hrtfCount );
		PrintHrtfSpecifierList( specifiers, s_alHrtfId != nullptr ? s_alHrtfId->string : "" );
	}

	Com_Printf( "------------------------\n" );
	loader.alcCloseDevice( device );
	loader.Unload();
}

class StreamBufferPool;

struct VoiceRouteCache {
	bool valid = false;
	float routedGain = -1.0f;
	float maxGain = -1.0f;
	AudioFilterSettings directTone;
	AudioFilterSettings sendTone;
};

static bool FilterSettingsNearlyEqual( const AudioFilterSettings &a, const AudioFilterSettings &b ) {
	return a.kind == b.kind &&
		std::fabs( a.gain - b.gain ) <= 0.002f &&
		std::fabs( a.gainLF - b.gainLF ) <= 0.002f &&
		std::fabs( a.gainHF - b.gainHF ) <= 0.002f;
}

class OpenALDevice {
public:
	bool Init();
	void Shutdown();

	OpenALLoader &AL() { return loader_; }
	const OpenALLoader &AL() const { return loader_; }
	bool Ready() const { return context_ != nullptr; }

	ALuint AcquireVoiceSource();
	void ReleaseVoiceSource( ALuint source );

	ALuint MusicSource() const { return musicSource_; }
	ALuint RawSource() const { return rawSource_; }

	int FreeVoiceCount() const { return static_cast<int>( freeVoiceSources_.size() ); }
	int TotalVoiceCount() const { return static_cast<int>( allVoiceSources_.size() ); }

	const std::string &RequestedDeviceName() const { return requestedDeviceName_; }
	const std::string &ActiveDeviceName() const { return activeDeviceName_; }
	const std::string &LibraryName() const { return loader_.LibraryName(); }
	bool UsingDefaultFallback() const { return usingDefaultFallback_; }
	bool HasEFX() const { return efxAvailable_; }
	bool HasReverb() const { return reverbEnabled_; }
	int MaxAuxiliarySends() const { return maxAuxSends_; }
	const char *CurrentReverbName() const { return currentReverbName_.c_str(); }
	const char *ReverbEffectName() const {
		if ( !reverbEnabled_ ) {
			return "disabled";
		}
		return reverbIsEax_ ? "eaxreverb" : "reverb";
	}
	const ModernOpenALCapabilities &Capabilities() const { return capabilities_; }
	const std::string &ContextAttributeMode() const { return contextAttributeMode_; }
	bool ContextUsedFallback() const { return contextUsedFallback_; }
	void PrintCapabilityMatrix() const;
	void PrintHrtfList() const;
	void PrintOpenALSoftConfigHints() const;
	bool RefreshTimingDiagnostics();
	bool RefreshDeviceConnection();
	bool DeviceConnected() const;
	bool RecoverDevice( bool force );
	void SetMasterGain( float gain );
	void ApplyDopplerState();
	void UpdateListener( const float *origin, const float *velocity, const float axis[3][3] ) const;
	void SetSourceSpatialize( ALuint source, bool spatialize ) const;
	void SetSourceDirectChannels( ALuint source, bool directChannels ) const;
	void ConfigureSourceDistance( ALuint source, bool worldDistance, float referenceScale = kDefaultSoundShaderScale, float rangeScale = kDefaultSoundShaderScale ) const;
	bool UsesOpenALDistanceAttenuation() const;
	bool BeginDeferredUpdates() const;
	void EndDeferredUpdates() const;
	bool QuerySourceLatency( ALuint source, double &offsetSeconds, double &latencyMilliseconds ) const;
	bool SupportsPCMChannels( int channels ) const;
	bool SupportsSampleFormat( const AudioSampleFormat &format ) const;
	ALenum PCM16FormatForChannels( int channels ) const;
	ALenum PCM16FormatForSampleFormat( const AudioSampleFormat &format ) const;
	const char *PCMChannelSupportDescription() const;

	StreamBufferPool &BufferPool();
	bool CreateVoiceFilters( ALuint &directFilter, ALuint &sendFilter );
	void DestroyVoiceFilters( ALuint &directFilter, ALuint &sendFilter );
	void ApplyVoiceRouting( ALuint source, ALuint directFilter, ALuint sendFilter, float sourceGain, float directGain, const AudioFilterSettings &directTone, const AudioFilterSettings &sendTone, VoiceRouteCache &cache ) const;
	void UpdateReverb( const EnvironmentState &environment );

private:
	bool CreateSource( ALuint &sourceOut );
	void ResetSource( ALuint source ) const;
	bool InitEFX();
	void ShutdownEFX();
	void DiscoverALCCapabilities();
	void DiscoverALCapabilities();
	void DiscoverHrtfSpecifiers();
	bool ApplyDistanceModel();
	bool QueryDistanceModel();
	bool HasALCExtension( const char *extensionName, bool includeDeviceLess ) const;
	bool HasALExtension( const char *extensionName ) const;
	bool QueryALCInt( ALCenum param, ALCint &value ) const;
	void *GetALCProcAddress( const char *name ) const;
	void *GetALProcAddress( const char *name ) const;
	bool CreateBestContext();
	bool TryCreateContext( const std::vector<ALCint> &attributes, const char *modeName, bool fallback );
	std::vector<ALCint> BuildContextAttributes( bool includeModernAttributes );
	std::vector<ALCint> BuildDeviceResetAttributes();
	bool ResolveRequestedHrtfId( ALCint &hrtfId ) const;
	void ClearRequestedModernContextAttributes();
	void RefreshActiveDeviceName();
	void RefreshRuntimeStateAfterDeviceReset();
	bool CreateFilter( ALuint &filter );
	bool ConfigureFilter( ALuint filter, const AudioFilterSettings &settings ) const;
	void DestroyFilter( ALuint &filter );

	OpenALLoader loader_;
	ALCdevice *device_ = nullptr;
	ALCcontext *context_ = nullptr;
	ALuint musicSource_ = 0;
	ALuint rawSource_ = 0;
	std::vector<ALuint> allVoiceSources_;
	std::vector<ALuint> freeVoiceSources_;
	std::string requestedDeviceName_;
	std::string activeDeviceName_;
	std::string contextAttributeMode_;
	bool usingDefaultFallback_ = false;
	bool contextUsedFallback_ = false;
	bool efxAvailable_ = false;
	bool filterAvailable_ = false;
	bool filterLowPassSupported_ = false;
	bool filterHighPassSupported_ = false;
	bool filterBandPassSupported_ = false;
	bool reverbEnabled_ = false;
	bool reverbIsEax_ = false;
	float appliedMasterGain_ = -1.0f;
	float appliedDopplerFactor_ = -1.0f;
	float appliedDopplerSpeed_ = -1.0f;
	ModernOpenALCapabilities capabilities_;
	int maxAuxSends_ = 0;
	ALuint auxEffectSlot_ = 0;
	ALuint reverbEffect_ = 0;
	int activeReverbPreset_ = -1;
	int activeReverbTargetPreset_ = -1;
	float activeReverbBlend_ = -1.0f;
	std::string currentReverbName_ = "disabled";
	LPALGENEFFECTS alGenEffects_ = nullptr;
	LPALDELETEEFFECTS alDeleteEffects_ = nullptr;
	LPALEFFECTI alEffecti_ = nullptr;
	LPALEFFECTF alEffectf_ = nullptr;
	LPALGENFILTERS alGenFilters_ = nullptr;
	LPALDELETEFILTERS alDeleteFilters_ = nullptr;
	LPALFILTERI alFilteri_ = nullptr;
	LPALFILTERF alFilterf_ = nullptr;
	LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots_ = nullptr;
	LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots_ = nullptr;
	LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti_ = nullptr;
	StreamBufferPool *bufferPool_ = nullptr;
};

class StreamBufferPool {
public:
	bool Init( OpenALDevice *device, int initialCount );
	void Shutdown();
	ALuint Acquire();
	void Release( ALuint buffer );
	int FreeCount() const { return static_cast<int>( freeBuffers_.size() ); }
	int TotalCount() const { return static_cast<int>( allBuffers_.size() ); }

private:
	OpenALDevice *device_ = nullptr;
	std::vector<ALuint> allBuffers_;
	std::vector<ALuint> freeBuffers_;
};

StreamBufferPool &OpenALDevice::BufferPool() {
	return *bufferPool_;
}

bool OpenALDevice::SupportsPCMChannels( int channels ) const {
	if ( !PCMChannelCountCanBeRepresented( channels ) ) {
		return false;
	}
	if ( channels == 1 || channels == 2 ) {
		return true;
	}
	return capabilities_.multiChannelFormats;
}

bool OpenALDevice::SupportsSampleFormat( const AudioSampleFormat &format ) const {
	if ( !AudioSampleFormatCanBeRepresented( format ) ) {
		return false;
	}

	switch ( format.encoding ) {
	case AudioSampleEncoding::PCM:
		return SupportsPCMChannels( format.channels );
	case AudioSampleEncoding::UHJ:
		return capabilities_.uhj;
	case AudioSampleEncoding::BFormat2D:
	case AudioSampleEncoding::BFormat3D:
		return capabilities_.bFormat;
	default:
		return false;
	}
}

ALenum OpenALDevice::PCM16FormatForSampleFormat( const AudioSampleFormat &format ) const {
	if ( !SupportsSampleFormat( format ) ) {
		return 0;
	}

	switch ( format.encoding ) {
	case AudioSampleEncoding::PCM:
		break;
	case AudioSampleEncoding::UHJ:
		switch ( format.channels ) {
		case 2:
			return AL_FORMAT_UHJ2CHN16_SOFT;
		case 3:
			return AL_FORMAT_UHJ3CHN16_SOFT;
		case 4:
			return AL_FORMAT_UHJ4CHN16_SOFT;
		default:
			return 0;
		}
	case AudioSampleEncoding::BFormat2D:
		return AL_FORMAT_BFORMAT2D_16;
	case AudioSampleEncoding::BFormat3D:
		return AL_FORMAT_BFORMAT3D_16;
	default:
		return 0;
	}

	switch ( format.channels ) {
	case 1:
		return AL_FORMAT_MONO16;
	case 2:
		return AL_FORMAT_STEREO16;
	case 4:
		return AL_FORMAT_QUAD16;
	case 6:
		return AL_FORMAT_51CHN16;
	case 7:
		return AL_FORMAT_61CHN16;
	case 8:
		return AL_FORMAT_71CHN16;
	default:
		return 0;
	}
}

ALenum OpenALDevice::PCM16FormatForChannels( int channels ) const {
	return PCM16FormatForSampleFormat( { AudioSampleEncoding::PCM, channels } );
}

const char *OpenALDevice::PCMChannelSupportDescription() const {
	if ( capabilities_.multiChannelFormats && capabilities_.uhj && capabilities_.bFormat ) {
		return "mono/stereo/quad/5.1/6.1/7.1, UHJ 2/3/4, B-Format 2D/3D";
	}
	if ( capabilities_.multiChannelFormats && capabilities_.uhj ) {
		return "mono/stereo/quad/5.1/6.1/7.1, UHJ 2/3/4";
	}
	if ( capabilities_.multiChannelFormats && capabilities_.bFormat ) {
		return "mono/stereo/quad/5.1/6.1/7.1, B-Format 2D/3D";
	}
	if ( capabilities_.uhj && capabilities_.bFormat ) {
		return "mono/stereo, UHJ 2/3/4, B-Format 2D/3D";
	}
	if ( capabilities_.uhj ) {
		return "mono/stereo, UHJ 2/3/4";
	}
	if ( capabilities_.bFormat ) {
		return "mono/stereo, B-Format 2D/3D";
	}
	return capabilities_.multiChannelFormats ? "mono/stereo/quad/5.1/6.1/7.1" : "mono/stereo";
}

bool OpenALDevice::HasALCExtension( const char *extensionName, bool includeDeviceLess ) const {
	if ( !loader_.Ready() || extensionName == nullptr || extensionName[0] == '\0' ) {
		return false;
	}

	if ( loader_.alcIsExtensionPresent != nullptr ) {
		if ( loader_.alcIsExtensionPresent( device_, extensionName ) == ALC_TRUE ) {
			return true;
		}
		if ( includeDeviceLess && loader_.alcIsExtensionPresent( nullptr, extensionName ) == ALC_TRUE ) {
			return true;
		}
	}

	if ( loader_.alcGetString != nullptr ) {
		const ALCchar *extensions = loader_.alcGetString( device_, ALC_EXTENSIONS );
		if ( ExtensionListContains( reinterpret_cast<const char *>( extensions ), extensionName ) ) {
			return true;
		}

		if ( includeDeviceLess ) {
			extensions = loader_.alcGetString( nullptr, ALC_EXTENSIONS );
			if ( ExtensionListContains( reinterpret_cast<const char *>( extensions ), extensionName ) ) {
				return true;
			}
		}
	}

	return false;
}

bool OpenALDevice::HasALExtension( const char *extensionName ) const {
	if ( !loader_.Ready() || extensionName == nullptr || extensionName[0] == '\0' ) {
		return false;
	}

	if ( loader_.alIsExtensionPresent != nullptr && loader_.alIsExtensionPresent( extensionName ) == AL_TRUE ) {
		return true;
	}

	if ( loader_.alGetString != nullptr ) {
		const ALchar *extensions = loader_.alGetString( AL_EXTENSIONS );
		if ( ExtensionListContains( reinterpret_cast<const char *>( extensions ), extensionName ) ) {
			return true;
		}
	}

	return false;
}

bool OpenALDevice::QueryALCInt( ALCenum param, ALCint &value ) const {
	if ( !loader_.Ready() || loader_.alcGetIntegerv == nullptr || device_ == nullptr ) {
		return false;
	}

	loader_.alcGetError( device_ );
	ALCint queried = 0;
	loader_.alcGetIntegerv( device_, param, 1, &queried );
	if ( loader_.alcGetError( device_ ) != ALC_NO_ERROR ) {
		return false;
	}

	value = queried;
	return true;
}

bool OpenALDevice::RefreshDeviceConnection() {
	capabilities_.connectedQuery = false;
	capabilities_.connectedState = -1;

	ALCint connected = ALC_TRUE;
	if ( !QueryALCInt( ALC_CONNECTED, connected ) ) {
		return false;
	}

	capabilities_.connectedQuery = true;
	capabilities_.connectedState = connected;
	return true;
}

bool OpenALDevice::DeviceConnected() const {
	return !capabilities_.connectedQuery || capabilities_.connectedState != ALC_FALSE;
}

void *OpenALDevice::GetALCProcAddress( const char *name ) const {
	if ( !loader_.Ready() || name == nullptr || name[0] == '\0' ) {
		return nullptr;
	}

	void *proc = nullptr;
	if ( loader_.alcGetProcAddress != nullptr ) {
		proc = loader_.alcGetProcAddress( device_, name );
	}
	if ( proc == nullptr && loader_.alGetProcAddress != nullptr ) {
		proc = loader_.alGetProcAddress( name );
	}
	return proc;
}

void *OpenALDevice::GetALProcAddress( const char *name ) const {
	if ( !loader_.Ready() || name == nullptr || name[0] == '\0' ) {
		return nullptr;
	}

	void *proc = nullptr;
	if ( loader_.alGetProcAddress != nullptr ) {
		proc = loader_.alGetProcAddress( name );
	}
	if ( proc == nullptr && loader_.alcGetProcAddress != nullptr ) {
		proc = loader_.alcGetProcAddress( device_, name );
	}
	return proc;
}

void OpenALDevice::DiscoverHrtfSpecifiers() {
	capabilities_.hrtfSpecifiers.clear();
	capabilities_.hrtfCount = 0;

	if ( !capabilities_.hrtf ) {
		return;
	}

	ALCint value = 0;
	if ( QueryALCInt( ALC_NUM_HRTF_SPECIFIERS_SOFT, value ) ) {
		capabilities_.hrtfCount = ClampInt( value, 0, 1024 );
	}

	if ( capabilities_.alcGetStringiSOFT == nullptr ) {
		return;
	}

	for ( ALCint i = 0; i < capabilities_.hrtfCount; ++i ) {
		const ALCchar *specifier = capabilities_.alcGetStringiSOFT( device_, ALC_HRTF_SPECIFIER_SOFT, i );
		if ( specifier != nullptr ) {
			capabilities_.hrtfSpecifiers.emplace_back( reinterpret_cast<const char *>( specifier ) );
		}
	}
}

bool OpenALDevice::ResolveRequestedHrtfId( ALCint &hrtfId ) const {
	hrtfId = -1;

	if ( s_alHrtfId == nullptr || StringIsBlank( s_alHrtfId->string ) ) {
		return false;
	}

	const char *request = s_alHrtfId->string;
	int numericId = -1;
	if ( ParseNonNegativeInt( request, numericId ) ) {
		if ( capabilities_.hrtfCount > 0 && numericId >= capabilities_.hrtfCount ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: s_alHrtfId %d is outside the available HRTF range 0-%d; using default HRTF\n",
				numericId, ( std::max )( 0, capabilities_.hrtfCount - 1 ) );
			return false;
		}
		hrtfId = numericId;
		return true;
	}

	for ( size_t i = 0; i < capabilities_.hrtfSpecifiers.size(); ++i ) {
		if ( !Q_stricmp( request, capabilities_.hrtfSpecifiers[i].c_str() ) ) {
			hrtfId = static_cast<ALCint>( i );
			return true;
		}
	}

	if ( capabilities_.hrtfSpecifiers.empty() ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: s_alHrtfId '%s' cannot be resolved because the device did not expose HRTF names before context creation; using default HRTF\n", request );
	} else {
		Com_Printf( S_COLOR_YELLOW "WARNING: s_alHrtfId '%s' did not match an available HRTF; using default HRTF\n", request );
	}
	return false;
}

void OpenALDevice::ClearRequestedModernContextAttributes() {
	capabilities_.requestedHrtfAttribute = false;
	capabilities_.requestedHrtf = ALC_DONT_CARE_SOFT;
	capabilities_.requestedHrtfIdAttribute = false;
	capabilities_.requestedHrtfId = -1;
	capabilities_.requestedOutputModeAttribute = false;
	capabilities_.requestedOutputMode = ALC_ANY_SOFT;
	capabilities_.requestedOutputLimiterAttribute = false;
	capabilities_.requestedOutputLimiter = ALC_TRUE;
}

std::vector<ALCint> OpenALDevice::BuildContextAttributes( bool includeModernAttributes ) {
	std::vector<ALCint> attributes;
	attributes.reserve( 18 );

	attributes.push_back( ALC_FREQUENCY );
	attributes.push_back( ClampInt( CvarIntegerOrDefault( s_alFrequency, 48000 ), 8000, 192000 ) );
	attributes.push_back( ALC_REFRESH );
	attributes.push_back( ClampInt( CvarIntegerOrDefault( s_alRefresh, 100 ), 20, 1000 ) );
	attributes.push_back( ALC_MONO_SOURCES );
	attributes.push_back( ClampInt( CvarIntegerOrDefault( s_alMonoSources, 64 ), 16, 256 ) );
	attributes.push_back( ALC_STEREO_SOURCES );
	attributes.push_back( ClampInt( CvarIntegerOrDefault( s_alStereoSources, 8 ), 0, 64 ) );

	if ( includeModernAttributes ) {
		if ( capabilities_.hrtf ) {
			const ALCint hrtfRequest = HrtfRequestFromCvar( s_alHrtf );
			attributes.push_back( ALC_HRTF_SOFT );
			attributes.push_back( hrtfRequest );
			capabilities_.requestedHrtfAttribute = true;
			capabilities_.requestedHrtf = hrtfRequest;

			ALCint hrtfId = -1;
			if ( hrtfRequest != ALC_FALSE && ResolveRequestedHrtfId( hrtfId ) ) {
				attributes.push_back( ALC_HRTF_ID_SOFT );
				attributes.push_back( hrtfId );
				capabilities_.requestedHrtfIdAttribute = true;
				capabilities_.requestedHrtfId = hrtfId;
			}
		} else if ( s_alHrtf != nullptr && Q_stricmp( s_alHrtf->string, "auto" ) ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: s_alHrtf '%s' requested, but ALC_SOFT_HRTF is unavailable on this device\n", s_alHrtf->string );
		}

		if ( capabilities_.outputMode ) {
			ALCint outputMode = ALC_ANY_SOFT;
			if ( OutputModeFromCvar( s_alOutputMode, outputMode ) ) {
				attributes.push_back( ALC_OUTPUT_MODE_SOFT );
				attributes.push_back( outputMode );
				capabilities_.requestedOutputModeAttribute = true;
				capabilities_.requestedOutputMode = outputMode;
			}
		} else if ( s_alOutputMode != nullptr && Q_stricmp( s_alOutputMode->string, "auto" ) ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: s_alOutputMode '%s' requested, but ALC_SOFT_output_mode is unavailable on this device\n", s_alOutputMode->string );
		}

		if ( capabilities_.outputLimiter ) {
			const ALCint outputLimiter = CvarIntegerOrDefault( s_alOutputLimiter, 1 ) ? ALC_TRUE : ALC_FALSE;
			attributes.push_back( ALC_OUTPUT_LIMITER_SOFT );
			attributes.push_back( outputLimiter );
			capabilities_.requestedOutputLimiterAttribute = true;
			capabilities_.requestedOutputLimiter = outputLimiter;
		}
	}

	attributes.push_back( 0 );
	return attributes;
}

std::vector<ALCint> OpenALDevice::BuildDeviceResetAttributes() {
	std::vector<ALCint> attributes;
	attributes.reserve( 14 );
	ClearRequestedModernContextAttributes();

	attributes.push_back( ALC_FREQUENCY );
	attributes.push_back( ClampInt( CvarIntegerOrDefault( s_alFrequency, 48000 ), 8000, 192000 ) );
	attributes.push_back( ALC_REFRESH );
	attributes.push_back( ClampInt( CvarIntegerOrDefault( s_alRefresh, 100 ), 20, 1000 ) );

	if ( capabilities_.hrtf ) {
		const ALCint hrtfRequest = HrtfRequestFromCvar( s_alHrtf );
		attributes.push_back( ALC_HRTF_SOFT );
		attributes.push_back( hrtfRequest );
		capabilities_.requestedHrtfAttribute = true;
		capabilities_.requestedHrtf = hrtfRequest;

		ALCint hrtfId = -1;
		if ( hrtfRequest != ALC_FALSE && ResolveRequestedHrtfId( hrtfId ) ) {
			attributes.push_back( ALC_HRTF_ID_SOFT );
			attributes.push_back( hrtfId );
			capabilities_.requestedHrtfIdAttribute = true;
			capabilities_.requestedHrtfId = hrtfId;
		}
	}

	if ( capabilities_.outputMode ) {
		ALCint outputMode = ALC_ANY_SOFT;
		if ( OutputModeFromCvar( s_alOutputMode, outputMode ) ) {
			attributes.push_back( ALC_OUTPUT_MODE_SOFT );
			attributes.push_back( outputMode );
			capabilities_.requestedOutputModeAttribute = true;
			capabilities_.requestedOutputMode = outputMode;
		}
	}

	if ( capabilities_.outputLimiter ) {
		const ALCint outputLimiter = CvarIntegerOrDefault( s_alOutputLimiter, 1 ) ? ALC_TRUE : ALC_FALSE;
		attributes.push_back( ALC_OUTPUT_LIMITER_SOFT );
		attributes.push_back( outputLimiter );
		capabilities_.requestedOutputLimiterAttribute = true;
		capabilities_.requestedOutputLimiter = outputLimiter;
	}

	attributes.push_back( 0 );
	return attributes;
}

bool OpenALDevice::TryCreateContext( const std::vector<ALCint> &attributes, const char *modeName, bool fallback ) {
	if ( !loader_.Ready() || device_ == nullptr ) {
		return false;
	}

	loader_.alcGetError( device_ );
	ALCcontext *candidate = loader_.alcCreateContext( device_, attributes.empty() ? nullptr : attributes.data() );
	ALCenum error = loader_.alcGetError( device_ );
	if ( candidate == nullptr ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: OpenAL context creation failed for %s attributes (%s)\n",
			modeName, ALCErrorName( error ) );
		return false;
	}

	if ( loader_.alcMakeContextCurrent( candidate ) == ALC_FALSE ) {
		error = loader_.alcGetError( device_ );
		Com_Printf( S_COLOR_YELLOW "WARNING: OpenAL context activation failed for %s attributes (%s)\n",
			modeName, ALCErrorName( error ) );
		loader_.alcDestroyContext( candidate );
		return false;
	}

	context_ = candidate;
	contextAttributeMode_ = modeName != nullptr ? modeName : "unknown";
	contextUsedFallback_ = fallback;
	return true;
}

bool OpenALDevice::CreateBestContext() {
	contextAttributeMode_.clear();
	contextUsedFallback_ = false;
	ClearRequestedModernContextAttributes();

	const std::vector<ALCint> requestedAttributes = BuildContextAttributes( true );
	const std::vector<ALCint> standardAttributes = BuildContextAttributes( false );

	if ( TryCreateContext( requestedAttributes, "requested", false ) ) {
		return true;
	}

	ClearRequestedModernContextAttributes();
	if ( requestedAttributes != standardAttributes ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: retrying OpenAL context without HRTF/output-mode/output-limiter attributes\n" );
		if ( TryCreateContext( standardAttributes, "standard", true ) ) {
			return true;
		}
	}

	Com_Printf( S_COLOR_YELLOW "WARNING: retrying OpenAL context with default attributes\n" );
	const std::vector<ALCint> defaultAttributes;
	return TryCreateContext( defaultAttributes, "default", true );
}

void OpenALDevice::DiscoverALCCapabilities() {
	capabilities_ = ModernOpenALCapabilities();
	if ( !loader_.Ready() || device_ == nullptr ) {
		return;
	}

	ALCint value = 0;
	if ( QueryALCInt( ALC_MAJOR_VERSION, value ) ) {
		capabilities_.alcMajor = value;
	}
	if ( QueryALCInt( ALC_MINOR_VERSION, value ) ) {
		capabilities_.alcMinor = value;
	}
	if ( QueryALCInt( ALC_FREQUENCY, value ) ) {
		capabilities_.mixerFrequency = value;
	}
	if ( QueryALCInt( ALC_REFRESH, value ) ) {
		capabilities_.refreshRate = value;
	}
	if ( QueryALCInt( ALC_MONO_SOURCES, value ) ) {
		capabilities_.monoSources = value;
	}
	if ( QueryALCInt( ALC_STEREO_SOURCES, value ) ) {
		capabilities_.stereoSources = value;
	}

	capabilities_.enumerateAll = HasALCExtension( "ALC_ENUMERATE_ALL_EXT", true ) || HasALCExtension( "ALC_ENUMERATION_EXT", true );
	capabilities_.disconnect = HasALCExtension( "ALC_EXT_disconnect", false );
	capabilities_.hrtf = HasALCExtension( "ALC_SOFT_HRTF", false );
	capabilities_.deviceClock = HasALCExtension( "ALC_SOFT_device_clock", false );
	capabilities_.reopenDevice = HasALCExtension( "ALC_SOFT_reopen_device", false );
	capabilities_.systemEvents = HasALCExtension( "ALC_SOFT_system_events", true );
	capabilities_.outputLimiter = HasALCExtension( "ALC_SOFT_output_limiter", false );
	capabilities_.outputMode = HasALCExtension( "ALC_SOFT_output_mode", false );
	capabilities_.loopback = HasALCExtension( "ALC_SOFT_loopback", true );

	RefreshDeviceConnection();

	if ( capabilities_.hrtf ) {
		capabilities_.alcGetStringiSOFT = reinterpret_cast<LPALCGETSTRINGISOFT>( GetALCProcAddress( "alcGetStringiSOFT" ) );
		capabilities_.alcResetDeviceSOFT = reinterpret_cast<LPALCRESETDEVICESOFT>( GetALCProcAddress( "alcResetDeviceSOFT" ) );
		DiscoverHrtfSpecifiers();
	}
	if ( capabilities_.reopenDevice ) {
		capabilities_.alcReopenDeviceSOFT = reinterpret_cast<LPALCREOPENDEVICESOFT>( GetALCProcAddress( "alcReopenDeviceSOFT" ) );
	}
	if ( capabilities_.systemEvents ) {
		capabilities_.alcEventIsSupportedSOFT = reinterpret_cast<LPALCEVENTISSUPPORTEDSOFT>( GetALCProcAddress( "alcEventIsSupportedSOFT" ) );
		capabilities_.alcEventControlSOFT = reinterpret_cast<LPALCEVENTCONTROLSOFT>( GetALCProcAddress( "alcEventControlSOFT" ) );
		capabilities_.alcEventCallbackSOFT = reinterpret_cast<LPALCEVENTCALLBACKSOFT>( GetALCProcAddress( "alcEventCallbackSOFT" ) );
		if ( capabilities_.alcEventIsSupportedSOFT != nullptr ) {
			capabilities_.defaultPlaybackEventSupport = capabilities_.alcEventIsSupportedSOFT( ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT, ALC_PLAYBACK_DEVICE_SOFT );
			capabilities_.playbackAddedEventSupport = capabilities_.alcEventIsSupportedSOFT( ALC_EVENT_TYPE_DEVICE_ADDED_SOFT, ALC_PLAYBACK_DEVICE_SOFT );
			capabilities_.playbackRemovedEventSupport = capabilities_.alcEventIsSupportedSOFT( ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT, ALC_PLAYBACK_DEVICE_SOFT );
		}
	}
	if ( capabilities_.deviceClock ) {
		capabilities_.alcGetInteger64vSOFT = reinterpret_cast<LPALCGETINTEGER64VSOFT>( GetALCProcAddress( "alcGetInteger64vSOFT" ) );
	}
	if ( capabilities_.loopback ) {
		capabilities_.alcLoopbackOpenDeviceSOFT = reinterpret_cast<LPALCLOOPBACKOPENDEVICESOFT>( GetALCProcAddress( "alcLoopbackOpenDeviceSOFT" ) );
		capabilities_.alcIsRenderFormatSupportedSOFT = reinterpret_cast<LPALCISRENDERFORMATSUPPORTEDSOFT>( GetALCProcAddress( "alcIsRenderFormatSupportedSOFT" ) );
		capabilities_.alcRenderSamplesSOFT = reinterpret_cast<LPALCRENDERSAMPLESSOFT>( GetALCProcAddress( "alcRenderSamplesSOFT" ) );
	}
}

bool OpenALDevice::QueryDistanceModel() {
	capabilities_.distanceModelValid = false;

	if ( !loader_.Ready() || context_ == nullptr || loader_.alGetIntegerv == nullptr ) {
		return false;
	}

	loader_.alGetError();
	ALint distanceModel = AL_NONE;
	loader_.alGetIntegerv( AL_DISTANCE_MODEL, &distanceModel );
	if ( loader_.alGetError() != AL_NO_ERROR ) {
		return false;
	}

	capabilities_.distanceModel = distanceModel;
	capabilities_.distanceModelValid = true;
	return true;
}

bool OpenALDevice::ApplyDistanceModel() {
	capabilities_.requestedDistanceModel = DistanceModelFromCvar( s_alDistanceModel );

	if ( !loader_.Ready() || context_ == nullptr || loader_.alDistanceModel == nullptr ) {
		QueryDistanceModel();
		return false;
	}

	loader_.alGetError();
	loader_.alDistanceModel( capabilities_.requestedDistanceModel );
	if ( loader_.alGetError() != AL_NO_ERROR ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: OpenAL rejected distance model '%s'; retrying inverse_clamped\n",
			ALDistanceModelName( capabilities_.requestedDistanceModel ) );
		capabilities_.requestedDistanceModel = AL_INVERSE_DISTANCE_CLAMPED;
		loader_.alGetError();
		loader_.alDistanceModel( capabilities_.requestedDistanceModel );
		if ( loader_.alGetError() != AL_NO_ERROR ) {
			QueryDistanceModel();
			return false;
		}
	}

	QueryDistanceModel();
	return capabilities_.distanceModelValid && capabilities_.distanceModel == capabilities_.requestedDistanceModel;
}

bool OpenALDevice::RefreshTimingDiagnostics() {
	capabilities_.deviceClockValuesValid = false;
	capabilities_.deviceClockValue = 0;
	capabilities_.deviceLatency = 0;

	if ( !loader_.Ready() || device_ == nullptr || !capabilities_.deviceClock ||
		capabilities_.alcGetInteger64vSOFT == nullptr ) {
		return false;
	}

	ALCint64SOFT clockAndLatency[2] = { 0, 0 };
	loader_.alcGetError( device_ );
	capabilities_.alcGetInteger64vSOFT( device_, ALC_DEVICE_CLOCK_LATENCY_SOFT, 2, clockAndLatency );
	if ( loader_.alcGetError( device_ ) == ALC_NO_ERROR ) {
		capabilities_.deviceClockValue = clockAndLatency[0];
		capabilities_.deviceLatency = clockAndLatency[1];
		capabilities_.deviceClockValuesValid = true;
		return true;
	}

	ALCint64SOFT clock = 0;
	ALCint64SOFT latency = 0;
	loader_.alcGetError( device_ );
	capabilities_.alcGetInteger64vSOFT( device_, ALC_DEVICE_CLOCK_SOFT, 1, &clock );
	const bool haveClock = ( loader_.alcGetError( device_ ) == ALC_NO_ERROR );

	loader_.alcGetError( device_ );
	capabilities_.alcGetInteger64vSOFT( device_, ALC_DEVICE_LATENCY_SOFT, 1, &latency );
	const bool haveLatency = ( loader_.alcGetError( device_ ) == ALC_NO_ERROR );

	if ( haveClock && haveLatency ) {
		capabilities_.deviceClockValue = clock;
		capabilities_.deviceLatency = latency;
		capabilities_.deviceClockValuesValid = true;
		return true;
	}

	return false;
}

void OpenALDevice::DiscoverALCapabilities() {
	if ( !loader_.Ready() || context_ == nullptr ) {
		return;
	}

	ALCint value = 0;
	if ( QueryALCInt( ALC_FREQUENCY, value ) ) {
		capabilities_.mixerFrequency = value;
	}
	if ( QueryALCInt( ALC_REFRESH, value ) ) {
		capabilities_.refreshRate = value;
	}
	if ( QueryALCInt( ALC_MONO_SOURCES, value ) ) {
		capabilities_.monoSources = value;
	}
	if ( QueryALCInt( ALC_STEREO_SOURCES, value ) ) {
		capabilities_.stereoSources = value;
	}

	capabilities_.deferredUpdates = HasALExtension( "AL_SOFT_deferred_updates" );
	capabilities_.directChannels = HasALExtension( "AL_SOFT_direct_channels" );
	capabilities_.directChannelsRemix = HasALExtension( "AL_SOFT_direct_channels_remix" );
	capabilities_.multiChannelFormats = HasALExtension( "AL_EXT_MCFORMATS" );
	capabilities_.bFormat = HasALExtension( "AL_EXT_BFORMAT" );
	capabilities_.uhj = HasALExtension( "AL_SOFT_UHJ" );
	capabilities_.uhjEx = HasALExtension( "AL_SOFT_UHJ_ex" );
	capabilities_.sourceSpatialize = HasALExtension( "AL_SOFT_source_spatialize" );
	capabilities_.sourceLatency = HasALExtension( "AL_SOFT_source_latency" );
	capabilities_.sourceEvents = HasALExtension( "AL_SOFT_events" );

	QueryDistanceModel();

	if ( capabilities_.deferredUpdates ) {
		capabilities_.alDeferUpdatesSOFT = reinterpret_cast<LPALDEFERUPDATESSOFT>( GetALProcAddress( "alDeferUpdatesSOFT" ) );
		capabilities_.alProcessUpdatesSOFT = reinterpret_cast<LPALPROCESSUPDATESSOFT>( GetALProcAddress( "alProcessUpdatesSOFT" ) );
	}
	if ( capabilities_.sourceLatency ) {
		capabilities_.alGetSourcedvSOFT = reinterpret_cast<LPALGETSOURCEDVSOFT>( GetALProcAddress( "alGetSourcedvSOFT" ) );
	}
	if ( capabilities_.sourceEvents ) {
		capabilities_.alEventControlSOFT = reinterpret_cast<LPALEVENTCONTROLSOFT>( GetALProcAddress( "alEventControlSOFT" ) );
		capabilities_.alEventCallbackSOFT = reinterpret_cast<LPALEVENTCALLBACKSOFT>( GetALProcAddress( "alEventCallbackSOFT" ) );
	}

	if ( capabilities_.hrtf ) {
		if ( QueryALCInt( ALC_HRTF_STATUS_SOFT, value ) ) {
			capabilities_.hrtfStatus = value;
		}
		DiscoverHrtfSpecifiers();
	}

	if ( capabilities_.outputLimiter && QueryALCInt( ALC_OUTPUT_LIMITER_SOFT, value ) ) {
		capabilities_.outputLimiterState = value;
	}

	if ( capabilities_.outputMode && QueryALCInt( ALC_OUTPUT_MODE_SOFT, value ) ) {
		capabilities_.outputModeValue = value;
	}

	RefreshTimingDiagnostics();
}

void OpenALDevice::PrintCapabilityMatrix() const {
	const ModernOpenALCapabilities &caps = capabilities_;

	Com_Printf( "OpenAL capability matrix:\n" );
	Com_Printf( "  Context attributes: %s%s\n",
		contextAttributeMode_.empty() ? "not-created" : contextAttributeMode_.c_str(),
		contextUsedFallback_ ? " (fallback)" : "" );
	Com_Printf( "  ALC version: %d.%d\n", caps.alcMajor, caps.alcMinor );
	Com_Printf( "  Mixer: %d Hz refresh %d Hz, mono sources %d, stereo sources %d\n",
		caps.mixerFrequency, caps.refreshRate, caps.monoSources, caps.stereoSources );
	Com_Printf( "  Device enumeration: %s\n", AvailableUnavailable( caps.enumerateAll ) );
	Com_Printf( "  ALC_EXT_disconnect: %s (state %s)\n",
		AvailableUnavailable( caps.disconnect ),
		caps.connectedQuery ? ALCConnectedName( caps.connectedState ) : "not-queried" );
	Com_Printf( "  ALC_SOFT_reopen_device: %s (alcReopenDeviceSOFT %s)\n",
		AvailableUnavailable( caps.reopenDevice ),
		LoadedMissing( caps.alcReopenDeviceSOFT ) );
	Com_Printf( "  ALC_SOFT_system_events: %s (default %s, added %s, removed %s)\n",
		AvailableUnavailable( caps.systemEvents ),
		ALCEventSupportName( caps.defaultPlaybackEventSupport ),
		ALCEventSupportName( caps.playbackAddedEventSupport ),
		ALCEventSupportName( caps.playbackRemovedEventSupport ) );
	Com_Printf( "  ALC_SOFT_HRTF: %s (status %s, HRTFs %d, alcGetStringiSOFT %s, alcResetDeviceSOFT %s)\n",
		AvailableUnavailable( caps.hrtf ),
		( caps.hrtfStatus >= 0 ) ? HrtfStatusName( caps.hrtfStatus ) : "not-queried",
		caps.hrtfCount,
		LoadedMissing( caps.alcGetStringiSOFT ),
		LoadedMissing( caps.alcResetDeviceSOFT ) );
	if ( caps.requestedHrtfAttribute ) {
		if ( caps.requestedHrtfIdAttribute ) {
			Com_Printf( "    requested HRTF attr: %s, id %d\n",
				HrtfRequestName( caps.requestedHrtf ), caps.requestedHrtfId );
		} else {
			Com_Printf( "    requested HRTF attr: %s\n", HrtfRequestName( caps.requestedHrtf ) );
		}
	}
	if ( !caps.hrtfSpecifiers.empty() ) {
		const int printed = ( std::min )( static_cast<int>( caps.hrtfSpecifiers.size() ), 8 );
		for ( int i = 0; i < printed; ++i ) {
			Com_Printf( "    HRTF %d: %s\n", i, caps.hrtfSpecifiers[static_cast<size_t>( i )].c_str() );
		}
		if ( static_cast<int>( caps.hrtfSpecifiers.size() ) > printed ) {
			Com_Printf( "    ... %d more HRTF specifiers\n", static_cast<int>( caps.hrtfSpecifiers.size() ) - printed );
		}
	}
	Com_Printf( "  ALC_SOFT_output_limiter: %s (state %s)\n",
		AvailableUnavailable( caps.outputLimiter ),
		( caps.outputLimiterState >= 0 ) ? ALCBooleanName( caps.outputLimiterState ) : "not-queried" );
	if ( caps.requestedOutputLimiterAttribute ) {
		Com_Printf( "    requested limiter attr: %s\n", ALCBooleanName( caps.requestedOutputLimiter ) );
	}
	Com_Printf( "  ALC_SOFT_output_mode: %s (active %s)\n",
		AvailableUnavailable( caps.outputMode ),
		( caps.outputModeValue >= 0 ) ? ALCOutputModeName( caps.outputModeValue ) : "not-queried" );
	if ( caps.requestedOutputModeAttribute ) {
		Com_Printf( "    requested output mode attr: %s\n", ALCOutputModeName( caps.requestedOutputMode ) );
	}
	Com_Printf( "  ALC_SOFT_device_clock: %s (alcGetInteger64vSOFT %s)\n",
		AvailableUnavailable( caps.deviceClock ),
		LoadedMissing( caps.alcGetInteger64vSOFT ) );
	if ( caps.deviceClockValuesValid ) {
		Com_Printf( "    clock %.3f s (%lld ns), latency %.2f ms (%lld ns)\n",
			NanosecondsToSeconds( caps.deviceClockValue ),
			static_cast<long long>( caps.deviceClockValue ),
			NanosecondsToMilliseconds( caps.deviceLatency ),
			static_cast<long long>( caps.deviceLatency ) );
	} else if ( caps.deviceClock && caps.alcGetInteger64vSOFT != nullptr ) {
		Com_Printf( "    timing query unavailable\n" );
	}
	Com_Printf( "  ALC_SOFT_loopback: %s (open %s, format query %s, render %s)\n",
		AvailableUnavailable( caps.loopback ),
		LoadedMissing( caps.alcLoopbackOpenDeviceSOFT ),
		LoadedMissing( caps.alcIsRenderFormatSupportedSOFT ),
		LoadedMissing( caps.alcRenderSamplesSOFT ) );
	Com_Printf( "  AL_SOFT_deferred_updates: %s (defer %s, process %s)\n",
		AvailableUnavailable( caps.deferredUpdates ),
		LoadedMissing( caps.alDeferUpdatesSOFT ),
		LoadedMissing( caps.alProcessUpdatesSOFT ) );
	Com_Printf( "  AL_SOFT_direct_channels: %s (remix %s)\n",
		AvailableUnavailable( caps.directChannels ),
		YesNo( caps.directChannelsRemix ) );
	Com_Printf( "  AL_EXT_MCFORMATS: %s (PCM layouts %s)\n",
		AvailableUnavailable( caps.multiChannelFormats ),
		PCMChannelSupportDescription() );
	Com_Printf( "  AL_SOFT_UHJ: %s (extended encodings %s)\n",
		AvailableUnavailable( caps.uhj ),
		YesNo( caps.uhjEx ) );
	Com_Printf( "  AL_EXT_BFORMAT: %s\n", AvailableUnavailable( caps.bFormat ) );
	Com_Printf( "  AL_SOFT_source_spatialize: %s\n", AvailableUnavailable( caps.sourceSpatialize ) );
	Com_Printf( "  AL_SOFT_source_latency: %s (alGetSourcedvSOFT %s)\n",
		AvailableUnavailable( caps.sourceLatency ),
		LoadedMissing( caps.alGetSourcedvSOFT ) );
	Com_Printf( "  EFX: %s (filters %s, reverb send %s, effect %s, aux sends %d)\n",
		AvailableUnavailable( efxAvailable_ ),
		AvailableUnavailable( filterAvailable_ ),
		reverbEnabled_ ? "enabled" : "disabled",
		ReverbEffectName(),
		maxAuxSends_ );
	if ( filterAvailable_ ) {
		Com_Printf( "    filter types: lowpass %s, highpass %s, bandpass %s%s\n",
			YesNo( filterLowPassSupported_ ),
			YesNo( filterHighPassSupported_ ),
			YesNo( filterBandPassSupported_ ),
			( filterHighPassSupported_ && filterBandPassSupported_ ) ? "" : " (unsupported kinds degrade to lowpass)" );
	}
	Com_Printf( "  AL_SOFT_events: %s (event control %s, callback %s)\n",
		AvailableUnavailable( caps.sourceEvents ),
		LoadedMissing( caps.alEventControlSOFT ),
		LoadedMissing( caps.alEventCallbackSOFT ) );
	Com_Printf( "  Distance model: requested %s, active %s\n",
		ALDistanceModelName( caps.requestedDistanceModel ),
		caps.distanceModelValid ? ALDistanceModelName( caps.distanceModel ) : "not-queried" );
	Com_Printf( "    world source distance params: reference %.1f, max %.1f, rolloff %.2f\n",
		kOpenALReferenceDistance, kOpenALMaxDistance, kOpenALRolloffFactor );
}

void OpenALDevice::PrintHrtfList() const {
	const ModernOpenALCapabilities &caps = capabilities_;

	Com_Printf( "----- OpenAL HRTFs -----\n" );
	Com_Printf( "OpenAL library: %s\n", LibraryName().empty() ? "unknown" : LibraryName().c_str() );
	Com_Printf( "Active device: %s\n", ActiveDeviceName().empty() ? "unknown" : ActiveDeviceName().c_str() );
	Com_Printf( "Requested HRTF: %s (id %s)\n",
		CvarStringOrDefault( s_alHrtf, "auto" ),
		CvarStringOrDefault( s_alHrtfId, "default" ) );
	Com_Printf( "Active HRTF status: %s\n",
		( caps.hrtfStatus >= 0 ) ? HrtfStatusName( caps.hrtfStatus ) : "not-queried" );
	Com_Printf( "ALC_SOFT_HRTF: %s\n", AvailableUnavailable( caps.hrtf ) );
	if ( caps.hrtf ) {
		Com_Printf( "HRTF count: %d\n", caps.hrtfCount );
		PrintHrtfSpecifierList( caps.hrtfSpecifiers, s_alHrtfId != nullptr ? s_alHrtfId->string : "" );
	}
	Com_Printf( "------------------------\n" );
}

void OpenALDevice::PrintOpenALSoftConfigHints() const {
	const ModernOpenALCapabilities &caps = capabilities_;

	Com_Printf( "----- OpenAL Soft Configuration Hints -----\n" );
	Com_Printf( "FnQL controls app-level startup requests with s_al* cvars; OpenAL Soft config files control library-global behavior.\n" );
#if defined(_WIN32)
	Com_Printf( "Config files: %%AppData%%\\alsoft.ini, or an app-local alsoft.ini beside the executable.\n" );
#else
	Com_Printf( "Config files: $XDG_CONFIG_HOME/alsoft.conf, /etc/xdg/alsoft.conf, or an app-local alsoft.conf beside the executable.\n" );
#endif
	Com_Printf( "High-value [general] options to inspect: stereo-mode, stereo-encoding, hrtf-mode, hrtf-size, default-hrtf, resampler, period_size, periods, output-limiter, channels, sample-type.\n" );
	Com_Printf( "Surround/ambisonic [decoder] options to inspect: hq-mode, distance-comp, nfc, speaker-dist, and per-layout decoder files.\n" );
	Com_Printf( "Backend-specific latency knobs are usually under [wasapi], [pipewire], [pulse], [alsa], or [jack]. Change one setting at a time and verify with s_info.\n" );
	Com_Printf( "Live device support: disconnect query %s (state %s), reopen %s, system events %s.\n",
		AvailableUnavailable( caps.disconnect ),
		caps.connectedQuery ? ALCConnectedName( caps.connectedState ) : "not-queried",
		AvailableUnavailable( caps.reopenDevice && caps.alcReopenDeviceSOFT != nullptr ),
		AvailableUnavailable( caps.systemEvents ) );
	Com_Printf( "Live render support: HRTF %s (status %s), output mode %s (active %s), limiter %s (state %s), resampler controlled by OpenAL Soft.\n",
		AvailableUnavailable( caps.hrtf ),
		( caps.hrtfStatus >= 0 ) ? HrtfStatusName( caps.hrtfStatus ) : "not-queried",
		AvailableUnavailable( caps.outputMode ),
		( caps.outputModeValue >= 0 ) ? ALCOutputModeName( caps.outputModeValue ) : "not-queried",
		AvailableUnavailable( caps.outputLimiter ),
		( caps.outputLimiterState >= 0 ) ? ALCBooleanName( caps.outputLimiterState ) : "not-queried" );
	Com_Printf( "Immersive asset support: %s\n", PCMChannelSupportDescription() );
	Com_Printf( "-------------------------------------------\n" );
}

bool OpenALDevice::CreateSource( ALuint &sourceOut ) {
	sourceOut = 0;
	if ( !loader_.Ready() ) {
		return false;
	}

	loader_.alGetError();
	loader_.alGenSources( 1, &sourceOut );
	if ( loader_.alGetError() != AL_NO_ERROR || sourceOut == 0 ) {
		sourceOut = 0;
		return false;
	}

	ResetSource( sourceOut );
	return true;
}

void OpenALDevice::ResetSource( ALuint source ) const {
	loader_.alSourceStop( source );
	loader_.alSourcei( source, AL_BUFFER, 0 );
	loader_.alSourcei( source, AL_LOOPING, AL_FALSE );
	if ( efxAvailable_ ) {
		loader_.alSourcei( source, AL_DIRECT_FILTER, AL_FILTER_NULL );
		loader_.alSource3i( source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL );
	}
	SetSourceSpatialize( source, false );
	SetSourceDirectChannels( source, false );
	loader_.alSourcei( source, AL_SOURCE_RELATIVE, AL_TRUE );
	loader_.alSourcef( source, AL_GAIN, 1.0f );
	loader_.alSourcef( source, AL_MIN_GAIN, 0.0f );
	loader_.alSourcef( source, AL_MAX_GAIN, 1.0f );
	loader_.alSourcef( source, AL_PITCH, 1.0f );
	ConfigureSourceDistance( source, false );
	loader_.alSource3f( source, AL_POSITION, 0.0f, 0.0f, -1.0f );
	loader_.alSource3f( source, AL_VELOCITY, 0.0f, 0.0f, 0.0f );
	loader_.alGetError();
}

bool OpenALDevice::InitEFX() {
	efxAvailable_ = false;
	filterAvailable_ = false;
	filterLowPassSupported_ = false;
	filterHighPassSupported_ = false;
	filterBandPassSupported_ = false;
	reverbEnabled_ = false;
	reverbIsEax_ = false;
	maxAuxSends_ = 0;
	auxEffectSlot_ = 0;
	reverbEffect_ = 0;
	activeReverbPreset_ = -1;
	activeReverbTargetPreset_ = -1;
	activeReverbBlend_ = -1.0f;
	currentReverbName_ = "disabled";

	if ( loader_.alcIsExtensionPresent == nullptr || loader_.alGetProcAddress == nullptr || device_ == nullptr ) {
		return true;
	}

	if ( loader_.alcIsExtensionPresent( device_, ALC_EXT_EFX_NAME ) != ALC_TRUE ) {
		return true;
	}

#define FNQL_LOAD_EFX_PROC(member, name) \
	member = reinterpret_cast<decltype(member)>( loader_.alGetProcAddress( name ) );
	FNQL_LOAD_EFX_PROC( alGenFilters_, "alGenFilters" )
	FNQL_LOAD_EFX_PROC( alDeleteFilters_, "alDeleteFilters" )
	FNQL_LOAD_EFX_PROC( alFilteri_, "alFilteri" )
	FNQL_LOAD_EFX_PROC( alFilterf_, "alFilterf" )
	FNQL_LOAD_EFX_PROC( alGenEffects_, "alGenEffects" )
	FNQL_LOAD_EFX_PROC( alDeleteEffects_, "alDeleteEffects" )
	FNQL_LOAD_EFX_PROC( alEffecti_, "alEffecti" )
	FNQL_LOAD_EFX_PROC( alEffectf_, "alEffectf" )
	FNQL_LOAD_EFX_PROC( alGenAuxiliaryEffectSlots_, "alGenAuxiliaryEffectSlots" )
	FNQL_LOAD_EFX_PROC( alDeleteAuxiliaryEffectSlots_, "alDeleteAuxiliaryEffectSlots" )
	FNQL_LOAD_EFX_PROC( alAuxiliaryEffectSloti_, "alAuxiliaryEffectSloti" )
#undef FNQL_LOAD_EFX_PROC

	efxAvailable_ = true;
	filterAvailable_ = ( alGenFilters_ != nullptr && alDeleteFilters_ != nullptr && alFilteri_ != nullptr && alFilterf_ != nullptr );

	// Legacy runtimes (the Creative router's "Generic Software" driver) accept
	// only a subset of EFX filter types; probe once so tone shaping can
	// degrade gracefully instead of silently losing its filters.
	if ( filterAvailable_ ) {
		ALuint probeFilter = 0;
		loader_.alGetError();
		alGenFilters_( 1, &probeFilter );
		if ( loader_.alGetError() == AL_NO_ERROR && probeFilter != 0 ) {
			const auto filterTypeSupported = [&]( ALint type ) {
				loader_.alGetError();
				alFilteri_( probeFilter, AL_FILTER_TYPE, type );
				return loader_.alGetError() == AL_NO_ERROR;
			};
			filterLowPassSupported_ = filterTypeSupported( AL_FILTER_LOWPASS );
			filterHighPassSupported_ = filterTypeSupported( AL_FILTER_HIGHPASS );
			filterBandPassSupported_ = filterTypeSupported( AL_FILTER_BANDPASS );
			alDeleteFilters_( 1, &probeFilter );
			loader_.alGetError();
		}
		filterAvailable_ = filterLowPassSupported_ || filterHighPassSupported_ || filterBandPassSupported_;
	}

	if ( alGenEffects_ == nullptr || alDeleteEffects_ == nullptr || alEffecti_ == nullptr || alEffectf_ == nullptr ||
		alGenAuxiliaryEffectSlots_ == nullptr || alDeleteAuxiliaryEffectSlots_ == nullptr || alAuxiliaryEffectSloti_ == nullptr ) {
		return true;
	}

	loader_.alcGetIntegerv( device_, ALC_MAX_AUXILIARY_SENDS, 1, &maxAuxSends_ );
	if ( maxAuxSends_ < 1 || s_alReverb == nullptr || s_alReverb->integer == 0 ) {
		return true;
	}

	alGenEffects_( 1, &reverbEffect_ );
	if ( loader_.alGetError() != AL_NO_ERROR || reverbEffect_ == 0 ) {
		reverbEffect_ = 0;
		return true;
	}

	// Prefer the extended reverb model: it adds LF decay control, echo, and
	// modulation (the underwater warble) on OpenAL Soft and other EFX runtimes
	// that accept it. Fall back to the basic reverb effect otherwise.
	alEffecti_( reverbEffect_, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB );
	reverbIsEax_ = ( loader_.alGetError() == AL_NO_ERROR );
	if ( !reverbIsEax_ ) {
		alEffecti_( reverbEffect_, AL_EFFECT_TYPE, AL_EFFECT_REVERB );
		if ( loader_.alGetError() != AL_NO_ERROR ) {
			alDeleteEffects_( 1, &reverbEffect_ );
			reverbEffect_ = 0;
			return true;
		}
	}

	alGenAuxiliaryEffectSlots_( 1, &auxEffectSlot_ );
	if ( loader_.alGetError() != AL_NO_ERROR || auxEffectSlot_ == 0 ) {
		auxEffectSlot_ = 0;
		alDeleteEffects_( 1, &reverbEffect_ );
		reverbEffect_ = 0;
		return true;
	}

	alAuxiliaryEffectSloti_( auxEffectSlot_, AL_EFFECTSLOT_EFFECT, reverbEffect_ );
	if ( loader_.alGetError() != AL_NO_ERROR ) {
		alDeleteAuxiliaryEffectSlots_( 1, &auxEffectSlot_ );
		auxEffectSlot_ = 0;
		alDeleteEffects_( 1, &reverbEffect_ );
		reverbEffect_ = 0;
		return true;
	}

	reverbEnabled_ = true;
	currentReverbName_ = "pending";
	return true;
}

void OpenALDevice::ShutdownEFX() {
	if ( reverbEnabled_ && alAuxiliaryEffectSloti_ != nullptr && auxEffectSlot_ != 0 ) {
		alAuxiliaryEffectSloti_( auxEffectSlot_, AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL );
	}
	if ( auxEffectSlot_ != 0 && alDeleteAuxiliaryEffectSlots_ != nullptr ) {
		alDeleteAuxiliaryEffectSlots_( 1, &auxEffectSlot_ );
	}
	if ( reverbEffect_ != 0 && alDeleteEffects_ != nullptr ) {
		alDeleteEffects_( 1, &reverbEffect_ );
	}

	auxEffectSlot_ = 0;
	reverbEffect_ = 0;
	efxAvailable_ = false;
	filterAvailable_ = false;
	filterLowPassSupported_ = false;
	filterHighPassSupported_ = false;
	filterBandPassSupported_ = false;
	reverbEnabled_ = false;
	reverbIsEax_ = false;
	maxAuxSends_ = 0;
	activeReverbPreset_ = -1;
	activeReverbTargetPreset_ = -1;
	activeReverbBlend_ = -1.0f;
	currentReverbName_ = "disabled";
	alGenEffects_ = nullptr;
	alDeleteEffects_ = nullptr;
	alEffecti_ = nullptr;
	alEffectf_ = nullptr;
	alGenFilters_ = nullptr;
	alDeleteFilters_ = nullptr;
	alFilteri_ = nullptr;
	alFilterf_ = nullptr;
	alGenAuxiliaryEffectSlots_ = nullptr;
	alDeleteAuxiliaryEffectSlots_ = nullptr;
	alAuxiliaryEffectSloti_ = nullptr;
}

bool OpenALDevice::CreateFilter( ALuint &filter ) {
	filter = 0;
	if ( !filterAvailable_ || alGenFilters_ == nullptr || alFilteri_ == nullptr || alFilterf_ == nullptr ) {
		return false;
	}

	alGenFilters_( 1, &filter );
	if ( loader_.alGetError() != AL_NO_ERROR || filter == 0 ) {
		filter = 0;
		return false;
	}

	alFilteri_( filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
	alFilterf_( filter, AL_LOWPASS_GAIN, 1.0f );
	alFilterf_( filter, AL_LOWPASS_GAINHF, 1.0f );
	if ( loader_.alGetError() != AL_NO_ERROR ) {
		DestroyFilter( filter );
		return false;
	}

	return true;
}

bool OpenALDevice::ConfigureFilter( ALuint filter, const AudioFilterSettings &settings ) const {
	if ( filter == 0 || !filterAvailable_ || alFilteri_ == nullptr || alFilterf_ == nullptr || settings.kind == AudioFilterKind::None ) {
		return false;
	}

	// Degrade unsupported filter kinds to a low-pass approximation so legacy
	// runtimes keep the gain and high cut instead of losing the filter.
	AudioFilterSettings effective = settings;
	switch ( effective.kind ) {
	case AudioFilterKind::BandPass:
		if ( !filterBandPassSupported_ ) {
			if ( !filterLowPassSupported_ ) {
				return false;
			}
			effective = LowPassFilter( effective.gain, effective.gainHF );
		}
		break;
	case AudioFilterKind::HighPass:
		if ( !filterHighPassSupported_ ) {
			if ( !filterLowPassSupported_ ) {
				return false;
			}
			effective = LowPassFilter( effective.gain, 1.0f );
			if ( !FilterHasAudibleEffect( effective ) ) {
				return false;
			}
		}
		break;
	case AudioFilterKind::LowPass:
		if ( !filterLowPassSupported_ ) {
			return false;
		}
		break;
	default:
		return false;
	}

	loader_.alGetError();
	switch ( effective.kind ) {
	case AudioFilterKind::LowPass:
		alFilteri_( filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
		alFilterf_( filter, AL_LOWPASS_GAIN, ClampFloat( effective.gain, 0.0f, 1.0f ) );
		alFilterf_( filter, AL_LOWPASS_GAINHF, ClampFloat( effective.gainHF, 0.0f, 1.0f ) );
		break;
	case AudioFilterKind::HighPass:
		alFilteri_( filter, AL_FILTER_TYPE, AL_FILTER_HIGHPASS );
		alFilterf_( filter, AL_HIGHPASS_GAIN, ClampFloat( effective.gain, 0.0f, 1.0f ) );
		alFilterf_( filter, AL_HIGHPASS_GAINLF, ClampFloat( effective.gainLF, 0.0f, 1.0f ) );
		break;
	case AudioFilterKind::BandPass:
		alFilteri_( filter, AL_FILTER_TYPE, AL_FILTER_BANDPASS );
		alFilterf_( filter, AL_BANDPASS_GAIN, ClampFloat( effective.gain, 0.0f, 1.0f ) );
		alFilterf_( filter, AL_BANDPASS_GAINLF, ClampFloat( effective.gainLF, 0.0f, 1.0f ) );
		alFilterf_( filter, AL_BANDPASS_GAINHF, ClampFloat( effective.gainHF, 0.0f, 1.0f ) );
		break;
	default:
		return false;
	}

	return loader_.alGetError() == AL_NO_ERROR;
}

void OpenALDevice::DestroyFilter( ALuint &filter ) {
	if ( filter != 0 && alDeleteFilters_ != nullptr ) {
		alDeleteFilters_( 1, &filter );
	}
	filter = 0;
}

bool OpenALDevice::CreateVoiceFilters( ALuint &directFilter, ALuint &sendFilter ) {
	directFilter = 0;
	sendFilter = 0;
	CreateFilter( directFilter );
	CreateFilter( sendFilter );
	return ( directFilter != 0 || sendFilter != 0 || reverbEnabled_ ) ? true : false;
}

void OpenALDevice::DestroyVoiceFilters( ALuint &directFilter, ALuint &sendFilter ) {
	DestroyFilter( directFilter );
	DestroyFilter( sendFilter );
}

void OpenALDevice::ApplyVoiceRouting( ALuint source, ALuint directFilter, ALuint sendFilter, float sourceGain, float directGain, const AudioFilterSettings &directTone, const AudioFilterSettings &sendTone, VoiceRouteCache &cache ) const {
	sourceGain = ClampFloat( sourceGain, 0.0f, 2.0f );
	directGain = ClampFloat( directGain, 0.0f, 1.0f );
	const float routedGain = ClampFloat( sourceGain * directGain, 0.0f, 2.0f );
	const float maxGain = ClampFloat( ( std::max )( routedGain, 1.0f ), 1.0f, 2.0f );

	// Sources snapshot filter parameters when a filter is attached, so tone
	// changes need a reconfigure and reattach. In the common steady state the
	// tones are unchanged frame to frame and only the gains need refreshing.
	if ( cache.valid && FilterSettingsNearlyEqual( cache.directTone, directTone ) &&
		FilterSettingsNearlyEqual( cache.sendTone, sendTone ) ) {
		if ( std::fabs( cache.maxGain - maxGain ) > 0.0005f ) {
			loader_.alSourcef( source, AL_MAX_GAIN, maxGain );
			cache.maxGain = maxGain;
		}
		if ( std::fabs( cache.routedGain - routedGain ) > 0.0005f ) {
			loader_.alSourcef( source, AL_GAIN, routedGain );
			cache.routedGain = routedGain;
		}
		return;
	}

	loader_.alSourcef( source, AL_MAX_GAIN, maxGain );
	loader_.alSourcef( source, AL_GAIN, routedGain );

	if ( efxAvailable_ ) {
		if ( FilterHasAudibleEffect( directTone ) && ConfigureFilter( directFilter, directTone ) ) {
			loader_.alSourcei( source, AL_DIRECT_FILTER, directFilter );
		} else {
			loader_.alSourcei( source, AL_DIRECT_FILTER, AL_FILTER_NULL );
		}

		if ( reverbEnabled_ && sendTone.kind != AudioFilterKind::None && sendTone.gain > 0.001f && auxEffectSlot_ != 0 ) {
			if ( FilterHasAudibleEffect( sendTone ) && ConfigureFilter( sendFilter, sendTone ) ) {
				loader_.alSource3i( source, AL_AUXILIARY_SEND_FILTER, auxEffectSlot_, 0, sendFilter );
			} else {
				loader_.alSource3i( source, AL_AUXILIARY_SEND_FILTER, auxEffectSlot_, 0, AL_FILTER_NULL );
			}
		} else {
			loader_.alSource3i( source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL );
		}
	}

	cache.valid = true;
	cache.routedGain = routedGain;
	cache.maxGain = maxGain;
	cache.directTone = directTone;
	cache.sendTone = sendTone;
}

void OpenALDevice::SetMasterGain( float gain ) {
	if ( !loader_.Ready() || loader_.alListenerf == nullptr ) {
		return;
	}

	gain = ClampFloat( gain, 0.0f, 2.0f );
	if ( gain == appliedMasterGain_ ) {
		return;
	}
	loader_.alListenerf( AL_GAIN, gain );
	appliedMasterGain_ = gain;
}

void OpenALDevice::ApplyDopplerState() {
	if ( !loader_.Ready() || context_ == nullptr ) {
		return;
	}

	const bool enabled = ( s_doppler == nullptr || s_doppler->integer != 0 );
	const float factor = enabled ? ClampFloat( ( s_alDopplerFactor != nullptr ) ? s_alDopplerFactor->value : 1.0f, 0.0f, 10.0f ) : 0.0f;
	const float speed = ClampFloat( ( s_alDopplerSpeed != nullptr ) ? s_alDopplerSpeed->value : 6000.0f, 1000.0f, 20000.0f );
	if ( factor == appliedDopplerFactor_ && speed == appliedDopplerSpeed_ ) {
		return;
	}

	loader_.alGetError();
	loader_.alDopplerFactor( factor );
	loader_.alSpeedOfSound( speed );
	loader_.alGetError();
	appliedDopplerFactor_ = factor;
	appliedDopplerSpeed_ = speed;
}

void OpenALDevice::UpdateListener( const float *origin, const float *velocity, const float axis[3][3] ) const {
	if ( !loader_.Ready() || loader_.alListener3f == nullptr || loader_.alListenerfv == nullptr ) {
		return;
	}

	const float zero[3] = { 0.0f, 0.0f, 0.0f };
	const float defaultAxis[3][3] = {
		{ 1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f }
	};
	const float *listenerOrigin = ( origin != nullptr ) ? origin : zero;
	const float *listenerVelocity = ( velocity != nullptr ) ? velocity : zero;
	const float ( *listenerAxis )[3] = ( axis != nullptr ) ? axis : defaultAxis;
	const float orientation[6] = {
		listenerAxis[0][0], listenerAxis[0][1], listenerAxis[0][2],
		listenerAxis[2][0], listenerAxis[2][1], listenerAxis[2][2]
	};

	loader_.alListener3f( AL_POSITION, listenerOrigin[0], listenerOrigin[1], listenerOrigin[2] );
	loader_.alListener3f( AL_VELOCITY, listenerVelocity[0], listenerVelocity[1], listenerVelocity[2] );
	loader_.alListenerfv( AL_ORIENTATION, orientation );
	loader_.alGetError();
}

void OpenALDevice::SetSourceSpatialize( ALuint source, bool spatialize ) const {
	if ( !loader_.Ready() || source == 0 || !capabilities_.sourceSpatialize ) {
		return;
	}

	loader_.alSourcei( source, AL_SOURCE_SPATIALIZE_SOFT, spatialize ? AL_TRUE : AL_FALSE );
	loader_.alGetError();
}

void OpenALDevice::SetSourceDirectChannels( ALuint source, bool directChannels ) const {
	if ( !loader_.Ready() || source == 0 || !capabilities_.directChannels ) {
		return;
	}

	ALint mode = AL_FALSE;
	if ( directChannels ) {
		mode = capabilities_.directChannelsRemix ? AL_REMIX_UNMATCHED_SOFT : AL_TRUE;
	}
	loader_.alSourcei( source, AL_DIRECT_CHANNELS_SOFT, mode );
	loader_.alGetError();
}

void OpenALDevice::ConfigureSourceDistance( ALuint source, bool worldDistance, float referenceScale, float rangeScale ) const {
	if ( !loader_.Ready() || source == 0 ) {
		return;
	}

	if ( worldDistance && UsesOpenALDistanceAttenuation() ) {
		referenceScale = ClampFloat( referenceScale, kMinSoundShaderDistanceScale, kMaxSoundShaderReferenceScale );
		rangeScale = ClampFloat( rangeScale, kMinSoundShaderDistanceScale, kMaxSoundShaderRangeScale );
		const float referenceDistance = kOpenALReferenceDistance * referenceScale;
		const float maxDistance = ( std::max )( kOpenALMaxDistance * rangeScale, referenceDistance + 1.0f );
		loader_.alSourcef( source, AL_REFERENCE_DISTANCE, referenceDistance );
		loader_.alSourcef( source, AL_MAX_DISTANCE, maxDistance );
		loader_.alSourcef( source, AL_ROLLOFF_FACTOR, kOpenALRolloffFactor / rangeScale );
	} else {
		loader_.alSourcef( source, AL_REFERENCE_DISTANCE, 1.0f );
		loader_.alSourcef( source, AL_MAX_DISTANCE, kOpenALMaxDistance );
		loader_.alSourcef( source, AL_ROLLOFF_FACTOR, 0.0f );
	}
	if ( efxAvailable_ ) {
		const float airAbsorption = worldDistance ?
			ClampFloat( ( s_alAirAbsorption != nullptr ) ? s_alAirAbsorption->value : 0.0f, 0.0f, 10.0f ) : 0.0f;
		loader_.alSourcef( source, AL_AIR_ABSORPTION_FACTOR, airAbsorption );
	}
	loader_.alGetError();
}

bool OpenALDevice::UsesOpenALDistanceAttenuation() const {
	return capabilities_.distanceModelValid && DistanceModelUsesOpenALAttenuation( capabilities_.distanceModel );
}

bool OpenALDevice::BeginDeferredUpdates() const {
	if ( !loader_.Ready() || !capabilities_.deferredUpdates ||
		capabilities_.alDeferUpdatesSOFT == nullptr || capabilities_.alProcessUpdatesSOFT == nullptr ) {
		return false;
	}

	loader_.alGetError();
	capabilities_.alDeferUpdatesSOFT();
	return loader_.alGetError() == AL_NO_ERROR;
}

void OpenALDevice::EndDeferredUpdates() const {
	if ( !loader_.Ready() || !capabilities_.deferredUpdates ||
		capabilities_.alProcessUpdatesSOFT == nullptr ) {
		return;
	}

	loader_.alGetError();
	capabilities_.alProcessUpdatesSOFT();
	loader_.alGetError();
}

bool OpenALDevice::QuerySourceLatency( ALuint source, double &offsetSeconds, double &latencyMilliseconds ) const {
	offsetSeconds = 0.0;
	latencyMilliseconds = 0.0;
	if ( !loader_.Ready() || source == 0 || !capabilities_.sourceLatency ||
		capabilities_.alGetSourcedvSOFT == nullptr ) {
		return false;
	}

	ALdouble values[2] = { 0.0, 0.0 };
	loader_.alGetError();
	capabilities_.alGetSourcedvSOFT( source, AL_SEC_OFFSET_LATENCY_SOFT, values );
	if ( loader_.alGetError() != AL_NO_ERROR ) {
		return false;
	}

	offsetSeconds = values[0];
	latencyMilliseconds = values[1] * 1000.0;
	return true;
}

void OpenALDevice::UpdateReverb( const EnvironmentState &environment ) {
	if ( !reverbEnabled_ || reverbEffect_ == 0 || alEffectf_ == nullptr || alAuxiliaryEffectSloti_ == nullptr ) {
		return;
	}
	if ( environment.presetIndex < 0 || environment.presetIndex >= static_cast<int>( sizeof( kReverbPresets ) / sizeof( kReverbPresets[0] ) ) ) {
		return;
	}
	if ( environment.targetPresetIndex < 0 || environment.targetPresetIndex >= static_cast<int>( sizeof( kReverbPresets ) / sizeof( kReverbPresets[0] ) ) ) {
		return;
	}

	const float blend = ClampFloat( environment.blend, 0.0f, 1.0f );
	if ( activeReverbPreset_ == environment.presetIndex &&
		activeReverbTargetPreset_ == environment.targetPresetIndex &&
		std::fabs( activeReverbBlend_ - blend ) < 0.015f ) {
		return;
	}

	const ReverbPreset &from = kReverbPresets[environment.presetIndex];
	const ReverbPreset &to = kReverbPresets[environment.targetPresetIndex];
	if ( reverbIsEax_ ) {
		alEffectf_( reverbEffect_, AL_EAXREVERB_DENSITY, LerpFloat( from.density, to.density, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_DIFFUSION, LerpFloat( from.diffusion, to.diffusion, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_GAIN, LerpFloat( from.gain, to.gain, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_GAINHF, LerpFloat( from.gainHF, to.gainHF, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_GAINLF, LerpFloat( from.gainLF, to.gainLF, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_DECAY_TIME, LerpFloat( from.decayTime, to.decayTime, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_DECAY_HFRATIO, LerpFloat( from.decayHFRatio, to.decayHFRatio, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_DECAY_LFRATIO, LerpFloat( from.decayLFRatio, to.decayLFRatio, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_REFLECTIONS_GAIN, LerpFloat( from.reflectionsGain, to.reflectionsGain, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_REFLECTIONS_DELAY, LerpFloat( from.reflectionsDelay, to.reflectionsDelay, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_LATE_REVERB_GAIN, LerpFloat( from.lateReverbGain, to.lateReverbGain, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_LATE_REVERB_DELAY, LerpFloat( from.lateReverbDelay, to.lateReverbDelay, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_ECHO_TIME, LerpFloat( from.echoTime, to.echoTime, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_ECHO_DEPTH, LerpFloat( from.echoDepth, to.echoDepth, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_MODULATION_TIME, LerpFloat( from.modulationTime, to.modulationTime, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_MODULATION_DEPTH, LerpFloat( from.modulationDepth, to.modulationDepth, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, LerpFloat( from.airAbsorptionGainHF, to.airAbsorptionGainHF, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_HFREFERENCE, LerpFloat( from.hfReference, to.hfReference, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_LFREFERENCE, LerpFloat( from.lfReference, to.lfReference, blend ) );
		alEffectf_( reverbEffect_, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, LerpFloat( from.roomRolloffFactor, to.roomRolloffFactor, blend ) );
		alEffecti_( reverbEffect_, AL_EAXREVERB_DECAY_HFLIMIT, ( blend < 0.5f ) ? from.decayHFLimit : to.decayHFLimit );
	} else {
		alEffectf_( reverbEffect_, AL_REVERB_DENSITY, LerpFloat( from.density, to.density, blend ) );
		alEffectf_( reverbEffect_, AL_REVERB_DIFFUSION, LerpFloat( from.diffusion, to.diffusion, blend ) );
		alEffectf_( reverbEffect_, AL_REVERB_GAIN, LerpFloat( from.gain, to.gain, blend ) );
		alEffectf_( reverbEffect_, AL_REVERB_GAINHF, LerpFloat( from.gainHF, to.gainHF, blend ) );
		alEffectf_( reverbEffect_, AL_REVERB_DECAY_TIME, LerpFloat( from.decayTime, to.decayTime, blend ) );
		alEffectf_( reverbEffect_, AL_REVERB_DECAY_HFRATIO, LerpFloat( from.decayHFRatio, to.decayHFRatio, blend ) );
		alEffectf_( reverbEffect_, AL_REVERB_REFLECTIONS_GAIN, LerpFloat( from.reflectionsGain, to.reflectionsGain, blend ) );
		alEffectf_( reverbEffect_, AL_REVERB_REFLECTIONS_DELAY, LerpFloat( from.reflectionsDelay, to.reflectionsDelay, blend ) );
		alEffectf_( reverbEffect_, AL_REVERB_LATE_REVERB_GAIN, LerpFloat( from.lateReverbGain, to.lateReverbGain, blend ) );
		alEffectf_( reverbEffect_, AL_REVERB_LATE_REVERB_DELAY, LerpFloat( from.lateReverbDelay, to.lateReverbDelay, blend ) );
		alEffectf_( reverbEffect_, AL_REVERB_AIR_ABSORPTION_GAINHF, LerpFloat( from.airAbsorptionGainHF, to.airAbsorptionGainHF, blend ) );
		alEffectf_( reverbEffect_, AL_REVERB_ROOM_ROLLOFF_FACTOR, LerpFloat( from.roomRolloffFactor, to.roomRolloffFactor, blend ) );
		alEffecti_( reverbEffect_, AL_REVERB_DECAY_HFLIMIT, ( blend < 0.5f ) ? from.decayHFLimit : to.decayHFLimit );
	}
	alAuxiliaryEffectSloti_( auxEffectSlot_, AL_EFFECTSLOT_EFFECT, reverbEffect_ );
	loader_.alGetError();

	activeReverbPreset_ = environment.presetIndex;
	activeReverbTargetPreset_ = environment.targetPresetIndex;
	activeReverbBlend_ = blend;
	if ( environment.presetIndex != environment.targetPresetIndex && blend > 0.0f && blend < 1.0f ) {
		currentReverbName_ = std::string( environment.name ) + "->" + environment.targetName;
	} else {
		currentReverbName_ = environment.targetName;
	}
}

void OpenALDevice::RefreshActiveDeviceName() {
	activeDeviceName_.clear();
	if ( !loader_.Ready() || device_ == nullptr || loader_.alcGetString == nullptr ) {
		return;
	}

	const ALCchar *activeDevice = loader_.alcGetString( device_, ALC_DEVICE_SPECIFIER );
	if ( activeDevice != nullptr ) {
		activeDeviceName_ = reinterpret_cast<const char *>( activeDevice );
	}
}

void OpenALDevice::RefreshRuntimeStateAfterDeviceReset() {
	DiscoverALCCapabilities();
	DiscoverALCapabilities();
	BuildDeviceResetAttributes();
	RefreshActiveDeviceName();
	ApplyDistanceModel();
	if ( loader_.Ready() && context_ != nullptr ) {
		appliedDopplerFactor_ = -1.0f;
		appliedDopplerSpeed_ = -1.0f;
		ApplyDopplerState();
		appliedMasterGain_ = -1.0f;
		SetMasterGain( ( s_volume != nullptr ) ? s_volume->value : 1.0f );
		if ( efxAvailable_ ) {
			loader_.alGetError();
			loader_.alListenerf( AL_METERS_PER_UNIT, kMetersPerGameUnit );
			loader_.alGetError();
		}
		UpdateListener( nullptr, nullptr, nullptr );
	}
}

bool OpenALDevice::RecoverDevice( bool force ) {
	adr::RecoveryStartDecision recoveryStart = adr::PlanRecoveryStart( Ready() && device_ != nullptr, force, false, false );
	if ( recoveryStart == adr::RecoveryStartDecision::Unavailable ) {
		Com_Printf( "OpenAL device recovery unavailable: backend is not active\n" );
		return false;
	}

	RefreshDeviceConnection();
	recoveryStart = adr::PlanRecoveryStart( true, force, capabilities_.connectedQuery, DeviceConnected() );
	if ( recoveryStart == adr::RecoveryStartDecision::SkipConnected ) {
		Com_Printf( "OpenAL device recovery skipped: device still reports %s. Use s_alRecoverDevice force to reopen anyway.\n",
			ALCConnectedName( capabilities_.connectedState ) );
		return true;
	}

	const std::vector<ALCint> attributes = BuildDeviceResetAttributes();
	const ALCint *attributePtr = attributes.size() > 1 ? attributes.data() : nullptr;
	const char *targetDevice = ( requestedDeviceName_.empty() || usingDefaultFallback_ ) ? nullptr : requestedDeviceName_.c_str();

	if ( capabilities_.alcReopenDeviceSOFT != nullptr ) {
		loader_.alcGetError( device_ );
		ALCboolean reopened = capabilities_.alcReopenDeviceSOFT( device_, targetDevice, attributePtr );
		ALCenum error = loader_.alcGetError( device_ );
		if ( reopened == ALC_FALSE && attributePtr != nullptr ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: OpenAL device reopen rejected requested attributes (%s); retrying without attributes\n",
				ALCErrorName( error ) );
			loader_.alcGetError( device_ );
			reopened = capabilities_.alcReopenDeviceSOFT( device_, targetDevice, nullptr );
			error = loader_.alcGetError( device_ );
		}
		if ( reopened == ALC_TRUE ) {
			RefreshRuntimeStateAfterDeviceReset();
			Com_Printf( "OpenAL device recovery: reopened %s device '%s'\n",
				targetDevice == nullptr ? "default" : "requested",
				activeDeviceName_.empty() ? "unknown" : activeDeviceName_.c_str() );
			PrintCapabilityMatrix();
			return true;
		}

		Com_Printf( S_COLOR_YELLOW "WARNING: OpenAL device reopen failed (%s)\n", ALCErrorName( error ) );
	}

	if ( capabilities_.alcResetDeviceSOFT != nullptr ) {
		loader_.alcGetError( device_ );
		ALCboolean reset = capabilities_.alcResetDeviceSOFT( device_, attributePtr );
		ALCenum error = loader_.alcGetError( device_ );
		if ( reset == ALC_FALSE && attributePtr != nullptr ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: OpenAL device reset rejected requested attributes (%s); retrying without attributes\n",
				ALCErrorName( error ) );
			loader_.alcGetError( device_ );
			reset = capabilities_.alcResetDeviceSOFT( device_, nullptr );
			error = loader_.alcGetError( device_ );
		}
		if ( reset == ALC_TRUE ) {
			RefreshRuntimeStateAfterDeviceReset();
			Com_Printf( "OpenAL device recovery: reset active device '%s'\n",
				activeDeviceName_.empty() ? "unknown" : activeDeviceName_.c_str() );
			PrintCapabilityMatrix();
			return true;
		}

		Com_Printf( S_COLOR_YELLOW "WARNING: OpenAL device reset failed (%s)\n", ALCErrorName( error ) );
	}

	Com_Printf( S_COLOR_YELLOW "WARNING: OpenAL device recovery is unavailable on this runtime; run snd_restart to rebuild the backend\n" );
	return false;
}

bool OpenALDevice::Init() {
	if ( !loader_.Load() ) {
		return false;
	}

	requestedDeviceName_ = ( s_alDevice != nullptr ) ? SafeString( s_alDevice->string ) : std::string();
	usingDefaultFallback_ = false;

	device_ = loader_.alcOpenDevice( requestedDeviceName_.empty() ? nullptr : requestedDeviceName_.c_str() );
	if ( device_ == nullptr && !requestedDeviceName_.empty() ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: requested OpenAL device '%s' could not be opened; trying system default device\n",
			requestedDeviceName_.c_str() );
		device_ = loader_.alcOpenDevice( nullptr );
		usingDefaultFallback_ = ( device_ != nullptr );
		if ( !usingDefaultFallback_ ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: system default OpenAL device could not be opened after requested-device failure\n" );
		}
	}
	if ( device_ == nullptr ) {
		Shutdown();
		return false;
	}

	DiscoverALCCapabilities();

	if ( !CreateBestContext() ) {
		Shutdown();
		return false;
	}

	DiscoverALCapabilities();

	RefreshActiveDeviceName();

	ApplyDistanceModel();
	ApplyDopplerState();
	SetMasterGain( ( s_volume != nullptr ) ? s_volume->value : 1.0f );
	UpdateListener( nullptr, nullptr, nullptr );
	InitEFX();
	if ( efxAvailable_ ) {
		// EFX distance features (per-source air absorption, reverb rolloff)
		// assume meters; Q3 world units are inches.
		loader_.alGetError();
		loader_.alListenerf( AL_METERS_PER_UNIT, kMetersPerGameUnit );
		loader_.alGetError();
	}

	if ( !CreateSource( musicSource_ ) || !CreateSource( rawSource_ ) ) {
		Shutdown();
		return false;
	}

	for ( int i = 0; i < kMaxVoices + 8; ++i ) {
		ALuint source = 0;
		if ( !CreateSource( source ) ) {
			break;
		}
		allVoiceSources_.push_back( source );
		freeVoiceSources_.push_back( source );
	}

	if ( allVoiceSources_.empty() ) {
		Shutdown();
		return false;
	}

	bufferPool_ = new StreamBufferPool();
	if ( !bufferPool_->Init( this, kInitialStreamBuffers ) ) {
		Shutdown();
		return false;
	}

	PrintCapabilityMatrix();

	// The legacy Creative router exposes none of the OpenAL Soft extensions
	// and only part of EFX, which silently strips most of the spatial layer.
	// Make that state loud so "no reverb/occlusion" is diagnosable.
	if ( !capabilities_.hrtf && !capabilities_.outputMode && !capabilities_.loopback ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: the active OpenAL runtime (%s / %s) has no OpenAL Soft extensions; HRTF and parts of the spatial filter set are unavailable.\n",
			loader_.LibraryName().empty() ? "unknown library" : loader_.LibraryName().c_str(),
			activeDeviceName_.empty() ? "unknown device" : activeDeviceName_.c_str() );
		Com_Printf( S_COLOR_YELLOW "         Install OpenAL Soft or place its soft_oal.dll (or OpenAL32.dll) next to the executable, then run snd_restart.\n" );
	}

	return true;
}

void OpenALDevice::Shutdown() {
	if ( bufferPool_ != nullptr ) {
		bufferPool_->Shutdown();
		delete bufferPool_;
		bufferPool_ = nullptr;
	}

	if ( loader_.Ready() ) {
		for ( ALuint source : allVoiceSources_ ) {
			ResetSource( source );
			loader_.alDeleteSources( 1, &source );
		}
		allVoiceSources_.clear();
		freeVoiceSources_.clear();

		if ( musicSource_ != 0 ) {
			ResetSource( musicSource_ );
			loader_.alDeleteSources( 1, &musicSource_ );
			musicSource_ = 0;
		}
		if ( rawSource_ != 0 ) {
			ResetSource( rawSource_ );
			loader_.alDeleteSources( 1, &rawSource_ );
			rawSource_ = 0;
		}

		ShutdownEFX();
	}

	if ( loader_.Ready() && context_ != nullptr ) {
		loader_.alcMakeContextCurrent( nullptr );
		loader_.alcDestroyContext( context_ );
	}
	context_ = nullptr;

	if ( loader_.Ready() && device_ != nullptr ) {
		loader_.alcCloseDevice( device_ );
	}
	device_ = nullptr;

	requestedDeviceName_.clear();
	activeDeviceName_.clear();
	contextAttributeMode_.clear();
	usingDefaultFallback_ = false;
	contextUsedFallback_ = false;
	appliedMasterGain_ = -1.0f;
	appliedDopplerFactor_ = -1.0f;
	appliedDopplerSpeed_ = -1.0f;
	capabilities_ = ModernOpenALCapabilities();

	loader_.Unload();
}

ALuint OpenALDevice::AcquireVoiceSource() {
	if ( freeVoiceSources_.empty() ) {
		return 0;
	}

	const ALuint source = freeVoiceSources_.back();
	freeVoiceSources_.pop_back();
	ResetSource( source );
	return source;
}

void OpenALDevice::ReleaseVoiceSource( ALuint source ) {
	if ( source == 0 ) {
		return;
	}

	ResetSource( source );
	if ( std::find( freeVoiceSources_.begin(), freeVoiceSources_.end(), source ) == freeVoiceSources_.end() ) {
		freeVoiceSources_.push_back( source );
	}
}

bool StreamBufferPool::Init( OpenALDevice *device, int initialCount ) {
	device_ = device;
	const int clampedInitialCount = ClampInt( initialCount, 0, kMaxStreamBuffers );
	for ( int i = 0; i < clampedInitialCount; ++i ) {
		ALuint buffer = 0;
		device_->AL().alGetError();
		device_->AL().alGenBuffers( 1, &buffer );
		if ( device_->AL().alGetError() != AL_NO_ERROR || buffer == 0 ) {
			break;
		}
		allBuffers_.push_back( buffer );
		freeBuffers_.push_back( buffer );
	}

	return !allBuffers_.empty();
}

void StreamBufferPool::Shutdown() {
	if ( device_ != nullptr && device_->AL().Ready() ) {
		for ( ALuint buffer : allBuffers_ ) {
			device_->AL().alDeleteBuffers( 1, &buffer );
		}
	}
	allBuffers_.clear();
	freeBuffers_.clear();
	device_ = nullptr;
}

ALuint StreamBufferPool::Acquire() {
	if ( device_ == nullptr ) {
		return 0;
	}

	if ( freeBuffers_.empty() ) {
		if ( allBuffers_.size() >= static_cast<size_t>( kMaxStreamBuffers ) ) {
			return 0;
		}
		ALuint buffer = 0;
		device_->AL().alGetError();
		device_->AL().alGenBuffers( 1, &buffer );
		if ( device_->AL().alGetError() != AL_NO_ERROR || buffer == 0 ) {
			return 0;
		}
		allBuffers_.push_back( buffer );
		freeBuffers_.push_back( buffer );
	}

	const ALuint buffer = freeBuffers_.back();
	freeBuffers_.pop_back();
	return buffer;
}

void StreamBufferPool::Release( ALuint buffer ) {
	if ( buffer == 0 ) {
		return;
	}

	if ( std::find( freeBuffers_.begin(), freeBuffers_.end(), buffer ) == freeBuffers_.end() ) {
		freeBuffers_.push_back( buffer );
	}
}
