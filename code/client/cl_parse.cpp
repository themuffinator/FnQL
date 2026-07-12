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
// cl_parse.cpp -- parse a message received from the server

extern "C" {
#include "client.h"
}

#include "client_cpp.h"
#include "../qcommon/netchan_safety.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

using fnql::CloseFile;
using fnql::FileWrite;
using fnql::ToQboolean;

static constexpr std::array svc_strings = {
	"svc_bad",
	"svc_nop",
	"svc_gamestate",
	"svc_configstring",
	"svc_baseline",
	"svc_serverCommand",
	"svc_download",
	"svc_snapshot",
	"svc_EOF",
	"svc_voipSpeex", // ioq3 extension
	"svc_voipOpus",  // ioq3 extension
};

static void CL_ValidateReliableAcknowledge( void ) {
	std::uint32_t pending = 0;
	if ( !fnql::net::PendingCounterCount( clc.reliableSequence,
		clc.reliableAcknowledge, pending ) ) {
		if ( clc.demoplaying ) {
			clc.reliableSequence = clc.reliableAcknowledge;
		} else {
			Com_Error( ERR_DROP, "%s: incorrect reliable sequence acknowledge number", __func__ );
		}
	} else if ( pending > MAX_RELIABLE_COMMANDS ) {
		if ( !clc.demoplaying ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: dropping %u commands from server\n", pending );
		}
		clc.reliableAcknowledge = clc.reliableSequence;
	}
}

static void SHOWNET( msg_t *msg, const char *s ) {
	if ( cl_shownet->integer >= 2) {
		Com_Printf ("%3i:%s\n", msg->readcount-1, s);
	}
}

static bool CL_IsLegacyDemoMessage( void ) {
	return clc.demoplaying && clc.demoLegacyFormat;
}

static int CL_LegacyDemoProtocol( void ) {
	return clc.demoLegacyProtocol ? clc.demoLegacyProtocol : 43;
}

static void CL_AlignLegacyDemoMessage( msg_t *msg ) {
	if ( ( msg->bit & 7 ) == 0 ) {
		return;
	}

	msg->bit = ( msg->bit + 7 ) & ~7;
	msg->readcount = msg->bit >> 3;
}

enum {
	LEGACY_DM3_MAX_CLIENTS = 128,
	LEGACY_DM3_ET_EVENTS = 12,
	LEGACY_CS_GAME_VERSION = 12,
	LEGACY_CS_LEVEL_START_TIME = 13,
	LEGACY_CS_INTERMISSION = 14,
	LEGACY_CS_FLAGSTATUS = 15,
	LEGACY_PERS_SCORE = 0,
	LEGACY_PERS_HITS = 1,
	LEGACY_PERS_RANK = 2,
	LEGACY_PERS_TEAM = 3,
	LEGACY_PERS_SPAWN_COUNT = 4,
	LEGACY_PERS_REWARD_COUNT = 5,
	LEGACY_PERS_REWARD = 6,
	LEGACY_PERS_ATTACKER = 7,
	LEGACY_PERS_KILLED = 8,
	LEGACY_PERS_IMPRESSIVE_COUNT = 9,
	LEGACY_PERS_EXCELLENT_COUNT = 10,
	LEGACY_PERS_GAUNTLET_FRAG_COUNT = 11,
	LEGACY_REWARD_DENIED = 3
};

static int CL_TranslateLegacyConfigstringIndex( int index ) {
	const int legacyLocationBase = CS_PLAYERS + LEGACY_DM3_MAX_CLIENTS;

	if ( !CL_IsLegacyDemoMessage() || CL_LegacyDemoProtocol() >= 46 ) {
		return index;
	}

	switch ( index ) {
		case LEGACY_CS_GAME_VERSION:
			return CS_GAME_VERSION;
		case LEGACY_CS_LEVEL_START_TIME:
			return CS_LEVEL_START_TIME;
		case LEGACY_CS_INTERMISSION:
			return CS_INTERMISSION;
		case LEGACY_CS_FLAGSTATUS:
			return CS_FLAGSTATUS;
		default:
			break;
	}

	if ( index >= CS_PLAYERS + MAX_CLIENTS && index < legacyLocationBase ) {
		// DM3 demos reserve 128 player configstring slots; the baseq3 VM only has room for 64.
		return -1;
	}

	if ( index >= legacyLocationBase && index < legacyLocationBase + MAX_LOCATIONS ) {
		return CS_LOCATIONS + ( index - legacyLocationBase );
	}

	return index;
}

static int CL_TranslateLegacyEventNumber43( int event ) {
	static constexpr std::array legacyEventMap43 = {
		EV_NONE,
		EV_FOOTSTEP,
		EV_FOOTSTEP_METAL,
		EV_FOOTSPLASH,
		EV_FOOTWADE,
		EV_SWIM,
		EV_STEP_4,
		EV_STEP_8,
		EV_STEP_12,
		EV_STEP_16,
		EV_FALL_SHORT,
		EV_FALL_MEDIUM,
		EV_FALL_FAR,
		EV_JUMP_PAD,
		EV_JUMP,
		EV_WATER_TOUCH,
		EV_WATER_LEAVE,
		EV_WATER_UNDER,
		EV_WATER_CLEAR,
		EV_ITEM_PICKUP,
		EV_GLOBAL_ITEM_PICKUP,
		EV_NOAMMO,
		EV_CHANGE_WEAPON,
		EV_FIRE_WEAPON,
		EV_USE_ITEM0,
		EV_USE_ITEM1,
		EV_USE_ITEM2,
		EV_USE_ITEM3,
		EV_USE_ITEM4,
		EV_USE_ITEM5,
		EV_USE_ITEM6,
		EV_USE_ITEM7,
		EV_USE_ITEM8,
		EV_USE_ITEM9,
		EV_USE_ITEM10,
		EV_USE_ITEM11,
		EV_USE_ITEM12,
		EV_USE_ITEM13,
		EV_USE_ITEM14,
		EV_USE_ITEM15,
		EV_ITEM_RESPAWN,
		EV_ITEM_POP,
		EV_PLAYER_TELEPORT_IN,
		EV_PLAYER_TELEPORT_OUT,
		EV_GRENADE_BOUNCE,
		EV_GENERAL_SOUND,
		EV_GLOBAL_SOUND,
		EV_BULLET_HIT_FLESH,
		EV_BULLET_HIT_WALL,
		EV_MISSILE_HIT,
		EV_MISSILE_MISS,
		EV_RAILTRAIL,
		EV_SHOTGUN,
		EV_BULLET,
		EV_PAIN,
		EV_DEATH1,
		EV_DEATH2,
		EV_DEATH3,
		EV_OBITUARY,
		EV_POWERUP_QUAD,
		EV_POWERUP_BATTLESUIT,
		EV_POWERUP_REGEN,
		EV_GIB_PLAYER,
		EV_DEBUG_LINE,
		EV_TAUNT
	};

	if ( event < 0 || static_cast<std::size_t>( event ) >= legacyEventMap43.size() ) {
		return event;
	}

	return legacyEventMap43[event];
}

