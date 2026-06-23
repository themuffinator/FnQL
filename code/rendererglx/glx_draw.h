#ifndef GLX_DRAW_H
#define GLX_DRAW_H

#include "glx_local.h"

namespace glx {

void GLX_Draw_Shutdown();
qboolean GLX_Draw_DrawElements( unsigned int mode, int count, unsigned int type,
	const void *indices );
qboolean GLX_Draw_DrawArrays( unsigned int mode, int first, int count );

} // namespace glx

#endif // GLX_DRAW_H
