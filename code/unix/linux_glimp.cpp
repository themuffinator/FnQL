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
/*
** GLW_IMP.C
**
** This file contains ALL Linux specific stuff having to do with the
** OpenGL refresh.  When a port is being made the following functions
** must be implemented by the port:
**
** GLimp_EndFrame
** GLimp_Init
** GLimp_Shutdown
** GLimp_SetGamma
**
*/

#include <termios.h>
#include <sys/ioctl.h>
#ifdef __linux__
  #include <sys/stat.h>
  #include <sys/vt.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <locale.h>

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "../client/client.h"
#include "../client/input_compat.hpp"
#include "../platform/window_placement.hpp"
#include "linux_local.h"
#include "unix_glw.h"

#ifdef USE_OPENGL_API
#include "../renderer/qgl.h"
#endif

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>

#include <X11/XKBlib.h>

#if !defined(__sun)
#include <X11/extensions/Xxf86dga.h>
#endif

#if defined(__sun)
#include <X11/Sunkeysym.h>
#endif

#ifdef _XF86DGA_H_
#define HAVE_XF86DGA
#endif

typedef enum
{
  RSERR_OK,

  RSERR_INVALID_FULLSCREEN,
  RSERR_INVALID_MODE,
  RSERR_FATAL_ERROR,

  RSERR_UNKNOWN
} rserr_t;

typedef struct motifHints_s
{
	unsigned long flags;
	unsigned long functions;
	unsigned long decorations;
	long input_mode;
	unsigned long status;
} motifHints_t;

glwstate_t glw_state;

/*
** GLimp_InvalidateConfig
**
** The renderer module owning the glconfig_t we feed back into is about to
** be unloaded: drop the pointer so nothing dereferences stale memory before
** the next GLimp_Init/VKimp_Init.
*/
void GLimp_InvalidateConfig( void )
{
	glw_state.config = NULL;
}

Display *dpy = NULL;
int scrnum;

Window win = 0;
#ifdef X_HAVE_UTF8_STRING
static XIM x11_input_method;
static XIC x11_input_context;
static qboolean x11_input_context_focused;
static qboolean x11_input_method_failure_reported;
static qboolean x11_input_method_reopen_pending;
#endif
#ifdef USE_OPENGL_API
static GLXContext ctx = NULL;
#endif
static Atom wmDeleteEvent = None;
static Atom motifWMHints = None;


static int window_width = 0;
static int window_height = 0;
static qboolean window_created;
static qboolean window_exposed;

#define KEY_MASK (KeyPressMask | KeyReleaseMask)
#define MOUSE_MASK (ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ButtonMotionMask )
#define X_MASK (KEY_MASK | MOUSE_MASK | VisibilityChangeMask | \
	StructureNotifyMask | FocusChangeMask | ExposureMask | PropertyChangeMask )

static qboolean mouse_avail;
static qboolean mouse_active = qfalse;
static Cursor invisible_cursor = None;
static qboolean window_cursor_valid = qfalse;
static qboolean window_cursor_shown = qtrue;
static qboolean absolute_position_valid = qfalse;
static int absolute_position_consumer = 0;
static int absolute_position_x;
static int absolute_position_y;
static unsigned int temporary_capture_buttons;
static unsigned int mouse_aux_button_state;

// X11 allows one pointer grab per client, so pointer confinement and the
// temporary drag capture share it and the grab is re-issued whenever the set of
// reasons changes.
#define POINTER_GRAB_CONFINE 0x1
#define POINTER_GRAB_DRAG    0x2
static int pointer_grab_reasons;
static qboolean gameplay_grab_failure_reported;
static qboolean keyboard_grabbed;
static qboolean keyboard_regrab_pending;

static int mwx, mwy;
static int mx = 0, my = 0;

// Time mouse was reset, we ignore the first 50ms of the mouse to allow settling of events
static int mouseResetTime = 0;
#define MOUSE_RESET_DELAY 50

static cvar_t *in_mouse;
static cvar_t *in_dgamouse; // user pref for dga mouse
static cvar_t *in_shiftedKeys; // obey modifiers for certain keys in non-console (comma, numbers, etc)

static cvar_t *in_subframe;
static cvar_t *in_nograb; // this is strictly for developers

cvar_t *in_forceCharset;

#ifdef USE_JOYSTICK
cvar_t   *in_joystick      = NULL;
cvar_t   *in_joystickDebug = NULL;
cvar_t   *joy_threshold    = NULL;
#endif

static int mouse_accel_numerator;
static int mouse_accel_denominator;
static int mouse_threshold;

static int win_x, win_y;

static constexpr int kPointerMenuMask = KEYCATCH_UI | KEYCATCH_CGAME | KEYCATCH_BROWSER;
static constexpr int kTextInputCatcherMask =
	KEYCATCH_CONSOLE | KEYCATCH_UI | KEYCATCH_MESSAGE |
	KEYCATCH_BROWSER;

using fnql::input::PointerMode;
using fnql::input::PointerOwner;

static PointerOwner absolute_pointer_owner = PointerOwner::Gameplay;
static unsigned int physical_modifier_state;
static int translated_key_by_keycode[256];
static qboolean suppressed_key_by_keycode[256];

enum {
	X11_MOD_LSHIFT = 1u << 0,
	X11_MOD_RSHIFT = 1u << 1,
	X11_MOD_LCTRL  = 1u << 2,
	X11_MOD_RCTRL  = 1u << 3,
	X11_MOD_LALT   = 1u << 4,
	X11_MOD_RALT   = 1u << 5,
	X11_MOD_LSUPER = 1u << 6,
	X11_MOD_RSUPER = 1u << 7,
	X11_MOD_LEVEL3 = 1u << 8,
	X11_MOD_MODE_SWITCH = 1u << 9
};


static unsigned int X11_ModifierSideBit( KeySym keysym )
{
	switch ( keysym ) {
		case XK_Shift_L:   return X11_MOD_LSHIFT;
		case XK_Shift_R:   return X11_MOD_RSHIFT;
		case XK_Control_L: return X11_MOD_LCTRL;
		case XK_Control_R: return X11_MOD_RCTRL;
		case XK_Alt_L:
		case XK_Meta_L:    return X11_MOD_LALT;
		case XK_Alt_R:
		case XK_Meta_R:    return X11_MOD_RALT;
		case XK_Super_L:     return X11_MOD_LSUPER;
		case XK_Super_R:     return X11_MOD_RSUPER;
		case XK_ISO_Level3_Shift: return X11_MOD_LEVEL3;
		case XK_Mode_switch: return X11_MOD_MODE_SWITCH;
		default:           return 0;
	}
}


static unsigned int X11_ModifierFamilyMask( unsigned int side )
{
	if ( side & ( X11_MOD_LSHIFT | X11_MOD_RSHIFT ) ) {
		return X11_MOD_LSHIFT | X11_MOD_RSHIFT;
	}
	if ( side & ( X11_MOD_LCTRL | X11_MOD_RCTRL ) ) {
		return X11_MOD_LCTRL | X11_MOD_RCTRL;
	}
	if ( side & ( X11_MOD_LALT | X11_MOD_RALT ) ) {
		return X11_MOD_LALT | X11_MOD_RALT;
	}
	if ( side & ( X11_MOD_LSUPER | X11_MOD_RSUPER ) ) {
		return X11_MOD_LSUPER | X11_MOD_RSUPER;
	}
	if ( side & ( X11_MOD_LEVEL3 | X11_MOD_MODE_SWITCH ) ) {
		return X11_MOD_LEVEL3 | X11_MOD_MODE_SWITCH;
	}
	return 0;
}


static qboolean X11_ShouldQueueModifierTransition(
	KeySym keysym, qboolean down )
{
	const unsigned int side = X11_ModifierSideBit( keysym );
	if ( !side ) {
		return qtrue;
	}

	const unsigned int family = X11_ModifierFamilyMask( side );
	const qboolean sideWasDown =
		( physical_modifier_state & side ) ? qtrue : qfalse;
	const qboolean familyWasDown =
		( physical_modifier_state & family ) ? qtrue : qfalse;
	if ( down ) {
		physical_modifier_state |= side;
		return ( !familyWasDown || sideWasDown ) ? qtrue : qfalse;
	} else {
		physical_modifier_state &= ~side;
	}
	return ( physical_modifier_state & family ) ? qfalse : qtrue;
}


static qboolean X11_KeySymIsDown( const char keymap[32], KeySym keysym )
{
	const KeyCode keycode = XKeysymToKeycode( dpy, keysym );
	return keycode && ( static_cast<unsigned char>( keymap[keycode >> 3] ) &
		( 1u << ( keycode & 7 ) ) )
		? qtrue : qfalse;
}


static unsigned int X11_ReadPhysicalModifiers( void )
{
	char keymap[32] = {};
	unsigned int state = 0;
	if ( !dpy ) {
		return state;
	}
	XQueryKeymap( dpy, keymap );

	if ( X11_KeySymIsDown( keymap, XK_Shift_L ) ) state |= X11_MOD_LSHIFT;
	if ( X11_KeySymIsDown( keymap, XK_Shift_R ) ) state |= X11_MOD_RSHIFT;
	if ( X11_KeySymIsDown( keymap, XK_Control_L ) ) state |= X11_MOD_LCTRL;
	if ( X11_KeySymIsDown( keymap, XK_Control_R ) ) state |= X11_MOD_RCTRL;
	if ( X11_KeySymIsDown( keymap, XK_Alt_L ) ||
		X11_KeySymIsDown( keymap, XK_Meta_L ) ) state |= X11_MOD_LALT;
	if ( X11_KeySymIsDown( keymap, XK_Alt_R ) ||
		X11_KeySymIsDown( keymap, XK_Meta_R ) ) state |= X11_MOD_RALT;
	if ( X11_KeySymIsDown( keymap, XK_Super_L ) ) state |= X11_MOD_LSUPER;
	if ( X11_KeySymIsDown( keymap, XK_Super_R ) ) state |= X11_MOD_RSUPER;
	if ( X11_KeySymIsDown(
			keymap, XK_ISO_Level3_Shift ) ) state |= X11_MOD_LEVEL3;
	if ( X11_KeySymIsDown(
			keymap, XK_Mode_switch ) ) state |= X11_MOD_MODE_SWITCH;
	return state;
}


static void X11_QueueHeldModifiers( int eventTime )
{
	physical_modifier_state =
		gw_active ? X11_ReadPhysicalModifiers() : 0;

	if ( physical_modifier_state & ( X11_MOD_LSHIFT | X11_MOD_RSHIFT ) ) {
		Sys_QueEvent( eventTime, SE_KEY, K_SHIFT, qtrue, 0, NULL );
	}
	if ( physical_modifier_state & ( X11_MOD_LCTRL | X11_MOD_RCTRL ) ) {
		Sys_QueEvent( eventTime, SE_KEY, K_CTRL, qtrue, 0, NULL );
	}
	if ( physical_modifier_state & ( X11_MOD_LALT | X11_MOD_RALT ) ) {
		Sys_QueEvent( eventTime, SE_KEY, K_ALT, qtrue, 0, NULL );
	}
	if ( physical_modifier_state & ( X11_MOD_LSUPER | X11_MOD_RSUPER ) ) {
		Sys_QueEvent( eventTime, SE_KEY, K_SUPER, qtrue, 0, NULL );
	}
	if ( physical_modifier_state &
		( X11_MOD_LEVEL3 | X11_MOD_MODE_SWITCH ) ) {
		Sys_QueEvent( eventTime, SE_KEY, K_MODE, qtrue, 0, NULL );
	}
}

static void X11_ResetInputContext( void );


/*
================
X11_TextInputOwnerActive

XIM composition belongs to the character-input lane, not to gameplay key
translation. Keeping the XIC focused while no text consumer is active lets an
input method filter compose/dead-key presses before the engine can bind them.
================
*/
static qboolean X11_TextInputOwnerActive( void )
{
	if ( !gw_active || gw_minimized ) {
		return qfalse;
	}

	return ( cls.state == CA_DISCONNECTED ||
		( Key_GetCatcher() & kTextInputCatcherMask ) ) ? qtrue : qfalse;
}


/*
================
X11_PrepareInputReset

Clear platform-side identity immediately before appending an ordered client
reset. A release after focus loss or window recreation must not target a key
translated in the previous context.
================
*/
static void X11_PrepareInputReset( void )
{
	// Reset producer caches before placing the ordered client barrier. XPending
	// may expose newer native events in the same drain, and those transitions
	// must rebuild state that barrier consumption will leave untouched.
	IN_ResetInputState();
	memset( translated_key_by_keycode, 0,
		sizeof( translated_key_by_keycode ) );
	memset( suppressed_key_by_keycode, 0,
		sizeof( suppressed_key_by_keycode ) );
	physical_modifier_state = 0;
	mouse_aux_button_state = 0;
	X11_ResetInputContext();
}


/*
================
X11_QueueInputReset

Append one ordered reset after clearing native producer identity. A caller may
request held modifiers to be rebuilt, but they are never reasserted while the
window is inactive or minimized.
================
*/
void X11_QueueInputReset( qboolean rebuildModifiers )
{
	X11_PrepareInputReset();
	Sys_QueEvent( 0, SE_INPUT_RESET, 0, 0, 0, NULL );
	if ( rebuildModifiers && gw_active && !gw_minimized ) {
		X11_QueueHeldModifiers( 0 );
	}
}


#ifdef X_HAVE_UTF8_STRING
/*
================
X11_SelectInputStyle

FnQL has no native preedit/status widgets in the game window. Prefer styles
that require no engine-owned preedit/status surfaces, and reject callback/area
styles that the engine cannot present safely.
================
*/
static XIMStyle X11_SelectInputStyle( const XIMStyles *styles )
{
	static const XIMStyle preferredStyles[] = {
		XIMPreeditNothing | XIMStatusNothing,
		XIMPreeditNone | XIMStatusNone,
		XIMPreeditNothing | XIMStatusNone,
		XIMPreeditNone | XIMStatusNothing
	};

	if ( !styles ) {
		return 0;
	}

	for ( const XIMStyle preferred : preferredStyles ) {
		for ( unsigned short i = 0; i < styles->count_styles; ++i ) {
			if ( styles->supported_styles[i] == preferred ) {
				return preferred;
			}
		}
	}
	return 0;
}


static void X11_SetInputContextFocus( qboolean focused )
{
	if ( !x11_input_context || x11_input_context_focused == focused ) {
		return;
	}

	if ( focused ) {
		XSetICFocus( x11_input_context );
	} else {
		XUnsetICFocus( x11_input_context );
	}
	x11_input_context_focused = focused;
}


static void X11_DestroyInputContext( void )
{
	if ( x11_input_context ) {
		X11_SetInputContextFocus( qfalse );
		XDestroyIC( x11_input_context );
		x11_input_context = NULL;
	}
	x11_input_context_focused = qfalse;
}


static void X11_ResetInputContext( void )
{
	if ( x11_input_context ) {
		char *discardedText = Xutf8ResetIC( x11_input_context );
		if ( discardedText ) {
			XFree( discardedText );
		}
	}
}


