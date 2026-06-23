/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#ifndef SDL_FUNCTION_POINTER_IS_VOID_POINTER
#	define SDL_FUNCTION_POINTER_IS_VOID_POINTER 1
#endif

#include <SDL3/SDL.h>
#ifdef USE_VULKAN_API
#	include <SDL3/SDL_vulkan.h>
#endif
#include <climits>
#include <limits>
#include <vector>

#include "../client/client.h"
#ifdef _WIN32
#	include "../win32/win_raii.h"
#endif
#ifndef _WIN32
#include "../unix/unix_syscon.h"
#endif
#include "../renderercommon/tr_public.h"
#include "sdl_glw.h"
#include "sdl_icon.h"
#include "sdl_raii.h"

typedef enum {
	RSERR_OK,
	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE,
	RSERR_FATAL_ERROR,
	RSERR_UNKNOWN
} rserr_t;

glwstate_t glw_state;

SDL_Window *SDL_window = NULL;
static fnql::sdl::ScopedGLContext SDL_glContext;
#ifdef USE_VULKAN_API
static PFN_vkGetInstanceProcAddr qvkGetInstanceProcAddr;
#endif
static qboolean glw_floatFramebufferActive = qfalse;

// framebuffer attributes the current SDL_window was created with;
// the window can only be reused across \vid_restart while these still match
// because the pixel format and the OpenGL/Vulkan surface kind are fixed
// at window creation time
typedef struct {
	qboolean valid;
	qboolean vulkan;
	int colorBits;
	int depthBits;
	int stencilBits;
	qboolean stereo;
	qboolean software;
	qboolean floatFramebuffer;
} windowRecipe_t;

static windowRecipe_t glw_windowRecipe;

cvar_t *r_stereoEnabled;
cvar_t *in_nograb;

static qboolean GLW_ShouldRequestGLxDebugContext( void )
{
	const char *renderer = Cvar_VariableString( "cl_renderer" );

	if ( !renderer || Q_stricmp( renderer, "glx" ) ) {
		return qfalse;
	}

	return Cvar_VariableIntegerValue( "r_glxDebug" ) ? qtrue : qfalse;
}

static qboolean GLW_DisplayReportsHdr( SDL_DisplayID display )
{
	SDL_PropertiesID displayProps;

	if ( !display ) {
		display = SDL_GetPrimaryDisplay();
	}
	if ( !display ) {
		return qfalse;
	}

	displayProps = SDL_GetDisplayProperties( display );
	if ( !displayProps ) {
		return qfalse;
	}

	return SDL_GetBooleanProperty( displayProps,
		SDL_PROP_DISPLAY_HDR_ENABLED_BOOLEAN, false ) ? qtrue : qfalse;
}

static qboolean GLW_ShouldRequestFloatFramebuffer( SDL_DisplayID display )
{
	const int outputBackend = Cvar_VariableIntegerValue( "r_outputBackend" );
	const int hdr = Cvar_VariableIntegerValue( "r_hdr" );

	if ( hdr <= 0 || !GLW_DisplayReportsHdr( display ) ) {
		return qfalse;
	}

	return ( outputBackend == ROUTPUT_REQUEST_AUTO ||
		outputBackend == ROUTPUT_REQUEST_WINDOWS_SCRGB ||
		outputBackend == ROUTPUT_REQUEST_MACOS_EDR ||
		outputBackend == ROUTPUT_REQUEST_LINUX_EXPERIMENTAL_HDR ) ? qtrue : qfalse;
}

static void GLW_InitDisplayOutput( rendererDisplayOutput_t *output )
{
	if ( !output ) {
		return;
	}

	Com_Memset( output, 0, sizeof( *output ) );
	output->displayIndex = -1;
	output->nativeBackend = ROUTPUT_BACKEND_SDR_SRGB;
	output->sdrWhiteNits = 203.0f;
	output->hdrHeadroom = 1.0f;
	output->maxLuminanceNits = 203.0f;
	output->maxFullFrameLuminanceNits = 203.0f;
	Q_strncpyz( output->reason, "SDR sRGB default", sizeof( output->reason ) );
}

static void GLW_SetDisplayOutputReason( rendererDisplayOutput_t *output, const char *reason )
{
	if ( !output ) {
		return;
	}
	if ( !reason ) {
		reason = "";
	}
	Q_strncpyz( output->reason, reason, sizeof( output->reason ) );
}

static void GLW_ShowCursor( qboolean show )
{
	if ( show ) {
		SDL_ShowCursor();
	} else {
		SDL_HideCursor();
	}
}

static SDL_Window *GLW_CreateWindow( const char *title, int x, int y, int w, int h, SDL_WindowFlags flags )
{
	fnql::sdl::ScopedProperties props( SDL_CreateProperties() );

	if ( !props ) {
		Com_DPrintf( "SDL_CreateProperties failed: %s\n", SDL_GetError() );
		return NULL;
	}

	SDL_SetStringProperty( props.get(), SDL_PROP_WINDOW_CREATE_TITLE_STRING, title );
	SDL_SetNumberProperty( props.get(), SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, (Sint64)flags );
	SDL_SetNumberProperty( props.get(), SDL_PROP_WINDOW_CREATE_X_NUMBER, x );
	SDL_SetNumberProperty( props.get(), SDL_PROP_WINDOW_CREATE_Y_NUMBER, y );
	SDL_SetNumberProperty( props.get(), SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w );
	SDL_SetNumberProperty( props.get(), SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h );

	return SDL_CreateWindowWithProperties( props.get() );
}

static void GLW_QuitVideoSubsystem( void )
{
#ifndef _WIN32
	if ( Sys_ConsoleVideoActive() ) {
		return;
	}
#endif
	SDL_QuitSubSystem( SDL_INIT_VIDEO );
}

static void GLW_DestroyWindow( void )
{
	SDL_glContext.reset();
	glw_floatFramebufferActive = qfalse;

	if ( SDL_window ) {
		SDL_DestroyWindow( SDL_window );
		SDL_window = NULL;
	}

	Com_Memset( &glw_windowRecipe, 0, sizeof( glw_windowRecipe ) );
}

