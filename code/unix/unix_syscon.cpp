#include "unix_syscon.h"

#include "../qcommon/qcommon.h"

#if defined(USE_SDL_SYSCON) && !defined(DEDICATED)

#include "../sdl/sdl_icon.h"

#define SYSCON_DEFAULT_WIDTH    1024
#define SYSCON_DEFAULT_HEIGHT   720
#define SYSCON_MIN_WIDTH        720
#define SYSCON_MIN_HEIGHT       520
#define SYSCON_MAX_SIZE         65536
#define SYSCON_SCALE            2.0f
#define SYSCON_CHAR_W           SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE
#define SYSCON_CHAR_H           SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE

typedef struct {
	int start;
	int len;
} sysconLine_t;

typedef struct {
	SDL_FRect headerRect;
	SDL_FRect errorRect;
	SDL_FRect logRect;
	SDL_FRect inputRect;
	SDL_FRect copyButtonRect;
	SDL_FRect clearButtonRect;
	int logicalWidth;
	int logicalHeight;
	int contentX;
	int contentY;
	int contentWidth;
	int contentHeight;
	int columns;
	int visibleLines;
	int inputColumns;
	int inputTextX;
	int inputTextY;
} sysconLayout_t;

typedef struct {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_WindowID windowID;
	qboolean videoStarted;
	qboolean quitOnClose;
	qboolean dirty;
	qboolean hasError;
	qboolean mouseTracked;
	int visLevel;
	int scrollOffset;
	int lastVisibleLines;
	float mouseX;
	float mouseY;
	Uint64 blinkTicks;
	char title[MAX_CVAR_VALUE_STRING];
	char statusText[256];
	char errorText[512];
	char submittedText[512];
	char returnedText[512];
	char logText[SYSCON_MAX_SIZE];
	int logLen;
	field_t field;
	int xPos;
	int yPos;
	qboolean useXYpos;
} sysconData_t;

static sysconData_t s_syscon;
static sysconLine_t s_wrappedLines[SYSCON_MAX_SIZE];

static const SDL_Color SYSCON_WINDOW_BG      = { 0x05, 0x0A, 0x12, 0xFF };
static const SDL_Color SYSCON_HEADER_BG      = { 0x0B, 0x14, 0x21, 0xFF };
static const SDL_Color SYSCON_PANEL_BG       = { 0x0D, 0x16, 0x24, 0xFF };
static const SDL_Color SYSCON_INPUT_BG       = { 0x10, 0x1B, 0x2B, 0xFF };
static const SDL_Color SYSCON_BORDER         = { 0x21, 0x34, 0x49, 0xFF };
static const SDL_Color SYSCON_TEXT           = { 0xDE, 0xE7, 0xF1, 0xFF };
static const SDL_Color SYSCON_MUTED          = { 0x93, 0xA4, 0xBA, 0xFF };
static const SDL_Color SYSCON_ACCENT         = { 0x57, 0xBC, 0xFF, 0xFF };
static const SDL_Color SYSCON_BUTTON_BG      = { 0x14, 0x22, 0x33, 0xFF };
static const SDL_Color SYSCON_BUTTON_HOVER   = { 0x1A, 0x2C, 0x44, 0xFF };
static const SDL_Color SYSCON_ERROR_BG       = { 0x38, 0x12, 0x18, 0xFF };
static const SDL_Color SYSCON_ERROR_TEXT     = { 0xFF, 0xC9, 0x73, 0xFF };

static void SysCon_SetDrawColor( const SDL_Color color )
{
	SDL_SetRenderDrawColor( s_syscon.renderer, color.r, color.g, color.b, color.a );
}

static void SysCon_FillRect( const SDL_FRect *rect, const SDL_Color color )
{
	SysCon_SetDrawColor( color );
	SDL_RenderFillRect( s_syscon.renderer, rect );
}

static void SysCon_DrawRect( const SDL_FRect *rect, const SDL_Color color )
{
	SysCon_SetDrawColor( color );
	SDL_RenderRect( s_syscon.renderer, rect );
}

static qboolean SysCon_PointInRect( float x, float y, const SDL_FRect *rect )
{
	return ( x >= rect->x && y >= rect->y &&
		x < rect->x + rect->w && y < rect->y + rect->h ) ? qtrue : qfalse;
}

static void SysCon_RenderText( float x, float y, const SDL_Color color, const char *text )
{
	if ( text == NULL || text[0] == '\0' ) {
		return;
	}

	SysCon_SetDrawColor( color );
	SDL_RenderDebugText( s_syscon.renderer, x, y, text );
}

static SDL_Window *SysCon_CreateWindow( const char *title, int x, int y, int w, int h, SDL_WindowFlags flags )
{
	SDL_PropertiesID props;
	SDL_Window *window;

	props = SDL_CreateProperties();
	if ( !props ) {
		return NULL;
	}

	SDL_SetStringProperty( props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title );
	SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, (Sint64)flags );
	SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_X_NUMBER, x );
	SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y );
	SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, w );
	SDL_SetNumberProperty( props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, h );

	window = SDL_CreateWindowWithProperties( props );
	SDL_DestroyProperties( props );

	return window;
}

static void SysCon_QueueQuitCommand( void )
{
	char *cmdString;

	cmdString = CopyString( "quit" );
	Sys_QueEvent( 0, SE_CONSOLE, 0, 0, (int)strlen( cmdString ) + 1, cmdString );
}

