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

#include "../client/client.h"
#include "../client/input_compat.hpp"
#include "../platform/window_placement.hpp"
#include "win_local.h"
#include "glw_win.h"
#include "win_raii.h"

#include <vector>

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL (WM_MOUSELAST+1)  // message that will be supported by the OS 
#endif
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

//static UINT MSH_MOUSEWHEEL;

// Console variables that we need to access from this module
cvar_t		*in_forceCharset;

static HHOOK WinHook;
static qboolean temporaryMouseCapture;
static qboolean temporaryCaptureFailureReported;
static unsigned int physicalModifierState;
static int windowWheelRemainder;
static fnql::input::PointerOwner windowWheelOwner =
	fnql::input::PointerOwner::Gameplay;
static int windowWheelConsumer;
static qboolean windowWheelOwnerValid;
static qboolean windowWheelFlip = qtrue;
static int windowWheelPendingKey;
static qboolean suppressedAltEnter;
static qboolean windowInputSuspended;
static qboolean windowHidden;
static qboolean windowFocused;

enum {
	WIN_MOD_LSHIFT = 1u << 0,
	WIN_MOD_RSHIFT = 1u << 1,
	WIN_MOD_LCTRL  = 1u << 2,
	WIN_MOD_RCTRL  = 1u << 3,
	WIN_MOD_LALT   = 1u << 4,
	WIN_MOD_RALT   = 1u << 5
};


static unsigned int WIN_ModifierSideBit( WPARAM key, LPARAM lParam )
{
	switch ( key ) {
		case VK_LSHIFT: return WIN_MOD_LSHIFT;
		case VK_RSHIFT: return WIN_MOD_RSHIFT;
		case VK_SHIFT:
			return ( ( lParam >> 16 ) & 0xff ) == 0x36
				? WIN_MOD_RSHIFT : WIN_MOD_LSHIFT;
		case VK_LCONTROL: return WIN_MOD_LCTRL;
		case VK_RCONTROL: return WIN_MOD_RCTRL;
		case VK_CONTROL:
			return ( lParam & ( 1 << 24 ) ) ? WIN_MOD_RCTRL : WIN_MOD_LCTRL;
		case VK_LMENU: return WIN_MOD_LALT;
		case VK_RMENU: return WIN_MOD_RALT;
		case VK_MENU:
			return ( lParam & ( 1 << 24 ) ) ? WIN_MOD_RALT : WIN_MOD_LALT;
		default:
			return 0;
	}
}


static unsigned int WIN_ModifierFamilyMask( unsigned int side )
{
	if ( side & ( WIN_MOD_LSHIFT | WIN_MOD_RSHIFT ) ) {
		return WIN_MOD_LSHIFT | WIN_MOD_RSHIFT;
	}
	if ( side & ( WIN_MOD_LCTRL | WIN_MOD_RCTRL ) ) {
		return WIN_MOD_LCTRL | WIN_MOD_RCTRL;
	}
	if ( side & ( WIN_MOD_LALT | WIN_MOD_RALT ) ) {
		return WIN_MOD_LALT | WIN_MOD_RALT;
	}
	return 0;
}


static qboolean WIN_SkipAltGrLeftControl( WPARAM key, LPARAM lParam )
{
	if ( key != VK_CONTROL || ( lParam & ( 1L << 24 ) ) ) {
		return qfalse;
	}

	// Windows synthesizes a non-extended LeftCtrl immediately before an
	// extended RightAlt for AltGr. Suppress only that adjacent, same-timestamp
	// pair; an independently pressed Ctrl remains a normal bindable modifier.
	MSG nextMessage;
	const DWORD messageTime =
		static_cast<DWORD>( GetMessageTime() );
	if ( PeekMessageW( &nextMessage, NULL, 0, 0, PM_NOREMOVE ) &&
		( nextMessage.message == WM_KEYDOWN ||
			nextMessage.message == WM_SYSKEYDOWN ) &&
		nextMessage.wParam == VK_MENU &&
		( nextMessage.lParam & ( 1L << 24 ) ) &&
		nextMessage.time == messageTime ) {
		return qtrue;
	}

	return qfalse;
}


static qboolean WIN_ShouldQueueModifierTransition(
	WPARAM key, LPARAM lParam, qboolean down )
{
	const unsigned int side = WIN_ModifierSideBit( key, lParam );
	if ( !side ) {
		return qtrue;
	}

	const unsigned int family = WIN_ModifierFamilyMask( side );
	const qboolean sideWasDown =
		( physicalModifierState & side ) ? qtrue : qfalse;
	const qboolean familyWasDown =
		( physicalModifierState & family ) ? qtrue : qfalse;
	if ( down ) {
		physicalModifierState |= side;
		return ( !familyWasDown || sideWasDown ) ? qtrue : qfalse;
	} else {
		physicalModifierState &= ~side;
	}
	return ( physicalModifierState & family ) ? qfalse : qtrue;
}


static unsigned int WIN_ReadPhysicalModifiers( void )
{
	unsigned int state = 0;
	if ( GetAsyncKeyState( VK_LSHIFT ) & 0x8000 ) state |= WIN_MOD_LSHIFT;
	if ( GetAsyncKeyState( VK_RSHIFT ) & 0x8000 ) state |= WIN_MOD_RSHIFT;
	if ( GetAsyncKeyState( VK_LCONTROL ) & 0x8000 ) state |= WIN_MOD_LCTRL;
	if ( GetAsyncKeyState( VK_RCONTROL ) & 0x8000 ) state |= WIN_MOD_RCTRL;
	if ( GetAsyncKeyState( VK_LMENU ) & 0x8000 ) state |= WIN_MOD_LALT;
	if ( GetAsyncKeyState( VK_RMENU ) & 0x8000 ) state |= WIN_MOD_RALT;
	return state;
}


void WIN_ResetMessageInputState( void )
{
	physicalModifierState = WIN_ReadPhysicalModifiers();
	windowWheelRemainder = 0;
	windowWheelConsumer = 0;
	windowWheelOwnerValid = qfalse;
	windowWheelFlip = qtrue;
	windowWheelPendingKey = 0;
	suppressedAltEnter = qfalse;
}


static void WIN_QueueHeldModifiers( int eventTime )
{
	physicalModifierState = WIN_ReadPhysicalModifiers();

	if ( physicalModifierState & ( WIN_MOD_LSHIFT | WIN_MOD_RSHIFT ) ) {
		Sys_QueEvent( eventTime, SE_KEY, K_SHIFT, qtrue, 0, NULL );
	}
	if ( physicalModifierState & ( WIN_MOD_LCTRL | WIN_MOD_RCTRL ) ) {
		Sys_QueEvent( eventTime, SE_KEY, K_CTRL, qtrue, 0, NULL );
	}
	if ( physicalModifierState & ( WIN_MOD_LALT | WIN_MOD_RALT ) ) {
		Sys_QueEvent( eventTime, SE_KEY, K_ALT, qtrue, 0, NULL );
	}
}


void WIN_QueueInputReset( qboolean rebuildModifiers )
{
	const int eventTime = Sys_Milliseconds();

	// Reset producer caches before placing the ordered client barrier. The
	// message drain may continue afterward, and its newer transitions must
	// survive until their events are consumed.
	IN_ResetInputState();
	Sys_QueEvent( eventTime, SE_INPUT_RESET, 0, 0, 0, NULL );
	windowWheelRemainder = 0;
	windowWheelOwnerValid = qfalse;
	windowWheelFlip = qtrue;
	windowWheelPendingKey = 0;
	if ( rebuildModifiers && gw_active && !gw_minimized &&
		!windowInputSuspended && windowFocused ) {
		WIN_QueueHeldModifiers( eventTime );
	} else {
		physicalModifierState = 0;
	}
}


static qboolean WIN_ConsoleOwnsPointer( void )
{
	return ( Key_GetCatcher() & KEYCATCH_CONSOLE ) ? qtrue : qfalse;
}


static int WIN_PointerConsumerIdentity( void )
{
	const int catcher = Key_GetCatcher();

	if ( catcher & KEYCATCH_CONSOLE ) return KEYCATCH_CONSOLE;
	if ( catcher & KEYCATCH_BROWSER ) return KEYCATCH_BROWSER;
	if ( catcher & KEYCATCH_UI ) return KEYCATCH_UI;
	if ( catcher & KEYCATCH_CGAME ) return KEYCATCH_CGAME;
	return 0;
}


static qboolean WIN_WindowAcceptsInput( void )
{
	return ( gw_active && !gw_minimized && !windowInputSuspended &&
		windowFocused )
		? qtrue : qfalse;
}


static void WIN_QueueMouseReset( void )
{
	// Capture messages can arrive in the same native drain as a button-down
	// whose queued key event has not reached consumed client state yet. An unconditional
	// mouse-only barrier is ordered, cheap, and cannot strand that transition.
	Sys_QueEvent( Sys_Milliseconds(),
		SE_MOUSE_RESET, 0, 0, 0, NULL );
}


