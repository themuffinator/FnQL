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
// cl_input.cpp -- builds an intended movement command to send to the server

extern "C" {
#include "client.h"
}

#include "client_cpp.h"
#include "input_compat.hpp"
#include "../qcommon/netchan_safety.hpp"

#include <array>
#include <cmath>

using fnql::ScopedFileHandle;

namespace {

constexpr std::size_t kInputButtonCount = 16;

struct InputCommandBinding {
	const char *name;
	xcommand_t handler;
};

} // namespace

static unsigned frame_msec;
static unsigned old_com_frameTime;

/*
===============================================================================

KEY BUTTONS

Continuous button event tracking is complicated by the fact that two different
input sources (say, mouse button 1 and the control key) can both press the
same button, but the button should only be released when both of the
pressing key have been released.

When a key event issues a button command (+forward, +attack, etc), it appends
its key number as argv(1) so it can be matched up with the release.

argv(2) will be set to the time the event happened, which allows exact
control even at low framerates when the down and up events may both get queued
at the same time.

===============================================================================
*/

struct kbutton_t {
	std::array<int, 2> down;		// key nums holding it down
	unsigned	downtime;		// msec timestamp
	unsigned	msec;			// msec down this frame if both a down and up happened
	bool		active;			// current state
	bool		wasPressed;		// set when down, not cleared when up
};

static kbutton_t in_left, in_right, in_forward, in_back;
static kbutton_t in_lookup, in_lookdown, in_moveleft, in_moveright;
static kbutton_t in_strafe, in_speed;
static kbutton_t in_up, in_down, in_mlook;
static std::array<kbutton_t, kInputButtonCount> in_buttons;

static cvar_t *cl_nodelta;

static cvar_t *cl_showSend;

static cvar_t *cl_sensitivity;
static cvar_t *cl_mouseAccel;
static cvar_t *cl_mouseAccelDebug;
static cvar_t *cl_mouseAccelOffset;
static cvar_t *cl_mouseAccelPower;
static cvar_t *cl_mouseAccelStyle;
static cvar_t *cl_mouseSensCap;
static cvar_t *cl_showMouseRate;
static cvar_t *cl_viewAccel;

static cvar_t *cl_run;
static cvar_t *cl_freelook;

static cvar_t *cl_yawspeed;
static cvar_t *cl_pitchspeed;
static cvar_t *cl_anglespeedkey;

static cvar_t *cl_maxpackets;
static cvar_t *cl_packetdup;

static cvar_t *m_pitch;
static cvar_t *m_yaw;
static cvar_t *m_forward;
static cvar_t *m_side;
static cvar_t *m_filter;
static cvar_t *m_cpi;

static fnql::input::RetailViewAngleFilter retailMouseFilter;
static ScopedFileHandle mouseAccelDebugLog;
static bool mouseAccelDebugOpenFailed;


/*
Relative mouse deltas have no retained timestamp. If usercmd generation stops
while the platform keeps delivering motion, replaying the whole gap in the
first resumed command creates an artificial view kick (up to the 90-degree
per-command pitch guard). Discard only that unsampleable motion and rebase the
frame clock; held keys and persistent joystick axes retain their normal state.
*/
static void CL_SuspendUsercmdInputSampling( void ) {
	for ( int i = 0; i < 2; ++i ) {
		cl.mouseDx[i] = 0;
		cl.mouseDy[i] = 0;
	}
	retailMouseFilter.Reset(
		{ cl.viewangles[YAW], cl.viewangles[PITCH] } );
	old_com_frameTime = static_cast<unsigned>( com_frameTime );
}

/*
Canonical engine-owned +/- binding commands carry a reserved generation tag as
argv(3). Return their validated source key, reject stale/malformed reserved
tags, and classify every untagged command as legacy so existing manual command
forms retain their semantics.
*/
int CL_ValidateInputCommandSource( void ) {
	const fnql::input::InputCommandGenerationTag tag =
		fnql::input::ParseInputCommandGenerationTag( Cmd_Argv( 3 ) );
	if ( !tag.tagged ) {
		return CL_INPUT_COMMAND_LEGACY;
	}

	const std::optional<unsigned> sourceKey =
		fnql::input::ParseUnsignedInputCommandArgument( Cmd_Argv( 1 ) );
	if ( !tag.valid || !sourceKey || *sourceKey == 0u ||
		*sourceKey >= MAX_KEYS ) {
		return CL_INPUT_COMMAND_STALE;
	}

	const int key = static_cast<int>( *sourceKey );
	return tag.value == Key_GetBindingGeneration( key )
		? key
		: CL_INPUT_COMMAND_STALE;
}


static std::array<kbutton_t *, 13> IN_CommandButtons( void ) {
	return {
		&in_left, &in_right, &in_forward, &in_back,
		&in_lookup, &in_lookdown, &in_moveleft, &in_moveright,
		&in_strafe, &in_speed, &in_up, &in_down, &in_mlook,
	};
}


static void IN_CenterView( void );


static void IN_ClearCommandInputState( void ) {
	const bool recenterMlook =
		in_mlook.active && cl_freelook && !cl_freelook->integer;
	for ( kbutton_t *button : IN_CommandButtons() ) {
		*button = kbutton_t{};
	}
	in_buttons.fill( kbutton_t{} );
	if ( recenterMlook ) {
		IN_CenterView();
	}
}


static void IN_RemoveCommandInputSource( int sourceKey ) {
	const bool mlookWasActive = in_mlook.active;
	const auto removeSource = [sourceKey]( kbutton_t *button ) {
		if ( !fnql::input::RemoveHeldInputSource(
			button->down, sourceKey ) ) {
			return;
		}
		if ( button->down[0] || button->down[1] ) {
			button->active = true;
			return;
		}

		if ( button->active ) {
			// Match an untimed legacy release for the source that disappeared.
			// Keep aggregate msec/wasPressed: without per-source provenance they
			// may also contain a completed keyboard tap that must survive this
			// deliberately narrow mouse-only recovery.
			button->msec = fnql::input::SaturatingAddUnsigned(
				button->msec, frame_msec / 2 );
		}
		button->active = false;
		button->downtime = 0;
	};

	for ( kbutton_t *button : IN_CommandButtons() ) {
		removeSource( button );
	}
	for ( kbutton_t& button : in_buttons ) {
		removeSource( &button );
	}
	// Mlook shares the source/active mechanics but has no fractional-frame
	// consumer, so do not let its otherwise-unused transient fields accumulate.
	in_mlook.msec = 0;
	in_mlook.wasPressed = false;

	if ( mlookWasActive && !in_mlook.active &&
		cl_freelook && !cl_freelook->integer ) {
		IN_CenterView();
	}
}


void CL_ClearKeyCommandInputState( void ) {
	Key_AdvanceAllBindingGenerations();
	IN_ClearCommandInputState();
	CL_ClearGeneratedVoiceInputState();
}

/*
Input cvars are persisted user data and can contain non-finite values from an
older or hand-edited configuration. Keep normal finite arithmetic identical,
but never let a float outside int's range reach ClampCharMove's int parameter.
*/
static int CL_SafeMoveValue( int current, float delta ) {
	const float finiteDelta = fnql::input::FiniteOr( delta, 0.0f );
	const float combined = fnql::input::FiniteOr(
		static_cast<float>( current ) + finiteDelta,
		static_cast<float>( current ) );
	return fnql::input::TruncateFiniteFloatToInt( combined );
}


/*
ANGLE2SHORT eventually converts view angles to int. Neutralize a non-finite
input delta (or an overflowing addition) at its source so mouse, keyboard, and
joystick look keep the last finite angle.
*/
static void CL_AddViewAngleDelta( int axis, float delta ) {
	const float current =
		fnql::input::FiniteOr( cl.viewangles[axis], 0.0f );
	const float finiteDelta = fnql::input::FiniteOr( delta, 0.0f );
	cl.viewangles[axis] =
		fnql::input::FiniteOr( current + finiteDelta, current );
}


static void IN_CenterView( void ) {
	cl.viewangles[PITCH] = -SHORT2ANGLE(cl.snap.ps.delta_angles[PITCH]);
}