/*
===============
GLimp_Shutdown
===============
*/
void GLimp_Shutdown( qboolean unloadDLL )
{
	const char* drv = SDL_GetCurrentVideoDriver();

	IN_Shutdown();

	if ( glw_state.isFullscreen ) {
		if ( drv && strcmp( drv, "x11" ) == 0 ) {
			SDL_WarpMouseGlobal( glw_state.desktop_width / 2, glw_state.desktop_height / 2 );
		} else {
			GLW_ShowCursor( qtrue );
		}
	}

	GLW_RestoreGamma();

	GLW_DestroyWindow();

	if ( unloadDLL )
		GLW_QuitVideoSubsystem();
}


/*
===============
GLimp_InvalidateConfig

The renderer module owning the glconfig_t we feed back into is about to be
unloaded: drop the pointer so window events arriving before the next
GLimp_Init/VKimp_Init cannot write through stale memory.
===============
*/
void GLimp_InvalidateConfig( void )
{
	glw_state.config = NULL;
}


/*
===============
GLimp_Minimize

Minimize the game so that user is back at the desktop
===============
*/
void GLimp_Minimize( void )
{
	SDL_MinimizeWindow( SDL_window );
}


/*
===============
GLimp_LogComment
===============
*/
void GLimp_LogComment( const char *comment )
{
}


static void GLW_SyncWindow( const char *reason )
{
	if ( SDL_window && !SDL_SyncWindow( SDL_window ) ) {
		Com_DPrintf( "SDL_SyncWindow failed after %s: %s\n", reason, SDL_GetError() );
	}
}


void GLW_UpdateWindowState( void )
{
	SDL_DisplayID display = 0;
	const SDL_DisplayMode *desktopMode;
	int numDisplays = 0;
	fnql::sdl::ScopedSdlMemory<SDL_DisplayID> displays( SDL_GetDisplays( &numDisplays ) );

	if ( numDisplays > 0 ) {
		glw_state.monitorCount = numDisplays;
	} else if ( glw_state.monitorCount <= 0 ) {
		glw_state.monitorCount = 1;
	}

	if ( SDL_window ) {
		SDL_WindowFlags windowFlags = SDL_GetWindowFlags( SDL_window );
		int w, h;

		glw_state.isFullscreen = ( windowFlags & SDL_WINDOW_FULLSCREEN ) ? qtrue : qfalse;
		if ( glw_state.config ) {
			glw_state.config->isFullscreen = glw_state.isFullscreen;
		}

		if ( !SDL_GetWindowSize( SDL_window, &w, &h ) ) {
			Com_DPrintf( "SDL_GetWindowSize failed: %s\n", SDL_GetError() );
		} else {
			glw_state.window_width = w;
			glw_state.window_height = h;
		}

		display = SDL_GetDisplayForWindow( SDL_window );
		if ( !display ) {
			Com_DPrintf( "SDL_GetDisplayForWindow() failed: %s\n", SDL_GetError() );
		}
	}

	desktopMode = display ? SDL_GetDesktopDisplayMode( display ) : NULL;
	if ( desktopMode ) {
		glw_state.desktop_width = desktopMode->w;
		glw_state.desktop_height = desktopMode->h;
	} else if ( !glw_state.desktop_width || !glw_state.desktop_height ) {
		glw_state.desktop_width = 640;
		glw_state.desktop_height = 480;
	}
}


static qboolean GLW_EnterFullscreen( SDL_Window *window, const SDL_DisplayMode *mode )
{
	qboolean exclusiveTried = qfalse;

#ifndef MACOS_X
	if ( mode ) {
		exclusiveTried = qtrue;
		if ( !SDL_SetWindowFullscreenMode( window, mode ) ) {
			Com_DPrintf( "SDL_SetWindowFullscreenMode failed: %s\n", SDL_GetError() );
		} else if ( SDL_SetWindowFullscreen( window, true ) ) {
			return qtrue;
		} else {
			Com_DPrintf( "SDL_SetWindowFullscreen failed: %s\n", SDL_GetError() );
		}
	}
#endif

	if ( !SDL_SetWindowFullscreenMode( window, NULL ) ) {
		Com_DPrintf( "SDL_SetWindowFullscreenMode failed: %s\n", SDL_GetError() );
		return qfalse;
	}

	if ( !SDL_SetWindowFullscreen( window, true ) ) {
		Com_DPrintf( "SDL_SetWindowFullscreen failed: %s\n", SDL_GetError() );
		return qfalse;
	}

	if ( exclusiveTried ) {
		Com_Printf( "...falling back to desktop fullscreen\n" );
	}

	return qtrue;
}


static SDL_DisplayID FindNearestDisplay( int *x, int *y, int w, int h )
{
	const int cx = *x + w / 2;
	const int cy = *y + h / 2;
	int i, index, numDisplays;
	SDL_Rect *m;
	SDL_DisplayID display = 0;

	index = -1; // selected display index

	fnql::sdl::ScopedSdlMemory<SDL_DisplayID> displays( SDL_GetDisplays( &numDisplays ) );
	if ( !displays || numDisplays <= 0 ) {
		return 0;
	}

	glw_state.monitorCount = numDisplays;

	std::vector<SDL_Rect> bounds( static_cast<std::size_t>( numDisplays ) );

	for ( i = 0; i < numDisplays; i++ )
	{
		if ( !SDL_GetDisplayBounds( displays.get()[i], &bounds[ static_cast<std::size_t>( i ) ] ) ) {
			SDL_Rect &rect = bounds[ static_cast<std::size_t>( i ) ];
			rect.x = 0;
			rect.y = 0;
			rect.w = 0;
			rect.h = 0;
		}
		//Com_Printf( "[%i]: x=%i, y=%i, w=%i, h=%i\n", i, list[i].x, list[i].y, list[i].w, list[i].h );
	}

	// select display by window center intersection
	for ( i = 0; i < numDisplays; i++ )
	{
		m = &bounds[ static_cast<std::size_t>( i ) ];
		if ( cx >= m->x && cx < (m->x + m->w) && cy >= m->y && cy < (m->y + m->h) )
		{
			index = i;
			break;
		}
	}

	// select display by nearest distance between window center and display center
	if ( index == -1 )
	{
		unsigned long nearest, dist;
		int dx, dy;
		nearest = (std::numeric_limits<unsigned long>::max)();
		for ( i = 0; i < numDisplays; i++ )
		{
			m = &bounds[ static_cast<std::size_t>( i ) ];
			dx = (m->x + m->w/2) - cx;
			dy = (m->y + m->h/2) - cy;
			dist = ( dx * dx ) + ( dy * dy );
			if ( dist < nearest )
			{
				nearest = dist;
				index = i;
			}
		}
	}

	// adjust x and y coordinates if needed
	if ( index >= 0 )
	{
		m = &bounds[ static_cast<std::size_t>( index ) ];
		display = displays.get()[index];
		if ( *x < m->x )
			*x = m->x;

		if ( *y < m->y )
			*y = m->y;
	}

	return display;
}


