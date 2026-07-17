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
// cl_console.cpp

extern "C" {
#include "client.h"
}

#include "client_cpp.h"
#include "ql_font_bridge.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

using fnql::FileWrite;
using fnql::ScopedFileHandle;
using fnql::ScopedTempMemory;
using fnql::ScopedZoneMemory;
using fnql::AllocateZoneMemory;

namespace {

constexpr int DEFAULT_CONSOLE_WIDTH = 78;
constexpr int MAX_CONSOLE_WIDTH = 120;
constexpr float RETAIL_CONSOLE_CHAR_WIDTH = 12.0f;
constexpr float RETAIL_CONSOLE_CHAR_HEIGHT = 24.0f;
constexpr float RETAIL_CONSOLE_REFERENCE_HEIGHT = 768.0f;
// FnQL exposes the retail half-size default as the normalized cvar value 1.
constexpr float RETAIL_CONSOLE_SCALE_UNIT = 0.5f;

constexpr int NUM_CON_TIMES = 4;

constexpr int CON_TEXTSIZE = 65536;
constexpr float CON_SCROLLBAR_BASE_WIDTH = 3.0f;
constexpr float CON_SCROLLBAR_HOVER_GROW = 5.0f;
constexpr float CON_SCROLLBAR_HIT_PAD = 5.0f;
constexpr float CON_SCROLLBAR_SIDE_PAD = 3.0f;
constexpr float CON_SCROLLBAR_MIN_THUMB = 18.0f;
constexpr float CON_SCROLLBAR_LERP_SPEED = 12.0f;
constexpr float CON_SELECTION_ALPHA = 0.35f;
constexpr int CON_COMPLETION_MAX_MATCHES = 64;
constexpr int CON_COMPLETION_MAX_VISIBLE = 8;
constexpr float CON_TEXT_DRAG_THRESHOLD = 4.0f;
constexpr int CONSOLE_HOST_FONT_MONO = 2;
// Retail Quake Live sizes its console mono face from the fixed cell width.
constexpr float CONSOLE_TTF_SCALE_PER_CELL = 2.1597f;

static int RoundToInt( float value )
{
	return static_cast<int>( value + 0.5f );
}

} // namespace

enum class ConFocus {
	Input,
	Log
};

using conCompletionMatch_t = std::array<char, MAX_EDIT_LINE>;

int bigchar_width;
int bigchar_height;
int smallchar_width;
int smallchar_height;

static int console_char_width;
static int console_char_height;

struct console_t {
	bool	initialized;

	std::array<short, CON_TEXTSIZE> text;
	int		current;		// line where next message will be printed
	int		x;				// offset in current line for next print
	int		display;		// bottom of console displays this line

	int 	linewidth;		// characters across screen
	int		totallines;		// total lines in console scrollback

	float	xadjust;		// for wide aspect screens

	float	displayFrac;	// aproaches finalFrac at con_speed
	float	finalFrac;		// 0.0 to 1.0 lines of console to display

	int		vislines;		// in scanlines
	float	displayWidth;
	float	displayLine;

	std::array<int, NUM_CON_TIMES> times;	// cls.realtime time the line was generated
								// for transparent notify lines
	vec4_t	color;

	int		viswidth;
	int		visheight;
	int		vispage;		
	int		inputSelectionAnchor;
	int		logSelectionAnchorLine;
	int		logSelectionAnchorColumn;
	int		logSelectionLine;
	int		logSelectionColumn;
	float	mouseX;
	float	mouseY;
	float	scrollbarHover;
	float	scrollbarDragOffset;
	float	completionScrollbarHover;
	float	completionScrollbarDragOffset;
	int		completionCount;
	int		completionSelection;
	int		completionScroll;
	int		completionReplaceOffset;
	int		completionReplaceLength;
	int		completionSnapshotCursor;
	bool	completionAppendSpace;
	bool	completionPopupVisible;
	bool	completionPrependSlash;
	bool	completionSnapshotValid;
	bool	textDragPending;
	bool	textDragging;
	bool	textDragFromInput;
	bool	textDragTargetInput;
	int		textDragSourceStart;
	int		textDragSourceEnd;
	int		textDragDropCursor;
	int		textDragTextLength;
	float	textDragStartMouseX;
	float	textDragStartMouseY;

	ConFocus focus;
	bool	mouseInitialized;
	bool	scrollbarDragging;
	bool	completionScrollbarDragging;
	bool	inputSelecting;
	bool	logSelecting;
	bool	newline;
	std::array<char, MAX_EDIT_LINE> completionSnapshotBuffer;
	std::array<conCompletionMatch_t, CON_COMPLETION_MAX_MATCHES> completionMatches;
	std::array<char, MAX_EDIT_LINE> textDragText;

};

extern "C" {
extern  qboolean    chat_team;
extern  int         chat_playerNum;
}

console_t	con;

cvar_t		*con_conspeed;
cvar_t		*con_autoClear;
cvar_t		*con_notifytime;
cvar_t		*con_scale;
cvar_t		*con_scaleUniform;
cvar_t		*con_screenExtents;
static cvar_t	*con_timestamps;
static cvar_t	*con_backgroundStyle;
static cvar_t	*con_backgroundColor;
static cvar_t	*con_backgroundOpacity;
static cvar_t	*con_scrollSmooth;
static cvar_t	*con_scrollSmoothSpeed;
static cvar_t	*con_completionPopup;
static cvar_t	*con_autoSay;
static cvar_t	*con_sayRaw;
static cvar_t	*con_showClock;
static cvar_t	*con_showVersion;
static cvar_t	*con_lineColor;
static cvar_t	*con_versionColor;
static cvar_t	*con_fade;
static cvar_t	*con_speedLegacy;
static cvar_t	*con_scrollLines;

static const vec4_t con_chatBackgroundColor = { 0.0f, 0.0f, 0.0f, 0.6f };
static const vec4_t con_chatPromptColor = { 1.0f, 0.8f, 0.0f, 1.0f };
static bool		con_ttfFontAvailable;
static float	con_ttfCellWidth = RETAIL_CONSOLE_CHAR_WIDTH;
static vec4_t	con_drawColor = { 1.0f, 1.0f, 1.0f, 1.0f };

int			g_console_field_width;

static void Con_Fixup( void );
static void Con_ClampMouseToConsole( void );
static void Con_ClearInputSelection( void );
static void Con_ClearLogSelection( void );
static int Con_GetScrollStep( int lines );
static void Con_InvalidateCompletionState( void );
static void Con_RefreshCompletionState( void );
static void Con_ApplySelectedCompletion( int direction );
static bool Con_GetCompletionScrollbarGeometry( float hoverFrac, float *trackX, float *trackY, float *trackW, float *trackH,
	float *thumbY, float *thumbH, float *hitX, float *hitW );
static bool Con_GetCompletionCvarInfo( const char *match, char *value, int valueSize, bool *modified );


static void Con_ParseColorString( const char *string, const vec4_t defaultColor, vec4_t outColor, qboolean allowAlpha ) {
	std::array<char, MAX_CVAR_VALUE_STRING> buffer;
	char *parts[4];
	int i;
	int count;

	for ( i = 0; i < 4; i++ ) {
		outColor[ i ] = defaultColor[ i ];
	}

	if ( !string || !string[ 0 ] ) {
		return;
	}

	Q_strncpyz( buffer.data(), string, static_cast<int>( buffer.size() ) );
	count = Com_Split( buffer.data(), parts, allowAlpha ? 4 : 3, ' ' );
	if ( count < 3 ) {
		return;
	}

	for ( i = 0; i < 3; i++ ) {
		float value = Q_atof( parts[ i ] ) / 255.0f;
		if ( value < 0.0f ) {
			value = 0.0f;
		} else if ( value > 1.0f ) {
			value = 1.0f;
		}
		outColor[ i ] = value;
	}

	if ( allowAlpha && count >= 4 ) {
		float value = Q_atof( parts[ 3 ] ) / 255.0f;
		if ( value < 0.0f ) {
			value = 0.0f;
		} else if ( value > 1.0f ) {
			value = 1.0f;
		}
		outColor[ 3 ] = value;
	}
}


static void Con_GetColorCvar( const cvar_t *cvar, const vec4_t defaultColor, vec4_t outColor, qboolean allowAlpha ) {
	int i;

	for ( i = 0; i < 4; i++ ) {
		outColor[ i ] = defaultColor[ i ];
	}

	if ( cvar && cvar->string[ 0 ] ) {
		Con_ParseColorString( cvar->string, defaultColor, outColor, allowAlpha );
	}
}


static void Con_GetBackgroundColor( vec4_t outColor ) {
	vec4_t defaultColor;
	float opacity;

	if ( con_backgroundStyle && !con_backgroundStyle->integer ) {
		defaultColor[ 0 ] = 1.0f;
		defaultColor[ 1 ] = 1.0f;
		defaultColor[ 2 ] = 1.0f;
		defaultColor[ 3 ] = 1.0f;
	} else {
		defaultColor[ 0 ] = 0.0f;
		defaultColor[ 1 ] = 0.0f;
		defaultColor[ 2 ] = 0.0f;
		defaultColor[ 3 ] = 1.0f;
	}

	Con_GetColorCvar( nullptr, defaultColor, outColor, qfalse );

	if ( cl_conColor && cl_conColor->string[ 0 ] ) {
		Con_ParseColorString( cl_conColor->string, outColor, outColor, qtrue );
	}

	if ( con_backgroundColor && con_backgroundColor->string[ 0 ] ) {
		float alpha = outColor[ 3 ];
		Con_ParseColorString( con_backgroundColor->string, outColor, outColor, qfalse );
		outColor[ 3 ] = alpha;
	}

	opacity = con_backgroundOpacity ? con_backgroundOpacity->value : 1.0f;
	if ( opacity < 0.0f ) {
		opacity = 0.0f;
	} else if ( opacity > 1.0f ) {
		opacity = 1.0f;
	}

	outColor[ 3 ] *= opacity;
}


static void Con_SetScaledColor( const vec4_t color, float alphaScale ) {
	con_drawColor[ 0 ] = color[ 0 ];
	con_drawColor[ 1 ] = color[ 1 ];
	con_drawColor[ 2 ] = color[ 2 ];
	con_drawColor[ 3 ] = color[ 3 ] * alphaScale;
	re.SetColor( con_drawColor );
}


static void Con_DrawSolidRect( float x, float y, float w, float h, const vec4_t color, float alphaScale ) {
	if ( w <= 0.0f || h <= 0.0f ) {
		return;
	}

	Con_SetScaledColor( color, alphaScale );
	re.DrawStretchPic( x, y, w, h, 0, 0, 1, 1, cls.whiteShader );
}


static void Con_LightenColor( const vec4_t color, float amount, vec4_t outColor ) {
	int i;

	if ( amount < 0.0f ) {
		amount = 0.0f;
	} else if ( amount > 1.0f ) {
		amount = 1.0f;
	}

	for ( i = 0; i < 3; i++ ) {
		outColor[ i ] = color[ i ] + ( 1.0f - color[ i ] ) * amount;
	}
	outColor[ 3 ] = color[ 3 ];
}


static float Con_GetFadeAlpha( float frac ) {
	float alphaScale;

	if ( !con_fade || !con_fade->integer ) {
		return 1.0f;
	}

	alphaScale = frac / 0.5f;
	if ( alphaScale < 0.0f ) {
		alphaScale = 0.0f;
	} else if ( alphaScale > 1.0f ) {
		alphaScale = 1.0f;
	}

	return alphaScale;
}


static float Con_GetTtfScale( void ) {
	return con_ttfCellWidth * CONSOLE_TTF_SCALE_PER_CELL;
}


static void Con_UpdateTtfFontAvailability( void ) {
	float lineHeight;

	con_ttfFontAvailable = re.DrawScaledText && re.GetScaledFontMetrics &&
		re.GetScaledFontMetrics( CONSOLE_HOST_FONT_MONO, Con_GetTtfScale(),
			nullptr, nullptr, &lineHeight ) && lineHeight > 0.0f;
}


static bool Con_DrawHostText( float x, float y, const char *text,
	qboolean forceColor, const vec4_t color ) {
	if ( !con_ttfFontAvailable || !text || !text[ 0 ] ) {
		return false;
	}

	re.SetColor( color );
	re.DrawScaledText( RoundToInt( x ), RoundToInt( y + console_char_height ),
		text, CONSOLE_HOST_FONT_MONO, Con_GetTtfScale(), -1, nullptr,
		forceColor );
	return true;
}


static bool Con_DrawHostTextSpan( float x, float y, const char *text, int length,
	qboolean forceColor, const vec4_t color ) {
	std::array<char, MAX_STRING_CHARS> buffer;

	if ( !con_ttfFontAvailable || !text || length <= 0 ) {
		return false;
	}
	if ( length >= static_cast<int>( buffer.size() ) ) {
		length = static_cast<int>( buffer.size() ) - 1;
	}
	std::copy_n( text, length, buffer.data() );
	buffer[ length ] = '\0';
	return Con_DrawHostText( x, y, buffer.data(), forceColor, color );
}


static float Con_MeasureHostText( const char *text, const char *end ) {
	float bounds[5] = {};

	if ( con_ttfFontAvailable && re.MeasureScaledText && text ) {
		re.MeasureScaledText( text, end, CONSOLE_HOST_FONT_MONO,
			Con_GetTtfScale(), -1, bounds );
	}
	return bounds[2] - bounds[0];
}


static void Con_DrawSmallCharFloat( float x, float y, int ch ) {
	int row, col;
	float frow, fcol;
	float size;
	char ttfText[ 2 ];

	ch &= 255;

	if ( ch == ' ' ) {
		return;
	}

	if ( y < -console_char_height ) {
		return;
	}

	if ( con_ttfFontAvailable && ( ch >= 32 || ch == 10 || ch == 11 ) ) {
		// The charset reserves these two bytes for insert/overwrite cursors.
		if ( ch == 10 ) {
			ch = '|';
		} else if ( ch == 11 ) {
			ch = '_';
		}
		ttfText[ 0 ] = static_cast<char>( ch );
		ttfText[ 1 ] = '\0';
		Con_DrawHostText( x, y, ttfText, qtrue, con_drawColor );
		return;
	}

	row = ch >> 4;
	col = ch & 15;

	frow = row * 0.0625f;
	fcol = col * 0.0625f;
	size = 0.0625f;

	re.DrawStretchPic( x, y, console_char_width, console_char_height,
		fcol, frow, fcol + size, frow + size,
		cls.charSetShader );
}


static bool Con_DrawConsoleLineText( float x, float y, const short *text,
	int count, float alphaScale ) {
	std::array<char, MAX_CONSOLE_WIDTH * 3 + 1> buffer;
	vec4_t baseColor;
	int bufferIndex = 0;
	int currentColor = ColorIndex( COLOR_WHITE );
	int lastVisible = 0;
	int i;

	if ( !con_ttfFontAvailable || !text || count <= 0 ) {
		return false;
	}

	for ( i = 0; i < count && bufferIndex < static_cast<int>( buffer.size() ) - 1; i++ ) {
		const int ch = text[ i ] & 0xff;
		const int color = ( text[ i ] >> 8 ) & 7;

		if ( ch != ' ' && color != currentColor &&
			bufferIndex < static_cast<int>( buffer.size() ) - 3 ) {
			buffer[ bufferIndex++ ] = '^';
			buffer[ bufferIndex++ ] = static_cast<char>( '0' + color );
			currentColor = color;
		}
		buffer[ bufferIndex++ ] = static_cast<char>( ch );
		if ( ch != ' ' ) {
			lastVisible = bufferIndex;
		}
	}

	buffer[ lastVisible ] = '\0';
	baseColor[ 0 ] = g_color_table[ ColorIndex( COLOR_WHITE ) ][ 0 ];
	baseColor[ 1 ] = g_color_table[ ColorIndex( COLOR_WHITE ) ][ 1 ];
	baseColor[ 2 ] = g_color_table[ ColorIndex( COLOR_WHITE ) ][ 2 ];
	baseColor[ 3 ] = g_color_table[ ColorIndex( COLOR_WHITE ) ][ 3 ] * alphaScale;
	return Con_DrawHostText( x, y, buffer.data(), qfalse, baseColor );
}


static int Con_GetOldestLine( void ) {
	if ( con.current >= con.totallines ) {
		return con.current - con.totallines + 1;
	}

	return 0;
}


static fnql::font::Utf8FieldWindow Con_GetInputDrawInfo( field_t *edit ) {
	fnql::font::Utf8FieldWindow window = fnql::font::GetUtf8FieldWindow(
		edit->buffer, edit->cursor, edit->widthInChars );
	edit->cursor = window.cursorByte;
	edit->scroll = window.startByte;
	return window;
}


static float Con_MeasureInputPrefix( const field_t *edit,
	const fnql::font::Utf8FieldWindow& window, int byteOffset ) {
	if ( byteOffset < window.startByte ) {
		byteOffset = window.startByte;
	} else if ( byteOffset > window.endByte ) {
		byteOffset = window.endByte;
	}
	byteOffset = fnql::font::ClampUtf8Boundary( edit->buffer, byteOffset );

	if ( con_ttfFontAvailable ) {
		float width = Con_MeasureHostText( edit->buffer + window.startByte,
			edit->buffer + byteOffset );
		// The retail measure ABI reports visible glyph bounds rather than the
		// trailing advance, so a terminal space needs its mono cell restored.
		if ( byteOffset > window.startByte && edit->buffer[ byteOffset - 1 ] == ' ' ) {
			width += console_char_width;
		}
		return width;
	}

	return fnql::font::CountUtf8Characters( edit->buffer,
		window.startByte, byteOffset ) * console_char_width;
}


static void Con_AdjustInputScroll( field_t *edit ) {
	Con_GetInputDrawInfo( edit );
}


static bool Con_HasInputSelection( void ) {
	return con.inputSelectionAnchor >= 0 && con.inputSelectionAnchor != g_consoleField.cursor;
}


static void Con_GetInputSelectionRange( int *start, int *end ) {
	if ( !Con_HasInputSelection() ) {
		if ( start ) {
			*start = g_consoleField.cursor;
		}
		if ( end ) {
			*end = g_consoleField.cursor;
		}
		return;
	}

	if ( con.inputSelectionAnchor < g_consoleField.cursor ) {
		if ( start ) {
			*start = con.inputSelectionAnchor;
		}
		if ( end ) {
			*end = g_consoleField.cursor;
		}
	} else {
		if ( start ) {
			*start = g_consoleField.cursor;
		}
		if ( end ) {
			*end = con.inputSelectionAnchor;
		}
	}
}


static void Con_ClearInputSelection( void ) {
	con.inputSelectionAnchor = -1;
	con.inputSelecting = false;
}


static void Con_DeleteInputRange( field_t *edit, int start, int end ) {
	int len;

	if ( start < 0 ) {
		start = 0;
	}

	len = strlen( edit->buffer );
	start = fnql::font::ClampUtf8Boundary( edit->buffer, start );
	end = fnql::font::ClampUtf8Boundary( edit->buffer, end );
	if ( end > len ) {
		end = len;
	}

	if ( end <= start ) {
		edit->cursor = start;
		Con_AdjustInputScroll( edit );
		Con_ClearInputSelection();
		return;
	}

	std::copy( edit->buffer + end, edit->buffer + len + 1, edit->buffer + start );
	edit->cursor = start;
	Con_AdjustInputScroll( edit );
	Con_ClearInputSelection();
	Con_InvalidateCompletionState();
}


static void Con_DeleteInputSelection( void ) {
	int start, end;

	if ( !Con_HasInputSelection() ) {
		return;
	}

	Con_GetInputSelectionRange( &start, &end );
	Con_DeleteInputRange( &g_consoleField, start, end );
}


static int Con_SeekWordCursor( const field_t *edit, int cursor, int direction ) {
	const char *buffer = edit->buffer;
	int len = strlen( buffer );

	if ( direction > 0 ) {
		while ( cursor < len && buffer[ cursor ] == ' ' ) {
			cursor++;
		}
		while ( cursor < len && buffer[ cursor ] != ' ' ) {
			cursor++;
		}
		while ( cursor < len && buffer[ cursor ] == ' ' ) {
			cursor++;
		}
	} else {
		while ( cursor > 0 && buffer[ cursor - 1 ] == ' ' ) {
			cursor--;
		}
		while ( cursor > 0 && buffer[ cursor - 1 ] != ' ' ) {
			cursor--;
		}
		if ( cursor == 0 && ( buffer[ 0 ] == '/' || buffer[ 0 ] == '\\' ) ) {
			cursor++;
		}
	}

	return cursor;
}