void WIN_ReleaseTemporaryMouseCapture( void )
{
	if ( temporaryMouseCapture && GetCapture() == g_wv.hWnd ) {
		if ( !ReleaseCapture() ) {
			Com_DPrintf( "%s: ReleaseCapture failed (Win32 error %lu)\n",
				__func__, GetLastError() );
			return;
		}
	}
	temporaryMouseCapture = qfalse;
	temporaryCaptureFailureReported = qfalse;
}


void WIN_RebuildTemporaryMouseCapture( void )
{
	const qboolean physicalButtonDown =
		( ( GetAsyncKeyState( VK_LBUTTON ) & 0x8000 ) ||
		( GetAsyncKeyState( VK_RBUTTON ) & 0x8000 ) ||
		( GetAsyncKeyState( VK_MBUTTON ) & 0x8000 ) ||
		( GetAsyncKeyState( VK_XBUTTON1 ) & 0x8000 ) ||
		( GetAsyncKeyState( VK_XBUTTON2 ) & 0x8000 ) ) ? qtrue : qfalse;
	qboolean logicalButtonDown = qfalse;

	for ( int key = K_MOUSE1; key <= K_MOUSE9; ++key ) {
		if ( keys[key].down ) {
			logicalButtonDown = qtrue;
			break;
		}
	}
	if ( !physicalButtonDown || !logicalButtonDown ) {
		if ( !physicalButtonDown && logicalButtonDown ) {
			// Capture can be cancelled without a corresponding button-up.
			// Balance the consumer state before allowing another overlay drag.
			Sys_QueEvent( Sys_Milliseconds(),
				SE_MOUSE_RESET, 0, 0, 0, NULL );
		}
		WIN_ReleaseTemporaryMouseCapture();
		return;
	}
	if ( GetCapture() != g_wv.hWnd ) {
		SetCapture( g_wv.hWnd );
	}
	temporaryMouseCapture =
		GetCapture() == g_wv.hWnd ? qtrue : qfalse;
	if ( temporaryMouseCapture ) {
		temporaryCaptureFailureReported = qfalse;
	} else if ( !temporaryCaptureFailureReported ) {
		Com_DPrintf( "%s: SetCapture failed (Win32 error %lu)\n",
			__func__, GetLastError() );
		temporaryCaptureFailureReported = qtrue;
	}
}


static int WIN_MouseMessageKey( UINT message, WPARAM wParam, qboolean *down )
{
	*down = qfalse;
	switch ( message ) {
		case WM_LBUTTONDOWN: *down = qtrue; return K_MOUSE1;
		case WM_LBUTTONUP: return K_MOUSE1;
		case WM_RBUTTONDOWN: *down = qtrue; return K_MOUSE2;
		case WM_RBUTTONUP: return K_MOUSE2;
		case WM_MBUTTONDOWN: *down = qtrue; return K_MOUSE3;
		case WM_MBUTTONUP: return K_MOUSE3;
		case WM_XBUTTONDOWN:
			*down = qtrue;
			return GET_XBUTTON_WPARAM( wParam ) == XBUTTON1 ? K_MOUSE4 : K_MOUSE5;
		case WM_XBUTTONUP:
			return GET_XBUTTON_WPARAM( wParam ) == XBUTTON1 ? K_MOUSE4 : K_MOUSE5;
		default: return 0;
	}
}


static void WIN_UpdateTemporaryMouseCapture( HWND hWnd, UINT message, WPARAM wParam )
{
	switch ( message ) {
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_XBUTTONDOWN:
			SetCapture( hWnd );
			temporaryMouseCapture =
				GetCapture() == hWnd ? qtrue : qfalse;
			if ( !temporaryMouseCapture ) {
				Com_DPrintf( "%s: SetCapture failed (Win32 error %lu)\n",
					__func__, GetLastError() );
				temporaryCaptureFailureReported = qtrue;
			} else {
				temporaryCaptureFailureReported = qfalse;
			}
			break;
		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		case WM_MBUTTONUP:
		case WM_XBUTTONUP:
			if ( !( GET_KEYSTATE_WPARAM( wParam ) &
				( MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2 ) ) &&
				GetCapture() == hWnd ) {
				WIN_ReleaseTemporaryMouseCapture();
			}
			break;
		default:
			break;
	}
}


static LRESULT WIN_MouseMessageResult( UINT message )
{
	return ( message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ) ? TRUE : 0;
}


static qboolean WIN_ConstrainWindowRectToWorkArea( RECT *rect )
{
	MONITORINFO monitorInfo;
	HMONITOR monitor;
	fnql::window::Position constrained;
	const int width = rect->right - rect->left;
	const int height = rect->bottom - rect->top;

	monitor = MonitorFromRect( rect, MONITOR_DEFAULTTONEAREST );
	Com_Memset( &monitorInfo, 0, sizeof( monitorInfo ) );
	monitorInfo.cbSize = sizeof( monitorInfo );
	if ( !monitor || !GetMonitorInfo( monitor, &monitorInfo ) ) {
		return qfalse;
	}

	constrained = fnql::window::ConstrainClientOrigin(
		{ rect->left, rect->top }, width, height,
		{ monitorInfo.rcWork.left, monitorInfo.rcWork.top,
			monitorInfo.rcWork.right - monitorInfo.rcWork.left,
			monitorInfo.rcWork.bottom - monitorInfo.rcWork.top } );
	if ( constrained.x == rect->left && constrained.y == rect->top ) {
		return qfalse;
	}

	rect->left = constrained.x;
	rect->top = constrained.y;
	rect->right = rect->left + width;
	rect->bottom = rect->top + height;
	return qtrue;
}


static void WIN_RecoverWindowPlacement( HWND hWnd )
{
	RECT rect;

	if ( glw_state.cdsFullscreen || IsIconic( hWnd ) || IsZoomed( hWnd ) ||
		!GetWindowRect( hWnd, &rect ) ||
		!WIN_ConstrainWindowRectToWorkArea( &rect ) ) {
		return;
	}

	SetWindowPos( hWnd, NULL, rect.left, rect.top,
		rect.right - rect.left, rect.bottom - rect.top,
		SWP_NOACTIVATE | SWP_NOZORDER );
	Cvar_SetIntegerValue( "vid_xpos", rect.left );
	Cvar_SetIntegerValue( "vid_ypos", rect.top );
}


static void WIN_RefreshWindowPlacementState( HWND hWnd )
{
	if ( GetWindowRect( hWnd, &g_wv.winRect ) ) {
		g_wv.winRectValid = qtrue;
		UpdateMonitorInfo( &g_wv.winRect );
	}
}


static void WIN_ApplyMinimumTrackSize( HWND hWnd, MINMAXINFO *info )
{
	RECT windowRect;
	RECT clientRect;
	int frameWidth;
	int frameHeight;
	int clientWidth;
	int clientHeight;
	int minimumWidth;
	int minimumHeight;

	if ( !info || glw_state.cdsFullscreen ||
		!GetWindowRect( hWnd, &windowRect ) ||
		!GetClientRect( hWnd, &clientRect ) ) {
		return;
	}

	// Deriving the non-client extent from the live HWND automatically follows
	// its current DPI, theme, and borderless style without hard-coded metrics.
	clientWidth = clientRect.right - clientRect.left;
	clientHeight = clientRect.bottom - clientRect.top;
	if ( clientWidth <= 0 || clientHeight <= 0 ) {
		return;
	}
	frameWidth = ( windowRect.right - windowRect.left ) - clientWidth;
	frameHeight = ( windowRect.bottom - windowRect.top ) - clientHeight;
	if ( frameWidth < 0 ) frameWidth = 0;
	if ( frameHeight < 0 ) frameHeight = 0;
	minimumWidth = 320 + frameWidth;
	minimumHeight = 240 + frameHeight;
	if ( info->ptMinTrackSize.x < minimumWidth ) {
		info->ptMinTrackSize.x = minimumWidth;
	}
	if ( info->ptMinTrackSize.y < minimumHeight ) {
		info->ptMinTrackSize.y = minimumHeight;
	}
}

/*
==================
WinKeyHook
==================
*/
static LRESULT CALLBACK WinKeyHook( int code, WPARAM wParam, LPARAM lParam )
{
	PKBDLLHOOKSTRUCT key = (PKBDLLHOOKSTRUCT)lParam;

	if ( code != HC_ACTION ) {
		return CallNextHookEx( NULL, code, wParam, lParam );
	}

	if ( key->vkCode == VK_SNAPSHOT || key->vkCode == VK_LWIN || key->vkCode == VK_RWIN ) {
		return CallNextHookEx( NULL, code, wParam, lParam );
	}

	return CallNextHookEx( NULL, code, wParam, lParam );
}


