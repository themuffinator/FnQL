/*
===========================================================================
Copyright (C) 2026

This file is part of FnQL.

FnQL is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

FnQL is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with FnQL; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// cl_webui.cpp -- Quake Live WebUI/Awesomium host wiring

#include "client.h"
#include "webui_backend.hpp"

#include <cstdint>

#if defined( _WIN32 )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "../win32/win_local.h"
#endif

namespace fnql::webui {

BackendHost &ClientBackendHost() noexcept {
	static BackendHost host;
	return host;
}

} // namespace fnql::webui

#define CL_WEB_DEFAULT_URL "asset://ql/index.html"
#define CL_WEB_BROWSER_EVENT_COUNT 32
#define CL_WEB_EVENT_NAME_LENGTH 128
#define CL_WEB_EVENT_PAYLOAD_LENGTH 4096
#define CL_WEB_NATIVE_REQUESTS_PER_FRAME 8
#define CL_WEB_NATIVE_REQUEST_BUSY_POLL_FRAMES 1
#define CL_WEB_NATIVE_REQUEST_IDLE_POLL_FRAMES 15
#define CL_WEB_NATIVE_REQUEST_LOADING_POLL_FRAMES 30
#define CL_WEB_MAX_RESOURCE_BYTES ( 64 * 1024 * 1024 )
#define CL_WEB_BRIDGE_RETRY_FRAMES 30
#define CL_WEB_CONFIG_SYNC_FRAMES 300
#define CL_WEB_CONFIG_JSON_LENGTH 8192
#define CL_WEB_CONFIG_ITEM_LENGTH 1536
#define CL_WEB_CATALOG_JSON_LENGTH 65536
#define CL_WEB_CATALOG_FILE_LIST_LENGTH 32768
#define CL_WEB_CATALOG_SYNC_CHUNK_CHARS 8192
#define CL_WEB_MAX_CATALOG_ITEMS 512
#define CL_WEB_SERVER_LOCAL_REFRESH_WAIT_MSEC 1000
#define CL_WEB_SERVER_REMOTE_REFRESH_WAIT_MSEC 5000
#define CL_WEB_SERVER_REFRESH_TIMEOUT_MSEC 15000
#define CL_WEB_SERVER_DETAILS_TIMEOUT_MSEC 5000
#define CL_WEB_KEYBOARD_EVENT_KEYDOWN_TYPE 0u
#define CL_WEB_KEYBOARD_EVENT_KEYUP_TYPE 1u
#define CL_WEB_KEYBOARD_EVENT_CHAR_TYPE 2u
#define CL_WEB_KEYBOARD_EVENT_ACTIVATION_TYPE 0u
#define CL_WEB_KEYBOARD_EVENT_ACTIVATION_VIRTUAL_KEY 0x11u
#define CL_WEB_KEYBOARD_EVENT_ACTIVATION_NATIVE_KEY 0x1d0001L
#define CL_ADVERTISEMENT_DEBUG_LABEL_COUNT 2

static cvar_t *cl_webuiEnable;
static cvar_t *cl_webZoom;
static cvar_t *cl_webConsole;
static cvar_t *cl_webBrowserActive;

typedef struct {
	int			sequence;
	char		name[CL_WEB_EVENT_NAME_LENGTH];
	char		payload[CL_WEB_EVENT_PAYLOAD_LENGTH];
} clWebUiBrowserEvent_t;

typedef struct {
	qboolean	initialized;
	qboolean	commandsRegistered;
	qboolean	browserVisible;
	qboolean	browserActive;
	qboolean	appActive;
	qboolean	reportedUnavailable;
	qboolean	startupBridgeInjected;
	qboolean	keyCaptureArmed;
	char		pendingHash[MAX_STRING_CHARS];
	char		currentUrl[MAX_STRING_CHARS];
	char		lastError[MAX_STRING_CHARS];
	int			lastErrorCode;
	int			eventSequence;
	int			eventHead;
	int			lastReplayedEventSequence;
	int			frameSequence;
	int			nextNativeRequestPollFrame;
	int			nextBridgeRetryFrame;
	int			nextConfigSnapshotFrame;
	qboolean	configSnapshotSynced;
	qboolean	demoSnapshotSynced;
	qboolean	mapCatalogSynced;
	qboolean	factoryCatalogSynced;
	qboolean	serverRefreshInitialized;
	qboolean	serverRefreshActive;
	int			serverRefreshRequestMode;
	int			serverRefreshSource;
	int			serverRefreshTime;
	int			serverRefreshTimeoutTime;
	qboolean	serverDetailActive;
	netadr_t	serverDetailAddress;
	unsigned int serverDetailIp;
	unsigned short serverDetailPort;
	int			serverDetailTimeoutTime;
	char		serverDetailId[32];
	qboolean	cursorPositionValid;
	int			cursorX;
	int			cursorY;
	char		tooltip[MAX_QPATH];
#if defined( _WIN32 )
	HCURSOR		activeCursorHandle;
	HCURSOR		restoreCursorHandle;
	int			activeCursorType;
	qboolean	cursorOverrideActive;
#endif
	clWebUiBrowserEvent_t events[CL_WEB_BROWSER_EVENT_COUNT];
} clWebUiState_t;

typedef struct {
	qboolean	initialised;
	qboolean	overlayCompiled;
	qboolean	overlayAvailable;
	int			frameTime;
	int			viewWidth;
	int			viewHeight;
	int			activeAdvertCellId;
	int			activatedAdvertCellId;
	int			delayDeadline;
	char		mapPath[MAX_QPATH];
} clAdvertisementBridgeState_t;

static clWebUiState_t cl_webui;
static clAdvertisementBridgeState_t cl_advertisementBridge;

static void CL_WebHost_UpdateOverlayOwnership( void );
static void CL_WebHost_EnsureStartupBridge( void );
static void CL_WebHost_UpdateBrowserNativeState( void );
static void CL_WebHost_PumpNativeJavascriptRequests( void );
static void CL_WebHost_SyncNativeSnapshots( qboolean force );
static void CL_WebHost_ServerBrowserFrame( void );
static void CL_WebHost_ServerDetailsFrame( void );
static void CL_WebHost_ClearCursorOverride( void );
static void CL_WebHost_UpdateBrowserCursorPosition( int x, int y );
static qboolean CL_WebUI_RuntimeRequested( void );
static qboolean CL_WebUI_RuntimeAvailable( void );
static void CL_WebView_ReplayRetainedEvents( void );
static void CL_AdvertisementBridge_UpdateCvars( void );
static void CL_Steam_OverlayCommand_f( void );
static void CL_WebHost_BuildConfigJson( char *buffer, size_t bufferSize );
static void CL_WebHost_BuildMapListJson( char *buffer, size_t bufferSize );
static void CL_WebHost_BuildFactoryListJson( char *buffer, size_t bufferSize );
static void CL_WebHost_BuildDemoListJson( char *buffer, size_t bufferSize );
static void CL_WebHost_ReadClipboardText( char *buffer, size_t bufferSize );
static qboolean CL_WebUI_EnsureBackendStarted( void );

static void CL_WebUI_SetCvarIfChanged( const char *name, const char *value ) {
	const char *current;

	if ( !name || !value ) {
		return;
	}

	current = Cvar_VariableString( name );
	if ( strcmp( current, value ) ) {
		Cvar_Set( name, value );
	}
}

static const char *CL_WebUI_BackendProviderLabel( void ) {
	return CL_WebUI_RuntimeAvailable()
		? fnql::webui::ClientBackendHost().ProviderName()
		: "Unavailable";
}

static const char *CL_WebUI_BackendPolicyLabel( void ) {
	if ( !CL_WebUI_RuntimeRequested() ) {
		return "default-off";
	}
	return CL_WebUI_RuntimeAvailable()
		? "explicit-external-backend"
		: "runtime-backend-unavailable";
}

static const char *CL_AdvertisementBridgeProviderLabel( void ) {
	return CL_WebUI_BackendProviderLabel();
}

static const char *CL_AdvertisementBridgePolicyLabel( void ) {
	return CL_WebUI_BackendPolicyLabel();
}

static const char *CL_WebHost_OnlineServicesModeLabel( void ) {
	return "Unavailable";
}

static const char *CL_WebHost_OnlineServicesPolicyLabel( void ) {
	return "compatibility-unavailable";
}

static const char *CL_WebHost_OnlineServicesParityScopeLabel( void ) {
	return "retail Steam online-services bridge";
}

static const char *CL_WebHost_OnlineServicesParityReasonLabel( void ) {
	return "backend scaffold only; Steamworks online services are not bundled";
}

static const char *CL_WebHost_MatchmakingProviderLabel( void ) {
	return "Unavailable";
}

static const char *CL_WebHost_MatchmakingPolicyLabel( void ) {
	return "Steamworks bridge unavailable";
}

static const char *CL_WebHost_WorkshopProviderLabel( void ) {
	return "Unavailable";
}

static const char *CL_WebHost_WorkshopPolicyLabel( void ) {
	return "Steamworks bridge unavailable";
}

static const char *CL_WebHost_ResourceBridgeProviderLabel( void ) {
	return "Unavailable";
}

static const char *CL_WebHost_ResourceBridgePolicyLabel( void ) {
	return "Steamworks resource bridge unavailable";
}

static const char *CL_WebHost_ResourceBridgeParityScopeLabel( void ) {
	return "retail Steam resource and avatar bridge";
}

static const char *CL_WebHost_ResourceBridgeParityReasonLabel( void ) {
	return "backend scaffold only; Steamworks resource data source is not bundled";
}

static qboolean CL_WebHost_IsResourceURI( const char *url ) {
	return ( url && strstr( url, "://" ) ) ? qtrue : qfalse;
}

static void CL_AdvertisementBridge_ClearLabel( char *buffer, int bufferSize ) {
	if ( !buffer || bufferSize <= 0 ) {
		return;
	}

	buffer[0] = '\0';
}

static void CL_AdvertisementBridge_LogLifecycle( const char *stage, int cellId ) {
	Com_DPrintf( "Advert bridge %s: cell=%d active=%d activated=%d via %s [%s]\n",
		stage ? stage : "sync",
		cellId,
		cl_advertisementBridge.activeAdvertCellId,
		cl_advertisementBridge.activatedAdvertCellId,
		CL_AdvertisementBridgeProviderLabel(),
		CL_AdvertisementBridgePolicyLabel() );
}

static void CL_AdvertisementBridge_UpdateCvars( void ) {
	cl_advertisementBridge.overlayCompiled = qfalse;
	cl_advertisementBridge.overlayAvailable = qfalse;

	CL_WebUI_SetCvarIfChanged( "ui_advertisementBridgeProvider", CL_AdvertisementBridgeProviderLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_advertisementBridgePolicy", CL_AdvertisementBridgePolicyLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_advertisementBridgeParityScope", "retail WebUI advertisement bridge" );
	CL_WebUI_SetCvarIfChanged( "ui_advertisementBridgeParityReason", "backend scaffold only; no live advertisement provider is bundled" );
}

static qhandle_t CL_AdvertisementBridge_RegisterDefaultAdvertCellShader( const char *defaultContent ) {
	if ( !defaultContent || !defaultContent[0] || !re.RegisterShaderNoMip ) {
		return 0;
	}

	return re.RegisterShaderNoMip( defaultContent );
}

qhandle_t CL_AdvertisementBridge_SetupAdvertCellShader( const char *defaultContent, const void *rect, int cellId ) {
	(void)rect;
	(void)cellId;
	CL_AdvertisementBridge_UpdateCvars();
	return CL_AdvertisementBridge_RegisterDefaultAdvertCellShader( defaultContent );
}

qhandle_t CL_AdvertisementBridge_RefreshAdvertCellShader( const char *defaultContent, const void *rect, int cellId ) {
	(void)rect;
	(void)cellId;
	CL_AdvertisementBridge_UpdateCvars();
	return CL_AdvertisementBridge_RegisterDefaultAdvertCellShader( defaultContent );
}

qhandle_t CL_AdvertisementBridge_SetupUIAdvertCellShader( const char *defaultContent, const void *rect, int cellId ) {
	(void)rect;
	(void)cellId;
	CL_AdvertisementBridge_UpdateCvars();
	return CL_AdvertisementBridge_RegisterDefaultAdvertCellShader( defaultContent );
}

qhandle_t CL_AdvertisementBridge_RefreshUIAdvertCellShader( const char *defaultContent, const void *rect, int cellId ) {
	(void)rect;
	(void)cellId;
	CL_AdvertisementBridge_UpdateCvars();
	return CL_AdvertisementBridge_RegisterDefaultAdvertCellShader( defaultContent );
}

void CL_AdvertisementBridge_InitCGame( void ) {
	cl_advertisementBridge.initialised = qtrue;
	cl_advertisementBridge.frameTime = 0;
	cl_advertisementBridge.viewWidth = cls.glconfig.vidWidth;
	cl_advertisementBridge.viewHeight = cls.glconfig.vidHeight;
	cl_advertisementBridge.activeAdvertCellId = 0;
	cl_advertisementBridge.activatedAdvertCellId = 0;
	cl_advertisementBridge.delayDeadline = 0;
	CL_AdvertisementBridge_UpdateCvars();
	CL_AdvertisementBridge_LogLifecycle( "init-cgame", 0 );
}

void CL_AdvertisementBridge_ShutdownCGame( void ) {
	cl_advertisementBridge.initialised = qfalse;
	cl_advertisementBridge.frameTime = 0;
	cl_advertisementBridge.activeAdvertCellId = 0;
	cl_advertisementBridge.activatedAdvertCellId = 0;
	cl_advertisementBridge.delayDeadline = 0;
	CL_AdvertisementBridge_UpdateCvars();
	CL_AdvertisementBridge_LogLifecycle( "shutdown-cgame", 0 );
}

void CL_AdvertisementBridge_InitUI( void ) {
	CL_AdvertisementBridge_UpdateCvars();
	CL_AdvertisementBridge_LogLifecycle( "init-ui", 0 );
}

void CL_AdvertisementBridge_Reserved21C0( void ) {
	CL_AdvertisementBridge_UpdateCvars();
}

void CL_AdvertisementBridge_SetActiveAdvert( int cellId ) {
	if ( cellId < 0 ) {
		cellId = 0;
	}

	cl_advertisementBridge.activeAdvertCellId = cellId;
	if ( cellId == 0 ) {
		cl_advertisementBridge.activatedAdvertCellId = 0;
	}

	CL_AdvertisementBridge_UpdateCvars();
	CL_AdvertisementBridge_LogLifecycle( "set-active", cellId );
}

void CL_AdvertisementBridge_ActivateAdvert( int cellId ) {
	if ( cellId < 0 ) {
		cellId = 0;
	}

	cl_advertisementBridge.activatedAdvertCellId = cellId;
	CL_AdvertisementBridge_UpdateCvars();
	CL_AdvertisementBridge_LogLifecycle( "activate", cellId );
}

void CL_AdvertisementBridge_UpdateAdvert( int handleOrToken, int area ) {
	(void)handleOrToken;
	(void)area;
	CL_AdvertisementBridge_UpdateCvars();
}

void CL_AdvertisementBridge_SetMapPath( const char *mapPath ) {
	Q_strncpyz( cl_advertisementBridge.mapPath, mapPath ? mapPath : "", sizeof( cl_advertisementBridge.mapPath ) );
	CL_AdvertisementBridge_UpdateCvars();
}

void CL_AdvertisementBridge_UpdateViewParameters( void ) {
	if ( cls.glconfig.vidWidth > 0 ) {
		cl_advertisementBridge.viewWidth = cls.glconfig.vidWidth;
	}
	if ( cls.glconfig.vidHeight > 0 ) {
		cl_advertisementBridge.viewHeight = cls.glconfig.vidHeight;
	}
	if ( cl_advertisementBridge.frameTime <= 0 ) {
		cl_advertisementBridge.frameTime = cls.realtime;
	}

	CL_AdvertisementBridge_UpdateCvars();
}

void CL_AdvertisementBridge_RefreshLoadingViewParameters( void ) {
	if ( !cl_advertisementBridge.initialised ) {
		return;
	}

	CL_AdvertisementBridge_UpdateViewParameters();
}

void CL_AdvertisementBridge_UpdateLoadingViewParameters( void ) {
	if ( re.AdvertisementBridge_UpdateLoadingViewParameters ) {
		re.AdvertisementBridge_UpdateLoadingViewParameters();
		return;
	}

	CL_AdvertisementBridge_RefreshLoadingViewParameters();
}

void CL_AdvertisementBridge_SetFrameTime( int frameTime ) {
	cl_advertisementBridge.frameTime = frameTime;
	CL_AdvertisementBridge_UpdateCvars();
}

void CL_AdvertisementBridge_ClearDelay( void ) {
	cl_advertisementBridge.delayDeadline = 0;
}

qboolean CL_AdvertisementBridge_IsDelayElapsed( void ) {
	return ( cl_advertisementBridge.delayDeadline <= 0 || cls.realtime >= cl_advertisementBridge.delayDeadline ) ? qtrue : qfalse;
}

int CL_AdvertisementBridge_GetCellDisplayState( int cellId ) {
	if ( cellId <= 0 ) {
		return 0;
	}

	if ( cellId == cl_advertisementBridge.activatedAdvertCellId ) {
		return 2;
	}

	if ( cellId == cl_advertisementBridge.activeAdvertCellId ) {
		return 1;
	}

	return 0;
}

void CL_AdvertisementBridge_GetCellLabel( int cellId, char *buffer, int bufferSize ) {
	CL_AdvertisementBridge_ClearLabel( buffer, bufferSize );
	if ( !buffer || bufferSize <= 0 || cellId <= 0 ) {
		return;
	}

	if ( cellId == cl_advertisementBridge.activatedAdvertCellId ) {
		Com_sprintf( buffer, bufferSize, "cell %d activated", cellId );
		return;
	}

	if ( cellId == cl_advertisementBridge.activeAdvertCellId ) {
		Com_sprintf( buffer, bufferSize, "cell %d active", cellId );
		return;
	}

	Com_sprintf( buffer, bufferSize, "cell %d available", cellId );
}

int CL_AdvertisementBridge_GetLabelList1Count( void ) {
	return CL_ADVERTISEMENT_DEBUG_LABEL_COUNT;
}

void CL_AdvertisementBridge_GetLabelList1Entry( int index, char *buffer, int bufferSize ) {
	CL_AdvertisementBridge_ClearLabel( buffer, bufferSize );
	if ( !buffer || bufferSize <= 0 ) {
		return;
	}

	switch ( index ) {
		case 0:
			Com_sprintf( buffer, bufferSize, "bridge: %s [%s]",
				CL_AdvertisementBridgeProviderLabel(),
				CL_AdvertisementBridgePolicyLabel() );
			break;

		case 1:
			Com_sprintf( buffer, bufferSize, "overlay: compiled=%d available=%d browser=%d",
				cl_advertisementBridge.overlayCompiled ? 1 : 0,
				cl_advertisementBridge.overlayAvailable ? 1 : 0,
				cl_webui.browserActive ? 1 : 0 );
			break;
	}
}

int CL_AdvertisementBridge_GetLabelList2Count( void ) {
	return CL_ADVERTISEMENT_DEBUG_LABEL_COUNT;
}

void CL_AdvertisementBridge_GetLabelList2Entry( int index, char *buffer, int bufferSize ) {
	CL_AdvertisementBridge_ClearLabel( buffer, bufferSize );
	if ( !buffer || bufferSize <= 0 ) {
		return;
	}

	switch ( index ) {
		case 0:
			Com_sprintf( buffer, bufferSize, "state: %s frame=%d view=%dx%d",
				cl_advertisementBridge.initialised ? "active" : "idle",
				cl_advertisementBridge.frameTime,
				cl_advertisementBridge.viewWidth,
				cl_advertisementBridge.viewHeight );
			break;

		case 1:
			Com_sprintf( buffer, bufferSize, "active=%d activated=%d",
				cl_advertisementBridge.activeAdvertCellId,
				cl_advertisementBridge.activatedAdvertCellId );
			break;
	}
}

static void CL_WebUI_JsonEscape( const char *value, char *buffer, size_t bufferSize ) {
	const char *cursor;
	char *out;
	char *limit;

	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	buffer[0] = '\0';
	if ( !value ) {
		return;
	}

	out = buffer;
	limit = buffer + bufferSize - 1;
	for ( cursor = value; *cursor && out < limit; ++cursor ) {
		unsigned char ch = (unsigned char)*cursor;

		if ( ch == '\\' || ch == '"' ) {
			if ( out + 2 > limit ) {
				break;
			}
			*out++ = '\\';
			*out++ = (char)ch;
			continue;
		}

		if ( ch == '\n' || ch == '\r' || ch == '\t' ) {
			if ( out + 2 > limit ) {
				break;
			}
			*out++ = '\\';
			*out++ = ch == '\n' ? 'n' : ch == '\r' ? 'r' : 't';
			continue;
		}

		if ( ch < 0x20 ) {
			continue;
		}

		*out++ = (char)ch;
	}

	*out = '\0';
}

static void CL_WebUI_AppendJsonFragment( char *buffer, size_t bufferSize, const char *fmt, ... ) {
	va_list argptr;
	char fragment[CL_WEB_EVENT_PAYLOAD_LENGTH];

	if ( !buffer || bufferSize == 0 || !fmt ) {
		return;
	}

	va_start( argptr, fmt );
	Q_vsnprintf( fragment, sizeof( fragment ), fmt, argptr );
	va_end( argptr );
	fragment[sizeof( fragment ) - 1] = '\0';

	Q_strcat( buffer, (int)bufferSize, fragment );
}

static qboolean CL_WebUI_AppendJsonLiteralIfFits( char *buffer, size_t bufferSize, const char *fragment ) {
	size_t currentLength;
	size_t fragmentLength;

	if ( !buffer || bufferSize == 0 || !fragment ) {
		return qfalse;
	}

	currentLength = strlen( buffer );
	fragmentLength = strlen( fragment );
	if ( currentLength + fragmentLength + 1 >= bufferSize ) {
		return qfalse;
	}

	Q_strcat( buffer, (int)bufferSize, fragment );
	return qtrue;
}

static qboolean CL_WebUI_AppendJsonFormattedIfFits( char *buffer, size_t bufferSize, const char *fmt, ... ) {
	va_list argptr;
	char fragment[CL_WEB_CONFIG_ITEM_LENGTH];

	if ( !buffer || bufferSize == 0 || !fmt ) {
		return qfalse;
	}

	va_start( argptr, fmt );
	Q_vsnprintf( fragment, sizeof( fragment ), fmt, argptr );
	va_end( argptr );
	fragment[sizeof( fragment ) - 1] = '\0';

	return CL_WebUI_AppendJsonLiteralIfFits( buffer, bufferSize, fragment );
}

static qboolean CL_WebUI_RuntimeRequested( void ) {
	return ( cl_webuiEnable && cl_webuiEnable->integer != 0 ) ? qtrue : qfalse;
}

static qboolean CL_WebUI_RuntimeAvailable( void ) {
	if ( !fnql::webui::ClientBackendHost().IsAvailable() ) {
		return qfalse;
	}

	return qtrue;
}

static void CL_WebHost_NormalizeHash( const char *hash, char *buffer, size_t bufferSize ) {
	const char *cursor;

	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	cursor = hash ? hash : "";
	while ( *cursor == '#' || *cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' ) {
		cursor++;
	}

	Q_strncpyz( buffer, cursor, (int)bufferSize );
}

static void CL_WebHost_BuildCurrentURL( const char *hash, char *buffer, size_t bufferSize ) {
	char normalizedHash[MAX_STRING_CHARS];

	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	CL_WebHost_NormalizeHash( hash, normalizedHash, sizeof( normalizedHash ) );
	if ( normalizedHash[0] ) {
		Com_sprintf( buffer, (int)bufferSize, "%s#%s", CL_WEB_DEFAULT_URL, normalizedHash );
		return;
	}

	Q_strncpyz( buffer, CL_WEB_DEFAULT_URL, (int)bufferSize );
}

static void CL_WebUI_SetLastError( const char *message, int code ) {
	Q_strncpyz( cl_webui.lastError, message ? message : "", sizeof( cl_webui.lastError ) );
	cl_webui.lastErrorCode = code;
}

static qboolean CL_WebUI_RecordBackendResult( fnql::webui::BackendResult result ) {
	if ( result ) {
		CL_WebUI_SetLastError( "", 0 );
		return qtrue;
	}

	char diagnostic[MAX_STRING_CHARS];
	const size_t copyLength = result.detail.size() < sizeof( diagnostic ) - 1
		? result.detail.size()
		: sizeof( diagnostic ) - 1;
	if ( copyLength > 0 ) {
		Com_Memcpy( diagnostic, result.detail.data(), copyLength );
	}
	diagnostic[copyLength] = '\0';
	CL_WebUI_SetLastError(
		diagnostic[0] ? diagnostic : "WebUI backend operation failed.",
		static_cast<int>( result.code ) );
	return qfalse;
}

static qboolean CL_WebUI_EnsureBackendStarted( void ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	char runtimePath[MAX_OSPATH];
	char basePath[MAX_OSPATH];
	char playerName[MAX_CVAR_VALUE_STRING];
	cgameClientIdentity_t identity;
	unsigned int identityLow = 0u;
	unsigned int identityHigh = 0u;
	char *configJson;
	char *mapJson;
	char *factoryJson;
	qboolean started;

	if ( host.IsRunning() ) {
		return qtrue;
	}
	if ( !CL_WebUI_RuntimeRequested() || !host.IsAvailable() ) {
		return qfalse;
	}
	if ( cls.glconfig.vidWidth <= 0 || cls.glconfig.vidHeight <= 0 ) {
		CL_WebUI_SetLastError( "WebUI runtime cannot start before the renderer has valid dimensions.",
			static_cast<int>( fnql::webui::BackendError::InvalidState ) );
		return qfalse;
	}

	runtimePath[0] = '\0';
	basePath[0] = '\0';
	playerName[0] = '\0';
	Cvar_VariableStringBuffer( "fs_homepath", runtimePath, sizeof( runtimePath ) );
	Cvar_VariableStringBuffer( "fs_basepath", basePath, sizeof( basePath ) );
	Cvar_VariableStringBuffer( "name", playerName, sizeof( playerName ) );
	if ( CL_CopyClientIdentity( clc.clientNum, &identity ) ) {
		identityLow = identity.identityLow;
		identityHigh = identity.identityHigh;
	}

	configJson = static_cast<char *>( Z_Malloc( CL_WEB_CONFIG_JSON_LENGTH ) );
	mapJson = static_cast<char *>( Z_Malloc( CL_WEB_CATALOG_JSON_LENGTH ) );
	factoryJson = static_cast<char *>( Z_Malloc( CL_WEB_CATALOG_JSON_LENGTH ) );
	if ( !configJson || !mapJson || !factoryJson ) {
		if ( configJson ) {
			Z_Free( configJson );
		}
		if ( mapJson ) {
			Z_Free( mapJson );
		}
		if ( factoryJson ) {
			Z_Free( factoryJson );
		}
		CL_WebUI_SetLastError( "WebUI startup snapshot allocation failed.",
			static_cast<int>( fnql::webui::BackendError::ResourceUnavailable ) );
		return qfalse;
	}

	configJson[0] = '\0';
	mapJson[0] = '\0';
	factoryJson[0] = '\0';
	CL_WebHost_BuildConfigJson( configJson, CL_WEB_CONFIG_JSON_LENGTH );
	CL_WebHost_BuildMapListJson( mapJson, CL_WEB_CATALOG_JSON_LENGTH );
	CL_WebHost_BuildFactoryListJson( factoryJson, CL_WEB_CATALOG_JSON_LENGTH );
	started = CL_Awesomium_Startup(
		runtimePath,
		basePath,
		playerName,
		(unsigned int)atoi( STEAMPATH_APPID ),
		identityLow,
		identityHigh,
		cls.glconfig.vidWidth,
		cls.glconfig.vidHeight,
		configJson,
		mapJson,
		factoryJson );
	Z_Free( factoryJson );
	Z_Free( mapJson );
	Z_Free( configJson );

	if ( !started ) {
		return qfalse;
	}

	cl_webui.reportedUnavailable = qfalse;
	if ( cl_webZoom ) {
		CL_Awesomium_SetZoom( cl_webZoom->integer );
	}
	CL_WebUI_SetLastError( "", 0 );
	return qtrue;
}

static void CL_WebUI_ReportUnavailable( const char *owner ) {
	if ( !CL_WebUI_RuntimeRequested() ) {
		Com_DPrintf( "%s ignored: WebUI runtime is disabled by cl_webuiEnable.\n", owner );
		return;
	}

	if ( cl_webui.reportedUnavailable ) {
		Com_DPrintf( "%s ignored: Awesomium/WebUI runtime backend is unavailable.\n", owner );
		return;
	}

	cl_webui.reportedUnavailable = qtrue;
	Com_Printf( "WebUI: Awesomium host is scaffolded but no runtime backend is available; using native UI.\n" );
	Com_DPrintf( "%s ignored: Awesomium/WebUI runtime backend is unavailable.\n", owner );
}

static void CL_WebHost_ClearTooltip( void ) {
	if ( cl_webui.tooltip[0] ) {
		CL_WebHost_OnChangeTooltip( "" );
	}
}

static void CL_WebUI_ClearBrowserState( void ) {
	CL_WebHost_ClearTooltip();
	CL_WebHost_ClearCursorOverride();
	cl_webui.browserVisible = qfalse;
	cl_webui.browserActive = qfalse;
	cl_webui.startupBridgeInjected = qfalse;
	cl_webui.keyCaptureArmed = qfalse;
	cl_webui.cursorPositionValid = qfalse;
	cl_webui.nextBridgeRetryFrame = 0;
	cl_webui.nextNativeRequestPollFrame = 0;
	cl_webui.nextConfigSnapshotFrame = 0;
	cl_webui.configSnapshotSynced = qfalse;
	cl_webui.demoSnapshotSynced = qfalse;
	cl_webui.mapCatalogSynced = qfalse;
	cl_webui.factoryCatalogSynced = qfalse;
	cl_webui.pendingHash[0] = '\0';
	cl_webui.currentUrl[0] = '\0';
	Key_SetCatcher( Key_GetCatcher() & ~KEYCATCH_BROWSER );
	CL_WebUI_SetCvarIfChanged( "web_browserActive", "0" );
}

void CL_RefreshOnlineServicesBridgeState( void ) {
	const qboolean requested = CL_WebUI_RuntimeRequested();
	const qboolean available = ( requested && CL_WebUI_RuntimeAvailable() ) ? qtrue : qfalse;
	const qboolean pendingSurface = ( available && cl_webui.browserVisible && cl_webui.browserActive && !CL_WebHost_HasDrawableSurface() ) ? qtrue : qfalse;

	CL_WebUI_SetCvarIfChanged( "ui_browserAwesomium", available ? "1" : "0" );
	CL_WebUI_SetCvarIfChanged( "ui_browserAwesomiumPending", pendingSurface ? "1" : "0" );
	CL_WebUI_SetCvarIfChanged( "ui_browserAwesomiumProvider", available ? CL_WebUI_BackendProviderLabel() : "Unavailable" );
	CL_WebUI_SetCvarIfChanged( "ui_browserAwesomiumPolicy", CL_WebUI_BackendPolicyLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_browserAwesomiumParityScope", "retail Windows Awesomium/WebUI" );
	CL_WebUI_SetCvarIfChanged( "ui_browserAwesomiumParityReason", "backend scaffold only; external Awesomium SDK/runtime is not bundled" );
	CL_WebUI_SetCvarIfChanged( "ui_onlineServicesMode", CL_WebHost_OnlineServicesModeLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_onlineServicesPolicy", CL_WebHost_OnlineServicesPolicyLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_subscriptionBridgeMode", CL_WebHost_OnlineServicesModeLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_subscriptionBridgePolicy", CL_WebHost_OnlineServicesPolicyLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_subscriptionBridgeParityScope", CL_WebHost_OnlineServicesParityScopeLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_subscriptionBridgeParityReason", CL_WebHost_OnlineServicesParityReasonLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_matchmakingProvider", CL_WebHost_MatchmakingProviderLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_matchmakingPolicy", CL_WebHost_MatchmakingPolicyLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_workshopProvider", CL_WebHost_WorkshopProviderLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_workshopPolicy", CL_WebHost_WorkshopPolicyLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_resourceBridgeProvider", CL_WebHost_ResourceBridgeProviderLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_resourceBridgePolicy", CL_WebHost_ResourceBridgePolicyLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_resourceBridgeParityScope", CL_WebHost_ResourceBridgeParityScopeLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_resourceBridgeParityReason", CL_WebHost_ResourceBridgeParityReasonLabel() );
	CL_WebUI_SetCvarIfChanged( "ui_resourceBridgeSteamDataSourceSubset", "avatars and live image resources" );
	CL_WebUI_SetCvarIfChanged( "ui_resourceBridgeSteamDataSourceNativeGap", "SteamDataSource and avatar callbacks are unavailable" );
	CL_WebUI_SetCvarIfChanged( "ui_resourceBridgeSteamDataSourceFallbackOwner", "renderer pak shader registry" );
	CL_AdvertisementBridge_UpdateCvars();
	CL_WebHost_UpdateOverlayOwnership();
}

qboolean CL_IsSubscribedApp( int appId ) {
	Com_DPrintf( "UI subscription bridge ignored for app %d: %s (%s [%s])\n",
		appId,
		"subscription bridge provider unavailable",
		CL_WebHost_OnlineServicesModeLabel(),
		CL_WebHost_OnlineServicesPolicyLabel() );
	return qfalse;
}

qhandle_t CL_GetAvatarImageHandle( unsigned int identityLow, unsigned int identityHigh ) {
	(void)identityLow;
	(void)identityHigh;
	return 0;
}

qhandle_t CL_Steam_RegisterShader( const char *url ) {
	if ( !url || !url[0] || !re.RegisterShaderNoMip ) {
		return 0;
	}

	if ( !CL_WebHost_IsResourceURI( url ) ) {
		return re.RegisterShaderNoMip( url );
	}

	CL_RefreshOnlineServicesBridgeState();
	Com_DPrintf( "UI resource bridge request %s ignored: %s (%s [%s])\n",
		url,
		CL_WebHost_ResourceBridgeParityReasonLabel(),
		CL_WebHost_ResourceBridgeProviderLabel(),
		CL_WebHost_ResourceBridgePolicyLabel() );
	return 0;
}

static void CL_WebUI_RegisterCvars( void ) {
	cl_webuiEnable = Cvar_Get( "cl_webuiEnable", "0", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( cl_webuiEnable, "0", "1", CV_INTEGER );
	Cvar_SetDescription( cl_webuiEnable, "Enable the Quake Live WebUI host when a supported Awesomium backend is available." );

	cl_webZoom = Cvar_Get( "web_zoom", "100", CVAR_ARCHIVE );
	Cvar_CheckRange( cl_webZoom, "25", "400", CV_INTEGER );
	Cvar_SetDescription( cl_webZoom, "Quake Live WebUI zoom percentage used by the Awesomium WebView path." );
	cl_webConsole = Cvar_Get( "web_console", "0", CVAR_ARCHIVE );
	Cvar_CheckRange( cl_webConsole, "0", "1", CV_INTEGER );
	Cvar_SetDescription( cl_webConsole, "Print WebUI console messages when a live browser backend publishes them." );
	cl_webBrowserActive = Cvar_Get( "web_browserActive", "0", CVAR_ROM );
	Cvar_SetDescription( cl_webBrowserActive, "Read-only state for the active Quake Live WebUI browser overlay." );

	Cvar_SetDescription( Cvar_Get( "ui_browserAwesomium", "0", CVAR_ROM ), "Read-only state for Quake Live WebUI availability." );
	Cvar_SetDescription( Cvar_Get( "ui_browserAwesomiumPending", "0", CVAR_ROM ), "Read-only state for a WebUI browser waiting on a drawable surface." );
	Cvar_SetDescription( Cvar_Get( "ui_browserAwesomiumProvider", "Unavailable", CVAR_ROM ), "Read-only provider label for the Quake Live WebUI browser path." );
	Cvar_SetDescription( Cvar_Get( "ui_browserAwesomiumPolicy", "default-off", CVAR_ROM ), "Read-only policy label for the Quake Live WebUI browser path." );
	Cvar_SetDescription( Cvar_Get( "ui_browserAwesomiumParityScope", "retail Windows Awesomium/WebUI", CVAR_ROM ), "Read-only parity scope for the Quake Live WebUI browser path." );
	Cvar_SetDescription( Cvar_Get( "ui_browserAwesomiumParityReason", "backend scaffold only; external Awesomium SDK/runtime is not bundled", CVAR_ROM ), "Read-only parity note for the Quake Live WebUI browser path." );
	Cvar_SetDescription( Cvar_Get( "ui_onlineServicesMode", "Unavailable", CVAR_ROM ), "Read-only mode label for the Quake Live WebUI online-services lane." );
	Cvar_SetDescription( Cvar_Get( "ui_onlineServicesPolicy", "compatibility-unavailable", CVAR_ROM ), "Read-only policy label for the Quake Live WebUI online-services lane." );
	Cvar_SetDescription( Cvar_Get( "ui_subscriptionBridgeMode", "Unavailable", CVAR_ROM ), "Read-only mode label for the Quake Live UI app-subscription bridge." );
	Cvar_SetDescription( Cvar_Get( "ui_subscriptionBridgePolicy", "compatibility-unavailable", CVAR_ROM ), "Read-only policy label for the Quake Live UI app-subscription bridge." );
	Cvar_SetDescription( Cvar_Get( "ui_subscriptionBridgeParityScope", "retail Steam online-services bridge", CVAR_ROM ), "Read-only parity scope for the Quake Live UI app-subscription bridge." );
	Cvar_SetDescription( Cvar_Get( "ui_subscriptionBridgeParityReason", "backend scaffold only; Steamworks online services are not bundled", CVAR_ROM ), "Read-only parity note for the Quake Live UI app-subscription bridge." );
	Cvar_SetDescription( Cvar_Get( "ui_matchmakingProvider", "Unavailable", CVAR_ROM ), "Read-only provider label for the Quake Live WebUI matchmaking and lobby lane." );
	Cvar_SetDescription( Cvar_Get( "ui_matchmakingPolicy", "Steamworks bridge unavailable", CVAR_ROM ), "Read-only policy label for the Quake Live WebUI matchmaking and lobby lane." );
	Cvar_SetDescription( Cvar_Get( "ui_workshopProvider", "Unavailable", CVAR_ROM ), "Read-only provider label for the Quake Live WebUI workshop lane." );
	Cvar_SetDescription( Cvar_Get( "ui_workshopPolicy", "Steamworks bridge unavailable", CVAR_ROM ), "Read-only policy label for the Quake Live WebUI workshop lane." );
	Cvar_SetDescription( Cvar_Get( "ui_resourceBridgeProvider", "Unavailable", CVAR_ROM ), "Read-only provider label for Quake Live UI resource and avatar requests." );
	// These six cvars are shared with the web.pak owner. Keep one canonical
	// reset value; each backend updates the live read-only value as state changes.
	Cvar_SetDescription( Cvar_Get( "ui_resourceBridgePolicy", "webpak-unavailable", CVAR_ROM ), "Read-only policy label for Quake Live UI resource and avatar requests." );
	Cvar_SetDescription( Cvar_Get( "ui_resourceBridgeParityScope", "retail web.pak resource bridge", CVAR_ROM ), "Read-only parity scope for Quake Live UI resource requests." );
	Cvar_SetDescription( Cvar_Get( "ui_resourceBridgeParityReason", "external retail assets only", CVAR_ROM ), "Read-only parity note for Quake Live UI resource requests." );
	Cvar_SetDescription( Cvar_Get( "ui_resourceBridgeSteamDataSourceSubset", "avatar-only SteamDataSource", CVAR_ROM ), "Read-only supported SteamDataSource subset for Quake Live UI resources." );
	Cvar_SetDescription( Cvar_Get( "ui_resourceBridgeSteamDataSourceNativeGap", "missing non-avatar SteamDataSource owner", CVAR_ROM ), "Read-only native gap for Quake Live UI resource requests." );
	Cvar_SetDescription( Cvar_Get( "ui_resourceBridgeSteamDataSourceFallbackOwner", "QLResourceInterceptor launcher/web fallback", CVAR_ROM ), "Read-only fallback owner for non-URI UI shader requests." );
	Cvar_SetDescription( Cvar_Get( "ui_advertisementBridgeProvider", "Unavailable", CVAR_ROM ), "Read-only provider label for the Quake Live advertisement bridge." );
	Cvar_SetDescription( Cvar_Get( "ui_advertisementBridgePolicy", "default-off", CVAR_ROM ), "Read-only policy label for the Quake Live advertisement bridge." );
	Cvar_SetDescription( Cvar_Get( "ui_advertisementBridgeParityScope", "retail WebUI advertisement bridge", CVAR_ROM ), "Read-only parity scope for the Quake Live advertisement bridge." );
	Cvar_SetDescription( Cvar_Get( "ui_advertisementBridgeParityReason", "backend scaffold only; no live advertisement provider is bundled", CVAR_ROM ), "Read-only parity note for the Quake Live advertisement bridge." );
}

static qboolean CL_WebUI_ServiceAvailable( void ) {
	return ( CL_WebUI_RuntimeRequested() && CL_WebUI_RuntimeAvailable() ) ? qtrue : qfalse;
}

static qboolean CL_WebHost_SurfaceReadyForOverlay( void ) {
	if ( !CL_WebUI_ServiceAvailable() ) {
		return qfalse;
	}

	if ( !cl_webui.browserVisible || !cl_webui.browserActive ) {
		return qfalse;
	}

	return CL_WebHost_HasDrawableSurface();
}

static void CL_WebHost_UpdateOverlayOwnership( void ) {
	const qboolean ownsOverlay = CL_WebHost_SurfaceReadyForOverlay();

	if ( ownsOverlay ) {
		Key_SetCatcher( Key_GetCatcher() | KEYCATCH_BROWSER );
	} else {
		Key_SetCatcher( Key_GetCatcher() & ~KEYCATCH_BROWSER );
	}

	CL_WebUI_SetCvarIfChanged( "web_browserActive", ownsOverlay ? "1" : "0" );
}

static void CL_WebHost_MarkBrowserUnavailable( void ) {
	CL_WebUI_ClearBrowserState();
	CL_RefreshOnlineServicesBridgeState();
}

static qboolean CL_WebHost_SetLocationHash( const char *hash ) {
	char escapedHash[MAX_STRING_CHARS * 2];
	char script[MAX_STRING_CHARS * 3];

	CL_WebHost_NormalizeHash( hash, cl_webui.pendingHash, sizeof( cl_webui.pendingHash ) );
	CL_WebHost_BuildCurrentURL( cl_webui.pendingHash, cl_webui.currentUrl, sizeof( cl_webui.currentUrl ) );

	if ( !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return qfalse;
	}

	CL_WebUI_JsonEscape( cl_webui.pendingHash, escapedHash, sizeof( escapedHash ) );
	Com_sprintf(
		script,
		sizeof( script ),
		"(function(){var h=\"%s\";if(window.location.hash.replace(/^#/,\"\")!==h){window.location.hash=h;}if(window.main_hook_v2){window.main_hook_v2();}})();",
		escapedHash
	);
	return CL_Awesomium_ExecuteJavascript( script, "" );
}

static qboolean CL_WebHost_OpenRequestedURL( const char *requestedUrl, const char *owner ) {
	qboolean relativeUrl;

	if ( !requestedUrl || !requestedUrl[0] ) {
		return qfalse;
	}

	relativeUrl = strstr( requestedUrl, "://" ) ? qfalse : qtrue;

	if ( !relativeUrl ) {
		if ( !fnql::webui::IsTrustedNavigationUrl( requestedUrl ) ) {
			CL_WebUI_SetLastError( "WebUI navigation is restricted to asset://ql/",
				static_cast<int>( fnql::webui::BackendError::InvalidArgument ) );
			return qfalse;
		}
		Q_strncpyz( cl_webui.currentUrl, requestedUrl, sizeof( cl_webui.currentUrl ) );
		cl_webui.pendingHash[0] = '\0';
	} else {
		if ( CL_WebHost_HasLiveView() && CL_WebHost_HasBoundWindowObject() && CL_WebHost_SetLocationHash( requestedUrl ) ) {
			cl_webui.browserVisible = qtrue;
			cl_webui.browserActive = qtrue;
			CL_RefreshOnlineServicesBridgeState();
			return qtrue;
		}

		CL_WebHost_NormalizeHash( requestedUrl, cl_webui.pendingHash, sizeof( cl_webui.pendingHash ) );
		CL_WebHost_BuildCurrentURL( cl_webui.pendingHash, cl_webui.currentUrl, sizeof( cl_webui.currentUrl ) );
	}

	cl_webui.browserVisible = qtrue;
	cl_webui.browserActive = qtrue;
	cl_webui.startupBridgeInjected = qfalse;
	cl_webui.nextBridgeRetryFrame = 0;
	cl_webui.configSnapshotSynced = qfalse;
	cl_webui.demoSnapshotSynced = qfalse;
	cl_webui.mapCatalogSynced = qfalse;
	cl_webui.factoryCatalogSynced = qfalse;
	cl_webui.nextConfigSnapshotFrame = 0;

	if ( !CL_WebUI_ServiceAvailable() ) {
		CL_WebUI_ReportUnavailable( owner ? owner : "qz.OpenURL" );
		CL_RefreshOnlineServicesBridgeState();
		return qfalse;
	}
	if ( !CL_WebUI_EnsureBackendStarted() ) {
		Com_DPrintf( "%s could not start WebUI backend: %s\n",
			owner ? owner : "qz.OpenURL", CL_Awesomium_LastError() );
		CL_WebHost_MarkBrowserUnavailable();
		return qfalse;
	}

	if ( !CL_Awesomium_OpenURL( cl_webui.currentUrl ) ) {
		CL_WebHost_MarkBrowserUnavailable();
		return qfalse;
	}

	CL_RefreshOnlineServicesBridgeState();
	return qtrue;
}

static void CL_Web_ShowBrowser_f( void ) {
	const char *requestedUrl = Cmd_Argc() > 1 ? Cmd_ArgsFrom( 1 ) : "#";

	CL_WebHost_OpenRequestedURL( requestedUrl, "web_showBrowser" );
}

static void CL_Web_ChangeHash_f( void ) {
	const char *requestedUrl = Cmd_Argc() > 1 ? Cmd_ArgsFrom( 1 ) : "#";

	CL_WebHost_OpenRequestedURL( requestedUrl, "web_changeHash" );
}

static void CL_Web_BrowserActive_f( void ) {
	const qboolean active = ( Cmd_Argc() > 1 && atoi( Cmd_Argv( 1 ) ) != 0 ) ? qtrue : qfalse;

	if ( !active ) {
		CL_WebHost_HideBrowser();
		return;
	}

	if ( !cl_webui.currentUrl[0] ) {
		CL_WebHost_BuildCurrentURL( cl_webui.pendingHash, cl_webui.currentUrl, sizeof( cl_webui.currentUrl ) );
	}

	cl_webui.browserVisible = qtrue;
	cl_webui.browserActive = qtrue;
	cl_webui.startupBridgeInjected = qfalse;
	cl_webui.nextBridgeRetryFrame = 0;

	if ( !CL_WebUI_ServiceAvailable() ) {
		CL_WebUI_ReportUnavailable( "web_browserActive" );
		CL_RefreshOnlineServicesBridgeState();
		return;
	}
	if ( !CL_WebUI_EnsureBackendStarted() ) {
		CL_WebHost_MarkBrowserUnavailable();
		return;
	}

	if ( !CL_Awesomium_OpenURL( cl_webui.currentUrl ) ) {
		CL_WebHost_MarkBrowserUnavailable();
		return;
	}

	CL_RefreshOnlineServicesBridgeState();
}

static void CL_Web_HideBrowser_f( void ) {
	CL_WebHost_HideBrowser();
}

static void CL_Web_ShowError_f( void ) {
	const char *message = Cmd_Argc() > 1 ? Cmd_ArgsFrom( 1 ) : "";

	Cvar_Set( "com_errorMessage", message );
	CL_WebView_PublishGameError( message );
}

static void CL_Web_ClearCache_f( void ) {
	if ( !fnql::webui::ClientBackendHost().IsRunning() ) {
		Com_DPrintf( "web_clearCache ignored: Awesomium/WebUI runtime backend is unavailable.\n" );
		return;
	}
	CL_Awesomium_ClearCache();
	if ( CL_Awesomium_LastError()[0] ) {
		Com_DPrintf( "web_clearCache failed: %s\n", CL_Awesomium_LastError() );
	}
}

static void CL_Web_Reload_f( void ) {
	if ( !fnql::webui::ClientBackendHost().IsRunning() ) {
		Com_DPrintf( "web_reload ignored: Awesomium/WebUI runtime backend is unavailable.\n" );
		return;
	}
	CL_Awesomium_Reload( qtrue );
	if ( CL_Awesomium_LastError()[0] ) {
		Com_DPrintf( "web_reload failed: %s\n", CL_Awesomium_LastError() );
	}
}

static void CL_Web_StopRefresh_f( void ) {
	if ( !fnql::webui::ClientBackendHost().IsRunning() ) {
		Com_DPrintf( "web_stopRefresh ignored: Awesomium/WebUI runtime backend is unavailable.\n" );
		return;
	}
	CL_Awesomium_Stop();
	if ( CL_Awesomium_LastError()[0] ) {
		Com_DPrintf( "web_stopRefresh failed: %s\n", CL_Awesomium_LastError() );
	}
}

void QLWebHost_RegisterCommands( void ) {
	if ( cl_webui.commandsRegistered ) {
		return;
	}

	Cmd_AddCommand( "web_showBrowser", CL_Web_ShowBrowser_f );
	Cmd_AddCommand( "web_changeHash", CL_Web_ChangeHash_f );
	Cmd_AddCommand( "web_browserActive", CL_Web_BrowserActive_f );
	Cmd_AddCommand( "web_hideBrowser", CL_Web_HideBrowser_f );
	Cmd_AddCommand( "web_showError", CL_Web_ShowError_f );
	Cmd_AddCommand( "web_clearCache", CL_Web_ClearCache_f );
	Cmd_AddCommand( "web_reload", CL_Web_Reload_f );
	Cmd_AddCommand( "web_stopRefresh", CL_Web_StopRefresh_f );
	Cmd_AddCommand( "clientviewprofile", CL_Steam_OverlayCommand_f );
	Cmd_AddCommand( "clientfriendinvite", CL_Steam_OverlayCommand_f );
	cl_webui.commandsRegistered = qtrue;
}

void QLWebHost_UnregisterCommands( void ) {
	if ( !cl_webui.commandsRegistered ) {
		return;
	}

	Cmd_RemoveCommand( "web_showBrowser" );
	Cmd_RemoveCommand( "web_changeHash" );
	Cmd_RemoveCommand( "web_browserActive" );
	Cmd_RemoveCommand( "web_hideBrowser" );
	Cmd_RemoveCommand( "web_showError" );
	Cmd_RemoveCommand( "web_clearCache" );
	Cmd_RemoveCommand( "web_reload" );
	Cmd_RemoveCommand( "web_stopRefresh" );
	Cmd_RemoveCommand( "clientviewprofile" );
	Cmd_RemoveCommand( "clientfriendinvite" );
	cl_webui.commandsRegistered = qfalse;
}

void CL_WebHost_Init( void ) {
	const qboolean commandsRegistered = cl_webui.commandsRegistered;

	Com_Memset( &cl_webui, 0, sizeof( cl_webui ) );
	cl_webui.initialized = qtrue;
	cl_webui.commandsRegistered = commandsRegistered;
	cl_webui.appActive = qtrue;
	CL_WebUI_RegisterCvars();
	CL_RefreshOnlineServicesBridgeState();
}

void CL_WebHost_Shutdown( void ) {
	if ( !cl_webui.initialized ) {
		return;
	}

	CL_Awesomium_Shutdown();
	CL_WebUI_ClearBrowserState();
	CL_RefreshOnlineServicesBridgeState();
}

void CL_WebHost_Frame( void ) {
	if ( !cl_webui.initialized ) {
		return;
	}

	cl_webui.frameSequence++;

	if ( !CL_WebUI_RuntimeRequested() ) {
		CL_Awesomium_Shutdown();
		CL_WebUI_ClearBrowserState();
	}

	if ( CL_WebUI_ServiceAvailable() ) {
		if ( CL_WebHost_HasLiveView() ) {
			const fnql::webui::BackendStatus backendStatus =
				fnql::webui::ClientBackendHost().Status();
			if ( cls.glconfig.vidWidth > 0 && cls.glconfig.vidHeight > 0
				&& backendStatus.surface != fnql::webui::SurfaceSize{
					cls.glconfig.vidWidth, cls.glconfig.vidHeight } ) {
				CL_Awesomium_Resize( cls.glconfig.vidWidth, cls.glconfig.vidHeight );
			}
			if ( cl_webZoom && cl_webZoom->modified
				&& CL_Awesomium_SetZoom( cl_webZoom->integer ) ) {
				cl_webZoom->modified = qfalse;
			}
			CL_Awesomium_Update();
			if ( !CL_WebHost_HasLiveView() ) {
				CL_WebHost_MarkBrowserUnavailable();
			} else {
				CL_WebHost_EnsureStartupBridge();
				CL_WebHost_UpdateBrowserNativeState();
				CL_WebHost_SyncNativeSnapshots( qfalse );
				CL_WebHost_PumpNativeJavascriptRequests();
			}
		}
	}

	CL_WebHost_ServerBrowserFrame();
	CL_WebHost_ServerDetailsFrame();
	CL_RefreshOnlineServicesBridgeState();
}

void CL_WebHost_BootstrapAwesomiumMenu( void ) {
	if ( !cl_webui.initialized || !CL_WebUI_RuntimeRequested() ) {
		return;
	}

	if ( !CL_WebUI_ServiceAvailable() ) {
		CL_WebUI_ReportUnavailable( "CL_WebHost_BootstrapAwesomiumMenu" );
		CL_RefreshOnlineServicesBridgeState();
		return;
	}

	CL_WebHost_OpenRequestedURL( "#", "CL_WebHost_BootstrapAwesomiumMenu" );
}

qboolean CL_WebHost_HasLiveView( void ) {
	const fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	return ( host.IsRunning() && host.Status().viewAlive ) ? qtrue : qfalse;
}

qboolean CL_WebHost_HasBoundWindowObject( void ) {
	const fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	return ( host.IsRunning() && host.Status().windowObjectBound ) ? qtrue : qfalse;
}

qboolean CL_WebHost_HasDrawableSurface( void ) {
	const fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( !host.IsRunning() || !host.Status().HasSurface() ) {
		return qfalse;
	}

	// The backend surface contract is live, but the renderer-neutral upload
	// path still needs its own compatibility slice.  Do not claim overlay
	// ownership until pixels can actually be presented; native UI remains the
	// safe fallback even when an adapter has started successfully.
	return qfalse;
}

static qboolean CL_WebHost_CanDispatchLiveEvent( void ) {
	return ( CL_WebHost_HasLiveView() && CL_WebHost_HasBoundWindowObject() && CL_WebHost_HasDrawableSurface() ) ? qtrue : qfalse;
}

void CL_WebHost_DrawBrowserSurface( void ) {
}

#if defined( _WIN32 )
static HCURSOR CL_WebHost_LoadWin32CursorHandle( int cursorType ) {
	LPCSTR cursorId;

	switch ( cursorType ) {
		case 1:
			cursorId = IDC_CROSS;
			break;
		case 2:
			cursorId = IDC_HAND;
			break;
		case 3:
			cursorId = IDC_IBEAM;
			break;
		case 4:
			cursorId = IDC_WAIT;
			break;
		case 5:
			cursorId = IDC_HELP;
			break;
		case 7:
			cursorId = IDC_SIZEWE;
			break;
		case 8:
			cursorId = IDC_SIZENS;
			break;
		case 9:
			cursorId = IDC_SIZENESW;
			break;
		case 10:
			cursorId = IDC_SIZENWSE;
			break;
		case 11:
			cursorId = IDC_SIZEALL;
			break;
		case 14:
			cursorId = IDC_NO;
			break;
		default:
			cursorId = IDC_ARROW;
			break;
	}

	return LoadCursorA( NULL, cursorId );
}
#endif

static void CL_WebHost_ClearCursorOverride( void ) {
#if defined( _WIN32 )
	if ( cl_webui.cursorOverrideActive && cl_webui.restoreCursorHandle ) {
		SetCursor( cl_webui.restoreCursorHandle );
	}

	cl_webui.activeCursorHandle = NULL;
	cl_webui.restoreCursorHandle = NULL;
	cl_webui.activeCursorType = 0;
	cl_webui.cursorOverrideActive = qfalse;
#endif
}

void *CL_WebHost_GetCursorHandle( void ) {
#if defined( _WIN32 )
	if ( !CL_WebHost_SurfaceReadyForOverlay() ) {
		return NULL;
	}

	if ( !cl_webui.cursorOverrideActive || !cl_webui.activeCursorHandle ) {
		return NULL;
	}

	return cl_webui.activeCursorHandle;
#else
	return NULL;
#endif
}

void *CL_WebHost_OnChangeCursor( int cursorType ) {
#if defined( _WIN32 )
	HCURSOR cursorHandle;

	if ( !cl_webui.cursorOverrideActive ) {
		cl_webui.restoreCursorHandle = GetCursor();
	}

	cursorHandle = CL_WebHost_LoadWin32CursorHandle( cursorType );
	if ( !cursorHandle ) {
		cursorHandle = LoadCursorA( NULL, IDC_ARROW );
	}

	cl_webui.activeCursorType = cursorType;
	cl_webui.activeCursorHandle = cursorHandle;
	cl_webui.cursorOverrideActive = cursorHandle ? qtrue : qfalse;
	if ( cl_webui.cursorOverrideActive && CL_WebHost_SurfaceReadyForOverlay() ) {
		SetCursor( cl_webui.activeCursorHandle );
	}

	return cl_webui.activeCursorHandle;
#else
	(void)cursorType;
	return NULL;
#endif
}

void CL_WebHost_OnChangeTooltip( const char *tooltip ) {
	char escapedTooltip[MAX_QPATH * 2];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];

	Q_strncpyz( cl_webui.tooltip, tooltip ? tooltip : "", sizeof( cl_webui.tooltip ) );
	CL_WebUI_JsonEscape( cl_webui.tooltip, escapedTooltip, sizeof( escapedTooltip ) );
	Com_sprintf( payload, sizeof( payload ), "{\"tooltip\":\"%s\"}", escapedTooltip );
	CL_WebView_PublishEvent( "web.tooltip", payload );
}

void CL_WebHost_SetCursorPosition( int x, int y ) {
	cl_webui.cursorX = x;
	cl_webui.cursorY = y;
	cl_webui.cursorPositionValid = qtrue;
	CL_WebHost_UpdateBrowserCursorPosition( x, y );
}

qboolean CL_WebHost_GetCursorPosition( int *x, int *y ) {
	if ( x ) {
		*x = 0;
	}
	if ( y ) {
		*y = 0;
	}

	if ( !cl_webui.cursorPositionValid ) {
		return qfalse;
	}

	if ( x ) {
		*x = cl_webui.cursorX;
	}
	if ( y ) {
		*y = cl_webui.cursorY;
	}

	return qtrue;
}

qboolean CL_WebHost_RequestCursorPosition( int *x, int *y ) {
	if ( x ) {
		*x = 0;
	}
	if ( y ) {
		*y = 0;
	}

	if ( cl_webui.cursorPositionValid && ( cl_webui.browserVisible || cl_webui.browserActive ) ) {
		return CL_WebHost_GetCursorPosition( x, y );
	}

#if defined( _WIN32 )
	POINT point;

	if ( !GetCursorPos( &point ) ) {
		return qfalse;
	}

	if ( g_wv.hWnd ) {
		ScreenToClient( g_wv.hWnd, &point );
	}

	if ( x ) {
		*x = point.x;
	}
	if ( y ) {
		*y = point.y;
	}

	return qtrue;
#else
	return qfalse;
#endif
}

static void CL_WebHost_UpdateBrowserCursorPosition( int x, int y ) {
	char script[256];

	if ( !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}

	Com_sprintf(
		script,
		sizeof( script ),
		"(function(){if(window.__qlr_set_cursor_position){window.__qlr_set_cursor_position(%d,%d);}})();",
		x,
		y
	);
	CL_Awesomium_ExecuteJavascript( script, "" );
}

void CL_WebHost_HideBrowser( void ) {
	if ( cl_webui.keyCaptureArmed ) {
		return;
	}

	if ( CL_WebUI_ServiceAvailable() ) {
		CL_Awesomium_PauseRendering();
		CL_Awesomium_Unfocus();
	}
	CL_WebUI_ClearBrowserState();
	CL_RefreshOnlineServicesBridgeState();
}

static void CL_WebHost_BuildStartupBridgeScript( char *buffer, size_t bufferSize ) {
	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	Q_strncpyz(
		buffer,
		"(function(){"
		"if(window.__qlr_qz_instance_ready){if(window.main_hook_v2){window.main_hook_v2();}return;}"
		"window.__qlr_qz_instance_ready=true;"
		"var noop=function(){return true;};var empty=function(){return [];};"
		"var maps={campgrounds:{id:'campgrounds',name:'Campgrounds',sysname:'campgrounds',gametypes:{0:true}}};"
		"var pendingNativeMaps={};"
		"var factories={ffa:{id:'ffa',title:'Free For All',basegt:0,settings:{}}};"
		"var pendingNativeFactories={};"
		"var config={appId:" STEAMPATH_APPID ",steamId:'0',playerName:'',playerAvatar:'',playerAvatarUrl:'',playerProfileUrl:'',playerProfile:{id:'0',name:'',avatar:'',avatarUrl:'',profileUrl:''},onlineServicesMode:'Unavailable',onlineServicesPolicy:'compatibility-unavailable',matchmakingProvider:'Unavailable',matchmakingPolicy:'Steamworks bridge unavailable',workshopProvider:'Unavailable',workshopPolicy:'Steamworks bridge unavailable',cvars:{},binds:[]};"
		"var nativeQueue=window.__qlr_native_requests=window.__qlr_native_requests||[];"
		"var fileExistsCache={};var cursorPosition={x:0,y:0};var clipboardText='';var clipboardPrimed=false;var mapPrimed=false;var factoryPrimed=false;var demoList=[];var demoPrimed=false;var friendList=[];var friendPrimed=false;var ugcList=[];var ugcPrimed=false;var nativeState={pakPresent:false,gameRunning:false};"
		"var canon=function(n){return String(n||'').toLowerCase();};"
		"var queue=function(kind,payload){try{nativeQueue.push(String(kind||'')+'\\n'+String(payload||''));return true;}catch(e){return false;}};"
		"var queueSocial=function(kind,payload){return queue('social.'+String(kind||''),String(payload||''));};"
		"var hasOwn=function(o,k){return Object.prototype.hasOwnProperty.call(o,k);};"
		"var objectHasEntries=function(o){for(var k in o){if(hasOwn(o,k)){return true;}}return false;};"
		"var catalogObject=function(v){var out={};if(!v){return out;}if(typeof v.length==='undefined'){return v;}for(var i=0;i<v.length;i++){var item=v[i]||{};var id=String(item.sysname||item.id||item.title||i);if(!id){continue;}if(!item.id){item.id=id;}if(!item.sysname){item.sysname=id;}out[id]=item;}return out;};"
		"var addCatalogObjects=function(target,v){var o=catalogObject(v);for(var k in o){if(hasOwn(o,k)){target[k]=o[k];}}return true;};"
		"var setNativeCvar=function(n,v){var k=canon(n);if(k){config.cvars[k]=String(v||'');}return true;};"
		"var setFileExists=function(p,v){fileExistsCache[String(p||'')]=!!v;return true;};"
		"var setCursorPosition=function(x,y){cursorPosition={x:x|0,y:y|0};return true;};"
		"var setClipboardText=function(v){clipboardText=String(v||'');clipboardPrimed=true;return true;};"
		"var setDemoList=function(v){demoList=v||[];demoPrimed=true;return true;};"
		"var setFriendList=function(v){friendList=v||[];friendPrimed=true;config.friends=friendList;return true;};"
		"var setUGCList=function(v){ugcList=v||[];ugcPrimed=true;config.ugc=ugcList;return true;};"
		"var setMapList=function(v){var o=catalogObject(v);if(!objectHasEntries(o)){return false;}maps=o;mapPrimed=true;config.maps=maps;return true;};"
		"var setFactoryList=function(v){var o=catalogObject(v);if(!objectHasEntries(o)){return false;}factories=o;factoryPrimed=true;config.factories=factories;return true;};"
		"var beginNativeMaps=function(){pendingNativeMaps={};return true;};var addNativeMaps=function(v){return addCatalogObjects(pendingNativeMaps,v);};var commitNativeMaps=function(){return setMapList(pendingNativeMaps);};"
		"var beginNativeFactories=function(){pendingNativeFactories={};return true;};var addNativeFactories=function(v){return addCatalogObjects(pendingNativeFactories,v);};var commitNativeFactories=function(){return setFactoryList(pendingNativeFactories);};"
		"var setNativeState=function(s){if(s){nativeState.pakPresent=!!s.pakPresent;nativeState.gameRunning=!!s.gameRunning;}return true;};"
		"var setNativeConfig=function(c){if(!c){return false;}if(typeof c.appId!=='undefined'){config.appId=c.appId;qz.appId=c.appId;}if(typeof c.steamId!=='undefined'){config.steamId=String(c.steamId||'');qz.steamId=config.steamId;}if(typeof c.playerName!=='undefined'){config.playerName=String(c.playerName||'');qz.playerName=config.playerName;}if(typeof c.playerAvatar!=='undefined'){config.playerAvatar=String(c.playerAvatar||'');qz.playerAvatar=config.playerAvatar;}if(typeof c.playerAvatarUrl!=='undefined'){config.playerAvatarUrl=String(c.playerAvatarUrl||'');qz.playerAvatarUrl=config.playerAvatarUrl;}if(typeof c.playerProfileUrl!=='undefined'){config.playerProfileUrl=String(c.playerProfileUrl||'');qz.playerProfileUrl=config.playerProfileUrl;}if(c.playerProfile){config.playerProfile=c.playerProfile;qz.playerProfile=c.playerProfile;}if(typeof c.onlineServicesMode!=='undefined'){config.onlineServicesMode=String(c.onlineServicesMode||'');}if(typeof c.onlineServicesPolicy!=='undefined'){config.onlineServicesPolicy=String(c.onlineServicesPolicy||'');}if(typeof c.matchmakingProvider!=='undefined'){config.matchmakingProvider=String(c.matchmakingProvider||'');}if(typeof c.matchmakingPolicy!=='undefined'){config.matchmakingPolicy=String(c.matchmakingPolicy||'');}if(typeof c.workshopProvider!=='undefined'){config.workshopProvider=String(c.workshopProvider||'');}if(typeof c.workshopPolicy!=='undefined'){config.workshopPolicy=String(c.workshopPolicy||'');}if(typeof c.version!=='undefined'){config.version=String(c.version||'');}if(typeof c.browserVisible!=='undefined'){config.browserVisible=!!c.browserVisible;}if(typeof c.browserActive!=='undefined'){config.browserActive=!!c.browserActive;}if(typeof c.url!=='undefined'){config.url=String(c.url||'');}if(c.cvars){for(var k in c.cvars){if(hasOwn(c.cvars,k)){setNativeCvar(k,c.cvars[k]);}}}if(c.binds&&typeof c.binds.length!=='undefined'){config.binds=c.binds;}return true;};"
		"var qz={appId:" STEAMPATH_APPID ",steamId:'0',playerName:'',playerAvatar:'',playerAvatarUrl:'',playerProfileUrl:'',playerProfile:config.playerProfile,"
		"IsPakFilePresent:function(path){path=String(path||'');if(!path){return !!nativeState.pakPresent;}if(!Object.prototype.hasOwnProperty.call(fileExistsCache,path)){queue('exists',path);}return !!fileExistsCache[path];},IsGameRunning:function(){return !!nativeState.gameRunning;},"
		"SendGameCommand:function(cmd){cmd=String(cmd||'');var m=/^\\s*web_changeHash(?:\\s+(.*))?\\s*$/.exec(cmd);if(m){window.location.hash=(m[1]||'').replace(/^#/,'');}return queue('cmd',cmd);},"
		"WriteTextFile:function(path,contents){return queue('write',String(path||'')+'\\n'+String(contents||''));},"
		"GetCvar:function(name){var k=canon(name);if(k&&typeof config.cvars[k]==='undefined'){queue('get',k);}return config.cvars[k]||'';},"
		"SetCvar:function(name,value){var k=canon(name);if(!k){return false;}value=String(value||'');config.cvars[k]=value;return queue('set',k+'\\n'+value);},"
		"ResetCvar:function(name){var k=canon(name);if(!k){return false;}delete config.cvars[k];return queue('reset',k);},"
		"GetMapList:function(){if(!mapPrimed){mapPrimed=true;queue('maps','');}return maps;},GetFactoryList:function(){if(!factoryPrimed){factoryPrimed=true;queue('factories','');}return factories;},GetDemoList:function(){if(!demoPrimed){demoPrimed=true;queue('demos','');}return demoList;},"
		"OpenURL:function(url){url=String(url||'');return url?queue('openurl',url):false;},OpenSteamOverlayURL:function(url){url=String(url||'');return url?queue('opensteamurl',url):false;},"
		"GetClipboardText:function(){if(!clipboardPrimed){queue('clipget','');}return clipboardText;},SetClipboardText:function(text){setClipboardText(text);return queue('clipset',clipboardText);},"
		"RequestServers:function(source){return queue('servers',String(typeof source==='undefined'?2:source));},RequestServerDetails:function(ip,port){return queue('serverdetails',String(ip||'')+'\\n'+String(port||''));},RefreshList:function(){return queue('refreshservers','');},"
		"CreateLobby:function(){return queueSocial('createlobby','');},LeaveLobby:function(){return queueSocial('leavelobby','');},JoinLobby:function(lobby){return queueSocial('joinlobby',String(lobby||''));},SetLobbyServer:function(ip,port){return queueSocial('setlobbyserver',String(ip||'')+'\\n'+String(port||''));},"
		"ShowInviteOverlay:function(){return queueSocial('showinviteoverlay','');},SayLobby:function(message){return queueSocial('saylobby',String(message||''));},"
		"RequestUserStats:function(steamId){return queueSocial('requestuserstats',String(steamId||''));},GetFriendList:function(){if(!friendPrimed){friendPrimed=true;queue('friends','');}return friendList;},"
		"ActivateGameOverlayToUser:function(dialog,steamId){return queueSocial('activategameoverlaytouser',String(dialog||'')+'\\n'+String(steamId||''));},Invite:function(steamId){return queueSocial('invite',String(steamId||''));},"
		"FileExists:function(path){path=String(path||'');if(!Object.prototype.hasOwnProperty.call(fileExistsCache,path)){queue('exists',path);}return !!fileExistsCache[path];},"
		"GetConfig:function(){return config;},GetCursorPosition:function(){return {x:cursorPosition.x,y:cursorPosition.y};},"
		"GetAllUGC:function(filter){ugcPrimed=true;queueSocial('getallugc',String(typeof filter==='undefined'?0:filter));return ugcList;},GetNextKeyDown:function(active){if(typeof active==='undefined'){return queue('keycapture','1');}var activeText=String(active).toLowerCase();var activeValue=(active===false||activeText==='false')?0:parseInt(active,10);return queue('keycapture',((isNaN(activeValue)?1:activeValue)!==0?'1':'0'));},"
		"SetFavoriteServer:function(ip,port,add){var addText=String(add).toLowerCase();var addValue=(add===false||addText==='false')?0:parseInt(add,10);return queue('favorite',String(ip||'')+'\\n'+String(port||'')+'\\n'+((isNaN(addValue)?1:addValue)!==0?'1':'0'));},NoOp:noop};"
		"if(!window.FakeClient){window.FakeClient={};}if(!window.FakeClient.qz_instance){window.FakeClient.qz_instance={};}"
		"window.__qlr_set_native_cvar=setNativeCvar;window.__qlr_set_file_exists=setFileExists;window.__qlr_set_cursor_position=setCursorPosition;window.__qlr_set_clipboard_text=setClipboardText;window.__qlr_set_demo_list=setDemoList;window.__qlr_set_friend_list=setFriendList;window.__qlr_set_ugc_list=setUGCList;window.__qlr_set_native_maps=setMapList;window.__qlr_set_native_factories=setFactoryList;window.__qlr_begin_native_maps=beginNativeMaps;window.__qlr_add_native_maps=addNativeMaps;window.__qlr_commit_native_maps=commitNativeMaps;window.__qlr_begin_native_factories=beginNativeFactories;window.__qlr_add_native_factories=addNativeFactories;window.__qlr_commit_native_factories=commitNativeFactories;window.__qlr_set_native_state=setNativeState;window.__qlr_set_native_config=setNativeConfig;"
		"window.main_hook_v2=function(){var f=window.FakeClient&&window.FakeClient.qz_instance;if(f){for(var k in qz){f[k]=qz[k];}}"
		"window.qz_instance=qz;if(typeof window.EnginePublish==='function'&&!window.EnginePublish.__qlr_wrapped){var oldPublish=window.EnginePublish;"
		"var wrapped=function(topic,data){try{if(String(topic).indexOf('cvar.')===0){var d=typeof data==='string'?JSON.parse(data):data;if(d){setNativeCvar(d.name||String(topic).substr(5),d.value||'');}}}catch(e){}return oldPublish.apply(this,arguments);};"
		"wrapped.__qlr_wrapped=true;window.EnginePublish=wrapped;}};"
		"window.main_hook_v2();if(document.addEventListener){document.addEventListener('DOMContentLoaded',window.main_hook_v2,false);}window.__qlr_browser_helpers_ready=true;"
		"try{var e=document.createEvent('Event');e.initEvent('qz_instance.ready',false,false);document.dispatchEvent(e);}catch(err){}"
		"var qlrBridgeTries=0;var qlrBridgeTimer=setInterval(function(){window.main_hook_v2();if(++qlrBridgeTries>=40){clearInterval(qlrBridgeTimer);}},250);"
		"window.__qlr_qz_instance_script_complete=true;"
		"})();",
		(int)bufferSize
	);
}

static void CL_WebHost_BuildStartupBridgeRetryScript( char *buffer, size_t bufferSize ) {
	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	Q_strncpyz( buffer, "(function(){if(window.main_hook_v2){window.main_hook_v2();}})();", (int)bufferSize );
}

static qboolean CL_WebHost_InjectStartupBridge( qboolean retryOnly ) {
	char script[MAX_STRING_CHARS];

	if ( !CL_WebHost_HasLiveView() ) {
		return qfalse;
	}

	if ( retryOnly ) {
		CL_WebHost_BuildStartupBridgeRetryScript( script, sizeof( script ) );
	} else {
		CL_WebHost_BuildStartupBridgeScript( script, sizeof( script ) );
	}

	if ( !CL_Awesomium_ExecuteJavascript( script, "" ) ) {
		return qfalse;
	}

	if ( !retryOnly ) {
		cl_webui.startupBridgeInjected = qtrue;
		CL_WebView_ReplayRetainedEvents();
		CL_WebView_PublishEvent( "web.object.ready", NULL );
		CL_WebHost_UpdateBrowserNativeState();
		CL_WebHost_SyncNativeSnapshots( qtrue );
	}
	cl_webui.nextBridgeRetryFrame = cl_webui.frameSequence + CL_WEB_BRIDGE_RETRY_FRAMES;
	return qtrue;
}

static void CL_WebHost_EnsureStartupBridge( void ) {
	if ( !CL_WebHost_HasLiveView() ) {
		return;
	}

	if ( !cl_webui.startupBridgeInjected ) {
		CL_WebHost_InjectStartupBridge( qfalse );
		return;
	}

	if ( cl_webui.frameSequence >= cl_webui.nextBridgeRetryFrame ) {
		CL_WebHost_InjectStartupBridge( qtrue );
	}
}

static void CL_WebHost_UpdateBrowserCvarCache( const char *name, const char *value ) {
	char escapedName[MAX_CVAR_VALUE_STRING * 2];
	char escapedValue[MAX_CVAR_VALUE_STRING * 2];
	char script[MAX_STRING_CHARS];

	if ( !name || !name[0] || ( Cvar_Flags( name ) & CVAR_PRIVATE ) ||
		!CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}

	CL_WebUI_JsonEscape( name, escapedName, sizeof( escapedName ) );
	CL_WebUI_JsonEscape( value ? value : "", escapedValue, sizeof( escapedValue ) );
	Com_sprintf(
		script,
		sizeof( script ),
		"(function(){if(window.__qlr_set_native_cvar){window.__qlr_set_native_cvar(\"%s\",\"%s\");}})();",
		escapedName,
		escapedValue
	);
	CL_Awesomium_ExecuteJavascript( script, "" );
}

static qboolean CL_WebHost_PathIsSafeRelative( const char *path ) {
	if ( !path || !path[0] ) {
		return qfalse;
	}

	if ( strstr( path, ".." ) || strstr( path, "::" ) || strchr( path, ':' ) ) {
		return qfalse;
	}

	if ( path[0] == '/' || path[0] == '\\' ) {
		return qfalse;
	}

	return qtrue;
}

static void CL_WebHost_UpdateBrowserFileExistsCache( const char *path, qboolean exists ) {
	char escapedPath[MAX_QPATH * 2];
	char script[MAX_STRING_CHARS];

	if ( !path || !path[0] || !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}

	CL_WebUI_JsonEscape( path, escapedPath, sizeof( escapedPath ) );
	Com_sprintf(
		script,
		sizeof( script ),
		"(function(){if(window.__qlr_set_file_exists){window.__qlr_set_file_exists(\"%s\",%s);}})();",
		escapedPath,
		exists ? "true" : "false"
	);
	CL_Awesomium_ExecuteJavascript( script, "" );
}

static void CL_WebHost_UpdateBrowserClipboardCache( const char *text ) {
	char escapedText[MAX_STRING_CHARS * 2];
	char script[CL_WEB_EVENT_PAYLOAD_LENGTH];

	if ( !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}

	CL_WebUI_JsonEscape( text ? text : "", escapedText, sizeof( escapedText ) );
	Com_sprintf(
		script,
		sizeof( script ),
		"(function(){if(window.__qlr_set_clipboard_text){window.__qlr_set_clipboard_text(\"%s\");}})();",
		escapedText
	);
	CL_Awesomium_ExecuteJavascript( script, "" );
}

static void CL_WebHost_UpdateBrowserDemoList( const char *demoListJson ) {
	char script[CL_WEB_EVENT_PAYLOAD_LENGTH];

	if ( !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}

	Com_sprintf(
		script,
		sizeof( script ),
		"(function(){if(window.__qlr_set_demo_list){window.__qlr_set_demo_list(%s);}})();",
		( demoListJson && demoListJson[0] ) ? demoListJson : "[]"
	);
	CL_Awesomium_ExecuteJavascript( script, "" );
}

static void CL_WebHost_UpdateBrowserFriendList( const char *friendListJson ) {
	char *script;
	int scriptSize;

	if ( !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}

	scriptSize = (int)strlen( ( friendListJson && friendListJson[0] ) ? friendListJson : "[]" ) + 128;
	script = (char *)Z_Malloc( scriptSize );
	if ( !script ) {
		return;
	}

	Com_sprintf(
		script,
		scriptSize,
		"(function(){if(window.__qlr_set_friend_list){window.__qlr_set_friend_list(%s);}})();",
		( friendListJson && friendListJson[0] ) ? friendListJson : "[]"
	);
	CL_Awesomium_ExecuteJavascript( script, "" );
	Z_Free( script );
}

static void CL_WebHost_UpdateBrowserUGCList( const char *ugcListJson ) {
	char script[CL_WEB_EVENT_PAYLOAD_LENGTH];

	if ( !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}

	Com_sprintf(
		script,
		sizeof( script ),
		"(function(){if(window.__qlr_set_ugc_list){window.__qlr_set_ugc_list(%s);}})();",
		( ugcListJson && ugcListJson[0] ) ? ugcListJson : "[]"
	);
	CL_Awesomium_ExecuteJavascript( script, "" );
}

static void CL_WebHost_UpdateBrowserCatalogCache( const char *setterName, const char *catalogJson ) {
	const char *json;
	char *script;
	int scriptSize;

	if ( !setterName || !setterName[0] || !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}

	json = ( catalogJson && catalogJson[0] ) ? catalogJson : "[]";
	scriptSize = (int)strlen( setterName ) * 2 + (int)strlen( json ) + 128;
	script = (char *)Z_Malloc( scriptSize );
	if ( !script ) {
		return;
	}

	Com_sprintf(
		script,
		scriptSize,
		"(function(){if(window.%s){window.%s(%s);}})();",
		setterName,
		setterName,
		json
	);
	CL_Awesomium_ExecuteJavascript( script, "" );
	Z_Free( script );
}

static qboolean CL_WebHost_ExecuteCatalogBatch( const char *addName, const char *entries, size_t entryLength ) {
	char *script;
	int scriptSize;
	int result;
	qboolean executed;

	if ( !entries || entryLength == 0 ) {
		return qtrue;
	}
	if ( !addName || !addName[0] || !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return qfalse;
	}

	scriptSize = (int)strlen( addName ) * 2 + (int)entryLength + 160;
	script = (char *)Z_Malloc( scriptSize );
	if ( !script ) {
		return qfalse;
	}

	Com_sprintf(
		script,
		scriptSize,
		"(function(){return(window.%s&&window.%s([%.*s]))?1:0;})()",
		addName,
		addName,
		(int)entryLength,
		entries
	);
	executed = ( CL_Awesomium_ExecuteJavascriptInteger( script, "", &result ) && result != 0 ) ? qtrue : qfalse;
	Z_Free( script );
	return executed;
}

static qboolean CL_WebHost_QueueCatalogEntry( const char *addName, const char **batchStart, const char **batchEnd, size_t *batchLength, const char *entryStart, size_t entryLength ) {
	if ( !batchStart || !batchEnd || !batchLength || !entryStart || entryLength == 0 ) {
		return qtrue;
	}

	if ( *batchStart && *batchLength + 1 + entryLength > CL_WEB_CATALOG_SYNC_CHUNK_CHARS ) {
		if ( !CL_WebHost_ExecuteCatalogBatch( addName, *batchStart, *batchLength ) ) {
			return qfalse;
		}
		*batchStart = NULL;
		*batchEnd = NULL;
		*batchLength = 0;
	}

	if ( !*batchStart ) {
		*batchStart = entryStart;
	}

	*batchEnd = entryStart + entryLength;
	*batchLength = (size_t)( *batchEnd - *batchStart );
	return qtrue;
}

static qboolean CL_WebHost_UpdateBrowserCatalogCacheBatched( const char *beginName, const char *addName, const char *commitName, const char *catalogJson ) {
	const char *json;
	const char *arrayEnd;
	const char *cursor;
	const char *entryStart;
	const char *batchStart;
	const char *batchEnd;
	size_t jsonLength;
	size_t batchLength;
	int depth;
	int result;
	qboolean inString;
	qboolean escaped;

	if ( !beginName || !beginName[0] || !addName || !addName[0] || !commitName || !commitName[0] ||
		!CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return qfalse;
	}

	json = ( catalogJson && catalogJson[0] ) ? catalogJson : "[]";
	jsonLength = strlen( json );
	if ( jsonLength < 2 || json[0] != '[' || json[jsonLength - 1] != ']' ) {
		return qfalse;
	}
	if ( !CL_Awesomium_ExecuteJavascriptInteger( va( "(function(){return(window.%s&&window.%s())?1:0;})()", beginName, beginName ), "", &result ) || result == 0 ) {
		return qfalse;
	}

	arrayEnd = json + jsonLength - 1;
	entryStart = json + 1;
	batchStart = NULL;
	batchEnd = NULL;
	batchLength = 0;
	depth = 0;
	inString = qfalse;
	escaped = qfalse;

	for ( cursor = json + 1; cursor < arrayEnd; ++cursor ) {
		char ch = *cursor;

		if ( inString ) {
			if ( escaped ) {
				escaped = qfalse;
			} else if ( ch == '\\' ) {
				escaped = qtrue;
			} else if ( ch == '"' ) {
				inString = qfalse;
			}
			continue;
		}

		if ( ch == '"' ) {
			inString = qtrue;
			continue;
		}
		if ( ch == '{' || ch == '[' ) {
			++depth;
			continue;
		}
		if ( ch == '}' || ch == ']' ) {
			if ( depth > 0 ) {
				--depth;
			}
			continue;
		}
		if ( ch == ',' && depth == 0 ) {
			if ( !CL_WebHost_QueueCatalogEntry( addName, &batchStart, &batchEnd, &batchLength, entryStart, (size_t)( cursor - entryStart ) ) ) {
				return qfalse;
			}
			entryStart = cursor + 1;
		}
	}

	if ( !CL_WebHost_QueueCatalogEntry( addName, &batchStart, &batchEnd, &batchLength, entryStart, (size_t)( arrayEnd - entryStart ) ) ) {
		return qfalse;
	}
	if ( !CL_WebHost_ExecuteCatalogBatch( addName, batchStart, batchLength ) ) {
		return qfalse;
	}

	return ( CL_Awesomium_ExecuteJavascriptInteger( va( "(function(){return(window.%s&&window.%s())?1:0;})()", commitName, commitName ), "", &result ) && result != 0 ) ? qtrue : qfalse;
}

static void CL_WebHost_UpdateBrowserMapList( const char *mapListJson ) {
	if ( CL_WebHost_UpdateBrowserCatalogCacheBatched( "__qlr_begin_native_maps", "__qlr_add_native_maps", "__qlr_commit_native_maps", mapListJson ) ) {
		return;
	}

	CL_WebHost_UpdateBrowserCatalogCache( "__qlr_set_native_maps", mapListJson );
}

static void CL_WebHost_UpdateBrowserFactoryList( const char *factoryListJson ) {
	if ( CL_WebHost_UpdateBrowserCatalogCacheBatched( "__qlr_begin_native_factories", "__qlr_add_native_factories", "__qlr_commit_native_factories", factoryListJson ) ) {
		return;
	}

	CL_WebHost_UpdateBrowserCatalogCache( "__qlr_set_native_factories", factoryListJson );
}

static qboolean CL_WebHost_IsGameRunning( void ) {
	return ( cls.state >= CA_CONNECTED && cls.state < CA_CINEMATIC ) ? qtrue : qfalse;
}

static void CL_WebHost_UpdateBrowserNativeState( void ) {
	char script[256];

	if ( !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}

	Com_sprintf(
		script,
		sizeof( script ),
		"(function(){if(window.__qlr_set_native_state){window.__qlr_set_native_state({pakPresent:%s,gameRunning:%s});}})();",
		CL_WebPak_Available() ? "true" : "false",
		CL_WebHost_IsGameRunning() ? "true" : "false"
	);
	CL_Awesomium_ExecuteJavascript( script, "" );
}

static qboolean CL_WebHost_AppendConfigCvar( char *buffer, size_t bufferSize, const char *name, qboolean *first ) {
	char value[MAX_CVAR_VALUE_STRING];
	char escapedName[MAX_CVAR_VALUE_STRING * 2];
	char escapedValue[MAX_CVAR_VALUE_STRING * 2];

	if ( !buffer || bufferSize == 0 || !name || !name[0] || !first ) {
		return qfalse;
	}

	if ( Cvar_Flags( name ) == CVAR_NONEXISTENT ) {
		return qtrue;
	}
	if ( Cvar_Flags( name ) & CVAR_PRIVATE ) {
		return qtrue;
	}

	Cvar_VariableStringBuffer( name, value, sizeof( value ) );
	CL_WebUI_JsonEscape( name, escapedName, sizeof( escapedName ) );
	CL_WebUI_JsonEscape( value, escapedValue, sizeof( escapedValue ) );
	if ( !CL_WebUI_AppendJsonFormattedIfFits( buffer, bufferSize, "%s\"%s\":\"%s\"", *first ? "" : ",", escapedName, escapedValue ) ) {
		return qfalse;
	}

	*first = qfalse;
	return qtrue;
}

static void CL_WebHost_BuildConfigCvarJson( char *buffer, size_t bufferSize ) {
	static const char *const configCvars[] = {
		"name",
		"version",
		"com_protocol",
		"fs_basegame",
		"fs_game",
		"com_errorMessage",
		"cl_webuiEnable",
		"web_zoom",
		"web_console",
		"web_browserActive",
		"ui_browserAwesomium",
		"ui_browserAwesomiumPending",
		"ui_browserAwesomiumProvider",
		"ui_browserAwesomiumPolicy",
		"ui_browserAwesomiumParityScope",
		"ui_browserAwesomiumParityReason",
		"ui_onlineServicesMode",
		"ui_onlineServicesPolicy",
		"ui_subscriptionBridgeMode",
		"ui_subscriptionBridgePolicy",
		"ui_subscriptionBridgeParityScope",
		"ui_subscriptionBridgeParityReason",
		"ui_matchmakingProvider",
		"ui_matchmakingPolicy",
		"ui_workshopProvider",
		"ui_workshopPolicy",
		"ui_resourceBridgeProvider",
		"ui_resourceBridgePolicy",
		"ui_resourceBridgeParityScope",
		"ui_resourceBridgeParityReason",
		"ui_resourceBridgeSteamDataSourceSubset",
		"ui_resourceBridgeSteamDataSourceNativeGap",
		"ui_resourceBridgeSteamDataSourceFallbackOwner",
		"r_mode",
		"r_modeFullscreen",
		"r_fullscreen",
		NULL
	};
	qboolean first;

	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	buffer[0] = '\0';
	first = qtrue;
	for ( int i = 0; configCvars[i]; ++i ) {
		if ( !CL_WebHost_AppendConfigCvar( buffer, bufferSize, configCvars[i], &first ) ) {
			break;
		}
	}
}

static void CL_WebHost_BuildConfigBindJson( char *buffer, size_t bufferSize ) {
	qboolean first;

	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	buffer[0] = '\0';
	first = qtrue;
	for ( int i = 0; i < MAX_KEYS; ++i ) {
		const char *binding;
		const char *keyName;
		char escapedName[MAX_TOKEN_CHARS * 2];
		char escapedBinding[CL_WEB_CONFIG_ITEM_LENGTH];

		binding = Key_GetBinding( i );
		if ( !binding || !binding[0] ) {
			continue;
		}

		keyName = Key_KeynumToString( i );
		CL_WebUI_JsonEscape( keyName ? keyName : "", escapedName, sizeof( escapedName ) );
		CL_WebUI_JsonEscape( binding, escapedBinding, sizeof( escapedBinding ) );
		if ( !CL_WebUI_AppendJsonFormattedIfFits(
			buffer,
			bufferSize,
			"%s{\"id\":%d,\"key\":\"%s\",\"value\":\"%s\"}",
			first ? "" : ",",
			i,
			escapedName,
			escapedBinding ) ) {
			break;
		}

		first = qfalse;
	}
}

static qboolean CL_WebHost_AppendCatalogObjectPrefix( char *buffer, size_t bufferSize, qboolean *first ) {
	if ( !buffer || bufferSize == 0 || !first ) {
		return qfalse;
	}

	if ( !*first && !CL_WebUI_AppendJsonLiteralIfFits( buffer, bufferSize, "," ) ) {
		return qfalse;
	}

	*first = qfalse;
	return qtrue;
}

static void CL_WebHost_BuildMapListJson( char *buffer, size_t bufferSize ) {
	char fileList[CL_WEB_CATALOG_FILE_LIST_LENGTH];
	char *cursor;
	int fileCount;
	qboolean first;

	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	buffer[0] = '\0';
	Q_strcat( buffer, (int)bufferSize, "[" );
	first = qtrue;
	fileCount = FS_GetFileList( "maps", ".bsp", fileList, sizeof( fileList ) );
	cursor = fileList;
	for ( int i = 0; i < fileCount && cursor[0]; ++i ) {
		char mapName[MAX_QPATH];
		char escapedMapName[MAX_QPATH * 2];
		char *extension;

		Q_strncpyz( mapName, cursor, sizeof( mapName ) );
		extension = strrchr( mapName, '.' );
		if ( extension ) {
			*extension = '\0';
		}
		if ( !mapName[0] ) {
			cursor += strlen( cursor ) + 1;
			continue;
		}

		CL_WebUI_JsonEscape( mapName, escapedMapName, sizeof( escapedMapName ) );
		if ( !CL_WebHost_AppendCatalogObjectPrefix( buffer, bufferSize, &first ) ||
			!CL_WebUI_AppendJsonFormattedIfFits(
				buffer,
				bufferSize,
				"{\"id\":\"%s\",\"sysname\":\"%s\",\"name\":\"%s\",\"gametypes\":[0,0,0,0,0,0,0,0,0,0,0,0,0]}",
				escapedMapName,
				escapedMapName,
				escapedMapName ) ) {
			break;
		}

		cursor += strlen( cursor ) + 1;
	}
	Q_strcat( buffer, (int)bufferSize, "]" );
}

static qboolean CL_WebHost_CatalogIdSeen( char seenIds[][MAX_QPATH], int seenCount, const char *id ) {
	if ( !seenIds || !id || !id[0] ) {
		return qfalse;
	}

	for ( int i = 0; i < seenCount; ++i ) {
		if ( !Q_stricmp( seenIds[i], id ) ) {
			return qtrue;
		}
	}

	return qfalse;
}

static const char *CL_WebHost_FindRangeText( const char *start, const char *end, const char *needle ) {
	size_t needleLength;

	if ( !start || !end || !needle || !needle[0] || start >= end ) {
		return NULL;
	}

	needleLength = strlen( needle );
	for ( const char *cursor = start; cursor + needleLength <= end; ++cursor ) {
		if ( !Q_strncmp( cursor, needle, (int)needleLength ) ) {
			return cursor;
		}
	}

	return NULL;
}

static qboolean CL_WebHost_CopyFactoryJsonString( const char *objectStart, const char *objectEnd, const char *key, char *buffer, size_t bufferSize ) {
	char keyPattern[MAX_QPATH];
	const char *cursor;
	char *out;
	char *limit;
	qboolean escaped;

	if ( !objectStart || !objectEnd || objectStart >= objectEnd || !key || !buffer || bufferSize == 0 ) {
		return qfalse;
	}

	buffer[0] = '\0';
	Com_sprintf( keyPattern, sizeof( keyPattern ), "\"%s\"", key );
	cursor = CL_WebHost_FindRangeText( objectStart, objectEnd, keyPattern );
	if ( !cursor ) {
		return qfalse;
	}

	cursor += strlen( keyPattern );
	while ( cursor < objectEnd && ( *cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' ) ) {
		cursor++;
	}
	if ( cursor >= objectEnd || *cursor != ':' ) {
		return qfalse;
	}
	cursor++;
	while ( cursor < objectEnd && ( *cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n' ) ) {
		cursor++;
	}
	if ( cursor >= objectEnd || *cursor != '"' ) {
		return qfalse;
	}
	cursor++;

	out = buffer;
	limit = buffer + bufferSize - 1;
	escaped = qfalse;
	while ( cursor < objectEnd && out < limit ) {
		char ch = *cursor++;

		if ( escaped ) {
			*out++ = ch;
			escaped = qfalse;
			continue;
		}
		if ( ch == '\\' ) {
			escaped = qtrue;
			continue;
		}
		if ( ch == '"' ) {
			break;
		}
		if ( (unsigned char)ch >= 0x20 ) {
			*out++ = ch;
		}
	}
	*out = '\0';
	return buffer[0] ? qtrue : qfalse;
}

static const char *CL_WebHost_FindFactoryObjectEnd( const char *objectStart, const char *dataEnd ) {
	const char *cursor;
	int depth;
	qboolean inString;
	qboolean escaped;

	if ( !objectStart || !dataEnd || objectStart >= dataEnd || *objectStart != '{' ) {
		return NULL;
	}

	depth = 0;
	inString = qfalse;
	escaped = qfalse;
	for ( cursor = objectStart; cursor < dataEnd; ++cursor ) {
		char ch = *cursor;

		if ( inString ) {
			if ( escaped ) {
				escaped = qfalse;
			} else if ( ch == '\\' ) {
				escaped = qtrue;
			} else if ( ch == '"' ) {
				inString = qfalse;
			}
			continue;
		}

		if ( ch == '"' ) {
			inString = qtrue;
		} else if ( ch == '{' ) {
			depth++;
		} else if ( ch == '}' ) {
			depth--;
			if ( depth == 0 ) {
				return cursor + 1;
			}
		}
	}

	return NULL;
}

static qboolean CL_WebHost_AppendFactoryDefinition( char *buffer, size_t bufferSize, qboolean *first, char seenIds[][MAX_QPATH], int *seenCount, const char *id, const char *title, const char *basegt ) {
	char escapedId[MAX_QPATH * 2];
	char escapedTitle[MAX_QPATH * 2];
	char escapedBasegt[MAX_QPATH * 2];

	if ( !buffer || bufferSize == 0 || !first || !seenIds || !seenCount || !id || !id[0] ) {
		return qfalse;
	}
	if ( *seenCount >= CL_WEB_MAX_CATALOG_ITEMS || CL_WebHost_CatalogIdSeen( seenIds, *seenCount, id ) ) {
		return qtrue;
	}

	CL_WebUI_JsonEscape( id, escapedId, sizeof( escapedId ) );
	CL_WebUI_JsonEscape( ( title && title[0] ) ? title : id, escapedTitle, sizeof( escapedTitle ) );
	CL_WebUI_JsonEscape( ( basegt && basegt[0] ) ? basegt : "ffa", escapedBasegt, sizeof( escapedBasegt ) );
	if ( !CL_WebHost_AppendCatalogObjectPrefix( buffer, bufferSize, first ) ||
		!CL_WebUI_AppendJsonFormattedIfFits(
			buffer,
			bufferSize,
			"{\"id\":\"%s\",\"sysname\":\"%s\",\"title\":\"%s\",\"basegt\":\"%s\",\"settings\":{}}",
			escapedId,
			escapedId,
			escapedTitle,
			escapedBasegt ) ) {
		return qfalse;
	}

	Q_strncpyz( seenIds[*seenCount], id, MAX_QPATH );
	( *seenCount )++;
	return qtrue;
}

static void CL_WebHost_AppendFactoryDefinitionsFromBuffer( const char *filename, const char *data, int dataLength, char *buffer, size_t bufferSize, qboolean *first, char seenIds[][MAX_QPATH], int *seenCount ) {
	const char *cursor;
	const char *dataEnd;
	int startCount;

	if ( !data || dataLength <= 0 || !buffer || !first || !seenIds || !seenCount ) {
		return;
	}

	startCount = *seenCount;
	cursor = data;
	dataEnd = data + dataLength;
	while ( cursor < dataEnd && *seenCount < CL_WEB_MAX_CATALOG_ITEMS ) {
		const char *objectStart;
		const char *objectEnd;
		char id[MAX_QPATH];
		char title[MAX_QPATH];
		char basegt[MAX_QPATH];

		objectStart = CL_WebHost_FindRangeText( cursor, dataEnd, "{" );
		if ( !objectStart ) {
			break;
		}
		objectEnd = CL_WebHost_FindFactoryObjectEnd( objectStart, dataEnd );
		if ( !objectEnd ) {
			break;
		}

		if ( CL_WebHost_CopyFactoryJsonString( objectStart, objectEnd, "id", id, sizeof( id ) ) ||
			CL_WebHost_CopyFactoryJsonString( objectStart, objectEnd, "sysname", id, sizeof( id ) ) ) {
			CL_WebHost_CopyFactoryJsonString( objectStart, objectEnd, "title", title, sizeof( title ) );
			CL_WebHost_CopyFactoryJsonString( objectStart, objectEnd, "basegt", basegt, sizeof( basegt ) );
			if ( !CL_WebHost_AppendFactoryDefinition( buffer, bufferSize, first, seenIds, seenCount, id, title, basegt ) ) {
				return;
			}
		}

		cursor = objectEnd;
	}

	if ( *seenCount == startCount && filename && filename[0] ) {
		char fallbackId[MAX_QPATH];
		const char *slash;
		char *extension;

		slash = strrchr( filename, '/' );
		Q_strncpyz( fallbackId, slash ? slash + 1 : filename, sizeof( fallbackId ) );
		extension = strrchr( fallbackId, '.' );
		if ( extension ) {
			*extension = '\0';
		}
		CL_WebHost_AppendFactoryDefinition( buffer, bufferSize, first, seenIds, seenCount, fallbackId, fallbackId, "ffa" );
	}
}

static void CL_WebHost_AppendFactoryDefinitionsFromFile( const char *filename, char *buffer, size_t bufferSize, qboolean *first, char seenIds[][MAX_QPATH], int *seenCount ) {
	void *fileBuffer;
	int length;

	if ( !filename || !filename[0] ) {
		return;
	}

	fileBuffer = NULL;
	length = FS_ReadFile( filename, &fileBuffer );
	if ( length <= 0 || !fileBuffer ) {
		return;
	}

	CL_WebHost_AppendFactoryDefinitionsFromBuffer( filename, (const char *)fileBuffer, length, buffer, bufferSize, first, seenIds, seenCount );
	FS_FreeFile( fileBuffer );
}

static void CL_WebHost_BuildFactoryListJson( char *buffer, size_t bufferSize ) {
	char fileList[CL_WEB_CATALOG_FILE_LIST_LENGTH];
	char seenIds[CL_WEB_MAX_CATALOG_ITEMS][MAX_QPATH];
	char *cursor;
	int fileCount;
	int seenCount;
	qboolean first;

	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	Com_Memset( seenIds, 0, sizeof( seenIds ) );
	buffer[0] = '\0';
	Q_strcat( buffer, (int)bufferSize, "[" );
	first = qtrue;
	seenCount = 0;

	CL_WebHost_AppendFactoryDefinitionsFromFile( "scripts/factories.txt", buffer, bufferSize, &first, seenIds, &seenCount );

	fileCount = FS_GetFileList( "scripts", ".factories", fileList, sizeof( fileList ) );
	cursor = fileList;
	for ( int i = 0; i < fileCount && cursor[0] && seenCount < CL_WEB_MAX_CATALOG_ITEMS; ++i ) {
		char path[MAX_QPATH];

		Com_sprintf( path, sizeof( path ), "scripts/%s", cursor );
		CL_WebHost_AppendFactoryDefinitionsFromFile( path, buffer, bufferSize, &first, seenIds, &seenCount );
		cursor += strlen( cursor ) + 1;
	}

	fileCount = FS_GetFileList( "scripts", ".factory", fileList, sizeof( fileList ) );
	cursor = fileList;
	for ( int i = 0; i < fileCount && cursor[0] && seenCount < CL_WEB_MAX_CATALOG_ITEMS; ++i ) {
		char path[MAX_QPATH];

		Com_sprintf( path, sizeof( path ), "scripts/%s", cursor );
		CL_WebHost_AppendFactoryDefinitionsFromFile( path, buffer, bufferSize, &first, seenIds, &seenCount );
		cursor += strlen( cursor ) + 1;
	}

	Q_strcat( buffer, (int)bufferSize, "]" );
}

static unsigned long long CL_WebHost_SteamIdFromWords( unsigned int identityLow, unsigned int identityHigh ) {
	return ( (unsigned long long)identityHigh << 32 ) | identityLow;
}

static void CL_WebHost_BuildFriendListJson( char *buffer, size_t bufferSize ) {
	qboolean first;

	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	buffer[0] = '\0';
	Q_strcat( buffer, bufferSize, "[" );
	first = qtrue;
	for ( int i = 0; i < MAX_CLIENTS; ++i ) {
		cgameClientIdentity_t identity;
		unsigned long long steamId;
		char escapedDisplayName[CG_CLIENT_IDENTITY_NAME_CHARS * 2];
		char escapedCleanName[CG_CLIENT_IDENTITY_NAME_CHARS * 2];

		if ( !CL_CopyClientIdentity( i, &identity ) ) {
			continue;
		}

		steamId = CL_WebHost_SteamIdFromWords( identity.identityLow, identity.identityHigh );
		CL_WebUI_JsonEscape( identity.displayName, escapedDisplayName, sizeof( escapedDisplayName ) );
		CL_WebUI_JsonEscape( identity.cleanName, escapedCleanName, sizeof( escapedCleanName ) );
		if ( !CL_WebUI_AppendJsonFormattedIfFits(
			buffer,
			bufferSize,
			"%s{\"clientNum\":%d,\"steamId\":\"%llu\",\"identityLow\":%u,\"identityHigh\":%u,\"name\":\"%s\",\"cleanName\":\"%s\",\"muted\":%s}",
			first ? "" : ",",
			identity.clientNum,
			steamId,
			identity.identityLow,
			identity.identityHigh,
			escapedDisplayName,
			escapedCleanName,
			CL_IsSteamIdentityMuted( identity.identityLow, identity.identityHigh ) ? "true" : "false" ) ) {
			break;
		}

		first = qfalse;
	}
	Q_strcat( buffer, bufferSize, "]" );
}

static void CL_WebHost_FormatSteamId( unsigned int identityLow, unsigned int identityHigh, char *buffer, size_t bufferSize ) {
	unsigned long long steamId;

	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	steamId = CL_WebHost_SteamIdFromWords( identityLow, identityHigh );
	Com_sprintf( buffer, bufferSize, "%llu", steamId );
}

static void CL_WebHost_FormatSteamAvatarUrl( const char *steamId, char *buffer, size_t bufferSize ) {
	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	if ( !steamId || !steamId[0] || !Q_stricmp( steamId, "0" ) ) {
		buffer[0] = '\0';
		return;
	}

	Com_sprintf( buffer, bufferSize, "steam://avatar/large/%s", steamId );
}

static void CL_WebHost_FormatSteamProfileUrl( const char *steamId, char *buffer, size_t bufferSize ) {
	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	if ( !steamId || !steamId[0] || !Q_stricmp( steamId, "0" ) ) {
		buffer[0] = '\0';
		return;
	}

	Com_sprintf( buffer, bufferSize, "https://steamcommunity.com/profiles/%s", steamId );
}

static void CL_WebHost_BuildConfigJson( char *buffer, size_t bufferSize ) {
	char cvarJson[CL_WEB_CONFIG_JSON_LENGTH / 2];
	char bindJson[CL_WEB_CONFIG_JSON_LENGTH / 2];
	cgameClientIdentity_t identity;
	unsigned int identityLow;
	unsigned int identityHigh;
	char steamId[32];
	char playerName[MAX_CVAR_VALUE_STRING];
	char version[MAX_CVAR_VALUE_STRING];
	char playerAvatarUrl[96];
	char playerProfileUrl[128];
	char escapedSteamId[64];
	char escapedPlayerName[MAX_CVAR_VALUE_STRING * 2];
	char escapedVersion[MAX_CVAR_VALUE_STRING * 2];
	char escapedPlayerAvatarUrl[192];
	char escapedPlayerProfileUrl[256];
	char escapedOnlineServicesMode[128];
	char escapedOnlineServicesPolicy[128];
	char escapedMatchmakingProvider[128];
	char escapedMatchmakingPolicy[128];
	char escapedWorkshopProvider[128];
	char escapedWorkshopPolicy[128];
	char escapedUrl[CL_WEB_CONFIG_ITEM_LENGTH];

	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	identityLow = 0u;
	identityHigh = 0u;
	if ( CL_CopyClientIdentity( clc.clientNum, &identity ) ) {
		identityLow = identity.identityLow;
		identityHigh = identity.identityHigh;
	}

	CL_WebHost_FormatSteamId( identityLow, identityHigh, steamId, sizeof( steamId ) );
	CL_WebHost_FormatSteamAvatarUrl( steamId, playerAvatarUrl, sizeof( playerAvatarUrl ) );
	CL_WebHost_FormatSteamProfileUrl( steamId, playerProfileUrl, sizeof( playerProfileUrl ) );
	Cvar_VariableStringBuffer( "name", playerName, sizeof( playerName ) );
	Cvar_VariableStringBuffer( "version", version, sizeof( version ) );
	CL_WebUI_JsonEscape( steamId, escapedSteamId, sizeof( escapedSteamId ) );
	CL_WebUI_JsonEscape( playerName, escapedPlayerName, sizeof( escapedPlayerName ) );
	CL_WebUI_JsonEscape( version, escapedVersion, sizeof( escapedVersion ) );
	CL_WebUI_JsonEscape( playerAvatarUrl, escapedPlayerAvatarUrl, sizeof( escapedPlayerAvatarUrl ) );
	CL_WebUI_JsonEscape( playerProfileUrl, escapedPlayerProfileUrl, sizeof( escapedPlayerProfileUrl ) );
	CL_WebUI_JsonEscape( CL_WebHost_OnlineServicesModeLabel(), escapedOnlineServicesMode, sizeof( escapedOnlineServicesMode ) );
	CL_WebUI_JsonEscape( CL_WebHost_OnlineServicesPolicyLabel(), escapedOnlineServicesPolicy, sizeof( escapedOnlineServicesPolicy ) );
	CL_WebUI_JsonEscape( CL_WebHost_MatchmakingProviderLabel(), escapedMatchmakingProvider, sizeof( escapedMatchmakingProvider ) );
	CL_WebUI_JsonEscape( CL_WebHost_MatchmakingPolicyLabel(), escapedMatchmakingPolicy, sizeof( escapedMatchmakingPolicy ) );
	CL_WebUI_JsonEscape( CL_WebHost_WorkshopProviderLabel(), escapedWorkshopProvider, sizeof( escapedWorkshopProvider ) );
	CL_WebUI_JsonEscape( CL_WebHost_WorkshopPolicyLabel(), escapedWorkshopPolicy, sizeof( escapedWorkshopPolicy ) );
	CL_WebUI_JsonEscape( cl_webui.currentUrl, escapedUrl, sizeof( escapedUrl ) );
	CL_WebHost_BuildConfigCvarJson( cvarJson, sizeof( cvarJson ) );
	CL_WebHost_BuildConfigBindJson( bindJson, sizeof( bindJson ) );

	buffer[0] = '\0';
	CL_WebUI_AppendJsonFormattedIfFits(
		buffer,
		bufferSize,
		"{\"version\":\"%s\",\"appId\":%s,\"steamId\":\"%s\",\"playerName\":\"%s\",\"playerAvatar\":\"%s\",\"playerAvatarUrl\":\"%s\",\"playerProfileUrl\":\"%s\",\"playerProfile\":{\"id\":\"%s\",\"name\":\"%s\",\"avatar\":\"%s\",\"avatarUrl\":\"%s\",\"profileUrl\":\"%s\"},\"onlineServicesMode\":\"%s\",\"onlineServicesPolicy\":\"%s\",\"matchmakingProvider\":\"%s\",\"matchmakingPolicy\":\"%s\",\"workshopProvider\":\"%s\",\"workshopPolicy\":\"%s\",\"browserVisible\":%d,\"browserActive\":%d,\"url\":\"",
		escapedVersion,
		STEAMPATH_APPID,
		escapedSteamId,
		escapedPlayerName,
		escapedPlayerAvatarUrl,
		escapedPlayerAvatarUrl,
		escapedPlayerProfileUrl,
		escapedSteamId,
		escapedPlayerName,
		escapedPlayerAvatarUrl,
		escapedPlayerAvatarUrl,
		escapedPlayerProfileUrl,
		escapedOnlineServicesMode,
		escapedOnlineServicesPolicy,
		escapedMatchmakingProvider,
		escapedMatchmakingPolicy,
		escapedWorkshopProvider,
		escapedWorkshopPolicy,
		cl_webui.browserVisible ? 1 : 0,
		cl_webui.browserActive ? 1 : 0
	);
	CL_WebUI_AppendJsonLiteralIfFits( buffer, bufferSize, escapedUrl );
	CL_WebUI_AppendJsonLiteralIfFits( buffer, bufferSize, "\",\"cvars\":{" );
	CL_WebUI_AppendJsonLiteralIfFits( buffer, bufferSize, cvarJson );
	CL_WebUI_AppendJsonLiteralIfFits( buffer, bufferSize, "},\"binds\":[" );
	CL_WebUI_AppendJsonLiteralIfFits( buffer, bufferSize, bindJson );
	CL_WebUI_AppendJsonLiteralIfFits( buffer, bufferSize, "]}" );
}

static void CL_WebHost_UpdateBrowserConfigCache( const char *configJson ) {
	char *script;
	int scriptSize;

	if ( !configJson || !configJson[0] || !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}

	scriptSize = (int)strlen( configJson ) + 128;
	script = (char *)Z_Malloc( scriptSize );
	if ( !script ) {
		return;
	}

	Com_sprintf(
		script,
		scriptSize,
		"(function(){if(window.__qlr_set_native_config){window.__qlr_set_native_config(%s);}})();",
		configJson
	);
	CL_Awesomium_ExecuteJavascript( script, "" );
	Z_Free( script );
}

static void CL_WebHost_SyncNativeSnapshots( qboolean force ) {
	static qboolean lastCursorPositionValid = qfalse;
	static int lastCursorX = 0;
	static int lastCursorY = 0;
	char configJson[CL_WEB_CONFIG_JSON_LENGTH];

	if ( !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}

	if ( cl_webui.cursorPositionValid ) {
		if ( force || !lastCursorPositionValid || cl_webui.cursorX != lastCursorX || cl_webui.cursorY != lastCursorY ) {
			CL_WebHost_UpdateBrowserCursorPosition( cl_webui.cursorX, cl_webui.cursorY );
			lastCursorPositionValid = qtrue;
			lastCursorX = cl_webui.cursorX;
			lastCursorY = cl_webui.cursorY;
		}
	} else {
		lastCursorPositionValid = qfalse;
	}

	if ( force ) {
		char clipboardText[MAX_STRING_CHARS];

		CL_WebHost_ReadClipboardText( clipboardText, sizeof( clipboardText ) );
		CL_WebHost_UpdateBrowserClipboardCache( clipboardText );
	}

	if ( !force && cl_webui.configSnapshotSynced && cl_webui.frameSequence < cl_webui.nextConfigSnapshotFrame ) {
		return;
	}

	if ( !force && CL_Awesomium_IsLoading() ) {
		cl_webui.nextConfigSnapshotFrame = cl_webui.frameSequence + CL_WEB_NATIVE_REQUEST_LOADING_POLL_FRAMES;
		return;
	}

	CL_WebHost_BuildConfigJson( configJson, sizeof( configJson ) );
	CL_WebHost_UpdateBrowserConfigCache( configJson );
	cl_webui.configSnapshotSynced = qtrue;
	cl_webui.nextConfigSnapshotFrame = cl_webui.frameSequence + CL_WEB_CONFIG_SYNC_FRAMES;

	if ( force || !cl_webui.demoSnapshotSynced ) {
		char demoJson[CL_WEB_EVENT_PAYLOAD_LENGTH];

		CL_WebHost_BuildDemoListJson( demoJson, sizeof( demoJson ) );
		CL_WebHost_UpdateBrowserDemoList( demoJson );
		cl_webui.demoSnapshotSynced = qtrue;
	}

	if ( force || !cl_webui.mapCatalogSynced ) {
		char mapJson[CL_WEB_CATALOG_JSON_LENGTH];

		CL_WebHost_BuildMapListJson( mapJson, sizeof( mapJson ) );
		CL_WebHost_UpdateBrowserMapList( mapJson );
		cl_webui.mapCatalogSynced = qtrue;
	}

	if ( force || !cl_webui.factoryCatalogSynced ) {
		char factoryJson[CL_WEB_CATALOG_JSON_LENGTH];

		CL_WebHost_BuildFactoryListJson( factoryJson, sizeof( factoryJson ) );
		CL_WebHost_UpdateBrowserFactoryList( factoryJson );
		cl_webui.factoryCatalogSynced = qtrue;
	}

	{
		char friendJson[CL_WEB_CONFIG_JSON_LENGTH];

		CL_WebHost_BuildFriendListJson( friendJson, sizeof( friendJson ) );
		CL_WebHost_UpdateBrowserFriendList( friendJson );
	}
}

static void CL_WebHost_ReadClipboardText( char *buffer, size_t bufferSize ) {
	char *clipboardData;

	if ( !buffer || bufferSize == 0 ) {
		return;
	}
	buffer[0] = '\0';

	clipboardData = Sys_GetClipboardData();
	if ( clipboardData ) {
		Q_strncpyz( buffer, clipboardData, (int)bufferSize );
		Z_Free( clipboardData );
	}
}

static void CL_WebHost_SetClipboardText( const char *text ) {
	Sys_SetClipboardData( text ? text : "" );
	CL_WebHost_UpdateBrowserClipboardCache( text ? text : "" );
}

static qboolean CL_WebHost_FileExists( const char *path ) {
	if ( !CL_WebHost_PathIsSafeRelative( path ) ) {
		return qfalse;
	}

	return FS_FileExists( path ) ? qtrue : qfalse;
}

static qboolean CL_WebHost_WriteTextFile( const char *path, const char *contents ) {
	if ( !CL_WebHost_PathIsSafeRelative( path ) ) {
		return qfalse;
	}

	FS_WriteFile( path, contents ? contents : "", (int)strlen( contents ? contents : "" ) );
	return qtrue;
}

static void CL_WebHost_AppendDemoListForExtension( const char *extension, char *buffer, size_t bufferSize, int *entryCount ) {
	char fileList[MAX_STRING_CHARS];
	char *cursor;
	int fileCount;

	if ( !extension || !extension[0] || !buffer || bufferSize == 0 || !entryCount ) {
		return;
	}

	fileCount = FS_GetFileList( "demos", extension, fileList, sizeof( fileList ) );
	cursor = fileList;
	for ( int i = 0; i < fileCount && cursor[0]; ++i ) {
		char escapedName[MAX_QPATH * 2];

		CL_WebUI_JsonEscape( cursor, escapedName, sizeof( escapedName ) );
		if ( *entryCount > 0 ) {
			Q_strcat( buffer, bufferSize, "," );
		}
		Q_strcat( buffer, bufferSize, "\"" );
		Q_strcat( buffer, bufferSize, escapedName );
		Q_strcat( buffer, bufferSize, "\"" );
		++*entryCount;

		cursor += strlen( cursor ) + 1;
	}
}

static void CL_WebHost_BuildDemoListJson( char *buffer, size_t bufferSize ) {
	char demoExtension[32];
	int entryCount;
	int protocol;

	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	protocol = com_protocol ? com_protocol->integer : DEFAULT_PROTOCOL_VERSION;
	Com_sprintf( demoExtension, sizeof( demoExtension ), DEMOEXT "%d", protocol );

	buffer[0] = '\0';
	Q_strcat( buffer, bufferSize, "[" );
	entryCount = 0;
	CL_WebHost_AppendDemoListForExtension( demoExtension, buffer, bufferSize, &entryCount );
	Q_strcat( buffer, bufferSize, "]" );
}

static void CL_WebHost_SaveServerCache( void ) {
	fileHandle_t file;
	int size;

	file = FS_FOpenFileWrite( "servercache.dat" );
	if ( !file ) {
		return;
	}

	size = sizeof( cls.globalServers ) + sizeof( cls.favoriteServers );
	FS_Write( &cls.numglobalservers, sizeof( cls.numglobalservers ), file );
	FS_Write( &cls.numfavoriteservers, sizeof( cls.numfavoriteservers ), file );
	FS_Write( &size, sizeof( size ), file );
	FS_Write( cls.globalServers, sizeof( cls.globalServers ), file );
	FS_Write( cls.favoriteServers, sizeof( cls.favoriteServers ), file );
	FS_FCloseFile( file );
}

static qboolean CL_WebHost_StringIsUnsignedInteger( const char *text ) {
	if ( !text || !text[0] ) {
		return qfalse;
	}

	for ( const char *cursor = text; *cursor; ++cursor ) {
		if ( *cursor < '0' || *cursor > '9' ) {
			return qfalse;
		}
	}

	return qtrue;
}

static void CL_WebHost_FormatNumericAddress( unsigned int ip, int port, char *buffer, size_t bufferSize ) {
	Com_sprintf(
		buffer,
		(int)bufferSize,
		"%u.%u.%u.%u:%d",
		ip & 0xffu,
		( ip >> 8 ) & 0xffu,
		( ip >> 16 ) & 0xffu,
		( ip >> 24 ) & 0xffu,
		port
	);
}

static qboolean CL_WebHost_ParseServerEndpoint( const char *payload, char *address, size_t addressSize ) {
	const char *portStart;
	char host[MAX_TOKEN_CHARS];
	char portText[32];
	int hostLength;
	int port;

	if ( !payload || !payload[0] || !address || addressSize == 0 ) {
		return qfalse;
	}
	address[0] = '\0';

	portStart = strchr( payload, '\n' );
	if ( !portStart ) {
		Q_strncpyz( address, payload, (int)addressSize );
		return qtrue;
	}

	hostLength = (int)( portStart - payload );
	if ( hostLength <= 0 ) {
		return qfalse;
	}
	if ( hostLength >= (int)sizeof( host ) ) {
		hostLength = (int)sizeof( host ) - 1;
	}
	Com_Memcpy( host, payload, hostLength );
	host[hostLength] = '\0';

	portStart++;
	Q_strncpyz( portText, portStart, sizeof( portText ) );
	port = atoi( portText );
	if ( port <= 0 || port > 65535 ) {
		port = PORT_SERVER;
	}

	if ( CL_WebHost_StringIsUnsignedInteger( host ) ) {
		CL_WebHost_FormatNumericAddress( (unsigned int)strtoul( host, NULL, 10 ), port, address, addressSize );
	} else if ( strchr( host, ':' ) ) {
		Q_strncpyz( address, host, (int)addressSize );
	} else {
		Com_sprintf( address, (int)addressSize, "%s:%d", host, port );
	}

	return qtrue;
}

static qboolean CL_WebHost_SetFavoriteServer( const char *payload ) {
	const char *addStart;
	char endpoint[MAX_STRING_CHARS];
	char address[MAX_TOKEN_CHARS];
	netadr_t adr;
	qboolean add;

	if ( !payload || !payload[0] ) {
		return qfalse;
	}

	addStart = strrchr( payload, '\n' );
	add = ( !addStart || addStart[1] == '\0' || atoi( addStart + 1 ) != 0 ) ? qtrue : qfalse;
	if ( addStart && addStart > payload ) {
		int endpointLength = (int)( addStart - payload );

		if ( endpointLength >= (int)sizeof( endpoint ) ) {
			endpointLength = (int)sizeof( endpoint ) - 1;
		}
		Com_Memcpy( endpoint, payload, endpointLength );
		endpoint[endpointLength] = '\0';
	} else {
		Q_strncpyz( endpoint, payload, sizeof( endpoint ) );
	}

	if ( !CL_WebHost_ParseServerEndpoint( endpoint, address, sizeof( address ) ) || !NET_StringToAdr( address, &adr, NA_UNSPEC ) ) {
		return qfalse;
	}

	for ( int i = 0; i < cls.numfavoriteservers; ++i ) {
		if ( NET_CompareAdr( &cls.favoriteServers[i].adr, &adr ) ) {
			if ( !add ) {
				memmove( &cls.favoriteServers[i], &cls.favoriteServers[i + 1], ( cls.numfavoriteservers - i - 1 ) * sizeof( cls.favoriteServers[i] ) );
				--cls.numfavoriteservers;
				Com_Memset( &cls.favoriteServers[cls.numfavoriteservers], 0, sizeof( cls.favoriteServers[cls.numfavoriteservers] ) );
				CL_WebHost_SaveServerCache();
			}
			return qtrue;
		}
	}

	if ( !add || cls.numfavoriteservers >= MAX_OTHER_SERVERS ) {
		return qfalse;
	}

	Com_Memset( &cls.favoriteServers[cls.numfavoriteservers], 0, sizeof( cls.favoriteServers[cls.numfavoriteservers] ) );
	cls.favoriteServers[cls.numfavoriteservers].adr = adr;
	Q_strncpyz( cls.favoriteServers[cls.numfavoriteservers].hostName, address, sizeof( cls.favoriteServers[cls.numfavoriteservers].hostName ) );
	cls.favoriteServers[cls.numfavoriteservers].visible = qtrue;
	cls.favoriteServers[cls.numfavoriteservers].ping = -1;
	++cls.numfavoriteservers;
	CL_WebHost_SaveServerCache();
	return qtrue;
}

static int CL_WebHost_ServerRequestModeToSource( int requestMode ) {
	switch ( requestMode ) {
		case 1:
			return AS_LOCAL;
		case 3:
		case 4:
			return AS_FAVORITES;
		case 0:
		case 2:
		default:
			return AS_GLOBAL;
	}
}

static serverInfo_t *CL_WebHost_GetServerList( int source, int *count, int *capacity ) {
	if ( count ) {
		*count = 0;
	}
	if ( capacity ) {
		*capacity = 0;
	}

	switch ( source ) {
		case AS_LOCAL:
			if ( count ) {
				*count = cls.numlocalservers;
			}
			if ( capacity ) {
				*capacity = MAX_OTHER_SERVERS;
			}
			return cls.localServers;
		case AS_GLOBAL:
			if ( count ) {
				*count = cls.numglobalservers;
			}
			if ( capacity ) {
				*capacity = MAX_GLOBAL_SERVERS;
			}
			return cls.globalServers;
		case AS_FAVORITES:
			if ( count ) {
				*count = cls.numfavoriteservers;
			}
			if ( capacity ) {
				*capacity = MAX_OTHER_SERVERS;
			}
			return cls.favoriteServers;
		default:
			return NULL;
	}
}

static void CL_WebHost_MarkServerVisible( int source, int index, qboolean visible ) {
	serverInfo_t *servers;
	int capacity;
	int i;

	servers = CL_WebHost_GetServerList( source, NULL, &capacity );
	if ( !servers ) {
		return;
	}

	if ( index == -1 ) {
		for ( i = 0; i < capacity; i++ ) {
			servers[i].visible = visible;
		}
		return;
	}

	if ( index >= 0 && index < capacity ) {
		servers[index].visible = visible;
	}
}

static void CL_WebHost_ResetServerPings( int source ) {
	serverInfo_t *servers;
	int capacity;
	int i;

	servers = CL_WebHost_GetServerList( source, NULL, &capacity );
	if ( !servers ) {
		return;
	}

	for ( i = 0; i < capacity; i++ ) {
		servers[i].ping = -1;
	}
}

static void CL_WebHost_StartServerRefresh( int requestMode, int source ) {
	cl_webui.serverRefreshInitialized = qtrue;
	cl_webui.serverRefreshActive = qtrue;
	cl_webui.serverRefreshRequestMode = requestMode;
	cl_webui.serverRefreshSource = source;
	cl_webui.serverRefreshTime = cls.realtime + ( source == AS_LOCAL ? CL_WEB_SERVER_LOCAL_REFRESH_WAIT_MSEC : CL_WEB_SERVER_REMOTE_REFRESH_WAIT_MSEC );
	cl_webui.serverRefreshTimeoutTime = cls.realtime + CL_WEB_SERVER_REFRESH_TIMEOUT_MSEC;

	CL_WebHost_MarkServerVisible( source, -1, qtrue );
	CL_WebHost_ResetServerPings( source );
}

static void CL_WebHost_PublishServerBrowserRefreshEnd( void ) {
	if ( !cl_webui.serverRefreshActive ) {
		return;
	}

	cl_webui.serverRefreshActive = qfalse;
	CL_WebView_PublishEvent( "servers.refresh.end", NULL );
}

static void CL_WebHost_ServerBrowserFrame( void ) {
	qboolean wait;
	int count;

	if ( !cl_webui.serverRefreshActive ) {
		return;
	}

	wait = qfalse;
	if ( cl_webui.serverRefreshSource != AS_FAVORITES ) {
		CL_WebHost_GetServerList( cl_webui.serverRefreshSource, &count, NULL );
		if ( cl_webui.serverRefreshSource == AS_LOCAL ) {
			if ( !count ) {
				wait = qtrue;
			}
		} else if ( count < 0 ) {
			wait = qtrue;
		}
	}

	if ( cls.realtime < cl_webui.serverRefreshTime && wait ) {
		return;
	}

	if ( CL_UpdateVisiblePings_f( cl_webui.serverRefreshSource ) ) {
		cl_webui.serverRefreshTime = cls.realtime + 1000;
		return;
	}

	if ( wait && cls.realtime < cl_webui.serverRefreshTimeoutTime ) {
		return;
	}

	CL_WebHost_PublishServerBrowserRefreshEnd();
}

static unsigned int CL_WebHost_PackedIPv4FromAddress( const netadr_t *address ) {
	if ( !address || address->type != NA_IP ) {
		return 0u;
	}

	return (unsigned int)address->ipv._4[0] |
		( (unsigned int)address->ipv._4[1] << 8 ) |
		( (unsigned int)address->ipv._4[2] << 16 ) |
		( (unsigned int)address->ipv._4[3] << 24 );
}

static unsigned short CL_WebHost_PortFromAddress( const netadr_t *address ) {
	if ( !address ) {
		return 0;
	}

	return (unsigned short)BigShort( address->port );
}

static void CL_WebHost_FormatServerDetailId( unsigned int serverIp, unsigned short serverPort, char *buffer, size_t bufferSize ) {
	Com_sprintf( buffer, bufferSize, "%u_%u", serverIp, (unsigned int)serverPort );
}

static serverInfo_t *CL_WebHost_FindServerInfoByAddress( const netadr_t *address ) {
	static const int sources[] = { AS_LOCAL, AS_GLOBAL, AS_FAVORITES };
	int sourceIndex;

	if ( !address ) {
		return NULL;
	}

	for ( sourceIndex = 0; sourceIndex < (int)( sizeof( sources ) / sizeof( sources[0] ) ); sourceIndex++ ) {
		serverInfo_t *servers;
		int capacity;
		int i;

		servers = CL_WebHost_GetServerList( sources[sourceIndex], NULL, &capacity );
		if ( !servers ) {
			continue;
		}

		for ( i = 0; i < capacity; i++ ) {
			if ( NET_CompareAdr( address, &servers[i].adr ) ) {
				return &servers[i];
			}
		}
	}

	return NULL;
}

static void CL_WebHost_ClearServerDetailRequest( void ) {
	cl_webui.serverDetailActive = qfalse;
	cl_webui.serverDetailTimeoutTime = 0;
	cl_webui.serverDetailIp = 0u;
	cl_webui.serverDetailPort = 0;
	cl_webui.serverDetailId[0] = '\0';
	Com_Memset( &cl_webui.serverDetailAddress, 0, sizeof( cl_webui.serverDetailAddress ) );
}

static qboolean CL_WebHost_DetailMatchesAddress( const netadr_t *address ) {
	if ( !cl_webui.serverDetailActive || !address ) {
		return qfalse;
	}

	return NET_CompareAdr( address, &cl_webui.serverDetailAddress ) ? qtrue : qfalse;
}

static void CL_WebHost_PublishServerDetailRulesEnd( void ) {
	char eventName[CL_WEB_EVENT_NAME_LENGTH];

	Com_sprintf( eventName, sizeof( eventName ), "servers.rules.%s.end", cl_webui.serverDetailId );
	CL_WebView_PublishEvent( eventName, NULL );
}

static void CL_WebHost_PublishServerDetailPlayersEnd( void ) {
	char eventName[CL_WEB_EVENT_NAME_LENGTH];

	Com_sprintf( eventName, sizeof( eventName ), "servers.players.%s.end", cl_webui.serverDetailId );
	CL_WebView_PublishEvent( eventName, NULL );
}

static void CL_WebHost_PublishServerDetailRulesFailed( void ) {
	char eventName[CL_WEB_EVENT_NAME_LENGTH];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];

	Com_sprintf( eventName, sizeof( eventName ), "servers.rules.%s.failed", cl_webui.serverDetailId );
	Com_sprintf(
		payload,
		sizeof( payload ),
		"{\"id\":\"%s\",\"ip\":%u,\"port\":%u}",
		cl_webui.serverDetailId,
		cl_webui.serverDetailIp,
		(unsigned int)cl_webui.serverDetailPort );
	CL_WebView_PublishEvent( eventName, payload );
}

static void CL_WebHost_PublishServerDetailPlayersFailed( void ) {
	char eventName[CL_WEB_EVENT_NAME_LENGTH];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];

	Com_sprintf( eventName, sizeof( eventName ), "servers.players.%s.failed", cl_webui.serverDetailId );
	Com_sprintf(
		payload,
		sizeof( payload ),
		"{\"id\":\"%s\",\"ip\":%u,\"port\":%u}",
		cl_webui.serverDetailId,
		cl_webui.serverDetailIp,
		(unsigned int)cl_webui.serverDetailPort );
	CL_WebView_PublishEvent( eventName, payload );
}

static void CL_WebHost_PublishServerDetailFailed( void ) {
	char eventName[CL_WEB_EVENT_NAME_LENGTH];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];

	if ( !cl_webui.serverDetailActive ) {
		return;
	}

	Com_sprintf( eventName, sizeof( eventName ), "servers.details.%s.failed", cl_webui.serverDetailId );
	Com_sprintf(
		payload,
		sizeof( payload ),
		"{\"id\":\"%s\",\"ip\":%u,\"port\":%u}",
		cl_webui.serverDetailId,
		cl_webui.serverDetailIp,
		(unsigned int)cl_webui.serverDetailPort );
	CL_WebView_PublishEvent( eventName, payload );
	CL_WebHost_PublishServerDetailRulesFailed();
	CL_WebHost_PublishServerDetailPlayersFailed();
	CL_WebHost_ClearServerDetailRequest();
}

static void CL_WebHost_PublishServerDetailRuleResponse( const char *rule, const char *value ) {
	char eventName[CL_WEB_EVENT_NAME_LENGTH];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];
	char escapedRule[512];
	char escapedValue[512];

	CL_WebUI_JsonEscape( rule ? rule : "", escapedRule, sizeof( escapedRule ) );
	CL_WebUI_JsonEscape( value ? value : "", escapedValue, sizeof( escapedValue ) );
	Com_sprintf( eventName, sizeof( eventName ), "servers.rules.%s.response", cl_webui.serverDetailId );
	Com_sprintf(
		payload,
		sizeof( payload ),
		"{\"id\":\"%s\",\"ip\":%u,\"port\":%u,\"rule\":\"%s\",\"value\":\"%s\"}",
		cl_webui.serverDetailId,
		cl_webui.serverDetailIp,
		(unsigned int)cl_webui.serverDetailPort,
		escapedRule,
		escapedValue );
	CL_WebView_PublishEvent( eventName, payload );
}

static void CL_WebHost_PublishServerDetailRulesFromInfoString( const char *infoString ) {
	const char *cursor;
	char rule[256];
	char value[256];
	int length;

	cursor = infoString ? infoString : "";
	while ( *cursor ) {
		if ( *cursor == '\\' ) {
			cursor++;
		}
		if ( !*cursor ) {
			break;
		}

		length = 0;
		while ( *cursor && *cursor != '\\' ) {
			if ( length < (int)sizeof( rule ) - 1 ) {
				rule[length++] = *cursor;
			}
			cursor++;
		}
		rule[length] = '\0';

		if ( *cursor == '\\' ) {
			cursor++;
		}

		length = 0;
		while ( *cursor && *cursor != '\\' ) {
			if ( length < (int)sizeof( value ) - 1 ) {
				value[length++] = *cursor;
			}
			cursor++;
		}
		value[length] = '\0';

		if ( rule[0] ) {
			CL_WebHost_PublishServerDetailRuleResponse( rule, value );
		}
	}

	CL_WebHost_PublishServerDetailRulesEnd();
}

static void CL_WebHost_PublishServerDetailPlayerResponse( const char *playerLine ) {
	char eventName[CL_WEB_EVENT_NAME_LENGTH];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];
	char name[MAX_NAME_LENGTH];
	char escapedName[MAX_NAME_LENGTH * 2];
	int score;
	int timeValue;

	score = 0;
	timeValue = 0;
	name[0] = '\0';

	if ( !playerLine || !playerLine[0] ) {
		return;
	}

	if ( sscanf( playerLine, "%d %d \"%31[^\"]\"", &score, &timeValue, name ) < 3 &&
		sscanf( playerLine, "%d %d %31[^\n]", &score, &timeValue, name ) < 3 ) {
		return;
	}

	CL_WebUI_JsonEscape( name, escapedName, sizeof( escapedName ) );
	Com_sprintf( eventName, sizeof( eventName ), "servers.players.%s.response", cl_webui.serverDetailId );
	Com_sprintf(
		payload,
		sizeof( payload ),
		"{\"id\":\"%s\",\"ip\":%u,\"port\":%u,\"name\":\"%s\",\"score\":%d,\"time\":%d}",
		cl_webui.serverDetailId,
		cl_webui.serverDetailIp,
		(unsigned int)cl_webui.serverDetailPort,
		escapedName,
		score,
		timeValue );
	CL_WebView_PublishEvent( eventName, payload );
}

static void CL_WebHost_PublishServerDetailResponse( const netadr_t *address, const char *infoString ) {
	serverInfo_t *server;
	char eventName[CL_WEB_EVENT_NAME_LENGTH];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];
	char hostName[MAX_NAME_LENGTH];
	char mapName[MAX_NAME_LENGTH];
	char tags[MAX_INFO_STRING];
	char steamId[64];
	char gametypeValue[16];
	char passwordValue[16];
	char hostNameEscaped[MAX_NAME_LENGTH * 2];
	char mapNameEscaped[MAX_NAME_LENGTH * 2];
	char tagsEscaped[MAX_INFO_STRING * 2];
	char steamIdEscaped[128];
	int numPlayers;
	int maxPlayers;
	int botPlayers;
	int password;
	int vac;
	int gametype;
	int ping;

	if ( !CL_WebHost_DetailMatchesAddress( address ) ) {
		return;
	}

	server = CL_WebHost_FindServerInfoByAddress( address );
	hostName[0] = '\0';
	mapName[0] = '\0';
	tags[0] = '\0';
	steamId[0] = '\0';
	gametypeValue[0] = '\0';
	passwordValue[0] = '\0';
	numPlayers = 0;
	maxPlayers = 0;
	botPlayers = 0;
	password = 0;
	vac = 0;
	gametype = 0;
	ping = 0;

	if ( infoString ) {
		Q_strncpyz( hostName, Info_ValueForKey( infoString, "hostname" ), sizeof( hostName ) );
		Q_strncpyz( mapName, Info_ValueForKey( infoString, "mapname" ), sizeof( mapName ) );
		Q_strncpyz( tags, Info_ValueForKey( infoString, "sv_keywords" ), sizeof( tags ) );
		Q_strncpyz( steamId, Info_ValueForKey( infoString, "steamid" ), sizeof( steamId ) );
		if ( !steamId[0] ) {
			Q_strncpyz( steamId, Info_ValueForKey( infoString, "steam_id" ), sizeof( steamId ) );
		}
		if ( !tags[0] ) {
			Q_strncpyz( tags, Info_ValueForKey( infoString, "game" ), sizeof( tags ) );
		}
		numPlayers = atoi( Info_ValueForKey( infoString, "clients" ) );
		maxPlayers = atoi( Info_ValueForKey( infoString, "sv_maxclients" ) );
		botPlayers = atoi( Info_ValueForKey( infoString, "botPlayers" ) );
		vac = atoi( Info_ValueForKey( infoString, "vac" ) );
		Q_strncpyz( gametypeValue, Info_ValueForKey( infoString, "gametype" ), sizeof( gametypeValue ) );
		Q_strncpyz( passwordValue, Info_ValueForKey( infoString, "g_needpass" ), sizeof( passwordValue ) );
		if ( !passwordValue[0] ) {
			Q_strncpyz( passwordValue, Info_ValueForKey( infoString, "needpass" ), sizeof( passwordValue ) );
		}
	}

	if ( server ) {
		if ( !hostName[0] ) {
			Q_strncpyz( hostName, server->hostName, sizeof( hostName ) );
		}
		if ( !mapName[0] ) {
			Q_strncpyz( mapName, server->mapName, sizeof( mapName ) );
		}
		if ( !tags[0] ) {
			Q_strncpyz( tags, server->game, sizeof( tags ) );
		}
		if ( !numPlayers ) {
			numPlayers = server->clients;
		}
		if ( !maxPlayers ) {
			maxPlayers = server->maxClients;
		}
		if ( !gametypeValue[0] ) {
			Com_sprintf( gametypeValue, sizeof( gametypeValue ), "%d", server->gameType );
		}
		if ( server->ping > 0 ) {
			ping = server->ping;
		}
	}

	if ( !steamId[0] ) {
		Q_strncpyz( steamId, "0", sizeof( steamId ) );
	}
	if ( gametypeValue[0] ) {
		gametype = atoi( gametypeValue );
	}
	if ( passwordValue[0] ) {
		password = atoi( passwordValue );
	}

	CL_WebUI_JsonEscape( hostName, hostNameEscaped, sizeof( hostNameEscaped ) );
	CL_WebUI_JsonEscape( mapName, mapNameEscaped, sizeof( mapNameEscaped ) );
	CL_WebUI_JsonEscape( tags, tagsEscaped, sizeof( tagsEscaped ) );
	CL_WebUI_JsonEscape( steamId, steamIdEscaped, sizeof( steamIdEscaped ) );
	Com_sprintf( eventName, sizeof( eventName ), "servers.details.%s.response", cl_webui.serverDetailId );
	Com_sprintf(
		payload,
		sizeof( payload ),
		"{\"name\":\"%s\",\"numPlayers\":%d,\"maxPlayers\":%d,\"ping\":%d,\"map\":\"%s\",\"botPlayers\":%d,\"password\":%d,\"vac\":%d,\"ip\":%u,\"port\":%u,\"id\":\"%s\",\"steam_id\":\"%s\",\"tags\":\"%s\",\"gametype\":%d,\"lastPlayed\":0}",
		hostNameEscaped,
		numPlayers,
		maxPlayers,
		ping > 0 ? ping : 0,
		mapNameEscaped,
		botPlayers,
		password,
		vac,
		cl_webui.serverDetailIp,
		(unsigned int)cl_webui.serverDetailPort,
		cl_webui.serverDetailId,
		steamIdEscaped,
		tagsEscaped,
		gametype );
	CL_WebView_PublishEvent( eventName, payload );
}

static qboolean CL_WebHost_StartServerDetailRequest( const netadr_t *address ) {
	if ( !address || address->type != NA_IP ) {
		CL_WebHost_ClearServerDetailRequest();
		return qfalse;
	}

	CL_WebHost_ClearServerDetailRequest();
	cl_webui.serverDetailActive = qtrue;
	cl_webui.serverDetailAddress = *address;
	cl_webui.serverDetailIp = CL_WebHost_PackedIPv4FromAddress( address );
	cl_webui.serverDetailPort = CL_WebHost_PortFromAddress( address );
	cl_webui.serverDetailTimeoutTime = cls.realtime + CL_WEB_SERVER_DETAILS_TIMEOUT_MSEC;
	CL_WebHost_FormatServerDetailId( cl_webui.serverDetailIp, cl_webui.serverDetailPort, cl_webui.serverDetailId, sizeof( cl_webui.serverDetailId ) );
	return qtrue;
}

static void CL_WebHost_ServerDetailsFrame( void ) {
	if ( !cl_webui.serverDetailActive ) {
		return;
	}

	if ( cls.realtime >= cl_webui.serverDetailTimeoutTime ) {
		CL_WebHost_PublishServerDetailFailed();
	}
}

qboolean CL_WebHost_OnServerStatusResponseInfo( const netadr_t *address, const char *infoString ) {
	if ( !CL_WebHost_DetailMatchesAddress( address ) ) {
		return qfalse;
	}

	CL_WebHost_PublishServerDetailResponse( address, infoString );
	CL_WebHost_PublishServerDetailRulesFromInfoString( infoString );
	return qtrue;
}

void CL_WebHost_OnServerStatusResponsePlayer( const netadr_t *address, const char *playerLine ) {
	if ( !CL_WebHost_DetailMatchesAddress( address ) ) {
		return;
	}

	CL_WebHost_PublishServerDetailPlayerResponse( playerLine );
}

void CL_WebHost_OnServerStatusResponseComplete( const netadr_t *address ) {
	if ( !CL_WebHost_DetailMatchesAddress( address ) ) {
		return;
	}

	CL_WebHost_PublishServerDetailPlayersEnd();
	CL_WebHost_ClearServerDetailRequest();
}

static const char *CL_WebHost_ServerSourceLabel( int source ) {
	switch ( source ) {
		case AS_LOCAL:
			return "local";
		case AS_GLOBAL:
			return "global";
		case AS_FAVORITES:
			return "favorites";
		default:
			return "compatibility";
	}
}

static const char *CL_WebHost_ServerRequestModeLabel( int requestMode ) {
	switch ( requestMode ) {
		case 1:
			return "local";
		case 3:
			return "favorites";
		case 4:
			return "history";
		case 0:
		case 2:
			return "global";
		default:
			return "compatibility";
	}
}

static const char *CL_WebHost_ServerBrowserCompatibilityReason( int requestMode ) {
	if ( requestMode == 4 ) {
		return "history fallback mapped to favorites source";
	}

	return "Steamworks server browser is not available in this build";
}

static void CL_WebHost_PublishServerBrowserCompatibility( int requestMode, int source ) {
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];

	Com_sprintf(
		payload,
		sizeof( payload ),
		"{\"mode\":%d,\"modeLabel\":\"%s\",\"source\":\"%s\",\"owner\":\"legacy engine server browser\",\"missingNativeOwner\":\"SteamMatchmakingServers\",\"reason\":\"%s\",\"policy\":\"compatibility\"}",
		requestMode,
		CL_WebHost_ServerRequestModeLabel( requestMode ),
		CL_WebHost_ServerSourceLabel( source ),
		CL_WebHost_ServerBrowserCompatibilityReason( requestMode )
	);
	CL_WebView_PublishEvent( "servers.refresh.compatibility", payload );
}

qboolean CL_Steam_RequestServers( int requestMode ) {
	const int source = CL_WebHost_ServerRequestModeToSource( requestMode );

	CL_WebHost_StartServerRefresh( requestMode, source );
	CL_WebView_PublishEvent( "servers.refresh.start", NULL );
	CL_WebHost_PublishServerBrowserCompatibility( requestMode, source );
	if ( source == AS_LOCAL ) {
		Cbuf_ExecuteText( EXEC_APPEND, "localservers\n" );
	} else if ( source == AS_FAVORITES ) {
		CL_WebHost_ServerBrowserFrame();
	} else {
		Cbuf_ExecuteText( EXEC_APPEND, va( "globalservers 0 %d\n", com_protocol ? com_protocol->integer : DEFAULT_PROTOCOL_VERSION ) );
	}

	return qtrue;
}

qboolean CL_Steam_RequestServerDetails( unsigned int serverIp, unsigned short serverPort ) {
	char address[MAX_TOKEN_CHARS];
	netadr_t adr;

	CL_WebHost_FormatNumericAddress( serverIp, serverPort, address, sizeof( address ) );
	if ( !NET_StringToAdr( address, &adr, NA_UNSPEC ) ) {
		return qfalse;
	}

	CL_WebHost_StartServerDetailRequest( &adr );
	Cbuf_ExecuteText( EXEC_APPEND, va( "serverstatus %s\n", address ) );
	return qtrue;
}

qboolean CL_Steam_RefreshServerList( void ) {
	if ( !cl_webui.serverRefreshInitialized ) {
		return qfalse;
	}

	return CL_Steam_RequestServers( cl_webui.serverRefreshRequestMode );
}

static void CL_WebHost_RequestServers( const char *payload ) {
	CL_Steam_RequestServers( payload && payload[0] ? atoi( payload ) : AS_GLOBAL );
}

static void CL_WebHost_RequestServerDetails( const char *payload ) {
	char address[MAX_TOKEN_CHARS];
	netadr_t adr;

	if ( CL_WebHost_ParseServerEndpoint( payload, address, sizeof( address ) ) && NET_StringToAdr( address, &adr, NA_UNSPEC ) ) {
		CL_WebHost_StartServerDetailRequest( &adr );
		Cbuf_ExecuteText( EXEC_APPEND, va( "serverstatus %s\n", address ) );
	}
}

static void CL_WebHost_CopyPayloadLine( const char *payload, int lineIndex, char *buffer, size_t bufferSize ) {
	const char *lineStart;
	const char *lineEnd;
	int currentLine;
	int lineLength;

	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	buffer[0] = '\0';
	if ( !payload || lineIndex < 0 ) {
		return;
	}

	lineStart = payload;
	currentLine = 0;
	while ( currentLine < lineIndex && lineStart[0] ) {
		lineStart = strchr( lineStart, '\n' );
		if ( !lineStart ) {
			return;
		}
		lineStart++;
		currentLine++;
	}

	lineEnd = strchr( lineStart, '\n' );
	if ( !lineEnd ) {
		Q_strncpyz( buffer, lineStart, (int)bufferSize );
		return;
	}

	lineLength = (int)( lineEnd - lineStart );
	if ( lineLength <= 0 ) {
		return;
	}
	if ( lineLength >= (int)bufferSize ) {
		lineLength = (int)bufferSize - 1;
	}
	Com_Memcpy( buffer, lineStart, lineLength );
	buffer[lineLength] = '\0';
}

static qboolean CL_WebHost_SteamBridgeUnavailable( const char *method, const char *payload ) {
	static const char unsupportedReason[] = "Steamworks bridge is not available in this build";
	char escapedMethod[MAX_TOKEN_CHARS * 2];
	char escapedPayload[CL_WEB_EVENT_PAYLOAD_LENGTH];
	char escapedReason[sizeof( unsupportedReason ) * 2];
	char eventPayload[CL_WEB_EVENT_PAYLOAD_LENGTH];

	if ( !method || !method[0] ) {
		return qfalse;
	}

	CL_WebUI_JsonEscape( method, escapedMethod, sizeof( escapedMethod ) );
	CL_WebUI_JsonEscape( payload ? payload : "", escapedPayload, sizeof( escapedPayload ) );
	CL_WebUI_JsonEscape( unsupportedReason, escapedReason, sizeof( escapedReason ) );
	Com_sprintf(
		eventPayload,
		sizeof( eventPayload ),
		"{\"method\":\"%s\",\"available\":false,\"reason\":\"%s\",\"payload\":\"%s\"}",
		escapedMethod,
		escapedReason,
		escapedPayload
	);
	CL_WebView_PublishEvent( "qz.unsupported", eventPayload );
	Com_DPrintf( "WebUI qz social bridge request %s ignored: Steamworks bridge is unavailable.\n", method );
	return qfalse;
}

static void CL_Steam_LogOnlineCallbackIgnored( const char *callbackName, const char *detail ) {
	Com_DPrintf( "Steam callback %s ignored: %s (%s [%s])\n",
		callbackName ? callbackName : "unknown",
		detail ? detail : "Steamworks callback bridge unavailable",
		CL_WebHost_OnlineServicesModeLabel(),
		CL_WebHost_OnlineServicesPolicyLabel() );
}

qboolean CL_Steam_OpenOverlayUrl( const char *url ) {
	if ( !url || !url[0] ) {
		return qfalse;
	}

	return CL_WebHost_SteamBridgeUnavailable( "OpenSteamOverlayURL", url );
}

void CL_Steam_OnRichPresenceJoinRequested( const char *command ) {
	CL_Steam_LogOnlineCallbackIgnored(
		"rich_presence_join_requested",
		( command && command[0] ) ? "Steamworks callback bridge unavailable" : "missing join command" );
}

void CL_Steam_OnGameServerChangeRequested( const char *server, const char *password ) {
	(void)password;
	CL_Steam_LogOnlineCallbackIgnored(
		"server_change_requested",
		( server && server[0] ) ? "Steamworks callback bridge unavailable" : "missing server target" );
}

qboolean CL_Steam_CreateLobby( void ) {
	return CL_WebHost_SteamBridgeUnavailable( "createlobby", "" );
}

qboolean CL_Steam_LeaveLobby( void ) {
	return CL_WebHost_SteamBridgeUnavailable( "leavelobby", "" );
}

qboolean CL_Steam_JoinLobby( const char *lobbyId ) {
	return CL_WebHost_SteamBridgeUnavailable( "joinlobby", lobbyId ? lobbyId : "" );
}

qboolean CL_Steam_SetLobbyServer( unsigned int serverIp, unsigned short serverPort ) {
	char payload[64];

	Com_sprintf( payload, sizeof( payload ), "%u\n%u", serverIp, (unsigned int)serverPort );
	return CL_WebHost_SteamBridgeUnavailable( "setlobbyserver", payload );
}

qboolean CL_Steam_ShowInviteOverlay( void ) {
	return CL_WebHost_SteamBridgeUnavailable( "showinviteoverlay", "" );
}

qboolean CL_Steam_Invite( const char *steamId ) {
	return CL_WebHost_SteamBridgeUnavailable( "invite", steamId ? steamId : "" );
}

qboolean CL_Steam_SayLobby( const char *message ) {
	return CL_WebHost_SteamBridgeUnavailable( "saylobby", message ? message : "" );
}

qboolean CL_Steam_RequestAllUGC( int filter ) {
	char payload[32];

	Com_sprintf( payload, sizeof( payload ), "%d", filter );
	CL_WebHost_UpdateBrowserUGCList( "[]" );
	CL_WebView_PublishEvent( "web.ugc.failed", NULL );
	return CL_WebHost_SteamBridgeUnavailable( "getallugc", payload );
}

qboolean CL_Steam_RequestUserStats( const char *steamId ) {
	return CL_WebHost_SteamBridgeUnavailable( "requestuserstats", steamId ? steamId : "" );
}

qboolean CL_Steam_ActivateOverlayToUser( const char *dialog, const char *steamId ) {
	char payload[MAX_TOKEN_CHARS];

	Com_sprintf( payload, sizeof( payload ), "%s\n%s", dialog ? dialog : "", steamId ? steamId : "" );
	return CL_WebHost_SteamBridgeUnavailable( "activategameoverlaytouser", payload );
}

qboolean CL_Steam_GetItemDownloadInfo( unsigned int itemIdLow, unsigned int itemIdHigh, unsigned long long *outDownloaded, unsigned long long *outTotal ) {
	(void)itemIdLow;
	(void)itemIdHigh;

	if ( outDownloaded ) {
		*outDownloaded = 0ull;
	}
	if ( outTotal ) {
		*outTotal = 0ull;
	}

	return qfalse;
}

static void CL_Steam_OverlayCommand_f( void ) {
	const char *commandName;
	const char *dialog;
	int clientNum;
	unsigned int steamIdLow;
	unsigned int steamIdHigh;
	char steamId[32];

	commandName = Cmd_Argv( 0 );
	if ( Cmd_Argc() < 2 ) {
		CL_WebHost_SteamBridgeUnavailable( commandName, "missing target client" );
		return;
	}

	dialog = NULL;
	if ( !Q_stricmp( commandName, "clientviewprofile" ) ) {
		dialog = "steamid";
	} else if ( !Q_stricmp( commandName, "clientfriendinvite" ) ) {
		dialog = "friendadd";
	}

	if ( !dialog ) {
		CL_WebHost_SteamBridgeUnavailable( commandName, "unsupported social overlay verb" );
		return;
	}

	clientNum = atoi( Cmd_Argv( 1 ) );
	if ( !CL_GetClientSteamId( clientNum, &steamIdLow, &steamIdHigh ) ) {
		CL_WebHost_SteamBridgeUnavailable( commandName, "target client has no Steam identity" );
		return;
	}

	CL_WebHost_FormatSteamId( steamIdLow, steamIdHigh, steamId, sizeof( steamId ) );
	CL_Steam_ActivateOverlayToUser( dialog, steamId );
}

static qboolean CL_WebHost_HandleSocialBridgeRequest( const char *kind, const char *payload ) {
	static const char prefix[] = "social.";
	const char *method;

	if ( !kind || Q_stricmpn( kind, prefix, (int)sizeof( prefix ) - 1 ) ) {
		return qfalse;
	}

	method = kind + sizeof( prefix ) - 1;
	if ( !method[0] ) {
		return qtrue;
	}

	if ( !Q_stricmp( method, "createlobby" ) ) {
		CL_Steam_CreateLobby();
		return qtrue;
	}

	if ( !Q_stricmp( method, "leavelobby" ) ) {
		CL_Steam_LeaveLobby();
		return qtrue;
	}

	if ( !Q_stricmp( method, "joinlobby" ) ) {
		CL_Steam_JoinLobby( payload );
		return qtrue;
	}

	if ( !Q_stricmp( method, "setlobbyserver" ) ) {
		char first[MAX_TOKEN_CHARS];
		char second[MAX_TOKEN_CHARS];
		char third[MAX_TOKEN_CHARS];
		const char *ipText;
		const char *portText;

		CL_WebHost_CopyPayloadLine( payload, 0, first, sizeof( first ) );
		CL_WebHost_CopyPayloadLine( payload, 1, second, sizeof( second ) );
		CL_WebHost_CopyPayloadLine( payload, 2, third, sizeof( third ) );
		ipText = third[0] ? second : first;
		portText = third[0] ? third : second;
		CL_Steam_SetLobbyServer( (unsigned int)strtoul( ipText, NULL, 10 ), (unsigned short)atoi( portText ) );
		return qtrue;
	}

	if ( !Q_stricmp( method, "showinviteoverlay" ) ) {
		CL_Steam_ShowInviteOverlay();
		return qtrue;
	}

	if ( !Q_stricmp( method, "saylobby" ) ) {
		char first[MAX_TOKEN_CHARS];
		char second[MAX_STRING_CHARS];

		CL_WebHost_CopyPayloadLine( payload, 0, first, sizeof( first ) );
		CL_WebHost_CopyPayloadLine( payload, 1, second, sizeof( second ) );
		CL_Steam_SayLobby( second[0] ? second : first );
		return qtrue;
	}

	if ( !Q_stricmp( method, "requestuserstats" ) ) {
		CL_Steam_RequestUserStats( payload );
		return qtrue;
	}

	if ( !Q_stricmp( method, "activategameoverlaytouser" ) ) {
		char dialog[MAX_TOKEN_CHARS];
		char steamId[MAX_TOKEN_CHARS];

		CL_WebHost_CopyPayloadLine( payload, 0, dialog, sizeof( dialog ) );
		CL_WebHost_CopyPayloadLine( payload, 1, steamId, sizeof( steamId ) );
		CL_Steam_ActivateOverlayToUser( dialog, steamId );
		return qtrue;
	}

	if ( !Q_stricmp( method, "invite" ) ) {
		char steamId[MAX_TOKEN_CHARS];

		CL_WebHost_CopyPayloadLine( payload, 0, steamId, sizeof( steamId ) );
		CL_Steam_Invite( steamId[0] ? steamId : payload );
		return qtrue;
	}

	if ( !Q_stricmp( method, "getallugc" ) ) {
		CL_Steam_RequestAllUGC( payload && payload[0] ? atoi( payload ) : 0 );
		return qtrue;
	}

	CL_WebHost_SteamBridgeUnavailable( method, payload );
	return qtrue;
}

static void CL_WebHost_ProcessNativeJavascriptRequest( const char *request ) {
	const char *payload;
	char kind[64];
	int kindLength;

	if ( !request || !request[0] ) {
		return;
	}

	payload = strchr( request, '\n' );
	if ( !payload ) {
		return;
	}

	kindLength = (int)( payload - request );
	if ( kindLength <= 0 ) {
		return;
	}
	if ( kindLength >= (int)sizeof( kind ) ) {
		return;
	}
	Com_Memcpy( kind, request, kindLength );
	kind[kindLength] = '\0';
	payload++;

	if ( !Q_stricmp( kind, "cmd" ) ) {
		if ( payload[0] ) {
			Cbuf_ExecuteText( EXEC_APPEND, va( "%s\n", payload ) );
		}
		return;
	}

	if ( !Q_stricmp( kind, "get" ) ) {
		char name[MAX_CVAR_VALUE_STRING];
		char value[MAX_CVAR_VALUE_STRING];

		if ( !payload[0] || strlen( payload ) >= sizeof( name ) ) {
			return;
		}
		Q_strncpyz( name, payload, sizeof( name ) );
		if ( Cvar_Flags( name ) & CVAR_PRIVATE ) {
			return;
		}
		Cvar_VariableStringBuffer( name, value, sizeof( value ) );
		CL_WebHost_UpdateBrowserCvarCache( name, value );
		return;
	}

	if ( !Q_stricmp( kind, "set" ) ) {
		const char *valueStart;
		char name[MAX_CVAR_VALUE_STRING];
		char value[MAX_CVAR_VALUE_STRING];
		int nameLength;

		valueStart = strchr( payload, '\n' );
		if ( !valueStart ) {
			return;
		}

		nameLength = (int)( valueStart - payload );
		if ( nameLength <= 0 ) {
			return;
		}
		if ( nameLength >= (int)sizeof( name ) ) {
			return;
		}
		Com_Memcpy( name, payload, nameLength );
		name[nameLength] = '\0';
		if ( Cvar_Flags( name ) & ( CVAR_PRIVATE | CVAR_PROTECTED ) ) {
			return;
		}

		valueStart++;
		Q_strncpyz( value, valueStart, sizeof( value ) );
		Cvar_Set( name, value );
		CL_WebHost_UpdateBrowserCvarCache( name, value );
		return;
	}

	if ( !Q_stricmp( kind, "reset" ) ) {
		char name[MAX_CVAR_VALUE_STRING];
		char value[MAX_CVAR_VALUE_STRING];

		if ( !payload[0] || strlen( payload ) >= sizeof( name ) ) {
			return;
		}
		Q_strncpyz( name, payload, sizeof( name ) );
		if ( Cvar_Flags( name ) & ( CVAR_PRIVATE | CVAR_PROTECTED ) ) {
			return;
		}
		Cvar_Reset( name );
		Cvar_VariableStringBuffer( name, value, sizeof( value ) );
		CL_WebHost_UpdateBrowserCvarCache( name, value );
		return;
	}

	if ( !Q_stricmp( kind, "exists" ) ) {
		char path[MAX_QPATH];
		qboolean exists;

		if ( !payload[0] || strlen( payload ) >= sizeof( path ) ) {
			return;
		}

		Q_strncpyz( path, payload, sizeof( path ) );
		exists = CL_WebHost_FileExists( path );
		CL_WebHost_UpdateBrowserFileExistsCache( path, exists );
		return;
	}

	if ( !Q_stricmp( kind, "write" ) ) {
		const char *contentsStart;
		char path[MAX_QPATH];
		int pathLength;
		qboolean wrote;

		contentsStart = strchr( payload, '\n' );
		if ( !contentsStart ) {
			return;
		}

		pathLength = (int)( contentsStart - payload );
		if ( pathLength <= 0 ) {
			return;
		}
		if ( pathLength >= (int)sizeof( path ) ) {
			return;
		}
		Com_Memcpy( path, payload, pathLength );
		path[pathLength] = '\0';

		contentsStart++;
		wrote = CL_WebHost_WriteTextFile( path, contentsStart );
		CL_WebHost_UpdateBrowserFileExistsCache( path, wrote ? qtrue : CL_WebHost_FileExists( path ) );
		return;
	}

	if ( !Q_stricmp( kind, "clipget" ) ) {
		char clipboardText[MAX_STRING_CHARS];

		CL_WebHost_ReadClipboardText( clipboardText, sizeof( clipboardText ) );
		CL_WebHost_UpdateBrowserClipboardCache( clipboardText );
		return;
	}

	if ( !Q_stricmp( kind, "clipset" ) ) {
		CL_WebHost_SetClipboardText( payload );
		return;
	}

	if ( !Q_stricmp( kind, "openurl" ) ) {
		CL_WebHost_OpenRequestedURL( payload, "qz.OpenURL" );
		return;
	}

	if ( !Q_stricmp( kind, "opensteamurl" ) ) {
		CL_Steam_OpenOverlayUrl( payload );
		return;
	}

	if ( !Q_stricmp( kind, "servers" ) ) {
		CL_WebHost_RequestServers( payload );
		return;
	}

	if ( !Q_stricmp( kind, "refreshservers" ) ) {
		CL_Steam_RefreshServerList();
		return;
	}

	if ( !Q_stricmp( kind, "serverdetails" ) ) {
		CL_WebHost_RequestServerDetails( payload );
		return;
	}

	if ( !Q_stricmp( kind, "favorite" ) ) {
		CL_WebHost_SetFavoriteServer( payload );
		return;
	}

	if ( CL_WebHost_HandleSocialBridgeRequest( kind, payload ) ) {
		return;
	}

	if ( !Q_stricmp( kind, "demos" ) ) {
		char demoJson[CL_WEB_EVENT_PAYLOAD_LENGTH];

		CL_WebHost_BuildDemoListJson( demoJson, sizeof( demoJson ) );
		CL_WebHost_UpdateBrowserDemoList( demoJson );
		return;
	}

	if ( !Q_stricmp( kind, "maps" ) ) {
		char mapJson[CL_WEB_CATALOG_JSON_LENGTH];

		CL_WebHost_BuildMapListJson( mapJson, sizeof( mapJson ) );
		CL_WebHost_UpdateBrowserMapList( mapJson );
		cl_webui.mapCatalogSynced = qtrue;
		return;
	}

	if ( !Q_stricmp( kind, "factories" ) ) {
		char factoryJson[CL_WEB_CATALOG_JSON_LENGTH];

		CL_WebHost_BuildFactoryListJson( factoryJson, sizeof( factoryJson ) );
		CL_WebHost_UpdateBrowserFactoryList( factoryJson );
		cl_webui.factoryCatalogSynced = qtrue;
		return;
	}

	if ( !Q_stricmp( kind, "friends" ) ) {
		char friendJson[CL_WEB_CONFIG_JSON_LENGTH];

		CL_WebHost_BuildFriendListJson( friendJson, sizeof( friendJson ) );
		CL_WebHost_UpdateBrowserFriendList( friendJson );
		return;
	}

	if ( !Q_stricmp( kind, "keycapture" ) ) {
		cl_webui.keyCaptureArmed = ( !payload[0] || atoi( payload ) != 0 ) ? qtrue : qfalse;
	}
}

static void CL_WebHost_PumpNativeJavascriptRequests( void ) {
	qboolean handledRequest;

	if ( !CL_WebHost_HasLiveView() ) {
		return;
	}

	if ( cl_webui.frameSequence < cl_webui.nextNativeRequestPollFrame ) {
		return;
	}

	if ( CL_Awesomium_IsLoading() ) {
		cl_webui.nextNativeRequestPollFrame = cl_webui.frameSequence + CL_WEB_NATIVE_REQUEST_LOADING_POLL_FRAMES;
		return;
	}

	handledRequest = qfalse;
	for ( int i = 0; i < CL_WEB_NATIVE_REQUESTS_PER_FRAME; ++i ) {
		char request[MAX_STRING_CHARS];

		if ( !CL_Awesomium_PopJavascriptRequest( request, sizeof( request ) ) ) {
			break;
		}

		if ( request[0] ) {
			CL_WebHost_ProcessNativeJavascriptRequest( request );
			handledRequest = qtrue;
		}
	}

	cl_webui.nextNativeRequestPollFrame = cl_webui.frameSequence +
		( handledRequest ? CL_WEB_NATIVE_REQUEST_BUSY_POLL_FRAMES : CL_WEB_NATIVE_REQUEST_IDLE_POLL_FRAMES );
}

static void CL_WebView_DispatchLiveEvent( const char *name, const char *payload ) {
	char escapedName[CL_WEB_EVENT_NAME_LENGTH * 2];
	char *escapedPayload;
	char *script;
	int payloadLength;
	int escapedPayloadSize;
	int scriptSize;

	if ( !name || !name[0] || !CL_WebHost_CanDispatchLiveEvent() ) {
		return;
	}

	CL_WebUI_JsonEscape( name, escapedName, sizeof( escapedName ) );
	if ( payload && payload[0] ) {
		payloadLength = (int)strlen( payload );
		escapedPayloadSize = payloadLength * 2 + 1;
		escapedPayload = (char *)Z_Malloc( escapedPayloadSize );
		if ( !escapedPayload ) {
			return;
		}

		CL_WebUI_JsonEscape( payload, escapedPayload, escapedPayloadSize );
		scriptSize = (int)strlen( escapedName ) + escapedPayloadSize + 320;
		script = (char *)Z_Malloc( scriptSize );
		if ( !script ) {
			Z_Free( escapedPayload );
			return;
		}

		Com_sprintf(
			script,
			scriptSize,
			"(function(){if(window.EnginePublish){var p=\"%s\";var d;try{d=JSON.parse(p);}catch(e){d=p;}window.EnginePublish(\"%s\",d);}})();",
			escapedPayload,
			escapedName
		);
		Z_Free( escapedPayload );
	} else {
		scriptSize = (int)strlen( escapedName ) + 160;
		script = (char *)Z_Malloc( scriptSize );
		if ( !script ) {
			return;
		}

		Com_sprintf(
			script,
			scriptSize,
			"(function(){if(window.EnginePublish){window.EnginePublish(\"%s\",null);}})();",
			escapedName
		);
	}

	CL_Awesomium_ExecuteJavascript( script, "" );
	Z_Free( script );
}

static void CL_WebView_ReplayRetainedEvents( void ) {
	int firstIndex;
	int i;

	if ( !CL_WebHost_CanDispatchLiveEvent() ) {
		return;
	}

	firstIndex = cl_webui.eventHead - CL_WEB_BROWSER_EVENT_COUNT;
	if ( firstIndex < 0 ) {
		firstIndex = 0;
	}

	for ( i = firstIndex; i < cl_webui.eventHead; i++ ) {
		clWebUiBrowserEvent_t *event = &cl_webui.events[i % CL_WEB_BROWSER_EVENT_COUNT];

		if ( !event->name[0] || event->sequence <= cl_webui.lastReplayedEventSequence ) {
			continue;
		}

		CL_WebView_DispatchLiveEvent( event->name, event->payload );
		cl_webui.lastReplayedEventSequence = event->sequence;
	}
}

void CL_WebView_PublishEvent( const char *name, const char *payload ) {
	clWebUiBrowserEvent_t *event;
	int slot;

	if ( !name || !name[0] ) {
		return;
	}

	slot = cl_webui.eventHead % CL_WEB_BROWSER_EVENT_COUNT;
	event = &cl_webui.events[slot];
	cl_webui.eventHead++;
	cl_webui.eventSequence++;

	event->sequence = cl_webui.eventSequence;
	Q_strncpyz( event->name, name, sizeof( event->name ) );
	Q_strncpyz( event->payload, payload ? payload : "", sizeof( event->payload ) );

	if ( cl_webConsole && cl_webConsole->integer ) {
		Com_DPrintf( "WebUI event #%d %s %s\n", event->sequence, event->name, event->payload );
	}

	CL_WebView_DispatchLiveEvent( event->name, event->payload );
	if ( CL_WebHost_CanDispatchLiveEvent() ) {
		cl_webui.lastReplayedEventSequence = event->sequence;
	}
}

void CL_WebView_InvokeCommNotice( const char *message ) {
	CL_WebView_PublishEvent( "web.commNotice", message ? message : "" );
}

static void CL_WebView_AppendTaggedInfoPair( char *payload, size_t payloadSize, const char *key, const char *value, qboolean *first ) {
	char escapedKey[MAX_INFO_KEY * 2];
	char escapedValue[MAX_INFO_VALUE * 2];

	if ( !payload || payloadSize == 0 || !key || !key[0] || !first ) {
		return;
	}

	CL_WebUI_JsonEscape( key, escapedKey, sizeof( escapedKey ) );
	CL_WebUI_JsonEscape( value ? value : "", escapedValue, sizeof( escapedValue ) );
	CL_WebUI_AppendJsonFragment( payload, payloadSize, "%s\"%s\":\"%s\"", *first ? "" : ",", escapedKey, escapedValue );
	*first = qfalse;
}

void CL_WebView_PublishTaggedInfoString( const char *messageType, const char *infoString ) {
	char key[MAX_INFO_KEY];
	char value[MAX_INFO_VALUE];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];
	const char *cursor;
	qboolean first;

	payload[0] = '\0';
	first = qtrue;
	cursor = infoString ? infoString : "";

	CL_WebUI_AppendJsonFragment( payload, sizeof( payload ), "{" );
	CL_WebView_AppendTaggedInfoPair( payload, sizeof( payload ), "MSG_TYPE", messageType ? messageType : "", &first );
	while ( *cursor ) {
		cursor = Info_NextPair( cursor, key, value );
		if ( key[0] ) {
			CL_WebView_AppendTaggedInfoPair( payload, sizeof( payload ), key, value, &first );
		}
	}
	CL_WebUI_AppendJsonFragment( payload, sizeof( payload ), "}" );

	CL_WebView_InvokeCommNotice( payload );
}

void CL_WebView_PublishGameError( const char *message ) {
	char escapedMessage[MAXPRINTMSG * 2];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];

	CL_WebUI_JsonEscape( message ? message : "", escapedMessage, sizeof( escapedMessage ) );
	Com_sprintf( payload, sizeof( payload ), "{\"text\":\"%s\"}", escapedMessage );
	CL_WebView_PublishEvent( "game.error", payload );
	Cvar_Set( "com_errorMessage", "" );
}

void CL_WebView_PublishGameEnd( void ) {
	CL_WebView_PublishEvent( "game.end", NULL );
}

void CL_WebView_PublishCvarChange( const char *name, const char *value, qboolean replicate ) {
	char escapedName[MAX_CVAR_VALUE_STRING * 2];
	char escapedValue[MAX_CVAR_VALUE_STRING * 2];
	char eventName[CL_WEB_EVENT_NAME_LENGTH];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];

	if ( !name || !name[0] || ( Cvar_Flags( name ) & CVAR_PRIVATE ) ) {
		return;
	}

	if ( !Q_stricmp( name, "vid_xpos" ) || !Q_stricmp( name, "vid_ypos" ) ) {
		return;
	}

	CL_WebUI_JsonEscape( name, escapedName, sizeof( escapedName ) );
	CL_WebUI_JsonEscape( value ? value : "", escapedValue, sizeof( escapedValue ) );
	Com_sprintf( eventName, sizeof( eventName ), "cvar.%s", name );
	Com_sprintf( payload, sizeof( payload ), "{\"name\":\"%s\",\"value\":\"%s\",\"replicate\":%d}", escapedName, escapedValue, replicate ? 1 : 0 );
	CL_WebHost_UpdateBrowserCvarCache( name, value ? value : "" );
	CL_WebView_PublishEvent( eventName, payload );
}

void CL_WebView_PublishBindChanged( const char *name, const char *value ) {
	char escapedName[MAX_CVAR_VALUE_STRING * 2];
	char escapedValue[MAX_STRING_CHARS * 2];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];

	CL_WebUI_JsonEscape( name ? name : "", escapedName, sizeof( escapedName ) );
	CL_WebUI_JsonEscape( value ? value : "", escapedValue, sizeof( escapedValue ) );
	Com_sprintf( payload, sizeof( payload ), "{\"name\":\"%s\",\"value\":\"%s\"}", escapedName, escapedValue );
	CL_WebView_PublishEvent( "bind.changed", payload );
}

static unsigned int CL_WebView_PackAddressIP( const netadr_t *serverAddress ) {
	if ( !serverAddress ) {
		return 0u;
	}

	if ( serverAddress->type == NA_LOOPBACK ) {
		return ( 127u << 24 ) | 1u;
	}

	if ( serverAddress->type != NA_IP ) {
		return 0u;
	}

	return ( (unsigned int)serverAddress->ipv._4[0] << 24 )
		| ( (unsigned int)serverAddress->ipv._4[1] << 16 )
		| ( (unsigned int)serverAddress->ipv._4[2] << 8 )
		| (unsigned int)serverAddress->ipv._4[3];
}

void CL_WebView_PublishGameStartForAddress( const netadr_t *serverAddress ) {
	unsigned int packedIp = 0;
	unsigned int port = PORT_SERVER;
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];

	if ( clc.demoplaying || !serverAddress ) {
		return;
	}

	packedIp = CL_WebView_PackAddressIP( serverAddress );
	if ( serverAddress->type == NA_IP || serverAddress->type == NA_LOOPBACK ) {
		port = (unsigned int)BigShort( (short)serverAddress->port );
		if ( port == 0 ) {
			port = PORT_SERVER;
		}
	}

	Com_sprintf( payload, sizeof( payload ), "{\"ip\":%u,\"port\":%u}", packedIp, port );
	CL_WebView_PublishEvent( "game.start", payload );
}

void CL_WebView_PublishGameStart( void ) {
	const netadr_t *serverAddress = &clc.serverAddress;

	if ( serverAddress->type == NA_BAD ) {
		serverAddress = &clc.netchan.remoteAddress;
	}

	CL_WebView_PublishGameStartForAddress( serverAddress );
}

void CL_WebView_PublishGameDemo( const char *id, const char *name ) {
	char escapedId[MAX_QPATH * 2];
	char escapedName[MAX_QPATH * 2];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];

	CL_WebUI_JsonEscape( id ? id : "", escapedId, sizeof( escapedId ) );
	CL_WebUI_JsonEscape( name ? name : "", escapedName, sizeof( escapedName ) );
	Com_sprintf( payload, sizeof( payload ), "{\"id\":\"%s\",\"name\":\"%s\"}", escapedId, escapedName );
	CL_WebView_PublishEvent( "game.demo", payload );
}

void CL_WebView_PublishGameScreenshot( const char *id, const char *name ) {
	char escapedId[MAX_QPATH * 2];
	char escapedName[MAX_QPATH * 2];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];

	CL_WebUI_JsonEscape( id ? id : "", escapedId, sizeof( escapedId ) );
	CL_WebUI_JsonEscape( name ? name : "", escapedName, sizeof( escapedName ) );
	Com_sprintf( payload, sizeof( payload ), "{\"id\":\"%s\",\"name\":\"%s\"}", escapedId, escapedName );
	CL_WebView_PublishEvent( "game.screenshot", payload );
}

static void CL_WebView_PublishGameKey( int key ) {
	char escapedName[128];
	char payload[256];
	const char *keyName;

	keyName = Key_KeynumToString( key & ~K_CHAR_FLAG );
	CL_WebUI_JsonEscape( keyName ? keyName : "", escapedName, sizeof( escapedName ) );
	Com_sprintf( payload, sizeof( payload ), "{\"id\":%d,\"key\":\"%s\"}", key & ~K_CHAR_FLAG, escapedName );
	CL_WebView_PublishEvent( "game.key", payload );
}

static qboolean CL_WebHost_BrowserAcceptsInput( void ) {
	if ( !CL_WebUI_ServiceAvailable() ) {
		return qfalse;
	}
	if ( !CL_WebHost_HasDrawableSurface() ) {
		return qfalse;
	}

	return ( cl_webui.browserVisible && cl_webui.browserActive ) ? qtrue : qfalse;
}

static int CL_WebHost_MapMouseButton( int key ) {
	switch ( key ) {
		case K_MOUSE1:
			return 0;

		case K_MOUSE2:
			return 2;

		case K_MOUSE3:
			return 1;

		default:
			return -1;
	}
}

void CL_WebView_OnMouseMove( int x, int y ) {
	CL_WebHost_SetCursorPosition( x, y );

	if ( CL_WebHost_BrowserAcceptsInput() ) {
		CL_Awesomium_InjectMouseMove( x, y );
	}
}

void CL_WebView_OnMouseButtonEvent( int key, qboolean down ) {
	int button;

	if ( !CL_WebHost_BrowserAcceptsInput() ) {
		return;
	}

	button = CL_WebHost_MapMouseButton( key );
	if ( button < 0 ) {
		return;
	}

	if ( down ) {
		CL_Awesomium_InjectMouseDown( button );
	} else {
		CL_Awesomium_InjectMouseUp( button );
	}
}

void CL_WebView_OnMouseWheelEvent( int direction ) {
	if ( !CL_WebHost_BrowserAcceptsInput() || direction == 0 ) {
		return;
	}

	if ( CL_WebUI_ServiceAvailable() ) {
		CL_Awesomium_InjectMouseWheel( direction );
	}
}

static void CL_WebView_InjectActivationKeyboardEvent( void ) {
	if ( !CL_WebHost_HasLiveView() ) {
		return;
	}

	if ( cl_webui.browserVisible || cl_webui.browserActive ) {
		CL_Awesomium_InjectKeyboardEvent(
			CL_WEB_KEYBOARD_EVENT_ACTIVATION_TYPE,
			CL_WEB_KEYBOARD_EVENT_ACTIVATION_VIRTUAL_KEY,
			CL_WEB_KEYBOARD_EVENT_ACTIVATION_NATIVE_KEY );
	}
}

void CL_WebHost_NotifyAppActivation( qboolean active ) {
	cl_webui.appActive = active;

	if ( !active ) {
		return;
	}

	CL_WebView_InjectActivationKeyboardEvent();
}

void CL_WebView_OnKeyEvent( int key, qboolean down ) {
	if ( !CL_WebHost_BrowserAcceptsInput() ) {
		return;
	}

	if ( !down && cl_webui.keyCaptureArmed && !( key & K_CHAR_FLAG ) ) {
		CL_WebView_PublishGameKey( key );
		cl_webui.keyCaptureArmed = qfalse;
	}

	if ( CL_WebUI_ServiceAvailable() ) {
		if ( key & K_CHAR_FLAG ) {
			if ( down ) {
				CL_Awesomium_InjectKeyboardEvent( CL_WEB_KEYBOARD_EVENT_CHAR_TYPE, (unsigned int)( key & ~K_CHAR_FLAG ), 0 );
			}
		} else {
			CL_Awesomium_InjectKeyboardEvent(
				down ? CL_WEB_KEYBOARD_EVENT_KEYDOWN_TYPE : CL_WEB_KEYBOARD_EVENT_KEYUP_TYPE,
				(unsigned int)key,
				0 );
		}
	}
}

qboolean CL_Awesomium_RequestResource( const char *virtualPath, void **outBuffer, int *outLength ) {
	if ( outBuffer ) {
		*outBuffer = NULL;
	}
	if ( outLength ) {
		*outLength = 0;
	}

	if ( !CL_LauncherRequestData( virtualPath, outBuffer, outLength ) ) {
		CL_WebUI_SetLastError( "WebUI resource request could not be resolved.", 2 );
		return qfalse;
	}

	CL_WebUI_SetLastError( "", 0 );
	return qtrue;
}

static bool CL_WebUI_BackendRequestResource( void *, std::string_view virtualPath,
	fnql::webui::ResourceBuffer *resource ) noexcept {
	char path[MAX_STRING_CHARS];
	void *buffer = NULL;
	int length = 0;

	if ( resource ) {
		*resource = {};
	}
	if ( !resource || virtualPath.empty() || virtualPath.size() >= sizeof( path )
		|| memchr( virtualPath.data(), '\0', virtualPath.size() ) ) {
		return false;
	}

	Com_Memcpy( path, virtualPath.data(), virtualPath.size() );
	path[virtualPath.size()] = '\0';
	if ( !CL_LauncherRequestData( path, &buffer, &length )
		|| !buffer || length < 0 || length > CL_WEB_MAX_RESOURCE_BYTES ) {
		if ( buffer ) {
			Z_Free( buffer );
		}
		return false;
	}

	resource->bytes = static_cast<const std::uint8_t *>( buffer );
	resource->size = static_cast<size_t>( length );
	resource->releaseToken = buffer;
	return true;
}

static void CL_WebUI_BackendReleaseResource( void *,
	fnql::webui::ResourceBuffer *resource ) noexcept {
	if ( !resource ) {
		return;
	}
	if ( resource->releaseToken ) {
		Z_Free( resource->releaseToken );
	}
	*resource = {};
}

qboolean CL_Awesomium_Startup( const char *runtimePath, const char *basePath, const char *playerName, unsigned int appId, unsigned int steamIdLow, unsigned int steamIdHigh, int width, int height, const char *initialConfigJson, const char *initialMapJson, const char *initialFactoryJson ) {
	fnql::webui::StartupParameters parameters;
	parameters.runtimePath = runtimePath ? runtimePath : "";
	parameters.basePath = basePath ? basePath : "";
	parameters.playerName = playerName ? playerName : "";
	parameters.appId = appId;
	parameters.identityLow = steamIdLow;
	parameters.identityHigh = steamIdHigh;
	parameters.initialSurface = { width, height };
	parameters.hostServices.requestResource = CL_WebUI_BackendRequestResource;
	parameters.hostServices.releaseResource = CL_WebUI_BackendReleaseResource;
	parameters.initialConfigJson = initialConfigJson ? initialConfigJson : "";
	parameters.initialMapJson = initialMapJson ? initialMapJson : "";
	parameters.initialFactoryJson = initialFactoryJson ? initialFactoryJson : "";
	return CL_WebUI_RecordBackendResult(
		fnql::webui::ClientBackendHost().Start( parameters ) );
}

qboolean CL_Awesomium_OpenURL( const char *url ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( !host.IsAvailable() ) {
		CL_WebUI_SetLastError( "Awesomium runtime backend is unavailable in this build.", 1 );
		return qfalse;
	}
	if ( !CL_WebUI_RecordBackendResult( host.Navigate(
		( url && url[0] ) ? url : CL_WEB_DEFAULT_URL ) ) ) {
		return qfalse;
	}
	if ( !CL_WebUI_RecordBackendResult( host.SetRenderingPaused( false ) )
		|| !CL_WebUI_RecordBackendResult( host.SetFocus( true ) ) ) {
		return qfalse;
	}
	return qtrue;
}

void CL_Awesomium_Update( void ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( host.IsRunning() ) {
		CL_WebUI_RecordBackendResult( host.Pump() );
	}
}

qboolean CL_Awesomium_Resize( int width, int height ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	return host.IsRunning()
		? CL_WebUI_RecordBackendResult( host.Resize( { width, height } ) )
		: qfalse;
}

int CL_Awesomium_SurfaceWidth( void ) {
	const fnql::webui::BackendStatus status = fnql::webui::ClientBackendHost().Status();
	return status.HasSurface() ? status.surface.width : 0;
}

int CL_Awesomium_SurfaceHeight( void ) {
	const fnql::webui::BackendStatus status = fnql::webui::ClientBackendHost().Status();
	return status.HasSurface() ? status.surface.height : 0;
}

qboolean CL_Awesomium_SurfaceDirty( void ) {
	const fnql::webui::BackendStatus status = fnql::webui::ClientBackendHost().Status();
	return ( status.HasSurface() && status.surfaceDirty ) ? qtrue : qfalse;
}

qboolean CL_Awesomium_IsLoading( void ) {
	const fnql::webui::BackendStatus status = fnql::webui::ClientBackendHost().Status();
	return ( status.viewAlive && status.loading ) ? qtrue : qfalse;
}

qboolean CL_Awesomium_IsCrashed( void ) {
	return fnql::webui::ClientBackendHost().Status().crashed ? qtrue : qfalse;
}

int CL_Awesomium_LastErrorCode( void ) {
	const fnql::webui::BackendStatus status = fnql::webui::ClientBackendHost().Status();
	if ( status.nativeErrorCode != 0 ) {
		return status.nativeErrorCode;
	}
	return cl_webui.lastErrorCode;
}

qboolean CL_Awesomium_ExecuteJavascript( const char *script, const char *frame ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( !host.IsRunning() ) {
		return qfalse;
	}
	return CL_WebUI_RecordBackendResult(
		host.ExecuteScript( {
			script ? script : "",
			frame ? frame : ""
		} ) );
}

qboolean CL_Awesomium_ExecuteJavascriptInteger( const char *script, const char *frame, int *outValue ) {
	if ( outValue ) {
		*outValue = 0;
	}

	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( !host.IsRunning() ) {
		return qfalse;
	}
	fnql::webui::IntegerScriptResult result = host.EvaluateInteger( {
		script ? script : "",
		frame ? frame : ""
	} );
	if ( !CL_WebUI_RecordBackendResult( result.result ) ) {
		return qfalse;
	}
	if ( outValue ) {
		*outValue = result.value;
	}
	return qtrue;
}

qboolean CL_Awesomium_PopJavascriptRequest( char *buffer, int bufferSize ) {
	int length;
	int i;
	int outputLength = 0;

	if ( buffer && bufferSize > 0 ) {
		buffer[0] = '\0';
	}

	if ( !buffer || bufferSize <= 0 ) {
		return qfalse;
	}

	if ( !CL_Awesomium_ExecuteJavascriptInteger(
		"(function(){var q=window.__qlr_native_requests||[];if(!q.length){return -1;}window.__qlr_native_read=String(q.shift());return window.__qlr_native_read.length;})()",
		"",
		&length ) ) {
		return qfalse;
	}

	if ( length < 0 ) {
		return qfalse;
	}

	const auto rejectRequest = [buffer]( const char *reason ) {
		buffer[0] = '\0';
		(void)CL_Awesomium_ExecuteJavascriptInteger(
			"(function(){window.__qlr_native_read='';return 0;})()", "", nullptr );
		CL_WebUI_SetLastError( reason,
			static_cast<int>( fnql::webui::BackendError::InvalidArgument ) );
		return qfalse;
	};
	const auto readCodeUnit = []( int index, int *codeUnit ) {
		char script[160];
		Com_sprintf(
			script,
			sizeof( script ),
			"(function(){var s=window.__qlr_native_read||'';return s.charCodeAt(%d)||0;})()",
			index
		);
		return CL_Awesomium_ExecuteJavascriptInteger( script, "", codeUnit ) != qfalse;
	};

	if ( length >= bufferSize ) {
		return rejectRequest( "WebUI native request exceeded the bridge buffer" );
	}

	for ( i = 0; i < length; ++i ) {
		int codeUnit;
		std::uint32_t scalar;
		unsigned char encoded[4];
		int encodedLength;

		if ( !readCodeUnit( i, &codeUnit ) ) {
			buffer[0] = '\0';
			return qfalse;
		}
		if ( codeUnit <= 0 || codeUnit > 0xffff ) {
			return rejectRequest( "WebUI native request contains an invalid UTF-16 code unit" );
		}

		scalar = static_cast<std::uint32_t>( codeUnit );
		if ( scalar >= 0xd800u && scalar <= 0xdbffu ) {
			int lowSurrogate;
			if ( ++i >= length || !readCodeUnit( i, &lowSurrogate )
				|| lowSurrogate < 0xdc00 || lowSurrogate > 0xdfff ) {
				return rejectRequest( "WebUI native request contains malformed UTF-16" );
			}
			scalar = 0x10000u + ( ( scalar - 0xd800u ) << 10u )
				+ ( static_cast<std::uint32_t>( lowSurrogate ) - 0xdc00u );
		} else if ( scalar >= 0xdc00u && scalar <= 0xdfffu ) {
			return rejectRequest( "WebUI native request contains malformed UTF-16" );
		}

		if ( scalar <= 0x7fu ) {
			encoded[0] = static_cast<unsigned char>( scalar );
			encodedLength = 1;
		} else if ( scalar <= 0x7ffu ) {
			encoded[0] = static_cast<unsigned char>( 0xc0u | ( scalar >> 6u ) );
			encoded[1] = static_cast<unsigned char>( 0x80u | ( scalar & 0x3fu ) );
			encodedLength = 2;
		} else if ( scalar <= 0xffffu ) {
			encoded[0] = static_cast<unsigned char>( 0xe0u | ( scalar >> 12u ) );
			encoded[1] = static_cast<unsigned char>( 0x80u | ( ( scalar >> 6u ) & 0x3fu ) );
			encoded[2] = static_cast<unsigned char>( 0x80u | ( scalar & 0x3fu ) );
			encodedLength = 3;
		} else {
			encoded[0] = static_cast<unsigned char>( 0xf0u | ( scalar >> 18u ) );
			encoded[1] = static_cast<unsigned char>( 0x80u | ( ( scalar >> 12u ) & 0x3fu ) );
			encoded[2] = static_cast<unsigned char>( 0x80u | ( ( scalar >> 6u ) & 0x3fu ) );
			encoded[3] = static_cast<unsigned char>( 0x80u | ( scalar & 0x3fu ) );
			encodedLength = 4;
		}

		if ( outputLength > bufferSize - 1 - encodedLength ) {
			return rejectRequest( "WebUI native request exceeds the UTF-8 bridge buffer" );
		}
		Com_Memcpy( buffer + outputLength, encoded, encodedLength );
		outputLength += encodedLength;
	}

	buffer[outputLength] = '\0';
	CL_Awesomium_ExecuteJavascript( "(function(){window.__qlr_native_read='';})()", "" );
	return qtrue;
}

qboolean CL_Awesomium_SetZoom( int zoomPercent ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	return host.IsRunning()
		? CL_WebUI_RecordBackendResult( host.SetZoom( zoomPercent ) )
		: qfalse;
}

void CL_Awesomium_PauseRendering( void ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( host.IsRunning() ) {
		CL_WebUI_RecordBackendResult( host.SetRenderingPaused( true ) );
	}
}

void CL_Awesomium_Unfocus( void ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( host.IsRunning() ) {
		CL_WebUI_RecordBackendResult( host.SetFocus( false ) );
	}
}

qboolean CL_Awesomium_CopySurface( byte *destination, int width, int height, int rowSpan ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( !host.IsRunning() ) {
		return qfalse;
	}
	if ( !destination || width <= 0 || height <= 0 || rowSpan <= 0 ) {
		return CL_WebUI_RecordBackendResult( fnql::webui::BackendResult::Failure(
			fnql::webui::BackendError::InvalidArgument,
			"WebUI destination surface is invalid" ) );
	}

	const size_t stride = static_cast<size_t>( rowSpan );
	const size_t rows = static_cast<size_t>( height );
	if ( rows > ( std::numeric_limits<size_t>::max )() / stride ) {
		return CL_WebUI_RecordBackendResult( fnql::webui::BackendResult::Failure(
			fnql::webui::BackendError::InvalidArgument,
			"WebUI destination surface size overflows" ) );
	}

	fnql::webui::MutableSurface surface;
	surface.pixels = destination;
	surface.capacity = stride * rows;
	surface.rowStride = stride;
	surface.size = { width, height };
	return CL_WebUI_RecordBackendResult( host.CopySurface( surface ) );
}

void CL_Awesomium_InjectMouseMove( int x, int y ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( host.IsRunning() ) {
		CL_WebUI_RecordBackendResult( host.InjectMouseMove( { x, y } ) );
	}
}

static qboolean CL_Awesomium_MapMouseButton( int button,
	fnql::webui::MouseButton *mappedButton ) {
	if ( !mappedButton ) {
		return qfalse;
	}

	switch ( button ) {
		case 0:
			*mappedButton = fnql::webui::MouseButton::Left;
			return qtrue;
		case 1:
			*mappedButton = fnql::webui::MouseButton::Middle;
			return qtrue;
		case 2:
			*mappedButton = fnql::webui::MouseButton::Right;
			return qtrue;
		default:
			return qfalse;
	}
}

void CL_Awesomium_InjectMouseDown( int button ) {
	fnql::webui::MouseButton mappedButton;
	if ( !CL_Awesomium_MapMouseButton( button, &mappedButton ) ) {
		return;
	}

	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( host.IsRunning() ) {
		CL_WebUI_RecordBackendResult( host.InjectMouseButton( {
			mappedButton, fnql::webui::ButtonAction::Press } ) );
	}
}

void CL_Awesomium_InjectMouseUp( int button ) {
	fnql::webui::MouseButton mappedButton;
	if ( !CL_Awesomium_MapMouseButton( button, &mappedButton ) ) {
		return;
	}

	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( host.IsRunning() ) {
		CL_WebUI_RecordBackendResult( host.InjectMouseButton( {
			mappedButton, fnql::webui::ButtonAction::Release } ) );
	}
}

void CL_Awesomium_InjectMouseWheel( int direction ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( host.IsRunning() && direction != 0 ) {
		CL_WebUI_RecordBackendResult( host.InjectMouseWheel( { 0, direction } ) );
	}
}

void CL_Awesomium_InjectKeyboardEvent( unsigned int eventType, unsigned int virtualKeyCode, long nativeKeyCode ) {
	fnql::webui::KeyboardEventType type;
	switch ( eventType ) {
		case CL_WEB_KEYBOARD_EVENT_KEYDOWN_TYPE:
			type = fnql::webui::KeyboardEventType::KeyDown;
			break;
		case CL_WEB_KEYBOARD_EVENT_KEYUP_TYPE:
			type = fnql::webui::KeyboardEventType::KeyUp;
			break;
		case CL_WEB_KEYBOARD_EVENT_CHAR_TYPE:
			type = fnql::webui::KeyboardEventType::Character;
			break;
		default:
			return;
	}

	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( host.IsRunning() ) {
		CL_WebUI_RecordBackendResult( host.InjectKeyboard( {
			type,
			static_cast<std::uint32_t>( virtualKeyCode ),
			static_cast<std::intptr_t>( nativeKeyCode )
		} ) );
	}
}

void CL_Awesomium_Stop( void ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( host.IsRunning() ) {
		CL_WebUI_RecordBackendResult( host.StopLoading() );
	}
}

void CL_Awesomium_ClearCache( void ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( host.IsRunning() ) {
		CL_WebUI_RecordBackendResult( host.ClearCache() );
	}
}

void CL_Awesomium_Reload( qboolean ignoreCache ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	if ( host.IsRunning() ) {
		CL_WebUI_RecordBackendResult( host.Reload( ignoreCache != qfalse ) );
	}
}

void CL_Awesomium_Shutdown( void ) {
	fnql::webui::ClientBackendHost().Shutdown();
}

const char *CL_Awesomium_LastError( void ) {
	return cl_webui.lastError;
}