static void Con_SetInputCursor( int cursor, qboolean keepSelection ) {
	int oldCursor;
	int len;

	len = strlen( g_consoleField.buffer );
	oldCursor = g_consoleField.cursor;

	if ( cursor < 0 ) {
		cursor = 0;
	} else if ( cursor > len ) {
		cursor = len;
	}
	cursor = fnql::font::ClampUtf8Boundary( g_consoleField.buffer, cursor );

	if ( keepSelection ) {
		if ( con.inputSelectionAnchor < 0 ) {
			con.inputSelectionAnchor = oldCursor;
		}
	} else {
		Con_ClearInputSelection();
	}

	g_consoleField.cursor = cursor;
	Con_AdjustInputScroll( &g_consoleField );
	con.focus = ConFocus::Input;
	Con_ClearLogSelection();
	Con_InvalidateCompletionState();
}


static void Con_SelectAllInput( void ) {
	con.focus = ConFocus::Input;
	con.inputSelectionAnchor = 0;
	g_consoleField.cursor = strlen( g_consoleField.buffer );
	Con_AdjustInputScroll( &g_consoleField );
	Con_ClearLogSelection();
	Con_InvalidateCompletionState();
}


static void Con_InsertInputChar( int ch ) {
	int len;
	const bool utf8Continuation = fnql::font::IsUtf8ContinuationByte(
		static_cast<unsigned char>( ch ) );

	if ( ch < ' ' ) {
		return;
	}

	Con_DeleteInputSelection();
	len = strlen( g_consoleField.buffer );

	if ( key_overstrikeMode && !utf8Continuation ) {
		if ( g_consoleField.cursor == MAX_EDIT_LINE - 2 ) {
			return;
		}
		if ( g_consoleField.cursor < len ) {
			const int overwriteEnd = fnql::font::NextUtf8Boundary(
				g_consoleField.buffer, g_consoleField.cursor );
			if ( overwriteEnd > g_consoleField.cursor + 1 ) {
				std::copy( g_consoleField.buffer + overwriteEnd,
					g_consoleField.buffer + len + 1,
					g_consoleField.buffer + g_consoleField.cursor + 1 );
				len -= overwriteEnd - ( g_consoleField.cursor + 1 );
			}
		}

		g_consoleField.buffer[ g_consoleField.cursor ] = ch;
		g_consoleField.cursor++;
		if ( g_consoleField.cursor > len ) {
			g_consoleField.buffer[ g_consoleField.cursor ] = '\0';
		}
	} else {
		if ( len == MAX_EDIT_LINE - 2 ) {
			return;
		}

		std::copy_backward( g_consoleField.buffer + g_consoleField.cursor,
			g_consoleField.buffer + len + 1, g_consoleField.buffer + len + 2 );
		g_consoleField.buffer[ g_consoleField.cursor ] = ch;
		g_consoleField.cursor++;
	}

	Con_AdjustInputScroll( &g_consoleField );
	Con_ClearInputSelection();
	con.focus = ConFocus::Input;
	Con_ClearLogSelection();
	Con_InvalidateCompletionState();
}


static int Con_BuildInputSelectionText( char *buffer, int bufferSize ) {
	int start, end;
	int length;

	if ( !buffer || bufferSize < 1 || !Con_HasInputSelection() ) {
		return 0;
	}

	Con_GetInputSelectionRange( &start, &end );
	length = end - start;
	if ( length <= 0 ) {
		buffer[ 0 ] = '\0';
		return 0;
	}

	if ( length >= bufferSize ) {
		length = bufferSize - 1;
	}

	std::copy_n( g_consoleField.buffer + start, length, buffer );
	buffer[ length ] = '\0';
	return length;
}


static void Con_CopyInputSelection( void ) {
	std::array<char, MAX_EDIT_LINE> text;

	if ( !Con_BuildInputSelectionText( text.data(), static_cast<int>( text.size() ) ) ) {
		return;
	}
	Sys_SetClipboardData( text.data() );
}


static void Con_CutInputSelection( void ) {
	if ( !Con_HasInputSelection() ) {
		return;
	}

	Con_CopyInputSelection();
	Con_DeleteInputSelection();
}


static void Con_PasteClipboardToInput( void ) {
	int i;

	ScopedZoneMemory clipboardText( Sys_GetClipboardData() );
	char *text = clipboardText.as<char>();
	if ( !text ) {
		return;
	}

	con.focus = ConFocus::Input;
	Con_DeleteInputSelection();

	for ( i = 0; text[ i ]; i++ ) {
		const int ch = static_cast<unsigned char>( text[ i ] );
		if ( ch >= ' ' ) {
			Con_InsertInputChar( ch );
		}
	}
}


static int Con_CompareLogPos( int line1, int column1, int line2, int column2 ) {
	if ( line1 < line2 ) {
		return -1;
	}
	if ( line1 > line2 ) {
		return 1;
	}
	if ( column1 < column2 ) {
		return -1;
	}
	if ( column1 > column2 ) {
		return 1;
	}
	return 0;
}


static void Con_ClampLogPosition( int *line, int *column ) {
	int oldestLine = Con_GetOldestLine();

	if ( *line < oldestLine ) {
		*line = oldestLine;
	} else if ( *line > con.current ) {
		*line = con.current;
	}

	if ( *column < 0 ) {
		*column = 0;
	} else if ( *column > con.linewidth ) {
		*column = con.linewidth;
	}
}


static void Con_ClearLogSelection( void ) {
	con.logSelectionAnchorLine = con.logSelectionLine;
	con.logSelectionAnchorColumn = con.logSelectionColumn;
	con.logSelecting = false;
}


static bool Con_HasLogSelection( void ) {
	return Con_CompareLogPos( con.logSelectionAnchorLine, con.logSelectionAnchorColumn,
		con.logSelectionLine, con.logSelectionColumn ) != 0;
}


static void Con_GetLogSelectionRange( int *startLine, int *startColumn, int *endLine, int *endColumn ) {
	if ( Con_CompareLogPos( con.logSelectionAnchorLine, con.logSelectionAnchorColumn,
		con.logSelectionLine, con.logSelectionColumn ) <= 0 ) {
		if ( startLine ) {
			*startLine = con.logSelectionAnchorLine;
		}
		if ( startColumn ) {
			*startColumn = con.logSelectionAnchorColumn;
		}
		if ( endLine ) {
			*endLine = con.logSelectionLine;
		}
		if ( endColumn ) {
			*endColumn = con.logSelectionColumn;
		}
	} else {
		if ( startLine ) {
			*startLine = con.logSelectionLine;
		}
		if ( startColumn ) {
			*startColumn = con.logSelectionColumn;
		}
		if ( endLine ) {
			*endLine = con.logSelectionAnchorLine;
		}
		if ( endColumn ) {
			*endColumn = con.logSelectionAnchorColumn;
		}
	}
}


static void Con_SetLogCursor( int line, int column, qboolean keepSelection ) {
	Con_ClampLogPosition( &line, &column );

	if ( !keepSelection ) {
		con.logSelectionAnchorLine = line;
		con.logSelectionAnchorColumn = column;
	}

	con.logSelectionLine = line;
	con.logSelectionColumn = column;
	con.focus = ConFocus::Log;
	Con_ClearInputSelection();
	Con_InvalidateCompletionState();
}


static short *Con_GetLogLineText( int line ) {
	int oldestLine;

	oldestLine = Con_GetOldestLine();
	if ( line < oldestLine || line > con.current ) {
		return nullptr;
	}

	return con.text.data() + ( line % con.totallines ) * con.linewidth;
}


static int Con_BuildLogSelectionText( char *buffer, int bufferSize ) {
	int startLine, startColumn, endLine, endColumn;
	int line;
	int length;

	if ( !buffer || bufferSize < 1 || !Con_HasLogSelection() ) {
		return 0;
	}

	Con_GetLogSelectionRange( &startLine, &startColumn, &endLine, &endColumn );
	length = 0;

	for ( line = startLine; line <= endLine; line++ ) {
		short *lineText = Con_GetLogLineText( line );
		int segmentStart = ( line == startLine ) ? startColumn : 0;
		int segmentEnd = ( line == endLine ) ? endColumn : con.linewidth;
		int copyEnd;
		int i;

		if ( !lineText ) {
			continue;
		}

		if ( segmentStart < 0 ) {
			segmentStart = 0;
		}
		if ( segmentEnd > con.linewidth ) {
			segmentEnd = con.linewidth;
		}
		if ( segmentEnd < segmentStart ) {
			segmentEnd = segmentStart;
		}

		copyEnd = segmentEnd;
		while ( copyEnd > segmentStart && ( lineText[ copyEnd - 1 ] & 0xff ) == ' ' ) {
			copyEnd--;
		}

		for ( i = segmentStart; i < copyEnd && length < bufferSize - 1; i++ ) {
			buffer[ length++ ] = lineText[ i ] & 0xff;
		}

		if ( line < endLine && length < bufferSize - 1 ) {
			buffer[ length++ ] = '\n';
		}
	}

	buffer[ length ] = '\0';
	return length;
}


static void Con_SelectAllLog( void ) {
	con.focus = ConFocus::Log;
	Con_ClearInputSelection();
	con.logSelectionAnchorLine = Con_GetOldestLine();
	con.logSelectionAnchorColumn = 0;
	con.logSelectionLine = con.current;
	con.logSelectionColumn = con.linewidth;
	Con_InvalidateCompletionState();
}


static void Con_CopyLogSelection( void ) {
	int length;
	int startLine, endLine;
	int bufferSize;

	if ( !Con_HasLogSelection() ) {
		return;
	}

	Con_GetLogSelectionRange( &startLine, nullptr, &endLine, nullptr );
	bufferSize = ( endLine - startLine + 1 ) * ( con.linewidth + 1 ) + 1;
	ScopedZoneMemory textStorage = AllocateZoneMemory( bufferSize, "console log selection", __FILE__, __LINE__ );
	char *text = textStorage.as<char>();
	length = Con_BuildLogSelectionText( text, bufferSize );
	text[ length ] = '\0';
	Sys_SetClipboardData( text );
}


static void Con_CopySelection( void ) {
	if ( Con_HasInputSelection() ) {
		Con_CopyInputSelection();
	} else if ( Con_HasLogSelection() ) {
		Con_CopyLogSelection();
	}
}


static void Con_InvalidateCompletionState( void ) {
	con.completionCount = 0;
	con.completionSelection = 0;
	con.completionScroll = 0;
	con.completionReplaceOffset = 0;
	con.completionReplaceLength = 0;
	con.completionAppendSpace = false;
	con.completionPopupVisible = false;
	con.completionPrependSlash = false;
	con.completionSnapshotValid = false;
	con.completionSnapshotCursor = 0;
	con.completionScrollbarDragging = false;
	con.completionScrollbarHover = 0.0f;
	con.completionScrollbarDragOffset = 0.0f;
	con.completionSnapshotBuffer[ 0 ] = '\0';
}


static void Con_InsertInputTextAt( const char *text, int cursor ) {
	bool lastWasConvertedSpace;
	int i;

	if ( !text || !text[ 0 ] ) {
		Con_SetInputCursor( cursor, qfalse );
		return;
	}

	Con_SetInputCursor( cursor, qfalse );
	lastWasConvertedSpace = false;

	for ( i = 0; text[ i ]; i++ ) {
		int ch = static_cast<unsigned char>( text[ i ] );

		if ( ch == '\r' || ch == '\n' || ch == '\t' ) {
			if ( lastWasConvertedSpace ) {
				continue;
			}
			ch = ' ';
			lastWasConvertedSpace = true;
		} else {
			lastWasConvertedSpace = false;
			if ( ch < ' ' ) {
				continue;
			}
		}

		Con_InsertInputChar( ch );
	}
}


static bool Con_CompletionMatchLess( const conCompletionMatch_t& a, const conCompletionMatch_t& b ) {
	return Q_stricmp( a.data(), b.data() ) < 0;
}


struct conFuzzyCompletionMatch_t {
	std::array<char, MAX_EDIT_LINE> match;
	int		category;
	int		primary;
	int		secondary;
	int		tertiary;
};

struct conFuzzyCompletionState_t {
	std::array<char, MAX_EDIT_LINE> needle;
	int		needleLen;
	int		maxDistance;
	int		count;
	std::array<conFuzzyCompletionMatch_t, CON_COMPLETION_MAX_MATCHES> matches;
};


static qboolean Con_CollectCompletionMatch( const char *match, void *context ) {
	int i;
	static_cast<void>( context );

	if ( !match || !match[ 0 ] ) {
		return qtrue;
	}

	for ( i = 0; i < con.completionCount; i++ ) {
		if ( !Q_stricmp( con.completionMatches[ i ].data(), match ) ) {
			return qtrue;
		}
	}

	if ( con.completionCount >= CON_COMPLETION_MAX_MATCHES ) {
		return qfalse;
	}

	Q_strncpyz( con.completionMatches[ con.completionCount ].data(), match,
		static_cast<int>( con.completionMatches[ con.completionCount ].size() ) );
	con.completionCount++;
	return qtrue;
}

static void QDECL Con_CollectNativeArenaCompletionMatch( const char *name ) {
	(void)Con_CollectCompletionMatch( name, nullptr );
}

static void Con_CollectNativeArenaCompletionMatches( qboolean firstArg ) {
	if ( firstArg || !uivm || !uivm->dllExports ) {
		return;
	}

	VM_Call( uivm, 1, UI_FOR_EACH_ARENA_NAME, (intptr_t)Con_CollectNativeArenaCompletionMatch );
}


static int Con_CompletionLower( int ch ) {
	return tolower( static_cast<unsigned char>( ch ) );
}


static bool Con_IsCompletionWordChar( int ch ) {
	ch = static_cast<unsigned char>( ch );
	return ( ch >= 'a' && ch <= 'z' ) ||
		( ch >= 'A' && ch <= 'Z' ) ||
		( ch >= '0' && ch <= '9' );
}


static bool Con_IsCompletionBoundary( const char *match, int index ) {
	if ( index <= 0 ) {
		return true;
	}

	return !Con_IsCompletionWordChar( match[ index - 1 ] );
}


static bool Con_FindSubstringCompletion( const char *match, const char *needle,
	int *outPos, bool *outBoundary )
{
	int i;
	int needleLen = strlen( needle );
	int bestPos = -1;
	bool bestBoundary = false;

	if ( needleLen < 2 ) {
		return false;
	}

	for ( i = 0; match[ i ]; i++ ) {
		if ( Q_stricmpn( match + i, needle, needleLen ) != 0 ) {
			continue;
		}

		if ( bestPos < 0 ||
			( Con_IsCompletionBoundary( match, i ) && !bestBoundary ) ||
			( Con_IsCompletionBoundary( match, i ) == bestBoundary && i < bestPos ) ) {
			bestPos = i;
			bestBoundary = Con_IsCompletionBoundary( match, i );

			if ( bestPos == 0 ) {
				break;
			}
		}
	}

	if ( bestPos < 0 ) {
		return false;
	}

	if ( outPos ) {
		*outPos = bestPos;
	}
	if ( outBoundary ) {
		*outBoundary = bestBoundary;
	}

	return true;
}


static bool Con_FindSubsequenceCompletion( const char *match, const char *needle,
	int *outStart, int *outGapScore, bool *outBoundary )
{
	int i;
	int j = 0;
	int start = -1;
	int previous = -1;
	int gapScore = 0;

	if ( !needle[ 0 ] || !needle[ 1 ] ) {
		return false;
	}

	for ( i = 0; match[ i ] && needle[ j ]; i++ ) {
		if ( Con_CompletionLower( match[ i ] ) != Con_CompletionLower( needle[ j ] ) ) {
			continue;
		}

		if ( start < 0 ) {
			start = i;
		}
		if ( previous >= 0 ) {
			gapScore += i - previous - 1;
		}

		previous = i;
		j++;
	}

	if ( needle[ j ] ) {
		return false;
	}

	if ( outStart ) {
		*outStart = start;
	}
	if ( outGapScore ) {
		*outGapScore = gapScore;
	}
	if ( outBoundary ) {
		*outBoundary = Con_IsCompletionBoundary( match, start );
	}

	return true;
}


static int Con_BoundedCompletionDistance( const char *needle, int needleLen,
	const char *candidate, int candidateLen, int maxDistance )
{
	std::array<int, MAX_EDIT_LINE + 1> prevPrev;
	std::array<int, MAX_EDIT_LINE + 1> prev;
	std::array<int, MAX_EDIT_LINE + 1> curr;
	int i, j;

	if ( needleLen < 1 || candidateLen < 1 ||
		needleLen > MAX_EDIT_LINE || candidateLen > MAX_EDIT_LINE ) {
		return maxDistance + 1;
	}

	if ( ( needleLen > candidateLen ? needleLen - candidateLen : candidateLen - needleLen ) > maxDistance ) {
		return maxDistance + 1;
	}

	for ( j = 0; j <= candidateLen; j++ ) {
		prevPrev[ j ] = j;
		prev[ j ] = j;
	}

	for ( i = 1; i <= needleLen; i++ ) {
		int rowMin;

		curr[ 0 ] = i;
		rowMin = curr[ 0 ];

		for ( j = 1; j <= candidateLen; j++ ) {
			int cost = ( Con_CompletionLower( needle[ i - 1 ] ) == Con_CompletionLower( candidate[ j - 1 ] ) ) ? 0 : 1;
			int best = prev[ j ] + 1;

			if ( curr[ j - 1 ] + 1 < best ) {
				best = curr[ j - 1 ] + 1;
			}
			if ( prev[ j - 1 ] + cost < best ) {
				best = prev[ j - 1 ] + cost;
			}
			if ( i > 1 && j > 1 &&
				Con_CompletionLower( needle[ i - 1 ] ) == Con_CompletionLower( candidate[ j - 2 ] ) &&
				Con_CompletionLower( needle[ i - 2 ] ) == Con_CompletionLower( candidate[ j - 1 ] ) &&
				prevPrev[ j - 2 ] + 1 < best ) {
				best = prevPrev[ j - 2 ] + 1;
			}

			curr[ j ] = best;
			if ( best < rowMin ) {
				rowMin = best;
			}
		}

		if ( rowMin > maxDistance ) {
			return maxDistance + 1;
		}

		std::copy_n( prev.begin(), candidateLen + 1, prevPrev.begin() );
		std::copy_n( curr.begin(), candidateLen + 1, prev.begin() );
	}

	return prev[ candidateLen ];
}


static int Con_GetMaxCompletionDistance( int needleLen ) {
	if ( needleLen <= 3 ) {
		return 1;
	}
	if ( needleLen <= 6 ) {
		return 2;
	}

	return 3;
}


static int Con_FindBestCompletionDistance( const char *match, const char *needle,
	int maxDistance, int *outStart, bool *outBoundary )
{
	int start;
	int needleLen = strlen( needle );
	int matchLen = strlen( match );
	int minWindowLen;
	int maxWindowLen;
	int bestDistance = maxDistance + 1;
	int bestStart = -1;
	bool bestBoundary = false;

	if ( needleLen < 3 || matchLen < 1 ) {
		return -1;
	}

	minWindowLen = needleLen - maxDistance;
	if ( minWindowLen < 1 ) {
		minWindowLen = 1;
	}
	maxWindowLen = needleLen + maxDistance;

	for ( start = 0; start < matchLen; start++ ) {
		int candidateMaxLen = matchLen - start;
		int windowLen;
		bool boundary = Con_IsCompletionBoundary( match, start );

		for ( windowLen = minWindowLen;
			windowLen <= maxWindowLen && windowLen <= candidateMaxLen;
			windowLen++ ) {
			int limit = ( bestDistance <= maxDistance ) ? bestDistance - 1 : maxDistance;
			int distance = Con_BoundedCompletionDistance( needle, needleLen,
				match + start, windowLen, limit );

			if ( distance > maxDistance ) {
				continue;
			}

			if ( distance < bestDistance ||
				( distance == bestDistance && boundary && !bestBoundary ) ||
				( distance == bestDistance && boundary == bestBoundary && start < bestStart ) ) {
				bestDistance = distance;
				bestStart = start;
				bestBoundary = boundary;
			}
		}
	}

	if ( bestDistance > maxDistance ) {
		return -1;
	}

	if ( outStart ) {
		*outStart = bestStart;
	}
	if ( outBoundary ) {
		*outBoundary = bestBoundary;
	}

	return bestDistance;
}


static bool Con_BuildFuzzyCompletionMatch( const char *match,
	const conFuzzyCompletionState_t *state, conFuzzyCompletionMatch_t *outMatch )
{
	int pos;
	int start;
	int metric;
	bool boundary;

	if ( !match || !match[ 0 ] || !state || !outMatch ) {
		return false;
	}

	Q_strncpyz( outMatch->match.data(), match, static_cast<int>( outMatch->match.size() ) );
	outMatch->secondary = strlen( match );
	outMatch->tertiary = 0;

	if ( Con_FindSubstringCompletion( match, state->needle.data(), &pos, &boundary ) ) {
		outMatch->category = boundary ? 0 : 1;
		outMatch->primary = pos;
		outMatch->tertiary = pos;
		return true;
	}

	if ( Con_FindSubsequenceCompletion( match, state->needle.data(), &start, &metric, &boundary ) ) {
		outMatch->category = boundary ? 2 : 3;
		outMatch->primary = metric;
		outMatch->tertiary = start;
		return true;
	}

	metric = Con_FindBestCompletionDistance( match, state->needle.data(),
		state->maxDistance, &start, &boundary );
	if ( metric >= 0 ) {
		outMatch->category = boundary ? 4 : 5;
		outMatch->primary = metric;
		outMatch->tertiary = start;
		return true;
	}

	return false;
}


