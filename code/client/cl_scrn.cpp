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
// cl_scrn.cpp -- master for refresh, status bar, console, chat, notify, etc

extern "C" {
#include "client.h"
}

#include <algorithm>
#include <array>

namespace {

constexpr int kDebugGraphSampleCount = 1024;

static int RoundToInt( float value )
{
	return static_cast<int>( value + 0.5f );
}

static float SCR_UpdateMenuDepthOfFieldAmount( bool targetActive )
{
	static int previousTime;
	static float amount;
	float target = 0.0f;
	int delta;
	int transitionTime;

	if ( targetActive && cl_menuDepthOfField ) {
		target = Com_Clamp( 0.0f, 1.0f, cl_menuDepthOfField->value );
	}

	delta = previousTime ? cls.realtime - previousTime : 0;
	previousTime = cls.realtime;
	if ( delta < 0 || delta > 1000 ) {
		delta = 0;
	}

	transitionTime = cl_menuDepthOfFieldTime ? cl_menuDepthOfFieldTime->integer : 160;
	if ( transitionTime <= 0 || delta <= 0 ) {
		amount = target;
	} else {
		const float step = static_cast<float>( delta ) / static_cast<float>( transitionTime );
		if ( amount < target ) {
			amount += step;
			if ( amount > target ) {
				amount = target;
			}
		} else if ( amount > target ) {
			amount -= step;
			if ( amount < target ) {
				amount = target;
			}
		}
	}

	if ( amount < 0.001f ) {
		amount = 0.0f;
	}
	return amount;
}

class ScopedRenderColor {
public:
	explicit ScopedRenderColor( const float *color ) {
		re.SetColor( color );
	}

	~ScopedRenderColor() {
		re.SetColor( nullptr );
	}

	ScopedRenderColor( const ScopedRenderColor& ) = delete;
	ScopedRenderColor& operator=( const ScopedRenderColor& ) = delete;
};

} // namespace

static bool	scr_initialized;		// ready to draw

cvar_t		*cl_timegraph;
static cvar_t		*cl_debuggraph;
static cvar_t		*cl_graphheight;
static cvar_t		*cl_graphscale;
static cvar_t		*cl_graphshift;

/*
================
SCR_DrawNamedPic

Coordinates are 640*480 virtual values
=================
*/
void SCR_DrawNamedPic( float x, float y, float width, float height, const char *picname ) {
	qhandle_t	hShader;

	assert( width != 0 );

	hShader = re.RegisterShader( picname );
	SCR_AdjustFrom640( &x, &y, &width, &height );
	re.DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}


/*
================
SCR_AdjustFrom640

Adjusted for resolution and screen aspect ratio
================
*/
void SCR_AdjustFrom640( float *x, float *y, float *w, float *h ) {
	float	xscale;
	float	yscale;

#if 0
		// adjust for wide screens
		if ( cls.glconfig.vidWidth * 480 > cls.glconfig.vidHeight * 640 ) {
			*x += 0.5 * ( cls.glconfig.vidWidth - ( cls.glconfig.vidHeight * 640 / 480 ) );
		}
#endif

	// scale for screen sizes
	xscale = cls.glconfig.vidWidth / 640.0;
	yscale = cls.glconfig.vidHeight / 480.0;
	if ( x ) {
		*x *= xscale;
	}
	if ( y ) {
		*y *= yscale;
	}
	if ( w ) {
		*w *= xscale;
	}
	if ( h ) {
		*h *= yscale;
	}
}


/*
================
SCR_AdjustFrom640Uniform

Adjusted for resolution using centered 4:3 virtual screen space
================
*/
void SCR_AdjustFrom640Uniform( float *x, float *y, float *w, float *h ) {
	if ( x ) {
		*x = *x * cls.scale + cls.biasX;
	}
	if ( y ) {
		*y = *y * cls.scale + cls.biasY;
	}
	if ( w ) {
		*w *= cls.scale;
	}
	if ( h ) {
		*h *= cls.scale;
	}
}


