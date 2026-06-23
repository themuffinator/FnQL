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
// win_syscon.h
#ifdef __cplusplus
extern "C" {
#endif
#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "win_local.h"
#include "resource.h"
#ifdef __cplusplus
}
#endif
#ifndef DEDICATED
#include "../client/client.h"
#endif
#include "win_raii.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COPY_ID			1
#define QUIT_ID			2
#define CLEAR_ID		3
#define ERROR_TIMER_ID	4

#define CON_TIMER_ID	5
#define BUF_TIMER_ID	6
#define TEX_TIMER_ID	7

#define ERRORBOX_ID		10
#define ERRORTEXT_ID	11

#define EDIT_ID			100
#define INPUT_ID		101
#define STATUS_ID       102

#define DEFAULT_WIDTH   860
#define DEFAULT_HEIGHT  620

#define MIN_WIDTH       620
#define MIN_HEIGHT      420

#define BORDERW			8
#define BORDERH			8
#define CONTROL_GAP     8
#define FOOTER_HEIGHT   44
#define BUTTON_HEIGHT   28
#define COPY_WIDTH      84
#define CLEAR_WIDTH     92

#define INPUT_HEIGHT    28
#define ERROR_HEIGHT    30

#define MAX_CONSIZE		65536

#define T TEXT

#define WINDOW_BG_COLOR RGB(0x08,0x0D,0x13)
#define EDIT_COLOR		RGB(0x22,0x10,0x14)
#define INPUT_BG_COLOR  RGB(0x2B,0x14,0x19)
#define FOOTER_BG_COLOR RGB(0x0D,0x15,0x21)
#define BORDER_COLOR    RGB(0x25,0x38,0x4D)
#define TEXT_COLOR		RGB(0xDE,0xE8,0xF2)
#define MUTED_COLOR     RGB(0x8F,0xA2,0xB8)
#define ACCENT_COLOR    RGB(0x58,0xBC,0xFF)
#define BUTTON_BG_COLOR RGB(0x16,0x22,0x32)
#define BUTTON_HOVER_BG RGB(0x1C,0x2C,0x42)
#define BUTTON_DOWN_BG  RGB(0x24,0x38,0x54)
#define BUTTON_OFF_BG   RGB(0x0F,0x17,0x22)

#define ERROR_BG_COLOR	RGB(0x4A,0x1B,0x24)

#define ERROR_COLOR_1   RGB(0xFF,0xD2,0x73)
#define ERROR_COLOR_2   RGB(0xFF,0x8E,0x7A)

static field_t console;

typedef struct
{
	HWND		hWnd;
	HWND		hwndBuffer;

	HWND		hwndInputLine;

	HWND		hwndStatusBar;
	HWND		hwndButtonClear;
	HWND		hwndButtonCopy;

	HWND		hwndErrorBox;

	HBRUSH		hbrEditBackground;
	HBRUSH		hbrInputBackground;
	HBRUSH		hbrErrorBackground;
	HBRUSH		hbrFooterBackground;
	HBRUSH		hbrWindowBackground;

	HFONT		hfBufferFont;
	HFONT		hfStatusFont;

	char		consoleText[512];
	char		returnedText[512];
	char		statusText[256];

	int			visLevel;
	qboolean	quitOnClose;
	int			windowWidth, windowHeight;

	LONG_PTR	SysInputLineWndProc;
	LONG_PTR	SysStatusWndProc;
	LONG_PTR	SysBufferWndProc;

	qboolean	newline;

} WinConData;

static WinConData s_wcd;

static int maxConSize; // up to MAX_CONSIZE
static int curConSize; // up to MAX_CONSIZE

static UINT texTimerID; // for flushing text in buffer

static char conBuffer[ MAXPRINTMSG ];
static int  conBufPos;

static void AddBufferText( const char *text, int textLength );

static void ConClear( void )
{
	//SendMessage( s_wcd.hwndBuffer, EM_SETSEL, 0, -1 );
	//SendMessage( s_wcd.hwndBuffer, EM_REPLACESEL, FALSE, ( LPARAM ) "" );
	SetWindowText( s_wcd.hwndBuffer, T("") );
	UpdateWindow( s_wcd.hwndBuffer );
	s_wcd.newline = qfalse;
	curConSize = 0;
	conBufPos = 0;
}

static int GetStatusBarHeight( void )
{
	RECT rect;

	if ( !s_wcd.hwndStatusBar )
		return FOOTER_HEIGHT;

	GetClientRect( s_wcd.hwndStatusBar, &rect );

	return (rect.bottom-rect.top+1);
}

static void LayoutFooterControls( HWND hWnd )
{
	RECT rect;
	int y;
	int x;

	if ( !s_wcd.hwndButtonCopy || !s_wcd.hwndButtonClear ) {
		return;
	}

	GetClientRect( hWnd, &rect );

	y = ( rect.bottom - rect.top - BUTTON_HEIGHT ) / 2;
	x = rect.right - rect.left - BORDERW;

	x -= CLEAR_WIDTH;
	SetWindowPos( s_wcd.hwndButtonClear, HWND_TOP, x, y, CLEAR_WIDTH, BUTTON_HEIGHT, SWP_NOZORDER );

	x -= CONTROL_GAP + COPY_WIDTH;
	SetWindowPos( s_wcd.hwndButtonCopy, HWND_TOP, x, y, COPY_WIDTH, BUTTON_HEIGHT, SWP_NOZORDER );
}

