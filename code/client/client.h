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
// client.h -- primary header for client

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderercommon/tr_public.h"
#include "../qcommon/vm_local.h"
#include "../ui/ui_public.h"
#include "../cgame/cg_public.h"
#include "../game/bg_public.h"
#include "audio/snd_public.h"
#include "keys.h"

#if defined(USE_CURL) && !defined(DEDICATED)
#define USE_CLIENT_CURL 1
#include "cl_curl.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// file full of random crap that gets used to create cl_guid
#define QKEY_FILE "qkey"
#define QKEY_SIZE 2048

#define	RETRANSMIT_TIMEOUT	3000	// time between connection packet retransmits

// snapshots are a view of the server at a given time
struct clSnapshot_t {
	qboolean		valid;			// cleared if delta parsing was invalid
	int				snapFlags;		// rate delayed and dropped commands

	int				serverTime;		// server time the message is valid for (in msec)

	int				messageNum;		// copied from netchan->incoming_sequence
	int				deltaNum;		// messageNum the delta is from
	int				ping;			// time from when cmdNum-1 was sent to time packet was reeceived
	int				areabytes;
	byte			areamask[MAX_MAP_AREA_BYTES];		// portalarea visibility bits

	int				cmdNum;			// the next cmdNum the server is expecting
	playerState_t	ps;						// complete information about the current player at this time
	playerState_t	psRaw;					// raw protocol-specific playerstate for legacy demo delta bases

	int				numEntities;			// all of the entities that need to be presented
	int				parseEntitiesNum;		// at the time of this snapshot

	int				serverCommandNum;		// execute all commands up to this before
											// making the snapshot current
};
#ifndef __cplusplus
#define clSnapshot_t struct clSnapshot_t
#endif



/*
=============================================================================

the clientActive_t structure is wiped completely at every
new gamestate_t, potentially several times during an established connection

=============================================================================
*/

struct outPacket_t {
	int		p_cmdNumber;		// cl.cmdNumber when packet was sent
	int		p_serverTime;		// usercmd->serverTime when packet was sent
	int		p_realtime;			// cls.realtime when packet was sent
};
#ifndef __cplusplus
#define outPacket_t struct outPacket_t
#endif

// the parseEntities array must be large enough to hold PACKET_BACKUP frames of
// entities, so that when a delta compressed message arives from the server
// it can be un-deltad from the original 
#define	MAX_PARSE_ENTITIES	( PACKET_BACKUP * MAX_SNAPSHOT_ENTITIES )

extern int g_console_field_width;

struct clientActive_t {
	int			timeoutcount;		// it requres several frames in a timeout condition
									// to disconnect, preventing debugging breaks from
									// causing immediate disconnects on continue
	clSnapshot_t	snap;			// latest received from server

	int			serverTime;			// may be paused during play
	int			oldServerTime;		// to prevent time from flowing bakcwards
	int			oldFrameServerTime;	// to check tournament restarts
	int			serverTimeDelta;	// cl.serverTime = cls.gametime + cl.serverTimeDelta
									// this value changes as net lag varies
	qboolean	extrapolatedSnapshot;	// set if any cgame frame has been forced to extrapolate
									// cleared when CL_AdjustTimeDelta looks at it
	qboolean	newSnapshots;		// set on parse of any valid packet

	gameState_t	gameState;			// configstrings
	char		mapname[MAX_QPATH];	// extracted from CS_SERVERINFO

	int			parseEntitiesNum;	// index (not anded off) into cl_parse_entities[]

	int			mouseDx[2], mouseDy[2];	// added to by mouse events
	int			mouseIndex;
	int			joystickAxis[MAX_JOYSTICK_AXIS];	// set by joystick events

	// cgame communicates a few values to the client system
	int			cgameUserCmdValue;	// current weapon to add to usercmd_t
	int			cgameUserCmdPrimary;
	int			cgameUserCmdFov;
	float		cgameSensitivity;

	// cmds[cmdNumber] is the predicted command, [cmdNumber-1] is the last
	// properly generated command
	usercmd_t	cmds[CMD_BACKUP];	// each message will send several old cmds
	int			cmdNumber;			// incremented each frame, because multiple
									// frames may need to be packed into a single packet

	outPacket_t	outPackets[PACKET_BACKUP];	// information about each packet we have sent out

