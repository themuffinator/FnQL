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
// win_input.c -- win32 mouse and joystick code
// 02/21/97 JCB Added extended DirectInput code to support external controllers.

#ifndef CINTERFACE
#define CINTERFACE
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif

#include "../client/client.h"
#include "win_local.h"
#include "glw_win.h"
#include "win_raii.h"
#include "../client/input_compat.hpp"

#include <cstddef>


typedef struct {
	int			oldButtonState;
	int			oldCursorX;
	int			oldCursorY;
	int			oldCursorConsumer;
	qboolean	cursorPositionValid;

	qboolean	mouseActive;
	qboolean	mouseInitialized;
} WinMouseVars_t;

static WinMouseVars_t s_wmv;

static POINT window_center;
static POINT client_center;

using fnql::input::PointerMode;
using fnql::input::PointerOwner;

static constexpr int kPointerMenuMask = KEYCATCH_UI | KEYCATCH_CGAME | KEYCATCH_BROWSER;

static PointerOwner s_pointerOwner = PointerOwner::Gameplay;
static qboolean s_pointerConfined;
static RECT s_pointerConfineRect;
static qboolean s_pointerConfinementFailureReported;
static qboolean s_legacyMouseDriving;
static qboolean s_gameplayClipActive;
static qboolean s_gameplayClipNeedsRefresh;
static qboolean s_gameplayCaptureOwned;
static qboolean s_gameplayCaptureFailureReported;
static qboolean s_cursorVisibilityRequestValid;
static qboolean s_cursorVisibilityRequested;
static qboolean s_legacyWarpFailureReported;


static int IN_PointerConsumerIdentity( void )
{
	const int catcher = Key_GetCatcher();
	if ( catcher & KEYCATCH_CONSOLE ) {
		return KEYCATCH_CONSOLE;
	}
	if ( catcher & KEYCATCH_BROWSER ) {
		return KEYCATCH_BROWSER;
	}
	if ( catcher & KEYCATCH_UI ) {
		return KEYCATCH_UI;
	}
	if ( catcher & KEYCATCH_CGAME ) {
		return KEYCATCH_CGAME;
	}
	return 0;
}

/*
================
WIN_ResolvePointerOwner

Shared with the SDL and X11 backends through input_compat.hpp so the three
cannot disagree about who owns the pointer for a given key catcher, and shared
with win_wndproc.cpp so the message pump routes each mouse message to the owner
this file is presenting. The console is an overlay that preserves the underlying
UI/browser bits, so it is resolved first; a fullscreen console exposes no
desktop and keeps the established relative gameplay pointer.
================
*/
fnql::input::PointerOwner WIN_ResolvePointerOwner( void )
{
	fnql::input::PointerOwnerInputs inputs;

	inputs.catcher = Key_GetCatcher();
	inputs.consoleMask = KEYCATCH_CONSOLE;
	inputs.menuMask = kPointerMenuMask;
	inputs.consoleUsesAbsolutePointer = !glw_state.cdsFullscreen;

	return fnql::input::ResolvePointerOwner( inputs );
}


static qboolean IN_UseAbsolutePointer( void )
{
	return fnql::input::PointerOwnerReportsAbsolute( WIN_ResolvePointerOwner() )
		? qtrue : qfalse;
}

#ifdef USE_MIDI
//
// MIDI definitions
//
static void IN_StartupMIDI( void );
static void IN_ShutdownMIDI( void );

#define MAX_MIDIIN_DEVICES	8

typedef struct {
	int			numDevices;
	MIDIINCAPS	caps[MAX_MIDIIN_DEVICES];

	HMIDIIN		hMidiIn;
} MidiInfo_t;

static MidiInfo_t s_midiInfo;
#endif

#ifdef USE_JOYSTICK
//
// Joystick definitions
//
#define	JOY_MAX_AXES		6				// X, Y, Z, R, U, V

typedef struct {
	qboolean	avail;
	int			id;			// joystick number
	JOYCAPS		jc;

	DWORD		oldbuttonstate;
	DWORD		oldpovstate;
	int			oldmoveaxisstate[MAX_JOYSTICK_AXIS];
	int			consecutiveReadFailures;

	JOYINFOEX	ji;
} joystickInfo_t;

static	joystickInfo_t	joy;
#endif


#ifdef USE_MIDI
cvar_t	*in_midi;
cvar_t	*in_midiport;
cvar_t	*in_midichannel;
cvar_t	*in_mididevice;
#endif

cvar_t	*in_minimize;
cvar_t	*in_nograb;
cvar_t	*in_lagged;

cvar_t	*in_mouse;
cvar_t  *in_logitechbug;

#ifdef USE_JOYSTICK
cvar_t	*in_joystick;
cvar_t	*in_joystickProfile;
cvar_t	*in_joystickInverted;
cvar_t	*in_joyBallScale;
cvar_t	*in_debugJoystick;
cvar_t	*in_joyHorizViewSensitivity;
cvar_t	*in_joyVertViewSensitivity;
cvar_t	*in_joyHorizViewDeadzone;
cvar_t	*in_joyVertViewDeadzone;
cvar_t	*in_joyHorizMoveDeadzone;
cvar_t	*in_joyVertMoveDeadzone;
cvar_t	*joy_threshold;
#endif

// forward-referenced functions
#ifdef USE_JOYSTICK
void IN_StartupJoystick (void);
void IN_JoyMove(void);
static void IN_ClearWinMMJoystickState( qboolean disable );
#endif

#ifdef USE_MIDI
static void MidiInfo_f( void );
#endif

/*
============================================================

WIN32 MOUSE CONTROL

============================================================
*/


static PointerMode IN_ResolvePointerMode( PointerOwner owner )
{
	fnql::input::PointerModeInputs inputs;

	inputs.owner = owner;
	inputs.focused =
		( gw_active && WIN_WindowFocused() && !WIN_InputSuspended() )
			? true : false;
	inputs.minimized =
		( gw_minimized || WIN_InputSuspended() ) ? true : false;
	inputs.fullscreen = glw_state.cdsFullscreen ? true : false;
	inputs.relativeAvailable = in_mouse->integer != 0;

	return fnql::input::ResolvePointerMode( inputs );
}


/*
================
IN_SetPointerConfinement

Absolute owners keep a visible, freely moving pointer, but a fullscreen window
has no desktop edge to stop it: without a clip region the pointer walks onto
another display, and a click there drops the game out of focus mid-menu.
Confinement is independent of the relative-input capture gameplay installs, so
it is tracked separately from s_wmv.mouseActive. The clip region is re-asserted
whenever the window rect moves, because Windows drops it on deactivation.
================
*/
static void IN_SetPointerConfinement( qboolean confine )
{
	RECT window_rect;

	if ( confine && g_wv.hWnd ) {
		IN_UpdateWindow( &window_rect, qfalse );
		if ( !s_pointerConfined || !EqualRect( &window_rect, &s_pointerConfineRect ) ) {
			if ( ClipCursor( &window_rect ) ) {
				s_pointerConfineRect = window_rect;
				s_pointerConfined = qtrue;
				s_gameplayClipActive = qfalse;
				s_gameplayClipNeedsRefresh = qfalse;
				s_pointerConfinementFailureReported = qfalse;
			} else if ( !s_pointerConfinementFailureReported ) {
				Com_Printf( S_COLOR_YELLOW
					"Unable to confine the mouse pointer (Win32 error %lu)\n",
					GetLastError() );
				s_pointerConfinementFailureReported = qtrue;
			}
		}
		return;
	}

	if ( s_pointerConfined ) {
		if ( ClipCursor( NULL ) ) {
			s_pointerConfined = qfalse;
			s_pointerConfinementFailureReported = qfalse;
		} else if ( !s_pointerConfinementFailureReported ) {
			Com_Printf( S_COLOR_YELLOW
				"Unable to release mouse-pointer confinement (Win32 error %lu)\n",
				GetLastError() );
			s_pointerConfinementFailureReported = qtrue;
		}
	}
}


/*
================
IN_MouseActive
================
*/
qboolean IN_MouseActive( void )
{
	return ( s_wmv.mouseActive && in_nograb->integer == 0 &&
		gw_active && WIN_WindowFocused() && !gw_minimized &&
		!WIN_InputSuspended() )
		? qtrue : qfalse;
}


/*
================
WIN_ProjectClientPointerToDrawable

Win32 reports mouse positions in client pixels, which are the renderer's
drawable pixels only while the renderer resolution matches the client area.
Every absolute consumer - the console, the WebUI browser, and retail's UI and
cgame overlays - works in drawable pixels, so project once here. Shared with
win_wndproc.cpp, which feeds the same lane from WM_MOUSEMOVE and the button
messages.
================
*/
void WIN_ProjectClientPointerToDrawable( int *x, int *y )
{
	fnql::input::PointerProjection projection;
	fnql::input::PointerPosition position;
	RECT client;

	if ( !x || !y || !g_wv.hWnd || !GetClientRect( g_wv.hWnd, &client ) ) {
		return;
	}

	projection.hostWidth = client.right - client.left;
	projection.hostHeight = client.bottom - client.top;
	projection.drawableWidth = cls.glconfig.vidWidth;
	projection.drawableHeight = cls.glconfig.vidHeight;

	position = fnql::input::ProjectPointerToDrawable( *x, *y, projection );
	*x = position.x;
	*y = position.y;
}


/*
================
IN_WindowMouse

Poll while an absolute owner holds the pointer so opening a menu or the console
under a stationary cursor still delivers an initial position without waiting for
a WM_MOUSEMOVE transition.
================
*/
static void IN_WindowMouse( qboolean force = qfalse )
{
	POINT position;
	int x;
	int y;
	const int consumer = IN_PointerConsumerIdentity();

	if ( !g_wv.hWnd || !GetCursorPos( &position )
		|| !ScreenToClient( g_wv.hWnd, &position ) ) {
		return;
	}

	x = position.x;
	y = position.y;
	WIN_ProjectClientPointerToDrawable( &x, &y );

	if ( !force && s_wmv.cursorPositionValid
		&& x == s_wmv.oldCursorX
		&& y == s_wmv.oldCursorY
		&& consumer == s_wmv.oldCursorConsumer ) {
		return;
	}

	if ( !force ) {
		s_wmv.cursorPositionValid = qtrue;
		s_wmv.oldCursorX = x;
		s_wmv.oldCursorY = y;
		s_wmv.oldCursorConsumer = consumer;
	}
	Sys_QueEvent( Sys_Milliseconds(), SE_MOUSE_ABSOLUTE, x, y, 0, NULL );
}