static void SCR_DrawLegacyBorders( void ) {
	if ( cls.biasX > 0.0f ) {
		re.DrawStretchPic( 0, 0, cls.biasX, cls.glconfig.vidHeight, 0, 0, 0, 0, cls.whiteShader );
		re.DrawStretchPic( cls.glconfig.vidWidth - cls.biasX, 0, cls.biasX, cls.glconfig.vidHeight, 0, 0, 0, 0, cls.whiteShader );
	}

	if ( cls.biasY > 0.0f ) {
		re.DrawStretchPic( 0, 0, cls.glconfig.vidWidth, cls.biasY, 0, 0, 0, 0, cls.whiteShader );
		re.DrawStretchPic( 0, cls.glconfig.vidHeight - cls.biasY, cls.glconfig.vidWidth, cls.biasY, 0, 0, 0, 0, cls.whiteShader );
	}
}

/*
================
SCR_FillRect

Coordinates are 640*480 virtual values
=================
*/
void SCR_FillRect( float x, float y, float width, float height, const float *color ) {
	ScopedRenderColor scopedColor( color );

	SCR_AdjustFrom640( &x, &y, &width, &height );
	re.DrawStretchPic( x, y, width, height, 0, 0, 0, 0, cls.whiteShader );
}


/*
================
SCR_DrawPic

Coordinates are 640*480 virtual values
=================
*/
void SCR_DrawPic( float x, float y, float width, float height, qhandle_t hShader ) {
	SCR_AdjustFrom640( &x, &y, &width, &height );
	re.DrawStretchPic( x, y, width, height, 0, 0, 1, 1, hShader );
}


/*
** SCR_DrawChar
** chars are drawn at 640*480 virtual screen size
*/
static void SCR_DrawChar( int x, int y, float size, int ch ) {
	int row, col;
	float frow, fcol;
	float	ax, ay, aw, ah;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -size ) {
		return;
	}

	ax = x;
	ay = y;
	aw = size;
	ah = size;
	SCR_AdjustFrom640( &ax, &ay, &aw, &ah );

	row = ch>>4;
	col = ch&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	re.DrawStretchPic( ax, ay, aw, ah,
					   fcol, frow, 
					   fcol + size, frow + size, 
					   cls.charSetShader );
}


/*
** SCR_DrawSmallChar
** small chars are drawn at native screen resolution
*/
void SCR_DrawSmallChar( int x, int y, int ch ) {
	int row, col;
	float frow, fcol;
	float size;

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -smallchar_height ) {
		return;
	}

	row = ch>>4;
	col = ch&15;

	frow = row*0.0625;
	fcol = col*0.0625;
	size = 0.0625;

	re.DrawStretchPic( x, y, smallchar_width, smallchar_height,
					   fcol, frow, 
					   fcol + size, frow + size, 
					   cls.charSetShader );
}


/*
** SCR_DrawSmallString
** small string are drawn at native screen resolution
*/
void SCR_DrawSmallString( int x, int y, const char *s, int len ) {
	int row, col, ch, i;
	float frow, fcol;
	float size;

	if ( y < -smallchar_height ) {
		return;
	}

	size = 0.0625;

	for ( i = 0; i < len; i++ ) {
		ch = *s++ & 255;
		row = ch>>4;
		col = ch&15;

		frow = row*0.0625;
		fcol = col*0.0625;

		re.DrawStretchPic( x, y, smallchar_width, smallchar_height,
						   fcol, frow, fcol + size, frow + size, 
						   cls.charSetShader );

		x += smallchar_width;
	}
}


/*
==================
SCR_DrawBigString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void SCR_DrawStringExt( int x, int y, float size, const char *string, const float *setColor, qboolean forceColor,
		qboolean noColorEscape ) {
	vec4_t		color;
	const char	*s;
	int			xx;

	// draw the drop shadow
	color[0] = color[1] = color[2] = 0.0;
	color[3] = setColor[3];
	ScopedRenderColor scopedColor( color );
	s = string;
	xx = x;
	while ( *s ) {
		if ( !noColorEscape && Q_IsColorString( s ) ) {
			s += 2;
			continue;
		}
		SCR_DrawChar( xx+2, y+2, size, *s );
		xx += size;
		s++;
	}


	// draw the colored text
	s = string;
	xx = x;
	re.SetColor( setColor );
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			if ( !forceColor ) {
				std::copy_n( g_color_table[ ColorIndexFromChar( *(s+1) ) ], 4, color );
				color[3] = setColor[3];
				re.SetColor( color );
			}
			if ( !noColorEscape ) {
				s += 2;
				continue;
			}
		}
		SCR_DrawChar( xx, y, size, *s );
		xx += size;
		s++;
	}
}


/*
==================
SCR_DrawBigString
==================
*/
void SCR_DrawBigString( int x, int y, const char *s, float alpha, qboolean noColorEscape ) {
	std::array<float, 4> color;

	color[0] = color[1] = color[2] = 1.0;
	color[3] = alpha;
	SCR_DrawStringExt( x, y, BIGCHAR_WIDTH, s, color.data(), qfalse, noColorEscape );
}


