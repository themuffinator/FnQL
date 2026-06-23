#ifndef FNQL_OPENAL_COMPAT_H
#define FNQL_OPENAL_COMPAT_H

#ifndef ALC_SOFT_output_mode
#define ALC_SOFT_output_mode
#define ALC_OUTPUT_MODE_SOFT 0x19AC
#define ALC_ANY_SOFT 0x19AD
#define ALC_MONO_SOFT 0x1500
#define ALC_STEREO_SOFT 0x1501
#define ALC_STEREO_BASIC_SOFT 0x19AE
#define ALC_STEREO_UHJ_SOFT 0x19AF
#define ALC_STEREO_HRTF_SOFT 0x19B2
#define ALC_QUAD_SOFT 0x1503
#define ALC_SURROUND_5_1_SOFT 0x1504
#define ALC_SURROUND_6_1_SOFT 0x1505
#define ALC_SURROUND_7_1_SOFT 0x1506
#endif

#ifndef ALC_SOFT_system_events
#define ALC_SOFT_system_events
#define ALC_PLAYBACK_DEVICE_SOFT 0x19D4
#define ALC_CAPTURE_DEVICE_SOFT 0x19D5
#define ALC_EVENT_TYPE_DEFAULT_DEVICE_CHANGED_SOFT 0x19D6
#define ALC_EVENT_TYPE_DEVICE_ADDED_SOFT 0x19D7
#define ALC_EVENT_TYPE_DEVICE_REMOVED_SOFT 0x19D8
#define ALC_EVENT_SUPPORTED_SOFT 0x19D9
#define ALC_EVENT_NOT_SUPPORTED_SOFT 0x19DA
typedef void ( ALC_APIENTRY *ALCEVENTPROCTYPESOFT )( ALCenum eventType, ALCenum deviceType, ALCdevice *device, ALCsizei length, const ALCchar *message, void *userParam );
typedef ALCenum ( ALC_APIENTRY *LPALCEVENTISSUPPORTEDSOFT )( ALCenum eventType, ALCenum deviceType );
typedef ALCboolean ( ALC_APIENTRY *LPALCEVENTCONTROLSOFT )( ALCsizei count, const ALCenum *events, ALCboolean enable );
typedef void ( ALC_APIENTRY *LPALCEVENTCALLBACKSOFT )( ALCEVENTPROCTYPESOFT callback, void *userParam );
#endif

#ifndef AL_SOFT_events
#define AL_SOFT_events
#define AL_EVENT_CALLBACK_FUNCTION_SOFT 0x19A2
#define AL_EVENT_CALLBACK_USER_PARAM_SOFT 0x19A3
#define AL_EVENT_TYPE_BUFFER_COMPLETED_SOFT 0x19A4
#define AL_EVENT_TYPE_SOURCE_STATE_CHANGED_SOFT 0x19A5
#define AL_EVENT_TYPE_DISCONNECTED_SOFT 0x19A6
typedef void ( AL_APIENTRY *ALEVENTPROCSOFT )( ALenum eventType, ALuint object, ALuint param,
	ALsizei length, const ALchar *message, void *userParam );
typedef void ( AL_APIENTRY *LPALEVENTCONTROLSOFT )( ALsizei count, const ALenum *types, ALboolean enable );
typedef void ( AL_APIENTRY *LPALEVENTCALLBACKSOFT )( ALEVENTPROCSOFT callback, void *userParam );
#endif

#ifndef ALC_SOFT_reopen_device
#define ALC_SOFT_reopen_device
typedef ALCboolean ( ALC_APIENTRY *LPALCREOPENDEVICESOFT )( ALCdevice *device, const ALCchar *deviceName, const ALCint *attribs );
#endif

#ifndef AL_SOFT_direct_channels
#define AL_SOFT_direct_channels
#define AL_DIRECT_CHANNELS_SOFT 0x1033
#endif

#ifndef AL_SOFT_direct_channels_remix
#define AL_SOFT_direct_channels_remix
#define AL_REMIX_UNMATCHED_SOFT 0x0002
#endif

#ifndef AL_SOFT_UHJ
#define AL_SOFT_UHJ
#define AL_FORMAT_UHJ2CHN8_SOFT 0x19A2
#define AL_FORMAT_UHJ2CHN16_SOFT 0x19A3
#define AL_FORMAT_UHJ2CHN_FLOAT32_SOFT 0x19A4
#define AL_FORMAT_UHJ3CHN8_SOFT 0x19A5
#define AL_FORMAT_UHJ3CHN16_SOFT 0x19A6
#define AL_FORMAT_UHJ3CHN_FLOAT32_SOFT 0x19A7
#define AL_FORMAT_UHJ4CHN8_SOFT 0x19A8
#define AL_FORMAT_UHJ4CHN16_SOFT 0x19A9
#define AL_FORMAT_UHJ4CHN_FLOAT32_SOFT 0x19AA
#define AL_STEREO_MODE_SOFT 0x19B0
#define AL_NORMAL_SOFT 0x0000
#define AL_SUPER_STEREO_SOFT 0x0001
#define AL_SUPER_STEREO_WIDTH_SOFT 0x19B1
#endif

#endif