/*
================
IN_UpdateWindow

Called when window gets resized/moved
Updates window center and clip region
================
*/
void IN_UpdateWindow( RECT *window_rect, qboolean updateClipRegion )
{
	RECT rect;

	if ( !window_rect ) 
		window_rect = &rect;

	if ( GetClientRect( g_wv.hWnd, window_rect ) ) {
		POINT pos;
		int sx = 0, sy = 0;

		pos.x = window_rect->left;
		pos.y = window_rect->top;
		ClientToScreen( g_wv.hWnd, &pos );
		window_rect->left = pos.x;
		window_rect->top = pos.y;
		window_rect->right += pos.x;
		window_rect->bottom += pos.y;

		// do not overlap with taskbar
		if ( window_rect->bottom > glw_state.workArea.bottom )
			window_rect->bottom = glw_state.workArea.bottom;
		// ... and with 90 degrees clockwise rotation
		if ( window_rect->right > glw_state.workArea.right )
			window_rect->right = glw_state.workArea.right;

		if ( window_rect->top < glw_state.workArea.top )
			sy = glw_state.workArea.top - window_rect->top;

		if ( window_rect->left < glw_state.workArea.left )
			sx = glw_state.workArea.left - window_rect->left;

		client_center.x = (window_rect->right - window_rect->left + sx) / 2;
		client_center.y = (window_rect->bottom - window_rect->top + sy) / 2;
		window_center = client_center;
		ClientToScreen( g_wv.hWnd, &window_center );

	} else {
		if ( !GetWindowRect( g_wv.hWnd, window_rect ) )
			return;
		window_center.x = ( window_rect->right + window_rect->left )/2;
		window_center.y = ( window_rect->top + window_rect->bottom )/2;
		client_center = window_center;
		ScreenToClient( g_wv.hWnd, &client_center );
	}

	if ( updateClipRegion && s_wmv.mouseActive && gw_active &&
		( s_gameplayCaptureOwned || s_gameplayClipActive ) ) {
		if ( ClipCursor( window_rect ) ) {
			s_gameplayClipActive = qtrue;
			s_gameplayClipNeedsRefresh = qfalse;
		} else {
			// A failed replacement leaves the previous Win32 clip in place.
			// Retain release ownership and request a refresh next frame.
			s_gameplayClipNeedsRefresh = qtrue;
		}
	}
}


/*
================
IN_CaptureMouse
================
*/
static void IN_SetSystemCursorVisible( qboolean visible )
{
	// ShowCursor adjusts a thread-owned display count rather than setting an
	// absolute state. Normalize that count only when FnQL's requested state
	// changes; consulting CURSORINFO every frame is not safe because a NULL
	// client cursor and touch suppression also report "not showing" without
	// implying that the display count needs another increment.
	if ( s_cursorVisibilityRequestValid &&
		s_cursorVisibilityRequested == visible ) {
		return;
	}

	if ( visible ) {
		while ( ShowCursor( TRUE ) < 0 )
			;
	} else {
		while ( ShowCursor( FALSE ) >= 0 )
			;
	}
	s_cursorVisibilityRequestValid = qtrue;
	s_cursorVisibilityRequested = visible;
}


static qboolean IN_CaptureMouse( const RECT *clipRect )
{
	const qboolean clipApplied =
		ClipCursor( clipRect ) ? qtrue : qfalse;

	SetCapture( g_wv.hWnd );
	if ( clipApplied ) {
		s_gameplayClipActive = qtrue;
		s_gameplayClipNeedsRefresh = qfalse;
	} else {
		s_gameplayClipNeedsRefresh = qtrue;
	}
	s_gameplayCaptureOwned =
		GetCapture() == g_wv.hWnd ? qtrue : qfalse;
	if ( !clipApplied || !s_gameplayCaptureOwned ) {
		if ( s_gameplayCaptureOwned && GetCapture() == g_wv.hWnd &&
			( ReleaseCapture() || GetCapture() != g_wv.hWnd ) ) {
			s_gameplayCaptureOwned = qfalse;
		} else if ( GetCapture() != g_wv.hWnd ) {
			s_gameplayCaptureOwned = qfalse;
		}
		if ( s_gameplayClipActive && ClipCursor( NULL ) ) {
			s_gameplayClipActive = qfalse;
			s_gameplayClipNeedsRefresh = qfalse;
		}
		if ( !s_gameplayCaptureFailureReported ) {
			Com_Printf( S_COLOR_YELLOW
				"Unable to apply complete gameplay mouse capture (Win32 error %lu)\n",
				GetLastError() );
			s_gameplayCaptureFailureReported = qtrue;
		}
		IN_SetSystemCursorVisible( qtrue );
		return qfalse;
	}

	s_gameplayCaptureFailureReported = qfalse;
	s_gameplayClipNeedsRefresh = qfalse;

	// Relative gameplay now owns the clip region. Drop the absolute owner's
	// bookkeeping so it re-asserts cleanly on the way back.
	s_pointerConfined = qfalse;
	IN_SetSystemCursorVisible( qfalse );
	return qtrue;
}


/*
================
IN_ActivateWin32Mouse
================
*/
static qboolean IN_ActivateWin32Mouse( void )
{
	RECT window_rect;
	s_legacyMouseDriving = qtrue;
	IN_UpdateWindow( &window_rect, qfalse );
	if ( !SetCursorPos( window_center.x, window_center.y ) ) {
		if ( !s_legacyWarpFailureReported ) {
			Com_Printf( S_COLOR_YELLOW
				"Unable to centre the Win32 gameplay mouse (error %lu)\n",
				GetLastError() );
			s_legacyWarpFailureReported = qtrue;
		}
		s_legacyMouseDriving = qfalse;
		return qfalse;
	}
	if ( !IN_CaptureMouse( &window_rect ) ) {
		s_legacyMouseDriving = qfalse;
		return qfalse;
	}
	s_legacyWarpFailureReported = qfalse;
	return qtrue;
}


/*
================
IN_DeactivateWin32Mouse
================
*/
static void IN_DeactivateWin32Mouse( qboolean restorePointer )
{
	if ( restorePointer && !gw_minimized && !IN_UseAbsolutePointer() ) {
		IN_UpdateWindow( NULL, qfalse );
		SetCursorPos( window_center.x, window_center.y );
	}

	if ( !s_gameplayCaptureOwned || GetCapture() != g_wv.hWnd ||
		ReleaseCapture() ) {
		s_gameplayCaptureOwned = qfalse;
	}
	if ( s_gameplayClipActive && ClipCursor( NULL ) ) {
		s_gameplayClipActive = qfalse;
		s_gameplayClipNeedsRefresh = qfalse;
	} else if ( !s_gameplayClipActive ) {
		s_gameplayClipNeedsRefresh = qfalse;
	}
	if ( !s_gameplayCaptureOwned && !s_gameplayClipActive ) {
		s_gameplayCaptureFailureReported = qfalse;
	} else if ( !s_gameplayCaptureFailureReported ) {
		Com_Printf( S_COLOR_YELLOW
			"Unable to release complete gameplay mouse capture (Win32 error %lu)\n",
			GetLastError() );
		s_gameplayCaptureFailureReported = qtrue;
	}
	s_pointerConfined = qfalse;

	IN_SetSystemCursorVisible( qtrue );
	s_legacyMouseDriving = qfalse;
}


/*
================
IN_Win32Mouse
================
*/
static void IN_Win32Mouse( int *mx, int *my ) 
{
	POINT current_pos = {};

	// find mouse movement
	*mx = 0;
	*my = 0;
	if ( !GetCursorPos( &current_pos ) ) {
		return;
	}

	*mx = current_pos.x - window_center.x;
	*my = current_pos.y - window_center.y;
}


/*
============================================================

RAW INPUT MOUSE CONTROL

============================================================
*/
#define ISWINXP(sys) (sys.dwPlatformId==VER_PLATFORM_WIN32_NT && \
	((sys.dwMajorVersion==5 && sys.dwMinorVersion>=1)||(sys.dwMajorVersion>5)))

typedef UINT (WINAPI *PGRRID)(PRAWINPUTDEVICE pRawInputDevices, PUINT puiNumDevices, UINT cbSize);
typedef BOOL (WINAPI *PRRID)(PCRAWINPUTDEVICE pRawInputDevices, UINT uiNumDevices, UINT cbSize);
typedef UINT (WINAPI *PGRID)(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader);

static	PGRRID	GRRID;
static	PRRID	RRID;
static	PGRID	GRID;

static	BOOL	raw_inited = FALSE;
static	BOOL	raw_activated = FALSE;
static	qboolean raw_driving;
static	HWND	raw_targetWindow;
static	int		raw_wheelRemainder;
static	LONG	raw_absoluteX;
static	LONG	raw_absoluteY;
static	qboolean raw_absoluteValid;
static	HANDLE	raw_absoluteDevice;
static	qboolean raw_removalFailureReported;
static	std::uint32_t raw_removalRetryAfter;
static	qboolean raw_deviceNotifications;
static	qboolean raw_deviceNotificationsUnavailable;
static	qboolean rawReadResetQueued;


static void IN_ClearRawMouseDeltas( void )
{
	g_wv.raw_mx = 0;
	g_wv.raw_my = 0;
	raw_wheelRemainder = 0;
	raw_absoluteX = 0;
	raw_absoluteY = 0;
	raw_absoluteValid = qfalse;
	raw_absoluteDevice = NULL;
}


static void IN_QueueRawMouseReadReset( void )
{
	IN_ClearRawMouseDeltas();
	if ( rawReadResetQueued ) {
		return;
	}

	// A malformed/unreadable WM_INPUT packet may have contained the only
	// button-up for a held binding. Recover at an ordered mouse-only barrier;
	// the latch avoids flooding while the raw-input stream remains unhealthy.
	Sys_QueEvent( g_wv.sysMsgTime, SE_MOUSE_RESET, 0, 0, 0, NULL );
	rawReadResetQueued = qtrue;
}


/*
================
IN_RawMouseDrivesInput

Raw packets are an input source only while registration and the shared mouse
activation state both agree.  WndProc may still receive packets queued before
a source or focus transition, so callers must not infer this from raw support
or general mouse availability alone.
================
*/
qboolean IN_RawMouseDrivesInput( void )
{
	return ( in_mouse && in_mouse->integer == 2 &&
		raw_driving && raw_activated && IN_MouseActive() ) ? qtrue : qfalse;
}


/*
================
IN_InitRawMouse
================
*/
static BOOL IN_InitRawMouse( void ) {

	HMODULE dll;

#ifndef idx64
	if ( !ISWINXP( g_wv.osversion ) ) {
		return FALSE; // operating system is not supported
	}
#endif

	if ( raw_inited ) {
		return TRUE; // already inited
	}

	GRRID = NULL;
	RRID  = NULL;
	GRID  = NULL;

	dll = GetModuleHandle( T("user32") ); // should always success
	if ( !dll ) {
		return FALSE;
	}

	GRRID = (PGRRID) GetProcAddress( dll, "GetRegisteredRawInputDevices" );
	RRID  = (PRRID) GetProcAddress( dll, "RegisterRawInputDevices" );
	GRID  = (PGRID) GetProcAddress( dll, "GetRawInputData" );

	//CloseHandle( dll );

	if ( !GRRID || !RRID || !GRID ) {
        return FALSE;
    }

	raw_inited = TRUE;

	return TRUE;
}