static void SysCon_ClearLog( void )
{
	s_syscon.logLen = 0;
	s_syscon.logText[0] = '\0';
	s_syscon.scrollOffset = 0;
	s_syscon.dirty = qtrue;
}

static void SysCon_EnsureCursorVisible( void )
{
	int len;

	if ( s_syscon.field.widthInChars <= 0 ) {
		s_syscon.field.widthInChars = 1;
	}

	len = (int)strlen( s_syscon.field.buffer );
	if ( s_syscon.field.cursor > len ) {
		s_syscon.field.cursor = len;
	}
	if ( s_syscon.field.cursor < 0 ) {
		s_syscon.field.cursor = 0;
	}
	if ( s_syscon.field.scroll > s_syscon.field.cursor ) {
		s_syscon.field.scroll = s_syscon.field.cursor;
	}
	while ( s_syscon.field.cursor - s_syscon.field.scroll >= s_syscon.field.widthInChars ) {
		s_syscon.field.scroll++;
	}
	if ( s_syscon.field.scroll < 0 ) {
		s_syscon.field.scroll = 0;
	}
	if ( s_syscon.field.scroll > len ) {
		s_syscon.field.scroll = len;
	}
}

static void SysCon_InsertChar( int ch )
{
	int len;

	if ( ch < ' ' || ch > '~' ) {
		return;
	}

	len = (int)strlen( s_syscon.field.buffer );
	if ( len >= MAX_EDIT_LINE - 2 ) {
		return;
	}

	memmove( s_syscon.field.buffer + s_syscon.field.cursor + 1,
		s_syscon.field.buffer + s_syscon.field.cursor,
		len + 1 - s_syscon.field.cursor );
	s_syscon.field.buffer[ s_syscon.field.cursor ] = (char)ch;
	s_syscon.field.cursor++;
	SysCon_EnsureCursorVisible();
	s_syscon.dirty = qtrue;
}

static void SysCon_PasteClipboard( void )
{
	char *clipText;
	char *s;

	clipText = Sys_GetClipboardData();
	if ( clipText == NULL ) {
		return;
	}

	for ( s = clipText; *s != '\0'; s++ ) {
		if ( *s == '\n' || *s == '\r' || *s == '\t' ) {
			SysCon_InsertChar( ' ' );
		} else {
			SysCon_InsertChar( (unsigned char)*s );
		}
	}

	Z_Free( clipText );
}

static void SysCon_AppendSubmittedCommand( const char *text )
{
	size_t currentLen;
	size_t textLen;
	size_t capacity;

	currentLen = strlen( s_syscon.submittedText );
	textLen = strlen( text );
	capacity = sizeof( s_syscon.submittedText );

	if ( currentLen + textLen + 2 >= capacity ) {
		s_syscon.submittedText[0] = '\0';
		currentLen = 0;
	}

	Q_strcat( s_syscon.submittedText, sizeof( s_syscon.submittedText ), text );
	Q_strcat( s_syscon.submittedText, sizeof( s_syscon.submittedText ), "\n" );
}

static void SysCon_SubmitField( void )
{
	const char *cmd;

	cmd = s_syscon.field.buffer;
	while ( *cmd == '\\' || *cmd == '/' ) {
		cmd++;
	}

	Conbuf_AppendText( va( "]%s\n", s_syscon.field.buffer ) );
	Con_SaveField( &s_syscon.field );
	SysCon_AppendSubmittedCommand( cmd );
	Field_Clear( &s_syscon.field );
	s_syscon.field.scroll = 0;
	s_syscon.scrollOffset = 0;
	s_syscon.dirty = qtrue;
}

static void SysCon_TrimLogBuffer( int incomingChars )
{
	int trimPoint;
	int i;

	if ( incomingChars >= SYSCON_MAX_SIZE ) {
		s_syscon.logLen = 0;
		s_syscon.logText[0] = '\0';
		return;
	}

	while ( s_syscon.logLen + incomingChars >= SYSCON_MAX_SIZE ) {
		trimPoint = s_syscon.logLen / 2;
		for ( i = trimPoint; i < s_syscon.logLen; i++ ) {
			if ( s_syscon.logText[i] == '\n' ) {
				trimPoint = i + 1;
				break;
			}
		}

		memmove( s_syscon.logText, s_syscon.logText + trimPoint, s_syscon.logLen - trimPoint );
		s_syscon.logLen -= trimPoint;
		if ( s_syscon.logLen < 0 ) {
			s_syscon.logLen = 0;
		}
		s_syscon.logText[ s_syscon.logLen ] = '\0';
	}
}

static int SysCon_BuildWrappedLines( int columns )
{
	int count;
	int i;
	int start;
	int len;

	if ( columns < 1 ) {
		columns = 1;
	}

	count = 0;
	start = 0;
	len = 0;

	if ( s_syscon.logLen <= 0 ) {
		s_wrappedLines[0].start = 0;
		s_wrappedLines[0].len = 0;
		return 1;
	}

	for ( i = 0; i < s_syscon.logLen; i++ ) {
		if ( s_syscon.logText[i] == '\n' ) {
			s_wrappedLines[count].start = start;
			s_wrappedLines[count].len = len;
			if ( count < ARRAY_LEN( s_wrappedLines ) - 1 ) {
				count++;
			}
			start = i + 1;
			len = 0;
			continue;
		}

		if ( len >= columns ) {
			s_wrappedLines[count].start = start;
			s_wrappedLines[count].len = len;
			if ( count < ARRAY_LEN( s_wrappedLines ) - 1 ) {
				count++;
			}
			start = i;
			len = 0;
		}

		len++;
	}

	s_wrappedLines[count].start = start;
	s_wrappedLines[count].len = len;
	if ( count < ARRAY_LEN( s_wrappedLines ) - 1 ) {
		count++;
	}

	return count;
}