/*
==================
SCR_DrawSmallString[Color]

Draws a multi-colored string with a drop shadow, optionally forcing
to a fixed color.
==================
*/
void SCR_DrawSmallStringExt( int x, int y, const char *string, const float *setColor, qboolean forceColor,
		qboolean noColorEscape ) {
	vec4_t		color;
	const char	*s;
	int			xx;

	// draw the colored text
	s = string;
	xx = x;
	ScopedRenderColor scopedColor( setColor );
	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			if ( !forceColor ) {
				std::copy_n( g_color_table[ ColorIndexFromChar( *(s+1) ) ], 4, color );
				color[3] = setColor[3];
				re.SetColor( color );
			}
			if ( !noColorEscape ) {
				s += 2;
				continue;
			}
		}
		SCR_DrawSmallChar( xx, y, *s );
		xx += smallchar_width;
		s++;
	}
}


/*
** SCR_Strlen -- skips color escape codes
*/
static int SCR_Strlen( const char *str ) {
	const char *s = str;
	int count = 0;

	while ( *s ) {
		if ( Q_IsColorString( s ) ) {
			s += 2;
		} else {
			count++;
			s++;
		}
	}

	return count;
}


/*
** SCR_GetBigStringWidth
*/ 
int SCR_GetBigStringWidth( const char *str ) {
	return SCR_Strlen( str ) * BIGCHAR_WIDTH;
}


//===============================================================================

/*
=================
SCR_DrawDemoRecording
=================
*/
static void SCR_DrawDemoRecording( void ) {
	std::array<char, sizeof( clc.recordNameShort ) + 32> string;
	int		pos;

	if ( !clc.demorecording ) {
		return;
	}
	if ( clc.spDemoRecording ) {
		return;
	}

	pos = FS_FTell( clc.recordfile );

	if (cl_drawRecording->integer == 1) {
		Com_sprintf( string.data(), static_cast<int>( string.size() ), "RECORDING %s: %ik", clc.recordNameShort, pos / 1024 );
		SCR_DrawStringExt( 320 - strlen( string.data() ) * 4, 20, 8, string.data(), g_color_table[ ColorIndex( COLOR_WHITE ) ], qtrue, qfalse );
	} else if (cl_drawRecording->integer == 2) {
		Com_sprintf( string.data(), static_cast<int>( string.size() ), "RECORDING: %ik", pos / 1024 );
		SCR_DrawStringExt( 320 - strlen( string.data() ) * 4, 20, 8, string.data(), g_color_table[ ColorIndex( COLOR_WHITE ) ], qtrue, qfalse );
	}
}


#ifdef USE_VOIP
/*
=================
SCR_DrawVoipMeter
=================
*/
static void SCR_DrawVoipMeter( void ) {
	std::array<char, 16> buffer;
	std::array<char, 256> string;
	int limit;

	if (!cl_voipShowMeter->integer)
		return;  // player doesn't want to show meter at all.
	else if (!cl_voipSend->integer)
		return;  // not recording at the moment.
	else if (clc.state != CA_ACTIVE)
		return;  // not connected to a server.
	else if (!clc.voipEnabled)
		return;  // server doesn't support VoIP.
	else if (clc.demoplaying)
		return;  // playing back a demo.
	else if (!cl_voip->integer)
		return;  // client has VoIP support disabled.

	limit = static_cast<int>( clc.voipPower * 10.0f );
	if (limit > 10)
		limit = 10;

	std::fill_n( buffer.begin(), limit, '*' );
	std::fill( buffer.begin() + limit, buffer.begin() + 10, ' ' );
	buffer[ 10 ] = '\0';

	Com_sprintf( string.data(), static_cast<int>( string.size() ), "VoIP: [%s]", buffer.data() );
	SCR_DrawStringExt( 320 - strlen( string.data() ) * 4, 10, 8, string.data(), g_color_table[ ColorIndex( COLOR_WHITE ) ], qtrue, qfalse );
}
#endif

