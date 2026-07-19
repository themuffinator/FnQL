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
#include "../server/server.h"
#include "awesomium_backend_win32.hpp"
#include "webui_backend.hpp"
#include "../platform/fnql_steam.h"
#include "../platform/fnql_steam_stats.hpp"

extern "C" {
#include "../qcommon/unzip.h"
}

#include <cstdint>
#include <array>
#include <limits>
#include <string>

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
// Reserve the retail DataPak before renderer/driver startup; the view is
// resized to renderer-compatible, aspect-preserving dimensions on the first
// client frame.
#define CL_WEB_BOOTSTRAP_WIDTH 1280
#define CL_WEB_BOOTSTRAP_HEIGHT 720
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
#define CL_WEB_FRIEND_JSON_LENGTH 262144
#define CL_WEB_CATALOG_JSON_LENGTH 65536
#define CL_WEB_CATALOG_FILE_LIST_LENGTH 32768
#define CL_WEB_CATALOG_SYNC_CHUNK_CHARS 8192
#define CL_WEB_STARTUP_SCRIPT_LENGTH 65536
#define CL_WEB_AWESOMIUM_PRELOAD_MAX_LENGTH 16384
#define CL_WEB_SERVER_LOCAL_REFRESH_WAIT_MSEC 1000
#define CL_WEB_SERVER_REMOTE_REFRESH_WAIT_MSEC 5000
#define CL_WEB_SERVER_REFRESH_TIMEOUT_MSEC 15000
#define CL_WEB_SERVER_DETAILS_TIMEOUT_MSEC 5000
#define CL_STEAM_FRIEND_FLAGS 4u
#define CL_STEAM_MAX_FRIENDS 512u
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
	qboolean	fnqlOverlayChecked;
	qboolean	fnqlOverlayAvailable;
	qboolean	fnqlOverlayInjected;
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
	qboolean	serverRefreshSteam;
	int			serverRefreshRequestMode;
	int			serverRefreshSource;
	int			serverRefreshTime;
	int			serverRefreshTimeoutTime;
	qboolean	serverDetailActive;
	qboolean	serverDetailSteam;
	netadr_t	serverDetailAddress;
	unsigned int serverDetailIp;
	unsigned short serverDetailPort;
	int			serverDetailTimeoutTime;
	char		serverDetailId[32];
	uint64_t	activeLobbyId;
	qboolean	cursorPositionValid;
	int			cursorX;
	int			cursorY;
	char		tooltip[MAX_QPATH];
	byte		*surfacePixels;
	size_t		surfaceBytes;
	fnql::webui::SurfaceSize surfaceSize;
	fnql::webui::SurfaceSize requestedSurfaceSize;
	qboolean	surfaceCopied;
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

void CL_WebHost_InvalidateFactoryCatalog( void ) {
	cl_webui.factoryCatalogSynced = qfalse;
}

static void CL_WebHost_UpdateOverlayOwnership( void );
static void CL_WebHost_EnsureStartupBridge( void );
static void CL_WebHost_EnsureFnqlOverlay( void );
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
static void CL_Steam_OnProviderEvent( const fnqlSteamEvent_t *event, void *context );
static qboolean CL_Steam_ResultAccepted( fnqlSteamResult_t result );
static qboolean CL_Steam_ParseIdentity( const char *text, uint64_t *identity );
static void CL_WebHost_BuildConfigJson( char *buffer, size_t bufferSize );
static void CL_WebHost_BuildMapListJson( char *buffer, size_t bufferSize );
static char *CL_WebHost_AllocateFactoryListJson( void );
static void CL_WebHost_BuildDemoListJson( char *buffer, size_t bufferSize );
static void CL_WebHost_BuildFriendListJson( char *buffer, size_t bufferSize );
static void CL_WebHost_ReadClipboardText( char *buffer, size_t bufferSize );
static qboolean CL_WebUI_EnsureBackendStarted( void );

static void CL_WebUI_FreeSurfaceBuffer( void ) {
	if ( cl_webui.surfacePixels ) {
		Z_Free( cl_webui.surfacePixels );
	}
	cl_webui.surfacePixels = NULL;
	cl_webui.surfaceBytes = 0;
	cl_webui.surfaceSize = {};
	cl_webui.surfaceCopied = qfalse;
}