static void SysCon_BuildLayout( int windowWidth, int windowHeight, sysconLayout_t *layout )
{
	int margin;
	int headerHeight;
	int errorHeight;
	int inputHeight;
	int buttonHeight;
	int copyWidth;
	int clearWidth;
	int buttonGap;
	int y;

	margin = 4;
	headerHeight = 20;
	errorHeight = s_syscon.hasError ? 14 : 0;
	inputHeight = 20;
	buttonHeight = 10;
	copyWidth = 20;
	clearWidth = 22;
	buttonGap = 2;

	layout->logicalWidth = (int)( windowWidth / SYSCON_SCALE );
	layout->logicalHeight = (int)( windowHeight / SYSCON_SCALE );

	if ( layout->logicalWidth < 320 ) {
		layout->logicalWidth = 320;
	}
	if ( layout->logicalHeight < 220 ) {
		layout->logicalHeight = 220;
	}

	layout->headerRect.x = (float)margin;
	layout->headerRect.y = (float)margin;
	layout->headerRect.w = (float)( layout->logicalWidth - margin * 2 );
	layout->headerRect.h = (float)headerHeight;

	layout->clearButtonRect.w = (float)clearWidth;
	layout->clearButtonRect.h = (float)buttonHeight;
	layout->clearButtonRect.x = layout->headerRect.x + layout->headerRect.w - clearWidth - margin;
	layout->clearButtonRect.y = layout->headerRect.y + ( headerHeight - buttonHeight ) * 0.5f;

	layout->copyButtonRect.w = (float)copyWidth;
	layout->copyButtonRect.h = (float)buttonHeight;
	layout->copyButtonRect.x = layout->clearButtonRect.x - copyWidth - buttonGap;
	layout->copyButtonRect.y = layout->clearButtonRect.y;

	y = margin + headerHeight + 3;
	if ( errorHeight > 0 ) {
		layout->errorRect.x = (float)margin;
		layout->errorRect.y = (float)y;
		layout->errorRect.w = (float)( layout->logicalWidth - margin * 2 );
		layout->errorRect.h = (float)errorHeight;
		y += errorHeight + 3;
	} else {
		memset( &layout->errorRect, 0, sizeof( layout->errorRect ) );
	}

	layout->inputRect.x = (float)margin;
	layout->inputRect.y = (float)( layout->logicalHeight - margin - inputHeight );
	layout->inputRect.w = (float)( layout->logicalWidth - margin * 2 );
	layout->inputRect.h = (float)inputHeight;

	layout->logRect.x = (float)margin;
	layout->logRect.y = (float)y;
	layout->logRect.w = (float)( layout->logicalWidth - margin * 2 );
	layout->logRect.h = layout->inputRect.y - y - 3;
	if ( layout->logRect.h < 32 ) {
		layout->logRect.h = 32;
	}

	layout->contentX = (int)layout->logRect.x + 3;
	layout->contentY = (int)layout->logRect.y + 3;
	layout->contentWidth = (int)layout->logRect.w - 6;
	layout->contentHeight = (int)layout->logRect.h - 6;
	layout->columns = layout->contentWidth / SYSCON_CHAR_W;
	if ( layout->columns < 1 ) {
		layout->columns = 1;
	}
	layout->visibleLines = layout->contentHeight / ( SYSCON_CHAR_H + 2 );
	if ( layout->visibleLines < 1 ) {
		layout->visibleLines = 1;
	}

	layout->inputTextX = (int)layout->inputRect.x + 5 + SYSCON_CHAR_W * 2;
	layout->inputTextY = (int)layout->inputRect.y + 6;
	layout->inputColumns = ( (int)layout->inputRect.w - 10 - SYSCON_CHAR_W * 3 ) / SYSCON_CHAR_W;
	if ( layout->inputColumns < 1 ) {
		layout->inputColumns = 1;
	}
}

static void SysCon_DrawButton( const SDL_FRect *rect, const char *label )
{
	SDL_Color fill;
	float centerX;
	float centerY;
	float labelWidth;

	fill = SysCon_PointInRect( s_syscon.mouseX, s_syscon.mouseY, rect ) && s_syscon.mouseTracked
		? SYSCON_BUTTON_HOVER : SYSCON_BUTTON_BG;

	SysCon_FillRect( rect, fill );
	SysCon_DrawRect( rect, SYSCON_BORDER );

	labelWidth = (float)( strlen( label ) * SYSCON_CHAR_W );
	centerX = rect->x + ( rect->w - labelWidth ) * 0.5f;
	centerY = rect->y + ( rect->h - SYSCON_CHAR_H ) * 0.5f;
	SysCon_RenderText( centerX, centerY, SYSCON_TEXT, label );
}