	// the client maintains its own idea of view angles, which are
	// sent to the server each frame.  It is cleared to 0 upon entering each level.
	// the server sends a delta each frame which is added to the locally
	// tracked view angles to account for standing on rotating objects,
	// and teleport direction changes
	vec3_t		viewangles;

	int			serverId;			// included in each client message so the server
												// can tell if it is for a prior map_restart
	// big stuff at end of structure so most offsets are 15 bits or less
	clSnapshot_t	snapshots[PACKET_BACKUP];

	entityState_t	entityBaselines[MAX_GENTITIES];	// for delta compression when not in previous frame

	entityState_t	parseEntities[MAX_PARSE_ENTITIES];

	byte			baselineUsed[MAX_GENTITIES];
};
#ifndef __cplusplus
#define clientActive_t struct clientActive_t
#endif

extern	clientActive_t		cl;

#define EM_GAMESTATE 1
#define EM_SNAPSHOT  2
#define EM_COMMAND   4

/*
=============================================================================

the clientConnection_t structure is wiped when disconnecting from a server,
either to go to a full screen console, play a demo, or connect to a different server

A connection can be to either a server through the network layer or a
demo through a file.

=============================================================================
*/

struct clientConnection_t {

	int			clientNum;
	int			lastPacketSentTime;			// for retransmits during connection
	int			lastPacketTime;				// for timeouts

	netadr_t	serverAddress;
	int			connectTime;				// for connection retransmits
	int			connectPacketCount;			// for display on connection dialog
	char		serverMessage[MAX_STRING_CHARS]; // for display on connection dialog

	int			challenge;					// from the server to use for connecting
	int			handshakeProtocol;			// protocol selected by challengeResponse
	int			checksumFeed;				// from the server for checksum calculations

	// these are our reliable messages that go to the server
	int			reliableSequence;
	int			reliableAcknowledge;		// the last one the server has executed
	char		reliableCommands[MAX_RELIABLE_COMMANDS][MAX_STRING_CHARS];

	// server message (unreliable) and command (reliable) sequence
	// numbers are NOT cleared at level changes, but continue to
	// increase as long as the connection is valid

	// message sequence is used by both the network layer and the
	// delta compression layer
	int			serverMessageSequence;

	// reliable messages received from server
	int			serverCommandSequence;
	int			lastExecutedServerCommand;		// last server command grabbed or executed with CL_GetServerCommand
	char		serverCommands[MAX_RELIABLE_COMMANDS][MAX_STRING_CHARS];
	qboolean	serverCommandsIgnore[MAX_RELIABLE_COMMANDS];

	// file transfer from server
	fileHandle_t download;
	char		downloadName[MAX_OSPATH];
	char		downloadTempName[MAX_OSPATH + 4]; // downloadName + ".tmp"
	int			sv_allowDownload;
	char		sv_dlURL[MAX_CVAR_VALUE_STRING];
	int			downloadNumber;
	int			downloadBlock;	// block we are waiting for
	int			downloadCount;	// how many bytes we got
	int			downloadSize;	// how many bytes we got
	char		downloadList[BIG_INFO_STRING]; // list of paks we need to download
	qboolean	downloadRestart;	// if true, we need to do another FS_Restart because we downloaded a pak

#ifdef USE_CLIENT_CURL
	qboolean	cURLEnabled;
	qboolean	cURLUsed;
	qboolean	cURLDisconnected;
	char		downloadURL[MAX_OSPATH];
	CURL		*downloadCURL;
	CURLM		*downloadCURLM;
#endif /* USE_CLIENT_CURL */

	// demo information
	char		demoName[MAX_OSPATH];
	char		recordName[MAX_OSPATH]; // without extension
	qboolean	explicitRecordName;
	char		recordNameShort[TRUNCATE_LENGTH]; // for recording message
	qboolean	dm68compat;
	qboolean	spDemoRecording;
	qboolean	demorecording;
	qboolean	demoplaying;
	qboolean	demoLegacyFormat;
	int			demoLegacyProtocol;
	qboolean	demowaiting;	// don't record until a non-delta message is received
	qboolean	firstDemoFrameSkipped;
	fileHandle_t	demofile;
	fileHandle_t	recordfile;

	int		timeDemoFrames;		// counter of rendered frames
	int		timeDemoStart;		// cls.realtime before first frame
	int		timeDemoBaseTime;	// each frame will be at this time + frameNum * 50

