// Included by AudioSystem.cpp inside its private implementation namespace.
// Shared helpers, cvars, filters, environments, audio zones, and listener probes.

namespace azfmt = fnql_audiozones;
namespace azrt = fnql_audiozones_runtime;
namespace adr = fnql_audio_device_recovery;
namespace occ = fnql_audio_occlusion;

constexpr int kMaxVoices = MAX_CHANNELS;
constexpr size_t kMaxRegisteredSamples = 4096;
constexpr int kReservedLoopFloor = 12;
constexpr int kInitialStreamBuffers = 48;
constexpr int kMaxStreamBuffers = 128;
constexpr int kQueuedStreamChunks = 4;
constexpr int kFallbackStreamRate = 48000;
constexpr int kDefaultStreamChannels = 2;
constexpr int kMaxPCMChannels = 8;
constexpr int kOpenALDeviceStatePollMs = 1000;
constexpr int kOpenALDeviceRecoveryRetryMs = 3000;
constexpr int kEnvironmentProbeIntervalMs = 200;
constexpr int kVoiceEnvironmentIntervalMs = 75;
constexpr int kEnvironmentTransitionMs = 650;
constexpr float kEnvironmentProbeDistance = 768.0f;
constexpr float kDiagonalProbeComponent = 0.70710677f;
constexpr float kQ3SoundFullVolumeDistance = 80.0f;
constexpr float kQ3SoundAttenuation = 0.0008f;
constexpr float kOpenALReferenceDistance = kQ3SoundFullVolumeDistance;
constexpr float kOpenALMaxDistance = kQ3SoundFullVolumeDistance + ( 1.0f / kQ3SoundAttenuation );
constexpr float kOpenALRolloffFactor = 1.0f;
constexpr float kDefaultSoundShaderScale = 1.0f;
// Sound-shader distance scaling limits. Range scale also softens rolloff
// (rolloff = 1 / rangeScale), so long-throw combat sounds carry through the
// mid field instead of only gaining a distant, quiet tail.
constexpr float kMinSoundShaderDistanceScale = 0.5f;
constexpr float kMaxSoundShaderReferenceScale = 2.0f;
constexpr float kMaxSoundShaderRangeScale = 3.0f;
constexpr float kOcclusionTargetRiseHysteresis = 0.04f;
constexpr float kOcclusionTargetFallHysteresis = 0.10f;
constexpr float kOcclusionAttackPerSecond = 5.5f;
constexpr float kOcclusionReleasePerSecond = 2.25f;
constexpr float kToneNeutralThreshold = 0.985f;
constexpr float kToneStrongOcclusionThreshold = 0.68f;
constexpr float kToneStrongOcclusionLowCut = 0.26f;
constexpr float kToneUnderwaterLowCut = 0.30f;
constexpr float kToneUnderwaterHighCut = 0.18f;
constexpr float kToneAnnouncerLowCut = 0.10f;
constexpr float kToneLocalLowCut = 0.08f;
constexpr float kToneItemLowCut = 0.06f;
constexpr float kToneVoiceLowCut = 0.08f;
constexpr float kToneBodyHighCut = 0.06f;
constexpr float kPanScale = 2.0f;
constexpr float kMetersPerGameUnit = 0.0254f;
constexpr float kListenerVelocityMax = 1500.0f;
constexpr float kListenerTeleportSpeed = 2500.0f;
constexpr float kListenerVelocitySmoothMs = 60.0f;
constexpr float kHorizonFadeStartFraction = 0.85f;
constexpr float kOneShotCullMargin = 1.15f;
constexpr float kLiquidBoundaryOcclusionFloor = 0.55f;
constexpr int kMaxStreamChunksPerService = 2;
constexpr int kOcclusionMask = CONTENTS_SOLID | CONTENTS_SLIME | CONTENTS_LAVA;
constexpr int kLiquidContents = CONTENTS_WATER | CONTENTS_SLIME | CONTENTS_LAVA;

cvar_t *s_alReverb = nullptr;
cvar_t *s_alOcclusion = nullptr;
cvar_t *s_alReverbGain = nullptr;
cvar_t *s_alOcclusionStrength = nullptr;
cvar_t *s_alDopplerFactor = nullptr;
cvar_t *s_alDopplerSpeed = nullptr;
cvar_t *s_alAirAbsorption = nullptr;
cvar_t *s_alDebugOverlay = nullptr;
cvar_t *s_alDebugVoice = nullptr;
cvar_t *s_alAudioZones = nullptr;
cvar_t *s_alSourceClassDebug = nullptr;
cvar_t *s_alAutoRecover = nullptr;

struct Vec3f {
	float v[3];

	Vec3f() : v{ 0.0f, 0.0f, 0.0f } {}

	explicit Vec3f( const float *src ) : v{ 0.0f, 0.0f, 0.0f } {
		if ( src != nullptr ) {
			VectorCopy( src, v );
		}
	}

	void Set( const float *src ) {
		if ( src != nullptr ) {
			VectorCopy( src, v );
		} else {
			VectorClear( v );
		}
	}

	const float *Data() const { return v; }
	float *Data() { return v; }
};

static int ClampInt( int value, int minimum, int maximum ) {
	if ( value < minimum ) {
		return minimum;
	}
	if ( value > maximum ) {
		return maximum;
	}
	return value;
}

static float ClampFloat( float value, float minimum, float maximum ) {
	if ( value < minimum ) {
		return minimum;
	}
	if ( value > maximum ) {
		return maximum;
	}
	return value;
}

static short ReadLittlePCM16( const byte *data ) {
	if ( data != nullptr ) {
		return LittleShort( fnql::ReadUnaligned<short>( data ) );
	}
	return 0;
}

static bool PCMByteCountFitsALsizei( size_t sampleCount ) {
	return sampleCount <= static_cast<size_t>( std::numeric_limits<ALsizei>::max() ) / sizeof( short );
}

static bool PCMFrameByteCount( int frames, int channels, int width, size_t &byteCount ) {
	byteCount = 0;
	if ( frames <= 0 || channels <= 0 || width <= 0 ) {
		return false;
	}

	const size_t frameCount = static_cast<size_t>( frames );
	const size_t channelCount = static_cast<size_t>( channels );
	const size_t sampleWidth = static_cast<size_t>( width );
	if ( frameCount > std::numeric_limits<size_t>::max() / channelCount ) {
		return false;
	}
	const size_t sampleCount = frameCount * channelCount;
	if ( sampleCount > std::numeric_limits<size_t>::max() / sampleWidth ) {
		return false;
	}

	byteCount = sampleCount * sampleWidth;
	return true;
}

static short ConvertUnsignedPCM8ToPCM16( byte sample ) {
	return static_cast<short>( ( static_cast<int>( sample ) - 128 ) << 8 );
}

static short ConvertSignedPCM8ToPCM16( byte sample ) {
	const int signedSample = ( sample < 128 ) ? static_cast<int>( sample ) : static_cast<int>( sample ) - 256;
	return static_cast<short>( signedSample << 8 );
}

static bool PCMChannelCountCanBeRepresented( int channels ) {
	switch ( channels ) {
	case 1:
	case 2:
	case 4:
	case 6:
	case 7:
	case 8:
		return true;
	default:
		return false;
	}
}

static const char *PCMChannelLayoutName( int channels ) {
	switch ( channels ) {
	case 1:
		return "mono";
	case 2:
		return "stereo";
	case 4:
		return "quad";
	case 6:
		return "5.1";
	case 7:
		return "6.1";
	case 8:
		return "7.1";
	default:
		return "unsupported";
	}
}

enum class AudioSampleEncoding {
	PCM,
	UHJ,
	BFormat2D,
	BFormat3D
};

struct AudioSampleFormat {
	AudioSampleEncoding encoding = AudioSampleEncoding::PCM;
	int channels = 0;
};

static bool AudioSampleFormatIsEncodedSoundField( const AudioSampleFormat &format ) {
	return format.encoding != AudioSampleEncoding::PCM;
}

