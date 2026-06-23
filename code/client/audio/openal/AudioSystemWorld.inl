// Included by AudioSystem.cpp inside its private implementation namespace.
// Registered samples, voice state, tone shaping, and the spatial sound world.

constexpr char kWeaponSoundShaderFile[] = "sound/fnql-weapon-sounds.sndshd";
constexpr int kMaxWeaponSoundShaders = 192;
constexpr float kMaxSoundShaderVolumeDb = 10.0f;

struct SoundShaderSettings {
	bool enabled = false;
	float gainScale = kDefaultSoundShaderScale;
	float referenceScale = kDefaultSoundShaderScale;
	float rangeScale = kDefaultSoundShaderScale;
	float wetScale = kDefaultSoundShaderScale;
	float pitchScale = kDefaultSoundShaderScale;
};

struct SoundShaderRule {
	std::array<char, MAX_QPATH> sample{};
	SoundShaderSettings settings;
};

class SoundShaderLibrary {
public:
	void Clear();
	void EnsureLoaded();
	SoundShaderSettings Find( const std::string &sampleName ) const;
	int Count() const { return count_; }

private:
	void AddRule( const std::string &sampleName, const SoundShaderSettings &settings );
	bool ParseShader( const char *shaderName, const char **text );

	std::array<SoundShaderRule, kMaxWeaponSoundShaders> rules_{};
	int count_ = 0;
	bool loaded_ = false;
	bool warned_ = false;
};

struct ParsedSoundShader {
	float volumeScale = kDefaultSoundShaderScale;
	float minDistance = kOpenALReferenceDistance;
	float maxDistance = kOpenALMaxDistance;
	float wetScale = kDefaultSoundShaderScale;
	float pitchScale = kDefaultSoundShaderScale;
	float shakes = 0.0f;
	std::vector<std::string> samples;
};

static bool SoundShaderTokenIsFlag( const char *token ) {
	static const char *const flags[] = {
		"antiPrivate",
		"causeRumble",
		"center",
		"frequentlyUsed",
		"global",
		"looping",
		"mask_backleft",
		"mask_backright",
		"mask_center",
		"mask_left",
		"mask_lfe",
		"mask_right",
		"noRandomStart",
		"no_dups",
		"no_flicker",
		"no_occlusion",
		"no_shakes",
		"omnidirectional",
		"onDemand",
		"ordered",
		"plain",
		"playonce",
		"private",
		"unclamped",
		"useDoppler",
		"voForPlayer",
	};

	for ( const char *flag : flags ) {
		if ( !Q_stricmp( token, flag ) ) {
			return true;
		}
	}
	return false;
}

static bool SoundShaderTokenLooksLikeSample( const char *token ) {
	if ( token == nullptr || token[0] == '\0' ) {
		return false;
	}
	if ( !Q_stricmpn( token, "sound/", 6 ) || std::strchr( token, '\\' ) != nullptr ) {
		return true;
	}
	return COM_CompareExtension( token, ".wav" ) ||
		COM_CompareExtension( token, ".ogg" ) ||
		COM_CompareExtension( token, ".flac" );
}

static bool SoundShaderPathHasExtension( const std::string &path ) {
	const size_t slash = path.find_last_of( "/\\" );
	const size_t dot = path.find_last_of( '.' );
	return dot != std::string::npos && ( slash == std::string::npos || dot > slash );
}

static std::string NormalizeSoundShaderSampleName( const char *sampleName ) {
	std::string normalized = NormalizeSoundName( sampleName );
	if ( !normalized.empty() && !SoundShaderPathHasExtension( normalized ) ) {
		normalized += ".wav";
	}
	return normalized;
}

static bool SoundShaderParseFloatToken( const char *token, float &value ) {
	if ( token == nullptr || token[0] == '\0' ) {
		return false;
	}

	char *end = nullptr;
	const float parsed = std::strtof( token, &end );
	if ( end == token || !std::isfinite( parsed ) ) {
		return false;
	}

	value = parsed;
	return true;
}

static bool SoundShaderParseFloat( const char **text, float &value, qboolean allowLineBreaks = qtrue ) {
	const char *token = COM_ParseExt( text, allowLineBreaks );
	return SoundShaderParseFloatToken( token, value );
}

static float SoundShaderDbToScale( float db ) {
	db = ClampFloat( db, -60.0f, kMaxSoundShaderVolumeDb );
	return std::pow( 10.0f, db / 20.0f );
}

static SoundShaderSettings SoundShaderSettingsFromDecl( const ParsedSoundShader &decl ) {
	SoundShaderSettings settings;
	const float shakePunch = 1.0f + ClampFloat( decl.shakes, 0.0f, 1.0f ) * 0.08f;
	settings.enabled = true;
	settings.gainScale = ClampFloat( decl.volumeScale * shakePunch, 0.25f, 2.0f );
	settings.referenceScale = ClampFloat( decl.minDistance / kOpenALReferenceDistance, 0.5f, 2.0f );
	settings.rangeScale = ClampFloat( decl.maxDistance / kOpenALMaxDistance, 0.5f, 2.0f );
	settings.rangeScale = ( std::max )( settings.referenceScale, settings.rangeScale );
	settings.wetScale = ClampFloat( decl.wetScale, 0.0f, 2.0f );
	settings.pitchScale = ClampFloat( decl.pitchScale, 0.85f, 1.15f );
	return settings;
}

void SoundShaderLibrary::Clear() {
	count_ = 0;
	loaded_ = false;
	warned_ = false;
	for ( SoundShaderRule &rule : rules_ ) {
		rule = {};
	}
}

void SoundShaderLibrary::AddRule( const std::string &sampleName, const SoundShaderSettings &settings ) {
	const std::string normalized = NormalizeSoundShaderSampleName( sampleName.c_str() );
	if ( normalized.empty() ) {
		return;
	}

	for ( int i = 0; i < count_; ++i ) {
		SoundShaderRule &rule = rules_[static_cast<size_t>( i )];
		if ( !FS_FilenameCompare( rule.sample.data(), normalized.c_str() ) ) {
			rule.settings = settings;
			return;
		}
	}

	if ( count_ >= kMaxWeaponSoundShaders ) {
		if ( !warned_ ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: %s has more than %d sample rules; ignoring the rest\n",
				kWeaponSoundShaderFile, kMaxWeaponSoundShaders );
			warned_ = true;
		}
		return;
	}

	SoundShaderRule &rule = rules_[static_cast<size_t>( count_++ )];
	Q_strncpyz( rule.sample.data(), normalized.c_str(), static_cast<int>( rule.sample.size() ) );
	rule.settings = settings;
}

bool SoundShaderLibrary::ParseShader( const char *shaderName, const char **text ) {
	const char *token = COM_ParseExt( text, qtrue );
	if ( token[0] != '{' ) {
		COM_ParseWarning( "sound shader '%s' missing opening brace", shaderName );
		return false;
	}

	ParsedSoundShader decl;
	while ( 1 ) {
		token = COM_ParseExt( text, qtrue );
		if ( token[0] == '\0' ) {
			COM_ParseWarning( "sound shader '%s' reached end of file before closing brace", shaderName );
			return false;
		}
		if ( token[0] == '}' && token[1] == '\0' ) {
			break;
		}

		if ( !Q_stricmp( token, "description" ) ) {
			COM_ParseExt( text, qtrue );
		} else if ( !Q_stricmp( token, "minDistance" ) ) {
			SoundShaderParseFloat( text, decl.minDistance );
		} else if ( !Q_stricmp( token, "maxDistance" ) ) {
			SoundShaderParseFloat( text, decl.maxDistance );
		} else if ( !Q_stricmp( token, "volume" ) ) {
			float value = decl.volumeScale;
			if ( SoundShaderParseFloat( text, value ) ) {
				decl.volumeScale = ClampFloat( value, 0.0f, 2.0f );
			}
		} else if ( !Q_stricmp( token, "volumeDb" ) ) {
			float value = 0.0f;
			if ( SoundShaderParseFloat( text, value ) ) {
				decl.volumeScale = SoundShaderDbToScale( value );
			}
		} else if ( !Q_stricmp( token, "reverb" ) ) {
			float wetLevel = 0.0f;
			if ( SoundShaderParseFloat( text, wetLevel ) ) {
				decl.wetScale = ClampFloat( 1.0f + wetLevel, 0.0f, 2.0f );
			}
			float ignoredDryLevel = 0.0f;
			SoundShaderParseFloat( text, ignoredDryLevel, qfalse );
		} else if ( !Q_stricmp( token, "wetLevel" ) ) {
			float value = 0.0f;
			if ( SoundShaderParseFloat( text, value ) ) {
				decl.wetScale = ClampFloat( 1.0f + value, 0.0f, 2.0f );
			}
		} else if ( !Q_stricmp( token, "dryLevel" ) ||
			!Q_stricmp( token, "leadinVolume" ) ||
			!Q_stricmp( token, "soundClass" ) ||
			!Q_stricmp( token, "minSamples" ) ||
			!Q_stricmp( token, "altSound" ) ) {
			COM_ParseExt( text, qtrue );
		} else if ( !Q_stricmp( token, "frequencyShift" ) ) {
			float minShift = 1.0f;
			float maxShift = 1.0f;
			if ( SoundShaderParseFloat( text, minShift ) ) {
				if ( SoundShaderParseFloat( text, maxShift, qfalse ) ) {
					decl.pitchScale = 0.5f * ( minShift + maxShift );
				} else {
					decl.pitchScale = minShift;
				}
			}
		} else if ( !Q_stricmp( token, "pitch" ) ) {
			SoundShaderParseFloat( text, decl.pitchScale );
		} else if ( !Q_stricmp( token, "shakes" ) ) {
			float value = 1.0f;
			const char *maybeValue = COM_ParseExt( text, qfalse );
			if ( maybeValue[0] == '\0' || !SoundShaderParseFloatToken( maybeValue, value ) ) {
				value = 1.0f;
			}
			decl.shakes = ClampFloat( value, 0.0f, 1.0f );
		} else if ( !Q_stricmp( token, "shakeData" ) ) {
			COM_ParseExt( text, qtrue );
			COM_ParseExt( text, qtrue );
		} else if ( !Q_stricmp( token, "leadin" ) ||
			!Q_stricmp( token, "sample" ) ||
			!Q_stricmp( token, "entry" ) ||
			!Q_stricmp( token, "soundFile" ) ) {
			const char *sample = COM_ParseExt( text, qtrue );
			if ( SoundShaderTokenLooksLikeSample( sample ) ) {
				decl.samples.emplace_back( sample );
			}
		} else if ( SoundShaderTokenIsFlag( token ) ) {
			continue;
		} else if ( SoundShaderTokenLooksLikeSample( token ) ) {
			decl.samples.emplace_back( token );
		} else {
			COM_ParseWarning( "sound shader '%s' ignored token '%s'", shaderName, token );
		}
	}

	if ( decl.samples.empty() && SoundShaderTokenLooksLikeSample( shaderName ) ) {
		decl.samples.emplace_back( shaderName );
	}
	if ( decl.samples.empty() ) {
		COM_ParseWarning( "sound shader '%s' has no samples", shaderName );
		return false;
	}

	const SoundShaderSettings settings = SoundShaderSettingsFromDecl( decl );
	for ( const std::string &sample : decl.samples ) {
		AddRule( sample, settings );
	}
	return true;
}