	float	aviVideoFrameRemainder;
	float	aviSoundFrameRemainder;
	int		aviFrameEndTime;
	char	videoName[MAX_QPATH];
	int		videoIndex;

	// big stuff at end of structure so most offsets are 15 bits or less
	netchan_t	netchan;

	qboolean compat;

	// simultaneous demo playback and recording
	int		eventMask;
	int		demoCommandSequence;
	int		demoDeltaNum;
	int		demoMessageSequence;

};
#ifndef __cplusplus
#define clientConnection_t struct clientConnection_t
#endif

extern	clientConnection_t clc;

/*
==================================================================

the clientStatic_t structure is never wiped, and is used even when
no client connection is active at all

==================================================================
*/

struct ping_t {
	netadr_t	adr;
	int			start;
	int			time;
	char		info[MAX_INFO_STRING];
};
#ifndef __cplusplus
#define ping_t struct ping_t
#endif

struct serverInfo_t {
	netadr_t	adr;
	char	  	hostName[MAX_NAME_LENGTH];
	char	  	mapName[MAX_NAME_LENGTH];
	char	  	game[MAX_NAME_LENGTH];
	int			netType;
	int			gameType;
	int		  	clients;
	int		  	maxClients;
	int			minPing;
	int			maxPing;
	int			ping;
	qboolean	visible;
	int			punkbuster;
	int			g_humanplayers;
	int			g_needpass;
};
#ifndef __cplusplus
#define serverInfo_t struct serverInfo_t
#endif

struct clientStatic_t {
	connstate_t	state;				// connection status
	qboolean	gameSwitch;

	qboolean	cddialog;			// bring up the cd needed dialog next frame

	char		servername[MAX_OSPATH];		// name of server from original connect (used by reconnect)

	// when the server clears the hunk, all of these must be restarted
	qboolean	rendererStarted;
	qboolean	soundStarted;
	qboolean	soundRegistered;
	qboolean	uiStarted;
	qboolean	cgameStarted;

	int			framecount;
	int			frametime;			// real msec since last frame
	int			gameFrametime;		// scaled world msec since last frame

	int			realtime;			// ignores pause and timescale
	int			gametime;			// scaled world time
	int			realFrametime;		// ignoring pause, so console always works

	int			numlocalservers;
	serverInfo_t	localServers[MAX_OTHER_SERVERS];

	int			numglobalservers;
	serverInfo_t  globalServers[MAX_GLOBAL_SERVERS];
	// additional global servers
	int			numGlobalServerAddresses;
	netadr_t		globalServerAddresses[MAX_GLOBAL_SERVERS];

	int			numfavoriteservers;
	serverInfo_t	favoriteServers[MAX_OTHER_SERVERS];

	int pingUpdateSource;		// source currently pinging or updating

	// update server info
	netadr_t	updateServer;
	char		updateChallenge[MAX_TOKEN_CHARS];
	char		updateInfoString[MAX_INFO_STRING];

	netadr_t	authorizeServer;

	// rendering info
	glconfig_t	glconfig;
	qhandle_t	charSetShader;
	qhandle_t	recordShader;
	qhandle_t	whiteShader;
	qhandle_t	consoleShader;
	qhandle_t	cursorShader;

	int			lastVidRestart;
	int			soundMuted;

	qboolean	startCgame;

	int			captureWidth;
	int			captureHeight;

	float		con_factor;

	float		scale;
	float		biasX;
	float		biasY;

};
#ifndef __cplusplus
#define clientStatic_t struct clientStatic_t
#endif

extern int bigchar_width;
extern int bigchar_height;
extern int smallchar_width;
extern int smallchar_height;

extern	clientStatic_t		cls;

extern	char		cl_oldGame[MAX_QPATH];
extern	qboolean	cl_oldGameSet;

#ifdef USE_CLIENT_CURL

extern		download_t	download;
qboolean	Com_DL_Perform( download_t *dl );
void		Com_DL_Cleanup( download_t *dl );
qboolean	Com_DL_Begin( download_t *dl, const char *localName, const char *remoteURL, qboolean autoDownload );
qboolean	Com_DL_InProgress( const download_t *dl );
qboolean	Com_DL_ValidFileName( const char *fileName );
qboolean	CL_Download( const char *cmd, const char *pakname, qboolean autoDownload );