static void SysCon_RenderStatusText( const sysconLayout_t *layout )
{
	SDL_Rect clip;

	clip.x = (int)layout->headerRect.x + 3;
	clip.y = (int)layout->headerRect.y + 2;
	clip.w = (int)( layout->copyButtonRect.x - layout->headerRect.x - 7 );
	clip.h = (int)layout->headerRect.h - 4;

	SDL_SetRenderClipRect( s_syscon.renderer, &clip );
	SysCon_RenderText( layout->headerRect.x + 3, layout->headerRect.y + 3, SYSCON_TEXT, s_syscon.title );
	SysCon_RenderText( layout->headerRect.x + 3, layout->headerRect.y + 11,
		s_syscon.statusText[0] ? SYSCON_ACCENT : SYSCON_MUTED,
		s_syscon.statusText[0] ? s_syscon.statusText : "System console ready" );
	SDL_SetRenderClipRect( s_syscon.renderer, NULL );
}

static void SysCon_RenderError( const sysconLayout_t *layout )
{
	SDL_Rect clip;

	if ( !s_syscon.hasError ) {
		return;
	}

	SysCon_FillRect( &layout->errorRect, SYSCON_ERROR_BG );
	SysCon_DrawRect( &layout->errorRect, SYSCON_BORDER );

	clip.x = (int)layout->errorRect.x + 3;
	clip.y = (int)layout->errorRect.y + 2;
	clip.w = (int)layout->errorRect.w - 6;
	clip.h = (int)layout->errorRect.h - 4;
	SDL_SetRenderClipRect( s_syscon.renderer, &clip );
	SysCon_RenderText( layout->errorRect.x + 3, layout->errorRect.y + 3, SYSCON_ERROR_TEXT, s_syscon.errorText );
	SDL_SetRenderClipRect( s_syscon.renderer, NULL );
}

static void SysCon_RenderLog( const sysconLayout_t *layout )
{
	SDL_Rect clip;
	int totalLines;
	int firstLine;
	int lastLine;
	int i;
	int drawY;
	char lineText[1024];
	int copyLen;

	SysCon_FillRect( &layout->logRect, SYSCON_PANEL_BG );
	SysCon_DrawRect( &layout->logRect, SYSCON_BORDER );

	totalLines = SysCon_BuildWrappedLines( layout->columns );
	if ( totalLines < 1 ) {
		totalLines = 1;
	}

	if ( s_syscon.scrollOffset < 0 ) {
		s_syscon.scrollOffset = 0;
	}
	if ( s_syscon.scrollOffset > totalLines - 1 ) {
		s_syscon.scrollOffset = totalLines - 1;
	}

	s_syscon.lastVisibleLines = layout->visibleLines;

	firstLine = totalLines - layout->visibleLines - s_syscon.scrollOffset;
	if ( firstLine < 0 ) {
		firstLine = 0;
	}
	lastLine = firstLine + layout->visibleLines;
	if ( lastLine > totalLines ) {
		lastLine = totalLines;
	}

	clip.x = layout->contentX;
	clip.y = layout->contentY;
	clip.w = layout->contentWidth;
	clip.h = layout->contentHeight;
	SDL_SetRenderClipRect( s_syscon.renderer, &clip );

	drawY = layout->contentY;
	for ( i = firstLine; i < lastLine; i++ ) {
		copyLen = s_wrappedLines[i].len;
		if ( copyLen >= (int)sizeof( lineText ) ) {
			copyLen = sizeof( lineText ) - 1;
		}
		memcpy( lineText, s_syscon.logText + s_wrappedLines[i].start, copyLen );
		lineText[ copyLen ] = '\0';
		SysCon_RenderText( (float)layout->contentX, (float)drawY, SYSCON_TEXT, lineText );
		drawY += SYSCON_CHAR_H + 2;
	}

	SDL_SetRenderClipRect( s_syscon.renderer, NULL );

	if ( s_syscon.scrollOffset > 0 ) {
		char info[64];

		Com_sprintf( info, sizeof( info ), "scroll +%d", s_syscon.scrollOffset );
		SysCon_RenderText( layout->logRect.x + layout->logRect.w - 3 - (float)( strlen( info ) * SYSCON_CHAR_W ),
			layout->logRect.y + 3, SYSCON_MUTED, info );
	}
}

static void SysCon_RenderInput( const sysconLayout_t *layout )
{
	int len;
	int drawLen;
	int cursorOffset;
	char visibleText[MAX_EDIT_LINE];
	SDL_FRect caretRect;
	Uint64 ticks;

	SysCon_FillRect( &layout->inputRect, SYSCON_INPUT_BG );
	SysCon_DrawRect( &layout->inputRect, SYSCON_BORDER );

	s_syscon.field.widthInChars = layout->inputColumns;
	SysCon_EnsureCursorVisible();

	len = (int)strlen( s_syscon.field.buffer );
	drawLen = len - s_syscon.field.scroll;
	if ( drawLen < 0 ) {
		drawLen = 0;
	}
	if ( drawLen > s_syscon.field.widthInChars ) {
		drawLen = s_syscon.field.widthInChars;
	}

	memcpy( visibleText, s_syscon.field.buffer + s_syscon.field.scroll, drawLen );
	visibleText[ drawLen ] = '\0';

	SysCon_RenderText( layout->inputRect.x + 4, layout->inputTextY, SYSCON_ACCENT, "]" );
	SysCon_RenderText( (float)layout->inputTextX, (float)layout->inputTextY, SYSCON_TEXT, visibleText );

	ticks = SDL_GetTicks();
	if ( ticks - s_syscon.blinkTicks >= 500 ) {
		s_syscon.blinkTicks = ticks;
		s_syscon.dirty = qtrue;
	}

	if ( ( ( ticks / 500 ) & 1 ) == 0 ) {
		cursorOffset = s_syscon.field.cursor - s_syscon.field.scroll;
		if ( cursorOffset < 0 ) {
			cursorOffset = 0;
		}
		if ( cursorOffset > s_syscon.field.widthInChars ) {
			cursorOffset = s_syscon.field.widthInChars;
		}

		caretRect.x = (float)( layout->inputTextX + cursorOffset * SYSCON_CHAR_W );
		caretRect.y = layout->inputRect.y + layout->inputRect.h - 5;
		caretRect.w = 1.0f;
		caretRect.h = 2.0f;
		SysCon_FillRect( &caretRect, SYSCON_ACCENT );
	}
}

