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

#include "../renderercommon/ql_font_text.h"

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
static cvar_t		*r_debugFontAtlas;
static qhandle_t	scr_connectBackground;

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
=======================
SCR_DrawNonGameBackdrop

The renderer does not implicitly clear the colour buffer at BeginFrame.  A
native non-game screen may draw only in the legacy 4:3 area, so it must begin
with a complete black frame rather than retain the previous browser or game
image outside that area.
=======================
*/
static void SCR_DrawNonGameBackdrop( void ) {
	ScopedRenderColor scopedColor( g_color_table[ ColorIndex( COLOR_BLACK ) ] );
	re.DrawStretchPic( 0, 0, cls.glconfig.vidWidth, cls.glconfig.vidHeight,
		0, 0, 0, 0, cls.whiteShader );
}


/*
=======================
SCR_DrawRetailConnectBackdrop

Retail ui/connect.menu uses ui/assets/backscreen_smoke with a 1920x1080
backgroundSize crop. Draw the same asset underneath the native connect export
so the screen remains correct if the retail UI's menu stack is unavailable
during a browser-to-game transition.
=======================
*/
static void SCR_DrawRetailConnectBackdrop( void ) {
	constexpr float sourceWidth = 1920.0f;
	constexpr float sourceHeight = 1080.0f;
	const float screenWidth = static_cast<float>( cls.glconfig.vidWidth );
	const float screenHeight = static_cast<float>( cls.glconfig.vidHeight );
	float s0;

	if ( !scr_connectBackground || screenWidth <= 0.0f || screenHeight <= 0.0f ) {
		return;
	}

	s0 = ( sourceWidth - screenWidth * ( sourceHeight / screenHeight ) )
		/ sourceWidth * 0.5f;
	ScopedRenderColor scopedColor( nullptr );
	re.DrawStretchPic( 0.0f, 0.0f, screenWidth, screenHeight,
		s0, 0.0f, 1.0f - s0, 1.0f, scr_connectBackground );
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
SCR_DrawStringExt

Draws retail client-owned large text through host font 2, optionally forcing
a fixed color. The deliberately separate small-string API retains the bitmap
charset contract.

Coordinates are at 640 by 480 virtual resolution
==================
*/
void SCR_DrawStringExt( int x, int y, float size, const char *string, const float *setColor, qboolean forceColor,
		qboolean noColorEscape ) {
	constexpr int hostFontMono = 2;
	float xscale;
	float yscale;
	float hostScale;
	int screenX;
	int screenY;

	if ( !string || !string[ 0 ] || !setColor ) {
		return;
	}

	xscale = cls.glconfig.vidWidth / 640.0f;
	yscale = cls.glconfig.vidHeight / 480.0f;
	hostScale = size * yscale;
	screenX = static_cast<int>( x * xscale );
	screenY = static_cast<int>( y * yscale );

	if ( !noColorEscape ) {
		RE_DrawScaledText( screenX, screenY, string, hostFontMono,
			hostScale, -1, nullptr, forceColor, setColor );
		return;
	}

	// FnQL's literal-color-code mode is not a retail host-text option. Draw
	// one decoded scalar at a time so ^0..^7 remain visible without breaking
	// UTF-8 or diverging from the renderer's glyph advances.
	{
		const char *cursor = string;
		const char *end = string + strlen( string );
		float penX = static_cast<float>( screenX );

		while ( cursor < end && *cursor ) {
			const qlFontUtf8Result_t decoded = QL_FontDecodeUtf8( cursor, end );
			std::array<char, 5> glyphText = {};
			float bounds[5] = {};
			int bytes;

			if ( decoded.bytes <= 0 ) {
				break;
			}
			bytes = decoded.bytes < static_cast<int>( glyphText.size() )
				? decoded.bytes : static_cast<int>( glyphText.size() ) - 1;
			std::copy_n( cursor, bytes, glyphText.data() );
			RE_DrawScaledText( RoundToInt( penX ), screenY, glyphText.data(),
				hostFontMono, hostScale, 1, nullptr, qtrue, setColor );
			RE_MeasureScaledText( glyphText.data(), nullptr, hostFontMono,
				hostScale, 1, bounds );
			float extent = penX + bounds[2] - bounds[0];
			if ( extent <= penX ) {
				extent = penX + hostScale * 0.6f;
			}
			penX = extent;
			cursor += decoded.bytes;
		}
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


static qboolean SCR_IsHostTextColorEscape( const char *s, const char *end ) {
	return QL_FontColorEscape( s, end, nullptr ) ? qtrue : qfalse;
}


static float SCR_HostTextHeight( float scale ) {
	return scale > 0.0f ? scale : 0.0f;
}


static float SCR_HostTextAdvance( float scale ) {
	return SCR_HostTextHeight( scale ) * 0.6f;
}


static void SCR_DrawHostScaledChar( float x, float y, float width, float height, int ch ) {
	int row, col;
	float frow, fcol;
	float size;

	ch &= 255;
	if ( ch == ' ' || width <= 0.0f || height <= 0.0f ) {
		return;
	}

	row = ch >> 4;
	col = ch & 15;
	frow = row * 0.0625f;
	fcol = col * 0.0625f;
	size = 0.0625f;

	re.DrawStretchPic( x, y, width, height, fcol, frow, fcol + size, frow + size, cls.charSetShader );
}


/*
=================
RE_DrawScaledText

Quake Live native UI/cgame pass screen-space coordinates and a host text scale.
The renderer owns the retail FontStash/STB faces and retained glyph atlas. The
charset lane remains available when the host text service is unavailable.
=================
*/
void RE_DrawScaledText( int x, int y, const char *text, int fontHandle, float scale, int limit, float *outMaxX, qboolean forceColor, const float *baseColor ) {
	vec4_t color;
	const char *cursor;
	const char *end;
	float penX;
	float advance;
	float height;
	float clipX;
	qboolean hasClipX;
	int remaining;

	if ( re.DrawScaledText && re.GetScaledFontMetrics &&
		re.GetScaledFontMetrics( fontHandle, scale, nullptr, nullptr, nullptr ) ) {
		if ( baseColor ) {
			re.SetColor( baseColor );
		}
		re.DrawScaledText( x, y, text, fontHandle, scale, limit, outMaxX,
			forceColor );
		return;
	}

	clipX = outMaxX ? *outMaxX : 0.0f;
	hasClipX = outMaxX ? qtrue : qfalse;
	if ( !text || !text[0] ) {
		return;
	}

	if ( baseColor ) {
		std::copy_n( baseColor, 4, color );
	} else {
		Vector4Set( color, 1.0f, 1.0f, 1.0f, 1.0f );
	}

	height = SCR_HostTextHeight( scale );
	advance = SCR_HostTextAdvance( scale );
	if ( height <= 0.0f || advance <= 0.0f ) {
		return;
	}

	penX = static_cast<float>( x );
	remaining = limit;

	end = text + strlen( text );
	ScopedRenderColor scopedColor( color );
	for ( cursor = text; cursor < end && *cursor; ) {
		qlFontUtf8Result_t decoded;
		float glyphMaxX;
		if ( remaining == 0 ) {
			break;
		}

		if ( SCR_IsHostTextColorEscape( cursor, end ) ) {
			if ( !forceColor ) {
				vec4_t escapedColor;

				std::copy_n( g_color_table[ cursor[ 1 ] - '0' ], 4, escapedColor );
				escapedColor[3] = color[3];
				re.SetColor( escapedColor );
			}
			cursor += 2;
			continue;
		}
		decoded = QL_FontDecodeUtf8( cursor, end );
		if ( decoded.bytes <= 0 ) {
			break;
		}

		glyphMaxX = penX + advance;
		if ( hasClipX && glyphMaxX > clipX ) {
			if ( outMaxX ) {
				*outMaxX = penX;
			}
			break;
		}

		SCR_DrawHostScaledChar( penX, static_cast<float>( y ) - height, advance, height,
			decoded.codepoint <= 255 ? static_cast<int>( decoded.codepoint ) : '?' );
		penX = glyphMaxX;
		cursor += decoded.bytes;
		if ( limit > 0 ) {
			--remaining;
		}
	}
}


/*
=================
RE_MeasureScaledText
=================
*/
void RE_MeasureScaledText( const char *text, const char *end, int fontHandle, float scale, int limit, float *bounds ) {
	const char *cursor;
	const char *textEnd;
	float width;
	float height;
	float advance;
	qboolean drewGlyph;
	int remaining;

	if ( re.MeasureScaledText && re.GetScaledFontMetrics &&
		re.GetScaledFontMetrics( fontHandle, scale, nullptr, nullptr, nullptr ) ) {
		re.MeasureScaledText( text, end, fontHandle, scale, limit, bounds );
		return;
	}

	if ( bounds ) {
		std::fill_n( bounds, 5, 0.0f );
	}
	if ( !text ) {
		return;
	}

	height = SCR_HostTextHeight( scale );
	advance = SCR_HostTextAdvance( scale );
	if ( height <= 0.0f || advance <= 0.0f ) {
		return;
	}

	width = 0.0f;
	drewGlyph = qfalse;
	remaining = limit;

	textEnd = end ? end : text + strlen( text );
	for ( cursor = text; cursor < textEnd && *cursor; ) {
		qlFontUtf8Result_t decoded;
		float glyphMaxX;
		if ( remaining == 0 ) {
			break;
		}

		if ( SCR_IsHostTextColorEscape( cursor, textEnd ) ) {
			cursor += 2;
			continue;
		}
		decoded = QL_FontDecodeUtf8( cursor, textEnd );
		if ( decoded.bytes <= 0 ) {
			break;
		}

		glyphMaxX = width + advance;
		width = glyphMaxX;
		drewGlyph = qtrue;
		cursor += decoded.bytes;
		if ( limit > 0 ) {
			--remaining;
		}
	}

	if ( bounds ) {
		bounds[0] = 0.0f;
		bounds[1] = drewGlyph ? -height : 0.0f;
		bounds[2] = width;
		bounds[3] = 0.0f;
		bounds[4] = height;
	}
}


//===============================================================================

/*
=================
SCR_DrawDemoRecording
=================
*/
static void SCR_DrawDemoRecording( void ) {
	std::array<char, sizeof( clc.recordNameShort ) + 32> string;
	const int messageMode = CL_DemoRecordMessageMode();
	int		pos;

	if ( !clc.demorecording ) {
		return;
	}
	if ( clc.spDemoRecording ) {
		return;
	}

	if ( messageMode == 1 ) {
		pos = FS_FTell( clc.recordfile );
		Com_sprintf( string.data(), static_cast<int>( string.size() ), "RECORDING %s: %ik", clc.recordNameShort, pos / 1024 );
		SCR_DrawStringExt( static_cast<int>( ( 80 - strlen( string.data() ) ) * 4 ),
			420, 8, string.data(), g_color_table[ ColorIndex( COLOR_WHITE ) ], qtrue, qfalse );
	} else if ( messageMode == 2 ) {
		SCR_DrawPic( 1, 470, 11, 11, cls.recordShader );
		SCR_DrawStringExt( 9, 477, 8, "REC",
			g_color_table[ ColorIndex( COLOR_WHITE ) ], qtrue, qfalse );
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
	r_debugFontAtlas = Cvar_Get( "r_debugFontAtlas", "0", CVAR_TEMP );
	scr_connectBackground = re.RegisterShaderNoMip( "ui/assets/backscreen_smoke" );

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


static void SCR_DrawFontAtlasDebug( void ) {
	int atlasWidth = 0;
	int atlasHeight = 0;
	qhandle_t shader;
	float scale = 1.0f;
	float maxWidth;
	float maxHeight;

	if ( !r_debugFontAtlas || !r_debugFontAtlas->integer ||
		!re.GetFontAtlasDebugShader ) {
		return;
	}
	shader = re.GetFontAtlasDebugShader( &atlasWidth, &atlasHeight );
	if ( !shader || atlasWidth <= 0 || atlasHeight <= 0 ) {
		return;
	}

	maxWidth = cls.glconfig.vidWidth - 32.0f;
	maxHeight = cls.glconfig.vidHeight - 32.0f;
	if ( maxWidth <= 0.0f || maxHeight <= 0.0f ) {
		return;
	}
	if ( atlasWidth > maxWidth ) {
		scale = maxWidth / atlasWidth;
	}
	if ( atlasHeight * scale > maxHeight ) {
		scale = maxHeight / atlasHeight;
	}

	ScopedRenderColor scopedColor( g_color_table[ ColorIndex( COLOR_WHITE ) ] );
	re.DrawStretchPic( 16.0f, 16.0f, atlasWidth * scale, atlasHeight * scale,
		0.0f, 0.0f, 1.0f, 1.0f, shader );
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
	bool browserOverlayRequested;
	bool browserPendingSurface;
	bool browserDrawableSurface;
	bool browserSuppressUiRefresh;
	bool browserOverlayAllowed;
	bool uiMenuVisible;
	bool drawConnectScreen;
	float menuDepthOfFieldAmount;

	re.BeginFrame( stereoFrame );

	uiFullscreen = uivm && VM_Call( uivm, 0, UI_IS_FULLSCREEN );
	uiMenuVisible = CL_UIMenusAreVisible();
	uiVisible = ( Key_GetCatcher() & KEYCATCH_UI ) && uivm;
	drawConnectScreen = cls.state == CA_CONNECTING || cls.state == CA_CHALLENGING
		|| cls.state == CA_CONNECTED;
	browserOverlayAllowed = !drawConnectScreen && cls.state != CA_LOADING
		&& cls.state != CA_PRIMED;
	browserOverlayRequested = browserOverlayAllowed && ( ( Key_GetCatcher() & KEYCATCH_BROWSER )
		|| Cvar_VariableIntegerValue( "web_browserActive" )
		|| Cvar_VariableIntegerValue( "ui_browserAwesomiumPending" ) );
	browserDrawableSurface = browserOverlayRequested && CL_WebHost_HasDrawableSurface();
	browserPendingSurface = browserOverlayRequested && !browserDrawableSurface;
	browserSuppressUiRefresh = browserDrawableSurface || ( Key_GetCatcher() & KEYCATCH_BROWSER );

	if ( browserSuppressUiRefresh && cls.state == CA_DISCONNECTED ) {
		uiFullscreen = true;
	} else if ( browserPendingSurface && uiFullscreen ) {
		uiFullscreen = false;
	}
	if ( browserDrawableSurface ) {
		uiFullscreen = true;
	}

	// Loading and live gameplay are always drawn by cgame plus the native UI
	// overlays.  A fullscreen main menu must not retain ownership after its
	// browser surface and key catcher have been released.
	if ( uiFullscreen && ( cls.state == CA_LOADING || cls.state == CA_PRIMED || cls.state == CA_ACTIVE ) ) {
		uiFullscreen = false;
	}
	if ( uiFullscreen && !browserOverlayRequested
		&& !( Key_GetCatcher() & KEYCATCH_UI ) ) {
		uiFullscreen = false;
	}

	// The retail connection dialog owns the whole frame while establishing a
	// connection.  Unlike a main menu, fullscreen must not suppress its draw
	// syscall or the non-game backdrop would be presented by itself.
	if ( cls.state != CA_ACTIVE ) {
		// BeginFrame does not clear the colour buffer.  Replace every previous
		// gameplay or browser frame before drawing a 4:3 native status screen.
		SCR_DrawNonGameBackdrop();
	} else if ( uiFullscreen ) {
		ScopedRenderColor scopedColor( g_color_table[ ColorIndex( COLOR_BLACK ) ] );
		SCR_DrawLegacyBorders();
	}

	// if the menu is going to cover the entire screen, we
	// don't need to render anything under it
	if ( uivm && ( !uiFullscreen || drawConnectScreen ) ) {
		switch( cls.state ) {
		default:
			Com_Error( ERR_FATAL, "SCR_DrawScreenField: bad cls.state" );
			break;
		case CA_CINEMATIC:
			SCR_DrawCinematic();
			break;
		case CA_DISCONNECTED:
			// Do not reset an existing native or browser menu every frame: doing
			// so restarts its background music on every key event.
			if ( !uiMenuVisible && !( Key_GetCatcher() & KEYCATCH_UI )
				&& !browserOverlayRequested ) {
				S_StopAllSounds();
				VM_Call( uivm, 1, UI_SET_ACTIVE_MENU, UIMENU_MAIN );
			}
			break;
		case CA_CONNECTING:
		case CA_CHALLENGING:
		case CA_CONNECTED:
			// connecting clients will only show the connection dialog
			SCR_DrawRetailConnectBackdrop();
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
	if ( uiVisible && !browserSuppressUiRefresh ) {
		VM_Call( uivm, 1, UI_REFRESH, cls.realtime );
	}

	// Native connection and level-loading screens retain exclusive ownership of
	// the frame even if a late browser callback leaves its overlay state armed.
	if ( browserOverlayAllowed ) {
		CL_WebHost_DrawBrowserSurface();
	}

	// console draws next
	Con_DrawConsole ();

	// debug graph can be drawn on top of anything
	if ( cl_debuggraph->integer || cl_timegraph->integer || cl_debugMove->integer ) {
		SCR_DrawDebugGraph ();
	}

	SCR_DrawFontAtlasDebug();
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

	if ( recursive >= 2 ) {
		Com_Error( ERR_FATAL, "SCR_UpdateScreen: recursively called" );
	}
	++recursive;

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

	--recursive;
}