static bool AudioSampleFormatCanBeRepresented( const AudioSampleFormat &format ) {
	switch ( format.encoding ) {
	case AudioSampleEncoding::PCM:
		return PCMChannelCountCanBeRepresented( format.channels );
	case AudioSampleEncoding::UHJ:
		return format.channels >= 2 && format.channels <= 4;
	case AudioSampleEncoding::BFormat2D:
		return format.channels == 3;
	case AudioSampleEncoding::BFormat3D:
		return format.channels == 4;
	default:
		return false;
	}
}

static const char *AudioSampleFormatName( const AudioSampleFormat &format ) {
	switch ( format.encoding ) {
	case AudioSampleEncoding::PCM:
		return PCMChannelLayoutName( format.channels );
	case AudioSampleEncoding::UHJ:
		switch ( format.channels ) {
		case 2:
			return "UHJ 2-channel";
		case 3:
			return "UHJ 3-channel";
		case 4:
			return "UHJ 4-channel";
		default:
			return "UHJ unsupported";
		}
	case AudioSampleEncoding::BFormat2D:
		return "B-Format 2D";
	case AudioSampleEncoding::BFormat3D:
		return "B-Format 3D";
	default:
		return "unsupported";
	}
}

static bool AudioTagBoundary( char c ) {
	return c == '\0' || c == '/' || c == '\\' || c == '.' || c == '_' || c == '-' || c == ' ' || c == '+';
}

static std::string LowercaseString( const std::string &value ) {
	std::string lower;
	lower.reserve( value.size() );
	for ( char c : value ) {
		lower.push_back( static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) ) );
	}
	return lower;
}

static bool StringHasDelimitedTag( const std::string &lowerName, const char *tag ) {
	if ( tag == nullptr || tag[0] == '\0' ) {
		return false;
	}

	const std::string needle = tag;
	size_t pos = lowerName.find( needle );
	while ( pos != std::string::npos ) {
		const char before = ( pos == 0 ) ? '\0' : lowerName[pos - 1u];
		const size_t afterPos = pos + needle.size();
		const char after = ( afterPos >= lowerName.size() ) ? '\0' : lowerName[afterPos];
		if ( AudioTagBoundary( before ) && AudioTagBoundary( after ) ) {
			return true;
		}
		pos = lowerName.find( needle, pos + 1u );
	}

	return false;
}

static bool NameHasAnyAudioTag( const std::string &lowerName, const char *const *tags, size_t tagCount ) {
	for ( size_t i = 0; i < tagCount; ++i ) {
		if ( StringHasDelimitedTag( lowerName, tags[i] ) ) {
			return true;
		}
	}
	return false;
}

static AudioSampleFormat DetectAudioSampleFormatFromName( const std::string &name, int channels ) {
	const std::string lowerName = LowercaseString( name );
	const char *uhjTags[] = { "uhj", "uhj2", "uhj2ch", "uhj2chn", "uhj3", "uhj3ch", "uhj3chn", "uhj4", "uhj4ch", "uhj4chn" };
	const char *uhj2Tags[] = { "uhj2", "uhj2ch", "uhj2chn" };
	const char *uhj3Tags[] = { "uhj3", "uhj3ch", "uhj3chn" };
	const char *uhj4Tags[] = { "uhj4", "uhj4ch", "uhj4chn" };
	const char *bformat2dTags[] = { "bformat2d", "b-format2d", "bformat-2d", "b-format-2d", "ambi2d", "ambisonic2d" };
	const char *bformat3dTags[] = { "bformat3d", "b-format3d", "bformat-3d", "b-format-3d", "ambi3d", "ambisonic3d" };
	const char *bformatTags[] = { "bformat", "b-format", "ambisonic" };

	if ( NameHasAnyAudioTag( lowerName, bformat2dTags, sizeof( bformat2dTags ) / sizeof( bformat2dTags[0] ) ) ) {
		return { AudioSampleEncoding::BFormat2D, channels };
	}
	if ( NameHasAnyAudioTag( lowerName, bformat3dTags, sizeof( bformat3dTags ) / sizeof( bformat3dTags[0] ) ) ) {
		return { AudioSampleEncoding::BFormat3D, channels };
	}
	if ( NameHasAnyAudioTag( lowerName, bformatTags, sizeof( bformatTags ) / sizeof( bformatTags[0] ) ) ) {
		if ( channels == 3 ) {
			return { AudioSampleEncoding::BFormat2D, channels };
		}
		if ( channels == 4 ) {
			return { AudioSampleEncoding::BFormat3D, channels };
		}
	}

	if ( NameHasAnyAudioTag( lowerName, uhj2Tags, sizeof( uhj2Tags ) / sizeof( uhj2Tags[0] ) ) ) {
		return { AudioSampleEncoding::UHJ, channels };
	}
	if ( NameHasAnyAudioTag( lowerName, uhj3Tags, sizeof( uhj3Tags ) / sizeof( uhj3Tags[0] ) ) ) {
		return { AudioSampleEncoding::UHJ, channels };
	}
	if ( NameHasAnyAudioTag( lowerName, uhj4Tags, sizeof( uhj4Tags ) / sizeof( uhj4Tags[0] ) ) ) {
		return { AudioSampleEncoding::UHJ, channels };
	}
	if ( NameHasAnyAudioTag( lowerName, uhjTags, sizeof( uhjTags ) / sizeof( uhjTags[0] ) ) ) {
		return { AudioSampleEncoding::UHJ, channels };
	}

	return { AudioSampleEncoding::PCM, channels };
}

static short ClampPCM16FromFloat( float sample ) {
	const int value = static_cast<int>( sample );
	return static_cast<short>( ClampInt( value, -32768, 32767 ) );
}

template<typename SampleType>
static void DownmixFrameToStereoFloat( const SampleType *input, int channels, float gain, float &leftOut, float &rightOut ) {
	if ( input == nullptr || channels <= 0 ) {
		leftOut = 0.0f;
		rightOut = 0.0f;
		return;
	}

	float left = static_cast<float>( input[0] );
	float right = ( channels > 1 ) ? static_cast<float>( input[1] ) : left;

	switch ( channels ) {
	case 1:
	case 2:
		break;
	case 4:
		left += static_cast<float>( input[2] ) * 0.5f;
		right += static_cast<float>( input[3] ) * 0.5f;
		break;
	case 6:
		left += static_cast<float>( input[2] ) * 0.7071f;
		right += static_cast<float>( input[2] ) * 0.7071f;
		left += static_cast<float>( input[3] ) * 0.25f;
		right += static_cast<float>( input[3] ) * 0.25f;
		left += static_cast<float>( input[4] ) * 0.5f;
		right += static_cast<float>( input[5] ) * 0.5f;
		break;
	case 7:
		left += static_cast<float>( input[2] ) * 0.7071f;
		right += static_cast<float>( input[2] ) * 0.7071f;
		left += static_cast<float>( input[3] ) * 0.25f;
		right += static_cast<float>( input[3] ) * 0.25f;
		left += static_cast<float>( input[4] ) * 0.35f;
		right += static_cast<float>( input[4] ) * 0.35f;
		left += static_cast<float>( input[5] ) * 0.5f;
		right += static_cast<float>( input[6] ) * 0.5f;
		break;
	case 8:
		left += static_cast<float>( input[2] ) * 0.7071f;
		right += static_cast<float>( input[2] ) * 0.7071f;
		left += static_cast<float>( input[3] ) * 0.25f;
		right += static_cast<float>( input[3] ) * 0.25f;
		left += static_cast<float>( input[4] ) * 0.5f;
		right += static_cast<float>( input[5] ) * 0.5f;
		left += static_cast<float>( input[6] ) * 0.5f;
		right += static_cast<float>( input[7] ) * 0.5f;
		break;
	default:
		break;
	}

	leftOut = left * gain;
	rightOut = right * gain;
}

