#ifndef GLX_CAPS_H
#define GLX_CAPS_H

#include "glx_local.h"

namespace glx {

void GLX_Caps_Reset( Capabilities *caps );
void GLX_Caps_Init( Capabilities *caps, const glconfig_t *config, const char *extensions );
qboolean GLX_Caps_HasExtension( const Capabilities &caps, const char *name );
qboolean GLX_Caps_VersionAtLeast( const Capabilities &caps, int major, int minor );
const char *GLX_Caps_TierName( RenderProductTier tier );
const char *GLX_Caps_HintName( CapabilityHint hint );

} // namespace glx

#endif // GLX_CAPS_H
