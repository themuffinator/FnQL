#ifndef GLX_CAPS_LOGIC_H
#define GLX_CAPS_LOGIC_H

#include "glx_types.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace glx {

static ID_INLINE qboolean GLX_Caps_LogicVersionAtLeast( int currentMajor, int currentMinor,
	int requiredMajor, int requiredMinor )
{
	return ( currentMajor > requiredMajor ||
		( currentMajor == requiredMajor && currentMinor >= requiredMinor ) ) ? qtrue : qfalse;
}

static ID_INLINE void GLX_Caps_ParseVersionString( const char *version, int *major, int *minor )
{
	const char *scan = version ? version : "";

	if ( major ) {
		*major = 0;
	}
	if ( minor ) {
		*minor = 0;
	}

	while ( *scan && !std::isdigit( static_cast<unsigned char>( *scan ) ) ) {
		scan++;
	}

	if ( !*scan ) {
		return;
	}

	if ( major ) {
		*major = std::atoi( scan );
	}
	while ( *scan && *scan != '.' ) {
		scan++;
	}

	if ( *scan == '.' && minor ) {
		scan++;
		*minor = std::atoi( scan );
	}
}

static ID_INLINE qboolean GLX_Caps_ExtensionListHas( const char *extensions, const char *name )
{
	if ( !name || !*name || !extensions || !*extensions ) {
		return qfalse;
	}

	const size_t len = std::strlen( name );
	const char *scan = extensions;

	while ( ( scan = std::strstr( scan, name ) ) != nullptr ) {
		const char before = ( scan == extensions ) ? ' ' : scan[ -1 ];
		const char after = scan[ len ];

		if ( ( before == ' ' || before == '\0' ) && ( after == ' ' || after == '\0' ) ) {
			return qtrue;
		}

		scan += len;
	}

	return qfalse;
}

static ID_INLINE FeatureSet GLX_Caps_FeaturesForVersionAndExtensions( int major, int minor,
	const char *extensions )
{
	FeatureSet features {};

	features.mapBufferRange = (
		GLX_Caps_LogicVersionAtLeast( major, minor, 3, 0 ) ||
		GLX_Caps_ExtensionListHas( extensions, "GL_ARB_map_buffer_range" ) ) ? qtrue : qfalse;
	features.uniformBufferObject = (
		GLX_Caps_LogicVersionAtLeast( major, minor, 3, 1 ) ||
		GLX_Caps_ExtensionListHas( extensions, "GL_ARB_uniform_buffer_object" ) ) ? qtrue : qfalse;
	features.instancedArrays = (
		GLX_Caps_LogicVersionAtLeast( major, minor, 3, 3 ) ||
		GLX_Caps_ExtensionListHas( extensions, "GL_ARB_instanced_arrays" ) ) ? qtrue : qfalse;
	features.bufferStorage = (
		GLX_Caps_LogicVersionAtLeast( major, minor, 4, 4 ) ||
		GLX_Caps_ExtensionListHas( extensions, "GL_ARB_buffer_storage" ) ) ? qtrue : qfalse;
	features.syncObjects = (
		GLX_Caps_LogicVersionAtLeast( major, minor, 3, 2 ) ||
		GLX_Caps_ExtensionListHas( extensions, "GL_ARB_sync" ) ) ? qtrue : qfalse;
	features.drawIndirect = (
		GLX_Caps_LogicVersionAtLeast( major, minor, 4, 0 ) ||
		GLX_Caps_ExtensionListHas( extensions, "GL_ARB_draw_indirect" ) ||
		GLX_Caps_ExtensionListHas( extensions, "GL_ARB_multi_draw_indirect" ) ) ? qtrue : qfalse;
	features.multiDrawIndirect = (
		GLX_Caps_LogicVersionAtLeast( major, minor, 4, 3 ) ||
		GLX_Caps_ExtensionListHas( extensions, "GL_ARB_multi_draw_indirect" ) ) ? qtrue : qfalse;
	features.directStateAccess = (
		GLX_Caps_LogicVersionAtLeast( major, minor, 4, 5 ) ||
		GLX_Caps_ExtensionListHas( extensions, "GL_ARB_direct_state_access" ) ) ? qtrue : qfalse;
	features.khrDebug = (
		GLX_Caps_LogicVersionAtLeast( major, minor, 4, 3 ) ||
		GLX_Caps_ExtensionListHas( extensions, "GL_KHR_debug" ) ) ? qtrue : qfalse;
	features.debugOutput = (
		features.khrDebug ||
		GLX_Caps_ExtensionListHas( extensions, "GL_ARB_debug_output" ) ) ? qtrue : qfalse;
	features.timerQuery = (
		GLX_Caps_LogicVersionAtLeast( major, minor, 3, 3 ) ||
		GLX_Caps_ExtensionListHas( extensions, "GL_ARB_timer_query" ) ||
		GLX_Caps_ExtensionListHas( extensions, "GL_EXT_timer_query" ) ) ? qtrue : qfalse;

	return features;
}

static ID_INLINE RenderProductTier GLX_Caps_TierForVersionAndFeatures( int major, int minor,
	const FeatureSet &features )
{
	return GLX_RenderProductTierForVersionAndFeatures( major, minor, features );
}

static ID_INLINE CapabilityHint GLX_Caps_HintForTierAndFeatures( RenderProductTier tier,
	const FeatureSet &features )
{
	return GLX_CapabilityHintForTierAndFeatures( tier, features );
}

} // namespace glx

#endif // GLX_CAPS_LOGIC_H
