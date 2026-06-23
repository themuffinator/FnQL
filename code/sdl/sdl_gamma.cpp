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

#include "../client/client.h"
#include "sdl_glw.h"

#ifdef _WIN32
#	include "../win32/win_raii.h"
#endif

#ifdef _WIN32
static unsigned short s_oldHardwareGamma[3][256];
static qboolean s_deviceSupportsGamma = qfalse;
static qboolean s_gammaSet = qfalse;
static char s_gammaDisplayName[CCHDEVICENAME];

class ScopedGammaDC {
public:
	static ScopedGammaDC ForDisplay( const char *displayName )
	{
		if ( displayName && displayName[0] ) {
			return ScopedGammaDC( CreateDCA( "DISPLAY", displayName, NULL, NULL ), true );
		}

		return ScopedGammaDC( GetDC( GetDesktopWindow() ), false );
	}

	ScopedGammaDC() = default;
	~ScopedGammaDC()
	{
		reset();
	}

	ScopedGammaDC( const ScopedGammaDC& ) = delete;
	ScopedGammaDC& operator=( const ScopedGammaDC& ) = delete;

	ScopedGammaDC( ScopedGammaDC&& other ) noexcept
		: hDC_( other.hDC_ )
		, deleteDC_( other.deleteDC_ )
	{
		other.hDC_ = NULL;
		other.deleteDC_ = false;
	}

	ScopedGammaDC& operator=( ScopedGammaDC&& other ) noexcept
	{
		if ( this != &other )
		{
			reset();
			hDC_ = other.hDC_;
			deleteDC_ = other.deleteDC_;
			other.hDC_ = NULL;
			other.deleteDC_ = false;
		}
		return *this;
	}

	HDC get() const noexcept
	{
		return hDC_;
	}

	void reset() noexcept
	{
		if ( !hDC_ ) {
			return;
		}

		if ( deleteDC_ ) {
			DeleteDC( hDC_ );
		} else {
			ReleaseDC( GetDesktopWindow(), hDC_ );
		}

		hDC_ = NULL;
		deleteDC_ = false;
	}

	explicit operator bool() const noexcept
	{
		return hDC_ != NULL;
	}

private:
	ScopedGammaDC( HDC hDC, bool deleteDC ) noexcept
		: hDC_( hDC )
		, deleteDC_( deleteDC )
	{
	}

	HDC hDC_ = NULL;
	bool deleteDC_ = false;
};

static BOOL IsCurrentSessionRemoteable( void )
{
	BOOL fIsRemoteable = FALSE;

	if ( GetSystemMetrics( SM_REMOTESESSION ) ) {
		fIsRemoteable = TRUE;
	} else {
		fnql::win::ScopedRegistryKey hRegKey;
		LONG lResult;

		lResult = RegOpenKeyExA( HKEY_LOCAL_MACHINE,
			"SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\",
			0, KEY_READ, hRegKey.receive() );

		if ( lResult == ERROR_SUCCESS ) {
			DWORD dwGlassSessionId;
			DWORD cbGlassSessionId = sizeof( dwGlassSessionId );
			DWORD dwType;

			lResult = RegQueryValueExA( hRegKey.get(), "GlassSessionId", NULL, &dwType,
				(BYTE *)&dwGlassSessionId, &cbGlassSessionId );

			if ( lResult == ERROR_SUCCESS ) {
				typedef BOOL (WINAPI *PFN_ProcessIdToSessionId)( DWORD dwProcessId, DWORD *pSessionId );
				PFN_ProcessIdToSessionId pProcessIdToSessionId;
				DWORD dwCurrentSessionId;
				HMODULE hKernel32;

				hKernel32 = GetModuleHandleA( "kernel32" );
				if ( hKernel32 != NULL ) {
					pProcessIdToSessionId = reinterpret_cast<PFN_ProcessIdToSessionId>( GetProcAddress( hKernel32, "ProcessIdToSessionId" ) );
					if ( pProcessIdToSessionId != NULL ) {
						if ( pProcessIdToSessionId( GetCurrentProcessId(), &dwCurrentSessionId ) ) {
							fIsRemoteable = ( dwCurrentSessionId != dwGlassSessionId );
						}
					}
				}
			}
		}
	}

	return fIsRemoteable;
}

