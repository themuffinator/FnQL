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
** linux_joystick.c
**
** This file contains ALL Linux specific stuff having to do with the
** Joystick input.  When a port is being made the following functions
** must be implemented by the port:
**
** Authors: mkv, bk
**
*/

#ifdef USE_JOYSTICK

#include <linux/joystick.h>
#include <cerrno>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>  // bk001204


#include "../client/client.h"
#include "../client/input_compat.hpp"
#include "linux_local.h"

/* We translate axes movement into keypresses. */
int joy_keys[16] = {
     K_LEFTARROW, K_RIGHTARROW,
     K_UPARROW, K_DOWNARROW,
     K_JOY16, K_JOY17,
     K_JOY18, K_JOY19,
     K_JOY20, K_JOY21,
     K_JOY22, K_JOY23,

     K_JOY24, K_JOY25,
     K_JOY26, K_JOY27
};

/* Our file descriptor for the joystick device. */
static int             joy_fd = -1;
static constexpr int kDigitalAxisCount =
  static_cast<int>( ARRAY_LEN( joy_keys ) ) / 2;
static constexpr int kRawButtonCount = K_JOY16 - K_JOY1 + 1;
static constexpr int kJoystickDeviceCount = 4;
static constexpr std::uint32_t kJoystickRetryIntervalMilliseconds = 1000u;
static int axes_state[kDigitalAxisCount];
static unsigned int old_axes;
static std::uint32_t joystick_last_open_attempt;
static qboolean joystick_open_attempted;
static qboolean joystick_unavailable_reported;


// bk001130 - from linux_glimp.c
extern cvar_t *  in_joystick;
extern cvar_t *  in_joystickDebug;
extern cvar_t *  joy_threshold;


/**********************************************/
/* Joystick routines.                         */
/**********************************************/
static void IN_CloseJoystickDevice( qboolean queueReset )
{
  if( joy_fd != -1 ) {
    close( joy_fd );
    joy_fd = -1;
  }

  memset( axes_state, 0, sizeof( axes_state ) );
  IN_ResetJoystickState();

  if( queueReset ) {
    X11_QueueInputReset( qtrue );
  }
}


void IN_ResetJoystickState( void )
{
  // Keep the latest physical axis samples so a held direction is rebuilt on
  // the next poll, but forget which digital transitions were already emitted.
  old_axes = 0;
}


static qboolean IN_JoystickRetryDue( std::uint32_t now )
{
  // Unsigned elapsed-time arithmetic remains correct across the millisecond
  // counter's wrap as long as the interval is less than half its range.
  return ( !joystick_open_attempted ||
    now - joystick_last_open_attempt >=
      kJoystickRetryIntervalMilliseconds ) ? qtrue : qfalse;
}


static void IN_ScheduleJoystickRetry( std::uint32_t now )
{
  joystick_last_open_attempt = now;
  joystick_open_attempted = qtrue;
}


static qboolean IN_TryOpenJoystick(
  std::uint32_t now, qboolean reportUnavailable )
{
  static const char *const devicePathFormats[] = {
    "/dev/input/js%d",
    "/dev/js%d"
  };

  IN_ScheduleJoystickRetry( now );
  for( const char *pathFormat : devicePathFormats ) {
    for( int i = 0; i < kJoystickDeviceCount; ++i ) {
      char filename[PATH_MAX];

      snprintf( filename, sizeof( filename ), pathFormat, i );
      joy_fd = open( filename, O_RDONLY | O_NONBLOCK | O_CLOEXEC );
      if( joy_fd == -1 ) {
        continue;
      }

      unsigned char axes = 0;
      unsigned char buttons = 0;
      char name[128] = {};

      Com_DPrintf( "Joystick %s found\n", filename );

      /* Get joystick statistics. */
      ioctl( joy_fd, JSIOCGAXES, &axes );
      ioctl( joy_fd, JSIOCGBUTTONS, &buttons );

      if( ioctl( joy_fd, JSIOCGNAME( sizeof( name ) ), name ) < 0 ) {
        Q_strncpyz( name, "Unknown", sizeof( name ) );
      }
      name[sizeof( name ) - 1] = '\0';

      Com_DPrintf( "Name:    %s\n", name );
      Com_DPrintf( "Axes:    %d\n", axes );
      Com_DPrintf( "Buttons: %d\n", buttons );
      joystick_unavailable_reported = qfalse;
      return qtrue;
    }
  }

  if( reportUnavailable && !joystick_unavailable_reported ) {
    Com_DPrintf( "No joystick found.\n" );
    joystick_unavailable_reported = qtrue;
  }
  return qfalse;
}