static void SysCon_Render( void )
{
	sysconLayout_t layout;
	SDL_FRect accentRect;
	int windowWidth;
	int windowHeight;

	if ( s_syscon.window == NULL || s_syscon.renderer == NULL || s_syscon.visLevel != 1 ) {
		return;
	}

	if ( !SDL_GetWindowSize( s_syscon.window, &windowWidth, &windowHeight ) ) {
		return;
	}

	SysCon_BuildLayout( windowWidth, windowHeight, &layout );

	SysCon_SetDrawColor( SYSCON_WINDOW_BG );
	SDL_RenderClear( s_syscon.renderer );

	accentRect.x = 0.0f;
	accentRect.y = 0.0f;
	accentRect.w = (float)layout.logicalWidth;
	accentRect.h = 1.5f;
	SysCon_FillRect( &accentRect, SYSCON_ACCENT );

	SysCon_FillRect( &layout.headerRect, SYSCON_HEADER_BG );
	SysCon_DrawRect( &layout.headerRect, SYSCON_BORDER );
	SysCon_DrawButton( &layout.copyButtonRect, "Copy" );
	SysCon_DrawButton( &layout.clearButtonRect, "Clear" );
	SysCon_RenderStatusText( &layout );
	SysCon_RenderError( &layout );
	SysCon_RenderLog( &layout );
	SysCon_RenderInput( &layout );

	SDL_RenderPresent( s_syscon.renderer );
	s_syscon.dirty = qfalse;
}

static void SysCon_HandleMouseDown( float x, float y )
{
	sysconLayout_t layout;
	int windowWidth;
	int windowHeight;
	int cursorOffset;
	int len;

	if ( s_syscon.window == NULL || !SDL_GetWindowSize( s_syscon.window, &windowWidth, &windowHeight ) ) {
		return;
	}

	SysCon_BuildLayout( windowWidth, windowHeight, &layout );

	if ( SysCon_PointInRect( x, y, &layout.copyButtonRect ) ) {
		Sys_SetClipboardData( s_syscon.logText );
		return;
	}

	if ( SysCon_PointInRect( x, y, &layout.clearButtonRect ) ) {
		SysCon_ClearLog();
		return;
	}

	if ( SysCon_PointInRect( x, y, &layout.inputRect ) ) {
		s_syscon.field.widthInChars = layout.inputColumns;
		SysCon_EnsureCursorVisible();

		cursorOffset = (int)( ( x - layout.inputTextX ) / SYSCON_CHAR_W );
		if ( cursorOffset < 0 ) {
			cursorOffset = 0;
		}

		len = (int)strlen( s_syscon.field.buffer ) - s_syscon.field.scroll;
		if ( len < 0 ) {
			len = 0;
		}
		if ( cursorOffset > len ) {
			cursorOffset = len;
		}

		s_syscon.field.cursor = s_syscon.field.scroll + cursorOffset;
		SysCon_EnsureCursorVisible();
		s_syscon.dirty = qtrue;
	}
}

static void SysCon_HandleMouseWheel( float deltaY )
{
	int amount;

	amount = (int)( deltaY > 0.0f ? 3 : -3 );
	s_syscon.scrollOffset += amount;
	if ( s_syscon.scrollOffset < 0 ) {
		s_syscon.scrollOffset = 0;
	}
	s_syscon.dirty = qtrue;
}