static void IN_KeyDown( kbutton_t *b );
static void IN_KeyUp( kbutton_t *b );


static void IN_MLookDown( void ) {
	IN_KeyDown( &in_mlook );
	in_mlook.msec = 0;
	in_mlook.wasPressed = false;
}


static void IN_MLookUp( void ) {
	const bool wasActive = in_mlook.active;
	IN_KeyUp( &in_mlook );
	in_mlook.msec = 0;
	in_mlook.wasPressed = false;
	if ( wasActive && !in_mlook.active && !cl_freelook->integer ) {
		IN_CenterView ();
	}
}


static void IN_KeyDown( kbutton_t *b ) {
	const char *c;
	int	k;
	const int commandSource = CL_ValidateInputCommandSource();

	if ( commandSource == CL_INPUT_COMMAND_STALE ) {
		return;
	}

	if ( commandSource >= 0 ) {
		k = commandSource;
	} else {
		c = Cmd_Argv(1);
		if ( c[0] ) {
			const std::optional<int> parsed =
				fnql::input::ParseSignedInputCommandArgument( c );
			if ( !parsed || *parsed == 0 ) {
				return;
			}
			k = *parsed;
		} else {
			k = -1;		// typed manually at the console for continuous down
		}
	}

	if ( k == b->down[0] || k == b->down[1] ) {
		return;		// repeating key
	}

	if ( !b->down[0] ) {
		b->down[0] = k;
	} else if ( !b->down[1] ) {
		b->down[1] = k;
	} else {
		Com_Printf ("Three keys down for a button!\n");
		return;
	}

	if ( b->active ) {
		return;		// still down
	}

	// save timestamp for partial frame summing
	const std::optional<unsigned> timestamp =
		fnql::input::ParseUnsignedInputCommandArgument( Cmd_Argv( 2 ) );
	b->downtime = timestamp.value_or( 0u );

	b->active = true;
	b->wasPressed = true;
}


static void IN_KeyUp( kbutton_t *b ) {
	unsigned uptime;
	const char *c;
	int		k;
	const int commandSource = CL_ValidateInputCommandSource();

	if ( commandSource == CL_INPUT_COMMAND_STALE ) {
		return;
	}

	if ( commandSource >= 0 ) {
		k = commandSource;
	} else {
		c = Cmd_Argv(1);
		if ( c[0] ) {
			const std::optional<int> parsed =
				fnql::input::ParseSignedInputCommandArgument( c );
			if ( !parsed || *parsed == 0 ) {
				return;
			}
			k = *parsed;
		} else {
			// typed manually at the console, assume for unsticking, so clear all
			b->down[0] = b->down[1] = 0;
			b->active = false;
			b->downtime = 0;
			return;
		}
	}

	if ( b->down[0] == k ) {
		b->down[0] = 0;
	} else if ( b->down[1] == k ) {
		b->down[1] = 0;
	} else {
		return;		// key up without corresponding down (menu pass through)
	}
	if ( b->down[0] || b->down[1] ) {
		return;		// some other key is still holding it down
	}

	b->active = false;

	// save timestamp for partial frame summing
	const std::optional<unsigned> timestamp =
		fnql::input::ParseUnsignedInputCommandArgument( Cmd_Argv( 2 ) );
	uptime = timestamp.value_or( 0u );
	if ( uptime ) {
		b->msec = fnql::input::SaturatingAddUnsigned(
			b->msec, uptime - b->downtime );
	} else {
		b->msec = fnql::input::SaturatingAddUnsigned(
			b->msec, frame_msec / 2 );
	}

	b->active = false;
}


/*
===============
CL_KeyState

Returns the fraction of the frame that the key was down
===============
*/
static float CL_KeyState( kbutton_t *key ) {
	float		val;
	unsigned	msec;

	msec = key->msec;
	key->msec = 0;

	if ( key->active ) {
		// still down
		if ( !key->downtime ) {
			msec = fnql::input::SaturatingAddUnsigned(
				msec, frame_msec );
		} else {
			msec = fnql::input::SaturatingAddUnsigned(
				msec, fnql::input::BoundedInputElapsedMilliseconds(
					key->downtime, static_cast<unsigned>( com_frameTime ),
					frame_msec ) );
		}
		key->downtime = static_cast<unsigned>( com_frameTime );
	}
	msec = ( std::min )( msec, frame_msec );

#if 0
	if (msec) {
		Com_Printf ("%i ", msec);
	}
#endif

	val = static_cast<float>( msec ) / frame_msec;
	if ( val > 1 ) {
		val = 1;
	}

	return val;
}


static void IN_UpDown() { IN_KeyDown( &in_up ); }
static void IN_UpUp() { IN_KeyUp( &in_up ); }
static void IN_DownDown() { IN_KeyDown( &in_down ); }
static void IN_DownUp() { IN_KeyUp( &in_down ); }
static void IN_LeftDown() { IN_KeyDown( &in_left ); }
static void IN_LeftUp() { IN_KeyUp( &in_left ); }
static void IN_RightDown() { IN_KeyDown( &in_right ); }
static void IN_RightUp() { IN_KeyUp( &in_right ); }
static void IN_ForwardDown() { IN_KeyDown( &in_forward ); }
static void IN_ForwardUp() { IN_KeyUp( &in_forward ); }
static void IN_BackDown() { IN_KeyDown( &in_back ); }
static void IN_BackUp() { IN_KeyUp( &in_back ); }
static void IN_LookupDown() { IN_KeyDown( &in_lookup ); }
static void IN_LookupUp() { IN_KeyUp( &in_lookup ); }
static void IN_LookdownDown() { IN_KeyDown( &in_lookdown ); }
static void IN_LookdownUp() { IN_KeyUp( &in_lookdown ); }
static void IN_MoveleftDown() { IN_KeyDown( &in_moveleft ); }
static void IN_MoveleftUp() { IN_KeyUp( &in_moveleft ); }
static void IN_MoverightDown() { IN_KeyDown( &in_moveright ); }
static void IN_MoverightUp() { IN_KeyUp( &in_moveright ); }

static void IN_SpeedDown() { IN_KeyDown( &in_speed ); }
static void IN_SpeedUp() { IN_KeyUp( &in_speed ); }
static void IN_StrafeDown() { IN_KeyDown( &in_strafe ); }
static void IN_StrafeUp() { IN_KeyUp( &in_strafe ); }

template <std::size_t Index>
static void IN_ButtonDown( void ) {
	static_assert( Index < kInputButtonCount, "input button index out of range" );
	IN_KeyDown( &in_buttons[Index] );
}


template <std::size_t Index>
static void IN_ButtonUp( void ) {
	static_assert( Index < kInputButtonCount, "input button index out of range" );
	IN_KeyUp( &in_buttons[Index] );
}


