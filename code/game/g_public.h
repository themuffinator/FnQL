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
//

// g_public.h -- game module information visible to server

#define	GAME_API_VERSION	8
#define	GAME_NATIVE_API_VERSION	10
#define	GAME_MATCH_GUID_BUFFER_SIZE	64

// entity->svFlags
// the server does not know how to interpret most of the values
// in entityStates (level eType), so the game must explicitly flag
// special server behaviors
#define	SVF_NOCLIENT			0x00000001	// don't send entity to clients, even if it has effects

// TTimo
// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=551
#define SVF_CLIENTMASK 0x00000002

#define SVF_BOT					0x00000008	// set if the entity is a bot
#define	SVF_BROADCAST			0x00000020	// send to all connected clients
#define	SVF_PORTAL				0x00000040	// merge a second pvs at origin2 into snapshots
#define	SVF_USE_CURRENT_ORIGIN	0x00000080	// entity->r.currentOrigin instead of entity->s.origin
											// for link position (missiles and movers)
#define SVF_SINGLECLIENT		0x00000100	// only send to a single client (entityShared_t->singleClient)
#define SVF_NOSERVERINFO		0x00000200	// don't send CS_SERVERINFO updates to this client
											// so that it can be updated for ping tools without
											// lagging clients
#define SVF_CAPSULE				0x00000400	// use capsule for collision detection instead of bbox
#define SVF_NOTSINGLECLIENT		0x00000800	// send entity to everyone but one client
											// (entityShared_t->singleClient)

#define SVF_SELF_PORTAL2		0x00020000  // merge a second pvs at entity->r.s.origin2 into snapshots

//===============================================================


typedef struct {
	entityState_t	s;				// communicated by server to clients

	qboolean	linked;				// qfalse if not in any good cluster
	int			linkcount;

	int			svFlags;			// SVF_NOCLIENT, SVF_BROADCAST, etc

	// only send to this client when SVF_SINGLECLIENT is set
	// if SVF_CLIENTMASK is set, use bitmask for clients to send to (maxclients must be <= 32, up to the mod to enforce this)
	int			singleClient;

	qboolean	bmodel;				// if false, assume an explicit mins / maxs bounding box
									// only set by trap_SetBrushModel
	vec3_t		mins, maxs;
	int			contents;			// CONTENTS_TRIGGER, CONTENTS_SOLID, CONTENTS_BODY, etc
									// a non-solid entity should set to 0

	vec3_t		absmin, absmax;		// derived from mins/maxs and origin + rotation

	// currentOrigin will be used for all collision detection and world linking.
	// it will not necessarily be the same as the trajectory evaluation for the current
	// time, because each entity must be moved one at a time after time is advanced
	// to avoid simultaneous collision issues
	vec3_t		currentOrigin;
	vec3_t		currentAngles;

	// when a trace call is made and passEntityNum != ENTITYNUM_NONE,
	// an ent will be excluded from testing if:
	// ent->s.number == passEntityNum	(don't interact with self)
	// ent->s.ownerNum = passEntityNum	(don't interact with your own missiles)
	// entity[ent->s.ownerNum].ownerNum = passEntityNum	(don't interact with other missiles from owner)
	int			ownerNum;
} entityShared_t;



// the server looks at a sharedEntity, which is the start of the game's gentity_t structure
typedef struct {
	entityState_t	s;				// communicated by server to clients
	entityShared_t	r;				// shared by both the server system and game
} sharedEntity_t;



//===============================================================

