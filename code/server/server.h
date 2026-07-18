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
// server.h

#ifdef __cplusplus
extern "C" {
#endif

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../qcommon/vm_local.h"
#include "../game/g_public.h"
#include "../game/bg_public.h"

//=============================================================================

#define	PERS_SCORE				0		// !!! MUST NOT CHANGE, SERVER AND
										// GAME BOTH REFERENCE !!!

#define	MAX_ENT_CLUSTERS	16

typedef struct svEntity_s {
	struct worldSector_s *worldSector;
	struct svEntity_s *nextEntityInWorldSector;

	entityState_t	baseline;		// for delta compression of initial sighting
	int			numClusters;		// if -1, use headnode instead
	int			clusternums[MAX_ENT_CLUSTERS];
	int			lastCluster;		// if all the clusters don't fit in clusternums
	int			areanum, areanum2;
	int			snapshotCounter;	// used to prevent double adding from portal views
} svEntity_t;

typedef enum {
	SS_DEAD,			// no map loaded
	SS_LOADING,			// spawning level entities
	SS_GAME				// actively running
} serverState_t;

// we might not use all MAX_GENTITIES every frame
// so leave more room for slow-snaps clients etc.
#define NUM_SNAPSHOT_FRAMES (PACKET_BACKUP*4)

typedef struct snapshotFrame_s {
	entityState_t *ents[ MAX_GENTITIES ];
	int	frameNum;
	int start;
	int count;
} snapshotFrame_t;

typedef struct {
	serverState_t	state;
	qboolean		restarting;			// if true, send configstring changes during SS_LOADING
	int				pure;				// fixed at level spawn
	int				maxclients;			// fixed at level spawn
	int				serverId;			// changes each server start
	int				restartedServerId;	// changes each map restart
	int				checksumFeed;		// the feed key that we use to compute the pure checksum strings
	int				snapshotCounter;	// incremented for each snapshot built
	int				timeResidual;		// <= 1000 / sv_frame->value
	char			*configstrings[MAX_CONFIGSTRINGS];
	svEntity_t		svEntities[MAX_GENTITIES];

	const char		*entityParsePoint;	// used during game VM init

	// the game virtual machine will update these on init and changes
	sharedEntity_t	*gentities;
	int				gentitySize;
	int				num_entities;		// current number, <= MAX_GENTITIES

	playerState_t	*gameClients;
	int				gameClientSize;		// will be > sizeof(playerState_t) due to game private data

	int				restartTime;
	int				time;

	byte			baselineUsed[ MAX_GENTITIES ];
} server_t;

typedef struct {
	int				areabytes;
	byte			areabits[MAX_MAP_AREA_BYTES];		// portalarea visibility bits
	playerState_t	ps;
	int				num_entities;
#if 0
	int				first_entity;		// into the circular sv_packet_entities[]
										// the entities MUST be in increasing state number
										// order, otherwise the delta compression will fail
#endif
	int				messageSent;		// time the message was transmitted
	int				messageAcked;		// time the message was acked
	int				messageSize;		// used to rate drop packets

	int				frameNum;			// from snapshot storage to compare with last valid
	entityState_t	*ents[ MAX_SNAPSHOT_ENTITIES ];

} clientSnapshot_t;

typedef enum {
	CS_FREE = 0,	// can be reused for a new connection
	CS_ZOMBIE,		// client has been disconnected, but don't reuse
					// connection for a couple seconds
	CS_CONNECTED,	// has been assigned to a client_t, but no gamestate yet or downloading
	CS_PRIMED,		// gamestate has been sent, but client hasn't sent a usercmd
	CS_ACTIVE		// client is fully in game
} clientState_t;

typedef struct netchan_buffer_s {
	msg_t           msg;
	byte            msgBuffer[MAX_MSGLEN];
	char		clientCommandString[MAX_STRING_CHARS];	// valid command string for SV_Netchan_Encode
	struct netchan_buffer_s *next;
} netchan_buffer_t;

typedef struct rateLimit_s {
	int			lastTime;
	int			burst;
} rateLimit_t;

typedef struct leakyBucket_s leakyBucket_t;
struct leakyBucket_s {
	netadrtype_t	type;

	union {
		byte	_4[4];
		byte	_6[16];
	} ipv;

	rateLimit_t rate;

	int			hash;
	int			toxic;

	leakyBucket_t *prev, *next;
};

#define SV_PLATFORM_STEAM_ID_SIZE	32

typedef enum {
	GSA_INIT = 0,	// gamestate never sent with current sv.serverId
	GSA_SENT_ONCE,	// gamestate sent once, client can reply with any (messageAcknowledge - gamestateMessageNum) >= 0 and correct serverId
	GSA_SENT_MANY,	// gamestate sent many times, client must reply with exact gamestateMessageNum == gamestateMessageNum and correct serverId
	GSA_ACKED		// gamestate acknowledged, no retansmissions needed
} gameStateAck_t;

typedef struct client_s {
	clientState_t	state;
	char			userinfo[MAX_INFO_STRING];		// name, etc

	char			reliableCommands[MAX_RELIABLE_COMMANDS][MAX_STRING_CHARS];
	int				reliableSequence;		// last added reliable message, not necessarily sent or acknowledged yet
	int				reliableAcknowledge;	// last acknowledged reliable message
	int				messageAcknowledge;

	int				gamestateMessageNum;	// netchan->outgoingSequence of gamestate
	int				challenge;

	usercmd_t		lastUsercmd;
	int				lastClientCommand;	// reliable client message sequence
	char			lastClientCommandString[MAX_STRING_CHARS];
	sharedEntity_t	*gentity;			// SV_GentityNum(clientnum)
	char			name[MAX_NAME_LENGTH];			// extracted from userinfo, high bits masked
	char			platformSteamId[SV_PLATFORM_STEAM_ID_SIZE];	// server-owned QL SteamID
	uint64_t		platformSteamIdValue;				// parsed provider identity
	qboolean		platformAuthSession;				// connection identity has passed the server auth gate
	qboolean		platformAuthTicketSession;			// provider BeginAuthSession succeeded; EndAuthSession is required
	qboolean		platformAuthValidated;			// provider delivered a successful auth result
	uint32_t		platformAuthStartedTime;			// wrap-safe timeout origin for pending auth

	gameStateAck_t	gamestateAck;
	qboolean		downloading;		// set at "download", reset at gamestate retransmission
	// int				serverId;		// last acknowledged serverId

	// downloading
	char			downloadName[MAX_QPATH]; // if not empty string, we are downloading
	fileHandle_t	download;			// file being downloaded
 	int				downloadSize;		// total bytes (can't use EOF because of paks)
 	int				downloadCount;		// bytes sent
	int				downloadClientBlock;	// last block we sent to the client, awaiting ack
	int				downloadCurrentBlock;	// current block number
	int				downloadXmitBlock;	// last block we xmited
	unsigned char	*downloadBlocks[MAX_DOWNLOAD_WINDOW];	// the buffers for the download blocks
	int				downloadBlockSize[MAX_DOWNLOAD_WINDOW];
	qboolean		downloadEOF;		// We have sent the EOF block
	int				downloadSendTime;	// time we last got an ack from the client

	int				deltaMessage;		// frame last client usercmd message
	int				lastPacketTime;		// svs.time when packet was last received
	int				lastConnectTime;	// svs.time when connection started
	int				lastDisconnectTime;
	int				lastSnapshotTime;	// svs.time of last sent snapshot
	qboolean		rateDelayed;		// true if nextSnapshotTime was set based on rate instead of snapshotMsec
	int				timeoutCount;		// must timeout a few frames in a row so debugging doesn't break
	clientSnapshot_t	frames[PACKET_BACKUP];	// updates can be delta'd from here
	int				ping;
	int				rate;				// bytes / second, 0 - unlimited
	int				snapshotMsec;		// requests a snapshot every snapshotMsec unless rate choked
	qboolean		pureAuthentic;
	qboolean		gotCP;				// TTimo - additional flag to distinguish between a bad pure checksum, and no cp command at all
	netchan_t		netchan;
	// TTimo
	// queuing outgoing fragmented messages to send them properly, without udp packet bursts
	// in case large fragmented messages are stacking up
	// buffer them into this queue, and hand them out to netchan as needed
	netchan_buffer_t *netchan_start_queue;
	netchan_buffer_t **netchan_end_queue;

	int				oldServerTime;
	qboolean		csUpdated[MAX_CONFIGSTRINGS];
	qboolean		compat;

	// flood protection
	rateLimit_t		cmd_rate;
	rateLimit_t		info_rate;
	rateLimit_t		gamestate_rate;

	// client can decode long strings
	qboolean		longstr;

	qboolean		justConnected;

	char			tld[3]; // "XX\0"
	const char		*country;

	fileHandle_t	demoRecordFile;
	char			demoRecordName[MAX_OSPATH]; // without extension
	qboolean		demoRecordHasSnapshot;
	int				demoRecordServerId;

} client_t;

//=============================================================================


// this structure will be cleared only when the game dll changes
typedef struct {
	qboolean	initialized;				// sv_init has completed

	int			time;						// real time, strictly increasing across level changes
	int			msgTime;					// will be used as precise sent time

	int			snapFlagServerBit;			// ^= SNAPFLAG_SERVERCOUNT every SV_SpawnServer()

	client_t	*clients;					// [sv_maxclients->integer];
	int			numSnapshotEntities;		// PACKET_BACKUP*MAX_SNAPSHOT_ENTITIES
	entityState_t	*snapshotEntities;		// [numSnapshotEntities]
	int			nextHeartbeatTime;

	netadr_t	authorizeAddress;			// for rcon return messages
	int			masterResolveTime[MAX_MASTER_SERVERS]; // next svs.time that server should do dns lookup for master server

	// common snapshot storage
	int			freeStorageEntities;
	int			currentStoragePosition;	// next snapshotEntities to use
	int			snapshotFrame;			// incremented with each common snapshot built
	int			currentSnapshotFrame;	// for initializing empty frames
	int			lastValidFrame;			// updated with each snapshot built
	snapshotFrame_t	snapFrames[ NUM_SNAPSHOT_FRAMES ];
	snapshotFrame_t	*currFrame; // current frame that clients can refer

} serverStatic_t;

#ifdef USE_BANS
#define SERVER_MAXBANS	1024
// Structure for managing bans
typedef struct
{
	netadr_t ip;
	// For a CIDR-Notation type suffix
	int subnet;

	qboolean isexception;
} serverBan_t;
#endif

//=============================================================================

extern	serverStatic_t	svs;				// persistant server info across maps
extern	server_t		sv;					// cleared each map
extern	vm_t			*gvm;				// game virtual machine

extern	cvar_t	*sv_fps;
extern	cvar_t	*sv_audioPVS;
extern	cvar_t	*sv_audioPVSRange;
extern	cvar_t	*sv_audioPVSMaxEntities;
extern	cvar_t	*sv_timeout;
extern	cvar_t	*sv_zombietime;
extern	cvar_t	*sv_rconPassword;
extern	cvar_t	*sv_privatePassword;
extern	cvar_t	*sv_allowDownload;
extern	cvar_t	*sv_maxclients;
extern	cvar_t	*sv_maxclientsPerIP;
extern	cvar_t	*sv_clientTLD;

extern	cvar_t	*sv_privateClients;
extern	cvar_t	*sv_hostname;
extern	cvar_t	*sv_tags;
extern	cvar_t	*sv_masterAdvertise;
extern	cvar_t	*sv_master[MAX_MASTER_SERVERS];
extern	cvar_t	*sv_reconnectlimit;
extern	cvar_t	*sv_padPackets;
extern	cvar_t	*sv_killserver;
extern	cvar_t	*sv_mapname;
extern	cvar_t	*sv_mapChecksum;
extern	cvar_t	*sv_referencedPakNames;
extern	cvar_t	*sv_serverid;
extern	cvar_t	*sv_minRate;
extern	cvar_t	*sv_maxRate;
extern	cvar_t	*sv_dlRate;
extern	cvar_t	*sv_gametype;
extern	cvar_t	*sv_ammoPack;
extern	cvar_t	*sv_pure;
extern	cvar_t	*sv_floodProtect;
extern	cvar_t	*sv_enableRankings;
extern	cvar_t	*sv_rankingsActive;
extern	cvar_t	*sv_lanForceRate;
extern	cvar_t	*sv_autoRecordDemos;
extern	cvar_t	*sv_cheats;
extern	cvar_t	*sv_gtid;
extern	cvar_t	*sv_idleRestart;
extern	cvar_t	*sv_idleExit;
extern	cvar_t	*sv_errorExit;
extern	cvar_t	*sv_quitOnEmpty;
extern	cvar_t	*sv_quitOnExitLevel;
extern	cvar_t	*sv_altEntDir;
extern	cvar_t	*sv_dumpEntities;
extern	cvar_t	*sv_cylinderScale;
extern	cvar_t	*sv_vac;
extern	cvar_t	*sv_showloss;

extern	cvar_t *sv_levelTimeReset;
extern	cvar_t *sv_filter;

typedef enum svPlatformCapability_e {
	SV_PLATFORM_CAPABILITY_ONLINE,
	SV_PLATFORM_CAPABILITY_AUTH,
	SV_PLATFORM_CAPABILITY_STEAM_GAME_SERVER,
	SV_PLATFORM_CAPABILITY_WORKSHOP,
	SV_PLATFORM_CAPABILITY_STATS,
	SV_PLATFORM_CAPABILITY_COUNT
} svPlatformCapability_t;

typedef struct svPlatformServiceStatus_s {
	const char	*key;
	const char	*provider;
	const char	*policy;
	qboolean	available;
	unsigned int	appId;
} svPlatformServiceStatus_t;

#ifdef USE_BANS
extern	cvar_t	*sv_banFile;
extern	serverBan_t serverBans[SERVER_MAXBANS];
extern	int serverBansCount;
#endif

//===========================================================

//
// sv_main.cpp
//
qboolean SVC_RateLimit( rateLimit_t *bucket, int burst, int period );
qboolean SVC_RateLimitAddress( const netadr_t *from, int burst, int period );
void SVC_RateRestoreBurstAddress( const netadr_t *from, int burst, int period );
void SVC_RateRestoreToxicAddress( const netadr_t *from, int burst, int period );
void SVC_RateDropAddress( const netadr_t *from, int burst, int period );

void QDECL SV_SendServerCommand( client_t *cl, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));