static constexpr std::array kInputCommandBindings{
	InputCommandBinding{ "centerview", IN_CenterView },
	InputCommandBinding{ "+moveup", IN_UpDown },
	InputCommandBinding{ "-moveup", IN_UpUp },
	InputCommandBinding{ "+movedown", IN_DownDown },
	InputCommandBinding{ "-movedown", IN_DownUp },
	InputCommandBinding{ "+left", IN_LeftDown },
	InputCommandBinding{ "-left", IN_LeftUp },
	InputCommandBinding{ "+right", IN_RightDown },
	InputCommandBinding{ "-right", IN_RightUp },
	InputCommandBinding{ "+forward", IN_ForwardDown },
	InputCommandBinding{ "-forward", IN_ForwardUp },
	InputCommandBinding{ "+back", IN_BackDown },
	InputCommandBinding{ "-back", IN_BackUp },
	InputCommandBinding{ "+lookup", IN_LookupDown },
	InputCommandBinding{ "-lookup", IN_LookupUp },
	InputCommandBinding{ "+lookdown", IN_LookdownDown },
	InputCommandBinding{ "-lookdown", IN_LookdownUp },
	InputCommandBinding{ "+strafe", IN_StrafeDown },
	InputCommandBinding{ "-strafe", IN_StrafeUp },
	InputCommandBinding{ "+moveleft", IN_MoveleftDown },
	InputCommandBinding{ "-moveleft", IN_MoveleftUp },
	InputCommandBinding{ "+moveright", IN_MoverightDown },
	InputCommandBinding{ "-moveright", IN_MoverightUp },
	InputCommandBinding{ "+speed", IN_SpeedDown },
	InputCommandBinding{ "-speed", IN_SpeedUp },
	InputCommandBinding{ "+attack", IN_ButtonDown<0> },
	InputCommandBinding{ "-attack", IN_ButtonUp<0> },
	InputCommandBinding{ "+button0", IN_ButtonDown<0> },
	InputCommandBinding{ "-button0", IN_ButtonUp<0> },
	InputCommandBinding{ "+button1", IN_ButtonDown<1> },
	InputCommandBinding{ "-button1", IN_ButtonUp<1> },
	InputCommandBinding{ "+button2", IN_ButtonDown<2> },
	InputCommandBinding{ "-button2", IN_ButtonUp<2> },
	InputCommandBinding{ "+button3", IN_ButtonDown<3> },
	InputCommandBinding{ "-button3", IN_ButtonUp<3> },
	InputCommandBinding{ "+button4", IN_ButtonDown<4> },
	InputCommandBinding{ "-button4", IN_ButtonUp<4> },
	InputCommandBinding{ "+button5", IN_ButtonDown<5> },
	InputCommandBinding{ "-button5", IN_ButtonUp<5> },
	InputCommandBinding{ "+button6", IN_ButtonDown<6> },
	InputCommandBinding{ "-button6", IN_ButtonUp<6> },
	InputCommandBinding{ "+button7", IN_ButtonDown<7> },
	InputCommandBinding{ "-button7", IN_ButtonUp<7> },
	InputCommandBinding{ "+button8", IN_ButtonDown<8> },
	InputCommandBinding{ "-button8", IN_ButtonUp<8> },
	InputCommandBinding{ "+button9", IN_ButtonDown<9> },
	InputCommandBinding{ "-button9", IN_ButtonUp<9> },
	InputCommandBinding{ "+button10", IN_ButtonDown<10> },
	InputCommandBinding{ "-button10", IN_ButtonUp<10> },
	InputCommandBinding{ "+button11", IN_ButtonDown<11> },
	InputCommandBinding{ "-button11", IN_ButtonUp<11> },
	InputCommandBinding{ "+button12", IN_ButtonDown<12> },
	InputCommandBinding{ "-button12", IN_ButtonUp<12> },
	InputCommandBinding{ "+button13", IN_ButtonDown<13> },
	InputCommandBinding{ "-button13", IN_ButtonUp<13> },
	InputCommandBinding{ "+button14", IN_ButtonDown<14> },
	InputCommandBinding{ "-button14", IN_ButtonUp<14> },
	InputCommandBinding{ "+button15", IN_ButtonDown<15> },
	InputCommandBinding{ "-button15", IN_ButtonUp<15> },
	InputCommandBinding{ "+mlook", IN_MLookDown },
	InputCommandBinding{ "-mlook", IN_MLookUp },
};


qboolean CL_IsEngineStatefulInputCommand( const char *command ) {
	for ( const InputCommandBinding& binding : kInputCommandBindings ) {
		if ( binding.name[0] == '+' &&
			fnql::input::IsCanonicalCommandSegment(
				command, binding.name ) ) {
			return qtrue;
		}
	}
	return fnql::input::IsCanonicalCommandSegment( command, "+voice" )
		? qtrue
		: qfalse;
}


static void IN_AddCommandBindings( void ) {
	for ( const InputCommandBinding& binding : kInputCommandBindings ) {
		Cmd_AddCommand( binding.name, binding.handler );
	}
}


static void IN_RemoveCommandBindings( void ) {
	for ( const InputCommandBinding& binding : kInputCommandBindings ) {
		Cmd_RemoveCommand( binding.name );
	}
}


//==========================================================================


/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/
static void CL_AdjustAngles( void ) {
	float	speed;

	if ( in_speed.active ) {
		speed = 0.001 * cls.gameFrametime * cl_anglespeedkey->value;
	} else {
		speed = 0.001 * cls.gameFrametime;
	}

	if ( !in_strafe.active ) {
		CL_AddViewAngleDelta(
			YAW, -speed * cl_yawspeed->value * CL_KeyState( &in_right ) );
		CL_AddViewAngleDelta(
			YAW, speed * cl_yawspeed->value * CL_KeyState( &in_left ) );
	}

	CL_AddViewAngleDelta(
		PITCH, -speed * cl_pitchspeed->value * CL_KeyState( &in_lookup ) );
	CL_AddViewAngleDelta(
		PITCH, speed * cl_pitchspeed->value * CL_KeyState( &in_lookdown ) );
}


/*
================
CL_KeyMove

Sets the usercmd_t based on key states
================
*/
static void CL_KeyMove( usercmd_t *cmd ) {
	int		movespeed;
	int		forward, side, up;

	//
	// adjust for speed key / running
	// the walking flag is to keep animations consistent
	// even during acceleration and deceleration
	//
	if ( in_speed.active != ( cl_run->integer != 0 ) ) {
		movespeed = 127;
		cmd->buttons &= ~BUTTON_WALKING;
	} else {
		cmd->buttons |= BUTTON_WALKING;
		movespeed = 64;
	}

	forward = 0;
	side = 0;
	up = 0;
	if ( in_strafe.active ) {
		side = CL_SafeMoveValue(
			side, movespeed * CL_KeyState( &in_right ) );
		side = CL_SafeMoveValue(
			side, -movespeed * CL_KeyState( &in_left ) );
	}

	side = CL_SafeMoveValue(
		side, movespeed * CL_KeyState( &in_moveright ) );
	side = CL_SafeMoveValue(
		side, -movespeed * CL_KeyState( &in_moveleft ) );


	up = CL_SafeMoveValue(
		up, movespeed * CL_KeyState( &in_up ) );
	up = CL_SafeMoveValue(
		up, -movespeed * CL_KeyState( &in_down ) );

	forward = CL_SafeMoveValue(
		forward, movespeed * CL_KeyState( &in_forward ) );
	forward = CL_SafeMoveValue(
		forward, -movespeed * CL_KeyState( &in_back ) );

	cmd->forwardmove = ClampCharMove( forward );
	cmd->rightmove = ClampCharMove( side );
	cmd->upmove = ClampCharMove( up );
}


/*
=================
CL_MouseEvent
=================
*/
void CL_MouseEvent( int dx, int dy /*, int time*/ ) {
	if ( Key_GetCatcher() & KEYCATCH_CONSOLE ) {
		Con_MouseEvent( dx, dy );
	} else if ( Key_GetCatcher() & ( KEYCATCH_BROWSER | KEYCATCH_UI | KEYCATCH_CGAME ) ) {
		// SE_MOUSE is a relative gameplay lane. An absolute owner may have been
		// selected by an earlier queued key event (for example Escape) after the
		// platform already queued this delta; never reinterpret it as a position.
		return;
	} else if ( !CL_AdvertisementBridge_IsDelayElapsed() ) {
		return;
	} else if ( Cvar_VariableIntegerValue( "cg_ignoreMouseInput" ) ) {
		return;
	} else if ( ( Key_GetCatcher() & ~KEYCATCH_RETAIL_MOUSEPASS ) == 0 ) {
		cl.mouseDx[cl.mouseIndex] =
			fnql::input::SaturatingAddInt( cl.mouseDx[cl.mouseIndex], dx );
		cl.mouseDy[cl.mouseIndex] =
			fnql::input::SaturatingAddInt( cl.mouseDy[cl.mouseIndex], dy );
	}
}