//
// system traps provided by the main engine
//
typedef enum {
	//============== general Quake services ==================

	G_PRINT,		// ( const char *string );
	// print message on the local console

	G_ERROR,		// ( const char *string );
	// abort the game

	G_MILLISECONDS,	// ( void );
	// get current time for profiling reasons
	// this should NOT be used for any game related tasks,
	// because it is not journaled

	// console variable interaction
	G_CVAR_REGISTER,	// ( vmCvar_t *vmCvar, const char *varName, const char *defaultValue, int flags );
	G_CVAR_UPDATE,	// ( vmCvar_t *vmCvar );
	G_CVAR_SET,		// ( const char *var_name, const char *value );
	G_CVAR_VARIABLE_INTEGER_VALUE,	// ( const char *var_name );

	G_CVAR_VARIABLE_STRING_BUFFER,	// ( const char *var_name, char *buffer, int bufsize );

	G_ARGC,			// ( void );
	// ClientCommand and ServerCommand parameter access

	G_ARGV,			// ( int n, char *buffer, int bufferLength );

	G_FS_FOPEN_FILE,	// ( const char *qpath, fileHandle_t *file, fsMode_t mode );
	G_FS_READ,		// ( void *buffer, int len, fileHandle_t f );
	G_FS_WRITE,		// ( const void *buffer, int len, fileHandle_t f );
	G_FS_FCLOSE_FILE,		// ( fileHandle_t f );

	G_SEND_CONSOLE_COMMAND,	// ( const char *text );
	// add commands to the console as if they were typed in
	// for map changing, etc


	//=========== server specific functionality =============

	G_LOCATE_GAME_DATA,		// ( gentity_t *gEnts, int numGEntities, int sizeofGEntity_t,
	//							playerState_t *clients, int sizeofGameClient );
	// the game needs to let the server system know where and how big the gentities
	// are, so it can look at them directly without going through an interface

	G_DROP_CLIENT,		// ( int clientNum, const char *reason );
	// kick a client off the server with a message

	G_SEND_SERVER_COMMAND,	// ( int clientNum, const char *text );
	// reliably sends a command string to be interpreted by the given
	// client.  If clientNum is -1, it will be sent to all clients

	G_SET_CONFIGSTRING,	// ( int num, const char *string );
	// config strings hold all the index strings, and various other information
	// that is reliably communicated to all clients
	// All of the current configstrings are sent to clients when
	// they connect, and changes are sent to all connected clients.
	// All confgstrings are cleared at each level start.

	G_GET_CONFIGSTRING,	// ( int num, char *buffer, int bufferSize );

	G_GET_USERINFO,		// ( int num, char *buffer, int bufferSize );
	// userinfo strings are maintained by the server system, so they
	// are persistant across level loads, while all other game visible
	// data is completely reset

	G_SET_USERINFO,		// ( int num, const char *buffer );

	G_GET_SERVERINFO,	// ( char *buffer, int bufferSize );
	// the serverinfo info string has all the cvars visible to server browsers

	G_SET_BRUSH_MODEL,	// ( gentity_t *ent, const char *name );
	// sets mins and maxs based on the brushmodel name

	G_TRACE,	// ( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask );
	// collision detection against all linked entities

	G_POINT_CONTENTS,	// ( const vec3_t point, int passEntityNum );
	// point contents against all linked entities

	G_IN_PVS,			// ( const vec3_t p1, const vec3_t p2 );

	G_IN_PVS_IGNORE_PORTALS,	// ( const vec3_t p1, const vec3_t p2 );

	G_ADJUST_AREA_PORTAL_STATE,	// ( gentity_t *ent, qboolean open );

	G_AREAS_CONNECTED,	// ( int area1, int area2 );

	G_LINKENTITY,		// ( gentity_t *ent );
	// an entity will never be sent to a client or used for collision
	// if it is not passed to linkentity.  If the size, position, or
	// solidity changes, it must be relinked.

	G_UNLINKENTITY,		// ( gentity_t *ent );
	// call before removing an interactive entity

	G_ENTITIES_IN_BOX,	// ( const vec3_t mins, const vec3_t maxs, gentity_t **list, int maxcount );
	// EntitiesInBox will return brush models based on their bounding box,
	// so exact determination must still be done with EntityContact

	G_ENTITY_CONTACT,	// ( const vec3_t mins, const vec3_t maxs, const gentity_t *ent );
	// perform an exact check against inline brush models of non-square shape

	// access for bots to get and free a server client (FIXME?)
	G_BOT_ALLOCATE_CLIENT,	// ( void );

	G_BOT_FREE_CLIENT,	// ( int clientNum );

	G_GET_USERCMD,	// ( int clientNum, usercmd_t *cmd )

	G_GET_ENTITY_TOKEN,	// qboolean ( char *buffer, int bufferSize )
	// Retrieves the next string token from the entity spawn text, returning
	// false when all tokens have been parsed.
	// This should only be done at GAME_INIT time.

	G_FS_GETFILELIST,
	G_DEBUG_POLYGON_CREATE,
	G_DEBUG_POLYGON_DELETE,
	G_REAL_TIME,
	G_SNAPVECTOR,

	G_TRACECAPSULE,	// ( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask );
	G_ENTITY_CONTACTCAPSULE,	// ( const vec3_t mins, const vec3_t maxs, const gentity_t *ent );

	// 1.32
	G_FS_SEEK,

	G_STEAMID_QUERY,
	G_STEAM_AUTH_VALIDATE,

	G_MATRIXMULTIPLY = TRAP_MATRIXMULTIPLY,
	G_ANGLEVECTORS = TRAP_ANGLEVECTORS,
	G_PERPENDICULARVECTOR = TRAP_PERPENDICULARVECTOR,
	G_FLOOR = TRAP_FLOOR,
	G_CEIL = TRAP_CEIL,
	G_TESTPRINTINT = TRAP_TESTPRINTINT,
	G_TESTPRINTFLOAT = TRAP_TESTPRINTFLOAT,

	BOTLIB_SETUP = 200,				// ( void );
	BOTLIB_SHUTDOWN,				// ( void );
	BOTLIB_LIBVAR_SET,
	BOTLIB_LIBVAR_GET,
	BOTLIB_PC_ADD_GLOBAL_DEFINE,
	BOTLIB_START_FRAME,
	BOTLIB_LOAD_MAP,
	BOTLIB_UPDATENTITY,
	BOTLIB_TEST,

	BOTLIB_GET_SNAPSHOT_ENTITY,		// ( int client, int ent );
	BOTLIB_GET_CONSOLE_MESSAGE,		// ( int client, char *message, int size );
	BOTLIB_USER_COMMAND,			// ( int client, usercmd_t *ucmd );

	BOTLIB_AAS_ENABLE_ROUTING_AREA = 300,
	BOTLIB_AAS_BBOX_AREAS,
	BOTLIB_AAS_AREA_INFO,
	BOTLIB_AAS_ENTITY_INFO,

	BOTLIB_AAS_INITIALIZED,
	BOTLIB_AAS_PRESENCE_TYPE_BOUNDING_BOX,
	BOTLIB_AAS_TIME,

	BOTLIB_AAS_POINT_AREA_NUM,
	BOTLIB_AAS_TRACE_AREAS,

	BOTLIB_AAS_POINT_CONTENTS,
	BOTLIB_AAS_NEXT_BSP_ENTITY,
	BOTLIB_AAS_VALUE_FOR_BSP_EPAIR_KEY,
	BOTLIB_AAS_VECTOR_FOR_BSP_EPAIR_KEY,
	BOTLIB_AAS_FLOAT_FOR_BSP_EPAIR_KEY,
	BOTLIB_AAS_INT_FOR_BSP_EPAIR_KEY,

	BOTLIB_AAS_AREA_REACHABILITY,

	BOTLIB_AAS_AREA_TRAVEL_TIME_TO_GOAL_AREA,

	BOTLIB_AAS_SWIMMING,
	BOTLIB_AAS_PREDICT_CLIENT_MOVEMENT,

	BOTLIB_EA_SAY = 400,
	BOTLIB_EA_SAY_TEAM,
	BOTLIB_EA_COMMAND,

	BOTLIB_EA_ACTION,
	BOTLIB_EA_GESTURE,
	BOTLIB_EA_TALK,
	BOTLIB_EA_ATTACK,
	BOTLIB_EA_USE,
	BOTLIB_EA_RESPAWN,
	BOTLIB_EA_CROUCH,
	BOTLIB_EA_MOVE_UP,
	BOTLIB_EA_MOVE_DOWN,
	BOTLIB_EA_MOVE_FORWARD,
	BOTLIB_EA_MOVE_BACK,
	BOTLIB_EA_MOVE_LEFT,
	BOTLIB_EA_MOVE_RIGHT,

	BOTLIB_EA_SELECT_WEAPON,
	BOTLIB_EA_JUMP,
	BOTLIB_EA_DELAYED_JUMP,
	BOTLIB_EA_MOVE,
	BOTLIB_EA_VIEW,

	BOTLIB_EA_END_REGULAR,
	BOTLIB_EA_GET_INPUT,
	BOTLIB_EA_RESET_INPUT,


	BOTLIB_AI_LOAD_CHARACTER = 500,
	BOTLIB_AI_FREE_CHARACTER,
	BOTLIB_AI_CHARACTERISTIC_FLOAT,
	BOTLIB_AI_CHARACTERISTIC_BFLOAT,
	BOTLIB_AI_CHARACTERISTIC_INTEGER,
	BOTLIB_AI_CHARACTERISTIC_BINTEGER,
	BOTLIB_AI_CHARACTERISTIC_STRING,

	BOTLIB_AI_ALLOC_CHAT_STATE,
	BOTLIB_AI_FREE_CHAT_STATE,
	BOTLIB_AI_QUEUE_CONSOLE_MESSAGE,
	BOTLIB_AI_REMOVE_CONSOLE_MESSAGE,
	BOTLIB_AI_NEXT_CONSOLE_MESSAGE,
	BOTLIB_AI_NUM_CONSOLE_MESSAGE,
	BOTLIB_AI_INITIAL_CHAT,
	BOTLIB_AI_REPLY_CHAT,
	BOTLIB_AI_CHAT_LENGTH,
	BOTLIB_AI_ENTER_CHAT,
	BOTLIB_AI_STRING_CONTAINS,
	BOTLIB_AI_FIND_MATCH,
	BOTLIB_AI_MATCH_VARIABLE,
	BOTLIB_AI_UNIFY_WHITE_SPACES,
	BOTLIB_AI_REPLACE_SYNONYMS,
	BOTLIB_AI_LOAD_CHAT_FILE,
	BOTLIB_AI_SET_CHAT_GENDER,
	BOTLIB_AI_SET_CHAT_NAME,

	BOTLIB_AI_RESET_GOAL_STATE,
	BOTLIB_AI_RESET_AVOID_GOALS,
	BOTLIB_AI_PUSH_GOAL,
	BOTLIB_AI_POP_GOAL,
	BOTLIB_AI_EMPTY_GOAL_STACK,
	BOTLIB_AI_DUMP_AVOID_GOALS,
	BOTLIB_AI_DUMP_GOAL_STACK,
	BOTLIB_AI_GOAL_NAME,
	BOTLIB_AI_GET_TOP_GOAL,
	BOTLIB_AI_GET_SECOND_GOAL,
	BOTLIB_AI_CHOOSE_LTG_ITEM,
	BOTLIB_AI_CHOOSE_NBG_ITEM,
	BOTLIB_AI_TOUCHING_GOAL,
	BOTLIB_AI_ITEM_GOAL_IN_VIS_BUT_NOT_VISIBLE,
	BOTLIB_AI_GET_LEVEL_ITEM_GOAL,
	BOTLIB_AI_AVOID_GOAL_TIME,
	BOTLIB_AI_INIT_LEVEL_ITEMS,
	BOTLIB_AI_UPDATE_ENTITY_ITEMS,
	BOTLIB_AI_LOAD_ITEM_WEIGHTS,
	BOTLIB_AI_FREE_ITEM_WEIGHTS,
	BOTLIB_AI_SAVE_GOAL_FUZZY_LOGIC,
	BOTLIB_AI_ALLOC_GOAL_STATE,
	BOTLIB_AI_FREE_GOAL_STATE,

	BOTLIB_AI_RESET_MOVE_STATE,
	BOTLIB_AI_MOVE_TO_GOAL,
	BOTLIB_AI_MOVE_IN_DIRECTION,
	BOTLIB_AI_RESET_AVOID_REACH,
	BOTLIB_AI_RESET_LAST_AVOID_REACH,
	BOTLIB_AI_REACHABILITY_AREA,
	BOTLIB_AI_MOVEMENT_VIEW_TARGET,
	BOTLIB_AI_ALLOC_MOVE_STATE,
	BOTLIB_AI_FREE_MOVE_STATE,
	BOTLIB_AI_INIT_MOVE_STATE,

	BOTLIB_AI_CHOOSE_BEST_FIGHT_WEAPON,
	BOTLIB_AI_GET_WEAPON_INFO,
	BOTLIB_AI_LOAD_WEAPON_WEIGHTS,
	BOTLIB_AI_ALLOC_WEAPON_STATE,
	BOTLIB_AI_FREE_WEAPON_STATE,
	BOTLIB_AI_RESET_WEAPON_STATE,

	BOTLIB_AI_GENETIC_PARENTS_AND_CHILD_SELECTION,
	BOTLIB_AI_INTERBREED_GOAL_FUZZY_LOGIC,
	BOTLIB_AI_MUTATE_GOAL_FUZZY_LOGIC,
	BOTLIB_AI_GET_NEXT_CAMP_SPOT_GOAL,
	BOTLIB_AI_GET_MAP_LOCATION_GOAL,
	BOTLIB_AI_NUM_INITIAL_CHATS,
	BOTLIB_AI_GET_CHAT_MESSAGE,
	BOTLIB_AI_REMOVE_FROM_AVOID_GOALS,
	BOTLIB_AI_PREDICT_VISIBLE_POSITION,

	BOTLIB_AI_SET_AVOID_GOAL_TIME,
	BOTLIB_AI_ADD_AVOID_SPOT,
	BOTLIB_AAS_ALTERNATIVE_ROUTE_GOAL,
	BOTLIB_AAS_PREDICT_ROUTE,
	BOTLIB_AAS_POINT_REACHABILITY_AREA_INDEX,

	BOTLIB_PC_LOAD_SOURCE,
	BOTLIB_PC_FREE_SOURCE,
	BOTLIB_PC_READ_TOKEN,
	BOTLIB_PC_SOURCE_FILE_AND_LINE,

	G_RANK_BEGIN,
	G_RANK_POLL,
	G_RANK_CHECK_INIT,
	G_RANK_ACTIVE,
	G_RANK_USER_STATUS,
	G_RANK_USER_RESET,
	G_RANK_REPORT_INT,
	G_RANK_REPORT_STR,

	// engine extensions
	G_CVAR_SETDESCRIPTION,
	G_TRAP_GETVALUE = COM_TRAP_GETVALUE

} gameImport_t;