static void CL_WebUI_SetCvarIfChanged( const char *name, const char *value ) {
	const char *current;

	if ( !name || !value ) {
		return;
	}

	current = Cvar_VariableString( name );
	if ( strcmp( current, value ) ) {
		// These are engine-owned status cvars and several are intentionally ROM
		// to the console and UI.  Internal state publication must bypass that
		// user-facing protection or the failed set is retried every frame.
		Cvar_Set2( name, value, qtrue );
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
	return FNQL_Steam_Available( FNQL_STEAM_CAP_CLIENT ) ? "FnQL-Steam" : "Unavailable";
}

static const char *CL_WebHost_OnlineServicesPolicyLabel( void ) {
	return FNQL_Steam_Available( FNQL_STEAM_CAP_CLIENT )
		? "explicit-external-provider" : "compatibility-unavailable";
}

static const char *CL_WebHost_OnlineServicesParityScopeLabel( void ) {
	return "retail Steam online-services bridge";
}

static const char *CL_WebHost_OnlineServicesParityReasonLabel( void ) {
	return FNQL_Steam_Available( FNQL_STEAM_CAP_CLIENT )
		? "versioned external provider active; retail Steam runtime remains external"
		: "external Steam provider is disabled or unavailable";
}

static const char *CL_WebHost_MatchmakingProviderLabel( void ) {
	return FNQL_Steam_Available( FNQL_STEAM_CAP_LOBBIES ) ? "FnQL-Steam" : "Unavailable";
}

static const char *CL_WebHost_MatchmakingPolicyLabel( void ) {
	return FNQL_Steam_Available( FNQL_STEAM_CAP_LOBBIES )
		? "external-provider" : "Steamworks bridge unavailable";
}

static const char *CL_WebHost_WorkshopProviderLabel( void ) {
	return FNQL_Steam_Available( FNQL_STEAM_CAP_UGC ) ? "FnQL-Steam" : "Unavailable";
}

static const char *CL_WebHost_WorkshopPolicyLabel( void ) {
	return FNQL_Steam_Available( FNQL_STEAM_CAP_UGC )
		? "external-provider" : "Steamworks bridge unavailable";
}

static const char *CL_WebHost_ResourceBridgeProviderLabel( void ) {
	return FNQL_Steam_Available( FNQL_STEAM_CAP_AVATARS )
		? "FnQL-Steam" : "Unavailable";
}

static const char *CL_WebHost_ResourceBridgePolicyLabel( void ) {
	return FNQL_Steam_Available( FNQL_STEAM_CAP_AVATARS )
		? "avatar-provider" : "Steamworks resource bridge unavailable";
}

static const char *CL_WebHost_ResourceBridgeParityScopeLabel( void ) {
	return "retail Steam resource and avatar bridge";
}

static const char *CL_WebHost_ResourceBridgeParityReasonLabel( void ) {
	return FNQL_Steam_Available( FNQL_STEAM_CAP_AVATARS )
		? "Steam avatar PNG data source and renderer bridge active; non-avatar SteamDataSource resources remain unavailable"
		: "external Steam avatar provider is disabled or unavailable";
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

static qboolean CL_Steam_GetLocalIdentityWords( unsigned int *identityLow,
	unsigned int *identityHigh ) {
	fnqlSteamStatus_t status = {};

	if ( identityLow ) {
		*identityLow = 0u;
	}
	if ( identityHigh ) {
		*identityHigh = 0u;
	}
	status.size = sizeof( status );
	if ( !identityLow || !identityHigh
		|| !FNQL_Steam_Available( FNQL_STEAM_CAP_IDENTITY )
		|| !FNQL_Steam_GetStatus( &status ) || !status.local_steam_id ) {
		return qfalse;
	}
	*identityLow = (unsigned int)( status.local_steam_id & 0xffffffffull );
	*identityHigh = (unsigned int)( status.local_steam_id >> 32 );
	return qtrue;
}

static void CL_Steam_GetLocalDisplayName( char *buffer, size_t bufferSize ) {
	fnqlSteamStatus_t status = {};

	if ( !buffer || bufferSize == 0 ) {
		return;
	}
	buffer[0] = '\0';
	status.size = sizeof( status );
	if ( FNQL_Steam_Available( FNQL_STEAM_CAP_IDENTITY )
		&& FNQL_Steam_GetStatus( &status ) && status.persona_name[0] ) {
		Q_strncpyz( buffer, status.persona_name, (int)bufferSize );
		return;
	}
	Cvar_VariableStringBuffer( "name", buffer, (int)bufferSize );
}

static fnql::webui::SurfaceSize CL_WebUI_SurfaceSizeForViewport(
	int width, int height ) {
	const fnql::webui::SurfaceSize viewport = {
		width > 0 ? width : CL_WEB_BOOTSTRAP_WIDTH,
		height > 0 ? height : CL_WEB_BOOTSTRAP_HEIGHT
	};

	// The inherited renderer image pipeline caps dynamic textures at the
	// reported maximum texture dimension. If the Awesomium surface is larger,
	// its first paint is downscaled but later full-size sub-image updates are
	// rejected, leaving the bootstrap-black texture on screen. Render the
	// offscreen document within that limit and scale its quad to the viewport;
	// browser input already maps between the two coordinate spaces.
	return viewport.ConstrainedTo( cls.glconfig.maxTextureSize );
}

static qboolean CL_WebUI_EnsureBackendStarted( void ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	char runtimePath[MAX_OSPATH];
	char basePath[MAX_OSPATH];
	char retailPath[MAX_OSPATH];
	char playerName[MAX_CVAR_VALUE_STRING];
	cgameClientIdentity_t identity;
	unsigned int identityLow = 0u;
	unsigned int identityHigh = 0u;
	char *configJson;
	char *friendJson;
	char *mapJson;
	char *factoryJson;
	qboolean started;
	fnql::webui::SurfaceSize initialSurfaceSize;

	if ( host.IsRunning() ) {
		return qtrue;
	}
	if ( !CL_WebUI_RuntimeRequested() || !host.IsAvailable() ) {
		return qfalse;
	}
	initialSurfaceSize = CL_WebUI_SurfaceSizeForViewport(
		cls.glconfig.vidWidth, cls.glconfig.vidHeight );

	runtimePath[0] = '\0';
	basePath[0] = '\0';
	retailPath[0] = '\0';
	playerName[0] = '\0';
	Cvar_VariableStringBuffer( "fs_homepath", runtimePath, sizeof( runtimePath ) );
	Cvar_VariableStringBuffer( "fs_basepath", basePath, sizeof( basePath ) );
	Cvar_VariableStringBuffer( "fs_steampath", retailPath, sizeof( retailPath ) );
	CL_Steam_GetLocalDisplayName( playerName, sizeof( playerName ) );
	if ( CL_CopyClientIdentity( clc.clientNum, &identity ) ) {
		identityLow = identity.identityLow;
		identityHigh = identity.identityHigh;
	} else {
		CL_Steam_GetLocalIdentityWords( &identityLow, &identityHigh );
	}

	configJson = static_cast<char *>( Z_Malloc( CL_WEB_CONFIG_JSON_LENGTH ) );
	friendJson = static_cast<char *>( Z_Malloc( CL_WEB_FRIEND_JSON_LENGTH ) );
	mapJson = static_cast<char *>( Z_Malloc( CL_WEB_CATALOG_JSON_LENGTH ) );
	factoryJson = CL_WebHost_AllocateFactoryListJson();
	if ( !configJson || !friendJson || !mapJson || !factoryJson ) {
		if ( configJson ) {
			Z_Free( configJson );
		}
		if ( friendJson ) {
			Z_Free( friendJson );
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
	friendJson[0] = '\0';
	mapJson[0] = '\0';
	CL_WebHost_BuildConfigJson( configJson, CL_WEB_CONFIG_JSON_LENGTH );
	CL_WebHost_BuildFriendListJson( friendJson, CL_WEB_FRIEND_JSON_LENGTH );
	CL_WebHost_BuildMapListJson( mapJson, CL_WEB_CATALOG_JSON_LENGTH );
	started = CL_Awesomium_Startup(
		runtimePath,
		basePath,
		retailPath,
		playerName,
		(unsigned int)atoi( STEAMPATH_APPID ),
		identityLow,
		identityHigh,
		initialSurfaceSize.width,
		initialSurfaceSize.height,
		configJson,
		mapJson,
		factoryJson,
		friendJson );
	Z_Free( factoryJson );
	Z_Free( mapJson );
	Z_Free( friendJson );
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

static void CL_WebHost_InvalidateDocumentSnapshots( void ) {
	cl_webui.startupBridgeInjected = qfalse;
	cl_webui.fnqlOverlayChecked = qfalse;
	cl_webui.fnqlOverlayAvailable = qfalse;
	cl_webui.fnqlOverlayInjected = qfalse;
	cl_webui.nextBridgeRetryFrame = 0;
	cl_webui.nextNativeRequestPollFrame = 0;
	cl_webui.nextConfigSnapshotFrame = 0;
	cl_webui.configSnapshotSynced = qfalse;
	cl_webui.demoSnapshotSynced = qfalse;
	cl_webui.mapCatalogSynced = qfalse;
	cl_webui.factoryCatalogSynced = qfalse;
}

static void CL_WebUI_ClearBrowserState( void ) {
	CL_WebHost_ClearTooltip();
	CL_WebHost_ClearCursorOverride();
	cl_webui.browserVisible = qfalse;
	cl_webui.browserActive = qfalse;
	cl_webui.keyCaptureArmed = qfalse;
	cl_webui.cursorPositionValid = qfalse;
	CL_WebHost_InvalidateDocumentSnapshots();
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
	CL_WebUI_SetCvarIfChanged( "ui_resourceBridgeSteamDataSourceSubset",
		FNQL_Steam_Available( FNQL_STEAM_CAP_AVATARS ) ? "small medium and large avatars" : "none" );
	CL_WebUI_SetCvarIfChanged( "ui_resourceBridgeSteamDataSourceNativeGap",
		FNQL_Steam_Available( FNQL_STEAM_CAP_AVATARS )
			? "non-avatar SteamDataSource resources are unavailable"
			: "SteamDataSource and avatar callbacks are unavailable" );
	CL_WebUI_SetCvarIfChanged( "ui_resourceBridgeSteamDataSourceFallbackOwner", "Awesomium Steam DataSource and renderer shader registry" );
	CL_AdvertisementBridge_UpdateCvars();
	CL_WebHost_UpdateOverlayOwnership();
}

qboolean CL_IsSubscribedApp( int appId ) {
	if ( appId > 0 && FNQL_Steam_Available( FNQL_STEAM_CAP_CLIENT ) ) {
		return FNQL_Steam_IsSubscribedApp( (uint32_t)appId );
	}
	Com_DPrintf( "UI subscription bridge ignored for app %d: %s (%s [%s])\n",
		appId,
		"subscription bridge provider unavailable",
		CL_WebHost_OnlineServicesModeLabel(),
		CL_WebHost_OnlineServicesPolicyLabel() );
	return qfalse;
}

struct clSteamAvatarCacheEntry_t {
	uint64_t steamId;
	uint32_t size;
	qhandle_t shader;
};

static std::array<clSteamAvatarCacheEntry_t, 64> cl_steamAvatarCache;
static qboolean cl_steamVoiceRecording;
static qboolean cl_steamVoicePacketLogged;
static uint64_t cl_steamPendingP2PRemote;
static uint64_t cl_steamAcceptedP2PServer;
static uint32_t cl_steamAvatarGeneration = 1;

void CL_ClearAvatarImageHandles( void ) {
	cl_steamAvatarCache.fill( {} );
	if ( ++cl_steamAvatarGeneration == 0 ) {
		cl_steamAvatarGeneration = 1;
	}
}

static void CL_InvalidateAvatarImageHandle( uint64_t steamId ) {
	for ( clSteamAvatarCacheEntry_t &entry : cl_steamAvatarCache ) {
		if ( entry.steamId == steamId ) {
			entry = {};
		}
	}
	if ( ++cl_steamAvatarGeneration == 0 ) {
		cl_steamAvatarGeneration = 1;
	}
}

static qhandle_t CL_GetSteamAvatarImageHandle( uint64_t steamId,
		uint32_t avatarSize ) {
	clSteamAvatarCacheEntry_t *freeEntry = nullptr;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t required = 0;
	char shaderName[MAX_QPATH];
	byte *rgba;
	qhandle_t shader;

	if ( !steamId || avatarSize > FNQL_STEAM_AVATAR_LARGE
		|| !re.RegisterShaderFromRGBA
		|| !FNQL_Steam_Available( FNQL_STEAM_CAP_AVATARS ) ) {
		return 0;
	}
	for ( clSteamAvatarCacheEntry_t &entry : cl_steamAvatarCache ) {
		if ( entry.steamId == steamId && entry.size == avatarSize ) {
			return entry.shader;
		}
		if ( !entry.steamId && !freeEntry ) {
			freeEntry = &entry;
		}
	}
	if ( !freeEntry ) {
		return 0;
	}
	if ( FNQL_Steam_GetAvatarRGBA( steamId, avatarSize,
		NULL, 0, &width, &height, &required ) != FNQL_STEAM_RESULT_BUFFER_TOO_SMALL
		|| !width || !height || required != width * height * 4u
		|| required > 4u * 1024u * 1024u ) {
		return 0;
	}
	rgba = static_cast<byte *>( Z_Malloc( required ) );
	if ( !rgba ) {
		return 0;
	}
	if ( FNQL_Steam_GetAvatarRGBA( steamId, avatarSize,
		rgba, required, &width, &height, &required ) != FNQL_STEAM_RESULT_OK ) {
		Z_Free( rgba );
		return 0;
	}
	Com_sprintf( shaderName, sizeof( shaderName ), "*steam/avatar/%u/%u/%llu",
		cl_steamAvatarGeneration, avatarSize, (unsigned long long)steamId );
	shader = re.RegisterShaderFromRGBA( shaderName, rgba, (int)width, (int)height );
	Z_Free( rgba );
	if ( shader ) {
		freeEntry->steamId = steamId;
		freeEntry->size = avatarSize;
		freeEntry->shader = shader;
	}
	return shader;
}

qhandle_t CL_GetAvatarImageHandle( unsigned int identityLow, unsigned int identityHigh ) {
	return CL_GetSteamAvatarImageHandle(
		( (uint64_t)identityHigh << 32 ) | identityLow,
		FNQL_STEAM_AVATAR_LARGE );
}

qhandle_t CL_Steam_RegisterShader( const char *url ) {
	struct AvatarPrefix {
		const char *text;
		uint32_t size;
	};
	static const AvatarPrefix avatarPrefixes[] = {
		{ "asset://steam/avatar/small/", FNQL_STEAM_AVATAR_SMALL },
		{ "asset://steam/avatar/medium/", FNQL_STEAM_AVATAR_MEDIUM },
		{ "asset://steam/avatar/large/", FNQL_STEAM_AVATAR_LARGE },
		{ "steam://avatar/small/", FNQL_STEAM_AVATAR_SMALL },
		{ "steam://avatar/medium/", FNQL_STEAM_AVATAR_MEDIUM },
		{ "steam://avatar/large/", FNQL_STEAM_AVATAR_LARGE },
		// Retail web.pak uses the unqualified form for profile and friend rows.
		{ "asset://steam/avatar/", FNQL_STEAM_AVATAR_LARGE },
		{ "steam://avatar/", FNQL_STEAM_AVATAR_LARGE }
	};
	uint64_t avatarSteamId;

	if ( !url || !url[0] || !re.RegisterShaderNoMip ) {
		return 0;
	}
	for ( const AvatarPrefix &prefix : avatarPrefixes ) {
		const size_t prefixLength = strlen( prefix.text );
		if ( !Q_stricmpn( url, prefix.text, (int)prefixLength )
			&& CL_Steam_ParseIdentity( url + prefixLength, &avatarSteamId ) ) {
			return CL_GetSteamAvatarImageHandle( avatarSteamId, prefix.size );
		}
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
	cl_webuiEnable = Cvar_Get( "cl_webuiEnable",
#if defined( _WIN32 ) && ( defined( _M_IX86 ) || defined( __i386__ ) )
		"1",
#else
		"0",
#endif
		CVAR_ARCHIVE_ND );
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
	cvar_t *steamMaxLobbyClients = Cvar_Get( "steam_maxLobbyClients", "16", CVAR_ARCHIVE );
	Cvar_CheckRange( steamMaxLobbyClients, "1", "64", CV_INTEGER );
	Cvar_SetDescription( steamMaxLobbyClients, "Maximum members requested for a Steam friends lobby." );

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
	Cvar_SetDescription( Cvar_Get( "ui_resourceBridgeSteamDataSourceSubset", "small medium and large avatars", CVAR_ROM ), "Read-only supported SteamDataSource subset for Quake Live UI resources." );
	Cvar_SetDescription( Cvar_Get( "ui_resourceBridgeSteamDataSourceNativeGap", "missing non-avatar SteamDataSource owner", CVAR_ROM ), "Read-only native gap for Quake Live UI resource requests." );
	Cvar_SetDescription( Cvar_Get( "ui_resourceBridgeSteamDataSourceFallbackOwner", "Awesomium Steam DataSource and renderer shader registry", CVAR_ROM ), "Read-only fallback owner for non-URI UI shader requests." );
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
		"(function(){var h=\"%s\";if(window.location.hash.replace(/^#/,\"\")!==h){window.location.hash=h;}if(window.__fnql_retry_qz_bridge){window.__fnql_retry_qz_bridge();}})();",
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
	CL_WebHost_InvalidateDocumentSnapshots();

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
	CL_WebHost_InvalidateDocumentSnapshots();

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
	CL_WebHost_InvalidateDocumentSnapshots();
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

	// This verb is inherited from the native UI's server-list refresh flow.
	// Retail does not register it as an Awesomium navigation command. Stopping
	// the WebView here races index.html startup and can leave a complete but
	// empty document on renderers with a longer initialization path.
	Com_DPrintf( "web_stopRefresh ignored for the live WebUI document.\n" );
}

static void CL_Web_DumpSurface_f( void ) {
	const int width = cl_webui.surfaceSize.width;
	const int height = cl_webui.surfaceSize.height;
	size_t pixelCount;
	size_t outputSize;
	byte *output;
	byte *destination;
	size_t index;

	if ( !cl_webui.surfaceCopied || !cl_webui.surfacePixels
		|| width <= 0 || height <= 0 || width > 65535 || height > 65535 ) {
		Com_Printf( "WebUI surface is not available for capture.\n" );
		return;
	}

	pixelCount = static_cast<size_t>( width ) * static_cast<size_t>( height );
	if ( pixelCount > ( ( std::numeric_limits<size_t>::max )() - 18u ) / 3u
		|| pixelCount > static_cast<size_t>( ( std::numeric_limits<int>::max )() - 18 ) / 3u ) {
		Com_Printf( "WebUI surface is too large for capture.\n" );
		return;
	}

	outputSize = 18u + pixelCount * 3u;
	output = static_cast<byte *>( Z_Malloc( outputSize ) );
	if ( !output ) {
		Com_Printf( "Could not allocate the WebUI surface capture.\n" );
		return;
	}

	Com_Memset( output, 0, 18 );
	output[2] = 2; // Uncompressed true-colour TGA.
	output[12] = static_cast<byte>( width & 0xff );
	output[13] = static_cast<byte>( ( width >> 8 ) & 0xff );
	output[14] = static_cast<byte>( height & 0xff );
	output[15] = static_cast<byte>( ( height >> 8 ) & 0xff );
	output[16] = 24;
	output[17] = 0x20; // The Awesomium copy is top-to-bottom.
	destination = output + 18;
	for ( index = 0; index < pixelCount; ++index ) {
		const byte *source = cl_webui.surfacePixels + index * 4u;
		destination[index * 3u + 0u] = source[2];
		destination[index * 3u + 1u] = source[1];
		destination[index * 3u + 2u] = source[0];
	}

	FS_WriteFile( "screenshots/webui-surface.tga", output,
		static_cast<int>( outputSize ) );
	Z_Free( output );
	Com_Printf( "Wrote screenshots/webui-surface.tga\n" );
}

static void CL_Web_Status_f( void ) {
	struct Query {
		const char *label;
		const char *script;
	};
	static const Query queries[] = {
		{ "ready", "(function(){return document.readyState==='complete'?2:(document.readyState==='interactive'?1:0);})()" },
		{ "assetLocation", "(function(){return String(window.location.href).indexOf('asset://ql/index.html')===0?1:0;})()" },
		{ "blankLocation", "(function(){return String(window.location.href)==='about:blank'?1:0;})()" },
		{ "locationLength", "(function(){return String(window.location.href).length;})()" },
		{ "hidden", "(function(){return document.hidden?1:0;})()" },
		{ "headChildren", "(function(){return document.head?document.head.children.length:-1;})()" },
		{ "bodyChildren", "(function(){return document.body?document.body.children.length:-1;})()" },
		{ "bodyWidth", "(function(){return document.body?document.body.offsetWidth:-1;})()" },
		{ "bodyHeight", "(function(){return document.body?document.body.offsetHeight:-1;})()" },
		{ "bodyDisplayNone", "(function(){return document.body&&getComputedStyle(document.body).display==='none'?1:0;})()" },
		{ "bodyOpacity", "(function(){return document.body?Math.round(parseFloat(getComputedStyle(document.body).opacity||'0')*100):-1;})()" },
		{ "elements", "(function(){return document.getElementsByTagName('*').length;})()" },
		{ "images", "(function(){return document.images.length;})()" },
		{ "imagesLoaded", "(function(){var n=0;for(var i=0;i<document.images.length;i++){if(document.images[i].complete&&document.images[i].naturalWidth>0){n++;}}return n;})()" },
		{ "textLength", "(function(){return document.body?(document.body.textContent||'').length:-1;})()" },
		{ "titleLength", "(function(){return String(document.title||'').length;})()" },
		{ "app", "(function(){return document.getElementById('app')?1:0;})()" },
		{ "bridge", "(function(){return window.__qlr_qz_instance_script_complete?1:0;})()" },
		{ "qz", "(function(){return window.qz_instance?1:0;})()" },
		{ "qzSteamId", "(function(){return window.qz_instance&&String(window.qz_instance.steamId||'0')!=='0'?1:0;})()" },
		{ "qzPlayerNameLength", "(function(){return window.qz_instance?String(window.qz_instance.playerName||'').length:-1;})()" },
		{ "qzFriendCount", "(function(){try{var f=window.qz_instance&&window.qz_instance.GetFriendList?window.qz_instance.GetFriendList():null;return f&&typeof f.length!=='undefined'?f.length:-1;}catch(e){return -2;}})()" },
		{ "steamAvatarImages", "(function(){var n=0,a=document.images;for(var i=0;i<a.length;i++){if(String(a[i].src||'').indexOf('asset://steam/avatar/')===0){n++;}}return n;})()" },
		{ "mainHook", "(function(){return typeof window.main_hook_v2==='function'?1:0;})()" },
		{ "fnqlStyle", "(function(){return document.getElementById('fnql-settings-style')?1:0;})()" },
		{ "fnqlScript", "(function(){return window.__fnql_settings_script_loaded?1:0;})()" },
		{ "fnqlTab", "(function(){return document.querySelector('.fnql-settings-tab')?1:0;})()" }
	};

	if ( !CL_WebHost_HasLiveView() ) {
		Com_Printf( "WebUI status is unavailable without a live browser.\n" );
		return;
	}

	const fnql::webui::BackendStatus backendStatus =
		fnql::webui::ClientBackendHost().Status();
	Com_Printf( "WebUI document status: loading=%d crashed=%d nativeError=%d",
		backendStatus.loading ? 1 : 0,
		backendStatus.crashed ? 1 : 0,
		backendStatus.nativeErrorCode );
	for ( const Query &query : queries ) {
		int value = 0;
		if ( CL_Awesomium_ExecuteJavascriptInteger( query.script, "", &value ) ) {
			Com_Printf( " %s=%d", query.label, value );
		} else {
			Com_Printf( " %s=?", query.label );
		}
	}
	Com_Printf( "\n" );
	char location[256];
	int locationLength = 0;
	for ( ; locationLength + 1 < static_cast<int>( sizeof( location ) ); ++locationLength ) {
		char script[160];
		int codeUnit = 0;
		Com_sprintf( script, sizeof( script ),
			"(function(){return String(window.location.href).charCodeAt(%d)||0;})()",
			locationLength );
		if ( !CL_Awesomium_ExecuteJavascriptInteger( script, "", &codeUnit )
			|| codeUnit <= 0 || codeUnit > 0x7f ) {
			break;
		}
		location[locationLength] = static_cast<char>( codeUnit );
	}
	location[locationLength] = '\0';
	Com_Printf( "WebUI location: %s\n", location[0] ? location : "<unavailable>" );
}

static void CL_Steam_AvatarTest_f( void ) {
	uint64_t steamId;
	qhandle_t shader;
	uint32_t avatarSize = FNQL_STEAM_AVATAR_LARGE;
	const char *sizeName = "large";
	if ( ( Cmd_Argc() != 2 && Cmd_Argc() != 3 )
		|| !CL_Steam_ParseIdentity( Cmd_Argv( 1 ), &steamId ) ) {
		Com_Printf( "usage: steam_avatar_test <steamid64> [small|medium|large]\n" );
		return;
	}
	if ( Cmd_Argc() == 3 ) {
		sizeName = Cmd_Argv( 2 );
		if ( !Q_stricmp( sizeName, "small" ) ) avatarSize = FNQL_STEAM_AVATAR_SMALL;
		else if ( !Q_stricmp( sizeName, "medium" ) ) avatarSize = FNQL_STEAM_AVATAR_MEDIUM;
		else if ( Q_stricmp( sizeName, "large" ) ) {
			Com_Printf( "usage: steam_avatar_test <steamid64> [small|medium|large]\n" );
			return;
		}
	}
	shader = CL_GetSteamAvatarImageHandle( steamId, avatarSize );
	Com_Printf( "Steam %s avatar shader for %llu: %d\n", sizeName,
		(unsigned long long)steamId, shader );
}

static void CL_Steam_FriendsTest_f( void ) {
	char *snapshot = static_cast<char *>( Z_Malloc( CL_WEB_FRIEND_JSON_LENGTH ) );
	if ( !snapshot ) return;
	CL_WebHost_BuildFriendListJson( snapshot, CL_WEB_FRIEND_JSON_LENGTH );
	Com_Printf( "Steam friends snapshot: %s\n", snapshot );
	Z_Free( snapshot );
}

static void CL_Steam_UgcQueryTest_f( void ) {
	/* Retail forwards this value as Steam's one-based query page. */
	int filter = 1;
	if ( Cmd_Argc() > 2 ) {
		Com_Printf( "usage: steam_ugc_query_test [filter]\n" );
		return;
	}
	if ( Cmd_Argc() == 2 ) filter = atoi( Cmd_Argv( 1 ) );
	if ( CL_Steam_RequestAllUGC( filter ) ) {
		Com_Printf( "Steam UGC query requested with filter %d.\n", filter );
	}
}

static void CL_Steam_ClearStats_f( void ) {
	if ( FNQL_Steam_ResetClientStats( qtrue ) == FNQL_STEAM_RESULT_OK ) {
		Com_Printf( "Steam client stats and achievements reset.\n" );
	} else {
		Com_Printf( S_COLOR_YELLOW "Steam client stats reset is unavailable.\n" );
	}
}

static void CL_Steam_UserStatsTest_f( void ) {
	uint64_t steamId = 0;
	fnqlSteamStatus_t status = {};
	if ( Cmd_Argc() > 2 ) {
		Com_Printf( "usage: steam_user_stats_test [steamid64]\n" );
		return;
	}
	if ( Cmd_Argc() == 2 ) {
		if ( !CL_Steam_ParseIdentity( Cmd_Argv( 1 ), &steamId ) ) {
			Com_Printf( "usage: steam_user_stats_test [steamid64]\n" );
			return;
		}
	} else {
		status.size = sizeof( status );
		if ( FNQL_Steam_GetStatus( &status ) ) steamId = status.local_steam_id;
	}
	if ( steamId && FNQL_Steam_RequestClientUserStats( steamId )
		== FNQL_STEAM_RESULT_PENDING ) {
		Com_Printf( "Steam user stats requested for %llu.\n",
			(unsigned long long)steamId );
	} else {
		Com_Printf( S_COLOR_YELLOW "Steam user stats request is unavailable.\n" );
	}
}

static void CL_Steam_VoiceStart_f( void ) {
	if ( cl_steamVoiceRecording ) return;
	if ( FNQL_Steam_StartVoiceRecording() != FNQL_STEAM_RESULT_OK ) {
		Com_DPrintf( "Steam voice recording is unavailable.\n" );
		return;
	}
	cl_steamVoiceRecording = qtrue;
	cl_steamVoicePacketLogged = qfalse;
	(void)FNQL_Steam_SetLocalVoiceSpeaking( qtrue );
	CL_SetLocalSpeakingState( qtrue );
	Com_DPrintf( "Steam voice recording started at decoder rate %u.\n",
		FNQL_Steam_GetVoiceSampleRate() );
}

static void CL_Steam_VoiceStop_f( void ) {
	if ( !cl_steamVoiceRecording ) return;
	(void)FNQL_Steam_SetLocalVoiceSpeaking( qfalse );
	FNQL_Steam_StopVoiceRecording();
	cl_steamVoiceRecording = qfalse;
	CL_SetLocalSpeakingState( qfalse );
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
	Cmd_AddCommand( "web_dumpSurface", CL_Web_DumpSurface_f );
	Cmd_AddCommand( "web_status", CL_Web_Status_f );
	Cmd_AddCommand( "steam_avatar_test", CL_Steam_AvatarTest_f );
	Cmd_AddCommand( "steam_friends_test", CL_Steam_FriendsTest_f );
	Cmd_AddCommand( "steam_ugc_query_test", CL_Steam_UgcQueryTest_f );
	Cmd_AddCommand( "stats_clear", CL_Steam_ClearStats_f );
	Cmd_AddCommand( "steam_user_stats_test", CL_Steam_UserStatsTest_f );
	Cmd_AddCommand( "+voice", CL_Steam_VoiceStart_f );
	Cmd_AddCommand( "-voice", CL_Steam_VoiceStop_f );
	Cmd_AddCommand( "steam_voice_start", CL_Steam_VoiceStart_f );
	Cmd_AddCommand( "steam_voice_stop", CL_Steam_VoiceStop_f );
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
	Cmd_RemoveCommand( "web_dumpSurface" );
	Cmd_RemoveCommand( "web_status" );
	Cmd_RemoveCommand( "steam_avatar_test" );
	Cmd_RemoveCommand( "steam_friends_test" );
	Cmd_RemoveCommand( "steam_ugc_query_test" );
	Cmd_RemoveCommand( "stats_clear" );
	Cmd_RemoveCommand( "steam_user_stats_test" );
	Cmd_RemoveCommand( "+voice" );
	Cmd_RemoveCommand( "-voice" );
	Cmd_RemoveCommand( "steam_voice_start" );
	Cmd_RemoveCommand( "steam_voice_stop" );
	CL_Steam_VoiceStop_f();
	Cmd_RemoveCommand( "clientviewprofile" );
	Cmd_RemoveCommand( "clientfriendinvite" );
	cl_webui.commandsRegistered = qfalse;
}

void CL_WebHost_Init( void ) {
	const qboolean commandsRegistered = cl_webui.commandsRegistered;

	CL_WebUI_FreeSurfaceBuffer();
	Com_Memset( &cl_webui, 0, sizeof( cl_webui ) );
	cl_webui.initialized = qtrue;
	cl_webui.commandsRegistered = commandsRegistered;
	cl_webui.appActive = qtrue;
	cl_steamPendingP2PRemote = 0;
	cl_steamAcceptedP2PServer = 0;
	fnql::webui::InstallRetailAwesomiumBackend(
		fnql::webui::ClientBackendHost() );
	FNQL_Steam_SetEventSink( CL_Steam_OnProviderEvent, NULL );
	CL_WebUI_RegisterCvars();
	CL_RefreshOnlineServicesBridgeState();
}

void CL_WebHost_Shutdown( void ) {
	if ( !cl_webui.initialized ) {
		return;
	}
	if ( cl_steamAcceptedP2PServer ) {
		(void)FNQL_Steam_CloseP2PSession( FNQL_STEAM_ROLE_CLIENT,
			cl_steamAcceptedP2PServer );
	}
	cl_steamPendingP2PRemote = 0;
	cl_steamAcceptedP2PServer = 0;

	CL_Awesomium_Shutdown();
	FNQL_Steam_SetEventSink( NULL, NULL );
	CL_WebUI_ClearBrowserState();
	CL_WebUI_FreeSurfaceBuffer();
	CL_RefreshOnlineServicesBridgeState();
}

void CL_WebHost_RefreshSurfaceSize( void ) {
	if ( !cl_webui.initialized || !CL_WebUI_ServiceAvailable() ||
		!CL_WebHost_HasLiveView() || cls.glconfig.vidWidth <= 0 ||
		cls.glconfig.vidHeight <= 0 ) {
		return;
	}

	const fnql::webui::BackendStatus backendStatus =
		fnql::webui::ClientBackendHost().Status();
	const fnql::webui::SurfaceSize desired = CL_WebUI_SurfaceSizeForViewport(
		cls.glconfig.vidWidth, cls.glconfig.vidHeight );
	if ( backendStatus.surface == desired ) {
		cl_webui.requestedSurfaceSize = desired;
		return;
	}
	// Awesomium temporarily reports no surface after Resize(). Remember the
	// requested extent so a newer resize can supersede it without resubmitting
	// the same request every frame while the replacement surface is produced.
	if ( !backendStatus.surface.IsValid() &&
		cl_webui.requestedSurfaceSize == desired ) {
		return;
	}
	if ( CL_Awesomium_Resize( desired.width, desired.height ) ) {
		cl_webui.requestedSurfaceSize = desired;
	}
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
			CL_WebHost_RefreshSurfaceSize();
			if ( cl_webZoom && cl_webZoom->modified
				&& CL_Awesomium_SetZoom( cl_webZoom->integer ) ) {
				cl_webZoom->modified = qfalse;
			}
			CL_Awesomium_Update();
			if ( !CL_WebHost_HasLiveView() ) {
				CL_WebHost_MarkBrowserUnavailable();
			} else {
				CL_WebHost_EnsureFnqlOverlay();
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
	if ( !host.IsRunning() || !host.Status().HasSurface()
		|| !re.DrawWebUISurface ) {
		return qfalse;
	}
	return qtrue;
}

static qboolean CL_WebHost_CanDispatchLiveEvent( void ) {
	return ( CL_WebHost_HasLiveView() && CL_WebHost_HasBoundWindowObject() && CL_WebHost_HasDrawableSurface() ) ? qtrue : qfalse;
}

void CL_WebHost_DrawBrowserSurface( void ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();
	const fnql::webui::BackendStatus status = host.Status();
	size_t rowBytes;
	size_t requiredBytes;
	qboolean dirty;

	if ( !CL_WebHost_SurfaceReadyForOverlay() || !re.DrawWebUISurface
		|| !status.surface.MinimumRowBytes( &rowBytes )
		|| rowBytes > static_cast<size_t>( ( std::numeric_limits<int>::max )() )
		|| static_cast<size_t>( status.surface.height )
			> ( std::numeric_limits<size_t>::max )() / rowBytes ) {
		return;
	}

	requiredBytes = rowBytes * static_cast<size_t>( status.surface.height );
	if ( cl_webui.surfaceSize != status.surface
		|| cl_webui.surfaceBytes != requiredBytes ) {
		CL_WebUI_FreeSurfaceBuffer();
		cl_webui.surfacePixels = static_cast<byte *>( Z_Malloc( requiredBytes ) );
		if ( !cl_webui.surfacePixels ) {
			CL_WebUI_SetLastError( "WebUI compositor surface allocation failed.",
				static_cast<int>( fnql::webui::BackendError::ResourceUnavailable ) );
			return;
		}
		cl_webui.surfaceBytes = requiredBytes;
		cl_webui.surfaceSize = status.surface;
	}

	dirty = ( status.surfaceDirty || !cl_webui.surfaceCopied ) ? qtrue : qfalse;
	if ( dirty ) {
		if ( !CL_Awesomium_CopySurface(
			cl_webui.surfacePixels,
			status.surface.width,
			status.surface.height,
			static_cast<int>( rowBytes ) ) ) {
			return;
		}
		cl_webui.surfaceCopied = qtrue;
	}

	re.SetColor( NULL );
	re.DrawWebUISurface(
		0,
		0,
		cls.glconfig.vidWidth,
		cls.glconfig.vidHeight,
		status.surface.width,
		status.surface.height,
		cl_webui.surfacePixels,
		dirty );
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


/*
=============
CL_WebHost_ShowAfterDisconnect

Restores ownership of the already-loaded retail WebUI after game.end.  Game
transitions pause and unfocus the WebView, so publishing the lifecycle event is
not by itself sufficient to make the menu drawable or interactive.  Keep the
existing document alive (and therefore keep its post-game route and state)
instead of navigating it back to a hard-coded hash.

Returns false only when the WebUI cannot own the menu, allowing the caller to
activate the retail native UI module as a deterministic fallback.
=============
*/
qboolean CL_WebHost_ShowAfterDisconnect( void ) {
	fnql::webui::BackendHost &host = fnql::webui::ClientBackendHost();

	cl_webui.keyCaptureArmed = qfalse;
	cl_webui.browserVisible = qtrue;
	cl_webui.browserActive = qtrue;

	if ( !CL_WebUI_ServiceAvailable() || !CL_WebUI_EnsureBackendStarted()
		|| !host.IsRunning() ) {
		CL_WebHost_MarkBrowserUnavailable();
		return qfalse;
	}

	if ( !CL_WebUI_RecordBackendResult( host.SetRenderingPaused( false ) )
		|| !CL_WebUI_RecordBackendResult( host.SetFocus( true ) ) ) {
		CL_WebHost_MarkBrowserUnavailable();
		return qfalse;
	}

	// The browser supersedes the native main menu as soon as its retained
	// surface is ready. Pending surfaces remain explicitly visible in bridge
	// state and acquire KEYCATCH_BROWSER on the next WebUI frame.
	Key_SetCatcher( Key_GetCatcher() & ~KEYCATCH_UI );
	CL_RefreshOnlineServicesBridgeState();
	return qtrue;
}


/*
=============
CL_WebHost_HideForGameTransition

A connection or map load owns the screen.  Unlike an ordinary browser-hide
request, this must also cancel pending key-binding capture so it cannot retain
the browser surface or input while the native status screen is active.
=============
*/
void CL_WebHost_HideForGameTransition( void ) {
	cl_webui.keyCaptureArmed = qfalse;
	CL_WebHost_HideBrowser();
}

static std::string CL_WebHost_JsonParseExpression( const char *json ) {
	const unsigned char *cursor = reinterpret_cast<const unsigned char *>(
		json && json[0] ? json : "{}" );
	std::string expression = "JSON.parse('";
	for ( ; *cursor; ++cursor ) {
		if ( cursor[0] == 0xe2u && cursor[1] == 0x80u &&
			( cursor[2] == 0xa8u || cursor[2] == 0xa9u ) ) {
			expression += cursor[2] == 0xa8u ? "\\u2028" : "\\u2029";
			cursor += 2;
			continue;
		}
		switch ( *cursor ) {
		case '\\': expression += "\\\\"; break;
		case '\'': expression += "\\'"; break;
		case '\n': expression += "\\n"; break;
		case '\r': expression += "\\r"; break;
		case '\t': expression += "\\t"; break;
		default: expression.push_back( static_cast<char>( *cursor ) ); break;
		}
	}
	expression += "')";
	return expression;
}

static void CL_WebHost_BuildStartupBridgeScript( char *buffer, size_t bufferSize,
		const char *configJson, const char *factoryJson, const char *friendJson ) {
	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	Q_strncpyz(
		buffer,
		"(function(){"
		"if(window.__qlr_qz_instance_ready){if(window.__fnql_retry_qz_bridge){window.__fnql_retry_qz_bridge();}return;}"
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
		"var putOwn=function(o,k,v){Object.defineProperty(o,k,{value:v,writable:true,enumerable:true,configurable:true});return v;};"
		"var objectHasEntries=function(o){for(var k in o){if(hasOwn(o,k)){return true;}}return false;};"
		"var catalogObject=function(v){var out={};if(!v){return out;}if(Object.prototype.toString.call(v)!=='[object Array]'){return v;}for(var i=0;i<v.length;i++){var item=v[i]||{};var id=String(item.sysname||item.id||item.title||i);if(!id){continue;}if(!item.id){item.id=id;}if(!item.sysname){item.sysname=id;}putOwn(out,id,item);}return out;};"
		"var addCatalogObjects=function(target,v){var o=catalogObject(v);for(var k in o){if(hasOwn(o,k)){putOwn(target,k,o[k]);}}return true;};"
		"var addFactoryObjects=function(target,v){var i,k,item;if(Object.prototype.toString.call(v)==='[object Array]'){for(i=0;i<v.length;i++){item=v[i];if(item&&typeof item.id!=='undefined'){putOwn(target,String(item.id),item);}}return true;}for(k in v){if(hasOwn(v,k)){putOwn(target,k,v[k]);}}return true;};"
		"var setNativeCvar=function(n,v){var k=canon(n);if(k){config.cvars[k]=String(v||'');}return true;};"
		"var setFileExists=function(p,v){fileExistsCache[String(p||'')]=!!v;return true;};"
		"var setCursorPosition=function(x,y){cursorPosition={x:x|0,y:y|0};return true;};"
		"var setClipboardText=function(v){clipboardText=String(v||'');clipboardPrimed=true;return true;};"
		"var setDemoList=function(v){demoList=v||[];demoPrimed=true;return true;};"
		"var setFriendList=function(v){friendList=v||[];friendPrimed=true;config.friends=friendList;return true;};"
		"var setUGCList=function(v){ugcList=v||[];ugcPrimed=true;config.ugc=ugcList;return true;};"
		"var setMapList=function(v){var o=catalogObject(v);if(!objectHasEntries(o)){return false;}maps=o;mapPrimed=true;config.maps=maps;return true;};"
		"var setFactoryList=function(v){var o=catalogObject(v),k;for(k in factories){if(hasOwn(factories,k)){delete factories[k];}}for(k in o){if(hasOwn(o,k)){putOwn(factories,k,o[k]);}}factoryPrimed=true;config.factories=factories;return true;};"
		"var beginNativeMaps=function(){pendingNativeMaps={};return true;};var addNativeMaps=function(v){return addCatalogObjects(pendingNativeMaps,v);};var commitNativeMaps=function(){return setMapList(pendingNativeMaps);};"
		"var beginNativeFactories=function(){pendingNativeFactories={};return true;};var addNativeFactories=function(v){return addFactoryObjects(pendingNativeFactories,v);};var commitNativeFactories=function(){return setFactoryList(pendingNativeFactories);};"
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
		"GetAllUGC:function(filter){ugcPrimed=true;queueSocial('getallugc',String(typeof filter==='undefined'?1:filter));return ugcList;},GetNextKeyDown:function(active){if(typeof active==='undefined'){return queue('keycapture','1');}var activeText=String(active).toLowerCase();var activeValue=(active===false||activeText==='false')?0:parseInt(active,10);return queue('keycapture',((isNaN(activeValue)?1:activeValue)!==0?'1':'0'));},"
		"SetFavoriteServer:function(ip,port,add){var addText=String(add).toLowerCase();var addValue=(add===false||addText==='false')?0:parseInt(add,10);return queue('favorite',String(ip||'')+'\\n'+String(port||'')+'\\n'+((isNaN(addValue)?1:addValue)!==0?'1':'0'));},NoOp:noop};"
		"if(!window.FakeClient){window.FakeClient={};}if(!window.FakeClient.qz_instance){window.FakeClient.qz_instance={};}"
		"window.__qlr_set_native_cvar=setNativeCvar;window.__qlr_set_file_exists=setFileExists;window.__qlr_set_cursor_position=setCursorPosition;window.__qlr_set_clipboard_text=setClipboardText;window.__qlr_set_demo_list=setDemoList;window.__qlr_set_friend_list=setFriendList;window.__qlr_set_ugc_list=setUGCList;window.__qlr_set_native_maps=setMapList;window.__qlr_set_native_factories=setFactoryList;window.__qlr_begin_native_maps=beginNativeMaps;window.__qlr_add_native_maps=addNativeMaps;window.__qlr_commit_native_maps=commitNativeMaps;window.__qlr_begin_native_factories=beginNativeFactories;window.__qlr_add_native_factories=addNativeFactories;window.__qlr_commit_native_factories=commitNativeFactories;window.__qlr_set_native_state=setNativeState;window.__qlr_set_native_config=setNativeConfig;"
		"var syncQzBridge=function(){var f=window.FakeClient&&window.FakeClient.qz_instance;if(f){for(var k in qz){f[k]=qz[k];}}"
		"window.qz_instance=qz;if(typeof window.EnginePublish==='function'&&!window.EnginePublish.__qlr_wrapped){var oldPublish=window.EnginePublish;"
		"var wrapped=function(topic,data){try{if(String(topic).indexOf('cvar.')===0){var d=typeof data==='string'?JSON.parse(data):data;if(d){setNativeCvar(d.name||String(topic).substr(5),d.value||'');}}}catch(e){}return oldPublish.apply(this,arguments);};"
		"wrapped.__qlr_wrapped=true;window.EnginePublish=wrapped;}};"
		"window.__fnql_retry_qz_bridge=syncQzBridge;if(typeof window.main_hook_v2!=='function'){window.main_hook_v2=syncQzBridge;}syncQzBridge();if(document.addEventListener){document.addEventListener('DOMContentLoaded',syncQzBridge,false);}window.__qlr_browser_helpers_ready=true;"
		"try{var e=document.createEvent('Event');e.initEvent('qz_instance.ready',false,false);document.dispatchEvent(e);}catch(err){}"
		"var qlrBridgeTries=0;var qlrBridgeTimer=setInterval(function(){syncQzBridge();if(++qlrBridgeTries>=40){clearInterval(qlrBridgeTimer);}},250);"
		"window.__qlr_qz_instance_script_complete=true;"
		"})();",
		(int)bufferSize
	);

	// Retail document components synchronously read qz identity, factories, and
	// friends while mounting. Seed all three as data before those scripts run;
	// later snapshot synchronization remains authoritative for live changes.
	static const char configPrefix[] = "(function(){if(window.__qlr_set_native_config){window.__qlr_set_native_config(";
	static const char factoryPrefix[] = "(function(){if(window.__qlr_set_native_factories){window.__qlr_set_native_factories(";
	static const char friendPrefix[] = "(function(){if(window.__qlr_set_friend_list){window.__qlr_set_friend_list(";
	static const char suffix[] = ");}})();";
	const std::string configExpression = CL_WebHost_JsonParseExpression( configJson );
	const std::string factoryExpression = CL_WebHost_JsonParseExpression( factoryJson );
	const std::string friendExpression = CL_WebHost_JsonParseExpression(
		friendJson && friendJson[0] ? friendJson : "[]" );
	const qboolean hasFactory = factoryJson && factoryJson[0] ? qtrue : qfalse;
	const size_t required = strlen( buffer ) + strlen( configPrefix ) +
		configExpression.size() + strlen( suffix ) + strlen( friendPrefix ) +
		friendExpression.size() + strlen( suffix ) +
		( hasFactory ? strlen( factoryPrefix ) + factoryExpression.size() + strlen( suffix ) : 0u ) + 1;
	if ( required <= bufferSize ) {
		Q_strcat( buffer, (int)bufferSize, configPrefix );
		Q_strcat( buffer, (int)bufferSize, configExpression.c_str() );
		Q_strcat( buffer, (int)bufferSize, suffix );
		if ( hasFactory ) {
			Q_strcat( buffer, (int)bufferSize, factoryPrefix );
			Q_strcat( buffer, (int)bufferSize, factoryExpression.c_str() );
			Q_strcat( buffer, (int)bufferSize, suffix );
		}
		Q_strcat( buffer, (int)bufferSize, friendPrefix );
		Q_strcat( buffer, (int)bufferSize, friendExpression.c_str() );
		Q_strcat( buffer, (int)bufferSize, suffix );
	}
}

static char *CL_WebHost_AllocateStartupBridgeScript( const char *configJson,
		const char *factoryJson, const char *friendJson ) {
	const std::string configExpression = CL_WebHost_JsonParseExpression( configJson );
	const std::string factoryExpression = CL_WebHost_JsonParseExpression( factoryJson );
	const std::string friendExpression = CL_WebHost_JsonParseExpression(
		friendJson && friendJson[0] ? friendJson : "[]" );
	const size_t required = static_cast<size_t>( CL_WEB_STARTUP_SCRIPT_LENGTH ) +
		configExpression.size() + factoryExpression.size() + friendExpression.size() + 512u;
	if ( required > static_cast<size_t>( ( std::numeric_limits<int>::max )() ) ) {
		return NULL;
	}
	char *script = static_cast<char *>( Z_Malloc( static_cast<int>( required ) ) );
	if ( script ) {
		CL_WebHost_BuildStartupBridgeScript( script, required, configJson,
			factoryJson, friendJson );
	}
	return script;
}

static void CL_WebHost_BuildStartupBridgeRetryScript( char *buffer, size_t bufferSize ) {
	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	Q_strncpyz( buffer, "(function(){if(window.__fnql_retry_qz_bridge){window.__fnql_retry_qz_bridge();}})();", (int)bufferSize );
}

static qboolean CL_WebHost_InjectStartupBridge( qboolean retryOnly ) {
	char retryScript[MAX_STRING_CHARS];
	char *script = retryScript;
	qboolean executed;

	if ( !CL_WebHost_HasLiveView() ) {
		return qfalse;
	}

	if ( retryOnly ) {
		CL_WebHost_BuildStartupBridgeRetryScript(
			retryScript, sizeof( retryScript ) );
	} else {
		char configJson[CL_WEB_CONFIG_JSON_LENGTH];
		char *factoryJson = CL_WebHost_AllocateFactoryListJson();
		char *friendJson = static_cast<char *>( Z_Malloc( CL_WEB_FRIEND_JSON_LENGTH ) );
		CL_WebHost_BuildConfigJson( configJson, sizeof( configJson ) );
		if ( friendJson ) {
			CL_WebHost_BuildFriendListJson( friendJson, CL_WEB_FRIEND_JSON_LENGTH );
		}
		script = factoryJson && friendJson
			? CL_WebHost_AllocateStartupBridgeScript( configJson, factoryJson, friendJson ) : NULL;
		if ( factoryJson ) {
			Z_Free( factoryJson );
		}
		if ( friendJson ) {
			Z_Free( friendJson );
		}
		if ( !script ) {
			CL_WebUI_SetLastError( "WebUI bridge reinjection allocation failed.",
				static_cast<int>( fnql::webui::BackendError::ResourceUnavailable ) );
			return qfalse;
		}
	}

	executed = CL_Awesomium_ExecuteJavascript( script, "" );
	if ( !retryOnly ) {
		Z_Free( script );
	}
	if ( !executed ) {
		return qfalse;
	}

	if ( !retryOnly ) {
		cl_webui.startupBridgeInjected = qtrue;
		CL_WebHost_UpdateBrowserNativeState();
		CL_WebHost_SyncNativeSnapshots( qtrue );
		CL_WebView_ReplayRetainedEvents();
		CL_WebView_PublishEvent( "web.object.ready", NULL );
	}
	cl_webui.nextBridgeRetryFrame = cl_webui.frameSequence + CL_WEB_BRIDGE_RETRY_FRAMES;
	return qtrue;
}

static void CL_WebHost_EnsureStartupBridge( void ) {
	if ( !CL_WebHost_HasLiveView() || CL_Awesomium_IsLoading() ) {
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

static qboolean CL_WebHost_InjectFnqlOverlayAssets( void ) {
	void *settingsBuffer = NULL;
	void *styleBuffer = NULL;
	int settingsLength = 0;
	int styleLength = 0;
	char *escapedStyle = NULL;
	char *styleScript = NULL;
	qboolean result = qfalse;
	int styleInjected = 0;

	if ( !CL_LauncherRequestData( "asset://ql/fnql-settings.js",
		&settingsBuffer, &settingsLength ) || !settingsBuffer || settingsLength <= 0
		|| !CL_LauncherRequestData( "asset://ql/css/fnql-settings.css",
			&styleBuffer, &styleLength ) || !styleBuffer || styleLength <= 0
		|| styleLength > ( ( std::numeric_limits<int>::max )() - 512 ) / 2 ) {
		goto cleanup;
	}

	{
		const int escapedSize = styleLength * 2 + 1;
		const int scriptSize = escapedSize + 511;
		escapedStyle = static_cast<char *>( Z_Malloc( escapedSize ) );
		styleScript = static_cast<char *>( Z_Malloc( scriptSize ) );
		if ( !escapedStyle || !styleScript ) {
			goto cleanup;
		}
		CL_WebUI_JsonEscape( static_cast<const char *>( styleBuffer ),
			escapedStyle, static_cast<size_t>( escapedSize ) );
		Com_sprintf( styleScript, scriptSize,
			"(function(){var h=document.head;if(!h){return 0;}"
			"var s=document.getElementById('fnql-settings-style');"
			"if(!s){s=document.createElement('style');s.id='fnql-settings-style';"
			"s.type='text/css';h.appendChild(s);}s.textContent=\"%s\";return 1;})()",
			escapedStyle );
	}

	if ( !CL_Awesomium_ExecuteJavascriptInteger( styleScript, "", &styleInjected )
		|| !styleInjected
		|| !CL_Awesomium_ExecuteJavascript(
			static_cast<const char *>( settingsBuffer ), "" ) ) {
		goto cleanup;
	}
	result = qtrue;

cleanup:
	if ( styleScript ) Z_Free( styleScript );
	if ( escapedStyle ) Z_Free( escapedStyle );
	if ( styleBuffer ) Z_Free( styleBuffer );
	if ( settingsBuffer ) Z_Free( settingsBuffer );
	return result;
}

static void CL_WebHost_EnsureFnqlOverlay( void ) {
	int documentReady = 0;

	if ( cl_webui.fnqlOverlayInjected || !CL_WebHost_HasLiveView()
		|| !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}
	if ( !CL_Awesomium_ExecuteJavascriptInteger(
		"(function(){return document.readyState!=='loading'&&String(window.location.href).indexOf('asset://ql/index.html')===0?1:0;})()",
		"", &documentReady ) || !documentReady ) {
		return;
	}
	if ( cl_webui.fnqlOverlayChecked && !cl_webui.fnqlOverlayAvailable ) {
		return;
	}
	cl_webui.fnqlOverlayChecked = qtrue;
	cl_webui.fnqlOverlayAvailable = CL_WebHost_InjectFnqlOverlayAssets();
	if ( cl_webui.fnqlOverlayAvailable ) {
		cl_webui.fnqlOverlayInjected = qtrue;
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
	const char *json;
	char *script;
	int scriptSize;

	if ( !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}

	json = ( friendListJson && friendListJson[0] ) ? friendListJson : "[]";
	scriptSize = (int)strlen( json ) + 512;
	script = (char *)Z_Malloc( scriptSize );
	if ( !script ) {
		return;
	}

	Com_sprintf(
		script,
		scriptSize,
		"(function(){var f=%s;if(window.__qlr_set_friend_list){window.__qlr_set_friend_list(f);}"
		"if(window.EnginePublish){for(var i=0;i<f.length;i++){var v=f[i];if(v&&v.id){"
		"window.EnginePublish('users.persona.'+v.id+'.change',{id:v.id,friend:v});}}}})();",
		json
	);
	CL_Awesomium_ExecuteJavascript( script, "" );
	Z_Free( script );
}

static void CL_WebHost_UpdateBrowserUGCList( const char *ugcListJson ) {
	const char *json;
	char *script;
	int scriptSize;

	if ( !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}
	json = ( ugcListJson && ugcListJson[0] ) ? ugcListJson : "[]";
	if ( strlen( json ) > (size_t)( ( std::numeric_limits<int>::max )() - 128 ) ) {
		return;
	}
	scriptSize = (int)strlen( json ) + 128;
	script = static_cast<char *>( Z_Malloc( scriptSize ) );
	if ( !script ) return;

	Com_sprintf(
		script,
		scriptSize,
		"(function(){if(window.__qlr_set_ugc_list){window.__qlr_set_ugc_list(%s);}})();",
		json
	);
	CL_Awesomium_ExecuteJavascript( script, "" );
	Z_Free( script );
}

static void CL_WebHost_PublishDynamicJsonEvent( const char *eventName,
		const char *json ) {
	char *script;
	int scriptSize;
	if ( !eventName || !eventName[0] || !json || !json[0]
		|| !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return;
	}
	if ( strlen( eventName ) + strlen( json )
		> (size_t)( ( std::numeric_limits<int>::max )() - 160 ) ) return;
	scriptSize = (int)( strlen( eventName ) + strlen( json ) ) + 160;
	script = static_cast<char *>( Z_Malloc( scriptSize ) );
	if ( !script ) return;
	Com_sprintf( script, scriptSize,
		"(function(){if(window.EnginePublish){window.EnginePublish('%s',%s);}})();",
		eventName, json );
	CL_Awesomium_ExecuteJavascript( script, "" );
	Z_Free( script );
}

static qboolean CL_WebHost_UpdateBrowserCatalogCache( const char *setterName, const char *catalogJson ) {
	char *script;
	int scriptSize;
	int result;

	if ( !setterName || !setterName[0] || !CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return qfalse;
	}

	const std::string expression = CL_WebHost_JsonParseExpression(
		( catalogJson && catalogJson[0] ) ? catalogJson : "[]" );
	const size_t required = strlen( setterName ) * 2u + expression.size() + 128u;
	if ( required > static_cast<size_t>( ( std::numeric_limits<int>::max )() ) ) {
		return qfalse;
	}
	scriptSize = static_cast<int>( required );
	script = (char *)Z_Malloc( scriptSize );
	if ( !script ) {
		return qfalse;
	}

	Com_sprintf(
		script,
		scriptSize,
		"(function(){return(window.%s&&window.%s(%s))?1:0;})()",
		setterName,
		setterName,
		expression.c_str()
	);
	result = 0;
	const qboolean executed = CL_Awesomium_ExecuteJavascriptInteger( script, "", &result );
	Z_Free( script );
	return ( executed && result != 0 ) ? qtrue : qfalse;
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

	std::string payload = "[";
	payload.append( entries, entryLength );
	payload.push_back( ']' );
	const std::string expression = CL_WebHost_JsonParseExpression( payload.c_str() );
	const size_t required = strlen( addName ) * 2u + expression.size() + 160u;
	if ( required > static_cast<size_t>( ( std::numeric_limits<int>::max )() ) ) {
		return qfalse;
	}
	scriptSize = static_cast<int>( required );
	script = (char *)Z_Malloc( scriptSize );
	if ( !script ) {
		return qfalse;
	}

	Com_sprintf(
		script,
		scriptSize,
		"(function(){return(window.%s&&window.%s(%s))?1:0;})()",
		addName,
		addName,
		expression.c_str()
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

static qboolean CL_WebHost_UpdateBrowserFactoryList( const char *factoryListJson ) {
	const char *json = factoryListJson && factoryListJson[0]
		? factoryListJson : "{}";
	const size_t jsonLength = strlen( json );
	if ( jsonLength < 2 || json[0] != '{' || json[jsonLength - 1] != '}' ||
		!CL_WebHost_HasLiveView() || !CL_WebHost_HasBoundWindowObject() ) {
		return qfalse;
	}

	int result = 0;
	if ( !CL_Awesomium_ExecuteJavascriptInteger(
			"(function(){return(window.__qlr_begin_native_factories&&window.__qlr_begin_native_factories())?1:0;})()",
			"", &result ) || result == 0 ) {
		return qfalse;
	}

	std::string batch;
	const char *const objectEnd = json + jsonLength - 1;
	const char *valueStart = NULL;
	int depth = 0;
	qboolean inString = qfalse;
	qboolean escaped = qfalse;
	auto flush = [&]() -> qboolean {
		if ( batch.empty() ) {
			return qtrue;
		}
		const qboolean ok = CL_WebHost_ExecuteCatalogBatch(
			"__qlr_add_native_factories", batch.data(), batch.size() );
		batch.clear();
		return ok;
	};
	auto appendValue = [&]( const char *start, const char *end ) -> qboolean {
		while ( start < end && ( *start == ' ' || *start == '\t' ||
			*start == '\r' || *start == '\n' ) ) ++start;
		while ( end > start && ( end[-1] == ' ' || end[-1] == '\t' ||
			end[-1] == '\r' || end[-1] == '\n' ) ) --end;
		if ( start == end ) {
			return qfalse;
		}
		const size_t length = static_cast<size_t>( end - start );
		if ( !batch.empty() && batch.size() + 1u + length >
				CL_WEB_CATALOG_SYNC_CHUNK_CHARS && !flush() ) {
			return qfalse;
		}
		if ( !batch.empty() ) batch.push_back( ',' );
		batch.append( start, length );
		return qtrue;
	};

	for ( const char *cursor = json + 1; cursor <= objectEnd; ++cursor ) {
		if ( cursor == objectEnd ) {
			if ( valueStart && !appendValue( valueStart, cursor ) ) return qfalse;
			break;
		}
		const char ch = *cursor;
		if ( inString ) {
			if ( escaped ) escaped = qfalse;
			else if ( ch == '\\' ) escaped = qtrue;
			else if ( ch == '"' ) inString = qfalse;
			continue;
		}
		if ( ch == '"' ) {
			inString = qtrue;
			continue;
		}
		if ( ch == ':' && depth == 0 && !valueStart ) {
			valueStart = cursor + 1;
			continue;
		}
		if ( ch == '{' || ch == '[' ) ++depth;
		else if ( ch == '}' || ch == ']' ) {
			if ( depth == 0 ) return qfalse;
			--depth;
		} else if ( ch == ',' && depth == 0 ) {
			if ( !valueStart || !appendValue( valueStart, cursor ) ) return qfalse;
			valueStart = NULL;
		}
	}
	if ( inString || depth != 0 || !flush() ) {
		return qfalse;
	}
	result = 0;
	return ( CL_Awesomium_ExecuteJavascriptInteger(
		"(function(){return(window.__qlr_commit_native_factories&&window.__qlr_commit_native_factories())?1:0;})()",
		"", &result ) && result != 0 ) ? qtrue : qfalse;
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

static qboolean CL_WebHost_AppendConfigCvar( char *buffer, size_t bufferSize,
		const char *name, const char *fallbackValue, qboolean *first ) {
	char value[MAX_CVAR_VALUE_STRING];
	char escapedName[MAX_CVAR_VALUE_STRING * 2];
	char escapedValue[MAX_CVAR_VALUE_STRING * 2];
	int flags;

	if ( !buffer || bufferSize == 0 || !name || !name[0] || !first ) {
		return qfalse;
	}

	flags = Cvar_Flags( name );
	if ( flags == CVAR_NONEXISTENT ) {
		if ( !fallbackValue ) {
			return qtrue;
		}
		Q_strncpyz( value, fallbackValue, sizeof( value ) );
	} else if ( flags & CVAR_PRIVATE ) {
		return qtrue;
	} else {
		Cvar_VariableStringBuffer( name, value, sizeof( value ) );
	}

	CL_WebUI_JsonEscape( name, escapedName, sizeof( escapedName ) );
	CL_WebUI_JsonEscape( value, escapedValue, sizeof( escapedValue ) );
	if ( !CL_WebUI_AppendJsonFormattedIfFits( buffer, bufferSize, "%s\"%s\":\"%s\"", *first ? "" : ",", escapedName, escapedValue ) ) {
		return qfalse;
	}

	*first = qfalse;
	return qtrue;
}

static void CL_WebHost_BuildConfigCvarJson( char *buffer, size_t bufferSize ) {
	struct clWebConfigCvarDefault_t {
		const char *name;
		const char *value;
	};
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
		"cl_fnqlWebPakLoaded",
		"cl_fnqlWebPakSource",
		"cl_fnqlWebPakVersion",
		"cl_fnqlWebPakResourceCount",
		"cl_renderer",
		"r_mode",
		"r_modeFullscreen",
		"r_fullscreen",
		"r_noborder",
		"r_customWidth",
		"r_customHeight",
		"r_displayRefresh",
		"r_swapInterval",
		"r_ext_multisample",
		"r_renderScale",
		"r_fbo",
		"r_hdr",
		"r_bloom",
		"r_depthFade",
		"r_globalFog",
		"r_globalFogStrength",
		"r_celShading",
		"r_celShadingWorld",
		"r_celShadingSteps",
		"r_celOutline",
		"r_dlightMode",
		"r_dlightShadows",
		"cl_playerHighlight",
		"cl_playerHighlightRimIntensity",
		"cl_playerHighlightOutlineIntensity",
		"cl_playerHighlightOutlineScale",
		"cl_playerHighlightRedColor",
		"cl_playerHighlightBlueColor",
		"cl_playerHighlightFreeColor",
		"cl_menuAspect",
		"cl_menuDepthOfField",
		"cl_menuDepthOfFieldTime",
		"cl_cinematicAspect",
		"cl_autoRecordDemo",
		"cl_drawRecording",
		"r_levelshotHideHud",
		"r_levelshotHideViewWeapon",
		"s_backend",
		"s_alDevice",
		"s_alHrtf",
		"s_alOutputMode",
		"s_alFrequency",
		"s_alOutputLimiter",
		"s_alSpatializeStereo",
		"s_muteWhenUnfocused",
		"s_muteWhenMinimized",
		NULL
	};
	// The retail Start Match component consumes GetConfig().cvars once while it
	// mounts. These values therefore need to exist in the initial synchronous
	// snapshot, before its later cvar.* subscriptions can observe native state.
	// Keep absent module-owned cvars as bridge defaults rather than registering
	// them on the client; qagame remains authoritative once a match starts.
	static const clWebConfigCvarDefault_t startMatchCvars[] = {
		{ "sv_hostname", "noname" },
		{ "sv_serverType", "0" },
		{ "net_port", "27960" },
		{ "sv_maxclients", "8" },
		{ "bot_minplayers", "0" },
		{ "g_spSkill", "2" },
		{ "teamsize", "0" },
		{ "g_password", "" },
		{ "sv_warmupReadyPercentage", "0.51" },
		{ "sv_mapPoolFile", "mappool.txt" },
		{ NULL, NULL }
	};
	qboolean first;

	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	buffer[0] = '\0';
	first = qtrue;
	for ( int i = 0; configCvars[i]; ++i ) {
		if ( !CL_WebHost_AppendConfigCvar( buffer, bufferSize,
			configCvars[i], NULL, &first ) ) {
			break;
		}
	}
	for ( int i = 0; startMatchCvars[i].name; ++i ) {
		if ( !CL_WebHost_AppendConfigCvar( buffer, bufferSize,
			startMatchCvars[i].name, startMatchCvars[i].value, &first ) ) {
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

static char *CL_WebHost_AllocateFactoryListJson( void ) {
	const int bufferSize = SV_FactoryWebCatalogJsonSize();
	if ( bufferSize < 3 ) {
		return NULL;
	}
	char *buffer = static_cast<char *>( Z_Malloc( bufferSize ) );
	if ( !buffer ) {
		return NULL;
	}
	if ( !SV_FactoryBuildWebCatalogJson( buffer, bufferSize ) ) {
		Z_Free( buffer );
		return NULL;
	}
	return buffer;
}

static unsigned long long CL_WebHost_SteamIdFromWords( unsigned int identityLow, unsigned int identityHigh ) {
	return ( (unsigned long long)identityHigh << 32 ) | identityLow;
}

static const char *CL_Steam_PersonaStateText( int state ) {
	switch ( state ) {
		case 0: return "Offline";
		case 1: return "Online";
		case 2: return "Busy";
		case 3: return "Away";
		case 4: return "Snooze";
		case 5: return "Looking to trade";
		case 6: return "Looking to play";
		case 7: return "Invisible";
		default: return "Unknown";
	}
}

static qboolean CL_WebHost_AppendSteamFriendJson( char *buffer,
		size_t bufferSize, const fnqlSteamFriend_t *friendInfo,
		qboolean first ) {
	char name[FNQL_STEAM_NAME_CAPACITY * 2];
	char nickname[FNQL_STEAM_NAME_CAPACITY * 2];
	char status[FNQL_STEAM_TEXT_CAPACITY * 2];
	char presence[FNQL_STEAM_TEXT_CAPACITY * 2];
	char lanIp[128];
	char connect[FNQL_STEAM_TEXT_CAPACITY * 2];
	char nicknameJson[FNQL_STEAM_NAME_CAPACITY * 2 + 3];
	char gameJson[256];
	char item[CL_WEB_CONFIG_JSON_LENGTH];
	const char *presenceText;

	if ( !buffer || !friendInfo || !friendInfo->steam_id ) {
		return qfalse;
	}
	CL_WebUI_JsonEscape( friendInfo->persona_name, name, sizeof( name ) );
	CL_WebUI_JsonEscape( friendInfo->nickname, nickname, sizeof( nickname ) );
	CL_WebUI_JsonEscape( friendInfo->status, status, sizeof( status ) );
	CL_WebUI_JsonEscape( friendInfo->lan_ip, lanIp, sizeof( lanIp ) );
	CL_WebUI_JsonEscape( friendInfo->connect, connect, sizeof( connect ) );
	if ( friendInfo->flags & FNQL_STEAM_FRIEND_HAS_NICKNAME ) {
		Com_sprintf( nicknameJson, sizeof( nicknameJson ), "\"%s\"", nickname );
	} else {
		Q_strncpyz( nicknameJson, "null", sizeof( nicknameJson ) );
	}
	if ( friendInfo->status[0] ) {
		presenceText = friendInfo->status;
	} else if ( friendInfo->flags & FNQL_STEAM_FRIEND_PLAYING_QUAKE_LIVE ) {
		presenceText = friendInfo->lobby_id ? "In a lobby" : "Playing Quake Live";
	} else if ( friendInfo->flags & FNQL_STEAM_FRIEND_HAS_GAME ) {
		presenceText = "Playing other game";
	} else {
		presenceText = CL_Steam_PersonaStateText( friendInfo->persona_state );
	}
	CL_WebUI_JsonEscape( presenceText, presence, sizeof( presence ) );
	if ( friendInfo->flags & FNQL_STEAM_FRIEND_HAS_GAME ) {
		Com_sprintf( gameJson, sizeof( gameJson ),
			"{\"lobby\":\"%llu\",\"appid\":%u,\"ip\":%u,\"port\":%u,\"queryport\":%u}",
			(unsigned long long)friendInfo->lobby_id, friendInfo->app_id,
			friendInfo->server_ip, (unsigned int)friendInfo->server_port,
			(unsigned int)friendInfo->query_port );
	} else {
		Q_strncpyz( gameJson, "null", sizeof( gameJson ) );
	}
	Com_sprintf( item, sizeof( item ),
		"%s{\"id\":\"%llu\",\"steamId\":\"%llu\",\"name\":\"%s\",\"personaName\":\"%s\",\"avatar\":\"asset://steam/avatar/large/%llu\",\"avatarUrl\":\"asset://steam/avatar/large/%llu\",\"avatarSmall\":\"asset://steam/avatar/small/%llu\",\"avatarLarge\":\"asset://steam/avatar/large/%llu\",\"image\":\"asset://steam/avatar/large/%llu\",\"profileUrl\":\"https://steamcommunity.com/profiles/%llu\",\"avatarSize\":\"large\",\"state\":%d,\"personaState\":%d,\"stateText\":\"%s\",\"relationship\":%d,\"nickname\":%s,\"status\":\"%s\",\"richPresence\":\"%s\",\"statusText\":\"%s\",\"presence\":\"%s\",\"lanIp\":\"%s\",\"connect\":\"%s\",\"playingQuake\":%d,\"playingQuakeLive\":%d,\"appId\":%u,\"lobbyId\":\"%llu\",\"gameId\":\"%llu\",\"game\":%s}",
		first ? "" : ",", (unsigned long long)friendInfo->steam_id,
		(unsigned long long)friendInfo->steam_id, name, name,
		(unsigned long long)friendInfo->steam_id,
		(unsigned long long)friendInfo->steam_id,
		(unsigned long long)friendInfo->steam_id,
		(unsigned long long)friendInfo->steam_id,
		(unsigned long long)friendInfo->steam_id,
		(unsigned long long)friendInfo->steam_id,
		friendInfo->persona_state, friendInfo->persona_state,
		CL_Steam_PersonaStateText( friendInfo->persona_state ),
		friendInfo->relationship, nicknameJson, status, status,
		presence, presence, lanIp, connect,
		( friendInfo->flags & FNQL_STEAM_FRIEND_PLAYING_QUAKE_LIVE ) ? 1 : 0,
		( friendInfo->flags & FNQL_STEAM_FRIEND_PLAYING_QUAKE_LIVE ) ? 1 : 0,
		friendInfo->app_id, (unsigned long long)friendInfo->lobby_id,
		(unsigned long long)friendInfo->game_id, gameJson );
	return CL_WebUI_AppendJsonLiteralIfFits( buffer, bufferSize, item );
}

static void CL_WebHost_FormatSteamFriendEventJson(
		const fnqlSteamFriend_t *friendInfo, char *buffer, size_t bufferSize ) {
	char name[FNQL_STEAM_NAME_CAPACITY * 2];
	char status[FNQL_STEAM_TEXT_CAPACITY * 2];
	char lanIp[128];
	if ( !buffer || bufferSize == 0 ) return;
	if ( !friendInfo || !friendInfo->steam_id ) {
		Q_strncpyz( buffer, "{}", (int)bufferSize );
		return;
	}
	CL_WebUI_JsonEscape( friendInfo->persona_name, name, sizeof( name ) );
	CL_WebUI_JsonEscape( friendInfo->status, status, sizeof( status ) );
	CL_WebUI_JsonEscape( friendInfo->lan_ip, lanIp, sizeof( lanIp ) );
	Com_sprintf( buffer, (int)bufferSize,
		"{\"id\":\"%llu\",\"steamId\":\"%llu\",\"name\":\"%s\",\"personaName\":\"%s\",\"state\":%d,\"personaState\":%d,\"relationship\":%d,\"status\":\"%s\",\"richPresence\":\"%s\",\"lanIp\":\"%s\",\"playingQuake\":%d,\"playingQuakeLive\":%d,\"appId\":%u,\"lobbyId\":\"%llu\"}",
		(unsigned long long)friendInfo->steam_id,
		(unsigned long long)friendInfo->steam_id, name, name,
		friendInfo->persona_state, friendInfo->persona_state,
		friendInfo->relationship, status, status, lanIp,
		( friendInfo->flags & FNQL_STEAM_FRIEND_PLAYING_QUAKE_LIVE ) ? 1 : 0,
		( friendInfo->flags & FNQL_STEAM_FRIEND_PLAYING_QUAKE_LIVE ) ? 1 : 0,
		friendInfo->app_id, (unsigned long long)friendInfo->lobby_id );
}

static void CL_WebHost_BuildFriendListJson( char *buffer, size_t bufferSize ) {
	qboolean first;

	if ( !buffer || bufferSize == 0 ) {
		return;
	}

	buffer[0] = '\0';
	Q_strcat( buffer, bufferSize, "[" );
	if ( FNQL_Steam_Available( FNQL_STEAM_CAP_FRIENDS ) ) {
		uint32_t count = 0;
		fnqlSteamResult_t result = FNQL_Steam_GetFriends(
			CL_STEAM_FRIEND_FLAGS, NULL, 0, &count );
		if ( result == FNQL_STEAM_RESULT_OK && count == 0 ) {
			Q_strcat( buffer, (int)bufferSize, "]" );
			return;
		}
		if ( result == FNQL_STEAM_RESULT_BUFFER_TOO_SMALL
			&& count > 0 && count <= CL_STEAM_MAX_FRIENDS ) {
			fnqlSteamFriend_t *friends = static_cast<fnqlSteamFriend_t *>(
				Z_Malloc( sizeof( *friends ) * count ) );
			if ( friends ) {
				result = FNQL_Steam_GetFriends( CL_STEAM_FRIEND_FLAGS,
					friends, count, &count );
				if ( result == FNQL_STEAM_RESULT_OK ) {
					fnqlSteamStatus_t status = {};
					status.size = sizeof( status );
					(void)FNQL_Steam_GetStatus( &status );
					first = qtrue;
					for ( uint32_t i = 0; i < count; ++i ) {
						if ( friends[i].steam_id == status.local_steam_id ) continue;
						if ( !CL_WebHost_AppendSteamFriendJson( buffer, bufferSize,
							&friends[i], first ) ) break;
						first = qfalse;
					}
					Z_Free( friends );
					Q_strcat( buffer, (int)bufferSize, "]" );
					return;
				}
				Z_Free( friends );
			}
		}
	}
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

	// Retail web.pak requests the unqualified form and treats it as the large
	// profile image. The Awesomium Steam data source also accepts explicit size
	// tokens for engine-owned callers.
	Com_sprintf( buffer, bufferSize, "asset://steam/avatar/%s", steamId );
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
	} else {
		CL_Steam_GetLocalIdentityWords( &identityLow, &identityHigh );
	}

	CL_WebHost_FormatSteamId( identityLow, identityHigh, steamId, sizeof( steamId ) );
	CL_WebHost_FormatSteamAvatarUrl( steamId, playerAvatarUrl, sizeof( playerAvatarUrl ) );
	CL_WebHost_FormatSteamProfileUrl( steamId, playerProfileUrl, sizeof( playerProfileUrl ) );
	CL_Steam_GetLocalDisplayName( playerName, sizeof( playerName ) );
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

	if ( !force && CL_Awesomium_IsLoading() ) {
		cl_webui.nextConfigSnapshotFrame = cl_webui.frameSequence + CL_WEB_NATIVE_REQUEST_LOADING_POLL_FRAMES;
		return;
	}

	// Catalog invalidation is event-driven and must not wait behind the slower
	// periodic config snapshot throttle.
	if ( force || !cl_webui.factoryCatalogSynced ) {
		char *factoryJson = CL_WebHost_AllocateFactoryListJson();
		if ( factoryJson ) {
			cl_webui.factoryCatalogSynced =
				CL_WebHost_UpdateBrowserFactoryList( factoryJson );
			Z_Free( factoryJson );
		}
	}

	if ( !force && cl_webui.configSnapshotSynced &&
		cl_webui.frameSequence < cl_webui.nextConfigSnapshotFrame ) {
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

	{
		char *friendJson = static_cast<char *>( Z_Malloc( CL_WEB_FRIEND_JSON_LENGTH ) );
		if ( friendJson ) {
			CL_WebHost_BuildFriendListJson( friendJson, CL_WEB_FRIEND_JSON_LENGTH );
			CL_WebHost_UpdateBrowserFriendList( friendJson );
			Z_Free( friendJson );
		}
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
		( ip >> 24 ) & 0xffu,
		( ip >> 16 ) & 0xffu,
		( ip >> 8 ) & 0xffu,
		ip & 0xffu,
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

	if ( adr.type == NA_IP && FNQL_Steam_Available( FNQL_STEAM_CAP_FAVORITES ) ) {
		const uint32_t packedIp = ( static_cast<uint32_t>( adr.ipv._4[0] ) << 24u )
			| ( static_cast<uint32_t>( adr.ipv._4[1] ) << 16u )
			| ( static_cast<uint32_t>( adr.ipv._4[2] ) << 8u )
			| static_cast<uint32_t>( adr.ipv._4[3] );
		const uint16_t port = static_cast<uint16_t>( BigShort( adr.port ) );
		const fnqlSteamResult_t result = FNQL_Steam_SetFavoriteServer(
			static_cast<uint32_t>( atoi( STEAMPATH_APPID ) ), packedIp, port,
			port, add );
		if ( result != FNQL_STEAM_RESULT_OK ) {
			Com_DPrintf( "Steam favorite %s failed for %s (%d); retained local cache fallback.\n",
				add ? "add" : "remove", address, result );
		}
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

static void CL_WebHost_StartServerRefresh( int requestMode, int source,
	qboolean resetLegacyServers ) {
	cl_webui.serverRefreshInitialized = qtrue;
	cl_webui.serverRefreshActive = qtrue;
	cl_webui.serverRefreshSteam = qfalse;
	cl_webui.serverRefreshRequestMode = requestMode;
	cl_webui.serverRefreshSource = source;
	cl_webui.serverRefreshTime = cls.realtime + ( source == AS_LOCAL ? CL_WEB_SERVER_LOCAL_REFRESH_WAIT_MSEC : CL_WEB_SERVER_REMOTE_REFRESH_WAIT_MSEC );
	cl_webui.serverRefreshTimeoutTime = cls.realtime + CL_WEB_SERVER_REFRESH_TIMEOUT_MSEC;

	/*
	 * A native Steam list is independent of the legacy source-browser
	 * caches.  Do not touch those caches until the native request actually
	 * declines and we select the fallback; otherwise a WebUI refresh would
	 * perturb the data shown by the legacy UI and subsequent fallback query.
	 */
	if ( resetLegacyServers ) {
		CL_WebHost_MarkServerVisible( source, -1, qtrue );
		CL_WebHost_ResetServerPings( source );
	}
}

static void CL_WebHost_PublishUnresponsiveServerRows( int source ) {
	serverInfo_t *servers;
	int count = 0;
	int capacity = 0;

	servers = CL_WebHost_GetServerList( source, &count, &capacity );
	if ( !servers || count <= 0 ) {
		return;
	}
	if ( count > capacity ) {
		count = capacity;
	}
	for ( int index = 0; index < count; ++index ) {
		char eventName[CL_WEB_EVENT_NAME_LENGTH];
		char payload[64];
		if ( !servers[index].visible || !servers[index].adr.port
			|| servers[index].ping != 0 ) {
			continue;
		}
		Com_sprintf( eventName, sizeof( eventName ),
			"servers.details.%d.failed", index );
		Com_sprintf( payload, sizeof( payload ), "{\"id\":%d}", index );
		CL_WebView_PublishEvent( eventName, payload );
	}
}

static void CL_WebHost_PublishServerBrowserRefreshEnd( void ) {
	qboolean releaseSteamRequest;

	if ( !cl_webui.serverRefreshActive ) {
		return;
	}

	// The provider may synchronously deliver a completion while its request is
	// being released. Make the terminal state visible first so completion is
	// one-shot and cannot re-enter this owner.
	releaseSteamRequest = cl_webui.serverRefreshSteam;
	if ( !releaseSteamRequest ) {
		CL_WebHost_PublishUnresponsiveServerRows( cl_webui.serverRefreshSource );
	}
	cl_webui.serverRefreshActive = qfalse;
	cl_webui.serverRefreshSteam = qfalse;
	if ( releaseSteamRequest ) {
		FNQL_Steam_CancelServers();
	}
	CL_WebView_PublishEvent( "servers.refresh.end", NULL );
}

static void CL_WebHost_ServerBrowserFrame( void ) {
	qboolean wait;
	int count;

	if ( !cl_webui.serverRefreshActive ) {
		return;
	}
	if ( cl_webui.serverRefreshSteam ) {
		if ( cls.realtime >= cl_webui.serverRefreshTimeoutTime ) {
			CL_WebHost_PublishServerBrowserRefreshEnd();
		}
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

	return ( (unsigned int)address->ipv._4[0] << 24 ) |
		( (unsigned int)address->ipv._4[1] << 16 ) |
		( (unsigned int)address->ipv._4[2] << 8 ) |
		(unsigned int)address->ipv._4[3];
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
	cl_webui.serverDetailSteam = qfalse;
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

static void CL_WebHost_PublishSteamServerDetailPlayer( const fnqlSteamEvent_t *event ) {
	char eventName[CL_WEB_EVENT_NAME_LENGTH];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];
	char escapedName[FNQL_STEAM_TEXT_CAPACITY * 2];
	const int score = event ? static_cast<int>( static_cast<int32_t>( event->value ) ) : 0;
	const double timePlayed = event ? atof( event->detail ) : 0.0;

	if ( !event || !cl_webui.serverDetailActive ) return;
	CL_WebUI_JsonEscape( event->text, escapedName, sizeof( escapedName ) );
	Com_sprintf( eventName, sizeof( eventName ), "servers.players.%s.response", cl_webui.serverDetailId );
	Com_sprintf( payload, sizeof( payload ),
		"{\"id\":\"%s\",\"ip\":%u,\"port\":%u,\"name\":\"%s\",\"score\":%d,\"time\":%.3f}",
		cl_webui.serverDetailId, cl_webui.serverDetailIp,
		(unsigned int)cl_webui.serverDetailPort, escapedName, score, timePlayed );
	CL_WebView_PublishEvent( eventName, payload );
}

/*
 * Retail web.pak subscribes to servers.details.<packed-ip>_<port>.response
 * for both list rows and an explicit server-detail ping.  Keep all provider
 * rows on that single observable schema; servers.refresh.* only brackets a
 * refresh and never carries server data.
 */
static void CL_WebHost_PublishSteamServerResponse( const fnqlSteamServer_t *server,
	unsigned int browserIp, unsigned short browserPort, const char *detailId ) {
	char eventName[CL_WEB_EVENT_NAME_LENGTH];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];
	char name[sizeof( server->server_name ) * 2];
	char map[sizeof( server->map ) * 2];
	char tags[sizeof( server->game_tags ) * 2];
	char gameDirectory[sizeof( server->game_directory ) * 2];
	char gametype[sizeof( server->game_description ) * 2];
	const int numPlayers = server && server->players > 0 ? server->players : 0;
	const int maxPlayers = server && server->max_players > 0 ? server->max_players : 0;
	const int botPlayers = server && server->bot_players > 0 ? server->bot_players : 0;
	const int ping = server && server->ping > 0 ? server->ping : 0;

	if ( !server || server->size < sizeof( *server ) || !detailId || !detailId[0] ) {
		return;
	}
	CL_WebUI_JsonEscape( server->server_name, name, sizeof( name ) );
	CL_WebUI_JsonEscape( server->map, map, sizeof( map ) );
	CL_WebUI_JsonEscape( server->game_tags, tags, sizeof( tags ) );
	CL_WebUI_JsonEscape( server->game_directory, gameDirectory, sizeof( gameDirectory ) );
	CL_WebUI_JsonEscape( server->game_description, gametype, sizeof( gametype ) );
	Com_sprintf( eventName, sizeof( eventName ), "servers.details.%s.response", detailId );
	Com_sprintf( payload, sizeof( payload ),
		"{\"name\":\"%s\",\"numPlayers\":%d,\"maxPlayers\":%d,\"ping\":%d,\"map\":\"%s\",\"botPlayers\":%d,\"password\":%s,\"vac\":%s,\"ip\":%u,\"port\":%u,\"id\":\"%s\",\"steam_id\":\"%llu\",\"tags\":\"%s\",\"gametype\":\"%s\",\"gamedir\":\"%s\",\"lastPlayed\":%u}",
		name, numPlayers, maxPlayers, ping, map, botPlayers,
		server->password_protected ? "true" : "false",
		server->secure ? "true" : "false", browserIp,
		(unsigned int)browserPort, detailId,
		(unsigned long long)server->steam_id, tags, gametype, gameDirectory,
		server->last_played );
	CL_WebView_PublishEvent( eventName, payload );
}

static void CL_WebHost_PublishSteamServerDetailResponse( const fnqlSteamServer_t *server ) {
	if ( !server || !cl_webui.serverDetailActive
		|| server->ip != cl_webui.serverDetailIp
		|| ( server->connection_port != cl_webui.serverDetailPort
			&& server->query_port != cl_webui.serverDetailPort ) ) {
		return;
	}

	CL_WebHost_PublishSteamServerResponse( server, cl_webui.serverDetailIp,
		cl_webui.serverDetailPort, cl_webui.serverDetailId );
}

static void CL_WebHost_PublishServerBrowserResponse( const netadr_t *address,
	unsigned int browserIp, unsigned short browserPort, const char *infoString,
	int responsePing ) {
	serverInfo_t *server;
	char eventName[CL_WEB_EVENT_NAME_LENGTH];
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];
	char hostName[MAX_NAME_LENGTH];
	char mapName[MAX_NAME_LENGTH];
	char tags[MAX_INFO_STRING];
	char gameDirectory[MAX_NAME_LENGTH];
	char steamId[64];
	char gametypeValue[16];
	char passwordValue[16];
	char hostNameEscaped[MAX_NAME_LENGTH * 2];
	char mapNameEscaped[MAX_NAME_LENGTH * 2];
	char tagsEscaped[MAX_INFO_STRING * 2];
	char gameDirectoryEscaped[MAX_NAME_LENGTH * 2];
	char steamIdEscaped[128];
	char responseId[32];
	int numPlayers;
	int maxPlayers;
	int botPlayers;
	int password;
	int vac;
	int gametype;
	int ping;

	if ( !address || !browserIp || !browserPort ) {
		return;
	}

	server = CL_WebHost_FindServerInfoByAddress( address );
	hostName[0] = '\0';
	mapName[0] = '\0';
	tags[0] = '\0';
	Q_strncpyz( gameDirectory, BASEGAME, sizeof( gameDirectory ) );
	steamId[0] = '\0';
	gametypeValue[0] = '\0';
	passwordValue[0] = '\0';
	numPlayers = 0;
	maxPlayers = 0;
	botPlayers = 0;
	password = 0;
	vac = 0;
	gametype = 0;
	ping = responsePing > 0 ? responsePing : 0;

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
		const char *infoGameDirectory = Info_ValueForKey( infoString, "game" );
		if ( infoGameDirectory && infoGameDirectory[0] ) {
			Q_strncpyz( gameDirectory, infoGameDirectory, sizeof( gameDirectory ) );
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
		if ( !gameDirectory[0] ) {
			Q_strncpyz( gameDirectory, server->game, sizeof( gameDirectory ) );
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
		if ( ping <= 0 && server->ping > 0 ) {
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
	CL_WebUI_JsonEscape( gameDirectory, gameDirectoryEscaped, sizeof( gameDirectoryEscaped ) );
	CL_WebUI_JsonEscape( steamId, steamIdEscaped, sizeof( steamIdEscaped ) );
	CL_WebHost_FormatServerDetailId( browserIp, browserPort, responseId, sizeof( responseId ) );
	Com_sprintf( eventName, sizeof( eventName ), "servers.details.%s.response", responseId );
	Com_sprintf(
		payload,
		sizeof( payload ),
		"{\"name\":\"%s\",\"numPlayers\":%d,\"maxPlayers\":%d,\"ping\":%d,\"map\":\"%s\",\"botPlayers\":%d,\"password\":%s,\"vac\":%s,\"ip\":%u,\"port\":%u,\"id\":\"%s\",\"steam_id\":\"%s\",\"tags\":\"%s\",\"gametype\":%d,\"gamedir\":\"%s\",\"lastPlayed\":0}",
		hostNameEscaped,
		numPlayers,
		maxPlayers,
		ping > 0 ? ping : 0,
		mapNameEscaped,
		botPlayers,
		password ? "true" : "false",
		vac ? "true" : "false",
		browserIp,
		(unsigned int)browserPort,
		responseId,
		steamIdEscaped,
		tagsEscaped,
		gametype,
		gameDirectoryEscaped );
	CL_WebView_PublishEvent( eventName, payload );
}

static void CL_WebHost_PublishServerDetailResponse( const netadr_t *address,
	const char *infoString ) {
	serverInfo_t *server;
	int ping = 0;

	if ( !CL_WebHost_DetailMatchesAddress( address ) ) {
		return;
	}

	server = CL_WebHost_FindServerInfoByAddress( address );
	if ( server && server->ping > 0 ) {
		ping = server->ping;
	}
	CL_WebHost_PublishServerBrowserResponse( address, cl_webui.serverDetailIp,
		cl_webui.serverDetailPort, infoString, ping );
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
		if ( cl_webui.serverDetailSteam ) FNQL_Steam_CancelServerDetails();
		CL_WebHost_PublishServerDetailFailed();
	}
}

void CL_WebHost_OnServerInfoResponse( const netadr_t *address,
	const char *infoString, int ping ) {
	/*
	 * Source-browser pings are the no-Steam fallback for retail WebUI list
	 * requests.  CL_ServerInfoPacket owns validation and ping timing; this
	 * bridge only projects the matching active refresh into web.pak's detail
	 * response family.  Native Steam rows arrive through the provider instead.
	 */
	if ( !address || !cl_webui.serverRefreshActive || cl_webui.serverRefreshSteam
		|| cls.pingUpdateSource != cl_webui.serverRefreshSource ) {
		return;
	}

	CL_WebHost_PublishServerBrowserResponse( address,
		CL_WebHost_PackedIPv4FromAddress( address ),
		CL_WebHost_PortFromAddress( address ), infoString, ping );
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
		case 2:
			return "friends";
		case 3:
			return "favorites";
		case 4:
			return "history";
		case 0:
		default:
			return "internet";
	}
}

static const char *CL_WebHost_ServerBrowserCompatibilityReason( int requestMode ) {
	if ( requestMode == 2 ) {
		return "friends fallback mapped to global source";
	}
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

static uint32_t CL_WebHost_ServerRequestModeToSteamRequestMode( int requestMode ) {
	/* WebUI's Friends filter is value 2; Steam's server browser uses 5. */
	switch ( requestMode ) {
		case 1:
			return FNQL_STEAM_SERVER_BROWSER_LAN;
		case 2:
			return FNQL_STEAM_SERVER_BROWSER_FRIENDS;
		case 3:
			return FNQL_STEAM_SERVER_BROWSER_FAVORITES;
		case 4:
			return FNQL_STEAM_SERVER_BROWSER_HISTORY;
		case 0:
		default:
			return FNQL_STEAM_SERVER_BROWSER_INTERNET;
	}
}

qboolean CL_Steam_RequestServers( int requestMode ) {
	const int source = CL_WebHost_ServerRequestModeToSource( requestMode );
	const qboolean nativeAvailable = FNQL_Steam_Available(
		FNQL_STEAM_CAP_SERVER_BROWSER );

	CL_WebHost_StartServerRefresh( requestMode, source,
		nativeAvailable ? qfalse : qtrue );
	CL_WebView_PublishEvent( "servers.refresh.start", NULL );
	if ( nativeAvailable ) {
		// The provider is allowed to synchronously notify the event sink. Claim
		// ownership before entering it so an immediate completion cannot be
		// mistaken for the legacy source-browser fallback.
		cl_webui.serverRefreshSteam = qtrue;
		if ( CL_Steam_ResultAccepted( FNQL_Steam_RequestServers(
			CL_WebHost_ServerRequestModeToSteamRequestMode( requestMode ),
			(uint32_t)atoi( STEAMPATH_APPID ) ) ) ) {
			return qtrue;
		}
		cl_webui.serverRefreshSteam = qfalse;
		CL_WebHost_StartServerRefresh( requestMode, source, qtrue );
	}
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

	if ( cl_webui.serverDetailSteam ) FNQL_Steam_CancelServerDetails();
	CL_WebHost_StartServerDetailRequest( &adr );
	if ( FNQL_Steam_Available( FNQL_STEAM_CAP_SERVER_DETAILS ) ) {
		cl_webui.serverDetailSteam = qtrue;
		if ( CL_Steam_ResultAccepted( FNQL_Steam_RequestServerDetails( serverIp,
			serverPort ) ) ) {
			return qtrue;
		}
		if ( !cl_webui.serverDetailActive || !cl_webui.serverDetailSteam ) {
			return qtrue;
		}
		cl_webui.serverDetailSteam = qfalse;
	}
	Cbuf_ExecuteText( EXEC_APPEND, va( "serverstatus %s\n", address ) );
	return qtrue;
}

qboolean CL_Steam_RefreshServerList( void ) {
	if ( !cl_webui.serverRefreshInitialized ) {
		return qfalse;
	}

	if ( cl_webui.serverRefreshSteam ) {
		/* RefreshQuery can synchronously dispatch callbacks.  Re-arm the
		 * active owner and deadline before crossing the provider boundary. */
		cl_webui.serverRefreshActive = qtrue;
		cl_webui.serverRefreshTimeoutTime = cls.realtime + CL_WEB_SERVER_REFRESH_TIMEOUT_MSEC;
		if ( CL_Steam_ResultAccepted( FNQL_Steam_RefreshServers() ) ) {
			return qtrue;
		}
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
		CL_Steam_RequestServerDetails( CL_WebHost_PackedIPv4FromAddress( &adr ),
			CL_WebHost_PortFromAddress( &adr ) );
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

static qboolean CL_Steam_ResultAccepted( fnqlSteamResult_t result ) {
	return ( result == FNQL_STEAM_RESULT_OK || result == FNQL_STEAM_RESULT_PENDING )
		? qtrue : qfalse;
}

static qboolean CL_Steam_ParseIdentity( const char *text, uint64_t *identity ) {
	uint64_t value = 0;
	int length = 0;

	if ( identity ) {
		*identity = 0;
	}
	if ( !text || !text[0] || !identity ) {
		return qfalse;
	}
	for ( ; text[length]; ++length ) {
		const uint64_t digit = (uint64_t)( text[length] - '0' );
		if ( text[length] < '0' || text[length] > '9'
			|| value > ( ~(uint64_t)0 - digit ) / 10u ) {
			return qfalse;
		}
		value = value * 10u + digit;
	}
	if ( length > 20 || !value ) {
		return qfalse;
	}
	*identity = value;
	return qtrue;
}

static qboolean CL_Steam_SafeCommandText( const char *text ) {
	return text && text[0] && !strpbrk( text, "\r\n;\"" ) ? qtrue : qfalse;
}

static qboolean CL_Steam_ConsumeUgcQueryResults( void ) {
	uint32_t count = 0;
	uint32_t totalMatching = 0;
	fnqlSteamResult_t result = FNQL_Steam_GetUgcQueryResults(
		NULL, 0, &count, &totalMatching );
	if ( result == FNQL_STEAM_RESULT_OK && count == 0 ) {
		Com_Printf( "Steam UGC query returned 0 of %u matching items.\n",
			totalMatching );
		CL_WebHost_UpdateBrowserUGCList( "[]" );
		CL_WebHost_PublishDynamicJsonEvent( "web.ugc.results", "[]" );
		CL_WebView_PublishEvent( "web.ugc.complete", NULL );
		return qtrue;
	}
	if ( result != FNQL_STEAM_RESULT_BUFFER_TOO_SMALL || count == 0
		|| count > 50u ) return qfalse;

	fnqlSteamUgcItem_t *items = static_cast<fnqlSteamUgcItem_t *>(
		Z_Malloc( sizeof( *items ) * count ) );
	const size_t perItemCapacity = FNQL_STEAM_UGC_TITLE_CAPACITY * 2u
		+ FNQL_STEAM_UGC_DESCRIPTION_CAPACITY * 2u
		+ FNQL_STEAM_UGC_PREVIEW_URL_CAPACITY * 2u + 256u;
	const size_t jsonCapacity = perItemCapacity * count + 2u;
	char *json = jsonCapacity <= 1024u * 1024u
		? static_cast<char *>( Z_Malloc( jsonCapacity ) ) : NULL;
	char *item = static_cast<char *>( Z_Malloc( perItemCapacity ) );
	char *escapedDescription = static_cast<char *>( Z_Malloc(
		FNQL_STEAM_UGC_DESCRIPTION_CAPACITY * 2u ) );
	if ( !items || !json || !item || !escapedDescription ) {
		if ( escapedDescription ) Z_Free( escapedDescription );
		if ( item ) Z_Free( item );
		if ( json ) Z_Free( json );
		if ( items ) Z_Free( items );
		return qfalse;
	}
	result = FNQL_Steam_GetUgcQueryResults( items, count, &count,
		&totalMatching );
	if ( result != FNQL_STEAM_RESULT_OK ) {
		Z_Free( escapedDescription );
		Z_Free( item );
		Z_Free( json );
		Z_Free( items );
		return qfalse;
	}
	Com_Printf( "Steam UGC query returned %u of %u matching items.\n",
		count, totalMatching );
	json[0] = '\0';
	(void)CL_WebUI_AppendJsonLiteralIfFits( json, jsonCapacity, "[" );
	qboolean first = qtrue;
	for ( uint32_t i = 0; i < count; ++i ) {
		char escapedTitle[FNQL_STEAM_UGC_TITLE_CAPACITY * 2];
		char escapedPreview[FNQL_STEAM_UGC_PREVIEW_URL_CAPACITY * 2];
		CL_WebUI_JsonEscape( items[i].title, escapedTitle,
			sizeof( escapedTitle ) );
		CL_WebUI_JsonEscape( items[i].description, escapedDescription,
			FNQL_STEAM_UGC_DESCRIPTION_CAPACITY * 2u );
		CL_WebUI_JsonEscape( items[i].preview_url, escapedPreview,
			sizeof( escapedPreview ) );
		Com_sprintf( item, (int)perItemCapacity,
			"%s{\"title\":\"%s\",\"description\":\"%s\",\"id\":\"%llu\",\"image\":\"%s\"}",
			first ? "" : ",", escapedTitle, escapedDescription,
			(unsigned long long)items[i].published_file_id, escapedPreview );
		if ( !CL_WebUI_AppendJsonLiteralIfFits( json, jsonCapacity, item ) ) break;
		first = qfalse;
	}
	(void)CL_WebUI_AppendJsonLiteralIfFits( json, jsonCapacity, "]" );
	CL_WebHost_UpdateBrowserUGCList( json );
	CL_WebHost_PublishDynamicJsonEvent( "web.ugc.results", json );
	CL_WebView_PublishEvent( "web.ugc.complete", NULL );
	Z_Free( escapedDescription );
	Z_Free( item );
	Z_Free( json );
	Z_Free( items );
	return qtrue;
}

static qboolean CL_Steam_GetCurrentServerP2PIdentity( uint64_t *steamId ) {
	static const int serverIdentityConfigstring = 0x2ca;
	if ( steamId ) *steamId = 0;
	if ( !steamId || cls.state != CA_ACTIVE
		|| serverIdentityConfigstring < 0
		|| serverIdentityConfigstring >= MAX_CONFIGSTRINGS ) return qfalse;
	const int offset = cl.gameState.stringOffsets[serverIdentityConfigstring];
	if ( offset < 0 || offset >= MAX_GAMESTATE_CHARS ) return qfalse;
	return CL_Steam_ParseIdentity( cl.gameState.stringData + offset, steamId );
}

static void CL_Steam_HandleP2PEvent( const fnqlSteamEvent_t *event ) {
	if ( !event || event->flags != FNQL_STEAM_ROLE_CLIENT ) return;
	if ( event->type == FNQL_STEAM_EVENT_P2P_SESSION_FAILURE ) {
		if ( cl_steamAcceptedP2PServer == event->subject_id ) {
			cl_steamAcceptedP2PServer = 0;
		}
		Com_Printf( S_COLOR_YELLOW
			"Steam client P2P session failed for %llu (error %llu).\n",
			(unsigned long long)event->subject_id,
			(unsigned long long)event->value );
		return;
	}
	uint64_t serverId = 0;
	if ( !CL_Steam_GetCurrentServerP2PIdentity( &serverId ) ) {
		cl_steamPendingP2PRemote = event->subject_id;
		Com_DPrintf( "Deferred Steam client P2P request from %llu until the active server identity arrives.\n",
			(unsigned long long)event->subject_id );
		return;
	}
	if ( serverId != event->subject_id ) {
		Com_DPrintf( "Ignored Steam client P2P request from untracked peer %llu.\n",
			(unsigned long long)event->subject_id );
		return;
	}
	const fnqlSteamResult_t result = FNQL_Steam_AcceptP2PSession(
		FNQL_STEAM_ROLE_CLIENT, serverId );
	Com_DPrintf( "Steam client P2P request from active authenticated server %llu: %s\n",
		(unsigned long long)serverId,
		result == FNQL_STEAM_RESULT_OK ? "accepted" : "failed" );
	if ( result == FNQL_STEAM_RESULT_OK ) {
		cl_steamAcceptedP2PServer = serverId;
		cl_steamPendingP2PRemote = 0;
	}
}

void CL_SteamP2PFrame( void ) {
	static const int statsChannel = 0;
	static const int voiceChannel = 1;
	static const uint32_t maxCompressedVoice = 0x4000u;
	static const uint32_t maxDecompressedVoice = 0x8000u;
	static const uint32_t sampleRate = 22050u;
	static const int maxPacketsPerFrame = 64;
	static const int maxStatsPacketsPerFrame = 8;
	static const uint32_t maxStatsBytes = 1024u * 1024u;
	uint64_t serverId = 0;
	const qboolean hasVoice = FNQL_Steam_Available( FNQL_STEAM_CAP_VOICE );
	if ( cls.state != CA_ACTIVE && cl_steamAcceptedP2PServer ) {
		(void)FNQL_Steam_CloseP2PSession( FNQL_STEAM_ROLE_CLIENT,
			cl_steamAcceptedP2PServer );
		cl_steamAcceptedP2PServer = 0;
	}
	if ( !FNQL_Steam_Available( FNQL_STEAM_CAP_CLIENT_P2P )
		|| !CL_Steam_GetCurrentServerP2PIdentity( &serverId ) ) return;
	if ( cl_steamAcceptedP2PServer != serverId ) {
		cl_steamAcceptedP2PServer = 0;
		if ( cl_steamPendingP2PRemote != serverId ) {
			if ( cl_steamPendingP2PRemote ) cl_steamPendingP2PRemote = 0;
			return;
		}
		if ( FNQL_Steam_AcceptP2PSession( FNQL_STEAM_ROLE_CLIENT, serverId )
			!= FNQL_STEAM_RESULT_OK ) return;
		cl_steamAcceptedP2PServer = serverId;
		cl_steamPendingP2PRemote = 0;
		Com_DPrintf( "Accepted deferred Steam P2P request from active authenticated server %llu.\n",
			(unsigned long long)serverId );
	}
	if ( hasVoice && cl_steamVoiceRecording ) {
		byte compressedVoice[maxCompressedVoice];
		uint32_t compressedSize = 0;
		if ( FNQL_Steam_GetCompressedVoice( compressedVoice,
			sizeof( compressedVoice ), &compressedSize ) == FNQL_STEAM_RESULT_OK
			&& compressedSize > 0u && compressedSize <= sizeof( compressedVoice ) ) {
			if ( FNQL_Steam_SendP2PPacket( FNQL_STEAM_ROLE_CLIENT, serverId,
				compressedVoice, compressedSize, 1u, voiceChannel )
				== FNQL_STEAM_RESULT_OK && !cl_steamVoicePacketLogged ) {
				cl_steamVoicePacketLogged = qtrue;
				Com_DPrintf( "Steam voice P2P packet sent: %u bytes.\n",
					compressedSize );
			}
		}
	}
	for ( int packetIndex = 0; packetIndex < maxStatsPacketsPerFrame;
		++packetIndex ) {
		uint32_t packetSize = 0;
		if ( FNQL_Steam_PeekP2PPacket( FNQL_STEAM_ROLE_CLIENT, statsChannel,
			&packetSize ) != FNQL_STEAM_RESULT_OK ) break;
		if ( packetSize == 0 || packetSize > maxStatsBytes ) {
			Com_Printf( S_COLOR_YELLOW
				"Rejected invalid Steam stats packet (%u bytes).\n", packetSize );
			break;
		}
		byte *compressed = static_cast<byte *>( Z_Malloc( packetSize ) );
		char *json = static_cast<char *>( Z_Malloc( maxStatsBytes + 1u ) );
		if ( !compressed || !json ) {
			if ( json ) Z_Free( json );
			if ( compressed ) Z_Free( compressed );
			break;
		}
		uint32_t bytesRead = 0;
		uint64_t remoteId = 0;
		const fnqlSteamResult_t readResult = FNQL_Steam_ReadP2PPacket(
			FNQL_STEAM_ROLE_CLIENT, statsChannel, compressed, packetSize,
			&bytesRead, &remoteId );
		unsigned long jsonSize = maxStatsBytes;
		if ( readResult == FNQL_STEAM_RESULT_OK && remoteId == serverId
			&& bytesRead > 0u && bytesRead <= packetSize
			&& QZ_Uncompress( reinterpret_cast<unsigned char *>( json ),
				&jsonSize, compressed, bytesRead ) == 0
			&& jsonSize > 1u && jsonSize <= maxStatsBytes ) {
			json[jsonSize] = '\0';
			const char first = json[0];
			const char last = json[jsonSize - 1u];
			if ( ( first == '{' && last == '}' )
				|| ( first == '[' && last == ']' ) ) {
				Com_DPrintf( "Steam stats P2P report decoded: %lu bytes.\n",
					jsonSize );
				CL_WebHost_PublishDynamicJsonEvent( "game.stats.report", json );
			}
		}
		Z_Free( json );
		Z_Free( compressed );
	}

	if ( !hasVoice ) return;
	for ( int packetIndex = 0; packetIndex < maxPacketsPerFrame; ++packetIndex ) {
		uint32_t packetSize = 0;
		if ( FNQL_Steam_PeekP2PPacket( FNQL_STEAM_ROLE_CLIENT, voiceChannel,
			&packetSize ) != FNQL_STEAM_RESULT_OK ) break;
		if ( packetSize <= 1u || packetSize > maxCompressedVoice + 1u ) {
			Com_Printf( S_COLOR_YELLOW
				"Rejected invalid Steam voice relay packet (%u bytes).\n", packetSize );
			break;
		}
		byte *packet = static_cast<byte *>( Z_Malloc( packetSize ) );
		if ( !packet ) break;
		uint32_t bytesRead = 0;
		uint64_t remoteId = 0;
		const fnqlSteamResult_t readResult = FNQL_Steam_ReadP2PPacket(
			FNQL_STEAM_ROLE_CLIENT, voiceChannel, packet, packetSize,
			&bytesRead, &remoteId );
		if ( readResult != FNQL_STEAM_RESULT_OK || remoteId != serverId
			|| bytesRead <= 1u || bytesRead > packetSize ) {
			Z_Free( packet );
			continue;
		}
		const int sender = packet[0];
		if ( sender >= MAX_CLIENTS || CL_IsVoiceSenderMuted( sender ) ) {
			Z_Free( packet );
			continue;
		}
		short pcm[maxDecompressedVoice / sizeof( short )];
		uint32_t pcmSize = 0;
		if ( FNQL_Steam_DecompressVoice( packet + 1, bytesRead - 1u, reinterpret_cast<byte *>( pcm ),
			sizeof( pcm ), &pcmSize, sampleRate ) == FNQL_STEAM_RESULT_OK
			&& pcmSize >= 2u && pcmSize <= sizeof( pcm ) && !( pcmSize & 1u ) ) {
			CL_SetClientSpeakingState( sender, qtrue );
			S_AddVoiceSamples( sender, static_cast<int>( pcmSize / sizeof( short ) ),
				static_cast<int>( sampleRate ), pcm );
		}
		Z_Free( packet );
	}
}

static void CL_Steam_PublishUserStats( const fnqlSteamEvent_t *event ) {
	static const size_t payloadCapacity = 256u * 1024u;
	char *json;
	char eventName[112];
	fnqlSteamFriend_t friendInfo = {};
	char escapedPersona[FNQL_STEAM_NAME_CAPACITY * 2];
	size_t statsRead = 0;
	size_t achievementsRead = 0;
	size_t displayAttributesRead = 0;
	const bool readValues = event->result == FNQL_STEAM_RESULT_OK
		&& FNQL_Steam_Available( FNQL_STEAM_CAP_CLIENT_STATS );

	json = static_cast<char *>( Z_Malloc( payloadCapacity ) );
	if ( !json ) return;
	json[0] = '\0';
	friendInfo.size = sizeof( friendInfo );
	escapedPersona[0] = '\0';
	if ( FNQL_Steam_GetFriend( event->subject_id, &friendInfo )
		== FNQL_STEAM_RESULT_OK ) {
		CL_WebUI_JsonEscape( friendInfo.persona_name, escapedPersona,
			sizeof( escapedPersona ) );
	}
	bool complete = CL_WebUI_AppendJsonFormattedIfFits( json,
		payloadCapacity, "{\"ID\":\"%llu\",\"NAME\":\"%s\",\"STATS\":{",
		(unsigned long long)event->subject_id, escapedPersona );
	for ( size_t i = 0; complete && i < fnql::steam::stats::FieldNames.size(); ++i ) {
		int32_t value = 0;
		const char *name = fnql::steam::stats::FieldNames[i].data();
		if ( readValues ) {
			if ( FNQL_Steam_GetClientUserStatI32( event->subject_id, name, &value )
				== FNQL_STEAM_RESULT_OK ) ++statsRead;
		}
		complete = CL_WebUI_AppendJsonFormattedIfFits( json, payloadCapacity,
			"%s\"%s\":%d", i ? "," : "", name, value );
	}
	complete = complete && CL_WebUI_AppendJsonLiteralIfFits( json,
		payloadCapacity, "},\"ACHIEVEMENTS\":{" );
	for ( size_t i = 0; complete && i < fnql::steam::stats::AchievementNames.size(); ++i ) {
		const char *name = fnql::steam::stats::AchievementNames[i].data();
		char displayName[256];
		char description[256];
		char escapedName[sizeof( displayName ) * 2];
		char escapedDescription[sizeof( description ) * 2];
		qboolean unlocked = qfalse;
		uint32_t unlockTime = 0;
		displayName[0] = '\0';
		description[0] = '\0';
		if ( FNQL_Steam_GetAchievementDisplayAttribute( name, "name",
			displayName, sizeof( displayName ) ) == FNQL_STEAM_RESULT_OK ) {
			++displayAttributesRead;
		}
		if ( FNQL_Steam_GetAchievementDisplayAttribute( name, "desc",
			description, sizeof( description ) ) == FNQL_STEAM_RESULT_OK ) {
			++displayAttributesRead;
		}
		if ( readValues ) {
			if ( FNQL_Steam_GetClientUserAchievement( event->subject_id, name,
				&unlocked, &unlockTime ) == FNQL_STEAM_RESULT_OK ) {
				++achievementsRead;
			}
		}
		CL_WebUI_JsonEscape( displayName, escapedName, sizeof( escapedName ) );
		CL_WebUI_JsonEscape( description, escapedDescription,
			sizeof( escapedDescription ) );
		complete = CL_WebUI_AppendJsonFormattedIfFits( json, payloadCapacity,
			"%s\"%s\":{\"ID\":\"%s\",\"NAME\":\"%s\",\"DESC\":\"%s\",\"UNLOCKED\":%d,\"TIME_UNLOCKED\":%u}",
			i ? "," : "", name, name, escapedName, escapedDescription,
			unlocked ? 1 : 0, unlockTime );
	}
	complete = complete && CL_WebUI_AppendJsonLiteralIfFits( json,
		payloadCapacity, "}}" );
	if ( complete ) {
		Com_sprintf( eventName, sizeof( eventName ),
			"users.stats.%llu.received", (unsigned long long)event->subject_id );
		CL_WebHost_PublishDynamicJsonEvent( eventName, json );
		Com_DPrintf( "Steam user stats publication built for %llu: read %u/%u stats, %u/%u achievements, %u/%u display attributes.\n",
			(unsigned long long)event->subject_id,
			(unsigned int)statsRead,
			(unsigned int)fnql::steam::stats::FieldNames.size(),
			(unsigned int)achievementsRead,
			(unsigned int)fnql::steam::stats::AchievementNames.size(),
			(unsigned int)displayAttributesRead,
			(unsigned int)( fnql::steam::stats::AchievementNames.size() * 2u ) );
	} else {
		Com_Printf( S_COLOR_YELLOW
			"Steam user-stats publication exceeded its bounded payload.\n" );
	}
	Z_Free( json );
}

static void CL_Steam_PublishLobbyEnter( uint64_t lobbyId ) {
	static const uint32_t maxMembers = 512;
	static const uint32_t maxDataItems = 256;
	static const size_t payloadCapacity = 1024u * 1024u;
	uint32_t memberCount = 0;
	uint32_t memberLimit = 0;
	uint32_t dataCount = 0;
	uint64_t ownerId = 0;
	fnqlSteamLobbyMember_t *members = NULL;
	fnqlSteamLobbyData_t *data = NULL;
	char *json = NULL;
	char eventName[96];
	fnqlSteamStatus_t status = {};
	bool complete = false;

	const fnqlSteamResult_t memberQuery = FNQL_Steam_GetLobbyMembers( lobbyId,
		NULL, 0, &memberCount, &ownerId, &memberLimit );
	const fnqlSteamResult_t dataQuery = FNQL_Steam_GetLobbyData( lobbyId, NULL,
		0, &dataCount );
	if ( ( memberQuery != FNQL_STEAM_RESULT_OK
			&& memberQuery != FNQL_STEAM_RESULT_BUFFER_TOO_SMALL )
		|| ( dataQuery != FNQL_STEAM_RESULT_OK
			&& dataQuery != FNQL_STEAM_RESULT_BUFFER_TOO_SMALL )
		|| memberCount > maxMembers || dataCount > maxDataItems ) return;
	if ( memberCount ) members = static_cast<fnqlSteamLobbyMember_t *>(
		Z_Malloc( sizeof( *members ) * memberCount ) );
	if ( dataCount ) data = static_cast<fnqlSteamLobbyData_t *>(
		Z_Malloc( sizeof( *data ) * dataCount ) );
	json = static_cast<char *>( Z_Malloc( payloadCapacity ) );
	if ( ( memberCount && !members ) || ( dataCount && !data ) || !json ) goto cleanup;
	if ( FNQL_Steam_GetLobbyMembers( lobbyId, members, memberCount, &memberCount,
		&ownerId, &memberLimit ) != FNQL_STEAM_RESULT_OK
		|| FNQL_Steam_GetLobbyData( lobbyId, data, dataCount, &dataCount )
			!= FNQL_STEAM_RESULT_OK ) goto cleanup;
	status.size = sizeof( status );
	(void)FNQL_Steam_GetStatus( &status );
	json[0] = '\0';
	complete = CL_WebUI_AppendJsonFormattedIfFits( json, payloadCapacity,
		"{\"id\":\"%llu\",\"is_owner\":%s,\"owner\":\"%llu\",\"lobbydata\":{",
		(unsigned long long)lobbyId,
		status.local_steam_id && status.local_steam_id == ownerId ? "true" : "false",
		(unsigned long long)ownerId );
	for ( uint32_t i = 0; complete && i < dataCount; ++i ) {
		char boundedValue[256];
		char escapedKey[FNQL_STEAM_NAME_CAPACITY * 2];
		char escapedValue[sizeof( boundedValue ) * 2];
		Q_strncpyz( boundedValue, data[i].value, sizeof( boundedValue ) );
		CL_WebUI_JsonEscape( data[i].key, escapedKey, sizeof( escapedKey ) );
		CL_WebUI_JsonEscape( boundedValue, escapedValue, sizeof( escapedValue ) );
		complete = CL_WebUI_AppendJsonFormattedIfFits( json, payloadCapacity,
			"%s\"%s\":\"%s\"", i ? "," : "", escapedKey, escapedValue );
	}
	complete = complete && CL_WebUI_AppendJsonFormattedIfFits( json,
		payloadCapacity, "},\"num_players\":%u,\"max_players\":%u,\"players\":{",
		memberCount, memberLimit );
	for ( uint32_t i = 0; complete && i < memberCount; ++i ) {
		char escapedName[FNQL_STEAM_NAME_CAPACITY * 2];
		CL_WebUI_JsonEscape( members[i].persona_name, escapedName,
			sizeof( escapedName ) );
		complete = CL_WebUI_AppendJsonFormattedIfFits( json, payloadCapacity,
			"%s\"%llu\":{\"id\":\"%llu\",\"name\":\"%s\"}",
			i ? "," : "", (unsigned long long)members[i].steam_id,
			(unsigned long long)members[i].steam_id, escapedName );
	}
	complete = complete && CL_WebUI_AppendJsonLiteralIfFits( json,
		payloadCapacity, "}}" );
	if ( complete ) {
		Com_sprintf( eventName, sizeof( eventName ), "lobby.%llu.enter",
			(unsigned long long)lobbyId );
		CL_WebHost_PublishDynamicJsonEvent( eventName, json );
		Com_DPrintf( "Steam lobby projection built: %u members, %u metadata entries.\n",
			memberCount, dataCount );
	}

cleanup:
	if ( json ) Z_Free( json );
	if ( data ) Z_Free( data );
	if ( members ) Z_Free( members );
}

static void CL_Steam_OnProviderEvent( const fnqlSteamEvent_t *event, void *context ) {
	char payload[CL_WEB_EVENT_PAYLOAD_LENGTH];
	(void)context;

	if ( !event || event->size < sizeof( *event ) ) {
		return;
	}
	switch ( event->type ) {
		case FNQL_STEAM_EVENT_SERVER_LIST_START:
			/* The native provider owns this list. Do not erase or repurpose
			 * legacy LAN/global/favorite caches while WebUI is displaying it. */
			break;
		case FNQL_STEAM_EVENT_SERVER_RESPONSE: {
			char detailId[32];
			if ( !cl_webui.serverRefreshActive || !cl_webui.serverRefreshSteam
				|| event->result != FNQL_STEAM_RESULT_OK
				|| event->server.size < sizeof( event->server )
				|| !event->server.ip || !event->server.connection_port
				|| event->server.app_id != (uint32_t)atoi( STEAMPATH_APPID ) ) {
				break;
			}
			CL_WebHost_FormatServerDetailId( event->server.ip,
				event->server.connection_port, detailId, sizeof( detailId ) );
			CL_WebHost_PublishSteamServerResponse( &event->server,
				event->server.ip, event->server.connection_port, detailId );
			break;
		}
		case FNQL_STEAM_EVENT_SERVER_LIST_COMPLETE:
			CL_WebHost_PublishServerBrowserRefreshEnd();
			break;
		case FNQL_STEAM_EVENT_SERVER_DETAIL_RESPONSE:
			if ( cl_webui.serverDetailSteam ) {
				CL_WebHost_PublishSteamServerDetailResponse( &event->server );
			}
			break;
		case FNQL_STEAM_EVENT_SERVER_DETAIL_PLAYER:
			if ( cl_webui.serverDetailSteam ) {
				CL_WebHost_PublishSteamServerDetailPlayer( event );
			}
			break;
		case FNQL_STEAM_EVENT_SERVER_DETAIL_RULE:
			if ( cl_webui.serverDetailSteam ) {
				CL_WebHost_PublishServerDetailRuleResponse( event->text, event->detail );
			}
			break;
		case FNQL_STEAM_EVENT_SERVER_DETAIL_COMPLETE:
			if ( cl_webui.serverDetailSteam && cl_webui.serverDetailActive ) {
				if ( event->flags & 2u ) CL_WebHost_PublishServerDetailPlayersEnd();
				else CL_WebHost_PublishServerDetailPlayersFailed();
				if ( event->flags & 4u ) CL_WebHost_PublishServerDetailRulesEnd();
				else CL_WebHost_PublishServerDetailRulesFailed();
				if ( event->flags & 1u ) {
					CL_WebHost_ClearServerDetailRequest();
				} else {
					cl_webui.serverDetailSteam = qfalse;
					cl_webui.serverDetailTimeoutTime = cls.realtime +
						CL_WEB_SERVER_DETAILS_TIMEOUT_MSEC;
					Cbuf_ExecuteText( EXEC_APPEND, va( "serverstatus %s\n",
						NET_AdrToStringwPort( &cl_webui.serverDetailAddress ) ) );
				}
			}
			break;
		case FNQL_STEAM_EVENT_RICH_PRESENCE_JOIN_REQUESTED:
			CL_Steam_OnRichPresenceJoinRequested( event->text );
			break;
		case FNQL_STEAM_EVENT_GAME_SERVER_CHANGE_REQUESTED:
			CL_Steam_OnGameServerChangeRequested( event->text, event->detail );
			break;
		case FNQL_STEAM_EVENT_LOBBY_CREATED: {
			if ( event->result == FNQL_STEAM_RESULT_OK ) {
				cl_webui.activeLobbyId = event->subject_id;
				(void)FNQL_Steam_SetLobbyData( event->subject_id, "hello", "world" );
			}
			Com_sprintf( payload, sizeof( payload ),
				"{\"lobbyId\":\"%llu\",\"result\":%d}",
				(unsigned long long)event->subject_id, event->result );
			CL_WebView_PublishEvent( "steam.lobby.created", payload );
			if ( event->result == FNQL_STEAM_RESULT_OK ) {
				char eventName[96];
				Com_sprintf( eventName, sizeof( eventName ), "lobby.%llu.create",
					(unsigned long long)event->subject_id );
				Com_sprintf( payload, sizeof( payload ),
					"{\"id\":\"%llu\",\"status\":1}",
					(unsigned long long)event->subject_id );
				CL_WebView_PublishEvent( eventName, payload );
			} else {
				Com_sprintf( payload, sizeof( payload ),
					"{\"code\":%d,\"message\":\"Unable to create lobby\"}",
					event->result );
				CL_WebView_PublishEvent( "lobby.error", payload );
			}
			break;
		}
		case FNQL_STEAM_EVENT_LOBBY_ENTERED:
			if ( event->result == FNQL_STEAM_RESULT_OK ) {
				cl_webui.activeLobbyId = event->subject_id;
			}
			Com_sprintf( payload, sizeof( payload ),
				"{\"lobbyId\":\"%llu\",\"result\":%d}",
				(unsigned long long)event->subject_id, event->result );
			CL_WebView_PublishEvent( "steam.lobby.entered", payload );
			if ( event->result == FNQL_STEAM_RESULT_OK ) {
				CL_Steam_PublishLobbyEnter( event->subject_id );
			}
			break;
		case FNQL_STEAM_EVENT_LOBBY_LEFT:
			cl_webui.activeLobbyId = 0;
			CL_WebView_PublishEvent( "steam.lobby.left", NULL );
			break;
		case FNQL_STEAM_EVENT_LOBBY_CHAT_MESSAGE: {
			char escapedMessage[FNQL_STEAM_TEXT_CAPACITY * 2];
			char escapedName[FNQL_STEAM_NAME_CAPACITY * 2];
			char eventName[96];
			fnqlSteamFriend_t sender = {};
			sender.size = sizeof( sender );
			escapedName[0] = '\0';
			if ( FNQL_Steam_GetFriend( event->value, &sender )
				== FNQL_STEAM_RESULT_OK ) {
				CL_WebUI_JsonEscape( sender.persona_name, escapedName,
					sizeof( escapedName ) );
			}
			CL_WebUI_JsonEscape( event->text, escapedMessage,
				sizeof( escapedMessage ) );
			Com_sprintf( payload, sizeof( payload ),
				"{\"id\":\"%llu\",\"name\":\"%s\",\"msg\":\"%s\"}",
				(unsigned long long)event->value, escapedName, escapedMessage );
			Com_sprintf( eventName, sizeof( eventName ), "lobby.%llu.chat",
				(unsigned long long)event->subject_id );
			CL_WebView_PublishEvent( eventName, payload );
			break;
		}
		case FNQL_STEAM_EVENT_LOBBY_CHAT_UPDATE: {
			char eventName[112];
			char escapedName[FNQL_STEAM_NAME_CAPACITY * 2];
			fnqlSteamFriend_t changedUser = {};
			changedUser.size = sizeof( changedUser );
			escapedName[0] = '\0';
			if ( FNQL_Steam_GetFriend( event->value, &changedUser )
				== FNQL_STEAM_RESULT_OK ) {
				CL_WebUI_JsonEscape( changedUser.persona_name, escapedName,
					sizeof( escapedName ) );
			}
			Com_sprintf( payload, sizeof( payload ),
				"{\"id\":\"%llu\",\"name\":\"%s\",\"state\":%u,\"changedBy\":\"%s\"}",
				(unsigned long long)event->value, escapedName, event->flags,
				event->detail );
			Com_sprintf( eventName, sizeof( eventName ), "lobby.%llu.user.%s",
				(unsigned long long)event->subject_id,
				( event->flags & 1u ) ? "joined" : "left" );
			CL_WebView_PublishEvent( eventName, payload );
			break;
		}
		case FNQL_STEAM_EVENT_LOBBY_DATA_UPDATED: {
			char eventName[96];
			Com_sprintf( payload, sizeof( payload ),
				"{\"id\":\"%llu\",\"memberId\":\"%llu\",\"result\":%d}",
				(unsigned long long)event->subject_id,
				(unsigned long long)event->value, event->result );
			Com_sprintf( eventName, sizeof( eventName ), "lobby.%llu.updated",
				(unsigned long long)event->subject_id );
			CL_WebView_PublishEvent( eventName, payload );
			break;
		}
		case FNQL_STEAM_EVENT_LOBBY_GAME_CREATED: {
			char eventName[96];
			Com_sprintf( payload, sizeof( payload ),
				"{\"ip\":%u,\"port\":%u,\"id\":\"%llu\",\"serverId\":\"%llu\"}",
				event->server.ip, (unsigned int)event->server.connection_port,
				(unsigned long long)event->subject_id,
				(unsigned long long)event->value );
			Com_sprintf( eventName, sizeof( eventName ), "lobby.%llu.game_created",
				(unsigned long long)event->subject_id );
			CL_WebView_PublishEvent( eventName, payload );
			break;
		}
		case FNQL_STEAM_EVENT_LOBBY_KICKED: {
			char eventName[96];
			if ( cl_webui.activeLobbyId == event->subject_id ) {
				cl_webui.activeLobbyId = 0;
			}
			Com_sprintf( payload, sizeof( payload ),
				"{\"id\":\"%llu\",\"adminId\":\"%llu\",\"disconnected\":%s}",
				(unsigned long long)event->subject_id,
				(unsigned long long)event->value,
				event->flags ? "true" : "false" );
			Com_sprintf( eventName, sizeof( eventName ), "lobby.%llu.kicked",
				(unsigned long long)event->subject_id );
			CL_WebView_PublishEvent( eventName, payload );
			break;
		}
		case FNQL_STEAM_EVENT_LOBBY_JOIN_REQUESTED: {
			char eventName[104];
			Com_sprintf( payload, sizeof( payload ),
				"{\"id\":\"%llu\",\"friendId\":\"%llu\"}",
				(unsigned long long)event->subject_id,
				(unsigned long long)event->value );
			Com_sprintf( eventName, sizeof( eventName ), "lobby.%llu.join_requested",
				(unsigned long long)event->subject_id );
			CL_WebView_PublishEvent( eventName, payload );
			break;
		}
		case FNQL_STEAM_EVENT_UGC_DOWNLOAD_COMPLETE:
		case FNQL_STEAM_EVENT_UGC_ITEM_INSTALLED: {
			char escapedFolder[FNQL_STEAM_TEXT_CAPACITY * 2];
			CL_WebUI_JsonEscape( event->text, escapedFolder,
				sizeof( escapedFolder ) );
			Com_sprintf( payload, sizeof( payload ),
				"{\"itemId\":\"%llu\",\"result\":%d,\"folder\":\"%s\",\"sizeOnDisk\":\"%llu\",\"timestamp\":%u}",
				(unsigned long long)event->subject_id, event->result, escapedFolder,
				(unsigned long long)event->value, event->flags );
			CL_WebView_PublishEvent(
				event->type == FNQL_STEAM_EVENT_UGC_ITEM_INSTALLED
					? "web.ugc.item.installed" : "web.ugc.download.complete",
				payload );
			break;
		}
		case FNQL_STEAM_EVENT_UGC_QUERY_COMPLETE:
			if ( event->result == FNQL_STEAM_RESULT_OK
				&& CL_Steam_ConsumeUgcQueryResults() ) break;
			CL_WebHost_UpdateBrowserUGCList( "[]" );
			CL_WebView_PublishEvent( "web.ugc.failed", NULL );
			break;
		case FNQL_STEAM_EVENT_MICROTRANSACTION_AUTHORIZATION:
			Com_sprintf( payload, sizeof( payload ),
				"{\"appid\":%u,\"orderid\":\"%llu\",\"authorized\":%u}",
				event->flags, (unsigned long long)event->subject_id,
				event->value ? 1u : 0u );
			Com_DPrintf( "Steam microtransaction authorization: appid=%u order=%llu authorized=%u\n",
				event->flags, (unsigned long long)event->subject_id,
				event->value ? 1u : 0u );
			CL_WebView_PublishEvent( "microtxn.authorization", payload );
			break;
		case FNQL_STEAM_EVENT_P2P_SESSION_REQUEST:
		case FNQL_STEAM_EVENT_P2P_SESSION_FAILURE:
			CL_Steam_HandleP2PEvent( event );
			break;
		case FNQL_STEAM_EVENT_AVATAR_IMAGE_LOADED:
			CL_InvalidateAvatarImageHandle( event->subject_id );
			Com_sprintf( payload, sizeof( payload ),
				"{\"steamId\":\"%llu\",\"width\":%u,\"height\":%u}",
				(unsigned long long)event->subject_id, event->flags & 0xffffu,
				event->flags >> 16 );
			CL_WebView_PublishEvent( "steam.avatar.loaded", payload );
			break;
		case FNQL_STEAM_EVENT_PERSONA_STATE_CHANGED:
		case FNQL_STEAM_EVENT_FRIEND_RICH_PRESENCE_UPDATED: {
			fnqlSteamFriend_t friendInfo = {};
			char friendJson[4096];
			char eventName[112];
			friendInfo.size = sizeof( friendInfo );
			friendJson[0] = '\0';
			if ( FNQL_Steam_GetFriend( event->subject_id, &friendInfo )
				== FNQL_STEAM_RESULT_OK ) {
				CL_WebHost_FormatSteamFriendEventJson( &friendInfo,
					friendJson, sizeof( friendJson ) );
			}
			if ( !friendJson[0] ) {
				Com_sprintf( friendJson, sizeof( friendJson ),
					"{\"id\":\"%llu\",\"steamId\":\"%llu\"}",
					(unsigned long long)event->subject_id,
					(unsigned long long)event->subject_id );
			}
			if ( event->type == FNQL_STEAM_EVENT_PERSONA_STATE_CHANGED ) {
				if ( event->flags & 0x40u ) {
					CL_InvalidateAvatarImageHandle( event->subject_id );
				}
				Com_sprintf( payload, sizeof( payload ),
					"{\"id\":\"%llu\",\"state\":%u,\"friend\":%s}",
					(unsigned long long)event->subject_id, event->flags,
					friendJson );
				Com_sprintf( eventName, sizeof( eventName ),
					"users.persona.%llu.change",
					(unsigned long long)event->subject_id );
			} else {
				char escapedStatus[FNQL_STEAM_TEXT_CAPACITY * 2];
				char escapedLanIp[128];
				CL_WebUI_JsonEscape( friendInfo.status, escapedStatus,
					sizeof( escapedStatus ) );
				CL_WebUI_JsonEscape( friendInfo.lan_ip, escapedLanIp,
					sizeof( escapedLanIp ) );
				Com_sprintf( payload, sizeof( payload ),
					"{\"id\":\"%llu\",\"status\":\"%s\",\"lanIp\":\"%s\",\"friend\":%s}",
					(unsigned long long)event->subject_id, escapedStatus,
					escapedLanIp, friendJson );
				Com_sprintf( eventName, sizeof( eventName ),
					"users.presence.%llu.change",
					(unsigned long long)event->subject_id );
			}
			CL_WebView_PublishEvent( eventName, payload );
			break;
		}
		case FNQL_STEAM_EVENT_USER_STATS_RECEIVED:
			CL_Steam_PublishUserStats( event );
			Com_sprintf( payload, sizeof( payload ),
				"{\"steamId\":\"%llu\",\"result\":%d}",
				(unsigned long long)event->subject_id, event->result );
			CL_WebView_PublishEvent( "steam.userstats.received", payload );
			break;
		case FNQL_STEAM_EVENT_OVERLAY_STATE:
			CL_WebView_PublishEvent( "steam.overlay.active", event->text );
			break;
		case FNQL_STEAM_EVENT_WARNING:
			Com_Printf( S_COLOR_YELLOW "Steam provider: %s\n", event->detail );
			break;
		default:
			break;
	}
}

qboolean CL_Steam_OpenOverlayUrl( const char *url ) {
	if ( !url || !url[0] ) {
		return qfalse;
	}
	if ( CL_Steam_ResultAccepted( FNQL_Steam_OpenOverlayUrl( url ) ) ) {
		return qtrue;
	}
	return CL_WebHost_SteamBridgeUnavailable( "OpenSteamOverlayURL", url );
}

void CL_Steam_OnRichPresenceJoinRequested( const char *command ) {
	const char *connectCommand = command;
	if ( connectCommand && connectCommand[0] == '+' ) {
		connectCommand++;
	}
	if ( CL_Steam_SafeCommandText( connectCommand )
		&& !Q_stricmpn( connectCommand, "connect ", 8 ) ) {
		Cbuf_ExecuteText( EXEC_APPEND, va( "%s\n", connectCommand ) );
		CL_WebView_PublishEvent( "steam.rich_presence_join_requested", NULL );
		return;
	}
	CL_Steam_LogOnlineCallbackIgnored(
		"rich_presence_join_requested",
		( command && command[0] ) ? "unsafe or unsupported join command" : "missing join command" );
}

void CL_Steam_OnGameServerChangeRequested( const char *server, const char *password ) {
	if ( CL_Steam_SafeCommandText( server ) ) {
		Cvar_Set( "password", password ? password : "" );
		Cbuf_ExecuteText( EXEC_APPEND, va( "connect %s\n", server ) );
		CL_WebView_PublishEvent( "steam.server_change_requested", NULL );
		return;
	}
	CL_Steam_LogOnlineCallbackIgnored(
		"server_change_requested",
		( server && server[0] ) ? "unsafe server target" : "missing server target" );
}

qboolean CL_Steam_CreateLobby( void ) {
	int maxMembers = Cvar_VariableIntegerValue( "steam_maxLobbyClients" );
	if ( maxMembers < 1 ) {
		maxMembers = 1;
	} else if ( maxMembers > 64 ) {
		maxMembers = 64;
	}
	if ( CL_Steam_ResultAccepted( FNQL_Steam_CreateLobby( 1u, (uint32_t)maxMembers ) ) ) {
		return qtrue;
	}
	return CL_WebHost_SteamBridgeUnavailable( "createlobby", "" );
}

qboolean CL_Steam_LeaveLobby( void ) {
	if ( CL_Steam_ResultAccepted( FNQL_Steam_LeaveLobby( cl_webui.activeLobbyId ) ) ) {
		cl_webui.activeLobbyId = 0;
		return qtrue;
	}
	return CL_WebHost_SteamBridgeUnavailable( "leavelobby", "" );
}

qboolean CL_Steam_JoinLobby( const char *lobbyId ) {
	uint64_t parsedLobbyId;
	if ( CL_Steam_ParseIdentity( lobbyId, &parsedLobbyId )
		&& CL_Steam_ResultAccepted( FNQL_Steam_JoinLobby( parsedLobbyId ) ) ) {
		cl_webui.activeLobbyId = parsedLobbyId;
		return qtrue;
	}
	return CL_WebHost_SteamBridgeUnavailable( "joinlobby", lobbyId ? lobbyId : "" );
}

qboolean CL_Steam_SetLobbyServer( unsigned int serverIp, unsigned short serverPort ) {
	char payload[64];

	if ( CL_Steam_ResultAccepted( FNQL_Steam_SetLobbyServer(
		cl_webui.activeLobbyId, serverIp, serverPort, 0 ) ) ) {
		return qtrue;
	}
	Com_sprintf( payload, sizeof( payload ), "%u\n%u", serverIp, (unsigned int)serverPort );
	return CL_WebHost_SteamBridgeUnavailable( "setlobbyserver", payload );
}

qboolean CL_Steam_ShowInviteOverlay( void ) {
	if ( CL_Steam_ResultAccepted( FNQL_Steam_OpenOverlayUser(
		"LobbyInvite", cl_webui.activeLobbyId ) ) ) {
		return qtrue;
	}
	return CL_WebHost_SteamBridgeUnavailable( "showinviteoverlay", "" );
}

qboolean CL_Steam_Invite( const char *steamId ) {
	uint64_t parsedSteamId;
	if ( CL_Steam_ParseIdentity( steamId, &parsedSteamId ) ) {
		if ( cls.state != CA_ACTIVE ) {
			if ( CL_Steam_ResultAccepted( FNQL_Steam_InviteToLobby(
				cl_webui.activeLobbyId, parsedSteamId ) ) ) return qtrue;
		} else {
			uint64_t serverId = 0;
			char connectString[128];
			/* Only advertise a target whose retail configstring proves it is an
			 * FnQL Steam peer; never turn this into a retail-server invite path. */
			if ( CL_Steam_GetCurrentServerP2PIdentity( &serverId ) ) {
				Com_sprintf( connectString, sizeof( connectString ), "+connect %s",
					NET_AdrToStringwPort( &clc.serverAddress ) );
				if ( CL_Steam_ResultAccepted( FNQL_Steam_InviteToGame(
					parsedSteamId, connectString ) ) ) return qtrue;
			}
		}
	}
	return CL_WebHost_SteamBridgeUnavailable( "invite", steamId ? steamId : "" );
}

qboolean CL_Steam_SayLobby( const char *message ) {
	const uint32_t length = message ? (uint32_t)strlen( message ) + 1u : 0u;
	if ( length > 1u && CL_Steam_ResultAccepted( FNQL_Steam_SendLobbyChat(
		cl_webui.activeLobbyId, message, length ) ) ) {
		return qtrue;
	}
	return CL_WebHost_SteamBridgeUnavailable( "saylobby", message ? message : "" );
}

qboolean CL_Steam_RequestAllUGC( int filter ) {
	char payload[32];
	uint32_t count = 0;
	uint64_t *itemIds = NULL;
	char *json = NULL;
	fnqlSteamResult_t result;
	qboolean success = qfalse;

	Com_sprintf( payload, sizeof( payload ), "%d", filter );
	if ( FNQL_Steam_Available( FNQL_STEAM_CAP_UGC_QUERY ) ) {
		result = FNQL_Steam_RequestUgcQuery( (uint32_t)filter );
		if ( result == FNQL_STEAM_RESULT_PENDING ) return qtrue;
		CL_WebHost_UpdateBrowserUGCList( "[]" );
		CL_WebView_PublishEvent( "web.ugc.failed", NULL );
		return CL_WebHost_SteamBridgeUnavailable( "getallugc", payload );
	}
	result = FNQL_Steam_GetSubscribedItems( NULL, 0, &count );
	if ( result == FNQL_STEAM_RESULT_OK && count == 0 ) {
		CL_WebHost_UpdateBrowserUGCList( "[]" );
		CL_WebView_PublishEvent( "web.ugc.complete", NULL );
		return qtrue;
	}
	if ( result == FNQL_STEAM_RESULT_BUFFER_TOO_SMALL && count > 0 && count <= 4096u ) {
		itemIds = (uint64_t *)Z_Malloc( sizeof( *itemIds ) * count );
		json = (char *)Z_Malloc( CL_WEB_CATALOG_JSON_LENGTH );
	}
	if ( itemIds && json
		&& FNQL_Steam_GetSubscribedItems( itemIds, count, &count ) == FNQL_STEAM_RESULT_OK ) {
		static const uint32_t steamItemStateInstalled = 1u << 2;
		json[0] = '\0';
		CL_WebUI_AppendJsonLiteralIfFits( json, CL_WEB_CATALOG_JSON_LENGTH, "[" );
		for ( uint32_t i = 0; i < count; ++i ) {
			uint32_t stateFlags = 0;
			uint64_t sizeOnDisk = 0;
			uint32_t timestamp = 0;
			char folder[FNQL_STEAM_PATH_CAPACITY];
			char escapedFolder[FNQL_STEAM_PATH_CAPACITY * 2];
			folder[0] = '\0';
			escapedFolder[0] = '\0';
			(void)FNQL_Steam_GetItemState( itemIds[i], &stateFlags );
			if ( stateFlags & steamItemStateInstalled ) {
				if ( FNQL_Steam_GetItemInstallInfo( itemIds[i], folder,
					sizeof( folder ), &sizeOnDisk, &timestamp ) ==
					FNQL_STEAM_RESULT_OK ) {
					CL_WebUI_JsonEscape( folder, escapedFolder,
						sizeof( escapedFolder ) );
				}
			}
			if ( !CL_WebUI_AppendJsonFormattedIfFits( json, CL_WEB_CATALOG_JSON_LENGTH,
				"%s{\"publishedFileId\":\"%llu\",\"id\":\"%llu\",\"state\":%u,\"installed\":%s,\"folder\":\"%s\",\"sizeOnDisk\":\"%llu\",\"timestamp\":%u}",
				i ? "," : "", (unsigned long long)itemIds[i],
				(unsigned long long)itemIds[i], stateFlags,
				( stateFlags & steamItemStateInstalled ) ? "true" : "false",
				escapedFolder, (unsigned long long)sizeOnDisk, timestamp ) ) {
				break;
			}
		}
		CL_WebUI_AppendJsonLiteralIfFits( json, CL_WEB_CATALOG_JSON_LENGTH, "]" );
		CL_WebHost_UpdateBrowserUGCList( json );
		CL_WebView_PublishEvent( "web.ugc.complete", NULL );
		success = qtrue;
	}
	if ( json ) {
		Z_Free( json );
	}
	if ( itemIds ) {
		Z_Free( itemIds );
	}
	if ( success ) {
		return qtrue;
	}
	CL_WebHost_UpdateBrowserUGCList( "[]" );
	CL_WebView_PublishEvent( "web.ugc.failed", NULL );
	return CL_WebHost_SteamBridgeUnavailable( "getallugc", payload );
}

qboolean CL_Steam_RequestUserStats( const char *steamId ) {
	uint64_t parsedSteamId;
	if ( CL_Steam_ParseIdentity( steamId, &parsedSteamId )
		&& CL_Steam_ResultAccepted( FNQL_Steam_RequestClientUserStats(
			parsedSteamId ) ) ) {
		return qtrue;
	}
	return CL_WebHost_SteamBridgeUnavailable( "requestuserstats", steamId ? steamId : "" );
}

qboolean CL_Steam_ActivateOverlayToUser( const char *dialog, const char *steamId ) {
	char payload[MAX_TOKEN_CHARS];

	uint64_t parsedSteamId;
	if ( dialog && dialog[0] && CL_Steam_ParseIdentity( steamId, &parsedSteamId )
		&& CL_Steam_ResultAccepted( FNQL_Steam_OpenOverlayUser( dialog, parsedSteamId ) ) ) {
		return qtrue;
	}
	Com_sprintf( payload, sizeof( payload ), "%s\n%s", dialog ? dialog : "", steamId ? steamId : "" );
	return CL_WebHost_SteamBridgeUnavailable( "activategameoverlaytouser", payload );
}

qboolean CL_Steam_GetItemDownloadInfo( unsigned int itemIdLow, unsigned int itemIdHigh, unsigned long long *outDownloaded, unsigned long long *outTotal ) {
	uint64_t downloaded = 0;
	uint64_t total = 0;
	if ( outDownloaded ) {
		*outDownloaded = 0ull;
	}
	if ( outTotal ) {
		*outTotal = 0ull;
	}

	if ( !outDownloaded || !outTotal ) {
		return qfalse;
	}
	if ( FNQL_Steam_GetItemDownloadInfo(
		( (uint64_t)itemIdHigh << 32 ) | itemIdLow,
		&downloaded, &total ) != FNQL_STEAM_RESULT_OK ) {
		return qfalse;
	}
	*outDownloaded = (unsigned long long)downloaded;
	*outTotal = (unsigned long long)total;
	return qtrue;
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
		char *factoryJson = CL_WebHost_AllocateFactoryListJson();
		if ( factoryJson ) {
			cl_webui.factoryCatalogSynced =
				CL_WebHost_UpdateBrowserFactoryList( factoryJson );
			Z_Free( factoryJson );
		}
		return;
	}

	if ( !Q_stricmp( kind, "friends" ) ) {
		char *friendJson = static_cast<char *>( Z_Malloc( CL_WEB_FRIEND_JSON_LENGTH ) );
		if ( friendJson ) {
			CL_WebHost_BuildFriendListJson( friendJson, CL_WEB_FRIEND_JSON_LENGTH );
			CL_WebHost_UpdateBrowserFriendList( friendJson );
			Z_Free( friendJson );
		}
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

	// Retail can queue SendGameCommand("quit") from the preload shell while the
	// final document is still loading. The native request queue remains safe to
	// drain during that handoff; snapshot/live-event synchronization is gated
	// separately.
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
	if ( serverAddress->type == NA_LOOPBACK &&
		FNQL_Steam_Available( FNQL_STEAM_CAP_GAME_SERVER_METADATA ) ) {
		uint32_t publicIp = 0;
		if ( FNQL_Steam_GetGameServerPublicIp( &publicIp ) ==
			FNQL_STEAM_RESULT_OK && publicIp != 0u ) {
			packedIp = publicIp;
			const int configuredPort = Cvar_VariableIntegerValue( "net_port" );
			if ( configuredPort > 0 && configuredPort <= 65535 ) {
				port = static_cast<unsigned int>( configuredPort );
			}
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

static int CL_WebHost_MapCursorCoordinate( int coordinate, int sourceDimension,
	int targetDimension ) {
	if ( sourceDimension <= 0 || targetDimension <= 0 ) {
		return coordinate;
	}

	if ( coordinate < 0 ) {
		coordinate = 0;
	} else if ( coordinate >= sourceDimension ) {
		coordinate = sourceDimension - 1;
	}

	return (int)( ( (double)coordinate / (double)sourceDimension )
		* (double)targetDimension + 0.5 );
}

void CL_WebView_OnMouseMove( int x, int y ) {
	const fnql::webui::BackendStatus status =
		fnql::webui::ClientBackendHost().Status();

	x = CL_WebHost_MapCursorCoordinate( x, cls.glconfig.vidWidth,
		status.surface.width );
	y = CL_WebHost_MapCursorCoordinate( y, cls.glconfig.vidHeight,
		status.surface.height );
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

static uint32_t CL_WebUI_PngCrc32( const byte *data, size_t length ) {
	uint32_t crc = 0xffffffffu;
	for ( size_t i = 0; i < length; ++i ) {
		crc ^= data[i];
		for ( int bit = 0; bit < 8; ++bit ) {
			crc = ( crc & 1u ) ? ( crc >> 1 ) ^ 0xedb88320u : crc >> 1;
		}
	}
	return ~crc;
}

static uint32_t CL_WebUI_PngAdler32( const byte *data, size_t length ) {
	uint32_t first = 1;
	uint32_t second = 0;
	for ( size_t i = 0; i < length; ++i ) {
		first = ( first + data[i] ) % 65521u;
		second = ( second + first ) % 65521u;
	}
	return ( second << 16 ) | first;
}

static void CL_WebUI_PngWriteU32( byte *output, uint32_t value ) {
	output[0] = static_cast<byte>( value >> 24 );
	output[1] = static_cast<byte>( value >> 16 );
	output[2] = static_cast<byte>( value >> 8 );
	output[3] = static_cast<byte>( value );
}

static byte *CL_WebUI_PngWriteChunk( byte *output, uint32_t type,
	const byte *data, uint32_t length ) {
	CL_WebUI_PngWriteU32( output, length );
	output += 4;
	CL_WebUI_PngWriteU32( output, type );
	output += 4;
	if ( data && length ) {
		Com_Memcpy( output, data, length );
	}
	const uint32_t crc = CL_WebUI_PngCrc32( output - 4,
		static_cast<size_t>( length ) + 4u );
	output += length;
	CL_WebUI_PngWriteU32( output, crc );
	return output + 4;
}

static qboolean CL_WebUI_EncodeAvatarPng( const byte *rgba, uint32_t width,
	uint32_t height, void **outBuffer, int *outLength ) {
	constexpr byte signature[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
	constexpr uint32_t ihdrType = 0x49484452u;
	constexpr uint32_t idatType = 0x49444154u;
	constexpr uint32_t iendType = 0x49454e44u;
	byte header[13]{};
	byte *raw = nullptr;
	byte *compressed = nullptr;
	byte *png = nullptr;

	if ( outBuffer ) {
		*outBuffer = nullptr;
	}
	if ( outLength ) {
		*outLength = 0;
	}
	if ( !rgba || !width || !height || !outBuffer || !outLength
		|| width > 1024u || height > 1024u ) {
		return qfalse;
	}

	const size_t rowBytes = static_cast<size_t>( width ) * 4u;
	if ( rowBytes > ( std::numeric_limits<size_t>::max )() - 1u
		|| static_cast<size_t>( height )
			> ( std::numeric_limits<size_t>::max )() / ( rowBytes + 1u ) ) {
		return qfalse;
	}
	const size_t rawBytes = ( rowBytes + 1u ) * static_cast<size_t>( height );
	const size_t blockCount = ( rawBytes + 65534u ) / 65535u;
	if ( blockCount > ( std::numeric_limits<size_t>::max )() / 5u ) {
		return qfalse;
	}
	const size_t idatBytes = 2u + rawBytes + blockCount * 5u + 4u;
	const size_t pngBytes = 8u + 12u + 13u + 12u + idatBytes + 12u;
	if ( idatBytes > 0xffffffffu || pngBytes > CL_WEB_MAX_RESOURCE_BYTES
		|| pngBytes > static_cast<size_t>( ( std::numeric_limits<int>::max )() ) ) {
		return qfalse;
	}

	raw = static_cast<byte *>( Z_Malloc( rawBytes ) );
	compressed = static_cast<byte *>( Z_Malloc( idatBytes ) );
	png = static_cast<byte *>( Z_Malloc( pngBytes ) );
	if ( !raw || !compressed || !png ) {
		if ( png ) Z_Free( png );
		if ( compressed ) Z_Free( compressed );
		if ( raw ) Z_Free( raw );
		return qfalse;
	}

	size_t rawPosition = 0;
	for ( uint32_t row = 0; row < height; ++row ) {
		raw[rawPosition++] = 0; // PNG filter: None
		Com_Memcpy( raw + rawPosition,
			rgba + static_cast<size_t>( row ) * rowBytes, rowBytes );
		rawPosition += rowBytes;
	}

	size_t compressedPosition = 0;
	compressed[compressedPosition++] = 0x78;
	compressed[compressedPosition++] = 0x01;
	for ( size_t copied = 0; copied < rawBytes; ) {
		const size_t remaining = rawBytes - copied;
		const uint16_t blockBytes = static_cast<uint16_t>(
			remaining > 65535u ? 65535u : remaining );
		const uint16_t inverse = static_cast<uint16_t>( ~blockBytes );
		compressed[compressedPosition++] = remaining <= 65535u ? 0x01 : 0x00;
		compressed[compressedPosition++] = static_cast<byte>( blockBytes );
		compressed[compressedPosition++] = static_cast<byte>( blockBytes >> 8 );
		compressed[compressedPosition++] = static_cast<byte>( inverse );
		compressed[compressedPosition++] = static_cast<byte>( inverse >> 8 );
		Com_Memcpy( compressed + compressedPosition, raw + copied, blockBytes );
		compressedPosition += blockBytes;
		copied += blockBytes;
	}
	CL_WebUI_PngWriteU32( compressed + compressedPosition,
		CL_WebUI_PngAdler32( raw, rawBytes ) );
	compressedPosition += 4;

	CL_WebUI_PngWriteU32( header, width );
	CL_WebUI_PngWriteU32( header + 4, height );
	header[8] = 8;
	header[9] = 6; // RGBA
	Com_Memcpy( png, signature, sizeof( signature ) );
	byte *output = png + sizeof( signature );
	output = CL_WebUI_PngWriteChunk( output, ihdrType, header, sizeof( header ) );
	output = CL_WebUI_PngWriteChunk( output, idatType, compressed,
		static_cast<uint32_t>( compressedPosition ) );
	output = CL_WebUI_PngWriteChunk( output, iendType, nullptr, 0 );

	Z_Free( compressed );
	Z_Free( raw );
	if ( static_cast<size_t>( output - png ) != pngBytes ) {
		Z_Free( png );
		return qfalse;
	}
	*outBuffer = png;
	*outLength = static_cast<int>( pngBytes );
	return qtrue;
}

static qboolean CL_WebUI_ParseSteamAvatarPath( const char *path,
	uint64_t *steamId, uint32_t *avatarSize ) {
	struct Prefix {
		const char *text;
		uint32_t size;
	};
	static const Prefix prefixes[] = {
		{ "asset://steam/avatar/small/", FNQL_STEAM_AVATAR_SMALL },
		{ "asset://steam/avatar/medium/", FNQL_STEAM_AVATAR_MEDIUM },
		{ "asset://steam/avatar/large/", FNQL_STEAM_AVATAR_LARGE },
		{ "steam://avatar/small/", FNQL_STEAM_AVATAR_SMALL },
		{ "steam://avatar/medium/", FNQL_STEAM_AVATAR_MEDIUM },
		{ "steam://avatar/large/", FNQL_STEAM_AVATAR_LARGE },
		{ "asset://steam/avatar/", FNQL_STEAM_AVATAR_LARGE },
		{ "steam://avatar/", FNQL_STEAM_AVATAR_LARGE }
	};
	if ( !path || !steamId || !avatarSize ) {
		return qfalse;
	}
	for ( const Prefix &prefix : prefixes ) {
		const size_t prefixLength = strlen( prefix.text );
		if ( Q_stricmpn( path, prefix.text, static_cast<int>( prefixLength ) ) ) {
			continue;
		}
		char identity[32];
		const char *source = path + prefixLength;
		size_t length = 0;
		while ( source[length] && source[length] != '?' && source[length] != '#'
			&& length + 1u < sizeof( identity ) ) {
			identity[length] = source[length];
			++length;
		}
		identity[length] = '\0';
		if ( source[length] && source[length] != '?' && source[length] != '#' ) {
			return qfalse;
		}
		if ( CL_Steam_ParseIdentity( identity, steamId ) ) {
			*avatarSize = prefix.size;
			return qtrue;
		}
	}
	return qfalse;
}

static qboolean CL_WebUI_RequestSteamAvatarPng( const char *path,
	void **outBuffer, int *outLength ) {
	uint64_t steamId = 0;
	uint32_t avatarSize = FNQL_STEAM_AVATAR_LARGE;
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t required = 0;
	if ( !CL_WebUI_ParseSteamAvatarPath( path, &steamId, &avatarSize )
		|| !FNQL_Steam_Available( FNQL_STEAM_CAP_AVATARS ) ) {
		return qfalse;
	}

	const fnqlSteamResult_t query = FNQL_Steam_GetAvatarRGBA( steamId,
		avatarSize, nullptr, 0, &width, &height, &required );
	const uint64_t expected = static_cast<uint64_t>( width )
		* static_cast<uint64_t>( height ) * 4ull;
	if ( query != FNQL_STEAM_RESULT_BUFFER_TOO_SMALL || !width || !height
		|| expected != required || required > 4u * 1024u * 1024u ) {
		return qfalse;
	}

	byte *rgba = static_cast<byte *>( Z_Malloc( required ) );
	if ( !rgba ) {
		return qfalse;
	}
	const fnqlSteamResult_t loaded = FNQL_Steam_GetAvatarRGBA( steamId,
		avatarSize, rgba, required, &width, &height, &required );
	const qboolean encoded = loaded == FNQL_STEAM_RESULT_OK
		? CL_WebUI_EncodeAvatarPng( rgba, width, height, outBuffer, outLength )
		: qfalse;
	Z_Free( rgba );
	return encoded;
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
	if ( !CL_WebUI_RequestSteamAvatarPng( path, &buffer, &length )
		&& !CL_LauncherRequestData( path, &buffer, &length ) ) {
		return false;
	}
	if ( !buffer || length <= 0 || length > CL_WEB_MAX_RESOURCE_BYTES ) {
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

qboolean CL_Awesomium_Startup( const char *runtimePath, const char *basePath, const char *retailPath, const char *playerName, unsigned int appId, unsigned int steamIdLow, unsigned int steamIdHigh, int width, int height, const char *initialConfigJson, const char *initialMapJson, const char *initialFactoryJson, const char *initialFriendJson ) {
	fnql::webui::StartupParameters parameters;
	char escapedPlayerName[MAX_CVAR_VALUE_STRING * 2];
	char preloadConfigJson[1024];
	char *startupScript;
	qboolean started;
	const uint64_t steamId = ( static_cast<uint64_t>( steamIdHigh ) << 32 )
		| static_cast<uint64_t>( steamIdLow );
	parameters.runtimePath = runtimePath ? runtimePath : "";
	parameters.basePath = basePath ? basePath : "";
	parameters.retailPath = retailPath ? retailPath : "";
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

	// Awesomium forwards this WebConfig script to its helper at process launch.
	// Windows limits that command line to 32767 UTF-16 code units. Only seed the
	// identity fields synchronously; config, catalog, and friend snapshots are
	// delivered by CL_WebHost_InjectStartupBridge once the document is live.
	CL_WebUI_JsonEscape( playerName ? playerName : "", escapedPlayerName,
		sizeof( escapedPlayerName ) );
	Com_sprintf( preloadConfigJson, sizeof( preloadConfigJson ),
		"{\"appId\":%u,\"steamId\":\"%llu\",\"playerName\":\"%s\","
		"\"playerProfile\":{\"id\":\"%llu\",\"name\":\"%s\"}}",
		appId, static_cast<unsigned long long>( steamId ), escapedPlayerName,
		static_cast<unsigned long long>( steamId ), escapedPlayerName );
	startupScript = CL_WebHost_AllocateStartupBridgeScript( preloadConfigJson,
		NULL, NULL );
	if ( !startupScript
		|| strlen( startupScript ) >= CL_WEB_AWESOMIUM_PRELOAD_MAX_LENGTH ) {
		if ( startupScript ) {
			Z_Free( startupScript );
		}
		return CL_WebUI_RecordBackendResult( fnql::webui::BackendResult::Failure(
			fnql::webui::BackendError::ResourceUnavailable,
			"WebUI startup preload exceeds the bounded Awesomium command line budget" ) );
	}
	(void)initialFriendJson;
	parameters.startupScript = startupScript;
	started = CL_WebUI_RecordBackendResult(
		fnql::webui::ClientBackendHost().Start( parameters ) );
	Z_Free( startupScript );
	return started;
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