void SoundShaderLibrary::EnsureLoaded() {
	if ( loaded_ ) {
		return;
	}

	loaded_ = true;
	count_ = 0;

	fnql::ScopedReadFile shaderFile = fnql::ScopedReadFile::Read( kWeaponSoundShaderFile );
	if ( !shaderFile ) {
		return;
	}

	const char *text = shaderFile.as<const char>();
	COM_BeginParseSession( kWeaponSoundShaderFile );
	while ( 1 ) {
		const char *token = COM_ParseExt( &text, qtrue );
		if ( token[0] == '\0' ) {
			break;
		}

		std::string shaderName = token;
		if ( !Q_stricmp( token, "sound" ) ) {
			const char *nameToken = COM_ParseExt( &text, qtrue );
			if ( nameToken[0] == '\0' ) {
				COM_ParseWarning( "expected sound shader name" );
				break;
			}
			shaderName = nameToken;
		}
		ParseShader( shaderName.c_str(), &text );
	}

	Com_Printf( "Loaded %d weapon sound shader%s from %s\n",
		count_, count_ == 1 ? "" : "s", kWeaponSoundShaderFile );
}

SoundShaderSettings SoundShaderLibrary::Find( const std::string &sampleName ) const {
	for ( int i = 0; i < count_; ++i ) {
		const SoundShaderRule &rule = rules_[static_cast<size_t>( i )];
		if ( !FS_FilenameCompare( rule.sample.data(), sampleName.c_str() ) ) {
			return rule.settings;
		}
	}
	return {};
}

class SoundSample {
public:
	explicit SoundSample( std::string sampleName, SoundShaderSettings shader = {} )
		: name_( std::move( sampleName ) ),
		  shader_( shader ) {
	}

	bool EnsureLoaded( OpenALDevice &device, bool allowToneFallback );
	void Unload( OpenALDevice &device );

	const std::string &Name() const { return name_; }
	bool Missing() const { return missing_; }
	bool Loaded() const { return loaded_; }
	bool DefaultSample() const { return defaultSample_; }
	ALuint Buffer() const { return buffer_; }
	int Channels() const { return channels_; }
	const AudioSampleFormat &Format() const { return format_; }
	const char *FormatName() const { return AudioSampleFormatName( format_ ); }
	bool EncodedSoundField() const { return AudioSampleFormatIsEncodedSoundField( format_ ); }
	int Rate() const { return rate_; }
	int DurationMs() const { return durationMs_; }
	const SoundShaderSettings &Shader() const { return shader_; }

private:
	static std::vector<short> ConvertToPCM16( const snd_info_t &info, const AudioSampleFormat &format, const byte *data );
	bool UploadPCM( OpenALDevice &device, const std::vector<short> &pcm, const AudioSampleFormat &format, int rate );
	bool GenerateFallbackTone( OpenALDevice &device );

	std::string name_;
	bool loadAttempted_ = false;
	bool loaded_ = false;
	bool missing_ = false;
	bool defaultSample_ = false;
	ALuint buffer_ = 0;
	AudioSampleFormat format_;
	int channels_ = 0;
	int rate_ = 0;
	int durationMs_ = 0;
	SoundShaderSettings shader_;
};

std::vector<short> SoundSample::ConvertToPCM16( const snd_info_t &info, const AudioSampleFormat &format, const byte *data ) {
	std::vector<short> pcm;

	if ( data == nullptr || info.samples <= 0 || info.rate <= 0 || !AudioSampleFormatCanBeRepresented( format ) ||
		format.channels != info.channels ||
		( info.width != 1 && info.width != 2 ) || info.size <= 0 || info.dataofs < 0 ) {
		return pcm;
	}

	size_t requiredBytes = 0;
	if ( !PCMFrameByteCount( info.samples, info.channels, info.width, requiredBytes ) ) {
		return pcm;
	}
	if ( static_cast<size_t>( info.dataofs ) > static_cast<size_t>( info.size ) ||
		requiredBytes > static_cast<size_t>( info.size ) - static_cast<size_t>( info.dataofs ) ) {
		return pcm;
	}

	const int outputChannels = info.channels;
	if ( static_cast<size_t>( info.samples ) > std::numeric_limits<size_t>::max() / static_cast<size_t>( outputChannels ) ) {
		return pcm;
	}
	pcm.resize( static_cast<size_t>( info.samples ) * static_cast<size_t>( outputChannels ) );
	const byte *pcmData = data + info.dataofs;

	for ( int i = 0; i < info.samples; ++i ) {
		for ( int c = 0; c < outputChannels; ++c ) {
			const size_t sourceIndex = static_cast<size_t>( i ) * static_cast<size_t>( info.channels ) + static_cast<size_t>( c );
			short sample = 0;
			if ( info.width == 2 ) {
				sample = ReadLittlePCM16( pcmData + sourceIndex * sizeof( short ) );
			} else {
				sample = ConvertUnsignedPCM8ToPCM16( pcmData[sourceIndex] );
			}
			pcm[static_cast<size_t>( i ) * static_cast<size_t>( outputChannels ) + static_cast<size_t>( c )] = sample;
		}
	}

	return pcm;
}

bool SoundSample::UploadPCM( OpenALDevice &device, const std::vector<short> &pcm, const AudioSampleFormat &format, int rate ) {
	if ( pcm.empty() || !device.SupportsSampleFormat( format ) || rate <= 0 ||
		pcm.size() % static_cast<size_t>( format.channels ) != 0 ) {
		return false;
	}
	if ( !PCMByteCountFitsALsizei( pcm.size() ) ) {
		return false;
	}

	const ALenum alFormat = device.PCM16FormatForSampleFormat( format );
	if ( alFormat == 0 ) {
		return false;
	}

	device.AL().alGetError();
	device.AL().alGenBuffers( 1, &buffer_ );
	if ( device.AL().alGetError() != AL_NO_ERROR || buffer_ == 0 ) {
		buffer_ = 0;
		return false;
	}

	const ALsizei byteCount = static_cast<ALsizei>( pcm.size() * sizeof( short ) );
	device.AL().alGetError();
	device.AL().alBufferData( buffer_, alFormat, pcm.data(), byteCount, rate );
	if ( device.AL().alGetError() != AL_NO_ERROR ) {
		device.AL().alDeleteBuffers( 1, &buffer_ );
		buffer_ = 0;
		return false;
	}

	format_ = format;
	channels_ = format.channels;
	rate_ = rate;
	const size_t frames = pcm.size() / static_cast<size_t>( format.channels );
	const int64_t durationMs = static_cast<int64_t>( frames ) * 1000 / rate;
	durationMs_ = durationMs > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : static_cast<int>( durationMs );
	return true;
}

bool SoundSample::GenerateFallbackTone( OpenALDevice &device ) {
	const int frames = 22050 / 4;
	std::vector<short> pcm( static_cast<size_t>( frames ) );

	for ( int i = 0; i < frames; ++i ) {
		const float phase = static_cast<float>( i ) * 440.0f * 6.2831853071795864769f / 22050.0f;
		pcm[static_cast<size_t>( i )] = static_cast<short>( std::sin( phase ) * 12000.0f );
	}

	defaultSample_ = true;
	missing_ = false;
	loaded_ = UploadPCM( device, pcm, { AudioSampleEncoding::PCM, 1 }, 22050 );
	if ( loaded_ ) {
		durationMs_ = 250;
	} else {
		defaultSample_ = false;
		missing_ = true;
		durationMs_ = 0;
	}
	return loaded_;
}

