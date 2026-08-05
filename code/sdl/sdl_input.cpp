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

#include <array>
#include <cmath>
#include <cstring>

#include "../client/client.h"
#include "../client/input_compat.hpp"
#ifndef _WIN32
#include "../unix/unix_syscon.h"
#endif
#include "sdl_glw.h"
#include "sdl_raii.h"

static cvar_t *in_keyboardDebug;
static cvar_t *in_forceCharset;

struct sdlKeyInfo_t {
	SDL_Scancode scancode;
	SDL_Keycode sym;
	SDL_Keymod mod;
};

#ifdef USE_JOYSTICK
static SDL_Gamepad *gamepad;
static SDL_Joystick *stick = NULL;
static SDL_JoystickID stickInstance;
#endif

static qboolean mouseAvailable = qfalse;

using fnql::input::PointerMode;
using fnql::input::PointerOwner;

static constexpr int kPointerMenuMask = KEYCATCH_UI | KEYCATCH_CGAME | KEYCATCH_BROWSER;

static cvar_t *in_mouse;

#ifdef USE_JOYSTICK
static cvar_t *in_joystick;
static cvar_t *in_joystickThreshold;
static cvar_t *in_joystickNo;
static cvar_t *in_joystickUseAnalog;

static cvar_t *j_pitch;
static cvar_t *j_yaw;
static cvar_t *j_forward;
static cvar_t *j_side;
static cvar_t *j_up;
static cvar_t *j_pitch_axis;
static cvar_t *j_yaw_axis;
static cvar_t *j_forward_axis;
static cvar_t *j_side_axis;
static cvar_t *j_up_axis;
#endif

#define Com_QueueEvent Sys_QueEvent

static cvar_t *cl_consoleKeys;

static int in_eventTime = 0;
static qboolean mouse_focus;

// Pointer ownership and presentation are resolved by the shared policy in
// input_compat.hpp so this backend, the native Win32 backend, and the X11
// backend cannot disagree about who owns the pointer for a given key catcher.
// s_pointerMode is the presentation that was last applied to SDL; it is latched
// so the per-frame update only issues SDL calls on an actual transition.
static PointerOwner s_pointerOwner = PointerOwner::Gameplay;
static PointerMode  s_pointerMode;
static qboolean     s_pointerModeValid;
static constexpr int kTextInputCatcherMask =
	KEYCATCH_CONSOLE | KEYCATCH_UI | KEYCATCH_MESSAGE |
	KEYCATCH_BROWSER;
static qboolean     s_textInputActive;
static qboolean     s_textInputFailureReported;

// One dedup cache for every absolute owner. The owner is recorded with it so a
// cached sample can never suppress the first sample the next owner receives
// after a catcher change.
static PointerOwner s_absLastOwner = PointerOwner::Gameplay;
static int          s_absLastConsumer;
static qboolean     s_absHaveLast; // s_absLast{X,Y} hold a valid previous sample
static int          s_absLastX;    // last reported position, to suppress duplicates
static int          s_absLastY;
static Uint32       s_absCaptureButtons;
static qboolean     s_absCaptureActive;
static qboolean     s_absCaptureFailureReported;
static qboolean     s_absPointerOutside;
static float        s_relativeRemainderX;
static float        s_relativeRemainderY;

struct sdlWheelAccumulator_t {
	SDL_MouseID device = 0;
	int consumer = 0;
	float remainder = 0.0f;
	std::uint64_t lastUse = 0;
	bool valid = false;
};

static std::array<sdlWheelAccumulator_t, 8> s_wheelAccumulators;
static std::uint64_t s_wheelAccumulatorUse;
static unsigned int s_mouseAuxButtonState;
static qboolean     s_pointerApplyFailureReported;
static SDL_Keymod   s_physicalModifiers;
static int          s_lastKeyDown;
static qboolean     s_lastKeyDownWasRepeat;
static qboolean     s_fullscreenOcclusionReset;

#define CTRL(a) ((a)-'a'+1)

static qboolean IN_ShowCursor( qboolean show )
{
	if ( show ) {
		return SDL_ShowCursor() ? qtrue : qfalse;
	}
	return SDL_HideCursor() ? qtrue : qfalse;
}


static qboolean IN_TextInputOwnerActive( void )
{
	if ( !gw_active || gw_minimized ) {
		return qfalse;
	}

	return ( cls.state == CA_DISCONNECTED ||
		( Key_GetCatcher() & kTextInputCatcherMask ) ) ? qtrue : qfalse;
}


static void IN_SetTextInputActive( qboolean active )
{
	if ( !SDL_window ) {
		s_textInputActive = qfalse;
		s_textInputFailureReported = qfalse;
		return;
	}
	if ( s_textInputActive == active ) {
		s_textInputFailureReported = qfalse;
		return;
	}

	if ( active ) {
		if ( !SDL_StartTextInput( SDL_window ) ) {
			if ( !s_textInputFailureReported ) {
				Com_Printf( S_COLOR_YELLOW
					"Unable to enable SDL text input: %s\n",
					SDL_GetError() );
				s_textInputFailureReported = qtrue;
			}
			return;
		}
	} else {
		// Abandon an unfinished IME/dead-key sequence before returning the
		// keyboard to gameplay bindings. Stopping must still be attempted if
		// the platform has no active composition to clear.
		if ( !SDL_ClearComposition( SDL_window ) ) {
			Com_DPrintf( "SDL_ClearComposition failed: %s\n", SDL_GetError() );
		}
		if ( !SDL_StopTextInput( SDL_window ) ) {
			if ( !s_textInputFailureReported ) {
				Com_Printf( S_COLOR_YELLOW
					"Unable to disable SDL text input: %s\n",
					SDL_GetError() );
				s_textInputFailureReported = qtrue;
			}
			return;
		}
	}

	s_textInputActive = active;
	s_textInputFailureReported = qfalse;
}


static void IN_ReconcileTextInput( void )
{
	IN_SetTextInputActive( IN_TextInputOwnerActive() );
}


static void IN_UpdateTemporaryMouseCapture( void )
{
	const qboolean requested = s_absCaptureButtons ? qtrue : qfalse;

	if ( requested == s_absCaptureActive ) {
		s_absCaptureFailureReported = qfalse;
		return;
	}
	if ( !SDL_CaptureMouse( requested != qfalse ) ) {
		if ( !s_absCaptureFailureReported ) {
			Com_Printf( S_COLOR_YELLOW
				"SDL temporary mouse capture transition failed: %s\n",
				SDL_GetError() );
			s_absCaptureFailureReported = qtrue;
		}
		return;
	}

	s_absCaptureActive = requested;
	s_absCaptureFailureReported = qfalse;
}


static void IN_FinishOutsideCaptureRelease( void )
{
	if ( s_absPointerOutside && !s_absCaptureButtons &&
		!s_absCaptureActive ) {
		// Capture kept the final release routable outside the window. Once SDL
		// confirms capture is gone, stop polling that outside desktop position
		// until this game window receives MOUSE_ENTER again.
		s_absPointerOutside = qfalse;
		mouse_focus = qfalse;
		s_absHaveLast = qfalse;
	}
}


static void IN_EndTemporaryMouseCapture( void )
{
	s_absCaptureButtons = 0;
	IN_UpdateTemporaryMouseCapture();
	IN_FinishOutsideCaptureRelease();
}


static void IN_ResetWheelAccumulator( void )
{
	s_wheelAccumulators.fill( sdlWheelAccumulator_t{} );
	s_wheelAccumulatorUse = 0;
}


static float& IN_WheelRemainder( SDL_MouseID device, int consumer )
{
	sdlWheelAccumulator_t *replacement = &s_wheelAccumulators[0];

	++s_wheelAccumulatorUse;
	if ( s_wheelAccumulatorUse == 0 ) {
		// Preserve deterministic replacement even after the theoretical
		// counter wrap; no wheel remainder depends on the age value itself.
		for ( sdlWheelAccumulator_t& slot : s_wheelAccumulators ) {
			slot.lastUse = 0;
		}
		s_wheelAccumulatorUse = 1;
	}

	for ( sdlWheelAccumulator_t& slot : s_wheelAccumulators ) {
		if ( slot.valid && slot.device == device &&
			slot.consumer == consumer ) {
			slot.lastUse = s_wheelAccumulatorUse;
			return slot.remainder;
		}
		if ( !slot.valid ) {
			replacement = &slot;
			break;
		}
		if ( slot.lastUse < replacement->lastUse ) {
			replacement = &slot;
		}
	}

	*replacement = sdlWheelAccumulator_t{
		device, consumer, 0.0f, s_wheelAccumulatorUse, true
	};
	return replacement->remainder;
}


static void IN_QueueMouseReset( void )
{
	Com_QueueEvent( in_eventTime, SE_MOUSE_RESET,
		static_cast<int>( s_mouseAuxButtonState ), 0, 0, NULL );
	s_mouseAuxButtonState = 0;
}


/*
===============
IN_ResolvePointerOwner

Cheap enough to re-evaluate per event, so a catcher change made while draining
the SDL queue takes effect on the very next event instead of the next frame.
===============
*/
static PointerOwner IN_ResolvePointerOwner( void )
{
	fnql::input::PointerOwnerInputs inputs;

	inputs.catcher = Key_GetCatcher();
	inputs.consoleMask = KEYCATCH_CONSOLE;
	inputs.menuMask = kPointerMenuMask;
	// A fullscreen window exposes no desktop for the OS pointer to reach, so the
	// fullscreen console keeps the established relative gameplay pointer.
	inputs.consoleUsesAbsolutePointer = !glw_state.isFullscreen;

	return fnql::input::ResolvePointerOwner( inputs );
}