/*
=================
CL_ProjectDrawableToRetailModule

Platform producers project into renderer drawable pixels, which is the space the
console and the WebUI browser address directly. A supersampling renderer however
enlarges that private target, and CL_CopyRetailGlconfig deliberately keeps
handing native retail modules the public capture dimensions instead. _UI_MouseEvent
and CG_MouseEvent divide the position by their own glconfig, and the retail UI
discards any result that leaves 640x480, so an unconverted position makes an
in-game menu track at double speed and then stop responding entirely outside the
top-left quadrant.

Convert into the space the module was told it owns. Bytecode modules read the
private dimensions through CL_GetGlconfig and draw through the unscaled syscall
lane, so they keep the drawable position; dllExports is the same retail-native
discriminator the rest of the client uses. The projection is an identity whenever
the renderer is not supersampling, and truncating keeps a position strictly
inside the drawable strictly inside the module's space, so the retail UI's
upper-bound test cannot reject the last row and column.
=================
*/
static void CL_ProjectDrawableToRetailModule( const vm_t *vm, int *x, int *y ) {
	fnql::input::PointerProjection projection;
	fnql::input::PointerPosition position;

	if ( !vm || !vm->dllExports ) {
		return;
	}

	projection.hostWidth = cls.glconfig.vidWidth;
	projection.hostHeight = cls.glconfig.vidHeight;
	projection.drawableWidth = cls.captureWidth;
	projection.drawableHeight = cls.captureHeight;

	position = fnql::input::ProjectPointerToDrawable( *x, *y, projection );
	*x = position.x;
	*y = position.y;
}


/*
=================
CL_MouseAbsoluteEvent

Dispatch a renderer-drawable pointer position without converting it into a
gameplay delta. Every platform producer projects from its host-window space
before queueing this event.
=================
*/
void CL_MouseAbsoluteEvent( int x, int y ) {
	if ( Key_GetCatcher() & KEYCATCH_CONSOLE ) {
		Con_SetMousePos( x, y );
	} else if ( Key_GetCatcher() & KEYCATCH_BROWSER ) {
		CL_WebView_OnMouseMove( x, y );
	} else if ( !CL_AdvertisementBridge_IsDelayElapsed() ) {
		return;
	} else if ( Cvar_VariableIntegerValue( "cg_ignoreMouseInput" ) ) {
		return;
	} else if ( Key_GetCatcher() & KEYCATCH_UI ) {
		if ( uivm ) {
			CL_ProjectDrawableToRetailModule( uivm, &x, &y );
			VM_Call( uivm, 2, UI_MOUSE_EVENT, x, y );
		}
	} else if ( Key_GetCatcher() & KEYCATCH_CGAME ) {
		if ( cgvm ) {
			CL_ProjectDrawableToRetailModule( cgvm, &x, &y );
			VM_Call( cgvm, 2, CG_MOUSE_EVENT, x, y );
		}
	}
}


/*
=================
CL_JoystickEvent

Joystick values stay set until changed
=================
*/
void CL_JoystickEvent( int axis, int value, int time ) {
	if ( axis < 0 || axis >= MAX_JOYSTICK_AXIS ) {
		// A corrupt journal or third-party platform producer must not turn a
		// local input sample into a client disconnect.
		Com_DPrintf( "CL_JoystickEvent: ignoring invalid axis %i\n", axis );
		return;
	}

	// Every supported producer is ultimately a signed 16-bit device axis
	// (retail movement producers use the narrower -127..127 subset).
	cl.joystickAxis[axis] = std::clamp( value, -32768, 32767 );
}


/*
=================
CL_JoystickMove
=================
*/
static void CL_JoystickMove( usercmd_t *cmd ) {
	//int		movespeed;
	float	anglespeed;

	if ( in_speed.active != ( cl_run->integer != 0 ) ) {
		//movespeed = 2;
	} else {
		//movespeed = 1;
		cmd->buttons |= BUTTON_WALKING;
	}

	if ( in_speed.active ) {
		anglespeed = 0.001 * cls.gameFrametime * cl_anglespeedkey->value;
	} else {
		anglespeed = 0.001 * cls.gameFrametime;
	}

	if ( !in_strafe.active ) {
		CL_AddViewAngleDelta( YAW,
			anglespeed * cl_yawspeed->value *
			cl.joystickAxis[AXIS_SIDE] );
	} else {
		cmd->rightmove = ClampCharMove( CL_SafeMoveValue(
			cmd->rightmove, static_cast<float>(
				cl.joystickAxis[AXIS_SIDE] ) ) );
	}

	if ( in_mlook.active ) {
		CL_AddViewAngleDelta( PITCH,
			anglespeed * cl_pitchspeed->value *
			cl.joystickAxis[AXIS_FORWARD] );
	} else {
		cmd->forwardmove = ClampCharMove( CL_SafeMoveValue(
			cmd->forwardmove, static_cast<float>(
				cl.joystickAxis[AXIS_FORWARD] ) ) );
	}

	cmd->upmove = ClampCharMove( CL_SafeMoveValue(
		cmd->upmove, static_cast<float>( cl.joystickAxis[AXIS_UP] ) ) );
}


static void CL_UpdateMouseAccelDebugLog( bool enabled ) {
	if ( !enabled ) {
		mouseAccelDebugLog.reset();
		mouseAccelDebugOpenFailed = false;
		return;
	}

	if ( mouseAccelDebugLog || mouseAccelDebugOpenFailed ) {
		return;
	}

	mouseAccelDebugLog.reset( FS_FOpenFileWrite( "mouse.log" ) );
	if ( !mouseAccelDebugLog ) {
		mouseAccelDebugOpenFailed = true;
		Com_Printf( S_COLOR_YELLOW "Could not open mouse.log for cl_mouseAccelDebug\n" );
		return;
	}

	static constexpr char header[] = "mx my frame_msec rate power\n";
	FS_Write( header, static_cast<int>( sizeof( header ) - 1 ), mouseAccelDebugLog.get() );
}


static void CL_LogRetailMouseMotion(
	const fnql::input::RetailMouseMotion& motion ) {
	if ( !mouseAccelDebugLog ) {
		return;
	}

	std::array<char, 192> line{};
	const int length = Com_sprintf(
		line.data(), static_cast<int>( line.size() ), "%g %g %u %g %g\n",
		motion.sampleX, motion.sampleY, frame_msec, motion.rate,
		motion.accelerationExponent );
	if ( length > 0 ) {
		FS_Write( line.data(), std::min( length, static_cast<int>( line.size() - 1 ) ),
			mouseAccelDebugLog.get() );
	}
}


/*
=================
CL_RetailMouseMove

Independently implements the retail Quake Live gameplay transform while the
legacy styles remain available through cl_mouseAccelStyle 0 and 1.
=================
*/
static void CL_RetailMouseMove( usercmd_t *cmd ) {
	const float rawX = static_cast<float>( cl.mouseDx[cl.mouseIndex] );
	const float rawY = static_cast<float>( cl.mouseDy[cl.mouseIndex] );

	cl.mouseIndex ^= 1;
	cl.mouseDx[cl.mouseIndex] = 0;
	cl.mouseDy[cl.mouseIndex] = 0;

	CL_UpdateMouseAccelDebugLog( cl_mouseAccelDebug->integer != 0 );
	const int filterSamples = std::clamp(
		m_filter->integer, 0, fnql::input::kRetailMouseFilterMaximum );
	if ( rawX == 0.0f && rawY == 0.0f && filterSamples == 0 ) {
		retailMouseFilter.Reset( { cl.viewangles[YAW], cl.viewangles[PITCH] } );
		return;
	}

	fnql::input::RetailMouseParameters parameters;
	parameters.sensitivity = cl_sensitivity->value;
	parameters.acceleration = cl_mouseAccel->value;
	parameters.accelerationOffset = cl_mouseAccelOffset->value;
	parameters.accelerationPower = cl_mouseAccelPower->value;
	parameters.sensitivityCap = cl_mouseSensCap->value;
	parameters.countsPerInch = m_cpi->value;
	parameters.frameMilliseconds = static_cast<int>( frame_msec );
	const fnql::input::RetailMouseMotion motion =
		fnql::input::TranslateRetailMouseMotion( rawX, rawY, parameters );

	CL_LogRetailMouseMotion( motion );
	if ( cl_showMouseRate->integer && cl_mouseAccel->value != 0.0f ) {
		Com_Printf( "rate: %g, power: %g, sensitivity: %g\n",
			motion.rate, motion.accelerationExponent, motion.sensitivity );
	}

	fnql::input::ViewAngles view = retailMouseFilter.Begin(
		{
			fnql::input::FiniteOr( cl.viewangles[YAW], 0.0f ),
			fnql::input::FiniteOr( cl.viewangles[PITCH], 0.0f )
		},
		filterSamples );
	cl.viewangles[YAW] = fnql::input::FiniteOr( view.yaw, 0.0f );
	cl.viewangles[PITCH] = fnql::input::FiniteOr( view.pitch, 0.0f );

	const float mx = motion.x * cl.cgameSensitivity;
	const float my = motion.y * cl.cgameSensitivity;
	const float viewAxisMultiplier =
		fnql::input::RetailMouseAxisMultiplier( parameters.countsPerInch );

	if ( in_strafe.active ) {
		cmd->rightmove = ClampCharMove( CL_SafeMoveValue(
			cmd->rightmove, m_side->value * mx ) );
	} else {
		CL_AddViewAngleDelta(
			YAW, -m_yaw->value * viewAxisMultiplier * mx );
	}

	if ( ( in_mlook.active || cl_freelook->integer ) && !in_strafe.active ) {
		CL_AddViewAngleDelta(
			PITCH, m_pitch->value * viewAxisMultiplier * my );
	} else {
		cmd->forwardmove = ClampCharMove( CL_SafeMoveValue(
			cmd->forwardmove, -m_forward->value * my ) );
	}

	const fnql::input::ViewAngles unfiltered{
		cl.viewangles[YAW], cl.viewangles[PITCH]
	};
	view = retailMouseFilter.End( unfiltered );
	cl.viewangles[YAW] =
		fnql::input::FiniteOr( view.yaw, unfiltered.yaw );
	cl.viewangles[PITCH] =
		fnql::input::FiniteOr( view.pitch, unfiltered.pitch );
}


