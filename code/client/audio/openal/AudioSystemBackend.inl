// Included by AudioSystem.cpp inside its private implementation namespace.
// The soundInterface_t-facing OpenAL backend facade.

class AudioSystem {
public:
	static AudioSystem &Get();

	bool Init( soundInterface_t *si );
	void Shutdown();

	void StartSound( const float *origin, int entnum, int entchannel, sfxHandle_t sfxHandle, float volume );
	void StartLocalSound( sfxHandle_t sfxHandle, int channelNum, float volume );
	void StartBackgroundTrack( const char *intro, const char *loop );
	void StopBackgroundTrack();
	void UpdateBackgroundTrack();
	void RawSamples( int samples, int rate, int width, int channels, const byte *data, float volume );
	void AddVoiceSamples( int clientNum, int samples, int rate, const short *data );
	void StopAllSounds();
	void ClearLoopingSoundsFrame();
	void ClearLoopingSounds( qboolean killall );
	void AddLoopingSound( int entityNum, const float *origin, const float *velocity, sfxHandle_t sfxHandle );
	void AddRealLoopingSound( int entityNum, const float *origin, const float *velocity, sfxHandle_t sfxHandle );
	void StopLoopingSound( int entityNum );
	void Respatialize( int entityNum, const float *origin, float axis[3][3], int inwater );
	void UpdateEntityPosition( int entityNum, const float *origin );
	void Update( int msec );
	void DisableSounds();
	void BeginRegistration();
	sfxHandle_t RegisterSound( const char *sample, qboolean compressed );
	void ClearSoundBuffer();
	void SoundInfo();
	void SoundList();
	void ListDevices() const;
	void ListHrtfs() const;
	void PrintOpenALSoftConfigHints() const;
	bool RecoverDevice( bool force );
	qboolean GetSpatialDebugInfo( spatialAudioDebugInfo_t *info ) const;
	void DumpSpatialDebug() const;

private:
	static std::vector<short> ConvertStreamToPCM16( int samples, int rate, int width, int channels, const byte *data, float volume, int outputRate, int outputChannels );
	bool QueueStreamChunk( StreamPlayer &player, int samples, int rate, int width, int channels, const byte *data, float volume, int outputChannels, bool logFallback );
	int StreamOutputRate() const;
	void ServiceBackgroundTrack();
	bool UpdateDeviceState( int msec );
	void CloseBackgroundStream();
	SoundSample *GetSample( sfxHandle_t handle );
	qboolean IsSoftMuted() const;

	OpenALDevice device_;
	Q3SoundWorld world_;
	StreamPlayer musicPlayer_;
	StreamPlayer rawPlayer_;
	VoiceChatPlayer voicePlayer_;
	std::deque<SoundSample> samples_;
	std::unordered_map<std::string, sfxHandle_t> sampleLookup_;
	SoundShaderLibrary weaponSoundShaders_;
	snd_stream_t *backgroundStream_ = nullptr;
	std::string backgroundIntro_;
	std::string backgroundLoop_;
	int backgroundStereoFallbackChannels_ = 0;
	adr::DeviceRecoveryState deviceRecovery_;
	bool started_ = false;
	bool hardMuted_ = true;
};

AudioSystem &AudioSystem::Get() {
	static AudioSystem audioSystem;
	return audioSystem;
}

static int SelectStreamOutputChannels( const OpenALDevice &device, int channels ) {
	if ( !PCMChannelCountCanBeRepresented( channels ) ) {
		return 0;
	}
	if ( channels > kDefaultStreamChannels && device.SupportsPCMChannels( channels ) ) {
		return channels;
	}
	return kDefaultStreamChannels;
}

static float ReadStreamPCM16Sample( const byte *data, size_t sampleIndex, int width, int channels ) {
	if ( data == nullptr ) {
		return 0.0f;
	}
	if ( width == 2 ) {
		return static_cast<float>( ReadLittlePCM16( data + sampleIndex * sizeof( short ) ) );
	}
	if ( channels == 1 ) {
		return static_cast<float>( ConvertUnsignedPCM8ToPCM16( data[sampleIndex] ) );
	}
	return static_cast<float>( ConvertSignedPCM8ToPCM16( data[sampleIndex] ) );
}

static float CubicInterpolatePCM16( float p0, float p1, float p2, float p3, float fraction ) {
	const float f2 = fraction * fraction;
	const float f3 = f2 * fraction;
	return 0.5f * ( ( 2.0f * p1 ) +
		( -p0 + p2 ) * fraction +
		( 2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 ) * f2 +
		( -p0 + 3.0f * p1 - 3.0f * p2 + p3 ) * f3 );
}

static float ReadInterpolatedStreamSample( const byte *data, int samples, int width, int channels, int channel, double sourcePosition ) {
	if ( data == nullptr || samples <= 0 || channel < 0 || channel >= channels ) {
		return 0.0f;
	}

	const int base = ClampInt( static_cast<int>( std::floor( sourcePosition ) ), 0, samples - 1 );
	const float fraction = static_cast<float>( sourcePosition - static_cast<double>( base ) );
	if ( samples == 1 || fraction <= 0.000001f ) {
		return ReadStreamPCM16Sample( data, static_cast<size_t>( base ) * static_cast<size_t>( channels ) + static_cast<size_t>( channel ), width, channels );
	}

	const int i0 = ClampInt( base - 1, 0, samples - 1 );
	const int i1 = base;
	const int i2 = ClampInt( base + 1, 0, samples - 1 );
	const int i3 = ClampInt( base + 2, 0, samples - 1 );
	const size_t channelIndex = static_cast<size_t>( channel );
	const float p0 = ReadStreamPCM16Sample( data, static_cast<size_t>( i0 ) * static_cast<size_t>( channels ) + channelIndex, width, channels );
	const float p1 = ReadStreamPCM16Sample( data, static_cast<size_t>( i1 ) * static_cast<size_t>( channels ) + channelIndex, width, channels );
	const float p2 = ReadStreamPCM16Sample( data, static_cast<size_t>( i2 ) * static_cast<size_t>( channels ) + channelIndex, width, channels );
	const float p3 = ReadStreamPCM16Sample( data, static_cast<size_t>( i3 ) * static_cast<size_t>( channels ) + channelIndex, width, channels );
	return CubicInterpolatePCM16( p0, p1, p2, p3, fraction );
}