bool SoundSample::EnsureLoaded( OpenALDevice &device, bool allowToneFallback ) {
	if ( loaded_ ) {
		return true;
	}
	if ( loadAttempted_ ) {
		return false;
	}

	loadAttempted_ = true;

	snd_info_t info = {};
	fnql::ScopedTempMemory data( S_CodecLoad( name_.c_str(), &info ) );
	if ( data.get() == nullptr ) {
		missing_ = true;
		defaultSample_ = allowToneFallback;
		if ( allowToneFallback ) {
			return GenerateFallbackTone( device );
		}
		return false;
	}

	const AudioSampleFormat sourceFormat = DetectAudioSampleFormatFromName( name_, info.channels );
	if ( AudioSampleFormatIsEncodedSoundField( sourceFormat ) && !AudioSampleFormatCanBeRepresented( sourceFormat ) ) {
		Com_DPrintf( S_COLOR_YELLOW "WARNING: audio sample %s is tagged as %s but has %d channels; treating it as ordinary PCM if possible\n",
			name_.c_str(), AudioSampleFormatName( sourceFormat ), info.channels );
	}
	const AudioSampleFormat loadFormat = AudioSampleFormatCanBeRepresented( sourceFormat ) ? sourceFormat : AudioSampleFormat{ AudioSampleEncoding::PCM, info.channels };
	const std::vector<short> pcm = ConvertToPCM16( info, loadFormat, static_cast<const byte *>( data.get() ) );

	if ( pcm.empty() ) {
		missing_ = true;
		defaultSample_ = allowToneFallback;
		if ( allowToneFallback ) {
			return GenerateFallbackTone( device );
		}
		return false;
	}

	missing_ = false;
	defaultSample_ = false;
	if ( device.SupportsSampleFormat( loadFormat ) ) {
		loaded_ = UploadPCM( device, pcm, loadFormat, info.rate );
		if ( !loaded_ && info.channels > 2 ) {
			Com_DPrintf( S_COLOR_YELLOW "WARNING: OpenAL rejected %s buffer for %s; retrying stereo fallback\n",
				AudioSampleFormatName( loadFormat ), name_.c_str() );
		}
	}
	if ( !loaded_ && AudioSampleFormatIsEncodedSoundField( loadFormat ) ) {
		const std::vector<short> stereoPCM = DownmixEncodedSoundFieldToStereo( pcm, loadFormat );
		loaded_ = UploadPCM( device, stereoPCM, { AudioSampleEncoding::PCM, kDefaultStreamChannels }, info.rate );
		if ( loaded_ ) {
			Com_DPrintf( S_COLOR_YELLOW "WARNING: using stereo fallback for %s sample %s because the active OpenAL runtime does not accept that encoded format\n",
				AudioSampleFormatName( loadFormat ), name_.c_str() );
		}
	}
	if ( !loaded_ && info.channels > 2 ) {
		const std::vector<short> stereoPCM = DownmixPCM16ToStereo( pcm, info.channels );
		loaded_ = UploadPCM( device, stereoPCM, { AudioSampleEncoding::PCM, kDefaultStreamChannels }, info.rate );
	}

	if ( !loaded_ ) {
		missing_ = true;
		if ( allowToneFallback ) {
			return GenerateFallbackTone( device );
		}
	}
	return loaded_;
}

void SoundSample::Unload( OpenALDevice &device ) {
	if ( buffer_ != 0 ) {
		device.AL().alDeleteBuffers( 1, &buffer_ );
		buffer_ = 0;
	}
	loadAttempted_ = false;
	loaded_ = false;
	missing_ = false;
	defaultSample_ = false;
	format_ = AudioSampleFormat();
	channels_ = 0;
	rate_ = 0;
	durationMs_ = 0;
}

enum class VoiceKind {
	OneShot,
	Loop
};

class SoundVoice {
public:
	VoiceKind kind = VoiceKind::OneShot;
	bool active = false;
	bool fixedOrigin = false;
	bool looping = false;
	bool killWhenUnrefreshed = false;
	bool refreshedThisFrame = false;
	bool doppler = false;
	int handle = 0;
	int entnum = ENTITYNUM_WORLD;
	int entchannel = CHAN_AUTO;
	int allocTime = 0;
	ALuint source = 0;
	ALuint directFilter = 0;
	ALuint sendFilter = 0;
	SoundSample *sample = nullptr;
	Vec3f origin;
	Vec3f velocity;
	float occlusion = 0.0f;
	float occlusionTarget = 0.0f;
	int lastOcclusionSmoothTime = 0;
	bool occlusionInitialized = false;
	int nextEnvironmentUpdateTime = 0;
	float debugGain = 0.0f;
	float debugPan = 0.0f;
	float debugDryGain = 1.0f;
	float debugWetGain = 0.0f;
	float debugPitch = 1.0f;
	float debugDistance = 0.0f;
	bool debugPositional = false;
	bool debugDirect = false;
	const char *debugToneClass = "neutral";
	const char *debugDirectTone = "none";
	const char *debugSendTone = "none";
	float debugDirectToneLF = 1.0f;
	float debugDirectToneHF = 1.0f;
	float debugSendToneLF = 1.0f;
	float debugSendToneHF = 1.0f;

	void Release( OpenALDevice &device ) {
		if ( source != 0 ) {
			device.ReleaseVoiceSource( source );
			source = 0;
		}
		device.DestroyVoiceFilters( directFilter, sendFilter );
		active = false;
		fixedOrigin = false;
		looping = false;
		killWhenUnrefreshed = false;
		refreshedThisFrame = false;
		doppler = false;
		handle = 0;
		entnum = ENTITYNUM_WORLD;
		entchannel = CHAN_AUTO;
		allocTime = 0;
		sample = nullptr;
		origin = Vec3f();
		velocity = Vec3f();
		occlusion = 0.0f;
		occlusionTarget = 0.0f;
		lastOcclusionSmoothTime = 0;
		occlusionInitialized = false;
		nextEnvironmentUpdateTime = 0;
		debugGain = 0.0f;
		debugPan = 0.0f;
		debugDryGain = 1.0f;
		debugWetGain = 0.0f;
		debugPitch = 1.0f;
		debugDistance = 0.0f;
		debugPositional = false;
		debugDirect = false;
		debugToneClass = "neutral";
		debugDirectTone = "none";
		debugSendTone = "none";
		debugDirectToneLF = 1.0f;
		debugDirectToneHF = 1.0f;
		debugSendToneLF = 1.0f;
		debugSendToneHF = 1.0f;
	}
};

enum class SoundToneClass {
	Neutral,
	World,
	Weapon,
	Voice,
	Item,
	Body,
	Local,
	Announcer,
	Stereo,
	Multichannel,
	Underwater
};

struct VoiceToneSettings {
	SoundToneClass soundClass = SoundToneClass::Neutral;
	AudioFilterSettings directTone;
	AudioFilterSettings sendTone;
};

static const char *SoundToneClassName( SoundToneClass soundClass ) {
	switch ( soundClass ) {
	case SoundToneClass::World:
		return "world";
	case SoundToneClass::Weapon:
		return "weapon";
	case SoundToneClass::Voice:
		return "voice";
	case SoundToneClass::Item:
		return "item";
	case SoundToneClass::Body:
		return "body";
	case SoundToneClass::Local:
		return "local";
	case SoundToneClass::Announcer:
		return "announcer";
	case SoundToneClass::Stereo:
		return "stereo";
	case SoundToneClass::Multichannel:
		return "multichannel";
	case SoundToneClass::Underwater:
		return "underwater";
	default:
		return "neutral";
	}
}

static bool SampleNameStartsWith( const SoundSample *sample, const char *prefix ) {
	if ( sample == nullptr || prefix == nullptr ) {
		return false;
	}

	const std::string &name = sample->Name();
	const size_t prefixLength = std::strlen( prefix );
	return name.size() >= prefixLength && std::strncmp( name.c_str(), prefix, prefixLength ) == 0;
}

static SoundToneClass ClassifyVoiceTone( const SoundVoice &voice, const EnvironmentState &environment, bool listenerAttached, bool localOnly, bool stereoSample, bool monoWorldSource ) {
	if ( stereoSample ) {
		if ( voice.sample != nullptr && voice.sample->Channels() > kDefaultStreamChannels ) {
			return SoundToneClass::Multichannel;
		}
		return SoundToneClass::Stereo;
	}
	if ( voice.entchannel == CHAN_ANNOUNCER ) {
		return SoundToneClass::Announcer;
	}

	switch ( voice.entchannel ) {
	case CHAN_WEAPON:
		return SoundToneClass::Weapon;
	case CHAN_VOICE:
		return SoundToneClass::Voice;
	case CHAN_ITEM:
		return SoundToneClass::Item;
	case CHAN_BODY:
		return SoundToneClass::Body;
	default:
		break;
	}

	if ( localOnly || listenerAttached ) {
		return SoundToneClass::Local;
	}
	if ( SampleNameStartsWith( voice.sample, "sound/weapons/" ) ) {
		return SoundToneClass::Weapon;
	}
	if ( SampleNameStartsWith( voice.sample, "sound/items/" ) ) {
		return SoundToneClass::Item;
	}
	if ( SampleNameStartsWith( voice.sample, "sound/player/" ) ) {
		return SoundToneClass::Body;
	}
	if ( SampleNameStartsWith( voice.sample, "sound/feedback/" ) ) {
		return SoundToneClass::Local;
	}
	if ( environment.underwater && monoWorldSource ) {
		return SoundToneClass::Underwater;
	}
	if ( monoWorldSource ) {
		return SoundToneClass::World;
	}

	return SoundToneClass::Neutral;
}