static void SCR_DrawSpatialAudioMeter( float x, float y, float width, float height, float value, const vec4_t fillColor ) {
	vec4_t backgroundColor = { 0.08f, 0.08f, 0.08f, 0.85f };
	vec4_t borderColor = { 0.25f, 0.25f, 0.25f, 0.95f };
	const float clamped = Com_Clamp( 0.0f, 1.0f, value );

	SCR_FillRect( x, y, width, height, backgroundColor );
	SCR_FillRect( x, y, width, 1, borderColor );
	SCR_FillRect( x, y + height - 1, width, 1, borderColor );
	SCR_FillRect( x, y, 1, height, borderColor );
	SCR_FillRect( x + width - 1, y, 1, height, borderColor );
	SCR_FillRect( x + 1, y + 1, ( width - 2 ) * clamped, height - 2, fillColor );
}

static void SCR_DrawSpatialAudioPanMeter( float x, float y, float width, float height, float pan ) {
	vec4_t backgroundColor = { 0.08f, 0.08f, 0.08f, 0.85f };
	vec4_t borderColor = { 0.25f, 0.25f, 0.25f, 0.95f };
	vec4_t centerColor = { 0.55f, 0.55f, 0.55f, 0.95f };
	vec4_t markerColor = { 0.95f, 0.80f, 0.25f, 0.95f };
	const float normalizedPan = Com_Clamp( -1.0f, 1.0f, pan ) * 0.5f + 0.5f;
	const float markerX = x + 1 + normalizedPan * ( width - 2 );

	SCR_FillRect( x, y, width, height, backgroundColor );
	SCR_FillRect( x, y, width, 1, borderColor );
	SCR_FillRect( x, y + height - 1, width, 1, borderColor );
	SCR_FillRect( x, y, 1, height, borderColor );
	SCR_FillRect( x + width - 1, y, 1, height, borderColor );
	SCR_FillRect( x + width * 0.5f - 1, y + 1, 2, height - 2, centerColor );
	SCR_FillRect( markerX - 2, y + 1, 4, height - 2, markerColor );
}

static void SCR_DrawSpatialAudioDebug( void ) {
	spatialAudioDebugInfo_t info;
	const vec4_t overlayColor = { 0.0f, 0.0f, 0.0f, 0.62f };
	const vec4_t textColor = { 0.95f, 0.95f, 0.95f, 1.0f };
	const vec4_t dryColor = { 0.40f, 0.78f, 0.95f, 0.95f };
	const vec4_t wetColor = { 0.35f, 0.90f, 0.55f, 0.95f };
	const vec4_t occColor = { 0.95f, 0.48f, 0.30f, 0.95f };
	const vec4_t pitchColor = { 0.88f, 0.70f, 0.98f, 0.95f };
	const float x = 12.0f;
	const float y = 74.0f;
	const float width = 328.0f;
	float barY;
	float height;
	int i;

	if ( !S_GetSpatialAudioDebugInfo( &info ) || !info.active ) {
		return;
	}

	height = 14.0f + info.lineCount * 10.0f;
	if ( info.hasSelectedVoice ) {
		height += 52.0f;
	}

	SCR_FillRect( x, y, width, height, overlayColor );

	for ( i = 0; i < info.lineCount; ++i ) {
		SCR_DrawStringExt( RoundToInt( x + 8.0f ), RoundToInt( y + 6.0f + i * 10.0f ), 8.0f,
			info.lines[i], textColor, qtrue, qfalse );
	}

	if ( !info.hasSelectedVoice ) {
		return;
	}

	barY = y + 10.0f + info.lineCount * 10.0f;
	SCR_DrawStringExt( RoundToInt( x + 8.0f ), RoundToInt( barY ), 8.0f, "dry", textColor, qtrue, qfalse );
	SCR_DrawSpatialAudioMeter( x + 48.0f, barY, 120.0f, 8.0f, info.dryGain, dryColor );
	SCR_DrawStringExt( RoundToInt( x + 178.0f ), RoundToInt( barY ), 8.0f, "wet", textColor, qtrue, qfalse );
	SCR_DrawSpatialAudioMeter( x + 214.0f, barY, 108.0f, 8.0f, info.wetGain, wetColor );

	barY += 12.0f;
	SCR_DrawStringExt( RoundToInt( x + 8.0f ), RoundToInt( barY ), 8.0f, "occ", textColor, qtrue, qfalse );
	SCR_DrawSpatialAudioMeter( x + 48.0f, barY, 120.0f, 8.0f, info.occlusion, occColor );
	SCR_DrawStringExt( RoundToInt( x + 178.0f ), RoundToInt( barY ), 8.0f, "pitch", textColor, qtrue, qfalse );
	SCR_DrawSpatialAudioMeter( x + 214.0f, barY, 108.0f, 8.0f, Com_Clamp( 0.0f, 1.0f, ( info.pitch - 0.85f ) / 0.30f ), pitchColor );

	barY += 12.0f;
	SCR_DrawStringExt( RoundToInt( x + 8.0f ), RoundToInt( barY ), 8.0f, "pan", textColor, qtrue, qfalse );
	SCR_DrawSpatialAudioPanMeter( x + 48.0f, barY, 274.0f, 8.0f, info.pan );
}