void SV_AddOperatorCommands( void );
void SV_RemoveOperatorCommands( void );

void SV_MasterShutdown( void );
int SV_RateMsec( const client_t *client );
qboolean SV_HandleQuitOnExitLevel( const char *context );
void SV_ResetServerCvarRuntime( void );

void Zmq_RegisterCvarsAndInitRcon( void );
void Zmq_UpdatePasswords( void );
void Zmq_InitStatsPublisher( void );
void Zmq_ShutdownStatsPublisher( void );
void Zmq_ShutdownRuntime( void );
void Zmq_PumpRcon( void );
void Zmq_BroadcastRconOutput( const char *message );
void Zmq_SubmitMatchReport( const void *report );
void Zmq_ReportPlayerEvent( unsigned int steamIdLow, unsigned int steamIdHigh,
	const void *clientStats, const char *eventName, const void *payload );
void Zmq_SubmitMatchReportJson( const char *json );
void Zmq_SubmitMatchSummaryJson( const char *json );
void Zmq_ReportPlayerEventJson( const char *eventName, const char *json );
qboolean Zmq_RconActive( void );


//
// sv_init.cpp
//
void SV_SetConfigstring( int index, const char *val );
void SV_GetConfigstring( int index, char *buffer, int bufferSize );
void SV_UpdateConfigstrings( client_t *client );
void SV_UpdateQLCvarConfigstrings( qboolean force );