static AudioFilterSettings ToneFilterFromBands( float gain, float gainLF, float gainHF, bool preferBandPass ) {
	gain = ClampFloat( gain, 0.0f, 1.0f );
	gainLF = ClampFloat( gainLF, 0.0f, 1.0f );
	gainHF = ClampFloat( gainHF, 0.0f, 1.0f );

	const bool cutsLow = gainLF < kToneNeutralThreshold;
	const bool cutsHigh = gainHF < kToneNeutralThreshold;
	if ( preferBandPass || ( cutsLow && cutsHigh ) ) {
		return BandPassFilter( gain, gainLF, gainHF );
	}
	if ( cutsLow ) {
		return HighPassFilter( gain, gainLF );
	}
	if ( cutsHigh || gain < kToneNeutralThreshold ) {
		return LowPassFilter( gain, gainHF );
	}
	return NoFilter();
}

static VoiceToneSettings BuildVoiceToneSettings( const SoundVoice &voice, const EnvironmentState &environment, bool listenerAttached, bool localOnly, bool stereoSample, bool monoWorldSource, float occlusion, float directHF, float wetGain, float wetGainHF ) {
	VoiceToneSettings settings;
	settings.soundClass = ClassifyVoiceTone( voice, environment, listenerAttached, localOnly, stereoSample, monoWorldSource );

	if ( settings.soundClass == SoundToneClass::Stereo || settings.soundClass == SoundToneClass::Multichannel ) {
		if ( wetGain > 0.001f && !localOnly ) {
			settings.sendTone = LowPassFilter( wetGain, wetGainHF );
		}
		return settings;
	}

	float directLF = ClampFloat( environment.directLF, 0.0f, 1.0f );
	float shapedDirectHF = ClampFloat( directHF, 0.0f, 1.0f );
	float sendLF = ClampFloat( environment.wetLF, 0.0f, 1.0f );
	float shapedWetHF = ClampFloat( wetGainHF, 0.0f, 1.0f );
	bool forceBandPass = false;

	switch ( settings.soundClass ) {
	case SoundToneClass::Announcer:
		directLF = ( std::min )( directLF, 1.0f - kToneAnnouncerLowCut );
		shapedDirectHF = 1.0f;
		break;
	case SoundToneClass::Local:
		directLF = ( std::min )( directLF, 1.0f - kToneLocalLowCut );
		shapedDirectHF = 1.0f;
		break;
	case SoundToneClass::Item:
		directLF = ( std::min )( directLF, 1.0f - kToneItemLowCut );
		break;
	case SoundToneClass::Voice:
		directLF = ( std::min )( directLF, 1.0f - kToneVoiceLowCut );
		shapedDirectHF = ( std::min )( shapedDirectHF, 0.96f );
		break;
	case SoundToneClass::Body:
		shapedDirectHF = ( std::min )( shapedDirectHF, 1.0f - kToneBodyHighCut );
		break;
	case SoundToneClass::Weapon:
		directLF = ( std::min )( directLF, 0.98f );
		break;
	default:
		break;
	}

	if ( environment.underwater && settings.soundClass != SoundToneClass::Announcer && settings.soundClass != SoundToneClass::Local ) {
		directLF = ( std::min )( directLF, 1.0f - kToneUnderwaterLowCut );
		shapedDirectHF = ( std::min )( shapedDirectHF, environment.directHF * ( 1.0f - kToneUnderwaterHighCut ) );
		sendLF = ( std::min )( sendLF, 0.82f );
		shapedWetHF = ( std::min )( shapedWetHF, environment.wetHF * ( 1.0f - kToneUnderwaterHighCut ) );
		forceBandPass = true;
	}

	if ( occlusion >= kToneStrongOcclusionThreshold && settings.soundClass != SoundToneClass::Announcer &&
		settings.soundClass != SoundToneClass::Local ) {
		const float occlusionBlend = ClampFloat( ( occlusion - kToneStrongOcclusionThreshold ) / ( 1.0f - kToneStrongOcclusionThreshold ), 0.0f, 1.0f );
		directLF = ( std::min )( directLF, 1.0f - occlusionBlend * kToneStrongOcclusionLowCut );
		sendLF = ( std::min )( sendLF, 1.0f - occlusionBlend * 0.12f );
		forceBandPass = true;
	}

	settings.directTone = ToneFilterFromBands( 1.0f, directLF, shapedDirectHF, forceBandPass );
	if ( wetGain > 0.001f && !localOnly ) {
		settings.sendTone = ToneFilterFromBands( wetGain, sendLF, shapedWetHF, forceBandPass );
	}

	return settings;
}

static void ResetVoiceOcclusion( SoundVoice &voice, float value, int now ) {
	value = ClampFloat( value, 0.0f, 1.0f );
	voice.occlusion = value;
	voice.occlusionTarget = value;
	voice.lastOcclusionSmoothTime = now;
	voice.occlusionInitialized = true;
}

static void SetVoiceOcclusionTarget( SoundVoice &voice, float measured, int now ) {
	measured = ClampFloat( measured, 0.0f, 1.0f );
	if ( !voice.occlusionInitialized ) {
		ResetVoiceOcclusion( voice, measured, now );
		return;
	}

	voice.occlusionTarget = ApplyOcclusionHysteresis( voice.occlusionTarget, measured );
}

static void SmoothVoiceOcclusion( SoundVoice &voice, int now ) {
	if ( !voice.occlusionInitialized ) {
		ResetVoiceOcclusion( voice, 0.0f, now );
		return;
	}

	const int elapsedMs = ClampInt( now - voice.lastOcclusionSmoothTime, 0, 200 );
	if ( elapsedMs <= 0 ) {
		return;
	}

	const float rate = ( voice.occlusionTarget > voice.occlusion ) ? kOcclusionAttackPerSecond : kOcclusionReleasePerSecond;
	voice.occlusion = MoveFloatTowards( voice.occlusion, voice.occlusionTarget, rate * static_cast<float>( elapsedMs ) * 0.001f );
	voice.lastOcclusionSmoothTime = now;
}

struct SpatialParams {
	float gain = 0.0f;
	float pan = 0.0f;
};

static SpatialParams ComputeSpatialParams( const float *origin, const float *listenerOrigin, const float listenerAxis[3][3], int masterVol, float referenceScale, float rangeScale ) {
	float sourceVec[3];
	float rotated[3];

	referenceScale = ClampFloat( referenceScale, 0.5f, 2.0f );
	rangeScale = ClampFloat( rangeScale, 0.5f, 2.0f );
	VectorSubtract( origin, listenerOrigin, sourceVec );

	float dist = VectorNormalize( sourceVec );
	const float fullVolumeDistance = kQ3SoundFullVolumeDistance * referenceScale;
	const float maxDistance = ( std::max )( kOpenALMaxDistance * rangeScale, fullVolumeDistance + 1.0f );
	dist -= fullVolumeDistance;
	if ( dist < 0.0f ) {
		dist = 0.0f;
	}
	dist /= maxDistance - fullVolumeDistance;

	VectorRotate( sourceVec, listenerAxis, rotated );

	float rightScale = 0.5f * ( 1.0f - rotated[1] );
	float leftScale = 0.5f * ( 1.0f + rotated[1] );
	rightScale = ClampFloat( rightScale, 0.0f, 1.0f );
	leftScale = ClampFloat( leftScale, 0.0f, 1.0f );

	const float right = static_cast<float>( masterVol ) * ( 1.0f - dist ) * rightScale;
	const float left = static_cast<float>( masterVol ) * ( 1.0f - dist ) * leftScale;
	const float maxSide = ( std::max )( left, right );
	const float sum = left + right;

	SpatialParams params;
	params.gain = ClampFloat( maxSide / 127.0f, 0.0f, 1.0f );
	if ( sum > 0.0f ) {
		params.pan = ClampFloat( ( right - left ) / sum, -1.0f, 1.0f );
	}
	return params;
}

static bool IsLocalOnlyChannel( int entchannel ) {
	switch ( entchannel ) {
	case CHAN_LOCAL:
	case CHAN_LOCAL_SOUND:
	case CHAN_ANNOUNCER:
		return true;
	default:
		return false;
	}
}

static const char *VoiceRouteName( const SoundVoice &voice ) {
	if ( voice.debugPositional ) {
		return "3d";
	}
	if ( voice.debugDirect ) {
		return "direct";
	}
	return "relative";
}

struct SourceClassDebugCounter {
	const char *className = nullptr;
	int loops = 0;
	int shots = 0;
	int positional = 0;
	int direct = 0;
	int relative = 0;
	float dryGain = 0.0f;
	float wetGain = 0.0f;
	float peakGain = 0.0f;
};

static SourceClassDebugCounter *FindOrAddSourceClassDebugCounter( std::array<SourceClassDebugCounter, 12> &counters, int &counterCount, const char *className ) {
	if ( className == nullptr ) {
		className = "neutral";
	}

	for ( int i = 0; i < counterCount; ++i ) {
		if ( counters[static_cast<size_t>( i )].className != nullptr &&
			std::strcmp( counters[static_cast<size_t>( i )].className, className ) == 0 ) {
			return &counters[static_cast<size_t>( i )];
		}
	}

	if ( counterCount >= static_cast<int>( counters.size() ) ) {
		return nullptr;
	}

	SourceClassDebugCounter &counter = counters[static_cast<size_t>( counterCount++ )];
	counter.className = className;
	return &counter;
}