static SDL_HitTestResult SDL_HitTestFunc( SDL_Window *win, const SDL_Point *area, void *data )
{
	if ( Key_GetCatcher() & KEYCATCH_CONSOLE && keys[ K_ALT ].down )
		return SDL_HITTEST_DRAGGABLE;

	return SDL_HITTEST_NORMAL;
}


/*
===============
GLW_ApplyFullscreen

Switch the window into the requested fullscreen mode and refresh the
reported display frequency.
===============
*/
static qboolean GLW_ApplyFullscreen( glconfig_t *config, SDL_DisplayID display, int colorBits )
{
	SDL_DisplayMode mode;
	const SDL_DisplayMode *currentMode;
	SDL_DisplayID fullscreenDisplay;

	SDL_zero( mode );
	mode.displayID = display;

	switch ( colorBits )
	{
		case 16: mode.format = SDL_PIXELFORMAT_RGB565; break;
		case 24: mode.format = SDL_PIXELFORMAT_RGB24;  break;
		default:
			Com_DPrintf( "colorBits is %d, can't fullscreen\n", colorBits );
			return qfalse;
	}

	mode.w = config->vidWidth;
	mode.h = config->vidHeight;
	mode.refresh_rate = /* config->displayFrequency = */ Cvar_VariableIntegerValue( "r_displayRefresh" );

	if ( !GLW_EnterFullscreen( SDL_window, &mode ) ) {
		return qfalse;
	}

	GLW_SyncWindow( "fullscreen transition" );
	GLW_UpdateWindowState();

	if ( ( currentMode = SDL_GetWindowFullscreenMode( SDL_window ) ) != NULL ) {
		config->displayFrequency = currentMode->refresh_rate;
	} else {
		fullscreenDisplay = SDL_GetDisplayForWindow( SDL_window );
		currentMode = fullscreenDisplay ? SDL_GetCurrentDisplayMode( fullscreenDisplay ) : NULL;
		if ( currentMode ) {
			config->displayFrequency = currentMode->refresh_rate;
		}
	}

	return qtrue;
}


/*
===============
GLW_SetupDrawableContext

Create the OpenGL context on the current window - or just publish the
framebuffer attributes for Vulkan - and report the resulting configuration.
===============
*/
static qboolean GLW_SetupDrawableContext( glconfig_t *config, qboolean vulkan, int testColorBits, int testDepthBits, int testStencilBits, qboolean requestFloatFramebuffer )
{
#ifdef USE_VULKAN_API
	if ( vulkan )
	{
		config->colorBits = testColorBits;
		config->depthBits = testDepthBits;
		config->stencilBits = testStencilBits;
	}
	else
#endif
	{
		int realColorBits[3];

		if ( !SDL_glContext )
		{
			SDL_glContext.reset( SDL_GL_CreateContext( SDL_window ) );
			if ( !SDL_glContext )
			{
				if ( GLW_ShouldRequestGLxDebugContext() )
				{
					Com_DPrintf( "SDL_GL_CreateContext with debug flag failed: %s\n", SDL_GetError( ) );
					SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, 0 );
					SDL_glContext.reset( SDL_GL_CreateContext( SDL_window ) );
				}

				if ( !SDL_glContext )
				{
					Com_DPrintf( "SDL_GL_CreateContext failed: %s\n", SDL_GetError( ) );
					return qfalse;
				}

				if ( GLW_ShouldRequestGLxDebugContext() )
				{
					Com_Printf( "...SDL debug context unavailable, using regular OpenGL context\n" );
				}
			}
			else if ( GLW_ShouldRequestGLxDebugContext() )
			{
				Com_Printf( "...created SDL OpenGL debug context\n" );
			}
		}

		if ( !SDL_GL_SetSwapInterval( r_swapInterval->integer ) )
		{
			Com_DPrintf( "SDL_GL_SetSwapInterval failed: %s\n", SDL_GetError( ) );
		}

		SDL_GL_GetAttribute( SDL_GL_RED_SIZE, &realColorBits[0] );
		SDL_GL_GetAttribute( SDL_GL_GREEN_SIZE, &realColorBits[1] );
		SDL_GL_GetAttribute( SDL_GL_BLUE_SIZE, &realColorBits[2] );
		SDL_GL_GetAttribute( SDL_GL_DEPTH_SIZE, &config->depthBits );
		SDL_GL_GetAttribute( SDL_GL_STENCIL_SIZE, &config->stencilBits );
		{
			int realFloatFramebuffer = 0;
			if ( SDL_GL_GetAttribute( SDL_GL_FLOATBUFFERS, &realFloatFramebuffer ) ) {
				glw_floatFramebufferActive = realFloatFramebuffer ? qtrue : qfalse;
			} else {
				glw_floatFramebufferActive = qfalse;
			}
			if ( requestFloatFramebuffer && !glw_floatFramebufferActive ) {
				Com_DPrintf( "SDL did not provide a floating-point OpenGL framebuffer for HDR output: %s\n",
					SDL_GetError() );
			}
		}

		config->colorBits = realColorBits[0] + realColorBits[1] + realColorBits[2];
	}

	Com_Printf( "Using %d color bits, %d depth, %d stencil display.\n", config->colorBits, config->depthBits, config->stencilBits );

	return qtrue;
}