static void WriteConvertedStreamFrame( const float *input, int inputChannels, short *output, int outputChannels, float gain ) {
	if ( input == nullptr || output == nullptr || inputChannels <= 0 || outputChannels <= 0 ) {
		return;
	}

	if ( outputChannels == inputChannels ) {
		for ( int channel = 0; channel < outputChannels; ++channel ) {
			output[channel] = ClampPCM16FromFloat( input[channel] * gain );
		}
		return;
	}

	if ( outputChannels == kDefaultStreamChannels ) {
		float left = 0.0f;
		float right = 0.0f;
		DownmixFrameToStereoFloat( input, inputChannels, gain, left, right );
		output[0] = ClampPCM16FromFloat( left );
		output[1] = ClampPCM16FromFloat( right );
		return;
	}

	for ( int channel = 0; channel < outputChannels; ++channel ) {
		output[channel] = 0;
	}
	if ( inputChannels == 1 ) {
		output[0] = ClampPCM16FromFloat( input[0] * gain );
		if ( outputChannels > 1 ) {
			output[1] = output[0];
		}
		return;
	}

	const int copiedChannels = ( std::min )( inputChannels, outputChannels );
	for ( int channel = 0; channel < copiedChannels; ++channel ) {
		output[channel] = ClampPCM16FromFloat( input[channel] * gain );
	}
}

std::vector<short> AudioSystem::ConvertStreamToPCM16( int samples, int rate, int width, int channels, const byte *data, float volume, int outputRate, int outputChannels ) {
	std::vector<short> pcm;
	if ( samples <= 0 || rate <= 0 || outputRate <= 0 || data == nullptr || ( width != 1 && width != 2 ) ||
		!PCMChannelCountCanBeRepresented( channels ) || !PCMChannelCountCanBeRepresented( outputChannels ) ) {
		return pcm;
	}

	size_t sourceBytes = 0;
	if ( !PCMFrameByteCount( samples, channels, width, sourceBytes ) ) {
		return pcm;
	}
	(void)sourceBytes;

	const float gain = ClampFloat( volume, 0.0f, 8.0f );
	const int64_t outputSamples64 = ( std::max )( static_cast<int64_t>( 1 ),
		( static_cast<int64_t>( samples ) * static_cast<int64_t>( outputRate ) + rate - 1 ) / rate );
	if ( outputSamples64 > std::numeric_limits<int>::max() ||
		static_cast<uint64_t>( outputSamples64 ) > std::numeric_limits<size_t>::max() / static_cast<uint64_t>( outputChannels ) ||
		!PCMByteCountFitsALsizei( static_cast<size_t>( outputSamples64 ) * static_cast<size_t>( outputChannels ) ) ) {
		return pcm;
	}
	const int outputSamples = static_cast<int>( outputSamples64 );
	pcm.resize( static_cast<size_t>( outputSamples ) * static_cast<size_t>( outputChannels ) );

	const double sourceStep = static_cast<double>( rate ) / static_cast<double>( outputRate );
	for ( int i = 0; i < outputSamples; ++i ) {
		const double sourcePosition = ( std::min )( static_cast<double>( samples - 1 ), static_cast<double>( i ) * sourceStep );
		std::array<float, kMaxPCMChannels> inputFrame = {};
		for ( int channel = 0; channel < channels; ++channel ) {
			inputFrame[static_cast<size_t>( channel )] = ReadInterpolatedStreamSample( data, samples, width, channels, channel, sourcePosition );
		}

		WriteConvertedStreamFrame( inputFrame.data(), channels,
			pcm.data() + static_cast<size_t>( i ) * static_cast<size_t>( outputChannels ),
			outputChannels, gain );
	}

	return pcm;
}

int AudioSystem::StreamOutputRate() const {
	const int mixerRate = device_.Capabilities().mixerFrequency;
	if ( mixerRate >= 8000 && mixerRate <= 192000 ) {
		return mixerRate;
	}

	return ClampInt( CvarIntegerOrDefault( s_alFrequency, kFallbackStreamRate ), 8000, 192000 );
}

bool AudioSystem::QueueStreamChunk( StreamPlayer &player, int samples, int rate, int width, int channels, const byte *data, float volume, int outputChannels, bool logFallback ) {
	if ( outputChannels <= 0 ) {
		return false;
	}

	const int outputRate = StreamOutputRate();
	const std::vector<short> pcm = ConvertStreamToPCM16( samples, rate, width, channels, data, volume, outputRate, outputChannels );
	if ( pcm.empty() ) {
		return false;
	}

	const int frameCount = static_cast<int>( pcm.size() / static_cast<size_t>( outputChannels ) );
	if ( player.QueuePCM16( pcm.data(), frameCount, outputChannels, outputRate ) ) {
		return true;
	}

	if ( outputChannels <= kDefaultStreamChannels ) {
		return false;
	}

	player.Clear();
	const std::vector<short> stereoPCM = ConvertStreamToPCM16( samples, rate, width, channels, data, volume, outputRate, kDefaultStreamChannels );
	if ( stereoPCM.empty() ) {
		return false;
	}

	if ( !player.QueuePCM16( stereoPCM.data(), static_cast<int>( stereoPCM.size() / static_cast<size_t>( kDefaultStreamChannels ) ), kDefaultStreamChannels, outputRate ) ) {
		return false;
	}

	if ( logFallback && backgroundStereoFallbackChannels_ != outputChannels ) {
		Com_DPrintf( S_COLOR_YELLOW "WARNING: OpenAL rejected %s stream buffers; using stereo downmix fallback\n",
			PCMChannelLayoutName( outputChannels ) );
		backgroundStereoFallbackChannels_ = outputChannels;
	}
	return true;
}