/*
===============================================================================

DEBUG GRAPH

===============================================================================
*/

static	int			current;
static	std::array<float, kDebugGraphSampleCount> values;

/*
==============
SCR_DebugGraph
==============
*/
void SCR_DebugGraph( float value )
{
	values[current] = value;
	current = ( current + 1 ) % static_cast<int>( values.size() );
}


/*
==============
SCR_DrawDebugGraph
==============
*/
static void SCR_DrawDebugGraph( void )
{
	int		a, x, y, w, i, h;
	float	v;

	//
	// draw the graph
	//
	w = cls.glconfig.vidWidth;
	x = 0;
	y = cls.glconfig.vidHeight;
	ScopedRenderColor graphColor( g_color_table[ ColorIndex( COLOR_BLACK ) ] );
	re.DrawStretchPic(x, y - cl_graphheight->integer, 
		w, cl_graphheight->integer, 0, 0, 0, 0, cls.whiteShader );

	for (a=0 ; a<w ; a++)
	{
		i = ( static_cast<int>( values.size() ) + current - 1 - ( a % static_cast<int>( values.size() ) ) ) %
			static_cast<int>( values.size() );
		v = values[i];
		v = v * cl_graphscale->integer + cl_graphshift->integer;
		
		if (v < 0)
			v += cl_graphheight->integer * ( 1 + static_cast<int>( -v / cl_graphheight->integer ) );
		h = static_cast<int>( v ) % cl_graphheight->integer;
		re.DrawStretchPic( x+w-1-a, y - h, 1, h, 0, 0, 0, 0, cls.whiteShader );
	}
}

//=============================================================================

/*
==================
SCR_Init
==================
*/
void SCR_Init( void ) {
	cl_timegraph = Cvar_Get ("timegraph", "0", CVAR_CHEAT);
	cl_debuggraph = Cvar_Get ("debuggraph", "0", CVAR_CHEAT);
	cl_graphheight = Cvar_Get ("graphheight", "32", CVAR_CHEAT);
	cl_graphscale = Cvar_Get ("graphscale", "1", CVAR_CHEAT);
	cl_graphshift = Cvar_Get ("graphshift", "0", CVAR_CHEAT);

	scr_initialized = true;
}


/*
==================
SCR_Done
==================
*/
void SCR_Done( void ) {
	scr_initialized = false;
}


//=======================================================