static void DrawFooterButton( const DRAWITEMSTRUCT *dis, const TCHAR *label )
{
	HBRUSH brush;
	HPEN pen;
	HGDIOBJ oldBrush, oldPen, oldFont;
	RECT rect;
	COLORREF fillColor, textColor;

	rect = dis->rcItem;

	if ( dis->itemState & ODS_DISABLED ) {
		fillColor = BUTTON_OFF_BG;
		textColor = MUTED_COLOR;
	} else if ( dis->itemState & ODS_SELECTED ) {
		fillColor = BUTTON_DOWN_BG;
		textColor = TEXT_COLOR;
	} else {
		fillColor = BUTTON_BG_COLOR;
		textColor = TEXT_COLOR;
	}

	brush = CreateSolidBrush( fillColor );
	pen = CreatePen( PS_SOLID, 1, BORDER_COLOR );

	oldBrush = SelectObject( dis->hDC, brush );
	oldPen = SelectObject( dis->hDC, pen );
	oldFont = SelectObject( dis->hDC, s_wcd.hfStatusFont );

	SetBkMode( dis->hDC, TRANSPARENT );
	SetTextColor( dis->hDC, textColor );

	RoundRect( dis->hDC, rect.left, rect.top, rect.right, rect.bottom, 8, 8 );
	DrawText( dis->hDC, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE );

	SelectObject( dis->hDC, oldFont );
	SelectObject( dis->hDC, oldPen );
	SelectObject( dis->hDC, oldBrush );

	DeleteObject( pen );
	DeleteObject( brush );
}

static void PaintFooter( HWND hWnd )
{
	PAINTSTRUCT ps;
	RECT rect;
	RECT textRect;
	HDC hdc;
	HPEN pen, oldPen;
	HGDIOBJ oldFont;

	hdc = BeginPaint( hWnd, &ps );
	GetClientRect( hWnd, &rect );

	FillRect( hdc, &rect, s_wcd.hbrFooterBackground );

	pen = CreatePen( PS_SOLID, 1, BORDER_COLOR );
	oldPen = static_cast<HPEN>( SelectObject( hdc, pen ) );
	MoveToEx( hdc, rect.left, rect.top, NULL );
	LineTo( hdc, rect.right, rect.top );
	SelectObject( hdc, oldPen );
	DeleteObject( pen );

	textRect = rect;
	textRect.left += BORDERW;
	textRect.right -= CLEAR_WIDTH + COPY_WIDTH + CONTROL_GAP * 3;

	oldFont = SelectObject( hdc, s_wcd.hfStatusFont );
	SetBkMode( hdc, TRANSPARENT );
	SetTextColor( hdc, s_wcd.statusText[0] ? ACCENT_COLOR : MUTED_COLOR );
	DrawText( hdc, AtoW( s_wcd.statusText[0] ? s_wcd.statusText : "System console ready" ),
		-1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS );
	SelectObject( hdc, oldFont );

	EndPaint( hWnd, &ps );
}


static int GetTimerMsec( void )
{
	int msec;
	if ( !com_sv_running || !com_sv_running->integer ) {
		msec = 50; // 20fps
	} else {
		msec = 1000 / Cvar_VariableIntegerValue( "sv_fps" );
	}
#ifndef DEDICATED
	if ( com_cl_running && com_cl_running->integer ) {
		if ( com_maxfps->integer ) {
			msec = 1000 / com_maxfps->integer;
		}
		if ( Cvar_VariableIntegerValue( "com_maxfpsUnfocused" ) ) {
			msec = 1000 / Cvar_VariableIntegerValue( "com_maxfpsUnfocused" );
		}
		if ( gw_minimized || CL_VideoRecording() ) {
			return 0;
		}
	}
#endif
	return msec;
}