#endif

//=============================================================================

extern	vm_t			*cgvm;	// interface to cgame dll or vm
extern	vm_t			*uivm;	// interface to ui dll or vm
extern	refexport_t		re;		// interface to refresh .dll


//
// cvars
//
extern	cvar_t	*cl_noprint;
extern	cvar_t	*cl_debugMove;
extern	cvar_t	*cl_timegraph;
extern	cvar_t	*cl_shownet;
extern	cvar_t	*cl_autoNudge;
extern	cvar_t	*cl_timeNudge;
extern	cvar_t	*cl_showTimeDelta;

extern	cvar_t	*com_timedemo;
extern	cvar_t	*cl_aviFrameRate;
extern	cvar_t	*cl_aviMotionJpeg;
extern	cvar_t	*cl_aviPipeFormat;

extern	cvar_t	*cl_activeAction;

extern	cvar_t	*cl_allowDownload;
#ifdef USE_CLIENT_CURL
extern	cvar_t	*cl_mapAutoDownload;
extern	cvar_t	*cl_dlDirectory;
#endif
extern	cvar_t	*cl_conXOffset;
extern	cvar_t	*cl_conColor;
extern	cvar_t	*cl_inGameVideo;

extern	cvar_t	*cl_lanForcePackets;
extern	cvar_t	*cl_autoRecordDemo;
extern	cvar_t	*cl_freezeDemo;
extern	cvar_t	*cl_drawRecording;
extern	cvar_t	*cl_menuAspect;
extern	cvar_t	*cl_menuDepthOfField;
extern	cvar_t	*cl_menuDepthOfFieldTime;
extern	cvar_t	*cl_cinematicAspect;
extern	cvar_t	*cl_captureActive;
extern	cvar_t	*cl_playerHighlight;
extern	cvar_t	*cl_playerHighlightRimIntensity;
extern	cvar_t	*cl_playerHighlightOutlineIntensity;
extern	cvar_t	*cl_playerHighlightOutlineScale;
extern	cvar_t	*cl_playerHighlightRedColor;
extern	cvar_t	*cl_playerHighlightBlueColor;
extern	cvar_t	*cl_playerHighlightFreeColor;
extern	cvar_t	*cl_playerHighlightEnemyColor;
extern	cvar_t	*cl_playerHighlightTeammateColor;
extern	cvar_t	*r_levelshotHideHud;
extern	cvar_t	*r_levelshotHideViewWeapon;

extern	cvar_t	*com_maxfps;

extern	cvar_t	*vid_xpos;
extern	cvar_t	*vid_ypos;
extern	cvar_t	*r_noborder;

extern	cvar_t	*r_allowSoftwareGL;
extern	cvar_t	*r_swapInterval;
extern	cvar_t	*r_glDriver;

extern	cvar_t	*r_displayRefresh;
extern	cvar_t	*r_fullscreen;
extern	cvar_t	*r_mode;
extern	cvar_t	*r_modeFullscreen;
extern	cvar_t	*r_customwidth;
extern	cvar_t	*r_customheight;
extern	cvar_t	*r_customPixelAspect;
extern	cvar_t	*r_colorbits;
extern	cvar_t	*cl_stencilbits;
extern	cvar_t	*cl_depthbits;
extern	cvar_t	*cl_drawBuffer;

//=================================================

//
// cl_main
//
void CL_AddReliableCommand( const char *cmd, qboolean isDisconnectCmd );

void CL_StartHunkUsers( void );

void CL_Disconnect_f( void );
void CL_ReadDemoMessage( void );
void CL_StopRecord_f( void );

void CL_InitDownloads( void );
void CL_NextDownload( void );
qboolean CL_GetWorkshopDownloadInfo( unsigned int itemIdLow, unsigned int itemIdHigh, unsigned long long *outDownloaded, unsigned long long *outTotal );
int CL_ParseRequiredWorkshopItems( const char *text, unsigned long long *items, int capacity );
void CL_Workshop_Init( void );
void CL_Workshop_Shutdown( void );
void CL_Workshop_Reset( void );
qboolean CL_WorkshopDownloadsActive( void );
qboolean CL_Workshop_BeginRequiredDownloads( const char *requiredItems );
void CL_Workshop_Frame( void );
void CL_ResumeDownloadsAfterWorkshop( void );