void SV_SetUserinfo( int index, const char *val );
void SV_GetUserinfo( int index, char *buffer, int bufferSize );

void SV_SpawnServer( const char *mapname, qboolean killBots );
void SV_ChangeMaxClients( void );

// Retail Quake Live factory, arena, and map-pool compatibility.
void SV_FactoryInit( void );
void SV_FactoryShutdown( void );
void SV_FactoryDeactivate( void );
void SV_FactoryReload_f( void );
void SV_ArenaReload_f( void );
void SV_MapPoolReload_f( void );
void SV_StartRandomMap_f( void );
qboolean SV_FactoryExists( const char *factoryName );
qboolean SV_FactoryHasActive( void );
qboolean SV_FactoryPrepareMap( const char *mapName, const char *factoryName,
	qboolean factoryArgumentPresent, qboolean developerMap );
void SV_FactoryPrintMapUsage( const char *commandName );
int SV_FactoryWebCatalogJsonSize( void );
qboolean SV_FactoryBuildWebCatalogJson( char *buffer, int bufferSize );
void SV_MapPoolRefreshCvars( void );
void SV_FactoryRefreshMountedContent( void );



//
// sv_client.cpp
//
void SV_GetChallenge( const netadr_t *from, const msg_t *msg );
void SV_InitChallenger( void );