static void SDLGamma_SetSupport( qboolean supported )
{
	s_deviceSupportsGamma = supported;
	if ( glw_state.config ) {
		glw_state.config->deviceSupportsGamma = supported;
	}
}

static ScopedGammaDC SDLGamma_OpenDC( const char *displayName )
{
	return ScopedGammaDC::ForDisplay( displayName );
}

static qboolean SDLGamma_GetWindowDisplayName( char *displayName, size_t displayNameSize )
{
	SDL_PropertiesID props;
	HWND hwnd;
	HMONITOR hMonitor;
	MONITORINFOEXA monitorInfo;

	if ( !displayName || !displayNameSize || !SDL_window ) {
		return qfalse;
	}

	props = SDL_GetWindowProperties( SDL_window );
	if ( !props ) {
		return qfalse;
	}

	hwnd = (HWND)SDL_GetPointerProperty( props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL );
	if ( !hwnd ) {
		return qfalse;
	}

	hMonitor = MonitorFromWindow( hwnd, MONITOR_DEFAULTTOPRIMARY );
	if ( !hMonitor ) {
		return qfalse;
	}

	monitorInfo.cbSize = sizeof( monitorInfo );
	if ( !GetMonitorInfoA( hMonitor, (MONITORINFO *)&monitorInfo ) ) {
		return qfalse;
	}

	Q_strncpyz( displayName, monitorInfo.szDevice, displayNameSize );
	return qtrue;
}

static qboolean SDLGamma_RestoreSavedRamp( void )
{
	ScopedGammaDC hDC;
	BOOL ret;

	if ( !s_gammaSet || !s_deviceSupportsGamma ) {
		return qfalse;
	}

	hDC = SDLGamma_OpenDC( s_gammaDisplayName );
	if ( !hDC ) {
		return qfalse;
	}

	ret = SetDeviceGammaRamp( hDC.get(), s_oldHardwareGamma );

	if ( ret ) {
		s_gammaSet = qfalse;
	}

	return ret ? qtrue : qfalse;
}

static qboolean SDLGamma_BackupMonitorGamma( const char *displayName )
{
	ScopedGammaDC hDC;
	qboolean supported = qfalse;

	hDC = SDLGamma_OpenDC( displayName );
	if ( !hDC ) {
		SDLGamma_SetSupport( qfalse );
		return qfalse;
	}

	supported = ( GetDeviceGammaRamp( hDC.get(), s_oldHardwareGamma ) != FALSE ) ? qtrue : qfalse;
	if ( supported ) {
		supported = ( SetDeviceGammaRamp( hDC.get(), s_oldHardwareGamma ) != FALSE ) ? qtrue : qfalse;
	}

	if ( !supported ) {
		SDLGamma_SetSupport( qfalse );
		return qfalse;
	}

	if ( ( HIBYTE( s_oldHardwareGamma[0][255] ) <= HIBYTE( s_oldHardwareGamma[0][0] ) ) ||
		( HIBYTE( s_oldHardwareGamma[1][255] ) <= HIBYTE( s_oldHardwareGamma[1][0] ) ) ||
		( HIBYTE( s_oldHardwareGamma[2][255] ) <= HIBYTE( s_oldHardwareGamma[2][0] ) ) ) {
		SDLGamma_SetSupport( qfalse );
		Com_Printf( S_COLOR_YELLOW "WARNING: device has broken gamma support\n" );
		return qfalse;
	}

	if ( HIBYTE( s_oldHardwareGamma[0][181] ) == 255 ) {
		int i;

		Com_Printf( S_COLOR_YELLOW "WARNING: suspicious gamma tables, using linear ramp for restoration\n" );

		for ( i = 0; i < 256; i++ ) {
			s_oldHardwareGamma[0][i] = i << 8;
			s_oldHardwareGamma[1][i] = i << 8;
			s_oldHardwareGamma[2][i] = i << 8;
		}
	}

	Q_strncpyz( s_gammaDisplayName, displayName, sizeof( s_gammaDisplayName ) );
	SDLGamma_SetSupport( qtrue );
	return qtrue;
}