static PointerMode IN_ResolvePointerMode( PointerOwner owner )
{
	fnql::input::PointerModeInputs inputs;

	inputs.owner = owner;
	inputs.focused = ( gw_active && mouse_focus ) ? true : false;
	inputs.minimized = gw_minimized ? true : false;
	inputs.fullscreen = glw_state.isFullscreen ? true : false;
	// -1 is the native-Windows selector, but archived/cloud configurations can
	// reach an SDL build. SDL has one relative implementation, so every
	// nonzero value degrades to it instead of disabling motion at the edge.
	inputs.relativeAvailable = in_mouse->integer != 0;

	return fnql::input::ResolvePointerMode( inputs );
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

/*
===============
IN_PrintKey
===============
*/
static void IN_PrintKey( const sdlKeyInfo_t *keyinfo, int key, qboolean down )
{
	if( down )
		Com_Printf( "+ " );
	else
		Com_Printf( "  " );

	Com_Printf( "Scancode: 0x%02x(%s) Sym: 0x%02x(%s)",
			keyinfo->scancode, SDL_GetScancodeName( keyinfo->scancode ),
			keyinfo->sym, SDL_GetKeyName( keyinfo->sym ) );

	if( keyinfo->mod & SDL_KMOD_LSHIFT )   Com_Printf( " SDL_KMOD_LSHIFT" );
	if( keyinfo->mod & SDL_KMOD_RSHIFT )   Com_Printf( " SDL_KMOD_RSHIFT" );
	if( keyinfo->mod & SDL_KMOD_LCTRL )    Com_Printf( " SDL_KMOD_LCTRL" );
	if( keyinfo->mod & SDL_KMOD_RCTRL )    Com_Printf( " SDL_KMOD_RCTRL" );
	if( keyinfo->mod & SDL_KMOD_LALT )     Com_Printf( " SDL_KMOD_LALT" );
	if( keyinfo->mod & SDL_KMOD_RALT )     Com_Printf( " SDL_KMOD_RALT" );
	if( keyinfo->mod & SDL_KMOD_LGUI )     Com_Printf( " SDL_KMOD_LGUI" );
	if( keyinfo->mod & SDL_KMOD_RGUI )     Com_Printf( " SDL_KMOD_RGUI" );
	if( keyinfo->mod & SDL_KMOD_NUM )      Com_Printf( " SDL_KMOD_NUM" );
	if( keyinfo->mod & SDL_KMOD_CAPS )     Com_Printf( " SDL_KMOD_CAPS" );
	if( keyinfo->mod & SDL_KMOD_LEVEL5 )   Com_Printf( " SDL_KMOD_LEVEL5" );
	if( keyinfo->mod & SDL_KMOD_MODE )     Com_Printf( " SDL_KMOD_MODE" );

	Com_Printf( " Q:0x%02x(%s)\n", key, Key_KeynumToString( key ) );
}


#define MAX_CONSOLE_KEYS 16

/*
===============
IN_IsConsoleKey

TODO: If the SDL_Scancode situation improves, use it instead of
      both of these methods
===============
*/
static qboolean IN_IsConsoleKey( int key, int character )
{
	enum consoleKeyType_t
	{
		QUAKE_KEY,
		CHARACTER
	};

	struct consoleKey_t {
		consoleKeyType_t type;

		union
		{
			int key;
			int character;
		} u;
	};

	static std::array<consoleKey_t, MAX_CONSOLE_KEYS> consoleKeys = {};
	static int numConsoleKeys = 0;
	int i;

	// Only parse the variable when it changes
	if ( cl_consoleKeys->modified )
	{
		const char *text_p, *token;

		cl_consoleKeys->modified = qfalse;
		text_p = cl_consoleKeys->string;
		numConsoleKeys = 0;

		while( numConsoleKeys < MAX_CONSOLE_KEYS )
		{
			consoleKey_t *c = &consoleKeys[ numConsoleKeys ];
			int charCode = 0;

			token = COM_Parse( &text_p );
			if( !token[ 0 ] )
				break;

			charCode = Com_HexStrToInt( token );

			if( charCode > 0 )
			{
				c->type = CHARACTER;
				c->u.character = charCode;
			}
			else
			{
				c->type = QUAKE_KEY;
				c->u.key = Key_StringToKeynum( token );

				// 0 isn't a key
				if ( c->u.key <= 0 )
					continue;
			}

			numConsoleKeys++;
		}
	}

	// If the character is the same as the key, prefer the character
	if ( key == character )
		key = 0;

	for ( i = 0; i < numConsoleKeys; i++ )
	{
		consoleKey_t *c = &consoleKeys[ i ];

		switch ( c->type )
		{
			case QUAKE_KEY:
				if( key && c->u.key == key )
					return qtrue;
				break;

			case CHARACTER:
				if( c->u.character == character )
					return qtrue;
				break;
		}
	}

	return qfalse;
}


static qboolean IN_IsLayoutConsoleKey( const sdlKeyInfo_t *keyinfo )
{
	const SDL_Keymod modifiers = keyinfo->mod;
	if ( modifiers & ( SDL_KMOD_GUI | SDL_KMOD_LALT ) ) {
		return qfalse;
	}

	const qboolean rightAlt =
		( modifiers & SDL_KMOD_RALT ) ? qtrue : qfalse;
	const qboolean mode =
		( modifiers & SDL_KMOD_MODE ) ? qtrue : qfalse;
	if ( ( modifiers & SDL_KMOD_CTRL ) && !rightAlt && !mode ) {
		return qfalse;
	}

	SDL_Keycode candidate = SDL_GetKeyFromScancode(
		keyinfo->scancode, modifiers, false );

	if ( rightAlt ) {
		// Windows commonly reports AltGr as synthetic Ctrl+RightAlt. Verify it
		// against SDL's explicit layout-mode translation. SDL deliberately
		// normalizes raw RightAlt as an Alt command modifier, so use the verified
		// MODE result rather than requiring that fallback to equal it.
		const SDL_Keymod normalModifiers = static_cast<SDL_Keymod>(
			modifiers & ~( SDL_KMOD_CTRL | SDL_KMOD_RALT | SDL_KMOD_MODE ) );
		const SDL_Keycode normalCandidate = SDL_GetKeyFromScancode(
			keyinfo->scancode, normalModifiers, false );
		const SDL_Keycode altGrCandidate = SDL_GetKeyFromScancode(
			keyinfo->scancode,
			static_cast<SDL_Keymod>( normalModifiers | SDL_KMOD_MODE ), false );
		if ( altGrCandidate == normalCandidate ) {
			return qfalse;
		}
		candidate = altGrCandidate;
	}

	// SDL reserves high bits for scancode and extended keycode namespaces.
	// Only a real Unicode scalar can represent a character entry from
	// cl_consoleKeys.
	if ( candidate & ( SDLK_SCANCODE_MASK | SDLK_EXTENDED_MASK ) ) {
		return qfalse;
	}
	if ( candidate == 0 || candidate > 0x10ffffu ||
		( candidate >= 0xd800u && candidate <= 0xdfffu ) ) {
		return qfalse;
	}

	return IN_IsConsoleKey( 0, static_cast<int>( candidate ) );
}


/*
===============
IN_TranslateSDLToQ3Key
===============
*/
static int IN_TranslateSDLToQ3Key( const sdlKeyInfo_t *keyinfo, qboolean down )
{
	int key = 0;

	if ( keyinfo->scancode >= SDL_SCANCODE_1 && keyinfo->scancode <= SDL_SCANCODE_0 )
	{
		// Always map the number keys as such even if they actually map
		// to other characters (eg, "1" is "&" on an AZERTY keyboard).
		// This is required for SDL before 2.0.6, except on Windows
		// which already had this behavior.
		if( keyinfo->scancode == SDL_SCANCODE_0 )
			key = '0';
		else
			key = '1' + keyinfo->scancode - SDL_SCANCODE_1;
	}
	else if ( in_forceCharset->integer > 0 )
	{
		if ( keyinfo->scancode >= SDL_SCANCODE_A && keyinfo->scancode <= SDL_SCANCODE_Z )
		{
			key = 'a' + keyinfo->scancode - SDL_SCANCODE_A;
		}
		else
		{
			switch ( keyinfo->scancode )
			{
				case SDL_SCANCODE_MINUS:        key = '-';  break;
				case SDL_SCANCODE_EQUALS:       key = '=';  break;
				case SDL_SCANCODE_LEFTBRACKET:  key = '[';  break;
				case SDL_SCANCODE_RIGHTBRACKET: key = ']';  break;
				case SDL_SCANCODE_NONUSBACKSLASH:
				case SDL_SCANCODE_BACKSLASH:    key = '\\'; break;
				case SDL_SCANCODE_SEMICOLON:    key = ';';  break;
				case SDL_SCANCODE_APOSTROPHE:   key = '\''; break;
				case SDL_SCANCODE_COMMA:        key = ',';  break;
				case SDL_SCANCODE_PERIOD:       key = '.';  break;
				case SDL_SCANCODE_SLASH:        key = '/';  break;
				default:
					/* key = 0 */
					break;
			}
		}
	}

	if( !key && keyinfo->sym >= SDLK_SPACE && keyinfo->sym < SDLK_DELETE )
	{
		// These happen to match the ASCII chars
		key = (int)keyinfo->sym;
	}
	else if( !key )
	{
		switch( keyinfo->sym )
		{
			case SDLK_PAGEUP:       key = K_PGUP;          break;
			case SDLK_KP_9:         key = K_KP_PGUP;       break;
			case SDLK_PAGEDOWN:     key = K_PGDN;          break;
			case SDLK_KP_3:         key = K_KP_PGDN;       break;
			case SDLK_KP_7:         key = K_KP_HOME;       break;
			case SDLK_HOME:         key = K_HOME;          break;
			case SDLK_KP_1:         key = K_KP_END;        break;
			case SDLK_END:          key = K_END;           break;
			case SDLK_KP_4:         key = K_KP_LEFTARROW;  break;
			case SDLK_LEFT:         key = K_LEFTARROW;     break;
			case SDLK_KP_6:         key = K_KP_RIGHTARROW; break;
			case SDLK_RIGHT:        key = K_RIGHTARROW;    break;
			case SDLK_KP_2:         key = K_KP_DOWNARROW;  break;
			case SDLK_DOWN:         key = K_DOWNARROW;     break;
			case SDLK_KP_8:         key = K_KP_UPARROW;    break;
			case SDLK_UP:           key = K_UPARROW;       break;
			case SDLK_ESCAPE:       key = K_ESCAPE;        break;
			case SDLK_KP_ENTER:     key = K_KP_ENTER;      break;
			case SDLK_RETURN:       key = K_ENTER;         break;
			case SDLK_TAB:          key = K_TAB;           break;
			case SDLK_F1:           key = K_F1;            break;
			case SDLK_F2:           key = K_F2;            break;
			case SDLK_F3:           key = K_F3;            break;
			case SDLK_F4:           key = K_F4;            break;
			case SDLK_F5:           key = K_F5;            break;
			case SDLK_F6:           key = K_F6;            break;
			case SDLK_F7:           key = K_F7;            break;
			case SDLK_F8:           key = K_F8;            break;
			case SDLK_F9:           key = K_F9;            break;
			case SDLK_F10:          key = K_F10;           break;
			case SDLK_F11:          key = K_F11;           break;
			case SDLK_F12:          key = K_F12;           break;
			case SDLK_F13:          key = K_F13;           break;
			case SDLK_F14:          key = K_F14;           break;
			case SDLK_F15:          key = K_F15;           break;

			case SDLK_BACKSPACE:    key = K_BACKSPACE;     break;
			case SDLK_KP_PERIOD:    key = K_KP_DEL;        break;
			case SDLK_DELETE:       key = K_DEL;           break;
			case SDLK_PAUSE:        key = K_PAUSE;         break;

			case SDLK_LSHIFT:
			case SDLK_RSHIFT:       key = K_SHIFT;         break;

			case SDLK_LCTRL:
			case SDLK_RCTRL:        key = K_CTRL;          break;

#ifdef SDL_PLATFORM_APPLE
			case SDLK_RGUI:
			case SDLK_LGUI:         key = K_COMMAND;       break;
#else
			case SDLK_RGUI:
			case SDLK_LGUI:         key = K_SUPER;         break;
#endif

			case SDLK_RALT:
			case SDLK_LALT:         key = K_ALT;           break;

			case SDLK_KP_5:         key = K_KP_5;          break;
			case SDLK_INSERT:       key = K_INS;           break;
			case SDLK_KP_0:         key = K_KP_INS;        break;
			case SDLK_KP_MULTIPLY:  key = '*'; /*K_KP_STAR;*/ break;
			case SDLK_KP_PLUS:      key = K_KP_PLUS;       break;
			case SDLK_KP_MINUS:     key = K_KP_MINUS;      break;
			case SDLK_KP_DIVIDE:    key = K_KP_SLASH;      break;
			case SDLK_KP_EQUALS:    key = K_KP_EQUALS;     break;

			case SDLK_MODE:         key = K_MODE;          break;
			case SDLK_LEVEL5_SHIFT: key = K_MODE;          break;
			case SDLK_MULTI_KEY_COMPOSE: key = K_COMPOSE;  break;
			case SDLK_HELP:         key = K_HELP;          break;
			case SDLK_PRINTSCREEN:  key = K_PRINT;         break;
			case SDLK_SYSREQ:       key = K_SYSREQ;        break;
			case SDLK_MENU:         key = K_MENU;          break;
			case SDLK_APPLICATION:	key = K_MENU;          break;
			case SDLK_POWER:        key = K_POWER;         break;
			case SDLK_UNDO:         key = K_UNDO;          break;
			case SDLK_SCROLLLOCK:   key = K_SCROLLOCK;     break;
			case SDLK_NUMLOCKCLEAR: key = K_KP_NUMLOCK;    break;
			case SDLK_CAPSLOCK:     key = K_CAPSLOCK;      break;

			default:
#if 1
				key = 0;
#else
				if( !( keyinfo->sym & SDLK_SCANCODE_MASK ) && keyinfo->scancode <= 95 )
				{
					// Map Unicode characters to 95 world keys using the key's scan code.
					// FIXME: There aren't enough world keys to cover all the scancodes.
					// Maybe create a map of scancode to quake key at start up and on
					// key map change; allocate world key numbers as needed similar
					// to SDL 1.2.
					key = K_WORLD_0 + (int)keyinfo->scancode;
				}
#endif
				break;
		}
	}

	if ( in_keyboardDebug->integer )
		IN_PrintKey( keyinfo, key, down );

	if ( keyinfo->scancode == SDL_SCANCODE_GRAVE )
	{
		//SDL_Keycode translated = SDL_GetKeyFromScancode( SDL_SCANCODE_GRAVE );

		//if ( translated == SDLK_CARET )
		{
			// Console keys can't be bound or generate characters
			key = K_CONSOLE;
		}
	}
	else if ( IN_IsConsoleKey( key, 0 ) ||
		( down && IN_IsLayoutConsoleKey( keyinfo ) ) )
	{
		// Console keys can't be bound or generate characters
		key = K_CONSOLE;
	}

	return key;
}


/*
===============
IN_GobbleMotionEvents
===============
*/
static void IN_GobbleMotionEvents( void )
{
	SDL_Event dummy[ 1 ];
	int val = 0;

	// Relative-mode transitions can synthesize motion, but button releases and
	// wheel events are stateful and must retain their queue ordering.
	SDL_PumpEvents();

	while( ( val = SDL_PeepEvents( dummy, ARRAY_LEN( dummy ), SDL_GETEVENT,
		SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_MOTION ) ) > 0 ) { }

	if ( val < 0 )
		Com_Printf( "%s failed: %s\n", __func__, SDL_GetError() );
}


/*
===============
IN_PointerProjection

SDL reports motion in logical window coordinates. Every absolute consumer - the
console, the WebUI browser, and retail's UI and cgame overlays - addresses
renderer drawable pixels, so the two differ on a scaled desktop and whenever the
renderer resolution is not the window size.
===============
*/
static fnql::input::PointerProjection IN_PointerProjection( void )
{
	fnql::input::PointerProjection projection;

	projection.hostWidth = glw_state.window_width;
	projection.hostHeight = glw_state.window_height;
	projection.drawableWidth = cls.glconfig.vidWidth;
	projection.drawableHeight = cls.glconfig.vidHeight;

	return projection;
}


/*
===============
IN_QueueAbsolutePointerPosition

Single absolute-position lane for the console, retail UI/cgame, and the browser.

Emitted only when the position or the owner changes: an unconditional per-drain
event would keep the event queue perpetually non-empty and stall Com_EventLoop,
which re-pumps input whenever the queue drains.
===============
*/
static void IN_QueueAbsolutePointerPosition( PointerOwner owner,
	float windowX, float windowY, int eventTime, qboolean force = qfalse )
{
	const int consumer = IN_PointerConsumerIdentity();
	const fnql::input::PointerPosition position =
		fnql::input::ProjectPointerToDrawable(
			windowX, windowY, IN_PointerProjection() );

	if ( !force && s_absHaveLast && owner == s_absLastOwner
		&& consumer == s_absLastConsumer
		&& position.x == s_absLastX && position.y == s_absLastY ) {
		return;
	}

	if ( !force ) {
		s_absLastOwner = owner;
		s_absLastConsumer = consumer;
		s_absLastX = position.x;
		s_absLastY = position.y;
		s_absHaveLast = qtrue;
	}

	Com_QueueEvent( eventTime, SE_MOUSE_ABSOLUTE, position.x, position.y, 0, NULL );
}


/*
===============
IN_PollAbsolutePointerPosition

Retail polls the window cursor while an absolute owner is active instead of
depending solely on motion messages, so a menu or console opened under a
stationary pointer still receives deterministic coordinates.
===============
*/
static void IN_PollAbsolutePointerPosition( PointerOwner owner )
{
	float x = 0.0f;
	float y = 0.0f;

	SDL_GetMouseState( &x, &y );
	IN_QueueAbsolutePointerPosition( owner, x, y, in_eventTime );
}


//#define DEBUG_EVENTS

/*
===============
IN_RestoreDesktopPointer

Run when an active mode transition stops driving the relative pointer. Some SDL
video drivers leave it at the window edge after an ungrab, so normalize it
inside the active window. Absolute ownership and focus loss leave the user's
visible cursor untouched.
===============
*/
static void IN_RestoreDesktopPointer( PointerOwner previousOwner )
{
	if ( fnql::input::PointerOwnerReportsAbsolute( previousOwner ) ||
		!gw_active || gw_minimized || !SDL_window ) {
		return;
	}

	// Releasing capture during an active mode transition may leave the hidden
	// relative pointer at a window edge. Focus loss is different: the desktop
	// cursor already belongs to the user and must never be teleported, notably
	// to the centre of an unrelated monitor on X11.
	SDL_WarpMouseInWindow( SDL_window,
		glw_state.window_width / 2, glw_state.window_height / 2 );
}


/*
===============
IN_ApplyPointerMode

Re-evaluated every frame so catcher, focus, and fullscreen transitions update
confinement, relative motion, and cursor visibility without an input restart.
The applied mode is latched, so a steady state issues no SDL calls at all.
===============
*/
static void IN_ApplyPointerMode( void )
{
	PointerOwner previousOwner;
	PointerOwner owner;
	PointerMode mode;

	if ( !mouseAvailable || !SDL_window ) {
		return;
	}

	previousOwner = s_pointerOwner;
	owner = IN_ResolvePointerOwner();
	mode = IN_ResolvePointerMode( owner );

	// Reconcile against physical state every frame. This both recovers a
	// release lost outside the event queue and retries a failed SDL capture or
	// release transition without flooding the log.
	if ( s_absCaptureButtons ) {
		const Uint32 reconciledButtons =
			s_absCaptureButtons & SDL_GetMouseState( nullptr, nullptr );
		if ( reconciledButtons != s_absCaptureButtons ) {
			// A release disappeared outside SDL's queue. Balance every logical
			// mouse key at an ordered barrier, then release capture completely.
			// The reset releases every logical button, so retaining the
			// still-physical subset would create capture without matching downs.
			IN_QueueMouseReset();
			IN_EndTemporaryMouseCapture();
		} else {
			IN_UpdateTemporaryMouseCapture();
		}
	}
	if ( s_absCaptureActive && !s_absCaptureButtons ) {
		// Retry a failed final release without forgetting that SDL still owns
		// capture, then stop outside polling as soon as it succeeds.
		IN_UpdateTemporaryMouseCapture();
		IN_FinishOutsideCaptureRelease();
	}

	// in_nograb releases the gameplay pointer for streaming and debugging.
	// Overlay owners already run with a free pointer, so the request only has to
	// suppress the relative gameplay path.
	if ( in_nograb->integer && owner == PointerOwner::Gameplay ) {
		mode = PointerMode{};
	}

	if ( owner != previousOwner ) {
		// The coordinate lane changed. Drop drag capture and any pending
		// relative motion so the new owner starts from a clean sample.
		IN_EndTemporaryMouseCapture();
		IN_GobbleMotionEvents();
		s_relativeRemainderX = 0.0f;
		s_relativeRemainderY = 0.0f;
		s_absHaveLast = qfalse;
		s_pointerOwner = owner;

		// Escape can hand a button-down already queued in the gameplay lane to
		// an absolute owner. Transfer capture for that physically and logically
		// held drag so its eventual release cannot be lost outside the window.
		if ( fnql::input::PointerOwnerReportsAbsolute( owner ) ) {
			const Uint32 heldButtons = SDL_GetMouseState( nullptr, nullptr );
			qboolean logicalButtonDown = qfalse;
			for ( int key = K_MOUSE1; key <= K_MOUSE9; ++key ) {
				if ( keys[key].down ) {
					logicalButtonDown = qtrue;
					break;
				}
			}
			if ( s_mouseAuxButtonState ) {
				logicalButtonDown = qtrue;
			}
			if ( heldButtons && logicalButtonDown ) {
				s_absCaptureButtons = heldButtons;
				IN_UpdateTemporaryMouseCapture();
			}
		}
	}

	if ( s_pointerModeValid && mode == s_pointerMode && !in_nograb->modified ) {
		return;
	}

	// Discard the motion spike SDL emits when relative mode is toggled.
	if ( s_pointerModeValid && mode.relativeMotion != s_pointerMode.relativeMotion ) {
		IN_GobbleMotionEvents();
		s_relativeRemainderX = 0.0f;
		s_relativeRemainderY = 0.0f;
	}

	if ( !mode.driveInput ) {
		IN_EndTemporaryMouseCapture();
		s_absHaveLast = qfalse;
	}

	const qboolean relativeApplied =
		SDL_SetWindowRelativeMouseMode( SDL_window, mode.relativeMotion ) ? qtrue : qfalse;
	const qboolean confinementApplied =
		SDL_SetWindowMouseGrab( SDL_window, mode.confineToWindow ) ? qtrue : qfalse;
	const qboolean cursorApplied =
		IN_ShowCursor( mode.showSystemCursor ? qtrue : qfalse );

	if ( !relativeApplied || !confinementApplied || !cursorApplied ) {
		if ( !s_pointerApplyFailureReported ) {
			Com_Printf( "%s: SDL pointer transition failed: %s\n",
				__func__, SDL_GetError() );
			s_pointerApplyFailureReported = qtrue;
		}

		// Leave the desktop usable while retaining an invalid latch so the
		// requested mode is retried on the next frame.
		SDL_SetWindowRelativeMouseMode( SDL_window, false );
		SDL_SetWindowMouseGrab( SDL_window, false );
		IN_ShowCursor( qtrue );
		s_pointerMode = PointerMode{};
		s_pointerModeValid = qfalse;
		return;
	}
	s_pointerApplyFailureReported = qfalse;

	// Re-centre only when the confined relative pointer is entered. Warping a
	// visible menu or console cursor would fight the player.
	if ( mode.recenterPointer &&
		( !s_pointerModeValid || !s_pointerMode.recenterPointer ) ) {
		SDL_WarpMouseInWindow( SDL_window,
			glw_state.window_width / 2, glw_state.window_height / 2 );
	} else if ( s_pointerModeValid && s_pointerMode.driveInput && !mode.driveInput ) {
		IN_RestoreDesktopPointer( previousOwner );
	}

	s_pointerMode = mode;
	s_pointerModeValid = qtrue;
	in_nograb->modified = qfalse;

#ifdef DEBUG_EVENTS
	Com_Printf( "%4i %s drive=%i abs=%i relative=%i confine=%i cursor=%i\n",
		Sys_Milliseconds(), __func__, mode.driveInput, mode.reportAbsolute,
		mode.relativeMotion, mode.confineToWindow, mode.showSystemCursor );
#endif
}


/*
===============
IN_ReleasePointer

Unconditional release for input shutdown. IN_ApplyPointerMode owns the per-frame
transitions; this exists so a shutdown or restart cannot leave SDL confined,
relative, or with a hidden cursor.
===============
*/
static void IN_ReleasePointer( void )
{
	const PointerOwner previousOwner = s_pointerOwner;
	const qboolean wasDriving = ( s_pointerModeValid && s_pointerMode.driveInput ) ? qtrue : qfalse;

	IN_EndTemporaryMouseCapture();
	IN_GobbleMotionEvents();

	if ( SDL_window ) {
		SDL_SetWindowMouseGrab( SDL_window, false );
		SDL_SetWindowRelativeMouseMode( SDL_window, false );
	}
	// Disabling relative/grab state can make a previously failed capture
	// release succeed. Preserve the latch if it still fails so an input restart
	// can retry instead of forgetting an active OS capture.
	IN_EndTemporaryMouseCapture();
	IN_ShowCursor( qtrue );

	if ( wasDriving ) {
		IN_RestoreDesktopPointer( previousOwner );
	}

	s_pointerOwner = PointerOwner::Gameplay;
	s_pointerMode = PointerMode{};
	s_pointerModeValid = qfalse;
	s_absHaveLast = qfalse;
	s_absPointerOutside = qfalse;
	s_relativeRemainderX = 0.0f;
	s_relativeRemainderY = 0.0f;
	IN_ResetWheelAccumulator();
	s_pointerApplyFailureReported = qfalse;
}


#ifdef USE_JOYSTICK
// We translate axes movement into keypresses
static const int joy_keys[16] = {
	K_LEFTARROW, K_RIGHTARROW,
	K_UPARROW, K_DOWNARROW,
	K_JOY17, K_JOY18,
	K_JOY19, K_JOY20,
	K_JOY21, K_JOY22,
	K_JOY23, K_JOY24,
	K_JOY25, K_JOY26,
	K_JOY27, K_JOY28
};

// translate hat events into keypresses
// the 4 highest buttons are used for the first hat ...
static const int hat_keys[16] = {
	K_JOY29, K_JOY30,
	K_JOY31, K_JOY32,
	K_JOY25, K_JOY26,
	K_JOY27, K_JOY28,
	K_JOY21, K_JOY22,
	K_JOY23, K_JOY24,
	K_JOY17, K_JOY18,
	K_JOY19, K_JOY20
};

static constexpr int kRawJoystickButtonCount = 16;
static constexpr int kSupportedGamepadButtonCount =
	static_cast<int>( SDL_GAMEPAD_BUTTON_TOUCHPAD ) + 1;
static constexpr int kRawDigitalAxisCount =
	static_cast<int>( ARRAY_LEN( joy_keys ) ) / 2;
static qboolean s_joystickSubsystemAcquired;
static qboolean s_gamepadSubsystemAcquired;

static_assert( SDL_GAMEPAD_BUTTON_SOUTH == 0 );
static_assert(
	SDL_GAMEPAD_BUTTON_TOUCHPAD - SDL_GAMEPAD_BUTTON_MISC1 ==
	K_PAD0_TOUCHPAD - K_PAD0_MISC1 );

struct joystickState_t {
	qboolean gamepadButtons[kSupportedGamepadButtonCount];
	qboolean rawButtons[kRawJoystickButtonCount];
	unsigned int oldaxes;
	int oldaaxes[MAX_JOYSTICK_AXIS];
	int oldTranslatedAxes[MAX_JOYSTICK_AXIS];
	qboolean gamepadDigitalDirections[SDL_GAMEPAD_AXIS_COUNT * 2];
	unsigned int oldhats;
};

static joystickState_t stick_state;

static void IN_CloseJoystickHandles( void )
{
	if ( gamepad )
	{
		SDL_CloseGamepad( gamepad );
		gamepad = NULL;
	}

	if ( stick )
	{
		SDL_CloseJoystick( stick );
		stick = NULL;
	}

	stickInstance = 0;
	stick_state = {};
}


static qboolean IN_AcquireJoystickSubsystem(
	Uint32 flags, qboolean *acquired, const char *name )
{
	if ( *acquired ) {
		return qtrue;
	}

	Com_DPrintf( "Calling SDL_InitSubSystem(%s)...\n", name );
	if ( !SDL_InitSubSystem( flags ) ) {
		Com_DPrintf( "SDL_InitSubSystem(%s) failed: %s\n",
			name, SDL_GetError() );
		return qfalse;
	}
	*acquired = qtrue;
	Com_DPrintf( "SDL_InitSubSystem(%s) passed.\n", name );
	return qtrue;
}


/*
===============
IN_InitJoystick
===============
*/
static void IN_InitJoystick( void )
{
	cvar_t *cv;
	int i = 0;
	int total = 0;
	char buf[16384] = "";

	IN_CloseJoystickHandles();

	// Acquire one explicit reference for each subsystem, even when another SDL
	// owner already initialized it. The persistent flags prevent hotplug
	// re-enumeration from acquiring another reference.
	if ( !IN_AcquireJoystickSubsystem(
			SDL_INIT_JOYSTICK, &s_joystickSubsystemAcquired,
			"SDL_INIT_JOYSTICK" ) ) {
		return;
	}
	if ( !IN_AcquireJoystickSubsystem(
			SDL_INIT_GAMEPAD, &s_gamepadSubsystemAcquired,
			"SDL_INIT_GAMEPAD" ) ) {
		return;
	}

	fnql::sdl::ScopedSdlMemory<SDL_JoystickID> joysticks( SDL_GetJoysticks( &total ) );
	if ( !joysticks ) {
		total = 0;
	}
	Com_DPrintf( "%d possible joysticks\n", total );

	// Print list and build cvar to allow ui to select joystick.
	for ( i = 0; i < total; i++ )
	{
		const char *name = SDL_GetJoystickNameForID( joysticks.get()[i] );

		Q_strcat( buf, sizeof( buf ), name ? name : "Unknown joystick" );
		Q_strcat( buf, sizeof( buf ), "\n" );
	}

	cv = Cvar_Get( "in_availableJoysticks", buf, CVAR_ROM );
	// The ROM cvar may already exist from an earlier enumeration. Refresh its
	// engine-owned value so UI code sees hotplug changes immediately.
	Cvar_Set( "in_availableJoysticks", buf );
	Cvar_SetDescription( cv, "List of available joysticks." );

	if( !in_joystick->integer ) {
		Com_DPrintf( "Joystick is not active.\n" );
		return;
	}

	in_joystickNo = Cvar_Get( "in_joystickNo", "0", CVAR_ARCHIVE );
	Cvar_SetDescription( in_joystickNo, "Select which joystick to use." );
	if( in_joystickNo->integer < 0 || in_joystickNo->integer >= total )
		Cvar_Set( "in_joystickNo", "0" );

	in_joystickUseAnalog = Cvar_Get(
		"in_joystickUseAnalog", "0", CVAR_ARCHIVE | CVAR_LATCH );
	Cvar_SetDescription( in_joystickUseAnalog,
		"Do not translate joystick axis events to keyboard commands. "
		"Changes take effect after an input restart." );

	if ( total <= 0 )
	{
		Com_DPrintf( "No joysticks found.\n" );
		return;
	}

	stickInstance = joysticks.get()[ in_joystickNo->integer ];
	stick = SDL_OpenJoystick( stickInstance );

	if (stick == NULL) {
		Com_DPrintf( "No joystick opened: %s\n", SDL_GetError() );
		return;
	}

	if (SDL_IsGamepad(stickInstance))
		gamepad = SDL_OpenGamepad(stickInstance);

	Com_DPrintf( "Joystick %d opened\n", in_joystickNo->integer );
	Com_DPrintf( "Name:       %s\n", SDL_GetJoystickNameForID(stickInstance) ? SDL_GetJoystickNameForID(stickInstance) : "Unknown joystick" );
	Com_DPrintf( "Axes:       %d\n", SDL_GetNumJoystickAxes(stick) );
	Com_DPrintf( "Hats:       %d\n", SDL_GetNumJoystickHats(stick) );
	Com_DPrintf( "Buttons:    %d\n", SDL_GetNumJoystickButtons(stick) );
	Com_DPrintf( "Balls:      %d\n", SDL_GetNumJoystickBalls(stick) );
	Com_DPrintf( "Use Analog: %s\n", in_joystickUseAnalog->integer ? "Yes" : "No" );
	Com_DPrintf( "Is gamepad: %s\n", gamepad ? "Yes" : "No" );
}


/*
===============
IN_ShutdownJoystick
===============
*/
static void IN_ShutdownJoystick( void )
{
	IN_CloseJoystickHandles();

	if ( s_gamepadSubsystemAcquired ) {
		SDL_QuitSubSystem( SDL_INIT_GAMEPAD );
		s_gamepadSubsystemAcquired = qfalse;
	}
	if ( s_joystickSubsystemAcquired ) {
		SDL_QuitSubSystem( SDL_INIT_JOYSTICK );
		s_joystickSubsystemAcquired = qfalse;
	}
}


static qboolean KeyToAxisAndSign(int keynum, int *outAxis, int *outSign)
{
	const char *bind;

	if (!keynum)
		return qfalse;

	bind = Key_GetBinding(keynum);

	if (!bind || *bind != '+')
		return qfalse;

	*outSign = 0;

	if (Q_stricmp(bind, "+forward") == 0)
	{
		*outAxis = j_forward_axis->integer;
		*outSign = j_forward->value > 0.0f ? 1 : -1;
	}
	else if (Q_stricmp(bind, "+back") == 0)
	{
		*outAxis = j_forward_axis->integer;
		*outSign = j_forward->value > 0.0f ? -1 : 1;
	}
	else if (Q_stricmp(bind, "+moveleft") == 0)
	{
		*outAxis = j_side_axis->integer;
		*outSign = j_side->value > 0.0f ? -1 : 1;
	}
	else if (Q_stricmp(bind, "+moveright") == 0)
	{
		*outAxis = j_side_axis->integer;
		*outSign = j_side->value > 0.0f ? 1 : -1;
	}
	else if (Q_stricmp(bind, "+lookup") == 0)
	{
		*outAxis = j_pitch_axis->integer;
		*outSign = j_pitch->value > 0.0f ? -1 : 1;
	}
	else if (Q_stricmp(bind, "+lookdown") == 0)
	{
		*outAxis = j_pitch_axis->integer;
		*outSign = j_pitch->value > 0.0f ? 1 : -1;
	}
	else if (Q_stricmp(bind, "+left") == 0)
	{
		*outAxis = j_yaw_axis->integer;
		*outSign = j_yaw->value > 0.0f ? 1 : -1;
	}
	else if (Q_stricmp(bind, "+right") == 0)
	{
		*outAxis = j_yaw_axis->integer;
		*outSign = j_yaw->value > 0.0f ? -1 : 1;
	}
	else if (Q_stricmp(bind, "+moveup") == 0)
	{
		*outAxis = j_up_axis->integer;
		*outSign = j_up->value > 0.0f ? 1 : -1;
	}
	else if (Q_stricmp(bind, "+movedown") == 0)
	{
		*outAxis = j_up_axis->integer;
		*outSign = j_up->value > 0.0f ? -1 : 1;
	}

	return *outSign != 0 ? qtrue : qfalse;
}


/*
===============
IN_GamepadMove
===============
*/
static void IN_GamepadMove( void )
{
	int i;
	std::array<int, MAX_JOYSTICK_AXIS> translatedAxes = {};
	std::array<int, SDL_GAMEPAD_AXIS_COUNT> physicalAxes = {};
	std::array<qboolean, SDL_GAMEPAD_AXIS_COUNT * 2>
		digitalDirections = {};
	static const int negMap[SDL_GAMEPAD_AXIS_COUNT] = {
		K_PAD0_LEFTSTICK_LEFT,
		K_PAD0_LEFTSTICK_UP,
		K_PAD0_RIGHTSTICK_LEFT,
		K_PAD0_RIGHTSTICK_UP,
		0,
		0
	};
	static const int posMap[SDL_GAMEPAD_AXIS_COUNT] = {
		K_PAD0_LEFTSTICK_RIGHT,
		K_PAD0_LEFTSTICK_DOWN,
		K_PAD0_RIGHTSTICK_RIGHT,
		K_PAD0_RIGHTSTICK_DOWN,
		K_PAD0_LEFTTRIGGER,
		K_PAD0_RIGHTTRIGGER
	};

	SDL_UpdateGamepads();

	// FnQL's ABI has distinct keys only through SDL3's touchpad button.
	for ( i = 0; i < kSupportedGamepadButtonCount; ++i )
	{
		const qboolean pressed = SDL_GetGamepadButton(
			gamepad, static_cast<SDL_GamepadButton>( i ) )
				? qtrue : qfalse;
		if ( pressed != stick_state.gamepadButtons[i] )
		{
			if ( i >= SDL_GAMEPAD_BUTTON_MISC1 ) {
				Com_QueueEvent(in_eventTime, SE_KEY, K_PAD0_MISC1 + i - SDL_GAMEPAD_BUTTON_MISC1, pressed, 0, NULL);
			} else
			{
				Com_QueueEvent(in_eventTime, SE_KEY, K_PAD0_SOUTH + i, pressed, 0, NULL);
			}
			stick_state.gamepadButtons[i] = pressed;
		}
	}

	for ( i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i ) {
		physicalAxes[i] = fnql::input::ApplyJoystickDeadzone(
			SDL_GetGamepadAxis( gamepad, static_cast<SDL_GamepadAxis>(
				SDL_GAMEPAD_AXIS_LEFTX + i ) ),
			in_joystickThreshold->value );
	}

	/*
	Recompute every translated output from the complete physical snapshot.
	Bindings may change while an axis is held, and multiple physical axes may
	target the same output; greatest magnitude gives deterministic aggregation.
	*/
	for ( i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i ) {
		const int axis = physicalAxes[i];
		const int negKey = negMap[i];
		const int posKey = posMap[i];
		qboolean posAnalog = qfalse;
		qboolean negAnalog = qfalse;

		if ( in_joystickUseAnalog->integer ) {
			int posAxis = 0;
			int posSign = 0;
			int negAxis = 0;
			int negSign = 0;

			posAnalog = KeyToAxisAndSign( posKey, &posAxis, &posSign );
			negAnalog = KeyToAxisAndSign( negKey, &negAxis, &negSign );
			if ( posAnalog && axis > 0 ) {
				translatedAxes[posAxis] = fnql::input::StrongerJoystickAxis(
					translatedAxes[posAxis], axis * posSign );
			}
			if ( negAnalog && axis < 0 ) {
				translatedAxes[negAxis] = fnql::input::StrongerJoystickAxis(
					translatedAxes[negAxis], -axis * negSign );
			}
		}

		// Store desired digital state rather than inferring it from oldAxis.
		// A live binding edit can reclassify a held direction between an analog
		// output and a key even though the physical value did not change.
		digitalDirections[i * 2] =
			!negAnalog && negKey && axis < 0 ? qtrue : qfalse;
		digitalDirections[i * 2 + 1] =
			!posAnalog && posKey && axis > 0 ? qtrue : qfalse;

		stick_state.oldaaxes[i] = axis;
	}

	// Balance every obsolete direction before pressing any replacement. This
	// gives sign crossings and live binding changes deterministic ordering.
	for ( i = 0; i < SDL_GAMEPAD_AXIS_COUNT * 2; ++i ) {
		const int axis = i / 2;
		const int key = ( i & 1 ) ? posMap[axis] : negMap[axis];
		if ( stick_state.gamepadDigitalDirections[i] &&
			!digitalDirections[i] && key ) {
			Com_QueueEvent(
				in_eventTime, SE_KEY, key, qfalse, 0, NULL );
		}
	}
	for ( i = 0; i < SDL_GAMEPAD_AXIS_COUNT * 2; ++i ) {
		const int axis = i / 2;
		const int key = ( i & 1 ) ? posMap[axis] : negMap[axis];
		if ( !stick_state.gamepadDigitalDirections[i] &&
			digitalDirections[i] && key ) {
			Com_QueueEvent(
				in_eventTime, SE_KEY, key, qtrue, 0, NULL );
		}
		stick_state.gamepadDigitalDirections[i] = digitalDirections[i];
	}

	if ( in_joystickUseAnalog->integer ) {
		for ( i = 0; i < MAX_JOYSTICK_AXIS; ++i ) {
			if ( translatedAxes[i] != stick_state.oldTranslatedAxes[i] ) {
				Com_QueueEvent( in_eventTime, SE_JOYSTICK_AXIS,
					i, translatedAxes[i], 0, NULL );
				stick_state.oldTranslatedAxes[i] = translatedAxes[i];
			}
		}
	}
}


static void IN_QueueHatKeys( Uint8 hat, int hatIndex, qboolean down )
{
	const int base = 4 * hatIndex;

	switch( hat )
	{
		case SDL_HAT_UP:
			Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[base + 0], down, 0, NULL );
			break;
		case SDL_HAT_RIGHT:
			Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[base + 1], down, 0, NULL );
			break;
		case SDL_HAT_DOWN:
			Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[base + 2], down, 0, NULL );
			break;
		case SDL_HAT_LEFT:
			Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[base + 3], down, 0, NULL );
			break;
		case SDL_HAT_RIGHTUP:
			Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[base + 0], down, 0, NULL );
			Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[base + 1], down, 0, NULL );
			break;
		case SDL_HAT_RIGHTDOWN:
			Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[base + 2], down, 0, NULL );
			Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[base + 1], down, 0, NULL );
			break;
		case SDL_HAT_LEFTUP:
			Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[base + 0], down, 0, NULL );
			Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[base + 3], down, 0, NULL );
			break;
		case SDL_HAT_LEFTDOWN:
			Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[base + 2], down, 0, NULL );
			Com_QueueEvent( in_eventTime, SE_KEY, hat_keys[base + 3], down, 0, NULL );
			break;
		default:
			break;
	}
}