static int CL_TranslateLegacyEvent( int event ) {
	const int eventBits = event & EV_EVENT_BITS;
	int eventNum = event & ~EV_EVENT_BITS;

	if ( CL_LegacyDemoProtocol() < 46 ) {
		eventNum = CL_TranslateLegacyEventNumber43( eventNum );
	}

	return eventBits | eventNum;
}

static void CL_CanonicalizeLegacyEntityState( entityState_t *state ) {
	if ( !CL_IsLegacyDemoMessage() || CL_LegacyDemoProtocol() >= 46 ) {
		return;
	}

	state->event = CL_TranslateLegacyEvent( state->event );

	/*
	Protocol 43 event entities are encoded relative to the pre-ET_TEAM
	entity enum, so every freestanding event type sits one slot lower than
	the modern baseq3 VM expects.
	*/
	if ( state->eType >= LEGACY_DM3_ET_EVENTS ) {
		const int eventNum = CL_TranslateLegacyEventNumber43( state->eType - LEGACY_DM3_ET_EVENTS );
		state->eType = ET_EVENTS + eventNum;
	}
}

static void CL_CanonicalizeLegacyPlayerstate( const playerState_t *raw, const playerState_t *oldRaw, const playerState_t *oldCanonical, playerState_t *canonical ) {
	int i;

	*canonical = *raw;

	if ( !CL_IsLegacyDemoMessage() || CL_LegacyDemoProtocol() >= 46 ) {
		return;
	}

	canonical->externalEvent = CL_TranslateLegacyEvent( canonical->externalEvent );
	for ( i = 0; i < MAX_PS_EVENTS; i++ ) {
		canonical->events[i] = CL_TranslateLegacyEvent( canonical->events[i] );
	}

	std::fill_n( canonical->persistant, MAX_PERSISTANT, 0 );
	canonical->persistant[PERS_SCORE] = raw->persistant[LEGACY_PERS_SCORE];
	canonical->persistant[PERS_HITS] = raw->persistant[LEGACY_PERS_HITS];
	canonical->persistant[PERS_RANK] = raw->persistant[LEGACY_PERS_RANK];
	canonical->persistant[PERS_TEAM] = raw->persistant[LEGACY_PERS_TEAM];
	canonical->persistant[PERS_SPAWN_COUNT] = raw->persistant[LEGACY_PERS_SPAWN_COUNT];
	canonical->persistant[PERS_ATTACKER] = raw->persistant[LEGACY_PERS_ATTACKER];
	canonical->persistant[PERS_KILLED] = raw->persistant[LEGACY_PERS_KILLED];
	canonical->persistant[PERS_IMPRESSIVE_COUNT] = raw->persistant[LEGACY_PERS_IMPRESSIVE_COUNT];
	canonical->persistant[PERS_EXCELLENT_COUNT] = raw->persistant[LEGACY_PERS_EXCELLENT_COUNT];
	canonical->persistant[PERS_GAUNTLET_FRAG_COUNT] = raw->persistant[LEGACY_PERS_GAUNTLET_FRAG_COUNT];

	if ( oldCanonical ) {
		canonical->persistant[PERS_PLAYEREVENTS] = oldCanonical->persistant[PERS_PLAYEREVENTS];
	}

	if ( oldRaw && raw->persistant[LEGACY_PERS_REWARD_COUNT] != oldRaw->persistant[LEGACY_PERS_REWARD_COUNT] &&
		raw->persistant[LEGACY_PERS_REWARD] == LEGACY_REWARD_DENIED ) {
		canonical->persistant[PERS_PLAYEREVENTS] ^= PLAYEREVENT_DENIEDREWARD;
	}
}

static bool CL_AppendTranslatedToken( char *rewritten, int rewrittenSize, const char *token ) {
	size_t currentLen;
	size_t tokenLen;

	currentLen = strlen( rewritten );
	tokenLen = strlen( token );
	if ( currentLen + ( currentLen ? 1 : 0 ) + tokenLen + 1 > static_cast<size_t>( rewrittenSize ) ) {
		return false;
	}

	if ( currentLen ) {
		rewritten[currentLen++] = ' ';
		rewritten[currentLen] = '\0';
	}

	std::copy_n( token, tokenLen + 1, rewritten + currentLen );
	return true;
}

