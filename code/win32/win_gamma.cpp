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
** WIN_GAMMA.C
*/
#include "../client/client.h"
#include "glw_win.h"
#include "win_local.h"
#include "win_raii.h"

// Drivers quantise ramps - many keep 8 or 10 significant bits - so a readback
// never matches bit for bit. This tolerance stays far below the distance
// between any two ramps the renderer asks for, so it still detects a LUT that
// the desktop reloaded from the display profile behind our back.
#define GAMMA_READBACK_TOLERANCE 0x0400

static unsigned short s_oldHardwareGamma[3][256];	// ramp the desktop owned before we touched it
static unsigned short s_activeGamma[3][256];		// ramp we last handed to the driver
static TCHAR s_gammaDisplayName[CCHDEVICENAME];		// display s_oldHardwareGamma was sampled from
static qboolean s_baselineValid = qfalse;

static fnql::win::ScopedDisplayDC GLW_OpenGammaDC( const TCHAR *displayName )
{
	if ( displayName && displayName[0] )
	{
		return fnql::win::ScopedDisplayDC::ForDisplay( displayName );
	}

	return fnql::win::ScopedDisplayDC::ForDesktop();
}

static BOOL IsCurrentSessionRemoteable( void )
{
	BOOL fIsRemoteable = FALSE;

	if ( GetSystemMetrics( SM_REMOTESESSION ) )
	{
		fIsRemoteable = TRUE;
	}
	else
	{
		#define TERMINAL_SERVER_KEY TEXT( "SYSTEM\\CurrentControlSet\\Control\\Terminal Server\\" )
		#define GLASS_SESSION_ID TEXT( "GlassSessionId" )

		fnql::win::ScopedRegistryKey hRegKey;
		LONG lResult;

		lResult = RegOpenKeyEx( HKEY_LOCAL_MACHINE,	TERMINAL_SERVER_KEY, 0, KEY_READ, hRegKey.receive() );

		if ( lResult == ERROR_SUCCESS )
		{
			DWORD dwGlassSessionId;
			DWORD cbGlassSessionId = sizeof(dwGlassSessionId);
			DWORD dwType;

			lResult = RegQueryValueEx( hRegKey.get(), GLASS_SESSION_ID, NULL, &dwType, (BYTE*)&dwGlassSessionId, &cbGlassSessionId );

			if ( lResult == ERROR_SUCCESS )
			{
				typedef BOOL (WINAPI *PFN_ProcessIdToSessionId)( DWORD dwProcessId, DWORD *pSessionId );
				PFN_ProcessIdToSessionId pProcessIdToSessionId;
				DWORD dwCurrentSessionId;
				HMODULE hKernel32;

				hKernel32 = GetModuleHandleA( "kernel32" );
				if ( hKernel32 != NULL )
				{
					pProcessIdToSessionId = reinterpret_cast<PFN_ProcessIdToSessionId>( GetProcAddress( hKernel32, "ProcessIdToSessionId" ) );
					if ( pProcessIdToSessionId != NULL )
					{
						if ( pProcessIdToSessionId( GetCurrentProcessId(), &dwCurrentSessionId  ) )
						{
							fIsRemoteable = ( dwCurrentSessionId != dwGlassSessionId );
						}
					}
				}
			}
		}
	}

	return fIsRemoteable;
}


static qboolean GLW_RampsMatch( const unsigned short a[3][256], const unsigned short b[3][256] )
{
	int channel, i;

	for ( channel = 0; channel < 3; channel++ )
	{
		for ( i = 0; i < 256; i++ )
		{
			if ( abs( (int)a[channel][i] - (int)b[channel][i] ) > GAMMA_READBACK_TOLERANCE )
				return qfalse;
		}
	}

	return qtrue;
}