static int Con_CompareFuzzyCompletionMatches( const conFuzzyCompletionMatch_t& matchA,
	const conFuzzyCompletionMatch_t& matchB ) {
	if ( matchA.category != matchB.category ) {
		return matchA.category - matchB.category;
	}
	if ( matchA.primary != matchB.primary ) {
		return matchA.primary - matchB.primary;
	}
	if ( matchA.secondary != matchB.secondary ) {
		return matchA.secondary - matchB.secondary;
	}
	if ( matchA.tertiary != matchB.tertiary ) {
		return matchA.tertiary - matchB.tertiary;
	}

	return Q_stricmp( matchA.match.data(), matchB.match.data() );
}


static bool Con_FuzzyCompletionMatchLess( const conFuzzyCompletionMatch_t& matchA,
	const conFuzzyCompletionMatch_t& matchB ) {
	return Con_CompareFuzzyCompletionMatches( matchA, matchB ) < 0;
}


static qboolean Con_CollectFuzzyCompletionMatch( const char *match, void *context ) {
	conFuzzyCompletionState_t *state = static_cast<conFuzzyCompletionState_t *>( context );
	conFuzzyCompletionMatch_t candidate;
	int i;
	int worst;

	if ( !state || !Con_BuildFuzzyCompletionMatch( match, state, &candidate ) ) {
		return qtrue;
	}

	for ( i = 0; i < state->count; i++ ) {
		if ( Q_stricmp( state->matches[ i ].match.data(), candidate.match.data() ) != 0 ) {
			continue;
		}

		if ( Con_CompareFuzzyCompletionMatches( candidate, state->matches[ i ] ) < 0 ) {
			state->matches[ i ] = candidate;
		}
		return qtrue;
	}

	if ( state->count < CON_COMPLETION_MAX_MATCHES ) {
		state->matches[ state->count++ ] = candidate;
		return qtrue;
	}

	worst = 0;
	for ( i = 1; i < state->count; i++ ) {
		if ( Con_CompareFuzzyCompletionMatches( state->matches[ worst ], state->matches[ i ] ) < 0 ) {
			worst = i;
		}
	}

	if ( Con_CompareFuzzyCompletionMatches( candidate, state->matches[ worst ] ) < 0 ) {
		state->matches[ worst ] = candidate;
	}

	return qtrue;
}


static void Con_FindCompletionSegment( int cursor, int *segmentStart, int *segmentEnd ) {
	const char *buffer = g_consoleField.buffer;
	int len = strlen( buffer );
	int start = 0;
	int end = len;
	int i;

	if ( cursor < 0 ) {
		cursor = 0;
	} else if ( cursor > len ) {
		cursor = len;
	}

	for ( i = 0; i < cursor; i++ ) {
		if ( buffer[ i ] == ';' ) {
			start = i + 1;
		}
	}

	for ( i = cursor; i < len; i++ ) {
		if ( buffer[ i ] == ';' ) {
			end = i;
			break;
		}
	}

	if ( segmentStart ) {
		*segmentStart = start;
	}
	if ( segmentEnd ) {
		*segmentEnd = end;
	}
}


static bool Con_CurrentTokenMatchesSelectedCompletion( void ) {
	int matchLen;

	if ( con.completionCount < 1 ||
		con.completionSelection < 0 ||
		con.completionSelection >= con.completionCount ) {
		return false;
	}

	matchLen = strlen( con.completionMatches[ con.completionSelection ].data() );
	if ( matchLen != con.completionReplaceLength ) {
		return false;
	}

	return !Q_stricmpn( g_consoleField.buffer + con.completionReplaceOffset,
		con.completionMatches[ con.completionSelection ].data(), matchLen );
}


static bool Con_ShouldHideMatchedCompletionPopup( const char *buffer, int cursor ) {
	char next;

	if ( !buffer || !Con_CurrentTokenMatchesSelectedCompletion() ) {
		return false;
	}

	if ( cursor != con.completionReplaceOffset + con.completionReplaceLength ) {
		return false;
	}

	next = buffer[ cursor ];
	return next == '\0' || next <= ' ' || next == ';';
}


static bool Con_CompletionPopupEnabled( void ) {
	return con_completionPopup && con_completionPopup->integer;
}


qboolean Con_UseAutoSay( void ) {
	return ( con_autoSay && con_autoSay->integer ) ? qtrue : qfalse;
}


qboolean Con_UseRawSay( void ) {
	return ( con_sayRaw && con_sayRaw->integer ) ? qtrue : qfalse;
}


static int Con_GetShowClockMode( void ) {
	if ( !con_showClock ) {
		return 0;
	}

	if ( con_showClock->integer < 0 ) {
		return 0;
	}

	if ( con_showClock->integer > 2 ) {
		return 2;
	}

	return con_showClock->integer;
}


static qboolean Con_ShowVersion( void ) {
	return ( !con_showVersion || con_showVersion->integer ) ? qtrue : qfalse;
}


static int Con_GetFooterRows( void ) {
	int rows = 2;

	if ( Con_GetShowClockMode() && Con_ShowVersion() ) {
		rows++;
	}

	return rows;
}


static qboolean Con_GetClockString( char *buffer, int bufferSize ) {
	qtime_t now;
	int mode = Con_GetShowClockMode();

	if ( !buffer || bufferSize < 1 || !mode ) {
		return qfalse;
	}

	Com_RealTime( &now );

	if ( mode == 1 ) {
		Com_sprintf( buffer, bufferSize, "%02d:%02d", now.tm_hour, now.tm_min );
	} else {
		int hour = now.tm_hour % 12;

		if ( hour == 0 ) {
			hour = 12;
		}

		Com_sprintf( buffer, bufferSize, "%d:%02d %s", hour, now.tm_min,
			( now.tm_hour >= 12 ) ? "PM" : "AM" );
	}

	return qtrue;
}


static qboolean Con_HasActiveCompletionPopup( void ) {
	return ( Con_CompletionPopupEnabled() &&
		con.completionPopupVisible &&
		con.focus == ConFocus::Input &&
		!con.textDragging &&
		con.completionCount > 0 ) ? qtrue : qfalse;
}


static void Con_DismissCompletionPopup( void ) {
	con.completionCount = 0;
	con.completionSelection = 0;
	con.completionScroll = 0;
	con.completionAppendSpace = false;
	con.completionPopupVisible = false;
	con.completionPrependSlash = false;
	con.completionSnapshotValid = true;
	con.completionSnapshotCursor = g_consoleField.cursor;
	con.completionScrollbarDragging = false;
	Q_strncpyz( con.completionSnapshotBuffer.data(), g_consoleField.buffer, static_cast<int>( con.completionSnapshotBuffer.size() ) );
}


static int Con_GetCompletionVisibleCount( void ) {
	int visibleCount = con.completionCount;

	if ( visibleCount > CON_COMPLETION_MAX_VISIBLE ) {
		visibleCount = CON_COMPLETION_MAX_VISIBLE;
	}

	if ( visibleCount < 0 ) {
		visibleCount = 0;
	}

	return visibleCount;
}


static void Con_ClampCompletionScroll( qboolean keepSelectionVisible ) {
	int visibleCount = Con_GetCompletionVisibleCount();
	int maxScroll = con.completionCount - visibleCount;

	if ( maxScroll < 0 ) {
		maxScroll = 0;
	}

	if ( con.completionScroll < 0 ) {
		con.completionScroll = 0;
	} else if ( con.completionScroll > maxScroll ) {
		con.completionScroll = maxScroll;
	}

	if ( !keepSelectionVisible || visibleCount < 1 || con.completionCount < 1 ) {
		return;
	}

	if ( con.completionSelection < 0 ) {
		con.completionSelection = 0;
	} else if ( con.completionSelection >= con.completionCount ) {
		con.completionSelection = con.completionCount - 1;
	}

	if ( con.completionSelection < con.completionScroll ) {
		con.completionScroll = con.completionSelection;
	} else if ( con.completionSelection >= con.completionScroll + visibleCount ) {
		con.completionScroll = con.completionSelection - visibleCount + 1;
	}

	if ( con.completionScroll < 0 ) {
		con.completionScroll = 0;
	} else if ( con.completionScroll > maxScroll ) {
		con.completionScroll = maxScroll;
	}
}


static void Con_MoveCompletionSelection( int delta ) {
	if ( con.completionCount < 1 || delta == 0 ) {
		return;
	}

	if ( delta < 0 ) {
		con.completionSelection = ( con.completionSelection + con.completionCount - 1 ) % con.completionCount;
	} else {
		con.completionSelection = ( con.completionSelection + 1 ) % con.completionCount;
	}

	Con_ClampCompletionScroll( qtrue );
}


static void Con_RefreshCompletionState( void ) {
	conFuzzyCompletionState_t fuzzyState{};
	std::array<char, MAX_EDIT_LINE> prefixBuffer;
	std::array<char, MAX_EDIT_LINE> fullSegment;
	std::array<char, MAX_EDIT_LINE> needle;
	std::array<char, MAX_EDIT_LINE> previousMatch;
	const char *buffer = g_consoleField.buffer;
	int cursor = g_consoleField.cursor;
	int len = strlen( buffer );
	int segmentStart, segmentEnd;
	int prefixLen, fullLen;
	int relativeCursor;
	int argIndex;
	int currentLen;
	int i;
	int strictMatchCount;
	int tokenOffset = 0;
	int tokenLength = 0;
	qboolean appendSpace;
	bool keepPrevious = false;
	bool firstArg = false;
	bool usedFuzzyMatches = false;

	if ( cursor < 0 ) {
		cursor = 0;
	} else if ( cursor > len ) {
		cursor = len;
	}

	if ( con.focus != ConFocus::Input || con.textDragging ) {
		con.completionCount = 0;
		con.completionSelection = 0;
		con.completionScroll = 0;
		con.completionAppendSpace = false;
		con.completionPopupVisible = false;
		con.completionPrependSlash = false;
		con.completionSnapshotValid = true;
		con.completionSnapshotCursor = cursor;
		Q_strncpyz( con.completionSnapshotBuffer.data(), buffer, static_cast<int>( con.completionSnapshotBuffer.size() ) );
		return;
	}

	if ( con.completionSnapshotValid &&
		con.completionSnapshotCursor == cursor &&
		!Q_stricmp( con.completionSnapshotBuffer.data(), buffer ) ) {
		return;
	}

	if ( con.completionCount > 0 &&
		con.completionSelection >= 0 &&
		con.completionSelection < con.completionCount ) {
		Q_strncpyz( previousMatch.data(), con.completionMatches[ con.completionSelection ].data(), static_cast<int>( previousMatch.size() ) );
		keepPrevious = true;
	} else {
		previousMatch[ 0 ] = '\0';
	}

	con.completionCount = 0;
	con.completionSelection = 0;
	con.completionScroll = 0;
	con.completionAppendSpace = false;
	con.completionPrependSlash = false;

	Con_FindCompletionSegment( cursor, &segmentStart, &segmentEnd );

	prefixLen = cursor - segmentStart;
	if ( prefixLen < 0 ) {
		prefixLen = 0;
	}
	if ( prefixLen >= static_cast<int>( prefixBuffer.size() ) ) {
		prefixLen = static_cast<int>( prefixBuffer.size() ) - 1;
	}

	std::copy_n( buffer + segmentStart, prefixLen, prefixBuffer.data() );
	prefixBuffer[ prefixLen ] = '\0';

	fullLen = segmentEnd - segmentStart;
	if ( fullLen < 0 ) {
		fullLen = 0;
	}
	if ( fullLen >= static_cast<int>( fullSegment.size() ) ) {
		fullLen = static_cast<int>( fullSegment.size() ) - 1;
	}

	std::copy_n( buffer + segmentStart, fullLen, fullSegment.data() );
	fullSegment[ fullLen ] = '\0';

	Cmd_TokenizeString( fullSegment.data() );
	relativeCursor = cursor - segmentStart;
	if ( relativeCursor < 0 ) {
		relativeCursor = 0;
	} else if ( relativeCursor > fullLen ) {
		relativeCursor = fullLen;
	}

	if ( Cmd_Argc() < 1 ) {
		con.completionReplaceOffset = cursor;
		con.completionReplaceLength = 0;
		firstArg = true;
	} else if ( relativeCursor > 0 && fullSegment[ relativeCursor - 1 ] <= ' ' ) {
		con.completionReplaceOffset = cursor;
		con.completionReplaceLength = 0;
		firstArg = Cmd_Argc() < 1;
	} else {
		argIndex = Cmd_ArgIndexFromOffset( relativeCursor );
		if ( argIndex < 0 ) {
			con.completionReplaceOffset = cursor;
			con.completionReplaceLength = 0;
			firstArg = false;
		} else {
			tokenOffset = Cmd_ArgOffset( argIndex );
			tokenLength = strlen( Cmd_Argv( argIndex ) );

			if ( argIndex == 0 && tokenLength > 0 &&
				( fullSegment[ tokenOffset ] == '\\' || fullSegment[ tokenOffset ] == '/' ) ) {
				tokenOffset++;
				tokenLength--;
			}

			con.completionReplaceOffset = segmentStart + tokenOffset;
			con.completionReplaceLength = tokenLength;
			firstArg = argIndex == 0;
		}
	}

	appendSpace = qfalse;
	strictMatchCount = Field_QueryCompletionMatches( prefixBuffer.data(), &appendSpace,
	Con_CollectCompletionMatch, nullptr );
	Con_CollectNativeArenaCompletionMatches( firstArg ? qtrue : qfalse );

	if ( ( strictMatchCount < 1 || con.completionCount < 1 ) &&
		con.completionReplaceLength > 0 &&
		con.completionReplaceOffset >= 0 &&
		con.completionReplaceOffset + con.completionReplaceLength <= len ) {
		std::copy_n( buffer + con.completionReplaceOffset, con.completionReplaceLength, needle.data() );
		needle[ con.completionReplaceLength ] = '\0';

		if ( needle[ 0 ] && needle[ 1 ] ) {
			Q_strncpyz( fuzzyState.needle.data(), needle.data(), static_cast<int>( fuzzyState.needle.size() ) );
			fuzzyState.needleLen = strlen( fuzzyState.needle.data() );
			fuzzyState.maxDistance = Con_GetMaxCompletionDistance( fuzzyState.needleLen );

			Field_QueryCompletionCandidates( prefixBuffer.data(), Con_CollectFuzzyCompletionMatch, &fuzzyState );

			if ( fuzzyState.count > 0 ) {
				std::sort( fuzzyState.matches.begin(), fuzzyState.matches.begin() + fuzzyState.count,
					Con_FuzzyCompletionMatchLess );

				for ( i = 0; i < fuzzyState.count; i++ ) {
					Q_strncpyz( con.completionMatches[ i ].data(), fuzzyState.matches[ i ].match.data(),
						static_cast<int>( con.completionMatches[ i ].size() ) );
				}

				con.completionCount = fuzzyState.count;
				appendSpace = ( fuzzyState.count == 1 ) ? qtrue : qfalse;
				usedFuzzyMatches = true;
			}
		}
	}

	if ( con.completionCount < 1 ) {
		con.completionPopupVisible = false;
		con.completionScroll = 0;
		con.completionSnapshotValid = true;
		con.completionSnapshotCursor = cursor;
		Q_strncpyz( con.completionSnapshotBuffer.data(), buffer, static_cast<int>( con.completionSnapshotBuffer.size() ) );
		return;
	}

	con.completionAppendSpace = appendSpace;
	con.completionPrependSlash = segmentStart == 0 && firstArg &&
		con.completionReplaceOffset == 0 &&
		buffer[ 0 ] != '\\' && buffer[ 0 ] != '/';

	if ( !usedFuzzyMatches ) {
		std::sort( con.completionMatches.begin(), con.completionMatches.begin() + con.completionCount,
			Con_CompletionMatchLess );
	}

	if ( keepPrevious ) {
		for ( i = 0; i < con.completionCount; i++ ) {
			if ( !Q_stricmp( con.completionMatches[ i ].data(), previousMatch.data() ) ) {
				con.completionSelection = i;
				break;
			}
		}
	}

	currentLen = con.completionReplaceLength;
	if ( currentLen > 0 ) {
		for ( i = 0; i < con.completionCount; i++ ) {
			if ( static_cast<int>( strlen( con.completionMatches[ i ].data() ) ) == currentLen &&
				!Q_stricmpn( buffer + con.completionReplaceOffset, con.completionMatches[ i ].data(), currentLen ) ) {
				con.completionSelection = i;
				break;
			}
		}
	}

	if ( Con_ShouldHideMatchedCompletionPopup( buffer, cursor ) ) {
		con.completionCount = 0;
		con.completionSelection = 0;
		con.completionScroll = 0;
		con.completionAppendSpace = false;
		con.completionPopupVisible = false;
		con.completionPrependSlash = false;
		con.completionSnapshotValid = true;
		con.completionSnapshotCursor = cursor;
		Q_strncpyz( con.completionSnapshotBuffer.data(), buffer, static_cast<int>( con.completionSnapshotBuffer.size() ) );
		return;
	}

	Con_ClampCompletionScroll( qtrue );

	con.completionSnapshotValid = true;
	con.completionSnapshotCursor = cursor;
	Q_strncpyz( con.completionSnapshotBuffer.data(), buffer, static_cast<int>( con.completionSnapshotBuffer.size() ) );
}


static void Con_ApplySelectedCompletion( int direction ) {
	std::array<char, MAX_EDIT_LINE> completed;
	const char *buffer = g_consoleField.buffer;
	const char *match;
	int len = strlen( buffer );
	int replaceOffset = con.completionReplaceOffset;
	int replaceLength = con.completionReplaceLength;
	int suffixOffset;
	int outLen = 0;
	int matchLen;
	int copyLen;
	int suffixLen;
	bool addSpace = false;

	if ( !Con_CompletionPopupEnabled() ) {
		Field_AutoComplete( &g_consoleField );
		Con_InvalidateCompletionState();
		return;
	}

	Con_RefreshCompletionState();

	if ( con.completionCount < 1 ) {
		Field_AutoComplete( &g_consoleField );
		Con_InvalidateCompletionState();
		return;
	}

	if ( direction < 0 ) {
		if ( !Con_CurrentTokenMatchesSelectedCompletion() ) {
			con.completionSelection = con.completionCount - 1;
		} else if ( con.completionCount > 1 ) {
			con.completionSelection = ( con.completionSelection + con.completionCount - 1 ) % con.completionCount;
		}
	} else if ( direction > 0 && Con_CurrentTokenMatchesSelectedCompletion() && con.completionCount > 1 ) {
		con.completionSelection = ( con.completionSelection + 1 ) % con.completionCount;
	}

	match = con.completionMatches[ con.completionSelection ].data();
	matchLen = strlen( match );

	if ( replaceOffset < 0 ) {
		replaceOffset = 0;
	} else if ( replaceOffset > len ) {
		replaceOffset = len;
	}
	if ( replaceLength < 0 ) {
		replaceLength = 0;
	}

	suffixOffset = replaceOffset + replaceLength;
	if ( suffixOffset > len ) {
		suffixOffset = len;
	}

	if ( con.completionAppendSpace ) {
		char next = buffer[ suffixOffset ];

		if ( next == '\0' || next == ';' || next > ' ' ) {
			addSpace = true;
		}
	}

	if ( con.completionPrependSlash ) {
		completed[ outLen++ ] = '/';
	}

	copyLen = replaceOffset;
	if ( copyLen > static_cast<int>( completed.size() ) - 1 - outLen ) {
		copyLen = static_cast<int>( completed.size() ) - 1 - outLen;
	}
	if ( copyLen > 0 ) {
		std::copy_n( buffer, copyLen, completed.data() + outLen );
		outLen += copyLen;
	}

	copyLen = matchLen;
	if ( copyLen > static_cast<int>( completed.size() ) - 1 - outLen ) {
		copyLen = static_cast<int>( completed.size() ) - 1 - outLen;
	}
	if ( copyLen > 0 ) {
		std::copy_n( match, copyLen, completed.data() + outLen );
		outLen += copyLen;
	}

	if ( addSpace && outLen < static_cast<int>( completed.size() ) - 1 ) {
		completed[ outLen++ ] = ' ';
	}

	suffixLen = len - suffixOffset;
	if ( suffixLen > static_cast<int>( completed.size() ) - 1 - outLen ) {
		suffixLen = static_cast<int>( completed.size() ) - 1 - outLen;
	}
	if ( suffixLen > 0 ) {
		std::copy_n( buffer + suffixOffset, suffixLen, completed.data() + outLen );
		outLen += suffixLen;
	}

	completed[ outLen ] = '\0';
	Q_strncpyz( g_consoleField.buffer, completed.data(), sizeof( g_consoleField.buffer ) );
	g_consoleField.cursor = ( con.completionPrependSlash ? 1 : 0 ) +
		replaceOffset + matchLen + ( addSpace ? 1 : 0 );
	Con_AdjustInputScroll( &g_consoleField );
	Con_ClearInputSelection();
	con.focus = ConFocus::Input;
	Con_ClearLogSelection();
	Con_InvalidateCompletionState();
}