/*
===============
GLW_CanReuseWindow
===============
*/
static qboolean GLW_CanReuseWindow( qboolean vulkan, int colorBits, int depthBits, int stencilBits, qboolean requestFloatFramebuffer )
{
	if ( SDL_window == NULL || !glw_windowRecipe.valid ) {
		return qfalse;
	}

	if ( glw_windowRecipe.vulkan != vulkan ) {
		return qfalse;
	}

	if ( vulkan ) {
		return qtrue;
	}

	// the GL pixel format is fixed at window creation time: any change
	// there requires a full window re-creation
	if ( glw_windowRecipe.colorBits != colorBits ||
		glw_windowRecipe.depthBits != depthBits ||
		glw_windowRecipe.stencilBits != stencilBits ||
		glw_windowRecipe.stereo != ( r_stereoEnabled->integer ? qtrue : qfalse ) ||
		glw_windowRecipe.software != ( ( r_allowSoftwareGL && r_allowSoftwareGL->integer ) ? qtrue : qfalse ) ||
		glw_windowRecipe.floatFramebuffer != requestFloatFramebuffer ) {
		return qfalse;
	}

	return qtrue;
}


/*
===============
GLW_ReuseExistingWindow

\vid_restart fast path: morph the existing window in place - windowed or
fullscreen state, borders, geometry - and rebuild the drawable context on
it instead of destroying it. Any failure makes the caller fall back to a
full window re-creation, so a partially morphed window is never kept.
===============
*/
static qboolean GLW_ReuseExistingWindow( glconfig_t *config, SDL_DisplayID display, qboolean fullscreen, qboolean vulkan, int colorBits, int depthBits, int stencilBits, qboolean requestFloatFramebuffer )
{
	SDL_WindowFlags windowFlags;

	if ( !GLW_CanReuseWindow( vulkan, colorBits, depthBits, stencilBits, requestFloatFramebuffer ) ) {
		return qfalse;
	}

	config->stereoEnabled = glw_windowRecipe.stereo;

	if ( fullscreen )
	{
		if ( !GLW_ApplyFullscreen( config, display, colorBits ) ) {
			return qfalse;
		}
	}
	else
	{
		int x, y;

		if ( !SDL_SetWindowFullscreen( SDL_window, false ) ) {
			Com_DPrintf( "SDL_SetWindowFullscreen failed: %s\n", SDL_GetError() );
			return qfalse;
		}

		SDL_SetWindowBordered( SDL_window, r_noborder->integer ? false : true );
		SDL_SetWindowSize( SDL_window, config->vidWidth, config->vidHeight );

		x = vid_xpos->integer;
		y = vid_ypos->integer;
		FindNearestDisplay( &x, &y, config->vidWidth, config->vidHeight );
		SDL_SetWindowPosition( SDL_window, x, y );

		GLW_SyncWindow( "windowed transition" );
		GLW_UpdateWindowState();
	}

#ifdef USE_VULKAN_API
	if ( !vulkan )
#endif
	{
		SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS,
			GLW_ShouldRequestGLxDebugContext() ? SDL_GL_CONTEXT_DEBUG_FLAG : 0 );
	}

	if ( !GLW_SetupDrawableContext( config, vulkan, colorBits, depthBits, stencilBits, requestFloatFramebuffer ) ) {
		return qfalse;
	}

	// the window keeps its focus state through the restart and no focus
	// events will arrive to refresh these, so derive them from live flags
	windowFlags = SDL_GetWindowFlags( SDL_window );
	gw_active = ( windowFlags & SDL_WINDOW_INPUT_FOCUS ) ? qtrue : qfalse;
	gw_minimized = ( windowFlags & SDL_WINDOW_MINIMIZED ) ? qtrue : qfalse;

	Com_Printf( "...reusing existing window (%s)\n", fullscreen ? "fullscreen" : "windowed" );

	return qtrue;
}