static void AccumulateSourceClassDebugCounter( std::array<SourceClassDebugCounter, 12> &counters, int &counterCount, const SoundVoice &voice ) {
	if ( !voice.active ) {
		return;
	}

	SourceClassDebugCounter *counter = FindOrAddSourceClassDebugCounter( counters, counterCount, voice.debugToneClass );
	if ( counter == nullptr ) {
		return;
	}

	if ( voice.looping ) {
		++counter->loops;
	} else {
		++counter->shots;
	}
	if ( voice.debugPositional ) {
		++counter->positional;
	} else if ( voice.debugDirect ) {
		++counter->direct;
	} else {
		++counter->relative;
	}
	counter->dryGain += voice.debugDryGain;
	counter->wetGain += voice.debugWetGain;
	counter->peakGain = ( std::max )( counter->peakGain, voice.debugGain );
}

class Q3SoundWorld {
public:
	void Reset( OpenALDevice *device );
	void StopAllSounds();
	void ClearSoundBuffer();
	void ClearLoopingSounds( qboolean killall );
	void StopLoopingSound( int entityNum );
	void AddLoopingSound( int entityNum, const float *origin, const float *velocity, sfxHandle_t handle, SoundSample *sample, qboolean realLoop );
	void StartSound( int entityNum, int entchannel, sfxHandle_t handle, SoundSample *sample, const float *origin );
	void UpdateEntityPosition( int entityNum, const float *origin );
	void Respatialize( int entityNum, const float *origin, float axis[3][3] );
	void Update( qboolean softMuted );
	qboolean GetSpatialDebugInfo( spatialAudioDebugInfo_t *info, const OpenALDevice &device, int preferredEntity, int overlayMode ) const;
	void DumpSpatialDebug( const OpenALDevice &device, int preferredEntity ) const;
	void PrintAudioZoneInfo() const;

	int ListenerNumber() const { return listenerNumber_; }

private:
	void ApplyVoice( SoundVoice &voice, qboolean softMuted );
	void CleanupFinishedVoices();
	bool RefreshAudioZonesForCurrentMap();
	EnvironmentState EvaluateCurrentListenerEnvironment() const;
	void RefreshEnvironment();
	void StartEnvironmentTransition( const EnvironmentState &target, int now );
	void AdvanceEnvironmentTransition( int now );
	void UpdateVoiceEnvironment( SoundVoice &voice, const float *voiceOrigin );
	SoundVoice *FindFreeOneShot();
	SoundVoice *FindEvictionCandidate();
	ALuint AllocateVoiceSourceForLoop();
	const SoundVoice *SelectDebugVoice( int preferredEntity ) const;

	OpenALDevice *device_ = nullptr;
	std::array<SoundVoice, kMaxVoices> oneShots_;
	std::array<SoundVoice, MAX_GENTITIES> loopVoices_;
	std::array<Vec3f, MAX_GENTITIES> entityOrigins_;
	int listenerNumber_ = 0;
	Vec3f listenerOrigin_;
	Vec3f listenerVelocity_;
	Vec3f lastEnvironmentProbeOrigin_;
	EnvironmentState environment_;
	EnvironmentState targetEnvironment_;
	EnvironmentState transitionStartEnvironment_;
	AudioZoneSet audioZones_;
	bool audioZonesEnabled_ = true;
	int nextEnvironmentProbeTime_ = 0;
	int environmentTransitionStartTime_ = 0;
	int environmentTransitionEndTime_ = 0;
	int lastListenerUpdateTime_ = 0;
	float listenerAxis_[3][3] = {
		{ 1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f }
	};
};

void Q3SoundWorld::Reset( OpenALDevice *device ) {
	device_ = device;
	listenerNumber_ = 0;
	listenerOrigin_ = Vec3f();
	listenerVelocity_ = Vec3f();
	lastEnvironmentProbeOrigin_ = Vec3f();
	environment_ = EnvironmentState();
	targetEnvironment_ = environment_;
	transitionStartEnvironment_ = environment_;
	audioZones_.Clear();
	audioZonesEnabled_ = true;
	nextEnvironmentProbeTime_ = 0;
	environmentTransitionStartTime_ = 0;
	environmentTransitionEndTime_ = 0;
	lastListenerUpdateTime_ = 0;
	for ( int i = 0; i < 3; ++i ) {
		for ( int j = 0; j < 3; ++j ) {
			listenerAxis_[i][j] = ( i == j ) ? 1.0f : 0.0f;
		}
	}
	for ( SoundVoice &voice : oneShots_ ) {
		voice.Release( *device_ );
	}
	for ( SoundVoice &voice : loopVoices_ ) {
		voice.Release( *device_ );
		voice.kind = VoiceKind::Loop;
	}
	for ( Vec3f &origin : entityOrigins_ ) {
		origin = Vec3f();
	}
	if ( device_ != nullptr ) {
		device_->UpdateListener( listenerOrigin_.Data(), listenerVelocity_.Data(), listenerAxis_ );
		device_->UpdateReverb( environment_ );
	}
}

void Q3SoundWorld::StopAllSounds() {
	for ( SoundVoice &voice : oneShots_ ) {
		voice.Release( *device_ );
	}
	for ( SoundVoice &voice : loopVoices_ ) {
		voice.Release( *device_ );
		voice.kind = VoiceKind::Loop;
	}
}

void Q3SoundWorld::ClearSoundBuffer() {
	StopAllSounds();
}

void Q3SoundWorld::ClearLoopingSounds( qboolean killall ) {
	for ( SoundVoice &voice : loopVoices_ ) {
		if ( !voice.active ) {
			continue;
		}
		if ( killall ) {
			voice.Release( *device_ );
			voice.kind = VoiceKind::Loop;
			continue;
		}
		voice.refreshedThisFrame = qfalse;
	}
}

void Q3SoundWorld::StopLoopingSound( int entityNum ) {
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES ) {
		return;
	}
	loopVoices_[entityNum].Release( *device_ );
	loopVoices_[entityNum].kind = VoiceKind::Loop;
}

ALuint Q3SoundWorld::AllocateVoiceSourceForLoop() {
	if ( device_->FreeVoiceCount() <= kReservedLoopFloor ) {
		return 0;
	}
	return device_->AcquireVoiceSource();
}

void Q3SoundWorld::AddLoopingSound( int entityNum, const float *origin, const float *velocity, sfxHandle_t handle, SoundSample *sample, qboolean realLoop ) {
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES || sample == nullptr ) {
		return;
	}

	SoundVoice &voice = loopVoices_[entityNum];
	if ( !voice.active || voice.handle != handle ) {
		voice.Release( *device_ );
		voice.kind = VoiceKind::Loop;
		voice.active = true;
		voice.handle = handle;
		voice.sample = sample;
		voice.entnum = entityNum;
		voice.entchannel = CHAN_AUTO;
		voice.looping = true;
		voice.occlusion = 0.0f;
		voice.occlusionTarget = 0.0f;
		voice.lastOcclusionSmoothTime = 0;
		voice.occlusionInitialized = false;
		voice.nextEnvironmentUpdateTime = 0;
	}

	voice.fixedOrigin = qtrue;
	voice.killWhenUnrefreshed = !realLoop;
	voice.refreshedThisFrame = qtrue;
	voice.origin.Set( origin );
	voice.velocity.Set( velocity );
	voice.doppler = ( s_doppler != nullptr && s_doppler->integer != 0 && VectorLengthSquared( voice.velocity.Data() ) > 0.0f );
}

SoundVoice *Q3SoundWorld::FindFreeOneShot() {
	for ( SoundVoice &voice : oneShots_ ) {
		if ( !voice.active ) {
			return &voice;
		}
	}
	return nullptr;
}

SoundVoice *Q3SoundWorld::FindEvictionCandidate() {
	SoundVoice *chosen = nullptr;

	for ( SoundVoice &voice : oneShots_ ) {
		if ( !voice.active || voice.entnum == listenerNumber_ || voice.entchannel == CHAN_ANNOUNCER ) {
			continue;
		}
		if ( chosen == nullptr || voice.allocTime < chosen->allocTime ) {
			chosen = &voice;
		}
	}

	if ( chosen != nullptr ) {
		return chosen;
	}

	for ( SoundVoice &voice : oneShots_ ) {
		if ( !voice.active ) {
			continue;
		}
		if ( chosen == nullptr || voice.allocTime < chosen->allocTime ) {
			chosen = &voice;
		}
	}

	return chosen;
}