static void Con_GetConsoleRect( float *x, float *y, float *w, float *h ) {
	float rectHeight;

	if ( x ) {
		*x = con.xadjust;
	}

	if ( y ) {
		*y = 0.0f;
	}

	if ( w ) {
		*w = ( con.displayWidth > 0.0f ) ? con.displayWidth : cls.glconfig.vidWidth;
	}

	rectHeight = static_cast<float>( con.vislines );
	if ( rectHeight <= 0.0f ) {
		rectHeight = cls.glconfig.vidHeight * con.displayFrac;
		if ( ( Key_GetCatcher() & KEYCATCH_CONSOLE ) && rectHeight < cls.glconfig.vidHeight * 0.5f ) {
			rectHeight = cls.glconfig.vidHeight * 0.5f;
		}
		if ( rectHeight > cls.glconfig.vidHeight ) {
			rectHeight = cls.glconfig.vidHeight;
		}
	}

	if ( h ) {
		*h = rectHeight;
	}
}


static int Con_GetLogRowCount( void ) {
	int rows;

	rows = con.vislines / console_char_height - Con_GetFooterRows() + 1;
	if ( rows < 1 ) {
		rows = 1;
	}

	return rows;
}


static void Con_GetLogAreaRect( float *x, float *y, float *w, float *h ) {
	float consoleX, consoleY, consoleW, consoleH;
	float logBottom;
	float logTop;
	int rows;

	Con_GetConsoleRect( &consoleX, &consoleY, &consoleW, &consoleH );
	rows = Con_GetLogRowCount();
	logBottom = consoleY + consoleH - console_char_height * Con_GetFooterRows();
	logTop = logBottom - rows * console_char_height;
	if ( logTop < consoleY ) {
		logTop = consoleY;
	}

	if ( x ) {
		*x = consoleX;
	}
	if ( y ) {
		*y = logTop;
	}
	if ( w ) {
		*w = consoleW;
	}
	if ( h ) {
		*h = logBottom - logTop;
	}
}


static bool Con_GetInputAreaRect( float *x, float *y, float *w, float *h ) {
	float consoleX, consoleY, consoleW, consoleH;
	int footerRows = Con_GetFooterRows();

	Con_GetConsoleRect( &consoleX, &consoleY, &consoleW, &consoleH );
	if ( consoleW <= 0.0f || consoleH <= console_char_height * footerRows ) {
		return false;
	}

	if ( x ) {
		*x = consoleX + 2 * console_char_width;
	}
	if ( y ) {
		*y = consoleY + consoleH - console_char_height * footerRows;
	}
	if ( w ) {
		*w = consoleW - 3 * console_char_width;
	}
	if ( h ) {
		*h = console_char_height;
	}

	return true;
}


static int Con_GetInputCursorFromMouse( void ) {
	float inputX, inputY, inputW, inputH;
	fnql::font::Utf8FieldWindow window;
	float mouseOffset;
	int cursor;

	if ( !Con_GetInputAreaRect( &inputX, &inputY, &inputW, &inputH ) ) {
		return g_consoleField.cursor;
	}

	window = Con_GetInputDrawInfo( &g_consoleField );
	mouseOffset = con.mouseX - inputX;
	cursor = window.startByte;
	if ( mouseOffset <= 0.0f ) {
		return cursor;
	}

	while ( cursor < window.endByte ) {
		const int next = fnql::font::NextUtf8Boundary( g_consoleField.buffer, cursor );
		const float left = Con_MeasureInputPrefix( &g_consoleField, window, cursor );
		const float right = Con_MeasureInputPrefix( &g_consoleField, window, next );
		if ( mouseOffset < left + ( right - left ) * 0.5f ) {
			break;
		}
		cursor = next;
	}

	return cursor;
}


static bool Con_GetLogPositionFromMouse( int *line, int *column ) {
	float logX, logY, logW, logH;
	int rows;
	int rowIndex;
	int outLine;
	int outColumn;

	Con_GetLogAreaRect( &logX, &logY, &logW, &logH );
	if ( con.mouseX < logX || con.mouseX > logX + logW || con.mouseY < logY || con.mouseY > logY + logH ) {
		return false;
	}

	rows = Con_GetLogRowCount();
	rowIndex = static_cast<int>( ( con.mouseY - logY ) / console_char_height );
	if ( rowIndex < 0 ) {
		rowIndex = 0;
	} else if ( rowIndex >= rows ) {
		rowIndex = rows - 1;
	}

	if ( con.display != con.current && rows > 1 && rowIndex == rows - 1 ) {
		rowIndex = rows - 2;
	}

	outLine = con.display - ( rows - 1 - rowIndex );
	outColumn = static_cast<int>( ( con.mouseX - ( con.xadjust + console_char_width ) ) / console_char_width );

	Con_ClampLogPosition( &outLine, &outColumn );

	if ( line ) {
		*line = outLine;
	}
	if ( column ) {
		*column = outColumn;
	}

	return true;
}


static bool Con_IsInputSelectionHit( int cursor ) {
	int start, end;

	if ( !Con_HasInputSelection() ) {
		return false;
	}

	Con_GetInputSelectionRange( &start, &end );
	return cursor >= start && cursor <= end;
}


static bool Con_IsLogSelectionHit( int line, int column ) {
	int startLine, startColumn, endLine, endColumn;

	if ( !Con_HasLogSelection() ) {
		return false;
	}

	Con_GetLogSelectionRange( &startLine, &startColumn, &endLine, &endColumn );
	return Con_CompareLogPos( line, column, startLine, startColumn ) >= 0 &&
		Con_CompareLogPos( line, column, endLine, endColumn ) <= 0;
}


static void Con_ClearTextDragState( void ) {
	con.textDragPending = false;
	con.textDragging = false;
	con.textDragFromInput = false;
	con.textDragTargetInput = false;
	con.textDragSourceStart = 0;
	con.textDragSourceEnd = 0;
	con.textDragDropCursor = g_consoleField.cursor;
	con.textDragTextLength = 0;
	con.textDragText[ 0 ] = '\0';
}


static bool Con_BeginTextDrag( bool fromInput ) {
	int length;

	if ( fromInput ) {
		length = Con_BuildInputSelectionText( con.textDragText.data(), static_cast<int>( con.textDragText.size() ) );
	} else {
		length = Con_BuildLogSelectionText( con.textDragText.data(), static_cast<int>( con.textDragText.size() ) );
	}

	if ( length < 1 ) {
		Con_ClearTextDragState();
		return false;
	}

	con.textDragging = true;
	con.textDragPending = false;
	con.textDragFromInput = fromInput;
	con.textDragTargetInput = false;
	con.textDragTextLength = length;
	con.inputSelecting = false;
	con.logSelecting = false;
	return true;
}


static void Con_UpdateTextDragTarget( void ) {
	float inputX, inputY, inputW, inputH;

	if ( !con.textDragging ) {
		return;
	}

	if ( Con_GetInputAreaRect( &inputX, &inputY, &inputW, &inputH ) &&
		con.mouseX >= inputX && con.mouseX <= inputX + inputW &&
		con.mouseY >= inputY && con.mouseY <= inputY + inputH ) {
		con.textDragTargetInput = true;
		con.textDragDropCursor = Con_GetInputCursorFromMouse();
	} else {
		con.textDragTargetInput = false;
	}
}


static void Con_FinishTextDrag( void ) {
	std::array<char, MAX_EDIT_LINE> draggedText;
	int dropCursor;

	if ( !con.textDragging ) {
		return;
	}

	if ( con.textDragTargetInput && con.textDragTextLength > 0 ) {
		Q_strncpyz( draggedText.data(), con.textDragText.data(), static_cast<int>( draggedText.size() ) );
		dropCursor = con.textDragDropCursor;

		if ( con.textDragFromInput ) {
			const int sourceStart = con.textDragSourceStart;
			const int sourceEnd = con.textDragSourceEnd;

			if ( dropCursor > sourceStart && dropCursor < sourceEnd ) {
				Con_ClearTextDragState();
				return;
			}

			if ( dropCursor > sourceStart ) {
				dropCursor -= sourceEnd - sourceStart;
			}

			Con_DeleteInputRange( &g_consoleField, sourceStart, sourceEnd );
		}

		Con_InsertInputTextAt( draggedText.data(), dropCursor );
	}

	Con_ClearTextDragState();
}


static bool Con_GetScrollRange( int *minDisplay, int *maxDisplay, int *filled ) {
	int totalLines;

	totalLines = ( con.current >= con.totallines ) ? con.totallines : con.current + 1;
	if ( filled ) {
		*filled = totalLines;
	}

	if ( totalLines <= con.vispage || con.vispage < 1 ) {
		if ( minDisplay ) {
			*minDisplay = con.current;
		}
		if ( maxDisplay ) {
			*maxDisplay = con.current;
		}
		return false;
	}

	if ( maxDisplay ) {
		*maxDisplay = con.current;
	}
	if ( minDisplay ) {
		*minDisplay = con.current - totalLines + con.vispage;
	}

	return true;
}


static void Con_GetScrollbarFrameGeometry( float areaX, float areaW, float areaY, float areaH, float hoverFrac,
	float *trackX, float *trackY, float *trackW, float *trackH, float *hitX, float *hitW ) {
	float width;
	float maxWidth;

	maxWidth = CON_SCROLLBAR_BASE_WIDTH + CON_SCROLLBAR_HOVER_GROW;
	width = CON_SCROLLBAR_BASE_WIDTH + CON_SCROLLBAR_HOVER_GROW * hoverFrac;

	if ( trackX ) {
		*trackX = areaX + areaW - CON_SCROLLBAR_SIDE_PAD - width;
	}
	if ( trackY ) {
		*trackY = areaY;
	}
	if ( trackW ) {
		*trackW = width;
	}
	if ( trackH ) {
		*trackH = areaH;
	}
	if ( hitX ) {
		*hitX = areaX + areaW - CON_SCROLLBAR_SIDE_PAD - maxWidth - CON_SCROLLBAR_HIT_PAD;
	}
	if ( hitW ) {
		*hitW = maxWidth + CON_SCROLLBAR_HIT_PAD * 2.0f;
	}
}


static void Con_GetScrollbarThumbGeometry( float trackY, float trackH, int visibleCount, int filled, float displayFrac,
	float *thumbY, float *thumbH ) {
	float height = trackH;

	if ( filled > 0 && visibleCount > 0 ) {
		height = trackH * ( static_cast<float>( visibleCount ) / static_cast<float>( filled ) );
		if ( height < CON_SCROLLBAR_MIN_THUMB ) {
			height = CON_SCROLLBAR_MIN_THUMB;
		}
		if ( height > trackH ) {
			height = trackH;
		}
	}

	if ( thumbH ) {
		*thumbH = height;
	}

	if ( thumbY ) {
		if ( trackH <= height ) {
			*thumbY = trackY;
		} else {
			if ( displayFrac < 0.0f ) {
				displayFrac = 0.0f;
			} else if ( displayFrac > 1.0f ) {
				displayFrac = 1.0f;
			}
			*thumbY = trackY + ( trackH - height ) * displayFrac;
		}
	}
}


static void Con_DrawScrollbarVisual( float trackX, float trackY, float trackW, float trackH,
	float thumbY, float thumbH, float hoverFrac, float alphaScale, const vec4_t lineColor ) {
	vec4_t trackColor;
	vec4_t thumbColor;

	trackColor[ 0 ] = lineColor[ 0 ];
	trackColor[ 1 ] = lineColor[ 1 ];
	trackColor[ 2 ] = lineColor[ 2 ];
	trackColor[ 3 ] = 0.14f + hoverFrac * 0.08f;

	Con_LightenColor( lineColor, 0.18f + hoverFrac * 0.3f, thumbColor );
	thumbColor[ 3 ] = 0.6f + hoverFrac * 0.2f;

	Con_DrawSolidRect( trackX, trackY, trackW, trackH, trackColor, alphaScale );
	Con_DrawSolidRect( trackX, thumbY, trackW, thumbH, thumbColor, alphaScale );
}


static void Con_UpdateScrollbarHoverValue( float *hoverFrac, bool dragging, bool hot ) {
	float target = ( dragging || hot ) ? 1.0f : 0.0f;
	float step = cls.realFrametime * 0.001f * CON_SCROLLBAR_LERP_SPEED;

	if ( step > 1.0f ) {
		step = 1.0f;
	}

	if ( *hoverFrac < target ) {
		*hoverFrac += step;
		if ( *hoverFrac > target ) {
			*hoverFrac = target;
		}
	} else if ( *hoverFrac > target ) {
		*hoverFrac -= step;
		if ( *hoverFrac < target ) {
			*hoverFrac = target;
		}
	}
}


static bool Con_GetScrollbarGeometry( float hoverFrac, float *trackX, float *trackY, float *trackW, float *trackH,
	float *thumbY, float *thumbH, float *hitX, float *hitW ) {
	float consoleX, consoleY, consoleW, consoleH;
	float logX, logY, logW, logH;
	float displayFrac;
	int minDisplay, maxDisplay, filled;

	if ( !Con_GetScrollRange( &minDisplay, &maxDisplay, &filled ) ) {
		return false;
	}

	Con_GetConsoleRect( &consoleX, &consoleY, &consoleW, &consoleH );
	if ( consoleW <= 0.0f || consoleH <= console_char_height * 4.0f ) {
		return false;
	}

	Con_GetLogAreaRect( &logX, &logY, &logW, &logH );
	if ( logH <= 0.0f ) {
		return false;
	}

	Con_GetScrollbarFrameGeometry( consoleX, consoleW, logY, logH, hoverFrac,
		trackX, trackY, trackW, trackH, hitX, hitW );

	if ( maxDisplay <= minDisplay ) {
		displayFrac = 1.0f;
	} else {
		displayFrac = ( con.displayLine - minDisplay ) / static_cast<float>( maxDisplay - minDisplay );
	}

	Con_GetScrollbarThumbGeometry( logY, logH, con.vispage, filled, displayFrac, thumbY, thumbH );

	return true;
}


static void Con_ClampMouseToConsole( void ) {
	float consoleX, consoleY, consoleW, consoleH;
	float maxX, maxY;

	if ( cls.glconfig.vidWidth <= 0 || cls.glconfig.vidHeight <= 0 ) {
		return;
	}

	Con_GetConsoleRect( &consoleX, &consoleY, &consoleW, &consoleH );

	if ( !con.mouseInitialized ) {
		con.mouseX = consoleX + consoleW - 12.0f;
		con.mouseY = consoleY + ( consoleH > 1.0f ? consoleH * 0.5f : cls.glconfig.vidHeight * 0.25f );
		con.mouseInitialized = true;
	}

	maxX = consoleX + consoleW - 1.0f;
	maxY = consoleY + consoleH - 1.0f;
	if ( maxX < consoleX ) {
		maxX = consoleX;
	}
	if ( maxY < consoleY ) {
		maxY = consoleY;
	}

	if ( con.mouseX < consoleX ) {
		con.mouseX = consoleX;
	} else if ( con.mouseX > maxX ) {
		con.mouseX = maxX;
	}

	if ( con.mouseY < consoleY ) {
		con.mouseY = consoleY;
	} else if ( con.mouseY > maxY ) {
		con.mouseY = maxY;
	}
}


static void Con_SetScrollbarDisplayFrac( float frac ) {
	int minDisplay, maxDisplay;
	int displayRange;

	if ( !Con_GetScrollRange( &minDisplay, &maxDisplay, nullptr ) ) {
		return;
	}

	if ( frac < 0.0f ) {
		frac = 0.0f;
	} else if ( frac > 1.0f ) {
		frac = 1.0f;
	}

	displayRange = maxDisplay - minDisplay;
	if ( displayRange <= 0 ) {
		con.display = maxDisplay;
	} else {
		con.display = minDisplay + RoundToInt( frac * displayRange );
	}

	Con_Fixup();
	con.displayLine = static_cast<float>( con.display );
}


static void Con_UpdateScrollbarDrag( void ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;
	float frac;

	if ( !con.scrollbarDragging || !keys[ K_MOUSE1 ].down ) {
		return;
	}

	if ( !Con_GetScrollbarGeometry( con.scrollbarHover, &trackX, &trackY, &trackW, &trackH, &thumbY, &thumbH, nullptr, nullptr ) ) {
		con.scrollbarDragging = false;
		return;
	}

	if ( trackH <= thumbH ) {
		Con_SetScrollbarDisplayFrac( 1.0f );
		return;
	}

	frac = ( con.mouseY - con.scrollbarDragOffset - trackY ) / ( trackH - thumbH );
	Con_SetScrollbarDisplayFrac( frac );
}


static void Con_SetCompletionScrollFrac( float frac ) {
	int visibleCount = Con_GetCompletionVisibleCount();
	int maxScroll = con.completionCount - visibleCount;

	if ( maxScroll <= 0 ) {
		con.completionScroll = 0;
		return;
	}

	if ( frac < 0.0f ) {
		frac = 0.0f;
	} else if ( frac > 1.0f ) {
		frac = 1.0f;
	}

	con.completionScroll = RoundToInt( frac * maxScroll );
	Con_ClampCompletionScroll( qfalse );
}


static void Con_UpdateCompletionScrollbarDrag( void ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;
	float frac;

	if ( !con.completionScrollbarDragging || !keys[ K_MOUSE1 ].down ) {
		return;
	}

	if ( !Con_GetCompletionScrollbarGeometry( con.completionScrollbarHover,
		&trackX, &trackY, &trackW, &trackH, &thumbY, &thumbH, nullptr, nullptr ) ) {
		con.completionScrollbarDragging = false;
		return;
	}

	if ( trackH <= thumbH ) {
		con.completionScroll = 0;
		return;
	}

	frac = ( con.mouseY - con.completionScrollbarDragOffset - trackY ) / ( trackH - thumbH );
	Con_SetCompletionScrollFrac( frac );
}


static void Con_UpdateScrollbarHover( void ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;
	float hitX, hitW;
	bool hot = false;

	if ( !keys[ K_MOUSE1 ].down ) {
		con.scrollbarDragging = false;
	}

	Con_ClampMouseToConsole();

	if ( ( Key_GetCatcher() & KEYCATCH_CONSOLE ) &&
		Con_GetScrollbarGeometry( con.scrollbarHover, &trackX, &trackY, &trackW, &trackH, &thumbY, &thumbH, &hitX, &hitW ) ) {
		if ( con.scrollbarDragging ||
			( con.mouseX >= hitX && con.mouseX <= hitX + hitW &&
			  con.mouseY >= trackY && con.mouseY <= trackY + trackH ) ) {
			hot = true;
		}
	}

	Con_UpdateScrollbarHoverValue( &con.scrollbarHover, con.scrollbarDragging ? qtrue : qfalse, hot );

	Con_UpdateScrollbarDrag();
}


static void Con_UpdateCompletionScrollbarHover( void ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;
	float hitX, hitW;
	bool hot = false;

	if ( !keys[ K_MOUSE1 ].down ) {
		con.completionScrollbarDragging = false;
	}

	Con_ClampMouseToConsole();

	if ( ( Key_GetCatcher() & KEYCATCH_CONSOLE ) &&
		Con_GetCompletionScrollbarGeometry( con.completionScrollbarHover,
			&trackX, &trackY, &trackW, &trackH, &thumbY, &thumbH, &hitX, &hitW ) ) {
		if ( con.completionScrollbarDragging ||
			( con.mouseX >= hitX && con.mouseX <= hitX + hitW &&
			  con.mouseY >= trackY && con.mouseY <= trackY + trackH ) ) {
			hot = true;
		}
	}

	Con_UpdateScrollbarHoverValue( &con.completionScrollbarHover, con.completionScrollbarDragging ? qtrue : qfalse, hot );
	Con_UpdateCompletionScrollbarDrag();
}