/*
================
IN_ActivateRawMouse
================
*/
static qboolean IN_ActivateRawMouse( void )
{
	RECT		window_rect;
	RAWINPUTDEVICE Rid;
	UINT num;
	int cnt;
	DWORD desiredFlags;

	IN_ClearRawMouseDeltas();
	rawReadResetQueued = qfalse;

	if ( raw_activated && raw_targetWindow == g_wv.hWnd )
	{
		IN_UpdateWindow( &window_rect, qfalse );
		if ( !IN_CaptureMouse( &window_rect ) ) {
			s_legacyMouseDriving = qfalse;
			raw_driving = qfalse;
			return qfalse;
		}
		s_legacyMouseDriving = qfalse;
		raw_driving = qtrue;
		raw_removalFailureReported = qfalse;
		return qtrue; // registration survived a failed removal
	}

	memset( &Rid, 0, sizeof( Rid ) );
	num = 1;
	cnt = GRRID( &Rid, &num, sizeof( Rid ) );
	if ( !g_wv.hWnd )
	{
		Com_Printf( S_COLOR_YELLOW "Cannot register raw mouse input without a window\n" );
		return qfalse;
	}

	IN_UpdateWindow( &window_rect, qfalse );
	desiredFlags = RIDEV_NOLEGACY;
	if ( !raw_deviceNotificationsUnavailable ) {
		desiredFlags |= RIDEV_DEVNOTIFY;
	}

	if ( cnt == 1 &&
		Rid.usUsagePage == HID_USAGE_PAGE_GENERIC &&
		Rid.usUsage == HID_USAGE_GENERIC_MOUSE &&
		Rid.dwFlags == desiredFlags &&
		Rid.hwndTarget == g_wv.hWnd )
	{
		// The desired process-wide mouse registration already exists.
		raw_deviceNotifications =
			( Rid.dwFlags & RIDEV_DEVNOTIFY ) ? qtrue : qfalse;
	}
	else
	{
		// A failed or truncated registration query does not prove that raw input
		// is unavailable.  Registering the desired top-level collection is the
		// definitive operation and replaces any prior mouse registration owned
		// by this process.
		Rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
		Rid.usUsage = HID_USAGE_GENERIC_MOUSE;
		Rid.dwFlags = desiredFlags; // skip all WM_*BUTTON* and WM_MOUSEMOVE
		Rid.hwndTarget = g_wv.hWnd;

		if( !RRID( &Rid, 1, sizeof( Rid ) ) )
		{
			// RIDEV_DEVNOTIFY was introduced after Windows XP and can also be
			// rejected by compatibility layers. Retain raw input there using
			// the older registration contract.
			if ( !( desiredFlags & RIDEV_DEVNOTIFY ) ) {
				Com_Printf( S_COLOR_YELLOW
					"Error registering raw input device\n" );
				raw_driving = qfalse;
				IN_ClearRawMouseDeltas();
				return qfalse;
			}

			Rid.dwFlags = RIDEV_NOLEGACY;
			if ( !RRID( &Rid, 1, sizeof( Rid ) ) ) {
				Com_Printf( S_COLOR_YELLOW
					"Error registering raw input device\n" );
				raw_driving = qfalse;
				IN_ClearRawMouseDeltas();
				return qfalse;
			}
			raw_deviceNotificationsUnavailable = qtrue;
			Com_DPrintf(
				"Raw mouse device notifications unavailable; using legacy registration\n" );
		}
		raw_deviceNotifications =
			( Rid.dwFlags & RIDEV_DEVNOTIFY ) ? qtrue : qfalse;
	}

	raw_activated = TRUE;
	raw_targetWindow = g_wv.hWnd;
	if ( !IN_CaptureMouse( &window_rect ) ) {
		s_legacyMouseDriving = qfalse;
		raw_driving = qfalse;
		return qfalse;
	}

	s_legacyMouseDriving = qfalse;
	raw_driving = qtrue;
	raw_removalFailureReported = qfalse;
	return qtrue;
}


/*
================
IN_RawMouse
================
*/
static void IN_RawMouse( int *mx, int *my ) {

	*mx = g_wv.raw_mx;
	*my = g_wv.raw_my;
}


/*
================
IN_DeactivateRawMouse
================
*/
static void IN_DeactivateRawMouse( void )
{
	const BOOL wasActivated = raw_activated;
	const std::uint32_t now =
		static_cast<std::uint32_t>( Sys_Milliseconds() );

	// Stop accepting queued WM_INPUT packets before asking Windows to remove
	// the registration.  Even if removal fails, the engine must not continue
	// reporting raw input as the active source.
	raw_driving = qfalse;
	IN_ClearRawMouseDeltas();
	rawReadResetQueued = qfalse;

	if ( wasActivated )
	{
		RAWINPUTDEVICE Rid;

		if ( raw_removalFailureReported &&
			static_cast<std::int32_t>( now - raw_removalRetryAfter ) < 0 ) {
			return;
		}

		Rid.usUsagePage = HID_USAGE_PAGE_GENERIC;
		Rid.usUsage = HID_USAGE_GENERIC_MOUSE;
		Rid.dwFlags = RIDEV_REMOVE;
		Rid.hwndTarget = NULL;
		if ( !RRID( &Rid, 1, sizeof( Rid ) ) )
		{
			// A retained RIDEV_NOLEGACY registration would suppress the
			// WM_*BUTTON and wheel messages used by absolute UI owners. Replace
			// it with a normal foreground registration before giving up.
			RAWINPUTDEVICE legacyRid;
			legacyRid.usUsagePage = HID_USAGE_PAGE_GENERIC;
			legacyRid.usUsage = HID_USAGE_GENERIC_MOUSE;
			legacyRid.dwFlags = 0;
			legacyRid.hwndTarget = NULL;
			if ( RRID( &legacyRid, 1, sizeof( legacyRid ) ) ) {
				Com_DPrintf(
					"Raw mouse removal failed; restored legacy mouse messages\n" );
				raw_activated = FALSE;
				raw_targetWindow = NULL;
				raw_deviceNotifications = qfalse;
				raw_removalFailureReported = qfalse;
			} else {
				if ( !raw_removalFailureReported ) {
					Com_Printf( S_COLOR_YELLOW
						"Error removing raw input; retrying while inactive\n" );
				}
				raw_activated = TRUE;
				raw_removalFailureReported = qtrue;
				raw_removalRetryAfter = now + 1000u;
			}
		}
		else
		{
			raw_activated = FALSE;
			raw_targetWindow = NULL;
			raw_deviceNotifications = qfalse;
			raw_removalFailureReported = qfalse;
		}
	}
}


void IN_RawInputDeviceChange( WPARAM change, LPARAM device )
{
	if ( change != GIDC_REMOVAL || !device ||
		!raw_deviceNotifications || !IN_RawMouseDrivesInput() ) {
		return;
	}

	// Raw input aggregates every attached mouse and exposes no retained
	// per-device button snapshot. If any contributing device disappears, clear
	// its motion baseline and balance every logical mouse button.
	IN_ClearRawMouseDeltas();
	Sys_QueEvent( Sys_Milliseconds(), SE_MOUSE_RESET, 0, 0, 0, NULL );
}


/*
============================================================

DIRECT INPUT MOUSE CONTROL

============================================================
*/

#undef DEFINE_GUID

#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        const GUID name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

DEFINE_GUID(GUID_SysMouse,   0x6F1D2B60,0xD5A0,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_XAxis,   0xA36D02E0,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_YAxis,   0xA36D02E1,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);
DEFINE_GUID(GUID_ZAxis,   0xA36D02E2,0xC9F3,0x11CF,0xBF,0xC7,0x44,0x45,0x53,0x54,0x00,0x00);


// Retail Quake Live configures 0x200 buffered DirectInput elements. Match that
// capacity so high-polling mice and frame hitches do not turn an otherwise
// recoverable burst into DI_BUFFEROVERFLOW and an input reset.
#define DINPUT_BUFFERSIZE           0x200
using DirectInputCreateFn = HRESULT (WINAPI *)( HINSTANCE hinst, DWORD dwVersion,
	LPDIRECTINPUT *lplpDirectInput, LPUNKNOWN punkOuter );

#define iDirectInputCreate(a,b,c,d)	pDirectInputCreate(a,b,c,d)

static DirectInputCreateFn pDirectInputCreate;
static fnql::win::ScopedLibrary s_directInputLibrary;
static int directInputWheelRemainder;
static qboolean directInputLossResetQueued;

typedef struct MYDATA {
	LONG  lX;                   // X axis goes here
	LONG  lY;                   // Y axis goes here
	LONG  lZ;                   // Z axis goes here
	BYTE  bButtonA;
	BYTE  bButtonB;
	BYTE  bButtonC;
	BYTE  bButtonD;
	BYTE  bButtonE;
	BYTE  bButtonF;
	BYTE  bButtonG;
	BYTE  bButtonH;
} MYDATA;

static DIOBJECTDATAFORMAT rgodf[] = {
  { &GUID_XAxis,    FIELD_OFFSET(MYDATA, lX),       DIDFT_AXIS | DIDFT_ANYINSTANCE,   0,},
  { &GUID_YAxis,    FIELD_OFFSET(MYDATA, lY),       DIDFT_AXIS | DIDFT_ANYINSTANCE,   0,},
  { &GUID_ZAxis,    FIELD_OFFSET(MYDATA, lZ),       0x80000000 | DIDFT_AXIS | DIDFT_ANYINSTANCE,   0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonA), DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonB), DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonC), 0x80000000 | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonD), 0x80000000 | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonE), 0x80000000 | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonF), 0x80000000 | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonG), 0x80000000 | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
  { 0,              FIELD_OFFSET(MYDATA, bButtonH), 0x80000000 | DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0,},
};

#define NUM_OBJECTS (sizeof(rgodf) / sizeof(rgodf[0]))

// NOTE TTimo: would be easier using c_dfDIMouse or c_dfDIMouse2 
static DIDATAFORMAT	df = {
	sizeof(DIDATAFORMAT),       // this structure
	sizeof(DIOBJECTDATAFORMAT), // size of object data format
	DIDF_RELAXIS,               // absolute axis coordinates
	sizeof(MYDATA),             // device data size
	NUM_OBJECTS,                // number of objects
	rgodf,                      // and here they are
};

static LPDIRECTINPUT		g_pdi;
static LPDIRECTINPUTDEVICE	g_pMouse;
static constexpr DWORD kDInputMouseWheelOffset = static_cast<DWORD>( offsetof( MYDATA, lZ ) );
static constexpr DWORD kDInputMouseFirstButtonOffset = static_cast<DWORD>( offsetof( MYDATA, bButtonA ) );
static constexpr int kDInputMouseButtonKeys[] = {
	K_MOUSE1, K_MOUSE2, K_MOUSE3, K_MOUSE4,
	K_MOUSE5, K_MOUSE6, K_MOUSE7, K_MOUSE8
};

static_assert( offsetof( MYDATA, bButtonH ) - offsetof( MYDATA, bButtonA ) == 7,
	"DirectInput mouse buttons must have contiguous byte offsets" );

static void IN_DIMouse( int *mx, int *my );
static void IN_ShutdownDIMouse( void );

static qboolean IN_LoadDirectInput( void )
{
	if ( !s_directInputLibrary )
	{
		s_directInputLibrary.reset( LoadLibrary( TEXT( "dinput.dll" ) ) );
		if ( !s_directInputLibrary )
		{
			Com_DPrintf( "Couldn't load dinput.dll\n" );
			return qfalse;
		}
	}

	if ( !pDirectInputCreate )
	{
		pDirectInputCreate = reinterpret_cast<DirectInputCreateFn>(
			GetProcAddress( s_directInputLibrary.get(), "DirectInputCreateA" ) );
		if ( !pDirectInputCreate )
		{
			Com_DPrintf( "Couldn't get DI proc addr\n" );
			s_directInputLibrary.reset();
			return qfalse;
		}
	}

	return qtrue;
}