static Uint8 IN_GetPackedHat( unsigned int hats, int index )
{
	return static_cast<Uint8>( ( hats >> ( index * 8 ) ) & 0xff );
}


/*
===============
IN_JoyMove
===============
*/
static void IN_JoyMove( void )
{
	unsigned int axes = 0;
	unsigned int hats = 0;
	int total = 0;
	int i = 0;

	in_eventTime = Sys_Milliseconds();

	if (gamepad)
	{
		IN_GamepadMove();
		return;
	}

	if (!stick)
		return;

	SDL_UpdateJoysticks();

	// update the ball state.
	total = SDL_GetNumJoystickBalls(stick);
	if (total > 0)
	{
		int balldx = 0;
		int balldy = 0;
		for (i = 0; i < total; i++)
		{
			int dx = 0;
			int dy = 0;
			SDL_GetJoystickBall(stick, i, &dx, &dy);
			balldx = fnql::input::SaturatingAddInt( balldx, dx );
			balldy = fnql::input::SaturatingAddInt( balldy, dy );
		}
		if (balldx || balldy)
		{
			// !!! FIXME: is this good for stick balls, or just mice?
			// Scale like the mouse input...
			if ( balldx < -1 || balldx > 1 ) {
				balldx = fnql::input::SaturatingIntFromInt64(
					static_cast<std::int64_t>( balldx ) * 2 );
			}
			if ( balldy < -1 || balldy > 1 ) {
				balldy = fnql::input::SaturatingIntFromInt64(
					static_cast<std::int64_t>( balldy ) * 2 );
			}
			Com_QueueEvent( in_eventTime, SE_MOUSE, balldx, balldy, 0, NULL );
		}
	}

	// now query the stick buttons...
	total = SDL_GetNumJoystickButtons(stick);
	if (total > 0)
	{
		if ( total > kRawJoystickButtonCount )
			total = kRawJoystickButtonCount;
		for (i = 0; i < total; i++)
		{
			qboolean pressed = (SDL_GetJoystickButton(stick, i) != 0) ? qtrue : qfalse;
			if (pressed != stick_state.rawButtons[i])
			{
				Com_QueueEvent( in_eventTime, SE_KEY, K_JOY1 + i, pressed, 0, NULL );
				stick_state.rawButtons[i] = pressed;
			}
		}
	}

	// look at the hats...
	total = SDL_GetNumJoystickHats(stick);
	if (total > 0)
	{
		if (total > 4) total = 4;
		for (i = 0; i < total; i++)
		{
			hats |= static_cast<unsigned int>( SDL_GetJoystickHat( stick, i ) ) << ( i * 8 );
		}
	}

	// update hat state
	if (hats != stick_state.oldhats)
	{
		for( i = 0; i < 4; i++ ) {
			const Uint8 previousHat = IN_GetPackedHat( stick_state.oldhats, i );
			const Uint8 currentHat = IN_GetPackedHat( hats, i );

			if( currentHat != previousHat ) {
				IN_QueueHatKeys( previousHat, i, qfalse );
				IN_QueueHatKeys( currentHat, i, qtrue );
			}
		}
	}

	// save hat state
	stick_state.oldhats = hats;

	// finally, look at the axes...
	total = SDL_GetNumJoystickAxes(stick);
	if (total > 0)
	{
		if (in_joystickUseAnalog->integer)
		{
			if (total > MAX_JOYSTICK_AXIS) total = MAX_JOYSTICK_AXIS;
			const float deadzone = fnql::input::FiniteJoystickDeadzone(
				in_joystickThreshold->value );
			for (i = 0; i < total; i++)
			{
				Sint16 axis = SDL_GetJoystickAxis(stick, i);
				const float f =
					static_cast<float>( axis < 0 ? -static_cast<int>( axis ) : axis ) /
					32767.0f;
				
				// At one the configured deadzone intentionally covers the
				// complete signed SDL range, including the asymmetric -32768
				// endpoint. Preserve legacy raw scaling at ordinary thresholds.
				if ( deadzone >= 1.0f || f < deadzone ) axis = 0;

				if ( axis != stick_state.oldaaxes[i] )
				{
					Com_QueueEvent( in_eventTime, SE_JOYSTICK_AXIS, i, axis, 0, NULL );
					stick_state.oldaaxes[i] = axis;
				}
			}
		}
		else
		{
			if ( total > kRawDigitalAxisCount )
				total = kRawDigitalAxisCount;
			const float deadzone = fnql::input::FiniteJoystickDeadzone(
				in_joystickThreshold->value );
			for (i = 0; i < total; i++)
			{
				if ( deadzone >= 1.0f ) {
					continue;
				}
				Sint16 axis = SDL_GetJoystickAxis(stick, i);
				float f = ( (float) axis ) / 32767.0f;
				if( f < -deadzone ) {
					axes |= ( 1u << ( i * 2 ) );
				} else if( f > deadzone ) {
					axes |= ( 1u << ( ( i * 2 ) + 1 ) );
				}
			}
		}
	}

	/* Time to update axes state based on old vs. new. */
	if (axes != stick_state.oldaxes)
	{
		for( i = 0; i < ARRAY_LEN( joy_keys ); i++ ) {
			if( ( axes & ( 1u << i ) ) && !( stick_state.oldaxes & ( 1u << i ) ) ) {
				Com_QueueEvent( in_eventTime, SE_KEY, joy_keys[i], qtrue, 0, NULL );
			}

			if( !( axes & ( 1u << i ) ) && ( stick_state.oldaxes & ( 1u << i ) ) ) {
				Com_QueueEvent( in_eventTime, SE_KEY, joy_keys[i], qfalse, 0, NULL );
			}
		}
	}

	/* Save for future generations. */
	stick_state.oldaxes = axes;
}
#endif  // USE_JOYSTICK