void SV_DirectConnect( const netadr_t *from );
void SV_PrintClientStateChange( const client_t *cl, clientState_t newState );

void SV_ExecuteClientMessage( client_t *cl, msg_t *msg );
void SV_UserinfoChanged( client_t *cl, qboolean updateUserinfo, qboolean runFilter );

void SV_ClientEnterWorld( client_t *client );
void SV_FreeClient( client_t *client );
void SV_DropClient( client_t *drop, const char *reason );
void SV_StartDemoRecord( client_t *client );
void SV_StopDemoRecord( client_t *client, qboolean discard );
void SV_RecordDemoMessage( client_t *client, const msg_t *msg );

qboolean SV_ExecuteClientCommand( client_t *cl, const char *s );
void SV_ClientThink( client_t *cl, usercmd_t *cmd );

int SV_SendDownloadMessages( void );
int SV_SendQueuedMessages( void );

void SV_FreeIP4DB( void );
void SV_PrintLocations_f( client_t *client );

//
// sv_ccmds.cpp
//
void SV_Heartbeat_f( void );
client_t *SV_GetPlayerByHandle( void );

//
// sv_snapshot.cpp
//
void SV_AddServerCommand( client_t *client, const char *cmd );
void SV_UpdateServerCommandsToClient( client_t *client, msg_t *msg );
void SV_WriteFrameToClient( client_t *client, msg_t *msg );
void SV_SendMessageToClient( msg_t *msg, client_t *client );
void SV_SendClientMessages( void );
void SV_SendClientSnapshot( client_t *client );