static void X11_InputMethodDestroyed(
	XIM inputMethod, XPointer, XPointer )
{
	if ( inputMethod != x11_input_method ) {
		return;
	}

	// Xlib owns destruction of every associated XIC and the XIM after this
	// callback returns. Drop our borrowed handles without closing either one.
	x11_input_context = NULL;
	x11_input_context_focused = qfalse;
	x11_input_method = NULL;
	x11_input_method_reopen_pending = qtrue;
}


static void X11_CloseInputMethod( void )
{
	X11_DestroyInputContext();
	if ( x11_input_method ) {
		XCloseIM( x11_input_method );
		x11_input_method = NULL;
	}
	x11_input_method_failure_reported = qfalse;
	x11_input_method_reopen_pending = qfalse;
}


static qboolean X11_OpenInputMethod( void )
{
	XIMCallback destroyCallback = {};

	if ( x11_input_method ) {
		return qtrue;
	}
	if ( !dpy ) {
		return qfalse;
	}

	if ( !setlocale( LC_CTYPE, "" ) || !XSupportsLocale() ) {
		if ( !x11_input_method_failure_reported ) {
			Com_DPrintf(
				"X11 locale input is unavailable; using legacy key text\n" );
			x11_input_method_failure_reported = qtrue;
		}
		return qfalse;
	}

	// Honour the user's configured input method first. If its server is not
	// reachable, Xlib's local IM still provides locale-aware UTF-8 lookup.
	if ( XSetLocaleModifiers( "" ) ) {
		x11_input_method = XOpenIM( dpy, NULL, NULL, NULL );
	}
	if ( !x11_input_method && XSetLocaleModifiers( "@im=none" ) ) {
		x11_input_method = XOpenIM( dpy, NULL, NULL, NULL );
	}
	if ( !x11_input_method ) {
		if ( !x11_input_method_failure_reported ) {
			Com_DPrintf(
				"XOpenIM failed; using legacy XLookupString text\n" );
			x11_input_method_failure_reported = qtrue;
		}
		return qfalse;
	}

	destroyCallback.callback = X11_InputMethodDestroyed;
	if ( XSetIMValues( x11_input_method,
			XNDestroyCallback, &destroyCallback, NULL ) != NULL ) {
		Com_DPrintf(
			"X11 input method has no destroy notification; recovery may require vid_restart\n" );
	}

	x11_input_method_failure_reported = qfalse;
	x11_input_method_reopen_pending = qfalse;
	return qtrue;
}


/*
================
X11_CreateInputContext

The XIM belongs to the Display and may survive a renderer-window recreation;
the XIC belongs to one Window and is rebuilt for every new native window.
================
*/
static void X11_CreateInputContext( void )
{
	XIMStyles *styles = NULL;
	XIMStyle style;
	long filterEvents = 0;

	X11_DestroyInputContext();
	if ( !dpy || !win || !X11_OpenInputMethod() ) {
		return;
	}

	if ( XGetIMValues( x11_input_method,
			XNQueryInputStyle, &styles, NULL ) != NULL || !styles ) {
		Com_DPrintf(
			"X11 input method exposes no usable input styles; using legacy key text\n" );
		return;
	}

	style = X11_SelectInputStyle( styles );
	XFree( styles );
	if ( !style ) {
		Com_DPrintf(
			"X11 input method requires unsupported preedit/status UI; using legacy key text\n" );
		return;
	}

	x11_input_context = XCreateIC( x11_input_method,
		XNInputStyle, style,
		XNClientWindow, win,
		XNFocusWindow, win,
		NULL );
	if ( !x11_input_context ) {
		Com_DPrintf(
			"XCreateIC failed; using legacy XLookupString text\n" );
		return;
	}

	if ( XGetICValues( x11_input_context,
			XNFilterEvents, &filterEvents, NULL ) == NULL ) {
		XSelectInput( dpy, win, X_MASK | filterEvents );
	}
	X11_SetInputContextFocus( X11_TextInputOwnerActive() );
}


static void X11_ReopenInputMethodIfNeeded( void )
{
	if ( !x11_input_method_reopen_pending ) {
		return;
	}

	x11_input_method_reopen_pending = qfalse;
	if ( dpy && win ) {
		X11_CreateInputContext();
	}
}


static qboolean X11_FilterInputEvent( XEvent *event )
{
	// Focus transitions are engine ownership changes even if an IM would also
	// like to observe them. The handler below explicitly focuses/unfocuses the
	// XIC, so never let XFilterEvent hide these transitions from the engine.
	if ( !x11_input_context || !x11_input_context_focused ||
		event->type == FocusIn || event->type == FocusOut ||
		!XFilterEvent( event, None ) ) {
		return qfalse;
	}

	// Xlib requires a client-owned keyboard grab to be released when an input
	// method consumes an event. Restore it after this X event batch once no
	// text-input catcher owns the keyboard.
	if ( keyboard_grabbed ) {
		XUngrabKeyboard( dpy, CurrentTime );
		XFlush( dpy );
		keyboard_grabbed = qfalse;
		keyboard_regrab_pending = qtrue;
	}
	return qtrue;
}


static void X11_QueueUtf8Text(
	const char *text, int textLength, int eventTime )
{
	const unsigned char *bytes =
		reinterpret_cast<const unsigned char *>( text );
	std::size_t remaining = textLength > 0
		? static_cast<std::size_t>( textLength ) : 0;
	qboolean reportedMalformed = qfalse;

	while ( remaining > 0 ) {
		const fnql::input::Utf8DecodeResult decoded =
			fnql::input::DecodeUtf8( bytes, remaining );
		bytes += decoded.size;
		remaining -= decoded.size;

		if ( !decoded.valid || decoded.codepoint == 0 ) {
			if ( !reportedMalformed ) {
				Com_DPrintf( "Ignoring malformed XIM UTF-8 text input\n" );
				reportedMalformed = qtrue;
			}
			continue;
		}
		Sys_QueEvent( eventTime, SE_CHAR,
			static_cast<int>( decoded.codepoint ), 0, 0, NULL );
	}
}


/*
================
X11_QueueInputMethodText

Returns qtrue whenever an active XIC handled the text lane, including a valid
key event that commits no characters. This prevents a dead/preedit key from
falling through and producing a duplicate legacy byte.
================
*/
static qboolean X11_QueueInputMethodText(
	XKeyEvent *event, int eventTime )
{
	char stackBuffer[64];
	char *buffer = stackBuffer;
	int bufferSize = static_cast<int>( sizeof( stackBuffer ) );
	int textLength;
	KeySym keysym = NoSymbol;
	Status status = XLookupNone;
	qboolean allocated = qfalse;

	if ( !x11_input_context || !x11_input_context_focused ) {
		return qfalse;
	}

	textLength = Xutf8LookupString( x11_input_context, event,
		buffer, bufferSize, &keysym, &status );
	if ( status == XBufferOverflow ) {
		if ( textLength <= 0 || textLength > MAX_EDIT_LINE * 4 ) {
			Com_DPrintf( "Ignoring oversized XIM text commit (%d bytes)\n",
				textLength );
			return qtrue;
		}

		bufferSize = textLength;
		buffer = static_cast<char *>( Z_Malloc( bufferSize ) );
		allocated = qtrue;
		textLength = Xutf8LookupString( x11_input_context, event,
			buffer, bufferSize, &keysym, &status );
	}

	if ( ( status == XLookupChars || status == XLookupBoth ) &&
		textLength > 0 && textLength <= bufferSize ) {
		X11_QueueUtf8Text( buffer, textLength, eventTime );
	}
	if ( allocated ) {
		Z_Free( buffer );
	}
	return qtrue;
}
#else
static void X11_SetInputContextFocus( qboolean ) {}
static void X11_DestroyInputContext( void ) {}
static void X11_ResetInputContext( void ) {}
static void X11_CloseInputMethod( void ) {}
static void X11_CreateInputContext( void ) {}
static void X11_ReopenInputMethodIfNeeded( void ) {}
static qboolean X11_FilterInputEvent( XEvent * ) { return qfalse; }
static qboolean X11_QueueInputMethodText(
	XKeyEvent *, int ) { return qfalse; }
#endif


/*
================
IN_ConsoleUsesAbsolutePointer

A fullscreen X11 window on a multi-monitor desktop still leaves the rest of the
desktop reachable, so the console keeps its absolute cursor there. On a single
display the fullscreen console keeps the relative gameplay pointer, matching the
SDL and Win32 backends.
================
*/
static qboolean IN_ConsoleUsesAbsolutePointer( void )
{
	return ( !glw_state.cdsFullscreen || glw_state.monitorCount > 1 ) ? qtrue : qfalse;
}

/*
================
IN_AbsolutePointerOwnerKind

Console toggling preserves any underlying UI/browser/cgame catcher, so the
overlay on top owns the pointer. Shared with the SDL and Win32 backends through
input_compat.hpp.
================
*/
static PointerOwner IN_AbsolutePointerOwnerKind( void )
{
	fnql::input::PointerOwnerInputs inputs;

	inputs.catcher = Key_GetCatcher();
	inputs.consoleMask = KEYCATCH_CONSOLE;
	inputs.menuMask = kPointerMenuMask;
	inputs.consoleUsesAbsolutePointer = IN_ConsoleUsesAbsolutePointer() ? true : false;

	return fnql::input::ResolvePointerOwner( inputs );
}

static PointerMode IN_ResolvePointerMode( PointerOwner owner )
{
	fnql::input::PointerModeInputs inputs;

	inputs.owner = owner;
	inputs.focused = gw_active ? true : false;
	inputs.minimized = gw_minimized ? true : false;
	inputs.fullscreen = glw_state.cdsFullscreen ? true : false;
	inputs.relativeAvailable = mouse_avail ? true : false;

	return fnql::input::ResolvePointerMode( inputs );
}

static qboolean IN_AbsolutePointerOwner( void )
{
	return fnql::input::PointerOwnerReportsAbsolute( IN_AbsolutePointerOwnerKind() )
		? qtrue : qfalse;
}


static int IN_PointerConsumerIdentity( void )
{
	const int catcher = Key_GetCatcher();

	if ( catcher & KEYCATCH_CONSOLE ) return KEYCATCH_CONSOLE;
	if ( catcher & KEYCATCH_BROWSER ) return KEYCATCH_BROWSER;
	if ( catcher & KEYCATCH_UI ) return KEYCATCH_UI;
	if ( catcher & KEYCATCH_CGAME ) return KEYCATCH_CGAME;
	return 0;
}

/*****************************************************************************
** KEYBOARD
** NOTE TTimo the keyboard handling is done with KeySyms
**   that means relying on the keyboard mapping provided by X
**   in-game it would probably be better to use KeyCode (i.e. hardware key codes)
**   you would still need the KeySyms in some cases, such as for the console and all entry textboxes
**     (cause there's nothing worse than a qwerty mapping on a french keyboard)
**
** you can turn on some debugging and verbose of the keyboard code with #define KBD_DBG
******************************************************************************/

//#define KBD_DBG
static const char s_keytochar[ 128 ] =
{
//0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F 
 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  '1',  '2',  '3',  '4',  '5',  '6',  // 0
 '7',  '8',  '9',  '0',  '-',  '=',  0x8,  0x9,  'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  // 1
 'o',  'p',  '[',  ']',  0x0,  0x0,  'a',  's',  'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  // 2
 '\'', 0x0,  0x0,  '\\', 'z',  'x',  'c',  'v',  'b',  'n',  'm',  ',',  '.',  '/',  0x0,  '*',  // 3

//0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F 
 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  '!',  '@',  '#',  '$',  '%',  '^',  // 4
 '&',  '*',  '(',  ')',  '_',  '+',  0x8,  0x9,  'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  // 5
 'O',  'P',  '{',  '}',  0x0,  0x0,  'A',  'S',  'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  // 6
 '"',  0x0,  0x0,  '|',  'Z',  'X',  'C',  'V',  'B',  'N',  'M',  '<',  '>',  '?',  0x0,  '*',  // 7
};

void IN_ActivateMouse( void );
void IN_DeactivateMouse( void );
qboolean IN_MouseActive( void );