/*
========================
IN_InitDIMouse
========================
*/
static qboolean IN_InitDIMouse( void ) {
    HRESULT		hr;
	DIPROPDWORD	dipdw = {
		{
			sizeof(DIPROPDWORD),        // diph.dwSize
			sizeof(DIPROPHEADER),       // diph.dwHeaderSize
			0,                          // diph.dwObj
			DIPH_DEVICE,                // diph.dwHow
		},
		DINPUT_BUFFERSIZE,              // dwData
	};

	Com_DPrintf( "Initializing DirectInput...\n");

	if ( !IN_LoadDirectInput() ) {
		return qfalse;
	}

	// register with DirectInput and get an IDirectInput to play with.
	hr = iDirectInputCreate( g_wv.hInstance, DIRECTINPUT_VERSION, &g_pdi, NULL);

	if (FAILED(hr)) {
		Com_DPrintf ("iDirectInputCreate failed\n");
		IN_ShutdownDIMouse();
		return qfalse;
	}

	// obtain an interface to the system mouse device.
	hr = IDirectInput_CreateDevice( g_pdi, GUID_SysMouse, &g_pMouse, NULL );

	if (FAILED(hr)) {
		Com_DPrintf ("Couldn't open DI mouse device\n");
		IN_ShutdownDIMouse();
		return qfalse;
	}

	// set the data format to "mouse format".
	hr = IDirectInputDevice_SetDataFormat( g_pMouse, &df );

	if (FAILED(hr)) 	{
		Com_DPrintf ("Couldn't set DI mouse format\n");
		IN_ShutdownDIMouse();
		return qfalse;
	}

	// set the cooperativity level.
	hr = IDirectInputDevice_SetCooperativeLevel( g_pMouse, g_wv.hWnd,
			DISCL_EXCLUSIVE | DISCL_FOREGROUND );

	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=50
	if (FAILED(hr)) {
		Com_DPrintf ("Couldn't set DI coop level\n");
		IN_ShutdownDIMouse();
		return qfalse;
	}


	// set the buffer size to DINPUT_BUFFERSIZE elements.
	// the buffer size is a DWORD property associated with the device
	hr = IDirectInputDevice_SetProperty( g_pMouse, DIPROP_BUFFERSIZE, &dipdw.diph );

	if (FAILED(hr)) {
		Com_DPrintf ("Couldn't set DI buffersize\n");
		IN_ShutdownDIMouse();
		return qfalse;
	}

	Com_DPrintf( "DirectInput initialized.\n");
	return qtrue;
}


/*
==========================
IN_ShutdownDIMouse
==========================
*/
static void IN_ShutdownDIMouse( void ) {
	directInputWheelRemainder = 0;
	directInputLossResetQueued = qfalse;

    if (g_pMouse) {
		IDirectInputDevice_Release( g_pMouse );
		g_pMouse = NULL;
	}

    if (g_pdi) {
		IDirectInput_Release( g_pdi );
		g_pdi = NULL;
	}

	pDirectInputCreate = nullptr;
	s_directInputLibrary.reset();
}


/*
==========================
IN_ActivateDIMouse
==========================
*/
static qboolean IN_ActivateDIMouse( void ) {
	HRESULT		hr;

	if (!g_pMouse) {
		return qfalse;
	}
	s_legacyMouseDriving = qfalse;
	raw_driving = qfalse;
	directInputWheelRemainder = 0;

	// we may fail to reacquire if the window has been recreated
	hr = IDirectInputDevice_Acquire( g_pMouse );
	if (FAILED(hr)) {
		IN_ShutdownDIMouse();
		if ( !IN_InitDIMouse() || !g_pMouse ||
			FAILED( IDirectInputDevice_Acquire( g_pMouse ) ) ) {
			IN_ShutdownDIMouse();
			Com_Printf ("Falling back to Win32 mouse support...\n");
			Cvar_Set( "in_mouse", "-1" );
			return IN_ActivateWin32Mouse();
		}
	}
	directInputLossResetQueued = qfalse;
	IN_SetSystemCursorVisible( qfalse );
	return qtrue;
}


/*
==========================
IN_DeactivateDIMouse
==========================
*/
static void IN_DeactivateDIMouse( void ) {
	if (!g_pMouse) {
		return;
	}
	IDirectInputDevice_Unacquire( g_pMouse );
}