void Q3SoundWorld::StartSound( int entityNum, int entchannel, sfxHandle_t handle, SoundSample *sample, const float *origin ) {
	if ( sample == nullptr ) {
		return;
	}

	if ( origin == nullptr && ( entityNum < 0 || entityNum >= MAX_GENTITIES ) ) {
		Com_Error( ERR_DROP, "S_StartSound: bad entitynum %i", entityNum );
	}

	const int startTime = Com_Milliseconds();

	if ( entityNum != ENTITYNUM_WORLD ) {
		for ( SoundVoice &voice : oneShots_ ) {
			if ( !voice.active ) {
				continue;
			}
			if ( voice.entnum == entityNum && voice.allocTime == startTime && voice.handle == handle ) {
				return;
			}
		}
	}

	const int allowed = ( entityNum == listenerNumber_ ) ? 16 : 8;
	int inPlay = 0;
	for ( SoundVoice &voice : oneShots_ ) {
		if ( !voice.active ) {
			continue;
		}
		if ( voice.entnum == entityNum && voice.handle == handle ) {
			if ( startTime - voice.allocTime < 20 ) {
				return;
			}
			++inPlay;
		}
	}

	if ( inPlay > allowed ) {
		return;
	}

	SoundVoice *voice = FindFreeOneShot();
	if ( voice == nullptr ) {
		voice = FindEvictionCandidate();
		if ( voice == nullptr ) {
			return;
		}
		voice->Release( *device_ );
	}

	if ( voice->source == 0 ) {
		voice->source = device_->AcquireVoiceSource();
		if ( voice->source == 0 ) {
			SoundVoice *candidate = FindEvictionCandidate();
			if ( candidate != nullptr && candidate != voice ) {
				candidate->Release( *device_ );
				voice->source = device_->AcquireVoiceSource();
			}
			if ( voice->source == 0 ) {
				voice->active = qfalse;
				return;
			}
		}
		device_->CreateVoiceFilters( voice->directFilter, voice->sendFilter );
	}

	voice->kind = VoiceKind::OneShot;
	voice->active = qtrue;
	voice->sample = sample;
	voice->handle = handle;
	voice->entnum = entityNum;
	voice->entchannel = entchannel;
	voice->allocTime = startTime;
	voice->fixedOrigin = ( origin != nullptr );
	voice->origin.Set( origin );
	voice->velocity = Vec3f();
	voice->doppler = qfalse;
	voice->looping = qfalse;
	voice->killWhenUnrefreshed = qfalse;
	voice->refreshedThisFrame = qfalse;
	voice->occlusion = 0.0f;
	voice->occlusionTarget = 0.0f;
	voice->lastOcclusionSmoothTime = 0;
	voice->occlusionInitialized = false;
	voice->nextEnvironmentUpdateTime = 0;

	device_->AL().alSourceStop( voice->source );
	device_->AL().alSourcei( voice->source, AL_BUFFER, sample->Buffer() );
	device_->AL().alSourcei( voice->source, AL_LOOPING, AL_FALSE );
	ApplyVoice( *voice, qfalse );
	device_->AL().alSourcePlay( voice->source );
}

void Q3SoundWorld::UpdateEntityPosition( int entityNum, const float *origin ) {
	if ( entityNum < 0 || entityNum >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "S_UpdateEntityPosition: bad entitynum %i", entityNum );
	}
	entityOrigins_[entityNum].Set( origin );
}

void Q3SoundWorld::Respatialize( int entityNum, const float *origin, float axis[3][3] ) {
	const int now = Com_Milliseconds();
	float velocity[3];

	VectorClear( velocity );
	if ( origin != nullptr && lastListenerUpdateTime_ > 0 ) {
		const int elapsedMs = now - lastListenerUpdateTime_;
		if ( elapsedMs > 0 && elapsedMs <= 500 ) {
			float delta[3];
			VectorSubtract( origin, listenerOrigin_.Data(), delta );
			VectorScale( delta, 1000.0f / static_cast<float>( elapsedMs ), velocity );
		}
	}

	listenerNumber_ = entityNum;
	listenerOrigin_.Set( origin );
	listenerVelocity_.Set( velocity );
	lastListenerUpdateTime_ = now;
	for ( int i = 0; i < 3; ++i ) {
		if ( axis != nullptr ) {
			VectorCopy( axis[i], listenerAxis_[i] );
		} else {
			VectorClear( listenerAxis_[i] );
			listenerAxis_[i][i] = 1.0f;
		}
	}
	if ( device_ != nullptr ) {
		device_->UpdateListener( listenerOrigin_.Data(), listenerVelocity_.Data(), listenerAxis_ );
	}
	RefreshEnvironment();
}

bool Q3SoundWorld::RefreshAudioZonesForCurrentMap() {
	const bool enabled = ( s_alAudioZones == nullptr || s_alAudioZones->integer != 0 );
	const std::string zoneQpath = enabled ? AudioZoneQpathForCurrentMap() : std::string();
	if ( enabled == audioZonesEnabled_ && zoneQpath == audioZones_.Qpath() ) {
		return false;
	}

	audioZonesEnabled_ = enabled;
	if ( !enabled ) {
		if ( audioZones_.Loaded() ) {
			Com_Printf( "Audio zones disabled\n" );
		}
		audioZones_.Clear();
		return true;
	}

	if ( zoneQpath.empty() ) {
		audioZones_.Clear();
		return true;
	}

	audioZones_.Load( zoneQpath.c_str() );
	return true;
}

EnvironmentState Q3SoundWorld::EvaluateCurrentListenerEnvironment() const {
	if ( audioZones_.Loaded() ) {
		EnvironmentState zoneEnvironment;
		if ( audioZones_.EvaluateEnvironment( listenerOrigin_.Data(), zoneEnvironment ) ) {
			return zoneEnvironment;
		}
	}

	return EvaluateListenerEnvironment( listenerOrigin_.Data() );
}

void Q3SoundWorld::RefreshEnvironment() {
	if ( device_ == nullptr ) {
		return;
	}

	const int now = Com_Milliseconds();
	if ( RefreshAudioZonesForCurrentMap() ) {
		nextEnvironmentProbeTime_ = 0;
	}
	AdvanceEnvironmentTransition( now );
	const float moved = DistanceBetweenPoints( listenerOrigin_.Data(), lastEnvironmentProbeOrigin_.Data() );
	if ( now < nextEnvironmentProbeTime_ && moved < 48.0f ) {
		device_->UpdateReverb( environment_ );
		return;
	}

	const EnvironmentState measured = EvaluateCurrentListenerEnvironment();
	if ( EnvironmentStateDiffers( measured, targetEnvironment_ ) ) {
		StartEnvironmentTransition( measured, now );
	}
	AdvanceEnvironmentTransition( now );
	lastEnvironmentProbeOrigin_ = listenerOrigin_;
	nextEnvironmentProbeTime_ = now + kEnvironmentProbeIntervalMs;
	device_->UpdateReverb( environment_ );
}

void Q3SoundWorld::StartEnvironmentTransition( const EnvironmentState &target, int now ) {
	targetEnvironment_ = target;
	if ( !EnvironmentStateDiffers( environment_, targetEnvironment_ ) ) {
		environment_ = targetEnvironment_;
		transitionStartEnvironment_ = environment_;
		environmentTransitionStartTime_ = now;
		environmentTransitionEndTime_ = now;
		return;
	}

	transitionStartEnvironment_ = environment_;
	environmentTransitionStartTime_ = now;
	environmentTransitionEndTime_ = now + ClampInt( target.transitionMs, 0, 10000 );
}

void Q3SoundWorld::AdvanceEnvironmentTransition( int now ) {
	if ( environmentTransitionEndTime_ <= environmentTransitionStartTime_ || now >= environmentTransitionEndTime_ ) {
		environment_ = targetEnvironment_;
		transitionStartEnvironment_ = environment_;
		environmentTransitionStartTime_ = now;
		environmentTransitionEndTime_ = now;
		return;
	}

	const float linearBlend = static_cast<float>( now - environmentTransitionStartTime_ ) /
		static_cast<float>( environmentTransitionEndTime_ - environmentTransitionStartTime_ );
	environment_ = BlendEnvironmentStates( transitionStartEnvironment_, targetEnvironment_, SmoothStep( linearBlend ) );
}

void Q3SoundWorld::UpdateVoiceEnvironment( SoundVoice &voice, const float *voiceOrigin ) {
	if ( !voice.active || voiceOrigin == nullptr ) {
		return;
	}

	const int now = Com_Milliseconds();
	if ( now < voice.nextEnvironmentUpdateTime ) {
		return;
	}

	if ( voice.sample == nullptr || !OcclusionEnabled() || !CollisionWorldReady() ) {
		ResetVoiceOcclusion( voice, 0.0f, now );
		voice.nextEnvironmentUpdateTime = now + kVoiceEnvironmentIntervalMs;
		return;
	}

	SetVoiceOcclusionTarget( voice,
		ClampFloat( ComputeOcclusionFactor( listenerOrigin_.Data(), voiceOrigin ) * environment_.occlusionMultiplier, 0.0f, 1.0f ),
		now );
	voice.nextEnvironmentUpdateTime = now + kVoiceEnvironmentIntervalMs;
}