static void SysCon_HandleKey( const SDL_KeyboardEvent *key )
{
	SDL_Keymod mods;
	qboolean primaryMod;
	int len;

	mods = key->mod;
	primaryMod = ( mods & ( SDL_KMOD_CTRL | SDL_KMOD_GUI ) ) ? qtrue : qfalse;

	switch ( key->key ) {
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		SysCon_SubmitField();
		return;

	case SDLK_TAB:
		Field_AutoComplete( &s_syscon.field );
		SysCon_EnsureCursorVisible();
		s_syscon.dirty = qtrue;
		return;

	case SDLK_UP:
		Con_HistoryGetPrev( &s_syscon.field );
		SysCon_EnsureCursorVisible();
		s_syscon.dirty = qtrue;
		return;

	case SDLK_DOWN:
		Con_HistoryGetNext( &s_syscon.field );
		SysCon_EnsureCursorVisible();
		s_syscon.dirty = qtrue;
		return;

	case SDLK_PAGEUP:
		s_syscon.scrollOffset += s_syscon.lastVisibleLines > 1 ? s_syscon.lastVisibleLines - 1 : 1;
		s_syscon.dirty = qtrue;
		return;

	case SDLK_PAGEDOWN:
		s_syscon.scrollOffset -= s_syscon.lastVisibleLines > 1 ? s_syscon.lastVisibleLines - 1 : 1;
		if ( s_syscon.scrollOffset < 0 ) {
			s_syscon.scrollOffset = 0;
		}
		s_syscon.dirty = qtrue;
		return;

	case SDLK_HOME:
		if ( primaryMod ) {
			s_syscon.scrollOffset = SYSCON_MAX_SIZE;
		} else {
			s_syscon.field.cursor = 0;
			SysCon_EnsureCursorVisible();
		}
		s_syscon.dirty = qtrue;
		return;

	case SDLK_END:
		if ( primaryMod ) {
			s_syscon.scrollOffset = 0;
		} else {
			s_syscon.field.cursor = (int)strlen( s_syscon.field.buffer );
			SysCon_EnsureCursorVisible();
		}
		s_syscon.dirty = qtrue;
		return;

	case SDLK_LEFT:
		if ( s_syscon.field.cursor > 0 ) {
			s_syscon.field.cursor--;
			SysCon_EnsureCursorVisible();
			s_syscon.dirty = qtrue;
		}
		return;

	case SDLK_RIGHT:
		len = (int)strlen( s_syscon.field.buffer );
		if ( s_syscon.field.cursor < len ) {
			s_syscon.field.cursor++;
			SysCon_EnsureCursorVisible();
			s_syscon.dirty = qtrue;
		}
		return;

	case SDLK_BACKSPACE:
		if ( s_syscon.field.cursor > 0 ) {
			len = (int)strlen( s_syscon.field.buffer );
			memmove( s_syscon.field.buffer + s_syscon.field.cursor - 1,
				s_syscon.field.buffer + s_syscon.field.cursor,
				len + 1 - s_syscon.field.cursor );
			s_syscon.field.cursor--;
			SysCon_EnsureCursorVisible();
			s_syscon.dirty = qtrue;
		}
		return;

	case SDLK_DELETE:
		len = (int)strlen( s_syscon.field.buffer );
		if ( s_syscon.field.cursor < len ) {
			memmove( s_syscon.field.buffer + s_syscon.field.cursor,
				s_syscon.field.buffer + s_syscon.field.cursor + 1,
				len - s_syscon.field.cursor );
			SysCon_EnsureCursorVisible();
			s_syscon.dirty = qtrue;
		}
		return;

	default:
		break;
	}

	if ( primaryMod && key->key == SDLK_C ) {
		Sys_SetClipboardData( s_syscon.logText );
		return;
	}

	if ( primaryMod && key->key == SDLK_V ) {
		SysCon_PasteClipboard();
		return;
	}

	if ( primaryMod && key->key == SDLK_L ) {
		SysCon_ClearLog();
		return;
	}
}

static SDL_WindowID SysCon_EventWindowID( const SDL_Event *event )
{
	switch ( event->type ) {
	case SDL_EVENT_WINDOW_SHOWN:
	case SDL_EVENT_WINDOW_HIDDEN:
	case SDL_EVENT_WINDOW_EXPOSED:
	case SDL_EVENT_WINDOW_MOVED:
	case SDL_EVENT_WINDOW_RESIZED:
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
	case SDL_EVENT_WINDOW_MINIMIZED:
	case SDL_EVENT_WINDOW_MAXIMIZED:
	case SDL_EVENT_WINDOW_RESTORED:
	case SDL_EVENT_WINDOW_MOUSE_ENTER:
	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
	case SDL_EVENT_WINDOW_FOCUS_LOST:
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
	case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
	case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
		return event->window.windowID;

	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		return event->key.windowID;

	case SDL_EVENT_TEXT_INPUT:
		return event->text.windowID;

	case SDL_EVENT_MOUSE_MOTION:
		return event->motion.windowID;

	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP:
		return event->button.windowID;

	case SDL_EVENT_MOUSE_WHEEL:
		return event->wheel.windowID;

	default:
		break;
	}

	return 0;
}

static qboolean SysCon_EnsureCreated( void )
{
	SDL_WindowFlags flags;
	int startVideo;

	if ( s_syscon.window != NULL ) {
		return qtrue;
	}

	startVideo = SDL_WasInit( SDL_INIT_VIDEO ) ? 0 : 1;
	if ( startVideo ) {
		if ( !SDL_Init( SDL_INIT_VIDEO ) ) {
			return qfalse;
		}
	}

	flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
	s_syscon.window = SysCon_CreateWindow( s_syscon.title[0] ? s_syscon.title : CONSOLE_WINDOW_TITLE,
		s_syscon.useXYpos ? s_syscon.xPos : SDL_WINDOWPOS_CENTERED,
		s_syscon.useXYpos ? s_syscon.yPos : SDL_WINDOWPOS_CENTERED,
		SYSCON_DEFAULT_WIDTH, SYSCON_DEFAULT_HEIGHT, flags );

	if ( s_syscon.window == NULL ) {
		if ( startVideo ) {
			SDL_QuitSubSystem( SDL_INIT_VIDEO );
		}
		return qfalse;
	}

	s_syscon.renderer = SDL_CreateRenderer( s_syscon.window, NULL );
	if ( s_syscon.renderer == NULL ) {
		s_syscon.renderer = SDL_CreateRenderer( s_syscon.window, "software" );
	}
	if ( s_syscon.renderer == NULL ) {
		SDL_DestroyWindow( s_syscon.window );
		s_syscon.window = NULL;
		if ( startVideo ) {
			SDL_QuitSubSystem( SDL_INIT_VIDEO );
		}
		return qfalse;
	}

	SDL_SetRenderScale( s_syscon.renderer, SYSCON_SCALE, SYSCON_SCALE );
	SDL_SetRenderDrawBlendMode( s_syscon.renderer, SDL_BLENDMODE_BLEND );
	SDL_SetWindowMinimumSize( s_syscon.window, SYSCON_MIN_WIDTH, SYSCON_MIN_HEIGHT );
	SDL_StartTextInput( s_syscon.window );

#ifdef USE_ICON
	{
		SDL_Surface *icon;

		icon = SDL_CreateSurfaceFrom(
			CLIENT_WINDOW_ICON.width,
			CLIENT_WINDOW_ICON.height,
			SDL_PIXELFORMAT_RGBA32,
			(void *)CLIENT_WINDOW_ICON.pixel_data,
			CLIENT_WINDOW_ICON.bytes_per_pixel * CLIENT_WINDOW_ICON.width
		);
		if ( icon ) {
			SDL_SetWindowIcon( s_syscon.window, icon );
			SDL_DestroySurface( icon );
		}
	}
#endif

	s_syscon.windowID = SDL_GetWindowID( s_syscon.window );
	s_syscon.videoStarted = startVideo ? qtrue : qfalse;
	s_syscon.blinkTicks = SDL_GetTicks();
	s_syscon.dirty = qtrue;

	return qtrue;
}