void CL_GetPing( int n, char *buf, int buflen, int *pingtime );
void CL_GetPingInfo( int n, char *buf, int buflen );
void CL_ClearPing( int n );
int CL_GetPingQueueCount( void );

void CL_ClearState( void );

int CL_ServerStatus( const char *serverAddress, char *serverStatusString, int maxLen );

qboolean CL_CheckPaused( void );
qboolean CL_NoDelay( void );

qboolean CL_GetModeInfo( int *width, int *height, float *windowAspect, int mode, const char *modeFS, int dw, int dh, qboolean fullscreen );
qboolean CL_CopyClientIdentity( int clientNum, cgameClientIdentity_t *identity );
qboolean CL_GetClientSteamId( int clientNum, unsigned int *steamIdLow, unsigned int *steamIdHigh );
qboolean CL_IsSteamIdentityMuted( unsigned int identityLow, unsigned int identityHigh );
qboolean CL_ToggleSteamIdentityMute( unsigned int identityLow, unsigned int identityHigh );
qboolean CL_IsVoiceSenderMuted( int clientNum );
void CL_SetClientSpeakingState( int clientNum, qboolean speaking );
void CL_SetLocalSpeakingState( qboolean speaking );


//
// cl_input.cpp
//
void CL_InitInput( void );
void CL_ClearInput( void );
void CL_SendCmd( void );
void CL_WritePacket( int repeat );
void CL_SetRetailClientMessageViewangleDeltaFlag( void );
void CL_SetRetailClientMessageCGameImportGuardFlag( void );
void CL_SetRetailClientMessageRendererNodeCount( int nodeCount );

//
// cl_keys.cpp
//
extern  field_t     chatField;
extern  field_t     g_consoleField;

void Field_Draw( field_t *edit, int x, int y, int width, qboolean showCursor, qboolean noColorEscape );
void Field_BigDraw( field_t *edit, int x, int y, int width, qboolean showCursor, qboolean noColorEscape );
void CL_ToggleMenu_f( void );

//
// cl_parse.cpp
//
extern int cl_connectedToPureServer;
extern int cl_connectedToCheatServer;

void CL_ParseServerMessage( msg_t *msg );

//====================================================================

qboolean CL_UpdateVisiblePings_f( int source );
qboolean CL_ValidPakSignature( const byte *data, int len );


//
// console
//

extern cvar_t *con_scale;

void Con_CheckResize( void );
void Con_Init( void );
void Con_Shutdown( void );
void Con_ToggleConsole_f( void );
void Con_ClearNotify( void );
void Con_RunConsole( void );
void Con_DrawConsole( void );
void Con_MouseEvent( int dx, int dy );
qboolean Con_KeyEvent( int key, qboolean down );
qboolean Con_InputKey( int key );
void Con_CharEvent( int key );
qboolean Con_UseAutoSay( void );
qboolean Con_UseRawSay( void );
void Con_PageUp( int lines );
void Con_PageDown( int lines );
void Con_Top( void );
void Con_Bottom( void );
void Con_Close( void );

void CL_LoadConsoleHistory( void );
void CL_SaveConsoleHistory( void );

//
// cl_scrn.cpp
//
void	SCR_Init( void );
void	SCR_Done( void );
void	SCR_UpdateScreen( void );
qboolean CL_UIMenusAreVisible( void );

void	SCR_DebugGraph( float value );

int		SCR_GetBigStringWidth( const char *str );	// returns in virtual 640x480 coordinates

void	SCR_AdjustFrom640( float *x, float *y, float *w, float *h );
void	SCR_AdjustFrom640Uniform( float *x, float *y, float *w, float *h );
void	SCR_FillRect( float x, float y, float width, float height, 
					 const float *color );
void	SCR_DrawPic( float x, float y, float width, float height, qhandle_t hShader );
void	SCR_DrawNamedPic( float x, float y, float width, float height, const char *picname );

void	SCR_DrawBigString( int x, int y, const char *s, float alpha, qboolean noColorEscape );			// draws a string with embedded color control characters with fade
void	SCR_DrawStringExt( int x, int y, float size, const char *string, const float *setColor, qboolean forceColor, qboolean noColorEscape );
void	SCR_DrawSmallStringExt( int x, int y, const char *string, const float *setColor, qboolean forceColor, qboolean noColorEscape );
void	SCR_DrawSmallChar( int x, int y, int ch );
void	SCR_DrawSmallString( int x, int y, const char *s, int len );
void	CL_CopyRetailGlconfig( void *glconfig );
void	RE_DrawScaledText( int x, int y, const char *text, int fontHandle, float scale, int limit, float *maxX, qboolean forceColor, const float *baseColor );
void	RE_MeasureScaledText( const char *text, const char *end, int fontHandle, float scale, int limit, float *bounds );

