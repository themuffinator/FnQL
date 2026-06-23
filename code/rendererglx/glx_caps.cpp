#include "glx_caps.h"
#include "glx_caps_logic.h"

#ifndef GL_CONTEXT_FLAGS
#define GL_CONTEXT_FLAGS 0x821E
#endif
#ifndef GL_CONTEXT_FLAG_DEBUG_BIT
#define GL_CONTEXT_FLAG_DEBUG_BIT 0x00000002
#endif

namespace glx {

typedef void ( APIENTRY *PFNGLXGETINTEGERVPROC )( GLenum pname, GLint *params );

void GLX_Caps_Reset( Capabilities *caps )
{
	if ( !caps ) {
		return;
	}

	caps->config = nullptr;
	caps->extensions = "";
	caps->major = 0;
	caps->minor = 0;
	caps->tier = RenderProductTier::GL12;
	caps->hint = CapabilityHint::FixedFunction;
	caps->features = {};
}

void GLX_Caps_Init( Capabilities *caps, const glconfig_t *config, const char *extensions )
{
	if ( !caps ) {
		return;
	}

	GLX_Caps_Reset( caps );

	caps->config = config;
	caps->extensions = extensions ? extensions : "";

	GLX_Caps_ParseVersionString( config ? config->version_string : "", &caps->major, &caps->minor );

	caps->features = GLX_Caps_FeaturesForVersionAndExtensions( caps->major, caps->minor, caps->extensions );
	if ( RI().GL_GetProcAddress && GLX_Caps_VersionAtLeast( *caps, 3, 0 ) ) {
		PFNGLXGETINTEGERVPROC getIntegerv =
			reinterpret_cast<PFNGLXGETINTEGERVPROC>( RI().GL_GetProcAddress( "glGetIntegerv" ) );
		GLint contextFlags = 0;

		if ( getIntegerv ) {
			getIntegerv( GL_CONTEXT_FLAGS, &contextFlags );
			caps->features.debugContext = ToQBool( ( contextFlags & GL_CONTEXT_FLAG_DEBUG_BIT ) != 0 );
		}
	}

	caps->tier = GLX_Caps_TierForVersionAndFeatures( caps->major, caps->minor, caps->features );
	caps->hint = GLX_Caps_HintForTierAndFeatures( caps->tier, caps->features );
}

qboolean GLX_Caps_HasExtension( const Capabilities &caps, const char *name )
{
	return GLX_Caps_ExtensionListHas( caps.extensions, name );
}

qboolean GLX_Caps_VersionAtLeast( const Capabilities &caps, int major, int minor )
{
	return ( caps.major > major || ( caps.major == major && caps.minor >= minor ) ) ? qtrue : qfalse;
}

const char *GLX_Caps_TierName( RenderProductTier tier )
{
	return GLX_RenderProductTierName( tier );
}

const char *GLX_Caps_HintName( CapabilityHint hint )
{
	return GLX_CapabilityHintName( hint );
}

} // namespace glx