void Q3SoundWorld::ApplyVoice( SoundVoice &voice, qboolean softMuted ) {
	if ( !voice.active || voice.source == 0 || voice.sample == nullptr ) {
		return;
	}

	const float *voiceOrigin = voice.fixedOrigin ? voice.origin.Data() : entityOrigins_[voice.entnum].Data();
	const float *sourceVelocity = voice.velocity.Data();
	const bool listenerAttached = ( voice.entnum == listenerNumber_ );
	const bool localOnly = IsLocalOnlyChannel( voice.entchannel );
	const bool stereoSample = ( voice.sample->Channels() > 1 );
	const bool twoChannelSample = ( voice.sample->Channels() == 2 );
	const bool encodedSoundField = voice.sample->EncodedSoundField();
	const bool stereoSpatializeRequested = ( s_alSpatializeStereo != nullptr && s_alSpatializeStereo->integer );
	const bool spatializedStereoWorldSource = ( twoChannelSample && !encodedSoundField && stereoSpatializeRequested && device_->Capabilities().sourceSpatialize &&
		!listenerAttached && !localOnly );
	const bool monoWorldSource = ( voice.sample->Channels() == 1 && !listenerAttached && !localOnly );
	const bool positionalSource = monoWorldSource || spatializedStereoWorldSource;
	const bool directStereoSource = stereoSample && !encodedSoundField && !spatializedStereoWorldSource;
	const bool useOpenALDistance = positionalSource && device_->UsesOpenALDistanceAttenuation();
	const SoundShaderSettings &shader = voice.sample->Shader();
	const float gainScale = shader.enabled ? shader.gainScale : kDefaultSoundShaderScale;
	const float referenceScale = shader.enabled ? shader.referenceScale : kDefaultSoundShaderScale;
	const float rangeScale = shader.enabled ? shader.rangeScale : kDefaultSoundShaderScale;
	const float wetScale = shader.enabled ? shader.wetScale : kDefaultSoundShaderScale;
	const float pitchScale = shader.enabled ? shader.pitchScale : kDefaultSoundShaderScale;
	SpatialParams spatial;
	if ( listenerAttached || localOnly ) {
		spatial.gain = 1.0f;
		spatial.pan = 0.0f;
	} else {
		spatial = ComputeSpatialParams( voiceOrigin, listenerOrigin_.Data(), listenerAxis_, 127, referenceScale, rangeScale );
	}

	float gain = useOpenALDistance ? 1.0f : spatial.gain;
	float directGain = 1.0f;
	float directToneHF = environment_.directHF;
	float wetGain = 0.0f;
	float wetGainHF = environment_.wetHF;
	float pitch = 1.0f;
	float occlusion = 0.0f;
	const int now = Com_Milliseconds();

	if ( stereoSample ) {
		spatial.pan = 0.0f;
	}

	if ( !localOnly ) {
		const float distance = listenerAttached ? 0.0f : DistanceBetweenPoints( voiceOrigin, listenerOrigin_.Data() );
		if ( listenerAttached ) {
			ResetVoiceOcclusion( voice, 0.0f, now );
		} else {
			UpdateVoiceEnvironment( voice, voiceOrigin );
			SmoothVoiceOcclusion( voice, now );
		}
		occlusion = voice.occlusion;
		const float distanceMix = ClampFloat( distance / 512.0f, 0.0f, 1.0f );
		directGain = occ::DirectGain( occlusion );
		directToneHF = occ::DirectHF( environment_.directHF, occlusion );
		wetGain = ClampFloat( occ::WetGain( environment_.baseWet, distanceMix, occlusion ) * wetScale, 0.0f, 2.0f );
		wetGainHF = occ::WetHF( environment_.wetHF, occlusion );
	} else {
		ResetVoiceOcclusion( voice, 0.0f, now );
	}

	if ( softMuted ) {
		gain = 0.0f;
		wetGain = 0.0f;
	} else if ( voice.doppler ) {
		pitch = ComputeDopplerPitch( listenerOrigin_.Data(), voiceOrigin, sourceVelocity );
	}
	gain = ClampFloat( gain * gainScale, 0.0f, 2.0f );
	pitch = ClampFloat( pitch * pitchScale, 0.5f, 2.0f );

	const VoiceToneSettings toneSettings = BuildVoiceToneSettings( voice, environment_, listenerAttached, localOnly, stereoSample, positionalSource, occlusion, directToneHF, wetGain, wetGainHF );

	voice.debugGain = gain;
	voice.debugPan = spatial.pan;
	voice.debugDryGain = directGain;
	voice.debugWetGain = wetGain;
	voice.debugPitch = pitch;
	voice.debugDistance = DistanceBetweenPoints( voiceOrigin, listenerOrigin_.Data() );
	voice.debugPositional = positionalSource;
	voice.debugDirect = directStereoSource;
	voice.debugToneClass = SoundToneClassName( toneSettings.soundClass );
	voice.debugDirectTone = AudioFilterKindName( toneSettings.directTone.kind );
	voice.debugSendTone = AudioFilterKindName( toneSettings.sendTone.kind );
	voice.debugDirectToneLF = toneSettings.directTone.gainLF;
	voice.debugDirectToneHF = toneSettings.directTone.gainHF;
	voice.debugSendToneLF = toneSettings.sendTone.gainLF;
	voice.debugSendToneHF = toneSettings.sendTone.gainHF;

	device_->AL().alSourcef( voice.source, AL_PITCH, pitch );

	if ( positionalSource ) {
		device_->SetSourceSpatialize( voice.source, true );
		device_->SetSourceDirectChannels( voice.source, false );
		device_->AL().alSourcei( voice.source, AL_SOURCE_RELATIVE, AL_FALSE );
		device_->ConfigureSourceDistance( voice.source, true, referenceScale, rangeScale );
		device_->AL().alSource3f( voice.source, AL_POSITION, voiceOrigin[0], voiceOrigin[1], voiceOrigin[2] );
		device_->AL().alSource3f( voice.source, AL_VELOCITY, sourceVelocity[0], sourceVelocity[1], sourceVelocity[2] );
	} else {
		device_->SetSourceSpatialize( voice.source, false );
		device_->SetSourceDirectChannels( voice.source, directStereoSource );
		device_->AL().alSourcei( voice.source, AL_SOURCE_RELATIVE, AL_TRUE );
		device_->ConfigureSourceDistance( voice.source, false, kDefaultSoundShaderScale, kDefaultSoundShaderScale );
		device_->AL().alSource3f( voice.source, AL_POSITION, spatial.pan * kPanScale, 0.0f, -1.0f );
		device_->AL().alSource3f( voice.source, AL_VELOCITY, 0.0f, 0.0f, 0.0f );
	}

	device_->ApplyVoiceRouting( voice.source, voice.directFilter, voice.sendFilter, gain, directGain, toneSettings.directTone, toneSettings.sendTone );

	if ( voice.looping ) {
		ALint state = 0;
		device_->AL().alGetSourcei( voice.source, AL_SOURCE_STATE, &state );
		if ( state != AL_PLAYING ) {
			device_->AL().alSourcei( voice.source, AL_BUFFER, voice.sample->Buffer() );
			device_->AL().alSourcei( voice.source, AL_LOOPING, AL_TRUE );
			device_->AL().alSourcePlay( voice.source );
		}
	}
}

const SoundVoice *Q3SoundWorld::SelectDebugVoice( int preferredEntity ) const {
	const SoundVoice *selected = nullptr;
	float bestDistance = 0.0f;

	auto considerVoice = [&]( const SoundVoice &voice ) {
		if ( !voice.active || voice.sample == nullptr ) {
			return;
		}

		if ( preferredEntity >= 0 && voice.entnum != preferredEntity ) {
			return;
		}

		const float *voiceOrigin = voice.fixedOrigin ? voice.origin.Data() : entityOrigins_[voice.entnum].Data();
		const float distance = DistanceBetweenPoints( voiceOrigin, listenerOrigin_.Data() );
		if ( selected == nullptr || distance < bestDistance ) {
			selected = &voice;
			bestDistance = distance;
		}
	};

	if ( preferredEntity >= 0 ) {
		for ( const SoundVoice &voice : loopVoices_ ) {
			considerVoice( voice );
		}
		for ( const SoundVoice &voice : oneShots_ ) {
			considerVoice( voice );
		}
		return selected;
	}

	for ( const SoundVoice &voice : loopVoices_ ) {
		if ( voice.entnum == listenerNumber_ ) {
			continue;
		}
		considerVoice( voice );
	}
	if ( selected != nullptr ) {
		return selected;
	}

	for ( const SoundVoice &voice : oneShots_ ) {
		if ( voice.entnum == listenerNumber_ ) {
			continue;
		}
		considerVoice( voice );
	}
	if ( selected != nullptr ) {
		return selected;
	}

	for ( const SoundVoice &voice : loopVoices_ ) {
		considerVoice( voice );
	}
	for ( const SoundVoice &voice : oneShots_ ) {
		considerVoice( voice );
	}

	return selected;
}

qboolean Q3SoundWorld::GetSpatialDebugInfo( spatialAudioDebugInfo_t *info, const OpenALDevice &device, int preferredEntity, int overlayMode ) const {
	const SoundVoice *selected;
	int activeOneShots = 0;
	int activeLoops = 0;
	std::array<char, 64> environmentSummary;
	std::array<char, 128> zoneSummary;

	if ( info == nullptr || overlayMode <= 0 ) {
		return qfalse;
	}

	Com_Memset( info, 0, sizeof( *info ) );
	info->active = qtrue;

	for ( const SoundVoice &voice : oneShots_ ) {
		if ( voice.active ) {
			++activeOneShots;
		}
	}
	for ( const SoundVoice &voice : loopVoices_ ) {
		if ( voice.active ) {
			++activeLoops;
		}
	}

	FormatEnvironmentSummary( environment_, environmentSummary.data(), environmentSummary.size() );
	FormatAudioZoneSummary( environment_, zoneSummary.data(), zoneSummary.size() );
	Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS,
		"spatial %s reverb:%s env:%s",
		device.LibraryName().empty() ? "openal" : "openal-soft",
		device.CurrentReverbName(),
		environmentSummary.data() );
	Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS,
		"listener:%d voices shot:%d loop:%d zone:%s",
		listenerNumber_, activeOneShots, activeLoops,
		zoneSummary.data() );
	if ( overlayMode > 1 ) {
		Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS,
			"env wet:%.2f lf/hf %.2f/%.2f wet %.2f/%.2f occx:%.2f",
			environment_.baseWet,
			environment_.directLF,
			environment_.directHF,
			environment_.wetLF,
			environment_.wetHF,
			environment_.occlusionMultiplier );
		Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS,
			"env flags out:%d water:%d mat:%s transition:%dms",
			environment_.outdoors, environment_.underwater,
			environment_.audioZone ? AudioZoneMaterialClassName( environment_.zoneMaterialClass ) : "none",
			environment_.transitionMs );
	}

	selected = SelectDebugVoice( preferredEntity );
	if ( selected == nullptr ) {
		Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS, "selected voice: none" );
		return qtrue;
	}

	info->hasSelectedVoice = qtrue;
	info->dryGain = selected->debugDryGain;
	info->wetGain = selected->debugWetGain;
	info->occlusion = selected->occlusion;
	info->pan = selected->debugPan;
	info->pitch = selected->debugPitch;

	Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS,
		"selected ent:%d %s %s dist:%.0f occ:%.2f->%.2f",
		selected->entnum,
		selected->looping ? "loop" : "shot",
		VoiceRouteName( *selected ),
		selected->debugDistance,
		selected->occlusion,
		selected->occlusionTarget );

	if ( overlayMode > 1 ) {
		Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS,
			"sample:%s",
			( selected->sample != nullptr ) ? selected->sample->Name().c_str() : "none" );
		Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS,
			"gain:%.2f pan:%.2f pitch:%.2f dry:%.2f wet:%.2f transient:%d",
			selected->debugGain, selected->debugPan, selected->debugPitch,
			selected->debugDryGain, selected->debugWetGain, selected->killWhenUnrefreshed ? 1 : 0 );
		Com_sprintf( info->lines[info->lineCount++], S_SPATIAL_DEBUG_LINE_CHARS,
			"tone:%s direct:%s %.2f/%.2f send:%s %.2f/%.2f",
			selected->debugToneClass,
			selected->debugDirectTone,
			selected->debugDirectToneLF,
			selected->debugDirectToneHF,
			selected->debugSendTone,
			selected->debugSendToneLF,
			selected->debugSendToneHF );
	}

	return qtrue;
}