static void DownmixPCM16FrameToStereo( const short *input, int channels, float gain, short &leftOut, short &rightOut ) {
	float left = 0.0f;
	float right = 0.0f;
	DownmixFrameToStereoFloat( input, channels, gain, left, right );
	leftOut = ClampPCM16FromFloat( left );
	rightOut = ClampPCM16FromFloat( right );
}

static std::vector<short> DownmixPCM16ToStereo( const std::vector<short> &input, int channels ) {
	std::vector<short> output;
	if ( input.empty() || channels <= 0 || input.size() % static_cast<size_t>( channels ) != 0 ) {
		return output;
	}

	const size_t frames = input.size() / static_cast<size_t>( channels );
	if ( frames > std::numeric_limits<size_t>::max() / 2u ) {
		return output;
	}
	output.resize( frames * 2u );
	for ( size_t frame = 0; frame < frames; ++frame ) {
		short left = 0;
		short right = 0;
		DownmixPCM16FrameToStereo( input.data() + frame * static_cast<size_t>( channels ), channels, 1.0f, left, right );
		output[frame * 2u] = left;
		output[frame * 2u + 1u] = right;
	}
	return output;
}

static std::vector<short> DownmixEncodedSoundFieldToStereo( const std::vector<short> &input, const AudioSampleFormat &format ) {
	std::vector<short> output;
	if ( input.empty() || format.channels <= 0 || input.size() % static_cast<size_t>( format.channels ) != 0 ) {
		return output;
	}

	const size_t frames = input.size() / static_cast<size_t>( format.channels );
	if ( frames > std::numeric_limits<size_t>::max() / 2u ) {
		return output;
	}
	output.resize( frames * 2u );

	for ( size_t frame = 0; frame < frames; ++frame ) {
		const short *src = input.data() + frame * static_cast<size_t>( format.channels );
		short left = 0;
		short right = 0;

		if ( format.encoding == AudioSampleEncoding::BFormat2D || format.encoding == AudioSampleEncoding::BFormat3D ) {
			left = src[0];
			right = src[0];
		} else if ( format.encoding == AudioSampleEncoding::UHJ && format.channels >= 2 ) {
			left = src[0];
			right = src[1];
		} else {
			DownmixPCM16FrameToStereo( src, format.channels, 1.0f, left, right );
		}

		output[frame * 2u] = left;
		output[frame * 2u + 1u] = right;
	}

	return output;
}

enum class AudioFilterKind {
	None,
	LowPass,
	HighPass,
	BandPass
};

struct AudioFilterSettings {
	AudioFilterKind kind = AudioFilterKind::None;
	float gain = 1.0f;
	float gainLF = 1.0f;
	float gainHF = 1.0f;
};

static AudioFilterSettings NoFilter() {
	return AudioFilterSettings();
}

static AudioFilterSettings LowPassFilter( float gain, float gainHF ) {
	AudioFilterSettings filter;
	filter.kind = AudioFilterKind::LowPass;
	filter.gain = ClampFloat( gain, 0.0f, 1.0f );
	filter.gainHF = ClampFloat( gainHF, 0.0f, 1.0f );
	return filter;
}

static AudioFilterSettings HighPassFilter( float gain, float gainLF ) {
	AudioFilterSettings filter;
	filter.kind = AudioFilterKind::HighPass;
	filter.gain = ClampFloat( gain, 0.0f, 1.0f );
	filter.gainLF = ClampFloat( gainLF, 0.0f, 1.0f );
	return filter;
}

static AudioFilterSettings BandPassFilter( float gain, float gainLF, float gainHF ) {
	AudioFilterSettings filter;
	filter.kind = AudioFilterKind::BandPass;
	filter.gain = ClampFloat( gain, 0.0f, 1.0f );
	filter.gainLF = ClampFloat( gainLF, 0.0f, 1.0f );
	filter.gainHF = ClampFloat( gainHF, 0.0f, 1.0f );
	return filter;
}

static bool FilterHasAudibleEffect( const AudioFilterSettings &filter ) {
	if ( filter.kind == AudioFilterKind::None ) {
		return false;
	}
	if ( filter.gain < kToneNeutralThreshold ) {
		return true;
	}
	if ( filter.kind == AudioFilterKind::HighPass || filter.kind == AudioFilterKind::BandPass ) {
		if ( filter.gainLF < kToneNeutralThreshold ) {
			return true;
		}
	}
	if ( filter.kind == AudioFilterKind::LowPass || filter.kind == AudioFilterKind::BandPass ) {
		if ( filter.gainHF < kToneNeutralThreshold ) {
			return true;
		}
	}
	return false;
}

static const char *AudioFilterKindName( AudioFilterKind kind ) {
	switch ( kind ) {
	case AudioFilterKind::LowPass:
		return "lowpass";
	case AudioFilterKind::HighPass:
		return "highpass";
	case AudioFilterKind::BandPass:
		return "bandpass";
	default:
		return "none";
	}
}

static std::string NormalizeSoundName( const char *name ) {
	std::string normalized = name != nullptr ? name : "";
	for ( char &ch : normalized ) {
		if ( ch == '\\' ) {
			ch = '/';
		} else if ( ch >= 'A' && ch <= 'Z' ) {
			ch = static_cast<char>( ch - 'A' + 'a' );
		}
	}
	return normalized;
}

static std::string SafeString( const char *value ) {
	return value != nullptr ? std::string( value ) : std::string();
}

static const char *CvarStringOrDefault( const cvar_t *cvar, const char *fallback ) {
	if ( cvar != nullptr && cvar->string != nullptr && cvar->string[0] != '\0' ) {
		return cvar->string;
	}
	return fallback;
}

static int CvarIntegerOrDefault( const cvar_t *cvar, int fallback ) {
	return cvar != nullptr ? cvar->integer : fallback;
}

static bool ExtensionListContains( const char *extensionList, const char *extensionName ) {
	if ( extensionList == nullptr || extensionName == nullptr || extensionName[0] == '\0' ) {
		return false;
	}

	const size_t nameLength = std::strlen( extensionName );
	const char *cursor = extensionList;
	while ( cursor[0] != '\0' ) {
		while ( cursor[0] == ' ' || cursor[0] == '\t' || cursor[0] == '\r' || cursor[0] == '\n' ) {
			++cursor;
		}

		const char *end = cursor;
		while ( end[0] != '\0' && end[0] != ' ' && end[0] != '\t' && end[0] != '\r' && end[0] != '\n' ) {
			++end;
		}

		if ( static_cast<size_t>( end - cursor ) == nameLength && std::strncmp( cursor, extensionName, nameLength ) == 0 ) {
			return true;
		}

		cursor = end;
	}

	return false;
}

static const char *YesNo( bool value ) {
	return value ? "yes" : "no";
}

static const char *AvailableUnavailable( bool value ) {
	return value ? "available" : "unavailable";
}

template<typename T>
static const char *LoadedMissing( T proc ) {
	return proc != nullptr ? "loaded" : "missing";
}

static const char *ALCBooleanName( ALCint value ) {
	switch ( value ) {
	case ALC_FALSE:
		return "off";
	case ALC_TRUE:
		return "on";
	default:
		return "unknown";
	}
}