bool AudioSystem::Init( soundInterface_t *si ) {
	if ( si == nullptr ) {
		return false;
	}

	s_alReverb = Cvar_Get( "s_alReverb", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_CheckRange( s_alReverb, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_alReverb, "Enables OpenAL environmental reverb sends when the active device supports EFX. Requires snd_restart to fully apply." );
	s_alOcclusion = Cvar_Get( "s_alOcclusion", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_alOcclusion, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_alOcclusion, "Enables smoothed world-geometry occlusion checks for the OpenAL backend." );
	s_alReverbGain = Cvar_Get( "s_alReverbGain", "1.0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_alReverbGain, "0", "2", CV_FLOAT );
	Cvar_SetDescription( s_alReverbGain, "Scales wet reverb send level for the OpenAL backend." );
	s_alOcclusionStrength = Cvar_Get( "s_alOcclusionStrength", "1.0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_alOcclusionStrength, "0", "2", CV_FLOAT );
	Cvar_SetDescription( s_alOcclusionStrength, "Scales how strongly world occlusion attenuates and muffles OpenAL sounds before smoothing." );
	s_alDopplerFactor = Cvar_Get( "s_alDopplerFactor", "1.0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_alDopplerFactor, "0", "10", CV_FLOAT );
	Cvar_SetDescription( s_alDopplerFactor, "Scales the OpenAL doppler effect from source and listener motion. Applies while s_doppler is enabled." );
	s_alDopplerSpeed = Cvar_Get( "s_alDopplerSpeed", "6000", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_alDopplerSpeed, "1000", "20000", CV_FLOAT );
	Cvar_SetDescription( s_alDopplerSpeed, "Speed of sound in world units per second for OpenAL doppler.\n"
		"Lower values exaggerate pitch shifts; about 13500 matches real-world acoustics at Quake III scale." );
	s_alAirAbsorption = Cvar_Get( "s_alAirAbsorption", "2.0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_alAirAbsorption, "0", "10", CV_FLOAT );
	Cvar_SetDescription( s_alAirAbsorption, "Scales distance-based high-frequency air absorption for positional sounds when EFX is available.\n"
		"0 disables it, 1 is physically neutral, higher values darken distant sounds more strongly." );
	s_alDebugOverlay = Cvar_Get( "s_alDebugOverlay", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_alDebugOverlay, "0", "2", CV_INTEGER );
	Cvar_SetDescription( s_alDebugOverlay, "Draws OpenAL spatial audio debug overlay.\n"
		"0 disables the overlay.\n"
		"1 shows summary environment and selected voice state.\n"
		"2 adds environment, sample, gain, and tone-filter details for the selected voice." );
	s_alDebugVoice = Cvar_Get( "s_alDebugVoice", "-1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( s_alDebugVoice, "Selects which entity the OpenAL spatial audio debug tools inspect.\n"
		"Use -1 to automatically pick the nearest active voice." );
	s_alSourceClassDebug = Cvar_Get( "s_alSourceClassDebug", "0", CVAR_DEVELOPER );
	Cvar_CheckRange( s_alSourceClassDebug, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_alSourceClassDebug, "Adds per-source-class aggregate routing and gain summaries to s_alDebugDump." );
	s_alAudioZones = Cvar_Get( "s_alAudioZones", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_alAudioZones, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_alAudioZones, "Enables optional maps/<map>.azb OpenAL audio-zone sidecars when present." );
	s_alAutoRecover = Cvar_Get( "s_alAutoRecover", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( s_alAutoRecover, "0", "1", CV_INTEGER );
	Cvar_SetDescription( s_alAutoRecover, "Automatically tries to reopen the OpenAL device after the runtime reports that it disconnected." );

	if ( !device_.Init() ) {
		return false;
	}
	if ( s_alSpatializeStereo != nullptr && s_alSpatializeStereo->integer && !device_.Capabilities().sourceSpatialize ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: s_alSpatializeStereo requested, but AL_SOFT_source_spatialize is unavailable; stereo world samples will stay direct\n" );
	}

	world_.Reset( &device_ );
	musicPlayer_.Init( &device_, device_.MusicSource() );
	rawPlayer_.Init( &device_, device_.RawSource() );
	voicePlayer_.Init( &device_ );

	samples_.clear();
	sampleLookup_.clear();
	weaponSoundShaders_.Clear();
	started_ = true;
	hardMuted_ = true;
	deviceRecovery_ = {};

	si->Shutdown = []() { AudioSystem::Get().Shutdown(); };
	si->StartSound = []( const vec3_t origin, int entnum, int entchannel, sfxHandle_t sfxHandle, float volume ) { AudioSystem::Get().StartSound( origin, entnum, entchannel, sfxHandle, volume ); };
	si->StartLocalSound = []( sfxHandle_t sfxHandle, int channelNum, float volume ) { AudioSystem::Get().StartLocalSound( sfxHandle, channelNum, volume ); };
	si->StartBackgroundTrack = []( const char *intro, const char *loop ) { AudioSystem::Get().StartBackgroundTrack( intro, loop ); };
	si->StopBackgroundTrack = []() { AudioSystem::Get().StopBackgroundTrack(); };
	si->UpdateBackgroundTrack = []() { AudioSystem::Get().UpdateBackgroundTrack(); };
	si->RawSamples = []( int samples, int rate, int width, int channels, const byte *data, float volume ) { AudioSystem::Get().RawSamples( samples, rate, width, channels, data, volume ); };
	si->AddVoiceSamples = []( int clientNum, int samples, int rate, const short *data ) { AudioSystem::Get().AddVoiceSamples( clientNum, samples, rate, data ); };
	si->StopAllSounds = []() { AudioSystem::Get().StopAllSounds(); };
	si->ClearLoopingSoundsFrame = []() { AudioSystem::Get().ClearLoopingSoundsFrame(); };
	si->ClearLoopingSounds = []( qboolean killall ) { AudioSystem::Get().ClearLoopingSounds( killall ); };
	si->AddLoopingSound = []( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfxHandle ) { AudioSystem::Get().AddLoopingSound( entityNum, origin, velocity, sfxHandle ); };
	si->AddRealLoopingSound = []( int entityNum, const vec3_t origin, const vec3_t velocity, sfxHandle_t sfxHandle ) { AudioSystem::Get().AddRealLoopingSound( entityNum, origin, velocity, sfxHandle ); };
	si->StopLoopingSound = []( int entityNum ) { AudioSystem::Get().StopLoopingSound( entityNum ); };
	si->Respatialize = []( int entityNum, const vec3_t origin, vec3_t axis[3], int inwater ) { AudioSystem::Get().Respatialize( entityNum, origin, axis, inwater ); };
	si->UpdateEntityPosition = []( int entityNum, const vec3_t origin ) { AudioSystem::Get().UpdateEntityPosition( entityNum, origin ); };
	si->Update = []( int msec ) { AudioSystem::Get().Update( msec ); };
	si->DisableSounds = []() { AudioSystem::Get().DisableSounds(); };
	si->BeginRegistration = []() { AudioSystem::Get().BeginRegistration(); };
	si->RegisterSound = []( const char *sample, qboolean compressed ) { return AudioSystem::Get().RegisterSound( sample, compressed ); };
	si->ClearSoundBuffer = []() { AudioSystem::Get().ClearSoundBuffer(); };
	si->SoundInfo = []() { AudioSystem::Get().SoundInfo(); };
	si->SoundList = []() { AudioSystem::Get().SoundList(); };
	si->GetSpatialDebugInfo = []( spatialAudioDebugInfo_t *info ) { return AudioSystem::Get().GetSpatialDebugInfo( info ); };
	si->DumpSpatialDebug = []() { AudioSystem::Get().DumpSpatialDebug(); };

	return true;
}

void AudioSystem::Shutdown() {
	if ( !started_ ) {
		return;
	}

	StopAllSounds();
	CloseBackgroundStream();
	for ( SoundSample &sample : samples_ ) {
		sample.Unload( device_ );
	}
	samples_.clear();
	sampleLookup_.clear();
	weaponSoundShaders_.Clear();
	voicePlayer_.Shutdown();
	musicPlayer_.Shutdown();
	rawPlayer_.Shutdown();
	device_.Shutdown();
	started_ = false;
	hardMuted_ = true;
	deviceRecovery_ = {};
	cls.soundRegistered = qfalse;
}

qboolean AudioSystem::IsSoftMuted() const {
	return ( ( !gw_active && !gw_minimized && s_muteWhenUnfocused != nullptr && s_muteWhenUnfocused->integer ) ||
		( gw_minimized && s_muteWhenMinimized != nullptr && s_muteWhenMinimized->integer ) ) ? qtrue : qfalse;
}

bool AudioSystem::UpdateDeviceState( int msec ) {
	if ( !started_ || !device_.Ready() ) {
		return false;
	}

	const adr::DevicePollDecision poll = adr::AdvanceDevicePoll( deviceRecovery_, msec, device_.DeviceConnected(), kOpenALDeviceStatePollMs );
	if ( !poll.shouldPoll ) {
		return poll.connected;
	}

	const bool refreshSucceeded = device_.RefreshDeviceConnection();
	const adr::DevicePollResult result = adr::FinishDevicePoll(
		deviceRecovery_,
		refreshSucceeded,
		device_.DeviceConnected(),
		s_alAutoRecover != nullptr && s_alAutoRecover->integer != 0,
		kOpenALDeviceRecoveryRetryMs );
	if ( result.printReconnected ) {
		Com_Printf( "OpenAL playback device reports connected again\n" );
	}
	if ( result.printDisconnectedWarning ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: OpenAL playback device disconnected; use s_alRecoverDevice or snd_restart if audio does not return\n" );
	}
	if ( result.shouldAttemptRecovery ) {
		const bool recovered = device_.RecoverDevice( false );
		adr::FinishRecoveryAttempt( deviceRecovery_, recovered );
		if ( recovered ) {
			return true;
		}
	}
	return result.connected;
}

SoundSample *AudioSystem::GetSample( sfxHandle_t handle ) {
	if ( handle < 0 || handle >= static_cast<sfxHandle_t>( samples_.size() ) ) {
		return nullptr;
	}
	return &samples_[static_cast<size_t>( handle )];
}

void AudioSystem::BeginRegistration() {
	hardMuted_ = false;

	if ( !samples_.empty() ) {
		return;
	}

	weaponSoundShaders_.EnsureLoaded();
	const std::string feedbackSample = NormalizeSoundName( "sound/feedback/hit.wav" );
	samples_.emplace_back( feedbackSample, weaponSoundShaders_.Find( feedbackSample ) );
	sampleLookup_.emplace( feedbackSample, 0 );
	samples_[0].EnsureLoaded( device_, true );
}

sfxHandle_t AudioSystem::RegisterSound( const char *sample, qboolean /*compressed*/ ) {
	if ( !started_ || sample == nullptr || sample[0] == '\0' ) {
		return 0;
	}

	if ( std::strlen( sample ) >= MAX_QPATH ) {
		Com_Printf( "Sound name exceeds MAX_QPATH\n" );
		return 0;
	}
	if ( sample[0] == '*' ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: Tried to load player sound directly: %s\n", sample );
		return 0;
	}

	if ( samples_.empty() ) {
		BeginRegistration();
	}

	const std::string normalized = NormalizeSoundName( sample );
	const auto existing = sampleLookup_.find( normalized );
	if ( existing != sampleLookup_.end() ) {
		SoundSample &found = samples_[static_cast<size_t>( existing->second )];
		if ( found.EnsureLoaded( device_, existing->second == 0 ) ) {
			return existing->second;
		}
		return 0;
	}

	if ( samples_.size() >= kMaxRegisteredSamples ) {
		Com_Error( ERR_FATAL, "S_FindName: out of sfx_t" );
	}
	const sfxHandle_t handle = static_cast<sfxHandle_t>( samples_.size() );
	weaponSoundShaders_.EnsureLoaded();
	samples_.emplace_back( normalized, weaponSoundShaders_.Find( normalized ) );
	sampleLookup_.emplace( normalized, handle );
	if ( !samples_.back().EnsureLoaded( device_, false ) ) {
		Com_DPrintf( S_COLOR_YELLOW "WARNING: couldn't load sound: %s\n", sample );
		return 0;
	}

	return handle;
}

void AudioSystem::StartSound( const float *origin, int entnum, int entchannel, sfxHandle_t sfxHandle, float volume ) {
	if ( !started_ || hardMuted_ ) {
		return;
	}
	if ( origin == nullptr && ( entnum < 0 || entnum >= MAX_GENTITIES ) ) {
		Com_Error( ERR_DROP, "S_StartSound: bad entitynum %i", entnum );
	}

	SoundSample *sample = GetSample( sfxHandle );
	if ( sample == nullptr ) {
		Com_Printf( S_COLOR_YELLOW "S_StartSound: handle %i out of range\n", sfxHandle );
		return;
	}
	if ( !sample->EnsureLoaded( device_, sfxHandle == 0 ) ) {
		return;
	}

	world_.StartSound( entnum, entchannel, sfxHandle, sample, origin, volume );
}

void AudioSystem::StartLocalSound( sfxHandle_t sfxHandle, int channelNum, float volume ) {
	if ( !started_ || hardMuted_ ) {
		return;
	}
	if ( GetSample( sfxHandle ) == nullptr ) {
		Com_Printf( S_COLOR_YELLOW "S_StartLocalSound: handle %i out of range\n", sfxHandle );
		return;
	}
	StartSound( nullptr, world_.ListenerNumber(), channelNum, sfxHandle, volume );
}

void AudioSystem::CloseBackgroundStream() {
	if ( backgroundStream_ != nullptr ) {
		S_CodecCloseStream( backgroundStream_ );
		backgroundStream_ = nullptr;
	}
	backgroundStereoFallbackChannels_ = 0;
}

void AudioSystem::StartBackgroundTrack( const char *intro, const char *loop ) {
	if ( !started_ ) {
		return;
	}

	backgroundIntro_ = SafeString( intro );
	backgroundLoop_ = ( loop != nullptr && loop[0] != '\0' ) ? SafeString( loop ) : backgroundIntro_;

	if ( backgroundIntro_.empty() ) {
		StopBackgroundTrack();
		return;
	}

	CloseBackgroundStream();
	backgroundStream_ = S_CodecOpenStream( backgroundIntro_.c_str() );
	musicPlayer_.Clear();
	if ( backgroundStream_ == nullptr ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: couldn't open music file %s\n", backgroundIntro_.c_str() );
		backgroundIntro_.clear();
		backgroundLoop_.clear();
	}
}

void AudioSystem::StopBackgroundTrack() {
	CloseBackgroundStream();
	backgroundIntro_.clear();
	backgroundLoop_.clear();
	musicPlayer_.Clear();
}

void AudioSystem::UpdateBackgroundTrack() {
	if ( !started_ ) {
		return;
	}
	const float musicVolume = ( s_musicVolume != nullptr ) ? s_musicVolume->value : 1.0f;
	if ( musicVolume > 0.0f ) {
		ServiceBackgroundTrack();
	}
}

void AudioSystem::ServiceBackgroundTrack() {
	if ( backgroundStream_ == nullptr ) {
		return;
	}

	// Cap decode work per frame so starting a track does not burst several
	// large codec reads into one frame; the queue still fills within a couple
	// of frames, well inside the buffered playback margin.
	int emptyRestarts = 0;
	int chunksQueuedThisCall = 0;
	while ( musicPlayer_.QueuedBufferCount() < kQueuedStreamChunks && chunksQueuedThisCall < kMaxStreamChunksPerService ) {
		std::array<byte, 32768> raw;
		const int bytesRead = S_CodecReadStream( backgroundStream_, static_cast<int>( raw.size() ), raw.data() );
		if ( bytesRead <= 0 ) {
			if ( !backgroundLoop_.empty() ) {
				if ( emptyRestarts > 0 ) {
					Com_Printf( S_COLOR_YELLOW "WARNING: music loop %s produced no audio\n", backgroundLoop_.c_str() );
					StopBackgroundTrack();
					break;
				}
				CloseBackgroundStream();
				backgroundStream_ = S_CodecOpenStream( backgroundLoop_.c_str() );
				if ( backgroundStream_ == nullptr ) {
					Com_Printf( S_COLOR_YELLOW "WARNING: couldn't open music file %s\n", backgroundLoop_.c_str() );
					StopBackgroundTrack();
					break;
				}
				++emptyRestarts;
				continue;
			}
			StopBackgroundTrack();
			break;
		}

		if ( backgroundStream_->info.rate <= 0 ||
			( backgroundStream_->info.width != 1 && backgroundStream_->info.width != 2 ) ||
			!PCMChannelCountCanBeRepresented( backgroundStream_->info.channels ) ) {
			StopBackgroundTrack();
			break;
		}

		size_t frameBytesSize = 0;
		if ( !PCMFrameByteCount( 1, backgroundStream_->info.channels, backgroundStream_->info.width, frameBytesSize ) ||
			frameBytesSize > static_cast<size_t>( std::numeric_limits<int>::max() ) ) {
			StopBackgroundTrack();
			break;
		}
		const int frameBytes = static_cast<int>( frameBytesSize );
		const int inputSamples = bytesRead / frameBytes;
		if ( inputSamples <= 0 ) {
			break;
		}
		const int outputChannels = SelectStreamOutputChannels( device_, backgroundStream_->info.channels );
		if ( !QueueStreamChunk( musicPlayer_, inputSamples, backgroundStream_->info.rate, backgroundStream_->info.width,
			backgroundStream_->info.channels, raw.data(), 1.0f, outputChannels, true ) ) {
			break;
		}
		emptyRestarts = 0;
		++chunksQueuedThisCall;
	}
}

void AudioSystem::RawSamples( int samples, int rate, int width, int channels, const byte *data, float volume ) {
	if ( !started_ || hardMuted_ ) {
		return;
	}

	const int outputChannels = SelectStreamOutputChannels( device_, channels );
	QueueStreamChunk( rawPlayer_, samples, rate, width, channels, data, volume, outputChannels, false );
}

void AudioSystem::AddVoiceSamples( int clientNum, int samples, int rate, const short *data ) {
	if ( !started_ || hardMuted_ ) {
		return;
	}
	voicePlayer_.Queue( clientNum, samples, rate, data );
}

void AudioSystem::StopAllSounds() {
	if ( !started_ ) {
		return;
	}

	world_.StopAllSounds();
	rawPlayer_.Clear();
	voicePlayer_.Clear();
	StopBackgroundTrack();
}

void AudioSystem::ClearLoopingSounds( qboolean killall ) {
	world_.ClearLoopingSounds( killall );
}

void AudioSystem::ClearLoopingSoundsFrame() {
	world_.ClearLoopingSoundsFrame();
}

qboolean AudioSystem::GetSpatialDebugInfo( spatialAudioDebugInfo_t *info ) const {
	int preferredEntity;
	int overlayMode;

	if ( !started_ || info == nullptr || s_alDebugOverlay == nullptr ) {
		return qfalse;
	}

	overlayMode = s_alDebugOverlay->integer;
	if ( overlayMode <= 0 ) {
		return qfalse;
	}

	preferredEntity = ( s_alDebugVoice != nullptr ) ? s_alDebugVoice->integer : -1;
	return world_.GetSpatialDebugInfo( info, device_, preferredEntity, overlayMode );
}

void AudioSystem::DumpSpatialDebug() const {
	const int preferredEntity = ( s_alDebugVoice != nullptr ) ? s_alDebugVoice->integer : -1;

	if ( !started_ ) {
		Com_Printf( "OpenAL spatial audio debug unavailable: backend not started\n" );
		return;
	}

	world_.DumpSpatialDebug( device_, preferredEntity );
}

void AudioSystem::AddLoopingSound( int entityNum, const float *origin, const float *velocity, sfxHandle_t sfxHandle ) {
	if ( !started_ || hardMuted_ ) {
		return;
	}

	SoundSample *sample = GetSample( sfxHandle );
	if ( sample == nullptr ) {
		Com_Printf( S_COLOR_YELLOW "S_AddLoopingSound: handle %i out of range\n", sfxHandle );
		return;
	}
	if ( !sample->EnsureLoaded( device_, sfxHandle == 0 ) ) {
		return;
	}

	world_.AddLoopingSound( entityNum, origin, velocity, sfxHandle, sample, qfalse );
}

void AudioSystem::AddRealLoopingSound( int entityNum, const float *origin, const float *velocity, sfxHandle_t sfxHandle ) {
	if ( !started_ || hardMuted_ ) {
		return;
	}

	SoundSample *sample = GetSample( sfxHandle );
	if ( sample == nullptr ) {
		Com_Printf( S_COLOR_YELLOW "S_AddRealLoopingSound: handle %i out of range\n", sfxHandle );
		return;
	}
	if ( !sample->EnsureLoaded( device_, sfxHandle == 0 ) ) {
		return;
	}

	world_.AddLoopingSound( entityNum, origin, velocity, sfxHandle, sample, qtrue );
}

void AudioSystem::StopLoopingSound( int entityNum ) {
	world_.StopLoopingSound( entityNum );
}

void AudioSystem::Respatialize( int entityNum, const float *origin, float axis[3][3], int /*inwater*/ ) {
	if ( !started_ ) {
		return;
	}

	world_.Respatialize( entityNum, origin, axis );
}

void AudioSystem::UpdateEntityPosition( int entityNum, const float *origin ) {
	world_.UpdateEntityPosition( entityNum, origin );
}

void AudioSystem::Update( int msec ) {
	if ( !started_ ) {
		return;
	}
	if ( !UpdateDeviceState( msec ) ) {
		return;
	}

	const qboolean softMuted = IsSoftMuted();
	device_.SetMasterGain( ( s_volume != nullptr ) ? s_volume->value : 1.0f );
	device_.ApplyDopplerState();
	const bool deferredUpdates = device_.BeginDeferredUpdates();
	world_.Update( softMuted );
	if ( deferredUpdates ) {
		device_.EndDeferredUpdates();
	}
	rawPlayer_.Update( softMuted ? 0.0f : 1.0f );
	const float voiceVolume = ( s_voiceVolume != nullptr ) ? ClampFloat( s_voiceVolume->value, 0.0f, 2.0f ) : 1.0f;
	voicePlayer_.Update( softMuted ? 0.0f : voiceVolume );
	const float musicVolume = ( s_musicVolume != nullptr ) ? s_musicVolume->value : 1.0f;
	musicPlayer_.Update( softMuted ? 0.0f : musicVolume );
	UpdateBackgroundTrack();
}

void AudioSystem::DisableSounds() {
	world_.StopAllSounds();
	rawPlayer_.Clear();
	voicePlayer_.Clear();
	StopBackgroundTrack();
	hardMuted_ = true;
}

void AudioSystem::ClearSoundBuffer() {
	world_.ClearSoundBuffer();
	rawPlayer_.Clear();
	voicePlayer_.Clear();
}

void AudioSystem::SoundInfo() {
	device_.RefreshDeviceConnection();
	device_.RefreshTimingDiagnostics();
	const ModernOpenALCapabilities &caps = device_.Capabilities();

	Com_Printf( "----- Sound Info -----\n" );
	Com_Printf( "Using OpenAL backend\n" );
	Com_Printf( "OpenAL library: %s\n", device_.LibraryName().empty() ? "unknown" : device_.LibraryName().c_str() );
	Com_Printf( "Requested device: %s\n", device_.RequestedDeviceName().empty() ? "default" : device_.RequestedDeviceName().c_str() );
	Com_Printf( "Active device: %s\n", device_.ActiveDeviceName().empty() ? "unknown" : device_.ActiveDeviceName().c_str() );
	if ( device_.UsingDefaultFallback() ) {
		Com_Printf( "Device fallback: using system default device\n" );
	}
	Com_Printf( "Device state: %s%s\n",
		caps.connectedQuery ? ALCConnectedName( caps.connectedState ) : "not-queried",
		( caps.reopenDevice && caps.alcReopenDeviceSOFT != nullptr ) ? ", live reopen supported" : "" );
	Com_Printf( "Context attributes: %s%s\n",
		device_.ContextAttributeMode().empty() ? "not-created" : device_.ContextAttributeMode().c_str(),
		device_.ContextUsedFallback() ? " (fallback)" : "" );
	Com_Printf( "EFX support: %s\n", device_.HasEFX() ? "enabled" : "unavailable" );
	if ( device_.HasEFX() ) {
		Com_Printf( "Auxiliary sends: %d\n", device_.MaxAuxiliarySends() );
		Com_Printf( "Reverb send: %s (%s effect, %s)\n",
			device_.HasReverb() ? "enabled" : "disabled",
			device_.ReverbEffectName(),
			device_.CurrentReverbName() );
		Com_Printf( "Air absorption: %.2f (world units calibrated to %.4f m)\n",
			ClampFloat( ( s_alAirAbsorption != nullptr ) ? s_alAirAbsorption->value : 0.0f, 0.0f, 10.0f ),
			kMetersPerGameUnit );
	}
	Com_Printf( "Doppler: %s (factor %.2f, speed of sound %.0f u/s)\n",
		( s_doppler != nullptr && s_doppler->integer ) ? "enabled" : "disabled",
		ClampFloat( ( s_alDopplerFactor != nullptr ) ? s_alDopplerFactor->value : 1.0f, 0.0f, 10.0f ),
		ClampFloat( ( s_alDopplerSpeed != nullptr ) ? s_alDopplerSpeed->value : 6000.0f, 1000.0f, 20000.0f ) );
	const bool stereoSpatializeRequested = CvarIntegerOrDefault( s_alSpatializeStereo, 0 ) != 0;
	Com_Printf( "OpenAL requested render: HRTF %s (id %s), output %s, distance %s, limiter %s, stereo spatialize %s%s\n",
		CvarStringOrDefault( s_alHrtf, "auto" ),
		CvarStringOrDefault( s_alHrtfId, "default" ),
		CvarStringOrDefault( s_alOutputMode, "auto" ),
		CvarStringOrDefault( s_alDistanceModel, "inverse_clamped" ),
		ALCBooleanName( CvarIntegerOrDefault( s_alOutputLimiter, 1 ) ? ALC_TRUE : ALC_FALSE ),
		stereoSpatializeRequested ? "on" : "off",
		( stereoSpatializeRequested && !caps.sourceSpatialize ) ? " (unsupported)" : "" );
	Com_Printf( "OpenAL active render: HRTF %s, output %s, distance %s, limiter %s\n",
		( caps.hrtfStatus >= 0 ) ? HrtfStatusName( caps.hrtfStatus ) : "not-queried",
		( caps.outputModeValue >= 0 ) ? ALCOutputModeName( caps.outputModeValue ) : "not-queried",
		caps.distanceModelValid ? ALDistanceModelName( caps.distanceModel ) : "not-queried",
		( caps.outputLimiterState >= 0 ) ? ALCBooleanName( caps.outputLimiterState ) : "not-queried" );
	Com_Printf( "OpenAL requested context: frequency %d Hz, refresh %d Hz, mono sources %d, stereo sources %d\n",
		CvarIntegerOrDefault( s_alFrequency, 48000 ),
		CvarIntegerOrDefault( s_alRefresh, 100 ),
		CvarIntegerOrDefault( s_alMonoSources, 64 ),
		CvarIntegerOrDefault( s_alStereoSources, 8 ) );
	Com_Printf( "OpenAL active context: frequency %d Hz, refresh %d Hz, mono sources %d, stereo sources %d\n",
		caps.mixerFrequency, caps.refreshRate, caps.monoSources, caps.stereoSources );
	Com_Printf( "OpenAL PCM layouts: %s\n", device_.PCMChannelSupportDescription() );
	Com_Printf( "OpenAL stream output: %d Hz cubic SRC\n", StreamOutputRate() );
	if ( caps.deviceClockValuesValid ) {
		Com_Printf( "OpenAL timing: device clock %.3f s, device latency %.2f ms\n",
			NanosecondsToSeconds( caps.deviceClockValue ),
			NanosecondsToMilliseconds( caps.deviceLatency ) );
	} else if ( caps.deviceClock ) {
		Com_Printf( "OpenAL timing: device clock supported, latency unavailable\n" );
	} else {
		Com_Printf( "OpenAL timing: device clock unavailable\n" );
	}
	device_.PrintCapabilityMatrix();
	Com_Printf( "Occlusion: %s (strength %.2f)\n",
		( s_alOcclusion != nullptr && s_alOcclusion->integer ) ? "enabled" : "disabled",
		( s_alOcclusionStrength != nullptr ) ? s_alOcclusionStrength->value : 1.0f );
	world_.PrintAudioZoneInfo();
	Com_Printf( "%5d voice sources (%d free)\n", device_.TotalVoiceCount(), device_.FreeVoiceCount() );
	Com_Printf( "%5d stream buffers (%d free)\n", device_.BufferPool().TotalCount(), device_.BufferPool().FreeCount() );
	Com_Printf( "Music stream buffers: %d queued\n", musicPlayer_.QueuedBufferCount() );
	Com_Printf( "Raw stream buffers: %d queued\n", rawPlayer_.QueuedBufferCount() );
	Com_Printf( "Remote voice lanes: %d allocated, %d active\n", voicePlayer_.LaneCount(), voicePlayer_.ActiveLaneCount() );
	weaponSoundShaders_.EnsureLoaded();
	Com_Printf( "%5d weapon sound shader rules\n", weaponSoundShaders_.Count() );
	Com_Printf( "%5lu registered samples\n", static_cast<unsigned long>( samples_.size() ) );
	Com_Printf( "Background track: %s\n", backgroundIntro_.empty() ? "none" : backgroundIntro_.c_str() );
	if ( !backgroundLoop_.empty() && backgroundLoop_ != backgroundIntro_ ) {
		Com_Printf( "Background loop: %s\n", backgroundLoop_.c_str() );
	}
	Com_Printf( "----------------------\n" );
}

void AudioSystem::SoundList() {
	for ( size_t i = 0; i < samples_.size(); ++i ) {
		const SoundSample &sample = samples_[i];
		if ( sample.Loaded() ) {
			Com_Printf( "%4d : %s [%s %d Hz %d ms%s]\n",
				static_cast<int>( i ),
				sample.Name().c_str(),
				sample.FormatName(),
				sample.Rate(),
				sample.DurationMs(),
				sample.DefaultSample() ? " fallback" : "" );
		} else {
			Com_Printf( "%4d : %s [%s]\n",
				static_cast<int>( i ),
				sample.Name().c_str(),
				sample.Missing() ? "missing" : "unloaded" );
		}
	}
}

void AudioSystem::ListDevices() const {
	PrintOpenALDeviceList( ( started_ && device_.Ready() ) ? device_.ActiveDeviceName().c_str() : nullptr );
}

void AudioSystem::ListHrtfs() const {
	if ( started_ && device_.Ready() ) {
		device_.PrintHrtfList();
		return;
	}

	PrintOpenALHrtfListForRequestedDevice();
}

void AudioSystem::PrintOpenALSoftConfigHints() const {
	if ( started_ && device_.Ready() ) {
		device_.PrintOpenALSoftConfigHints();
		return;
	}

	Com_Printf( "----- OpenAL Soft Configuration Hints -----\n" );
	Com_Printf( "OpenAL backend is not active; start it with s_backend openal and snd_restart for live capability details.\n" );
#if defined(_WIN32)
	Com_Printf( "Config files: %%AppData%%\\alsoft.ini, or an app-local alsoft.ini beside the executable.\n" );
#else
	Com_Printf( "Config files: $XDG_CONFIG_HOME/alsoft.conf, /etc/xdg/alsoft.conf, or an app-local alsoft.conf beside the executable.\n" );
#endif
	Com_Printf( "Useful OpenAL Soft options include stereo-mode, stereo-encoding, hrtf-mode, hrtf-size, resampler, period_size, periods, output-limiter, channels, sample-type, and decoder hq-mode/distance-comp/nfc.\n" );
	Com_Printf( "-------------------------------------------\n" );
}

bool AudioSystem::RecoverDevice( bool force ) {
	if ( !started_ || !device_.Ready() ) {
		Com_Printf( "OpenAL device recovery unavailable for backend '%s'\n",
			( s_backendActive != nullptr ) ? s_backendActive->string : "none" );
		return false;
	}

	const bool recovered = device_.RecoverDevice( force );
	deviceRecovery_.warningPrinted = !device_.DeviceConnected();
	deviceRecovery_.retryMs = recovered ? 0 : kOpenALDeviceRecoveryRetryMs;
	return recovered;
}