static void Con_UpdateDisplayLine( void ) {
	float target;
	float delta;
	float step;
	float speed;

	target = static_cast<float>( con.display );

	if ( !con_scrollSmooth || !con_scrollSmooth->integer ) {
		con.displayLine = target;
		return;
	}

	if ( con.displayLine < 0.0f || con.displayLine > con.current + con.vispage + 1 ) {
		con.displayLine = target;
		return;
	}

	delta = target - con.displayLine;
	if ( fabs( delta ) < 0.01f ) {
		con.displayLine = target;
		return;
	}

	speed = con_scrollSmoothSpeed ? con_scrollSmoothSpeed->value : 24.0f;
	if ( speed < 1.0f ) {
		speed = 1.0f;
	}

	step = speed * cls.realFrametime * 0.001f;
	if ( step >= fabs( delta ) ) {
		con.displayLine = target;
	} else if ( delta > 0.0f ) {
		con.displayLine += step;
	} else {
		con.displayLine -= step;
	}
}


static void Con_GetSelectionColor( vec4_t outColor ) {
	vec4_t baseColor;

	Con_GetColorCvar( con_lineColor, g_color_table[ ColorIndex( COLOR_RED ) ], baseColor, qfalse );
	Con_LightenColor( baseColor, 0.55f, outColor );
	outColor[ 3 ] = CON_SELECTION_ALPHA;
}


static void Con_DrawInputSelection( const field_t *edit, float x, float y,
	const fnql::font::Utf8FieldWindow& window, float alphaScale ) {
	int start, end;
	int visibleStart, visibleEnd;
	float selectionX;
	float selectionWidth;
	vec4_t selectionColor;

	if ( !Con_HasInputSelection() ) {
		return;
	}

	Con_GetInputSelectionRange( &start, &end );
	visibleStart = start;
	if ( visibleStart < window.startByte ) {
		visibleStart = window.startByte;
	}
	visibleEnd = end;
	if ( visibleEnd > window.endByte ) {
		visibleEnd = window.endByte;
	}

	if ( visibleEnd <= visibleStart ) {
		return;
	}

	selectionX = Con_MeasureInputPrefix( edit, window, visibleStart );
	selectionWidth = Con_MeasureInputPrefix( edit, window, visibleEnd ) - selectionX;
	if ( selectionWidth <= 0.0f ) {
		return;
	}

	Con_GetSelectionColor( selectionColor );
	Con_DrawSolidRect( x + selectionX, y, selectionWidth, console_char_height,
		selectionColor, alphaScale );
}


static void Con_DrawLogSelectionRow( int line, float y, float alphaScale ) {
	int startLine, startColumn, endLine, endColumn;
	int segmentStart;
	int segmentEnd;
	vec4_t selectionColor;

	if ( !Con_HasLogSelection() ) {
		return;
	}

	Con_GetLogSelectionRange( &startLine, &startColumn, &endLine, &endColumn );
	if ( line < startLine || line > endLine ) {
		return;
	}

	segmentStart = ( line == startLine ) ? startColumn : 0;
	segmentEnd = ( line == endLine ) ? endColumn : con.linewidth;
	if ( segmentEnd <= segmentStart ) {
		return;
	}

	Con_GetSelectionColor( selectionColor );
	Con_DrawSolidRect( con.xadjust + ( segmentStart + 1 ) * console_char_width, y,
		( segmentEnd - segmentStart ) * console_char_width, console_char_height,
		selectionColor, alphaScale );
}


static void Con_DrawInputText( field_t *edit, float x, float y, float alphaScale,
	bool drawSelection ) {
	fnql::font::Utf8FieldWindow window;
	int drawBytes;
	int cursorChar;
	int cursorOffset;
	int i;
	int currentColorIndex;
	float cursorX;
	float prefixWidth;
	bool hostTextDrawn;
	std::array<char, MAX_STRING_CHARS> str;

	window = Con_GetInputDrawInfo( edit );
	drawBytes = window.endByte - window.startByte;

	if ( drawBytes >= MAX_STRING_CHARS ) {
		Com_Error( ERR_DROP, "drawBytes >= MAX_STRING_CHARS" );
	}

	std::copy_n( edit->buffer + window.startByte, drawBytes, str.data() );
	str[ drawBytes ] = '\0';

	currentColorIndex = ColorIndex( COLOR_WHITE );
	for ( i = 0; i < window.startByte; i++ ) {
		if ( Q_IsColorString( edit->buffer + i ) ) {
			currentColorIndex = ColorIndexFromChar( edit->buffer[ i + 1 ] );
			i++;
		}
	}

	Con_SetScaledColor( g_color_table[ currentColorIndex ], alphaScale );

	if ( drawSelection ) {
		Con_DrawInputSelection( edit, x, y, window, alphaScale );
	}
	Con_SetScaledColor( g_color_table[ currentColorIndex ], alphaScale );
	hostTextDrawn = Con_DrawHostText( x, y, str.data(), qfalse, con_drawColor );

	for ( i = 0; !hostTextDrawn && i < drawBytes; i++ ) {
		if ( Q_IsColorString( str.data() + i ) ) {
			int colorIndex = ColorIndexFromChar( str[ i + 1 ] );

			if ( colorIndex != currentColorIndex ) {
				currentColorIndex = colorIndex;
				Con_SetScaledColor( g_color_table[ currentColorIndex ], alphaScale );
			}
		}

		Con_DrawSmallCharFloat( x + i * console_char_width, y, str[ i ] );
	}

	Con_SetScaledColor( g_color_table[ ColorIndex( COLOR_WHITE ) ], alphaScale );

	if ( cls.realtime & 256 ) {
		re.SetColor( nullptr );
		return;
	}

	if ( key_overstrikeMode ) {
		cursorChar = 11;
	} else {
		cursorChar = 10;
	}

	if ( hostTextDrawn ) {
		cursorOffset = window.cursorByte - window.startByte;
		if ( cursorOffset < 0 ) {
			cursorOffset = 0;
		} else if ( cursorOffset > drawBytes ) {
			cursorOffset = drawBytes;
		}
		prefixWidth = Con_MeasureInputPrefix( edit, window,
			window.startByte + cursorOffset );
		cursorX = x + prefixWidth;
		if ( !key_overstrikeMode ) {
			cursorX -= console_char_width * 0.5f;
		}
		Con_DrawSmallCharFloat( cursorX, y, cursorChar );
	} else {
		Con_DrawSmallCharFloat( x + fnql::font::CountUtf8Characters( edit->buffer,
			window.startByte, window.cursorByte ) * console_char_width, y, cursorChar );
	}

	re.SetColor( nullptr );
}


static void Con_DrawInputDropCursor( field_t *edit, float x, float y, float alphaScale ) {
	fnql::font::Utf8FieldWindow window;
	int dropCursor;
	float dropX;
	vec4_t selectionColor;

	if ( !con.textDragging || !con.textDragTargetInput ) {
		return;
	}

	window = Con_GetInputDrawInfo( edit );
	dropCursor = con.textDragDropCursor;
	if ( dropCursor < window.startByte ) {
		dropCursor = window.startByte;
	} else if ( dropCursor > window.endByte ) {
		dropCursor = window.endByte;
	}
	dropCursor = fnql::font::ClampUtf8Boundary( edit->buffer, dropCursor );
	dropX = Con_MeasureInputPrefix( edit, window, dropCursor );

	Con_GetSelectionColor( selectionColor );
	selectionColor[ 3 ] = 0.9f;
	Con_DrawSolidRect( x + dropX, y,
		2.0f, console_char_height, selectionColor, alphaScale );
}


static bool Con_GetCompletionPopupGeometry( float *popupX, float *popupY, float *popupW, float *popupH,
	int *first, int *visibleCount ) {
	float consoleX, consoleW;
	float x, y;
	float outPopupX, outPopupY;
	float outPopupW, outPopupH;
	float scrollbarReserve = 0.0f;
	std::array<char, MAX_CVAR_VALUE_STRING> cvarValue;
	int outFirst;
	int outVisibleCount;
	int maxChars;
	int longest;
	int i;

	if ( !Con_CompletionPopupEnabled() ) {
		con.completionPopupVisible = false;
		return false;
	}

	Con_RefreshCompletionState();
	if ( con.completionCount < 1 || con.textDragging || con.focus != ConFocus::Input ) {
		con.completionPopupVisible = false;
		return false;
	}
	con.completionPopupVisible = true;

	outVisibleCount = Con_GetCompletionVisibleCount();
	Con_ClampCompletionScroll( qfalse );
	outFirst = con.completionScroll;

	longest = 0;
	for ( i = 0; i < con.completionCount; i++ ) {
		int matchLen = strlen( con.completionMatches[ i ].data() );
		bool modified;

		if ( Con_GetCompletionCvarInfo( con.completionMatches[ i ].data(), cvarValue.data(), static_cast<int>( cvarValue.size() ), &modified ) ) {
			matchLen += 3 + strlen( cvarValue.data() );
		}

		if ( matchLen > longest ) {
			longest = matchLen;
		}
	}
	if ( longest < 8 ) {
		longest = 8;
	}

	Con_GetConsoleRect( &consoleX, nullptr, &consoleW, nullptr );
	maxChars = static_cast<int>( ( consoleW - 6.0f * console_char_width ) / console_char_width );
	if ( maxChars < 8 ) {
		maxChars = 8;
	}
	if ( longest > maxChars ) {
		longest = maxChars;
	}

	if ( con.completionCount > outVisibleCount ) {
		scrollbarReserve = CON_SCROLLBAR_BASE_WIDTH + CON_SCROLLBAR_HOVER_GROW +
			CON_SCROLLBAR_SIDE_PAD * 2.0f + 1.0f;
	}

	outPopupW = ( longest + 2 ) * console_char_width + scrollbarReserve;
	outPopupH = outVisibleCount * console_char_height + 4.0f;
	x = con.xadjust + 2 * console_char_width;
	y = con.vislines - ( console_char_height * Con_GetFooterRows() );
	outPopupX = x;
	if ( outPopupX + outPopupW > consoleX + consoleW - console_char_width ) {
		outPopupX = consoleX + consoleW - outPopupW - console_char_width;
	}
	if ( outPopupX < consoleX + console_char_width ) {
		outPopupX = consoleX + console_char_width;
	}

	outPopupY = y - outPopupH - 4.0f;
	if ( outPopupY < 0.0f ) {
		outPopupY = 0.0f;
	}

	if ( popupX ) {
		*popupX = outPopupX;
	}
	if ( popupY ) {
		*popupY = outPopupY;
	}
	if ( popupW ) {
		*popupW = outPopupW;
	}
	if ( popupH ) {
		*popupH = outPopupH;
	}
	if ( first ) {
		*first = outFirst;
	}
	if ( visibleCount ) {
		*visibleCount = outVisibleCount;
	}

	return true;
}


static bool Con_GetCompletionScrollbarGeometry( float hoverFrac, float *trackX, float *trackY, float *trackW, float *trackH,
	float *thumbY, float *thumbH, float *hitX, float *hitW ) {
	float popupX, popupY, popupW, popupH;
	float contentY, contentH;
	float displayFrac;
	int first;
	int visibleCount;
	int maxScroll;

	if ( !Con_GetCompletionPopupGeometry( &popupX, &popupY, &popupW, &popupH, &first, &visibleCount ) ) {
		return false;
	}

	if ( con.completionCount <= visibleCount || visibleCount < 1 ) {
		return false;
	}

	contentY = popupY + 2.0f;
	contentH = visibleCount * console_char_height;
	Con_GetScrollbarFrameGeometry( popupX + 1.0f, popupW - 2.0f, contentY, contentH, hoverFrac,
		trackX, trackY, trackW, trackH, hitX, hitW );

	maxScroll = con.completionCount - visibleCount;
	if ( maxScroll <= 0 ) {
		displayFrac = 0.0f;
	} else {
		displayFrac = static_cast<float>( first ) / static_cast<float>( maxScroll );
	}

	Con_GetScrollbarThumbGeometry( contentY, contentH, visibleCount, con.completionCount, displayFrac,
		thumbY, thumbH );

	return true;
}


static bool Con_GetCompletionSelectionFromMouse( int *selection ) {
	float popupX, popupY, popupW, popupH;
	int first, visibleCount;
	int rowIndex;

	if ( !selection ||
		!Con_GetCompletionPopupGeometry( &popupX, &popupY, &popupW, &popupH, &first, &visibleCount ) ) {
		return false;
	}

	if ( con.mouseX < popupX || con.mouseX > popupX + popupW ||
		con.mouseY < popupY + 2.0f || con.mouseY >= popupY + 2.0f + visibleCount * console_char_height ) {
		return false;
	}

	rowIndex = static_cast<int>( ( con.mouseY - ( popupY + 2.0f ) ) / console_char_height );
	if ( rowIndex < 0 || rowIndex >= visibleCount ) {
		return false;
	}

	*selection = first + rowIndex;
	return true;
}


static bool Con_GetCompletionCvarInfo( const char *match, char *value, int valueSize, bool *modified ) {
	unsigned flags;

	if ( modified ) {
		*modified = false;
	}

	if ( value && valueSize > 0 ) {
		value[ 0 ] = '\0';
	}

	if ( !match || !match[ 0 ] ) {
		return false;
	}

	flags = Cvar_Flags( match );
	if ( flags & CVAR_NONEXISTENT ) {
		return false;
	}

	if ( modified ) {
		*modified = ( flags & CVAR_MODIFIED ) != 0;
	}

	if ( value && valueSize > 0 ) {
		if ( flags & CVAR_PRIVATE ) {
			Q_strncpyz( value, "<private>", valueSize );
		} else {
			Cvar_VariableStringBuffer( match, value, valueSize );
			if ( !value[ 0 ] ) {
				Q_strncpyz( value, "\"\"", valueSize );
			}
		}
	}

	return true;
}


static void Con_DrawCompletionPopup( float x, float y, float alphaScale, const vec4_t lineColor ) {
	float popupX, popupY;
	float popupW, popupH;
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;
	float textStartX, textRightX;
	float rowWidth;
	float valueRightX;
	int first;
	int visibleCount;
	int maxDrawChars;
	int i;
	bool hasScrollbar;
	const int valueSeparatorChars = 3;
	vec4_t backgroundColor;
	vec4_t borderColor;
	vec4_t selectionColor;
	vec4_t textColor;
	vec4_t valueColor;
	vec4_t modifiedValueColor;
	vec4_t modifiedValueBg;

	static_cast<void>( x );
	static_cast<void>( y );

	if ( !Con_GetCompletionPopupGeometry( &popupX, &popupY, &popupW, &popupH, &first, &visibleCount ) ) {
		return;
	}
	hasScrollbar = Con_GetCompletionScrollbarGeometry( con.completionScrollbarHover,
		&trackX, &trackY, &trackW, &trackH, &thumbY, &thumbH, nullptr, nullptr );
	textStartX = popupX + console_char_width;
	textRightX = hasScrollbar ? ( trackX - CON_SCROLLBAR_SIDE_PAD ) : ( popupX + popupW - console_char_width );
	valueRightX = textRightX;
	maxDrawChars = static_cast<int>( ( textRightX - textStartX ) / console_char_width );
	if ( maxDrawChars < 1 ) {
		maxDrawChars = 1;
	}
	rowWidth = textRightX - ( popupX + 1.0f );
	if ( rowWidth < 1.0f ) {
		rowWidth = 1.0f;
	}

	backgroundColor[ 0 ] = 0.0f;
	backgroundColor[ 1 ] = 0.0f;
	backgroundColor[ 2 ] = 0.0f;
	backgroundColor[ 3 ] = 1.0f;
	Con_LightenColor( lineColor, 0.25f, borderColor );
	borderColor[ 3 ] = 0.7f;
	Con_GetSelectionColor( selectionColor );
	selectionColor[ 3 ] = 0.85f;
	textColor[ 0 ] = 1.0f;
	textColor[ 1 ] = 1.0f;
	textColor[ 2 ] = 1.0f;
	textColor[ 3 ] = 0.95f;
	Con_LightenColor( lineColor, 0.42f, valueColor );
	valueColor[ 3 ] = 0.72f;
	modifiedValueColor[ 0 ] = 1.0f;
	modifiedValueColor[ 1 ] = 0.84f;
	modifiedValueColor[ 2 ] = 0.46f;
	modifiedValueColor[ 3 ] = 0.98f;
	modifiedValueBg[ 0 ] = modifiedValueColor[ 0 ];
	modifiedValueBg[ 1 ] = modifiedValueColor[ 1 ];
	modifiedValueBg[ 2 ] = modifiedValueColor[ 2 ];
	modifiedValueBg[ 3 ] = 0.14f;

	Con_DrawSolidRect( popupX, popupY, popupW, popupH, backgroundColor, alphaScale );
	Con_DrawSolidRect( popupX, popupY, popupW, 1.0f, borderColor, alphaScale );
	Con_DrawSolidRect( popupX, popupY + popupH - 1.0f, popupW, 1.0f, borderColor, alphaScale );
	Con_DrawSolidRect( popupX, popupY, 1.0f, popupH, borderColor, alphaScale );
	Con_DrawSolidRect( popupX + popupW - 1.0f, popupY, 1.0f, popupH, borderColor, alphaScale );

	for ( i = 0; i < visibleCount; i++ ) {
		const char *match = con.completionMatches[ first + i ].data();
		std::array<char, MAX_CVAR_VALUE_STRING> cvarValue;
		float rowY = popupY + 2.0f + i * console_char_height;
		int nameDrawLen = strlen( match );
		int availableChars = maxDrawChars;
		int valueDrawLen = 0;
		int separatorChars = 0;
		int nameCharsAvailable = maxDrawChars;
		float valueX = valueRightX;
		float separatorX = valueRightX;
		bool cvarModified = false;
		bool isCvar = Con_GetCompletionCvarInfo( match, cvarValue.data(), static_cast<int>( cvarValue.size() ), &cvarModified );
		int j;

		if ( isCvar && availableChars > 6 ) {
			valueDrawLen = strlen( cvarValue.data() );
			if ( valueDrawLen > availableChars - 4 ) {
				valueDrawLen = availableChars - 4;
			}
			if ( valueDrawLen < 0 ) {
				valueDrawLen = 0;
			}
			if ( valueDrawLen > 0 ) {
				separatorChars = valueSeparatorChars;
				nameCharsAvailable = availableChars - separatorChars - valueDrawLen;
				if ( nameCharsAvailable < 1 ) {
					nameCharsAvailable = 1;
					valueDrawLen = availableChars - separatorChars - nameCharsAvailable;
					if ( valueDrawLen < 0 ) {
						valueDrawLen = 0;
						separatorChars = 0;
					}
				}
				valueX = valueRightX - valueDrawLen * console_char_width;
				separatorX = valueX - separatorChars * console_char_width;
			}
		}

		if ( nameDrawLen > nameCharsAvailable ) {
			nameDrawLen = nameCharsAvailable;
		}

		if ( first + i == con.completionSelection ) {
			Con_DrawSolidRect( popupX + 1.0f, rowY, rowWidth, console_char_height,
				selectionColor, alphaScale );
		}

		Con_SetScaledColor( textColor, alphaScale );
		if ( !Con_DrawHostTextSpan( popupX + console_char_width, rowY, match,
			nameDrawLen, qtrue, con_drawColor ) ) {
			for ( j = 0; j < nameDrawLen; j++ ) {
				Con_DrawSmallCharFloat( popupX + console_char_width + j * console_char_width, rowY, match[ j ] );
			}
		}

		if ( valueDrawLen > 0 ) {
			if ( cvarModified ) {
				Con_DrawSolidRect( separatorX - 0.5f * console_char_width, rowY + 1.0f,
					( separatorChars + valueDrawLen + 1 ) * console_char_width, console_char_height - 2.0f,
					modifiedValueBg, alphaScale );
			}

			Con_SetScaledColor( cvarModified ? modifiedValueColor : valueColor, alphaScale );

			if ( separatorChars >= valueSeparatorChars ) {
				if ( !Con_DrawHostText( separatorX, rowY, " = ", qtrue, con_drawColor ) ) {
					Con_DrawSmallCharFloat( separatorX + console_char_width, rowY, '=' );
				}
			}

			if ( !Con_DrawHostTextSpan( valueX, rowY, cvarValue.data(), valueDrawLen,
				qtrue, con_drawColor ) ) {
				for ( j = 0; j < valueDrawLen; j++ ) {
					Con_DrawSmallCharFloat( valueX + j * console_char_width, rowY, cvarValue[ j ] );
				}
			}
		}
	}

	if ( hasScrollbar ) {
		Con_DrawScrollbarVisual( trackX, trackY, trackW, trackH, thumbY, thumbH,
			con.completionScrollbarHover, alphaScale, lineColor );
	}

	re.SetColor( nullptr );
}


static void Con_DrawScrollbar( float alphaScale, const vec4_t lineColor ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;

	if ( !Con_GetScrollbarGeometry( con.scrollbarHover, &trackX, &trackY, &trackW, &trackH, &thumbY, &thumbH, nullptr, nullptr ) ) {
		return;
	}

	Con_DrawScrollbarVisual( trackX, trackY, trackW, trackH, thumbY, thumbH,
		con.scrollbarHover, alphaScale, lineColor );
}


