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
// Drivers quantise ramps - many keep 8 or 10 significant bits - so a readback
// never matches bit for bit. This tolerance stays far below the distance
// between any two ramps the renderer asks for, so it still detects a LUT that
// the desktop reloaded from the display profile behind our back.
#define GAMMA_READBACK_TOLERANCE 0x0400

static unsigned short s_oldHardwareGamma[3][256];	// ramp the desktop owned before we touched it
static unsigned short s_activeGamma[3][256];		// ramp we last handed to the driver
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

static qboolean SDLGamma_RampsMatch( const unsigned short a[3][256], const unsigned short b[3][256] )
{
	int channel, i;

	for ( channel = 0; channel < 3; channel++ ) {
		for ( i = 0; i < 256; i++ ) {
			if ( abs( (int)a[channel][i] - (int)b[channel][i] ) > GAMMA_READBACK_TOLERANCE ) {
				return qfalse;
			}
		}
	}

	return qtrue;
}


/*
=================
SDLGamma_WriteRamp

Hands a ramp to the driver and confirms it stuck. Windows accepts ramps and
then silently reloads the LUT from the display profile while a mode set, an ICC
reload or a desktop switch is in flight, so a TRUE return from
SetDeviceGammaRamp is not proof that the ramp is live.
=================
*/
static qboolean SDLGamma_WriteRamp( const char *displayName, const unsigned short table[3][256] )
{
	unsigned short readback[3][256];
	LPVOID ramp = const_cast<unsigned short (*)[256]>( table );
	qboolean accepted = qfalse;
	int attempt;

	for ( attempt = 0; attempt < 2; attempt++ ) {
		ScopedGammaDC hDC = SDLGamma_OpenDC( displayName );

		if ( !hDC ) {
			break;
		}

		if ( !SetDeviceGammaRamp( hDC.get(), ramp ) ) {
			continue;
		}

		accepted = qtrue;

		// A driver that refuses the readback leaves us nothing to compare, so
		// take the accepted write at face value rather than retrying forever.
		if ( !GetDeviceGammaRamp( hDC.get(), readback ) || SDLGamma_RampsMatch( readback, table ) ) {
			return qtrue;
		}
	}

	if ( accepted ) {
		Com_DPrintf( "gamma ramp did not survive readback verification.\n" );
	}

	return accepted;
}


static qboolean SDLGamma_IsIdentityRamp( const unsigned char red[256], const unsigned char green[256], const unsigned char blue[256] )
{
	int i;

	for ( i = 0; i < 256; i++ ) {
		if ( red[i] != i || green[i] != i || blue[i] != i ) {
			return qfalse;
		}
	}

	return qtrue;
}


static qboolean SDLGamma_RestoreSavedRamp( void )
{
	if ( !s_gammaSet ) {
		return qfalse;
	}

	if ( !s_deviceSupportsGamma ) {
		// Nothing trustworthy to put back; stop claiming we own the LUT so a
		// later restore cannot push a stale ramp at an unrelated display.
		s_gammaSet = qfalse;
		return qfalse;
	}

	if ( !SDLGamma_WriteRamp( s_gammaDisplayName, s_oldHardwareGamma ) ) {
		return qfalse;
	}

	s_gammaSet = qfalse;
	return qtrue;
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

		Com_Printf( S_COLOR_CYAN "Gamma restoration: suspicious tables replaced with a linear ramp\n" );

		for ( i = 0; i < 256; i++ ) {
			s_oldHardwareGamma[0][i] = ( i << 8 ) | i;
			s_oldHardwareGamma[1][i] = ( i << 8 ) | i;
			s_oldHardwareGamma[2][i] = ( i << 8 ) | i;
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
	// A renderer restart can reach this point while we still own the LUT. Hand
	// it back before sampling or the game's ramp is what we later "restore".
	SDLGamma_RestoreSavedRamp();

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
	int i, j;

	if ( !SDL_window ) {
		return;
	}

	// A device gamma ramp is desktop-global on Windows. Retail rendering in a
	// window must stay on the shader/FBO color path so an abnormal process exit
	// cannot leave the user's desktop or ICC calibration altered.
	if ( !glw_state.isFullscreen ) {
		SDLGamma_RestoreSavedRamp();
		return;
	}

	// An identity request means the renderer applies its own correction - the
	// FBO shader path, or r_gamma 1 with overbright disabled. Give the monitor
	// its own calibration back instead of flattening the LUT to a linear ramp,
	// which is what made the image jump on every ALT+TAB.
	if ( SDLGamma_IsIdentityRamp( red, green, blue ) ) {
		SDLGamma_RestoreSavedRamp();
		return;
	}

	if ( !gw_active ) {
		// The desktop owns the LUT while we are in the background. Focus events
		// drive SetColorMappings again on the way back in.
		return;
	}

	if ( !SDLGamma_TrackWindowMonitor( qtrue ) ) {
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

	if ( !SDLGamma_WriteRamp( s_gammaDisplayName, table ) ) {
		Com_Printf( S_COLOR_YELLOW "SetDeviceGammaRamp failed.\n" );
		return;
	}

	memcpy( s_activeGamma, table, sizeof( s_activeGamma ) );
	s_gammaSet = qtrue;
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


/*
** GLW_ReapplyGamma
**
** Mode sets, ICC profile reloads and desktop switches reload the LUT from the
** display profile after the fact, which shows up as the game running on the
** desktop ramp once it is back in the foreground. Re-assert the ramp we own
** when the window system tells us the display changed underneath us.
*/
void GLW_ReapplyGamma( void )
{
#ifdef _WIN32
	if ( !s_gammaSet || !s_deviceSupportsGamma ) {
		return;
	}

	if ( !glw_state.isFullscreen || !gw_active ) {
		SDLGamma_RestoreSavedRamp();
		return;
	}

	SDLGamma_WriteRamp( s_gammaDisplayName, s_activeGamma );
#endif
}