/*
=================
CL_MouseMove
=================
*/
static void CL_MouseMove( usercmd_t *cmd )
{
	float mx, my;

	if ( cl_mouseAccelStyle->integer == 2 ) {
		CL_RetailMouseMove( cmd );
		return;
	}

	CL_UpdateMouseAccelDebugLog( false );
	retailMouseFilter.Reset( { cl.viewangles[YAW], cl.viewangles[PITCH] } );

	// allow mouse smoothing
	if (m_filter->integer)
	{
		// Convert before adding: either accumulator can legitimately saturate
		// under an event flood, and signed-int addition would otherwise overflow.
		mx = ( static_cast<float>( cl.mouseDx[0] ) +
			static_cast<float>( cl.mouseDx[1] ) ) * 0.5f;
		my = ( static_cast<float>( cl.mouseDy[0] ) +
			static_cast<float>( cl.mouseDy[1] ) ) * 0.5f;
	}
	else
	{
		mx = cl.mouseDx[cl.mouseIndex];
		my = cl.mouseDy[cl.mouseIndex];
	}

	cl.mouseIndex ^= 1;
	cl.mouseDx[cl.mouseIndex] = 0;
	cl.mouseDy[cl.mouseIndex] = 0;

	if (mx == 0.0f && my == 0.0f)
		return;

	if ( cl_mouseAccel->value != 0.0f )
	{
		if ( cl_mouseAccelStyle->integer == 0 )
		{
			float accelSensitivity;
			float rate;

			rate = sqrt(mx * mx + my * my) / static_cast<float>( frame_msec );

			accelSensitivity = cl_sensitivity->value + rate * cl_mouseAccel->value;
			mx *= accelSensitivity;
			my *= accelSensitivity;

			if ( cl_showMouseRate->integer )
				Com_Printf( "rate: %f, accelSensitivity: %f\n", rate, accelSensitivity );
		}
		else
		{
			std::array<float, 2> rate;
			std::array<float, 2> power;
			float offset = cl_mouseAccelOffset->value;

			// clip at a small positive number to avoid division
			// by zero (or indeed going backwards!)
			if ( offset < 0.001f ) {
				offset = 0.001f;
			}

			// sensitivity remains pretty much unchanged at low speeds
			// cl_mouseAccel is a power value to how the acceleration is shaped
			// cl_mouseAccelOffset is the rate for which the acceleration will have doubled the non accelerated amplification
			// NOTE: decouple the config cvars for independent acceleration setup along X and Y?

			rate[0] = fabsf( mx ) / static_cast<float>( frame_msec );
			rate[1] = fabsf( my ) / static_cast<float>( frame_msec );
			power[0] = powf( rate[0] / offset, cl_mouseAccel->value );
			power[1] = powf( rate[1] / offset, cl_mouseAccel->value );

			mx = cl_sensitivity->value * (mx + ((mx < 0) ? -power[0] : power[0]) * offset);
			my = cl_sensitivity->value * (my + ((my < 0) ? -power[1] : power[1]) * offset);

			if(cl_showMouseRate->integer)
				Com_Printf("ratex: %f, ratey: %f, powx: %f, powy: %f\n", rate[0], rate[1], power[0], power[1]);
		}
	}
	else
	{
		mx *= cl_sensitivity->value;
		my *= cl_sensitivity->value;
	}

	// ingame FOV
	mx *= cl.cgameSensitivity;
	my *= cl.cgameSensitivity;

	// add mouse X/Y movement to cmd
	if ( in_strafe.active )
		cmd->rightmove = ClampCharMove( CL_SafeMoveValue(
			cmd->rightmove, m_side->value * mx ) );
	else
		CL_AddViewAngleDelta( YAW, -m_yaw->value * mx );

	if ( (in_mlook.active || cl_freelook->integer) && !in_strafe.active )
		CL_AddViewAngleDelta( PITCH, m_pitch->value * my );
	else
		cmd->forwardmove = ClampCharMove( CL_SafeMoveValue(
			cmd->forwardmove, -m_forward->value * my ) );
}


/*
==============
CL_CmdButtons
==============
*/
static void CL_CmdButtons( usercmd_t *cmd ) {
	const int catcher = Key_GetCatcher();
	const bool gameplayInputCaptured = fnql::input::CatcherBlocksGameplayInput(
		catcher, KEYCATCH_RETAIL_MOUSEPASS );

	//
	// figure button bits
	// send a button bit even if the key was pressed and released in
	// less than a frame
	//
	for ( std::size_t i = 0; i < in_buttons.size(); i++ ) {
		if ( in_buttons[i].active || in_buttons[i].wasPressed ) {
			cmd->buttons |= 1 << static_cast<int>( i );
		}
		in_buttons[i].wasPressed = false;
	}

	// BUTTON_TALK makes retail game code discard movement.  The retail
	// mouse-pass catcher used by held HUD overlays must remain transparent.
	if ( gameplayInputCaptured ) {
		cmd->buttons |= BUTTON_TALK;
	}

	// allow the game to know if any key at all is
	// currently pressed, even if it isn't bound to anything
	if ( anykeydown && !gameplayInputCaptured ) {
		cmd->buttons |= BUTTON_ANY;
	}
}


/*
=================
CL_ResetInputState

Ordered system-event barrier used for focus, window, and queue-overflow
recovery. Key_ClearStates is the logical-key barrier; this full barrier also
clears explicit/manual voice ownership and persistent motion state so input
sampled before it cannot affect a later frame.
=================
*/
void CL_ResetInputState( void ) {
	Key_ClearStates();
	CL_ResetVoiceInputState();

	for ( int i = 0; i < 2; ++i ) {
		cl.mouseDx[i] = 0;
		cl.mouseDy[i] = 0;
	}
	for ( int& axis : cl.joystickAxis ) {
		axis = 0;
	}
	retailMouseFilter.Reset( { cl.viewangles[YAW], cl.viewangles[PITCH] } );
}