//
// cl_cin.cpp
//

void CL_PlayCinematic_f( void );
void SCR_DrawCinematic (void);
void SCR_RunCinematic (void);
void SCR_StopCinematic (void);
int CIN_PlayCinematic( const char *arg0, int xpos, int ypos, int width, int height, int bits);
e_status CIN_StopCinematic(int handle);
e_status CIN_RunCinematic (int handle);
void CIN_DrawCinematic (int handle);
void CIN_DrawCinematicUI( int handle );
void CIN_SetExtents (int handle, int x, int y, int w, int h);
void CIN_UploadCinematic(int handle);
void CIN_CloseAllVideos(void);

//
// cl_cgame.cpp
//
void CL_InitCGame( void );
void CL_ShutdownCGame( void );
qboolean CL_GameCommand( void );
void CL_CGameRendering( stereoFrame_t stereo );
void CL_SetCGameTime( void );
void CL_CheckCGameNativeImportIntegrity( void );
void CL_ShowFirstTrackedPlayer( void );
void CL_ShowSecondTrackedPlayer( void );
int CL_GetCGamePhysicsTime( void );

//
// cl_webpak.cpp
//
void CL_WebPak_Init( void );
void CL_WebPak_Shutdown( void );
qboolean CL_WebPak_Available( void );
qboolean CL_WebPak_Fetch( const char *virtualPath, void **outBuffer, int *outLength );
int CL_WebPak_GetFileList( const char *path, const char *extension, char *listbuf, int bufsize );
qboolean CL_WebRequestResolve( const char *virtualPath, void **outBuffer, int *outLength );
qboolean CL_LauncherRequestData( const char *virtualPath, void **outBuffer, int *outLength );