static LRESULT WINAPI ConWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	char *cmdString;
	static qboolean s_timePolarity;
	static UINT conTimerID;
	int v;

	switch ( uMsg )
	{

	case WM_SETFOCUS:
		if ( s_wcd.hwndInputLine )
		{
			SetFocus( s_wcd.hwndInputLine );
		}
		break;

	case WM_ACTIVATE:

		if ( com_viewlog && ( com_dedicated && !com_dedicated->integer ) )
		{
			// if the viewlog is open, check to see if it's being minimized
			if ( com_viewlog->integer == 1 )
			{
				if ( HIWORD( wParam ) )		// minimized flag
				{
					Cvar_Set( "viewlog", "2" );
				}
			}
			else if ( com_viewlog->integer == 2 )
			{
				if ( !HIWORD( wParam ) )		// minimized flag
				{
					Cvar_Set( "viewlog", "1" );
				}
			}
		}
		break;

	case WM_QUERYENDSESSION:
		if ( com_dedicated && com_dedicated->integer && !com_errorEntered )
		{
			cmdString = CopyString( "quit" );
			Sys_QueEvent( 0, SE_CONSOLE, 0, 0, strlen( cmdString ) + 1, cmdString );
		}
		else
		{
			PostQuitMessage( 0 );
		}
		return TRUE;

	case WM_CLOSE:
		if ( com_dedicated && com_dedicated->integer && !com_errorEntered )
		{
			cmdString = CopyString( "quit" );
			Sys_QueEvent( 0, SE_CONSOLE, 0, 0, strlen( cmdString ) + 1, cmdString );
		}
		else if ( s_wcd.quitOnClose )
		{
			PostQuitMessage( 0 );
		}
		else
		{
			Sys_ShowConsole( 0, qfalse );
			Cvar_Set( "viewlog", "0" );
		}
		return 0;

	case WM_CTLCOLOREDIT:
		if ( ( HWND ) lParam == s_wcd.hwndInputLine )
		{
			SetBkColor( ( HDC ) wParam, INPUT_BG_COLOR );
			SetTextColor( ( HDC ) wParam, TEXT_COLOR );
			return ( LRESULT ) s_wcd.hbrInputBackground;
		}
		break;

	case WM_CTLCOLORSTATIC:
		if ( ( HWND ) lParam == s_wcd.hwndBuffer )
		{
			SetBkColor( ( HDC ) wParam, EDIT_COLOR );
			SetTextColor( ( HDC ) wParam, TEXT_COLOR );
			return ( LRESULT ) s_wcd.hbrEditBackground;
		}
		else if ( ( HWND ) lParam == s_wcd.hwndErrorBox )
		{
			if ( s_timePolarity & 1 )
			{
				SetBkColor( ( HDC ) wParam, ERROR_BG_COLOR );
				SetTextColor( ( HDC ) wParam, ERROR_COLOR_1 );
			}
			else
			{
				SetBkColor( ( HDC ) wParam, ERROR_BG_COLOR );
				SetTextColor( ( HDC ) wParam, ERROR_COLOR_2 );
			}
			return ( LRESULT ) s_wcd.hbrErrorBackground;
		}
		break;

	case WM_CREATE:
		s_wcd.hbrEditBackground = CreateSolidBrush( EDIT_COLOR );
		s_wcd.hbrInputBackground = CreateSolidBrush( INPUT_BG_COLOR );
		s_wcd.hbrFooterBackground = CreateSolidBrush( FOOTER_BG_COLOR );
		s_wcd.hbrWindowBackground = CreateSolidBrush( WINDOW_BG_COLOR );
		s_wcd.statusText[0] = '\0';
		GetWindowRect( hWnd, &g_wv.conRect );
		break;

	case WM_ERASEBKGND:
		{
			RECT rect;
			GetClientRect( hWnd, &rect );
			FillRect( (HDC)wParam, &rect, s_wcd.hbrWindowBackground );
			return 1;
		}

	case WM_MOVE:
		GetWindowRect( hWnd, &g_wv.conRect );
		break;

	case WM_SIZE:
		{
			RECT rect;
			int sth;
			int footerY;
			int inputY;
			int bufferY;
			int bufferHeight;

			sth = GetStatusBarHeight();
			GetClientRect( hWnd, &rect );

			s_wcd.windowWidth = rect.right - rect.left + 1;
			s_wcd.windowHeight = rect.bottom - rect.top + 1;

			footerY = rect.bottom - sth + 1;
			if ( s_wcd.hwndInputLine ) {
				inputY = footerY - CONTROL_GAP - INPUT_HEIGHT;
			} else {
				inputY = footerY;
			}
			bufferY = s_wcd.hwndErrorBox ? BORDERH + ERROR_HEIGHT + CONTROL_GAP : BORDERH;
			bufferHeight = ( s_wcd.hwndInputLine ? inputY - CONTROL_GAP : footerY - CONTROL_GAP ) - bufferY;
			if ( bufferHeight < 32 ) {
				bufferHeight = 32;
			}

			if ( s_wcd.hwndErrorBox ) {
				SetWindowPos( s_wcd.hwndBuffer, HWND_TOP, BORDERW, bufferY, rect.right - BORDERW*2, bufferHeight, SWP_NOZORDER );
			} else {
				SetWindowPos( s_wcd.hwndBuffer, HWND_TOP, BORDERW, bufferY, rect.right - BORDERW*2, bufferHeight, SWP_NOZORDER );
			}

			if ( s_wcd.hwndErrorBox ) {
				SetWindowPos( s_wcd.hwndErrorBox, HWND_TOP, BORDERW, BORDERH, rect.right - BORDERW*2, ERROR_HEIGHT, SWP_NOZORDER );
				InvalidateRect( s_wcd.hwndErrorBox, NULL, FALSE );
			}

			if ( s_wcd.hwndInputLine ) {
				SetWindowPos( s_wcd.hwndInputLine, HWND_TOP, BORDERW, inputY, rect.right - BORDERW*2, INPUT_HEIGHT, SWP_NOZORDER );
				InvalidateRect( s_wcd.hwndInputLine, NULL, FALSE );
			}

			if ( s_wcd.hwndStatusBar ) {
				SetWindowPos( s_wcd.hwndStatusBar, HWND_TOP, BORDERW, footerY, rect.right - BORDERW*2, sth, SWP_NOZORDER );
				LayoutFooterControls( s_wcd.hwndStatusBar );
				InvalidateRect( s_wcd.hwndStatusBar, NULL, FALSE );
			}

			GetWindowRect( hWnd, &g_wv.conRect );

			return 0;
		}

	case WM_SIZING:
		{
			int w, h;
			RECT *r;
			r = (LPRECT) lParam;
			w = r->right - r->left - MIN_WIDTH;
			h = r->bottom - r->top - MIN_HEIGHT;
			if ( w < 0 ) {
				if ( wParam == WMSZ_RIGHT || wParam == WMSZ_TOPRIGHT || wParam == WMSZ_BOTTOMRIGHT ) {
					r->right -= w;
				}
				if ( wParam == WMSZ_LEFT || wParam == WMSZ_TOPLEFT || wParam == WMSZ_BOTTOMLEFT ) {
					r->left += w;
				}
			}
			if ( h < 0 ) {
				if ( wParam == WMSZ_BOTTOM || wParam == WMSZ_BOTTOMLEFT || wParam == WMSZ_BOTTOMRIGHT ) {
					r->bottom -= h;
				}
				if ( wParam == WMSZ_TOP || wParam == WMSZ_TOPLEFT || wParam == WMSZ_TOPRIGHT ) {
					r->top += h;
				}
			}
			return TRUE;
		}

	case WM_SYSCOMMAND:
		// Prevent Alt+Letter commands from hanging the application temporarily
		if ( wParam == SC_KEYMENU || wParam == SC_MOUSEMENU + HTSYSMENU || wParam == SC_CLOSE + HTSYSMENU )
			return 0;

		// simulate drag move to avoid ~500ms delay between DefWindowProc() and further WM_ENTERSIZEMOVE
		if ( wParam == SC_MOVE + HTCAPTION )
		{
			mouse_event( MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN, 7, 0, 0, 0 );
			mouse_event( MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN, (DWORD)-7, 0, 0, 0 );
		}
		break;

	case WM_ENTERSIZEMOVE:
		if ( conTimerID == 0 && (v = GetTimerMsec()) > 0 ) {
			conTimerID = SetTimer( s_wcd.hWnd, CON_TIMER_ID, v, NULL );
		}
		break;

	case WM_EXITSIZEMOVE:
		if ( conTimerID != 0 ) {
			KillTimer( s_wcd.hWnd, conTimerID );
			conTimerID = 0;
		}
		break;

	case WM_TIMER:
		if ( wParam == ERROR_TIMER_ID )
		{
			s_timePolarity = s_timePolarity ? qfalse : qtrue;
			if ( s_wcd.hwndErrorBox )
			{
				InvalidateRect( s_wcd.hwndErrorBox, NULL, FALSE );
			}
		}
		else if ( wParam == CON_TIMER_ID && conTimerID != 0 && !com_errorEntered )
		{
#ifdef DEDICATED
			Com_Frame( qfalse );
#else
			//Com_Frame( CL_NoDelay() );
#endif
		}
		break;

	case WM_CONTEXTMENU:
			return 0;
    }

    return DefWindowProc( hWnd, uMsg, wParam, lParam );
}