#ifdef DEBUG_EVENTS
static const char *eventName( Uint32 event )
{
	static char buf[32];

	switch ( event )
	{
		case SDL_EVENT_WINDOW_SHOWN: return "SHOWN";
		case SDL_EVENT_WINDOW_HIDDEN: return "HIDDEN";
		case SDL_EVENT_WINDOW_EXPOSED: return "EXPOSED";
		case SDL_EVENT_WINDOW_MOVED: return "MOVED";
		case SDL_EVENT_WINDOW_RESIZED: return "RESIZED";
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: return "SIZE_CHANGED";
		case SDL_EVENT_WINDOW_MINIMIZED: return "MINIMIZED";
		case SDL_EVENT_WINDOW_MAXIMIZED: return "MAXIMIZED";
		case SDL_EVENT_WINDOW_RESTORED: return "RESTORED";
		case SDL_EVENT_WINDOW_MOUSE_ENTER: return "ENTER";
		case SDL_EVENT_WINDOW_MOUSE_LEAVE: return "LEAVE";
		case SDL_EVENT_WINDOW_FOCUS_GAINED: return "FOCUS_GAINED";
		case SDL_EVENT_WINDOW_FOCUS_LOST: return "FOCUS_LOST";
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED: return "CLOSE";
		case SDL_EVENT_WINDOW_HIT_TEST: return "HIT_TEST"; 
		default:
			sprintf( buf, "EVENT#%u", (unsigned int)event );
			return buf;
	}
}
#endif