/*
==================
SCR_DrawScreenField

This will be called twice if rendering in stereo mode
==================
*/
static void SCR_DrawScreenField( stereoFrame_t stereoFrame ) {
	bool uiFullscreen;
	bool uiVisible;
	float menuDepthOfFieldAmount;

	re.BeginFrame( stereoFrame );

	uiFullscreen = uivm && VM_Call( uivm, 0, UI_IS_FULLSCREEN );
	uiVisible = ( Key_GetCatcher() & KEYCATCH_UI ) && uivm;

	// wide aspect ratio screens need to have the sides cleared
	// unless they are displaying game renderings
	if ( uiFullscreen || cls.state < CA_LOADING || ( cl_cinematicAspect && cl_cinematicAspect->integer && cls.state == CA_CINEMATIC ) ) {
		ScopedRenderColor scopedColor( g_color_table[ ColorIndex( COLOR_BLACK ) ] );
		SCR_DrawLegacyBorders();
	}

	// if the menu is going to cover the entire screen, we
	// don't need to render anything under it
	if ( uivm && !uiFullscreen ) {
		switch( cls.state ) {
		default:
			Com_Error( ERR_FATAL, "SCR_DrawScreenField: bad cls.state" );
			break;
		case CA_CINEMATIC:
			SCR_DrawCinematic();
			break;
		case CA_DISCONNECTED:
			// force menu up
			S_StopAllSounds();
			VM_Call( uivm, 1, UI_SET_ACTIVE_MENU, UIMENU_MAIN );
			break;
		case CA_CONNECTING:
		case CA_CHALLENGING:
		case CA_CONNECTED:
			// connecting clients will only show the connection dialog
			// refresh to update the time
			VM_Call( uivm, 1, UI_REFRESH, cls.realtime );
			VM_Call( uivm, 1, UI_DRAW_CONNECT_SCREEN, qfalse );
			break;
		case CA_LOADING:
		case CA_PRIMED:
			// draw the game information screen and loading progress
			if ( cgvm ) {
				CL_CGameRendering( stereoFrame );
			}
			// also draw the connection information, so it doesn't
			// flash away too briefly on local or lan games
			// refresh to update the time
			VM_Call( uivm, 1, UI_REFRESH, cls.realtime );
			VM_Call( uivm, 1, UI_DRAW_CONNECT_SCREEN, qtrue );
			break;
		case CA_ACTIVE:
			// always supply STEREO_CENTER as vieworg offset is now done by the engine.
			CL_CGameRendering( stereoFrame );
			SCR_DrawDemoRecording();
#ifdef USE_VOIP
			SCR_DrawVoipMeter();
#endif
			SCR_DrawSpatialAudioDebug();
			break;
		}
	}

	menuDepthOfFieldAmount = SCR_UpdateMenuDepthOfFieldAmount(
		uiVisible && !uiFullscreen && cls.state == CA_ACTIVE );
	if ( menuDepthOfFieldAmount > 0.0f && re.DrawMenuDepthOfField ) {
		re.DrawMenuDepthOfField( menuDepthOfFieldAmount );
	}

	// the menu draws next
	if ( uiVisible ) {
		VM_Call( uivm, 1, UI_REFRESH, cls.realtime );
	}

	// console draws next
	Con_DrawConsole ();

	// debug graph can be drawn on top of anything
	if ( cl_debuggraph->integer || cl_timegraph->integer || cl_debugMove->integer ) {
		SCR_DrawDebugGraph ();
	}
}


/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen( void ) {
	static int recursive;
	static int framecount;
	static int next_frametime;

	if ( !scr_initialized )
		return; // not initialized yet

	if ( framecount == cls.framecount ) {
		int ms = Sys_Milliseconds();
		if ( next_frametime && ms - next_frametime < 0 ) {
			re.ThrottleBackend();
		} else {
			next_frametime = ms + 16; // limit to 60 FPS
		}
	} else {
		next_frametime = 0;
		framecount = cls.framecount;
	}

	if ( ++recursive > 2 ) {
		Com_Error( ERR_FATAL, "SCR_UpdateScreen: recursively called" );
	}
	recursive = 1;

	// If there is no VM, there are also no rendering commands issued. Stop the renderer in
	// that case.
	if ( uivm )
	{
		// XXX
		int in_anaglyphMode = Cvar_VariableIntegerValue("r_anaglyphMode");
		// if running in stereo, we need to draw the frame twice
		if ( cls.glconfig.stereoEnabled || in_anaglyphMode) {
			SCR_DrawScreenField( STEREO_LEFT );
			SCR_DrawScreenField( STEREO_RIGHT );
		} else {
			SCR_DrawScreenField( STEREO_CENTER );
		}

		if ( com_speeds->integer ) {
			re.EndFrame( &time_frontend, &time_backend );
		} else {
			re.EndFrame( nullptr, nullptr );
		}
	}

	recursive = 0;
}
