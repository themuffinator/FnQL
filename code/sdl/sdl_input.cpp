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

#include "../client/client.h"
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
static qboolean mouseActive = qfalse;

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

#define CTRL(a) ((a)-'a'+1)

static void IN_ShowCursor( qboolean show )
{
	if ( show ) {
		SDL_ShowCursor();
	} else {
		SDL_HideCursor();
	}
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

			case SDLK_MODE:         key = K_MODE;          break;
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
	else if ( IN_IsConsoleKey( key, 0 ) )
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
static void IN_GobbleMouseEvents( void )
{
	SDL_Event dummy[ 1 ];
	int val = 0;

	// Gobble any mouse events
	SDL_PumpEvents();

	while( ( val = SDL_PeepEvents( dummy, ARRAY_LEN( dummy ), SDL_GETEVENT,
		SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL ) ) > 0 ) { }

	if ( val < 0 )
		Com_Printf( "%s failed: %s\n", __func__, SDL_GetError() );
}


//#define DEBUG_EVENTS

/*
===============
IN_ActivateMouse
===============
*/
static void IN_ActivateMouse( void )
{
	const qboolean consoleActive = ( Key_GetCatcher() & KEYCATCH_CONSOLE ) ? qtrue : qfalse;
	const qboolean grabMouse = ( !in_nograb->integer || consoleActive ) ? qtrue : qfalse;

	if ( !mouseAvailable )
		return;

	if ( !mouseActive )
	{
		IN_GobbleMouseEvents();

		SDL_SetWindowRelativeMouseMode( SDL_window, ( in_mouse->integer == 1 && grabMouse ) ? true : false );
		SDL_SetWindowMouseGrab( SDL_window, grabMouse ? true : false );

		if ( glw_state.isFullscreen )
			IN_ShowCursor( qfalse );

		SDL_WarpMouseInWindow( SDL_window, glw_state.window_width / 2, glw_state.window_height / 2 );

#ifdef DEBUG_EVENTS
		Com_Printf( "%4i %s\n", Sys_Milliseconds(), __func__ );
#endif
	}

	// in_nograb makes no sense in fullscreen mode
	if ( !glw_state.isFullscreen )
	{
		if ( in_nograb->modified || !mouseActive )
		{
			if ( !grabMouse ) {
				SDL_SetWindowRelativeMouseMode( SDL_window, false );
				SDL_SetWindowMouseGrab( SDL_window, false );
			} else {
				SDL_SetWindowRelativeMouseMode( SDL_window, in_mouse->integer == 1 ? true : false );
				SDL_SetWindowMouseGrab( SDL_window, true );
			}

			in_nograb->modified = qfalse;
		}
	}

	mouseActive = qtrue;
}


/*
===============
IN_DeactivateMouse
===============
*/
static void IN_DeactivateMouse( void )
{
	const char* drv = SDL_GetCurrentVideoDriver();

	if ( !mouseAvailable )
		return;

	if ( mouseActive )
	{
#ifdef DEBUG_EVENTS
		Com_Printf( "%4i %s\n", Sys_Milliseconds(), __func__ );
#endif
		IN_GobbleMouseEvents();

		SDL_SetWindowMouseGrab( SDL_window, false );
		SDL_SetWindowRelativeMouseMode( SDL_window, false );

		if ( gw_active )
			SDL_WarpMouseInWindow( SDL_window, glw_state.window_width / 2, glw_state.window_height / 2 );
		else
		{
			if ( glw_state.isFullscreen )
				IN_ShowCursor( qtrue );

			if ( drv && strcmp( drv, "x11" ) == 0 ) {
				SDL_WarpMouseGlobal( glw_state.desktop_width / 2, glw_state.desktop_height / 2 );
			}
		}

		mouseActive = qfalse;
	}

	// Always show the cursor when the mouse is disabled,
	// but not when fullscreen
	if ( !glw_state.isFullscreen )
		IN_ShowCursor( qtrue );
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


struct joystickState_t {
	qboolean buttons[SDL_GAMEPAD_BUTTON_COUNT + 1]; // +1 because old max was 16, current SDL_GAMEPAD_BUTTON_COUNT is 15
	unsigned int oldaxes;
	int oldaaxes[MAX_JOYSTICK_AXIS];
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
	fnql::sdl::ScopedSubSystem joystickSubsystem;
	fnql::sdl::ScopedSubSystem gamepadSubsystem;

	IN_CloseJoystickHandles();

	// Initialize joystick and gamepad subsystems explicitly so the raw
	// joystick path and the SDL3 gamepad path stay available together.
	if ( !SDL_WasInit( SDL_INIT_JOYSTICK ) )
	{
		Com_DPrintf( "Calling SDL_InitSubSystem(SDL_INIT_JOYSTICK)...\n" );
		if ( !SDL_InitSubSystem( SDL_INIT_JOYSTICK ) )
		{
			Com_DPrintf( "SDL_InitSubSystem(SDL_INIT_JOYSTICK) failed: %s\n", SDL_GetError() );
			return;
		}
		joystickSubsystem = fnql::sdl::ScopedSubSystem( SDL_INIT_JOYSTICK, true );
		Com_DPrintf( "SDL_InitSubSystem(SDL_INIT_JOYSTICK) passed.\n" );
	}

	if ( !SDL_WasInit( SDL_INIT_GAMEPAD ) )
	{
		Com_DPrintf( "Calling SDL_InitSubSystem(SDL_INIT_GAMEPAD)...\n" );
		if ( !SDL_InitSubSystem( SDL_INIT_GAMEPAD ) )
		{
			Com_DPrintf( "SDL_InitSubSystem(SDL_INIT_GAMEPAD) failed: %s\n", SDL_GetError() );
			return;
		}
		gamepadSubsystem = fnql::sdl::ScopedSubSystem( SDL_INIT_GAMEPAD, true );
		Com_DPrintf( "SDL_InitSubSystem(SDL_INIT_GAMEPAD) passed.\n" );
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
	Cvar_SetDescription( cv, "List of available joysticks." );

	if( !in_joystick->integer ) {
		Com_DPrintf( "Joystick is not active.\n" );
		return;
	}

	in_joystickNo = Cvar_Get( "in_joystickNo", "0", CVAR_ARCHIVE );
	Cvar_SetDescription( in_joystickNo, "Select which joystick to use." );
	if( in_joystickNo->integer < 0 || in_joystickNo->integer >= total )
		Cvar_Set( "in_joystickNo", "0" );

	in_joystickUseAnalog = Cvar_Get( "in_joystickUseAnalog", "0", CVAR_ARCHIVE );
	Cvar_SetDescription( in_joystickUseAnalog, "Do not translate joystick axis events to keyboard commands." );

	if ( total <= 0 )
	{
		Com_DPrintf( "No joysticks found.\n" );
		joystickSubsystem.release();
		gamepadSubsystem.release();
		return;
	}

	stickInstance = joysticks.get()[ in_joystickNo->integer ];
	stick = SDL_OpenJoystick( stickInstance );

	if (stick == NULL) {
		Com_DPrintf( "No joystick opened: %s\n", SDL_GetError() );
		joystickSubsystem.release();
		gamepadSubsystem.release();
		return;
	}

	if (SDL_IsGamepad(stickInstance))
		gamepad = SDL_OpenGamepad(stickInstance);

	joystickSubsystem.release();
	gamepadSubsystem.release();

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
	if ( !SDL_WasInit( SDL_INIT_GAMEPAD ) )
		return;

	if ( !SDL_WasInit( SDL_INIT_JOYSTICK ) )
		return;

	IN_CloseJoystickHandles();

	SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
	SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
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

	return *outSign != 0;
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
	std::array<qboolean, MAX_JOYSTICK_AXIS> translatedAxesSet = {};
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

	// check buttons
	for (i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++)
	{
		qboolean pressed = SDL_GetGamepadButton(gamepad, (SDL_GamepadButton)(SDL_GAMEPAD_BUTTON_SOUTH + i)) ? qtrue : qfalse;
		if (pressed != stick_state.buttons[i])
		{
			if ( i >= SDL_GAMEPAD_BUTTON_MISC1 ) {
				Com_QueueEvent(in_eventTime, SE_KEY, K_PAD0_MISC1 + i - SDL_GAMEPAD_BUTTON_MISC1, pressed, 0, NULL);
			} else
			{
				Com_QueueEvent(in_eventTime, SE_KEY, K_PAD0_SOUTH + i, pressed, 0, NULL);
			}
			stick_state.buttons[i] = pressed;
		}
	}

	// must defer translated axes until all real axes are processed
	// must be done this way to prevent a later mapped axis from zeroing out a previous one
	// check axes
	for (i = 0; i < SDL_GAMEPAD_AXIS_COUNT; i++)
	{
		int axis = SDL_GetGamepadAxis(gamepad, (SDL_GamepadAxis)(SDL_GAMEPAD_AXIS_LEFTX + i));
		int oldAxis = stick_state.oldaaxes[i];

		// Smoothly ramp from dead zone to maximum value
		float f = ((float)abs(axis) / 32767.0f - in_joystickThreshold->value) / (1.0f - in_joystickThreshold->value);

		if (f < 0.0f)
			f = 0.0f;

		axis = (int)(32767 * ((axis < 0) ? -f : f));

		if (axis != oldAxis)
		{
			qboolean posAnalog = qfalse, negAnalog = qfalse;
			int negKey = negMap[i];
			int posKey = posMap[i];

			if (in_joystickUseAnalog->integer)
			{
				int posAxis = 0, posSign = 0, negAxis = 0, negSign = 0;

				// get axes and axes signs for keys if available
				posAnalog = KeyToAxisAndSign(posKey, &posAxis, &posSign);
				negAnalog = KeyToAxisAndSign(negKey, &negAxis, &negSign);

				// positive to negative/neutral -> keyup if axis hasn't yet been set
				if (posAnalog && !translatedAxesSet[posAxis] && oldAxis > 0 && axis <= 0)
				{
					translatedAxes[posAxis] = 0;
					translatedAxesSet[posAxis] = qtrue;
				}

				// negative to positive/neutral -> keyup if axis hasn't yet been set
				if (negAnalog && !translatedAxesSet[negAxis] && oldAxis < 0 && axis >= 0)
				{
					translatedAxes[negAxis] = 0;
					translatedAxesSet[negAxis] = qtrue;
				}

				// negative/neutral to positive -> keydown
				if (posAnalog && axis > 0)
				{
					translatedAxes[posAxis] = axis * posSign;
					translatedAxesSet[posAxis] = qtrue;
				}

				// positive/neutral to negative -> keydown
				if (negAnalog && axis < 0)
				{
					translatedAxes[negAxis] = -axis * negSign;
					translatedAxesSet[negAxis] = qtrue;
				}
			}

			// keyups first so they get overridden by keydowns later

			// positive to negative/neutral -> keyup
			if (!posAnalog && posKey && oldAxis > 0 && axis <= 0)
				Com_QueueEvent(in_eventTime, SE_KEY, posKey, qfalse, 0, NULL);

			// negative to positive/neutral -> keyup
			if (!negAnalog && negKey && oldAxis < 0 && axis >= 0)
				Com_QueueEvent(in_eventTime, SE_KEY, negKey, qfalse, 0, NULL);

			// negative/neutral to positive -> keydown
			if (!posAnalog && posKey && oldAxis <= 0 && axis > 0)
				Com_QueueEvent(in_eventTime, SE_KEY, posKey, qtrue, 0, NULL);

			// positive/neutral to negative -> keydown
			if (!negAnalog && negKey && oldAxis >= 0 && axis < 0)
				Com_QueueEvent(in_eventTime, SE_KEY, negKey, qtrue, 0, NULL);

			stick_state.oldaaxes[i] = axis;
		}
	}

	// set translated axes
	if (in_joystickUseAnalog->integer)
	{
		for (i = 0; i < MAX_JOYSTICK_AXIS; i++)
		{
			if (translatedAxesSet[i])
				Com_QueueEvent(in_eventTime, SE_JOYSTICK_AXIS, i, translatedAxes[i], 0, NULL);
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
			balldx += dx;
			balldy += dy;
		}
		if (balldx || balldy)
		{
			// !!! FIXME: is this good for stick balls, or just mice?
			// Scale like the mouse input...
			if (abs(balldx) > 1)
				balldx *= 2;
			if (abs(balldy) > 1)
				balldy *= 2;
			Com_QueueEvent( in_eventTime, SE_MOUSE, balldx, balldy, 0, NULL );
		}
	}

	// now query the stick buttons...
	total = SDL_GetNumJoystickButtons(stick);
	if (total > 0)
	{
		if (total > ARRAY_LEN(stick_state.buttons))
			total = ARRAY_LEN(stick_state.buttons);
		for (i = 0; i < total; i++)
		{
			qboolean pressed = (SDL_GetJoystickButton(stick, i) != 0) ? qtrue : qfalse;
			if (pressed != stick_state.buttons[i])
			{
				Com_QueueEvent( in_eventTime, SE_KEY, K_JOY1 + i, pressed, 0, NULL );
				stick_state.buttons[i] = pressed;
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
			for (i = 0; i < total; i++)
			{
				Sint16 axis = SDL_GetJoystickAxis(stick, i);
				float f = ( (float) abs(axis) ) / 32767.0f;
				
				if( f < in_joystickThreshold->value ) axis = 0;

				if ( axis != stick_state.oldaaxes[i] )
				{
					Com_QueueEvent( in_eventTime, SE_JOYSTICK_AXIS, i, axis, 0, NULL );
					stick_state.oldaaxes[i] = axis;
				}
			}
		}
		else
		{
			if (total > 16) total = 16;
			for (i = 0; i < total; i++)
			{
				Sint16 axis = SDL_GetJoystickAxis(stick, i);
				float f = ( (float) axis ) / 32767.0f;
				if( f < -in_joystickThreshold->value ) {
					axes |= ( 1 << ( i * 2 ) );
				} else if( f > in_joystickThreshold->value ) {
					axes |= ( 1 << ( ( i * 2 ) + 1 ) );
				}
			}
		}
	}

	/* Time to update axes state based on old vs. new. */
	if (axes != stick_state.oldaxes)
	{
		for( i = 0; i < 16; i++ ) {
			if( ( axes & ( 1 << i ) ) && !( stick_state.oldaxes & ( 1 << i ) ) ) {
				Com_QueueEvent( in_eventTime, SE_KEY, joy_keys[i], qtrue, 0, NULL );
			}

			if( !( axes & ( 1 << i ) ) && ( stick_state.oldaxes & ( 1 << i ) ) ) {
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

static void IN_SyncModifiers( void );

static sdlKeyInfo_t IN_MakeKeyInfo( const SDL_KeyboardEvent *event )
{
	sdlKeyInfo_t keyinfo;

	keyinfo.scancode = event->scancode;
	keyinfo.sym = event->key;
	keyinfo.mod = event->mod;

	return keyinfo;
}

static void IN_HandleWindowEvent( Uint32 type, const SDL_WindowEvent *window, int *lastKeyDown )
{
#ifdef DEBUG_EVENTS
	Com_Printf( "%4i %s\n", window->timestamp, eventName( type ) );
#endif

	switch ( type )
	{
		case SDL_EVENT_WINDOW_MOVED:
			GLW_UpdateWindowState();
			if ( gw_active && !gw_minimized && !glw_state.isFullscreen ) {
				Cvar_SetIntegerValue( "vid_xpos", window->data1 );
				Cvar_SetIntegerValue( "vid_ypos", window->data2 );
			}
			break;

		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
			GLW_UpdateWindowState();
			break;

		case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
		case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
		case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
			GLW_UpdateWindowState();
			if ( gw_active && re.SetColorMappings ) {
				re.SetColorMappings();
			}
			break;

		case SDL_EVENT_WINDOW_HIDDEN:
		case SDL_EVENT_WINDOW_MINIMIZED:
			gw_active = qfalse;
			gw_minimized = qtrue;
			mouse_focus = qfalse;
			break;

		case SDL_EVENT_WINDOW_OCCLUDED:
			if ( glw_state.isFullscreen ) {
				gw_minimized = qtrue;
				mouse_focus = qfalse;
			}
			break;

		case SDL_EVENT_WINDOW_EXPOSED:
		case SDL_EVENT_WINDOW_SHOWN:
		case SDL_EVENT_WINDOW_RESTORED:
		case SDL_EVENT_WINDOW_MAXIMIZED:
			if ( gw_active || !glw_state.isFullscreen ) {
				gw_minimized = qfalse;
			}
			GLW_UpdateWindowState();
			break;

		case SDL_EVENT_WINDOW_FOCUS_LOST:
			*lastKeyDown = 0;
			Key_ClearStates();
			IN_SyncModifiers();
			gw_active = qfalse;
			if ( glw_state.isFullscreen ) {
				gw_minimized = qtrue;
			}
			mouse_focus = qfalse;
			break;

		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			*lastKeyDown = 0;
			Key_ClearStates();
			IN_SyncModifiers();
			gw_active = qtrue;
			gw_minimized = qfalse;
			mouse_focus = qtrue;
			GLW_UpdateWindowState();
			if ( re.SetColorMappings ) {
				re.SetColorMappings();
			}
			break;

		case SDL_EVENT_WINDOW_MOUSE_ENTER:
			mouse_focus = qtrue;
			break;

		case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			if ( glw_state.isFullscreen ) {
				mouse_focus = qfalse;
			}
			break;

		default:
			break;
	}
}


/*
===============
IN_SyncModifiers
===============
*/
static void IN_SyncModifiers( void ) {
    SDL_Keymod mod = SDL_GetModState();

    keys[K_CTRL].down  = (mod & SDL_KMOD_CTRL)  ? qtrue : qfalse;
    keys[K_SHIFT].down = (mod & SDL_KMOD_SHIFT) ? qtrue : qfalse;
    keys[K_ALT].down   = (mod & SDL_KMOD_ALT)   ? qtrue : qfalse;
}


static int IN_ReadUtf8Codepoint( const char **cursor )
{
	const unsigned char *c = reinterpret_cast<const unsigned char *>( *cursor );
	int utf32 = 0;

	if ( ( c[0] & 0x80 ) == 0 )
	{
		utf32 = c[0];
		c++;
	}
	else if ( ( c[0] & 0xE0 ) == 0xC0 && c[1] ) // 110x xxxx
	{
		utf32 |= ( c[0] & 0x1F ) << 6;
		utf32 |= ( c[1] & 0x3F );
		c += 2;
	}
	else if ( ( c[0] & 0xF0 ) == 0xE0 && c[1] && c[2] ) // 1110 xxxx
	{
		utf32 |= ( c[0] & 0x0F ) << 12;
		utf32 |= ( c[1] & 0x3F ) << 6;
		utf32 |= ( c[2] & 0x3F );
		c += 3;
	}
	else if ( ( c[0] & 0xF8 ) == 0xF0 && c[1] && c[2] && c[3] ) // 1111 0xxx
	{
		utf32 |= ( c[0] & 0x07 ) << 18;
		utf32 |= ( c[1] & 0x3F ) << 12;
		utf32 |= ( c[2] & 0x3F ) << 6;
		utf32 |= ( c[3] & 0x3F );
		c += 4;
	}
	else
	{
		Com_DPrintf( "Unrecognised UTF-8 lead byte: 0x%x\n", (unsigned int)c[0] );
		c++;
	}

	*cursor = reinterpret_cast<const char *>( c );
	return utf32;
}


static void IN_QueueTextInput( const char *text )
{
	const char *c = text;

	while ( *c )
	{
		const int utf32 = IN_ReadUtf8Codepoint( &c );

		if( utf32 != 0 )
		{
			if ( IN_IsConsoleKey( 0, utf32 ) )
			{
				Com_QueueEvent( in_eventTime, SE_KEY, K_CONSOLE, qtrue, 0, NULL );
				Com_QueueEvent( in_eventTime, SE_KEY, K_CONSOLE, qfalse, 0, NULL );
			}
			else
			{
				Com_QueueEvent( in_eventTime, SE_CHAR, utf32, 0, 0, NULL );
			}
		}
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
	static int lastKeyDown = 0;

	if ( !SDL_WasInit( SDL_INIT_VIDEO ) )
			return;

	in_eventTime = Sys_Milliseconds();

	IN_SyncModifiers();

	while ( SDL_PollEvent( &e ) )
	{
#ifndef _WIN32
		if ( Sys_ConsoleHandleEvent( &e ) ) {
			continue;
		}
#endif

		switch( e.type )
		{
			case SDL_EVENT_KEY_DOWN:
			{
				sdlKeyInfo_t keyinfo = IN_MakeKeyInfo( &e.key );

				if ( e.key.repeat && Key_GetCatcher() == 0 )
					break;
				key = IN_TranslateSDLToQ3Key( &keyinfo, qtrue );

				if ( key == K_ENTER && keys[K_ALT].down ) {
					Cvar_SetIntegerValue( "r_fullscreen", glw_state.isFullscreen ? 0 : 1 );
					// fast restart keeps the window alive so the
					// fullscreen state can be toggled in place
					Cbuf_AddText( "vid_restart fast\n" );
					break;
				}

				if ( key ) {
					Com_QueueEvent( in_eventTime, SE_KEY, key, qtrue, 0, NULL );

					if ( key == K_BACKSPACE )
						Com_QueueEvent( in_eventTime, SE_CHAR, CTRL('h'), 0, 0, NULL );
					else if ( key == K_ESCAPE )
						Com_QueueEvent( in_eventTime, SE_CHAR, key, 0, 0, NULL );
					else if( keys[K_CTRL].down && key >= 'a' && key <= 'z' )
						Com_QueueEvent( in_eventTime, SE_CHAR, CTRL(key), 0, 0, NULL );
#ifdef MACOS_X
					else if( keys[K_COMMAND].down && key == 'v' )
						Com_QueueEvent( in_eventTime, SE_CHAR, CTRL(key), 0, 0, NULL );
#endif
				}

				lastKeyDown = key;
				break;
			}

			case SDL_EVENT_KEY_UP:
			{
				sdlKeyInfo_t keyinfo = IN_MakeKeyInfo( &e.key );

				if( ( key = IN_TranslateSDLToQ3Key( &keyinfo, qfalse ) ) )
					Com_QueueEvent( in_eventTime, SE_KEY, key, qfalse, 0, NULL );

				lastKeyDown = 0;
				break;
			}

			case SDL_EVENT_TEXT_INPUT:
				if( lastKeyDown != K_CONSOLE )
				{
					IN_QueueTextInput( e.text.text );
				}
				break;

			case SDL_EVENT_MOUSE_MOTION:
				if( mouseActive )
				{
					if( !e.motion.xrel && !e.motion.yrel )
						break;
					Com_QueueEvent( in_eventTime, SE_MOUSE, (int)e.motion.xrel, (int)e.motion.yrel, 0, NULL );
				}
				break;

			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
				{
					int b;
					switch( e.button.button )
					{
						case SDL_BUTTON_LEFT:   b = K_MOUSE1;     break;
						case SDL_BUTTON_MIDDLE: b = K_MOUSE3;     break;
						case SDL_BUTTON_RIGHT:  b = K_MOUSE2;     break;
						case SDL_BUTTON_X1:     b = K_MOUSE4;     break;
						case SDL_BUTTON_X2:     b = K_MOUSE5;     break;
						default:                b = K_AUX1 + ( e.button.button - SDL_BUTTON_X2 + 1 ) % 16; break;
					}
					Com_QueueEvent( in_eventTime, SE_KEY, b,
						( e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? qtrue : qfalse ), 0, NULL );
				}
				break;

			case SDL_EVENT_MOUSE_WHEEL:
				if( e.wheel.y > 0.0f )
				{
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELUP, qtrue, 0, NULL );
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELUP, qfalse, 0, NULL );
				}
				else if( e.wheel.y < 0.0f )
				{
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELDOWN, qtrue, 0, NULL );
					Com_QueueEvent( in_eventTime, SE_KEY, K_MWHEELDOWN, qfalse, 0, NULL );
				}
				break;

#ifdef USE_JOYSTICK
			case SDL_EVENT_JOYSTICK_ADDED:
			case SDL_EVENT_JOYSTICK_REMOVED:
			case SDL_EVENT_GAMEPAD_ADDED:
			case SDL_EVENT_GAMEPAD_REMOVED:
				if ( in_joystick && in_joystick->integer )
					IN_InitJoystick();
				break;
#endif

			case SDL_EVENT_QUIT:
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				Cbuf_ExecuteText( EXEC_NOW, "quit Closed window\n" );
				break;

			case SDL_EVENT_WINDOW_MOVED:
			case SDL_EVENT_WINDOW_HIDDEN:
			case SDL_EVENT_WINDOW_MINIMIZED:
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
				IN_HandleWindowEvent( e.type, &e.window, &lastKeyDown );
				break;

			default:
				break;
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
#ifdef USE_JOYSTICK
	IN_JoyMove();
#endif

	if ( !gw_active || !mouse_focus || ( in_nograb->integer && !( Key_GetCatcher() & KEYCATCH_CONSOLE ) ) ) {
		IN_DeactivateMouse();
		return;
	}

	IN_ActivateMouse();

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
#ifdef USE_JOYSTICK
	IN_ShutdownJoystick();
#endif
	IN_Shutdown();
	IN_Init();
}


/*
===============
IN_Init
===============
*/
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
	in_mouse = Cvar_Get( "in_mouse", "1", CVAR_ARCHIVE );
	Cvar_CheckRange( in_mouse, "-1", "1", CV_INTEGER );
	Cvar_SetDescription( in_mouse,
		"Mouse data input source:\n" \
		"  0 - disable mouse input\n" \
		"  1 - di/raw mouse\n" \
		" -1 - win32 mouse" );

#ifdef USE_JOYSTICK
	in_joystick = Cvar_Get( "in_joystick", "0", CVAR_ARCHIVE|CVAR_LATCH );
	Cvar_SetDescription( in_joystick, "Whether or not joystick support is on." );
	in_joystickThreshold = Cvar_Get( "joy_threshold", "0.15", CVAR_ARCHIVE );
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

	mouseAvailable = ( in_mouse->value != 0 ) ? qtrue : qfalse;
	mouse_focus = ( SDL_GetMouseFocus() == SDL_window ) ? qtrue : ( glw_state.isFullscreen ? qtrue : qfalse );

	if ( SDL_window && !SDL_StartTextInput( SDL_window ) ) {
		Com_DPrintf( "SDL_StartTextInput failed: %s\n", SDL_GetError() );
	}

	//IN_DeactivateMouse();

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
	if ( SDL_window ) {
		SDL_StopTextInput( SDL_window );
	}

	IN_DeactivateMouse();

	mouseAvailable = qfalse;
	mouse_focus = qfalse;

#ifdef USE_JOYSTICK
	IN_ShutdownJoystick();
#endif

	Cmd_RemoveCommand( "minimize" );
	Cmd_RemoveCommand( "in_restart" );
}