/*
===============
GLimp_SetMode
===============
*/
static rserr_t GLW_SetMode( int mode, const char *modeFS, qboolean fullscreen, qboolean vulkan )
{
	glconfig_t *config = glw_state.config;
	int perChannelColorBits;
	int colorBits, depthBits, stencilBits;
	int i;
	const SDL_DisplayMode *desktopMode;
	SDL_DisplayID display = 0;
	int x;
	int y;
	SDL_WindowFlags flags = 0;
	qboolean requestFloatFramebuffer = qfalse;
	qboolean reusedWindow;

#ifdef USE_VULKAN_API
	if ( vulkan ) {
		flags |= SDL_WINDOW_VULKAN;
		Com_Printf( "Initializing Vulkan display\n");
	} else
#endif
	{
		flags |= SDL_WINDOW_OPENGL;
		Com_Printf( "Initializing OpenGL display\n");
	}

	// If a window exists, note its display index
	if ( SDL_window != NULL )
	{
		display = SDL_GetDisplayForWindow( SDL_window );
		if ( !display )
		{
			Com_DPrintf( "SDL_GetDisplayForWindow() failed: %s\n", SDL_GetError() );
		}
	}
	else
	{
		x = vid_xpos->integer;
		y = vid_ypos->integer;

		// find out to which display our window belongs to
		// according to previously stored \vid_xpos and \vid_ypos coordinates
		display = FindNearestDisplay( &x, &y, 640, 480 );

		//Com_Printf("Selected display: %i\n", display );
	}

#ifdef USE_VULKAN_API
	if ( !vulkan )
#endif
	{
		requestFloatFramebuffer = GLW_ShouldRequestFloatFramebuffer( display );
		if ( requestFloatFramebuffer ) {
			Com_Printf( "...requesting floating-point OpenGL framebuffer for HDR output\n" );
		}
	}

	desktopMode = display ? SDL_GetDesktopDisplayMode( display ) : NULL;
	if ( desktopMode ) {
		glw_state.desktop_width = desktopMode->w;
		glw_state.desktop_height = desktopMode->h;
	} else {
		glw_state.desktop_width = 640;
		glw_state.desktop_height = 480;
	}

	config->isFullscreen = fullscreen;
	glw_state.isFullscreen = fullscreen;

	Com_Printf( "...setting mode %d:", mode );

	if ( !CL_GetModeInfo( &config->vidWidth, &config->vidHeight, &config->windowAspect, mode, modeFS, glw_state.desktop_width, glw_state.desktop_height, fullscreen ) )
	{
		Com_Printf( " invalid mode\n" );
		return RSERR_INVALID_MODE;
	}

	Com_Printf( " %d %d\n", config->vidWidth, config->vidHeight );

	colorBits = r_colorbits->value;

	if ( colorBits == 0 || colorBits > 24 )
		colorBits = 24;

	if ( cl_depthbits->integer == 0 )
	{
		// implicitly assume Z-buffer depth == desktop color depth
		if ( colorBits > 16 )
			depthBits = 24;
		else
			depthBits = 16;
	}
	else
		depthBits = cl_depthbits->integer;

	stencilBits = cl_stencilbits->integer;

	// do not allow stencil if Z-buffer depth likely won't contain it
	if ( depthBits < 24 )
		stencilBits = 0;

	// Destroy existing state if it exists
	if ( SDL_glContext )
	{
		SDL_glContext.reset();
	}

	reusedWindow = qfalse;

	if ( SDL_window != NULL )
	{
		GLW_RestoreGamma();

		// \vid_restart fast keeps the window alive: morph it in place -
		// the usual case for windowed/fullscreen toggles - and only fall
		// back to a full re-creation when that is not possible
		reusedWindow = GLW_ReuseExistingWindow( config, display, fullscreen, vulkan,
			colorBits, depthBits, stencilBits, requestFloatFramebuffer );

		if ( !reusedWindow )
		{
			SDL_GetWindowPosition( SDL_window, &x, &y );
			Com_DPrintf( "Existing window at %dx%d before being destroyed\n", x, y );
			GLW_DestroyWindow();
		}
	}

	if ( !reusedWindow )
	{
		gw_active = qfalse;
		gw_minimized = qtrue;

		if ( fullscreen )
		{
			flags |= SDL_WINDOW_HIDDEN;
		}
		else if ( r_noborder->integer )
		{
			flags |= SDL_WINDOW_BORDERLESS;
		}

		flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
	}

	for ( i = 0; i < 16 && !reusedWindow; i++ )
	{
		int testColorBits, testDepthBits, testStencilBits;
		qboolean testFloatFramebuffer;

		// 0 - default
		// 1 - minus colorBits
		// 2 - minus depthBits
		// 3 - minus stencil
		if ((i % 4) == 0 && i)
		{
			// one pass, reduce
			switch (i / 4)
			{
				case 2 :
					if (colorBits == 24)
						colorBits = 16;
					break;
				case 1 :
					if (depthBits == 24)
						depthBits = 16;
					else if (depthBits == 16)
						depthBits = 8;
				case 3 :
					if (stencilBits == 24)
						stencilBits = 16;
					else if (stencilBits == 16)
						stencilBits = 8;
			}
		}

		testColorBits = colorBits;
		testDepthBits = depthBits;
		testStencilBits = stencilBits;

		if ((i % 4) == 3)
		{ // reduce colorBits
			if (testColorBits == 24)
				testColorBits = 16;
		}

		if ((i % 4) == 2)
		{ // reduce depthBits
			if (testDepthBits == 24)
				testDepthBits = 16;
		}

		if ((i % 4) == 1)
		{ // reduce stencilBits
			if (testStencilBits == 8)
				testStencilBits = 0;
		}

		if ( testColorBits == 24 )
			perChannelColorBits = 8;
		else
			perChannelColorBits = 4;

		testFloatFramebuffer = ( requestFloatFramebuffer && i < 4 ) ? qtrue : qfalse;

#ifdef USE_VULKAN_API
		if ( !vulkan )
#endif
		{
			const qboolean requestDebugContext = GLW_ShouldRequestGLxDebugContext();

#ifdef __sgi /* Fix for SGIs grabbing too many bits of color */
			if (perChannelColorBits == 4)
				perChannelColorBits = 0; /* Use minimum size for 16-bit color */

			/* Need alpha or else SGIs choose 36+ bit RGB mode */
			SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 1 );
#endif

			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, perChannelColorBits );
			SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, testDepthBits );
			SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, testStencilBits );
			SDL_GL_SetAttribute( SDL_GL_FLOATBUFFERS, testFloatFramebuffer ? 1 : 0 );

			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLEBUFFERS, 0 );
			SDL_GL_SetAttribute( SDL_GL_MULTISAMPLESAMPLES, 0 );

			if ( r_stereoEnabled->integer )
			{
				config->stereoEnabled = qtrue;
				SDL_GL_SetAttribute( SDL_GL_STEREO, 1 );
			}
			else
			{
				config->stereoEnabled = qfalse;
				SDL_GL_SetAttribute( SDL_GL_STEREO, 0 );
			}
		
			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

			if ( !r_allowSoftwareGL->integer )
				SDL_GL_SetAttribute( SDL_GL_ACCELERATED_VISUAL, 1 );

			SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS,
				requestDebugContext ? SDL_GL_CONTEXT_DEBUG_FLAG : 0 );
		}

		if ( ( SDL_window = GLW_CreateWindow( cl_title, x, y, config->vidWidth, config->vidHeight, flags ) ) == NULL )
		{
			Com_DPrintf( "SDL_CreateWindowWithProperties failed: %s\n", SDL_GetError() );
			continue;
		}

		if ( fullscreen )
		{
			if ( !GLW_ApplyFullscreen( config, display, testColorBits ) ) {
				GLW_DestroyWindow();
				continue;
			}
		}
		else
		{
			GLW_SyncWindow( "window creation" );
			GLW_UpdateWindowState();
		}

		if ( !GLW_SetupDrawableContext( config, vulkan, testColorBits, testDepthBits, testStencilBits, testFloatFramebuffer ) )
		{
			GLW_DestroyWindow();
			continue;
		}

		glw_windowRecipe.valid = qtrue;
		glw_windowRecipe.vulkan = vulkan;
		glw_windowRecipe.colorBits = testColorBits;
		glw_windowRecipe.depthBits = testDepthBits;
		glw_windowRecipe.stencilBits = testStencilBits;
		glw_windowRecipe.stereo = config->stereoEnabled;
		glw_windowRecipe.software = ( r_allowSoftwareGL && r_allowSoftwareGL->integer ) ? qtrue : qfalse;
		glw_windowRecipe.floatFramebuffer = testFloatFramebuffer;

		break;
	}

	if ( SDL_window )
	{
#ifdef USE_ICON
		fnql::sdl::ScopedSurface icon( SDL_CreateSurfaceFrom(
			CLIENT_WINDOW_ICON.width,
			CLIENT_WINDOW_ICON.height,
			SDL_PIXELFORMAT_RGBA32,
			(void *)CLIENT_WINDOW_ICON.pixel_data,
			CLIENT_WINDOW_ICON.bytes_per_pixel * CLIENT_WINDOW_ICON.width
		) );
		if ( icon )
		{
			SDL_SetWindowIcon( SDL_window, icon.get() );
		}
#endif
	}
	else
	{
		Com_Printf( "Couldn't get a visual\n" );
		return RSERR_INVALID_MODE;
	}

	if ( !fullscreen && r_noborder->integer )
		SDL_SetWindowHitTest( SDL_window, SDL_HitTestFunc, NULL );
	else
		SDL_SetWindowHitTest( SDL_window, NULL, NULL );

	if ( SDL_GetWindowFlags( SDL_window ) & SDL_WINDOW_HIDDEN )
	{
		if ( !SDL_ShowWindow( SDL_window ) ) {
			Com_DPrintf( "SDL_ShowWindow failed: %s\n", SDL_GetError() );
		}
		if ( !SDL_RaiseWindow( SDL_window ) ) {
			Com_DPrintf( "SDL_RaiseWindow failed: %s\n", SDL_GetError() );
		}
		GLW_SyncWindow( "window show" );
	}

	GLW_UpdateWindowState();

	if ( !SDL_GetWindowSizeInPixels( SDL_window, &config->vidWidth, &config->vidHeight ) )
	{
		Com_DPrintf( "SDL_GetWindowSizeInPixels failed: %s\n", SDL_GetError() );
		config->vidWidth = glw_state.window_width;
		config->vidHeight = glw_state.window_height;
	}

	SDL_WarpMouseInWindow( SDL_window, glw_state.window_width / 2, glw_state.window_height / 2 );

	return RSERR_OK;
}