void Sys_CreateConsole( const char *title, int xPos, int yPos, qboolean useXYpos )
{
	memset( &s_syscon, 0, sizeof( s_syscon ) );
	Field_Clear( &s_syscon.field );
	s_syscon.field.widthInChars = 1;
	s_syscon.visLevel = 1;
	s_syscon.xPos = xPos;
	s_syscon.yPos = yPos;
	s_syscon.useXYpos = useXYpos;

	if ( title && *title ) {
		Q_strncpyz( s_syscon.title, title, sizeof( s_syscon.title ) );
	} else {
		Q_strncpyz( s_syscon.title, CONSOLE_WINDOW_TITLE, sizeof( s_syscon.title ) );
	}

	SysCon_EnsureCreated();
	Sys_ShowConsole( 1, qfalse );
}

void Sys_DestroyConsole( void )
{
	if ( s_syscon.window ) {
		SDL_StopTextInput( s_syscon.window );
	}
	if ( s_syscon.renderer ) {
		SDL_DestroyRenderer( s_syscon.renderer );
		s_syscon.renderer = NULL;
	}
	if ( s_syscon.window ) {
		SDL_DestroyWindow( s_syscon.window );
		s_syscon.window = NULL;
	}
	s_syscon.windowID = 0;

	if ( s_syscon.videoStarted && SDL_WasInit( SDL_INIT_VIDEO ) ) {
		SDL_QuitSubSystem( SDL_INIT_VIDEO );
		s_syscon.videoStarted = qfalse;
	}
}

void Sys_ShowConsole( int visLevel, qboolean quitOnClose )
{
	s_syscon.quitOnClose = quitOnClose;
	s_syscon.visLevel = visLevel;

	if ( visLevel == 0 && s_syscon.window == NULL ) {
		return;
	}

	if ( !SysCon_EnsureCreated() ) {
		return;
	}

	switch ( visLevel ) {
	case 0:
		SDL_HideWindow( s_syscon.window );
		break;

	case 1:
		SDL_RestoreWindow( s_syscon.window );
		SDL_ShowWindow( s_syscon.window );
		SDL_RaiseWindow( s_syscon.window );
		break;

	case 2:
		SDL_ShowWindow( s_syscon.window );
		SDL_MinimizeWindow( s_syscon.window );
		break;

	default:
		Sys_Error( "Invalid visLevel %d sent to Sys_ShowConsole\n", visLevel );
		break;
	}

	s_syscon.dirty = qtrue;
	SysCon_Render();
}

void QDECL Sys_SetStatus( const char *format, ... )
{
	va_list argptr;

	va_start( argptr, format );
	Q_vsnprintf( s_syscon.statusText, sizeof( s_syscon.statusText ), format, argptr );
	va_end( argptr );

	s_syscon.dirty = qtrue;
	SysCon_Render();
}

void Sys_SetErrorText( const char *text )
{
	if ( text && *text ) {
		Q_strncpyz( s_syscon.errorText, text, sizeof( s_syscon.errorText ) );
		s_syscon.hasError = qtrue;
	} else {
		s_syscon.errorText[0] = '\0';
		s_syscon.hasError = qfalse;
	}

	s_syscon.dirty = qtrue;
	SysCon_Render();
}

void Conbuf_AppendText( const char *msg )
{
	char buffer[MAXPRINTMSG * 2];
	char *dst;
	int incoming;
	qboolean atBottom;

	if ( msg == NULL || *msg == '\0' ) {
		return;
	}

	atBottom = ( s_syscon.scrollOffset == 0 ) ? qtrue : qfalse;
	dst = buffer;

	while ( *msg && dst < buffer + sizeof( buffer ) - 1 ) {
		if ( *msg == '\r' ) {
			*dst++ = '\n';
			msg++;
			if ( *msg == '\n' ) {
				msg++;
			}
			continue;
		}

		if ( *msg == '\n' ) {
			*dst++ = '\n';
			msg++;
			continue;
		}

		if ( Q_IsColorString( msg ) ) {
			msg += 2;
			continue;
		}

		if ( (unsigned char)*msg >= ' ' && (unsigned char)*msg <= '~' ) {
			*dst++ = *msg;
		} else if ( *msg == '\t' ) {
			*dst++ = ' ';
		}

		msg++;
	}

	*dst = '\0';
	incoming = (int)( dst - buffer );
	if ( incoming <= 0 ) {
		return;
	}

	SysCon_TrimLogBuffer( incoming );
	memcpy( s_syscon.logText + s_syscon.logLen, buffer, incoming + 1 );
	s_syscon.logLen += incoming;
	if ( atBottom ) {
		s_syscon.scrollOffset = 0;
	}
	s_syscon.dirty = qtrue;
	SysCon_Render();
}