static char *XLateKey( XKeyEvent *ev, int *key, int *textLength )
{
  static unsigned char buf[64];
  static unsigned char bufnomod[2];
  KeySym keysym;
  int lookupLength;
  int unmodifiedLength = 0;

  *key = 0;

  lookupLength = XLookupString(ev, (char*)buf, sizeof(buf), &keysym, 0);
  if ( lookupLength < 0 ) {
    lookupLength = 0;
  } else if ( lookupLength > (int)sizeof(buf) ) {
    lookupLength = sizeof(buf);
  }
  *textLength = lookupLength;
#ifdef KBD_DBG
  Com_Printf( "XLookupString ret: %d buf: %.*s keysym: %x\n",
    lookupLength, lookupLength, buf, (int)keysym );
#endif

  if (!in_shiftedKeys->integer) {
    // also get a buffer without modifiers held
    ev->state = 0;
    unmodifiedLength = XLookupString(ev, (char*)bufnomod, sizeof(bufnomod), &keysym, 0);
    if ( unmodifiedLength < 0 ) {
      unmodifiedLength = 0;
    } else if ( unmodifiedLength > (int)sizeof(bufnomod) ) {
      unmodifiedLength = sizeof(bufnomod);
    }
#ifdef KBD_DBG
    Com_Printf( "XLookupString (minus modifiers) ret: %d buf: %.*s keysym: %x\n",
      unmodifiedLength, unmodifiedLength, bufnomod, (int)keysym );
#endif
  }

  switch (keysym)
  {
  case XK_grave:
  case XK_twosuperior:
    *key = K_CONSOLE;
    *textLength = 0;
    return (char*)buf;

  case XK_KP_Page_Up:
  case XK_KP_9:  *key = K_KP_PGUP; break;
  case XK_Page_Up:   *key = K_PGUP; break;

  case XK_KP_Page_Down:
  case XK_KP_3: *key = K_KP_PGDN; break;
  case XK_Page_Down:   *key = K_PGDN; break;

  case XK_KP_Home: *key = K_KP_HOME; break;
  case XK_KP_7: *key = K_KP_HOME; break;
  case XK_Home:  *key = K_HOME; break;

  case XK_KP_End:
  case XK_KP_1:   *key = K_KP_END; break;
  case XK_End:   *key = K_END; break;

  case XK_KP_Left: *key = K_KP_LEFTARROW; break;
  case XK_KP_4: *key = K_KP_LEFTARROW; break;
  case XK_Left:  *key = K_LEFTARROW; break;

  case XK_KP_Right: *key = K_KP_RIGHTARROW; break;
  case XK_KP_6: *key = K_KP_RIGHTARROW; break;
  case XK_Right:  *key = K_RIGHTARROW;    break;

  case XK_KP_Down:
  case XK_KP_2:  if ( Key_GetCatcher() &&
                       ( lookupLength > 0 || unmodifiedLength > 0 ) )
                   *key = 0;
                 else
                   *key = K_KP_DOWNARROW;
                 break;

  case XK_Down:  *key = K_DOWNARROW; break;

  case XK_KP_Up:
  case XK_KP_8:  if ( Key_GetCatcher() &&
                       ( lookupLength > 0 || unmodifiedLength > 0 ) )
                   *key = 0;
                 else
                   *key = K_KP_UPARROW;
                 break;

  case XK_Up:    *key = K_UPARROW;   break;

  case XK_Escape: *key = K_ESCAPE;    break;

  case XK_KP_Enter: *key = K_KP_ENTER;  break;
  case XK_Return: *key = K_ENTER;    break;

  case XK_Tab:    *key = K_TAB;      break;

  case XK_F1:    *key = K_F1;       break;

  case XK_F2:    *key = K_F2;       break;

  case XK_F3:    *key = K_F3;       break;

  case XK_F4:    *key = K_F4;       break;

  case XK_F5:    *key = K_F5;       break;

  case XK_F6:    *key = K_F6;       break;

  case XK_F7:    *key = K_F7;       break;

  case XK_F8:    *key = K_F8;       break;

  case XK_F9:    *key = K_F9;       break;

  case XK_F10:    *key = K_F10;      break;

  case XK_F11:    *key = K_F11;      break;

  case XK_F12:    *key = K_F12;      break;

    // bk001206 - from Ryan's Fakk2
    //case XK_BackSpace: *key = 8; break; // ctrl-h
  case XK_BackSpace: *key = K_BACKSPACE; break; // ctrl-h

  case XK_KP_Delete:
  case XK_KP_Decimal: *key = K_KP_DEL; break;
  case XK_Delete: *key = K_DEL; break;

  case XK_Pause:  *key = K_PAUSE;    break;

  case XK_Shift_L:
  case XK_Shift_R:  *key = K_SHIFT;   break;

  case XK_Execute:
  case XK_Control_L:
  case XK_Control_R:  *key = K_CTRL;  break;

  case XK_Alt_L:
  case XK_Meta_L:
  case XK_Alt_R:
  case XK_Meta_R: *key = K_ALT;     break;
  case XK_ISO_Level3_Shift:
  case XK_Mode_switch: *key = K_MODE; break;

  case XK_KP_Begin: *key = K_KP_5;  break;

  case XK_Insert:   *key = K_INS; break;
  case XK_KP_Insert:
  case XK_KP_0: *key = K_KP_INS; break;

  case XK_KP_Multiply: *key = '*'; break;
  case XK_KP_Equal: *key = K_KP_EQUALS; break;
  case XK_KP_Add:  *key = K_KP_PLUS; break;
  case XK_KP_Subtract: *key = K_KP_MINUS; break;
  case XK_KP_Divide: *key = K_KP_SLASH; break;

  case XK_exclam: *key = '1'; break;
  case XK_at: *key = '2'; break;
  case XK_numbersign: *key = '3'; break;
  case XK_dollar: *key = '4'; break;
  case XK_percent: *key = '5'; break;
  case XK_asciicircum: *key = '6'; break;
  case XK_ampersand: *key = '7'; break;
  case XK_asterisk: *key = '8'; break;
  case XK_parenleft: *key = '9'; break;
  case XK_parenright: *key = '0'; break;

  // weird french keyboards ..
  // NOTE: console toggle is hardcoded in cl_keys.c, can't be unbound
  //   cleaner would be .. using hardware key codes instead of the key syms
  //   could also add a new K_KP_CONSOLE
  //case XK_twosuperior: *key = '~'; break;

  case XK_space:
  case XK_KP_Space: *key = K_SPACE; break;

  case XK_Menu:	*key = K_MENU; break;
  case XK_Multi_key: *key = K_COMPOSE; break;
  case XK_Help: *key = K_HELP; break;
  case XK_Print: *key = K_PRINT; break;
  case XK_Sys_Req: *key = K_SYSREQ; break;
  case XK_Break: *key = K_BREAK; break;
  case XK_Undo: *key = K_UNDO; break;
  case XK_Super_L:
  case XK_Super_R: *key = K_SUPER; break;
  case XK_Num_Lock: *key = K_KP_NUMLOCK; break;
  case XK_Caps_Lock: *key = K_CAPSLOCK; break;
  case XK_Scroll_Lock: *key = K_SCROLLOCK; break;
  case XK_backslash: *key = '\\'; break;

  default:
    //Com_Printf( "unknown keysym: %08X\n", keysym );
    if ( ( in_shiftedKeys->integer && lookupLength == 0 ) ||
      ( !in_shiftedKeys->integer && unmodifiedLength == 0 ) )
    {
      if (com_developer->value)
      {
        Com_Printf( "Warning: XLookupString failed on KeySym %d\n", (int)keysym );
      }
      *textLength = 0;
      return (char*)buf;
    }
    else
    {
      // XK_* tests failed, but XLookupString got a buffer, so let's try it
      if (in_shiftedKeys->integer) {
        *key = *(unsigned char *)buf;
        if (*key >= 'A' && *key <= 'Z')
          *key = *key - 'A' + 'a';
        // if ctrl is pressed, the keys are not between 'A' and 'Z', for instance ctrl-z == 26 ^Z ^C etc.
        // see https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=19
        else if (*key >= 1 && *key <= 26)
          *key = *key + 'a' - 1;
      } else {
        *key = bufnomod[0];
      }
    }
    break;
  }

  return (char*)buf;
}


// ========================================================================
// makes a null cursor
// ========================================================================

static Cursor CreateNullCursor( Display *display, Window root )
{
	Pixmap cursormask;
	XGCValues xgc;
	GC gc;
	XColor dummycolour;
	Cursor cursor;

	cursormask = XCreatePixmap( display, root, 1, 1, 1/*depth*/ );
	xgc.function = GXclear;
	gc = XCreateGC( display, cursormask, GCFunction, &xgc );
	XFillRectangle( display, cursormask, gc, 0, 0, 1, 1 );
	dummycolour.pixel = 0;
	dummycolour.red = 0;
	dummycolour.flags = 04;
	cursor = XCreatePixmapCursor( display, cursormask, cursormask, &dummycolour, &dummycolour, 0, 0 );
	XFreePixmap( display, cursormask );
	XFreeGC( display, gc );
	return cursor;
}

/*
================
IN_ShowWindowCursor

Latched: IN_Frame evaluates the wanted cursor state every frame and this would
otherwise cost an X round trip per frame while any overlay is open.
================
*/
static void IN_ShowWindowCursor( qboolean show )
{
	if ( !dpy || !win ) {
		return;
	}
	if ( window_cursor_valid && window_cursor_shown == show ) {
		return;
	}

	if ( show ) {
		XUndefineCursor( dpy, win );
	} else {
		if ( invisible_cursor == None ) {
			invisible_cursor = CreateNullCursor( dpy, win );
		}
		XDefineCursor( dpy, win, invisible_cursor );
	}

	window_cursor_shown = show;
	window_cursor_valid = qtrue;
}


/*
================
IN_ApplyPointerGrab

Confinement and drag capture share the single X11 client pointer grab.
Confinement needs confine_to=win; a drag over an unconfined pointer must not
confine, so the grab is re-issued whenever the reason set changes. A failed
grab leaves the previous reasons in place so the next frame retries.
================
*/
static void IN_ApplyPointerGrab( int reasons )
{
	Window confineTo;
	int result;

	if ( !dpy || !win ) {
		pointer_grab_reasons = 0;
		return;
	}

	if ( reasons == pointer_grab_reasons ) {
		return;
	}

	if ( !reasons ) {
		XUngrabPointer( dpy, CurrentTime );
		pointer_grab_reasons = 0;
		return;
	}

	confineTo = ( reasons & POINTER_GRAB_CONFINE ) ? win : None;
	result = XGrabPointer( dpy, win, True, MOUSE_MASK,
		GrabModeAsync, GrabModeAsync, confineTo, None, CurrentTime );
	if ( result != GrabSuccess ) {
		Com_DPrintf( "X11 pointer grab failed: %d\n", result );
		return;
	}

	pointer_grab_reasons = reasons;
}


/*
================
IN_SetPointerConfinement

A fullscreen window has no desktop edge to stop a freely moving overlay pointer,
so without a confining grab it walks onto another display and a click there
drops the game out of focus mid-menu.
================
*/
static void IN_SetPointerConfinement( qboolean confine )
{
	if ( confine ) {
		IN_ApplyPointerGrab( pointer_grab_reasons | POINTER_GRAB_CONFINE );
	} else {
		IN_ApplyPointerGrab( pointer_grab_reasons & ~POINTER_GRAB_CONFINE );
	}
}


static void IN_QueueAbsolutePointerPosition(
	int eventTime, int x, int y, qboolean force = qfalse )
{
	const int consumer = IN_PointerConsumerIdentity();
	// X11 reports window coordinates, which are the renderer's drawable pixels
	// only while the renderer resolution matches the window. Every absolute
	// consumer - the console, the WebUI browser, and retail's UI and cgame
	// overlays - works in drawable pixels, so project once here.
	fnql::input::PointerProjection projection;
	fnql::input::PointerPosition position;

	projection.hostWidth = window_width;
	projection.hostHeight = window_height;
	projection.drawableWidth = cls.glconfig.vidWidth;
	projection.drawableHeight = cls.glconfig.vidHeight;
	position = fnql::input::ProjectPointerToDrawable( x, y, projection );

	if ( !force && absolute_position_valid
		&& consumer == absolute_position_consumer
		&& position.x == absolute_position_x
		&& position.y == absolute_position_y ) {
		return;
	}
	if ( !force ) {
		absolute_position_valid = qtrue;
		absolute_position_consumer = consumer;
		absolute_position_x = position.x;
		absolute_position_y = position.y;
	}
	Sys_QueEvent( eventTime, SE_MOUSE_ABSOLUTE, position.x, position.y, 0, NULL );
}


static void IN_BeginTemporaryPointerCapture( unsigned int button );
static void IN_EndTemporaryPointerCapture( void );


static void IN_QueueMouseReset( int eventTime )
{
	Sys_QueEvent( eventTime, SE_MOUSE_RESET,
		static_cast<int>( mouse_aux_button_state ), 0, 0, NULL );
	mouse_aux_button_state = 0;
}


/*
================
IN_ReconcileAbsolutePointerButtons

XQueryPointer supplies position and the physical core-button snapshot in one
server round trip. Use that snapshot to recover from a lost release and to
establish a drag grab when an absolute-pointer owner takes over while a button
is already held.
================
*/
static void IN_ReconcileAbsolutePointerButtons(
	unsigned int physicalState, int eventTime )
{
	struct CoreButton {
		unsigned int button;
		unsigned int physicalMask;
		int key;
	};
	static const CoreButton coreButtons[] = {
		{ Button1, Button1Mask, K_MOUSE1 },
		{ Button2, Button2Mask, K_MOUSE3 },
		{ Button3, Button3Mask, K_MOUSE2 }
	};
	qboolean lostRelease = qfalse;

	for ( const CoreButton &button : coreButtons ) {
		const qboolean physicalDown =
			( physicalState & button.physicalMask ) ? qtrue : qfalse;
		const qboolean logicalDown = Key_IsDown( button.key );
		if ( logicalDown && !physicalDown ) {
			lostRelease = qtrue;
			break;
		}
	}

	if ( lostRelease ) {
		// A grab can disappear without delivering its final release. Balance
		// every logical mouse button before accepting another overlay drag.
		IN_QueueMouseReset( eventTime );
		IN_EndTemporaryPointerCapture();
		return;
	}

	for ( const CoreButton &button : coreButtons ) {
		if ( ( physicalState & button.physicalMask ) &&
			Key_IsDown( button.key ) ) {
			// A failed XGrabPointer leaves no temporary button latch, so this
			// naturally retries on the next successful pointer snapshot.
			IN_BeginTemporaryPointerCapture( button.button );
		}
	}
}


static void IN_PollAbsolutePointerPosition( void )
{
	Window root;
	Window child;
	int rootX;
	int rootY;
	int windowX;
	int windowY;
	unsigned int mask;

	if ( dpy && win && XQueryPointer( dpy, win, &root, &child, &rootX, &rootY,
		&windowX, &windowY, &mask ) ) {
		const int eventTime = Sys_Milliseconds();
		IN_ReconcileAbsolutePointerButtons( mask, eventTime );
		IN_QueueAbsolutePointerPosition( eventTime, windowX, windowY );
	}
}


static unsigned int IN_PointerButtonMask( unsigned int button )
{
	if ( button == 0 || button > sizeof( temporary_capture_buttons ) * 8 ) {
		return 0;
	}
	return 1u << ( button - 1 );
}


static void IN_BeginTemporaryPointerCapture( unsigned int button )
{
	const unsigned int buttonMask = IN_PointerButtonMask( button );
	// X11 commonly exposes vertical and horizontal wheel ticks as buttons 4-7;
	// they may be press-only, so never let them establish a drag capture.
	if ( !dpy || !win || !buttonMask || ( button >= 4 && button <= 7 ) ) {
		return;
	}

	if ( !temporary_capture_buttons ) {
		IN_ApplyPointerGrab( pointer_grab_reasons | POINTER_GRAB_DRAG );
		if ( !( pointer_grab_reasons & POINTER_GRAB_DRAG ) ) {
			return;
		}
	}
	temporary_capture_buttons |= buttonMask;
}


static void IN_EndTemporaryPointerCapture( void )
{
	if ( temporary_capture_buttons ) {
		temporary_capture_buttons = 0;
	}
	// Confinement, if any, survives the drag.
	IN_ApplyPointerGrab( pointer_grab_reasons & ~POINTER_GRAB_DRAG );
}


static void IN_ReleaseTemporaryPointerButton( unsigned int button )
{
	if ( !temporary_capture_buttons ) {
		return;
	}
	temporary_capture_buttons &= ~IN_PointerButtonMask( button );
	if ( !temporary_capture_buttons ) {
		IN_ApplyPointerGrab( pointer_grab_reasons & ~POINTER_GRAB_DRAG );
	}
}


static qboolean install_mouse_grab( void )
{
	int res;

	// move pointer to destination window area
	XWarpPointer( dpy, None, win, 0, 0, 0, 0, window_width / 2, window_height / 2 );

	XSync( dpy, False );

	// hide cursor
	IN_ShowWindowCursor( qfalse );

	// save old mouse settings
	XGetPointerControl( dpy, &mouse_accel_numerator, &mouse_accel_denominator, &mouse_threshold );

	// do this earlier?
	// Relative gameplay installs its own grab, so the shared confine/drag
	// bookkeeping no longer describes the server state.
	pointer_grab_reasons = 0;
	res = XGrabPointer( dpy, win, False, MOUSE_MASK, GrabModeAsync, GrabModeAsync, win, None, CurrentTime );
	if ( res != GrabSuccess )
	{
		IN_ShowWindowCursor( qtrue );
		if ( !gameplay_grab_failure_reported ) {
			Com_Printf( S_COLOR_YELLOW
				"Warning: XGrabPointer() failed (%d); retrying\n", res );
			gameplay_grab_failure_reported = qtrue;
		}
		XSync( dpy, False );
		return qfalse;
	}
	else
	{
		// set new mouse settings
		XChangePointerControl( dpy, True, True, 1, 1, 1 );
	}
	gameplay_grab_failure_reported = qfalse;

	XSync( dpy, False );

	mouseResetTime = Sys_Milliseconds();

#ifdef HAVE_XF86DGA
	if ( in_dgamouse->integer )
	{
		if ( !glw_state.dga_ext )
		{
			Cvar_Set( "in_dgamouse", "0" );
		}
		else
		{
			DGA_Mouse( qtrue );
			XWarpPointer( dpy, None, win, 0, 0, 0, 0, window_width / 2, window_height / 2 );
		}
	}
	else
#endif /* HAVE_XF86DGA */
	{
		mwx = window_width / 2;
		mwy = window_height / 2;
		mx = my = 0;
	}

	XSync( dpy, False );
	return qtrue;
}