static qboolean GLW_CvarEnabled( const cvar_t *cvar )
{
	return ( cvar && cvar->integer ) ? qtrue : qfalse;
}


/*
===============
GLimp_StartDriverAndSetMode
===============
*/
static rserr_t GLimp_StartDriverAndSetMode( int mode, const char *modeFS, qboolean fullscreen, qboolean vulkan )
{
	rserr_t err;

	if ( fullscreen && in_nograb->integer )
	{
		Com_Printf( "Fullscreen not allowed with \\in_nograb 1\n");
		Cvar_Set( "r_fullscreen", "0" );
		r_fullscreen->modified = qfalse;
		fullscreen = qfalse;
	}

	if ( !SDL_WasInit( SDL_INIT_VIDEO ) )
	{
		const char *driverName;

		if ( !SDL_Init( SDL_INIT_VIDEO ) )
		{
			Com_Printf( "SDL_Init( SDL_INIT_VIDEO ) FAILED (%s)\n", SDL_GetError() );
			return RSERR_FATAL_ERROR;
		}

		driverName = SDL_GetCurrentVideoDriver();

		Com_Printf( "SDL using driver \"%s\"\n", driverName );
	}

	err = GLW_SetMode( mode, modeFS, fullscreen, vulkan );

	switch ( err )
	{
		case RSERR_INVALID_FULLSCREEN:
			Com_Printf( "...WARNING: fullscreen unavailable in this mode\n" );
			return err;
		case RSERR_INVALID_MODE:
			Com_Printf( "...WARNING: could not set the given mode (%d)\n", mode );
			return err;
		default:
			break;
	}

	return RSERR_OK;
}


/*
===============
GLimp_Init

This routine is responsible for initializing the OS specific portions
of OpenGL
===============
*/
void GLimp_Init( glconfig_t *config )
{
	rserr_t err;

#ifndef _WIN32
	InitSig();
#endif

	Com_DPrintf( "GLimp_Init()\n" );

	glw_state.config = config; // feedback renderer configuration

	in_nograb = Cvar_Get( "in_nograb", "0", 0 );
	Cvar_SetDescription( in_nograb, "Do not capture mouse in game, may be useful during online streaming." );

	r_allowSoftwareGL = Cvar_Get( "r_allowSoftwareGL", "0", CVAR_LATCH );

	r_swapInterval = Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_stereoEnabled = Cvar_Get( "r_stereoEnabled", "0", CVAR_ARCHIVE | CVAR_LATCH );
	Cvar_SetDescription( r_stereoEnabled, "Enable stereo rendering for techniques like shutter glasses." );

	// Create the window and set up the context
	err = GLimp_StartDriverAndSetMode( r_mode->integer, r_modeFullscreen->string, GLW_CvarEnabled( r_fullscreen ), qfalse );
	if ( err != RSERR_OK )
	{
		if ( err == RSERR_FATAL_ERROR )
		{
			Com_Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem" );
			return;
		}

		if ( r_mode->integer != 3 || ( r_fullscreen->integer && atoi( r_modeFullscreen->string ) != 3 ) )
		{
			Com_Printf( "Setting \\r_mode %d failed, falling back on \\r_mode %d\n", r_mode->integer, 3 );
			if ( GLimp_StartDriverAndSetMode( 3, "", GLW_CvarEnabled( r_fullscreen ), qfalse ) != RSERR_OK )
			{
				// Nothing worked, give up
				Com_Error( ERR_FATAL, "GLimp_Init() - could not load OpenGL subsystem" );
				return;
			}
		}
	}

	// These values force the UI to disable driver selection
	config->driverType = GLDRV_ICD;
	config->hardwareType = GLHW_GENERIC;

	// This depends on SDL_INIT_VIDEO, hence having it here
	IN_Init();

	HandleEvents();

	Key_ClearStates();
}


/*
===============
GLimp_EndFrame

Responsible for doing a swapbuffers
===============
*/
void GLimp_EndFrame( void )
{
	// don't flip if drawing to front buffer
	if ( Q_stricmp( cl_drawBuffer->string, "GL_FRONT" ) != 0 )
	{
		SDL_GL_SwapWindow( SDL_window );
	}
}


/*
===============
GL_GetProcAddress

Used by opengl renderers to resolve all qgl* function pointers
===============
*/
void *GL_GetProcAddress( const char *symbol )
{
	return SDL_GL_GetProcAddress( symbol );
}