/*
===================
IN_DIMouse
===================
*/
static void IN_DIMouse( int *mx, int *my ) {
	DIDEVICEOBJECTDATA	od;
	MYDATA				state;
	DWORD				dwElements;
	HRESULT				hr;

	if ( !g_pMouse ) {
		return;
	}

	const auto queueLossReset = [&]() {
		*mx = *my = 0;
		directInputWheelRemainder = 0;
		if ( !directInputLossResetQueued ) {
			Sys_QueEvent( Sys_Milliseconds(),
				SE_MOUSE_RESET, 0, 0, 0, NULL );
			directInputLossResetQueued = qtrue;
		}
	};

	// fetch new events
	for (;;)
	{
		dwElements = 1;

		hr = IDirectInputDevice_GetDeviceData( g_pMouse, sizeof( DIDEVICEOBJECTDATA ), &od, &dwElements, 0 );
		if ((hr == DIERR_INPUTLOST) || (hr == DIERR_NOTACQUIRED)) {
			// Loss can include the final button-up. Balance logical state before
			// attempting recovery; a failed Acquire must not strand a binding.
			queueLossReset();
			IDirectInputDevice_Acquire( g_pMouse );
			return;
		}
		if ( hr == DI_BUFFEROVERFLOW ) {
			// At least one buffered transition was lost. An ordered reset is
			// safer than allowing a missing button release to hold a +binding.
			directInputWheelRemainder = 0;
			Sys_QueEvent( Sys_Milliseconds(),
				SE_MOUSE_RESET, 0, 0, 0, NULL );
			*mx = *my = 0;
			return;
		}

		/* Unable to read data or no data available */
		if ( FAILED(hr) ) {
			queueLossReset();
			return;
		}
		directInputLossResetQueued = qfalse;

		if ( dwElements == 0 ) {
			break;
		}

		const DWORD buttonIndex = od.dwOfs - kDInputMouseFirstButtonOffset;
		if ( buttonIndex < ARRAY_LEN( kDInputMouseButtonKeys ) ) {
			IN_WindowMouse( qtrue );
			Sys_QueEvent( Sys_Milliseconds(), SE_KEY,
				kDInputMouseButtonKeys[buttonIndex],
				( od.dwData & 0x80 ) ? qtrue : qfalse, 0, NULL );
			continue;
		}

		// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=50
		if ( od.dwOfs == kDInputMouseWheelOffset ) {
			IN_WindowMouse( qtrue );
			const LONG value = static_cast<LONG>( od.dwData );
			directInputWheelRemainder = fnql::input::SaturatingAddInt(
				directInputWheelRemainder, value );
			int steps = directInputWheelRemainder / WHEEL_DELTA;
			directInputWheelRemainder %= WHEEL_DELTA;
			steps = std::clamp( steps, -32, 32 );
			const int eventTime = Sys_Milliseconds();
			while ( steps < 0 ) {
				Sys_QueEvent( eventTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
				Sys_QueEvent( eventTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
				++steps;
			}
			while ( steps > 0 ) {
				Sys_QueEvent( eventTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
				Sys_QueEvent( eventTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
				--steps;
			}
		}
	}

	// read the raw delta counter and ignore
	// the individual sample time / values
	hr = IDirectInputDevice_GetDeviceState( g_pMouse, sizeof( state ), &state );
	if ( FAILED(hr) ) {
		queueLossReset();
		if ( hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED ) {
			IDirectInputDevice_Acquire( g_pMouse );
		}
		return;
	}
	directInputLossResetQueued = qfalse;
	*mx = state.lX;
	*my = state.lY;
}

/*
============================================================

  MOUSE CONTROL

============================================================
*/

/*
===========
IN_ActivateMouse

Called when the window gains focus or changes in some way
===========
*/
static void IN_ActivateMouse( void )
{
	if ( !s_wmv.mouseInitialized )
		return;

	if ( !in_mouse->integer ) {
		s_wmv.mouseActive = qfalse;
		return;
	}

	if ( s_wmv.mouseActive )
		return;

	IN_ClearRawMouseDeltas();

	if ( in_mouse->integer == -1 ) {
		raw_driving = qfalse;
		s_wmv.mouseActive = IN_ActivateWin32Mouse();
	} else {
		if ( in_mouse->integer == 2 && raw_inited ) {
			if ( IN_ActivateRawMouse() ) {
				s_wmv.mouseActive = qtrue;
				return;
			}

			// Registration or capture failure must not leave RIDEV_NOLEGACY
			// suppressing the eventual fallback's window messages.
			IN_DeactivateRawMouse();
			if ( !g_pMouse ) {
				IN_InitDIMouse();
			}
			if ( g_pMouse ) {
				s_wmv.mouseActive = IN_ActivateDIMouse();
				if ( s_wmv.mouseActive ) {
					Com_Printf( S_COLOR_YELLOW
						"Raw mouse unavailable; using %s for this activation\n",
						s_legacyMouseDriving ? "Win32 mouse" : "DirectInput" );
				}
			} else {
				s_wmv.mouseActive = IN_ActivateWin32Mouse();
				if ( s_wmv.mouseActive ) {
					Com_Printf( S_COLOR_YELLOW
						"Raw mouse unavailable; using Win32 mouse for this activation\n" );
				}
			}
		} else if ( g_pMouse ) {
			raw_driving = qfalse;
			s_wmv.mouseActive = IN_ActivateDIMouse();
		} else {
			raw_driving = qfalse;
			Com_Printf( S_COLOR_YELLOW "Falling back to Win32 mouse support...\n" );
			s_wmv.mouseActive = IN_ActivateWin32Mouse();
		}
	}
}


/*
===========
IN_DeactivateMouse

Called when the window loses focus
===========
*/
static void IN_DeactivateMouse( void )
{
	IN_ClearRawMouseDeltas();

	if ( !s_wmv.mouseActive ) {
		IN_DeactivateRawMouse();
		if ( s_gameplayCaptureOwned || s_gameplayClipActive ) {
			IN_DeactivateWin32Mouse( qfalse );
		}
		return;
	}

	if ( !s_wmv.mouseInitialized )
		return;

	s_wmv.oldButtonState = 0;
	s_wmv.mouseActive = qfalse;

	IN_DeactivateDIMouse();
	IN_DeactivateRawMouse();
	IN_DeactivateWin32Mouse( qtrue );
}


/*
===========
IN_StartupMouse
===========
*/
static void IN_StartupMouse( void )
{
	s_wmv.mouseInitialized = qfalse;
	raw_driving = qfalse;
	s_legacyMouseDriving = qfalse;
	IN_ClearRawMouseDeltas();

	if ( in_mouse->integer == 0 ) {
		Com_DPrintf( "Mouse control not active.\n" );
		// Pointer presentation and absolute UI input remain initialized, as in
		// retail; only the relative gameplay source is disabled.
		s_wmv.mouseInitialized = qtrue;
		return;
	}

	if ( in_mouse->integer == -1 ) {
		Com_DPrintf( "Skipping check for Raw/DirectInput\n" ); 
	} else {

		if ( !g_wv.hWnd ) {
			Com_Error( ERR_FATAL, "No window for mouse init" );
		}

		if ( in_mouse->integer == 2 && IN_InitRawMouse() ) {
			s_wmv.mouseInitialized = qtrue;
			Com_DPrintf( "Raw mouse input initialized.\n" );
			return;
		}

		if ( in_mouse->integer >= 1 && IN_InitDIMouse() ) {
			s_wmv.mouseInitialized = qtrue;
			return;
		}
		Com_DPrintf( "Falling back to Win32 mouse support...\n" );
	}

	s_wmv.mouseInitialized = qtrue;
}


/*
===========
IN_LegacyMouseDrivesInput

The WM_MOUSEMOVE/WM_*BUTTON* fall-through in the message pump is a real input
source only while neither raw input nor DirectInput owns the device. While raw
input is registered Windows suppresses legacy mouse messages, so any that still
arrive were queued before (re)registration: stale positions from the click that
closed an in-game menu, or from crossing the window while regaining focus.
Converting one of those into a delta kicks the view by (position - window
centre) without any physical mouse motion, so the pump must drop them.
===========
*/
qboolean IN_LegacyMouseDrivesInput( void )
{
	return ( s_legacyMouseDriving && IN_MouseActive() ) ? qtrue : qfalse;
}


/*
===========
IN_Win32MouseEvent
===========
*/
void IN_Win32MouseEvent( int x, int y, int mstate )
{
	int dx, dy;

	if ( in_lagged->integer ) {
		
	} else {
		dx = x - g_wv.mouse.x;
		dy = y - g_wv.mouse.y;
		g_wv.mouse.x = x;
		g_wv.mouse.y = y;
		if ( dx || dy ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_MOUSE, dx, dy, 0, NULL );
		}
	}

#define CHECK_BUTTON(button) \
	if ( mstate & (1<<(button-1)) ) { \
		if ( !(s_wmv.oldButtonState & (1<<(button-1))) ) \
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MOUSE##button, qtrue, 0, NULL ); \
	} else { \
		if 	( s_wmv.oldButtonState & (1<<(button-1)) ) \
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MOUSE##button, qfalse, 0, NULL ); \
	}

	// perform button actions
	CHECK_BUTTON(1);
	CHECK_BUTTON(2);
	CHECK_BUTTON(3);
	CHECK_BUTTON(4);
	CHECK_BUTTON(5);

#undef CHECK_BUTTON

	s_wmv.oldButtonState = mstate;
}


/*
===========
IN_RawMouseEvent
===========
*/
void IN_RawMouseEvent( LPARAM lParam )
{
	UINT err, dwSize;
	union {
		BYTE lpb[40];
		RAWINPUT raw;
	} u;

	if ( !IN_RawMouseDrivesInput() ) {
		return;
	}

	dwSize = sizeof( u.raw );

	err = GRID( (HRAWINPUT) lParam, RID_INPUT, &u.raw, &dwSize, sizeof( RAWINPUTHEADER ) );
	if ( err == static_cast<UINT>( -1 ) || err != dwSize ) {
		IN_QueueRawMouseReadReset();
		return;
	}

	if ( u.raw.header.dwType != RIM_TYPEMOUSE )
		return;

	rawReadResetQueued = qfalse;

	// MOUSE_MOVE_RELATIVE is zero.  Test only the absolute-mode bit so valid
	// relative packets carrying MOUSE_MOVE_NOCOALESCE or
	// MOUSE_ATTRIBUTES_CHANGED still contribute motion.  Button and wheel
	// transitions remain meaningful even on absolute/RDP packets and are
	// therefore processed independently below.
	if ( ( u.raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE ) == MOUSE_MOVE_RELATIVE ) {
		raw_absoluteValid = qfalse;
		raw_absoluteDevice = NULL;
		if ( u.raw.data.mouse.lLastX || u.raw.data.mouse.lLastY ) {
			if ( in_lagged->integer ) {
				g_wv.raw_mx = fnql::input::SaturatingAddInt(
					g_wv.raw_mx, u.raw.data.mouse.lLastX );
				g_wv.raw_my = fnql::input::SaturatingAddInt(
					g_wv.raw_my, u.raw.data.mouse.lLastY );
			} else {
				Sys_QueEvent( g_wv.sysMsgTime, SE_MOUSE, u.raw.data.mouse.lLastX,
					u.raw.data.mouse.lLastY, 0, NULL );
			}
		}
	} else if ( u.raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE ) {
		if ( raw_absoluteDevice != u.raw.header.hDevice ||
			( u.raw.data.mouse.usFlags & MOUSE_ATTRIBUTES_CHANGED ) ) {
			raw_absoluteValid = qfalse;
		}
		const int originX = ( u.raw.data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP )
			? GetSystemMetrics( SM_XVIRTUALSCREEN ) : 0;
		const int originY = ( u.raw.data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP )
			? GetSystemMetrics( SM_YVIRTUALSCREEN ) : 0;
		const int extentX = ( u.raw.data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP )
			? GetSystemMetrics( SM_CXVIRTUALSCREEN ) : GetSystemMetrics( SM_CXSCREEN );
		const int extentY = ( u.raw.data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP )
			? GetSystemMetrics( SM_CYVIRTUALSCREEN ) : GetSystemMetrics( SM_CYSCREEN );
		const LONG absoluteX = originX + MulDiv(
			u.raw.data.mouse.lLastX, ( std::max )( extentX - 1, 1 ), 65535 );
		const LONG absoluteY = originY + MulDiv(
			u.raw.data.mouse.lLastY, ( std::max )( extentY - 1, 1 ), 65535 );

		if ( raw_absoluteValid ) {
			const int dx = fnql::input::SaturatingIntFromInt64(
				static_cast<std::int64_t>( absoluteX ) - raw_absoluteX );
			const int dy = fnql::input::SaturatingIntFromInt64(
				static_cast<std::int64_t>( absoluteY ) - raw_absoluteY );
			if ( dx || dy ) {
				if ( in_lagged->integer ) {
					g_wv.raw_mx = fnql::input::SaturatingAddInt( g_wv.raw_mx, dx );
					g_wv.raw_my = fnql::input::SaturatingAddInt( g_wv.raw_my, dy );
				} else {
					Sys_QueEvent( g_wv.sysMsgTime, SE_MOUSE, dx, dy, 0, NULL );
				}
			}
		}
		raw_absoluteX = absoluteX;
		raw_absoluteY = absoluteY;
		raw_absoluteValid = qtrue;
		raw_absoluteDevice = u.raw.header.hDevice;
	}

	if ( u.raw.data.mouse.usButtonFlags ) {
		// Preserve position-before-button ordering across an Escape/menu
		// transition queued earlier in this same Win32 message drain.
		IN_WindowMouse( qtrue );
	}

	if ( !u.raw.data.mouse.usButtonFlags )
		return;

#define CHECK_RAW_BUTTON(button) \
	if ( u.raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_##button##_DOWN ) \
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MOUSE##button, qtrue, 0, NULL ); \
	if ( u.raw.data.mouse.usButtonFlags & RI_MOUSE_BUTTON_##button##_UP ) \
		Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MOUSE##button, qfalse, 0, NULL )

	CHECK_RAW_BUTTON(1);
	CHECK_RAW_BUTTON(2);
	CHECK_RAW_BUTTON(3);
	CHECK_RAW_BUTTON(4);
	CHECK_RAW_BUTTON(5);

#undef CHECK_RAW_BUTTON

	if ( u.raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL ) 
	{
		const int data = static_cast<short>( u.raw.data.mouse.usButtonData );
		raw_wheelRemainder = fnql::input::SaturatingAddInt(
			raw_wheelRemainder, data );
		int steps = raw_wheelRemainder / WHEEL_DELTA;
		raw_wheelRemainder %= WHEEL_DELTA;
		steps = std::clamp( steps, -32, 32 );
		if ( steps > 0 )
		{
			while( steps-- > 0 )
			{
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
			}
		}
		else
		{
			while( steps++ < 0 )
			{
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
			}
		}
	}
}


/*
===========
IN_MouseMove
===========
*/
static void IN_MouseMove( void ) {
	int		mx = 0, my = 0;

	if ( raw_driving ) {
		if ( in_lagged->integer ) {
			IN_RawMouse( &mx, &my );
		}
		g_wv.raw_mx = 0;
		g_wv.raw_my = 0;
	} else if ( g_pMouse && !s_legacyMouseDriving ) {
		IN_DIMouse( &mx, &my );
	} else {
		if ( in_lagged->integer ) {
			IN_Win32Mouse( &mx, &my );
		}
		g_wv.raw_mx = 0;
		g_wv.raw_my = 0;

		// force the mouse to the center, so there's room to move
		if ( s_legacyMouseDriving &&
			!SetCursorPos( window_center.x, window_center.y ) ) {
			if ( !s_legacyWarpFailureReported ) {
				Com_Printf( S_COLOR_YELLOW
					"Lost Win32 gameplay mouse centering (error %lu)\n",
					GetLastError() );
				s_legacyWarpFailureReported = qtrue;
			}
			mx = 0;
			my = 0;
			s_wmv.mouseActive = qfalse;
			IN_DeactivateWin32Mouse( qfalse );
		} else if ( s_legacyMouseDriving ) {
			s_legacyWarpFailureReported = qfalse;
		}

		// reset delta base
		g_wv.mouse = client_center;
	}

	if ( !mx && !my ) {
		return;
	}

	Sys_QueEvent( 0, SE_MOUSE, mx, my, 0, NULL );
}


/*
	in_minimize processing
*/

extern int HotKey;
extern void Win_RemoveHotkey( void );
extern void Win_AddHotkey( void );

extern int Win32_GetKey( const char **s, char *buf, int buflen );

/*
=========================================================================

=========================================================================
*/
static void IN_GetHotkey( cvar_t *var, int *pHotKey ) {

	char	kset[256], buf[64];
	const char *s;
	int		i, code;

	if ( !pHotKey )
		return;

	*pHotKey = 0;

	if ( !var )
		return;

	s = var->string;

	if ( !s ) {
		Win_RemoveHotkey();
		return;
	}

	memset( kset, 0, sizeof( kset ) );

	for ( i = 0; i < 4; i++ ) 
	{
		code = Win32_GetKey( &s, buf, sizeof( buf ) );
		if ( code == 0 ) // no more tokens
			break;
		if ( code < 0 || kset[code & 0xFF ] ||
			(	code != VK_CONTROL && code != VK_LCONTROL && code != VK_RCONTROL
				&& code != VK_MENU && code != VK_LMENU && code != VK_RMENU
				&& code != VK_SHIFT && code != VK_LSHIFT && code != VK_RSHIFT
				&& code != (VK_LWIN|HK_MOD_LWIN) && code != (VK_RWIN|HK_MOD_RWIN)
				&& *pHotKey & 0xFF )) {
			Com_Printf( "%s:" S_COLOR_YELLOW " invalid token %s\n", var->name, buf );
			*pHotKey = 0;
			break;
		}
		kset[code & 0xFF] = 1;
		switch ( code ) {
			case VK_MENU:	 *pHotKey |= HK_MOD_ALT; break;
			case VK_LMENU:	 *pHotKey |= (HK_MOD_ALT|HK_MOD_LALT); break;
			case VK_RMENU:	 *pHotKey |= (HK_MOD_ALT|HK_MOD_RALT); break;
			case VK_CONTROL: *pHotKey |= HK_MOD_CONTROL; break;
			case VK_LCONTROL:*pHotKey |= (HK_MOD_CONTROL|HK_MOD_LCONTROL); break;
			case VK_RCONTROL:*pHotKey |= (HK_MOD_CONTROL|HK_MOD_RCONTROL); break;
			case VK_SHIFT:	 *pHotKey |= HK_MOD_SHIFT; break;
			case VK_LSHIFT:	 *pHotKey |= HK_MOD_SHIFT|HK_MOD_LSHIFT; break;
			case VK_RSHIFT:	 *pHotKey |= HK_MOD_SHIFT|HK_MOD_RSHIFT; break;
			case VK_LWIN:	 *pHotKey |= HK_MOD_WIN; break;
			case (VK_LWIN|HK_MOD_LWIN): *pHotKey |= (HK_MOD_WIN|HK_MOD_LWIN); break;
			case (VK_RWIN|HK_MOD_RWIN): *pHotKey |= (HK_MOD_WIN|HK_MOD_RWIN); break;
			default:		 *pHotKey |= (code & 0xFF); break;
		};
    }

	if ( i == 0 ) 
	{
		Win_RemoveHotkey();
		return;
	}

	if ( *pHotKey == VK_OEM_3 // '~'
			|| *pHotKey == VK_RETURN
			|| *pHotKey == HK_MOD_WIN
			|| *pHotKey == (HK_MOD_WIN|HK_MOD_LWIN)
			|| *pHotKey == (HK_MOD_WIN|HK_MOD_RWIN)
			|| *pHotKey == (VK_RETURN|HK_MOD_ALT)
			|| *pHotKey == (HK_MOD_CONTROL|VK_PAUSE)) {
		Com_Printf( "%s:" S_COLOR_YELLOW " invalid hotkey %s\n", var->name, var->string );
		*pHotKey = 0;
	}

	//Com_Printf("GetHotkey: %06X\n",*HotKey);
	Win_RemoveHotkey();
	Win_AddHotkey();
}


/*
===========
IN_Minimize
===========
*/
static void IN_Minimize( void )
{
	if ( !CL_VideoRecording() || ( re.CanMinimize && re.CanMinimize() ) )
		WIN_Minimize();
}


/*
===========
IN_Startup
===========
*/
void IN_Startup( void ) {
	Com_DPrintf( "\n------- Input Initialization -------\n" );
	IN_StartupMouse();
#ifdef USE_JOYSTICK
	IN_StartupJoystick ();
#endif
#ifdef USE_MIDI
	IN_StartupMIDI();
#endif
	Com_DPrintf( "------------------------------------\n" );

	in_mouse->modified = qfalse;
#ifdef USE_JOYSTICK
	in_joystick->modified = qfalse;
#endif
}


/*
===========
IN_Shutdown
===========
*/
void IN_ResetInputState( void )
{
	WIN_ReleaseTemporaryMouseCapture();
	IN_ClearRawMouseDeltas();
	rawReadResetQueued = qfalse;
	directInputWheelRemainder = 0;
	directInputLossResetQueued = qfalse;
	s_wmv.oldButtonState = 0;
	s_wmv.cursorPositionValid = qfalse;
	g_wv.mouse = client_center;
	WIN_ResetMessageInputState();
#ifdef USE_JOYSTICK
	// SE_INPUT_RESET clears the client's logical controller state. Drop the
	// WinMM producer snapshot at the same boundary so controls still held after
	// focus restoration are emitted again on the first fresh poll.
	IN_ClearWinMMJoystickState( qfalse );
#endif
}


void IN_Shutdown( void ) {
	WIN_ReleaseTemporaryMouseCapture();
	IN_SetPointerConfinement( qfalse );
	s_pointerOwner = PointerOwner::Gameplay;
	IN_DeactivateMouse();
	IN_ShutdownDIMouse();
#ifdef USE_MIDI
	IN_ShutdownMIDI();
	Cmd_RemoveCommand( "midiinfo" );
#endif
	Cmd_RemoveCommand( "minimize" );
	Cmd_RemoveCommand( "in_restart" );
}


/*
===========
IN_Init
===========
*/
void IN_Restart_f( void );

void IN_Init( void ) {

#ifdef USE_MIDI
	// MIDI input controler variables
	in_midi = Cvar_Get( "in_midi", "0", CVAR_ARCHIVE );
	in_midiport = Cvar_Get( "in_midiport", "1", CVAR_ARCHIVE );
	Cvar_SetDescription( in_midiport, "Toggle the use of a midi port as an input device." );
	in_midichannel = Cvar_Get( "in_midichannel", "1", CVAR_ARCHIVE );
	Cvar_CheckRange( in_midichannel, "1", "16", CV_INTEGER );
	Cvar_SetDescription( in_midichannel, "Toggle the use of a midi channel as an input device." );
	in_mididevice = Cvar_Get( "in_mididevice", "0", CVAR_ARCHIVE );
	Cvar_SetDescription( in_mididevice, "Toggle the use of a midi device as an input device." );
	Cmd_AddCommand( "midiinfo", MidiInfo_f );
#endif

#ifdef USE_JOYSTICK
	// joystick variables
	in_joystick = Cvar_Get( "in_joystick", "0", CVAR_ARCHIVE | CVAR_LATCH );
	Cvar_SetDescription( in_joystick, "Whether or not joystick support is on." );
	in_joystickProfile = Cvar_Get( "in_joystickProfile", "0", CVAR_ARCHIVE | CVAR_LATCH );
	Cvar_CheckRange( in_joystickProfile, "0", "1", CV_INTEGER );
	Cvar_SetDescription( in_joystickProfile,
		"Legacy WinMM joystick profile: 0 preserves FnQ3 direction/trackball input; 1 enables Quake Live X/Y movement and R/U view input." );
	in_joystickInverted = Cvar_Get( "in_joystick_inverted", "0", CVAR_ARCHIVE );
	Cvar_SetDescription( in_joystickInverted, "Invert vertical view input in the Quake Live WinMM joystick profile." );
	in_joyBallScale = Cvar_Get( "in_joyBallScale", "0.02", CVAR_ARCHIVE );
	Cvar_SetDescription( in_joyBallScale, "Legacy trackball scale, or X/Y movement scale in the Quake Live WinMM joystick profile (retail default 1.0)." );
	in_debugJoystick = Cvar_Get( "in_debugjoystick", "0", CVAR_TEMP );
	joy_threshold = Cvar_Get( "joy_threshold", "0.15", CVAR_ARCHIVE );
	Cvar_CheckRange( joy_threshold, "0", "1", CV_FLOAT );
	Cvar_SetDescription( joy_threshold, "Threshold of joystick moving distance." );
	in_joyHorizViewSensitivity = Cvar_Get( "in_joyHorizViewSensitivity", "20.0", CVAR_ARCHIVE );
	in_joyVertViewSensitivity = Cvar_Get( "in_joyVertViewSensitivity", "15.0", CVAR_ARCHIVE );
	in_joyHorizViewDeadzone = Cvar_Get( "in_joyHorizViewDeadzone", "0.15", CVAR_ARCHIVE );
	in_joyVertViewDeadzone = Cvar_Get( "in_joyVertViewDeadzone", "0.15", CVAR_ARCHIVE );
	in_joyHorizMoveDeadzone = Cvar_Get( "in_joyHorizMoveDeadzone", "0.50", CVAR_ARCHIVE );
	in_joyVertMoveDeadzone = Cvar_Get( "in_joyVertMoveDeadzone", "0.15", CVAR_ARCHIVE );
	Cvar_CheckRange( in_joyHorizViewDeadzone, "0", "1", CV_FLOAT );
	Cvar_CheckRange( in_joyVertViewDeadzone, "0", "1", CV_FLOAT );
	Cvar_CheckRange( in_joyHorizMoveDeadzone, "0", "1", CV_FLOAT );
	Cvar_CheckRange( in_joyVertMoveDeadzone, "0", "1", CV_FLOAT );
	Cvar_SetDescription( in_joyHorizViewSensitivity, "Horizontal view sensitivity for the Quake Live WinMM joystick profile." );
	Cvar_SetDescription( in_joyVertViewSensitivity, "Vertical view sensitivity for the Quake Live WinMM joystick profile." );
	Cvar_SetDescription( in_joyHorizViewDeadzone, "Horizontal view deadzone for the Quake Live WinMM joystick profile." );
	Cvar_SetDescription( in_joyVertViewDeadzone, "Vertical view deadzone for the Quake Live WinMM joystick profile." );
	Cvar_SetDescription( in_joyHorizMoveDeadzone, "Horizontal movement deadzone for the Quake Live WinMM joystick profile." );
	Cvar_SetDescription( in_joyVertMoveDeadzone, "Vertical movement deadzone for the Quake Live WinMM joystick profile." );
#endif

	// mouse variables
	in_mouse = Cvar_Get ("in_mouse", "2", CVAR_ARCHIVE | CVAR_LATCH | CVAR_CLOUD );
	Cvar_CheckRange( in_mouse, "-1", "2", CV_INTEGER );
	Cvar_SetDescription( in_mouse,
		"Mouse data input source:\n" \
		"  0 - disable mouse input\n" \
		"  1 - DirectInput mouse\n" \
		"  2 - Quake Live raw mouse\n" \
		" -1 - win32 mouse" );
		
	in_nograb = Cvar_Get( "in_nograb", "0", 0 );
	Cvar_SetDescription( in_nograb, "Do not capture mouse in game, may be useful during online streaming." );
	in_lagged = Cvar_Get( "in_lagged", "0", 0 );
	Cvar_SetDescription( in_lagged, 
		"Mouse movement processing order:\n" \
		" 0 - before rendering\n" \
		" 1 - before framerate limiter" );

	in_logitechbug = Cvar_Get( "in_logitechbug", "0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( in_logitechbug, "Toggle the use of special code in the game that addresses a bug in the logitech mouse driver software." );

	in_minimize	= Cvar_Get( "in_minimize", "", CVAR_ARCHIVE | CVAR_LATCH );
	Cvar_SetDescription( in_minimize, "Hotkey for minimize/restore main window." );

	Cmd_AddCommand( "minimize", IN_Minimize );
	Cmd_AddCommand( "in_restart", IN_Restart_f );

	IN_GetHotkey( in_minimize, &HotKey );

	IN_Startup();
}


/*
===========
IN_Activate

Called when the main window gains or loses focus.
The window may have been destroyed and recreated
between a deactivate and an activate.
===========
*/
void IN_Activate( qboolean active ) {

	if ( !active ) {
		// Windows drops the clip region on deactivation, so the confinement
		// latch has to be invalidated here or IN_Frame would never re-assert it.
		IN_SetPointerConfinement( qfalse );
		IN_DeactivateMouse();
	}
}


/*
==================
IN_Frame

Called every frame, even if not generating commands
==================
*/
void IN_Frame( void ) {
	PointerOwner owner;
	PointerMode mode;

	// post joystick events
#ifdef USE_JOYSTICK
	if ( gw_active && WIN_WindowFocused() && !gw_minimized &&
		!WIN_InputSuspended() ) {
		IN_JoyMove();
	}
#endif

	// mouseInitialized implies IN_Init registered the input cvars the pointer
	// mode is resolved from, so nothing above may dereference them.
	if ( !s_wmv.mouseInitialized ) {
		return;
	}

	owner = WIN_ResolvePointerOwner();
	mode = IN_ResolvePointerMode( owner );

	if ( fnql::input::PointerOwnerReportsAbsolute( owner ) ) {
		if ( owner != s_pointerOwner ) {
			// The coordinate lane changed; drop drag capture and the dedup
			// cache so the new owner receives a deterministic first position.
			WIN_ReleaseTemporaryMouseCapture();
			s_wmv.cursorPositionValid = qfalse;
		}
		s_pointerOwner = owner;
		IN_DeactivateMouse();
		// Win32's ShowCursor count is desktop-wide for this UI thread. Keep it
		// visible for every free, absolute pointer; WM_SETCURSOR independently
		// selects NULL over the windowed console's client area.
		IN_SetSystemCursorVisible( qtrue );

		if ( !mode.driveInput ) {
			// Unfocused or minimized. Stop driving the overlay cursor from the
			// desktop pointer, and give back capture and confinement, so the
			// menu does not track the pointer while another app has focus.
			WIN_ReleaseTemporaryMouseCapture();
			IN_SetPointerConfinement( qfalse );
			s_wmv.cursorPositionValid = qfalse;
			return;
		}

		WIN_RebuildTemporaryMouseCapture();
		IN_SetPointerConfinement( mode.confineToWindow ? qtrue : qfalse );
		IN_WindowMouse();
		return;
	}

	WIN_ReleaseTemporaryMouseCapture();
	IN_SetPointerConfinement( qfalse );
	s_pointerOwner = owner;
	s_wmv.cursorPositionValid = qfalse;

	if ( !mode.driveInput || in_nograb->integer ) {
		IN_DeactivateMouse();
		return;
	}

	IN_ActivateMouse();
	if ( !s_wmv.mouseActive ) {
		return;
	}

	if ( s_gameplayCaptureOwned && GetCapture() != g_wv.hWnd ) {
		s_gameplayCaptureOwned = qfalse;
	}
	if ( ( raw_driving || s_legacyMouseDriving ) &&
		( !s_gameplayClipActive || s_gameplayClipNeedsRefresh ||
			!s_gameplayCaptureOwned ) ) {
		RECT windowRect;
		IN_UpdateWindow( &windowRect, qfalse );
		if ( !IN_CaptureMouse( &windowRect ) ) {
			const qboolean wasRaw = raw_driving;
			s_wmv.mouseActive = qfalse;
			raw_driving = qfalse;
			s_legacyMouseDriving = qfalse;
			if ( wasRaw ) {
				IN_DeactivateRawMouse();
			}
			return;
		}
	}

	//WIN_DisableAltTab();
	WIN_EnableHook();

	// post events to the system que
	IN_MouseMove();
}


/*
=================
In_Restart_f

Restart the input subsystem
=================
*/
void IN_Restart_f( void ) {
	WIN_QueueInputReset( qtrue );
	IN_Shutdown();
	IN_Init();
}


/*
=========================================================================

JOYSTICK

=========================================================================
*/

#ifdef USE_JOYSTICK
/* 
=============== 
IN_StartupJoystick 
=============== 
*/  
static void IN_ClearWinMMJoystickState( qboolean disable )
{
	joy.oldbuttonstate = 0;
	joy.oldpovstate = 0;
	for ( int& axis : joy.oldmoveaxisstate ) {
		axis = 0;
	}
	Com_Memset( &joy.ji, 0, sizeof( joy.ji ) );
	joy.consecutiveReadFailures = 0;
	if ( disable ) {
		joy.avail = qfalse;
		Cvar_Set( "ui_joyavail", "0" );
	}
}


void IN_StartupJoystick (void) {
	int			numdevs;
	MMRESULT	mmr;

	// assume no joystick
	IN_ClearWinMMJoystickState( qtrue );

	if (! in_joystick->integer ) {
		Com_DPrintf ("Joystick is not active.\n");
		return;
	}

	// verify joystick driver is present
	if ((numdevs = joyGetNumDevs ()) == 0)
	{
		Com_DPrintf ("joystick not found -- driver not present\n");
		return;
	}

	// cycle through the joystick ids for the first valid one
	mmr = 0;
	for (joy.id=0 ; joy.id<numdevs ; joy.id++)
	{
		Com_Memset (&joy.ji, 0, sizeof(joy.ji));
		joy.ji.dwSize = sizeof(joy.ji);
		joy.ji.dwFlags = JOY_RETURNCENTERED;

		if ((mmr = joyGetPosEx (joy.id, &joy.ji)) == JOYERR_NOERROR)
			break;
	} 

	// abort startup if we didn't find a valid joystick
	if (mmr != JOYERR_NOERROR)
	{
		Com_DPrintf ("joystick not found -- no valid joysticks (%x)\n", mmr);
		return;
	}

	// get the capabilities of the selected joystick
	// abort startup if command fails
	Com_Memset (&joy.jc, 0, sizeof(joy.jc));
	if ((mmr = joyGetDevCaps (joy.id, &joy.jc, sizeof(joy.jc))) != JOYERR_NOERROR)
	{
		Com_DPrintf ("joystick not found -- invalid joystick capabilities (%x)\n", mmr); 
		return;
	}

	Com_DPrintf( "Joystick found.\n" );
	Com_DPrintf( "Pname: %s\n", joy.jc.szPname );
	Com_DPrintf( "OemVxD: %s\n", joy.jc.szOEMVxD );
	Com_DPrintf( "RegKey: %s\n", joy.jc.szRegKey );

	Com_DPrintf( "Numbuttons: %i / %i\n", joy.jc.wNumButtons, joy.jc.wMaxButtons );
	Com_DPrintf( "Axis: %i / %i\n", joy.jc.wNumAxes, joy.jc.wMaxAxes );
	Com_DPrintf( "Caps: 0x%x\n", joy.jc.wCaps );
	if ( joy.jc.wCaps & JOYCAPS_HASPOV ) {
		Com_DPrintf( "HASPOV\n" );
	} else {
		Com_DPrintf( "no POV\n" );
	}

	// mark the joystick as available
	joy.avail = qtrue; 
	Cvar_Set( "ui_joyavail", "1" );
}

/*
===========
JoyToF
===========
*/
float JoyToF( int value ) {
	return fnql::input::NormaliseJoystickAxis( value );
}

int JoyToI( int value ) {
	// move centerpoint to zero
	value -= 32768;

	return value;
}


static void IN_QueueRetailJoystickAxis( int axis, int value ) {
	if ( axis < 0 || axis >= MAX_JOYSTICK_AXIS ||
		joy.oldmoveaxisstate[axis] == value ) {
		return;
	}

	Sys_QueEvent( g_wv.sysMsgTime, SE_JOYSTICK_AXIS, axis, value, 0, NULL );
	joy.oldmoveaxisstate[axis] = value;
}

int	joyDirectionKeys[16] = {
	K_LEFTARROW, K_RIGHTARROW,
	K_UPARROW, K_DOWNARROW,
	K_JOY16, K_JOY17,
	K_JOY18, K_JOY19,
	K_JOY20, K_JOY21,
	K_JOY22, K_JOY23,

	K_JOY24, K_JOY25,
	K_JOY26, K_JOY27
};


enum winMMAxis_t {
	WINMM_AXIS_X,
	WINMM_AXIS_Y,
	WINMM_AXIS_Z,
	WINMM_AXIS_R,
	WINMM_AXIS_U,
	WINMM_AXIS_V
};


static qboolean IN_WinMMAxisPosition( winMMAxis_t axis, DWORD *position )
{
	if ( !position ) {
		return qfalse;
	}

	switch ( axis ) {
		case WINMM_AXIS_X:
			if ( joy.jc.wNumAxes < 1 ) return qfalse;
			*position = joy.ji.dwXpos;
			return qtrue;
		case WINMM_AXIS_Y:
			if ( joy.jc.wNumAxes < 2 ) return qfalse;
			*position = joy.ji.dwYpos;
			return qtrue;
		case WINMM_AXIS_Z:
			if ( !( joy.jc.wCaps & JOYCAPS_HASZ ) ) return qfalse;
			*position = joy.ji.dwZpos;
			return qtrue;
		case WINMM_AXIS_R:
			if ( !( joy.jc.wCaps & JOYCAPS_HASR ) ) return qfalse;
			*position = joy.ji.dwRpos;
			return qtrue;
		case WINMM_AXIS_U:
			if ( !( joy.jc.wCaps & JOYCAPS_HASU ) ) return qfalse;
			*position = joy.ji.dwUpos;
			return qtrue;
		case WINMM_AXIS_V:
			if ( !( joy.jc.wCaps & JOYCAPS_HASV ) ) return qfalse;
			*position = joy.ji.dwVpos;
			return qtrue;
		default:
			return qfalse;
	}
}

/*
===========
IN_JoyMove
===========
*/
void IN_JoyMove( void ) {
	float	fAxisValue;
	int		i;
	DWORD	buttonstate, povstate;
	int		x, y;
	const bool retailProfile = in_joystickProfile->integer != 0;
	static constexpr int kRawButtonCount = K_JOY16 - K_JOY1 + 1;
	static constexpr int kMaximumReadFailures = 3;

	// verify joystick is available and that the user wants to use it
	if ( !joy.avail ) {
		return; 
	}

	// collect the joystick data, if possible
	Com_Memset (&joy.ji, 0, sizeof(joy.ji));
	joy.ji.dwSize = sizeof(joy.ji);
	joy.ji.dwFlags = JOY_RETURNALL;

	const MMRESULT readResult = joyGetPosEx( joy.id, &joy.ji );
	if ( readResult != JOYERR_NOERROR ) {
		++joy.consecutiveReadFailures;
		if ( readResult == JOYERR_UNPLUGGED ||
			joy.consecutiveReadFailures >= kMaximumReadFailures ) {
			Com_DPrintf(
				"Joystick read failed (%u); disabling device\n",
				static_cast<unsigned int>( readResult ) );
			IN_ClearWinMMJoystickState( qtrue );
			WIN_QueueInputReset( qtrue );
		}
		return;
	}
	joy.consecutiveReadFailures = 0;

	if ( in_debugJoystick->integer ) {
		Com_Printf( "%8x %5i %5.2f %5.2f %5.2f %5.2f %6i %6i\n", 
			JoyToI( joy.ji.dwButtons ),
			JoyToI( joy.ji.dwPOV ),
			JoyToF( joy.ji.dwXpos ), JoyToF( joy.ji.dwYpos ),
			JoyToF( joy.ji.dwZpos ), JoyToF( joy.ji.dwRpos ),
			JoyToI( joy.ji.dwUpos ), JoyToI( joy.ji.dwVpos ) );
	}

	// loop through the joystick buttons
	// key a joystick event or auxiliary event for higher number buttons for each state change
	buttonstate = joy.ji.dwButtons;
	const int buttonCount =
		( std::min<int> )( joy.jc.wNumButtons, kRawButtonCount );
	for ( i = 0; i < buttonCount; ++i ) {
		const DWORD mask = DWORD{ 1 } << i;
		if ( (buttonstate & mask) && !(joy.oldbuttonstate & mask) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_JOY1 + i, qtrue, 0, NULL );
		}
		if ( !(buttonstate & mask) && (joy.oldbuttonstate & mask) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_JOY1 + i, qfalse, 0, NULL );
		}
	}
	joy.oldbuttonstate = buttonstate;

	povstate = 0;

	// The opt-in retail profile reserves X/Y for analog movement. The legacy
	// profile keeps translating those axes into direction keys exactly as FnQ3
	// did, preserving existing non-SDL configurations.
	int firstDirectionAxis = 0;
	if ( retailProfile ) {
		DWORD axisPosition = 0;
		const int side = IN_WinMMAxisPosition(
			WINMM_AXIS_X, &axisPosition )
			? fnql::input::RetailJoystickMoveAxis(
				JoyToF( axisPosition ), in_joyHorizMoveDeadzone->value,
				in_joyBallScale->value )
			: 0;
		const int forward = IN_WinMMAxisPosition(
			WINMM_AXIS_Y, &axisPosition )
			? fnql::input::RetailJoystickMoveAxis(
				JoyToF( axisPosition ), in_joyVertMoveDeadzone->value,
				in_joyBallScale->value )
			: 0;
		IN_QueueRetailJoystickAxis( AXIS_SIDE, side );
		IN_QueueRetailJoystickAxis( AXIS_FORWARD, forward );
		firstDirectionAxis = 2;
	}

	const float deadzone =
		fnql::input::FiniteJoystickDeadzone( joy_threshold->value );
	for ( i = firstDirectionAxis; i < 4; ++i ) {
		DWORD axisPosition;
		if ( !IN_WinMMAxisPosition(
				static_cast<winMMAxis_t>( i ), &axisPosition ) ) {
			continue;
		}
		// get the floating point zero-centered, potentially-inverted data for the current axis
		fAxisValue = JoyToF( axisPosition );

		if ( fAxisValue < -deadzone ) {
			povstate |= ( 1u << ( i * 2 ) );
		} else if ( fAxisValue > deadzone ) {
			povstate |= ( 1u << ( i * 2 + 1 ) );
		}
	}

	// convert POV information from a direction into 4 button bits
	if ( joy.jc.wCaps & JOYCAPS_HASPOV ) {
		if ( joy.ji.dwPOV != JOY_POVCENTERED ) {
			if (joy.ji.dwPOV == JOY_POVFORWARD)
				povstate |= 1u << 12;
			if (joy.ji.dwPOV == JOY_POVBACKWARD)
				povstate |= 1u << 13;
			if (joy.ji.dwPOV == JOY_POVRIGHT)
				povstate |= 1u << 14;
			if (joy.ji.dwPOV == JOY_POVLEFT)
				povstate |= 1u << 15;
		}
	}

	// determine which bits have changed and key an auxiliary event for each change
	for (i=0 ; i < 16 ; i++) {
		if ( (povstate & ( 1u << i )) &&
			!(joy.oldpovstate & ( 1u << i )) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, joyDirectionKeys[i], qtrue, 0, NULL );
		}

		if ( !(povstate & ( 1u << i )) &&
			(joy.oldpovstate & ( 1u << i )) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, joyDirectionKeys[i], qfalse, 0, NULL );
		}
	}
	joy.oldpovstate = povstate;

	DWORD horizontalLookPosition = 0;
	DWORD verticalLookPosition = 0;
	const qboolean hasHorizontalLook =
		IN_WinMMAxisPosition( WINMM_AXIS_R, &horizontalLookPosition );
	const qboolean hasVerticalLook =
		IN_WinMMAxisPosition( WINMM_AXIS_U, &verticalLookPosition );
	if ( retailProfile && ( hasHorizontalLook || hasVerticalLook ) ) {
		const float viewAcceleration = Cvar_VariableValue( "cl_viewAccel" );
		x = hasHorizontalLook
			? fnql::input::RetailJoystickLookDelta(
				JoyToF( horizontalLookPosition ),
				in_joyHorizViewDeadzone->value,
				in_joyHorizViewSensitivity->value, viewAcceleration, false )
			: 0;
		y = hasVerticalLook
			? fnql::input::RetailJoystickLookDelta(
				JoyToF( verticalLookPosition ),
				in_joyVertViewDeadzone->value,
				in_joyVertViewSensitivity->value, viewAcceleration,
				in_joystickInverted->integer != 0 )
			: 0;
		if ( x || y ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_MOUSE, x, y, 0, NULL );
		}
	} else if ( !retailProfile ) {
		// Preserve FnQ3's U/V trackball lane when the retail profile is off.
		DWORD trackballX;
		DWORD trackballY;
		if ( !IN_WinMMAxisPosition( WINMM_AXIS_U, &trackballX ) ||
			!IN_WinMMAxisPosition( WINMM_AXIS_V, &trackballY ) ) {
			return;
		}
		x = fnql::input::TruncateFiniteFloatToInt(
			static_cast<float>( JoyToI( trackballX ) ) *
			in_joyBallScale->value );
		y = fnql::input::TruncateFiniteFloatToInt(
			static_cast<float>( JoyToI( trackballY ) ) *
			in_joyBallScale->value );
		if ( x || y ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_MOUSE, x, y, 0, NULL );
		}
	}
}
#endif