void SV_InitSnapshotStorage( void );
void SV_IssueNewSnapshot( void );

int SV_RemainingGameState( void );

//
// sv_game.cpp
//
int	SV_NumForGentity( sharedEntity_t *ent );
sharedEntity_t *SV_GentityNum( int num );
playerState_t *SV_GameClientNum( int num );
svEntity_t	*SV_SvEntityForGentity( sharedEntity_t *gEnt );
sharedEntity_t *SV_GEntityForSvEntity( svEntity_t *svEnt );
void		SV_InitGameProgs ( void );
void		SV_RegisterGameCvars( void );
const svPlatformServiceStatus_t *SV_GetPlatformServiceStatus( svPlatformCapability_t capability );
qboolean	SV_PlatformServiceAvailable( svPlatformCapability_t capability );
const char	*SV_GetPlatformAuthProviderLabel( void );
const char	*SV_GetPlatformAuthPolicyLabel( void );
const char	*SV_GetSteamServerProviderLabel( void );
const char	*SV_GetSteamServerPolicyLabel( void );
const char	*SV_GetWorkshopProviderLabel( void );
const char	*SV_GetWorkshopPolicyLabel( void );
const char	*SV_GetServerStatsProviderLabel( void );
const char	*SV_GetServerStatsPolicyLabel( void );
void		SV_RefreshPlatformServiceCvars( void );
void		SV_RegisterSteamEventSink( void );
void		SV_PublishWorkshopReferences( void );
void		SV_SteamGameServerStart( const char *mapName );
void		SV_SteamGameServerStop( void );
void		SV_SteamP2PFrame( void );
void		SV_SteamP2PCloseClient( uint64_t steamId );
void		SV_SteamHandleIncomingPacket( const netadr_t *from, const msg_t *msg );
void		SV_GameRefreshRankingsPolicyCvars( void );
void		SV_ShutdownGameProgs ( void );
void		SV_RestartGameProgs( void );
qboolean	SV_ClientSteamId( int clientNum, unsigned int *steamIdLow, unsigned int *steamIdHigh );
qboolean	SV_VerifyClientSteamAuth( int clientNum );
void		SV_SteamStats_AddFieldValue( int clientNum, int statIndex, int delta );
void		SV_SteamStats_UnlockAchievement( int clientNum, int achievementId );
qboolean	SV_SteamStats_HasAchievement( int clientNum, int achievementId );
const void	*SV_SteamStats_ProcessMatchReport( const void *report, char *buffer, int bufferSize );
void		SV_SteamStats_ProcessEvent( unsigned int steamIdLow, unsigned int steamIdHigh,
				const void *clientStats, const char *eventName, const void *payload );