//
// cl_webui.cpp
//
void CL_RefreshOnlineServicesBridgeState( void );
qboolean CL_IsSubscribedApp( int appId );
qhandle_t CL_GetAvatarImageHandle( unsigned int identityLow, unsigned int identityHigh );
void CL_ClearAvatarImageHandles( void );
qboolean CL_Steam_OpenOverlayUrl( const char *url );
qhandle_t CL_Steam_RegisterShader( const char *url );
qboolean CL_Steam_RequestServers( int requestMode );
qboolean CL_Steam_RequestServerDetails( unsigned int serverIp, unsigned short serverPort );
qboolean CL_Steam_RefreshServerList( void );
qboolean CL_Steam_CreateLobby( void );
qboolean CL_Steam_LeaveLobby( void );
qboolean CL_Steam_JoinLobby( const char *lobbyId );
qboolean CL_Steam_SetLobbyServer( unsigned int serverIp, unsigned short serverPort );
qboolean CL_Steam_ShowInviteOverlay( void );
qboolean CL_Steam_Invite( const char *steamId );
qboolean CL_Steam_SayLobby( const char *message );
qboolean CL_Steam_RequestAllUGC( int filter );
qboolean CL_Steam_RequestUserStats( const char *steamId );
qboolean CL_Steam_ActivateOverlayToUser( const char *dialog, const char *steamId );
qboolean CL_Steam_GetItemDownloadInfo( unsigned int itemIdLow, unsigned int itemIdHigh, unsigned long long *outDownloaded, unsigned long long *outTotal );
void CL_Steam_OnRichPresenceJoinRequested( const char *command );
void CL_Steam_OnGameServerChangeRequested( const char *server, const char *password );
void CL_SteamP2PFrame( void );
void QLWebHost_RegisterCommands( void );
void QLWebHost_UnregisterCommands( void );
void CL_WebHost_Init( void );
void CL_WebHost_Shutdown( void );
void CL_WebHost_Frame( void );
void CL_WebHost_InvalidateFactoryCatalog( void );
void CL_WebHost_BootstrapAwesomiumMenu( void );
qboolean CL_WebHost_HasLiveView( void );
qboolean CL_WebHost_HasBoundWindowObject( void );
qboolean CL_WebHost_HasDrawableSurface( void );
void CL_WebHost_DrawBrowserSurface( void );
void *CL_WebHost_GetCursorHandle( void );
void *CL_WebHost_OnChangeCursor( int cursorType );
void CL_WebHost_OnChangeTooltip( const char *tooltip );
void CL_WebHost_SetCursorPosition( int x, int y );
qboolean CL_WebHost_GetCursorPosition( int *x, int *y );
qboolean CL_WebHost_RequestCursorPosition( int *x, int *y );
void CL_WebHost_HideBrowser( void );
void CL_WebHost_HideForGameTransition( void );
void CL_WebHost_NotifyAppActivation( qboolean active );
void CL_WebHost_OnServerInfoResponse( const netadr_t *address, const char *infoString, int ping );
qboolean CL_WebHost_OnServerStatusResponseInfo( const netadr_t *address, const char *infoString );
void CL_WebHost_OnServerStatusResponsePlayer( const netadr_t *address, const char *playerLine );
void CL_WebHost_OnServerStatusResponseComplete( const netadr_t *address );
void CL_WebView_PublishEvent( const char *name, const char *payload );
void CL_WebView_InvokeCommNotice( const char *message );
void CL_WebView_PublishTaggedInfoString( const char *messageType, const char *infoString );
void CL_WebView_PublishGameError( const char *message );
void CL_WebView_PublishGameEnd( void );
void CL_WebView_PublishCvarChange( const char *name, const char *value, qboolean replicate );
void CL_WebView_PublishBindChanged( const char *name, const char *value );
void CL_WebView_PublishGameStartForAddress( const netadr_t *serverAddress );
void CL_WebView_PublishGameStart( void );
void CL_WebView_PublishGameDemo( const char *id, const char *name );
void CL_WebView_PublishGameScreenshot( const char *id, const char *name );
void CL_WebView_OnMouseMove( int x, int y );
void CL_WebView_OnMouseButtonEvent( int key, qboolean down );
void CL_WebView_OnMouseWheelEvent( int direction );
void CL_WebView_OnKeyEvent( int key, qboolean down );
qhandle_t CL_AdvertisementBridge_SetupAdvertCellShader( const char *defaultContent, const void *rect, int cellId );
qhandle_t CL_AdvertisementBridge_RefreshAdvertCellShader( const char *defaultContent, const void *rect, int cellId );
qhandle_t CL_AdvertisementBridge_SetupUIAdvertCellShader( const char *defaultContent, const void *rect, int cellId );
qhandle_t CL_AdvertisementBridge_RefreshUIAdvertCellShader( const char *defaultContent, const void *rect, int cellId );
void CL_AdvertisementBridge_InitCGame( void );
void CL_AdvertisementBridge_ShutdownCGame( void );
void CL_AdvertisementBridge_InitUI( void );
void CL_AdvertisementBridge_Reserved21C0( void );
void CL_AdvertisementBridge_SetActiveAdvert( int cellId );
void CL_AdvertisementBridge_ActivateAdvert( int cellId );
void CL_AdvertisementBridge_UpdateAdvert( int handleOrToken, int area );
void CL_AdvertisementBridge_SetMapPath( const char *mapPath );
void CL_AdvertisementBridge_UpdateViewParameters( void );
void CL_AdvertisementBridge_RefreshLoadingViewParameters( void );
void CL_AdvertisementBridge_UpdateLoadingViewParameters( void );
void CL_AdvertisementBridge_SetFrameTime( int frameTime );
void CL_AdvertisementBridge_ClearDelay( void );
qboolean CL_AdvertisementBridge_IsDelayElapsed( void );
int CL_AdvertisementBridge_GetCellDisplayState( int cellId );
void CL_AdvertisementBridge_GetCellLabel( int cellId, char *buffer, int bufferSize );
int CL_AdvertisementBridge_GetLabelList1Count( void );
void CL_AdvertisementBridge_GetLabelList1Entry( int index, char *buffer, int bufferSize );
int CL_AdvertisementBridge_GetLabelList2Count( void );
void CL_AdvertisementBridge_GetLabelList2Entry( int index, char *buffer, int bufferSize );
qboolean CL_Awesomium_RequestResource( const char *virtualPath, void **outBuffer, int *outLength );
qboolean CL_Awesomium_Startup( const char *runtimePath, const char *basePath, const char *retailPath, const char *playerName, unsigned int appId, unsigned int steamIdLow, unsigned int steamIdHigh, int width, int height, const char *initialConfigJson, const char *initialMapJson, const char *initialFactoryJson );
qboolean CL_Awesomium_OpenURL( const char *url );
void CL_Awesomium_Update( void );
qboolean CL_Awesomium_Resize( int width, int height );
int CL_Awesomium_SurfaceWidth( void );
int CL_Awesomium_SurfaceHeight( void );
qboolean CL_Awesomium_SurfaceDirty( void );
qboolean CL_Awesomium_IsLoading( void );
qboolean CL_Awesomium_IsCrashed( void );
int CL_Awesomium_LastErrorCode( void );
qboolean CL_Awesomium_ExecuteJavascript( const char *script, const char *frame );
qboolean CL_Awesomium_ExecuteJavascriptInteger( const char *script, const char *frame, int *outValue );
qboolean CL_Awesomium_PopJavascriptRequest( char *buffer, int bufferSize );
qboolean CL_Awesomium_SetZoom( int zoomPercent );
void CL_Awesomium_PauseRendering( void );
void CL_Awesomium_Unfocus( void );
qboolean CL_Awesomium_CopySurface( byte *destination, int width, int height, int rowSpan );
void CL_Awesomium_InjectMouseMove( int x, int y );
void CL_Awesomium_InjectMouseDown( int button );
void CL_Awesomium_InjectMouseUp( int button );
void CL_Awesomium_InjectMouseWheel( int direction );
void CL_Awesomium_InjectKeyboardEvent( unsigned int eventType, unsigned int virtualKeyCode, long nativeKeyCode );
void CL_Awesomium_Stop( void );
void CL_Awesomium_ClearCache( void );
void CL_Awesomium_Reload( qboolean ignoreCache );
void CL_Awesomium_Shutdown( void );
const char *CL_Awesomium_LastError( void );