/*
===============
GLimp_QueryDisplayOutput

Returns the current window/display color-management and HDR hints exposed by
SDL. The renderer still owns policy: this is capability/state reporting only.
===============
*/
void GLimp_QueryDisplayOutput( rendererDisplayOutput_t *output )
{
	SDL_DisplayID display = 0;
	SDL_PropertiesID displayProps = 0;
	SDL_PropertiesID windowProps = 0;
	const char *driver;
	const char *displayName;
	size_t iccSize = 0;
	fnql::sdl::ScopedSdlMemory<void> iccProfile;
	float sdrWhite;
	float hdrHeadroom;
	qboolean windowHdr;
	qboolean displayHdr;

	GLW_InitDisplayOutput( output );
	if ( !output ) {
		return;
	}

	driver = SDL_GetCurrentVideoDriver();
	if ( driver && driver[0] ) {
		Q_strncpyz( output->videoDriver, driver, sizeof( output->videoDriver ) );
	}

	if ( !SDL_window ) {
		GLW_SetDisplayOutputReason( output, "No SDL window is active" );
		return;
	}

	output->valid = qtrue;

	display = SDL_GetDisplayForWindow( SDL_window );
	if ( !display ) {
		display = SDL_GetPrimaryDisplay();
	}
	output->displayIndex = (int)display;

	displayName = display ? SDL_GetDisplayName( display ) : NULL;
	if ( displayName && displayName[0] ) {
		Q_strncpyz( output->displayName, displayName, sizeof( output->displayName ) );
	} else if ( display ) {
		Com_sprintf( output->displayName, sizeof( output->displayName ), "SDL display %u", (unsigned)display );
	}

	if ( display ) {
		displayProps = SDL_GetDisplayProperties( display );
	}
	if ( displayProps ) {
		displayHdr = SDL_GetBooleanProperty( displayProps, SDL_PROP_DISPLAY_HDR_ENABLED_BOOLEAN, false ) ? qtrue : qfalse;
		if ( displayHdr ) {
			output->hdrEnabled = qtrue;
		}
#ifdef _WIN32
		if ( SDL_GetPointerProperty( displayProps, SDL_PROP_DISPLAY_WINDOWS_HMONITOR_POINTER, NULL ) ) {
			output->windowsScRgbSupported = glw_floatFramebufferActive ? qtrue : qfalse;
			output->windowsHdr10Supported = qfalse;
		}
#endif
#if !defined(_WIN32)
		if ( SDL_GetPointerProperty( displayProps, SDL_PROP_DISPLAY_WAYLAND_WL_OUTPUT_POINTER, NULL ) ) {
			output->waylandColorProtocol = qtrue;
		}
#endif
	}

	windowProps = SDL_GetWindowProperties( SDL_window );
	if ( windowProps ) {
		windowHdr = SDL_GetBooleanProperty( windowProps, SDL_PROP_WINDOW_HDR_ENABLED_BOOLEAN, false ) ? qtrue : qfalse;
		if ( windowHdr ) {
			output->hdrEnabled = qtrue;
		}

		sdrWhite = SDL_GetFloatProperty( windowProps, SDL_PROP_WINDOW_SDR_WHITE_LEVEL_FLOAT, 0.0f );
		if ( sdrWhite >= 80.0f ) {
			output->sdrWhiteNits = sdrWhite;
		}

		hdrHeadroom = SDL_GetFloatProperty( windowProps, SDL_PROP_WINDOW_HDR_HEADROOM_FLOAT, 1.0f );
		if ( hdrHeadroom > 1.0f ) {
			output->hdrHeadroomValid = qtrue;
			output->hdrHeadroom = hdrHeadroom;
			output->hdrEnabled = qtrue;
		}
	}

	iccProfile.reset( SDL_GetWindowICCProfile( SDL_window, &iccSize ) );
	if ( iccProfile ) {
		output->iccProfileAvailable = qtrue;
		output->iccProfileBytes = ( iccSize > INT_MAX ) ? INT_MAX : (int)iccSize;
	}

	if ( output->hdrHeadroomValid ) {
		output->maxLuminanceNits = output->sdrWhiteNits * output->hdrHeadroom;
		output->maxFullFrameLuminanceNits = output->maxLuminanceNits;
	} else if ( output->hdrEnabled ) {
		output->maxLuminanceNits = 1000.0f;
		output->maxFullFrameLuminanceNits = 400.0f;
	}

#if defined(_WIN32)
	if ( output->hdrEnabled && glw_floatFramebufferActive ) {
		output->windowsAdvancedColor = qtrue;
		output->windowsScRgbSupported = qtrue;
		output->nativeBackend = ROUTPUT_BACKEND_WINDOWS_SCRGB;
		GLW_SetDisplayOutputReason( output, "Windows HDR/Advanced Color headroom reported by SDL" );
	} else if ( output->hdrEnabled ) {
		GLW_SetDisplayOutputReason( output, "Windows HDR reported by SDL but the OpenGL framebuffer is not floating-point" );
	} else {
		GLW_SetDisplayOutputReason( output, "Windows display is SDR or SDL did not report HDR headroom" );
	}
#elif defined(MACOS_X) || defined(__APPLE__)
	if ( ( output->hdrEnabled || output->hdrHeadroomValid ) && glw_floatFramebufferActive ) {
		output->macosEdrSupported = qtrue;
		output->nativeBackend = ROUTPUT_BACKEND_MACOS_EDR;
		GLW_SetDisplayOutputReason( output, "Apple EDR headroom reported by SDL" );
	} else if ( output->hdrEnabled || output->hdrHeadroomValid ) {
		GLW_SetDisplayOutputReason( output, "Apple EDR headroom reported by SDL but the OpenGL framebuffer is not floating-point" );
	} else {
		GLW_SetDisplayOutputReason( output, "Apple display is SDR or SDL did not report EDR headroom" );
	}
#else
	if ( output->hdrEnabled && output->waylandColorProtocol && glw_floatFramebufferActive ) {
		output->linuxHdrExperimental = qtrue;
		output->explicitLinuxHdrProtocol = qtrue;
		output->nativeBackend = ROUTPUT_BACKEND_LINUX_EXPERIMENTAL_HDR;
		GLW_SetDisplayOutputReason( output, "Linux Wayland display reports HDR headroom; compositor HDR remains experimental" );
	} else if ( output->hdrEnabled && output->waylandColorProtocol ) {
		GLW_SetDisplayOutputReason( output, "Linux Wayland display reports HDR headroom but the OpenGL framebuffer is not floating-point" );
	} else if ( output->hdrEnabled ) {
		GLW_SetDisplayOutputReason( output, "Linux display reports HDR headroom without a validated compositor protocol" );
	} else {
		GLW_SetDisplayOutputReason( output, "Linux display is SDR or SDL did not report HDR headroom" );
	}
#endif
}