static constexpr SDL_Keymod kModeModifierFamily =
	static_cast<SDL_Keymod>( SDL_KMOD_MODE | SDL_KMOD_LEVEL5 );


static SDL_Keymod IN_ModifierBit(
	SDL_Scancode scancode, SDL_Keycode keycode )
{
	// Level-5 Shift is an SDL extended keycode and deliberately has no
	// SDL_SCANCODE_LEVEL5_SHIFT identity. Preserve it as a distinct physical
	// bit while presenting the same logical K_MODE family to the engine.
	if ( keycode == SDLK_LEVEL5_SHIFT ) {
		return SDL_KMOD_LEVEL5;
	}

	switch ( scancode ) {
		case SDL_SCANCODE_LCTRL:  return SDL_KMOD_LCTRL;
		case SDL_SCANCODE_RCTRL:  return SDL_KMOD_RCTRL;
		case SDL_SCANCODE_LSHIFT: return SDL_KMOD_LSHIFT;
		case SDL_SCANCODE_RSHIFT: return SDL_KMOD_RSHIFT;
		case SDL_SCANCODE_LALT:   return SDL_KMOD_LALT;
		case SDL_SCANCODE_RALT:   return SDL_KMOD_RALT;
		case SDL_SCANCODE_LGUI:   return SDL_KMOD_LGUI;
		case SDL_SCANCODE_RGUI:   return SDL_KMOD_RGUI;
		case SDL_SCANCODE_MODE:   return SDL_KMOD_MODE;
		default:                  return SDL_KMOD_NONE;
	}
}


static SDL_Keymod IN_ModifierFamily( SDL_Keymod bit )
{
	if ( bit & SDL_KMOD_CTRL ) {
		return SDL_KMOD_CTRL;
	}
	if ( bit & SDL_KMOD_SHIFT ) {
		return SDL_KMOD_SHIFT;
	}
	if ( bit & SDL_KMOD_ALT ) {
		return SDL_KMOD_ALT;
	}
	if ( bit & SDL_KMOD_GUI ) {
		return SDL_KMOD_GUI;
	}
	if ( bit & kModeModifierFamily ) {
		return kModeModifierFamily;
	}
	return SDL_KMOD_NONE;
}


static qboolean IN_ShouldQueueModifierTransition(
	SDL_Scancode scancode, SDL_Keycode keycode, qboolean down )
{
	const SDL_Keymod bit = IN_ModifierBit( scancode, keycode );
	if ( bit == SDL_KMOD_NONE ) {
		return qtrue;
	}

	const SDL_Keymod family = IN_ModifierFamily( bit );
	const qboolean sideWasDown =
		( s_physicalModifiers & bit ) ? qtrue : qfalse;
	const qboolean familyWasDown =
		( s_physicalModifiers & family ) ? qtrue : qfalse;
	if ( down ) {
		s_physicalModifiers =
			static_cast<SDL_Keymod>( s_physicalModifiers | bit );
		// Preserve key-repeat behavior for the same physical side, while the
		// second side of a modifier family must not create another logical down.
		return ( !familyWasDown || sideWasDown ) ? qtrue : qfalse;
	} else {
		s_physicalModifiers =
			static_cast<SDL_Keymod>( s_physicalModifiers & ~bit );
	}
	const qboolean familyIsDown =
		( s_physicalModifiers & family ) ? qtrue : qfalse;
	// An unmatched release is useful recovery unless the other physical side
	// is known to remain held. CL_KeyEvent safely ignores duplicate releases.
	return familyIsDown ? qfalse : qtrue;
}


static void IN_QueueHeldModifiers( int eventTime )
{
	s_physicalModifiers = SDL_GetModState();
	if ( s_physicalModifiers & SDL_KMOD_CTRL ) {
		Com_QueueEvent( eventTime, SE_KEY, K_CTRL, qtrue, 0, NULL );
	}
	if ( s_physicalModifiers & SDL_KMOD_SHIFT ) {
		Com_QueueEvent( eventTime, SE_KEY, K_SHIFT, qtrue, 0, NULL );
	}
	if ( s_physicalModifiers & SDL_KMOD_ALT ) {
		Com_QueueEvent( eventTime, SE_KEY, K_ALT, qtrue, 0, NULL );
	}
	if ( s_physicalModifiers & kModeModifierFamily ) {
		Com_QueueEvent( eventTime, SE_KEY, K_MODE, qtrue, 0, NULL );
	}
#ifdef MACOS_X
	if ( s_physicalModifiers & SDL_KMOD_GUI ) {
		Com_QueueEvent( eventTime, SE_KEY, K_COMMAND, qtrue, 0, NULL );
	}
#else
	if ( s_physicalModifiers & SDL_KMOD_GUI ) {
		Com_QueueEvent( eventTime, SE_KEY, K_SUPER, qtrue, 0, NULL );
	}
#endif
}


