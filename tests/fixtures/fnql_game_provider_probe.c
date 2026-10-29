/*
===========================================================================

FnQL synthetic qagame provider probe

This is test-only code.  It implements the structured Quake Live qagame
dllEntry contract with no game behavior, so the runtime test can exercise
vm_gameProvider selection without depending on the FnQL-game repository.

Build variants:
  default               identifies as an FnQL-game qagame (FnQL_GetModuleInfo)
  PROBE_NO_IDENTITY     exports no identity record
  PROBE_WRONG_MODULE    identifies as an FnQL-game cgame (must be rejected)

===========================================================================
*/

#define GAME_NATIVE_API_VERSION 10
#define GAME_NATIVE_EXPORT_COUNT 19

#define QDECL __cdecl
#define PROBE_EXPORT __declspec(dllexport)

enum {
	G_QL_IMPORT_PRINT = 2,
	G_QL_IMPORT_LOCATE_GAME_DATA = 22
};

enum {
	GAME_NATIVE_EXPORT_SHUTDOWN,
	GAME_NATIVE_EXPORT_RUN_FRAME,
	GAME_NATIVE_EXPORT_REGISTER_CVARS,
	GAME_NATIVE_EXPORT_INIT,
	GAME_NATIVE_EXPORT_CONSOLE_COMMAND,
	GAME_NATIVE_EXPORT_CLIENT_USERINFO_CHANGED,
	GAME_NATIVE_EXPORT_CLIENT_THINK,
	GAME_NATIVE_EXPORT_CLIENT_DISCONNECT,
	GAME_NATIVE_EXPORT_CLIENT_CONNECT,
	GAME_NATIVE_EXPORT_CLIENT_COMMAND,
	GAME_NATIVE_EXPORT_CLIENT_BEGIN,
	GAME_NATIVE_EXPORT_BOTAI_START_FRAME,
	GAME_NATIVE_EXPORT_CAN_CLIENT_SEE_CLIENT,
	GAME_NATIVE_EXPORT_FREEZE_CAN_SEE_THAW_PROGRESS_EVENT,
	GAME_NATIVE_EXPORT_IS_OBJECTIVE_ENTITY,
	GAME_NATIVE_EXPORT_SHOULD_SUPPRESS_VOICE_TO_CLIENT,
	GAME_NATIVE_EXPORT_IS_CLIENT_ADMIN,
	GAME_NATIVE_EXPORT_ARE_ENEMY_CLIENTS,
	GAME_NATIVE_EXPORT_GET_CLIENT_SCORE
};

typedef void ( QDECL *print_f )( const char *text );
typedef void ( QDECL *locate_game_data_f )( void *gEnts, int numGEntities, int sizeofGEntity,
	void *clients, int sizeofGClient );

// Generous strides: native modules are not bounds-checked by the engine and
// only the entity and client prefixes it shares are ever read.
#define PROBE_ENTITY_COUNT	64
#define PROBE_ENTITY_STRIDE	2048
#define PROBE_CLIENT_STRIDE	4096

static void **probeImports;
static void *probeExports[GAME_NATIVE_EXPORT_COUNT];
static unsigned char probeEntities[PROBE_ENTITY_COUNT * PROBE_ENTITY_STRIDE];
static unsigned char probeClients[64 * PROBE_CLIENT_STRIDE];
static int probeFrames;

static void ProbePrint( const char *text ) {
	( (print_f)probeImports[G_QL_IMPORT_PRINT] )( text );
}

static void QDECL ProbeShutdown( int restart ) {
	(void)restart;
	ProbePrint( "FnQL game provider probe: shutdown\n" );
}

static void QDECL ProbeRunFrame( int levelTime ) {
	(void)levelTime;
	if ( ++probeFrames == 10 ) {
		ProbePrint( "FnQL game provider probe: frames ok\n" );
	}
}

static void QDECL ProbeRegisterCvars( void ) {
}

static void QDECL ProbeInit( int levelTime, int randomSeed, int restart ) {
	(void)levelTime;
	(void)randomSeed;
	(void)restart;
	probeFrames = 0;
	( (locate_game_data_f)probeImports[G_QL_IMPORT_LOCATE_GAME_DATA] )( probeEntities,
		PROBE_ENTITY_COUNT, PROBE_ENTITY_STRIDE, probeClients, PROBE_CLIENT_STRIDE );
	ProbePrint( "FnQL game provider probe: init ok\n" );
}

