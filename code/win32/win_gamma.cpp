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

static unsigned short s_oldHardwareGamma[3][256];

static fnql::win::ScopedDisplayDC GLW_OpenGammaDC( void )
{
	if ( glw_state.displayName[0] )
	{
		return fnql::win::ScopedDisplayDC::ForDisplay( glw_state.displayName );
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


/*
** GLW_InitGamma
**
** Determines if the underlying hardware supports the Win32 gamma correction API.
*/
void GLimp_InitGamma( glconfig_t *config )
{
	config->deviceSupportsGamma = qfalse;

	if ( IsCurrentSessionRemoteable() )
	{
		glw_state.deviceSupportsGamma = qfalse;
		return; // no hardware gamma control via RDP
	}

	auto hDC = GLW_OpenGammaDC();
	if ( hDC )
	{
		config->deviceSupportsGamma = ( GetDeviceGammaRamp( hDC.get(), s_oldHardwareGamma ) == FALSE ) ? qfalse : qtrue;
		if ( config->deviceSupportsGamma )
		{
			// do test setup
			if ( SetDeviceGammaRamp( hDC.get(), s_oldHardwareGamma ) == FALSE )
			{
				config->deviceSupportsGamma = qfalse;
			}
		}
	}

	if ( config->deviceSupportsGamma )
	{
		//
		// do a sanity check on the gamma values
		//
		if ( ( HIBYTE( s_oldHardwareGamma[0][255] ) <= HIBYTE( s_oldHardwareGamma[0][0] ) ) ||
			 ( HIBYTE( s_oldHardwareGamma[1][255] ) <= HIBYTE( s_oldHardwareGamma[1][0] ) ) ||
			 ( HIBYTE( s_oldHardwareGamma[2][255] ) <= HIBYTE( s_oldHardwareGamma[2][0] ) ) )
		{
			config->deviceSupportsGamma = qfalse;
			Com_Printf( S_COLOR_YELLOW "WARNING: device has broken gamma support\n" );
		}

		//
		// make sure that we didn't have a prior crash in the game, and if so we need to
		// restore the gamma values to at least a linear value
		//
		if ( ( HIBYTE( s_oldHardwareGamma[0][181] ) == 255 ) )
		{
			int g;

			Com_Printf( S_COLOR_YELLOW "WARNING: suspicious gamma tables, using linear ramp for restoration\n" );

			for ( g = 0; g < 256; g++ )
			{
				s_oldHardwareGamma[0][g] = g << 8;
				s_oldHardwareGamma[1][g] = g << 8;
				s_oldHardwareGamma[2][g] = g << 8;
			}
		}
	} // if ( config->deviceSupportsGamma )

	glw_state.deviceSupportsGamma = config->deviceSupportsGamma;
}


/*
void mapGammaMax( void ) {
	int		i, j;
	unsigned short table[3][256];

	// try to figure out what win2k will let us get away with setting
	for ( i = 0 ; i < 256 ; i++ ) {
		if ( i >= 128 ) {
			table[0][i] = table[1][i] = table[2][i] = 0xffff;
		} else {
			table[0][i] = table[1][i] = table[2][i] = i<<9;
		}
	}

	for ( i = 0 ; i < 128 ; i++ ) {
		for ( j = i*2 ; j < 255 ; j++ ) {
			table[0][i] = table[1][i] = table[2][i] = j<<8;
			if ( !SetDeviceGammaRamp( glw_state.hDC, table ) ) {
				break;
			}
		}
		table[0][i] = table[1][i] = table[2][i] = i<<9;
		Com_Printf( "index %i max: %i\n", i, j-1 );
	}
}
*/


/*
** GLimp_SetGamma
**
** This routine should only be called if glConfig.deviceSupportsGamma is TRUE
*/
void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] ) {
	unsigned short table[3][256];
	int		i, j;
	BOOL	ret;

	if ( /*!glw_state.hDC* ||*/ !gw_active )
		return;

//mapGammaMax();

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

	auto hDC = GLW_OpenGammaDC();
	ret = hDC ? SetDeviceGammaRamp( hDC.get(), table ) : FALSE;

	if ( !ret ) {
		Com_Printf( S_COLOR_YELLOW "SetDeviceGammaRamp failed.\n" );
	} else {
		glw_state.gammaSet = qtrue;
	}
}


/*
** GLW_RestoreGamma
*/
void GLW_RestoreGamma( void )
{
	BOOL ret;

	if ( !glw_state.gammaSet ) {
		return;
	}

	if ( !glw_state.deviceSupportsGamma ) {
		return;
	}	

	auto hDC = GLW_OpenGammaDC();
	ret = hDC ? SetDeviceGammaRamp( hDC.get(), s_oldHardwareGamma ) : FALSE;

	if ( ret ) {
		glw_state.gammaSet = qfalse;
	}
}