static void install_kb_grab( void )
{
	int res;

	res = XGrabKeyboard( dpy, win, False, GrabModeAsync, GrabModeAsync, CurrentTime );
	if ( res != GrabSuccess )
	{
		Com_DPrintf( "Warning: XGrabKeyboard() failed (%d)\n", res );
		keyboard_grabbed = qfalse;
		keyboard_regrab_pending = qfalse;
	}
	else
	{
		keyboard_grabbed = qtrue;
		keyboard_regrab_pending = qfalse;
	}

	XSync( dpy, False );
}


static void uninstall_mouse_grab( void )
{
#ifdef HAVE_XF86DGA
	if ( in_dgamouse->integer )
	{
		if ( com_developer->integer )
		{
			Com_Printf( "DGA Mouse - Disabling DGA DirectVideo\n" );
		}
		DGA_Mouse( qfalse );
	}
#endif /* HAVE_XF86DGA */

	// restore mouse settings
	XChangePointerControl( dpy, qtrue, qtrue, mouse_accel_numerator, 
		mouse_accel_denominator, mouse_threshold );

	if ( gw_active && !gw_minimized && !IN_AbsolutePointerOwner() ) {
		XWarpPointer( dpy, None, win, 0, 0, 0, 0, window_width / 2, window_height / 2 );
	}

	XUngrabPointer( dpy, CurrentTime );
	XUngrabKeyboard( dpy, CurrentTime );
	keyboard_grabbed = qfalse;
	keyboard_regrab_pending = qfalse;
	pointer_grab_reasons = 0;

	// Retail UI owners retain the host cursor. The console draws its own cursor,
	// so hide the host cursor only over the client area.
	IN_ShowWindowCursor(
		IN_AbsolutePointerOwnerKind() == PointerOwner::Console ? qfalse : qtrue );

	XSync( dpy, False );
}


// bk001206 - from Ryan's Fakk2
/**
 * XPending() actually performs a blocking read 
 *  if no events available. From Fakk2, by way of
 *  Heretic2, by way of SDL, original idea GGI project.
 * The benefit of this approach over the quite
 *  badly behaved XAutoRepeatOn/Off is that you get
 *  focus handling for free, which is a major win
 *  with debug and windowed mode. It rests on the
 *  assumption that the X server will use the
 *  same timestamp on press/release event pairs 
 *  for key repeats.
 */
static qboolean X11_PendingInput( void )
{
	assert(dpy != NULL);

	// Flush the display connection and look to see if events are queued
	XFlush( dpy );

	if ( XEventsQueued( dpy, QueuedAlready ) )
	{
		return qtrue;
	}

	// More drastic measures are required -- see if X is ready to talk
	{
		static struct timeval zero_time;
		int x11_fd;
		fd_set fdset;

		x11_fd = ConnectionNumber( dpy );
		FD_ZERO( &fdset );
		FD_SET( x11_fd, &fdset );
		if ( select( x11_fd+1, &fdset, NULL, NULL, &zero_time ) == 1 )
		{
			return XPending( dpy ) ? qtrue : qfalse;
		}
	}

	// Oh well, nothing is ready ..
	return qfalse;
}


static qboolean repeated_press( XEvent *event )
{
	XEvent        peek;

	assert( dpy != NULL );

	if ( X11_PendingInput() )
	{
		XPeekEvent( dpy, &peek );

		if ( ( peek.type == KeyPress ) &&
			 ( peek.xkey.keycode == event->xkey.keycode ) &&
			 ( peek.xkey.time == event->xkey.time ) )
		{
			return qtrue;
		}
	}

	return qfalse;
}


static qboolean WindowMinimized(
	Display *display, Window window, qboolean *minimized )
{
	static constexpr long kMaximumWindowStateAtoms = 64;
	unsigned long numItems = 0;
	unsigned long bytesAfter = 0;
	Atom actualType = None;
	unsigned char *propertyData = NULL;
	int actualFormat = 0;

	if ( !display || !window || !minimized ) {
		return qfalse;
	}

	const Atom netWMState =
		XInternAtom( display, "_NET_WM_STATE", True );
	const Atom netWMStateHidden =
		XInternAtom( display, "_NET_WM_STATE_HIDDEN", True );
	if ( netWMState == None || netWMStateHidden == None ) {
		return qfalse;
	}

	if ( XGetWindowProperty( display, window, netWMState,
			0, kMaximumWindowStateAtoms, False, XA_ATOM,
			&actualType, &actualFormat,
			&numItems, &bytesAfter, &propertyData ) != Success ) {
		if ( propertyData ) {
			XFree( propertyData );
		}
		return qfalse;
	}

	// An absent state property is a valid normal-window snapshot. Other types
	// or malformed item storage are unknown, not evidence of a restore.
	if ( actualType == None && actualFormat == 0 && numItems == 0 ) {
		if ( propertyData ) {
			XFree( propertyData );
		}
		*minimized = qfalse;
		return qtrue;
	}
	if ( actualType != XA_ATOM || actualFormat != 32 || bytesAfter != 0 ||
		( numItems > 0 && !propertyData ) ) {
		if ( propertyData ) {
			XFree( propertyData );
		}
		return qfalse;
	}

	const Atom *atoms =
		reinterpret_cast<const Atom *>( propertyData );
	*minimized = qfalse;
	for ( unsigned long i = 0; i < numItems; ++i ) {
		if ( atoms[i] == netWMStateHidden ) {
			*minimized = qtrue;
			break;
		}
	}
	if ( propertyData ) {
		XFree( propertyData );
	}
	return qtrue;
}


/*
================
X11_UpdateMinimizedState

FocusOut is not guaranteed for every window-manager minimize. Treat only a
real state transition as a reset boundary; focus notifications may still add
their own ordered barriers when the WM emits both.
================
*/
static void X11_UpdateMinimizedState(
	qboolean minimized, qboolean *dowarp )
{
	if ( minimized == gw_minimized ) {
		return;
	}

	gw_minimized = minimized;
	if ( minimized ) {
		if ( dowarp ) {
			*dowarp = qfalse;
		}
		IN_DeactivateMouse();
		IN_EndTemporaryPointerCapture();
		IN_SetPointerConfinement( qfalse );
		IN_ShowWindowCursor( qtrue );
		X11_SetInputContextFocus( qfalse );
		X11_QueueInputReset( qfalse );
		Com_DPrintf( "Window minimized\n" );
		return;
	}

	Com_DPrintf( "Window restored\n" );
	if ( gw_active ) {
		X11_SetInputContextFocus( X11_TextInputOwnerActive() );
		X11_QueueInputReset( qtrue );
	}
}


static int X11_CardinalCoordinate( unsigned long value )
{
	return static_cast<int>( static_cast<std::int32_t>(
		static_cast<std::uint32_t>( value ) ) );
}


static int X11_CardinalExtent( unsigned long value )
{
	return fnql::window::SaturateToInt( static_cast<std::int64_t>( value ) );
}


static int X11_ReadCardinals( Window target, const char *name,
	unsigned long *values, int capacity )
{
	Atom property;
	Atom actualType;
	int actualFormat;
	unsigned long count;
	unsigned long bytesAfter;
	unsigned char *data = NULL;
	int copied;

	if ( !dpy || !target || capacity <= 0 ) {
		return 0;
	}

	property = XInternAtom( dpy, name, True );
	if ( property == None ||
		XGetWindowProperty( dpy, target, property, 0, capacity, False,
			XA_CARDINAL, &actualType, &actualFormat, &count, &bytesAfter,
			&data ) != Success ||
		actualType != XA_CARDINAL || actualFormat != 32 || !data ) {
		if ( data ) {
			XFree( data );
		}
		return 0;
	}

	copied = count < static_cast<unsigned long>( capacity )
		? static_cast<int>( count ) : capacity;
	memcpy( values, data, copied * sizeof( values[0] ) );
	XFree( data );
	return copied;
}


static qboolean X11_WindowHasState( Window target, const char *stateName )
{
	Atom property = XInternAtom( dpy, "_NET_WM_STATE", True );
	Atom state = XInternAtom( dpy, stateName, True );
	Atom actualType;
	int actualFormat;
	unsigned long count;
	unsigned long bytesAfter;
	unsigned char *data = NULL;
	qboolean found = qfalse;

	if ( property == None || state == None ||
		XGetWindowProperty( dpy, target, property, 0, 32, False, XA_ATOM,
			&actualType, &actualFormat, &count, &bytesAfter, &data ) != Success ||
		actualType != XA_ATOM || actualFormat != 32 || !data ) {
		if ( data ) {
			XFree( data );
		}
		return qfalse;
	}

	for ( unsigned long i = 0; i < count; ++i ) {
		if ( reinterpret_cast<Atom *>( data )[i] == state ) {
			found = qtrue;
			break;
		}
	}
	XFree( data );
	return found;
}


static fnql::window::Bounds X11_GetUsableBounds( Window root )
{
	fnql::window::Bounds monitor = {
		glw_state.desktop_x, glw_state.desktop_y,
		glw_state.desktop_width, glw_state.desktop_height
	};
	unsigned long desktopValue[1];
	unsigned long workAreas[64];
	int desktop = 0;
	int workAreaCount;
	int offset;
	fnql::window::Bounds work;
	int monitorRight;
	int monitorBottom;
	int workRight;
	int workBottom;

	if ( monitor.width <= 0 || monitor.height <= 0 ) {
		monitor = { 0, 0, DisplayWidth( dpy, scrnum ),
			DisplayHeight( dpy, scrnum ) };
	}
	monitorRight = fnql::window::SaturateToInt(
		static_cast<std::int64_t>( monitor.x ) + monitor.width );
	monitorBottom = fnql::window::SaturateToInt(
		static_cast<std::int64_t>( monitor.y ) + monitor.height );

	if ( X11_ReadCardinals( root, "_NET_CURRENT_DESKTOP", desktopValue, 1 ) == 1 ) {
		desktop = X11_CardinalExtent( desktopValue[0] );
	}
	workAreaCount = X11_ReadCardinals( root, "_NET_WORKAREA", workAreas,
		static_cast<int>( sizeof( workAreas ) / sizeof( workAreas[0] ) ) );
	offset = desktop >= 0 && desktop <= ( workAreaCount / 4 ) - 1
		? desktop * 4 : 0;
	if ( workAreaCount < offset + 4 ) {
		return monitor;
	}

	work = {
		X11_CardinalCoordinate( workAreas[offset] ),
		X11_CardinalCoordinate( workAreas[offset + 1] ),
		X11_CardinalExtent( workAreas[offset + 2] ),
		X11_CardinalExtent( workAreas[offset + 3] )
	};
	workRight = fnql::window::SaturateToInt(
		static_cast<std::int64_t>( work.x ) + work.width );
	workBottom = fnql::window::SaturateToInt(
		static_cast<std::int64_t>( work.y ) + work.height );

	// EWMH exposes one work area for the whole desktop. Intersect it with the
	// current RandR monitor so panels/docks and multi-monitor bounds both apply.
	if ( work.x < monitor.x ) work.x = monitor.x;
	if ( work.y < monitor.y ) work.y = monitor.y;
	if ( workRight > monitorRight ) workRight = monitorRight;
	if ( workBottom > monitorBottom ) workBottom = monitorBottom;
	work.width = workRight - work.x;
	work.height = workBottom - work.y;
	return work.width > 0 && work.height > 0 ? work : monitor;
}


static fnql::window::Insets X11_GetFrameInsets( Window root, int clientX,
	int clientY, int clientWidth, int clientHeight )
{
	unsigned long extents[4];
	fnql::window::Insets frame{};

	if ( X11_ReadCardinals( win, "_NET_FRAME_EXTENTS", extents, 4 ) == 4 ) {
		frame.left = X11_CardinalExtent( extents[0] );
		frame.right = X11_CardinalExtent( extents[1] );
		frame.top = X11_CardinalExtent( extents[2] );
		frame.bottom = X11_CardinalExtent( extents[3] );
		return frame;
	}

	// Older window managers may not implement EWMH frame extents. Derive the
	// same information from the reparented frame without assuming titlebar size.
	Window rootReturn;
	Window parent;
	Window *children = NULL;
	unsigned int childCount = 0;
	if ( XQueryTree( dpy, win, &rootReturn, &parent, &children, &childCount ) ) {
		if ( children ) {
			XFree( children );
		}
		if ( parent != None && parent != root ) {
			XWindowAttributes attributes;
			Window ignored;
			int frameX;
			int frameY;
			if ( XGetWindowAttributes( dpy, parent, &attributes ) &&
				XTranslateCoordinates( dpy, parent, root, 0, 0,
					&frameX, &frameY, &ignored ) ) {
				frame.left = clientX > frameX ? clientX - frameX : 0;
				frame.top = clientY > frameY ? clientY - frameY : 0;
				frame.right = attributes.width > clientWidth + frame.left
					? attributes.width - clientWidth - frame.left : 0;
				frame.bottom = attributes.height > clientHeight + frame.top
					? attributes.height - clientHeight - frame.top : 0;
			}
		}
	}

	return frame;
}


static qboolean X11_ApplyWindowPlacement( int width, int height,
	const fnql::window::Position *requestedClientOrigin )
{
	Window root = RootWindow( dpy, scrnum );
	Window ignored;
	int clientX;
	int clientY;
	fnql::window::Bounds outer;
	fnql::window::Insets frame;
	fnql::window::Position desired;
	fnql::window::Position outerOrigin;
	fnql::window::Position constrained;

	if ( glw_state.cdsFullscreen || gw_minimized || !win ||
		!XTranslateCoordinates( dpy, win, root, 0, 0,
			&clientX, &clientY, &ignored ) ) {
		return qfalse;
	}

	frame = X11_GetFrameInsets( root, clientX, clientY, width, height );
	desired = requestedClientOrigin
		? *requestedClientOrigin
		: fnql::window::Position{ clientX, clientY };
	outer = fnql::window::OuterBoundsFromClient(
		desired, width, height, frame );
	RandR_UpdateMonitor( outer.x, outer.y, outer.width, outer.height );

	win_x = clientX;
	win_y = clientY;
	if ( X11_WindowHasState( win, "_NET_WM_STATE_MAXIMIZED_HORZ" ) ||
		X11_WindowHasState( win, "_NET_WM_STATE_MAXIMIZED_VERT" ) ) {
		return qfalse;
	}

	constrained = fnql::window::ConstrainClientOrigin(
		desired, width, height, X11_GetUsableBounds( root ),
		frame );
	win_x = constrained.x;
	win_y = constrained.y;
	if ( constrained.x == clientX && constrained.y == clientY ) {
		return qfalse;
	}

	// A reparenting window manager interprets XMoveWindow coordinates as the
	// outer frame origin. Convert the constrained client origin just as SDL's
	// X11 backend does, otherwise the leading chrome is added a second time and
	// the opposite edge can remain off-screen.
	outerOrigin = fnql::window::OuterOriginFromClient( constrained, frame );
	XMoveWindow( dpy, win, outerOrigin.x, outerOrigin.y );
	return qtrue;
}