/*
** GLW_WriteGammaRamp
**
** Hands a ramp to the driver and confirms it stuck. Windows accepts ramps and
** then silently reloads the LUT from the display profile while a mode set, an
** ICC reload or a desktop switch is in flight, so a TRUE return from
** SetDeviceGammaRamp is not proof that the ramp is live.
*/
static qboolean GLW_WriteGammaRamp( const TCHAR *displayName, const unsigned short table[3][256] )
{
	unsigned short readback[3][256];
	LPVOID ramp = const_cast<unsigned short (*)[256]>( table );
	qboolean accepted = qfalse;
	int attempt;

	for ( attempt = 0; attempt < 2; attempt++ )
	{
		auto hDC = GLW_OpenGammaDC( displayName );
		if ( !hDC )
			break;

		if ( !SetDeviceGammaRamp( hDC.get(), ramp ) )
			continue;

		accepted = qtrue;

		// A driver that refuses the readback leaves us nothing to compare, so
		// take the accepted write at face value rather than retrying forever.
		if ( !GetDeviceGammaRamp( hDC.get(), readback ) || GLW_RampsMatch( readback, table ) )
			return qtrue;
	}

	if ( accepted )
		Com_DPrintf( "gamma ramp did not survive readback verification.\n" );

	return accepted;
}


/*
** GLW_CaptureBaseline
**
** Samples the ramp a display is currently showing and adopts it as the ramp we
** owe the user back. Only call this while we do not own the LUT, or the game's
** own ramp becomes the "desktop" ramp for the rest of the process lifetime.
*/
static qboolean GLW_CaptureBaseline( const TCHAR *displayName )
{
	auto hDC = GLW_OpenGammaDC( displayName );
	int i;

	s_baselineValid = qfalse;
	s_gammaDisplayName[0] = '\0';

	if ( !hDC )
		return qfalse;

	if ( !GetDeviceGammaRamp( hDC.get(), s_oldHardwareGamma ) )
		return qfalse;

	// do test setup
	if ( !SetDeviceGammaRamp( hDC.get(), s_oldHardwareGamma ) )
		return qfalse;

	//
	// do a sanity check on the gamma values
	//
	if ( ( HIBYTE( s_oldHardwareGamma[0][255] ) <= HIBYTE( s_oldHardwareGamma[0][0] ) ) ||
		 ( HIBYTE( s_oldHardwareGamma[1][255] ) <= HIBYTE( s_oldHardwareGamma[1][0] ) ) ||
		 ( HIBYTE( s_oldHardwareGamma[2][255] ) <= HIBYTE( s_oldHardwareGamma[2][0] ) ) )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: device has broken gamma support\n" );
		return qfalse;
	}

	//
	// make sure that we didn't have a prior crash in the game, and if so we need to
	// restore the gamma values to at least a linear value
	//
	if ( HIBYTE( s_oldHardwareGamma[0][181] ) == 255 )
	{
		Com_Printf( S_COLOR_CYAN "Gamma restoration: suspicious tables replaced with a linear ramp\n" );

		for ( i = 0; i < 256; i++ )
		{
			s_oldHardwareGamma[0][i] = ( i << 8 ) | i;
			s_oldHardwareGamma[1][i] = ( i << 8 ) | i;
			s_oldHardwareGamma[2][i] = ( i << 8 ) | i;
		}
	}

	lstrcpyn( s_gammaDisplayName, displayName ? displayName : TEXT( "" ), CCHDEVICENAME );
	s_baselineValid = qtrue;

	return qtrue;
}


/*
** GLW_TrackDisplay
**
** Keeps the saved desktop ramp paired with the monitor the window lives on. A
** ramp is per display, so restoring monitor A's calibration onto monitor B
** after the window moves would corrupt B instead of repairing A.
*/
static qboolean GLW_TrackDisplay( void )
{
	if ( s_baselineValid && lstrcmpi( s_gammaDisplayName, glw_state.displayName ) == 0 )
		return qtrue;

	// hand the previous display its own ramp back before adopting a new one
	GLW_RestoreGamma();

	return GLW_CaptureBaseline( glw_state.displayName );
}


static qboolean GLW_IsIdentityRamp( const unsigned char red[256], const unsigned char green[256], const unsigned char blue[256] )
{
	int i;

	for ( i = 0; i < 256; i++ )
	{
		if ( red[i] != i || green[i] != i || blue[i] != i )
			return qfalse;
	}

	return qtrue;
}