void		SV_FlushAllSteamStats( void );
void		SV_HandleSteamProviderEvent( unsigned int type, int result,
				uint64_t subjectId );
qboolean	SV_GameShouldSuppressVoiceToClient( int senderClientNum, int recipientClientNum );
qboolean	SV_GameIsClientAdmin( int clientNum );
qboolean	SV_GameAreEnemyClients( int clientNumA, int clientNumB );
int		SV_GameGetClientScore( int clientNum, int fallbackScore );
qboolean	SV_inPVS (const vec3_t p1, const vec3_t p2);

//
// sv_bot.cpp
//
void		SV_BotFrame( int time );
int			SV_BotAllocateClient(void);
void		SV_BotFreeClient( int clientNum );

void		SV_BotInitCvars(void);
int			SV_BotLibSetup( void );
int			SV_BotLibShutdown( void );
int			SV_BotGetSnapshotEntity( int client, int ent );
int			SV_BotGetConsoleMessage( int client, char *buf, int size );

int BotImport_DebugPolygonCreate(int color, int numPoints, vec3_t *points);
void BotImport_DebugPolygonDelete(int id);
void BotDrawDebugPolygons(void (*drawPoly)(int color, int numPoints, float *points), int value);

void SV_BotInitBotLib(void);

//============================================================
//
// high level object sorting to reduce interaction tests
//

void SV_ClearWorld (void);
// called after the world model has been loaded, before linking any entities

void SV_UnlinkEntity( sharedEntity_t *ent );
// call before removing an entity, and before trying to move one,
// so it doesn't clip against itself

void SV_LinkEntity( sharedEntity_t *ent );
// Needs to be called any time an entity changes origin, mins, maxs,
// or solid.  Automatically unlinks if needed.
// sets ent->r.absmin and ent->r.absmax
// sets ent->leafnums[] for pvs determination even if the entity
// is not solid


clipHandle_t SV_ClipHandleForEntity( const sharedEntity_t *ent );


void SV_SectorList_f( void );


int SV_AreaEntities( const vec3_t mins, const vec3_t maxs, int *entityList, int maxcount );
// fills in a table of entity numbers with entities that have bounding boxes
// that intersect the given area.  It is possible for a non-axial bmodel
// to be returned that doesn't actually intersect the area on an exact
// test.
// returns the number of pointers filled in
// The world entity is never returned in this list.


int SV_PointContents( const vec3_t p, int passEntityNum );
// returns the CONTENTS_* value from the world and all entities at the given point.


void SV_Trace( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask, qboolean capsule );
// mins and maxs are relative

// if the entire move stays in a solid volume, trace.allsolid will be set,
// trace.startsolid will be set, and trace.fraction will be 0

// if the starting point is in a solid, it will be allowed to move out
// to an open area

// passEntityNum is explicitly excluded from clipping checks (normally ENTITYNUM_NONE)


void SV_ClipToEntity( trace_t *trace, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int entityNum, int contentmask, qboolean capsule );
// clip to a specific entity