static void Con_DrawMouseCursor( float alphaScale, const vec4_t lineColor ) {
	vec4_t cursorColor;
	vec4_t shadowColor;
	float x, y;

	if ( !( Key_GetCatcher() & KEYCATCH_CONSOLE ) ) {
		return;
	}

	Con_ClampMouseToConsole();
	x = con.mouseX;
	y = con.mouseY;

	if ( cls.cursorShader ) {
		cursorColor[ 0 ] = 1.0f;
		cursorColor[ 1 ] = 1.0f;
		cursorColor[ 2 ] = 1.0f;
		cursorColor[ 3 ] = alphaScale;
		re.SetColor( cursorColor );
		re.DrawStretchPic( x - 16.0f, y - 16.0f, 32.0f, 32.0f, 0, 0, 1, 1, cls.cursorShader );
		re.SetColor( nullptr );
		return;
	}

	Con_LightenColor( lineColor, 0.65f, cursorColor );
	cursorColor[ 3 ] = 0.95f;

	shadowColor[ 0 ] = 0.0f;
	shadowColor[ 1 ] = 0.0f;
	shadowColor[ 2 ] = 0.0f;
	shadowColor[ 3 ] = 0.35f;

	Con_DrawSolidRect( x + 1.0f, y + 1.0f, 3.0f, 13.0f, shadowColor, alphaScale );
	Con_DrawSolidRect( x + 1.0f, y + 1.0f, 10.0f, 3.0f, shadowColor, alphaScale );
	Con_DrawSolidRect( x + 4.0f, y + 4.0f, 3.0f, 3.0f, shadowColor, alphaScale );
	Con_DrawSolidRect( x + 7.0f, y + 7.0f, 3.0f, 3.0f, shadowColor, alphaScale );
	Con_DrawSolidRect( x + 4.0f, y + 11.0f, 7.0f, 3.0f, shadowColor, alphaScale );

	Con_DrawSolidRect( x, y, 3.0f, 13.0f, cursorColor, alphaScale );
	Con_DrawSolidRect( x, y, 10.0f, 3.0f, cursorColor, alphaScale );
	Con_DrawSolidRect( x + 3.0f, y + 3.0f, 3.0f, 3.0f, cursorColor, alphaScale );
	Con_DrawSolidRect( x + 6.0f, y + 6.0f, 3.0f, 3.0f, cursorColor, alphaScale );
	Con_DrawSolidRect( x + 3.0f, y + 10.0f, 7.0f, 3.0f, cursorColor, alphaScale );
}


static void Con_ScrollToLogCursor( void ) {
	int rows;
	int topLine;

	rows = Con_GetLogRowCount();
	topLine = con.display - ( rows - 1 );

	if ( con.logSelectionLine > con.display ) {
		con.display = con.logSelectionLine;
	} else if ( con.logSelectionLine < topLine ) {
		con.display = con.logSelectionLine + rows - 1;
	}

	Con_Fixup();
	con.displayLine = static_cast<float>( con.display );
}


static void Con_MoveLogCursorByChars( int delta, qboolean keepSelection ) {
	int line = con.logSelectionLine;
	int column = con.logSelectionColumn;
	int oldestLine = Con_GetOldestLine();

	while ( delta < 0 ) {
		if ( column > 0 ) {
			column--;
		} else if ( line > oldestLine ) {
			line--;
			column = con.linewidth;
		}
		delta++;
	}

	while ( delta > 0 ) {
		if ( column < con.linewidth ) {
			column++;
		} else if ( line < con.current ) {
			line++;
			column = 0;
		}
		delta--;
	}

	Con_SetLogCursor( line, column, keepSelection );
	Con_ScrollToLogCursor();
}


static void Con_MoveLogCursorByLines( int delta, qboolean keepSelection ) {
	int line = con.logSelectionLine + delta;
	int column = con.logSelectionColumn;

	Con_SetLogCursor( line, column, keepSelection );
	Con_ScrollToLogCursor();
}


static void Con_MoveLogCursorToBoundary( qboolean toStart, qboolean wholeLog, qboolean keepSelection ) {
	int line = con.logSelectionLine;
	int column = con.logSelectionColumn;

	if ( wholeLog ) {
		line = toStart ? Con_GetOldestLine() : con.current;
		column = toStart ? 0 : con.linewidth;
	} else {
		column = toStart ? 0 : con.linewidth;
	}

	Con_SetLogCursor( line, column, keepSelection );
	Con_ScrollToLogCursor();
}


static qboolean Con_HandleLogSelectionKey( int key ) {
	if ( con.focus != ConFocus::Log || !keys[ K_CTRL ].down || !keys[ K_SHIFT ].down ) {
		return qfalse;
	}

	switch ( key ) {
	case K_LEFTARROW:
		Con_MoveLogCursorByChars( -1, qtrue );
		return qtrue;
	case K_RIGHTARROW:
		Con_MoveLogCursorByChars( 1, qtrue );
		return qtrue;
	case K_UPARROW:
	case K_KP_UPARROW:
		Con_MoveLogCursorByLines( -1, qtrue );
		return qtrue;
	case K_DOWNARROW:
	case K_KP_DOWNARROW:
		Con_MoveLogCursorByLines( 1, qtrue );
		return qtrue;
	case K_PGUP:
		Con_MoveLogCursorByLines( -Con_GetScrollStep( 0 ), qtrue );
		return qtrue;
	case K_PGDN:
		Con_MoveLogCursorByLines( Con_GetScrollStep( 0 ), qtrue );
		return qtrue;
	case K_HOME:
		Con_MoveLogCursorToBoundary( qtrue, qtrue, qtrue );
		return qtrue;
	case K_END:
		Con_MoveLogCursorToBoundary( qfalse, qtrue, qtrue );
		return qtrue;
	default:
		return qfalse;
	}
}


qboolean Con_InputKey( int key ) {
	int cursor;
	int len;
	int lowerKey = ( key >= 0 && key < 128 ) ? tolower( key ) : key;

	if ( Con_HandleLogSelectionKey( key ) ) {
		return qtrue;
	}

	if ( Con_HasActiveCompletionPopup() ) {
		switch ( key ) {
		case K_UPARROW:
		case K_KP_UPARROW:
		case K_MWHEELUP:
			Con_MoveCompletionSelection( -1 );
			return qtrue;
		case K_DOWNARROW:
		case K_KP_DOWNARROW:
		case K_MWHEELDOWN:
			Con_MoveCompletionSelection( 1 );
			return qtrue;
		case K_PGUP:
			con.completionSelection -= CON_COMPLETION_MAX_VISIBLE;
			if ( con.completionSelection < 0 ) {
				con.completionSelection = 0;
			}
			Con_ClampCompletionScroll( qtrue );
			return qtrue;
		case K_PGDN:
			con.completionSelection += CON_COMPLETION_MAX_VISIBLE;
			if ( con.completionSelection >= con.completionCount ) {
				con.completionSelection = con.completionCount - 1;
			}
			Con_ClampCompletionScroll( qtrue );
			return qtrue;
		case K_HOME:
			con.completionSelection = 0;
			Con_ClampCompletionScroll( qtrue );
			return qtrue;
		case K_END:
			con.completionSelection = con.completionCount - 1;
			Con_ClampCompletionScroll( qtrue );
			return qtrue;
		case K_ENTER:
		case K_KP_ENTER:
			Con_ApplySelectedCompletion( 0 );
			Con_DismissCompletionPopup();
			return qtrue;
		default:
			break;
		}

		if ( keys[ K_CTRL ].down ) {
			switch ( lowerKey ) {
			case 'p':
				Con_MoveCompletionSelection( -1 );
				return qtrue;
			case 'n':
				Con_MoveCompletionSelection( 1 );
				return qtrue;
			default:
				break;
			}
		}
	}

	if ( keys[ K_CTRL ].down ) {
		switch ( lowerKey ) {
		case 'a':
			if ( con.focus == ConFocus::Log ) {
				Con_SelectAllLog();
			} else {
				Con_SelectAllInput();
			}
			return qtrue;
		case 'c':
			Con_CopySelection();
			return qtrue;
		case 'v':
			Con_PasteClipboardToInput();
			return qtrue;
		case 'x':
			Con_CutInputSelection();
			return qtrue;
		default:
			break;
		}
	}

	if ( key == K_INS || key == K_KP_INS ) {
		if ( keys[ K_SHIFT ].down ) {
			Con_PasteClipboardToInput();
		} else {
			key_overstrikeMode = key_overstrikeMode ? qfalse : qtrue;
		}
		return qtrue;
	}

	if ( key == K_TAB ) {
		if ( Con_CompletionPopupEnabled() ) {
			Con_ApplySelectedCompletion( keys[ K_SHIFT ].down ? -1 : 1 );
		} else {
			Field_AutoComplete( &g_consoleField );
			Con_InvalidateCompletionState();
		}
		return qtrue;
	}

	switch ( key ) {
	case K_BACKSPACE:
		if ( Con_HasInputSelection() ) {
			Con_DeleteInputSelection();
		} else if ( g_consoleField.cursor > 0 ) {
			Con_DeleteInputRange( &g_consoleField,
				fnql::font::PreviousUtf8Boundary( g_consoleField.buffer, g_consoleField.cursor ),
				g_consoleField.cursor );
		}
		return qtrue;
	case K_DEL:
		if ( Con_HasInputSelection() ) {
			Con_DeleteInputSelection();
		} else {
			len = strlen( g_consoleField.buffer );
			if ( g_consoleField.cursor < len ) {
				Con_DeleteInputRange( &g_consoleField, g_consoleField.cursor,
					fnql::font::NextUtf8Boundary( g_consoleField.buffer, g_consoleField.cursor ) );
			}
		}
		return qtrue;
	case K_LEFTARROW:
		if ( keys[ K_SHIFT ].down ) {
			cursor = keys[ K_CTRL ].down ? Con_SeekWordCursor( &g_consoleField, g_consoleField.cursor, -1 ) :
				fnql::font::PreviousUtf8Boundary( g_consoleField.buffer, g_consoleField.cursor );
			Con_SetInputCursor( cursor, qtrue );
		} else if ( Con_HasInputSelection() ) {
			int start, end;
			Con_GetInputSelectionRange( &start, &end );
			Con_SetInputCursor( start, qfalse );
		} else {
			cursor = keys[ K_CTRL ].down ? Con_SeekWordCursor( &g_consoleField, g_consoleField.cursor, -1 ) :
				fnql::font::PreviousUtf8Boundary( g_consoleField.buffer, g_consoleField.cursor );
			Con_SetInputCursor( cursor, qfalse );
		}
		return qtrue;
	case K_RIGHTARROW:
		if ( keys[ K_SHIFT ].down ) {
			cursor = keys[ K_CTRL ].down ? Con_SeekWordCursor( &g_consoleField, g_consoleField.cursor, 1 ) :
				fnql::font::NextUtf8Boundary( g_consoleField.buffer, g_consoleField.cursor );
			Con_SetInputCursor( cursor, qtrue );
		} else if ( Con_HasInputSelection() ) {
			int start, end;
			Con_GetInputSelectionRange( &start, &end );
			Con_SetInputCursor( end, qfalse );
		} else {
			cursor = keys[ K_CTRL ].down ? Con_SeekWordCursor( &g_consoleField, g_consoleField.cursor, 1 ) :
				fnql::font::NextUtf8Boundary( g_consoleField.buffer, g_consoleField.cursor );
			Con_SetInputCursor( cursor, qfalse );
		}
		return qtrue;
	case K_HOME:
		Con_SetInputCursor( 0, keys[ K_SHIFT ].down ? qtrue : qfalse );
		return qtrue;
	case K_END:
		Con_SetInputCursor( strlen( g_consoleField.buffer ), keys[ K_SHIFT ].down ? qtrue : qfalse );
		return qtrue;
	default:
		return qfalse;
	}
}


void Con_CharEvent( int key ) {
	if ( key < ' ' ) {
		return;
	}

	con.focus = ConFocus::Input;
	Con_InsertInputChar( key );
}


void Con_MouseEvent( int dx, int dy ) {
	int line, column;
	float moveX, moveY;

	if ( !( Key_GetCatcher() & KEYCATCH_CONSOLE ) ) {
		return;
	}

	Con_ClampMouseToConsole();

	con.mouseX += dx;
	con.mouseY += dy;

	Con_ClampMouseToConsole();

	if ( con.scrollbarDragging ) {
		Con_UpdateScrollbarDrag();
		return;
	}

	if ( con.completionScrollbarDragging ) {
		Con_UpdateCompletionScrollbarDrag();
		return;
	}

	if ( con.textDragPending && keys[ K_MOUSE1 ].down ) {
		moveX = fabs( con.mouseX - con.textDragStartMouseX );
		moveY = fabs( con.mouseY - con.textDragStartMouseY );
		if ( moveX >= CON_TEXT_DRAG_THRESHOLD || moveY >= CON_TEXT_DRAG_THRESHOLD ) {
			Con_BeginTextDrag( con.textDragFromInput );
		}
	}

	if ( con.textDragging ) {
		Con_UpdateTextDragTarget();
		return;
	}

	if ( con.inputSelecting && keys[ K_MOUSE1 ].down ) {
		Con_SetInputCursor( Con_GetInputCursorFromMouse(), qtrue );
	}

	if ( con.logSelecting && keys[ K_MOUSE1 ].down && Con_GetLogPositionFromMouse( &line, &column ) ) {
		Con_SetLogCursor( line, column, qtrue );
	}

	Con_UpdateScrollbarDrag();
}


qboolean Con_KeyEvent( int key, qboolean down ) {
	float trackX, trackY, trackW, trackH;
	float thumbY, thumbH;
	float hitX, hitW;
	float inputX, inputY, inputW, inputH;
	int completionSelection;
	int line, column;

	if ( !( Key_GetCatcher() & KEYCATCH_CONSOLE ) ) {
		return qfalse;
	}

	if ( !down ) {
		if ( key == K_MOUSE1 ) {
			con.scrollbarDragging = false;
			con.completionScrollbarDragging = false;
			con.inputSelecting = false;
			con.logSelecting = false;
			if ( con.textDragging ) {
				Con_FinishTextDrag();
			} else if ( con.textDragPending ) {
				if ( con.textDragFromInput ) {
					Con_SetInputCursor( Con_GetInputCursorFromMouse(), qfalse );
				} else if ( Con_GetLogPositionFromMouse( &line, &column ) ) {
					Con_SetLogCursor( line, column, qfalse );
				}
				Con_ClearTextDragState();
			}
		}
		return ( key >= K_MOUSE1 && key <= K_MOUSE5 ) ? qtrue : qfalse;
	}

	if ( key < K_MOUSE1 || key > K_MOUSE5 ) {
		return qfalse;
	}

	Con_ClampMouseToConsole();

	if ( key == K_MOUSE1 ) {
		float completionTrackX, completionTrackY, completionTrackW, completionTrackH;
		float completionThumbY, completionThumbH;
		float completionHitX, completionHitW;

		if ( Con_GetCompletionScrollbarGeometry( con.completionScrollbarHover,
			&completionTrackX, &completionTrackY, &completionTrackW, &completionTrackH,
			&completionThumbY, &completionThumbH, &completionHitX, &completionHitW ) &&
			con.mouseX >= completionHitX && con.mouseX <= completionHitX + completionHitW &&
			con.mouseY >= completionTrackY && con.mouseY <= completionTrackY + completionTrackH ) {
			Con_ClearTextDragState();
			con.scrollbarDragging = false;
			con.completionScrollbarDragging = true;
			con.inputSelecting = false;
			con.logSelecting = false;

			if ( con.mouseY >= completionThumbY && con.mouseY <= completionThumbY + completionThumbH ) {
				con.completionScrollbarDragOffset = con.mouseY - completionThumbY;
			} else {
				con.completionScrollbarDragOffset = completionThumbH * 0.5f;
				Con_UpdateCompletionScrollbarDrag();
			}

			return qtrue;
		}
	}

	if ( key == K_MOUSE1 && Con_GetCompletionSelectionFromMouse( &completionSelection ) ) {
		Con_ClearTextDragState();
		con.scrollbarDragging = false;
		con.completionScrollbarDragging = false;
		con.inputSelecting = false;
		con.logSelecting = false;
		con.completionSelection = completionSelection;
		Con_ApplySelectedCompletion( 0 );
		Con_DismissCompletionPopup();
		return qtrue;
	}

	if ( key == K_MOUSE1 &&
		Con_GetScrollbarGeometry( con.scrollbarHover, &trackX, &trackY, &trackW, &trackH, &thumbY, &thumbH, &hitX, &hitW ) &&
		con.mouseX >= hitX && con.mouseX <= hitX + hitW &&
		con.mouseY >= trackY && con.mouseY <= trackY + trackH ) {
		Con_ClearTextDragState();
		con.scrollbarDragging = true;
		con.inputSelecting = false;
		con.logSelecting = false;

		if ( con.mouseY >= thumbY && con.mouseY <= thumbY + thumbH ) {
			con.scrollbarDragOffset = con.mouseY - thumbY;
		} else {
			con.scrollbarDragOffset = thumbH * 0.5f;
			Con_UpdateScrollbarDrag();
		}

		return qtrue;
	}

	if ( key == K_MOUSE1 && Con_GetInputAreaRect( &inputX, &inputY, &inputW, &inputH ) &&
		con.mouseY >= inputY && con.mouseY <= inputY + inputH &&
		con.mouseX >= con.xadjust &&
		con.mouseX <= con.xadjust + ( ( con.displayWidth > 0.0f ) ? con.displayWidth : cls.glconfig.vidWidth ) ) {
		int inputCursor = Con_GetInputCursorFromMouse();

		if ( !keys[ K_SHIFT ].down && Con_IsInputSelectionHit( inputCursor ) ) {
			Con_GetInputSelectionRange( &con.textDragSourceStart, &con.textDragSourceEnd );
			con.textDragPending = true;
			con.textDragFromInput = true;
			con.textDragStartMouseX = con.mouseX;
			con.textDragStartMouseY = con.mouseY;
			con.inputSelecting = false;
			con.logSelecting = false;
			con.focus = ConFocus::Input;
			return qtrue;
		}

		Con_ClearTextDragState();
		Con_SetInputCursor( inputCursor, keys[ K_SHIFT ].down ? qtrue : qfalse );
		if ( !keys[ K_SHIFT ].down ) {
			Con_ClearLogSelection();
		}
		con.inputSelecting = true;
		con.logSelecting = false;
		return qtrue;
	}

	if ( key == K_MOUSE1 && Con_GetLogPositionFromMouse( &line, &column ) ) {
		if ( !keys[ K_SHIFT ].down && Con_IsLogSelectionHit( line, column ) ) {
			con.textDragPending = true;
			con.textDragFromInput = false;
			con.textDragStartMouseX = con.mouseX;
			con.textDragStartMouseY = con.mouseY;
			con.inputSelecting = false;
			con.logSelecting = false;
			return qtrue;
		}

		Con_ClearTextDragState();
		Con_ClearInputSelection();
		Con_SetLogCursor( line, column, ( keys[ K_SHIFT ].down && con.focus == ConFocus::Log ) ? qtrue : qfalse );
		con.inputSelecting = false;
		con.logSelecting = true;
		return qtrue;
	}

	return qtrue;
}


static int Con_GetScrollStep( int lines ) {
	int maxLines;

	if ( lines > 0 ) {
		return lines;
	}

	maxLines = con.vispage - 2;
	if ( maxLines < 1 ) {
		maxLines = 1;
	}

	if ( lines == 0 ) {
		return maxLines;
	}

	if ( !con_scrollLines ) {
		return maxLines < 8 ? maxLines : 8;
	}

	lines = con_scrollLines->integer;
	if ( lines < 1 ) {
		lines = 1;
	}

	if ( lines > maxLines ) {
		lines = maxLines;
	}

	return lines;
}


