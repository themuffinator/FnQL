#include "glx_draw.h"

namespace glx {

typedef void ( APIENTRY *PFNGLXDRAWELEMENTSPROC )( GLenum mode, GLsizei count,
	GLenum type, const GLvoid *indices );
typedef void ( APIENTRY *PFNGLXDRAWARRAYSPROC )( GLenum mode, GLint first,
	GLsizei count );

struct DrawFns {
	PFNGLXDRAWELEMENTSPROC DrawElements;
	PFNGLXDRAWARRAYSPROC DrawArrays;
};

static DrawFns s_fns {};

static void *GLX_Draw_ProcAddress( const char *name )
{
	return RI().GL_GetProcAddress ? RI().GL_GetProcAddress( name ) : nullptr;
}

static qboolean GLX_Draw_ResolveElements()
{
	if ( s_fns.DrawElements ) {
		return qtrue;
	}

	s_fns.DrawElements = reinterpret_cast<PFNGLXDRAWELEMENTSPROC>(
		GLX_Draw_ProcAddress( "glDrawElements" ) );

	return s_fns.DrawElements ? qtrue : qfalse;
}

static qboolean GLX_Draw_ResolveArrays()
{
	if ( s_fns.DrawArrays ) {
		return qtrue;
	}

	s_fns.DrawArrays = reinterpret_cast<PFNGLXDRAWARRAYSPROC>(
		GLX_Draw_ProcAddress( "glDrawArrays" ) );

	return s_fns.DrawArrays ? qtrue : qfalse;
}

void GLX_Draw_Shutdown()
{
	s_fns = {};
}

qboolean GLX_Draw_DrawElements( unsigned int mode, int count, unsigned int type,
	const void *indices )
{
	if ( count <= 0 || !GLX_Draw_ResolveElements() ) {
		return qfalse;
	}

	s_fns.DrawElements( static_cast<GLenum>( mode ), static_cast<GLsizei>( count ),
		static_cast<GLenum>( type ), static_cast<const GLvoid *>( indices ) );
	return qtrue;
}

qboolean GLX_Draw_DrawArrays( unsigned int mode, int first, int count )
{
	if ( count <= 0 || !GLX_Draw_ResolveArrays() ) {
		return qfalse;
	}

	s_fns.DrawArrays( static_cast<GLenum>( mode ), static_cast<GLint>( first ),
		static_cast<GLsizei>( count ) );
	return qtrue;
}

} // namespace glx