//
// sv_net_chan.cpp
//
void SV_Netchan_Transmit( client_t *client, msg_t *msg);
int SV_Netchan_TransmitNextFragment( client_t *client );
qboolean SV_Netchan_Process( client_t *client, msg_t *msg );
void SV_Netchan_FreeQueue( client_t *client );

//
// sv_filter.cpp
//
void SV_LoadFilters( const char *filename );
const char *SV_RunFilters( const char *userinfo, const netadr_t *addr );
void SV_AddFilter_f( void );
void SV_AddFilterCmd_f( void );

#ifdef __cplusplus
}

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <utility>

static inline qboolean SV_QBool( bool value ) {
	return value ? qtrue : qfalse;
}

static inline bool SV_AsBool( qboolean value ) {
	return value != qfalse;
}

static inline int SV_ClientIndex( const client_t *client ) {
	return static_cast<int>( client - svs.clients );
}

static inline int SV_SvEntityIndex( const svEntity_t *entity ) {
	return static_cast<int>( entity - sv.svEntities );
}

static inline bool SV_IsClientIndex( int clientNum ) {
	return clientNum >= 0 && clientNum < sv.maxclients;
}

class SV_IndexIterator {
public:
	explicit SV_IndexIterator( int value ) : value_( value ) {}

	bool operator!=( const SV_IndexIterator &other ) const { return value_ != other.value_; }
	void operator++() { ++value_; }
	int operator*() const { return value_; }

private:
	int value_;
};

struct SV_IndexRange {
	int first;
	int last;

	SV_IndexIterator begin() const { return SV_IndexIterator( first ); }
	SV_IndexIterator end() const { return SV_IndexIterator( last ); }
};

static inline SV_IndexRange SV_Indices( int count ) {
	return { 0, count };
}

static inline SV_IndexRange SV_Indices( int first, int last ) {
	return { first, last };
}

static inline client_t &SV_ClientForIndex( int clientNum ) {
	return svs.clients[clientNum];
}

static inline client_t *SV_ClientBegin( void ) {
	return svs.clients;
}

static inline client_t *SV_ClientEnd( void ) {
	return svs.clients ? svs.clients + sv.maxclients : nullptr;
}

struct SV_ClientRange {
	client_t *begin() const { return SV_ClientBegin(); }
	client_t *end() const { return SV_ClientEnd(); }
};

static inline SV_ClientRange SV_Clients( void ) {
	return {};
}

struct SV_ClientSlot {
	int index;
	client_t &client;
};

class SV_ClientSlotIterator {
public:
	SV_ClientSlotIterator( int index, client_t *client ) : index_( index ), client_( client ) {}

	bool operator!=( const SV_ClientSlotIterator &other ) const { return index_ != other.index_; }
	void operator++() { ++index_; ++client_; }
	SV_ClientSlot operator*() const { return { index_, *client_ }; }

private:
	int index_;
	client_t *client_;
};

struct SV_ClientSlotRange {
	SV_ClientSlotIterator begin() const { return { 0, SV_ClientBegin() }; }
	SV_ClientSlotIterator end() const { return { svs.clients ? sv.maxclients : 0, SV_ClientEnd() }; }
};

static inline SV_ClientSlotRange SV_ClientSlots( void ) {
	return {};
}

#ifdef USE_BANS
static inline serverBan_t &SV_BanForIndex( int index ) {
	return serverBans[index];
}

static inline serverBan_t *SV_BanBegin( void ) {
	return serverBans;
}

static inline serverBan_t *SV_BanEnd( void ) {
	return serverBans + serverBansCount;
}

struct SV_BanRange {
	serverBan_t *begin() const { return SV_BanBegin(); }
	serverBan_t *end() const { return SV_BanEnd(); }
};

static inline SV_BanRange SV_Bans( void ) {
	return {};
}

struct SV_BanSlot {
	int index;
	serverBan_t &ban;
};

class SV_BanSlotIterator {
public:
	SV_BanSlotIterator( int index, serverBan_t *ban ) : index_( index ), ban_( ban ) {}