static qboolean SDLGamma_TrackWindowMonitor( qboolean restorePrevious )
{
	char displayName[CCHDEVICENAME];

	if ( IsCurrentSessionRemoteable() ) {
		if ( restorePrevious ) {
			SDLGamma_RestoreSavedRamp();
		}
		s_gammaDisplayName[0] = '\0';
		SDLGamma_SetSupport( qfalse );
		return qfalse;
	}

	if ( !SDLGamma_GetWindowDisplayName( displayName, sizeof( displayName ) ) ) {
		SDLGamma_SetSupport( qfalse );
		return qfalse;
	}

	if ( !Q_stricmp( displayName, s_gammaDisplayName ) ) {
		return s_deviceSupportsGamma;
	}

	if ( restorePrevious ) {
		SDLGamma_RestoreSavedRamp();
	}

	return SDLGamma_BackupMonitorGamma( displayName );
}
#endif

void GLimp_InitGamma( glconfig_t *config )
{
#ifdef _WIN32
	s_gammaSet = qfalse;
	s_gammaDisplayName[0] = '\0';
	config->deviceSupportsGamma = qfalse;

	if ( !SDL_window ) {
		return;
	}

	if ( SDLGamma_TrackWindowMonitor( qfalse ) ) {
		config->deviceSupportsGamma = s_deviceSupportsGamma;
	}
#else
	config->deviceSupportsGamma = qfalse;
#endif
}


/*
=================
GLimp_SetGamma
=================
*/
void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
#ifdef _WIN32
	unsigned short table[3][256];
	ScopedGammaDC hDC;
	BOOL ret;
	int i, j;

	if ( !SDL_window || !SDLGamma_TrackWindowMonitor( qtrue ) ) {
		return;
	}

	for ( i = 0; i < 256; i++ ) {
		table[0][i] = ( ( (unsigned short)red[i] ) << 8 ) | red[i];
		table[1][i] = ( ( (unsigned short)green[i] ) << 8 ) | green[i];
		table[2][i] = ( ( (unsigned short)blue[i] ) << 8 ) | blue[i];
	}

	Com_DPrintf( "performing gamma clamp.\n" );
	for ( j = 0; j < 3; j++ ) {
		for ( i = 0; i < 128; i++ ) {
			if ( table[j][i] > ( ( 128 + i ) << 8 ) ) {
				table[j][i] = ( 128 + i ) << 8;
			}
		}
		if ( table[j][127] > 254 << 8 ) {
			table[j][127] = 254 << 8;
		}
	}

	for ( j = 0; j < 3; j++ ) {
		for ( i = 1; i < 256; i++ ) {
			if ( table[j][i] < table[j][i - 1] ) {
				table[j][i] = table[j][i - 1];
			}
		}
	}

	hDC = SDLGamma_OpenDC( s_gammaDisplayName );
	if ( !hDC ) {
		return;
	}

	ret = SetDeviceGammaRamp( hDC.get(), table );

	if ( !ret ) {
		Com_Printf( S_COLOR_YELLOW "SetDeviceGammaRamp failed.\n" );
	} else {
		s_gammaSet = qtrue;
	}
#else
	(void)red;
	(void)green;
	(void)blue;
#endif
}


/*
** GLW_RestoreGamma
*/
void GLW_RestoreGamma( void )
{
#ifdef _WIN32
	if ( !SDLGamma_RestoreSavedRamp() && s_gammaSet ) {
		Com_DPrintf( "SDL gamma restore failed.\n" );
	}
#endif
}