static const char *ALCErrorName( ALCenum error ) {
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

static bool StringIsBlank( const char *text ) {
	if ( text == nullptr ) {
		return true;
	}
	while ( text[0] != '\0' ) {
		if ( text[0] != ' ' && text[0] != '\t' && text[0] != '\r' && text[0] != '\n' ) {
			return false;
		}
		++text;
	}
	return true;
}

static bool ParseNonNegativeInt( const char *text, int &value ) {
	if ( StringIsBlank( text ) ) {
		return false;
	}

	char *end = nullptr;
	const long parsed = std::strtol( text, &end, 10 );
	if ( end == text || parsed < 0 || parsed > 0x7fffffff ) {
		return false;
	}
	while ( end != nullptr && end[0] != '\0' ) {
		if ( end[0] != ' ' && end[0] != '\t' && end[0] != '\r' && end[0] != '\n' ) {
			return false;
		}
		++end;
	}

	value = static_cast<int>( parsed );
	return true;
}

static ALCint HrtfRequestFromCvar( const cvar_t *cvar ) {
	const char *request = CvarStringOrDefault( cvar, "auto" );
	if ( !Q_stricmp( request, "auto" ) ) {
		return ALC_DONT_CARE_SOFT;
	}
	if ( !Q_stricmp( request, "on" ) || !Q_stricmp( request, "1" ) || !Q_stricmp( request, "true" ) ) {
		return ALC_TRUE;
	}
	if ( !Q_stricmp( request, "off" ) || !Q_stricmp( request, "0" ) || !Q_stricmp( request, "false" ) ) {
		return ALC_FALSE;
	}

	Com_Printf( S_COLOR_YELLOW "WARNING: unknown s_alHrtf '%s', using auto\n", request );
	return ALC_DONT_CARE_SOFT;
}

static bool OutputModeFromCvar( const cvar_t *cvar, ALCint &mode ) {
	const char *request = CvarStringOrDefault( cvar, "auto" );
	if ( !Q_stricmp( request, "auto" ) ) {
		mode = ALC_ANY_SOFT;
		return false;
	}
	if ( !Q_stricmp( request, "headphones" ) || !Q_stricmp( request, "headphone" ) || !Q_stricmp( request, "hrtf" ) ) {
		mode = ALC_STEREO_HRTF_SOFT;
		return true;
	}
	if ( !Q_stricmp( request, "speakers" ) || !Q_stricmp( request, "speaker" ) || !Q_stricmp( request, "stereo" ) ) {
		mode = ALC_STEREO_BASIC_SOFT;
		return true;
	}
	if ( !Q_stricmp( request, "quad" ) || !Q_stricmp( request, "4.0" ) || !Q_stricmp( request, "surround4.0" ) ) {
		mode = ALC_QUAD_SOFT;
		return true;
	}
	if ( !Q_stricmp( request, "surround" ) || !Q_stricmp( request, "5.1" ) || !Q_stricmp( request, "surround5.1" ) ) {
		mode = ALC_SURROUND_5_1_SOFT;
		return true;
	}
	if ( !Q_stricmp( request, "6.1" ) || !Q_stricmp( request, "surround6.1" ) ) {
		mode = ALC_SURROUND_6_1_SOFT;
		return true;
	}
	if ( !Q_stricmp( request, "7.1" ) || !Q_stricmp( request, "surround7.1" ) ) {
		mode = ALC_SURROUND_7_1_SOFT;
		return true;
	}

	Com_Printf( S_COLOR_YELLOW "WARNING: unknown s_alOutputMode '%s', using auto\n", request );
	mode = ALC_ANY_SOFT;
	return false;
}

static const char *ALDistanceModelName( ALint model ) {
	switch ( model ) {
	case AL_NONE:
		return "none";
	case AL_INVERSE_DISTANCE:
		return "inverse";
	case AL_INVERSE_DISTANCE_CLAMPED:
		return "inverse_clamped";
	case AL_LINEAR_DISTANCE:
		return "linear";
	case AL_LINEAR_DISTANCE_CLAMPED:
		return "linear_clamped";
	case AL_EXPONENT_DISTANCE:
		return "exponent";
	case AL_EXPONENT_DISTANCE_CLAMPED:
		return "exponent_clamped";
	default:
		return "unknown";
	}
}

static ALint DistanceModelFromCvar( const cvar_t *cvar ) {
	const char *request = CvarStringOrDefault( cvar, "inverse_clamped" );
	if ( !Q_stricmp( request, "none" ) || !Q_stricmp( request, "off" ) || !Q_stricmp( request, "0" ) ) {
		return AL_NONE;
	}
	if ( !Q_stricmp( request, "inverse" ) ) {
		return AL_INVERSE_DISTANCE;
	}
	if ( !Q_stricmp( request, "inverse_clamped" ) || !Q_stricmp( request, "inverse-clamped" ) ||
		!Q_stricmp( request, "inverseclamped" ) || !Q_stricmp( request, "inverse_clamp" ) ) {
		return AL_INVERSE_DISTANCE_CLAMPED;
	}
	if ( !Q_stricmp( request, "linear" ) ) {
		return AL_LINEAR_DISTANCE;
	}
	if ( !Q_stricmp( request, "linear_clamped" ) || !Q_stricmp( request, "linear-clamped" ) ||
		!Q_stricmp( request, "linearclamped" ) || !Q_stricmp( request, "linear_clamp" ) ) {
		return AL_LINEAR_DISTANCE_CLAMPED;
	}
	if ( !Q_stricmp( request, "exponent" ) || !Q_stricmp( request, "exponential" ) ) {
		return AL_EXPONENT_DISTANCE;
	}
	if ( !Q_stricmp( request, "exponent_clamped" ) || !Q_stricmp( request, "exponent-clamped" ) ||
		!Q_stricmp( request, "exponentclamped" ) || !Q_stricmp( request, "exponent_clamp" ) ||
		!Q_stricmp( request, "exponential_clamped" ) || !Q_stricmp( request, "exponential-clamped" ) ) {
		return AL_EXPONENT_DISTANCE_CLAMPED;
	}

	Com_Printf( S_COLOR_YELLOW "WARNING: unknown s_alDistanceModel '%s', using inverse_clamped\n", request );
	return AL_INVERSE_DISTANCE_CLAMPED;
}

static bool DistanceModelUsesOpenALAttenuation( ALint model ) {
	return model != AL_NONE;
}

static const char *ALCOutputModeName( ALCint mode ) {
	switch ( mode ) {
	case ALC_ANY_SOFT:
		return "any";
	case ALC_MONO_SOFT:
		return "mono";
	case ALC_STEREO_SOFT:
		return "stereo";
	case ALC_STEREO_BASIC_SOFT:
		return "stereo-basic";
	case ALC_STEREO_UHJ_SOFT:
		return "stereo-uhj";
	case ALC_STEREO_HRTF_SOFT:
		return "stereo-hrtf";
	case ALC_QUAD_SOFT:
		return "quad";
	case ALC_SURROUND_5_1_SOFT:
		return "surround-5.1";
	case ALC_SURROUND_6_1_SOFT:
		return "surround-6.1";
	case ALC_SURROUND_7_1_SOFT:
		return "surround-7.1";
	default:
		return "unknown";
	}
}

static const char *HrtfStatusName( ALCint status ) {
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

static const char *HrtfRequestName( ALCint request ) {
	switch ( request ) {
	case ALC_FALSE:
		return "off";
	case ALC_TRUE:
		return "on";
	case ALC_DONT_CARE_SOFT:
		return "auto";
	default:
		return "unknown";
	}
}

static const char *ALCConnectedName( ALCint connected ) {
	switch ( connected ) {
	case ALC_TRUE:
		return "connected";
	case ALC_FALSE:
		return "disconnected";
	default:
		return "unknown";
	}
}

static const char *ALCEventSupportName( ALCenum support ) {
	switch ( support ) {
	case ALC_EVENT_SUPPORTED_SOFT:
		return "supported";
	case ALC_EVENT_NOT_SUPPORTED_SOFT:
		return "not-supported";
	default:
		return "not-queried";
	}
}

static double NanosecondsToMilliseconds( ALCint64SOFT nanoseconds ) {
	return static_cast<double>( nanoseconds ) / 1000000.0;
}

static double NanosecondsToSeconds( ALCint64SOFT nanoseconds ) {
	return static_cast<double>( nanoseconds ) / 1000000000.0;
}

struct ModernOpenALCapabilities {
	int alcMajor = 0;
	int alcMinor = 0;
	int mixerFrequency = 0;
	int refreshRate = 0;
	int monoSources = 0;
	int stereoSources = 0;
	bool enumerateAll = false;
	bool hrtf = false;
	bool deviceClock = false;
	bool disconnect = false;
	bool reopenDevice = false;
	bool systemEvents = false;
	bool outputLimiter = false;
	bool outputMode = false;
	bool loopback = false;
	bool sourceEvents = false;
	bool deferredUpdates = false;
	bool directChannels = false;
	bool directChannelsRemix = false;
	bool multiChannelFormats = false;
	bool bFormat = false;
	bool uhj = false;
	bool uhjEx = false;
	bool sourceSpatialize = false;
	bool sourceLatency = false;
	ALCint hrtfStatus = -1;
	ALCint hrtfCount = 0;
	ALCint outputLimiterState = -1;
	ALCint outputModeValue = -1;
	bool connectedQuery = false;
	ALCint connectedState = -1;
	ALCenum defaultPlaybackEventSupport = 0;
	ALCenum playbackAddedEventSupport = 0;
	ALCenum playbackRemovedEventSupport = 0;
	ALint distanceModel = AL_NONE;
	bool distanceModelValid = false;
	ALint requestedDistanceModel = AL_INVERSE_DISTANCE_CLAMPED;
	bool requestedHrtfAttribute = false;
	ALCint requestedHrtf = ALC_DONT_CARE_SOFT;
	bool requestedHrtfIdAttribute = false;
	ALCint requestedHrtfId = -1;
	bool requestedOutputModeAttribute = false;
	ALCint requestedOutputMode = ALC_ANY_SOFT;
	bool requestedOutputLimiterAttribute = false;
	ALCint requestedOutputLimiter = ALC_TRUE;
	ALCint64SOFT deviceClockValue = 0;
	ALCint64SOFT deviceLatency = 0;
	bool deviceClockValuesValid = false;
	LPALCGETSTRINGISOFT alcGetStringiSOFT = nullptr;
	LPALCRESETDEVICESOFT alcResetDeviceSOFT = nullptr;
	LPALCREOPENDEVICESOFT alcReopenDeviceSOFT = nullptr;
	LPALCEVENTISSUPPORTEDSOFT alcEventIsSupportedSOFT = nullptr;
	LPALCEVENTCONTROLSOFT alcEventControlSOFT = nullptr;
	LPALCEVENTCALLBACKSOFT alcEventCallbackSOFT = nullptr;
	LPALCGETINTEGER64VSOFT alcGetInteger64vSOFT = nullptr;
	LPALCLOOPBACKOPENDEVICESOFT alcLoopbackOpenDeviceSOFT = nullptr;
	LPALCISRENDERFORMATSUPPORTEDSOFT alcIsRenderFormatSupportedSOFT = nullptr;
	LPALCRENDERSAMPLESSOFT alcRenderSamplesSOFT = nullptr;
	LPALDEFERUPDATESSOFT alDeferUpdatesSOFT = nullptr;
	LPALPROCESSUPDATESSOFT alProcessUpdatesSOFT = nullptr;
	LPALEVENTCONTROLSOFT alEventControlSOFT = nullptr;
	LPALEVENTCALLBACKSOFT alEventCallbackSOFT = nullptr;
	LPALGETSOURCEDVSOFT alGetSourcedvSOFT = nullptr;
	std::vector<std::string> hrtfSpecifiers;
};

struct ReverbPreset {
	const char *name;
	float density;
	float diffusion;
	float gain;
	float gainHF;
	float decayTime;
	float decayHFRatio;
	float reflectionsGain;
	float reflectionsDelay;
	float lateReverbGain;
	float lateReverbDelay;
	float airAbsorptionGainHF;
	float roomRolloffFactor;
	ALint decayHFLimit;
	float baseWet;
	float directHF;
	float wetHF;
	// Extended parameters used when the runtime accepts AL_EFFECT_EAXREVERB.
	float gainLF;
	float decayLFRatio;
	float echoTime;
	float echoDepth;
	float modulationTime;
	float modulationDepth;
	float hfReference;
	float lfReference;
};

static constexpr ReverbPreset kReverbPresets[] = {
	{ "small-room", 0.70f, 0.60f, 0.28f, 0.89f, 0.90f, 0.78f, 0.20f, 0.002f, 1.10f, 0.011f, 0.994f, 0.0f, AL_TRUE, 0.30f, 0.95f, 1.00f,
		1.00f, 1.00f, 0.25f, 0.00f, 0.25f, 0.00f, 5000.0f, 250.0f },
	{ "room", 0.80f, 0.70f, 0.32f, 0.70f, 1.40f, 0.83f, 0.25f, 0.003f, 1.20f, 0.016f, 0.994f, 0.0f, AL_TRUE, 0.36f, 1.00f, 1.00f,
		1.00f, 1.00f, 0.25f, 0.00f, 0.25f, 0.00f, 5000.0f, 250.0f },
	{ "stone-room", 1.00f, 0.85f, 0.36f, 0.50f, 2.20f, 0.72f, 0.45f, 0.012f, 1.45f, 0.028f, 0.993f, 0.0f, AL_TRUE, 0.46f, 0.95f, 0.95f,
		1.00f, 1.05f, 0.25f, 0.05f, 0.25f, 0.00f, 5000.0f, 250.0f },
	{ "hallway", 1.00f, 0.78f, 0.33f, 0.59f, 1.80f, 0.69f, 0.42f, 0.008f, 1.35f, 0.019f, 0.994f, 0.0f, AL_TRUE, 0.40f, 0.95f, 0.95f,
		1.00f, 0.95f, 0.25f, 0.10f, 0.25f, 0.00f, 5000.0f, 250.0f },
	{ "hall", 1.00f, 0.92f, 0.35f, 0.66f, 3.10f, 0.78f, 0.50f, 0.020f, 1.55f, 0.036f, 0.993f, 0.0f, AL_TRUE, 0.55f, 1.00f, 1.00f,
		1.00f, 0.90f, 0.25f, 0.08f, 0.25f, 0.00f, 5000.0f, 250.0f },
	{ "outdoors", 0.25f, 0.30f, 0.18f, 0.99f, 1.60f, 0.85f, 0.05f, 0.007f, 0.28f, 0.011f, 0.999f, 0.0f, AL_TRUE, 0.16f, 1.00f, 1.00f,
		0.90f, 0.75f, 0.25f, 0.30f, 0.25f, 0.00f, 5000.0f, 250.0f },
	{ "underwater", 1.00f, 1.00f, 0.20f, 0.01f, 1.50f, 0.10f, 0.59f, 0.007f, 1.18f, 0.011f, 0.994f, 0.0f, AL_TRUE, 0.75f, 0.25f, 0.30f,
		1.00f, 1.10f, 0.25f, 0.00f, 1.18f, 0.35f, 5000.0f, 250.0f }
};

struct EnvironmentState {
	int presetIndex = 0;
	int targetPresetIndex = 0;
	const char *name = "small-room";
	const char *targetName = "small-room";
	float blend = 1.0f;
	float baseWet = 0.18f;
	float directLF = 1.0f;
	float directHF = 0.95f;
	float wetLF = 1.0f;
	float wetHF = 1.0f;
	float occlusionMultiplier = 1.0f;
	int transitionMs = kEnvironmentTransitionMs;
	qboolean outdoors = qfalse;
	qboolean underwater = qfalse;
	qboolean audioZone = qfalse;
	uint8_t zoneMaterialClass = static_cast<uint8_t>( azfmt::MaterialClass::Unknown );
	uint8_t zoneFlags = 0;
	float zonePortalBlend = 0.0f;
	std::string zoneName;
	std::string zonePortalTargetName;
};

static float LerpFloat( float from, float to, float blend ) {
	return from + ( to - from ) * ClampFloat( blend, 0.0f, 1.0f );
}

static float SmoothStep( float value ) {
	value = ClampFloat( value, 0.0f, 1.0f );
	return value * value * ( 3.0f - 2.0f * value );
}

static void SetEnvironmentPreset( EnvironmentState &state, int presetIndex ) {
	presetIndex = ClampInt( presetIndex, 0, static_cast<int>( sizeof( kReverbPresets ) / sizeof( kReverbPresets[0] ) ) - 1 );
	const ReverbPreset &preset = kReverbPresets[presetIndex];
	const float reverbGain = ( s_alReverbGain != nullptr ) ? s_alReverbGain->value : 1.0f;

	state.presetIndex = presetIndex;
	state.targetPresetIndex = presetIndex;
	state.name = preset.name;
	state.targetName = preset.name;
	state.blend = 1.0f;
	state.baseWet = ClampFloat( preset.baseWet * reverbGain, 0.0f, 1.0f );
	state.directLF = 1.0f;
	state.directHF = preset.directHF;
	state.wetLF = 1.0f;
	state.wetHF = preset.wetHF;
	state.occlusionMultiplier = 1.0f;
	state.transitionMs = kEnvironmentTransitionMs;
	state.outdoors = qfalse;
	state.underwater = qfalse;
	state.audioZone = qfalse;
	state.zoneMaterialClass = static_cast<uint8_t>( azfmt::MaterialClass::Unknown );
	state.zoneFlags = 0;
	state.zonePortalBlend = 0.0f;
	state.zoneName.clear();
	state.zonePortalTargetName.clear();
}

static bool EnvironmentStateDiffers( const EnvironmentState &a, const EnvironmentState &b ) {
	return a.presetIndex != b.presetIndex ||
		a.outdoors != b.outdoors ||
		a.underwater != b.underwater ||
		a.audioZone != b.audioZone ||
		a.zoneMaterialClass != b.zoneMaterialClass ||
		a.zoneFlags != b.zoneFlags ||
		a.zoneName != b.zoneName ||
		a.zonePortalTargetName != b.zonePortalTargetName ||
		std::fabs( a.zonePortalBlend - b.zonePortalBlend ) > 0.01f ||
		std::fabs( a.baseWet - b.baseWet ) > 0.01f ||
		std::fabs( a.directLF - b.directLF ) > 0.01f ||
		std::fabs( a.directHF - b.directHF ) > 0.01f ||
		std::fabs( a.wetLF - b.wetLF ) > 0.01f ||
		std::fabs( a.wetHF - b.wetHF ) > 0.01f ||
		std::fabs( a.occlusionMultiplier - b.occlusionMultiplier ) > 0.01f ||
		a.transitionMs != b.transitionMs;
}

static EnvironmentState BlendEnvironmentStates( const EnvironmentState &from, const EnvironmentState &to, float blend ) {
	blend = ClampFloat( blend, 0.0f, 1.0f );
	if ( blend >= 1.0f ) {
		return to;
	}

	EnvironmentState state = from;
	state.targetPresetIndex = to.presetIndex;
	state.targetName = to.name;
	state.blend = blend;
	state.baseWet = LerpFloat( from.baseWet, to.baseWet, blend );
	state.directLF = LerpFloat( from.directLF, to.directLF, blend );
	state.directHF = LerpFloat( from.directHF, to.directHF, blend );
	state.wetLF = LerpFloat( from.wetLF, to.wetLF, blend );
	state.wetHF = LerpFloat( from.wetHF, to.wetHF, blend );
	state.occlusionMultiplier = LerpFloat( from.occlusionMultiplier, to.occlusionMultiplier, blend );
	state.transitionMs = to.transitionMs;
	state.outdoors = ( blend >= 0.5f ) ? to.outdoors : from.outdoors;
	state.underwater = ( blend >= 0.5f ) ? to.underwater : from.underwater;
	state.audioZone = ( blend >= 0.5f ) ? to.audioZone : from.audioZone;
	state.zoneMaterialClass = ( blend >= 0.5f ) ? to.zoneMaterialClass : from.zoneMaterialClass;
	state.zoneFlags = ( blend >= 0.5f ) ? to.zoneFlags : from.zoneFlags;
	state.zonePortalBlend = LerpFloat( from.zonePortalBlend, to.zonePortalBlend, blend );
	state.zoneName = ( blend >= 0.5f ) ? to.zoneName : from.zoneName;
	state.zonePortalTargetName = ( blend >= 0.5f ) ? to.zonePortalTargetName : from.zonePortalTargetName;
	return state;
}

static const char *EnvironmentNameOrDefault( const char *name ) {
	return ( name != nullptr && name[0] != '\0' ) ? name : "unknown";
}

static bool EnvironmentIsTransitioning( const EnvironmentState &state ) {
	return state.blend < 0.995f;
}

static void FormatEnvironmentSummary( const EnvironmentState &state, char *buffer, int bufferSize ) {
	if ( buffer == nullptr || bufferSize <= 0 ) {
		return;
	}

	if ( EnvironmentIsTransitioning( state ) ) {
		if ( state.presetIndex != state.targetPresetIndex ) {
			Com_sprintf( buffer, bufferSize, "%s->%s %.2f",
				EnvironmentNameOrDefault( state.name ),
				EnvironmentNameOrDefault( state.targetName ),
				ClampFloat( state.blend, 0.0f, 1.0f ) );
		} else {
			Com_sprintf( buffer, bufferSize, "%s %.2f",
				EnvironmentNameOrDefault( state.targetName ),
				ClampFloat( state.blend, 0.0f, 1.0f ) );
		}
		return;
	}

	Com_sprintf( buffer, bufferSize, "%s", EnvironmentNameOrDefault( state.targetName ) );
}

static const char *AudioZoneMaterialClassName( uint8_t materialClass ) {
	if ( materialClass < static_cast<uint8_t>( azfmt::MaterialClass::Count ) ) {
		return azfmt::kMaterialClassNames[materialClass];
	}
	return "unknown";
}

static void FormatAudioZoneSummary( const EnvironmentState &state, char *buffer, int bufferSize ) {
	if ( buffer == nullptr || bufferSize <= 0 ) {
		return;
	}
	if ( !state.audioZone || state.zoneName.empty() ) {
		Com_sprintf( buffer, bufferSize, "%s", "generic" );
		return;
	}
	if ( state.zonePortalBlend > azrt::kAudioZonePortalMinimumBlend && !state.zonePortalTargetName.empty() ) {
		Com_sprintf( buffer, bufferSize, "%s->%s %.2f",
			state.zoneName.c_str(),
			state.zonePortalTargetName.c_str(),
			ClampFloat( state.zonePortalBlend, 0.0f, azrt::kAudioZonePortalMaxBlend ) );
		return;
	}
	Com_sprintf( buffer, bufferSize, "%s", state.zoneName.c_str() );
}

static EnvironmentState EnvironmentStateForAudioZone( const azrt::AudioZone &zone );

class AudioZoneSet {
public:
	bool Load( const char *qpath );
	void Clear();
	const azrt::AudioZone *FindZone( const float *origin ) const;
	bool EvaluateEnvironment( const float *origin, EnvironmentState &environment ) const;
	void PrintStatus( const EnvironmentState &environment ) const;

	bool Loaded() const { return loaded_; }
	const std::string &Qpath() const { return qpath_; }
	size_t ZoneCount() const { return zones_.size(); }

private:
	std::vector<azrt::AudioZone> zones_;
	std::string qpath_;
	bool loaded_ = false;
};

void AudioZoneSet::Clear() {
	zones_.clear();
	qpath_.clear();
	loaded_ = false;
}

bool AudioZoneSet::Load( const char *qpath ) {
	zones_.clear();
	qpath_ = ( qpath != nullptr ) ? qpath : "";
	loaded_ = false;

	if ( qpath_.empty() ) {
		return false;
	}

	fnql::ScopedReadFile fileData = fnql::ScopedReadFile::Read( qpath_.c_str() );
	if ( fileData.length() <= 0 || fileData.get() == nullptr ) {
		Com_DPrintf( "No audio zone sidecar found for %s\n", qpath_.c_str() );
		return false;
	}

	std::string error;
	std::vector<azrt::AudioZone> parsedZones;
	const bool parsed = azrt::ParseAudioZoneBinary(
		reinterpret_cast<const uint8_t *>( fileData.get() ),
		static_cast<size_t>( fileData.length() ),
		parsedZones,
		error );
	if ( !parsed ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: ignoring audio zone sidecar %s: %s\n", qpath_.c_str(), error.c_str() );
		return false;
	}

	zones_ = std::move( parsedZones );
	loaded_ = true;
	Com_Printf( "Loaded %lu audio zones from %s\n", static_cast<unsigned long>( zones_.size() ), qpath_.c_str() );
	return true;
}

const azrt::AudioZone *AudioZoneSet::FindZone( const float *origin ) const {
	return azrt::FindAudioZone( zones_, origin );
}

bool AudioZoneSet::EvaluateEnvironment( const float *origin, EnvironmentState &environment ) const {
	const azrt::AudioZone *zone = FindZone( origin );
	if ( zone == nullptr ) {
		return false;
	}

	environment = EnvironmentStateForAudioZone( *zone );
	if ( zone->portals.empty() ) {
		return true;
	}

	const azrt::AudioZonePortalBlend portalBlend = azrt::FindAudioZonePortalBlend( zones_, *zone, origin );
	if ( portalBlend.target != nullptr ) {
		const float blend = ClampFloat( portalBlend.blend, 0.0f, azrt::kAudioZonePortalMaxBlend );
		EnvironmentState targetEnvironment = EnvironmentStateForAudioZone( *portalBlend.target );
		EnvironmentState blendedEnvironment = BlendEnvironmentStates( environment, targetEnvironment, blend );
		blendedEnvironment.audioZone = qtrue;
		blendedEnvironment.zoneName = zone->name;
		blendedEnvironment.zoneMaterialClass = zone->materialClass;
		blendedEnvironment.zoneFlags = zone->flags;
		blendedEnvironment.zonePortalBlend = blend;
		blendedEnvironment.zonePortalTargetName = portalBlend.target->name;
		environment = blendedEnvironment;
	}
	return true;
}

void AudioZoneSet::PrintStatus( const EnvironmentState &environment ) const {
	if ( qpath_.empty() ) {
		Com_Printf( "Audio zones: no active map\n" );
		return;
	}
	if ( !loaded_ ) {
		Com_Printf( "Audio zones: none loaded (looked for %s)\n", qpath_.c_str() );
		return;
	}
	std::array<char, 128> zoneSummary;
	FormatAudioZoneSummary( environment, zoneSummary.data(), zoneSummary.size() );
	Com_Printf( "Audio zones: %lu loaded from %s, active %s, material %s, flags 0x%02x\n",
		static_cast<unsigned long>( zones_.size() ),
		qpath_.c_str(),
		zoneSummary.data(),
		environment.audioZone ? AudioZoneMaterialClassName( environment.zoneMaterialClass ) : "none",
		environment.audioZone ? environment.zoneFlags : 0 );
}

static std::string AudioZoneQpathForCurrentMap() {
	if ( cl.mapname[0] == '\0' ) {
		return std::string();
	}

	std::array<char, MAX_QPATH> stripped;
	std::array<char, MAX_QPATH> zonePath;
	COM_StripExtension( cl.mapname, stripped.data(), static_cast<int>( stripped.size() ) );
	if ( stripped[0] == '\0' ) {
		return std::string();
	}
	Com_sprintf( zonePath.data(), static_cast<int>( zonePath.size() ), "%s.azb", stripped.data() );
	return zonePath.data();
}

static EnvironmentState EnvironmentStateForAudioZone( const azrt::AudioZone &zone ) {
	EnvironmentState state;
	SetEnvironmentPreset( state, zone.presetIndex );
	state.baseWet = ClampFloat( state.baseWet * zone.reverbGain, 0.0f, 1.0f );
	state.directLF = ClampFloat( zone.directLF, 0.0f, 1.0f );
	state.directHF = ClampFloat( state.directHF * zone.directHF, 0.0f, 1.0f );
	state.wetLF = ClampFloat( zone.wetLF, 0.0f, 1.0f );
	state.wetHF = ClampFloat( state.wetHF * zone.wetHF, 0.0f, 1.0f );
	state.occlusionMultiplier = ClampFloat( zone.occlusionMultiplier, 0.0f, 4.0f );
	state.transitionMs = ClampInt( zone.transitionMs, 0, 10000 );
	state.outdoors = ( zone.presetIndex == static_cast<int>( azfmt::Preset::Outdoors ) ||
		( zone.flags & azfmt::ZoneFlagOutdoor ) != 0 ) ? qtrue : qfalse;
	state.underwater = ( zone.presetIndex == static_cast<int>( azfmt::Preset::Underwater ) ||
		( zone.flags & azfmt::ZoneFlagUnderwater ) != 0 ) ? qtrue : qfalse;
	state.audioZone = qtrue;
	state.zoneMaterialClass = zone.materialClass;
	state.zoneFlags = zone.flags;
	state.zonePortalBlend = 0.0f;
	state.zoneName = zone.name;
	state.zonePortalTargetName.clear();
	return state;
}

struct ProbeResult {
	float distance = 0.0f;
	qboolean blocked = qfalse;
	qboolean hitSky = qfalse;
};

static float DistanceBetweenPoints( const float *a, const float *b ) {
	if ( a == nullptr || b == nullptr ) {
		return 0.0f;
	}

	vec3_t delta;
	VectorSubtract( a, b, delta );
	return VectorLength( delta );
}

static qboolean CollisionWorldReady( void ) {
	return ( CM_NumInlineModels() > 0 ) ? qtrue : qfalse;
}

static ProbeResult ProbeEnvironment( const float *origin, const vec3_t dir, float maxDistance ) {
	ProbeResult result;
	result.distance = maxDistance;

	if ( origin == nullptr || !CollisionWorldReady() ) {
		return result;
	}

	trace_t trace;
	vec3_t end;
	vec3_t zero = { 0.0f, 0.0f, 0.0f };
	VectorMA( origin, maxDistance, dir, end );
	CM_BoxTrace( &trace, origin, end, zero, zero, 0, kOcclusionMask, qfalse );
	if ( trace.allsolid ) {
		result.distance = 0.0f;
		result.blocked = qtrue;
		return result;
	}

	result.distance = ClampFloat( trace.fraction, 0.0f, 1.0f ) * maxDistance;
	if ( trace.startsolid || trace.fraction < 1.0f ) {
		result.blocked = qtrue;
		result.hitSky = ( trace.surfaceFlags & SURF_SKY ) ? qtrue : qfalse;
	}

	return result;
}

static EnvironmentState EvaluateListenerEnvironment( const float *listenerOrigin ) {
	EnvironmentState state;
	if ( listenerOrigin == nullptr || !CollisionWorldReady() ) {
		SetEnvironmentPreset( state, 0 );
		return state;
	}

	const int contents = CM_PointContents( listenerOrigin, 0 );
	if ( contents & kLiquidContents ) {
		SetEnvironmentPreset( state, 6 );
		state.underwater = qtrue;
		return state;
	}

	const vec3_t probes[6] = {
		{ 1.0f, 0.0f, 0.0f },
		{ -1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f },
		{ 0.0f, -1.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, -1.0f }
	};

	float distances[6];
	for ( int i = 0; i < 6; ++i ) {
		distances[i] = ProbeEnvironment( listenerOrigin, probes[i], kEnvironmentProbeDistance ).distance;
	}

	const vec3_t skyProbes[5] = {
		{ 0.0f, 0.0f, 1.0f },
		{ kDiagonalProbeComponent, 0.0f, kDiagonalProbeComponent },
		{ -kDiagonalProbeComponent, 0.0f, kDiagonalProbeComponent },
		{ 0.0f, kDiagonalProbeComponent, kDiagonalProbeComponent },
		{ 0.0f, -kDiagonalProbeComponent, kDiagonalProbeComponent }
	};

	int skyHits = 0;
	int skyOpenings = 0;
	for ( const vec3_t &probe : skyProbes ) {
		const ProbeResult result = ProbeEnvironment( listenerOrigin, probe, kEnvironmentProbeDistance );
		if ( result.hitSky ) {
			++skyHits;
		}
		if ( result.hitSky || !result.blocked || result.distance > 512.0f ) {
			++skyOpenings;
		}
	}

	const float horizontalAverage = ( distances[0] + distances[1] + distances[2] + distances[3] ) * 0.25f;
	const float horizontalMin = ( std::min )( ( std::min )( distances[0], distances[1] ), ( std::min )( distances[2], distances[3] ) );
	const float horizontalMax = ( std::max )( ( std::max )( distances[0], distances[1] ), ( std::max )( distances[2], distances[3] ) );
	const float ceilingDistance = distances[4];
	const bool skyDominated = ( skyHits >= 3 ) || ( skyHits >= 2 && skyOpenings >= 4 );
	const bool outdoors = skyDominated || ( skyOpenings >= 4 && horizontalAverage > 192.0f && ceilingDistance > 256.0f );
	const bool hallway = ( horizontalMin > 1.0f && ( horizontalMax / horizontalMin ) > 1.85f && horizontalAverage < 320.0f );

	int presetIndex = 0;
	if ( outdoors ) {
		presetIndex = 5;
	} else if ( hallway ) {
		presetIndex = 3;
	} else if ( horizontalAverage < 110.0f ) {
		presetIndex = 0;
	} else if ( horizontalAverage < 220.0f ) {
		presetIndex = 1;
	} else if ( horizontalAverage < 360.0f ) {
		presetIndex = 2;
	} else {
		presetIndex = 4;
	}

	SetEnvironmentPreset( state, presetIndex );
	state.outdoors = outdoors ? qtrue : qfalse;
	return state;
}

static qboolean TraceBlocked( const float *listenerOrigin, const float *sourceOrigin ) {
	if ( !CollisionWorldReady() ) {
		return qfalse;
	}

	trace_t trace;
	vec3_t zero = { 0.0f, 0.0f, 0.0f };
	CM_BoxTrace( &trace, listenerOrigin, sourceOrigin, zero, zero, 0, kOcclusionMask, qfalse );
	return ( trace.allsolid || trace.startsolid || trace.fraction < 0.999f ) ? qtrue : qfalse;
}

static bool OcclusionEnabled() {
	return s_alOcclusion == nullptr || s_alOcclusion->integer != 0;
}

static float MoveFloatTowards( float current, float target, float maxDelta ) {
	if ( current < target ) {
		return ( std::min )( current + maxDelta, target );
	}
	if ( current > target ) {
		return ( std::max )( current - maxDelta, target );
	}
	return current;
}

static float ApplyOcclusionHysteresis( float previousTarget, float measured ) {
	if ( measured > previousTarget + kOcclusionTargetRiseHysteresis ) {
		return measured;
	}
	if ( measured < previousTarget - kOcclusionTargetFallHysteresis ) {
		return measured;
	}
	return previousTarget;
}

static float ComputeOcclusionFactor( const float *listenerOrigin, const float *sourceOrigin ) {
	if ( listenerOrigin == nullptr || sourceOrigin == nullptr || !CollisionWorldReady() ||
		!OcclusionEnabled() ) {
		return 0.0f;
	}

	vec3_t toSource;
	VectorSubtract( sourceOrigin, listenerOrigin, toSource );
	const float distance = VectorNormalize( toSource );
	if ( !occ::DistanceCanOcclude( distance ) ) {
		return 0.0f;
	}

	vec3_t worldUp = { 0.0f, 0.0f, 1.0f };
	vec3_t right;
	vec3_t vertical;
	CrossProduct( toSource, worldUp, right );
	if ( VectorNormalize( right ) == 0.0f ) {
		right[0] = 1.0f;
		right[1] = 0.0f;
		right[2] = 0.0f;
	}
	CrossProduct( right, toSource, vertical );
	if ( VectorNormalize( vertical ) == 0.0f ) {
		vertical[0] = 0.0f;
		vertical[1] = 0.0f;
		vertical[2] = 1.0f;
	}

	const bool centerBlocked = TraceBlocked( listenerOrigin, sourceOrigin ) != qfalse;
	int blocked = centerBlocked ? 1 : 0;
	int total = 1;

	if ( occ::UsesProbeFan( distance ) ) {
		const float spread = occ::ProbeSpreadForDistance( distance );
		vec3_t shifted;

		VectorMA( sourceOrigin, spread, right, shifted );
		blocked += TraceBlocked( listenerOrigin, shifted ) ? 1 : 0;
		VectorMA( sourceOrigin, -spread, right, shifted );
		blocked += TraceBlocked( listenerOrigin, shifted ) ? 1 : 0;
		VectorMA( sourceOrigin, spread, vertical, shifted );
		blocked += TraceBlocked( listenerOrigin, shifted ) ? 1 : 0;
		VectorMA( sourceOrigin, -spread, vertical, shifted );
		blocked += TraceBlocked( listenerOrigin, shifted ) ? 1 : 0;
		total = 5;
	}

	const float strength = ( s_alOcclusionStrength != nullptr ) ? s_alOcclusionStrength->value : 1.0f;
	return occ::ApplyStrength( occ::OcclusionFromProbeHits( blocked, total, centerBlocked ), strength );
}

static float AudibilityHorizonDistance( float rangeScale ) {
	return kOpenALMaxDistance * ClampFloat( rangeScale, kMinSoundShaderDistanceScale, kMaxSoundShaderRangeScale );
}

// Q3's legacy mixer reaches true silence at its maximum range, while the
// clamped OpenAL distance models hold a residual gain floor forever. Fading
// the final stretch of the legacy range keeps far-away sounds out of the mix
// without changing close and mid-range rolloff.
static float HorizonGain( float distance, float rangeScale ) {
	const float horizon = AudibilityHorizonDistance( rangeScale );
	const float fadeStart = horizon * kHorizonFadeStartFraction;
	if ( distance <= fadeStart ) {
		return 1.0f;
	}
	if ( distance >= horizon ) {
		return 0.0f;
	}
	return 1.0f - SmoothStep( ( distance - fadeStart ) / ( horizon - fadeStart ) );
}

static bool PointInLiquid( const float *origin ) {
	if ( origin == nullptr || !CollisionWorldReady() ) {
		return false;
	}
	return ( CM_PointContents( origin, 0 ) & kLiquidContents ) != 0;
}

// Raw listener velocity comes from frame-to-frame position deltas, which spike
// on teleports/respawns and jitter with frame timing. Native OpenAL doppler
// hears every spike, so clamp and smooth before handing it to the listener.
static void UpdateSmoothedListenerVelocity( Vec3f &smoothed, const float *rawVelocity, int elapsedMs ) {
	vec3_t velocity;
	VectorCopy( rawVelocity, velocity );

	const float speed = VectorLength( velocity );
	if ( speed > kListenerTeleportSpeed ) {
		VectorClear( velocity );
		smoothed = Vec3f();
		return;
	}
	if ( speed > kListenerVelocityMax ) {
		VectorScale( velocity, kListenerVelocityMax / speed, velocity );
	}

	const float blend = 1.0f - std::exp( -static_cast<float>( ClampInt( elapsedMs, 0, 500 ) ) / kListenerVelocitySmoothMs );
	smoothed.v[0] = LerpFloat( smoothed.v[0], velocity[0], blend );
	smoothed.v[1] = LerpFloat( smoothed.v[1], velocity[1], blend );
	smoothed.v[2] = LerpFloat( smoothed.v[2], velocity[2], blend );
}