/*
==================
WIN_DisableHook
==================
*/
void WIN_DisableHook( void  ) 
{
	if ( WinHook ) {
		UnhookWindowsHookEx( WinHook );
		WinHook = NULL;
	}
}


/*
==================
WIN_EnableHook

Capture PrintScreen and Win* keys
==================
*/
void WIN_EnableHook( void  ) 
{
	if ( !WinHook )
	{
		WinHook = SetWindowsHookEx( WH_KEYBOARD_LL, WinKeyHook, g_wv.hInstance, 0 );
	}
}


static qboolean s_alttab_disabled;

/*
==================
WIN_DisableAltTab
==================
*/
void WIN_DisableAltTab( void )
{
	BOOL old;

	if ( s_alttab_disabled )
		return;

#if 0
	if ( g_wv.hWnd && glw_state.cdsFullscreen && glw_state.monitorCount > 1 ) {
		// topmost window
		SetWindowLong( g_wv.hWnd, GWL_EXSTYLE, WINDOW_ESTYLE_FULLSCREEN );
		SetWindowLong( g_wv.hWnd, GWL_STYLE, WINDOW_STYLE_FULLSCREEN );
	}
#endif

	if ( !Q_stricmp( Cvar_VariableString( "arch" ), "winnt" ) )
		RegisterHotKey( NULL, 0, MOD_ALT, VK_TAB );
	else
		SystemParametersInfo( SPI_SETSCREENSAVERRUNNING, 1, &old, 0 );

	s_alttab_disabled = qtrue;
}