/*
** GLimp_InitGamma
**
** Determines if the underlying hardware supports the Win32 gamma correction API.
*/
void GLimp_InitGamma( glconfig_t *config )
{
	// A renderer restart can reach this point while we still own the LUT. Hand
	// it back before sampling or the game's ramp is what we later "restore".
	GLW_RestoreGamma();

	config->deviceSupportsGamma = qfalse;
	glw_state.deviceSupportsGamma = qfalse;
	glw_state.gammaSet = qfalse;
	s_baselineValid = qfalse;
	s_gammaDisplayName[0] = '\0';

	if ( IsCurrentSessionRemoteable() )
	{
		return; // no hardware gamma control via RDP
	}

	if ( !GLW_CaptureBaseline( glw_state.displayName ) )
	{
		return;
	}

	config->deviceSupportsGamma = qtrue;
	glw_state.deviceSupportsGamma = qtrue;
}


/*
** GLimp_SetGamma
**
** This routine should only be called if glConfig.deviceSupportsGamma is TRUE
*/
void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] ) {
	unsigned short table[3][256];
	int		i, j;

	if ( !glw_state.deviceSupportsGamma ) {
		return;
	}

	// A device gamma ramp is desktop-global on Windows. Retail rendering in a
	// window must stay on the shader/FBO color path so an abnormal process exit
	// cannot leave the user's desktop or ICC calibration altered.
	if ( !glw_state.cdsFullscreen ) {
		GLW_RestoreGamma();
		return;
	}

	// An identity request means the renderer applies its own correction - the
	// FBO shader path, or r_gamma 1 with overbright disabled. Give the monitor
	// its own calibration back instead of flattening the LUT to a linear ramp,
	// which is what made the image jump on every ALT+TAB.
	if ( GLW_IsIdentityRamp( red, green, blue ) ) {
		GLW_RestoreGamma();
		return;
	}

	if ( !gw_active ) {
		// The desktop owns the LUT while we are in the background. WM_ACTIVATE
		// drives SetColorMappings again on the way back in.
		return;
	}

	for ( i = 0; i < 256; i++ ) {
		table[0][i] = ( ( ( unsigned short ) red[i] ) << 8 ) | red[i];
		table[1][i] = ( ( ( unsigned short ) green[i] ) << 8 ) | green[i];
		table[2][i] = ( ( ( unsigned short ) blue[i] ) << 8 ) | blue[i];
	}

	// Win2K and newer put this odd restriction on gamma ramps...
	Com_DPrintf( "performing gamma clamp.\n" );
	for ( j = 0 ; j < 3 ; j++ ) {
		for ( i = 0 ; i < 128 ; i++ ) {
			if ( table[j][i] > ( (128+i) << 8 ) ) {
				table[j][i] = (128+i) << 8;
			}
		}
		if ( table[j][127] > 254<<8 ) {
			table[j][127] = 254<<8;
		}
	}

	// enforce constantly increasing
	for ( j = 0 ; j < 3 ; j++ ) {
		for ( i = 1 ; i < 256 ; i++ ) {
			if ( table[j][i] < table[j][i-1] ) {
				table[j][i] = table[j][i-1];
			}
		}
	}

	if ( !GLW_TrackDisplay() ) {
		return;
	}

	if ( !GLW_WriteGammaRamp( s_gammaDisplayName, table ) ) {
		Com_Printf( S_COLOR_YELLOW "SetDeviceGammaRamp failed.\n" );
		return;
	}

	memcpy( s_activeGamma, table, sizeof( s_activeGamma ) );
	glw_state.gammaSet = qtrue;
}


/*
** GLW_RestoreGamma
*/
void GLW_RestoreGamma( void )
{
	if ( !glw_state.gammaSet ) {
		return;
	}

	if ( !glw_state.deviceSupportsGamma || !s_baselineValid ) {
		// Nothing trustworthy to put back; stop claiming we own the LUT so a
		// later restore cannot push a stale ramp at an unrelated display.
		glw_state.gammaSet = qfalse;
		return;
	}

	if ( GLW_WriteGammaRamp( s_gammaDisplayName, s_oldHardwareGamma ) ) {
		glw_state.gammaSet = qfalse;
	}
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
	if ( !glw_state.gammaSet || !s_baselineValid ) {
		return;
	}

	if ( !glw_state.cdsFullscreen || !gw_active ) {
		GLW_RestoreGamma();
		return;
	}

	GLW_WriteGammaRamp( s_gammaDisplayName, s_activeGamma );
}