static LRESULT WINAPI BufferWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	static UINT bufTimerID;
	int v;

	switch ( uMsg ) {

	case WM_VSCROLL:
		if ( (int)LOWORD(wParam) == SB_ENDSCROLL ) {
			if ( bufTimerID != 0 ) {
				KillTimer( hWnd, bufTimerID );
				bufTimerID = 0;
			}
		} else {
			if ( bufTimerID == 0 && (v = GetTimerMsec()) > 0 ) {
				bufTimerID = SetTimer( hWnd, BUF_TIMER_ID, v, NULL );
			}
		}
		break;

	case WM_CAPTURECHANGED:
		if ( (HWND)lParam == hWnd ) {
			if ( bufTimerID == 0 && (v = GetTimerMsec()) > 0 )
				bufTimerID = SetTimer( hWnd, BUF_TIMER_ID, v, NULL );
		} else {
			if ( bufTimerID != 0 ) {
				KillTimer( hWnd, bufTimerID );
				bufTimerID = 0;
			}
		}
		return 0;

#if 0 // this is actually redundant except setting focus to s_wcd.hwndInputLine
	case WM_COPY: {
			DWORD selStart, selEnd;
			SendMessage( hWnd, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd );
			if ( selStart != selEnd ) {
				if ( OpenClipboard( s_wcd.hWnd ) ) {
					int len;
					HGLOBAL hMem;
					TCHAR *text, *tmp;
					len = GetWindowTextLength( s_wcd.hwndBuffer ) + 1;
					tmp = (TCHAR*) malloc( len * sizeof( TCHAR ) );
					if ( tmp ) {
						GetWindowText( s_wcd.hwndBuffer, tmp, len );
						hMem = GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, ( selEnd - selStart + 1 ) * sizeof( TCHAR ) );
						if ( hMem != NULL ) {
							EmptyClipboard();
							text = (TCHAR*) GlobalLock( hMem );
							if ( text != NULL ) {
								memcpy( text, tmp + selStart, ( selEnd - selStart ) * sizeof( text[0] ) );
							}
							GlobalUnlock( hMem );
#ifdef UNICODE
							SetClipboardData( CF_UNICODETEXT, hMem );
#else
							SetClipboardData( CF_TEXT, hMem );
#endif
						}
						free( tmp );
					}
					CloseClipboard();
				}
				if ( s_wcd.hwndInputLine ) {
					SetFocus( s_wcd.hwndInputLine );
				}
				return 0;
			}
		}
		break;
#endif

	case WM_TIMER:
		if ( wParam == BUF_TIMER_ID && bufTimerID != 0 && !com_errorEntered )
		{
#ifdef DEDICATED
			Com_Frame( qfalse );
#else
			//Com_Frame( CL_NoDelay() );
#endif
		}
		if ( wParam == TEX_TIMER_ID && texTimerID != 0 ) {
			if ( conBufPos ) {
				// dump text
				AddBufferText( conBuffer, conBufPos );
				conBufPos = 0;
			} else {
				// kill timer
				KillTimer( hWnd, texTimerID );
				texTimerID = 0;
			}
		}
		return 0;

	case WM_CONTEXTMENU:
		return 0;

	case WM_CHAR: {
			if ( wParam != VK_CANCEL ) {
				// forward to input line
				SetFocus( s_wcd.hwndInputLine );
				SendMessage( s_wcd.hwndInputLine, WM_CHAR, wParam, lParam );
				return 0;
			}
		}
		break;
	}

	return CallWindowProc( (WNDPROC) s_wcd.SysBufferWndProc, hWnd, uMsg, wParam, lParam );
}