void IN_QueueInputReset( qboolean rebuildModifiers )
{
	const int eventTime = Sys_Milliseconds();

	// Reset producer caches before placing the ordered client barrier. SDL may
	// continue draining native events afterward; those newer transitions then
	// rebuild the caches and must not be erased when the barrier is consumed.
	IN_ResetInputState();
	Com_QueueEvent( eventTime, SE_INPUT_RESET, 0, 0, 0, NULL );
	s_mouseAuxButtonState = 0;
	if ( rebuildModifiers ) {
		IN_QueueHeldModifiers( eventTime );
	} else {
		s_physicalModifiers = SDL_KMOD_NONE;
	}
}


static int IN_EventTime( Uint64 timestamp )
{
	const int now = Sys_Milliseconds();
	const Uint64 nowNanoseconds = SDL_GetTicksNS();

	if ( timestamp && timestamp <= nowNanoseconds ) {
		const Uint64 elapsedMilliseconds =
			( nowNanoseconds - timestamp ) / SDL_NS_PER_MS;
		const std::uint32_t elapsed = static_cast<std::uint32_t>(
			( std::min )( elapsedMilliseconds,
				static_cast<Uint64>(
					( std::numeric_limits<std::uint32_t>::max )() ) ) );
		const std::uint32_t candidate =
			static_cast<std::uint32_t>( now ) - elapsed;
		if ( candidate <= static_cast<std::uint32_t>(
				( std::numeric_limits<int>::max )() ) ) {
			return static_cast<int>( candidate );
		}
		return static_cast<int>(
			static_cast<std::int64_t>( candidate ) -
			( static_cast<std::int64_t>( 1 ) << 32 ) );
	}
	return now;
}

static sdlKeyInfo_t IN_MakeKeyInfo( const SDL_KeyboardEvent *event )
{
	sdlKeyInfo_t keyinfo;

	keyinfo.scancode = event->scancode;
	keyinfo.sym = event->key;
	keyinfo.mod = event->mod;

	return keyinfo;
}

static void IN_RefreshDrawableIfChanged( int oldPixelWidth, int oldPixelHeight )
{
	if ( glw_state.isFullscreen || gw_minimized ||
		glw_state.pixel_width < 4 || glw_state.pixel_height < 4 ||
		( oldPixelWidth == glw_state.pixel_width &&
		oldPixelHeight == glw_state.pixel_height ) ) {
		return;
	}

	// A drag can end at the renderer's existing size. Cancel the intermediate
	// request instead of rebuilding for dimensions that are no longer current.
	if ( glw_state.config &&
		glw_state.pixel_width == glw_state.config->vidWidth &&
		glw_state.pixel_height == glw_state.config->vidHeight ) {
		CL_CancelWindowResize();
		return;
	}

	CL_NotifyWindowResize( glw_state.window_width,
		glw_state.window_height, qtrue );
}

static void IN_UpdateWindowGeometry( qboolean savePosition, qboolean notifyResize )
{
	const int oldPixelWidth = glw_state.pixel_width;
	const int oldPixelHeight = glw_state.pixel_height;
	const SDL_WindowFlags flags = SDL_GetWindowFlags( SDL_window );
	int x, y;

	GLW_UpdateWindowState();

	if ( savePosition && !gw_minimized && !glw_state.isFullscreen &&
		!( flags & SDL_WINDOW_MAXIMIZED ) &&
		SDL_GetWindowPosition( SDL_window, &x, &y ) ) {
		Cvar_SetIntegerValue( "vid_xpos", x );
		Cvar_SetIntegerValue( "vid_ypos", y );
	}

	if ( notifyResize ) {
		IN_RefreshDrawableIfChanged( oldPixelWidth, oldPixelHeight );
	}
}

static void IN_HandleDisplayEvent( void )
{
	const int oldPixelWidth = glw_state.pixel_width;
	const int oldPixelHeight = glw_state.pixel_height;

	GLW_UpdateWindowState();
	if ( !glw_state.isFullscreen ) {
		// Recover from monitor removal, taskbar/dock changes, and display
		// rearrangement without allowing decorations to become unreachable.
		GLW_EnsureWindowOnScreen();
		GLW_UpdateWindowState();
		IN_RefreshDrawableIfChanged( oldPixelWidth, oldPixelHeight );
	}
}

static void IN_HandleWindowEvent( Uint32 type, const SDL_WindowEvent *window, int *lastKeyDown )
{
#ifdef DEBUG_EVENTS
	Com_Printf( "%4i %s\n", window->timestamp, eventName( type ) );
#else
	(void)window;
#endif

	switch ( type )
	{
		case SDL_EVENT_WINDOW_MOVED:
			IN_UpdateWindowGeometry( qtrue, qtrue );
			break;

		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			IN_UpdateWindowGeometry( qfalse, qtrue );
			break;

		case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
			IN_UpdateWindowGeometry( qfalse, qtrue );
			if ( !glw_state.isFullscreen ) {
				// Decoration extents can change with display scale even when the
				// client size does not. Recheck the complete frame against the
				// destination display's usable area.
				GLW_EnsureWindowOnScreen();
				GLW_UpdateWindowState();
			}
			break;

		case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
			IN_UpdateWindowGeometry( qfalse, qtrue );
			if ( gw_active && re.SetColorMappings ) {
				re.SetColorMappings();
			}
			// the display swap reloaded the LUT from its profile
			GLW_ReapplyGamma();
			break;

		case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
		case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
			IN_UpdateWindowGeometry( qfalse, qfalse );
			if ( gw_active && re.SetColorMappings ) {
				re.SetColorMappings();
			}
			GLW_ReapplyGamma();
			break;

		case SDL_EVENT_WINDOW_HIDDEN:
		case SDL_EVENT_WINDOW_MINIMIZED:
			*lastKeyDown = 0;
			IN_QueueInputReset( qfalse );
			gw_active = qfalse;
			gw_minimized = qtrue;
			CL_WebHost_NotifyAppActivation( qfalse );
			mouse_focus = qfalse;
			s_absPointerOutside = qfalse;
			s_fullscreenOcclusionReset = qfalse;
			GLW_RestoreGamma();
			break;

		case SDL_EVENT_WINDOW_OCCLUDED:
			if ( glw_state.isFullscreen ) {
				*lastKeyDown = 0;
				IN_QueueInputReset( qfalse );
				gw_minimized = qtrue;
				mouse_focus = qfalse;
				s_absPointerOutside = qfalse;
				s_fullscreenOcclusionReset = qtrue;
			}
			break;

		case SDL_EVENT_WINDOW_EXPOSED:
			if ( glw_state.isFullscreen && s_fullscreenOcclusionReset &&
				gw_active ) {
				// OCCLUDED is sometimes a synthetic minimize without a focus
				// transition. Rebuild physical modifiers behind its reset and
				// restore fullscreen mouse focus without duplicating FOCUS_GAINED.
				gw_minimized = qfalse;
				mouse_focus =
					( SDL_GetMouseFocus() == SDL_window ||
						glw_state.isFullscreen ) ? qtrue : qfalse;
				IN_QueueHeldModifiers( in_eventTime );
				s_fullscreenOcclusionReset = qfalse;
			}
			[[fallthrough]];
		case SDL_EVENT_WINDOW_SHOWN:
		case SDL_EVENT_WINDOW_RESTORED:
		case SDL_EVENT_WINDOW_MAXIMIZED:
			if ( gw_active || !glw_state.isFullscreen ) {
				gw_minimized = qfalse;
			}
			IN_UpdateWindowGeometry( qfalse, qtrue );
			break;

		case SDL_EVENT_WINDOW_FOCUS_LOST:
			*lastKeyDown = 0;
			IN_QueueInputReset( qfalse );
			gw_active = qfalse;
			if ( glw_state.isFullscreen ) {
				gw_minimized = qtrue;
			}
			CL_WebHost_NotifyAppActivation( qfalse );
			mouse_focus = qfalse;
			s_absPointerOutside = qfalse;
			s_fullscreenOcclusionReset = qfalse;
			// a device gamma ramp is desktop-global: give the user their own
			// calibration back for as long as we are in the background
			GLW_RestoreGamma();
			break;

		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			*lastKeyDown = 0;
			IN_QueueInputReset( qtrue );
			gw_active = qtrue;
			gw_minimized = qfalse;
			CL_WebHost_NotifyAppActivation( qtrue );
			mouse_focus =
				( SDL_GetMouseFocus() == SDL_window ||
					glw_state.isFullscreen ) ? qtrue : qfalse;
			s_fullscreenOcclusionReset = qfalse;
			IN_UpdateWindowGeometry( qfalse, qtrue );
			if ( re.SetColorMappings ) {
				re.SetColorMappings();
			}
			break;

		case SDL_EVENT_WINDOW_MOUSE_ENTER:
			s_absPointerOutside = qfalse;
			mouse_focus = qtrue;
			break;

		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			if ( s_absCaptureActive && s_absCaptureButtons ) {
				// A verified SDL capture deliberately keeps an absolute drag
				// alive outside the window until its matching button-up.
				s_absPointerOutside = qtrue;
				break;
			}
			s_absPointerOutside = qfalse;
			if ( s_absCaptureButtons || s_absCaptureActive ) {
				// A requested capture that never became active cannot promise
				// an outside release. Balance its logical buttons now rather
				// than polling a different SDL window after this drain.
				IN_QueueMouseReset();
				IN_EndTemporaryMouseCapture();
				mouse_focus = qfalse;
				break;
			}
			if ( glw_state.isFullscreen ||
				( s_pointerModeValid &&
					( s_pointerMode.relativeMotion ||
						s_pointerMode.confineToWindow ) ) ) {
				// A failed/externally-broken grab can still let the pointer
				// leave a driven pointer mode without a keyboard focus change.
				// Release only mouse state before gating the eventual button-up;
				// free windowed overlays deliberately do not take this path.
				IN_QueueMouseReset();
				s_relativeRemainderX = 0.0f;
				s_relativeRemainderY = 0.0f;
				IN_ResetWheelAccumulator();
				IN_EndTemporaryMouseCapture();
				mouse_focus = qfalse;
				s_pointerModeValid = qfalse;
			}
			// Free windowed overlays receive a normal leave without a grab.
			// Stop absolute polling until this window gets MOUSE_ENTER again.
			mouse_focus = qfalse;
			break;

		default:
			break;
	}
}


static void IN_QueueTextInput(
	const char *text, qboolean suppressRepeatedConsoleKey )
{
	const unsigned char *bytes =
		reinterpret_cast<const unsigned char *>( text );
	std::size_t remaining = std::strlen( text );

	while ( remaining > 0 )
	{
		const fnql::input::Utf8DecodeResult decoded =
			fnql::input::DecodeUtf8( bytes, remaining );
		bytes += decoded.size;
		remaining -= decoded.size;

		if ( !decoded.valid || decoded.codepoint == 0 ) {
			Com_DPrintf( "Ignoring malformed SDL UTF-8 text input\n" );
			continue;
		}

		const int utf32 = static_cast<int>( decoded.codepoint );
		if ( IN_IsConsoleKey( 0, utf32 ) )
			{
				if ( !suppressRepeatedConsoleKey ) {
					Com_QueueEvent(
						in_eventTime, SE_KEY, K_CONSOLE, qtrue, 0, NULL );
					Com_QueueEvent(
						in_eventTime, SE_KEY, K_CONSOLE, qfalse, 0, NULL );
				}
			}
			else
			{
				Com_QueueEvent( in_eventTime, SE_CHAR, utf32, 0, 0, NULL );
			}
	}
}


static SDL_WindowID IN_EventWindowID( const SDL_Event& event )
{
	if ( event.type >= SDL_EVENT_WINDOW_FIRST &&
		event.type <= SDL_EVENT_WINDOW_LAST ) {
		return event.window.windowID;
	}

	switch ( event.type ) {
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP:
			return event.key.windowID;
		case SDL_EVENT_TEXT_EDITING:
			return event.edit.windowID;
		case SDL_EVENT_TEXT_EDITING_CANDIDATES:
			return event.edit_candidates.windowID;
		case SDL_EVENT_TEXT_INPUT:
			return event.text.windowID;
		case SDL_EVENT_MOUSE_MOTION:
			return event.motion.windowID;
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP:
			return event.button.windowID;
		case SDL_EVENT_MOUSE_WHEEL:
			return event.wheel.windowID;
		default:
			return 0;
	}
}