static bool CL_TranslateLegacyScoresCommand( const char *command, char *rewritten, int rewrittenSize ) {
	static constexpr std::array legacyScorePadding = { "0", "0", "0", "0", "0", "0", "0", "0" };
	int argc;
	int i;
	int numScores;

	// Protocol 43 demos send only the original six score columns per player.
	Cmd_TokenizeString( command );
	argc = Cmd_Argc();
	if ( argc < 4 || Q_stricmp( Cmd_Argv( 0 ), "scores" ) ) {
		Q_strncpyz( rewritten, command, rewrittenSize );
		return true;
	}

	numScores = std::atoi( Cmd_Argv( 1 ) );
	if ( numScores < 0 || argc != ( numScores * 6 + 4 ) ) {
		Q_strncpyz( rewritten, command, rewrittenSize );
		return true;
	}

	rewritten[0] = '\0';
	for ( i = 0; i < 4; i++ ) {
		if ( !CL_AppendTranslatedToken( rewritten, rewrittenSize, Cmd_Argv( i ) ) ) {
			Q_strncpyz( rewritten, command, rewrittenSize );
			return true;
		}
	}

	for ( i = 0; i < numScores; i++ ) {
		int scoreArg;

		for ( scoreArg = 0; scoreArg < 6; scoreArg++ ) {
			if ( !CL_AppendTranslatedToken( rewritten, rewrittenSize, Cmd_Argv( i * 6 + 4 + scoreArg ) ) ) {
				Q_strncpyz( rewritten, command, rewrittenSize );
				return true;
			}
		}

		for ( const char *padding : legacyScorePadding ) {
			if ( !CL_AppendTranslatedToken( rewritten, rewrittenSize, padding ) ) {
				Q_strncpyz( rewritten, command, rewrittenSize );
				return true;
			}
		}
	}

	return true;
}

static bool CL_TranslateLegacyConfigstringCommand( const char *command, char *rewritten, int rewrittenSize ) {
	const char *numberStart;
	char *numberEnd;
	int mappedIndex;
	long rawIndex;
	size_t prefixLen;

	if ( !CL_IsLegacyDemoMessage() || CL_LegacyDemoProtocol() >= 46 ) {
		Q_strncpyz( rewritten, command, rewrittenSize );
		return true;
	}

	if ( !strncmp( command, "cs ", 3 ) ) {
		prefixLen = 3;
	} else if ( !strncmp( command, "bcs0 ", 5 ) ) {
		prefixLen = 5;
	} else if ( !strncmp( command, "scores ", 7 ) ) {
		return CL_TranslateLegacyScoresCommand( command, rewritten, rewrittenSize );
	} else {
		Q_strncpyz( rewritten, command, rewrittenSize );
		return true;
	}

	numberStart = command + prefixLen;
	rawIndex = strtol( numberStart, &numberEnd, 10 );
	if ( numberEnd == numberStart || ( *numberEnd != '\0' && *numberEnd != ' ' ) ) {
		Q_strncpyz( rewritten, command, rewrittenSize );
		return true;
	}

	mappedIndex = CL_TranslateLegacyConfigstringIndex( static_cast<int>( rawIndex ) );
	if ( mappedIndex < 0 ) {
		rewritten[0] = '\0';
		return false;
	}

	Com_sprintf( rewritten, rewrittenSize, "%.*s%d%s", static_cast<int>( prefixLen ), command, mappedIndex, numberEnd );
	return true;
}

static void CL_ReadDeltaEntity( msg_t *msg, const entityState_t *from, entityState_t *to, int number ) {
	if ( CL_IsLegacyDemoMessage() ) {
		MSG_ReadDeltaEntityLegacy( msg, from, to, number, CL_LegacyDemoProtocol() );
		CL_CanonicalizeLegacyEntityState( to );
	} else {
		MSG_ReadDeltaEntity( msg, from, to, number );
	}
}

static void CL_ReadDeltaPlayerstate( msg_t *msg, const playerState_t *from, playerState_t *to ) {
	if ( CL_IsLegacyDemoMessage() ) {
		MSG_ReadDeltaPlayerstateLegacy( msg, from, to, CL_LegacyDemoProtocol() );
	} else {
		MSG_ReadDeltaPlayerstate( msg, from, to );
	}
}


/*
=========================================================================

MESSAGE PARSING

=========================================================================
*/

/*
==================
CL_DeltaEntity

Parses deltas from the given base and adds the resulting entity
to the current frame
==================
*/
static void CL_DeltaEntity( msg_t *msg, clSnapshot_t *frame, int newnum, const entityState_t *old, qboolean unchanged) {
	entityState_t	*state;

	// save the parsed entity state into the big circular buffer so
	// it can be used as the source for a later delta
	state = &cl.parseEntities[cl.parseEntitiesNum & (MAX_PARSE_ENTITIES-1)];

	if ( unchanged ) {
		*state = *old;
	} else {
		CL_ReadDeltaEntity( msg, old, state, newnum );
	}

	if ( state->number == (MAX_GENTITIES-1) ) {
		return;		// entity was delta removed
	}
	cl.parseEntitiesNum++;
	frame->numEntities++;
}