static int QDECL ProbeConsoleCommand( void ) {
	return 0;
}

static void QDECL ProbeClientNoop( int clientNum ) {
	(void)clientNum;
}

static const char * QDECL ProbeClientConnect( int clientNum, int firstTime, int isBot ) {
	(void)clientNum;
	(void)firstTime;
	(void)isBot;
	return "FnQL game provider probe accepts no clients";
}

static int QDECL ProbeBotAIStartFrame( int time ) {
	(void)time;
	return 0;
}

static int QDECL ProbeClientPair( int first, int second ) {
	(void)first;
	(void)second;
	return 0;
}

static int QDECL ProbeClientQuery( int clientNum ) {
	(void)clientNum;
	return 0;
}

PROBE_EXPORT void QDECL dllEntry( void **exports, void *imports, int *apiVersion ) {
	probeImports = (void **)imports;

	probeExports[GAME_NATIVE_EXPORT_SHUTDOWN] = (void *)ProbeShutdown;
	probeExports[GAME_NATIVE_EXPORT_RUN_FRAME] = (void *)ProbeRunFrame;
	probeExports[GAME_NATIVE_EXPORT_REGISTER_CVARS] = (void *)ProbeRegisterCvars;
	probeExports[GAME_NATIVE_EXPORT_INIT] = (void *)ProbeInit;
	probeExports[GAME_NATIVE_EXPORT_CONSOLE_COMMAND] = (void *)ProbeConsoleCommand;
	probeExports[GAME_NATIVE_EXPORT_CLIENT_USERINFO_CHANGED] = (void *)ProbeClientNoop;
	probeExports[GAME_NATIVE_EXPORT_CLIENT_THINK] = (void *)ProbeClientNoop;
	probeExports[GAME_NATIVE_EXPORT_CLIENT_DISCONNECT] = (void *)ProbeClientNoop;
	probeExports[GAME_NATIVE_EXPORT_CLIENT_CONNECT] = (void *)ProbeClientConnect;
	probeExports[GAME_NATIVE_EXPORT_CLIENT_COMMAND] = (void *)ProbeClientNoop;
	probeExports[GAME_NATIVE_EXPORT_CLIENT_BEGIN] = (void *)ProbeClientNoop;
	probeExports[GAME_NATIVE_EXPORT_BOTAI_START_FRAME] = (void *)ProbeBotAIStartFrame;
	probeExports[GAME_NATIVE_EXPORT_CAN_CLIENT_SEE_CLIENT] = (void *)ProbeClientPair;
	probeExports[GAME_NATIVE_EXPORT_FREEZE_CAN_SEE_THAW_PROGRESS_EVENT] = (void *)ProbeClientPair;
	probeExports[GAME_NATIVE_EXPORT_IS_OBJECTIVE_ENTITY] = (void *)ProbeClientQuery;
	probeExports[GAME_NATIVE_EXPORT_SHOULD_SUPPRESS_VOICE_TO_CLIENT] = (void *)ProbeClientPair;
	probeExports[GAME_NATIVE_EXPORT_IS_CLIENT_ADMIN] = (void *)ProbeClientQuery;
	probeExports[GAME_NATIVE_EXPORT_ARE_ENEMY_CLIENTS] = (void *)ProbeClientPair;
	probeExports[GAME_NATIVE_EXPORT_GET_CLIENT_SCORE] = (void *)ProbeClientQuery;

	*exports = probeExports;
	*apiVersion = GAME_NATIVE_API_VERSION;
}

#ifndef PROBE_NO_IDENTITY
typedef struct {
	int				size;
	int				infoVersion;
	const char		*product;
	const char		*module;
	const char		*version;
	const char		*build;
	unsigned int	capabilities;
	int				nativeApi;
} probeModuleInfo_t;

static const probeModuleInfo_t probeModuleInfo = {
	sizeof( probeModuleInfo_t ),
	1,
	"FnQL-game",
#ifdef PROBE_WRONG_MODULE
	"cgame",
#else
	"qagame",
#endif
	"0.0.0-probe",
	"provider-probe",
	0u,
	GAME_NATIVE_API_VERSION
};

PROBE_EXPORT const probeModuleInfo_t * QDECL FnQL_GetModuleInfo( void ) {
	return &probeModuleInfo;
}
#endif