/*
===============
HandleEvents
===============
*/
//static void IN_ProcessEvents( void )
void HandleEvents( void )
{
	SDL_Event e;
	int key = 0;
#ifdef USE_JOYSTICK
	struct sdlTopologyTransition_t {
		SDL_JoystickID device;
		int kind;
	};
	std::array<sdlTopologyTransition_t, 32> topologyTransitions{};
	std::size_t topologyTransitionCount = 0;
#endif

	if ( !SDL_WasInit( SDL_INIT_VIDEO ) )
		return;

	// Com_EventLoop can consume a catcher-changing key and re-enter this native
	// drain before IN_Frame. Reconcile here as well so gameplay never inherits
	// an IME-enabled window, and a newly opened text owner is ready immediately.
	IN_ReconcileTextInput();

	const SDL_WindowID currentWindowID =
		SDL_window ? SDL_GetWindowID( SDL_window ) : 0;
	in_eventTime = Sys_Milliseconds();

	while ( SDL_PollEvent( &e ) )
	{
		in_eventTime = IN_EventTime( e.common.timestamp );
#ifndef _WIN32
		if ( Sys_ConsoleHandleEvent( &e ) ) {
			continue;
		}
#endif
		const SDL_WindowID eventWindowID = IN_EventWindowID( e );
		if ( eventWindowID && eventWindowID != currentWindowID ) {
			// Video/input restarts deliberately drain SDL's retained queue. Do
			// not let focus, text, or mouse transitions from a destroyed window
			// mutate the replacement window; device/display/quit events have no
			// window target and remain global. The optional SDL system-console
			// window gets first refusal above because its distinct ID is live.
			continue;
		}

		if ( e.type != SDL_EVENT_KEY_DOWN &&
			e.type != SDL_EVENT_KEY_UP &&
			e.type != SDL_EVENT_TEXT_EDITING &&
			e.type != SDL_EVENT_TEXT_EDITING_CANDIDATES &&
			e.type != SDL_EVENT_TEXT_INPUT ) {
			s_lastKeyDown = 0;
			s_lastKeyDownWasRepeat = qfalse;
		}

		switch( e.type )
		{
			case SDL_EVENT_KEY_DOWN:
			{
				if ( !gw_active || gw_minimized ) {
					break;
				}

				sdlKeyInfo_t keyinfo = IN_MakeKeyInfo( &e.key );
				key = IN_TranslateSDLToQ3Key( &keyinfo, qtrue );
				s_lastKeyDownWasRepeat =
					e.key.repeat ? qtrue : qfalse;

				if ( e.key.repeat ) {
					// Console/Escape are one-shot state transitions. Editable
					// console and menu keys may still repeat under a real catcher.
					if ( key == K_CONSOLE ) {
						// Con_ToggleConsole_f clears logical keys; retain the
						// physical producer marker so paired text cannot toggle.
						s_lastKeyDown = K_CONSOLE;
						break;
					}
					if ( key == K_ESCAPE ||
						( !fnql::input::CatcherBlocksGameplayInput(
							Key_GetCatcher(), KEYCATCH_RETAIL_MOUSEPASS ) &&
							cls.state != CA_DISCONNECTED ) ) {
						break;
					}
				}

				if ( key == K_ENTER && ( keyinfo.mod & SDL_KMOD_ALT ) ) {
					if ( !e.key.repeat ) {
						Cvar_SetIntegerValue( "r_fullscreen",
							glw_state.isFullscreen ? 0 : 1 );
						// fast restart keeps the window alive so the
						// fullscreen state can be toggled in place
						Cbuf_AddText( "vid_restart fast\n" );
					}
					break;
				}

				if ( key && IN_ShouldQueueModifierTransition(
					keyinfo.scancode, keyinfo.sym, qtrue ) ) {
					Com_QueueEvent( in_eventTime, SE_KEY, key, qtrue, 0, NULL );

					if ( key == K_BACKSPACE )
						Com_QueueEvent( in_eventTime, SE_CHAR, CTRL('h'), 0, 0, NULL );
					else if ( key == K_ESCAPE )
						Com_QueueEvent( in_eventTime, SE_CHAR, key, 0, 0, NULL );
					else if( ( keyinfo.mod & SDL_KMOD_CTRL ) &&
						!( keyinfo.mod & ( SDL_KMOD_ALT |
							kModeModifierFamily ) ) &&
						key >= 'a' && key <= 'z' )
						Com_QueueEvent( in_eventTime, SE_CHAR, CTRL(key), 0, 0, NULL );
#ifdef MACOS_X
					else if( ( keyinfo.mod & SDL_KMOD_GUI ) && key == 'v' )
						Com_QueueEvent( in_eventTime, SE_CHAR, CTRL(key), 0, 0, NULL );
#endif
				}

				s_lastKeyDown = key;
				break;
			}

			case SDL_EVENT_KEY_UP:
			{
				if ( !gw_active || gw_minimized ) {
					break;
				}

				sdlKeyInfo_t keyinfo = IN_MakeKeyInfo( &e.key );

				if( ( key = IN_TranslateSDLToQ3Key( &keyinfo, qfalse ) ) &&
					IN_ShouldQueueModifierTransition(
						keyinfo.scancode, keyinfo.sym, qfalse ) )
					Com_QueueEvent( in_eventTime, SE_KEY, key, qfalse, 0, NULL );

				s_lastKeyDown = 0;
				s_lastKeyDownWasRepeat = qfalse;
				break;
			}

			case SDL_EVENT_TEXT_EDITING:
			case SDL_EVENT_TEXT_EDITING_CANDIDATES:
				// SDL owns the platform composition UI. Preserve the physical
				// producer marker until the committed TEXT_INPUT transaction.
				break;

			case SDL_EVENT_TEXT_INPUT:
				if ( gw_active && !gw_minimized && s_textInputActive ) {
					if ( s_lastKeyDown != K_CONSOLE ) {
						IN_QueueTextInput(
							e.text.text, s_lastKeyDownWasRepeat );
					}
				}
				// Only the immediately paired text event inherits either marker.
				// IME or programmatic text arriving later remains ordinary input,
				// even if focus changed while this event was retained.
				s_lastKeyDown = 0;
				s_lastKeyDownWasRepeat = qfalse;
				break;

			case SDL_EVENT_MOUSE_MOTION:
			{
				if ( !gw_active || gw_minimized || !mouse_focus ) {
					break;
				}

				// Resolve the producer-visible owner per event. Catcher changes
				// queued earlier in this SDL drain are enforced again when the
				// common event is consumed.
				const PointerOwner owner = IN_ResolvePointerOwner();

				if ( fnql::input::PointerOwnerReportsAbsolute( owner ) ) {
					// Preserve OS event order so a click is hit-tested at the
					// position from the immediately preceding motion event.
					IN_QueueAbsolutePointerPosition( owner,
						e.motion.x, e.motion.y, in_eventTime );
				} else if ( s_pointerModeValid &&
					s_pointerMode.driveInput &&
					s_pointerMode.relativeMotion &&
					( e.motion.xrel || e.motion.yrel ) ) {
					const float combinedX = fnql::input::FiniteOr(
						fnql::input::FiniteOr( e.motion.xrel, 0.0f ) +
						s_relativeRemainderX, 0.0f );
					const float combinedY = fnql::input::FiniteOr(
						fnql::input::FiniteOr( e.motion.yrel, 0.0f ) +
						s_relativeRemainderY, 0.0f );
					const int dx = fnql::input::TruncateFiniteFloatToInt( combinedX );
					const int dy = fnql::input::TruncateFiniteFloatToInt( combinedY );
					s_relativeRemainderX =
						dx == ( std::numeric_limits<int>::max )() ||
						dx == ( std::numeric_limits<int>::min )()
							? 0.0f : combinedX - static_cast<float>( dx );
					s_relativeRemainderY =
						dy == ( std::numeric_limits<int>::max )() ||
						dy == ( std::numeric_limits<int>::min )()
							? 0.0f : combinedY - static_cast<float>( dy );
					if ( dx || dy ) {
						Com_QueueEvent( in_eventTime, SE_MOUSE, dx, dy, 0, NULL );
					}
				}
				break;
			}

			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
				{
					if ( !gw_active || gw_minimized || !mouse_focus ) {
						break;
					}

					int b = 0;
					switch( e.button.button )
					{
						case SDL_BUTTON_LEFT:   b = K_MOUSE1; break;
						case SDL_BUTTON_MIDDLE: b = K_MOUSE3; break;
						case SDL_BUTTON_RIGHT:  b = K_MOUSE2; break;
						case SDL_BUTTON_X1:     b = K_MOUSE4; break;
						case SDL_BUTTON_X2:     b = K_MOUSE5; break;
						default:
							if ( e.button.button >= SDL_BUTTON_X2 + 1 &&
								e.button.button <= SDL_BUTTON_X2 + 4 ) {
								b = K_MOUSE6 +
									( e.button.button - ( SDL_BUTTON_X2 + 1 ) );
							} else if ( e.button.button >= SDL_BUTTON_X2 + 5 &&
								e.button.button <= SDL_BUTTON_X2 + 20 ) {
								b = K_AUX1 +
									( e.button.button - ( SDL_BUTTON_X2 + 5 ) );
							}
							break;
					}

					// The key namespace can represent buttons 1 through 25
					// uniquely. Ignore invalid or higher SDL IDs instead of
					// aliasing two physical buttons onto one logical key.
					if ( !b ) {
						if ( e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ) {
							Com_DPrintf( "Ignoring unsupported SDL mouse button %u\n",
								static_cast<unsigned int>( e.button.button ) );
						}
						break;
					}

					const PointerOwner owner = IN_ResolvePointerOwner();
					if ( owner == PointerOwner::Gameplay &&
						in_mouse->integer == 0 ) {
						break;
					}

					// Queue the click location even if gameplay still appears to
					// own the pointer. An earlier Escape in this same SDL drain
					// may open an absolute UI before this button is dispatched.
					IN_QueueAbsolutePointerPosition( owner,
						e.button.x, e.button.y, in_eventTime, qtrue );

					if ( fnql::input::PointerOwnerReportsAbsolute( owner ) ) {
						const Uint32 buttonMask = ( e.button.button > 0 && e.button.button <= 32 ) ?
							( (Uint32)1u << ( e.button.button - 1 ) ) : 0;

						// Every absolute owner holds the pointer for the duration of
						// a press, matching the native Win32 and X11 backends, so a
						// drag that leaves the window still delivers its release.
						if ( e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && buttonMask ) {
							s_absCaptureButtons |= buttonMask;
							IN_UpdateTemporaryMouseCapture();
						} else if ( buttonMask ) {
							s_absCaptureButtons &= ~buttonMask;
							IN_UpdateTemporaryMouseCapture();
							IN_FinishOutsideCaptureRelease();
						}
					}
					if ( b >= K_AUX1 && b < K_AUX1 + 16 ) {
						const unsigned int mask =
							1u << static_cast<unsigned int>( b - K_AUX1 );
						if ( e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ) {
							s_mouseAuxButtonState |= mask;
						} else {
							s_mouseAuxButtonState &= ~mask;
						}
					}
					Com_QueueEvent( in_eventTime, SE_KEY, b,
						( e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? qtrue : qfalse ), 0, NULL );
				}
				break;

			case SDL_EVENT_MOUSE_WHEEL:
				{
					if ( !gw_active || gw_minimized || !mouse_focus ) {
						break;
					}

					const PointerOwner owner = IN_ResolvePointerOwner();
					if ( owner == PointerOwner::Gameplay &&
						in_mouse->integer == 0 ) {
						break;
					}
					const int consumer = IN_PointerConsumerIdentity();
					float& wheelRemainder =
						IN_WheelRemainder( e.wheel.which, consumer );

					float wheelY = fnql::input::FiniteOr( e.wheel.y, 0.0f );
					if ( e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ) {
						wheelY = -wheelY;
					}
					const float combinedY = fnql::input::FiniteOr(
						wheelY + wheelRemainder, 0.0f );
					int steps =
						fnql::input::TruncateFiniteFloatToInt( combinedY );
					wheelRemainder =
						steps == ( std::numeric_limits<int>::max )() ||
						steps == ( std::numeric_limits<int>::min )()
							? 0.0f : combinedY - static_cast<float>( steps );
					steps = std::clamp( steps, -32, 32 );

					// Ownership may change when an earlier Escape from this same
					// drain is dispatched, so preserve wheel hit-test position
					// without polluting the producer's dedup cache.
					IN_QueueAbsolutePointerPosition( owner,
						e.wheel.mouse_x, e.wheel.mouse_y,
						in_eventTime, qtrue );
					const int wheelKey = steps > 0 ? K_MWHEELUP : K_MWHEELDOWN;
					for ( int i = 0; i < std::abs( steps ); ++i ) {
						Com_QueueEvent( in_eventTime, SE_KEY, wheelKey, qtrue, 0, NULL );
						Com_QueueEvent( in_eventTime, SE_KEY, wheelKey, qfalse, 0, NULL );
					}
				}
				break;

#ifdef USE_JOYSTICK
			case SDL_EVENT_JOYSTICK_ADDED:
			case SDL_EVENT_JOYSTICK_REMOVED:
			case SDL_EVENT_GAMEPAD_ADDED:
			case SDL_EVENT_GAMEPAD_REMOVED:
			case SDL_EVENT_GAMEPAD_REMAPPED:
			{
				const qboolean joystickEvent =
					( e.type == SDL_EVENT_JOYSTICK_ADDED ||
						e.type == SDL_EVENT_JOYSTICK_REMOVED )
						? qtrue : qfalse;
				const SDL_JoystickID device = joystickEvent
					? e.jdevice.which : e.gdevice.which;
				const int kind =
					( e.type == SDL_EVENT_JOYSTICK_ADDED ||
						e.type == SDL_EVENT_GAMEPAD_ADDED ) ? 1
					: ( e.type == SDL_EVENT_GAMEPAD_REMAPPED ? 2 : 0 );
				qboolean duplicate = qfalse;

				// SDL deliberately emits both joystick and gamepad topology
				// events for a recognized gamepad. Re-enumerate once for the
				// same device and logical transition, while still preserving an
				// add followed by a real remove in this drain.
				for ( std::size_t i = 0; i < topologyTransitionCount; ++i ) {
					if ( topologyTransitions[i].device == device &&
						topologyTransitions[i].kind == kind ) {
						duplicate = qtrue;
						break;
					}
				}
				if ( duplicate ) {
					break;
				}
				if ( topologyTransitionCount < topologyTransitions.size() ) {
					topologyTransitions[topologyTransitionCount++] =
						{ device, kind };
				}

				if ( in_joystick ) {
					if ( in_joystick->integer ) {
						// Balance held buttons, hats, digital directions, and
						// analog axes before IN_InitJoystick clears its producer
						// snapshot.
						IN_QueueInputReset(
							gw_active && !gw_minimized ? qtrue : qfalse );
					}
					// Keep the UI-facing device list current even while runtime
					// joystick input is disabled.
					IN_InitJoystick();
				}
				break;
			}
#endif

			case SDL_EVENT_KEYBOARD_REMOVED:
				// SDL's aggregate state has already dropped the device. Put an
				// ordered release behind its retained transitions, then rebuild
				// modifier families still held on another keyboard.
				IN_QueueInputReset(
					gw_active && !gw_minimized ? qtrue : qfalse );
				break;

			case SDL_EVENT_MOUSE_REMOVED:
				// A removed device cannot deliver its final button-up or motion
				// sample. Recover only mouse state so held keyboard/joystick
				// input from other devices remains intact.
				IN_QueueMouseReset();
				s_relativeRemainderX = 0.0f;
				s_relativeRemainderY = 0.0f;
				IN_ResetWheelAccumulator();
				s_absHaveLast = qfalse;
				IN_EndTemporaryMouseCapture();
				break;

			case SDL_EVENT_QUIT:
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				Cbuf_ExecuteText( EXEC_NOW, "quit Closed window\n" );
				break;

			case SDL_EVENT_DISPLAY_ORIENTATION:
			case SDL_EVENT_DISPLAY_ADDED:
			case SDL_EVENT_DISPLAY_REMOVED:
			case SDL_EVENT_DISPLAY_MOVED:
			case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
			case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
			case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
			case SDL_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED:
				IN_HandleDisplayEvent();
				break;

			case SDL_EVENT_WINDOW_MOVED:
			case SDL_EVENT_WINDOW_HIDDEN:
			case SDL_EVENT_WINDOW_MINIMIZED:
			case SDL_EVENT_WINDOW_OCCLUDED:
			case SDL_EVENT_WINDOW_EXPOSED:
			case SDL_EVENT_WINDOW_SHOWN:
			case SDL_EVENT_WINDOW_RESTORED:
			case SDL_EVENT_WINDOW_MAXIMIZED:
			case SDL_EVENT_WINDOW_FOCUS_LOST:
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
			case SDL_EVENT_WINDOW_MOUSE_ENTER:
			case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			case SDL_EVENT_WINDOW_RESIZED:
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
			case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
			case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
			case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
			case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
				IN_HandleWindowEvent( e.type, &e.window, &s_lastKeyDown );
				IN_ReconcileTextInput();
				break;

			default:
				break;
		}
	}

	// With the SDL queue drained, sync the absolute owner's cursor to the OS
	// pointer. Sys_SendKeyEvents runs whenever the event queue empties, so an
	// overlay opened under a stationary pointer gets its first position without
	// waiting for the next IN_Frame.
	if ( gw_active && !gw_minimized && mouse_focus &&
		s_pointerMode.driveInput ) {
		const PointerOwner owner = IN_ResolvePointerOwner();

		if ( fnql::input::PointerOwnerReportsAbsolute( owner ) ) {
			IN_PollAbsolutePointerPosition( owner );
		}
	}

#ifndef _WIN32
	Sys_ConsoleFrame();
#endif
}