#define GAME_LEGACY_IMPORT_COUNT	(G_TRAP_GETVALUE + 1)

// Retail Quake Live native DLL import slab recovered by QLSRP from
// qagamex86.dll. Gaps stay deliberately unbound until they are proven.
typedef enum {
	G_QL_IMPORT_SEND_CONSOLE_COMMAND = 0,
	G_QL_IMPORT_REAL_TIME = 1,
	G_QL_IMPORT_PRINT = 2,
	G_QL_IMPORT_MILLISECONDS = 3,
	G_QL_IMPORT_FS_WRITE = 4,
	G_QL_IMPORT_FS_SEEK = 5,
	G_QL_IMPORT_FS_READ = 6,
	G_QL_IMPORT_FS_GETFILELIST = 7,
	G_QL_IMPORT_FS_FOPEN_FILE = 8,
	G_QL_IMPORT_FS_FCLOSE_FILE = 9,
	G_QL_IMPORT_ERROR = 10,
	G_QL_IMPORT_CVAR_VARIABLE_INTEGER_VALUE = 11,
	G_QL_IMPORT_CVAR_UPDATE = 12,
	G_QL_IMPORT_CVAR_VARIABLE_STRING_BUFFER = 13,
	G_QL_IMPORT_CVAR_SET_VALUE = 14,
	G_QL_IMPORT_CVAR_SET = 15,
	G_QL_IMPORT_CVAR_REGISTER_BOUNDED = 16,
	G_QL_IMPORT_CVAR_REGISTER = 17,
	G_QL_IMPORT_ARGV = 18,
	G_QL_IMPORT_ARGS = 19,
	G_QL_IMPORT_ARGC = 20,
	G_QL_IMPORT_CMD_TOKENIZE_STRING = 21,
	G_QL_IMPORT_LOCATE_GAME_DATA = 22,
	G_QL_IMPORT_DROP_CLIENT = 23,
	G_QL_IMPORT_SEND_SERVER_COMMAND = 24,
	G_QL_IMPORT_SET_CONFIGSTRING = 25,
	G_QL_IMPORT_GET_CONFIGSTRING = 26,
	G_QL_IMPORT_RESEND_CONFIGSTRING = 27,
	G_QL_IMPORT_GET_USERINFO = 28,
	G_QL_IMPORT_SET_USERINFO = 29,
	G_QL_IMPORT_GET_SERVERINFO = 30,
	G_QL_IMPORT_SET_BRUSH_MODEL = 31,
	G_QL_IMPORT_TRACE = 32,
	G_QL_IMPORT_TRACECAPSULE = 33,
	G_QL_IMPORT_POINT_CONTENTS = 34,
	G_QL_IMPORT_IN_PVS = 35,
	G_QL_IMPORT_IN_PVS_IGNORE_PORTALS = 36,
	G_QL_IMPORT_ADJUST_AREA_PORTAL_STATE = 37,
	G_QL_IMPORT_AREAS_CONNECTED = 38,
	G_QL_IMPORT_LINKENTITY = 39,
	G_QL_IMPORT_UNLINK_ENTITY = 40,
	G_QL_IMPORT_ENTITIES_IN_BOX = 41,
	G_QL_IMPORT_ENTITY_CONTACT = 42,
	G_QL_IMPORT_BOT_ALLOCATE_CLIENT = 43,
	G_QL_IMPORT_BOT_FREE_CLIENT = 44,
	G_QL_IMPORT_GET_USERCMD = 45,
	G_QL_IMPORT_GET_ENTITY_TOKEN = 46,
	G_QL_IMPORT_DEBUG_POLYGON_CREATE = 47,
	G_QL_IMPORT_DEBUG_POLYGON_DELETE = 48,
	G_QL_IMPORT_BOTLIB_SETUP = 49,
	G_QL_IMPORT_BOTLIB_SHUTDOWN = 50,
	G_QL_IMPORT_BOTLIB_LIBVAR_SET = 51,
	G_QL_IMPORT_BOTLIB_LIBVAR_GET = 52,
	G_QL_IMPORT_BOTLIB_PC_ADD_GLOBAL_DEFINE = 53,
	G_QL_IMPORT_BOTLIB_START_FRAME = 54,
	G_QL_IMPORT_BOTLIB_LOAD_MAP = 55,
	G_QL_IMPORT_BOTLIB_UPDATE_ENTITY = 56,
	G_QL_IMPORT_BOTLIB_TEST = 57,
	G_QL_IMPORT_BOTLIB_GET_SNAPSHOT_ENTITY = 58,
	G_QL_IMPORT_BOTLIB_GET_CONSOLE_MESSAGE = 59,
	G_QL_IMPORT_BOTLIB_USER_COMMAND = 60,
	G_QL_IMPORT_BOTLIB_AAS_BBOX_AREAS = 61,
	G_QL_IMPORT_BOTLIB_AAS_AREA_INFO = 62,
	G_QL_IMPORT_BOTLIB_AAS_ENTITY_INFO = 63,
	G_QL_IMPORT_BOTLIB_AAS_INITIALIZED = 64,
	G_QL_IMPORT_BOTLIB_AAS_PRESENCE_TYPE_BOUNDING_BOX = 65,
	G_QL_IMPORT_BOTLIB_AAS_TIME = 66,
	G_QL_IMPORT_BOTLIB_AAS_POINT_AREA_NUM = 67,
	G_QL_IMPORT_BOTLIB_AAS_POINT_REACHABILITY_AREA_INDEX = 68,
	G_QL_IMPORT_BOTLIB_AAS_TRACE_AREAS = 69,
	G_QL_IMPORT_BOTLIB_AAS_POINT_CONTENTS = 70,
	G_QL_IMPORT_BOTLIB_AAS_NEXT_BSP_ENTITY = 71,
	G_QL_IMPORT_BOTLIB_AAS_VALUE_FOR_BSP_EPAIR_KEY = 72,
	G_QL_IMPORT_BOTLIB_AAS_VECTOR_FOR_BSP_EPAIR_KEY = 73,
	G_QL_IMPORT_BOTLIB_AAS_FLOAT_FOR_BSP_EPAIR_KEY = 74,
	G_QL_IMPORT_BOTLIB_AAS_INT_FOR_BSP_EPAIR_KEY = 75,
	G_QL_IMPORT_BOTLIB_AAS_AREA_REACHABILITY = 76,
	G_QL_IMPORT_BOTLIB_AAS_AREA_TRAVEL_TIME_TO_GOAL_AREA = 77,
	G_QL_IMPORT_BOTLIB_AAS_ENABLE_ROUTING_AREA = 78,
	G_QL_IMPORT_BOTLIB_AAS_PREDICT_ROUTE = 79,
	G_QL_IMPORT_BOTLIB_AAS_ALTERNATIVE_ROUTE_GOAL = 80,
	G_QL_IMPORT_BOTLIB_AAS_SWIMMING = 81,
	G_QL_IMPORT_BOTLIB_AAS_PREDICT_CLIENT_MOVEMENT = 82,
	G_QL_IMPORT_BOTLIB_AI_DRAW_DEBUG_AREAS = 83,
	G_QL_IMPORT_BOTLIB_AI_DRAW_AVOID_SPOTS = 84,
	G_QL_IMPORT_BOTLIB_EA_SAY = 85,
	G_QL_IMPORT_BOTLIB_EA_SAY_TEAM = 86,
	G_QL_IMPORT_BOTLIB_EA_COMMAND = 87,
	G_QL_IMPORT_BOTLIB_EA_ACTION = 88,
	G_QL_IMPORT_BOTLIB_EA_WALK = 89,
	G_QL_IMPORT_BOTLIB_EA_GESTURE = 90,
	G_QL_IMPORT_BOTLIB_EA_TALK = 91,
	G_QL_IMPORT_BOTLIB_EA_ATTACK = 92,
	G_QL_IMPORT_BOTLIB_EA_USE = 93,
	G_QL_IMPORT_BOTLIB_EA_RESPAWN = 94,
	G_QL_IMPORT_BOTLIB_EA_CROUCH = 95,
	G_QL_IMPORT_BOTLIB_EA_MOVE_UP = 96,
	G_QL_IMPORT_BOTLIB_EA_MOVE_DOWN = 97,
	G_QL_IMPORT_BOTLIB_EA_MOVE_FORWARD = 98,
	G_QL_IMPORT_BOTLIB_EA_MOVE_BACK = 99,
	G_QL_IMPORT_BOTLIB_EA_MOVE_LEFT = 100,
	G_QL_IMPORT_BOTLIB_EA_MOVE_RIGHT = 101,
	G_QL_IMPORT_BOTLIB_EA_SELECT_WEAPON = 102,
	G_QL_IMPORT_BOTLIB_EA_JUMP = 103,
	G_QL_IMPORT_BOTLIB_EA_DELAYED_JUMP = 104,
	G_QL_IMPORT_BOTLIB_EA_MOVE = 105,
	G_QL_IMPORT_BOTLIB_EA_VIEW = 106,
	G_QL_IMPORT_BOTLIB_EA_END_REGULAR = 107,
	G_QL_IMPORT_BOTLIB_EA_GET_INPUT = 108,
	G_QL_IMPORT_BOTLIB_EA_RESET_INPUT = 109,
	G_QL_IMPORT_BOTLIB_AI_LOAD_CHARACTER = 110,
	G_QL_IMPORT_BOTLIB_AI_FREE_CHARACTER = 111,
	G_QL_IMPORT_BOTLIB_AI_CHARACTERISTIC_FLOAT = 112,
	G_QL_IMPORT_BOTLIB_AI_CHARACTERISTIC_BFLOAT = 113,
	G_QL_IMPORT_BOTLIB_AI_CHARACTERISTIC_INTEGER = 114,
	G_QL_IMPORT_BOTLIB_AI_CHARACTERISTIC_BINTEGER = 115,
	G_QL_IMPORT_BOTLIB_AI_CHARACTERISTIC_STRING = 116,
	G_QL_IMPORT_BOTLIB_AI_ALLOC_CHAT_STATE = 117,
	G_QL_IMPORT_BOTLIB_AI_FREE_CHAT_STATE = 118,
	G_QL_IMPORT_BOTLIB_AI_QUEUE_CONSOLE_MESSAGE = 119,
	G_QL_IMPORT_BOTLIB_AI_REMOVE_CONSOLE_MESSAGE = 120,
	G_QL_IMPORT_BOTLIB_AI_NEXT_CONSOLE_MESSAGE = 121,
	G_QL_IMPORT_BOTLIB_AI_NUM_CONSOLE_MESSAGE = 122,
	G_QL_IMPORT_BOTLIB_AI_INITIAL_CHAT = 123,
	G_QL_IMPORT_BOTLIB_AI_NUM_INITIAL_CHATS = 124,
	G_QL_IMPORT_BOTLIB_AI_REPLY_CHAT = 125,
	G_QL_IMPORT_BOTLIB_AI_CHAT_LENGTH = 126,
	G_QL_IMPORT_BOTLIB_AI_ENTER_CHAT = 127,
	G_QL_IMPORT_BOTLIB_AI_GET_CHAT_MESSAGE = 128,
	G_QL_IMPORT_BOTLIB_AI_STRING_CONTAINS = 129,
	G_QL_IMPORT_BOTLIB_AI_FIND_MATCH = 130,
	G_QL_IMPORT_BOTLIB_AI_MATCH_VARIABLE = 131,
	G_QL_IMPORT_BOTLIB_AI_UNIFY_WHITE_SPACES = 132,
	G_QL_IMPORT_BOTLIB_AI_REPLACE_SYNONYMS = 133,
	G_QL_IMPORT_BOTLIB_AI_LOAD_CHAT_FILE = 134,
	G_QL_IMPORT_BOTLIB_AI_SET_CHAT_GENDER = 135,
	G_QL_IMPORT_BOTLIB_AI_SET_CHAT_NAME = 136,
	G_QL_IMPORT_BOTLIB_AI_RESET_GOAL_STATE = 137,
	G_QL_IMPORT_BOTLIB_AI_REMOVE_FROM_AVOID_GOALS = 138,
	G_QL_IMPORT_BOTLIB_AI_RESET_AVOID_GOALS = 139,
	G_QL_IMPORT_BOTLIB_AI_PUSH_GOAL = 140,
	G_QL_IMPORT_BOTLIB_AI_POP_GOAL = 141,
	G_QL_IMPORT_BOTLIB_AI_EMPTY_GOAL_STACK = 142,
	G_QL_IMPORT_BOTLIB_AI_DUMP_AVOID_GOALS = 143,
	G_QL_IMPORT_BOTLIB_AI_DUMP_GOAL_STACK = 144,
	G_QL_IMPORT_BOTLIB_AI_GOAL_NAME = 145,
	G_QL_IMPORT_BOTLIB_AI_GET_TOP_GOAL = 146,
	G_QL_IMPORT_BOTLIB_AI_GET_SECOND_GOAL = 147,
	G_QL_IMPORT_BOTLIB_AI_CHOOSE_LTG_ITEM = 148,
	G_QL_IMPORT_BOTLIB_AI_CHOOSE_NBG_ITEM = 149,
	G_QL_IMPORT_BOTLIB_AI_TOUCHING_GOAL = 150,
	G_QL_IMPORT_BOTLIB_AI_ITEM_GOAL_IN_VIS_BUT_NOT_VISIBLE = 151,
	G_QL_IMPORT_BOTLIB_AI_GET_NEXT_CAMP_SPOT_GOAL = 152,
	G_QL_IMPORT_BOTLIB_AI_GET_MAP_LOCATION_GOAL = 153,
	G_QL_IMPORT_BOTLIB_AI_GET_LEVEL_ITEM_GOAL = 154,
	G_QL_IMPORT_BOTLIB_AI_AVOID_GOAL_TIME = 155,
	G_QL_IMPORT_BOTLIB_AI_SET_AVOID_GOAL_TIME = 156,
	G_QL_IMPORT_BOTLIB_AI_INIT_LEVEL_ITEMS = 157,
	G_QL_IMPORT_BOTLIB_AI_UPDATE_ENTITY_ITEMS = 158,
	G_QL_IMPORT_BOTLIB_AI_LOAD_ITEM_WEIGHTS = 159,
	G_QL_IMPORT_BOTLIB_AI_FREE_ITEM_WEIGHTS = 160,
	G_QL_IMPORT_BOTLIB_AI_INTERBREED_GOAL_FUZZY_LOGIC = 161,
	G_QL_IMPORT_BOTLIB_AI_SAVE_GOAL_FUZZY_LOGIC = 162,
	G_QL_IMPORT_BOTLIB_AI_MUTATE_GOAL_FUZZY_LOGIC = 163,
	G_QL_IMPORT_BOTLIB_AI_ALLOC_GOAL_STATE = 164,
	G_QL_IMPORT_BOTLIB_AI_FREE_GOAL_STATE = 165,
	G_QL_IMPORT_BOTLIB_AI_RESET_MOVE_STATE = 166,
	G_QL_IMPORT_BOTLIB_AI_MOVE_TO_GOAL = 167,
	G_QL_IMPORT_BOTLIB_AI_MOVE_IN_DIRECTION = 168,
	G_QL_IMPORT_BOTLIB_AI_RESET_AVOID_REACH = 169,
	G_QL_IMPORT_BOTLIB_AI_RESET_LAST_AVOID_REACH = 170,
	G_QL_IMPORT_BOTLIB_AI_REACHABILITY_AREA = 171,
	G_QL_IMPORT_BOTLIB_AI_MOVEMENT_VIEW_TARGET = 172,
	G_QL_IMPORT_BOTLIB_AI_PREDICT_VISIBLE_POSITION = 173,
	G_QL_IMPORT_BOTLIB_AI_ALLOC_MOVE_STATE = 174,
	G_QL_IMPORT_BOTLIB_AI_FREE_MOVE_STATE = 175,
	G_QL_IMPORT_BOTLIB_AI_INIT_MOVE_STATE = 176,
	G_QL_IMPORT_BOTLIB_AI_ADD_AVOID_SPOT = 177,
	G_QL_IMPORT_BOTLIB_AI_CHOOSE_BEST_FIGHT_WEAPON = 178,
	G_QL_IMPORT_BOTLIB_AI_GET_WEAPON_INFO = 179,
	G_QL_IMPORT_BOTLIB_AI_LOAD_WEAPON_WEIGHTS = 180,
	G_QL_IMPORT_BOTLIB_AI_ALLOC_WEAPON_STATE = 181,
	G_QL_IMPORT_BOTLIB_AI_FREE_WEAPON_STATE = 182,
	G_QL_IMPORT_BOTLIB_AI_RESET_WEAPON_STATE = 183,
	G_QL_IMPORT_BOTLIB_AI_GENETIC_PARENTS_AND_CHILD_SELECTION = 184,
	G_QL_IMPORT_SUBMIT_MATCH_REPORT = 185,
	G_QL_IMPORT_REPORT_PLAYER_EVENT = 186,
	G_QL_IMPORT_MARK_CLIENT_GOT_CP = 192,
	G_QL_IMPORT_RETURN_ZERO_198 = 198,
	G_QL_IMPORT_RETURN_ZERO_199 = 199,
	G_QL_IMPORT_STEAMID_QUERY = 200,
	G_QL_IMPORT_STEAM_STAT_ADD = 201,
	G_QL_IMPORT_GENERATE_MATCH_GUID = 202,
	G_QL_IMPORT_STEAM_UNLOCK_ACHIEVEMENT = 203,
	G_QL_IMPORT_STEAM_HAS_ACHIEVEMENT = 204,
	G_QL_IMPORT_STEAM_AUTH_VALIDATE = 205,
	G_QL_IMPORT_FACTORY_EXISTS = 206,

	G_QL_IMPORT_COUNT = 207,
	G_QL_IMPORT_COMPAT_BASE = G_QL_IMPORT_COUNT,
	G_QL_IMPORT_TOTAL_COUNT = G_QL_IMPORT_COMPAT_BASE + GAME_LEGACY_IMPORT_COUNT
} gameNativeImport_t;