static LRESULT WINAPI StatusWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	int len;

	switch (uMsg)
	{

	case WM_SIZE:
		LayoutFooterControls( hWnd );
		InvalidateRect( hWnd, NULL, FALSE );
		return 0;

	case WM_PAINT:
		PaintFooter( hWnd );
		return 0;

	case WM_DRAWITEM:
		{
			const DRAWITEMSTRUCT *dis = (const DRAWITEMSTRUCT *)lParam;
			if ( dis->CtlID == COPY_ID ) {
				DrawFooterButton( dis, T("Copy All") );
				return TRUE;
			}
			if ( dis->CtlID == CLEAR_ID ) {
				DrawFooterButton( dis, T("Clear Log") );
				return TRUE;
			}
		}
		break;

	case WM_COMMAND:
		if ( LOWORD( wParam ) == COPY_ID )
		{
			fnql::win::ScopedClipboard clipboard( s_wcd.hWnd );
			if ( clipboard )
			{
				EmptyClipboard();
				len = GetWindowTextLength( s_wcd.hwndBuffer );
				if ( len > 0 ) {
					fnql::win::ScopedGlobalMemory hMem(
						GlobalAlloc( GMEM_MOVEABLE | GMEM_DDESHARE | GMEM_ZEROINIT, (len + 1) * sizeof( TCHAR ) ) );
					if ( hMem ) {
						bool copied = false;
						{
							fnql::win::ScopedGlobalLock<TCHAR> text( hMem.get() );
							if ( text ) {
								GetWindowText( s_wcd.hwndBuffer, text.get(), len + 1 );
								copied = true;
							}
						}
#ifdef UNICODE
						if ( copied && SetClipboardData( CF_UNICODETEXT, hMem.get() ) ) {
#else
						if ( copied && SetClipboardData( CF_TEXT, hMem.get() ) ) {
#endif
							hMem.release();
						}
					}
				}
			}
			if ( s_wcd.hwndInputLine ) {
				SetFocus( s_wcd.hwndInputLine );
			}
		}
		else if ( LOWORD( wParam ) == CLEAR_ID )
		{
			ConClear();
			if ( s_wcd.hwndInputLine ) {
				SetFocus( s_wcd.hwndInputLine );
			}
		}
		break;
	case WM_LBUTTONDBLCLK:
	case WM_LBUTTONDOWN:
		if ( s_wcd.hwndInputLine ) {
			SetFocus( s_wcd.hwndInputLine );
		}
		return 0;
    }

	return CallWindowProc( (WNDPROC)s_wcd.SysStatusWndProc, hWnd, uMsg, wParam, lParam );
}


static LRESULT WINAPI InputLineWndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	TCHAR inputBuffer[ MAX_EDIT_LINE ];
	int zDelta, fwKeys, i;
	WPARAM scrollMsg;

	switch ( uMsg )
	{
#if 0
	case WM_KILLFOCUS:
		if ( (HWND)wParam == s_wcd.hwndBuffer ) {
			SetFocus( s_wcd.hwndInputLine );
			return 0;
		}
		break;
#endif

	case WM_MOUSEWHEEL:
		zDelta = (short) HIWORD( wParam ) / WHEEL_DELTA;
		if ( zDelta ) {
			fwKeys = LOWORD( wParam );
			if ( zDelta > 0 ) {
				if ( fwKeys & MK_CONTROL )
					scrollMsg = SB_PAGEUP;
				else
					scrollMsg = SB_LINEUP;
			} else {
				zDelta = -zDelta;
				if ( fwKeys & MK_CONTROL )
					scrollMsg = SB_PAGEDOWN;
				else
					scrollMsg = SB_LINEDOWN;
			}
			for ( i = 0; i < zDelta; i++ ) {
				SendMessage( s_wcd.hwndBuffer, EM_SCROLL, scrollMsg, 0 );
			}
			return 0;
		}
		break;

	case WM_KEYDOWN:
	{
		if ( wParam == 'L' && ( GetKeyState( VK_CONTROL ) & 0x8000 ) ) {
			ConClear();
			return 0;
		}

		if ( wParam == VK_PRIOR ) {
			if ( GetKeyState( VK_CONTROL ) & 0x8000 )
				SendMessage( s_wcd.hwndBuffer, EM_SCROLL, (WPARAM)SB_PAGEUP, 0 );
			else
				SendMessage( s_wcd.hwndBuffer, EM_SCROLL, (WPARAM)SB_LINEUP, 0 );
			return 0;
		}

		if ( wParam == VK_NEXT ) {
			if ( GetKeyState( VK_CONTROL ) & 0x8000 )
				SendMessage( s_wcd.hwndBuffer, EM_SCROLL, (WPARAM)SB_PAGEDOWN, 0 );
			else
				SendMessage( s_wcd.hwndBuffer, EM_SCROLL, (WPARAM)SB_LINEDOWN, 0 );
			return 0;
		}

		if ( wParam == VK_UP ) {
			Con_HistoryGetPrev( &console );
			SetWindowText( hWnd, AtoW( console.buffer ) );
			SendMessage( hWnd, EM_SETSEL, (WPARAM) console.cursor, console.cursor );
			return 0;
		}

		if ( wParam == VK_DOWN ) {
			Con_HistoryGetNext( &console );
			SetWindowText( hWnd, AtoW( console.buffer ) );
			SendMessage( hWnd, EM_SETSEL, (WPARAM) console.cursor, console.cursor );
			return 0;
		}

		break;
	}

	case WM_CHAR:
		if ( wParam > 255 )
			return 0;
		if ( wParam == VK_RETURN )
		{
			DWORD pos;
			char *s;

			GetWindowText( hWnd, inputBuffer, sizeof( inputBuffer ) );
			Q_strncpyz( console.buffer, WtoA( inputBuffer ), sizeof( console.buffer ) );
			SendMessage( hWnd, EM_GETSEL, (WPARAM) &pos, (LPARAM) 0 );
			console.cursor = pos;
			Con_SaveField( &console );

			s = console.buffer;

			while ( *s == '\\' || *s == '/' ) // skip leading slashes
				s++;

			strncat( s_wcd.consoleText, s, sizeof( s_wcd.consoleText ) - strlen( s_wcd.consoleText ) - 2 );
			strcat( s_wcd.consoleText, "\n" );

			SetWindowText( s_wcd.hwndInputLine, T("") );
			Field_Clear( &console );

			Sys_Print( va( "]%s\n", WtoA( inputBuffer ) ) );

			return 0;
		}

		if ( wParam == VK_TAB ) {
			DWORD pos;

			GetWindowText( hWnd, inputBuffer, sizeof( inputBuffer ) );
			Q_strncpyz( console.buffer, WtoA( inputBuffer ), sizeof( console.buffer ) );
			SendMessage( hWnd, EM_GETSEL, (WPARAM) &pos, (LPARAM) 0 );
			console.cursor = pos;

			Field_AutoComplete( &console );

			SetWindowText( hWnd, AtoW( console.buffer ) );
			SendMessage( hWnd, EM_SETSEL, console.cursor, console.cursor );
			return 0;
		}
		break;

	case WM_CONTEXTMENU:
		return 0;
	}

	return CallWindowProc( (WNDPROC)s_wcd.SysInputLineWndProc, hWnd, uMsg, wParam, lParam );
}