//
// cl_ui.cpp
//
void CL_InitUI( void );
void CL_ShutdownUI( void );
int Key_GetCatcher( void );
void Key_SetCatcher( int catcher );


//
// cl_net_chan.cpp
//
void CL_Netchan_Transmit( netchan_t *chan, msg_t *msg );
void CL_Netchan_Enqueue( netchan_t *chan, msg_t *msg, int times );
qboolean CL_Netchan_Process( netchan_t *chan, msg_t *msg );

//
// cl_avi.cpp
//
qboolean CL_OpenAVIForWriting( const char *filename, qboolean pipe, qboolean reopen );
void CL_TakeVideoFrame( void );
void CL_WriteAVIVideoFrame( const byte *imageBuffer, int size );
void CL_WriteAVIAudioFrame( const byte *pcmBuffer, int size );
qboolean CL_CloseAVI( qboolean reopen );
qboolean CL_VideoRecording( void );

//
// cl_jpeg.cpp
//
size_t	CL_SaveJPGToBuffer( byte *buffer, size_t bufSize, int quality, int image_width, int image_height, byte *image_buffer, int padding );
void	CL_SaveJPG( const char *filename, int quality, int image_width, int image_height, byte *image_buffer, int padding );
void	CL_LoadJPG( const char *filename, unsigned char **pic, int *width, int *height );


// base backend functions
void	HandleEvents( void );

// platform-specific
void	GLimp_InitGamma(glconfig_t *config);
void	GLimp_SetGamma(unsigned char red[256], unsigned char green[256], unsigned char blue[256]);
void	GLimp_QueryDisplayOutput(rendererDisplayOutput_t *output);
void	GLimp_InvalidateConfig( void );

// OpenGL
#ifdef USE_OPENGL_API
void	GLimp_Init( glconfig_t *config );
void	GLimp_Shutdown( qboolean unloadDLL );
void	GLimp_EndFrame( void );
void	*GL_GetProcAddress( const char *name );
#endif

// Vulkan
#ifdef USE_VULKAN_API
void	VKimp_Init( glconfig_t *config );
void	VKimp_Shutdown( qboolean unloadDLL );
void	*VK_GetInstanceProcAddr( VkInstance instance, const char *name );
qboolean VK_CreateSurface( VkInstance instance, VkSurfaceKHR* pSurface );
#endif

#ifdef __cplusplus
}
#endif