#define GAME_NATIVE_IMPORT_COUNT	G_QL_IMPORT_TOTAL_COUNT


//
// functions exported by the game subsystem through vmMain
//
typedef enum {
	GAME_INIT,	// ( int levelTime, int randomSeed, int restart );
	// init and shutdown will be called every single level
	// The game should call G_GET_ENTITY_TOKEN to parse through all the
	// entity configuration text and spawn gentities.

	GAME_SHUTDOWN,	// ( int restart );

	GAME_CLIENT_CONNECT,	// ( int clientNum, qboolean firstTime, qboolean isBot );
	// return NULL if the client is allowed to connect, otherwise return
	// a text string with the reason for denial

	GAME_CLIENT_BEGIN,				// ( int clientNum );

	GAME_CLIENT_USERINFO_CHANGED,	// ( int clientNum );

	GAME_CLIENT_DISCONNECT,			// ( int clientNum );

	GAME_CLIENT_COMMAND,			// ( int clientNum );

	GAME_CLIENT_THINK,				// ( int clientNum );

	GAME_RUN_FRAME,					// ( int levelTime );

	GAME_CONSOLE_COMMAND,			// ( void );
	// ConsoleCommand will be called when a command has been issued
	// that is not recognized as a builtin function.
	// The game can issue trap_argc() / trap_argv() commands to get the command
	// and parameters.  Return qfalse if the game doesn't recognize it as a command.

	BOTAI_START_FRAME,				// ( int time );

	GAME_CAN_CLIENT_SEE_CLIENT,			// ( int viewerClientNum, int targetClientNum );
	GAME_FREEZE_CAN_SEE_THAW_PROGRESS_EVENT,	// ( int clientNum, int entNum );
	GAME_IS_OBJECTIVE_ENTITY,			// ( int entNum );
	GAME_SHOULD_SUPPRESS_VOICE_TO_CLIENT,		// ( int senderClientNum, int recipientClientNum );
	GAME_IS_CLIENT_ADMIN,				// ( int clientNum );
	GAME_ARE_ENEMY_CLIENTS,			// ( int clientNumA, int clientNumB );
	GAME_GET_CLIENT_SCORE,				// ( int clientNum );

	GAME_EXPORT_LAST
} gameExport_t;

// Retail Quake Live native DLL slot order recovered from qagamex86.dll dllEntry().
typedef enum {
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
	GAME_NATIVE_EXPORT_GET_CLIENT_SCORE,
	GAME_NATIVE_EXPORT_COUNT
} gameNativeExport_t;