/*
=================
CL_ResetMouseInputState

Narrow recovery for loss in a mouse-only device buffer. Releasing the mouse
range prevents a missing button-up from holding a binding, without disrupting
keyboard movement or persistent joystick axes that the failed device cannot
reconstruct. Backends that expose higher mouse buttons through K_AUX pass an
ordered source-state mask with the reset so those bindings are balanced too.
=================
*/
void CL_ResetMouseInputState( unsigned int auxiliaryKeyMask ) {
	for ( int key = K_MOUSE1; key <= K_MWHEELUP; ++key ) {
		if ( keys[key].down ) {
			CL_KeyEvent( key, qfalse, 0 );
		}
	}
	for ( unsigned int bit = 0; bit < 16; ++bit ) {
		if ( ( auxiliaryKeyMask & ( 1u << bit ) ) &&
			keys[K_AUX1 + bit].down ) {
			CL_KeyEvent( K_AUX1 + bit, qfalse, 0 );
		}
	}

	// Invalidate old commands even when the logical key is already up: its
	// deferred press may still be waiting in the command buffer.
	for ( int key = K_MOUSE1; key <= K_MWHEELUP; ++key ) {
		Key_AdvanceBindingGeneration( key );
		IN_RemoveCommandInputSource( key );
		CL_RemoveVoiceInputSource( key );
	}
	for ( unsigned int bit = 0; bit < 16; ++bit ) {
		if ( auxiliaryKeyMask & ( 1u << bit ) ) {
			const int key = K_AUX1 + static_cast<int>( bit );
			Key_AdvanceBindingGeneration( key );
			IN_RemoveCommandInputSource( key );
			CL_RemoveVoiceInputSource( key );
		}
	}

	for ( int i = 0; i < 2; ++i ) {
		cl.mouseDx[i] = 0;
		cl.mouseDy[i] = 0;
	}
	retailMouseFilter.Reset( { cl.viewangles[YAW], cl.viewangles[PITCH] } );
}


/*
==============
CL_FinishMove
==============
*/
static void CL_FinishMove( usercmd_t *cmd ) {
	int		i;

	// copy the state that the cgame is currently sending
	cmd->weapon = cl.cgameUserCmdValue;
	cmd->weaponPrimary = cl.cgameUserCmdPrimary;
	cmd->fov = cl.cgameUserCmdFov;

	// send the current server time so the amount of movement
	// can be determined without allowing cheating
	cmd->serverTime = cl.serverTime;

	for (i=0 ; i<3 ; i++) {
		const float safeAngle = fnql::input::FiniteAngleForShort(
			fnql::input::FiniteOr( cl.viewangles[i], 0.0f ) );
		// Keep the reduced value as the next frame's accumulator. Retaining a
		// pathological finite extreme would make ordinary look deltas disappear
		// into float precision even though command serialization was safe.
		cl.viewangles[i] = safeAngle;
		cmd->angles[i] = ANGLE2SHORT( safeAngle );
	}
}


/*
=================
CL_CreateCmd
=================
*/
static usercmd_t CL_CreateCmd( void ) {
	usercmd_t	cmd{};
	vec3_t		oldAngles;
	bool		pitchLimited = false;

	VectorCopy( cl.viewangles, oldAngles );

	// keyboard angle adjustment
	CL_AdjustAngles ();

	CL_CmdButtons( &cmd );

	// get basic movement from keyboard
	CL_KeyMove( &cmd );

	// get basic movement from mouse
	CL_MouseMove( &cmd );

	// get basic movement from joystick
	CL_JoystickMove( &cmd );

	// check to make sure the angles haven't wrapped
	if ( cl.viewangles[PITCH] - oldAngles[PITCH] > 90 ) {
		cl.viewangles[PITCH] = oldAngles[PITCH] + 90;
		pitchLimited = true;
	} else if ( oldAngles[PITCH] - cl.viewangles[PITCH] > 90 ) {
		cl.viewangles[PITCH] = oldAngles[PITCH] - 90;
		pitchLimited = true;
	}

	// store out the final values
	CL_FinishMove( &cmd );

	// Mouse filtering keeps a private unfiltered base. Retain legitimate look
	// changes made after its End() step, but discard the history when the pitch
	// guard rejected an excessive sample. Translating that hidden overshoot
	// would replay it as another 90-degree step on subsequent commands.
	const fnql::input::ViewAngles finalView{
		cl.viewangles[YAW], cl.viewangles[PITCH]
	};
	if ( pitchLimited ) {
		retailMouseFilter.Reset( finalView );
	} else {
		retailMouseFilter.Synchronize( finalView );
	}

	// draw debug graphs of turning for mouse testing
	if ( cl_debugMove->integer ) {
		if ( cl_debugMove->integer == 1 ) {
			SCR_DebugGraph( fabsf( cl.viewangles[YAW] - oldAngles[YAW] ) );
		} else if ( cl_debugMove->integer == 2 ) {
			SCR_DebugGraph( fabsf( cl.viewangles[PITCH] - oldAngles[PITCH] ) );
		}
	}

	return cmd;
}


/*
=================
CL_CreateNewCommands

Create a new usercmd_t structure for this frame
=================
*/
static void CL_CreateNewCommands( void ) {
	int			cmdNum;

	// no need to create usercmds until we have a gamestate
	if ( cls.state < CA_PRIMED ) {
		CL_SuspendUsercmdInputSampling();
		return;
	}

	frame_msec = static_cast<unsigned>( com_frameTime ) - old_com_frameTime;

	// if running over 1000fps, act as if each frame is 1ms
	// prevents divisions by zero
	if ( frame_msec < 1 ) {
		frame_msec = 1;
	}

	// if running less than 5fps, truncate the extra time to prevent
	// unexpected moves after a hitch
	if ( frame_msec > 200 ) {
		frame_msec = 200;
	}
	old_com_frameTime = static_cast<unsigned>( com_frameTime );


	// generate a command for this frame
	cl.cmdNumber = fnql::net::NextCounter( cl.cmdNumber );
	cmdNum = cl.cmdNumber & CMD_MASK;
	cl.cmds[cmdNum] = CL_CreateCmd();
}


/*
=================
CL_ReadyToSendPacket

Returns qfalse if we are over the maxpackets limit
and should choke back the bandwidth a bit by not sending
a packet this frame.  All the commands will still get
delivered in the next packet, but saving a header and
getting more delta compression will reduce total bandwidth.
=================
*/
static bool CL_ReadyToSendPacket( void ) {
	int		oldPacketNum;
	int		delta;

	// don't send anything if playing back a demo
	if ( clc.demoplaying || cls.state == CA_CINEMATIC ) {
		return false;
	}

	// If we are downloading, we send no less than 50ms between packets
	if ( *clc.downloadTempName && cls.realtime - clc.lastPacketSentTime < 50 ) {
		return false;
	}

	// if we don't have a valid gamestate yet, only send
	// one packet a second
	if ( cls.state != CA_ACTIVE &&
		cls.state != CA_PRIMED &&
		!*clc.downloadTempName &&
		cls.realtime - clc.lastPacketSentTime < 1000 ) {
		return false;
	}

	// send every frame for loopbacks
	if ( clc.netchan.remoteAddress.type == NA_LOOPBACK ) {
		return true;
	}

	// send every frame for LAN
	if ( cl_lanForcePackets->integer && clc.netchan.isLANAddress ) {
		return true;
	}

	oldPacketNum = (clc.netchan.outgoingSequence - 1) & PACKET_MASK;
	delta = cls.realtime - cl.outPackets[ oldPacketNum ].p_realtime;
	if ( delta < 1000 / cl_maxpackets->integer ) {
		// the accumulated commands will go out in the next packet
		return false;
	}

	return true;
}


#define RETAIL_CLIENT_MESSAGE_FLAG_VIEWANGLE_DELTA	0x20
#define RETAIL_CLIENT_MESSAGE_FLAG_CGAME_IMPORT_GUARD	0x40
#define RETAIL_CLIENT_MESSAGE_FLAG_INITIAL_HIGH_BIT	0x80
#define RETAIL_CLIENT_MESSAGE_RENDERER_NODE_MASK		0x1f
#define RETAIL_CLIENT_MESSAGE_RENDERER_NODE_LIMIT	0x20

static int cl_retailClientMessageFlags = RETAIL_CLIENT_MESSAGE_FLAG_INITIAL_HIGH_BIT;

void CL_SetRetailClientMessageViewangleDeltaFlag( void ) {
	cl_retailClientMessageFlags |= RETAIL_CLIENT_MESSAGE_FLAG_VIEWANGLE_DELTA;
}