	bool operator!=( const SV_BanSlotIterator &other ) const { return index_ != other.index_; }
	void operator++() { ++index_; ++ban_; }
	SV_BanSlot operator*() const { return { index_, *ban_ }; }

private:
	int index_;
	serverBan_t *ban_;
};

struct SV_BanSlotRange {
	SV_BanSlotIterator begin() const { return { 0, SV_BanBegin() }; }
	SV_BanSlotIterator end() const { return { serverBansCount, SV_BanEnd() }; }
};

static inline SV_BanSlotRange SV_BanSlots( void ) {
	return {};
}
#endif

static inline int SV_ParseInt( const char *text ) {
	return text ? static_cast<int>( std::strtol( text, nullptr, 10 ) ) : 0;
}

static inline void SV_CloseFileHandle( fileHandle_t &handle ) {
	if ( handle != FS_INVALID_HANDLE ) {
		FS_FCloseFile( handle );
		handle = FS_INVALID_HANDLE;
	}
}

static inline void SV_CloseStdFile( FILE *&file ) {
	if ( file != nullptr ) {
		fclose( file );
		file = nullptr;
	}
}

template <typename T>
static inline void SV_ZFree( T *&ptr ) {
	if ( ptr != nullptr ) {
		Z_Free( ptr );
		ptr = nullptr;
	}
}

template <typename T>
static inline void SV_HunkFreeTemp( T *&ptr ) {
	if ( ptr != nullptr ) {
		Hunk_FreeTempMemory( ptr );
		ptr = nullptr;
	}
}

template <typename T, std::size_t N>
static constexpr int SV_ArraySize( const std::array<T, N> & ) {
	static_assert( N <= static_cast<std::size_t>( (std::numeric_limits<int>::max)() ), "array size exceeds int range" );
	return static_cast<int>( N );
}

template <typename T, std::size_t N>
static constexpr int SV_ArraySize( const T (&)[N] ) {
	static_assert( N <= static_cast<std::size_t>( (std::numeric_limits<int>::max)() ), "array size exceeds int range" );
	return static_cast<int>( N );
}

template <typename F>
class SV_ScopeExit {
public:
	explicit SV_ScopeExit( F &&func ) : func_( std::move( func ) ) {}
	SV_ScopeExit( const SV_ScopeExit & ) = delete;
	SV_ScopeExit &operator=( const SV_ScopeExit & ) = delete;
	~SV_ScopeExit() { func_(); }

private:
	F func_;
};

template <typename F>
static inline SV_ScopeExit<F> SV_MakeScopeExit( F &&func ) {
	return SV_ScopeExit<F>( std::forward<F>( func ) );
}

template <typename T>
static inline int SV_AllocByteCount( int count ) {
	if ( count < 0 || static_cast<std::size_t>( count ) > static_cast<std::size_t>( (std::numeric_limits<int>::max)() ) / sizeof( T ) ) {
		Com_Error( ERR_FATAL, "SV_AllocByteCount: allocation size overflow" );
	}
	return static_cast<int>( sizeof( T ) * static_cast<std::size_t>( count ) );
}

template <typename T>
static inline T *SV_ZMalloc( void ) {
	return static_cast<T *>( Z_Malloc( sizeof( T ) ) );
}

template <typename T>
static inline T *SV_ZMallocArray( int count ) {
	return static_cast<T *>( Z_Malloc( SV_AllocByteCount<T>( count ) ) );
}

template <typename T>
static inline T *SV_ZMallocBytes( int bytes ) {
	return static_cast<T *>( Z_Malloc( bytes ) );
}

template <typename T>
static inline T *SV_ZTagMallocArray( int count, memtag_t tag ) {
	return static_cast<T *>( Z_TagMalloc( SV_AllocByteCount<T>( count ), tag ) );
}

template <typename T>
static inline T *SV_HunkAllocArray( int count, ha_pref preference ) {
	return static_cast<T *>( Hunk_Alloc( SV_AllocByteCount<T>( count ), preference ) );
}

template <typename T>
static inline T *SV_HunkTempAllocArray( int count ) {
	return static_cast<T *>( Hunk_AllocateTempMemory( SV_AllocByteCount<T>( count ) ) );
}
#endif