char *Sys_WindowConsoleInput( void )
{
	if ( s_syscon.submittedText[0] == '\0' ) {
		return NULL;
	}

	Q_strncpyz( s_syscon.returnedText, s_syscon.submittedText, sizeof( s_syscon.returnedText ) );
	s_syscon.submittedText[0] = '\0';
	return s_syscon.returnedText;
}

void Sys_ConsoleFrame( void )
{
	if ( s_syscon.window == NULL || s_syscon.visLevel != 1 ) {
		return;
	}

	if ( s_syscon.dirty || s_syscon.visLevel == 1 ) {
		SysCon_Render();
	}
}

qboolean Sys_ConsoleVideoActive( void )
{
	return s_syscon.window != NULL ? qtrue : qfalse;
}

qboolean Sys_ConsoleHandleEvent( const SDL_Event *event )
{
	SDL_WindowID windowID;
	float logicalX;
	float logicalY;

	if ( s_syscon.window == NULL || event == NULL ) {
		return qfalse;
	}

	windowID = SysCon_EventWindowID( event );
	if ( windowID != s_syscon.windowID ) {
		return qfalse;
	}

	switch ( event->type ) {
	case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
		if ( ( com_dedicated && com_dedicated->integer && !com_errorEntered ) || s_syscon.quitOnClose ) {
			SysCon_QueueQuitCommand();
		} else {
			Sys_ShowConsole( 0, qfalse );
			if ( com_viewlog ) {
				Cvar_Set( "viewlog", "0" );
			}
		}
		return qtrue;

	case SDL_EVENT_WINDOW_HIDDEN:
		s_syscon.visLevel = 0;
		s_syscon.dirty = qtrue;
		return qtrue;

	case SDL_EVENT_WINDOW_MINIMIZED:
		s_syscon.visLevel = 2;
		s_syscon.dirty = qtrue;
		return qtrue;

	case SDL_EVENT_WINDOW_SHOWN:
	case SDL_EVENT_WINDOW_RESTORED:
	case SDL_EVENT_WINDOW_MAXIMIZED:
		s_syscon.visLevel = 1;
		s_syscon.dirty = qtrue;
		return qtrue;

	case SDL_EVENT_WINDOW_EXPOSED:
	case SDL_EVENT_WINDOW_RESIZED:
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
	case SDL_EVENT_WINDOW_FOCUS_LOST:
	case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
	case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
		s_syscon.dirty = qtrue;
		return qtrue;

	case SDL_EVENT_WINDOW_MOUSE_ENTER:
		s_syscon.mouseTracked = qtrue;
		s_syscon.dirty = qtrue;
		return qtrue;

	case SDL_EVENT_WINDOW_MOUSE_LEAVE:
		s_syscon.mouseTracked = qfalse;
		s_syscon.dirty = qtrue;
		return qtrue;

	case SDL_EVENT_MOUSE_MOTION:
		s_syscon.mouseTracked = qtrue;
		s_syscon.mouseX = event->motion.x / SYSCON_SCALE;
		s_syscon.mouseY = event->motion.y / SYSCON_SCALE;
		s_syscon.dirty = qtrue;
		return qtrue;

	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if ( event->button.button == SDL_BUTTON_LEFT ) {
			logicalX = event->button.x / SYSCON_SCALE;
			logicalY = event->button.y / SYSCON_SCALE;
			s_syscon.mouseX = logicalX;
			s_syscon.mouseY = logicalY;
			s_syscon.mouseTracked = qtrue;
			SysCon_HandleMouseDown( logicalX, logicalY );
		}
		return qtrue;

	case SDL_EVENT_MOUSE_BUTTON_UP:
	case SDL_EVENT_KEY_UP:
		return qtrue;

	case SDL_EVENT_MOUSE_WHEEL:
		SysCon_HandleMouseWheel( event->wheel.y );
		return qtrue;

	case SDL_EVENT_KEY_DOWN:
		SysCon_HandleKey( &event->key );
		return qtrue;

	case SDL_EVENT_TEXT_INPUT:
		{
			const char *s;

			for ( s = event->text.text; *s != '\0'; s++ ) {
				SysCon_InsertChar( (unsigned char)*s );
			}
		}
		return qtrue;

	default:
		break;
	}

	return qfalse;
}

#else

void Sys_CreateConsole( const char *title, int xPos, int yPos, qboolean useXYpos )
{
}

void Sys_DestroyConsole( void )
{
}

void Sys_ShowConsole( int visLevel, qboolean quitOnClose )
{
}

void QDECL Sys_SetStatus( const char *format, ... )
{
}

void Sys_SetErrorText( const char *text )
{
}

void Conbuf_AppendText( const char *msg )
{
}

char *Sys_WindowConsoleInput( void )
{
	return NULL;
}

void Sys_ConsoleFrame( void )
{
}

qboolean Sys_ConsoleVideoActive( void )
{
	return qfalse;
}

#if defined(USE_SDL_SYSCON) && !defined(DEDICATED)
qboolean Sys_ConsoleHandleEvent( const SDL_Event *event )
{
	return qfalse;
}
#endif

#endif