void Q3SoundWorld::DumpSpatialDebug( const OpenALDevice &device, int preferredEntity ) const {
	const SoundVoice *selected = SelectDebugVoice( preferredEntity );
	std::array<char, 64> environmentSummary;
	std::array<char, 128> zoneSummary;
	std::array<SourceClassDebugCounter, 12> sourceClassCounters = {};
	int sourceClassCounterCount = 0;

	FormatEnvironmentSummary( environment_, environmentSummary.data(), environmentSummary.size() );
	FormatAudioZoneSummary( environment_, zoneSummary.data(), zoneSummary.size() );
	Com_Printf( "----- Spatial Audio Debug -----\n" );
	Com_Printf( "Environment: %s (zone %s, reverb %s, blend %.2f, wet %.2f, directLF/HF %.2f/%.2f, wetLF/HF %.2f/%.2f, occx %.2f, outdoors %d, underwater %d, transition %dms)\n",
		environmentSummary.data(),
		zoneSummary.data(),
		device.CurrentReverbName(),
		ClampFloat( environment_.blend, 0.0f, 1.0f ),
		environment_.baseWet,
		environment_.directLF,
		environment_.directHF,
		environment_.wetLF,
		environment_.wetHF,
		environment_.occlusionMultiplier,
		environment_.outdoors,
		environment_.underwater,
		environment_.transitionMs );
	if ( environment_.audioZone ) {
		Com_Printf( "Audio zone metadata: material=%s flags=0x%02x portal=%s %.2f\n",
			AudioZoneMaterialClassName( environment_.zoneMaterialClass ),
			environment_.zoneFlags,
			environment_.zonePortalTargetName.empty() ? "none" : environment_.zonePortalTargetName.c_str(),
			ClampFloat( environment_.zonePortalBlend, 0.0f, azrt::kAudioZonePortalMaxBlend ) );
	}
	Com_Printf( "Listener entity: %d\n", listenerNumber_ );

	if ( selected != nullptr ) {
		double sourceOffsetSeconds = 0.0;
		double sourceLatencyMilliseconds = 0.0;
		Com_Printf( "Selected voice: ent=%d type=%s mode=%s sample=%s dist=%.1f occ=%.2f target=%.2f gain=%.2f pan=%.2f pitch=%.2f dry=%.2f wet=%.2f transient=%d\n",
			selected->entnum,
			selected->looping ? "loop" : "shot",
			VoiceRouteName( *selected ),
			( selected->sample != nullptr ) ? selected->sample->Name().c_str() : "none",
			selected->debugDistance,
			selected->occlusion,
			selected->occlusionTarget,
			selected->debugGain,
			selected->debugPan,
			selected->debugPitch,
			selected->debugDryGain,
			selected->debugWetGain,
			selected->killWhenUnrefreshed ? 1 : 0 );
		if ( selected->source != 0 && device.QuerySourceLatency( selected->source, sourceOffsetSeconds, sourceLatencyMilliseconds ) ) {
			Com_Printf( "Selected source timing: offset=%.3fs latency=%.2fms\n",
				sourceOffsetSeconds,
				sourceLatencyMilliseconds );
		}
		Com_Printf( "Selected tone: class=%s direct=%s lf=%.2f hf=%.2f send=%s lf=%.2f hf=%.2f\n",
			selected->debugToneClass,
			selected->debugDirectTone,
			selected->debugDirectToneLF,
			selected->debugDirectToneHF,
			selected->debugSendTone,
			selected->debugSendToneLF,
			selected->debugSendToneHF );
	}

	if ( s_alSourceClassDebug != nullptr && s_alSourceClassDebug->integer ) {
		for ( const SoundVoice &voice : loopVoices_ ) {
			AccumulateSourceClassDebugCounter( sourceClassCounters, sourceClassCounterCount, voice );
		}
		for ( const SoundVoice &voice : oneShots_ ) {
			AccumulateSourceClassDebugCounter( sourceClassCounters, sourceClassCounterCount, voice );
		}

		Com_Printf( "Source class summary:\n" );
		if ( sourceClassCounterCount == 0 ) {
			Com_Printf( "  none active\n" );
		}
		for ( int i = 0; i < sourceClassCounterCount; ++i ) {
			const SourceClassDebugCounter &counter = sourceClassCounters[static_cast<size_t>( i )];
			const int total = counter.loops + counter.shots;
			Com_Printf( "  class=%s voices=%d loops=%d shots=%d route 3d=%d direct=%d relative=%d dry=%.2f wet=%.2f peak=%.2f\n",
				counter.className != nullptr ? counter.className : "neutral",
				total,
				counter.loops,
				counter.shots,
				counter.positional,
				counter.direct,
				counter.relative,
				counter.dryGain,
				counter.wetGain,
				counter.peakGain );
		}
	}

	for ( const SoundVoice &voice : loopVoices_ ) {
		if ( !voice.active ) {
			continue;
		}
		Com_Printf( "loop ent=%d mode=%s tone=%s/%s sample=%s dist=%.1f occ=%.2f target=%.2f gain=%.2f pan=%.2f pitch=%.2f transient=%d refreshed=%d\n",
			voice.entnum,
			VoiceRouteName( voice ),
			voice.debugToneClass,
			voice.debugDirectTone,
			( voice.sample != nullptr ) ? voice.sample->Name().c_str() : "none",
			voice.debugDistance,
			voice.occlusion,
			voice.occlusionTarget,
			voice.debugGain,
			voice.debugPan,
			voice.debugPitch,
			voice.killWhenUnrefreshed ? 1 : 0,
			voice.refreshedThisFrame ? 1 : 0 );
	}
	for ( const SoundVoice &voice : oneShots_ ) {
		if ( !voice.active ) {
			continue;
		}
		Com_Printf( "shot ent=%d mode=%s tone=%s/%s sample=%s dist=%.1f occ=%.2f target=%.2f gain=%.2f pan=%.2f pitch=%.2f\n",
			voice.entnum,
			VoiceRouteName( voice ),
			voice.debugToneClass,
			voice.debugDirectTone,
			( voice.sample != nullptr ) ? voice.sample->Name().c_str() : "none",
			voice.debugDistance,
			voice.occlusion,
			voice.occlusionTarget,
			voice.debugGain,
			voice.debugPan,
			voice.debugPitch );
	}
	Com_Printf( "-------------------------------\n" );
}

void Q3SoundWorld::PrintAudioZoneInfo() const {
	if ( s_alAudioZones != nullptr && s_alAudioZones->integer == 0 ) {
		Com_Printf( "Audio zones: disabled\n" );
		return;
	}
	audioZones_.PrintStatus( environment_ );
}

void Q3SoundWorld::CleanupFinishedVoices() {
	for ( SoundVoice &voice : oneShots_ ) {
		if ( !voice.active || voice.source == 0 ) {
			continue;
		}
		ALint state = 0;
		device_->AL().alGetError();
		device_->AL().alGetSourcei( voice.source, AL_SOURCE_STATE, &state );
		if ( device_->AL().alGetError() != AL_NO_ERROR || ( state != AL_PLAYING && state != AL_PAUSED ) ) {
			voice.Release( *device_ );
		}
	}
}

void Q3SoundWorld::Update( qboolean softMuted ) {
	CleanupFinishedVoices();
	RefreshEnvironment();

	for ( SoundVoice &voice : oneShots_ ) {
		if ( voice.active ) {
			ApplyVoice( voice, softMuted );
		}
	}

	for ( SoundVoice &voice : loopVoices_ ) {
		if ( !voice.active ) {
			continue;
		}
		if ( voice.killWhenUnrefreshed && !voice.refreshedThisFrame ) {
			voice.Release( *device_ );
			voice.kind = VoiceKind::Loop;
			continue;
		}
		if ( voice.source == 0 ) {
			voice.source = AllocateVoiceSourceForLoop();
			if ( voice.source == 0 ) {
				continue;
			}
			device_->CreateVoiceFilters( voice.directFilter, voice.sendFilter );
			device_->AL().alSourcei( voice.source, AL_BUFFER, voice.sample->Buffer() );
			device_->AL().alSourcei( voice.source, AL_LOOPING, AL_TRUE );
		}
		ApplyVoice( voice, softMuted );
		voice.refreshedThisFrame = voice.killWhenUnrefreshed ? qfalse : qtrue;
	}
}