static void Con_SyncSpeedCvars( void ) {
	if ( con_speedLegacy && con_speedLegacy->modified ) {
		if ( con_conspeed && Q_stricmp( con_conspeed->string, con_speedLegacy->string ) ) {
			Cvar_Set2( "con_speed", con_speedLegacy->string, qtrue );
		}
		con_speedLegacy->modified = qfalse;
		if ( con_conspeed ) {
			con_conspeed->modified = qfalse;
		}
	} else if ( con_conspeed && con_conspeed->modified ) {
		if ( con_speedLegacy && Q_stricmp( con_speedLegacy->string, con_conspeed->string ) ) {
			Cvar_Set2( "scr_conspeed", con_conspeed->string, qtrue );
			con_speedLegacy->modified = qfalse;
		}
		con_conspeed->modified = qfalse;
	}
}

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f( void ) {
	int catcher;

	// Can't toggle the console when it's the only thing available
    if ( cls.state == CA_DISCONNECTED && Key_GetCatcher() == KEYCATCH_CONSOLE ) {
		return;
	}

	if ( con_autoClear->integer ) {
		Field_Clear( &g_consoleField );
	}

	g_consoleField.widthInChars = g_console_field_width;

	Con_ClearNotify();
	catcher = Key_GetCatcher() ^ KEYCATCH_CONSOLE;
	Key_SetCatcher( catcher );

	if ( catcher & KEYCATCH_CONSOLE ) {
		con.focus = ConFocus::Input;
		Con_ClearInputSelection();
		Con_ClearLogSelection();
		con.mouseInitialized = false;
		con.scrollbarDragging = false;
		con.scrollbarHover = 0.0f;
		con.inputSelecting = false;
		con.logSelecting = false;
	} else {
		con.scrollbarDragging = false;
		con.inputSelecting = false;
		con.logSelecting = false;
	}
}


/*
================
Con_MessageMode_f
================
*/
static void Con_ToggleMessageCatcher( void ) {
	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_MESSAGE );
	if ( cgvm && cgvm->dllExports ) {
		VM_Call( cgvm, 0, ( Key_GetCatcher() & KEYCATCH_MESSAGE ) ? CG_CHAT_DOWN : CG_CHAT_UP );
	}
}

static int Con_GetChatFieldY( void ) {
	int chatFieldY = 413;

	if ( cgvm && cgvm->dllExports ) {
		const int cgameY = VM_Call( cgvm, 0, CG_GET_CHAT_FIELD_Y );

		if ( cgameY > 0 ) {
			chatFieldY = cgameY;
		}
	}

	return chatFieldY;
}

static int Con_GetChatFieldPixelWidth( void ) {
	int chatFieldWidth = SCREEN_WIDTH;

	if ( cgvm && cgvm->dllExports ) {
		const int cgameWidth = VM_Call( cgvm, 0, CG_GET_CHAT_FIELD_PIXEL_WIDTH );

		if ( cgameWidth > 0 ) {
			chatFieldWidth = cgameWidth;
		}
	}

	return chatFieldWidth;
}

static int Con_GetChatFieldWidthInChars( qboolean teamChat ) {
	int width = 73;

	if ( cgvm && cgvm->dllExports ) {
		const int cgameWidth = VM_Call( cgvm, 0, CG_GET_CHAT_FIELD_WIDTH_IN_CHARS );

		if ( cgameWidth > 0 ) {
			width = cgameWidth;
		}
	}

	if ( teamChat && width > 5 ) {
		width -= 5;
	}
	if ( width < 1 ) {
		width = 1;
	}

	return width;
}

static void Con_ResetChatField( qboolean teamChat ) {
	Field_Clear( &chatField );
	chatField.widthInChars = Con_GetChatFieldWidthInChars( teamChat );
}

static void Con_MessageMode_f( void ) {
	chat_playerNum = -1;
	chat_team = qfalse;
	Con_ResetChatField( qfalse );

	Con_ToggleMessageCatcher();
}


/*
================
Con_MessageMode2_f
================
*/
static void Con_MessageMode2_f( void ) {
	chat_playerNum = -1;
	chat_team = qtrue;
	Con_ResetChatField( qtrue );
	Con_ToggleMessageCatcher();
}


/*
================
Con_MessageMode3_f
================
*/
static void Con_MessageMode3_f( void ) {
	chat_playerNum = cgvm ? VM_Call( cgvm, 0, CG_CROSSHAIR_PLAYER ) : -1;
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Con_ResetChatField( qfalse );
	Con_ToggleMessageCatcher();
}


/*
================
Con_MessageMode4_f
================
*/
static void Con_MessageMode4_f( void ) {
	chat_playerNum = cgvm ? VM_Call( cgvm, 0, CG_LAST_ATTACKER ) : -1;
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Con_ResetChatField( qfalse );
	Con_ToggleMessageCatcher();
}


/*
================
Con_Clear_f
================
*/
static void Con_Clear_f( void ) {
	int		i;

	for ( i = 0 ; i < con.linewidth ; i++ ) {
		con.text[i] = ( ColorIndex( COLOR_WHITE ) << 8 ) | ' ';
	}

	con.x = 0;
	con.current = 0;
	con.newline = true;
	con.logSelectionAnchorLine = 0;
	con.logSelectionAnchorColumn = 0;
	con.logSelectionLine = 0;
	con.logSelectionColumn = 0;
	con.focus = ConFocus::Input;
	Con_ClearInputSelection();
	Con_ClearLogSelection();

	Con_Bottom();		// go to end
}

						
/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
static void Con_Dump_f( void )
{
	int		l, x, i, n;
	short	*line;
	int		bufferlen;
	std::array<char, MAX_OSPATH> filename;
	const char *ext;

	if ( Cmd_Argc() != 2 )
	{
		Com_Printf( "usage: condump <filename>\n" );
		return;
	}

	Q_strncpyz( filename.data(), Cmd_Argv( 1 ), static_cast<int>( filename.size() ) );
	COM_DefaultExtension( filename.data(), static_cast<int>( filename.size() ), ".txt" );

	if ( !FS_AllowedExtension( filename.data(), qfalse, &ext ) ) {
		Com_Printf( "%s: Invalid filename extension '%s'.\n", __func__, ext );
		return;
	}

	ScopedFileHandle file( FS_FOpenFileWrite( filename.data() ) );
	if ( !file )
	{
		Com_Printf( "ERROR: couldn't open %s.\n", filename.data() );
		return;
	}

	Com_Printf( "Dumped console text to %s.\n", filename.data() );

	if ( con.current >= con.totallines ) {
		n = con.totallines;
		l = con.current + 1;
	} else {
		n = con.current + 1;
		l = 0;
	}

	bufferlen = con.linewidth + ARRAY_LEN( Q_NEWLINE ) * sizeof( char );
	auto bufferStorage = ScopedTempMemory::Allocate( bufferlen );
	char *buffer = bufferStorage.as<char>();

	// write the remaining lines
	buffer[ bufferlen - 1 ] = '\0';

	for ( i = 0; i < n ; i++, l++ ) 
	{
		line = con.text.data() + (l % con.totallines) * con.linewidth;
		// store line
		for( x = 0; x < con.linewidth; x++ )
			buffer[ x ] = line[ x ] & 0xff;
		buffer[ con.linewidth ] = '\0';
		// terminate on ending space characters
		for ( x = con.linewidth - 1 ; x >= 0 ; x-- ) {
			if ( buffer[ x ] == ' ' )
				buffer[ x ] = '\0';
			else
				break;
		}
		Q_strcat( buffer, bufferlen, Q_NEWLINE );
		FileWrite( file.get(), buffer, strlen( buffer ) );
	}
}

						
/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify( void ) {
	int		i;
	
	for ( i = 0 ; i < NUM_CON_TIMES ; i++ ) {
		con.times[i] = 0;
	}
}


/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize( void )
{
	int		i, j, width, oldwidth, oldtotallines, oldcurrent, numlines, numchars;
	std::array<short, CON_TEXTSIZE> tbuf;
	short *src, *dst;
	int		vispage;
	float	scale;
	float	charScale;
	float	legacyCharScale;
	float	contentWidth;
	float	contentHeight;
	float	xadjust;
	float	displayWidth;
	bool	uniformScale;
	bool	centeredExtents;
	bool	scaleModified;
	bool	uniformModified;
	bool	extentsModified;
	bool	showClockModified;
	bool	showVersionModified;
	bool	sizeChanged;
	bool	widthChanged;
	int		footerRows;

	scale = con_scale ? con_scale->value : 1.0f;
	if ( scale <= 0.0f ) {
		scale = 1.0f;
	}

	uniformScale = con_scaleUniform && con_scaleUniform->integer;
	centeredExtents = con_screenExtents && con_screenExtents->integer;
	scaleModified = con_scale && con_scale->modified;
	uniformModified = con_scaleUniform && con_scaleUniform->modified;
	extentsModified = con_screenExtents && con_screenExtents->modified;
	showClockModified = con_showClock && con_showClock->modified;
	showVersionModified = con_showVersion && con_showVersion->modified;

	charScale = scale * RETAIL_CONSOLE_SCALE_UNIT * cls.con_factor;
	if ( uniformScale && cls.glconfig.vidHeight > 0 ) {
		charScale *= cls.glconfig.vidHeight / RETAIL_CONSOLE_REFERENCE_HEIGHT;
	}
	con_ttfCellWidth = RETAIL_CONSOLE_CHAR_WIDTH * charScale;
	if ( con_ttfCellWidth <= 0.0f ) {
		con_ttfCellWidth = RETAIL_CONSOLE_CHAR_WIDTH;
	}

	console_char_width = RoundToInt( RETAIL_CONSOLE_CHAR_WIDTH * charScale );
	console_char_height = RoundToInt( RETAIL_CONSOLE_CHAR_HEIGHT * charScale );
	// Retail con_scale is console-owned. Keep FnQ3's capture-resolution factor
	// for legacy bitmap overlays without leaking console sizing into them.
	legacyCharScale = cls.con_factor;
	smallchar_width = RoundToInt( SMALLCHAR_WIDTH * legacyCharScale );
	smallchar_height = RoundToInt( SMALLCHAR_HEIGHT * legacyCharScale );
	bigchar_width = RoundToInt( BIGCHAR_WIDTH * legacyCharScale );
	bigchar_height = RoundToInt( BIGCHAR_HEIGHT * legacyCharScale );

	if ( console_char_width < 1 ) {
		console_char_width = 1;
	}
	if ( console_char_height < 1 ) {
		console_char_height = 1;
	}
	if ( smallchar_width < 1 ) {
		smallchar_width = 1;
	}
	if ( smallchar_height < 1 ) {
		smallchar_height = 1;
	}
	if ( bigchar_width < 1 ) {
		bigchar_width = 1;
	}
	if ( bigchar_height < 1 ) {
		bigchar_height = 1;
	}

	if ( cls.glconfig.vidWidth == 0 ) // video hasn't been initialized yet
	{
		g_console_field_width = DEFAULT_CONSOLE_WIDTH;
		width = DEFAULT_CONSOLE_WIDTH;
		if ( width < 1 ) {
			width = 1;
		}
		con.linewidth = width;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		con.vispage = 4;
		con.viswidth = 0;
		con.visheight = 0;
		con.xadjust = 0.0f;
		con.displayWidth = 0.0f;
		con.displayLine = 0.0f;
		g_consoleField.widthInChars = g_console_field_width;

		Con_Clear_f();
	}
	else
	{
		contentWidth = cls.glconfig.vidWidth;
		contentHeight = cls.glconfig.vidHeight;
		xadjust = 0.0f;
		displayWidth = cls.glconfig.vidWidth;

		if ( centeredExtents ) {
			if ( cls.biasX > 0.0f ) {
				contentWidth -= cls.biasX * 2.0f;
				xadjust = cls.biasX;
				displayWidth = contentWidth;
			}
		}

		if ( contentWidth <= 0.0f ) {
			contentWidth = cls.glconfig.vidWidth;
			xadjust = 0.0f;
			displayWidth = cls.glconfig.vidWidth;
		}

		width = static_cast<int>( contentWidth / console_char_width ) - 2;
		if ( width < 1 ) {
			width = 1;
		}

		if ( width > MAX_CONSOLE_WIDTH )
			width = MAX_CONSOLE_WIDTH;

		footerRows = Con_GetFooterRows();
		vispage = static_cast<int>( contentHeight / ( console_char_height * 2 ) ) - footerRows + 1;
		if ( vispage < 1 ) {
			vispage = 1;
		}

		sizeChanged = con.viswidth != cls.glconfig.vidWidth || con.visheight != cls.glconfig.vidHeight ||
			con.xadjust != xadjust || con.displayWidth != displayWidth;
		widthChanged = con.linewidth != width;

		con.viswidth = cls.glconfig.vidWidth;
		con.visheight = cls.glconfig.vidHeight;
		con.xadjust = xadjust;
		con.displayWidth = displayWidth;
		g_console_field_width = width;
		g_consoleField.widthInChars = g_console_field_width;

		if ( !widthChanged && con.vispage == vispage && !sizeChanged &&
			!scaleModified && !uniformModified && !extentsModified &&
			!showClockModified && !showVersionModified ) {
			return;
		}

		if ( !widthChanged ) {
			con.vispage = vispage;
			Con_Fixup();
			con.displayLine = static_cast<float>( con.display );
		} else {
			oldwidth = con.linewidth;
			oldtotallines = con.totallines;
			oldcurrent = con.current;

			con.linewidth = width;
			con.totallines = CON_TEXTSIZE / con.linewidth;
			con.vispage = vispage;

			numchars = oldwidth;
			if ( numchars > con.linewidth )
				numchars = con.linewidth;

			if ( oldcurrent > oldtotallines )
				numlines = oldtotallines;
			else
				numlines = oldcurrent + 1;

			if ( numlines > con.totallines )
				numlines = con.totallines;

			std::copy_n( con.text.begin(), CON_TEXTSIZE, tbuf.begin() );

			for ( i = 0; i < CON_TEXTSIZE; i++ )
				con.text[i] = (ColorIndex(COLOR_WHITE)<<8) | ' ';

			for ( i = 0; i < numlines; i++ )
			{
				src = &tbuf[ ((oldcurrent - i + oldtotallines) % oldtotallines) * oldwidth ];
				dst = con.text.data() + (numlines - 1 - i) * con.linewidth;
				for ( j = 0; j < numchars; j++ )
					*dst++ = *src++;
			}

			Con_ClearNotify();

			con.current = numlines - 1;
			con.display = con.current;
			con.displayLine = static_cast<float>( con.display );
		}
	}

	Con_AdjustInputScroll( &g_consoleField );
	con_scale->modified = qfalse;
	if ( con_scaleUniform ) {
		con_scaleUniform->modified = qfalse;
	}
	if ( con_screenExtents ) {
		con_screenExtents->modified = qfalse;
	}
	if ( con_showClock ) {
		con_showClock->modified = qfalse;
	}
	if ( con_showVersion ) {
		con_showVersion->modified = qfalse;
	}
	if ( con.mouseInitialized ) {
		Con_ClampMouseToConsole();
	}
}


/*
==================
Cmd_CompleteTxtName
==================
*/
static void Cmd_CompleteTxtName(const char *args, int argNum ) {
	if ( argNum == 2 ) {
		Field_CompleteFilename( "", "txt", qfalse, FS_MATCH_EXTERN | FS_MATCH_STICK );
	}
}