/*
=========================================================================

MIDI

=========================================================================
*/

#ifdef USE_MIDI
static qboolean MIDI_WindowAcceptsInput( void )
{
	const HWND window = g_wv.hWnd;
	return window && GetForegroundWindow() == window && !IsIconic( window ) &&
		WIN_WindowFocused() && !WIN_InputSuspended()
		? qtrue : qfalse;
}


static void MIDI_NoteOff( int note )
{
	int qkey;

	qkey = note - 60 + K_AUX1;

	if ( qkey > 255 || qkey < K_AUX1 )
		return;

	Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, qkey, qfalse, 0, NULL );
}

static void MIDI_NoteOn( int note, int velocity )
{
	int qkey;

	if ( velocity == 0 ) {
		MIDI_NoteOff( note );
		return;
	}

	qkey = note - 60 + K_AUX1;

	if ( qkey > 255 || qkey < K_AUX1 )
		return;

	Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, qkey, qtrue, 0, NULL );
}

void IN_MIDIMessage( HMIDIIN device, DWORD packedMessage )
{
	if ( !device || device != s_midiInfo.hMidiIn ||
		!MIDI_WindowAcceptsInput() ) {
		return;
	}

	const int message = packedMessage & 0xff;
	const int channel = ( message & 0x0f ) + 1;
	if ( channel != in_midichannel->integer ) {
		return;
	}

	const int note = ( packedMessage & 0xff00 ) >> 8;
	if ( ( message & 0xf0 ) == 0x90 ) {
		MIDI_NoteOn( note, ( packedMessage & 0xff0000 ) >> 16 );
	} else if ( ( message & 0xf0 ) == 0x80 ) {
		MIDI_NoteOff( note );
	}
}