static qboolean X11_EnsureWindowOnScreen( int width, int height )
{
	return X11_ApplyWindowPlacement( width, height, NULL );
}


static qboolean directMap( const byte chr )
{
	if ( !in_forceCharset->integer )
		return qtrue;

	switch ( chr ) // edit control sequences
	{
		case 'c'-'a'+1:
		case 'v'-'a'+1:
		case 'h'-'a'+1:
		case 'a'-'a'+1:
		case 'e'-'a'+1:
		case 'n'-'a'+1:
		case 'p'-'a'+1:
		case 'l'-'a'+1: // CTRL+L
			return qtrue;
	}
	if ( chr < ' ' || chr > 127 || in_forceCharset->integer > 1 )
		return qfalse;
	else
		return qtrue;
}


/*
================
Sys_XTimeToSysTime
sub-frame timing of events returned by X
X uses the Time typedef - unsigned long
disable with in_subframe 0

 sys_timeBase*1000 is the number of ms since the Epoch of our origin
 xtime is in ms and uses the Epoch as origin
   Time data type is an unsigned long: 0xffffffff ms - ~49 days period
 I didn't find much info in the XWindow documentation about the wrapping
   we clamp sys_timeBase*1000 to unsigned long, that gives us the current origin for xtime
   the computation will still work if xtime wraps (at ~49 days period since the Epoch) after we set sys_timeBase

================
*/
static int Sys_XTimeToSysTime( Time xtime )
{
	extern unsigned long sys_timeBase;
	int ret, t, test;

	if ( !in_subframe->integer )
	{
		// if you don't want to do any event times corrections
		return Sys_Milliseconds();
	}

	// test the wrap issue
#if 0
	// reference values for test: sys_timeBase 0x3dc7b5e9 xtime 0x541ea451 (read these from a test run)
	// xtime will wrap in 0xabe15bae ms >~ 0x2c0056 s (33 days from Nov 5 2002 -> 8 Dec)
	//   NOTE: date -d '1970-01-01 UTC 1039384002 seconds' +%c
	// use sys_timeBase 0x3dc7b5e9+0x2c0056 = 0x3df3b63f
	// after around 5s, xtime would have wrapped around
	// we get 7132, the formula handles the wrap safely
	unsigned long xtime_aux,base_aux;
	int test;
//	Com_Printf("sys_timeBase: %p\n", sys_timeBase);
//	Com_Printf("xtime: %p\n", xtime);
	xtime_aux = 500; // 500 ms after wrap
	base_aux = 0x3df3b63f; // the base a few seconds before wrap
	test = xtime_aux - (unsigned long)(base_aux*1000);
	Com_Printf("xtime wrap test: %d\n", test);
#endif

	// some X servers (like suse 8.1's) report weird event times
	// if the game is loading, resolving DNS, etc. we are also getting old events
	// so we only deal with subframe corrections that look 'normal'
	ret = xtime - (unsigned long)(sys_timeBase * 1000);
	t = Sys_Milliseconds();
	test = t - ret;

	//printf("delta: %d\n", test);
	if (test < 0 || test > 30) // in normal conditions I've never seen this go above
	{
		return t;
	}

	return ret;
}