/*
================
Con_Init
================
*/
void Con_Init( void ) 
{
	const char *speedValue;

	con_notifytime = Cvar_Get( "con_notifytime", "3", 0 );
	Cvar_SetDescription( con_notifytime, "Defines how long messages (from players or the system) are on the screen (in seconds)." );
	speedValue = Cvar_VariableString( "con_speed" );
	if ( !speedValue[ 0 ] ) {
		speedValue = Cvar_VariableString( "scr_conspeed" );
	}
	if ( !speedValue[ 0 ] ) {
		speedValue = "3";
	}
	con_conspeed = Cvar_Get( "con_speed", speedValue, CVAR_ARCHIVE_ND );
	Cvar_SetDescription( con_conspeed, "Console opening/closing scroll speed." );
	con_speedLegacy = Cvar_Get( "scr_conspeed", con_conspeed->string, CVAR_NOTABCOMPLETE );
	Cvar_SetDescription( con_speedLegacy, "Deprecated alias for con_speed." );
	con_autoClear = Cvar_Get("con_autoClear", "1", CVAR_ARCHIVE_ND);
	Cvar_SetDescription( con_autoClear, "Enable/disable clearing console input text when console is closed." );
	con_scale = Cvar_Get( "con_scale", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_scale, "0.5", "8", CV_FLOAT );
	Cvar_SetDescription( con_scale, "Console font size scale." );
	con_scaleUniform = Cvar_Get( "con_scaleUniform", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_scaleUniform, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_scaleUniform, "Use retail height-derived 768-reference scaling for console font metrics instead of native pixel sizing." );
	con_screenExtents = Cvar_Get( "con_screenExtents", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_screenExtents, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_screenExtents,
		"Console display extents:\n"
		" 0 - use the full screen width\n"
		" 1 - keep the console display in centered 4:3 space" );
	con_timestamps = Cvar_Get( "con_timestamps", "0", CVAR_ARCHIVE_ND | CVAR_PROTECTED );
	Cvar_SetDescription( con_timestamps, "Show Quake Live style elapsed-time prefixes on console and notify lines." );
	con_scrollLines = Cvar_Get( "con_scrollLines", "8", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_scrollLines, "1", "256", CV_INTEGER );
	Cvar_SetDescription( con_scrollLines, "Number of console lines scrolled per step, clamped to the current visible console page." );
	con_backgroundStyle = Cvar_Get( "con_backgroundStyle", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_backgroundStyle, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_backgroundStyle,
		"Console background style:\n"
		" 0 - legacy textured background\n"
		" 1 - flat shaded background" );
	con_backgroundColor = Cvar_Get( "con_backgroundColor", "24 0 0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( con_backgroundColor, "Console background RGB color as R G B values from 0-255. Empty keeps the style default or legacy cl_conColor fallback." );
	con_backgroundOpacity = Cvar_Get( "con_backgroundOpacity", "0.8", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_backgroundOpacity, "0", "1", CV_FLOAT );
	Cvar_SetDescription( con_backgroundOpacity, "Console background opacity from 0 to 1." );
	con_scrollSmooth = Cvar_Get( "con_scrollSmooth", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_scrollSmooth, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_scrollSmooth, "Smoothly animate console scrollback and new line movement." );
	con_scrollSmoothSpeed = Cvar_Get( "con_scrollSmoothSpeed", "72", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_scrollSmoothSpeed, "1", "240", CV_FLOAT );
	Cvar_SetDescription( con_scrollSmoothSpeed, "Console smooth scrolling speed in lines per second." );
	con_completionPopup = Cvar_Get( "con_completionPopup", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_completionPopup, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_completionPopup, "Show the live console completion popup while typing. Disable to keep classic Tab completion behavior." );
	con_autoSay = Cvar_Get( "con_autoSay", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_autoSay, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_autoSay, "Implicitly treat slashless console input as say text while in-game. Disabled by default so bare input executes as a command." );
	con_sayRaw = Cvar_Get( "con_sayRaw", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_sayRaw, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_sayRaw, "Use quoted raw console input for in-game plain-text say chat instead of legacy cmd say tokenization. Disabled by default." );
	con_showClock = Cvar_Get( "con_showClock", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_showClock, "0", "2", CV_INTEGER );
	Cvar_SetDescription( con_showClock,
		"Show the current system time in the console header:\n"
		" 0 - off\n"
		" 1 - 24-hour clock\n"
		" 2 - 12-hour AM/PM clock" );
	con_showVersion = Cvar_Get( "con_showVersion", "1", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_showVersion, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_showVersion, "Show the console version string in the bottom-right header area." );
	con_lineColor = Cvar_Get( "con_lineColor", "255 0 0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( con_lineColor, "Console separator and scrollback marker RGB color as R G B values from 0-255." );
	con_versionColor = Cvar_Get( "con_versionColor", "255 0 0", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( con_versionColor, "Console version and clock text RGB color as R G B values from 0-255." );
	con_fade = Cvar_Get( "con_fade", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_fade, "0", "1", CV_INTEGER );
	Cvar_SetDescription( con_fade, "Fade console background and text in and out while opening or closing the console." );

	Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;
	con.inputSelectionAnchor = -1;
	con.logSelectionAnchorLine = 0;
	con.logSelectionAnchorColumn = 0;
	con.logSelectionLine = 0;
	con.logSelectionColumn = 0;
	con.focus = ConFocus::Input;
	Con_ClearTextDragState();
	Con_InvalidateCompletionState();

	Cmd_AddCommand( "clear", Con_Clear_f );
	Cmd_AddCommand( "condump", Con_Dump_f );
	Cmd_SetCommandCompletionFunc( "condump", Cmd_CompleteTxtName );
	Cmd_AddCommand( "toggleconsole", Con_ToggleConsole_f );
	Cmd_AddCommand( "messagemode", Con_MessageMode_f );
	Cmd_AddCommand( "messagemode2", Con_MessageMode2_f );
	Cmd_AddCommand( "messagemode3", Con_MessageMode3_f );
	Cmd_AddCommand( "messagemode4", Con_MessageMode4_f );
}


/*
================
Con_Shutdown
================
*/
void Con_Shutdown( void )
{
	Cmd_RemoveCommand( "clear" );
	Cmd_RemoveCommand( "condump" );
	Cmd_RemoveCommand( "toggleconsole" );
	Cmd_RemoveCommand( "messagemode" );
	Cmd_RemoveCommand( "messagemode2" );
	Cmd_RemoveCommand( "messagemode3" );
	Cmd_RemoveCommand( "messagemode4" );
}


/*
===============
Con_Fixup
===============
*/
static void Con_Fixup( void ) 
{
	int filled;

	if ( con.current >= con.totallines ) {
		filled = con.totallines;
	} else {
		filled = con.current + 1;
	}

	if ( filled <= con.vispage ) {
		con.display = con.current;
	} else if ( con.current - con.display > filled - con.vispage ) {
		con.display = con.current - filled + con.vispage;
	} else if ( con.display > con.current ) {
		con.display = con.current;
	}

	if ( !con_scrollSmooth || !con_scrollSmooth->integer ||
		con.displayLine < static_cast<float>( con.current - con.totallines ) ||
		con.displayLine > static_cast<float>( con.current + con.vispage + 1 ) ) {
		con.displayLine = static_cast<float>( con.display );
	}

	Con_ClampLogPosition( &con.logSelectionAnchorLine, &con.logSelectionAnchorColumn );
	Con_ClampLogPosition( &con.logSelectionLine, &con.logSelectionColumn );
	if ( con.focus != ConFocus::Log && !Con_HasLogSelection() ) {
		con.logSelectionAnchorLine = con.current;
		con.logSelectionAnchorColumn = 0;
		con.logSelectionLine = con.current;
		con.logSelectionColumn = 0;
	}

	if ( con.scrollbarDragging && !keys[ K_MOUSE1 ].down ) {
		con.scrollbarDragging = false;
	}
	if ( ( con.textDragging || con.textDragPending ) && !keys[ K_MOUSE1 ].down ) {
		Con_ClearTextDragState();
	}
}


/*
===============
Con_Linefeed

Move to newline only when we _really_ need this
===============
*/
static void Con_NewLine( void )
{
	short *s;
	int i;

	// follow last line
	if ( con.display == con.current )
		con.display++;
	con.current++;

	s = con.text.data() + ( con.current % con.totallines ) * con.linewidth;
	for ( i = 0; i < con.linewidth ; i++ ) 
		*s++ = (ColorIndex(COLOR_WHITE)<<8) | ' ';

	con.x = 0;
}


/*
===============
Con_Linefeed
===============
*/
static void Con_Linefeed( bool skipnotify )
{
	// mark time for transparent overlay
	if ( con.current >= 0 )	{
		if ( skipnotify )
			con.times[ con.current % NUM_CON_TIMES ] = 0;
		else
			con.times[ con.current % NUM_CON_TIMES ] = cls.realtime;
	}

	if ( con.newline ) {
		Con_NewLine();
	} else {
		con.newline = true;
		con.x = 0;
	}

	Con_Fixup();
}

static int Con_GetTimestampTime( void ) {
	int timestampTime = cl.serverTime;

	if ( con_timestamps && con_timestamps->integer == 1 ) {
		const int physicsTime = CL_GetCGamePhysicsTime();

		if ( physicsTime > 0 ) {
			timestampTime = physicsTime;
		}
	}

	if ( timestampTime < 0 ) {
		timestampTime = 0;
	}

	return timestampTime;
}

static void Con_FormatTimestamp( char *buffer, int bufferSize ) {
	int timestampTime;
	int totalSeconds;
	int minutes;
	int seconds;
	int millis;

	if ( !buffer || bufferSize <= 0 ) {
		return;
	}

	timestampTime = Con_GetTimestampTime();
	totalSeconds = timestampTime / 1000;
	minutes = totalSeconds / 60;
	seconds = totalSeconds % 60;
	millis = timestampTime % 1000;

	Com_sprintf( buffer, bufferSize, "[%d:%02d.%03d] ", minutes, seconds, millis );
}

static void Con_WriteTimestampPrefix( bool skipnotify, int colorIndex ) {
	char timestamp[32];
	int timestampLength;
	int timestampIndex;

	if ( !con_timestamps || !con_timestamps->integer || con.x != 0 ) {
		return;
	}

	Con_FormatTimestamp( timestamp, sizeof( timestamp ) );
	timestampLength = strlen( timestamp );

	for ( timestampIndex = 0; timestampIndex < timestampLength; timestampIndex++ ) {
		if ( con.newline ) {
			Con_NewLine();
			Con_Fixup();
			con.newline = false;
		}

		con.text[( con.current % con.totallines ) * con.linewidth + con.x] =
			( colorIndex << 8 ) | timestamp[timestampIndex];
		con.x++;
		if ( con.x >= con.linewidth ) {
			Con_Linefeed( skipnotify );
		}
	}
}


/*
================
CL_ConsolePrint

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
================
*/
void CL_ConsolePrint( const char *txt ) {
	int		y;
	int		c, l;
	int		colorIndex;
	bool skipnotify = false;			// NERVE - SMF
	int prev;							// NERVE - SMF

	// TTimo - prefix for text that shows up in console but not in notify
	// backported from RTCW
	if ( !Q_strncmp( txt, "[skipnotify]", 12 ) ) {
		skipnotify = true;
		txt += 12;
	}

	// for some demos we don't want to ever show anything on the console
	if ( cl_noprint && cl_noprint->integer ) {
		return;
	}
	
	if ( !con.initialized ) {
		static cvar_t null_cvar = {};
		con.color[0] =
		con.color[1] =
		con.color[2] =
		con.color[3] = 1.0f;
		con.viswidth = -9999;
		cls.con_factor = 1.0f;
		con_scale = &null_cvar;
		con_scale->value = 1.0f;
		con_scale->modified = qtrue;
		Con_CheckResize();
		con.initialized = true;
	}

	colorIndex = ColorIndex( COLOR_WHITE );

	Con_WriteTimestampPrefix( skipnotify, colorIndex );

	while ( (c = *txt) != 0 ) {
		if ( Q_IsColorString( txt ) && *(txt+1) != '\n' ) {
			colorIndex = ColorIndexFromChar( *(txt+1) );
			txt += 2;
			continue;
		}

		// count word length
		for ( l = 0 ; l < con.linewidth ; l++ ) {
			if ( txt[l] <= ' ' ) {
				break;
			}
		}

		// word wrap
		if ( l != con.linewidth && ( con.x + l >= con.linewidth ) ) {
			Con_Linefeed( skipnotify );
		}

		txt++;

		switch( c )
		{
		case '\n':
			Con_Linefeed( skipnotify );
			break;
		case '\r':
			con.x = 0;
			break;
		default:
			if ( con.newline ) {
				Con_NewLine();
				Con_Fixup();
				con.newline = false;
			}
			// display character and advance
			y = con.current % con.totallines;
			con.text[y * con.linewidth + con.x ] = (colorIndex << 8) | (c & 255);
			con.x++;
			if ( con.x >= con.linewidth ) {
				Con_Linefeed( skipnotify );
			}
			break;
		}
	}

	// mark time for transparent overlay
	if ( con.current >= 0 ) {
		if ( skipnotify ) {
			prev = con.current % NUM_CON_TIMES - 1;
			if ( prev < 0 )
				prev = NUM_CON_TIMES - 1;
			con.times[ prev ] = 0;
		} else {
			con.times[ con.current % NUM_CON_TIMES ] = cls.realtime;
		}
	}
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

Draw the editline after a ] prompt
================
*/
static void Con_DrawInput( float alphaScale, const vec4_t lineColor ) {
	int		y;
	vec4_t	color;

	if ( cls.state != CA_DISCONNECTED && !(Key_GetCatcher( ) & KEYCATCH_CONSOLE ) ) {
		return;
	}

	y = con.vislines - ( console_char_height * Con_GetFooterRows() );

	color[ 0 ] = con.color[ 0 ];
	color[ 1 ] = con.color[ 1 ];
	color[ 2 ] = con.color[ 2 ];
	color[ 3 ] = con.color[ 3 ];

	Con_SetScaledColor( color, alphaScale );
	if ( !Con_DrawHostText( con.xadjust + console_char_width, y, "]", qtrue, con_drawColor ) ) {
		Con_DrawSmallCharFloat( con.xadjust + console_char_width, y, ']' );
	}
	Con_DrawInputText( &g_consoleField, con.xadjust + 2 * console_char_width, y,
		alphaScale, true );
	Con_DrawInputDropCursor( &g_consoleField, con.xadjust + 2 * console_char_width, y, alphaScale );
	Con_DrawCompletionPopup( con.xadjust + 2 * console_char_width, y, alphaScale, lineColor );
}


/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
static void Con_DrawNotify( void )
{
	int		x, v;
	short	*text;
	int		i;
	int		time;
	int		currentColorIndex;
	int		colorIndex;

	currentColorIndex = ColorIndex( COLOR_WHITE );
	Con_SetScaledColor( g_color_table[ currentColorIndex ], 1.0f );
	Con_UpdateTtfFontAvailability();

	v = 0;
	for (i= con.current-NUM_CON_TIMES+1 ; i<=con.current ; i++)
	{
		if (i < 0)
			continue;
		time = con.times[i % NUM_CON_TIMES];
		if (time == 0)
			continue;
		time = cls.realtime - time;
		if ( time >= con_notifytime->value*1000 )
			continue;
		text = con.text.data() + (i % con.totallines)*con.linewidth;

		if (cl.snap.ps.pm_type != PM_INTERMISSION && Key_GetCatcher( ) & (KEYCATCH_UI | KEYCATCH_CGAME | KEYCATCH_BROWSER) ) {
			continue;
		}

		if ( !Con_DrawConsoleLineText( cl_conXOffset->integer + con.xadjust + console_char_width,
			v, text, con.linewidth, 1.0f ) ) {
			for (x = 0 ; x < con.linewidth ; x++) {
				if ( ( text[x] & 0xff ) == ' ' ) {
					continue;
				}
				colorIndex = ( text[x] >> 8 ) & 63;
				if ( currentColorIndex != colorIndex ) {
					currentColorIndex = colorIndex;
					Con_SetScaledColor( g_color_table[ colorIndex ], 1.0f );
				}
				Con_DrawSmallCharFloat( cl_conXOffset->integer + con.xadjust + (x+1)*console_char_width, v, text[x] & 0xff );
			}
		}

		v += console_char_height;
	}

	re.SetColor( nullptr );

	if ( Key_GetCatcher() & (KEYCATCH_UI | KEYCATCH_CGAME | KEYCATCH_BROWSER) ) {
		return;
	}

	// draw the chat line
	if ( Key_GetCatcher( ) & KEYCATCH_MESSAGE )
	{
		const char *prompt = chat_team ? "say team:" : "say:";
		const int promptCells = chat_team ? 11 : 6;
		const int chatFieldY = Con_GetChatFieldY();
		const int chatFieldPixelWidth = Con_GetChatFieldPixelWidth();
		const int promptLength = static_cast<int>( strlen( prompt ) );
		float promptX = 8.0f;
		float promptY = static_cast<float>( chatFieldY );
		int promptIndex;

		// Retail QL gives chat a translucent strip in virtual coordinates, then
		// draws its prompt and field with console font 2 in screen coordinates.
		SCR_FillRect( 6.0f, chatFieldY - 3.0f,
			chatFieldPixelWidth + 12.0f, 22.0f, con_chatBackgroundColor );
		SCR_AdjustFrom640( &promptX, &promptY, nullptr, nullptr );

		Con_SetScaledColor( con_chatPromptColor, 1.0f );
		if ( !Con_DrawHostText( promptX, promptY, prompt, qtrue, con_drawColor ) ) {
			for ( promptIndex = 0; promptIndex < promptLength; ++promptIndex ) {
				Con_DrawSmallCharFloat( promptX + promptIndex * console_char_width,
					promptY, prompt[ promptIndex ] );
			}
		}

		Con_SetScaledColor( g_color_table[ ColorIndex( COLOR_WHITE ) ], 1.0f );
		Con_DrawInputText( &chatField, promptX + promptCells * console_char_width,
			promptY, 1.0f, false );
	}
}


/*
================
Con_DrawSolidConsole

Draws the console with the solid background
================
*/
static void Con_DrawSolidConsole( float frac ) {
	int				i, x;
	int				rows;
	short			*text;
	int				row;
	int				lines;
	int				statusRows;
	int				currentColorIndex;
	int				colorIndex;
	float			xf, yf, wf;
	float			alphaScale;
	float			markerY;
	float			drawY;
	vec4_t			backgroundColor;
	vec4_t			lineColor;
	vec4_t			versionColor;
	std::array<char, 16> clockString;
	bool			showVersion;

	lines = cls.glconfig.vidHeight * frac;
	if ( lines <= 0 )
		return;

	if ( re.FinishBloom )
		re.FinishBloom();

	if ( lines > cls.glconfig.vidHeight )
		lines = cls.glconfig.vidHeight;

	xf = con.xadjust;
	wf = con.displayWidth;
	if ( wf <= 0.0f ) {
		xf = 0.0f;
		wf = cls.glconfig.vidWidth;
	}
	yf = lines;
	alphaScale = Con_GetFadeAlpha( frac );
	statusRows = Con_GetFooterRows();
	showVersion = Con_ShowVersion() != qfalse;
	Con_GetBackgroundColor( backgroundColor );
	Con_GetColorCvar( con_lineColor, g_color_table[ ColorIndex( COLOR_RED ) ], lineColor, qfalse );
	Con_GetColorCvar( con_versionColor, lineColor, versionColor, qfalse );
	Con_UpdateTtfFontAvailability();

	if ( yf < 1.0 ) {
		yf = 0;
	} else {
		Con_SetScaledColor( backgroundColor, alphaScale );
		if ( con_backgroundStyle && con_backgroundStyle->integer ) {
			re.DrawStretchPic( xf, 0, wf, yf, 0, 0, 1, 1, cls.whiteShader );
		} else {
			re.DrawStretchPic( xf, 0, wf, yf, 0, 0, 1, 1, cls.consoleShader );
		}
	}

	Con_SetScaledColor( lineColor, alphaScale );
	re.DrawStretchPic( xf, yf, wf, 2, 0, 0, 1, 1, cls.whiteShader );

	if ( showVersion ) {
		Con_SetScaledColor( versionColor, alphaScale );
		if ( !Con_DrawHostText( xf + wf - ( ARRAY_LEN( Q3_VERSION ) - 1 ) * console_char_width,
			lines - console_char_height, Q3_VERSION, qtrue, con_drawColor ) ) {
			for ( i = 0; i < ARRAY_LEN( Q3_VERSION ) - 1; i++ ) {
				Con_DrawSmallCharFloat( xf + wf - ( ARRAY_LEN( Q3_VERSION ) - 1 - i ) * console_char_width,
					lines - console_char_height, Q3_VERSION[ i ] );
			}
		}
	}

	if ( Con_GetClockString( clockString.data(), static_cast<int>( clockString.size() ) ) ) {
		const int clockLen = strlen( clockString.data() );
		const int clockRow = showVersion ? 2 : 1;

		Con_SetScaledColor( versionColor, alphaScale );
		if ( !Con_DrawHostText( xf + wf - clockLen * console_char_width,
			lines - console_char_height * clockRow, clockString.data(), qtrue, con_drawColor ) ) {
			for ( i = 0; i < clockLen; i++ ) {
				Con_DrawSmallCharFloat( xf + wf - ( clockLen - i ) * console_char_width,
					lines - console_char_height * clockRow, clockString[ i ] );
			}
		}
	}

	// draw the text
	con.vislines = lines;
	Con_UpdateScrollbarHover();
	Con_UpdateCompletionScrollbarHover();
	rows = Con_GetLogRowCount();	// rows of text to draw

	markerY = lines - ( console_char_height * ( statusRows + 1 ) );
	drawY = markerY;
	row = static_cast<int>( con.displayLine );
	if ( static_cast<float>( row ) < con.displayLine ) {
		row++;
	}
	drawY += ( row - con.displayLine ) * console_char_height;

	// draw from the bottom up
	if ( con.display != con.current )
	{
		// draw arrows to show the buffer is backscrolled
		Con_SetScaledColor( lineColor, alphaScale );
		for ( x = 0 ; x < con.linewidth ; x += 4 )
			Con_DrawSmallCharFloat( con.xadjust + (x+1)*console_char_width, markerY, '^' );
		drawY -= console_char_height;
		row--;
	}

#ifdef USE_CURL
	if ( download.progress[ 0 ] ) 
	{
		currentColorIndex = ColorIndex( COLOR_CYAN );
		Con_SetScaledColor( g_color_table[ currentColorIndex ], alphaScale );

		i = strlen( download.progress );
		for ( x = 0 ; x < i ; x++ ) 
		{
			Con_DrawSmallCharFloat( con.xadjust + ( x + 1 ) * console_char_width,
				lines - console_char_height, download.progress[x] );
		}
	}
#endif

	currentColorIndex = ColorIndex( COLOR_WHITE );
	Con_SetScaledColor( g_color_table[ currentColorIndex ], alphaScale );

	for ( i = 0 ; i <= rows ; i++, drawY -= console_char_height, row-- )
	{
		if ( row < 0 )
			break;

		if ( con.current - row >= con.totallines ) {
			// past scrollback wrap point
			continue;
		}

		text = con.text.data() + (row % con.totallines) * con.linewidth;
		Con_DrawLogSelectionRow( row, drawY, alphaScale );

		if ( !Con_DrawConsoleLineText( con.xadjust + console_char_width, drawY,
			text, con.linewidth, alphaScale ) ) {
			for ( x = 0 ; x < con.linewidth ; x++ ) {
				// skip rendering whitespace
				if ( ( text[x] & 0xff ) == ' ' ) {
					continue;
				}
				// track color changes
				colorIndex = ( text[ x ] >> 8 ) & 63;
				if ( currentColorIndex != colorIndex ) {
					currentColorIndex = colorIndex;
					Con_SetScaledColor( g_color_table[ colorIndex ], alphaScale );
				}
				Con_DrawSmallCharFloat( con.xadjust + (x + 1) * console_char_width, drawY, text[x] & 0xff );
			}
		}
	}

	// draw the input prompt, user text, and cursor if desired
	Con_DrawInput( alphaScale, lineColor );
	Con_DrawScrollbar( alphaScale, lineColor );
	Con_DrawMouseCursor( alphaScale, lineColor );

	re.SetColor( nullptr );
}


/*
==================
Con_DrawConsole
==================
*/
void Con_DrawConsole( void ) {

	// check for console width changes from a vid mode change
	Con_CheckResize();

	// if disconnected, render console full screen
	if ( cls.state == CA_DISCONNECTED ) {
		if ( !( Key_GetCatcher( ) & (KEYCATCH_UI | KEYCATCH_CGAME | KEYCATCH_BROWSER)) ) {
			Con_DrawSolidConsole( 1.0 );
			return;
		}
	}

	if ( con.displayFrac ) {
		Con_DrawSolidConsole( con.displayFrac );
	} else {
		// draw notify lines
		if ( cls.state == CA_ACTIVE ) {
			Con_DrawNotify();
		}
	}
}

//================================================================

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole( void ) 
{
	Con_SyncSpeedCvars();

	// decide on the destination height of the console
	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE )
		con.finalFrac = 0.5;	// half screen
	else
		con.finalFrac = 0.0;	// none visible
	
	// scroll towards the destination height
	if ( con.finalFrac < con.displayFrac )
	{
		con.displayFrac -= con_conspeed->value * cls.realFrametime * 0.001;
		if ( con.finalFrac > con.displayFrac )
			con.displayFrac = con.finalFrac;

	}
	else if ( con.finalFrac > con.displayFrac )
	{
		con.displayFrac += con_conspeed->value * cls.realFrametime * 0.001;
		if ( con.finalFrac < con.displayFrac )
			con.displayFrac = con.finalFrac;
	}

	Con_UpdateDisplayLine();
}


void Con_PageUp( int lines )
{
	lines = Con_GetScrollStep( lines );

	con.display -= lines;
	
	Con_Fixup();
}


void Con_PageDown( int lines )
{
	lines = Con_GetScrollStep( lines );

	con.display += lines;

	Con_Fixup();
}


void Con_Top( void )
{
	// this is generally incorrect but will be adjusted in Con_Fixup()
	con.display = con.current - con.totallines;

	Con_Fixup();
}


void Con_Bottom( void )
{
	con.display = con.current;

	Con_Fixup();
}


void Con_Close( void )
{
	if ( !com_cl_running->integer )
		return;

	Field_Clear( &g_consoleField );
	Con_ClearNotify();
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CONSOLE );
	con.finalFrac = 0.0;			// none visible
	con.displayFrac = 0.0;
	con.displayLine = static_cast<float>( con.display );
	con.scrollbarDragging = false;
	con.scrollbarHover = 0.0f;
	con.mouseInitialized = false;
	con.inputSelecting = false;
	con.logSelecting = false;
	con.focus = ConFocus::Input;
	Con_ClearTextDragState();
	Con_InvalidateCompletionState();
	Con_ClearInputSelection();
	Con_ClearLogSelection();
}