/*
** Sys_CreateConsole
*/
void Sys_CreateConsole( const char *title, int xPos, int yPos, qboolean useXYpos )
{
	WNDCLASS wc;
	RECT rect;
	const TCHAR *DEDCLASS = T("Q3 WinConsole");

	int DEDSTYLE = WS_POPUPWINDOW | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SIZEBOX;
	int	fontWidth, fontHeight, statusFontHeight;
	int x, y, w, h, sth;
	int con_x, con_y;

	HMONITOR hMonitor;
	MONITORINFO mInfo;
	POINT		p;

	memset( &wc, 0, sizeof( wc ) );

	wc.style         = 0;
	wc.lpfnWndProc   = ConWndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = g_wv.hInstance;
	wc.hIcon         = LoadIcon( g_wv.hInstance, MAKEINTRESOURCE(IDI_ICON1));
	wc.hCursor       = LoadCursor (NULL,IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszMenuName  = 0;
	wc.lpszClassName = DEDCLASS;

	if ( !RegisterClass (&wc) )
		return;

	rect.left = 0;
	rect.right = DEFAULT_WIDTH;
	rect.top = 0;
	rect.bottom = DEFAULT_HEIGHT;

	AdjustWindowRect( &rect, DEDSTYLE, FALSE );

	// try to use command line provided coodinates to locate primary monitor
	if ( useXYpos ) {
		p.x = xPos;
		p.y = yPos;
	} else {
		GetCursorPos( &p );
	}

	memset( &mInfo, 0, sizeof( mInfo ) );
	mInfo.cbSize = sizeof( MONITORINFO );
	// Query display dimensions
	hMonitor = MonitorFromPoint( p, MONITOR_DEFAULTTONEAREST );
	if ( hMonitor && GetMonitorInfo( hMonitor, &mInfo ) ) {
		// current monitor info
		w = mInfo.rcMonitor.right - mInfo.rcMonitor.left;
		h = mInfo.rcMonitor.bottom - mInfo.rcMonitor.top;
		x = mInfo.rcMonitor.left;
		y = mInfo.rcMonitor.top;
	} else {
		// primary display info
		auto hDC = fnql::win::ScopedDisplayDC::ForDesktop();
		w = hDC ? GetDeviceCaps( hDC.get(), HORZRES ) : DEFAULT_WIDTH;
		h = hDC ? GetDeviceCaps( hDC.get(), VERTRES ) : DEFAULT_HEIGHT;
		x = 0;
		y = 0;
	}

	fontWidth = 0;
	fontHeight = -18;
	statusFontHeight = -15;

	s_wcd.windowWidth = rect.right - rect.left + 1;
	s_wcd.windowHeight = rect.bottom - rect.top + 1;

#ifdef DEDICATED
	if ( useXYpos )
	{
		con_x = xPos;
		con_y = yPos;
	}
	else
#endif
	{
		con_x = x + ( w - s_wcd.windowWidth ) / 2;
		con_y = y + ( h - s_wcd.windowHeight ) / 2;
	}

	s_wcd.hWnd = CreateWindowEx( 0, DEDCLASS,
		T(CONSOLE_WINDOW_TITLE), DEDSTYLE, con_x, con_y,
		s_wcd.windowWidth, s_wcd.windowHeight,
		NULL, NULL, g_wv.hInstance, NULL );

	if ( s_wcd.hWnd == NULL )
		return;

	InitCommonControls();

	s_wcd.hfBufferFont = CreateFont( fontHeight, fontWidth,
		0,
		0,
		FW_NORMAL,
		FALSE,
		FALSE,
		FALSE,
		DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY,
		FF_MODERN | FIXED_PITCH,
		T("Consolas") );

	s_wcd.hfStatusFont = CreateFont( statusFontHeight, 0,
		0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY,
		DEFAULT_PITCH,
		T("Segoe UI") );

	s_wcd.hwndStatusBar = CreateWindow( T("static"), NULL, WS_VISIBLE | WS_CHILD,
		1, 1, 32, FOOTER_HEIGHT, s_wcd.hWnd, NULL, g_wv.hInstance, NULL );

	// create the buttons
	s_wcd.hwndButtonCopy = CreateWindow( T("button"), T("Copy All"), WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
		0, 0, COPY_WIDTH, BUTTON_HEIGHT, s_wcd.hwndStatusBar, (HMENU)(LRESULT)COPY_ID, g_wv.hInstance, NULL );

	s_wcd.hwndButtonClear = CreateWindow( T("button"), T("Clear Log"), WS_VISIBLE | WS_CHILD | BS_OWNERDRAW,
		0, 0, CLEAR_WIDTH, BUTTON_HEIGHT, s_wcd.hwndStatusBar, (HMENU)(LRESULT)CLEAR_ID, g_wv.hInstance, NULL );

	SendMessage( s_wcd.hwndButtonCopy, WM_SETFONT, ( WPARAM ) s_wcd.hfStatusFont, 0 );
	SendMessage( s_wcd.hwndButtonClear, WM_SETFONT, ( WPARAM ) s_wcd.hfStatusFont, 0 );

	sth = GetStatusBarHeight();
	GetClientRect( s_wcd.hWnd, &rect );

	// create fonts
	//hDC = GetDC( s_wcd.hWnd );
	//nHeight = -MulDiv( 8, GetDeviceCaps( hDC, LOGPIXELSY ), 72);
	//ReleaseDC( s_wcd.hWnd, hDC );

	// create the input line
	s_wcd.hwndInputLine = CreateWindow( T("edit"), NULL, WS_CHILD | WS_VISIBLE | WS_BORDER |
		ES_LEFT | ES_AUTOHSCROLL,
		BORDERW, rect.bottom - sth - CONTROL_GAP - INPUT_HEIGHT, rect.right - BORDERW*2, INPUT_HEIGHT,
		s_wcd.hWnd,
		(HMENU)(LRESULT)INPUT_ID,	// child window ID
		g_wv.hInstance, NULL );

	// create the scrollbuffer
	s_wcd.hwndBuffer = CreateWindow( T("edit"), NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER |
		ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
		BORDERW, BORDERH, rect.right - BORDERW*2, rect.bottom - sth - INPUT_HEIGHT - CONTROL_GAP * 2 - BORDERH,
		s_wcd.hWnd,
		(HMENU)(LRESULT)EDIT_ID,	// child window ID
		g_wv.hInstance, NULL );

	SendMessage( s_wcd.hwndBuffer, WM_SETFONT, ( WPARAM ) s_wcd.hfBufferFont, 0 );
	SendMessage( s_wcd.hwndInputLine, WM_SETFONT, ( WPARAM ) s_wcd.hfBufferFont, 0 );

	s_wcd.SysInputLineWndProc = SetWindowLongPtr( s_wcd.hwndInputLine, GWLP_WNDPROC, ( LONG_PTR ) InputLineWndProc );
	s_wcd.SysStatusWndProc = SetWindowLongPtr( s_wcd.hwndStatusBar, GWLP_WNDPROC, ( LONG_PTR ) StatusWndProc );
	s_wcd.SysBufferWndProc = SetWindowLongPtr( s_wcd.hwndBuffer, GWLP_WNDPROC, ( LONG_PTR ) BufferWndProc );
	LayoutFooterControls( s_wcd.hwndStatusBar );

	if ( title && *title ) {
		SetWindowText( s_wcd.hWnd, AtoW( title ) );
	}

	ShowWindow( s_wcd.hWnd, SW_SHOWDEFAULT );
	UpdateWindow( s_wcd.hWnd );
	SetForegroundWindow( s_wcd.hWnd );

	SendMessage( s_wcd.hwndBuffer, EM_SETLIMITTEXT, MAX_CONSIZE, 0 );
	maxConSize = SendMessage( s_wcd.hwndBuffer, EM_GETLIMITTEXT, 0, 0 );

	SendMessage( s_wcd.hwndInputLine, EM_SETLIMITTEXT, MAX_EDIT_LINE, 0 );

	Field_Clear( &console );

	ConClear();

	Sys_SetStatus( "Server is not running" );

	s_wcd.visLevel = 1;
}


/*
** Sys_DestroyConsole
*/
void Sys_DestroyConsole( void )
{
	if ( s_wcd.hWnd )
	{
		ShowWindow( s_wcd.hWnd, SW_HIDE );
		CloseWindow( s_wcd.hWnd );
		DestroyWindow( s_wcd.hWnd );
		s_wcd.hWnd = NULL;
	}
}


/*
** Sys_ShowConsole
*/
void Sys_ShowConsole( int visLevel, qboolean quitOnClose )
{
	s_wcd.quitOnClose = quitOnClose;

	if ( visLevel == s_wcd.visLevel )
	{
		return;
	}

	s_wcd.visLevel = visLevel;

	if ( !s_wcd.hWnd )
		return;

	switch ( visLevel )
	{
	case 0:
		ShowWindow( s_wcd.hWnd, SW_HIDE );
		break;
	case 1:
		ShowWindow( s_wcd.hWnd, SW_SHOWNORMAL );
		curConSize = GetWindowTextLength( s_wcd.hwndBuffer );
		SendMessage( s_wcd.hwndBuffer, EM_SETSEL, curConSize, curConSize );
		SendMessage( s_wcd.hwndBuffer, EM_SCROLLCARET, 0, 0 );
		//SendMessage( s_wcd.hwndBuffer, EM_LINESCROLL, 0, 0xffff );
		break;
	case 2:
		ShowWindow( s_wcd.hWnd, SW_MINIMIZE );
		break;
	default:
		Sys_Error( "Invalid visLevel %d sent to Sys_ShowConsole\n", visLevel );
		break;
	}
}


/*
=============
Sys_SetStatus
=============
*/
void QDECL Sys_SetStatus( const char *format, ... )
{
	va_list		argptr;

	if ( s_wcd.hwndStatusBar == NULL )
		return;

	va_start( argptr, format );
	Q_vsnprintf( s_wcd.statusText, sizeof( s_wcd.statusText ), format, argptr );
	va_end( argptr );

	InvalidateRect( s_wcd.hwndStatusBar, NULL, FALSE );
}


/*
 =================
 Sys_ConsoleInput
 =================
*/
char *Sys_ConsoleInput( void )
{
	if ( s_wcd.consoleText[0] == '\0' )
	{
		return NULL;
	}

	strcpy( s_wcd.returnedText, s_wcd.consoleText );
	s_wcd.consoleText[0] = '\0';

	return s_wcd.returnedText;
}


/*
 =================
 Conbuf_AppendText
 =================
*/
void Conbuf_AppendText( const char *msg )
{
	char buffer[MAXPRINTMSG*2]; // reserve space for CR-LF expansion
	char *b = buffer;
	int bufLen, n;

	n = strlen( msg );

	// if the message is REALLY long, use just the last portion of it
	if ( n > (MAXPRINTMSG - 1) ) {
		msg += n - (MAXPRINTMSG - 1);
	}

	// insert skipped newline from previous message
	if ( s_wcd.newline ) {
		s_wcd.newline = qfalse;
		*b++ = '\r';
		*b++ = '\n';
	}

	// copy into an intermediate buffer
	while ( *msg )
	{
		if ( *msg == '\n' )
		{
			*b++ = '\r';
			*b++ = '\n';
			msg++;
		}
		else if ( *msg == '\r' )
		{
			*b++ = '\r';
			*b++ = '\n';
			msg++;
			if ( *msg == '\n' )
				msg++;
		}
		else if ( Q_IsColorString( msg ) )
		{
			msg += 2;
		}
		else
		{
			*b++ = *msg++;
		}
	}

	// try to skip ending newline to avoid inserting empty line in edit control
	if ( b - buffer >= 2 && *(b-1) == '\n' && *(b-2) == '\r' ) {
		s_wcd.newline = qtrue;
		b -= 2;
	}

	*b = '\0';
	bufLen = b - buffer;

	// not enough space in buffer -> flush
	if ( bufLen + conBufPos >= static_cast<int>( sizeof( conBuffer ) ) - 1 ) {
		AddBufferText( conBuffer, conBufPos );
		conBufPos = 0;
	}

	// new message is too long -> flush
	if ( bufLen >= static_cast<int>( sizeof( conBuffer ) ) - 1 ) {
		if ( conBufPos ) {
			AddBufferText( conBuffer, conBufPos );
			conBufPos = 0;
		}
		AddBufferText( buffer, bufLen );
		return;
	}

	// accumulate
	memcpy( conBuffer + conBufPos, buffer, bufLen + 1 );
	conBufPos += bufLen;

	// set flush timer
	if ( texTimerID == 0 ) {
		texTimerID = SetTimer( s_wcd.hwndBuffer, TEX_TIMER_ID,
			s_wcd.visLevel == 1 ? 25 : 100, NULL );
	}
}


static void AddBufferText( const char *text, int textLength )
{
	int lineCount;
	int pos, n;

	if ( textLength + curConSize >= maxConSize ) {
		lineCount = SendMessage( s_wcd.hwndBuffer, EM_GETLINECOUNT, 0, 0 );
		// cut off half from total lines count
		lineCount /= 2;
		if ( lineCount <= 1 ) {
			SetWindowText( s_wcd.hwndBuffer, T("") );
		} else {
			pos = SendMessage( s_wcd.hwndBuffer, EM_LINEINDEX, lineCount, 0 );
			SendMessage( s_wcd.hwndBuffer, EM_SETSEL, 0, pos );
			SendMessage( s_wcd.hwndBuffer, EM_REPLACESEL, FALSE, (LPARAM) TEXT("") );
		}
		curConSize = 0;
	}

	if ( !curConSize )
		curConSize = GetWindowTextLength( s_wcd.hwndBuffer );

	SendMessage( s_wcd.hwndBuffer, EM_GETSEL, (WPARAM)(LPDWORD)&pos, (LPARAM)(LPDWORD)&n );
	if ( pos != curConSize || n != curConSize ) {
		SendMessage( s_wcd.hwndBuffer, EM_SETSEL, curConSize, curConSize );
	}

	// put this text into the windows console
	//SendMessage( s_wcd.hwndBuffer, EM_LINESCROLL, 0, 0xffff );
	SendMessage( s_wcd.hwndBuffer, EM_SCROLLCARET, 0, 0 );
	SendMessage( s_wcd.hwndBuffer, EM_REPLACESEL, 0, (LPARAM) AtoW( text ) );

	curConSize += textLength;
}



/*
** Sys_SetErrorText
*/
void Sys_SetErrorText( const char *buf )
{
	RECT rect;
	int sth;

	if ( s_wcd.hwndErrorBox ) // already created
		return;

	// remove input field
	DestroyWindow( s_wcd.hwndInputLine );
	s_wcd.hwndInputLine = NULL;

	EnableWindow( s_wcd.hwndButtonClear, FALSE );

	s_wcd.hbrErrorBackground = CreateSolidBrush( ERROR_BG_COLOR );
	SetTimer( s_wcd.hWnd, ERROR_TIMER_ID, 1000, NULL );

	sth = GetStatusBarHeight();
	GetClientRect( s_wcd.hWnd, &rect );

	// shift buffer position
	SetWindowPos( s_wcd.hwndBuffer, HWND_TOP, BORDERW, BORDERH + ERROR_HEIGHT + CONTROL_GAP,
		rect.right - BORDERW*2,
		rect.bottom - sth - ERROR_HEIGHT - CONTROL_GAP * 2 - BORDERH,
		SWP_NOZORDER );

	s_wcd.hwndErrorBox = CreateWindow( T("static"), NULL, WS_CHILD | WS_VISIBLE | SS_SUNKEN,
		BORDERW, BORDERH, rect.right - BORDERW*2, ERROR_HEIGHT,
		s_wcd.hWnd,
		(HMENU)(LRESULT)ERRORBOX_ID,	// child window ID
		g_wv.hInstance, NULL );

	SendMessage( s_wcd.hwndErrorBox, WM_SETFONT, ( WPARAM ) s_wcd.hfBufferFont, 0 );
	SetWindowText( s_wcd.hwndErrorBox, AtoW( buf ) );

	Sys_SetStatus( "Fatal error occurred" );
}


void HandleConsoleEvents( void ) {
	MSG msg;

	// pump the message loop
	while ( PeekMessage( &msg, NULL, 0, 0, PM_NOREMOVE ) ) {
		if ( GetMessage( &msg, NULL, 0, 0 ) <= 0 ) {
			Cmd_Clear();
			Com_Quit_f();
		}

		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}
}

#ifdef __cplusplus
}
#endif