// bk001130 - from cvs1.17 (mkv), removed from linux_glimp.c
void IN_StartupJoystick( void )
{
  // in_restart and repeated initialization must never retain the previous
  // descriptor or its instantaneous axis snapshot.
  IN_CloseJoystickDevice( qfalse );
  joystick_last_open_attempt = 0;
  joystick_open_attempted = qfalse;
  joystick_unavailable_reported = qfalse;

  if( !in_joystick->integer ) {
    Com_DPrintf( "Joystick is not active.\n" );
    return;
  }

  IN_TryOpenJoystick(
    static_cast<std::uint32_t>( Sys_Milliseconds() ), qtrue );
}


void IN_ShutdownJoystick( void )
{
  IN_CloseJoystickDevice( qfalse );
  joystick_last_open_attempt = 0;
  joystick_open_attempted = qfalse;
  joystick_unavailable_reported = qfalse;
}


void IN_JoyMove( void )
{
  /* Our current goodies. */
  unsigned int axes = 0;
  int i = 0;
  const std::uint32_t now =
    static_cast<std::uint32_t>( Sys_Milliseconds() );

  if( joy_fd == -1 ) {
    if( !in_joystick->integer || !IN_JoystickRetryDue( now ) ||
      !IN_TryOpenJoystick( now, qfalse ) ) {
      return;
    }
  }

  /* Empty the queue, dispatching button presses immediately
	 * and updating the instantaneous state for the axes.
	 */
  do {
    ssize_t n;
    struct js_event event;

    do {
      n = read( joy_fd, &event, sizeof( event ) );
    } while( n < 0 && errno == EINTR );

    if( n < 0 && ( errno == EAGAIN || errno == EWOULDBLOCK ) ) {
      break;
    }
    if( n != static_cast<ssize_t>( sizeof( event ) ) ) {
      Com_DPrintf( "Joystick read failed; closing device\n" );
      IN_CloseJoystickDevice( qtrue );
      IN_ScheduleJoystickRetry( now );
      return;
    }

    if( event.type & JS_EVENT_BUTTON ) {
      if( event.number < kRawButtonCount ) {
        Sys_QueEvent( 0, SE_KEY, K_JOY1 + event.number,
          event.value ? qtrue : qfalse, 0, NULL );
      }
    } else if( event.type & JS_EVENT_AXIS ) {

      if( event.number >= kDigitalAxisCount ) {
	continue;
      }

      axes_state[event.number] = event.value;
    } else {
      Com_Printf( "Unknown joystick event type\n" );
    }

  } while( 1 );


  /* Translate our instantaneous state to bits. */
  const float deadzone =
    fnql::input::FiniteJoystickDeadzone( joy_threshold->value );
  if( deadzone < 1.0f ) {
    for( i = 0; i < kDigitalAxisCount; i++ ) {
      const float f = static_cast<float>( axes_state[i] ) / 32767.0f;

      if( f < -deadzone ) {
        axes |= ( 1u << ( i * 2 ) );
      } else if( f > deadzone ) {
        axes |= ( 1u << ( ( i * 2 ) + 1 ) );
      }

    }
  }

  /* Time to update axes state based on old vs. new. */
  for( i = 0; i < 16; i++ ) {

    if( ( axes & ( 1u << i ) ) && !( old_axes & ( 1u << i ) ) ) {
      Sys_QueEvent( 0, SE_KEY, joy_keys[i], qtrue, 0, NULL );
    }

    if( !( axes & ( 1u << i ) ) && ( old_axes & ( 1u << i ) ) ) {
      Sys_QueEvent( 0, SE_KEY, joy_keys[i], qfalse, 0, NULL );
    }
  }

  /* Save for future generations. */
  old_axes = axes;
}

#endif