void HandleEvents( void )
{
	XEvent event;
	int btn_code;
	int key;
	int textLength;
	KeySym physicalKeysym;
	qboolean dowarp = qfalse;
	const char *p;
	int dx, dy;
	int t = 0; // default to 0 in case we don't set
	qboolean btn_press;
	qboolean textHandled;
	char buf[2];

	if ( !dpy )
		return;

	// Com_EventLoop can consume a catcher-changing key event and immediately
	// re-enter this native drain before IN_Frame runs. Reconcile here as well
	// so XFilterEvent never sees gameplay through a stale text-owner focus.
	X11_SetInputContextFocus( X11_TextInputOwnerActive() );

	while( XPending( dpy ) )
	{
		XNextEvent( dpy, &event );
		if ( X11_FilterInputEvent( &event ) ) {
			continue;
		}

		switch( event.type )
		{

		case ClientMessage:

			if ( static_cast<Atom>( event.xclient.data.l[0] ) == wmDeleteEvent ) {
				Cmd_Clear();
				Com_Quit_f();
			}
			break;

		case KeyPress: {
			if ( !gw_active || gw_minimized ) {
				break;
			}
			// Com_Printf("^2K+^7 %08X\n", event.xkey.keycode );
			t = Sys_XTimeToSysTime( event.xkey.time );
			physicalKeysym = XLookupKeysym( &event.xkey, 0 );
			const qboolean isRepeat =
				event.xkey.keycode < ARRAY_LEN( translated_key_by_keycode ) &&
				translated_key_by_keycode[event.xkey.keycode] != 0
					? qtrue : qfalse;
			if ( event.xkey.keycode == 0x31 )
			{
				key = K_CONSOLE;
				p = "";
				textLength = 0;
			}
			else
			{
				XKeyEvent translatedEvent = event.xkey;
				int shift = (event.xkey.state & 1);
				p = XLateKey( &translatedEvent, &key, &textLength );
				if ( textLength > 0 && event.xkey.keycode == 0x5B )
				{
					p = ".";
					textLength = 1;
				}
				else
				if ( textLength > 0 &&
					!directMap( (unsigned char)p[0] ) &&
					event.xkey.keycode < 0x3F )
				{
					char ch;
					ch = s_keytochar[ event.xkey.keycode ];
					if ( ch >= 'a' && ch <= 'z' )
					{
						unsigned int capital = 0;
						if ( XkbGetIndicatorState( dpy, XkbUseCoreKbd, &capital ) != Success ) {
							capital = 0;
						}
						capital &= 1;
						if ( capital ^ shift )
						{
							ch = ch - 'a' + 'A';
						}
					}
					else
					{
						ch = s_keytochar[ event.xkey.keycode | (shift<<6) ];
					}
					buf[0] = ch;
					buf[1] = '\0';
					p = buf;
					textLength = 1;
				}
			}
			if ( key == K_ENTER &&
				( event.xkey.state & Mod1Mask ||
					physical_modifier_state &
						( X11_MOD_LALT | X11_MOD_RALT ) ) ) {
				if ( event.xkey.keycode <
					ARRAY_LEN( translated_key_by_keycode ) ) {
					translated_key_by_keycode[event.xkey.keycode] =
						K_ENTER;
					suppressed_key_by_keycode[event.xkey.keycode] =
						qtrue;
				}
				if ( !isRepeat ) {
					Cvar_SetIntegerValue( "r_fullscreen",
						glw_state.cdsFullscreen ? 0 : 1 );
					Cbuf_AddText( "vid_restart\n" );
				}
				break;
			}
			if ( isRepeat && ( key == K_CONSOLE || key == K_ESCAPE ) ) {
				break;
			}
			if ( key && event.xkey.keycode < ARRAY_LEN( translated_key_by_keycode ) &&
				translated_key_by_keycode[event.xkey.keycode] == 0 ) {
				translated_key_by_keycode[event.xkey.keycode] = key;
			}
			if ( key && X11_ShouldQueueModifierTransition(
				physicalKeysym, qtrue ) )
			{
				Sys_QueEvent( t, SE_KEY, key, qtrue, 0, NULL );
			}
			textHandled = qfalse;
			if ( key == K_CONSOLE ) {
				textHandled = qtrue;
			} else if ( !textHandled ) {
				textHandled =
					X11_QueueInputMethodText( &event.xkey, t );
			}
			if ( !textHandled ) {
				for ( int i = 0; i < textLength; ++i )
				{
					Sys_QueEvent( t, SE_CHAR,
						(unsigned char)p[i], 0, 0, NULL );
				}
			}
			break; // case KeyPress
		}

		case KeyRelease:
			if ( !gw_active || gw_minimized ) {
				break;
			}

			if ( repeated_press( &event ) )
				break; // XNextEvent( dpy, &event )

			if ( event.xkey.keycode <
					ARRAY_LEN( suppressed_key_by_keycode ) &&
				suppressed_key_by_keycode[event.xkey.keycode] ) {
				suppressed_key_by_keycode[event.xkey.keycode] = qfalse;
				translated_key_by_keycode[event.xkey.keycode] = 0;
				break;
			}

			t = Sys_XTimeToSysTime( event.xkey.time );
			physicalKeysym = XLookupKeysym( &event.xkey, 0 );
#if 0
			Com_Printf("^5K-^7 %08X %s\n",
				event.xkey.keycode,
				X11_PendingInput()?"pending":"");
#endif
			if ( event.xkey.keycode < ARRAY_LEN( translated_key_by_keycode ) &&
				translated_key_by_keycode[event.xkey.keycode] ) {
				key = translated_key_by_keycode[event.xkey.keycode];
				translated_key_by_keycode[event.xkey.keycode] = 0;
			} else {
				XKeyEvent translatedEvent = event.xkey;
				XLateKey( &translatedEvent, &key, &textLength );
			}
			if ( key && X11_ShouldQueueModifierTransition(
				physicalKeysym, qfalse ) )
			{
				Sys_QueEvent( t, SE_KEY, key, qfalse, 0, NULL );
			}

			break; // case KeyRelease

		case MotionNotify:
			if ( !gw_active || gw_minimized ) {
				break;
			}
			if ( IN_AbsolutePointerOwner() )
			{
				t = Sys_XTimeToSysTime( event.xmotion.time );
				IN_QueueAbsolutePointerPosition( t, event.xmotion.x, event.xmotion.y );
			}
			else if ( IN_MouseActive() )
			{
				t = Sys_XTimeToSysTime( event.xmotion.time );
#ifdef HAVE_XF86DGA
				if ( in_dgamouse->integer )
				{
					mx = fnql::input::SaturatingAddInt(
						mx, event.xmotion.x_root );
					my = fnql::input::SaturatingAddInt(
						my, event.xmotion.y_root );
					if (t - mouseResetTime > MOUSE_RESET_DELAY )
					{
						Sys_QueEvent( t, SE_MOUSE, mx, my, 0, NULL );
					}
					mx = my = 0;
				}
				else
#endif // HAVE_XF86DGA
				{
					// If it's a center motion, we've just returned from our warp
					if ( event.xmotion.x == window_width/2 && event.xmotion.y == window_height/2 )
					{
						mwx = window_width/2;
						mwy = window_height/2;
						if ( t - mouseResetTime > MOUSE_RESET_DELAY )
						{
							Sys_QueEvent( t, SE_MOUSE, mx, my, 0, NULL );
						}
						mx = my = 0;
						break;
					}

					dx = ((int)event.xmotion.x - mwx);
					dy = ((int)event.xmotion.y - mwy);
					mx = fnql::input::SaturatingAddInt( mx, dx );
					my = fnql::input::SaturatingAddInt( my, dy );
					mwx = event.xmotion.x;
					mwy = event.xmotion.y;
					dowarp = qtrue;
				} // if ( !in_dgamouse->value )
			} // if ( mouse_active )
			break;

		case ButtonPress:
		case ButtonRelease:
			if ( !gw_active || gw_minimized ) {
				break;
			}
			if ( !IN_MouseActive() && !IN_AbsolutePointerOwner() )
				break;

			if ( event.type == ButtonPress )
				btn_press = qtrue;
			else
				btn_press = qfalse;
			t = Sys_XTimeToSysTime( event.xkey.time );
			if ( !btn_press &&
				( event.xbutton.button == 4 || event.xbutton.button == 5 ) ) {
				break;
			}
			// Preserve pointer-position-before-click/wheel ordering even if an
			// earlier Escape in this same X event drain changes ownership.
			IN_QueueAbsolutePointerPosition(
				t, event.xbutton.x, event.xbutton.y, qtrue );
			if ( IN_AbsolutePointerOwner() ) {
				if ( btn_press ) {
					IN_BeginTemporaryPointerCapture( event.xbutton.button );
				}
			}
			// NOTE TTimo there seems to be a weird mapping for K_MOUSE1 K_MOUSE2 K_MOUSE3 ..
			btn_code = -1;
			switch ( event.xbutton.button )
			{
				case 1: btn_code = K_MOUSE1; break;
				case 2: btn_code = K_MOUSE3; break;
				case 3: btn_code = K_MOUSE2; break;
				case 4:
					if ( btn_press ) {
						Sys_QueEvent( t, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
						Sys_QueEvent( t, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
					}
					break;
				case 5:
					if ( btn_press ) {
						Sys_QueEvent( t, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
						Sys_QueEvent( t, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
					}
					break;
				case 6: btn_code = K_MOUSE4; break;
				case 7: btn_code = K_MOUSE5; break;
				case 8: case 9:
				case 10: case 11:
					btn_code = event.xbutton.button - 8 + K_MOUSE6;
					break;
				default:
					if ( event.xbutton.button >= 12 &&
						event.xbutton.button <= 27 ) {
						btn_code =
							event.xbutton.button - 12 + K_AUX1;
					}
					break;
			}

			if ( btn_code != -1 )
			{
				if ( btn_code >= K_AUX1 && btn_code < K_AUX1 + 16 ) {
					const unsigned int mask =
						1u << static_cast<unsigned int>(
							btn_code - K_AUX1 );
					if ( btn_press ) {
						mouse_aux_button_state |= mask;
					} else {
						mouse_aux_button_state &= ~mask;
					}
				}
				Sys_QueEvent( t, SE_KEY, btn_code, btn_press, 0, NULL );
			}
			if ( !btn_press && IN_AbsolutePointerOwner() ) {
				IN_ReleaseTemporaryPointerButton( event.xbutton.button );
			}
			break; // case ButtonPress/ButtonRelease

		case CreateNotify:
			win_x = event.xcreatewindow.x;
			win_y = event.xcreatewindow.y;
			Com_DPrintf( "CreateNotify: x=%i, y=%i\n", win_x, win_y );
			break;

		case MapNotify:
			if ( event.xmap.window == win ) {
				X11_UpdateMinimizedState( qfalse, &dowarp );
			}
			break;

		case UnmapNotify:
			if ( event.xunmap.window == win &&
				!event.xunmap.from_configure ) {
				X11_UpdateMinimizedState( qtrue, &dowarp );
			}
			break;

		case PropertyNotify: {
			const Atom netWMState =
				XInternAtom( dpy, "_NET_WM_STATE", True );
			const Atom netFrameExtents =
				XInternAtom( dpy, "_NET_FRAME_EXTENTS", True );
			qboolean minimizedState;
			if ( event.xproperty.window == win &&
				netWMState != None &&
				event.xproperty.atom == netWMState &&
				WindowMinimized( dpy, win, &minimizedState ) ) {
				X11_UpdateMinimizedState( minimizedState, &dowarp );
			}
			if ( event.xproperty.window == win &&
				netFrameExtents != None &&
				event.xproperty.atom == netFrameExtents &&
				window_created && window_exposed && !gw_minimized &&
				window_width > 0 && window_height > 0 ) {
				// Some WMs publish chrome only after the first expose. Re-run
				// placement as soon as those authoritative extents arrive.
				X11_EnsureWindowOnScreen( window_width, window_height );
			}
			break;
		}

		case ConfigureNotify: {
			qboolean minimizedState;
			if ( WindowMinimized( dpy, win, &minimizedState ) ) {
				X11_UpdateMinimizedState( minimizedState, &dowarp );
			}
			win_x = event.xconfigure.x;
			win_y = event.xconfigure.y;

			Com_DPrintf( "ConfigureNotify: gw_minimized=%i, created=%i, exposed=%i, x=%i, y=%i\n",
				gw_minimized, window_created, window_exposed, win_x, win_y );

			if ( !glw_state.cdsFullscreen && window_created && !gw_minimized && window_exposed )
			{
				Window ignored;
				XTranslateCoordinates( dpy, win, RootWindow( dpy, scrnum ), 0, 0,
					&win_x, &win_y, &ignored );
				X11_EnsureWindowOnScreen( event.xconfigure.width,
					event.xconfigure.height );

				if ( !X11_WindowHasState( win, "_NET_WM_STATE_MAXIMIZED_HORZ" ) &&
					!X11_WindowHasState( win, "_NET_WM_STATE_MAXIMIZED_VERT" ) ) {
					Cvar_SetIntegerValue( "vid_xpos", win_x );
					Cvar_SetIntegerValue( "vid_ypos", win_y );
				}

				if ( window_width > 0 && window_height > 0 &&
					( window_width != event.xconfigure.width ||
					window_height != event.xconfigure.height ) ) {
					if ( glw_state.config &&
						event.xconfigure.width == glw_state.config->vidWidth &&
						event.xconfigure.height == glw_state.config->vidHeight ) {
						CL_CancelWindowResize();
					} else {
						// The legacy X11 path recreates its native window on restart;
						// unlike SDL/Win32, it cannot retain the current X Window safely.
						CL_NotifyWindowResize( event.xconfigure.width,
							event.xconfigure.height, qfalse );
					}
				}
				window_width = event.xconfigure.width;
				window_height = event.xconfigure.height;
			}
			break;
		}

		case FocusIn:
		case FocusOut:
			if ( event.xfocus.mode == NotifyGrab ||
				event.xfocus.mode == NotifyUngrab ) {
				break;
			}
			if ( event.type == FocusIn ) {
				if ( gw_active ) {
					break;
				}
				gw_active = qtrue;
				X11_SetInputContextFocus( X11_TextInputOwnerActive() );
				Com_DPrintf( "FocusIn\n" );
			} else {
				if ( !gw_active ) {
					break;
				}
				X11_SetInputContextFocus( qfalse );
				gw_active = qfalse;
				dowarp = qfalse;
				IN_DeactivateMouse();
				IN_EndTemporaryPointerCapture();
				// Never hold the pointer confined or hidden across focus loss.
				IN_SetPointerConfinement( qfalse );
				IN_ShowWindowCursor( qtrue );
				Com_DPrintf( "FocusOut\n" );
			}
			X11_QueueInputReset(
				event.type == FocusIn ? qtrue : qfalse );
			break;

		case Expose:
			window_exposed = qtrue;
			if ( !glw_state.cdsFullscreen && window_created && !gw_minimized &&
				window_width > 0 && window_height > 0 ) {
				X11_EnsureWindowOnScreen( window_width, window_height );
			}
			break;
		}
	}

	if ( dowarp && gw_active && !gw_minimized )
	{
		XWarpPointer( dpy, None, win, 0, 0, 0, 0, window_width/2, window_height/2 );
	}
}


// NOTE TTimo for the tty console input, we didn't rely on those .. 
//   it's not very surprising actually cause they are not used otherwise
void KBD_Init( void )
{

}


void KBD_Close( void )
{

}


/*
================
IN_ActivateMouse
================
*/
void IN_ActivateMouse( void )
{
	if ( !mouse_avail || !dpy || !win )
	{
		return;
	}

	if ( !mouse_active )
	{
		if ( in_dgamouse->integer && in_nograb->integer ) // force dga mouse to 0 if using nograb
		{
			Cvar_Set( "in_dgamouse", "0" );
		}
		if ( install_mouse_grab() ) {
			install_kb_grab();
			mouse_active = qtrue;
		}
	}
}


/*
================
IN_DeactivateMouse
================
*/
void IN_DeactivateMouse( void )
{
	if ( !mouse_avail || !dpy || !win )
	{
		return;
	}

	if ( mouse_active )
	{
		uninstall_mouse_grab();
		if ( in_dgamouse->integer && in_nograb->integer ) // force dga mouse to 0 if using nograb
		{
			Cvar_Set( "in_dgamouse", "0" );
		}
		mouse_active = qfalse;
	}
}


/*
================
IN_MouseActive
================
*/
qboolean IN_MouseActive( void )
{
	return ( in_nograb->integer == 0 && mouse_active ) ? qtrue : qfalse;
}


/*
================
IN_Minimize
================
*/
void IN_Minimize( void )
{
	if ( !CL_VideoRecording() || ( re.CanMinimize && re.CanMinimize() ) )
	{
		XIconifyWindow( dpy, win, scrnum );
		XFlush( dpy );
	}
}


qboolean BuildGammaRampTable( unsigned char *red, unsigned char *green, unsigned char *blue, int gammaRampSize, unsigned short table[3][4096] )
{
	int i, j;
	int m, m1;
	int shift;

	switch ( gammaRampSize )
	{
		case 256: shift = 0; break;
		case 512: shift = 1; break;
		case 1024: shift = 2; break;
		case 2048: shift = 3; break;
		case 4096: shift = 4; break;
		default:
			Com_Printf( "Unsupported gamma ramp size: %d\n", gammaRampSize );
		return qfalse;
	};
	
	m = gammaRampSize / 256;
	m1 = 256 / m;

	for ( i = 0; i < 256; i++ ) {
		for ( j = 0; j < m; j++ ) {
			table[0][i*m+j] = (unsigned short)(red[i] << 8)   | (m1 * j) | ( red[i] >> shift );
			table[1][i*m+j] = (unsigned short)(green[i] << 8) | (m1 * j) | ( green[i] >> shift );
			table[2][i*m+j] = (unsigned short)(blue[i] << 8)  | (m1 * j) | ( blue[i] >> shift );
		}
	}

	// enforce constantly increasing
	for ( j = 0 ; j < 3 ; j++ ) {
		for ( i = 1 ; i < gammaRampSize ; i++ ) {
			if ( table[j][i] < table[j][i-1] ) {
				table[j][i] = table[j][i-1];
			}
		}
	}

	return qtrue;
}

/*****************************************************************************/

/*
** GLimp_SetGamma
**
** This routine should only be called if glConfig.deviceSupportsGamma is TRUE
*/
void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
	if ( glw_state.randr_gamma )
	{
		RandR_SetGamma( red, green, blue );
		return;
	}

	if ( glw_state.vidmode_gamma )
	{
		VidMode_SetGamma( red, green, blue );
		return;
	}
}


#ifdef USE_OPENGL_API
/*
** GLimp_Shutdown
**
** This routine does all OS specific shutdown procedures for the OpenGL
** subsystem.  Under OpenGL this means NULLing out the current DC and
** HGLRC, deleting the rendering context, and releasing the DC acquired
** for the window.  The state structure is also nulled out.
**
*/
void GLimp_Shutdown( qboolean unloadDLL )
{
	IN_DeactivateMouse();

	IN_Shutdown();

	if ( dpy )
	{
		XSync( dpy, True );
		X11_DestroyInputContext();

		if ( glw_state.randr_gamma && glw_state.gammaSet )
		{
			RandR_RestoreGamma();
			glw_state.gammaSet = qfalse;
		}

		RandR_RestoreMode();

		if ( ctx )
		{
			qglXMakeCurrent( dpy, None, NULL );
			qglXDestroyContext( dpy, ctx );
			ctx = NULL;
		}

		if ( win )
		{
			XDestroyWindow( dpy, win );
			win = 0;
		}

		if ( glw_state.gammaSet )
		{
			VidMode_RestoreGamma();
			glw_state.gammaSet = qfalse;
		}

		if ( glw_state.vidmode_active )
			VidMode_RestoreMode();

		XSync( dpy, False );

		// NOTE TTimo opening/closing the display should be necessary only once per run
		// but it seems QGL_Shutdown gets called in a lot of occasion
		// in some cases, this XCloseDisplay is known to raise some X errors
		// ( https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=33 )
		if ( unloadDLL )
		{
			X11_CloseInputMethod();
			if ( invisible_cursor != None ) {
				XFreeCursor( dpy, invisible_cursor );
				invisible_cursor = None;
			}
			XCloseDisplay( dpy );
			dpy = NULL;
		}
	}

	if ( unloadDLL )
	{
		RandR_Done();
		VidMode_Done();
	}

	glw_state.desktop_ok = qfalse;
	glw_state.cdsFullscreen = qfalse;

	unsetenv( "vblank_mode" );

	QGL_Shutdown( unloadDLL );
}
#endif // USE_OPENGL_API


#ifdef USE_VULKAN_API
/*
** VKimp_Shutdown
*/
void VKimp_Shutdown( qboolean unloadDLL )
{
	IN_DeactivateMouse();

	IN_Shutdown();

	if ( dpy )
	{
		XSync( dpy, True );
		X11_DestroyInputContext();

		if ( glw_state.randr_gamma && glw_state.gammaSet )
		{
			RandR_RestoreGamma();
			glw_state.gammaSet = qfalse;
		}

		RandR_RestoreMode();

		if ( win )
		{
			XDestroyWindow( dpy, win );
			win = 0;
		}

		if ( glw_state.gammaSet )
		{
			VidMode_RestoreGamma();
			glw_state.gammaSet = qfalse;
		}

		if ( glw_state.vidmode_active )
			VidMode_RestoreMode();

		XSync( dpy, False );

		// NOTE TTimo opening/closing the display should be necessary only once per run
		// but it seems QGL_Shutdown gets called in a lot of occasion
		// in some cases, this XCloseDisplay is known to raise some X errors
		// ( https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=33 )
		if ( unloadDLL )
		{
			X11_CloseInputMethod();
			if ( invisible_cursor != None ) {
				XFreeCursor( dpy, invisible_cursor );
				invisible_cursor = None;
			}
			XCloseDisplay( dpy );
			dpy = NULL;
		}
	}

	if ( unloadDLL )
	{
		RandR_Done();
		VidMode_Done();
	}

	glw_state.desktop_ok = qfalse;
	glw_state.cdsFullscreen = qfalse;

	unsetenv( "vblank_mode" );

	QVK_Shutdown( unloadDLL );
}
#endif // USE_VULKAN_API


/*
** GLimp_LogComment
*/
void GLimp_LogComment( const char *comment )
{
	if ( glw_state.log_fp )
	{
		fprintf( glw_state.log_fp, "%s", comment );
	}
}


/*
** GLW_StartDriverAndSetMode
*/
rserr_t GLW_SetMode( int mode, const char *modeFS, qboolean fullscreen, qboolean vulkan );

static rserr_t GLW_StartDriverAndSetMode( int mode, const char *modeFS, qboolean fullscreen, qboolean vulkan )
{
	rserr_t err;
	
	if ( fullscreen && in_nograb->integer )
	{
		Com_Printf( "Fullscreen not allowed with in_nograb 1\n");
		Cvar_Set( "r_fullscreen", "0" );
		r_fullscreen->modified = qfalse;
		fullscreen = qfalse;
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

	case RSERR_FATAL_ERROR:
		Com_Printf( "...WARNING: couldn't open the X display\n" );
		return err;

	default:
	    break;
	}

	glw_state.config->isFullscreen = fullscreen;

	return RSERR_OK;
}


#ifdef USE_OPENGL_API
static XVisualInfo *GL_SelectVisual( int colorbits, int depthbits, int stencilbits, glconfig_t *config )
{
	// these match in the array
	#define ATTR_RED_IDX     3
	#define ATTR_GREEN_IDX   5
	#define ATTR_BLUE_IDX    7
	#define ATTR_ALPHA_IDX   9
	#define ATTR_DEPTH_IDX   11
	#define ATTR_STENCIL_IDX 13

	static int attrib[] =
	{
		GLX_RGBA,             // 0
		GLX_DOUBLEBUFFER,     // 1
		GLX_RED_SIZE,     8,  // 2, 3
		GLX_GREEN_SIZE,   8,  // 4, 5
		GLX_BLUE_SIZE,    8,  // 6, 7
		GLX_ALPHA_SIZE,   8,  // 8, 9
		GLX_DEPTH_SIZE,   24, // 10, 11
		GLX_STENCIL_SIZE, 8,  // 12, 13
		None
	};

	int tcolorbits, tdepthbits, tstencilbits, i;
	XVisualInfo *visinfo = NULL;

	for ( i = 0; i < 16; i++ )
	{
		// 0 - default
		// 1 - minus colorbits
		// 2 - minus depthbits
		// 3 - minus stencil
		if ( (i % 4) == 0 && i )
		{
			// one pass, reduce
			switch (i / 4)
			{
			case 2 :
				if ( colorbits == 24 )
					colorbits = 16;
				break;
			case 1 :
				if ( depthbits == 24 )
					depthbits = 16;
			case 3 :
				if ( stencilbits == 8 )
					stencilbits = 0;
			}
		}

		tcolorbits = colorbits;
		tdepthbits = depthbits;
		tstencilbits = stencilbits;

		if ( (i % 4) == 3 )
		{ // reduce colorbits
			if ( tcolorbits == 24 )
				tcolorbits = 16;
		}

		if ( (i % 4) == 2 )
		{ // reduce depthbits
			if ( tdepthbits == 24 )
				tdepthbits = 16;
		}

		if ((i % 4) == 1)
		{ // reduce stencilbits
			if ( tstencilbits == 8 )
				tstencilbits = 0;
		}

		if (tcolorbits == 24)
		{
			attrib[ATTR_RED_IDX] = 8;
			attrib[ATTR_GREEN_IDX] = 8;
			attrib[ATTR_BLUE_IDX] = 8;
			attrib[ATTR_ALPHA_IDX] = 8;
		}
		else
		{
			// must be 16 bit
			attrib[ATTR_RED_IDX] = 4;
			attrib[ATTR_GREEN_IDX] = 4;
			attrib[ATTR_BLUE_IDX] = 4;
			attrib[ATTR_ALPHA_IDX] = 0; // prefer smallest available alpha
		}

		attrib[ATTR_DEPTH_IDX] = tdepthbits; // default to 24 depth
		attrib[ATTR_STENCIL_IDX] = tstencilbits;

		visinfo = qglXChooseVisual( dpy, scrnum, attrib );
		if ( !visinfo )
			continue;

		Com_Printf( "Using %d/%d/%d Color bits, %d depth, %d stencil display.\n", 
			attrib[ATTR_RED_IDX], attrib[ATTR_GREEN_IDX], attrib[ATTR_BLUE_IDX],
			attrib[ATTR_DEPTH_IDX], attrib[ATTR_STENCIL_IDX]);

		config->colorBits = tcolorbits;
		config->depthBits = tdepthbits;
		config->stencilBits = tstencilbits;

		break;
	}

	return visinfo;
}
#endif // USE_OPENGL_API


#ifdef USE_VULKAN_API
static XVisualInfo *VK_SelectVisual( int colorbits, int depthbits, int stencilbits, glconfig_t *config )
{
	static XVisualInfo visinfo;
	XVisualInfo visualTemplate;
	XVisualInfo *list;
	int i, nvisuals;

	visualTemplate.screen = scrnum;
	list = XGetVisualInfo( dpy, VisualScreenMask, &visualTemplate, &nvisuals );

	for ( i = 0; i < nvisuals; i++ )
	{
#if 0
		printf("  %3d: screen %i  visual 0x%lx class %d (%s) depth %d bits_per_rgb %d\n",
			i,
			list[i].screen,
			list[i].visualid,
			list[i].class,
			list[i].class == TrueColor ? "TrueColor" : "unknown",
			list[i].depth,
			list[i].bits_per_rgb );
#endif
		if ( list[i].depth == colorbits ) {
			//if ( list[i] == TrueColor )
			break;
		}
	}

	if ( i != nvisuals )
	{
		memcpy( &visinfo, &list[i], sizeof( visinfo ) );
	}

	config->colorBits = colorbits;
	config->depthBits = depthbits;
	config->stencilBits = stencilbits;

	XFree( list );

//	return NULL; // debug

	if ( i == nvisuals )
		return NULL;
	else
		return &visinfo;

//	for ( ;; ) {
//		if ( XMatchVisualInfo( dpy, scrnum, colorbits, &vinfo ) )
//		{
//		
//		}
//	}
}
#endif


/*
** GLW_SetMode
*/
rserr_t GLW_SetMode( int mode, const char *modeFS, qboolean fullscreen, qboolean vulkan )
{
	glconfig_t *config = glw_state.config;
	const fnql::window::Position requestedWindowOrigin = {
		vid_xpos->integer, vid_ypos->integer
	};
	Window root;
	XVisualInfo *visinfo = NULL;

	XSetWindowAttributes attr;
	XSizeHints sizehints;
	unsigned long mask;
	int colorbits, depthbits, stencilbits;
	int actualWidth, actualHeight, actualRate;

	window_width = 0;
	window_height = 0;
	window_created = qfalse;
	X11_DestroyInputContext();

	glw_state.dga_ext = qfalse;
	glw_state.randr_ext = qfalse;
	glw_state.vidmode_ext = qfalse;

	if ( dpy == NULL )
	{
		dpy = XOpenDisplay( NULL );
		if ( dpy == NULL )
		{
			fprintf( stderr, "Error: couldn't open the X display\n" );
			return RSERR_FATAL_ERROR;
		}
	}

	//XSync( dpy, True );

	scrnum = DefaultScreen( dpy );
	root = RootWindow( dpy, scrnum );

	// Init xrandr and get desktop resolution if available
	RandR_Init( vid_xpos->integer, vid_ypos->integer, 640, 480 );

	if ( !glw_state.randr_ext )
	{
		VidMode_Init();
	}

	XSync( dpy, False );

#ifdef HAVE_XF86DGA
	if ( in_dgamouse && in_dgamouse->integer )
	{
		if ( !DGA_Init( dpy ) )
		{
			Cvar_Set( "in_dgamouse", "0" );
		}
	}
#endif
	Com_Printf( "Initializing display\n" );

	Com_Printf( "...setting mode %d:", mode );

	if ( !CL_GetModeInfo( &config->vidWidth, &config->vidHeight, &config->windowAspect,
		mode, modeFS, glw_state.desktop_width, glw_state.desktop_height, fullscreen ) )
	{
		Com_Printf( " invalid mode\n" );
		return RSERR_INVALID_MODE;
	}

	actualWidth = config->vidWidth;
	actualHeight = config->vidHeight;
	actualRate = r_displayRefresh->integer;

	if ( actualRate )
		Com_Printf( " %d %d @%iHz\n", actualWidth, actualHeight, actualRate );
	else
		Com_Printf( " %d %d\n", actualWidth, actualHeight );

	if ( glw_state.randr_ext ) // try randr first
	{
		if ( fullscreen )
			RandR_SetMode( &actualWidth, &actualHeight, &actualRate );
		else
			glw_state.randr_active = qtrue;
	}

	if ( glw_state.vidmode_ext && !glw_state.randr_active )
	{
		if ( fullscreen )
			VidMode_SetMode( &actualWidth, &actualHeight, &actualRate );
		else
			Com_Printf( "XFree86-VidModeExtension: Ignored on non-fullscreen\n" );
	}

	colorbits = r_colorbits->integer;

	if ( colorbits == 0 || colorbits > 24 )
		colorbits = 24;

	if ( cl_depthbits->integer == 0 )
	{
		// implicitly assume Z-buffer depth == desktop color depth
		if ( colorbits > 16 )
			depthbits = 24;
		else
			depthbits = 16;
	}
	else
		depthbits = cl_depthbits->integer;

	stencilbits = cl_stencilbits->integer;

	// do not allow stencil if Z-buffer depth likely won't contain it
	if ( depthbits < 24 )
		stencilbits = 0;

#ifdef USE_VULKAN_API
	if ( vulkan )
		visinfo = VK_SelectVisual( colorbits, depthbits, stencilbits, config );
#endif
#ifdef USE_OPENGL_API
	if ( !vulkan )
		visinfo = GL_SelectVisual( colorbits, depthbits, stencilbits, config );
#endif

	if ( !visinfo )
	{
		Com_Printf( "Couldn't get a visual\n" );
		return RSERR_INVALID_MODE;
	}

	window_width = actualWidth;
	window_height = actualHeight;

	glw_state.cdsFullscreen = fullscreen;

	/* window attributes */
	memset( &attr, 0, sizeof( attr ) );
	attr.background_pixel = BlackPixel( dpy, scrnum );
	attr.border_pixel = 0;
	attr.colormap = XCreateColormap( dpy, root, visinfo->visual, AllocNone );
	attr.event_mask = X_MASK;

	if ( fullscreen )
	{
		mask = CWBackPixel | CWColormap | CWSaveUnder | CWBackingStore |
			CWEventMask | CWOverrideRedirect;
		attr.override_redirect = True;
		attr.backing_store = NotUseful;
		attr.save_under = False;
	}
	else
	{
		mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;
	}

	window_exposed = qfalse;
	window_created = qfalse;

	gw_active = qfalse;
	gw_minimized = qfalse; /* safe default */

	win = XCreateWindow( dpy, root, 0, 0, actualWidth, actualHeight,
		0, visinfo->depth, InputOutput, visinfo->visual, mask, &attr );

	// A fresh window inherits the default cursor and holds no grab, so the
	// latched pointer presentation from the previous window is meaningless.
	window_cursor_valid = qfalse;
	window_cursor_shown = qtrue;
	pointer_grab_reasons = 0;
	temporary_capture_buttons = 0;
	keyboard_grabbed = qfalse;
	keyboard_regrab_pending = qfalse;

	motifWMHints = XInternAtom( dpy, "_MOTIF_WM_HINTS", True );

	if ( motifWMHints != None )
	{
		motifHints_t decohint;
		decohint.flags = (1L << 1);
		decohint.functions = 0;
		decohint.decorations = r_noborder->integer ? 0 : 1;
		decohint.input_mode = decohint.status = 0;

		XChangeProperty( dpy, win, motifWMHints, motifWMHints, 32,
			PropModeReplace, (unsigned char*)& decohint,
			sizeof(decohint) / sizeof(long) );
	}

	XStoreName( dpy, win, cl_title );

	// Keep the native fallback consistent with SDL: the window manager owns
	// interactive sizing and the client refreshes its renderer after it settles.
	memset( &sizehints, 0, sizeof( sizehints ) );
	sizehints.flags = PMinSize;
	sizehints.min_width = 320;
	sizehints.min_height = 240;

	XSetWMNormalHints( dpy, win, &sizehints );
	X11_CreateInputContext();

	XMapWindow( dpy, win );

	wmDeleteEvent = XInternAtom( dpy, "WM_DELETE_WINDOW", True );
	if ( wmDeleteEvent == BadValue )
		wmDeleteEvent = None;
	if ( wmDeleteEvent != None )
		XSetWMProtocols( dpy, win, &wmDeleteEvent, 1 );

	window_created = qtrue;

	if ( fullscreen )
	{
		if ( glw_state.randr_active || glw_state.vidmode_active )
			XMoveWindow( dpy, win, glw_state.desktop_x, glw_state.desktop_y );
	}
	else
	{
		XMoveWindow( dpy, win, vid_xpos->integer, vid_ypos->integer );
	}

//	XSync( dpy, False );

	// create rendering context
#ifdef USE_OPENGL_API
	if ( !vulkan )
	{
		ctx = qglXCreateContext( dpy, visinfo, NULL, True );

		if ( ctx == NULL )
		{
			Com_Error( ERR_FATAL, "Error creating GLX context" );
		}

		/* GH: Free the visinfo after we're done with it */
		XFree( visinfo );

		if ( !qglXMakeCurrent( dpy, win, ctx ) )
		{
			Com_Error( ERR_FATAL, "Error setting GLX context" );
		}
	}
	else
	{
		// nothing to do
	}
#endif

	X11_PrepareInputReset();
	Sys_QueEvent( 0, SE_INPUT_RESET, 0, 0, 0, NULL );

	if ( fullscreen )
	{
		XSetInputFocus( dpy, win, RevertToParent, CurrentTime );
	}

//	XSync( dpy, False );
	while ( window_exposed == qfalse )
	{
		HandleEvents();
	}

	if ( !fullscreen ) {
		// Mapping is the first point at which every reparenting WM can expose
		// the real title-bar and border extents. Reapply the persisted client
		// origin in outer-frame coordinates so restarts do not drift by one
		// decoration width/height each time.
		X11_ApplyWindowPlacement( actualWidth, actualHeight,
			&requestedWindowOrigin );
		XSync( dpy, False );
		Cvar_SetIntegerValue( "vid_xpos", win_x );
		Cvar_SetIntegerValue( "vid_ypos", win_y );
	}

	return RSERR_OK;
}


void GLimp_InitGamma( glconfig_t *config )
{
	config->deviceSupportsGamma = qfalse;

	if ( glw_state.randr_gamma )
	{
		Com_Printf( "...using xrandr gamma extension\n" );
		config->deviceSupportsGamma = qtrue;
		return;
	}

	if ( glw_state.vidmode_gamma )
	{
		Com_Printf( "...using vidmode gamma extension\n" );
		config->deviceSupportsGamma = qtrue;
		return;
	}
}


/*
** XErrorHandler
**   the default X error handler exits the application
**   I found out that on some hosts some operations would raise X errors (GLXUnsupportedPrivateRequest)
**   but those don't seem to be fatal .. so the default would be to just ignore them
**   our implementation mimics the default handler behaviour (not completely cause I'm lazy)
*/
static int qXErrorHandler( Display *dpy, XErrorEvent *ev )
{
	static char buf[1024];

	XGetErrorText( dpy, ev->error_code, buf, sizeof( buf ) );
	Com_Printf( "X Error of failed request: %s\n", buf) ;
	Com_Printf( "  Major opcode of failed request: %d\n", ev->request_code );
	Com_Printf( "  Minor opcode of failed request: %d\n", ev->minor_code );
	Com_Printf( "  Serial number of failed request: %d\n", (int)ev->serial );

#ifdef DEBUG
	raise( SIGABRT );
#endif

	return 0;
}


static void InitCvars( void )
{
	// referenced in GLW_StartDriverAndSetMode() so must be inited there
	in_nograb = Cvar_Get( "in_nograb", "0", 0 );
	Cvar_SetDescription( in_nograb, "Do not capture mouse in game, may be useful during online streaming." );

	// turn on-off sub-frame timing of X events, referenced in Sys_XTimeToSysTime
	in_subframe = Cvar_Get( "in_subframe", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( in_subframe, "Toggle X sub-frame event handling." );

	in_dgamouse = Cvar_Get( "in_dgamouse", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( in_dgamouse, "DGA Mouse support." );
	in_shiftedKeys = Cvar_Get( "in_shiftedKeys", "0", CVAR_ARCHIVE_ND );

	in_forceCharset = Cvar_Get( "in_forceCharset", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( in_forceCharset, "Try to translate non-ASCII chars in keyboard input or force EN/US keyboard layout." );
}


void GLimp_QueryDisplayOutput( rendererDisplayOutput_t *output )
{
	if ( !output ) {
		return;
	}

	Com_Memset( output, 0, sizeof( *output ) );
	output->displayIndex = scrnum;
	output->nativeBackend = ROUTPUT_BACKEND_SDR_SRGB;
	output->sdrWhiteNits = 203.0f;
	output->hdrHeadroom = 1.0f;
	output->maxLuminanceNits = 203.0f;
	output->maxFullFrameLuminanceNits = 203.0f;
	Q_strncpyz( output->videoDriver, "x11", sizeof( output->videoDriver ) );
	Q_strncpyz( output->reason,
		"X11 path has no explicit HDR compositor protocol; Linux HDR is only enabled through SDL3 Wayland output checks",
		sizeof( output->reason ) );

	if ( !dpy || !win ) {
		return;
	}

	output->valid = qtrue;
	if ( DisplayString( dpy ) ) {
		Q_strncpyz( output->displayName, DisplayString( dpy ), sizeof( output->displayName ) );
	}
}


#ifdef USE_OPENGL_API
/*
** GLW_LoadOpenGL
**
** GLimp_win.c internal function that that attempts to load and use 
** a specific OpenGL DLL.
*/
static qboolean GLW_LoadOpenGL( const char *name )
{
	qboolean fullscreen;

	if ( r_swapInterval->integer )
		setenv( "vblank_mode", "2", 1 );
	else
		setenv( "vblank_mode", "1", 1 );

	// load the QGL layer
	if ( QGL_Init( name ) )
	{
		rserr_t err;
		fullscreen = ( r_fullscreen->integer != 0 ) ? qtrue : qfalse;
		const int mode = CL_GetRequestedMode( fullscreen );

		// create the window and set up the context
		err = GLW_StartDriverAndSetMode( mode, r_modeFullscreen->string, fullscreen, qfalse /* vulkan */ );
		if ( err != RSERR_OK )
		{
			if ( err == RSERR_FATAL_ERROR )
				goto fail;

			if ( mode != 3 || ( fullscreen && atoi( r_modeFullscreen->string ) != 3 ) )
			{
				Com_Printf( "Setting video mode %d failed, falling back on mode %d\n", mode, 3 );

				if ( GLW_StartDriverAndSetMode( 3, "", fullscreen, qfalse /* vulkan */ ) != RSERR_OK )
				{
					goto fail;
				}
			}
			else
			{
				goto fail;
			}
		}
		return qtrue;
	}
	fail:

	QGL_Shutdown( qtrue );

	return qfalse;
}


static qboolean GLW_StartOpenGL( void )
{
	//
	// load and initialize the specific OpenGL driver
	//
	if ( !GLW_LoadOpenGL( r_glDriver->string ) )
	{
		if ( Q_stricmp( r_glDriver->string, OPENGL_DRIVER_NAME ) != 0 )
		{
			// try default driver
			if ( GLW_LoadOpenGL( OPENGL_DRIVER_NAME ) )
			{
				Cvar_Set( "r_glDriver", OPENGL_DRIVER_NAME );
				r_glDriver->modified = qfalse;
				return qtrue;
			}
		}

		Com_Error( ERR_FATAL, "GLW_StartOpenGL() - could not load OpenGL subsystem\n" );
		return qfalse;
	}

	return qtrue;
}
/*
** GLimp_Init
**
** This routine is responsible for initializing the OS specific portions
** of OpenGL.
*/
void GLimp_Init( glconfig_t *config )
{
	InitSig();

	// initialize variables that may be referenced during window creation/setup
	InitCvars();

	// set up our custom error handler for X failures
	XSetErrorHandler( &qXErrorHandler );

	// feedback to renderer configuration
	glw_state.config = config;

	// load and initialize the specific OpenGL driver
	if ( !GLW_StartOpenGL() )
	{
		return;
	}

	// These values force the UI to disable driver selection
	config->driverType = GLDRV_ICD;
	config->hardwareType = GLHW_GENERIC;

	// optional
#define GLE( ret, name, ... ) q##name = reinterpret_cast<ret (*)( __VA_ARGS__ )>( GL_GetProcAddress( XSTRING( name ) ) );
	QGL_Swp_PROCS;
#undef GLE

	if ( qglXSwapIntervalEXT || qglXSwapIntervalMESA || qglXSwapIntervalSGI )
	{
		Com_Printf( "...using GLX_EXT_swap_control\n" );
		Cvar_SetModified( "r_swapInterval", qtrue ); // force a set next frame
	}
	else
	{
		Com_Printf( "...GLX_EXT_swap_control not found\n" );
	}

	X11_PrepareInputReset();
	Sys_QueEvent( 0, SE_INPUT_RESET, 0, 0, 0, NULL );
	X11_QueueHeldModifiers( 0 );

	IN_Init();
}


/*
** GLimp_EndFrame
** 
** Responsible for doing a swapbuffers and possibly for other stuff
** as yet to be determined.  Probably better not to make this a GLimp
** function and instead do a call to GLimp_SwapBuffers.
*/
void GLimp_EndFrame( void )
{
	//
	// swapinterval stuff
	//
	if ( r_swapInterval->modified ) {
		r_swapInterval->modified = qfalse;

		if ( qglXSwapIntervalEXT ) {
			qglXSwapIntervalEXT( dpy, win, r_swapInterval->integer );
		} else if ( qglXSwapIntervalMESA ) {
			qglXSwapIntervalMESA( r_swapInterval->integer );
		} else if ( qglXSwapIntervalSGI ) {
			qglXSwapIntervalSGI( r_swapInterval->integer );
		}
	}

	// don't flip if drawing to front buffer
	if ( Q_stricmp( cl_drawBuffer->string, "GL_FRONT" ) != 0 )
	{
		qglXSwapBuffers( dpy, win );
	}
}
#endif // USE_OPENGL_API


#ifdef USE_VULKAN_API
/*
** GLW_LoadVulkan
*/
static qboolean GLW_LoadVulkan( void )
{
	if ( r_swapInterval->integer )
		setenv( "vblank_mode", "2", 1 );
	else
		setenv( "vblank_mode", "1", 1 );

	// load the QVK layer
	if ( QVK_Init() )
	{
		rserr_t err;
		qboolean fullscreen = ( r_fullscreen->integer != 0 ) ? qtrue : qfalse;
		const int mode = CL_GetRequestedMode( fullscreen );

		// create the window and set up the context
		err = GLW_StartDriverAndSetMode( mode, r_modeFullscreen->string, fullscreen, qtrue /* vulkan */ );
		if ( err == RSERR_OK )
		{
			return qtrue;
		}
	}

	QVK_Shutdown( qtrue );

	return qfalse;
}


static qboolean GLW_StartVulkan( void )
{
	//
	// load and initialize the specific Vulkan driver
	//
	if ( !GLW_LoadVulkan() )
	{
		Com_Error( ERR_FATAL, "GLW_StartVulkan() - could not load Vulkan subsystem\n" );
		return qfalse;
	}

	return qtrue;
}


/*
** VKimp_Init
**
** This routine is responsible for initializing the OS specific portions
** of Vulkan.
*/
void VKimp_Init( glconfig_t *config )
{
	InitSig();

	// initialize variables that may be referenced during window creation/setup
	InitCvars();

	// set up our custom error handler for X failures
	XSetErrorHandler( &qXErrorHandler );

	// feedback to renderer configuration
	glw_state.config = config;

	// load and initialize the specific Vulkan driver
	if ( !GLW_StartVulkan() )
	{
		return;
	}

	// These values force the UI to disable driver selection
	config->driverType = GLDRV_ICD;
	config->hardwareType = GLHW_GENERIC;

	X11_PrepareInputReset();
	Sys_QueEvent( 0, SE_INPUT_RESET, 0, 0, 0, NULL );
	X11_QueueHeldModifiers( 0 );

	IN_Init();
}
#endif // USE_VULKAN_API


/*****************************************************************************/
/* MOUSE                                                                     */
/*****************************************************************************/

void IN_Restart_f( void );

void IN_Init( void )
{
	Com_DPrintf( "\n------- Input Initialization -------\n" );

	// mouse variables
	in_mouse = Cvar_Get(
		"in_mouse", "1", CVAR_ARCHIVE | CVAR_LATCH | CVAR_CLOUD );
	Cvar_CheckRange( in_mouse, "0", "1", CV_INTEGER );
	Cvar_SetDescription( in_mouse,
		"Mouse data input source:\n" \
		"  0 - disable mouse input\n" \
		"  1 - enable mouse input" );

	if ( in_mouse->integer )
	{
		mouse_avail = qtrue;
	}
	else
	{
		mouse_avail = qfalse;
	}

#ifdef USE_JOYSTICK
	// bk001130 - from cvs.17 (mkv), joystick variables
	in_joystick = Cvar_Get( "in_joystick", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
	Cvar_SetDescription( in_joystick, "Whether or not joystick support is on." );
	// bk001130 - changed this to match win32
	in_joystickDebug = Cvar_Get( "in_debugjoystick", "0", CVAR_TEMP );
	joy_threshold = Cvar_Get( "joy_threshold", "0.15", CVAR_ARCHIVE_ND ); // FIXME: in_joythreshold
	Cvar_CheckRange( joy_threshold, "0", "1", CV_FLOAT );
	Cvar_SetDescription( joy_threshold, "Threshold of joystick moving distance." );

	IN_StartupJoystick(); // bk001130 - from cvs1.17 (mkv)
#endif

	Cmd_AddCommand( "minimize", IN_Minimize );
	Cmd_AddCommand( "in_restart", IN_Restart_f );

	Com_DPrintf( "------------------------------------\n" );
}


void IN_Shutdown( void )
{
#ifdef USE_JOYSTICK
	IN_ShutdownJoystick();
#endif
	IN_EndTemporaryPointerCapture();
	IN_SetPointerConfinement( qfalse );
	IN_ShowWindowCursor( qtrue );
	absolute_pointer_owner = PointerOwner::Gameplay;
	absolute_position_valid = qfalse;
	absolute_position_consumer = 0;
	mouse_avail = qfalse;

	Cmd_RemoveCommand( "minimize" );
	Cmd_RemoveCommand( "in_restart" );
}


void IN_ResetInputState( void )
{
#ifdef USE_JOYSTICK
	IN_ResetJoystickState();
#endif
	absolute_position_valid = qfalse;
	absolute_position_consumer = 0;
	mx = my = 0;
	mwx = window_width / 2;
	mwy = window_height / 2;
	mouseResetTime = Sys_Milliseconds();
	physical_modifier_state =
		gw_active ? X11_ReadPhysicalModifiers() : 0;
	memset( translated_key_by_keycode, 0,
		sizeof( translated_key_by_keycode ) );
	memset( suppressed_key_by_keycode, 0,
		sizeof( suppressed_key_by_keycode ) );
	IN_EndTemporaryPointerCapture();
}


/*
=================
IM_Restart

Restart the input subsystem
=================
*/
void IN_Restart_f( void )
{
	X11_PrepareInputReset();
	Sys_QueEvent( 0, SE_INPUT_RESET, 0, 0, 0, NULL );
	X11_QueueHeldModifiers( 0 );
	IN_DeactivateMouse();
	IN_Shutdown();
	IN_Init();
}


void IN_Frame( void )
{
	X11_ReopenInputMethodIfNeeded();
	X11_SetInputContextFocus( X11_TextInputOwnerActive() );

	const PointerOwner pointerOwner = IN_AbsolutePointerOwnerKind();
	const PointerMode mode = IN_ResolvePointerMode( pointerOwner );

#ifdef USE_JOYSTICK
	if ( gw_active && !gw_minimized ) {
		IN_JoyMove();
	}
#endif

	if ( fnql::input::PointerOwnerReportsAbsolute( pointerOwner ) ) {
		if ( pointerOwner != absolute_pointer_owner ) {
			// The coordinate lane changed; drop drag capture and the dedup cache
			// so the new owner receives a deterministic first position.
			IN_EndTemporaryPointerCapture();
			absolute_position_valid = qfalse;
			absolute_position_consumer = 0;
		}
		absolute_pointer_owner = pointerOwner;
		IN_DeactivateMouse();

		if ( !mode.driveInput ) {
			IN_EndTemporaryPointerCapture();
			IN_SetPointerConfinement( qfalse );
			IN_ShowWindowCursor( qtrue );
			absolute_position_valid = qfalse;
			absolute_position_consumer = 0;
			return;
		}

		IN_SetPointerConfinement( mode.confineToWindow ? qtrue : qfalse );
		IN_ShowWindowCursor( mode.showSystemCursor ? qtrue : qfalse );
		IN_PollAbsolutePointerPosition();
		return;
	}

	if ( absolute_pointer_owner != PointerOwner::Gameplay ) {
		IN_EndTemporaryPointerCapture();
		IN_SetPointerConfinement( qfalse );
		absolute_pointer_owner = PointerOwner::Gameplay;
		absolute_position_valid = qfalse;
		absolute_position_consumer = 0;
	}

	if ( !mode.driveInput || in_nograb->integer ) {
		IN_DeactivateMouse();
		return;
	}

	IN_ActivateMouse();
	if ( mouse_active && keyboard_regrab_pending &&
		!( Key_GetCatcher() & kTextInputCatcherMask ) ) {
		install_kb_grab();
	}
}


/*
=================
Sys_GetClipboardData
=================
*/
struct X11ClipboardSelectionMatch {
	Window requestor;
	Atom selection;
	Atom target;
};


static Bool X11_MatchClipboardSelection(
	Display *, XEvent *event, XPointer opaque )
{
	const X11ClipboardSelectionMatch *match =
		reinterpret_cast<const X11ClipboardSelectionMatch *>( opaque );
	return event->type == SelectionNotify &&
		event->xselection.requestor == match->requestor &&
		event->xselection.selection == match->selection &&
		event->xselection.target == match->target;
}


char *Sys_GetClipboardData( void )
{
	if ( !dpy || !win ) {
		return NULL;
	}

	const Atom xtarget = XInternAtom( dpy, "UTF8_STRING", 0 );
	const Atom property = XInternAtom( dpy, "FNQL_CLIPBOARD", 0 );
	unsigned long nitems, rem;
	unsigned char *data = NULL;
	Atom type;
	XEvent ev;
	char *buf = NULL;
	char *cutBuffer;
	int cutBufferLength;
	int format;
	qboolean selectionReceived = qfalse;
	X11ClipboardSelectionMatch match = {
		win, XA_PRIMARY, xtarget
	};

	if ( dpy && win && xtarget != None && property != None ) {
		XConvertSelection(
			dpy, XA_PRIMARY, xtarget, property, win, CurrentTime );
		XFlush( dpy );

		const std::uint32_t waitStart =
			static_cast<std::uint32_t>( Sys_Milliseconds() );
		for (;;) {
			if ( XCheckIfEvent( dpy, &ev, X11_MatchClipboardSelection,
					reinterpret_cast<XPointer>( &match ) ) ) {
				selectionReceived = qtrue;
				break;
			}

			const std::uint32_t elapsed =
				static_cast<std::uint32_t>( Sys_Milliseconds() ) - waitStart;
			if ( elapsed >= 250u ) {
				break;
			}

			fd_set readSet;
			FD_ZERO( &readSet );
			FD_SET( ConnectionNumber( dpy ), &readSet );
			const std::uint32_t remaining = 250u - elapsed;
			struct timeval timeout;
			timeout.tv_sec = remaining / 1000u;
			timeout.tv_usec = ( remaining % 1000u ) * 1000u;
			const int selected = select(
				ConnectionNumber( dpy ) + 1,
				&readSet, NULL, NULL, &timeout );
			if ( selected <= 0 ) {
				break;
			}
			XEventsQueued( dpy, QueuedAfterReading );
		}
	}

	if ( selectionReceived && ev.xselection.property != None ) {
		if ( XGetWindowProperty( dpy, win, property, 0,
				( MAX_EDIT_LINE + 3 ) / 4, True, AnyPropertyType,
				&type, &format, &nitems, &rem, &data ) == Success ) {
			if ( format == 8 && nitems > 0 &&
				nitems < static_cast<unsigned long>( MAX_EDIT_LINE ) ) {
				buf = static_cast<char *>( Z_Malloc(
					static_cast<int>( nitems ) + 1 ) );
				memcpy( buf, data, nitems );
				buf[nitems] = '\0';
				strtok( buf, "\n\r\b" );
			} else if ( format != 8 ) {
				Com_DPrintf( "Bad X11 clipboard format %i\n", format );
			}
		}
		if ( data ) {
			XFree( data );
			data = NULL;
		}
		if ( buf ) {
			return buf;
		}
	}

	cutBuffer = XFetchBytes( dpy, &cutBufferLength );
	if ( cutBuffer && cutBufferLength > 0 ) {
		buf = static_cast<char *>( Z_Malloc( cutBufferLength + 1 ) );
		Q_strncpyz( buf, cutBuffer, cutBufferLength + 1 );
		strtok( buf, "\n\r\b" );
		XFree( cutBuffer );
		return buf;
	}
	if ( cutBuffer ) {
		XFree( cutBuffer );
	}
	return NULL;
}


/*
=================
Sys_SetClipboardData
=================
*/
void Sys_SetClipboardData( const char *text )
{
	if ( !dpy ) {
		return;
	}

	if ( !text ) {
		text = "";
	}

	/*
	 * The legacy non-SDL X11 backend does not implement full selection-owner
	 * handling. Store the text in the X cut buffer so copy/paste remains usable
	 * for the console without introducing a larger clipboard event path here.
	 */
	XStoreBytes( dpy, text, strlen( text ) );
	XFlush( dpy );
}


/*
=================
Sys_SetClipboardBitmap
=================
*/
void Sys_SetClipboardBitmap( const byte *bitmap, int length )
{
	// TODO: implement
}


#ifdef USE_JOYSTICK
// bk010216 - added stubs for non-Linux UNIXes here
// FIXME - use NO_JOYSTICK or something else generic

#if (defined( __FreeBSD__ ) || defined( __sun)) // rb010123
void IN_StartupJoystick( void ) {}
void IN_ShutdownJoystick( void ) {}
void IN_ResetJoystickState( void ) {}
void IN_JoyMove( void ) {}
#endif
#endif