/*
==================
CL_ParsePacketEntities
==================
*/
static void CL_ParsePacketEntities( msg_t *msg, const clSnapshot_t *oldframe, clSnapshot_t *newframe ) {
	const entityState_t	*oldstate;
	int	newnum;
	int	oldindex, oldnum;

	newframe->parseEntitiesNum = cl.parseEntitiesNum;
	newframe->numEntities = 0;

	// delta from the entities present in oldframe
	oldindex = 0;
	oldstate = nullptr;
	if ( !oldframe ) {
		oldnum = MAX_GENTITIES+1;
	} else {
		if ( oldindex >= oldframe->numEntities ) {
			oldnum = MAX_GENTITIES+1;
		} else {
			oldstate = &cl.parseEntities[
				(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
			oldnum = oldstate->number;
		}
	}

	while ( 1 ) {
		// read the entity index number
		newnum = MSG_ReadEntitynum( msg );

		if ( newnum < 0 ) {
			Com_Error( ERR_DROP, "CL_ParsePacketEntities: end of message" );
		}

		if ( newnum == (MAX_GENTITIES-1) ) {
			break;
		}

		while ( oldnum < newnum ) {
			// one or more entities from the old packet are unchanged
			if ( cl_shownet->integer == 3 ) {
				Com_Printf ("%3i:  unchanged: %i\n", msg->readcount, oldnum);
			}
			CL_DeltaEntity( msg, newframe, oldnum, oldstate, qtrue );

			oldindex++;

			if ( oldindex >= oldframe->numEntities ) {
				oldnum = MAX_GENTITIES+1;
			} else {
				oldstate = &cl.parseEntities[
					(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
		}
		if (oldnum == newnum) {
			// delta from previous state
			if ( cl_shownet->integer == 3 ) {
				Com_Printf ("%3i:  delta: %i\n", msg->readcount, newnum);
			}
			CL_DeltaEntity( msg, newframe, newnum, oldstate, qfalse );

			oldindex++;

			if ( oldindex >= oldframe->numEntities ) {
				oldnum = MAX_GENTITIES+1;
			} else {
				oldstate = &cl.parseEntities[
					(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
				oldnum = oldstate->number;
			}
			continue;
		}

		if ( oldnum > newnum ) {
			// delta from baseline
			if ( cl_shownet->integer == 3 ) {
				Com_Printf ("%3i:  baseline: %i\n", msg->readcount, newnum);
			}
			CL_DeltaEntity( msg, newframe, newnum, &cl.entityBaselines[newnum], qfalse );
			continue;
		}

	}

	// any remaining entities in the old frame are copied over
	while ( oldnum != MAX_GENTITIES+1 ) {
		// one or more entities from the old packet are unchanged
		if ( cl_shownet->integer == 3 ) {
			Com_Printf ("%3i:  unchanged: %i\n", msg->readcount, oldnum);
		}
		CL_DeltaEntity( msg, newframe, oldnum, oldstate, qtrue );

		oldindex++;

		if ( oldindex >= oldframe->numEntities ) {
			oldnum = MAX_GENTITIES+1;
		} else {
			oldstate = &cl.parseEntities[
				(oldframe->parseEntitiesNum + oldindex) & (MAX_PARSE_ENTITIES-1)];
			oldnum = oldstate->number;
		}
	}
}


/*
================
CL_ParseSnapshot

If the snapshot is parsed properly, it will be copied to
cl.snap and saved in cl.snapshots[].  If the snapshot is invalid
for any reason, no changes to the state will be made at all.
================
*/
static void CL_ParseSnapshot( msg_t *msg ) {
	const clSnapshot_t *old;
	clSnapshot_t	newSnap{};
	int			deltaNum;
	int			i, n, packetNum;

	// read in the new snapshot to a temporary buffer
	// we will only copy to cl.snap if it is valid
	// we will have read any new server commands in this
	// message before we got to svc_snapshot
	newSnap.serverCommandNum = clc.serverCommandSequence;

	if ( CL_IsLegacyDemoMessage() && CL_LegacyDemoProtocol() < 46 ) {
		newSnap.cmdNum = MSG_ReadLong( msg );
	}

	newSnap.serverTime = MSG_ReadLong( msg );

	// if we were just unpaused, we can only *now* really let the
	// change come into effect or the client hangs.
	cl_paused->modified = qfalse;

	newSnap.messageNum = clc.serverMessageSequence;

	deltaNum = MSG_ReadByte( msg );
	if ( !deltaNum ) {
		newSnap.deltaNum = -1;
	} else {
		newSnap.deltaNum = fnql::net::RetreatSequence(
			newSnap.messageNum, static_cast<std::uint32_t>( deltaNum ) );
	}
	newSnap.snapFlags = MSG_ReadByte( msg );

	// If the frame is delta compressed from data that we
	// no longer have available, we must suck up the rest of
	// the frame, but not use it, then ask for a non-compressed
	// message
	if ( deltaNum == 0 ) {
		newSnap.valid = qtrue;		// uncompressed frame
		old = nullptr;
		clc.demowaiting = qfalse;	// we can start recording now
	} else {
		old = &cl.snapshots[newSnap.deltaNum & PACKET_MASK];
		if ( !old->valid ) {
			// should never happen
			Com_Printf ("Delta from invalid frame (not supposed to happen!).\n");
		} else if ( old->messageNum != newSnap.deltaNum ) {
			// The frame that the server did the delta from
			// is too old, so we can't reconstruct it properly.
			Com_Printf ("Delta frame too old.\n");
		} else if ( cl.parseEntitiesNum - old->parseEntitiesNum > MAX_PARSE_ENTITIES - MAX_SNAPSHOT_ENTITIES ) {
			Com_Printf ("Delta parseEntitiesNum too old.\n");
		} else {
			newSnap.valid = qtrue;	// valid delta parse
		}
	}

	// read areamask
	newSnap.areabytes = MSG_ReadByte( msg );

	if ( newSnap.areabytes > static_cast<int>( sizeof(newSnap.areamask) ) )
	{
		Com_Error( ERR_DROP,"CL_ParseSnapshot: Invalid size %d for areamask", newSnap.areabytes );
		return;
	}

	MSG_ReadData( msg, &newSnap.areamask, newSnap.areabytes );

	// read playerinfo
	SHOWNET( msg, "playerstate" );
	if ( CL_IsLegacyDemoMessage() ) {
		if ( old ) {
			CL_ReadDeltaPlayerstate( msg, &old->psRaw, &newSnap.psRaw );
			CL_CanonicalizeLegacyPlayerstate( &newSnap.psRaw, &old->psRaw, &old->ps, &newSnap.ps );
		} else {
			CL_ReadDeltaPlayerstate( msg, nullptr, &newSnap.psRaw );
			CL_CanonicalizeLegacyPlayerstate( &newSnap.psRaw, nullptr, nullptr, &newSnap.ps );
		}
	} else {
		if ( old ) {
			CL_ReadDeltaPlayerstate( msg, &old->ps, &newSnap.ps );
		} else {
			CL_ReadDeltaPlayerstate( msg, nullptr, &newSnap.ps );
		}
		newSnap.psRaw = newSnap.ps;
	}

	// read packet entities
	SHOWNET( msg, "packet entities" );
	CL_ParsePacketEntities( msg, old, &newSnap );

	// if not valid, dump the entire thing now that it has
	// been properly read
	if ( !newSnap.valid ) {
		return;
	}

	// clear the valid flags of any snapshots between the last
	// received and this one, so if there was a dropped packet
	// it won't look like something valid to delta from next
	// time we wrap around in the buffer
	const std::uint32_t messageGap = fnql::net::SequenceDistance(
		newSnap.messageNum, cl.snap.messageNum );
	n = static_cast<int>( std::min<std::uint32_t>(
		messageGap > 0 ? messageGap - 1u : 0u, PACKET_BACKUP - 1u ) );
	const int firstMissing = fnql::net::RetreatSequence(
		newSnap.messageNum, static_cast<std::uint32_t>( n ) );
	for ( i = 0; i < n; i++ ) {
		const int missing = fnql::net::AdvanceSequence( firstMissing,
			static_cast<std::uint32_t>( i ) );
		cl.snapshots[ missing & PACKET_MASK ].valid = qfalse;
	}

	// copy to the current good spot
	cl.snap = newSnap;
	cl.snap.ping = 999;
	// calculate ping time
	for ( i = 0 ; i < PACKET_BACKUP ; i++ ) {
		packetNum = fnql::net::RetreatSequence(
			clc.netchan.outgoingSequence, static_cast<std::uint32_t>( i + 1 ) ) & PACKET_MASK;
		if ( cl.snap.ps.commandTime - cl.outPackets[packetNum].p_serverTime >= 0 ) {
			cl.snap.ping = cls.realtime - cl.outPackets[ packetNum ].p_realtime;
			break;
		}
	}
	// save the frame off in the backup array for later delta comparisons
	cl.snapshots[cl.snap.messageNum & PACKET_MASK] = cl.snap;

	if (cl_shownet->integer == 3) {
		Com_Printf( "   snapshot:%i  delta:%i  ping:%i\n", cl.snap.messageNum,
		cl.snap.deltaNum, cl.snap.ping );
	}

	cl.newSnapshots = qtrue;

	clc.eventMask |= EM_SNAPSHOT;
}


//=====================================================================

int cl_connectedToPureServer;
int cl_connectedToCheatServer;

/*
==================
CL_SystemInfoChanged

The systeminfo configstring has been changed, so parse
new information out of it.  This will happen at every
gamestate, and possibly during gameplay.
==================
*/
void CL_SystemInfoChanged( qboolean onlyGame ) {
	const char		*systemInfo;
	const char		*s, *t;
	std::array<char, BIG_INFO_KEY> key;
	std::array<char, BIG_INFO_VALUE> value;

	systemInfo = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SYSTEMINFO ];
	// NOTE TTimo:
	// when the serverId changes, any further messages we send to the server will use this new serverId
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=475
	// in some cases, outdated cp commands might get sent with this news serverId
	cl.serverId = std::atoi( Info_ValueForKey( systemInfo, "sv_serverid" ) );

	// don't set any vars when playing a demo
	if ( clc.demoplaying ) {
		return;
	}

	s = Info_ValueForKey( systemInfo, "sv_pure" );
	cl_connectedToPureServer = std::atoi( s );

	// parse/update fs_game in first place
	s = Info_ValueForKey( systemInfo, "fs_game" );

	if ( FS_InvalidGameDir( s ) ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: Server sent invalid fs_game value %s\n", s );
	} else {
		Cvar_Set( "fs_game", s );
	}

	// if game folder should not be set and it is set at the client side
	if ( *s == '\0' && *Cvar_VariableString( "fs_game" ) != '\0' ) {
		Cvar_Set( "fs_game", "" );
	}

	if ( onlyGame && Cvar_Flags( "fs_game" ) & CVAR_MODIFIED ) {
		// game directory change is needed
		// return early to avoid systeminfo-cvar pollution in current fs_game
		return;
	}

	if ( CL_GameSwitch() ) {
		// we just restored fs_game from saved systeminfo
		// reset modified flag to avoid unwanted side-effecfs
		Cvar_SetModified( "fs_game", qfalse );
	}

	s = Info_ValueForKey( systemInfo, "sv_cheats" );
	cl_connectedToCheatServer = std::atoi( s );
	if ( !cl_connectedToCheatServer ) {
		Cvar_SetCheatState();
	}

	if ( com_sv_running->integer ) {
		// no filesystem restrictions for localhost
		FS_PureServerSetLoadedPaks( "", "" );
		FS_PureServerSetReferencedPaks( "", "" );
	} else {
		// check pure server string
		s = Info_ValueForKey( systemInfo, "sv_paks" );
		t = Info_ValueForKey( systemInfo, "sv_pakNames" );
		FS_PureServerSetLoadedPaks( s, t );

		s = Info_ValueForKey( systemInfo, "sv_referencedPaks" );
		t = Info_ValueForKey( systemInfo, "sv_referencedPakNames" );
		FS_PureServerSetReferencedPaks( s, t );
	}

	// scan through all the variables in the systeminfo and locally set cvars to match
	s = systemInfo;
	do {
		unsigned cvar_flags;

		s = Info_NextPair( s, key.data(), value.data() );
		if ( key[0] == '\0' ) {
			break;
		}

		// we don't really need any of these server cvars to be set on client-side
		if ( !Q_stricmp( key.data(), "sv_pure" ) || !Q_stricmp( key.data(), "sv_serverid" ) || !Q_stricmp( key.data(), "sv_fps" ) ) {
			continue;
		}
		if ( !Q_stricmp( key.data(), "sv_paks" ) || !Q_stricmp( key.data(), "sv_pakNames" ) ) {
			continue;
		}
		if ( !Q_stricmp( key.data(), "sv_referencedPaks" ) || !Q_stricmp( key.data(), "sv_referencedPakNames" ) ) {
			continue;
		}

		if ( !Q_stricmp( key.data(), "fs_game" ) ) {
			continue; // already processed
		}

		if ( ( cvar_flags = Cvar_Flags( key.data() ) ) == CVAR_NONEXISTENT )
			Cvar_Get( key.data(), value.data(), CVAR_SERVER_CREATED | CVAR_ROM );
		else
		{
			// If this cvar may not be modified by a server discard the value.
			if ( !(cvar_flags & ( CVAR_SYSTEMINFO | CVAR_SERVER_CREATED | CVAR_USER_CREATED ) ) )
			{
#ifndef STANDALONE
				if ( Q_stricmp( key.data(), "g_synchronousClients" ) && Q_stricmp( key.data(), "pmove_fixed" ) && Q_stricmp( key.data(), "pmove_msec" ) )
#endif
				{
					Com_Printf( S_COLOR_YELLOW "WARNING: server is not allowed to set %s=%s\n", key.data(), value.data() );
					continue;
				}
			}

			Cvar_SetSafe( key.data(), value.data() );
		}
	}
	while ( *s != '\0' );
}


/*
==================
CL_GameSwitch
==================
*/
qboolean CL_GameSwitch( void )
{
	return ToQboolean( cls.gameSwitch && !com_errorEntered );
}


/*
==================
CL_ParseServerInfo
==================
*/
static void CL_ParseServerInfo( void )
{
	const char *serverInfo;
	size_t	len;

	serverInfo = cl.gameState.stringData
		+ cl.gameState.stringOffsets[ CS_SERVERINFO ];

	clc.sv_allowDownload = std::atoi(Info_ValueForKey(serverInfo,
		"sv_allowDownload"));
	Q_strncpyz(clc.sv_dlURL,
		Info_ValueForKey(serverInfo, "sv_dlURL"),
		sizeof(clc.sv_dlURL));

	/* remove ending slash in URLs */
	len = strlen( clc.sv_dlURL );
	if ( len > 0 &&  clc.sv_dlURL[len-1] == '/' )
		clc.sv_dlURL[len-1] = '\0';
}


/*
==================
CL_ParseGamestate
==================
*/
static void CL_ParseGamestate( msg_t *msg ) {
	int				i;
	entityState_t	*es;
	int				newnum;
	entityState_t	nullstate{};
	int				cmd;
	const char		*s;
	std::array<char, MAX_QPATH> oldGame;
	std::array<char, MAX_CVAR_VALUE_STRING> reconnectArgs;
	bool			gamedirModified;

	Con_Close();

	clc.connectPacketCount = 0;

	// clear old error message
	Cvar_Set( "com_errorMessage", "" );

	// wipe local client state
	CL_ClearState();

	// all configstring updates received before new gamestate must be discarded
	for ( i = 0; i < MAX_RELIABLE_COMMANDS; i++ ) {
		s = clc.serverCommands[ i ];
		if ( !strncmp( s, "cs ", 3 ) || !strncmp( s, "bcs0 ", 5 ) || !strncmp( s, "bcs1 ", 5 ) || !strncmp( s, "bcs2 ", 5 ) ) {
			clc.serverCommandsIgnore[ i ] = qtrue;
		}
	}

	// a gamestate always marks a server command sequence
	clc.serverCommandSequence = MSG_ReadLong( msg );

	// parse all the configstrings and baselines
	cl.gameState.dataCount = 1;	// leave a 0 at the beginning for uninitialized configstrings
	while ( 1 ) {
		cmd = MSG_ReadByte( msg );

		if ( CL_IsLegacyDemoMessage() && cmd == svc_bad ) {
			break;
		}

		if ( cmd == svc_EOF ) {
			break;
		}

		if ( cmd == svc_configstring ) {
			int		len;
			int		rawIndex;

			rawIndex = MSG_ReadShort( msg );
			if ( rawIndex < 0 || rawIndex >= MAX_CONFIGSTRINGS ) {
				Com_Error( ERR_DROP, "%s: configstring > MAX_CONFIGSTRINGS", __func__ );
			}

			s = MSG_ReadBigString( msg );
			len = strlen( s );
			i = CL_TranslateLegacyConfigstringIndex( rawIndex );
			if ( i < 0 ) {
				continue;
			}

			if ( len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS ) {
				Com_Error( ERR_DROP, "%s: MAX_GAMESTATE_CHARS exceeded: %i", __func__,
					len + 1 + cl.gameState.dataCount );
			}

			// append it to the gameState string buffer
			cl.gameState.stringOffsets[ i ] = cl.gameState.dataCount;
			std::copy_n( s, len + 1, cl.gameState.stringData + cl.gameState.dataCount );
			cl.gameState.dataCount += len + 1;

			if ( CL_IsLegacyDemoMessage() && rawIndex == CS_SERVERINFO ) {
				const int protocol = std::atoi( Info_ValueForKey( s, "protocol" ) );
				if ( protocol >= 43 && protocol <= 48 ) {
					clc.demoLegacyProtocol = protocol;
				}
			}
		} else if ( cmd == svc_baseline ) {
			newnum = MSG_ReadEntitynum( msg );

			if ( newnum < 0 ) {
				Com_Error( ERR_DROP, "%s: end of message", __func__ );
			}

			if ( newnum >= MAX_GENTITIES ) {
				Com_Error( ERR_DROP, "%s: baseline number out of range: %i", __func__, newnum );
			}

			es = &cl.entityBaselines[ newnum ];
			CL_ReadDeltaEntity( msg, &nullstate, es, newnum );
			cl.baselineUsed[ newnum ] = 1;
		} else {
			Com_Error( ERR_DROP, "%s: bad command byte", __func__ );
		}
	}

	clc.eventMask |= EM_GAMESTATE;

	if ( !CL_IsLegacyDemoMessage() ) {
		clc.clientNum = MSG_ReadLong(msg);
		// read the checksum feed
		clc.checksumFeed = MSG_ReadLong( msg );
	}

	// save old gamedir
	Cvar_VariableStringBuffer( "fs_game", oldGame.data(), static_cast<int>( oldGame.size() ) );

	// parse useful values out of CS_SERVERINFO
	CL_ParseServerInfo();

	// parse serverId and other cvars
	CL_SystemInfoChanged( qtrue );

	// stop recording now so the demo won't have an unnecessary level load at the end.
	if ( cl_autoRecordDemo->integer && clc.demorecording ) {
		if ( !clc.demoplaying ) {
			CL_StopRecord_f();
		}
	}

	gamedirModified = ( Cvar_Flags( "fs_game" ) & CVAR_MODIFIED ) != 0;

	if ( !cl_oldGameSet && gamedirModified ) {
		cl_oldGameSet = qtrue;
		Q_strncpyz( cl_oldGame, oldGame.data(), sizeof( cl_oldGame ) );
	}

	// try to keep gamestate and connection state during game switch
	cls.gameSwitch = ToQboolean( gamedirModified );

	// preserve \cl_reconnectAgrs between online game directory changes
	// so after mod switch \reconnect will not restore old value from config but use new one
	if ( gamedirModified ) {
		Cvar_VariableStringBuffer( "cl_reconnectArgs", reconnectArgs.data(), static_cast<int>( reconnectArgs.size() ) );
	}

	// reinitialize the filesystem if the game directory has changed
	FS_ConditionalRestart( clc.checksumFeed, ToQboolean( gamedirModified ) );

	// restore \cl_reconnectAgrs
	if ( gamedirModified ) {
		Cvar_Set( "cl_reconnectArgs", reconnectArgs.data() );
	}

	cls.gameSwitch = qfalse;

	// This used to call CL_StartHunkUsers, but now we enter the download state before loading the cgame
	CL_InitDownloads();

	// make sure the game starts
	Cvar_Set( "cl_paused", "0" );
}


/*
=====================
CL_ValidPakSignature

checks for valid ZIP or PAK signatures
returns qtrue for normal and empty archives
=====================
*/
qboolean CL_ValidPakSignature( const byte *data, int len )
{
	// maybe it is not 100% correct to check for file size here
	// because we may receive more data in future packets
	// but situation when server sends fragmented/shortened
	// archive header in first packet - looks pretty suspicious
	if ( len >= 22 && data[0] == 'P' && data[1] == 'K' ) {
		if ( data[2] == 0x3 && data[3] == 0x4 )
			return qtrue; // local file header

		if ( data[2] == 0x5 && data[3] == 0x6 )
			return qtrue; // EOCD
	}

	if ( len >= 12 && data[0] == 'P' && data[1] == 'A' && data[2] == 'C' && data[3] == 'K' ) {
		return qtrue;
	}

	return qfalse;
}

//=====================================================================

/*
=====================
CL_ParseDownload

A download message has been received from the server
=====================
*/
static void CL_ParseDownload( msg_t *msg ) {
	int		size;
	std::array<byte, MAX_MSGLEN> data;
	uint16_t block;

	if (!*clc.downloadTempName) {
		Com_Printf("Server sending download, but no download was requested\n");
		CL_AddReliableCommand( "stopdl", qfalse );
		return;
	}

	if ( clc.recordfile != FS_INVALID_HANDLE ) {
		CL_StopRecord_f();
	}

	// read the data
	block = MSG_ReadShort ( msg );

	if(!block && !clc.downloadBlock)
	{
		// block zero is special, contains file size
		clc.downloadSize = MSG_ReadLong ( msg );

		Cvar_SetIntegerValue( "cl_downloadSize", clc.downloadSize );

		if (clc.downloadSize < 0)
		{
			Com_Error( ERR_DROP, "%s", MSG_ReadString( msg ) );
			return;
		}
	}

	size = MSG_ReadShort ( msg );
	if (size < 0 || size > static_cast<int>( data.size() ))
	{
		Com_Error(ERR_DROP, "CL_ParseDownload: Invalid size %d for download chunk", size);
		return;
	}

	MSG_ReadData(msg, data.data(), size);

	if((clc.downloadBlock & 0xFFFF) != block)
	{
		Com_DPrintf( "CL_ParseDownload: Expected block %d, got %d\n", (clc.downloadBlock & 0xFFFF), block);
		return;
	}

	// open the file if not opened yet
	if ( clc.download == FS_INVALID_HANDLE )
	{
		if ( !CL_ValidPakSignature( data.data(), size ) )
		{
			Com_Printf( S_COLOR_YELLOW "Invalid pak signature for %s\n", clc.downloadName );
			CL_AddReliableCommand( "stopdl", qfalse );
			CL_NextDownload();
			return;
		}

		clc.download = FS_SV_FOpenFileWrite( clc.downloadTempName );

		if ( clc.download == FS_INVALID_HANDLE )
		{
			Com_Printf( "Could not create %s\n", clc.downloadTempName );
			CL_AddReliableCommand( "stopdl", qfalse );
			CL_NextDownload();
			return;
		}
	}

	if (size)
		FileWrite( clc.download, data.data(), size );

	CL_AddReliableCommand( va("nextdl %d", clc.downloadBlock), qfalse );
	clc.downloadBlock++;

	clc.downloadCount += size;

	// So UI gets access to it
	Cvar_SetIntegerValue( "cl_downloadCount", clc.downloadCount );

	if ( size == 0 ) { // A zero length block means EOF
		if ( clc.download != FS_INVALID_HANDLE ) {
			CloseFile( clc.download );

			// rename the file
			FS_SV_Rename( clc.downloadTempName, clc.downloadName );
		}

		// send intentions now
		// We need this because without it, we would hold the last nextdl and then start
		// loading right away.  If we take a while to load, the server is happily trying
		// to send us that last block over and over.
		// Write it twice to help make sure we acknowledge the download
		CL_WritePacket( 1 );

		// get another file if needed
		CL_NextDownload();
	}
}


/*
=====================
CL_ParseCommandString

Command strings are just saved off until cgame asks for them
when it transitions a snapshot
=====================
*/
static void CL_ParseCommandString( msg_t *msg ) {
	const char *s;
	int		seq;
	int		index;
	std::array<char, MAX_STRING_CHARS> translated;
	bool	keepCommand;
	const char *storedCommand;

	seq = MSG_ReadLong( msg );
	s = MSG_ReadString( msg );
	storedCommand = s;
	keepCommand = CL_TranslateLegacyConfigstringCommand( s, translated.data(), static_cast<int>( translated.size() ) );
	if ( keepCommand ) {
		storedCommand = translated.data();
	}

	if ( cl_shownet->integer >= 3 )
		Com_Printf( " %3i(%3i) %s\n", seq, clc.serverCommandSequence, s );

	// see if we have already executed stored it off
	if ( clc.serverCommandSequence - seq >= 0 ) {
		return;
	}
	clc.serverCommandSequence = seq;

	index = seq & (MAX_RELIABLE_COMMANDS-1);
	if ( keepCommand ) {
		Q_strncpyz( clc.serverCommands[ index ], storedCommand, sizeof( clc.serverCommands[ index ] ) );
		clc.serverCommandsIgnore[ index ] = qfalse;
	} else {
		clc.serverCommands[ index ][0] = '\0';
		clc.serverCommandsIgnore[ index ] = qtrue;
	}

#ifdef USE_CURL
	if ( !clc.cURLUsed )
#endif
	// -EC- : we may stuck on downloading because of non-working cgvm
	// or in "awaiting snapshot..." state so handle "disconnect" here
	if ( ( !cgvm && cls.state == CA_CONNECTED && clc.download != FS_INVALID_HANDLE ) || ( cgvm && cls.state == CA_PRIMED ) ) {
		const char *text;
		Cmd_TokenizeString( storedCommand );
		if ( !Q_stricmp( Cmd_Argv(0), "disconnect" ) ) {
			text = ( Cmd_Argc() > 1 ) ? va( "Server disconnected: %s", Cmd_Argv( 1 ) ) : "Server disconnected.";
			Cvar_Set( "com_errorMessage", text );
			Com_Printf( "%s\n", text );
			if ( !CL_Disconnect( qtrue ) ) { // restart client if not done already
				CL_FlushMemory();
			}
			return;
		}
	}

	clc.eventMask |= EM_COMMAND;
}


/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage( msg_t *msg ) {
	int cmd;

	if ( cl_shownet->integer == 1 ) {
		Com_Printf( "%i ",msg->cursize );
	} else if ( cl_shownet->integer >= 2 ) {
		Com_Printf( "------------------\n" );
	}

	clc.eventMask = 0;
	if ( CL_IsLegacyDemoMessage() ) {
		MSG_RawBitstream( msg );
	} else {
		MSG_Bitstream( msg );
	}

	if ( !CL_IsLegacyDemoMessage() || CL_LegacyDemoProtocol() >= 46 ) {
		clc.reliableAcknowledge = MSG_ReadLong( msg );
		CL_ValidateReliableAcknowledge();
	}

	// parse the message
	while ( 1 ) {
		if ( msg->readcount > msg->cursize ) {
			Com_Error( ERR_DROP,"%s: read past end of server message", __func__ );
			break;
		}

		if ( CL_IsLegacyDemoMessage() && msg->readcount == msg->cursize ) {
			SHOWNET( msg, "END OF MESSAGE" );
			break;
		}

		cmd = MSG_ReadByte( msg );

		if ( cmd == svc_EOF ) {
			SHOWNET( msg, "END OF MESSAGE" );
			break;
		}

		if ( cl_shownet->integer >= 2 ) {
			if ( static_cast<std::size_t>( cmd ) >= svc_strings.size() ) {
				Com_Printf( "%3i:BAD CMD %i\n", msg->readcount-1, cmd );
			} else {
				SHOWNET( msg, svc_strings[cmd] );
			}
		}

		// other commands
		switch ( cmd ) {
		default:
			Com_Error( ERR_DROP,"%s: Illegible server message", __func__ );
			break;
		case svc_nop:
			break;
		case svc_serverCommand:
			CL_ParseCommandString( msg );
			break;
		case svc_gamestate:
			CL_ParseGamestate( msg );
			break;
		case svc_snapshot:
			CL_ParseSnapshot( msg );
			break;
		case svc_download:
			if ( clc.demofile != FS_INVALID_HANDLE )
				return;
			CL_ParseDownload( msg );
			break;
		case svc_voipSpeex: // ioq3 extension
			clc.dm68compat = qfalse;
#ifdef USE_VOIP
			CL_ParseVoip( msg, qtrue );
			break;
#else
			return;
#endif
		case svc_voipOpus: // ioq3 extension
			clc.dm68compat = qfalse;
#ifdef USE_VOIP
			CL_ParseVoip( msg, !clc.voipEnabled );
			break;
#else
			return;
#endif
		}

		if ( CL_IsLegacyDemoMessage() ) {
			CL_AlignLegacyDemoMessage( msg );
		}
	}
}