void CL_SetRetailClientMessageCGameImportGuardFlag( void ) {
	cl_retailClientMessageFlags |= RETAIL_CLIENT_MESSAGE_FLAG_CGAME_IMPORT_GUARD;
}

void CL_SetRetailClientMessageRendererNodeCount( int nodeCount ) {
	int clampedNodeCount;

	if ( nodeCount < 0 ) {
		clampedNodeCount = 0;
	} else if ( nodeCount > RETAIL_CLIENT_MESSAGE_RENDERER_NODE_LIMIT ) {
		clampedNodeCount = RETAIL_CLIENT_MESSAGE_RENDERER_NODE_LIMIT;
	} else {
		clampedNodeCount = nodeCount;
	}

	cl_retailClientMessageFlags ^= ( cl_retailClientMessageFlags ^ clampedNodeCount ) & RETAIL_CLIENT_MESSAGE_RENDERER_NODE_MASK;
}

static qboolean CL_UseRetailClientMessageSideband( void ) {
	return clc.netchan.wireProfile == NETCHAN_WIRE_QL_RETAIL ? qtrue : qfalse;
}

static int CL_RetailClientMessageFlags( void ) {
	return cl_retailClientMessageFlags;
}


/*
===================
CL_WritePacket

Create and send the command packet to the server
Including both the reliable commands and the usercmds

During normal gameplay, a client packet will contain something like:

4	sequence number
2	qport
4	serverid
4	acknowledged sequence number
4	clc.serverCommandSequence
<optional reliable commands>
1	clc_move or clc_moveNoDelta
1	command count
<count * usercmds>

===================
*/
void CL_WritePacket( int repeat ) {
	msg_t		buf;
	std::array<byte, MAX_MSGLEN_BUF> data;
	int			i, j, n;
	usercmd_t	*cmd, *oldcmd;
	usercmd_t	nullcmd{};
	int			packetNum;
	int			oldPacketNum;
	int			count, key;

	// don't send anything if playing back a demo
	if ( clc.demoplaying || cls.state == CA_CINEMATIC ) {
		return;
	}

	oldcmd = &nullcmd;

	MSG_Init( &buf, data.data(), MAX_MSGLEN );

	MSG_Bitstream( &buf );
	// write the current serverId so the server
	// can tell if this is from the current gameState
	MSG_WriteLong( &buf, cl.serverId );

	// write the last message we received, which can
	// be used for delta compression, and is also used
	// to tell if we dropped a gamestate
	MSG_WriteLong( &buf, clc.serverMessageSequence );

	// write the last reliable message we received
	MSG_WriteLong( &buf, clc.serverCommandSequence );
	if ( CL_UseRetailClientMessageSideband() ) {
		MSG_WriteByte( &buf, CL_RetailClientMessageFlags() ^ ( clc.serverCommandSequence & 0xff ) );
	}

	// write any unacknowledged clientCommands
	std::uint32_t pendingReliable = 0;
	if ( !fnql::net::PendingCounterCount( clc.reliableSequence,
		clc.reliableAcknowledge, pendingReliable ) ||
		pendingReliable > MAX_RELIABLE_COMMANDS ) {
		Com_Error( ERR_DROP, "CL_WritePacket: invalid reliable acknowledgement window" );
		return;
	}
	n = static_cast<int>( pendingReliable );
	for ( i = 0; i < n; i++ ) {
		const int index = fnql::net::CounterAdd( clc.reliableAcknowledge,
			static_cast<std::uint32_t>( i + 1 ) );
		MSG_WriteByte( &buf, clc_clientCommand );
		MSG_WriteLong( &buf, index );
		MSG_WriteStringForWireProfile( &buf,
			clc.reliableCommands[ index & ( MAX_RELIABLE_COMMANDS - 1 ) ],
			clc.netchan.wireProfile );
	}

	// we want to send all the usercmds that were generated in the last
	// few packet, so even if a couple packets are dropped in a row,
	// all the cmds will make it to the server

	oldPacketNum = (clc.netchan.outgoingSequence - 1 - cl_packetdup->integer) & PACKET_MASK;
	std::uint32_t pendingCommands = 0;
	if ( !fnql::net::PendingCounterCount( cl.cmdNumber,
		cl.outPackets[ oldPacketNum ].p_cmdNumber, pendingCommands ) ) {
		Com_Error( ERR_DROP, "CL_WritePacket: invalid usercmd acknowledgement window" );
		return;
	}
	count = static_cast<int>( ( std::min )(
		pendingCommands, static_cast<std::uint32_t>( MAX_PACKET_USERCMDS ) ) );
	if ( pendingCommands > MAX_PACKET_USERCMDS ) {
		count = MAX_PACKET_USERCMDS;
		Com_Printf("MAX_PACKET_USERCMDS\n");
	}
	if ( count >= 1 ) {
		if ( cl_showSend->integer ) {
			Com_Printf( "(%i)", count );
		}

		// begin a client move command
		if ( cl_nodelta->integer || !cl.snap.valid || clc.demowaiting || clc.serverMessageSequence != cl.snap.messageNum ) {
			MSG_WriteByte( &buf, clc_moveNoDelta );
		} else {
			MSG_WriteByte( &buf, clc_move );
		}

		// write the command count
		MSG_WriteByte( &buf, count );

		// use the checksum feed in the key
		key = clc.checksumFeed;
		// also use the message acknowledge
		key ^= clc.serverMessageSequence;
		// also use the last acknowledged server command in the key
		key ^= clc.serverCommandHashes[
			clc.serverCommandSequence & ( MAX_RELIABLE_COMMANDS - 1 ) ];

		// write all the commands, including the predicted command
		const int firstCommand = fnql::net::CounterSubtract(
			cl.cmdNumber, static_cast<std::uint32_t>( count - 1 ) );
		for ( i = 0 ; i < count ; i++ ) {
			j = fnql::net::CounterAdd(
				firstCommand, static_cast<std::uint32_t>( i ) ) & CMD_MASK;
			cmd = &cl.cmds[j];
			MSG_WriteDeltaUsercmdKey (&buf, key, oldcmd, cmd);
			oldcmd = cmd;
		}
	}

	//
	// deliver the message
	//
	packetNum = clc.netchan.outgoingSequence & PACKET_MASK;
	cl.outPackets[ packetNum ].p_realtime = cls.realtime;
	cl.outPackets[ packetNum ].p_serverTime = oldcmd->serverTime;
	cl.outPackets[ packetNum ].p_cmdNumber = cl.cmdNumber;
	clc.lastPacketSentTime = cls.realtime;

	if ( cl_showSend->integer ) {
		Com_Printf( "%i ", buf.cursize );
	}

	MSG_WriteByte( &buf, clc_EOF );

	if ( buf.overflowed ) {
		if ( cls.state >= CA_CONNECTED && cls.state != CA_CINEMATIC ) {
			cls.state = CA_CONNECTING; // to avoid recursive error
		}
		Com_Error( ERR_DROP, "%s: message overflowed", __func__ );
	}

	if ( repeat == 0 || clc.netchan.remoteAddress.type == NA_LOOPBACK ) {
		CL_Netchan_Transmit( &clc.netchan, &buf );
	} else {
		CL_Netchan_Enqueue( &clc.netchan, &buf, repeat + 1 );
		NET_FlushPacketQueue( 0 );
	}
}


/*
=================
CL_SendCmd

Called every frame to builds and sends a command packet to the server.
=================
*/
void CL_SendCmd( void ) {
	// don't send any message if not connected
	if ( cls.state < CA_CONNECTED ) {
		CL_SuspendUsercmdInputSampling();
		return;
	}

	// don't send commands if paused
	if ( com_sv_running->integer && sv_paused->integer && cl_paused->integer ) {
		CL_SuspendUsercmdInputSampling();
		return;
	}

	// we create commands even if a demo is playing,
	CL_CreateNewCommands();

	// don't send a packet if the last packet was sent too recently
	if ( !CL_ReadyToSendPacket() ) {
		if ( cl_showSend->integer ) {
			Com_Printf( ". " );
		}
		return;
	}

	CL_WritePacket( 0 );
}


