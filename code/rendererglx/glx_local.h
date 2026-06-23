#ifndef GLX_LOCAL_H
#define GLX_LOCAL_H

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderercommon/tr_public.h"
#include "../renderercommon/tr_glx_public.h"
#include "../renderer/qgl.h"
#include "glx_types.h"

namespace glx {

struct Capabilities {
	const glconfig_t *config;
	const char *extensions;
	int major;
	int minor;
	RenderProductTier tier;
	CapabilityHint hint;
	FeatureSet features;
};

extern refimport_t *g_imports;

refimport_t &RI();
qboolean ImportsReady();
const char *BoolName( qboolean value );
qboolean ToQBool( bool value );

static ID_INLINE void MakeCvarInstant( cvar_t *cvar )
{
	if ( cvar ) {
		cvar->flags &= ~CVAR_LATCH;
	}
}

} // namespace glx

#endif // GLX_LOCAL_H