/*
==================
WIN_EnableAltTab
==================
*/
void WIN_EnableAltTab( void )
{
	BOOL old;

	if ( !s_alttab_disabled )
		return;

#if 0
	if ( g_wv.hWnd && glw_state.cdsFullscreen && glw_state.monitorCount > 1 ) {
		// allow moving other windows on foreground
		SetWindowLong( g_wv.hWnd, GWL_EXSTYLE, WINDOW_ESTYLE_NORMAL );
		SetWindowLong( g_wv.hWnd, GWL_STYLE, WINDOW_STYLE_FULLSCREEN_MIN );
		SetWindowPos( g_wv.hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
	}
#endif

	if ( !Q_stricmp( Cvar_VariableString( "arch" ), "winnt" ) )
		UnregisterHotKey( NULL, 0 );
	else 
		SystemParametersInfo( SPI_SETSCREENSAVERRUNNING, 0, &old, 0 );

	s_alttab_disabled = qfalse;
}


/*
==================
VID_AppActivate
==================
*/
static void VID_AppActivate( qboolean active )
{
	// Keep the reset ordered with key/button transitions already queued by the
	// same Win32 message drain. Clearing synchronously here can resurrect an
	// earlier queued key-down after focus has already been lost.
	WIN_QueueInputReset( active );

	CL_WebHost_NotifyAppActivation( active );
	IN_Activate( active );

	if ( active ) {
		WIN_EnableHook();
		SetWindowPos( g_wv.hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
	} else {
		WIN_DisableHook();
		SetWindowPos( g_wv.hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
	}
}


qboolean WIN_InputSuspended( void )
{
	return windowInputSuspended;
}


qboolean WIN_WindowFocused( void )
{
	return windowFocused;
}


static void WIN_UpdateInputSuspension( void )
{
	const qboolean shouldSuspend =
		( windowHidden || gw_minimized ) ? qtrue : qfalse;
	if ( shouldSuspend ) {
		if ( windowInputSuspended ) {
			return;
		}

		// Keep retained native transitions before the barrier, then release
		// every pointer-ownership path even when WM_ACTIVATE/FocusOut never
		// follows a minimize or hide notification.
		windowInputSuspended = qtrue;
		WIN_QueueInputReset( qfalse );
		IN_Activate( qfalse );
		WIN_ReleaseTemporaryMouseCapture();
		return;
	}

	if ( !windowInputSuspended || !gw_active || !windowFocused ) {
		return;
	}

	// WM_SIZE, WM_SHOWWINDOW, focus, and activation can report restoration in
	// either order. Resume once all conditions agree and rebuild modifiers
	// behind a fresh ordered barrier.
	windowInputSuspended = qfalse;
	WIN_QueueInputReset( qtrue );
	IN_Activate( qtrue );
}


static void WIN_UpdateWindowFocus( qboolean focused )
{
	if ( windowFocused == focused ) {
		return;
	}

	windowFocused = focused;
	if ( !windowFocused ) {
		// WM_KILLFOCUS is not guaranteed to be paired with WM_ACTIVATE. If the
		// app remains active, independently balance held input and release every
		// pointer-ownership path.
		if ( gw_active && !windowInputSuspended ) {
			WIN_QueueInputReset( qfalse );
			IN_Activate( qfalse );
		}
		WIN_ReleaseTemporaryMouseCapture();
		return;
	}

	const qboolean wasSuspended = windowInputSuspended;
	WIN_UpdateInputSuspension();
	if ( !wasSuspended && gw_active && !gw_minimized && !windowHidden &&
		!windowInputSuspended ) {
		// Focus can return without an activation transition. Rebuild modifier
		// families behind a fresh barrier before polling resumes.
		WIN_QueueInputReset( qtrue );
		IN_Activate( qtrue );
	}
}

//==========================================================================

static const int s_scantokey[ 128 ] = 
{ 
//	0        1       2       3       4       5       6       7 
//	8        9       A       B       C       D       E       F 
	0  , K_ESCAPE,  '1',    '2',    '3',    '4',    '5',    '6', 
	'7',    '8',    '9',    '0',    '-',    '=',K_BACKSPACE,K_TAB,  // 0 
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i', 
	'o',    'p',    '[',    ']',  K_ENTER, K_CTRL,	'a',	's',	// 1 
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';', 
	'\'',K_CONSOLE,K_SHIFT, '\\',   'z',    'x',    'c',    'v',	// 2 
	'b',    'n',    'm',    ',',    '.',    '/',  K_SHIFT,  '*', 
	K_ALT,  ' ',K_CAPSLOCK, K_F1,   K_F2,   K_F3,   K_F4,  K_F5,    // 3 
	K_F6, K_F7,  K_F8,   K_F9,  K_F10, K_PAUSE, K_SCROLLOCK, K_HOME, 
	K_UPARROW,K_PGUP,K_KP_MINUS,K_LEFTARROW,K_KP_5,K_RIGHTARROW,K_KP_PLUS,K_END, //4 
	K_DOWNARROW,K_PGDN,K_INS,K_DEL, 0,      0,      0,    K_F11, 
	K_F12,  0  ,    0  ,    0  ,    0  ,  K_MENU,   0  ,    0,     // 5
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0, 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0,     // 6 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0, 
	0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0  ,    0      // 7 
}; 


/*
==================
MapKey

Map from windows to quake keynums
==================
*/
static int MapKey( int nVirtKey, int key )
{
	int result;
	int modified;
	qboolean is_extended;

	modified = ( key >> 16 ) & 255;

	if ( modified > 127 )
		return 0;

	if ( key & ( 1 << 24 ) )
	{
		is_extended = qtrue;
	}
	else
	{
		is_extended = qfalse;
	}

	result = s_scantokey[modified];

	//Com_Printf( "key: 0x%08x modified:%i extended:%i result:%i(%02x) vk=%i\n",
	//	key, modified, is_extended, result, result, nVirtKey );

	if ( !is_extended )
	{
		switch ( result )
		{
		case K_HOME:
			return K_KP_HOME;
		case K_UPARROW:
			if ( Key_GetCatcher() && nVirtKey == VK_NUMPAD8 )
				return 0;
			return K_KP_UPARROW;
		case K_DOWNARROW:
			if ( Key_GetCatcher() && nVirtKey == VK_NUMPAD2 )
				return 0;
			return K_KP_DOWNARROW;
		case K_PGUP:
			return K_KP_PGUP;
		case K_LEFTARROW:
			return K_KP_LEFTARROW;
		case K_RIGHTARROW:
			return K_KP_RIGHTARROW;
		case K_END:
			return K_KP_END;
		case K_PGDN:
			return K_KP_PGDN;
		case K_INS:
			return K_KP_INS;
		case K_DEL:
			return K_KP_DEL;
		default:
			return result;
		}
	}
	else
	{
		switch ( result )
		{
		case K_PAUSE:
			return K_KP_NUMLOCK;
		case K_ENTER:
			return K_KP_ENTER;
		case '/':
			return K_KP_SLASH;
		case 0xAF:
			return K_KP_PLUS;
		//case '*':
		//	return K_KP_STAR;
		}
		return result;
	}
}


static qboolean directMap( const WPARAM chr ) {

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
==================
MapChar

Map input to ASCII charset
==================
*/
static int MapChar( WPARAM wParam, byte scancode ) 
{
	static const int s_scantochar[ 128 ] = 
	{ 
//	0        1       2       3       4       5       6       7 
//	8        9       A       B       C       D       E       F 
 	 0,      0,     '1',    '2',    '3',    '4',    '5',    '6', 
	'7',    '8',    '9',    '0',    '-',    '=',    0x8,    0x9,	// 0
	'q',    'w',    'e',    'r',    't',    'y',    'u',    'i', 
	'o',    'p',    '[',    ']',    0xD,     0,     'a',    's',	// 1
	'd',    'f',    'g',    'h',    'j',    'k',    'l',    ';', 
	'\'',    0,      0,     '\\',   'z',    'x',    'c',    'v',	// 2
	'b',    'n',    'm',    ',',    '.',    '/',     0,     '*', 
	 0,     ' ',     0,      0,      0,      0,      0,      0,     // 3

	 0,      0,     '!',    '@',    '#',    '$',    '%',    '^', 
	'&',    '*',    '(',    ')',    '_',    '+',    0x8,    0x9,	// 4
	'Q',    'W',    'E',    'R',    'T',    'Y',    'U',    'I', 
	'O',    'P',    '{',    '}',    0xD,     0,     'A',    'S',	// 5
	'D',    'F',    'G',    'H',    'J',    'K',    'L',    ':',
	'"',     0,      0,     '|',    'Z',    'X',    'C',    'V',	// 6
	'B',    'N',    'M',    '<',    '>',    '?',     0,     '*', 
 	 0,     ' ',     0,      0,      0,      0,      0,      0,     // 7
	}; 

	// A Unicode window delivers composed UTF-16 through WM_CHAR. Physical-key
	// charset forcing is an ASCII compatibility policy and must not replace a
	// real Unicode character (including a surrogate unit) with a US scancode.
	if ( wParam > 127 )
		return static_cast<int>( wParam );

	if ( scancode == 0x53 )
		return '.';

	if ( directMap( wParam ) || scancode > 0x39 )
	{
		return wParam;
	}
	else 
	{
		char ch = s_scantochar[ scancode ];
		int shift = (GetKeyState( VK_SHIFT ) >> 15) & 1;
		if ( ch >= 'a' && ch <= 'z' ) 
		{
			int  capital = GetKeyState( VK_CAPITAL ) & 1;
			if ( capital ^ shift ) 
			{
				ch = ch - 'a' + 'A';
			}
		} 
		else 
		{
			ch = s_scantochar[ scancode | (shift<<6) ];
		}

		return ch;
	}
}


/*
====================
MainWndProc

main window procedure
====================
*/
extern cvar_t *in_mouse;
extern cvar_t *in_logitechbug;


static void WIN_QueueWheelStep( int key, int eventTime )
{
	if ( !in_logitechbug->integer ) {
		// If the compatibility mode was disabled between duplicate messages,
		// first balance its outstanding transition.
		if ( windowWheelPendingKey ) {
			Sys_QueEvent( eventTime, SE_KEY,
				windowWheelPendingKey, qfalse, 0, NULL );
			windowWheelPendingKey = 0;
		}
		windowWheelFlip = qtrue;
		Sys_QueEvent( eventTime, SE_KEY, key, qtrue, 0, NULL );
		Sys_QueEvent( eventTime, SE_KEY, key, qfalse, 0, NULL );
		return;
	}

	// The legacy Logitech workaround interprets duplicate messages as the down
	// and up halves of one detent. If the direction changes between those
	// halves, balance the old pseudo-key before starting the new direction.
	if ( windowWheelPendingKey && windowWheelPendingKey != key ) {
		Sys_QueEvent( eventTime, SE_KEY,
			windowWheelPendingKey, qfalse, 0, NULL );
		windowWheelPendingKey = 0;
		windowWheelFlip = qtrue;
	}

	Sys_QueEvent( eventTime, SE_KEY, key, windowWheelFlip, 0, NULL );
	if ( windowWheelFlip ) {
		windowWheelPendingKey = key;
	} else {
		windowWheelPendingKey = 0;
	}
	windowWheelFlip = windowWheelFlip == qtrue ? qfalse : qtrue;
}


int			HotKey = 0;
int			hkinstalled = 0;

extern void SetGameDisplaySettings( void );
extern void SetDesktopDisplaySettings( void );

void Win_AddHotkey( void ) 
{
	UINT modifiers, vk;
	ATOM atom;

	if ( !HotKey || !g_wv.hWnd || hkinstalled )
		return;

	modifiers = 0;

	if ( HotKey & HK_MOD_ALT )		modifiers |= MOD_ALT;
	if ( HotKey & HK_MOD_CONTROL )	modifiers |= MOD_CONTROL;
	if ( HotKey & HK_MOD_SHIFT )	modifiers |= MOD_SHIFT;
	if ( HotKey & HK_MOD_WIN )		modifiers |= MOD_WIN;

	vk = HotKey & 0xFF;

	atom = GlobalAddAtom( TEXT( "Q3MinimizeHotkey" ) );
	if ( !RegisterHotKey( g_wv.hWnd, atom, modifiers, vk ) ) {
		GlobalDeleteAtom( atom );
		return;
	}
	hkinstalled = 1;
}


void Win_RemoveHotkey( void ) 
{
	ATOM atom;

	if ( !g_wv.hWnd || !hkinstalled )
		return;

	atom = GlobalFindAtom( TEXT( "Q3MinimizeHotkey" ) );
	if ( atom ) {
		UnregisterHotKey( g_wv.hWnd, atom );
 		GlobalDeleteAtom( atom );
		hkinstalled = 0;
	}
}


BOOL Win_CheckHotkeyMod( void ) {

	if ( !(HotKey & HK_MOD_XMASK) )
 		return TRUE;

 	if ((HotKey&HK_MOD_LALT) && !GetKeyState(VK_LMENU)) return FALSE;
 	if ((HotKey&HK_MOD_RALT) && !GetKeyState(VK_RMENU)) return FALSE;
 	if ((HotKey&HK_MOD_LSHIFT) && !GetKeyState(VK_LSHIFT)) return FALSE;
 	if ((HotKey&HK_MOD_RSHIFT) && !GetKeyState(VK_RSHIFT)) return FALSE;
 	if ((HotKey&HK_MOD_LCONTROL) && !GetKeyState(VK_LCONTROL)) return FALSE;
 	if ((HotKey&HK_MOD_RCONTROL) && !GetKeyState(VK_RCONTROL)) return FALSE;
 	if ((HotKey&HK_MOD_LWIN) && !GetKeyState(VK_LWIN)) return FALSE;
 	if ((HotKey&HK_MOD_RWIN) && !GetKeyState(VK_RWIN)) return FALSE;

 	return TRUE;
}


#if 0
static int GetTimerMsec( void ) {
	int msec;
	
	if ( gw_minimized || CL_VideoRecording() )
		return 0;

	if ( com_maxfps->integer > 0 ) {
		msec = 1000 / com_maxfps->integer;
		if ( msec < 1 )
			msec = 1;
	} else {
		msec = 16; // 62.5fps
	}

	return msec;
}
#endif


static HWINEVENTHOOK hWinEventHook;

static VOID CALLBACK WinEventProc( HWINEVENTHOOK h_WinEventHook, DWORD dwEvent, HWND hWnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime )
{
	if ( gw_active )
	{
		if ( glw_state.cdsFullscreen )// disable topmost window style
		{
			SetWindowPos( g_wv.hWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE );
		}
		SetForegroundWindow( hWnd );
	}
}

#define TIMER_M 11
#define TIMER_T 12
#define TIMER_G 13
static UINT uTimerM;
static UINT uTimerT;
static UINT uTimerG;

/*
==================
WIN_ScheduleGammaReapply

A mode set or desktop switch reloads the display LUT from the ICC profile, and
the driver can finish doing so after ChangeDisplaySettings and WM_DISPLAYCHANGE
have already returned. Re-assert our ramp once the dust settles so returning to
the game does not leave it running on the desktop ramp.
==================
*/
static void WIN_ScheduleGammaReapply( void )
{
	if ( !g_wv.hWnd || !glw_state.gammaSet )
		return;

	if ( uTimerG )
		KillTimer( g_wv.hWnd, uTimerG );

	uTimerG = SetTimer( g_wv.hWnd, TIMER_G, 250, NULL );
}

void WIN_Minimize( void ) {
	static int minimize = 0;

	if ( minimize )
		return;

	minimize = 1;

#ifdef FAST_MODE_SWITCH
	// move game window to background
	if ( glw_state.cdsFullscreen ) {
		if ( gw_active )
			SetForegroundWindow( GetDesktopWindow() );
		// and wait some time before minimizing
		if ( !uTimerM )
			uTimerM = SetTimer( g_wv.hWnd, TIMER_M, 50, NULL );
	} else {
		ShowWindow( g_wv.hWnd, SW_MINIMIZE );
	}
#else
	ShowWindow( g_wv.hWnd, SW_MINIMIZE );
#endif

	minimize = 0;
}


LRESULT WINAPI MainWndProc( HWND hWnd, UINT uMsg, WPARAM  wParam, LPARAM lParam )
{
	#define TIMER_ID 10
	//static UINT uTimerID;
	qboolean active;
	qboolean minimized;
	qboolean activationChanged;
	int zDelta, i;

	// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/winui/winui/windowsuserinterface/userinput/mouseinput/aboutmouseinput.asp
	// Windows 95, Windows NT 3.51 - uses MSH_MOUSEWHEEL
	// only relevant for non-DI input
	//
	// NOTE: not sure how reliable this is anymore, might trigger double wheel events
	/* if (in_mouse->integer == -1)
	{
		if ( uMsg == MSH_MOUSEWHEEL )
		{
			if ( ( ( int ) wParam ) > 0 )
			{
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
			}
			else
			{
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
			}
			return DefWindowProcW( hWnd, uMsg, wParam, lParam );
		}
	} */

	switch (uMsg)
	{
	case WM_SETCURSOR:
		if ( LOWORD( lParam ) == HTCLIENT ) {
			if ( !WIN_WindowAcceptsInput() ) {
				break;
			}
			if ( WIN_ConsoleOwnsPointer() ) {
				// The console draws its own cursor. In a window the pointer remains
				// free for the desktop; in fullscreen this also prevents an
				// underlying UI/browser catcher from exposing its host cursor.
				SetCursor( NULL );
				return TRUE;
			}
			HCURSOR browserCursor = (HCURSOR)CL_WebHost_GetCursorHandle();
			if ( browserCursor ) {
				SetCursor( browserCursor );
				return TRUE;
			}
			if ( Key_GetCatcher() & ( KEYCATCH_UI | KEYCATCH_CGAME ) ) {
				// Retail retains the normal Win32 arrow while native UI or
				// cgame overlays own absolute mouse input.
				SetCursor( LoadCursor( NULL, IDC_ARROW ) );
				return TRUE;
			}
		}
		break;

	case WM_MOUSEWHEEL:
		// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/winui/winui/windowsuserinterface/userinput/mouseinput/aboutmouseinput.asp
		// Windows 98/Me, Windows NT 4.0 and later - uses WM_MOUSEWHEEL
		{
			const fnql::input::PointerOwner pointerOwner = WIN_ResolvePointerOwner();
			const int pointerConsumer = WIN_PointerConsumerIdentity();

			// Raw/DirectInput is suspended for every absolute overlay. In that
			// state WM_MOUSEWHEEL is the sole producer, including native UI and
			// cgame catchers. Gameplay accepts it only from the legacy backend,
			// avoiding a duplicate of raw/DirectInput wheel data.
			if ( WIN_WindowAcceptsInput() &&
				( IN_LegacyMouseDrivesInput() ||
					fnql::input::PointerOwnerReportsAbsolute( pointerOwner ) ) )
			{
				POINT position;
				const int wheelDelta = static_cast<short>( HIWORD( wParam ) );

				if ( !windowWheelOwnerValid ||
					windowWheelOwner != pointerOwner ||
					windowWheelConsumer != pointerConsumer ) {
					if ( windowWheelPendingKey ) {
						Sys_QueEvent( g_wv.sysMsgTime, SE_KEY,
							windowWheelPendingKey, qfalse, 0, NULL );
						windowWheelPendingKey = 0;
					}
					windowWheelRemainder = 0;
					windowWheelOwner = pointerOwner;
					windowWheelConsumer = pointerConsumer;
					windowWheelOwnerValid = qtrue;
					windowWheelFlip = qtrue;
				}
				windowWheelRemainder = fnql::input::SaturatingAddInt(
					windowWheelRemainder, wheelDelta );
				zDelta = windowWheelRemainder / WHEEL_DELTA;
				windowWheelRemainder %= WHEEL_DELTA;
				zDelta = std::clamp( zDelta, -32, 32 );

				// Queue the hit-test position even if gameplay still appears to
				// own the pointer: an earlier Escape in this message drain can
				// open an absolute UI before the wheel event is consumed.
				position.x = static_cast<short>( LOWORD( lParam ) );
				position.y = static_cast<short>( HIWORD( lParam ) );
				if ( ScreenToClient( hWnd, &position ) ) {
					int x = position.x;
					int y = position.y;
					WIN_ProjectClientPointerToDrawable( &x, &y );
					Sys_QueEvent( g_wv.sysMsgTime,
						SE_MOUSE_ABSOLUTE, x, y, 0, NULL );
				}

				const int wheelKey =
					zDelta > 0 ? K_MWHEELUP : K_MWHEELDOWN;
				for ( i = 0; i < std::abs( zDelta ); ++i ) {
					WIN_QueueWheelStep( wheelKey, g_wv.sysMsgTime );
				}

				// An application that processes WM_MOUSEWHEEL must return zero.
				return 0;
			}
			if ( windowWheelPendingKey ) {
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY,
					windowWheelPendingKey, qfalse, 0, NULL );
				windowWheelPendingKey = 0;
			}
			windowWheelRemainder = 0;
			windowWheelOwnerValid = qfalse;
			windowWheelFlip = qtrue;
		}
		break;

	case WM_CREATE:

		//MSH_MOUSEWHEEL = RegisterWindowMessage( TEXT( "MSWHEEL_ROLLMSG" ) ); 

		WIN_EnableHook(); // installs Win32 low-level hook, pass-through for OS key handling

		hWinEventHook = SetWinEventHook( EVENT_SYSTEM_SWITCHSTART, EVENT_SYSTEM_SWITCHSTART, NULL, WinEventProc, 
			0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS );
		g_wv.hWnd = hWnd;
		GetWindowRect( hWnd, &g_wv.winRect );
		g_wv.winRectValid = qtrue;
		gw_minimized = qfalse;
		windowFocused = qfalse;
		windowHidden = qfalse;
		windowInputSuspended = qfalse;
		uTimerM = 0;
		uTimerT = 0;
		uTimerG = 0;

		in_forceCharset = Cvar_Get( "in_forceCharset", "1", CVAR_ARCHIVE_ND );
		Cvar_SetDescription( in_forceCharset, "Try to translate non-ASCII chars in keyboard input or force EN/US keyboard layout." );

		break;

	case WM_DISPLAYCHANGE:
		Com_DPrintf( "WM_DISPLAYCHANGE\n" );
		if ( !glw_state.cdsFullscreen && ( !r_fullscreen || !r_fullscreen->integer ) ) {
			WIN_RecoverWindowPlacement( hWnd );
			WIN_RefreshWindowPlacementState( hWnd );
		}
		// the mode set just reloaded the display LUT from its profile
		GLW_ReapplyGamma();
		WIN_ScheduleGammaReapply();
		break;

	case WM_SETTINGCHANGE:
		if ( wParam == SPI_SETWORKAREA && !glw_state.cdsFullscreen ) {
			WIN_RecoverWindowPlacement( hWnd );
			WIN_RefreshWindowPlacementState( hWnd );
		}
		break;

	case WM_DPICHANGED:
		if ( !glw_state.cdsFullscreen && lParam ) {
			RECT suggested = *(RECT *)lParam;
			WIN_ConstrainWindowRectToWorkArea( &suggested );
			if ( SetWindowPos( hWnd, NULL, suggested.left, suggested.top,
				suggested.right - suggested.left,
				suggested.bottom - suggested.top,
				SWP_NOACTIVATE | SWP_NOZORDER ) ) {
				// Persist the outer-frame origin selected for the new DPI. WM_MOVE
				// may be suppressed while inactive, but the chrome-aware suggested
				// rectangle must still survive the next restart.
				Cvar_SetIntegerValue( "vid_xpos", suggested.left );
				Cvar_SetIntegerValue( "vid_ypos", suggested.top );
				vid_xpos->modified = qfalse;
				vid_ypos->modified = qfalse;
			}
			WIN_RefreshWindowPlacementState( hWnd );
			return 0;
		}
		break;

	case WM_GETMINMAXINFO:
		WIN_ApplyMinimumTrackSize( hWnd,
			reinterpret_cast<MINMAXINFO *>( lParam ) );
		return 0;

	case WM_DESTROY:
		Win_RemoveHotkey();
		if ( hWinEventHook ) {
			UnhookWinEvent( hWinEventHook );
		}
		if ( uTimerM ) {
			KillTimer( g_wv.hWnd, uTimerM ); uTimerM = 0;
		}
		if ( uTimerT ) {
			KillTimer( g_wv.hWnd, uTimerT ); uTimerT = 0;
		}
		if ( uTimerG ) {
			KillTimer( g_wv.hWnd, uTimerG ); uTimerG = 0;
		}
		hWinEventHook = NULL;
		g_wv.hWnd = NULL;
		g_wv.winRectValid = qfalse;
		gw_minimized = qfalse;
		gw_active = qfalse;
		windowFocused = qfalse;
		windowHidden = qfalse;
		windowInputSuspended = qfalse;
		//WIN_EnableAltTab();
		return 0;

	case WM_CLOSE:
		// Restore the desktop before queuing shutdown. This also covers failures
		// after the close request but before renderer teardown completes.
		GLW_RestoreGamma();
		Cbuf_ExecuteText( EXEC_APPEND, "quit\n" );
		// filter this message or we may lose window before renderer shutdown ?
		return 0;

	/*
		on minimize:
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWMINIMIZED
			WM_KILLFOCUS
			WM_MOVE (x:garbage y:garbage)
			WM_SIZE (SIZE_MINIMIZED w=0 h=0)
			WM_ACTIVATE (active=0 minimized=1)

		on restore:
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWNORMAL
			WM_ACTIVATE (active=1 minimized=1)
			WM_MOVE (x, y)
			WM_SIZE (SIZE_RESTORED width height)
			WM_SETFOCUS
			WM_ACTIVATE (active=1 minimized=0)
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWNORMAL

		on click in:
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWNORMAL
			WM_ACTIVATE (active=1 minimized=0)
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWNORMAL
			WM_SETFOCUS

		on click out, destroy:
			WM_ACTIVATE (active=0 minimized=0)
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWNORMAL
			WM_KILLFOCUS

		on create:
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWNORMAL
			WM_ACTIVATE (active=1 minimized=0)
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWNORMAL
			WM_SETFOCUS
			WM_SIZE (SIZE_RESTORED width height)
			WM_MOVE (x, y)

		on win+d:
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWMINIMIZED
			WM_MOVE (x:garbage, y:garbage)
			WM_SIZE (SIZE_MINIMIZED)
			WM_ACTIVATE (active=0 minimized=1)
			WM_WINDOWPOSCHANGING WindowPlacement:ShowCmd = SW_SHOWMINIMIZED
			WM_KILLFOCUS
			
	*/

	case WM_ACTIVATE:
		active = (LOWORD( wParam ) != WA_INACTIVE) ? qtrue : qfalse;
		minimized = (BOOL)HIWORD( wParam ) ? qtrue : qfalse;

		// We can receive Active & Minimized when restoring from minimized state
		if ( active && minimized ) {
			gw_minimized = qtrue;
			WIN_UpdateInputSuspension();
			break;
		}

		activationChanged = gw_active != active ? qtrue : qfalse;
		gw_active = active;
		gw_minimized = minimized;

		if ( activationChanged ) {
			VID_AppActivate( gw_active );
			if ( !gw_active ) {
				// Release bounded UI/console drag capture on focus loss. Relative
				// gameplay capture has already been deactivated by VID_AppActivate.
				WIN_ReleaseTemporaryMouseCapture();
			}
		}
		WIN_UpdateInputSuspension();
		Win_AddHotkey();

		if ( glw_state.cdsFullscreen ) {
			if ( gw_active ) {
				SetGameDisplaySettings();
				if ( re.SetColorMappings )
					re.SetColorMappings();
				// the mode set above can still be settling: the driver reloads
				// the LUT from the display profile and drops the ramp we just
				// wrote, which is what leaves the game dark after ALT+TAB
				WIN_ScheduleGammaReapply();
			} else {
				// don't restore gamma if we have multiple monitors
				if ( glw_state.monitorCount <= 1 || gw_minimized )
					GLW_RestoreGamma();
				// minimize if there is only one monitor
				if ( glw_state.monitorCount <= 1 ) {
					if ( !CL_VideoRecording() || ( re.CanMinimize && re.CanMinimize() ) ) {
						if ( !gw_minimized ) {
							WIN_Minimize();
						}
						SetDesktopDisplaySettings();
					}
				}
			}
		} else {
			if ( gw_active ) {
				if ( re.SetColorMappings )
					re.SetColorMappings();
			} else {
				GLW_RestoreGamma();
			}
		}

		// after ALT+TAB, even if we selected other window we may receive WM_ACTIVATE 1 and then WM_ACTIVATE 0
		// if we set HWND_TOPMOST in VID_AppActivate() other window will be not visible despite obtained input focus
		// so delay HWND_TOPMOST setup to make sure we have no such bogus activation
		if ( gw_active && glw_state.cdsFullscreen ) {
			if ( uTimerT ) {
				KillTimer( g_wv.hWnd, uTimerT );
			}
			uTimerT = SetTimer( g_wv.hWnd, TIMER_T, 20, NULL );
		}

		SNDDMA_Activate();
		break;

	case WM_SETFOCUS:
		WIN_UpdateWindowFocus( qtrue );
		break;

	case WM_KILLFOCUS:
		WIN_UpdateWindowFocus( qfalse );
		break;

	case WM_CAPTURECHANGED:
		// Capture can be stolen without a matching button-up. Invalidate the
		// overlay latch immediately and balance either absolute or gameplay
		// buttons before a different capture path can silently reacquire.
		WIN_QueueMouseReset();
		temporaryMouseCapture = qfalse;
		break;

	case WM_CANCELMODE:
		WIN_QueueMouseReset();
		WIN_ReleaseTemporaryMouseCapture();
		break;

	case WM_MOVE:
		if ( !gw_active || gw_minimized || !windowFocused ||
			IsZoomed( hWnd ) )
			break;

		GetWindowRect( hWnd, &g_wv.winRect );
		g_wv.winRectValid = qtrue;
		UpdateMonitorInfo( &g_wv.winRect );
		IN_UpdateWindow( NULL, qtrue );
		IN_Activate( gw_active );

		if ( !glw_state.cdsFullscreen )	{
			Cvar_SetIntegerValue( "vid_xpos", g_wv.winRect.left );
			Cvar_SetIntegerValue( "vid_ypos", g_wv.winRect.top );
			vid_xpos->modified = qfalse;
			vid_ypos->modified = qfalse;
		}
		break;

	case WM_EXITSIZEMOVE:
		if ( !glw_state.cdsFullscreen ) {
			RECT clientRect;
			WIN_RecoverWindowPlacement( hWnd );
			WIN_RefreshWindowPlacementState( hWnd );
			if ( GetClientRect( hWnd, &clientRect ) ) {
				const int clientWidth = clientRect.right - clientRect.left;
				const int clientHeight = clientRect.bottom - clientRect.top;
				if ( glw_state.config &&
					clientWidth == glw_state.config->vidWidth &&
					clientHeight == glw_state.config->vidHeight ) {
					CL_CancelWindowResize();
				} else {
					CL_NotifyWindowResize( clientWidth, clientHeight, qtrue );
					CL_CompleteWindowResize();
				}
			}
		}
		break;

	case WM_SIZE:
		gw_minimized = ( wParam == SIZE_MINIMIZED ) ? qtrue : qfalse;
		WIN_UpdateInputSuspension();
		if ( !gw_minimized && !glw_state.cdsFullscreen ) {
			RECT clientRect;
			if ( GetClientRect( hWnd, &clientRect ) ) {
				const int clientWidth = clientRect.right - clientRect.left;
				const int clientHeight = clientRect.bottom - clientRect.top;
				if ( glw_state.config &&
					clientWidth == glw_state.config->vidWidth &&
					clientHeight == glw_state.config->vidHeight ) {
					CL_CancelWindowResize();
				} else {
					CL_NotifyWindowResize( clientWidth, clientHeight, qtrue );
				}
			}
		}
		if ( gw_active && windowFocused && !gw_minimized ) {
			GetWindowRect( hWnd, &g_wv.winRect );
			g_wv.winRectValid = qtrue;
			UpdateMonitorInfo( &g_wv.winRect );
			IN_UpdateWindow( NULL, qtrue );
		}
		break;

	case WM_TIMER:
		//if ( wParam == TIMER_ID && uTimerID != 0 && !CL_VideoRecording() ) {
		//	Com_Frame( CL_NoDelay() );
		//	return 0;
		//}
		if ( wParam == TIMER_M ) {
			KillTimer( g_wv.hWnd, uTimerM ); uTimerM = 0;
			ShowWindow( hWnd, SW_MINIMIZE );
			return 0;
		}
		if ( wParam == TIMER_T ) {
			KillTimer( g_wv.hWnd, uTimerT ); uTimerT = 0;
			if ( gw_active && glw_state.cdsFullscreen ) {
				// set TOPMOST style to avoid losing input focus because of other underlying topmost windows
				// such as on-screen keyboard
				SetWindowPos( g_wv.hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
			}
			return 0;
		}
		if ( wParam == TIMER_G ) {
			KillTimer( g_wv.hWnd, uTimerG ); uTimerG = 0;
			GLW_ReapplyGamma();
			return 0;
		}
		break;

	case WM_WINDOWPOSCHANGING:
		{
			WINDOWPLACEMENT wp;

			// set minimized flag as early as possible
			Com_Memset( &wp, 0, sizeof( wp ) );
			wp.length = sizeof( WINDOWPLACEMENT );
			if ( GetWindowPlacement( hWnd, &wp ) && wp.showCmd == SW_SHOWMINIMIZED )
				gw_minimized = qtrue;

			if ( g_wv.borderless )
			{
				WINDOWPOS *pos = (LPWINDOWPOS) lParam;
				const int threshold = 10;
				HMONITOR hMonitor;
				MONITORINFO mi;
				const RECT *r;
				RECT rr;

				rr.left = pos->x;
				rr.right = pos->x + pos->cx;
				rr.top = pos->y;
				rr.bottom = pos->y + pos->cy;
				hMonitor = MonitorFromRect( &rr, MONITOR_DEFAULTTONEAREST );

				if ( hMonitor )
				{
					mi.cbSize = sizeof( mi );
					GetMonitorInfo( hMonitor, &mi );
					r = &mi.rcWork;

					// snap window to current monitor borders
					if ( pos->x >= ( r->left - threshold ) && pos->x <= ( r->left + threshold ) )
						pos->x = r->left;
					else if ( ( pos->x + pos->cx ) >= ( r->right - threshold ) && ( pos->x + pos->cx ) <= ( r->right + threshold ) )
						pos->x = ( r->right - pos->cx );

					if ( pos->y >= ( r->top - threshold ) && pos->y <= ( r->top + threshold ) )
						pos->y = r->top;
					else if ( ( pos->y + pos->cy ) >= ( r->bottom - threshold ) && ( pos->y + pos->cy ) <= ( r->bottom + threshold ) )
						pos->y = ( r->bottom - pos->cy );

					return 0;
				}
			}
		}
		break;

	// this is complicated because Win32 seems to pack multiple mouse events into
	// one update sometimes, so we always check all states and look for events
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_XBUTTONDOWN:
	case WM_XBUTTONUP:
	case WM_MOUSEMOVE:
	{
		// One resolver with win_input.cpp, so the message pump always routes to
		// the owner that IN_Frame is presenting a pointer for.
		const fnql::input::PointerOwner pointerOwner = WIN_ResolvePointerOwner();
		int messageX = (int)(short)LOWORD( lParam );
		int messageY = (int)(short)HIWORD( lParam );
		qboolean positionQueued = qfalse;

		if ( uMsg != WM_MOUSEMOVE && WIN_WindowAcceptsInput() ) {
			// An earlier Escape in this message drain can open an absolute UI
			// before this button is dispatched even though the producer still
			// observes gameplay ownership.
			WIN_ProjectClientPointerToDrawable( &messageX, &messageY );
			Sys_QueEvent( g_wv.sysMsgTime,
				SE_MOUSE_ABSOLUTE, messageX, messageY, 0, NULL );
			positionQueued = qtrue;
		}

		if ( pointerOwner != fnql::input::PointerOwner::Gameplay ) {
			if ( !WIN_WindowAcceptsInput() ) {
				return WIN_MouseMessageResult( uMsg );
			}
			// Every absolute consumer works in renderer drawable pixels, which
			// are the client pixels Windows reports only while the renderer
			// resolution matches the client area.
			int x = messageX;
			int y = messageY;
			qboolean down = qfalse;
			int key;

			if ( !positionQueued ) {
				WIN_ProjectClientPointerToDrawable( &x, &y );
			}
			key = WIN_MouseMessageKey( uMsg, wParam, &down );

			if ( uMsg == WM_MOUSEMOVE ) {
				Sys_QueEvent( g_wv.sysMsgTime, SE_MOUSE_ABSOLUTE, x, y, 0, NULL );
				return 0;
			}
			if ( key ) {
				// Keep position-before-click ordering even if Windows did not emit
				// a distinct WM_MOUSEMOVE for this location.
				if ( !positionQueued ) {
					Sys_QueEvent( g_wv.sysMsgTime,
						SE_MOUSE_ABSOLUTE, x, y, 0, NULL );
				}
				Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, key, down, 0, NULL );
				WIN_UpdateTemporaryMouseCapture( hWnd, uMsg, wParam );
			}
			return WIN_MouseMessageResult( uMsg );
		}
	}
		if ( IN_MouseActive() ) {
			// Legacy messages drive gameplay input only while the legacy Win32
			// mouse is the active source. With raw input or DirectInput active,
			// any legacy message still in the queue is a stale position from a
			// menu or focus transition; converting it into a delta kicks the
			// view by (position - window centre) without any physical mouse
			// motion. Swallow it instead.
			if ( IN_LegacyMouseDrivesInput() ) {
				int mstate = (wParam & (MK_LBUTTON|MK_RBUTTON)) + ((wParam & (MK_MBUTTON|MK_XBUTTON1|MK_XBUTTON2)) >> 2);
				IN_Win32MouseEvent(
					static_cast<int>( static_cast<short>( LOWORD( lParam ) ) ),
					static_cast<int>( static_cast<short>( HIWORD( lParam ) ) ),
					mstate );
			}
			return WIN_MouseMessageResult( uMsg );
		}
		break;

	case WM_INPUT:
		if ( IN_RawMouseDrivesInput() ) {
			IN_RawMouseEvent( lParam );
			return DefWindowProcW( hWnd, uMsg, wParam, lParam );
		}
		break;

	case WM_SHOWWINDOW:
		windowHidden = wParam ? qfalse : qtrue;
		WIN_UpdateInputSuspension();
		break;

	case WM_INPUT_DEVICE_CHANGE:
		IN_RawInputDeviceChange( wParam, lParam );
		return 0;

	case WM_SYSCOMMAND:
		// Prevent Alt+Letter commands from hanging the application temporarily
		if ( wParam == SC_KEYMENU || wParam == SC_MOUSEMENU + HTSYSMENU || wParam == SC_CLOSE + HTSYSMENU )
			return 0;

		if ( wParam == SC_SCREENSAVE || wParam == SC_MONITORPOWER )
			return 0;

		if ( wParam == SC_MINIMIZE && CL_VideoRecording() && !( re.CanMinimize && re.CanMinimize() ) )
			return 0;

		// simulate drag move to avoid ~500ms delay between DefWindowProc() and further WM_ENTERSIZEMOVE
		if ( wParam == SC_MOVE + HTCAPTION )
		{
			mouse_event( MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN, 7, 0, 0, 0 );
			mouse_event( MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN, (DWORD)-7, 0, 0, 0 );
		}
		break;

	case WM_CONTEXTMENU:
		// disable context menus to avoid blocking message loop
		return 0;

	case WM_HOTKEY:
		// check for left/right modifiers
		if ( Win_CheckHotkeyMod() )
		{
			if ( gw_active )
			{
				if ( !CL_VideoRecording() || ( re.CanMinimize && re.CanMinimize() ) )
					WIN_Minimize();
			}
			else
			{
				SetForegroundWindow( hWnd );
				SetFocus( hWnd );
				ShowWindow( hWnd, SW_RESTORE );
			}
			return 0;
		}
		break;

	case WM_SYSKEYDOWN:
	case WM_KEYDOWN: {
		if ( !WIN_WindowAcceptsInput() ) {
			break;
		}
		if ( WIN_SkipAltGrLeftControl( wParam, lParam ) ) {
			return 0;
		}
		const qboolean isRepeat =
			( lParam & ( 1L << 30 ) ) ? qtrue : qfalse;
		if ( wParam == VK_RETURN && ( uMsg == WM_SYSKEYDOWN || GetKeyState( VK_RMENU ) & 0x8000 ) ) {
			suppressedAltEnter = qtrue;
			if ( !isRepeat ) {
				Cvar_SetIntegerValue( "r_fullscreen", glw_state.cdsFullscreen ? 0 : 1 );
				Cbuf_AddText( "vid_restart\n" );
			}
			return 0;
		}
		if ( wParam == VK_SNAPSHOT || wParam == VK_LWIN || wParam == VK_RWIN ) {
			return DefWindowProcW( hWnd, uMsg, wParam, lParam );
		}
		//Com_Printf( "^2k+^7 wParam:%08x lParam:%08x\n", wParam, lParam );
		const int mappedKey = MapKey( wParam, lParam );
		if ( isRepeat && ( mappedKey == K_CONSOLE || mappedKey == K_ESCAPE ) ) {
			return 0;
		}
		if ( mappedKey && WIN_ShouldQueueModifierTransition(
				wParam, lParam, qtrue ) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, mappedKey, qtrue, 0, NULL );
		}
		break;
	}

	case WM_SYSKEYUP:
	case WM_KEYUP:
		if ( !WIN_WindowAcceptsInput() ) {
			break;
		}
		if ( WIN_SkipAltGrLeftControl( wParam, lParam ) ) {
			return 0;
		}
		if ( wParam == VK_RETURN && suppressedAltEnter ) {
			suppressedAltEnter = qfalse;
			return 0;
		}
		if ( wParam == VK_SNAPSHOT || wParam == VK_LWIN || wParam == VK_RWIN ) {
			return DefWindowProcW( hWnd, uMsg, wParam, lParam );
		}
		//Com_Printf( "^5k-^7 wParam:%08x lParam:%08x\n", wParam, lParam );
		if ( const int mappedKey = MapKey( wParam, lParam );
			mappedKey && WIN_ShouldQueueModifierTransition(
				wParam, lParam, qfalse ) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_KEY, mappedKey, qfalse, 0, NULL );
		}
		break;

	case WM_SYSCHAR:
		// WM_SYSKEYDOWN already owns Alt+Enter. Consume the paired character
		// so DefWindowProc cannot beep or activate a system menu.
		if ( wParam == VK_RETURN ) {
			return 0;
		}
		break;

	case WM_CHAR:
		if ( !WIN_WindowAcceptsInput() ) {
			return 0;
		}
		{
			byte scancode = ((lParam >> 16) & 0xFF);
			if ( wParam != VK_NUMPAD0 && scancode != 0x29 ) {
				const int character = MapChar( wParam, scancode );
				if ( character ) {
					Sys_QueEvent( g_wv.sysMsgTime, SE_CHAR, character, 0, 0, NULL );
				}
			}
		}
		return 0;

	case WM_UNICHAR:
		if ( wParam == UNICODE_NOCHAR ) {
			return TRUE;
		}
		if ( !WIN_WindowAcceptsInput() ) {
			return 0;
		}
		if ( wParam > 0 && wParam <= 0x10ffff &&
			!( wParam >= 0xd800 && wParam <= 0xdfff ) ) {
			Sys_QueEvent( g_wv.sysMsgTime, SE_CHAR,
				static_cast<int>( wParam ), 0, 0, NULL );
		}
		return 0;

#ifdef USE_MIDI
	case MM_MIM_DATA:
		IN_MIDIMessage(
			reinterpret_cast<HMIDIIN>( wParam ),
			static_cast<DWORD>( lParam ) );
		return 0;
#endif

	case WM_NCHITTEST:
		// in borderless mode - drag using client area when holding ALT
		if ( g_wv.borderless && GetKeyState( VK_MENU ) & (1<<15) )
			return HTCAPTION;
		break;

	case WM_ERASEBKGND: 
		// avoid GDI clearing the OpenGL window background in Vista/7
		return 1;
	}

	return DefWindowProcW( hWnd, uMsg, wParam, lParam );
}


/*
================
HandleEvents
================
*/
void HandleEvents( void ) {
	MSG msg;

	// pump the message loop
	while ( PeekMessage( &msg, NULL, 0, 0, PM_NOREMOVE ) ) {
		if ( GetMessage( &msg, NULL, 0, 0 ) <= 0 ) {
			Cmd_Clear();
			Com_Quit_f();
		}

		// save the msg time, because wndprocs don't have access to the timestamp
		//g_wv.sysMsgTime = msg.time;
		g_wv.sysMsgTime = Sys_Milliseconds();

		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}
}


/*
================
GLW_HideFullscreenWindow
================
*/
void GLW_HideFullscreenWindow( void ) {
	if ( g_wv.hWnd && glw_state.cdsFullscreen ) {
		ShowWindow( g_wv.hWnd, SW_HIDE );
	}
}


/*
================
Sys_GetClipboardData
================
*/
static char *WIN_WideClipboardTextToUtf8(
	const wchar_t *text, int characterCount )
{
	if ( !text || characterCount < 0 ) {
		return nullptr;
	}
	if ( characterCount == 0 ) {
		char *empty = static_cast<char *>( Z_Malloc( 1 ) );
		empty[0] = '\0';
		return empty;
	}

	const int utf8Bytes = WideCharToMultiByte(
		CP_UTF8, 0, text, characterCount,
		nullptr, 0, nullptr, nullptr );
	if ( utf8Bytes <= 0 ||
		utf8Bytes == ( std::numeric_limits<int>::max )() ) {
		return nullptr;
	}

	char *data = static_cast<char *>( Z_Malloc( utf8Bytes + 1 ) );
	if ( WideCharToMultiByte( CP_UTF8, 0,
			text, characterCount, data, utf8Bytes, nullptr, nullptr ) != utf8Bytes ) {
		Z_Free( data );
		return nullptr;
	}
	data[utf8Bytes] = '\0';
	strtok( data, "\n\r\b" );
	return data;
}


char *Sys_GetClipboardData( void ) {
	char *data = nullptr;

	fnql::win::ScopedClipboard clipboard( nullptr );
	if ( clipboard ) {
		if ( HANDLE unicodeHandle = GetClipboardData( CF_UNICODETEXT ) ) {
			fnql::win::ScopedGlobalLock<wchar_t> cliptext( unicodeHandle );
			const SIZE_T capacity = GlobalSize( unicodeHandle ) / sizeof( wchar_t );
			if ( cliptext && capacity > 0 ) {
				SIZE_T length = 0;
				while ( length < capacity && cliptext.get()[length] != L'\0' ) {
					++length;
				}
				if ( length <= static_cast<SIZE_T>(
						( std::numeric_limits<int>::max )() ) ) {
					data = WIN_WideClipboardTextToUtf8(
						cliptext.get(), static_cast<int>( length ) );
				}
			}
		}

		// Older applications may expose only CF_TEXT. Convert its active-code-
		// page bytes to Unicode first; never mislabel them as engine UTF-8.
		if ( !data ) {
			if ( HANDLE ansiHandle = GetClipboardData( CF_TEXT ) ) {
				fnql::win::ScopedGlobalLock<char> cliptext( ansiHandle );
				const SIZE_T capacity = GlobalSize( ansiHandle );
				if ( cliptext && capacity > 0 ) {
					SIZE_T length = 0;
					while ( length < capacity && cliptext.get()[length] != '\0' ) {
						++length;
					}
					if ( length <= static_cast<SIZE_T>(
							( std::numeric_limits<int>::max )() ) ) {
						const int byteCount = static_cast<int>( length );
						const int wideCount = MultiByteToWideChar(
							CP_ACP, 0, cliptext.get(), byteCount, nullptr, 0 );
						if ( wideCount > 0 ) {
							std::vector<wchar_t> wide( wideCount );
							if ( MultiByteToWideChar( CP_ACP, 0,
									cliptext.get(), byteCount,
									wide.data(), wideCount ) == wideCount ) {
								data = WIN_WideClipboardTextToUtf8(
									wide.data(), wideCount );
							}
						}
					}
				}
			}
		}
	}
	return data;
}


/*
================
Sys_SetClipboardData
================
*/
void Sys_SetClipboardData( const char *text )
{
	if ( !g_wv.hWnd )
		return;

	fnql::win::ScopedClipboard clipboard( g_wv.hWnd );
	if ( !clipboard )
		return;

	if ( !text ) {
		text = "";
	}

	int wideCount = MultiByteToWideChar(
		CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0 );
	if ( wideCount <= 0 ) {
		// Preserve a usable clipboard even if an external caller supplied a
		// malformed byte sequence; Windows substitutes replacement characters.
		wideCount = MultiByteToWideChar( CP_UTF8, 0, text, -1, nullptr, 0 );
	}
	if ( wideCount <= 0 ||
		static_cast<SIZE_T>( wideCount ) >
			( std::numeric_limits<SIZE_T>::max )() / sizeof( wchar_t ) ) {
		return;
	}

	fnql::win::ScopedGlobalMemory hMem( GlobalAlloc(
		GMEM_MOVEABLE, static_cast<SIZE_T>( wideCount ) * sizeof( wchar_t ) ) );
	if ( hMem ) {
		bool copied = false;
		{
			fnql::win::ScopedGlobalLock<wchar_t> ptr( hMem.get() );
			if ( ptr ) {
				copied = MultiByteToWideChar(
					CP_UTF8, 0, text, -1, ptr.get(), wideCount ) == wideCount;
			}
		}
		if ( copied && EmptyClipboard() &&
			SetClipboardData( CF_UNICODETEXT, hMem.get() ) ) {
			hMem.release();
		}
	}
}


/*
================
Sys_SetClipboardBitmap
================
*/
void Sys_SetClipboardBitmap( const byte *bitmap, int length )
{
	if ( !g_wv.hWnd )
		return;

	fnql::win::ScopedClipboard clipboard( g_wv.hWnd );
	if ( !clipboard )
		return;

	EmptyClipboard();
	fnql::win::ScopedGlobalMemory hMem( GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE, length ) );
	if ( hMem ) {
		bool copied = false;
		{
			fnql::win::ScopedGlobalLock<byte> ptr( hMem.get() );
			if ( ptr ) {
				memcpy( ptr.get(), bitmap, length ); 
				copied = true;
			}
		}
		if ( copied && SetClipboardData( CF_DIB, hMem.get() ) ) {
			hMem.release();
		}
	}
}