/*
============
CL_InitInput
============
*/
void CL_InitInput( void ) {
	IN_AddCommandBindings();

	cl_nodelta = Cvar_Get( "cl_nodelta", "0", 0 );
	Cvar_SetDescription( cl_nodelta, "Flag server to disable delta compression on server snapshots." );
	cl_debugMove = Cvar_Get( "cl_debugMove", "0", 0 );
	Cvar_CheckRange( cl_debugMove, "0", "2", CV_INTEGER );
	Cvar_SetDescription( cl_debugMove, "Prints a graph of view angle deltas.\n 0: Disabled\n 1: Yaw\n 2: Pitch" );

	cl_showSend = Cvar_Get( "cl_showSend", "0", CVAR_TEMP );
	Cvar_SetDescription( cl_showSend, "Prints client to server packet information." );

	cl_yawspeed = Cvar_Get( "cl_yawspeed", "140", CVAR_ARCHIVE_ND | CVAR_CHEAT );
	Cvar_SetDescription( cl_yawspeed, "Side-to-side turning speed using keys (+left and +right)." );
	cl_pitchspeed = Cvar_Get( "cl_pitchspeed", "140", CVAR_ARCHIVE_ND | CVAR_CHEAT );
	Cvar_SetDescription( cl_pitchspeed, "Up and down pitching speed using keys (+lookup and +lookdown)." );
	cl_anglespeedkey = Cvar_Get( "cl_anglespeedkey", "1.5", CVAR_CHEAT );
	Cvar_SetDescription( cl_anglespeedkey, "Set the speed that the direction keys (not mouse) change the view angle." );

	cl_maxpackets = Cvar_Get( "cl_maxpackets", "125", CVAR_ARCHIVE | CVAR_CHEAT );
	Cvar_CheckRange( cl_maxpackets, "15", "125", CV_INTEGER );
	Cvar_SetDescription( cl_maxpackets, "Set how many client packets are sent to the server per second, can't exceed \\com_maxFPS." );
	cl_packetdup = Cvar_Get( "cl_packetdup", "1", CVAR_ARCHIVE_ND | CVAR_CLOUD );
	Cvar_CheckRange( cl_packetdup, "0", "5", CV_INTEGER );
	Cvar_SetDescription( cl_packetdup, "Limits the number of previous client commands added in packet, helps in packet loss mitigation, increases client command packets size a bit." );

	cl_run = Cvar_Get( "cl_run", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( cl_run, "Persistent player running movement." );
	cl_sensitivity = Cvar_Get( "sensitivity", "2", CVAR_ARCHIVE );
	Cvar_CheckRange( cl_sensitivity, "0.1", "10", CV_FLOAT );
	Cvar_SetDescription( cl_sensitivity, "Sets base mouse sensitivity (mouse speed)." );
	Cvar_SetDescription( Cvar_Get( "cg_ignoreMouseInput", "0", CVAR_ROM ), "Read-only Quake Live cgame/UI bridge flag that blocks gameplay mouse deltas while retained overlays own input." );
	cl_mouseAccel = Cvar_Get( "cl_mouseAccel", "0",
		CVAR_ARCHIVE_ND | CVAR_PROTECTED | CVAR_CLOUD );
	Cvar_SetDescription( cl_mouseAccel, "Mouse acceleration amount. Negative values select Quake Live deceleration when the retail input style is active." );
	cl_mouseAccelDebug = Cvar_Get( "cl_mouseAccelDebug", "0", CVAR_TEMP );
	Cvar_SetDescription( cl_mouseAccelDebug, "Write bounded Quake Live mouse-transform diagnostics to mouse.log." );
	cl_freelook = Cvar_Get( "cl_freelook", "1",
		CVAR_ARCHIVE_ND | CVAR_PROTECTED | CVAR_CLOUD );
	Cvar_SetDescription( cl_freelook, "Allow pitching or up/down look with mouse." );

	// 0: legacy mouse acceleration
	// 1: ioquake3 power implementation
	// 2: retail Quake Live CPI/acceleration/filter implementation
	cl_mouseAccelStyle = Cvar_Get( "cl_mouseAccelStyle", "2", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( cl_mouseAccelStyle, "0", "2", CV_INTEGER );
	Cvar_SetDescription( cl_mouseAccelStyle,
		"Mouse transform profile:\n"
		" 0 - classic FnQ3/ioquake3 acceleration and delta filter\n"
		" 1 - ioquake3 power acceleration and delta filter\n"
		" 2 - retail Quake Live CPI, signed acceleration, cap, and view filter" );
	// offset for the power function (for style 1, ignored otherwise)
	// this should be set to the max rate value
	cl_mouseAccelOffset = Cvar_Get( "cl_mouseAccelOffset", "0",
		CVAR_ARCHIVE | CVAR_PROTECTED | CVAR_CLOUD );
	Cvar_CheckRange( cl_mouseAccelOffset, "0", "50000", CV_FLOAT );
	Cvar_SetDescription( cl_mouseAccelOffset, "Acceleration rate offset used by ioquake3 power and retail Quake Live mouse profiles." );
	cl_mouseAccelPower = Cvar_Get( "cl_mouseAccelPower", "2",
		CVAR_ARCHIVE | CVAR_PROTECTED | CVAR_CLOUD );
	Cvar_CheckRange( cl_mouseAccelPower, "0", "16", CV_FLOAT );
	Cvar_SetDescription( cl_mouseAccelPower, "Quake Live mouse acceleration exponent before the retail power-minus-one transform." );
	cl_mouseSensCap = Cvar_Get( "cl_mouseSensCap", "0", CVAR_ARCHIVE | CVAR_CLOUD );
	Cvar_CheckRange( cl_mouseSensCap, "0", "1000", CV_FLOAT );
	Cvar_SetDescription( cl_mouseSensCap, "Optional upper sensitivity cap for the retail Quake Live mouse profile; zero disables the cap." );
	cl_viewAccel = Cvar_Get( "cl_viewAccel", "1.7", CVAR_ARCHIVE | CVAR_CLOUD );
	Cvar_CheckRange( cl_viewAccel, "0", "8", CV_FLOAT );
	Cvar_SetDescription( cl_viewAccel, "Quake Live legacy joystick look acceleration exponent." );

	cl_showMouseRate = Cvar_Get( "cl_showMouseRate", "0", 0 );
	Cvar_SetDescription( cl_showMouseRate, "Prints mouse acceleration info when 'cl_mouseAccel' has a value set (rate of mouse samples per frame)." );

	m_pitch = Cvar_Get( "m_pitch", "0.022", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( m_pitch, "Set the up and down movement distance of the player in relation to how much the mouse moves." );
	m_yaw = Cvar_Get( "m_yaw", "0.022", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( m_yaw, "Set the speed at which the player's screen moves left and right while using the mouse." );
	m_forward = Cvar_Get( "m_forward", "0.25", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( m_forward, "Set the back and forth movement distance of the player in relation to how much the mouse moves." );
	m_side = Cvar_Get( "m_side", "0.25", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( m_side, "Set the strafe movement distance of the player in relation to how much the mouse moves." );
#ifdef MACOS_X
	// Input is jittery on OS X w/o this
	m_filter = Cvar_Get( "m_filter", "1", CVAR_ARCHIVE_ND );
#else
	m_filter = Cvar_Get( "m_filter", "0", CVAR_ARCHIVE_ND );
#endif
	Cvar_CheckRange( m_filter, "0", "31", CV_INTEGER );
	Cvar_SetDescription( m_filter, "Mouse smoothing strength. Styles 0/1 use the legacy two-delta average; style 2 averages 1-31 completed view angles like retail Quake Live." );
	m_cpi = Cvar_Get( "m_cpi", "0", CVAR_ARCHIVE | CVAR_PROTECTED | CVAR_CLOUD );
	Cvar_CheckRange( m_cpi, "0", "100000", CV_FLOAT );
	Cvar_SetDescription( m_cpi, "Physical mouse counts per inch for Quake Live input normalization; zero preserves count-based input." );
}


/*
============
CL_ClearInput
============
*/
void CL_ClearInput( void ) {
	mouseAccelDebugLog.reset();
	mouseAccelDebugOpenFailed = false;
	retailMouseFilter.Reset();
	IN_RemoveCommandBindings();
}