#ifdef USE_VULKAN_API
/*
===============
VKimp_Init

This routine is responsible for initializing the OS specific portions
of Vulkan
===============
*/
void VKimp_Init( glconfig_t *config )
{
	rserr_t err;

#ifndef _WIN32
	InitSig();
#endif

	Com_DPrintf( "VKimp_Init()\n" );

	in_nograb = Cvar_Get( "in_nograb", "0", CVAR_ARCHIVE );
	Cvar_SetDescription( in_nograb, "Do not capture mouse in game, may be useful during online streaming." );

	r_swapInterval = Cvar_Get( "r_swapInterval", "0", CVAR_ARCHIVE | CVAR_LATCH );
	r_stereoEnabled = Cvar_Get( "r_stereoEnabled", "0", CVAR_ARCHIVE | CVAR_LATCH );
	Cvar_SetDescription( r_stereoEnabled, "Enable stereo rendering for techniques like shutter glasses." );

	// feedback to renderer configuration
	glw_state.config = config;

	// Create the window and set up the context
	err = GLimp_StartDriverAndSetMode( r_mode->integer, r_modeFullscreen->string, GLW_CvarEnabled( r_fullscreen ), qtrue /* Vulkan */ );
	if ( err != RSERR_OK )
	{
		if ( err == RSERR_FATAL_ERROR )
		{
			Com_Error( ERR_FATAL, "VKimp_Init() - could not load Vulkan subsystem" );
			return;
		}

		Com_Printf( "Setting r_mode %d failed, falling back on r_mode %d\n", r_mode->integer, 3 );

		err = GLimp_StartDriverAndSetMode( 3, "", GLW_CvarEnabled( r_fullscreen ), qtrue /* Vulkan */ );
		if( err != RSERR_OK )
		{
			// Nothing worked, give up
			Com_Error( ERR_FATAL, "VKimp_Init() - could not load Vulkan subsystem" );
			return;
		}
	}

	qvkGetInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>( SDL_Vulkan_GetVkGetInstanceProcAddr() );

	if ( qvkGetInstanceProcAddr == NULL )
	{
		GLW_QuitVideoSubsystem();
		Com_Error( ERR_FATAL, "VKimp_Init: qvkGetInstanceProcAddr is NULL" );
	}

	// These values force the UI to disable driver selection
	config->driverType = GLDRV_ICD;
	config->hardwareType = GLHW_GENERIC;

	// This depends on SDL_INIT_VIDEO, hence having it here
	IN_Init();

	HandleEvents();

	Key_ClearStates();
}


/*
===============
VK_GetInstanceProcAddr
===============
*/
void *VK_GetInstanceProcAddr( VkInstance instance, const char *name )
{
	return reinterpret_cast<void *>( qvkGetInstanceProcAddr( instance, name ) );
}


/*
===============
VK_CreateSurface
===============
*/
qboolean VK_CreateSurface( VkInstance instance, VkSurfaceKHR *surface )
{
	if ( SDL_Vulkan_CreateSurface( SDL_window, instance, NULL, surface ) )
		return qtrue;
	else
		return qfalse;
}


/*
===============
VKimp_Shutdown
===============
*/
void VKimp_Shutdown( qboolean unloadDLL )
{
	const char* drv = SDL_GetCurrentVideoDriver();

	IN_Shutdown();

	if ( glw_state.isFullscreen ) {
		if ( drv && strcmp( drv, "x11" ) == 0 ) {
			SDL_WarpMouseGlobal( glw_state.desktop_width / 2, glw_state.desktop_height / 2 );
		} else {
			GLW_ShowCursor( qtrue );
		}
	}

	if ( SDL_window ) {
		GLW_DestroyWindow();
	}

	if ( unloadDLL )
		GLW_QuitVideoSubsystem();
}
#endif // USE_VULKAN_API


/*
================
GLW_HideFullscreenWindow
================
*/
void GLW_HideFullscreenWindow( void ) {
	if ( SDL_window && glw_state.isFullscreen ) {
		SDL_HideWindow( SDL_window );
	}
}


/*
===============
Sys_GetClipboardData
===============
*/
char *Sys_GetClipboardData( void )
{
#ifdef DEDICATED
	return NULL;
#else
	char *data = NULL;
	fnql::sdl::ScopedSdlMemory<char> cliptext( SDL_GetClipboardText() );

	if ( cliptext ) {
		if ( cliptext.get()[0] != '\0' ) {
			size_t bufsize = strlen( cliptext.get() ) + 1;

			data = static_cast<char *>( Z_Malloc( bufsize ) );
			Q_strncpyz( data, cliptext.get(), bufsize );

			// find first listed char and set to '\0'
			strtok( data, "\n\r\b" );
		}
	}
	return data;
#endif
}


/*
===============
Sys_SetClipboardData
===============
*/
void Sys_SetClipboardData( const char *text )
{
#ifndef DEDICATED
	SDL_SetClipboardText( text ? text : "" );
#else
	(void)text;
#endif
}


/*
===============
Sys_SetClipboardBitmap
===============
*/
void Sys_SetClipboardBitmap( const byte *bitmap, int length )
{
#ifdef _WIN32
	if ( !bitmap || length <= 0 ) {
		return;
	}

	fnql::win::ScopedClipboard clipboard( NULL );
	if ( !clipboard ) {
		return;
	}

	EmptyClipboard();

	fnql::win::ScopedGlobalMemory memory( GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, length ) );
	if ( !memory ) {
		return;
	}

	{
		fnql::win::ScopedGlobalLock<byte> lock( memory.get() );
		if ( !lock ) {
			return;
		}
		memcpy( lock.get(), bitmap, length );
	}

	if ( SetClipboardData( CF_DIB, memory.get() ) ) {
		memory.release();
	}
#endif
}