static void MidiInfo_f( void )
{
	int i;

	const char *enableStrings[] = { "disabled", "enabled" };

	Com_Printf( "\nMIDI control:       %s\n", enableStrings[in_midi->integer != 0] );
	Com_Printf( "port:               %d\n", in_midiport->integer );
	Com_Printf( "channel:            %d\n", in_midichannel->integer );
	Com_Printf( "current device:     %d\n", in_mididevice->integer );
	Com_Printf( "number of devices:  %d\n", s_midiInfo.numDevices );
	for ( i = 0; i < s_midiInfo.numDevices; i++ )
	{
		if ( i == Cvar_VariableIntegerValue( "in_mididevice" ) )
			Com_Printf( "***" );
		else
			Com_Printf( "..." );
		Com_Printf(    "device %2d:       %s\n", i, s_midiInfo.caps[i].szPname );
		Com_Printf( "...manufacturer ID: 0x%hx\n", s_midiInfo.caps[i].wMid );
		Com_Printf( "...product ID:      0x%hx\n", s_midiInfo.caps[i].wPid );

		Com_Printf( "\n" );
	}
}

static void IN_StartupMIDI( void )
{
	int i;

	if ( !Cvar_VariableIntegerValue( "in_midi" ) )
		return;

	//
	// enumerate MIDI IN devices
	//
	const int systemDeviceCount = static_cast<int>( midiInGetNumDevs() );
	s_midiInfo.numDevices =
		( std::min )( systemDeviceCount, MAX_MIDIIN_DEVICES );

	for ( i = 0; i < s_midiInfo.numDevices; i++ )
	{
		midiInGetDevCaps( i, &s_midiInfo.caps[i], sizeof( s_midiInfo.caps[i] ) );
	}

	const int selectedDevice = in_mididevice->integer;
	if ( selectedDevice < 0 || selectedDevice >= systemDeviceCount ||
		selectedDevice >= s_midiInfo.numDevices ) {
		Com_DPrintf( "WARNING: MIDI device %d is outside the supported "
			"device range (system=%d, cached=%d)\n",
			selectedDevice, systemDeviceCount, s_midiInfo.numDevices );
		return;
	}

	//
	// open the MIDI IN port
	//
	if ( !g_wv.hWnd ) {
		Com_DPrintf( "WARNING: cannot open MIDI input without a game window\n" );
		return;
	}
	if ( midiInOpen( &s_midiInfo.hMidiIn, 
		             selectedDevice,
					 reinterpret_cast<DWORD_PTR>( g_wv.hWnd ),
					 0,
					 CALLBACK_WINDOW ) != MMSYSERR_NOERROR )
	{
		Com_DPrintf( "WARNING: could not open MIDI device %d: '%s'\n",
			selectedDevice, s_midiInfo.caps[selectedDevice].szPname );
		return;
	}

	const MMRESULT startResult = midiInStart( s_midiInfo.hMidiIn );
	if ( startResult != MMSYSERR_NOERROR ) {
		Com_DPrintf( "WARNING: could not start MIDI device %d (%u)\n",
			selectedDevice, static_cast<unsigned int>( startResult ) );
		midiInClose( s_midiInfo.hMidiIn );
		s_midiInfo.hMidiIn = NULL;
	}
}

static void IN_ShutdownMIDI( void )
{
	if ( s_midiInfo.hMidiIn )
	{
		midiInStop( s_midiInfo.hMidiIn );
		midiInReset( s_midiInfo.hMidiIn );
		midiInClose( s_midiInfo.hMidiIn );
	}
	Com_Memset( &s_midiInfo, 0, sizeof( s_midiInfo ) );
}
#endif