/*
===============
IN_Minimize

Minimize the game so that user is back at the desktop
===============
*/
static void IN_Minimize( void )
{
	SDL_MinimizeWindow( SDL_window );
}


/*
===============
IN_Frame
===============
*/
void IN_Frame( void )
{
	IN_ReconcileTextInput();

#ifdef USE_JOYSTICK
	if ( gw_active && !gw_minimized ) {
		IN_JoyMove();
	}
#endif

	IN_ApplyPointerMode();

	if ( s_pointerMode.driveInput && s_pointerMode.reportAbsolute ) {
		IN_PollAbsolutePointerPosition( s_pointerOwner );
	}

	//IN_ProcessEvents();
	//HandleEvents();

	// Set event time for next frame to earliest possible time an event could happen
	//in_eventTime = Sys_Milliseconds();
}


/*
===============
IN_Restart
===============
*/
static void IN_Restart( void )
{
	IN_Shutdown();
	IN_Init();

	// Consume transitions retained by SDL while the backend was being rebuilt,
	// then put the recovery barrier and current physical modifiers behind them.
	// This is the same ordering used for a video restart.
	HandleEvents();
	IN_QueueInputReset(
		gw_active && !gw_minimized ? qtrue : qfalse );
}


/*
===============
IN_Init
===============
*/
void IN_ResetInputState( void )
{
	IN_EndTemporaryMouseCapture();
	s_absPointerOutside = qfalse;
	s_absHaveLast = qfalse;
	s_relativeRemainderX = 0.0f;
	s_relativeRemainderY = 0.0f;
	IN_ResetWheelAccumulator();
	s_physicalModifiers = SDL_GetModState();
	s_lastKeyDown = 0;
	s_lastKeyDownWasRepeat = qfalse;
#ifdef USE_JOYSTICK
	// The ordered client barrier clears logical joystick state. Clear the
	// producer snapshot with it so a control still held after focus restoration
	// is emitted again on the first fresh poll.
	stick_state = {};
#endif
}


void IN_Init( void )
{
	if ( !SDL_WasInit( SDL_INIT_VIDEO ) )
	{
		Com_Error( ERR_FATAL, "IN_Init called before SDL_Init( SDL_INIT_VIDEO )" );
		return;
	}

	Com_DPrintf( "\n------- Input Initialization -------\n" );

	in_keyboardDebug = Cvar_Get( "in_keyboardDebug", "0", CVAR_ARCHIVE );
	Cvar_SetDescription( in_keyboardDebug, "Print keyboard debug info." );
	in_forceCharset = Cvar_Get( "in_forceCharset", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( in_forceCharset, "Try to translate non-ASCII chars in keyboard input or force EN/US keyboard layout." );

	// mouse variables
	in_mouse = Cvar_Get( "in_mouse", "2", CVAR_ARCHIVE | CVAR_LATCH | CVAR_CLOUD );
	Cvar_CheckRange( in_mouse, "-1", "2", CV_INTEGER );
	Cvar_SetDescription( in_mouse,
		"Mouse data input source:\n" \
		"  0 - disable mouse input\n" \
		"  1 - SDL relative mouse\n" \
		"  2 - Quake Live raw/relative mouse\n" \
		" -1 - win32 mouse" );

#ifdef USE_JOYSTICK
	in_joystick = Cvar_Get( "in_joystick", "0", CVAR_ARCHIVE|CVAR_LATCH );
	Cvar_SetDescription( in_joystick, "Whether or not joystick support is on." );
	in_joystickThreshold = Cvar_Get( "joy_threshold", "0.15", CVAR_ARCHIVE );
	Cvar_CheckRange( in_joystickThreshold, "0", "1", CV_FLOAT );
	Cvar_SetDescription( in_joystickThreshold, "Threshold of joystick moving distance." );

	j_pitch =        Cvar_Get( "j_pitch",        "0.022", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( j_pitch, "Joystick pitch rotation speed/direction." );
	j_yaw =          Cvar_Get( "j_yaw",          "-0.022", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( j_yaw, "Joystick yaw rotation speed/direction." );
	j_forward =      Cvar_Get( "j_forward",      "-0.25", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( j_forward, "Joystick forward movement speed/direction." );
	j_side =         Cvar_Get( "j_side",         "0.25", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( j_side, "Joystick side movement speed/direction." );
	j_up =           Cvar_Get( "j_up",           "0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( j_up, "Joystick up movement speed/direction." );

	j_pitch_axis =   Cvar_Get( "j_pitch_axis",   "3", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( j_pitch_axis,   "0", va("%i",MAX_JOYSTICK_AXIS-1), CV_INTEGER );
	Cvar_SetDescription( j_pitch_axis, "Selects which joystick axis controls pitch." );
	j_yaw_axis =     Cvar_Get( "j_yaw_axis",     "2", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( j_yaw_axis,     "0", va("%i",MAX_JOYSTICK_AXIS-1), CV_INTEGER );
	Cvar_SetDescription( j_yaw_axis, "Selects which joystick axis controls yaw." );
	j_forward_axis = Cvar_Get( "j_forward_axis", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( j_forward_axis, "0", va("%i",MAX_JOYSTICK_AXIS-1), CV_INTEGER );
	Cvar_SetDescription( j_forward_axis, "Selects which joystick axis controls forward/back." );
	j_side_axis =    Cvar_Get( "j_side_axis",    "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( j_side_axis,    "0", va("%i",MAX_JOYSTICK_AXIS-1), CV_INTEGER );
	Cvar_SetDescription( j_side_axis, "Selects which joystick axis controls left/right." );
	j_up_axis =      Cvar_Get( "j_up_axis",      "4", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( j_up_axis,      "0", va("%i",MAX_JOYSTICK_AXIS-1), CV_INTEGER );
	Cvar_SetDescription( j_up_axis, "Selects which joystick axis controls up/down." );
#endif

	// ~ and `, as keys and characters
	cl_consoleKeys = Cvar_Get( "cl_consoleKeys", "~ ` 0x7e 0x60", CVAR_ARCHIVE );
	Cvar_SetDescription( cl_consoleKeys, "Space delimited list of key names or characters that toggle the console." );

	// Retail keeps absolute menu input and pointer presentation alive when
	// in_mouse 0 disables the relative gameplay source.
	mouseAvailable = qtrue;
	mouse_focus = ( SDL_GetMouseFocus() == SDL_window ) ? qtrue : ( glw_state.isFullscreen ? qtrue : qfalse );

	// The window may have been recreated, so nothing about the previous SDL
	// pointer state can be assumed. IN_ApplyPointerMode reapplies on the first
	// frame because the latch starts invalid.
	s_pointerOwner = PointerOwner::Gameplay;
	s_pointerMode = PointerMode{};
	s_pointerModeValid = qfalse;
	s_absHaveLast = qfalse;
	s_absPointerOutside = qfalse;
	IN_EndTemporaryMouseCapture();
	s_relativeRemainderX = 0.0f;
	s_relativeRemainderY = 0.0f;
	IN_ResetWheelAccumulator();
	s_pointerApplyFailureReported = qfalse;
	s_physicalModifiers = SDL_KMOD_NONE;
	s_mouseAuxButtonState = 0;
	s_lastKeyDown = 0;
	s_lastKeyDownWasRepeat = qfalse;
	s_fullscreenOcclusionReset = qfalse;

	s_textInputActive =
		( SDL_window && SDL_TextInputActive( SDL_window ) ) ? qtrue : qfalse;
	s_textInputFailureReported = qfalse;
	IN_ReconcileTextInput();

#ifdef USE_JOYSTICK
	IN_InitJoystick();
#endif

	Cmd_AddCommand( "minimize", IN_Minimize );
	Cmd_AddCommand( "in_restart", IN_Restart );

	Com_DPrintf( "------------------------------------\n" );
}


/*
===============
IN_Shutdown
===============
*/
void IN_Shutdown( void )
{
	IN_SetTextInputActive( qfalse );

	IN_ReleasePointer();

	mouseAvailable = qfalse;
	mouse_focus = qfalse;

#ifdef USE_JOYSTICK
	IN_ShutdownJoystick();
#endif

	Cmd_RemoveCommand( "minimize" );
	Cmd_RemoveCommand( "in_restart" );
}
