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
// cl_cgame.cpp -- client system interaction with client game

extern "C" {
#include "client.h"

#include "../botlib/botlib.h"
}

#include "client_cpp.h"
#include "client_cvar_compat.hpp"
#include "ql_font_bridge.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>

using fnql::ToQboolean;

namespace {

static const char *SkipPath( const char *path )
{
	return COM_SkipPath( const_cast<char *>( path ) );
}

} // namespace

extern botlib_export_t	*botlib_export;

constexpr int kEnemyHighlightMaxModels = 1024;
constexpr int kEnemyHighlightMaxSkins = 1024;
constexpr int kEnemyHighlightMaxPending = 4096;
constexpr float kEnemyHighlightMatchRadius = 64.0f;

enum class EnemyHighlightColor {
	None,
	Red,
	Blue,
	Free,
	Count
};

constexpr int kEnemyHighlightColorCount = static_cast<int>( EnemyHighlightColor::Count );

static int EnemyHighlightColorIndex( EnemyHighlightColor color )
{
	return static_cast<int>( color );
}

using EnemyHighlightName = std::array<char, MAX_QPATH>;

static std::array<EnemyHighlightName, kEnemyHighlightMaxModels> cl_enemyHighlightModelNames;
static std::array<EnemyHighlightName, kEnemyHighlightMaxSkins> cl_enemyHighlightSkinNames;
static qhandle_t cl_enemyHighlightRimShader;
static qhandle_t cl_enemyHighlightOutlineShader;

struct clEnemyHighlightPending_t {
	refEntity_t ent;
	bool intShaderTime;
	EnemyHighlightColor colorSlot;
};

static std::array<clEnemyHighlightPending_t, kEnemyHighlightMaxPending> cl_enemyHighlightPending;
static int cl_enemyHighlightNumPending;

constexpr int kLiquidMaxEmissionsPerTime = 8;
constexpr int kLiquidMaxSampleGapMsec = 500;
constexpr int kLiquidPlayerWakeIntervalMsec = 140;
constexpr int kLiquidProjectileWakeIntervalMsec = 90;
constexpr float kLiquidPlayerMaxTrackedSpeed = 2400.0f;
constexpr float kLiquidProjectileMaxTrackedSpeed = 6000.0f;
constexpr float kLiquidPlayerWakeDistance = 8.0f;
constexpr float kLiquidProjectileWakeDistance = 5.0f;
constexpr float kLiquidPlayerWakeSurfaceRange = 64.0f;
constexpr float kLiquidProjectileWakeSurfaceRange = 32.0f;

struct clLiquidEntityKind_t {
	qboolean valid;
	liquidInteractionSource_t source;
	int eFlags;
};

struct clLiquidMotionTrack_t {
	qboolean valid;
	liquidInteractionSource_t source;
	vec3_t origin;
	int contents;
	int time;
	int eFlags;
	vec3_t wakeOrigin;
	int wakeTime;
};

static std::array<clLiquidEntityKind_t, MAX_GENTITIES> cl_liquidEntityKinds;
static std::array<clLiquidMotionTrack_t, MAX_GENTITIES> cl_liquidEntityTracks;
static clLiquidMotionTrack_t cl_liquidViewTrack;
static int cl_liquidKindSnapshot = -1;
static int cl_liquidServerCount = -1;
static int cl_liquidLocalPlayerSampleTime = INT_MIN;
static int cl_liquidEmissionTime = INT_MIN;
static int cl_liquidEmissionCount;
static int cl_liquidFeedCheckFrame = INT_MIN;
static qboolean cl_liquidFeedEnabled;
static qboolean cl_liquidFeedWasEnabled;


static qboolean CL_CaptureHidesHud( void ) {
	return ( cl_captureActive && cl_captureActive->integer &&
		r_levelshotHideHud && r_levelshotHideHud->integer ) ? qtrue : qfalse;
}


static qboolean CL_LiquidOriginValid( const vec3_t origin ) {
	return ( origin && std::isfinite( origin[0] ) && std::isfinite( origin[1] ) &&
		std::isfinite( origin[2] ) ) ? qtrue : qfalse;
}


static int CL_LiquidContents( const vec3_t origin ) {
	return CM_PointContents( origin, 0 ) & MASK_WATER;
}


static void CL_ResetLiquidMotionTracks( void ) {
	cl_liquidEntityTracks.fill( clLiquidMotionTrack_t{} );
	cl_liquidViewTrack = clLiquidMotionTrack_t{};
	cl_liquidLocalPlayerSampleTime = INT_MIN;
	cl_liquidEmissionTime = INT_MIN;
	cl_liquidEmissionCount = 0;
}


static void CL_ClearLiquidInteractionState( void ) {
	cl_liquidEntityKinds.fill( clLiquidEntityKind_t{} );
	CL_ResetLiquidMotionTracks();
	cl_liquidKindSnapshot = -1;
	cl_liquidServerCount = -1;
	cl_liquidFeedCheckFrame = INT_MIN;
	cl_liquidFeedEnabled = qfalse;
	cl_liquidFeedWasEnabled = qfalse;
}


static qboolean CL_LiquidInteractionFeedEnabled( void ) {
	qboolean enabled;

	// This is reached once per sound-position submission, which can be hundreds
	// of calls per rendered frame. Avoid repeating a named cvar lookup for every
	// entity while still noticing changes on the next rendered frame.
	if ( cl_liquidFeedCheckFrame != cls.framecount ) {
		cl_liquidFeedCheckFrame = cls.framecount;
		cl_liquidFeedEnabled = ( re.AddLiquidInteractionToScene &&
			Cvar_VariableIntegerValue( "r_liquid" ) > 0 &&
			Cvar_VariableValue( "r_liquidRipples" ) > 0.0f ) ? qtrue : qfalse;
	}
	enabled = cl_liquidFeedEnabled;

	if ( enabled != cl_liquidFeedWasEnabled ) {
		CL_ResetLiquidMotionTracks();
		cl_liquidFeedWasEnabled = enabled;
	}

	return enabled;
}


static void CL_RebuildLiquidEntityKinds( void ) {
	int serverCount;
	int i;

	if ( !cl.snap.valid ) {
		cl_liquidEntityKinds.fill( clLiquidEntityKind_t{} );
		CL_ResetLiquidMotionTracks();
		cl_liquidKindSnapshot = -1;
		cl_liquidServerCount = -1;
		return;
	}

	if ( cl_liquidKindSnapshot == cl.snap.messageNum ) {
		return;
	}

	serverCount = cl.snap.snapFlags & SNAPFLAG_SERVERCOUNT;
	if ( ( cl_liquidServerCount != -1 && serverCount != cl_liquidServerCount ) ||
		( cl_liquidKindSnapshot != -1 && cl.snap.messageNum < cl_liquidKindSnapshot ) ) {
		CL_ResetLiquidMotionTracks();
	}

	cl_liquidServerCount = serverCount;
	cl_liquidKindSnapshot = cl.snap.messageNum;
	cl_liquidEntityKinds.fill( clLiquidEntityKind_t{} );

	if ( cl.snap.ps.clientNum >= 0 && cl.snap.ps.clientNum < MAX_GENTITIES ) {
		clLiquidEntityKind_t &kind = cl_liquidEntityKinds[cl.snap.ps.clientNum];
		kind.valid = qtrue;
		kind.source = LIQUID_INTERACTION_PLAYER;
		kind.eFlags = cl.snap.ps.eFlags;
	}

	for ( i = 0; i < cl.snap.numEntities; i++ ) {
		const entityState_t *state = &cl.parseEntities[
			( cl.snap.parseEntitiesNum + i ) & ( MAX_PARSE_ENTITIES - 1 ) ];
		clLiquidEntityKind_t *kind;

		if ( state->number < 0 || state->number >= MAX_GENTITIES ) {
			continue;
		}
		if ( state->eType != ET_PLAYER && state->eType != ET_MISSILE ) {
			continue;
		}

		kind = &cl_liquidEntityKinds[state->number];
		kind->valid = qtrue;
		kind->source = state->eType == ET_PLAYER ?
			LIQUID_INTERACTION_PLAYER : LIQUID_INTERACTION_PROJECTILE;
		kind->eFlags = state->eFlags;
	}

	for ( i = 0; i < MAX_GENTITIES; i++ ) {
		if ( !cl_liquidEntityKinds[i].valid ) {
			cl_liquidEntityTracks[i].valid = qfalse;
		}
	}
}


static qboolean CL_TraceLiquidHit( const vec3_t start, const vec3_t end, vec3_t hit ) {
	trace_t trace;

	Com_Memset( &trace, 0, sizeof( trace ) );
	CM_BoxTrace( &trace, start, end, nullptr, nullptr, 0, MASK_WATER, qfalse );
	if ( trace.startsolid || trace.allsolid || trace.fraction < 0.0f || trace.fraction >= 1.0f ||
		!( trace.contents & MASK_WATER ) || !CL_LiquidOriginValid( trace.endpos ) ) {
		return qfalse;
	}

	VectorCopy( trace.endpos, hit );
	return qtrue;
}


static qboolean CL_FindLiquidBoundary( const vec3_t outside, const vec3_t inside, vec3_t boundary ) {
	vec3_t dry;
	vec3_t wet;
	vec3_t midpoint;
	int i;

	if ( CL_LiquidContents( outside ) || !CL_LiquidContents( inside ) ) {
		return qfalse;
	}
	if ( CL_TraceLiquidHit( outside, inside, boundary ) ) {
		return qtrue;
	}

	VectorCopy( outside, dry );
	VectorCopy( inside, wet );
	for ( i = 0; i < 8; i++ ) {
		VectorAdd( dry, wet, midpoint );
		VectorScale( midpoint, 0.5f, midpoint );
		if ( CL_LiquidContents( midpoint ) ) {
			VectorCopy( midpoint, wet );
		} else {
			VectorCopy( midpoint, dry );
		}
	}

	VectorCopy( wet, boundary );
	return qtrue;
}


static qboolean CL_FindLiquidSurfaceAbove( const vec3_t origin, float maxRise, vec3_t surface ) {
	vec3_t outside;

	if ( !CL_LiquidContents( origin ) ) {
		return qfalse;
	}

	VectorCopy( origin, outside );
	outside[2] += maxRise;
	if ( CL_LiquidContents( outside ) ) {
		return qfalse;
	}

	return CL_FindLiquidBoundary( outside, origin, surface );
}


static void CL_EmitLiquidInteraction( const vec3_t origin, float radius, float strength,
	int time, liquidInteractionSource_t source ) {
	liquidInteraction_t interaction{};

	if ( !re.AddLiquidInteractionToScene || !CL_LiquidOriginValid( origin ) ) {
		return;
	}
	if ( cl_liquidEmissionTime != time ) {
		cl_liquidEmissionTime = time;
		cl_liquidEmissionCount = 0;
	}
	if ( cl_liquidEmissionCount >= kLiquidMaxEmissionsPerTime ) {
		return;
	}

	VectorCopy( origin, interaction.origin );
	interaction.radius = std::clamp( radius, 4.0f, 96.0f );
	interaction.strength = std::clamp( strength, 0.05f, 2.0f );
	interaction.time = time;
	interaction.source = source;
	re.AddLiquidInteractionToScene( &interaction );
	cl_liquidEmissionCount++;
}


static void CL_SeedLiquidMotionTrack( clLiquidMotionTrack_t *track,
	liquidInteractionSource_t source, const vec3_t origin, int time, int eFlags ) {
	track->valid = qtrue;
	track->source = source;
	VectorCopy( origin, track->origin );
	track->contents = CL_LiquidContents( origin );
	track->time = time;
	track->eFlags = eFlags;
	VectorCopy( origin, track->wakeOrigin );
	track->wakeTime = time;
}


static void CL_UpdateLiquidMotionTrack( clLiquidMotionTrack_t *track,
	liquidInteractionSource_t source, const vec3_t origin, int time, int eFlags ) {
	vec3_t delta;
	vec3_t boundary;
	float distance;
	float speed;
	float speedScale;
	float maxSpeed;
	float crossingRadius;
	float crossingStrength;
	int newContents;
	int64_t elapsed;
	qboolean oldWet;
	qboolean newWet;

	if ( !track->valid || track->source != source || time < track->time ) {
		CL_SeedLiquidMotionTrack( track, source, origin, time, eFlags );
		return;
	}
	// Stereo and recursive screen updates can submit the same cgame frame more
	// than once. Treat an equal-time sample as idempotent so it cannot reset the
	// accumulated wake interval or manufacture zero-time motion.
	if ( time == track->time ) {
		return;
	}

	elapsed = (int64_t)time - (int64_t)track->time;
	VectorSubtract( origin, track->origin, delta );
	distance = VectorLength( delta );
	speed = distance * 1000.0f / (float)elapsed;
	maxSpeed = source == LIQUID_INTERACTION_PLAYER ?
		kLiquidPlayerMaxTrackedSpeed : kLiquidProjectileMaxTrackedSpeed;

	if ( elapsed > kLiquidMaxSampleGapMsec || !std::isfinite( speed ) || speed > maxSpeed ||
		( ( track->eFlags ^ eFlags ) & EF_TELEPORT_BIT ) ) {
		CL_SeedLiquidMotionTrack( track, source, origin, time, eFlags );
		return;
	}

	newContents = CL_LiquidContents( origin );
	oldWet = track->contents ? qtrue : qfalse;
	newWet = newContents ? qtrue : qfalse;
	speedScale = std::clamp( speed / ( source == LIQUID_INTERACTION_PLAYER ? 500.0f : 1400.0f ), 0.0f, 1.0f );
	if ( source == LIQUID_INTERACTION_PLAYER ) {
		crossingRadius = 36.0f + 20.0f * speedScale;
		crossingStrength = 0.55f + 0.55f * speedScale;
	} else {
		crossingRadius = 16.0f + 16.0f * speedScale;
		crossingStrength = 0.35f + 0.65f * speedScale;
	}

	if ( !oldWet && newWet ) {
		if ( CL_FindLiquidBoundary( track->origin, origin, boundary ) ) {
			CL_EmitLiquidInteraction( boundary, crossingRadius, crossingStrength, time, source );
		}
		VectorCopy( origin, track->wakeOrigin );
		track->wakeTime = time;
	} else if ( oldWet && !newWet ) {
		if ( CL_FindLiquidBoundary( origin, track->origin, boundary ) ) {
			CL_EmitLiquidInteraction( boundary, crossingRadius, crossingStrength, time, source );
		}
		VectorCopy( origin, track->wakeOrigin );
		track->wakeTime = time;
	} else if ( !oldWet && !newWet && source == LIQUID_INTERACTION_PROJECTILE && distance >= 8.0f ) {
		vec3_t exitBoundary;
		qboolean entered;
		qboolean exited;

		entered = CL_TraceLiquidHit( track->origin, origin, boundary );
		exited = CL_TraceLiquidHit( origin, track->origin, exitBoundary );
		if ( entered ) {
			CL_EmitLiquidInteraction( boundary, crossingRadius, crossingStrength, time, source );
		}
		if ( exited && ( !entered || DistanceSquared( boundary, exitBoundary ) > 4.0f ) ) {
			CL_EmitLiquidInteraction( exitBoundary, crossingRadius, crossingStrength, time, source );
		}
	} else if ( oldWet && newWet ) {
		const int wakeInterval = source == LIQUID_INTERACTION_PLAYER ?
			kLiquidPlayerWakeIntervalMsec : kLiquidProjectileWakeIntervalMsec;
		const float wakeDistance = source == LIQUID_INTERACTION_PLAYER ?
			kLiquidPlayerWakeDistance : kLiquidProjectileWakeDistance;
		const float surfaceRange = source == LIQUID_INTERACTION_PLAYER ?
			kLiquidPlayerWakeSurfaceRange : kLiquidProjectileWakeSurfaceRange;

		if ( (int64_t)time - (int64_t)track->wakeTime >= wakeInterval &&
			DistanceSquared( origin, track->wakeOrigin ) >= wakeDistance * wakeDistance ) {
			if ( CL_FindLiquidSurfaceAbove( origin, surfaceRange, boundary ) ) {
				const float wakeRadius = source == LIQUID_INTERACTION_PLAYER ?
					20.0f + 12.0f * speedScale : 10.0f + 10.0f * speedScale;
				const float wakeStrength = source == LIQUID_INTERACTION_PLAYER ?
					0.18f + 0.37f * speedScale : 0.12f + 0.28f * speedScale;

				CL_EmitLiquidInteraction( boundary, wakeRadius, wakeStrength, time, source );
			}
			VectorCopy( origin, track->wakeOrigin );
			track->wakeTime = time;
		}
	} else {
		VectorCopy( origin, track->wakeOrigin );
		track->wakeTime = time;
	}

	VectorCopy( origin, track->origin );
	track->contents = newContents;
	track->time = time;
	track->eFlags = eFlags;
}


static void CL_UpdateLiquidEntityPosition( int entityNum, const vec3_t origin ) {
	const clLiquidEntityKind_t *kind;

	if ( !CL_LiquidInteractionFeedEnabled() || !CL_LiquidOriginValid( origin ) ||
		entityNum < 0 || entityNum >= MAX_GENTITIES ) {
		return;
	}

	CL_RebuildLiquidEntityKinds();
	kind = &cl_liquidEntityKinds[entityNum];
	if ( !kind->valid ) {
		cl_liquidEntityTracks[entityNum].valid = qfalse;
		return;
	}

	CL_UpdateLiquidMotionTrack( &cl_liquidEntityTracks[entityNum], kind->source,
		origin, cl.serverTime, kind->eFlags );
	if ( kind->source == LIQUID_INTERACTION_PLAYER && entityNum == cl.snap.ps.clientNum ) {
		cl_liquidLocalPlayerSampleTime = cl.serverTime;
	}
}


static void CL_UpdateLocalViewLiquidInteraction( const refdef_t *refdef ) {
	if ( !CL_LiquidInteractionFeedEnabled() ) {
		cl_liquidViewTrack.valid = qfalse;
		return;
	}
	// Cgame commonly submits a HUD/model scene after the world view. It must not
	// erase the previous world-camera sample used by this fallback.
	if ( !refdef || ( refdef->rdflags & RDF_NOWORLDMODEL ) ) {
		return;
	}
	if ( ( refdef->rdflags & RDF_HYPERSPACE ) || !CL_LiquidOriginValid( refdef->vieworg ) ) {
		cl_liquidViewTrack.valid = qfalse;
		return;
	}

	if ( cl_liquidLocalPlayerSampleTime != INT_MIN &&
		refdef->time >= cl_liquidLocalPlayerSampleTime &&
		(int64_t)refdef->time - (int64_t)cl_liquidLocalPlayerSampleTime <= 100 ) {
		CL_SeedLiquidMotionTrack( &cl_liquidViewTrack, LIQUID_INTERACTION_PLAYER,
			refdef->vieworg, refdef->time, 0 );
		return;
	}

	CL_UpdateLiquidMotionTrack( &cl_liquidViewTrack, LIQUID_INTERACTION_PLAYER,
		refdef->vieworg, refdef->time, 0 );
}

//extern qboolean loadCamera(const char *name);
//extern void startCamera(int time);
//extern qboolean getCameraInfo(int time, vec3_t *origin, vec3_t *angles);

/*
====================
CL_GetGameState
====================
*/
static void CL_GetGameState( gameState_t *gs ) {
	*gs = cl.gameState;
}


/*
====================
CL_GetGlconfig
====================
*/
static void CL_GetGlconfig( glconfig_t *glconfig ) {
	*glconfig = cls.glconfig;
}


typedef struct {
	char					renderer_string[MAX_STRING_CHARS];
	char					vendor_string[MAX_STRING_CHARS];
	char					version_string[MAX_STRING_CHARS];
	char					extensions_string[BIG_INFO_STRING];

	int						maxTextureSize;
	int						maxActiveTextures;

	int						colorBits, depthBits, stencilBits;

	glDriverType_t			driverType;
	glHardwareType_t		hardwareType;

	qboolean				deviceSupportsGamma;
	textureCompression_t	textureCompression;
	qboolean				textureEnvAddAvailable;
	qboolean				multitextureAvailable;

	int						vidWidth, vidHeight;
	float					windowAspect;

	int						displayFrequency;

	qboolean				isFullscreen;
	qboolean				stereoEnabled;
} qlRetailGlconfig_t;

typedef char qlRetailGlconfigSizeCheck[( sizeof( qlRetailGlconfig_t ) == 0x2c44 ) ? 1 : -1 ];

/*
====================
CL_CopyRetailGlconfig

Retail Quake Live exposes multitextureAvailable before vidWidth and omits the
engine-local smpActive tail retained by the shared renderer glconfig_t.
====================
*/
void CL_CopyRetailGlconfig( void *glconfig ) {
	qlRetailGlconfig_t retailConfig;

	if ( !glconfig ) {
		return;
	}

	Com_Memset( &retailConfig, 0, sizeof( retailConfig ) );
	Q_strncpyz( retailConfig.renderer_string, cls.glconfig.renderer_string, sizeof( retailConfig.renderer_string ) );
	Q_strncpyz( retailConfig.vendor_string, cls.glconfig.vendor_string, sizeof( retailConfig.vendor_string ) );
	Q_strncpyz( retailConfig.version_string, cls.glconfig.version_string, sizeof( retailConfig.version_string ) );
	Q_strncpyz( retailConfig.extensions_string, cls.glconfig.extensions_string, sizeof( retailConfig.extensions_string ) );
	retailConfig.maxTextureSize = cls.glconfig.maxTextureSize;
	retailConfig.maxActiveTextures = cls.glconfig.numTextureUnits;
	retailConfig.colorBits = cls.glconfig.colorBits;
	retailConfig.depthBits = cls.glconfig.depthBits;
	retailConfig.stencilBits = cls.glconfig.stencilBits;
	retailConfig.driverType = cls.glconfig.driverType;
	retailConfig.hardwareType = cls.glconfig.hardwareType;
	retailConfig.deviceSupportsGamma = cls.glconfig.deviceSupportsGamma;
	retailConfig.textureCompression = cls.glconfig.textureCompression;
	retailConfig.textureEnvAddAvailable = cls.glconfig.textureEnvAddAvailable;
	retailConfig.multitextureAvailable = ( cls.glconfig.numTextureUnits > 1 ) ? qtrue : qfalse;
	retailConfig.vidWidth = cls.glconfig.vidWidth;
	retailConfig.vidHeight = cls.glconfig.vidHeight;
	retailConfig.windowAspect = cls.glconfig.windowAspect;
	retailConfig.displayFrequency = cls.glconfig.displayFrequency;
	retailConfig.isFullscreen = cls.glconfig.isFullscreen;
	retailConfig.stereoEnabled = cls.glconfig.stereoEnabled;

	Com_Memcpy( glconfig, &retailConfig, sizeof( retailConfig ) );
}


static void CL_ClearEnemyHighlightState( void ) {
	cl_enemyHighlightModelNames.fill( EnemyHighlightName{} );
	cl_enemyHighlightSkinNames.fill( EnemyHighlightName{} );
	cl_enemyHighlightRimShader = 0;
	cl_enemyHighlightOutlineShader = 0;
	cl_enemyHighlightNumPending = 0;
}


static void CL_RecordEnemyHighlightModel( qhandle_t handle, const char *name ) {
	if ( handle <= 0 || handle >= kEnemyHighlightMaxModels || !name ) {
		return;
	}

	Q_strncpyz( cl_enemyHighlightModelNames[handle].data(), name, cl_enemyHighlightModelNames[handle].size() );
}


static void CL_RecordEnemyHighlightSkin( qhandle_t handle, const char *name ) {
	if ( handle <= 0 || handle >= kEnemyHighlightMaxSkins || !name ) {
		return;
	}

	Q_strncpyz( cl_enemyHighlightSkinNames[handle].data(), name, cl_enemyHighlightSkinNames[handle].size() );
}


static const char *CL_GetEnemyHighlightModelName( qhandle_t handle ) {
	if ( handle <= 0 || handle >= kEnemyHighlightMaxModels || !cl_enemyHighlightModelNames[handle][0] ) {
		return nullptr;
	}

	return cl_enemyHighlightModelNames[handle].data();
}


static const char *CL_GetEnemyHighlightSkinName( qhandle_t handle ) {
	if ( handle <= 0 || handle >= kEnemyHighlightMaxSkins || !cl_enemyHighlightSkinNames[handle][0] ) {
		return nullptr;
	}

	return cl_enemyHighlightSkinNames[handle].data();
}


static bool CL_IsEnemyHighlightPlayerModel( const char *name ) {
	const char *base;

	if ( !name || Q_stricmpn( name, "models/players/", 15 ) ) {
		return false;
	}

	base = SkipPath( name );

	if ( !Q_stricmp( base, "lower.md3" ) || !Q_stricmp( base, "upper.md3" ) || !Q_stricmp( base, "head.md3" ) ) {
		return true;
	}

	if ( !Q_stricmp( base, "lower.iqm" ) || !Q_stricmp( base, "upper.iqm" ) || !Q_stricmp( base, "head.iqm" ) ) {
		return true;
	}

	return false;
}


static bool CL_IsEnemyHighlightPlayerSkin( const char *name ) {
	const char *base;

	if ( !name || Q_stricmpn( name, "models/players/", 15 ) ) {
		return false;
	}

	base = SkipPath( name );
	return !Q_stricmpn( base, "lower_", 6 ) || !Q_stricmpn( base, "upper_", 6 ) || !Q_stricmpn( base, "head_", 5 );
}


static bool CL_IsEnemyHighlightRedSkin( const char *name ) {
	return CL_IsEnemyHighlightPlayerSkin( name ) && strstr( name, "_red.skin" ) != nullptr;
}


static bool CL_IsEnemyHighlightBlueSkin( const char *name ) {
	return CL_IsEnemyHighlightPlayerSkin( name ) && strstr( name, "_blue.skin" ) != nullptr;
}


static void CL_GetEnemyHighlightMatchOrigin( const refEntity_t *ent, vec3_t outOrigin ) {
	if ( ( ent->renderfx & RF_LIGHTING_ORIGIN ) != 0 ) {
		VectorCopy( ent->lightingOrigin, outOrigin );
		return;
	}

	VectorCopy( ent->origin, outOrigin );
}


static const entityState_t *CL_FindEnemyHighlightPlayerState( const refEntity_t *ent ) {
	const entityState_t *best = nullptr;
	vec3_t matchOrigin;
	float bestDistSq = Square( kEnemyHighlightMatchRadius );
	int i;

	if ( !cl.snap.valid ) {
		return nullptr;
	}

	CL_GetEnemyHighlightMatchOrigin( ent, matchOrigin );

	for ( i = 0; i < cl.snap.numEntities; i++ ) {
		const entityState_t *state = &cl.parseEntities[ ( cl.snap.parseEntitiesNum + i ) & ( MAX_PARSE_ENTITIES - 1 ) ];
		float distSq;

		if ( state->eType != ET_PLAYER ) {
			continue;
		}

		distSq = DistanceSquared( matchOrigin, state->origin );
		if ( distSq > bestDistSq ) {
			continue;
		}

		bestDistSq = distSq;
		best = state;
	}

	return best;
}


static const cvar_t *CL_GetEnemyHighlightColorCvar( EnemyHighlightColor colorSlot ) {
	switch ( colorSlot ) {
		case EnemyHighlightColor::Red:
			return cl_playerHighlightRedColor;
		case EnemyHighlightColor::Blue:
			return cl_playerHighlightBlueColor;
		case EnemyHighlightColor::Free:
			return cl_playerHighlightFreeColor;
		default:
			return nullptr;
	}
}


static const cvar_t *CL_GetEnemyHighlightOverrideColorCvar( EnemyHighlightColor colorSlot ) {
	int localTeam;

	if ( colorSlot == EnemyHighlightColor::Free ) {
		return ( cl_playerHighlightEnemyColor && cl_playerHighlightEnemyColor->string[0] ) ? cl_playerHighlightEnemyColor : nullptr;
	}

	localTeam = cl.snap.ps.persistant[PERS_TEAM];
	if ( localTeam == TEAM_RED ) {
		if ( colorSlot == EnemyHighlightColor::Red ) {
			return ( cl_playerHighlightTeammateColor && cl_playerHighlightTeammateColor->string[0] ) ? cl_playerHighlightTeammateColor : nullptr;
		}

		if ( colorSlot == EnemyHighlightColor::Blue ) {
			return ( cl_playerHighlightEnemyColor && cl_playerHighlightEnemyColor->string[0] ) ? cl_playerHighlightEnemyColor : nullptr;
		}
	}

	if ( localTeam == TEAM_BLUE ) {
		if ( colorSlot == EnemyHighlightColor::Blue ) {
			return ( cl_playerHighlightTeammateColor && cl_playerHighlightTeammateColor->string[0] ) ? cl_playerHighlightTeammateColor : nullptr;
		}

		if ( colorSlot == EnemyHighlightColor::Red ) {
			return ( cl_playerHighlightEnemyColor && cl_playerHighlightEnemyColor->string[0] ) ? cl_playerHighlightEnemyColor : nullptr;
		}
	}

	return nullptr;
}


static byte CL_ParseEnemyHighlightColorComponent( const char *part ) {
	return static_cast<byte>( std::clamp( Q_atof( part ), 0.0f, 255.0f ) + 0.5f );
}


static void CL_ParseEnemyHighlightColorString( const char *string, const color4ub_t *defaultColor, color4ub_t *outColor ) {
	std::array<char, MAX_CVAR_VALUE_STRING> buffer;
	char *parts[4];
	int i;
	int count;

	*outColor = *defaultColor;

	if ( !string || !string[0] ) {
		return;
	}

	Q_strncpyz( buffer.data(), string, static_cast<int>( buffer.size() ) );
	count = Com_Split( buffer.data(), parts, 4, ' ' );
	if ( count < 3 ) {
		return;
	}

	for ( i = 0; i < 3; i++ ) {
		outColor->rgba[i] = CL_ParseEnemyHighlightColorComponent( parts[i] );
	}

	if ( count >= 4 ) {
		outColor->rgba[3] = CL_ParseEnemyHighlightColorComponent( parts[3] );
	}
}


static byte CL_ScaleEnemyHighlightAlpha( byte alpha, const cvar_t *intensityCvar ) {
	const float intensity = intensityCvar ? std::clamp( intensityCvar->value, 0.0f, 2.0f ) : 1.0f;
	const int value = static_cast<int>( static_cast<float>( alpha ) * intensity + 0.5f );

	return static_cast<byte>( std::clamp( value, 0, 255 ) );
}


static void CL_GetEnemyHighlightPassColor( EnemyHighlightColor colorSlot, byte defaultAlpha, const cvar_t *intensityCvar, color4ub_t *outColor ) {
	color4ub_t defaultColor;
	const cvar_t *cvar;

	switch ( colorSlot ) {
		case EnemyHighlightColor::Red:
			defaultColor.rgba[0] = 208;
			defaultColor.rgba[1] = 96;
			defaultColor.rgba[2] = 96;
			break;
		case EnemyHighlightColor::Blue:
			defaultColor.rgba[0] = 96;
			defaultColor.rgba[1] = 144;
			defaultColor.rgba[2] = 224;
			break;
		case EnemyHighlightColor::Free:
			defaultColor.rgba[0] = 208;
			defaultColor.rgba[1] = 96;
			defaultColor.rgba[2] = 96;
			break;
		default:
			defaultColor.rgba[0] = 0;
			defaultColor.rgba[1] = 0;
			defaultColor.rgba[2] = 0;
			break;
	}

	defaultColor.rgba[3] = defaultAlpha;
	cvar = CL_GetEnemyHighlightOverrideColorCvar( colorSlot );
	if ( !cvar ) {
		cvar = CL_GetEnemyHighlightColorCvar( colorSlot );
	}
	CL_ParseEnemyHighlightColorString( cvar ? cvar->string : nullptr, &defaultColor, outColor );
	outColor->rgba[3] = CL_ScaleEnemyHighlightAlpha( outColor->rgba[3], intensityCvar );
}


static EnemyHighlightColor CL_GetEnemyHighlightColorSlot( const refEntity_t *ent ) {
	const entityState_t *state;
	const char *info;
	const char *modelName;
	const char *skinName;
	int gametype;
	vec3_t delta;
	vec3_t matchOrigin;

	if ( !cl_playerHighlight || !cl_playerHighlight->integer ) {
		return EnemyHighlightColor::None;
	}

	if ( !cl.snap.valid ) {
		return EnemyHighlightColor::None;
	}

	if ( ent->reType != RT_MODEL || ent->customShader != 0 ||
		( ent->renderfx & ( RF_THIRD_PERSON | RF_FIRST_PERSON | RF_DEPTHHACK | RF_CROSSHAIR ) ) != 0 ) {
		return EnemyHighlightColor::None;
	}

	modelName = CL_GetEnemyHighlightModelName( ent->hModel );
	skinName = CL_GetEnemyHighlightSkinName( ent->customSkin );

	if ( !CL_IsEnemyHighlightPlayerModel( modelName ) && !CL_IsEnemyHighlightPlayerSkin( skinName ) ) {
		return EnemyHighlightColor::None;
	}

	state = CL_FindEnemyHighlightPlayerState( ent );
	CL_GetEnemyHighlightMatchOrigin( ent, matchOrigin );
	VectorSubtract( matchOrigin, cl.snap.ps.origin, delta );
	if ( state ) {
		if ( state->clientNum == cl.snap.ps.clientNum || ( state->eFlags & EF_DEAD ) != 0 ) {
			return EnemyHighlightColor::None;
		}

		/*
		The local third-person model can be predicted ahead of the snapshot
		playerstate. If another player is nearby, do not let a looser snapshot
		match override a stronger local-player origin match.
		*/
		if ( DotProduct( delta, delta ) <= DistanceSquared( matchOrigin, state->origin ) ) {
			return EnemyHighlightColor::None;
		}
	} else {
		/*
		Fallback when the current snapshot lookup does not find a stable player
		match. Prefer lightingOrigin so multipart player models compare against
		the shared player origin instead of head/tag attachment points.
		*/
		if ( DotProduct( delta, delta ) <= Square( kEnemyHighlightMatchRadius ) ) {
			return EnemyHighlightColor::None;
		}
	}

	info = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
	gametype = std::atoi( Info_ValueForKey( info, "g_gametype" ) );

	if ( gametype >= GT_TEAM ) {
		if ( CL_IsEnemyHighlightRedSkin( skinName ) ) {
			return EnemyHighlightColor::Red;
		}

		if ( CL_IsEnemyHighlightBlueSkin( skinName ) ) {
			return EnemyHighlightColor::Blue;
		}

		return EnemyHighlightColor::None;
	}

	return EnemyHighlightColor::Free;
}


static void CL_QueueEnemyHighlightRefEntity( const refEntity_t *ent, qboolean intShaderTime, EnemyHighlightColor colorSlot ) {
	if ( cl_enemyHighlightNumPending >= kEnemyHighlightMaxPending ) {
		return;
	}

	cl_enemyHighlightPending[cl_enemyHighlightNumPending].ent = *ent;
	cl_enemyHighlightPending[cl_enemyHighlightNumPending].intShaderTime = intShaderTime != qfalse;
	cl_enemyHighlightPending[cl_enemyHighlightNumPending].colorSlot = colorSlot;
	cl_enemyHighlightNumPending++;
}


static void CL_EnsureEnemyHighlightShaders( void ) {
	if ( !cl_enemyHighlightRimShader ) {
		cl_enemyHighlightRimShader = re.RegisterShader( "<fnql_enemy_rim>" );
	}

	if ( !cl_enemyHighlightOutlineShader ) {
		cl_enemyHighlightOutlineShader = re.RegisterShader( "<fnql_enemy_outline>" );
	}
}


static void CL_AddEnemyHighlightRefEntity( const refEntity_t *ent, qboolean intShaderTime ) {
	EnemyHighlightColor colorSlot;

	re.AddRefEntityToScene( ent, intShaderTime );

	colorSlot = CL_GetEnemyHighlightColorSlot( ent );
	if ( colorSlot == EnemyHighlightColor::None ) {
		return;
	}

	CL_QueueEnemyHighlightRefEntity( ent, intShaderTime, colorSlot );
}


static void CL_FlushEnemyHighlightRefEntities( const refdef_t *fd ) {
	color4ub_t rimColors[kEnemyHighlightColorCount];
	color4ub_t outlineColors[kEnemyHighlightColorCount];
	int i;

	if ( cl_enemyHighlightNumPending <= 0 ) {
		return;
	}

	if ( !cl_playerHighlight || !cl_playerHighlight->integer || !fd || ( fd->rdflags & RDF_NOWORLDMODEL ) != 0 ) {
		cl_enemyHighlightNumPending = 0;
		return;
	}

	CL_EnsureEnemyHighlightShaders();
	for ( i = EnemyHighlightColorIndex( EnemyHighlightColor::Red ); i < kEnemyHighlightColorCount; i++ ) {
		const EnemyHighlightColor colorSlot = static_cast<EnemyHighlightColor>( i );
		CL_GetEnemyHighlightPassColor( colorSlot, 112, cl_playerHighlightRimIntensity, &rimColors[i] );
		CL_GetEnemyHighlightPassColor( colorSlot, 208, cl_playerHighlightOutlineIntensity, &outlineColors[i] );
	}

	for ( i = 0; i < cl_enemyHighlightNumPending; i++ ) {
		const clEnemyHighlightPending_t *pending = &cl_enemyHighlightPending[i];
		const int colorIndex = EnemyHighlightColorIndex( pending->colorSlot );
		const color4ub_t *rimColor = &rimColors[colorIndex];
		const color4ub_t *outlineColor = &outlineColors[colorIndex];
		refEntity_t highlight = pending->ent;

		if ( ( cl_playerHighlight->integer & 1 ) && cl_enemyHighlightRimShader && rimColor->rgba[3] != 0 ) {
			highlight.customShader = cl_enemyHighlightRimShader;
			highlight.renderfx |= RF_NOSHADOW;
			highlight.shader = *rimColor;
			highlight.shaderTexCoord[0] = 1.03f;
			highlight.shaderTexCoord[1] = 0.0f;
			re.AddRefEntityToScene( &highlight, ToQboolean( pending->intShaderTime ) );
		}

		if ( ( cl_playerHighlight->integer & 2 ) && cl_enemyHighlightOutlineShader && cls.glconfig.stencilBits > 0 && outlineColor->rgba[3] != 0 ) {
			highlight = pending->ent;
			highlight.customShader = cl_enemyHighlightOutlineShader;
			highlight.renderfx |= RF_NOSHADOW;
			highlight.shader = *outlineColor;
			highlight.shaderTexCoord[0] = cl_playerHighlightOutlineScale ? cl_playerHighlightOutlineScale->value : 1.01f;
			highlight.shaderTexCoord[1] = 0.0f;
			re.AddRefEntityToScene( &highlight, ToQboolean( pending->intShaderTime ) );
		}
	}

	cl_enemyHighlightNumPending = 0;
}


/*
====================
CL_GetUserCmd
====================
*/
static qboolean CL_GetUserCmd( int cmdNumber, usercmd_t *ucmd ) {
	// cmds[cmdNumber] is the last properly generated command

	// can't return anything that we haven't created yet
	if ( cl.cmdNumber - cmdNumber < 0 ) {
		Com_Error( ERR_DROP, "CL_GetUserCmd: cmdNumber (%i) > cl.cmdNumber (%i)", cmdNumber, cl.cmdNumber );
	}

	// the usercmd has been overwritten in the wrapping
	// buffer because it is too far out of date
	if ( cl.cmdNumber - cmdNumber >= CMD_BACKUP ) {
		return qfalse;
	}

	*ucmd = cl.cmds[ cmdNumber & CMD_MASK ];

	return qtrue;
}


/*
====================
CL_GetCurrentCmdNumber
====================
*/
static int CL_GetCurrentCmdNumber( void ) {
	return cl.cmdNumber;
}


/*
====================
CL_GetCurrentSnapshotNumber
====================
*/
static void CL_GetCurrentSnapshotNumber( int *snapshotNumber, int *serverTime ) {
	*snapshotNumber = cl.snap.messageNum;
	*serverTime = cl.snap.serverTime;
}


/*
====================
CL_GetSnapshot
====================
*/
static qboolean CL_GetSnapshot( int snapshotNumber, snapshot_t *snapshot ) {
	clSnapshot_t	*clSnap;
	int				i, count;

	if ( cl.snap.messageNum - snapshotNumber < 0 ) {
		Com_Error( ERR_DROP, "CL_GetSnapshot: snapshotNumber (%i) > cl.snapshot.messageNum (%i)", snapshotNumber, cl.snap.messageNum );
	}

	// if the frame has fallen out of the circular buffer, we can't return it
	if ( cl.snap.messageNum - snapshotNumber >= PACKET_BACKUP ) {
		return qfalse;
	}

	// if the frame is not valid, we can't return it
	clSnap = &cl.snapshots[snapshotNumber & PACKET_MASK];
	if ( !clSnap->valid ) {
		return qfalse;
	}

	// if the entities in the frame have fallen out of their
	// circular buffer, we can't return it
	if ( cl.parseEntitiesNum - clSnap->parseEntitiesNum >= MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	// write the snapshot
	snapshot->snapFlags = clSnap->snapFlags;
	snapshot->serverCommandSequence = clSnap->serverCommandNum;
	snapshot->ping = clSnap->ping;
	snapshot->serverTime = clSnap->serverTime;
	std::copy_n( clSnap->areamask, sizeof( snapshot->areamask ), snapshot->areamask );
	snapshot->ps = clSnap->ps;
	count = clSnap->numEntities;
	if ( count > MAX_ENTITIES_IN_SNAPSHOT ) {
		Com_DPrintf( "CL_GetSnapshot: truncated %i entities to %i\n", count, MAX_ENTITIES_IN_SNAPSHOT );
		count = MAX_ENTITIES_IN_SNAPSHOT;
	}
	snapshot->numEntities = count;
	for ( i = 0 ; i < count ; i++ ) {
		snapshot->entities[i] =
			cl.parseEntities[ ( clSnap->parseEntitiesNum + i ) & (MAX_PARSE_ENTITIES-1) ];
	}

	// FIXME: configstring changes and server commands!!!

	return qtrue;
}


namespace {

constexpr int kRetailPlayerStateBytes = 0x250;
constexpr int kRetailSnapshotEntityCount = 0x180;

// Retail cgamex86.dll consumes a larger native snapshot packet than the
// inherited VM snapshot_t. Keep this layout at the native import boundary so
// bytecode modules continue to receive their original ABI.
struct RetailNativeSnapshot {
	int snapFlags;
	int ping;
	int serverTime;
	byte areamask[MAX_MAP_AREA_BYTES];
	byte playerState[kRetailPlayerStateBytes];
	int numEntities;
	entityState_t entities[kRetailSnapshotEntityCount];
	int numServerCommands;
	int serverCommandSequence;
};

static_assert( offsetof( RetailNativeSnapshot, playerState ) == 0x2c,
	"retail snapshot playerState offset changed" );
static_assert( offsetof( RetailNativeSnapshot, numEntities ) == 0x27c,
	"retail snapshot entity-count offset changed" );
static_assert( offsetof( RetailNativeSnapshot, entities ) == 0x280,
	"retail snapshot entity-array offset changed" );
static_assert( sizeof( entityState_t ) == 0xec,
	"retail entityState ABI changed" );
static_assert( sizeof( playerState_t ) <= kRetailPlayerStateBytes,
	"playerState no longer fits the retail native snapshot" );
static_assert( sizeof( RetailNativeSnapshot ) == 0x16488,
	"retail native snapshot size changed" );

static qboolean CL_GetRetailNativeSnapshot( int snapshotNumber, void *destination ) {
	if ( !destination ) {
		return qfalse;
	}
	if ( snapshotNumber > cl.snap.messageNum ) {
		Com_Error( ERR_DROP,
			"CL_GetRetailNativeSnapshot: snapshotNumber (%i) > current snapshot (%i)",
			snapshotNumber, cl.snap.messageNum );
	}
	if ( cl.snap.messageNum - snapshotNumber >= PACKET_BACKUP ) {
		return qfalse;
	}

	const clSnapshot_t &source = cl.snapshots[snapshotNumber & PACKET_MASK];
	if ( !source.valid ||
		cl.parseEntitiesNum - source.parseEntitiesNum >= MAX_PARSE_ENTITIES ) {
		return qfalse;
	}

	auto &snapshot = *static_cast<RetailNativeSnapshot *>( destination );
	std::memset( &snapshot, 0, sizeof( snapshot ) );
	snapshot.snapFlags = source.snapFlags;
	snapshot.ping = source.ping;
	snapshot.serverTime = source.serverTime;
	std::memcpy( snapshot.areamask, source.areamask, sizeof( snapshot.areamask ) );
	std::memcpy( snapshot.playerState, &source.ps, sizeof( source.ps ) );

	const int entityCount = std::min( source.numEntities,
		kRetailSnapshotEntityCount );
	if ( entityCount != source.numEntities ) {
		Com_DPrintf( "CL_GetRetailNativeSnapshot: truncated %i entities to %i\n",
			source.numEntities, entityCount );
	}
	snapshot.numEntities = entityCount;
	for ( int i = 0; i < entityCount; ++i ) {
		snapshot.entities[i] = cl.parseEntities[
			( source.parseEntitiesNum + i ) & ( MAX_PARSE_ENTITIES - 1 ) ];
	}
	snapshot.serverCommandSequence = source.serverCommandNum;
	return qtrue;
}

} // namespace


/*
=====================
CL_SetUserCmdValue
=====================
*/
static void CL_SetUserCmdValue( int userCmdValue, int userCmdPrimary, float sensitivityScale, int userCmdFov ) {
	cl.cgameUserCmdValue = userCmdValue;
	cl.cgameUserCmdPrimary = userCmdPrimary;
	cl.cgameUserCmdFov = userCmdFov;
	cl.cgameSensitivity = sensitivityScale;
}


/*
=====================
CL_AddCgameCommand
=====================
*/
static void CL_AddCgameCommand( const char *cmdName ) {
	Cmd_AddCommand( cmdName, nullptr );
}


/*
=====================
CL_ConfigstringModified
=====================
*/
static void CL_ConfigstringModified( void ) {
	const char	*old, *s;
	int			i, index;
	const char	*dup;
	gameState_t	oldGs;
	int			len;

	index = std::atoi( Cmd_Argv(1) );
	if ( static_cast<unsigned>( index ) >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "%s: bad configstring index %i", __func__, index );
	}
	// get everything after "cs <num>"
	s = Cmd_ArgsFrom(2);

	old = cl.gameState.stringData + cl.gameState.stringOffsets[ index ];
	if ( !strcmp( old, s ) ) {
		return;		// unchanged
	}

	// build the new gameState_t
	oldGs = cl.gameState;

	cl.gameState = {};

	// leave the first 0 for uninitialized strings
	cl.gameState.dataCount = 1;

	for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		if ( i == index ) {
			dup = s;
		} else {
			dup = oldGs.stringData + oldGs.stringOffsets[ i ];
		}
		if ( !dup[0] ) {
			continue;		// leave with the default empty string
		}

		len = strlen( dup );

		if ( len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS ) {
			Com_Error( ERR_DROP, "%s: MAX_GAMESTATE_CHARS exceeded", __func__ );
		}

		// append it to the gameState string buffer
		cl.gameState.stringOffsets[ i ] = cl.gameState.dataCount;
		std::copy_n( dup, len + 1, cl.gameState.stringData + cl.gameState.dataCount );
		cl.gameState.dataCount += len + 1;
	}

	if ( index == CS_SYSTEMINFO ) {
		// parse serverId and other cvars
		CL_SystemInfoChanged( qfalse );
	}
}


/*
===================
CL_GetServerCommand

Set up argc/argv for the given command
===================
*/
static qboolean CL_GetServerCommand( int serverCommandNumber ) {
	const char *s;
	const char *cmd;
	static std::array<char, BIG_INFO_STRING> bigConfigString;
	int argc, index;

	// if we have irretrievably lost a reliable command, drop the connection
	if ( clc.serverCommandSequence - serverCommandNumber >= MAX_RELIABLE_COMMANDS ) {
		// when a demo record was started after the client got a whole bunch of
		// reliable commands then the client never got those first reliable commands
		if ( clc.demoplaying ) {
			Cmd_Clear();
			return qfalse;
		}
		Com_Error( ERR_DROP, "CL_GetServerCommand: a reliable command was cycled out" );
		return qfalse;
	}

	if ( clc.serverCommandSequence - serverCommandNumber < 0 ) {
		Com_Error( ERR_DROP, "CL_GetServerCommand: requested a command not received" );
		return qfalse;
	}

	index = serverCommandNumber & ( MAX_RELIABLE_COMMANDS - 1 );
	s = clc.serverCommands[ index ];
	clc.lastExecutedServerCommand = serverCommandNumber;

	Com_DPrintf( "serverCommand: %i : %s\n", serverCommandNumber, s );

	if ( clc.serverCommandsIgnore[ index ] ) {
		Cmd_Clear();
		return qfalse;
	}

	const auto tokenizeCurrentCommand = [&]() {
		Cmd_TokenizeString( s );
		cmd = Cmd_Argv(0);
		argc = Cmd_Argc();
	};

	tokenizeCurrentCommand();

	if ( !strcmp( cmd, "disconnect" ) ) {
		// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=552
		// allow server to indicate why they were disconnected
		if ( argc >= 2 )
			Com_Error( ERR_SERVERDISCONNECT, "Server disconnected - %s", Cmd_Argv( 1 ) );
		else
			Com_Error( ERR_SERVERDISCONNECT, "Server disconnected" );
	}

	if ( !strcmp( cmd, "bcs0" ) ) {
		Com_sprintf( bigConfigString.data(), bigConfigString.size(), "cs %s \"%s", Cmd_Argv(1), Cmd_Argv(2) );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs1" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString.data()) + strlen(s) >= bigConfigString.size() ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		Q_strcat( bigConfigString.data(), static_cast<int>( bigConfigString.size() ), s );
		return qfalse;
	}

	if ( !strcmp( cmd, "bcs2" ) ) {
		s = Cmd_Argv(2);
		if( strlen(bigConfigString.data()) + strlen(s) + 1 >= bigConfigString.size() ) {
			Com_Error( ERR_DROP, "bcs exceeded BIG_INFO_STRING" );
		}
		Q_strcat( bigConfigString.data(), static_cast<int>( bigConfigString.size() ), s );
		Q_strcat( bigConfigString.data(), static_cast<int>( bigConfigString.size() ), "\"" );
		s = bigConfigString.data();
		tokenizeCurrentCommand();
	}

	if ( !strcmp( cmd, "cs" ) ) {
		CL_ConfigstringModified();
		// reparse the string, because CL_ConfigstringModified may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		return qtrue;
	}

	if ( !strcmp( cmd, "map_restart" ) ) {
		// clear notify lines and outgoing commands before passing
		// the restart to the cgame
		Con_ClearNotify();
		// reparse the string, because Con_ClearNotify() may have done another Cmd_TokenizeString()
		Cmd_TokenizeString( s );
		std::fill_n( cl.cmds, CMD_BACKUP, usercmd_t{} );
		cls.lastVidRestart = Sys_Milliseconds(); // hack for OSP mod
		return qtrue;
	}

	// the clientLevelShot command is used during development
	// to generate 128*128 screenshots from the intermission
	// point of levels for the menu system to use
	// we pass it along to the cgame to make appropriate adjustments,
	// but we also clear the console and notify lines here
	if ( !strcmp( cmd, "clientLevelShot" ) ) {
		// don't do it if we aren't running the server locally,
		// otherwise malicious remote servers could overwrite
		// the existing thumbnails
		if ( !com_sv_running->integer ) {
			return qfalse;
		}
		// close the console
		Con_Close();
		// take a special screenshot next frame
		Cbuf_AddText( "wait ; wait ; wait ; wait ; screenshot levelshot\n" );
		return qtrue;
	}

	// we may want to put a "connect to other server" command here

	// cgame can now act on the command
	return qtrue;
}


/*
====================
CL_CM_LoadMap

Just adds default parameters that cgame doesn't need to know about
====================
*/
static void CL_CM_LoadMap( const char *mapname ) {
	int		checksum;

	CM_LoadMap( mapname, qtrue, &checksum );
}


/*
====================
CL_ShutdonwCGame

====================
*/
void CL_ShutdownCGame( void ) {

	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CGAME );
	cls.cgameStarted = qfalse;
	CL_ClearEnemyHighlightState();
	CL_ClearLiquidInteractionState();

	if ( !cgvm ) {
		return;
	}

	re.VertexLighting( qfalse );

	VM_Call( cgvm, 0, CG_SHUTDOWN );
	VM_Free( cgvm );
	cgvm = nullptr;
	FS_VM_CloseFiles( H_CGAME );
}


static int FloatAsInt( float f ) {
	floatint_t fi;
	fi.f = f;
	return fi.i;
}


static void *VM_ArgPtr( intptr_t intValue ) {

	if ( !intValue || cgvm == nullptr )
	  return nullptr;

	if ( cgvm->entryPoint || cgvm->dllExports )
		return reinterpret_cast<void *>( intValue );
	else
		return cgvm->dataBase + ( intValue & cgvm->dataMask );
}

class VmArgPtr {
public:
	explicit VmArgPtr( void *ptr ) : ptr_( ptr ) {
	}

	template <typename T>
	operator T *() const {
		return static_cast<T *>( ptr_ );
	}

	operator void *() const {
		return ptr_;
	}

	const char *String() const {
		return static_cast<const char *>( ptr_ );
	}

private:
	void *ptr_;
};

#undef VMA
#define VMA(x) VmArgPtr( VM_ArgPtr( args[x] ) )


static qboolean CL_GetValue( char* value, int valueSize, const char* key ) {

	if ( !Q_stricmp( key, "trap_R_AddRefEntityToScene2" ) ) {
		Com_sprintf( value, valueSize, "%i", CG_R_ADDREFENTITYTOSCENE2 );
		return qtrue;
	}

	if ( !Q_stricmp( key, "trap_R_ForceFixedDLights" ) ) {
		Com_sprintf( value, valueSize, "%i", CG_R_FORCEFIXEDDLIGHTS );
		return qtrue;
	}

	if ( !Q_stricmp( key, "trap_R_AddLinearLightToScene_Q3E" ) && re.AddLinearLightToScene ) {
		Com_sprintf( value, valueSize, "%i", CG_R_ADDLINEARLIGHTTOSCENE );
		return qtrue;
	}

	if ( !Q_stricmp( key, "trap_IsRecordingDemo" ) ) {
		Com_sprintf( value, valueSize, "%i", CG_IS_RECORDING_DEMO );
		return qtrue;
	}

	if ( !Q_stricmp( key, "trap_Cvar_SetDescription_Q3E" ) ) {
		Com_sprintf( value, valueSize, "%i", CG_CVAR_SETDESCRIPTION );
		return qtrue;
	}

	return qfalse;
}


static void CL_ForceFixedDlights( void ) {
	cvar_t *cv;

	cv = Cvar_Get( "r_dlightMode", "2", 0 );
	if ( cv ) {
		Cvar_CheckRange( cv, "1", "2", CV_INTEGER );
	}
}


/*
====================
CL_CgameSystemCalls

The cgame module is making a system call
====================
*/
static intptr_t CL_CgameSystemCalls( intptr_t *args ) {
	switch( args[0] ) {
	case CG_PRINT:
		Com_Printf( "%s", VMA(1).String() );
		return 0;
	case CG_ERROR:
		Com_Error( ERR_DROP, "%s", VMA(1).String() );
		return 0;
	case CG_MILLISECONDS:
		return Sys_Milliseconds();
	case CG_CVAR_REGISTER:
		if ( cgvm->dllExports ) {
			Cvar_Register( VMA(1), VMA(2), VMA(3), args[4], cgvm->privateFlag );
		} else {
			Cvar_RegisterLegacy( VMA(1), VMA(2), VMA(3), args[4], cgvm->privateFlag );
		}
		return 0;
	case CG_CVAR_UPDATE:
		if ( cgvm->dllExports ) {
			Cvar_Update( VMA(1), cgvm->privateFlag );
		} else {
			Cvar_UpdateLegacy( VMA(1), cgvm->privateFlag );
		}
		return 0;
	case CG_CVAR_SET:
		Cvar_SetSafe( VMA(1), VMA(2) );
		return 0;
	case CG_CVAR_VARIABLESTRINGBUFFER:
		VM_CHECKBOUNDS( cgvm, args[2], args[3] );
		Cvar_VariableStringBufferSafe( VMA(1), VMA(2), args[3], CVAR_PRIVATE );
		return 0;
	case CG_ARGC:
		return Cmd_Argc();
	case CG_ARGV:
		VM_CHECKBOUNDS( cgvm, args[2], args[3] );
		Cmd_ArgvBuffer( args[1], VMA(2), args[3] );
		return 0;
	case CG_ARGS:
		VM_CHECKBOUNDS( cgvm, args[1], args[2] );
		Cmd_ArgsBuffer( VMA(1), args[2] );
		return 0;
	case CG_CMD_EXECUTETEXT:
		Cbuf_ExecuteText( static_cast<cbufExec_t>( args[1] ), VMA(2) );
		return 0;

	case CG_FS_FOPENFILE:
		return FS_VM_OpenFile( VMA(1), VMA(2), static_cast<fsMode_t>( args[3] ), H_CGAME );
	case CG_FS_READ:
		VM_CHECKBOUNDS( cgvm, args[1], args[2] );
		FS_VM_ReadFile( VMA(1), args[2], args[3], H_CGAME );
		return 0;
	case CG_FS_WRITE:
		VM_CHECKBOUNDS( cgvm, args[1], args[2] );
		FS_VM_WriteFile( VMA(1), args[2], args[3], H_CGAME );
		return 0;
	case CG_FS_FCLOSEFILE:
		FS_VM_CloseFile( args[1], H_CGAME );
		return 0;
	case CG_FS_SEEK:
		return FS_VM_SeekFile( args[1], args[2], static_cast<fsOrigin_t>( args[3] ), H_CGAME );

	case CG_SENDCONSOLECOMMAND: {
		const char *cmd = VMA(1);
		Cbuf_NestedAdd( cmd );
		return 0;
	}
	case CG_ADDCOMMAND:
		CL_AddCgameCommand( VMA(1) );
		return 0;
	case CG_REMOVECOMMAND:
		Cmd_RemoveCommandSafe( VMA(1) );
		return 0;
	case CG_SENDCLIENTCOMMAND:
		CL_AddReliableCommand( VMA(1), qfalse );
		return 0;
	case CG_UPDATESCREEN:
		// this is used during lengthy level loading, so pump message loop
		// Com_EventLoop();	// FIXME: if a server restarts here, BAD THINGS HAPPEN!
		// We can't call Com_EventLoop here, a restart will crash and this _does_ happen
		// if there is a map change while we are downloading at pk3.
		// ZOID
		SCR_UpdateScreen();
		return 0;
	case CG_CM_LOADMAP:
		CL_CM_LoadMap( VMA(1) );
		return 0;
	case CG_CM_NUMINLINEMODELS:
		return CM_NumInlineModels();
	case CG_CM_INLINEMODEL:
		return CM_InlineModel( args[1] );
	case CG_CM_LOADMODEL:
		return 0;
	case CG_CM_TEMPBOXMODEL:
		return CM_TempBoxModel( VMA(1), VMA(2), /*int capsule*/ qfalse );
	case CG_CM_TEMPCAPSULEMODEL:
		return CM_TempBoxModel( VMA(1), VMA(2), /*int capsule*/ qtrue );
	case CG_CM_POINTCONTENTS:
		return CM_PointContents( VMA(1), args[2] );
	case CG_CM_TRANSFORMEDPOINTCONTENTS:
		return CM_TransformedPointContents( VMA(1), args[2], VMA(3), VMA(4) );
	case CG_CM_BOXTRACE:
		CM_BoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], /*int capsule*/ qfalse );
		return 0;
	case CG_CM_CAPSULETRACE:
		CM_BoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], /*int capsule*/ qtrue );
		return 0;
	case CG_CM_TRANSFORMEDBOXTRACE:
		CM_TransformedBoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], VMA(8), VMA(9), /*int capsule*/ qfalse );
		return 0;
	case CG_CM_TRANSFORMEDCAPSULETRACE:
		CM_TransformedBoxTrace( VMA(1), VMA(2), VMA(3), VMA(4), VMA(5), args[6], args[7], VMA(8), VMA(9), /*int capsule*/ qtrue );
		return 0;
	case CG_CM_MARKFRAGMENTS:
		return re.MarkFragments( args[1], VMA(2), VMA(3), args[4], VMA(5), args[6], VMA(7) );
	case CG_S_STARTSOUND:
		S_StartSound( VMA(1), args[2], args[3], args[4] );
		return 0;
	case CG_S_STARTLOCALSOUND:
		S_StartLocalSound( args[1], args[2] );
		return 0;
	case CG_S_CLEARLOOPINGSOUNDS:
		S_ClearLoopingSounds( static_cast<qboolean>( args[1] ) );
		return 0;
	case CG_S_ADDLOOPINGSOUND:
		S_AddLoopingSound( args[1], VMA(2), VMA(3), args[4] );
		return 0;
	case CG_S_ADDREALLOOPINGSOUND:
		S_AddRealLoopingSound( args[1], VMA(2), VMA(3), args[4] );
		return 0;
	case CG_S_STOPLOOPINGSOUND:
		S_StopLoopingSound( args[1] );
		return 0;
	case CG_S_UPDATEENTITYPOSITION: {
		const vec_t *origin = VMA(2);

		S_UpdateEntityPosition( args[1], origin );
		CL_UpdateLiquidEntityPosition( args[1], origin );
		return 0;
	}
	case CG_S_RESPATIALIZE:
		S_Respatialize( args[1], VMA(2), VMA(3), args[4] );
		return 0;
	case CG_S_REGISTERSOUND:
		return S_RegisterSound( VMA(1), static_cast<qboolean>( args[2] ) );
	case CG_S_STARTBACKGROUNDTRACK:
		S_StartBackgroundTrack( VMA(1), VMA(2) );
		return 0;
	case CG_R_LOADWORLDMAP:
		re.LoadWorld( VMA(1) );
		return 0;
	case CG_R_REGISTERMODEL:
	{
		qhandle_t model = re.RegisterModel( VMA(1) );
		CL_RecordEnemyHighlightModel( model, VMA(1) );
		return model;
	}
	case CG_R_REGISTERSKIN:
	{
		qhandle_t skin = re.RegisterSkin( VMA(1) );
		CL_RecordEnemyHighlightSkin( skin, VMA(1) );
		return skin;
	}
	case CG_R_REGISTERSHADER:
		return re.RegisterShader( VMA(1) );
	case CG_R_REGISTERSHADERNOMIP:
		return re.RegisterShaderNoMip( VMA(1) );
	case CG_R_REGISTERFONT:
		re.RegisterFont( VMA(1), args[2], VMA(3));
		return 0;
	case CG_R_CLEARSCENE:
		cl_enemyHighlightNumPending = 0;
		re.ClearScene();
		return 0;
	case CG_R_ADDREFENTITYTOSCENE:
		CL_AddEnemyHighlightRefEntity( VMA(1), qfalse );
		return 0;
	case CG_R_ADDPOLYTOSCENE:
		re.AddPolyToScene( args[1], args[2], VMA(3), 1 );
		return 0;
	case CG_R_ADDPOLYSTOSCENE:
		re.AddPolyToScene( args[1], args[2], VMA(3), args[4] );
		return 0;
	case CG_R_LIGHTFORPOINT:
		return re.LightForPoint( VMA(1), VMA(2), VMA(3), VMA(4) );
	case CG_R_ADDLIGHTTOSCENE:
		re.AddLightToScene( VMA(1), VMF(2), VMF(3), VMF(4), VMF(5) );
		return 0;
	case CG_R_ADDADDITIVELIGHTTOSCENE:
		re.AddAdditiveLightToScene( VMA(1), VMF(2), VMF(3), VMF(4), VMF(5) );
		return 0;
	case CG_R_RENDERSCENE: {
		const refdef_t *refdef = VMA(1);
		refdef_t adjustedRefdef;

		/* Cgame uses separate RDF_NOWORLDMODEL scenes for 3D HUD models.  A
		 * capture that hides the HUD must discard these without suppressing the
		 * primary world scene or carrying deferred highlight entities forward. */
		if ( refdef && CL_CaptureHidesHud() &&
			( refdef->rdflags & RDF_NOWORLDMODEL ) ) {
			cl_enemyHighlightNumPending = 0;
			return 0;
		}

		if ( refdef && cl_captureActive && cl_captureActive->integer &&
			r_levelshotHideViewWeapon && r_levelshotHideViewWeapon->integer ) {
			adjustedRefdef = *refdef;
			refdef = &adjustedRefdef;

			adjustedRefdef.rdflags |= RDF_NOFIRSTPERSON;
		}

		CL_UpdateLocalViewLiquidInteraction( refdef );
		CL_FlushEnemyHighlightRefEntities( refdef );
		re.RenderScene( refdef );
		return 0;
	}
	case CG_R_SETCOLOR:
		re.SetColor( VMA(1) );
		return 0;
	case CG_R_DRAWSTRETCHPIC: {
		if ( !CL_CaptureHidesHud() ) {
			float x = VMF(1);
			float y = VMF(2);
			float w = VMF(3);
			float h = VMF(4);
			float s0 = VMF(5);
			float t0 = VMF(6);
			float s1 = VMF(7);
			float t1 = VMF(8);

			SCR_AdjustRetailCGameLoadingBackdropUV( x, y, w, h,
				&s0, &t0, &s1, &t1 );
			re.DrawStretchPic( x, y, w, h, s0, t0, s1, t1, args[9] );
		}
		return 0;
	}
	case CG_R_MODELBOUNDS:
		re.ModelBounds( args[1], VMA(2), VMA(3) );
		return 0;
	case CG_R_LERPTAG:
		return re.LerpTag( VMA(1), args[2], args[3], args[4], VMF(5), VMA(6) );
	case CG_GETGLCONFIG:
		VM_CHECKBOUNDS( cgvm, args[1], sizeof( glconfig_t ) );
		CL_GetGlconfig( VMA(1) );
		return 0;
	case CG_GETGAMESTATE:
		VM_CHECKBOUNDS( cgvm, args[1], sizeof( gameState_t ) );
		CL_GetGameState( VMA(1) );
		return 0;
	case CG_GETCURRENTSNAPSHOTNUMBER:
		CL_GetCurrentSnapshotNumber( VMA(1), VMA(2) );
		return 0;
	case CG_GETSNAPSHOT:
		return CL_GetSnapshot( args[1], VMA(2) );
	case CG_GETSERVERCOMMAND:
		return CL_GetServerCommand( args[1] );
	case CG_GETCURRENTCMDNUMBER:
		return CL_GetCurrentCmdNumber();
	case CG_GETUSERCMD:
		return CL_GetUserCmd( args[1], VMA(2) );
	case CG_SETUSERCMDVALUE:
		CL_SetUserCmdValue( args[1], args[2], VMF(3), args[4] );
		return 0;
	case CG_MEMORY_REMAINING:
		return Hunk_MemoryRemaining();
	case CG_KEY_ISDOWN:
		return Key_IsDown( args[1] );
	case CG_KEY_GETCATCHER:
		return Key_GetCatcher();
	case CG_KEY_SETCATCHER:
		// Console and browser ownership are controlled by their host subsystems.
		Key_SetCatcher( args[1] | ( Key_GetCatcher( ) & ( KEYCATCH_CONSOLE | KEYCATCH_BROWSER ) ) );
		return 0;
	case CG_KEY_GETKEY:
		return Key_GetKey( VMA(1) );
	case CG_KEY_KEYNUMTOSTRINGBUF:
		VM_CHECKBOUNDS( cgvm, args[2], args[3] );
		Q_strncpyz( VMA(2), Key_KeynumToString( args[1] ), args[3] );
		return 0;
	case CG_KEY_GETBINDINGBUF:
	{
		const char *binding;

		VM_CHECKBOUNDS( cgvm, args[2], args[3] );
		binding = Key_GetBinding( args[1] );
		Q_strncpyz( VMA(2), binding ? binding : "", args[3] );
		return 0;
	}
	case CG_KEY_SETBINDING:
		Key_SetBinding( args[1], VMA(2) );
		return 0;
	case CG_KEY_GETOVERSTRIKEMODE:
		return Key_GetOverstrikeMode();
	case CG_KEY_SETOVERSTRIKEMODE:
		Key_SetOverstrikeMode( static_cast<qboolean>( args[1] ) );
		return 0;

	// shared syscalls
	case TRAP_MEMSET:
		VM_CHECKBOUNDS( cgvm, args[1], args[3] );
		Com_Memset( VMA(1), args[2], args[3] );
		return args[1];
	case TRAP_MEMCPY:
		VM_CHECKBOUNDS2( cgvm, args[1], args[2], args[3] );
		Com_Memcpy( VMA(1), VMA(2), args[3] );
		return args[1];
	case TRAP_STRNCPY:
		VM_CHECKBOUNDS( cgvm, args[1], args[3] );
		Q_strncpy( VMA(1), VMA(2), args[3] );
		return args[1];
	case TRAP_SIN:
		return FloatAsInt( sin( VMF(1) ) );
	case TRAP_COS:
		return FloatAsInt( cos( VMF(1) ) );
	case TRAP_ATAN2:
		return FloatAsInt( atan2( VMF(1), VMF(2) ) );
	case TRAP_SQRT:
		return FloatAsInt( sqrt( VMF(1) ) );

	case CG_FLOOR:
		return FloatAsInt( floor( VMF(1) ) );
	case CG_CEIL:
		return FloatAsInt( ceil( VMF(1) ) );
	case CG_TESTPRINTINT:
		return Com_sprintf( VMA(1), MAX_STRING_CHARS, "%i", static_cast<int>( args[2] ) );
	case CG_TESTPRINTFLOAT:
		return Com_sprintf( VMA(1), MAX_STRING_CHARS, "%f", VMF(2) );
	case CG_ACOS:
		return FloatAsInt( Q_acos( VMF(1) ) );

	case CG_PC_ADD_GLOBAL_DEFINE:
		return botlib_export->PC_AddGlobalDefine( VMA(1) );
	case CG_PC_LOAD_SOURCE:
		return botlib_export->PC_LoadSourceHandle( VMA(1) );
	case CG_PC_FREE_SOURCE:
		return botlib_export->PC_FreeSourceHandle( args[1] );
	case CG_PC_READ_TOKEN:
		return botlib_export->PC_ReadTokenHandle( args[1], VMA(2) );
	case CG_PC_SOURCE_FILE_AND_LINE:
		return botlib_export->PC_SourceFileAndLine( args[1], VMA(2), VMA(3) );

	case CG_S_STOPBACKGROUNDTRACK:
		S_StopBackgroundTrack();
		return 0;

	case CG_REAL_TIME:
		return Com_RealTime( VMA(1) );
	case CG_SNAPVECTOR:
		Sys_SnapVector( VMA(1) );
		return 0;

	case CG_CIN_PLAYCINEMATIC:
		return CIN_PlayCinematic(VMA(1), args[2], args[3], args[4], args[5], args[6]);

	case CG_CIN_STOPCINEMATIC:
		return CIN_StopCinematic(args[1]);

	case CG_CIN_RUNCINEMATIC:
		return CIN_RunCinematic(args[1]);

	case CG_CIN_DRAWCINEMATIC:
		CIN_DrawCinematic(args[1]);
		return 0;

	case CG_CIN_SETEXTENTS:
		CIN_SetExtents(args[1], args[2], args[3], args[4], args[5]);
		return 0;

	case CG_R_REMAP_SHADER:
		re.RemapShader( VMA(1), VMA(2), VMA(3) );
		return 0;

/*
	case CG_LOADCAMERA:
		return loadCamera(VMA(1));

	case CG_STARTCAMERA:
		startCamera(args[1]);
		return 0;

	case CG_GETCAMERAINFO:
		return getCameraInfo(args[1], VMA(2), VMA(3));
*/
	case CG_GET_ENTITY_TOKEN:
		VM_CHECKBOUNDS( cgvm, args[1], args[2] );
		return re.GetEntityToken( VMA(1), args[2] );

	case CG_R_INPVS:
		return re.inPVS( VMA(1), VMA(2) );

	// engine extensions
	case CG_R_ADDREFENTITYTOSCENE2:
		CL_AddEnemyHighlightRefEntity( VMA(1), qtrue );
		return 0;

	case CG_R_ADDLINEARLIGHTTOSCENE:
		re.AddLinearLightToScene( VMA(1), VMA(2), VMF(3), VMF(4), VMF(5), VMF(6) );
		return 0;

	case CG_R_FORCEFIXEDDLIGHTS:
		CL_ForceFixedDlights();
		return 0;

	case CG_IS_RECORDING_DEMO:
		return clc.demorecording;

	case CG_CVAR_SETDESCRIPTION:
		Cvar_SetDescription2( VMA(1).String(), VMA(2).String() );
		return 0;

	case CG_ADVERTISEMENTBRIDGE_INITCGAME:
		CL_AdvertisementBridge_InitCGame();
		return 0;

	case CG_ADVERTISEMENTBRIDGE_SHUTDOWNCGAME:
		CL_AdvertisementBridge_ShutdownCGame();
		return 0;

	case CG_ADVERTISEMENTBRIDGE_UPDATELOADINGVIEWPARAMETERS:
		CL_AdvertisementBridge_UpdateLoadingViewParameters();
		return 0;

	case CG_ADVERTISEMENTBRIDGE_SETFRAMETIME:
		CL_AdvertisementBridge_SetFrameTime( args[1] );
		return 0;

	case CG_TRAP_GETVALUE:
		VM_CHECKBOUNDS( cgvm, args[1], args[2] );
		return CL_GetValue( VMA(1), args[2], VMA(3) );

	default:
		Com_Error( ERR_DROP, "Bad cgame system trap: %ld", static_cast<long>( args[0] ) );
	}
	return 0;
}


/*
====================
CL_DllSyscall
====================
*/
static intptr_t QDECL CL_DllSyscall( intptr_t arg, ... ) {
#if !id386 || defined __clang__
	intptr_t	args[10]; // max.count for cgame
	va_list	ap;
	int i;

	args[0] = arg;
	va_start( ap, arg );
	for (i = 1; i < static_cast<int>( ARRAY_LEN( args ) ); i++ )
		args[ i ] = va_arg( ap, intptr_t );
	va_end( ap );

	return CL_CgameSystemCalls( args );
#else
	return CL_CgameSystemCalls( &arg );
#endif
}

typedef void (QDECL *ql_import_f)( void );

static ql_import_f ql_cgame_imports[CGAME_NATIVE_IMPORT_COUNT];
static vec4_t ql_cgame_currentColor = { 1.0f, 1.0f, 1.0f, 1.0f };
static std::array<uint64_t, MAX_CLIENTS> ql_cgame_mutedIdentitySet;
static int ql_cgame_mutedIdentityCount;

template<typename T>
static intptr_t CG_ImportArgValue( T value ) {
	using ArgType = typename std::remove_cv<typename std::remove_reference<T>::type>::type;

	if constexpr ( std::is_pointer<ArgType>::value ) {
		return reinterpret_cast<intptr_t>( value );
	} else if constexpr ( std::is_enum<ArgType>::value ) {
		return static_cast<intptr_t>( value );
	} else if constexpr ( std::is_integral<ArgType>::value ) {
		return static_cast<intptr_t>( value );
	} else if constexpr ( std::is_floating_point<ArgType>::value ) {
		float packedFloat = static_cast<float>( value );
		int packedBits;

		std::memcpy( &packedBits, &packedFloat, sizeof( packedBits ) );
		return static_cast<intptr_t>( packedBits );
	} else {
		static_assert( std::is_arithmetic<ArgType>::value, "unsupported cgame import argument type" );
		return 0;
	}
}

static intptr_t CG_Import_Dispatch( intptr_t *args ) {
	const intptr_t arg = args[0];

	if ( arg == CG_GETGLCONFIG ) {
		CL_CopyRetailGlconfig( reinterpret_cast<void *>( args[1] ) );
		return 0;
	}

	if ( arg == CG_GETSNAPSHOT ) {
		return CL_GetRetailNativeSnapshot( static_cast<int>( args[1] ),
			reinterpret_cast<void *>( args[2] ) );
	}

	if ( arg == CG_SETUSERCMDVALUE ) {
		CL_SetUserCmdValue( static_cast<int>( args[1] ), static_cast<int>( args[2] ), _vmf( args[3] ), static_cast<int>( args[4] ) );
		return 0;
	}

	return CL_CgameSystemCalls( args );
}

static intptr_t CG_Import_Syscall( intptr_t arg ) {
	intptr_t args[16] = {};

	args[0] = arg;
	return CG_Import_Dispatch( args );
}

template<typename... Args>
static intptr_t CG_Import_Syscall( intptr_t arg, Args... rawArgs ) {
	static_assert( sizeof...( rawArgs ) < 16, "too many cgame import arguments" );

	intptr_t args[16] = { arg, CG_ImportArgValue( rawArgs )... };

	return CG_Import_Dispatch( args );
}

#include "ql_cgame_imports.inc"

void CL_ShowFirstTrackedPlayer( void ) {
	if ( !cgvm || !cgvm->dllExports ) {
		return;
	}

	VM_Call( cgvm, 0, CG_SHOW_1ST_TRACKED_PLAYER );
}

void CL_ShowSecondTrackedPlayer( void ) {
	if ( !cgvm || !cgvm->dllExports ) {
		return;
	}

	VM_Call( cgvm, 0, CG_SHOW_2ND_TRACKED_PLAYER );
}

int CL_GetCGamePhysicsTime( void ) {
	if ( !cgvm || !cgvm->dllExports ) {
		return 0;
	}

	const int physicsTime = VM_Call( cgvm, 0, CG_GET_PHYSICS_TIME );
	return physicsTime > 0 ? physicsTime : 0;
}

static uint64_t QL_CG_CombineIdentityWords( unsigned int identityLow, unsigned int identityHigh ) {
	return ( static_cast<uint64_t>( identityHigh ) << 32 ) | static_cast<uint64_t>( identityLow );
}

static int QL_CG_FindMutedIdentityIndex( uint64_t identity ) {
	for ( int i = 0; i < ql_cgame_mutedIdentityCount; ++i ) {
		if ( ql_cgame_mutedIdentitySet[i] == identity ) {
			return i;
		}
	}

	return -1;
}

qboolean CL_IsSteamIdentityMuted( unsigned int identityLow, unsigned int identityHigh ) {
	const uint64_t identity = QL_CG_CombineIdentityWords( identityLow, identityHigh );
	return ( identity && QL_CG_FindMutedIdentityIndex( identity ) >= 0 ) ? qtrue : qfalse;
}

qboolean CL_ToggleSteamIdentityMute( unsigned int identityLow, unsigned int identityHigh ) {
	const uint64_t identity = QL_CG_CombineIdentityWords( identityLow, identityHigh );
	if ( !identity ) {
		return qfalse;
	}

	const int index = QL_CG_FindMutedIdentityIndex( identity );
	if ( index >= 0 ) {
		--ql_cgame_mutedIdentityCount;
		ql_cgame_mutedIdentitySet[index] = ql_cgame_mutedIdentitySet[ql_cgame_mutedIdentityCount];
		ql_cgame_mutedIdentitySet[ql_cgame_mutedIdentityCount] = 0;
		return qfalse;
	}

	if ( ql_cgame_mutedIdentityCount >= static_cast<int>( ql_cgame_mutedIdentitySet.size() ) ) {
		return qfalse;
	}

	ql_cgame_mutedIdentitySet[ql_cgame_mutedIdentityCount++] = identity;
	return qtrue;
}

static uint64_t QL_CG_PackFloatBits64( float lo, float hi ) {
	union {
		uint64_t value;
		struct {
			float lo;
			float hi;
		} parts;
	} packed;

	packed.parts.lo = lo;
	packed.parts.hi = hi;
	return packed.value;
}

static void QDECL QL_CG_trap_Cvar_RegisterRange( vmCvar_t *vmCvar, const char *varName,
		const char *defaultValue, const char *minimumValue, const char *maximumValue, int flags ) {
	Cvar_Register( vmCvar, varName, defaultValue, flags, cgvm ? cgvm->privateFlag : CVAR_PRIVATE );

	cvar_t *cv = Cvar_Get( varName, defaultValue, flags );
	if ( cv ) {
		// Retail bounds are numeric clamps, not an integer type declaration. A
		// large part of cgame uses integral-looking endpoints for float defaults
		// such as 0.75; treating those as integers destroys retail settings.
		Cvar_CheckRange( cv, minimumValue, maximumValue, CV_FLOAT );
	}
}

static void QDECL QL_CG_trap_Cvar_SetValue( const char *varName, float value ) {
	Cvar_SetValueSafe( varName, value );
}

static float QDECL QL_CG_trap_Cvar_VariableValue( const char *varName ) {
	return Cvar_VariableValue( varName );
}

static void QDECL QL_CG_trap_Cvar_Reset( const char *varName ) {
	Cvar_Reset( varName );
}

static int QDECL QL_CG_trap_FS_GetFileList( const char *path, const char *extension, char *listbuf, int bufsize ) {
	return FS_GetFileList( path, extension, listbuf, bufsize );
}

static void QDECL QL_CG_trap_S_StartSoundVolume( vec3_t origin, int entityNum, int entchannel, sfxHandle_t sfx, float volume ) {
	S_StartSoundVolume( origin, entityNum, entchannel, sfx, volume );
}

static void QDECL QL_CG_trap_S_StartLocalSoundVolume( sfxHandle_t sfx, int channelNum, float volume ) {
	S_StartLocalSoundVolume( sfx, channelNum, volume );
}

static void QDECL QL_CG_trap_S_ClearLoopingSoundsFrame( void ) {
	S_ClearLoopingSoundsFrame();
}

static void QDECL QL_CG_trap_S_ClearLoopingSoundsKillAll( void ) {
	S_ClearLoopingSounds( qtrue );
	S_ClearSoundBuffer();
}

static qhandle_t QDECL QL_CG_trap_SetupAdvertCellShader( const char *defaultContent, const void *rect, int cellId ) {
	return CL_AdvertisementBridge_SetupAdvertCellShader( defaultContent, rect, cellId );
}

static qhandle_t QDECL QL_CG_trap_RefreshAdvertCellShader( const char *defaultContent, const void *rect, int cellId ) {
	return CL_AdvertisementBridge_RefreshAdvertCellShader( defaultContent, rect, cellId );
}

static void QDECL QL_CG_trap_SetActiveAdvert( int cellId ) {
	CL_AdvertisementBridge_SetActiveAdvert( cellId );
}

static void QDECL QL_CG_trap_UpdateAdvert( int handleOrToken, int area ) {
	CL_AdvertisementBridge_UpdateAdvert( handleOrToken, area );
}

static int QDECL QL_CG_trap_RetailReservedImport( void ) {
	return 0;
}

static void QDECL QL_CG_trap_AdvertisementBridge_Reserved21C0( void ) {
	CL_AdvertisementBridge_Reserved21C0();
}

static void QDECL QL_CG_trap_AdvertisementBridge_SetMapPath( const char *mapPath ) {
	CL_AdvertisementBridge_SetMapPath( mapPath );
}

static void QDECL QL_CG_trap_AdvertisementBridge_UpdateViewParameters( void ) {
	CL_AdvertisementBridge_UpdateViewParameters();
}

static void QDECL QL_CG_trap_AdvertisementBridge_ClearDelay( void ) {
	CL_AdvertisementBridge_ClearDelay();
}

static void QDECL QL_CG_trap_R_SetColor_QL( const float *rgba ) {
	if ( rgba ) {
		ql_cgame_currentColor[0] = rgba[0];
		ql_cgame_currentColor[1] = rgba[1];
		ql_cgame_currentColor[2] = rgba[2];
		ql_cgame_currentColor[3] = rgba[3];
	} else {
		ql_cgame_currentColor[0] = 1.0f;
		ql_cgame_currentColor[1] = 1.0f;
		ql_cgame_currentColor[2] = 1.0f;
		ql_cgame_currentColor[3] = 1.0f;
	}

	re.SetColor( rgba );
}

static void QDECL QL_CG_trap_PublishTaggedInfoString( const char *messageType, const char *infoString ) {
	CL_WebView_PublishTaggedInfoString( messageType, infoString );
}

static void QDECL QL_CG_trap_R_MirrorPoint( vec3_t in, orientation_t *surface, orientation_t *camera, vec3_t out ) {
	vec3_t local;
	vec3_t transformed;

	VectorSubtract( in, surface->origin, local );
	VectorClear( transformed );
	for ( int i = 0; i < 3; ++i ) {
		const float d = DotProduct( local, surface->axis[i] );
		VectorMA( transformed, d, camera->axis[i], transformed );
	}
	VectorAdd( transformed, camera->origin, out );
}

static void QDECL QL_CG_trap_R_MirrorVector( vec3_t in, orientation_t *surface, orientation_t *camera, vec3_t out ) {
	VectorClear( out );
	for ( int i = 0; i < 3; ++i ) {
		const float d = DotProduct( in, surface->axis[i] );
		VectorMA( out, d, camera->axis[i], out );
	}
}

static void QDECL QL_CG_trap_DrawScaledText( int x, int y, const char *text, int fontHandle,
		float scale, int limit, float *maxX, int forceColor ) {
	if ( !CL_CaptureHidesHud() ) {
		RE_DrawScaledText( x, y, text, fontHandle, scale, limit, maxX,
			forceColor != qfalse ? qtrue : qfalse, ql_cgame_currentColor );
	}
}

static uint64_t QDECL QL_CG_trap_MeasureText( const char *text, const char *end, int fontHandle,
		float scale, int limit, float *outLeft ) {
	float bounds[5] = {};
	float width;
	float height;

	RE_MeasureScaledText( text, end, fontHandle, scale, limit, bounds );
	fnql::font::CopyMeasureBounds( outLeft, bounds );
	width = bounds[2] - bounds[0];
	height = bounds[4];

	return QL_CG_PackFloatBits64( width, height );
}

static int QDECL QL_CG_trap_IsClientMuted( unsigned int identityLow, unsigned int identityHigh ) {
	return CL_IsSteamIdentityMuted( identityLow, identityHigh ) ? 1 : 0;
}

static int QDECL QL_CG_trap_ToggleClientMute( unsigned int identityLow, unsigned int identityHigh ) {
	return CL_ToggleSteamIdentityMute( identityLow, identityHigh ) ? 1 : 0;
}

static int QDECL QL_CG_trap_IsSubscribedApp( int appId ) {
	return CL_IsSubscribedApp( appId ) ? 1 : 0;
}

static qhandle_t QDECL QL_CG_trap_GetAvatarImageHandle( unsigned int identityLow, unsigned int identityHigh ) {
	return CL_GetAvatarImageHandle( identityLow, identityHigh );
}

/*
====================
CL_IsExpectedRetailCGameNullImport

The retail cgame import slab has three intentionally empty rows.  Keep those
slots empty and require a callable target for every other retail row before
passing the table to a native module.  The compatibility tail after slot 127
is engine-owned and is not part of this retail-table validation.
====================
*/
static bool CL_IsExpectedRetailCGameNullImport( int slot ) {
	switch ( slot ) {
	case CG_QL_IMPORT_NULL_100:
	case CG_QL_IMPORT_NULL_118:
	case CG_QL_IMPORT_NULL_119:
		return true;
	default:
		return false;
	}
}

/*
====================
CL_IsExpectedRetailCGameReservedFallbackImport

These rows are present in the recovered retail import slab but remain
unclassified.  The current retail HLIL corpus has no direct calls through
them, so keep their explicit no-op fallback rather than accidentally exposing
an unrelated host service at one of their ABI positions.
====================
*/
static bool CL_IsExpectedRetailCGameReservedFallbackImport( int slot ) {
	switch ( slot ) {
	case CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_59:
	case CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_63:
	case CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_65:
	case CG_QL_IMPORT_RETAIL_RESERVED_66:
	case CG_QL_IMPORT_RETAIL_RESERVED_67:
	case CG_QL_IMPORT_RETAIL_RESERVED_68:
	case CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_69:
	case CG_QL_IMPORT_RETAIL_RESERVED_80:
	case CG_QL_IMPORT_RETAIL_RESERVED_112:
	case CG_QL_IMPORT_RETAIL_RESERVED_113:
	case CG_QL_IMPORT_RETAIL_RESERVED_117:
		return true;
	default:
		return false;
	}
}

/*
====================
CL_ValidateRetailCGameImportTable

Retail cgame's dllEntry retains the raw host-table pointer and calls it by
slot.  Fail the module load deterministically if a host edit leaves a retail
slot null or populates one of retail's deliberate null rows.
====================
*/
static bool CL_ValidateRetailCGameImportTable( void ) {
	static_assert( CG_QL_IMPORT_COUNT == 128,
		"retail cgame import slab length changed; revalidate native module ABI" );

	for ( int slot = 0; slot < CG_QL_IMPORT_COUNT; ++slot ) {
		const bool isNull = ql_cgame_imports[slot] == nullptr;
		const bool expectedNull = CL_IsExpectedRetailCGameNullImport( slot );

		if ( isNull != expectedNull ) {
			Com_Printf( "CL_InitCGameImports: retail cgame import slot %i is %s; expected %s\\n",
				slot, isNull ? "null" : "callable", expectedNull ? "null" : "callable" );
			return false;
		}

		if ( !isNull && CL_IsExpectedRetailCGameReservedFallbackImport( slot ) &&
			ql_cgame_imports[slot] != (ql_import_f)QL_CG_trap_RetailReservedImport ) {
			Com_Printf( "CL_InitCGameImports: retail cgame reserved import slot %i has no fallback\\n", slot );
			return false;
		}
	}

	return true;
}

static bool CL_InitCGameImports( void ) {
	Com_Memset( ql_cgame_imports, 0, sizeof( ql_cgame_imports ) );

	ql_cgame_currentColor[0] = 1.0f;
	ql_cgame_currentColor[1] = 1.0f;
	ql_cgame_currentColor[2] = 1.0f;
	ql_cgame_currentColor[3] = 1.0f;

	ql_cgame_imports[CG_QL_IMPORT_PRINT] = (ql_import_f)QL_CG_trap_Print;
	ql_cgame_imports[CG_QL_IMPORT_ERROR] = (ql_import_f)QL_CG_trap_Error;
	ql_cgame_imports[CG_QL_IMPORT_MILLISECONDS] = (ql_import_f)QL_CG_trap_Milliseconds;
	ql_cgame_imports[CG_QL_IMPORT_REAL_TIME] = (ql_import_f)QL_CG_trap_RealTime;
	ql_cgame_imports[CG_QL_IMPORT_CVAR_REGISTER] = (ql_import_f)QL_CG_trap_Cvar_Register;
	ql_cgame_imports[CG_QL_IMPORT_CVAR_REGISTER_RANGE] = (ql_import_f)QL_CG_trap_Cvar_RegisterRange;
	ql_cgame_imports[CG_QL_IMPORT_CVAR_UPDATE] = (ql_import_f)QL_CG_trap_Cvar_Update;
	ql_cgame_imports[CG_QL_IMPORT_CVAR_SET] = (ql_import_f)QL_CG_trap_Cvar_Set;
	ql_cgame_imports[CG_QL_IMPORT_CVAR_SET_VALUE] = (ql_import_f)QL_CG_trap_Cvar_SetValue;
	ql_cgame_imports[CG_QL_IMPORT_CVAR_VARIABLESTRINGBUFFER] = (ql_import_f)QL_CG_trap_Cvar_VariableStringBuffer;
	ql_cgame_imports[CG_QL_IMPORT_CVAR_VARIABLEVALUE] = (ql_import_f)QL_CG_trap_Cvar_VariableValue;
	ql_cgame_imports[CG_QL_IMPORT_ARGC] = (ql_import_f)QL_CG_trap_Argc;
	ql_cgame_imports[CG_QL_IMPORT_ARGV] = (ql_import_f)QL_CG_trap_Argv;
	ql_cgame_imports[CG_QL_IMPORT_ARGS] = (ql_import_f)QL_CG_trap_Args;
	ql_cgame_imports[CG_QL_IMPORT_FS_FOPENFILE] = (ql_import_f)QL_CG_trap_FS_FOpenFile;
	ql_cgame_imports[CG_QL_IMPORT_FS_READ] = (ql_import_f)QL_CG_trap_FS_Read;
	ql_cgame_imports[CG_QL_IMPORT_FS_WRITE] = (ql_import_f)QL_CG_trap_FS_Write;
	ql_cgame_imports[CG_QL_IMPORT_FS_FCLOSEFILE] = (ql_import_f)QL_CG_trap_FS_FCloseFile;
	ql_cgame_imports[CG_QL_IMPORT_FS_SEEK] = (ql_import_f)QL_CG_trap_FS_Seek;
	ql_cgame_imports[CG_QL_IMPORT_FS_GETFILELIST] = (ql_import_f)QL_CG_trap_FS_GetFileList;
	ql_cgame_imports[CG_QL_IMPORT_SENDCONSOLECOMMAND] = (ql_import_f)QL_CG_trap_SendConsoleCommand;
	ql_cgame_imports[CG_QL_IMPORT_ADDCOMMAND] = (ql_import_f)QL_CG_trap_AddCommand;
	ql_cgame_imports[CG_QL_IMPORT_REMOVECOMMAND] = (ql_import_f)QL_CG_trap_RemoveCommand;
	ql_cgame_imports[CG_QL_IMPORT_SENDCLIENTCOMMAND] = (ql_import_f)QL_CG_trap_SendClientCommand;
	ql_cgame_imports[CG_QL_IMPORT_UPDATESCREEN] = (ql_import_f)QL_CG_trap_UpdateScreen;
	ql_cgame_imports[CG_QL_IMPORT_CM_LOADMAP] = (ql_import_f)QL_CG_trap_CM_LoadMap;
	ql_cgame_imports[CG_QL_IMPORT_CM_NUMINLINEMODELS] = (ql_import_f)QL_CG_trap_CM_NumInlineModels;
	ql_cgame_imports[CG_QL_IMPORT_CM_INLINEMODEL] = (ql_import_f)QL_CG_trap_CM_InlineModel;
	ql_cgame_imports[CG_QL_IMPORT_CM_TEMPBOXMODEL] = (ql_import_f)QL_CG_trap_CM_TempBoxModel;
	ql_cgame_imports[CG_QL_IMPORT_CM_TEMPCAPSULEMODEL] = (ql_import_f)QL_CG_trap_CM_TempCapsuleModel;
	ql_cgame_imports[CG_QL_IMPORT_CM_POINTCONTENTS] = (ql_import_f)QL_CG_trap_CM_PointContents;
	ql_cgame_imports[CG_QL_IMPORT_CM_TRANSFORMEDPOINTCONTENTS] = (ql_import_f)QL_CG_trap_CM_TransformedPointContents;
	ql_cgame_imports[CG_QL_IMPORT_CM_BOXTRACE] = (ql_import_f)QL_CG_trap_CM_BoxTrace;
	ql_cgame_imports[CG_QL_IMPORT_CM_CAPSULETRACE] = (ql_import_f)QL_CG_trap_CM_CapsuleTrace;
	ql_cgame_imports[CG_QL_IMPORT_CM_TRANSFORMEDBOXTRACE] = (ql_import_f)QL_CG_trap_CM_TransformedBoxTrace;
	ql_cgame_imports[CG_QL_IMPORT_CM_TRANSFORMEDCAPSULETRACE] = (ql_import_f)QL_CG_trap_CM_TransformedCapsuleTrace;
	ql_cgame_imports[CG_QL_IMPORT_CM_MARKFRAGMENTS] = (ql_import_f)QL_CG_trap_CM_MarkFragments;
	ql_cgame_imports[CG_QL_IMPORT_S_STARTSOUND] = (ql_import_f)QL_CG_trap_S_StartSound;
	ql_cgame_imports[CG_QL_IMPORT_S_STARTSOUND_VOLUME] = (ql_import_f)QL_CG_trap_S_StartSoundVolume;
	ql_cgame_imports[CG_QL_IMPORT_S_STARTLOCALSOUND] = (ql_import_f)QL_CG_trap_S_StartLocalSound;
	ql_cgame_imports[CG_QL_IMPORT_S_STARTLOCALSOUND_VOLUME] = (ql_import_f)QL_CG_trap_S_StartLocalSoundVolume;
	ql_cgame_imports[CG_QL_IMPORT_S_CLEARLOOPINGSOUNDS_FRAME] = (ql_import_f)QL_CG_trap_S_ClearLoopingSoundsFrame;
	ql_cgame_imports[CG_QL_IMPORT_S_CLEARLOOPINGSOUNDS_KILLALL] = (ql_import_f)QL_CG_trap_S_ClearLoopingSoundsKillAll;
	ql_cgame_imports[CG_QL_IMPORT_S_ADDLOOPINGSOUND] = (ql_import_f)QL_CG_trap_S_AddLoopingSound;
	ql_cgame_imports[CG_QL_IMPORT_S_UPDATEENTITYPOSITION] = (ql_import_f)QL_CG_trap_S_UpdateEntityPosition;
	ql_cgame_imports[CG_QL_IMPORT_S_RESPATIALIZE] = (ql_import_f)QL_CG_trap_S_Respatialize;
	ql_cgame_imports[CG_QL_IMPORT_S_REGISTERSOUND] = (ql_import_f)QL_CG_trap_S_RegisterSound;
	ql_cgame_imports[CG_QL_IMPORT_S_STARTBACKGROUNDTRACK] = (ql_import_f)QL_CG_trap_S_StartBackgroundTrack;
	ql_cgame_imports[CG_QL_IMPORT_S_STOPBACKGROUNDTRACK] = (ql_import_f)QL_CG_trap_S_StopBackgroundTrack;
	ql_cgame_imports[CG_QL_IMPORT_R_LOADWORLDMAP] = (ql_import_f)QL_CG_trap_R_LoadWorldMap;
	ql_cgame_imports[CG_QL_IMPORT_R_REGISTERMODEL] = (ql_import_f)QL_CG_trap_R_RegisterModel;
	ql_cgame_imports[CG_QL_IMPORT_R_REGISTERSKIN] = (ql_import_f)QL_CG_trap_R_RegisterSkin;
	ql_cgame_imports[CG_QL_IMPORT_R_REGISTERSHADER] = (ql_import_f)QL_CG_trap_R_RegisterShader;
	ql_cgame_imports[CG_QL_IMPORT_R_REGISTERSHADERNOMIP] = (ql_import_f)QL_CG_trap_R_RegisterShaderNoMip;
	ql_cgame_imports[CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_21C0] = (ql_import_f)QL_CG_trap_AdvertisementBridge_Reserved21C0;
	ql_cgame_imports[CG_QL_IMPORT_SETUP_ADVERT_CELL_SHADER] = (ql_import_f)QL_CG_trap_SetupAdvertCellShader;
	ql_cgame_imports[CG_QL_IMPORT_REFRESH_ADVERT_CELL_SHADER] = (ql_import_f)QL_CG_trap_RefreshAdvertCellShader;
	ql_cgame_imports[CG_QL_IMPORT_SET_ACTIVE_ADVERT] = (ql_import_f)QL_CG_trap_SetActiveAdvert;
	ql_cgame_imports[CG_QL_IMPORT_UPDATE_ADVERT] = (ql_import_f)QL_CG_trap_UpdateAdvert;
	ql_cgame_imports[CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_59] = (ql_import_f)QL_CG_trap_RetailReservedImport;
	ql_cgame_imports[CG_QL_IMPORT_ADVERTISEMENTBRIDGE_SET_MAP_PATH] = (ql_import_f)QL_CG_trap_AdvertisementBridge_SetMapPath;
	ql_cgame_imports[CG_QL_IMPORT_ADVERTISEMENTBRIDGE_INITCGAME] = (ql_import_f)QL_CG_trap_AdvertisementBridge_InitCGame;
	ql_cgame_imports[CG_QL_IMPORT_ADVERTISEMENTBRIDGE_SHUTDOWNCGAME] = (ql_import_f)QL_CG_trap_AdvertisementBridge_ShutdownCGame;
	ql_cgame_imports[CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_63] = (ql_import_f)QL_CG_trap_RetailReservedImport;
	ql_cgame_imports[CG_QL_IMPORT_ADVERTISEMENTBRIDGE_SETFRAMETIME] = (ql_import_f)QL_CG_trap_AdvertisementBridge_SetFrameTime;
	ql_cgame_imports[CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_65] = (ql_import_f)QL_CG_trap_RetailReservedImport;
	ql_cgame_imports[CG_QL_IMPORT_RETAIL_RESERVED_66] = (ql_import_f)QL_CG_trap_RetailReservedImport;
	ql_cgame_imports[CG_QL_IMPORT_RETAIL_RESERVED_67] = (ql_import_f)QL_CG_trap_RetailReservedImport;
	ql_cgame_imports[CG_QL_IMPORT_RETAIL_RESERVED_68] = (ql_import_f)QL_CG_trap_RetailReservedImport;
	ql_cgame_imports[CG_QL_IMPORT_ADVERTISEMENTBRIDGE_RESERVED_69] = (ql_import_f)QL_CG_trap_RetailReservedImport;
	ql_cgame_imports[CG_QL_IMPORT_R_CLEARSCENE] = (ql_import_f)QL_CG_trap_R_ClearScene;
	ql_cgame_imports[CG_QL_IMPORT_R_ADDREFENTITYTOSCENE] = (ql_import_f)QL_CG_trap_R_AddRefEntityToScene;
	ql_cgame_imports[CG_QL_IMPORT_R_ADDPOLYTOSCENE] = (ql_import_f)QL_CG_trap_R_AddPolyToScene;
	ql_cgame_imports[CG_QL_IMPORT_R_ADDPOLYSTOSCENE] = (ql_import_f)QL_CG_trap_R_AddPolysToScene;
	ql_cgame_imports[CG_QL_IMPORT_R_ADDLIGHTTOSCENE] = (ql_import_f)QL_CG_trap_R_AddLightToScene;
	ql_cgame_imports[CG_QL_IMPORT_R_LIGHTFORPOINT] = (ql_import_f)QL_CG_trap_R_LightForPoint;
	ql_cgame_imports[CG_QL_IMPORT_R_RENDERSCENE] = (ql_import_f)QL_CG_trap_R_RenderScene;
	ql_cgame_imports[CG_QL_IMPORT_ADVERTISEMENTBRIDGE_UPDATE_LOADING_VIEW_PARAMETERS] = (ql_import_f)QL_CG_trap_AdvertisementBridge_UpdateLoadingViewParameters;
	ql_cgame_imports[CG_QL_IMPORT_R_SETCOLOR] = (ql_import_f)QL_CG_trap_R_SetColor_QL;
	ql_cgame_imports[CG_QL_IMPORT_R_DRAWSTRETCHPIC] = (ql_import_f)QL_CG_trap_R_DrawStretchPic;
	ql_cgame_imports[CG_QL_IMPORT_RETAIL_RESERVED_80] = (ql_import_f)QL_CG_trap_RetailReservedImport;
	ql_cgame_imports[CG_QL_IMPORT_R_MODELBOUNDS] = (ql_import_f)QL_CG_trap_R_ModelBounds;
	ql_cgame_imports[CG_QL_IMPORT_R_LERPTAG] = (ql_import_f)QL_CG_trap_R_LerpTag;
	ql_cgame_imports[CG_QL_IMPORT_R_REMAP_SHADER] = (ql_import_f)QL_CG_trap_R_RemapShader;
	ql_cgame_imports[CG_QL_IMPORT_GETGLCONFIG] = (ql_import_f)QL_CG_trap_GetGlconfig;
	ql_cgame_imports[CG_QL_IMPORT_GETGAMESTATE] = (ql_import_f)QL_CG_trap_GetGameState;
	ql_cgame_imports[CG_QL_IMPORT_GETCURRENTSNAPSHOTNUMBER] = (ql_import_f)QL_CG_trap_GetCurrentSnapshotNumber;
	ql_cgame_imports[CG_QL_IMPORT_GETSNAPSHOT] = (ql_import_f)QL_CG_trap_GetSnapshot;
	ql_cgame_imports[CG_QL_IMPORT_GETSERVERCOMMAND] = (ql_import_f)QL_CG_trap_GetServerCommand;
	ql_cgame_imports[CG_QL_IMPORT_GETCURRENTCMDNUMBER] = (ql_import_f)QL_CG_trap_GetCurrentCmdNumber;
	ql_cgame_imports[CG_QL_IMPORT_GETUSERCMD] = (ql_import_f)QL_CG_trap_GetUserCmd;
	ql_cgame_imports[CG_QL_IMPORT_SETUSERCMDVALUE] = (ql_import_f)QL_CG_trap_SetUserCmdValue;
	ql_cgame_imports[CG_QL_IMPORT_MEMORY_REMAINING] = (ql_import_f)QL_CG_trap_MemoryRemaining;
	ql_cgame_imports[CG_QL_IMPORT_R_REGISTERFONT] = (ql_import_f)QL_CG_trap_R_RegisterFont;
	ql_cgame_imports[CG_QL_IMPORT_KEY_ISDOWN] = (ql_import_f)QL_CG_trap_Key_IsDown;
	ql_cgame_imports[CG_QL_IMPORT_KEY_GETCATCHER] = (ql_import_f)QL_CG_trap_Key_GetCatcher;
	ql_cgame_imports[CG_QL_IMPORT_KEY_SETCATCHER] = (ql_import_f)QL_CG_trap_Key_SetCatcher;
	ql_cgame_imports[CG_QL_IMPORT_KEY_GETKEY] = (ql_import_f)QL_CG_trap_Key_GetKey;
	ql_cgame_imports[CG_QL_IMPORT_KEY_KEYNUMTOSTRINGBUF] = (ql_import_f)QL_CG_trap_Key_KeynumToStringBuf;
	ql_cgame_imports[CG_QL_IMPORT_KEY_GETBINDINGBUF] = (ql_import_f)QL_CG_trap_Key_GetBindingBuf;
	ql_cgame_imports[CG_QL_IMPORT_CIN_PLAYCINEMATIC] = (ql_import_f)QL_CG_trap_CIN_PlayCinematic;
	ql_cgame_imports[CG_QL_IMPORT_CIN_STOPCINEMATIC] = (ql_import_f)QL_CG_trap_CIN_StopCinematic;
	ql_cgame_imports[CG_QL_IMPORT_CIN_RUNCINEMATIC] = (ql_import_f)QL_CG_trap_CIN_RunCinematic;
	ql_cgame_imports[CG_QL_IMPORT_CIN_DRAWCINEMATIC] = (ql_import_f)QL_CG_trap_CIN_DrawCinematic;
	ql_cgame_imports[CG_QL_IMPORT_CIN_SETEXTENTS] = (ql_import_f)QL_CG_trap_CIN_SetExtents;
	ql_cgame_imports[CG_QL_IMPORT_GET_ENTITY_TOKEN] = (ql_import_f)QL_CG_trap_GetEntityToken;
	ql_cgame_imports[CG_QL_IMPORT_PC_ADD_GLOBAL_DEFINE] = (ql_import_f)QL_CG_trap_PC_AddGlobalDefine;
	ql_cgame_imports[CG_QL_IMPORT_PC_LOAD_SOURCE] = (ql_import_f)QL_CG_trap_PC_LoadSource;
	ql_cgame_imports[CG_QL_IMPORT_PC_FREE_SOURCE] = (ql_import_f)QL_CG_trap_PC_FreeSource;
	ql_cgame_imports[CG_QL_IMPORT_PC_READ_TOKEN] = (ql_import_f)QL_CG_trap_PC_ReadToken;
	ql_cgame_imports[CG_QL_IMPORT_PC_SOURCE_FILE_AND_LINE] = (ql_import_f)QL_CG_trap_PC_SourceFileAndLine;
	ql_cgame_imports[CG_QL_IMPORT_RETAIL_RESERVED_112] = (ql_import_f)QL_CG_trap_RetailReservedImport;
	ql_cgame_imports[CG_QL_IMPORT_RETAIL_RESERVED_113] = (ql_import_f)QL_CG_trap_RetailReservedImport;
	ql_cgame_imports[CG_QL_IMPORT_ADVERTISEMENTBRIDGE_UPDATE_VIEW_PARAMETERS] = (ql_import_f)QL_CG_trap_AdvertisementBridge_UpdateViewParameters;
	ql_cgame_imports[CG_QL_IMPORT_ADVERTISEMENTBRIDGE_CLEAR_DELAY] = (ql_import_f)QL_CG_trap_AdvertisementBridge_ClearDelay;
	ql_cgame_imports[CG_QL_IMPORT_PUBLISH_TAGGED_INFO_STRING] = (ql_import_f)QL_CG_trap_PublishTaggedInfoString;
	ql_cgame_imports[CG_QL_IMPORT_RETAIL_RESERVED_117] = (ql_import_f)QL_CG_trap_RetailReservedImport;
	ql_cgame_imports[CG_QL_IMPORT_R_MIRROR_POINT] = (ql_import_f)QL_CG_trap_R_MirrorPoint;
	ql_cgame_imports[CG_QL_IMPORT_R_MIRROR_VECTOR] = (ql_import_f)QL_CG_trap_R_MirrorVector;
	ql_cgame_imports[CG_QL_IMPORT_IS_SUBSCRIBED_APP] = (ql_import_f)QL_CG_trap_IsSubscribedApp;
	ql_cgame_imports[CG_QL_IMPORT_DRAW_SCALED_TEXT] = (ql_import_f)QL_CG_trap_DrawScaledText;
	ql_cgame_imports[CG_QL_IMPORT_MEASURE_TEXT] = (ql_import_f)QL_CG_trap_MeasureText;
	ql_cgame_imports[CG_QL_IMPORT_IS_CLIENT_MUTED] = (ql_import_f)QL_CG_trap_IsClientMuted;
	ql_cgame_imports[CG_QL_IMPORT_TOGGLE_CLIENT_MUTE] = (ql_import_f)QL_CG_trap_ToggleClientMute;
	ql_cgame_imports[CG_QL_IMPORT_GET_AVATAR_IMAGE_HANDLE] = (ql_import_f)QL_CG_trap_GetAvatarImageHandle;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_CM_LOADMODEL] = (ql_import_f)QL_CG_trap_CM_LoadModel;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_S_ADDREALLOOPINGSOUND] = (ql_import_f)QL_CG_trap_S_AddRealLoopingSound;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_S_STOPLOOPINGSOUND] = (ql_import_f)QL_CG_trap_S_StopLoopingSound;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_R_ADDADDITIVELIGHTTOSCENE] = (ql_import_f)QL_CG_trap_R_AddAdditiveLightToScene;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_R_INPVS] = (ql_import_f)QL_CG_trap_R_inPVS;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_KEY_SETBINDING] = (ql_import_f)QL_CG_trap_Key_SetBinding;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_KEY_GETOVERSTRIKEMODE] = (ql_import_f)QL_CG_trap_Key_GetOverstrikeMode;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_KEY_SETOVERSTRIKEMODE] = (ql_import_f)QL_CG_trap_Key_SetOverstrikeMode;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_CMD_EXECUTETEXT] = (ql_import_f)QL_CG_trap_Cmd_ExecuteText;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_PC_ADD_GLOBAL_DEFINE] = (ql_import_f)QL_CG_trap_PC_AddGlobalDefine;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_REAL_TIME] = (ql_import_f)QL_CG_trap_RealTime;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_SNAPVECTOR] = (ql_import_f)QL_CG_trap_SnapVector;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_MEMSET] = (ql_import_f)QL_CG_trap_Memset;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_MEMCPY] = (ql_import_f)QL_CG_trap_Memcpy;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_STRNCPY] = (ql_import_f)QL_CG_trap_Strncpy;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_SIN] = (ql_import_f)QL_CG_trap_Sin;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_COS] = (ql_import_f)QL_CG_trap_Cos;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_ATAN2] = (ql_import_f)QL_CG_trap_Atan2;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_SQRT] = (ql_import_f)QL_CG_trap_Sqrt;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_FLOOR] = (ql_import_f)QL_CG_trap_Floor;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_CEIL] = (ql_import_f)QL_CG_trap_Ceil;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_TESTPRINTINT] = (ql_import_f)QL_CG_trap_TestPrintInt;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_TESTPRINTFLOAT] = (ql_import_f)QL_CG_trap_TestPrintFloat;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_ACOS] = (ql_import_f)QL_CG_trap_ACos;
	ql_cgame_imports[CG_QL_IMPORT_COMPAT_CVAR_RESET] = (ql_import_f)QL_CG_trap_Cvar_Reset;

	return CL_ValidateRetailCGameImportTable();
}

/*
====================
CL_CheckCGameNativeImportIntegrity

Checks the native cgame import slots guarded by retail before cgame frame work.
====================
*/
void CL_CheckCGameNativeImportIntegrity( void ) {
	if ( !cgvm || !cgvm->dllExports ) {
		return;
	}

	if ( ql_cgame_imports[CG_QL_IMPORT_R_ADDREFENTITYTOSCENE] != (ql_import_f)QL_CG_trap_R_AddRefEntityToScene ||
		ql_cgame_imports[CG_QL_IMPORT_R_RENDERSCENE] != (ql_import_f)QL_CG_trap_R_RenderScene ) {
		CL_SetRetailClientMessageCGameImportGuardFlag();
	}
}

/*
====================
CL_LoadCGameVM

Attempts to load cgame, preferring the retail native import-table bridge when
that path is allowed, while preserving explicit bytecode/compiled fallback.
====================
*/
static vm_t *CL_LoadCGameVM( vmInterpret_t interpret ) {
	vm_t	*vm;

	if ( !CL_InitCGameImports() ) {
		Com_Printf( S_COLOR_RED "Native cgame import table validation failed.\\n" );
		return nullptr;
	}

	if ( interpret != VMI_COMPILED &&
		( !cl_connectedToPureServer || interpret == VMI_PINNED_NATIVE ) ) {
		vm = VM_CreateNative( VM_CGAME, CL_CgameSystemCalls, CL_DllSyscall,
			interpret == VMI_PINNED_NATIVE ? VMI_PINNED_NATIVE : VMI_NATIVE,
			ql_cgame_imports, CGAME_NATIVE_API_VERSION );
		if ( vm ) {
			if ( vm->dllHandle || interpret != VMI_BYTECODE || !vm->compiled ) {
				return vm;
			}

			VM_Free( vm );
			FS_VM_CloseFiles( H_CGAME );
		}
	}

	return VM_CreateNative( VM_CGAME, CL_CgameSystemCalls, CL_DllSyscall,
		interpret, ql_cgame_imports, CGAME_NATIVE_API_VERSION );
}

/*
====================
CL_RegisterCGameCvars

Calls the retail native cgame startup cvar export without running CG_Init.
====================
*/
void CL_RegisterCGameCvars( void ) {
	vm_t			*registrationVm;
	vm_t			*oldCgvm;
	vmInterpret_t	interpret;

	if ( cgvm || ( com_dedicated && com_dedicated->integer ) ) {
		return;
	}

	interpret = static_cast<vmInterpret_t>( Cvar_VariableIntegerValue( "vm_cgame" ) );
	registrationVm = CL_LoadCGameVM( interpret );
	if ( !registrationVm ) {
		return;
	}

	oldCgvm = cgvm;
	cgvm = registrationVm;
	(void)VM_CallCGameRegisterCvars( registrationVm );
	cgvm = oldCgvm;

	VM_Free( registrationVm );
	FS_VM_CloseFiles( H_CGAME );
}


/*
====================
CL_InitCGame

Should only be called by CL_StartHunkUsers
====================
*/
void CL_InitCGame( void ) {
	const char			*info;
	const char			*mapname;
	int					t1, t2;
	vmInterpret_t		interpret;

	Cbuf_NestedReset();
	CL_ClearEnemyHighlightState();
	CL_ClearLiquidInteractionState();

	t1 = Sys_Milliseconds();

	// put away the console
	Con_Close();

	// find the current mapname
	info = cl.gameState.stringData + cl.gameState.stringOffsets[ CS_SERVERINFO ];
	mapname = Info_ValueForKey( info, "mapname" );
	Com_sprintf( cl.mapname, sizeof( cl.mapname ), "maps/%s.bsp", mapname );

	// allow vertex lighting for in-game elements
	re.VertexLighting( qtrue );

	// load the dll or bytecode
	interpret = static_cast<vmInterpret_t>( Cvar_VariableIntegerValue( "vm_cgame" ) );
	if ( cl_connectedToPureServer )
	{
		// Retail QL has no cgame QVM. Permit only the exact native image
		// pinned before the server's pure filesystem policy took effect.
		if ( interpret != VMI_COMPILED && interpret != VMI_BYTECODE ) {
			interpret = VM_HasPinnedNativeModule( VM_CGAME )
				? VMI_PINNED_NATIVE : VMI_COMPILED;
		}
	}

	cgvm = CL_LoadCGameVM( interpret );
	if ( !cgvm ) {
		Com_Error( ERR_DROP, "VM_Create on cgame failed" );
	}
	cls.state = CA_LOADING;

	// init for this gamestate
	// use the lastExecutedServerCommand instead of the serverCommandSequence
	// otherwise server commands sent just before a gamestate are dropped
	VM_Call( cgvm, 3, CG_INIT, clc.serverMessageSequence, clc.lastExecutedServerCommand, clc.clientNum );

	// reset any CVAR_CHEAT cvars registered by cgame
	if ( !clc.demoplaying && !cl_connectedToCheatServer )
		Cvar_SetCheatState();

	// we will send a usercmd this frame, which
	// will cause the server to send us the first snapshot
	cls.state = CA_PRIMED;

	t2 = Sys_Milliseconds();

	Com_Printf( "CL_InitCGame: %5.2f seconds\n", (t2-t1)/1000.0 );

	// have the renderer touch all its images, so they are present
	// on the card even if the driver does deferred loading
	re.EndRegistration();

	// make sure everything is paged in
	if (!Sys_LowPhysicalMemory()) {
		Com_TouchMemory();
	}

	// clear anything that got printed
	Con_ClearNotify ();

	// do not allow vid_restart for first time
	cls.lastVidRestart = Sys_Milliseconds();
}


/*
====================
CL_GameCommand

See if the current console command is claimed by the cgame
====================
*/

qboolean CL_GameCommand( void ) {
	bool bRes;

	if ( !cgvm ) {
		return qfalse;
	}

	bRes = VM_Call( cgvm, 0, CG_CONSOLE_COMMAND ) != 0;

	Cbuf_NestedReset();

	return ToQboolean( bRes );
}


/*
=====================
CL_CGameRendering
=====================
*/
void CL_CGameRendering( stereoFrame_t stereo ) {
	VM_Call( cgvm, 3, CG_DRAW_ACTIVE_FRAME, cl.serverTime, stereo, clc.demoplaying );
#ifdef DEBUG
	VM_Debug( 0 );
#endif
}


/*
=================
CL_AdjustTimeDelta

Adjust the clients view of server time.

We attempt to have cl.serverTime exactly equal the server's view
of time plus the timeNudge, but with variable latencies over
the internet it will often need to drift a bit to match conditions.

Our ideal time would be to have the adjusted time approach, but not pass,
the very latest snapshot.

Adjustments are only made when a new snapshot arrives with a rational
latency, which keeps the adjustment process framerate independent and
prevents massive overadjustment during times of significant packet loss
or bursted delayed packets.
=================
*/

#define	RESET_TIME	500

static void CL_AdjustTimeDelta( void ) {
	int		newDelta;
	int		deltaDelta;

	cl.newSnapshots = qfalse;

	// the delta never drifts when replaying a demo
	if ( clc.demoplaying ) {
		return;
	}

	newDelta = cl.snap.serverTime - cls.gametime;
	deltaDelta = abs( newDelta - cl.serverTimeDelta );

	if ( deltaDelta > RESET_TIME ) {
		cl.serverTimeDelta = newDelta;
		cl.oldServerTime = cl.snap.serverTime;	// FIXME: is this a problem for cgame?
		cl.serverTime = cl.snap.serverTime;
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<RESET> " );
		}
	} else if ( deltaDelta > 100 ) {
		// fast adjust, cut the difference in half
		if ( cl_showTimeDelta->integer ) {
			Com_Printf( "<FAST> " );
		}
		cl.serverTimeDelta = ( cl.serverTimeDelta + newDelta ) >> 1;
	} else {
		// slow drift adjust, only move 1 or 2 msec

		// if any of the frames between this and the previous snapshot
		// had to be extrapolated, nudge our sense of time back a little
		// the granularity of +1 / -2 is too high for timescale-modified world frames
		if ( com_timescale->value == 1 ) {
			if ( cl.extrapolatedSnapshot ) {
				cl.extrapolatedSnapshot = qfalse;
				cl.serverTimeDelta -= 2;
			} else {
				// otherwise, move our sense of time forward to minimize total latency
				cl.serverTimeDelta++;
			}
		}
	}

	if ( cl_showTimeDelta->integer ) {
		Com_Printf( "%i ", cl.serverTimeDelta );
	}
}


/*
==================
CL_FirstSnapshot
==================
*/
static void CL_FirstSnapshot( void ) {
	// ignore snapshots that don't have entities
	if ( cl.snap.snapFlags & SNAPFLAG_NOT_ACTIVE ) {
		return;
	}
	cls.state = CA_ACTIVE;

	// clear old game so we will not switch back to old mod on disconnect
	CL_ResetOldGame();

	// set the timedelta so we are exactly on this first frame
	cl.serverTimeDelta = cl.snap.serverTime - cls.gametime;
	cl.oldServerTime = cl.snap.serverTime;

	clc.timeDemoBaseTime = cl.snap.serverTime;
	CL_WebView_PublishGameStart();

	// if this is the first frame of active play,
	// execute the contents of activeAction now
	// this is to allow scripting a timedemo to start right
	// after loading
	if ( cl_activeAction->string[0] ) {
		Cbuf_AddText( cl_activeAction->string );
		Cbuf_AddText( "\n" );
		Cvar_Set( "activeAction", "" );
	}

	Sys_BeginProfiling();
}


/*
==================
CL_AvgPing

Calculates Average Ping from snapshots in buffer. Used by AutoNudge.
==================
*/
static float CL_AvgPing( void ) {
	std::array<int, PACKET_BACKUP> ping;
	int count = 0;
	int i;

	for ( i = 0; i < PACKET_BACKUP; i++ ) {
		if ( cl.snapshots[i].ping > 0 && cl.snapshots[i].ping < 999 ) {
			ping[count] = cl.snapshots[i].ping;
			count++;
		}
	}

	if ( count == 0 )
		return 0;

	std::sort( ping.begin(), ping.begin() + count );

	// use median average ping
	if ( (count % 2) == 0 )
		return (ping[count / 2] + ping[(count / 2) - 1]) / 2.0f;

	return static_cast<float>( ping[count / 2] );
}


/*
==================
CL_TimeNudge

Returns either auto-nudge or cl_timeNudge value.
==================
*/
static int CL_TimeNudge( void ) {
	static int previousRetailAutomatic;
	fnql::client::cvars::TimeNudgeInputs inputs;
	inputs.spectating = Cvar_VariableIntegerValue( "cg_spectating" ) != 0;
	inputs.localServer = Sys_IsLANAddress( &clc.serverAddress ) != qfalse;
	inputs.retailAutomatic = cl_autoTimeNudge &&
		cl_autoTimeNudge->integer != 0;
	inputs.snapshotPing = cl.snap.ping;
	inputs.manual = cl_timeNudge ? cl_timeNudge->integer : 0;
	inputs.fnqlAutomaticFactor = cl_autoNudge ? cl_autoNudge->value : 0.0f;
	if ( inputs.fnqlAutomaticFactor > 0.0f ) {
		inputs.fnqlAveragePing = CL_AvgPing();
	}
	return fnql::client::cvars::SelectTimeNudge(
		inputs, previousRetailAutomatic );
}


/*
==================
CL_SetCGameTime
==================
*/
void CL_SetCGameTime( void ) {
	bool demoFreezed;

	// getting a valid frame message ends the connection process
	if ( cls.state != CA_ACTIVE ) {
		if ( cls.state != CA_PRIMED ) {
			return;
		}
		if ( clc.demoplaying ) {
			// we shouldn't get the first snapshot on the same frame
			// as the gamestate, because it causes a bad time skip
			if ( !clc.firstDemoFrameSkipped ) {
				clc.firstDemoFrameSkipped = qtrue;
				return;
			}
			CL_ReadDemoMessage();
		}
		if ( cl.newSnapshots ) {
			cl.newSnapshots = qfalse;
			CL_FirstSnapshot();
		}
		if ( cls.state != CA_ACTIVE ) {
			return;
		}
	}

	// if we have gotten to this point, cl.snap is guaranteed to be valid
	if ( !cl.snap.valid ) {
		Com_Error( ERR_DROP, "CL_SetCGameTime: !cl.snap.valid" );
	}

	// allow pause in single player
	if ( sv_paused->integer && CL_CheckPaused() && com_sv_running->integer ) {
		// paused
		return;
	}

	if ( cl.snap.serverTime - cl.oldFrameServerTime < 0 ) {
		Com_Error( ERR_DROP, "cl.snap.serverTime < cl.oldFrameServerTime" );
	}
	cl.oldFrameServerTime = cl.snap.serverTime;

	// get our current view of time
	demoFreezed = clc.demoplaying && ( ( cl_freezeDemo && cl_freezeDemo->integer ) || com_timescale->value == 0.0f );
	if ( demoFreezed ) {
		// cl_freezeDemo and \timescale 0 lock a demo in place for single frame advances.
		cl.serverTimeDelta -= cls.gameFrametime;
	} else {
		// cl_timeNudge is a user adjustable cvar that allows more
		// or less latency to be added in the interest of better
		// smoothness or better responsiveness.
		cl.serverTime = cls.gametime + cl.serverTimeDelta - CL_TimeNudge();

		// guarantee that time will never flow backwards, even if
		// serverTimeDelta made an adjustment or cl_timeNudge was changed
		if ( cl.serverTime - cl.oldServerTime < 0 ) {
			cl.serverTime = cl.oldServerTime;
		}
		cl.oldServerTime = cl.serverTime;

		// note if we are almost past the latest frame (without timeNudge),
		// so we will try and adjust back a bit when the next snapshot arrives
		//if ( cls.gametime + cl.serverTimeDelta >= cl.snap.serverTime - 5 ) {
		if ( cls.gametime + cl.serverTimeDelta - cl.snap.serverTime >= -5 ) {
			cl.extrapolatedSnapshot = qtrue;
		}
	}

	// if we have gotten new snapshots, drift serverTimeDelta
	// don't do this every frame, or a period of packet loss would
	// make a huge adjustment
	if ( cl.newSnapshots ) {
		CL_AdjustTimeDelta();
	}

	if ( !clc.demoplaying ) {
		return;
	}

	// if we are playing a demo back, we can just keep reading
	// messages from the demo file until the cgame definitely
	// has valid snapshots to interpolate between

	// a timedemo will always use a deterministic set of time samples
	// no matter what speed machine it is run on,
	// while a normal demo may have different time samples
	// each time it is played back
	if ( com_timedemo->integer ) {
		if ( !clc.timeDemoStart ) {
			clc.timeDemoStart = Sys_Milliseconds();
		}
		clc.timeDemoFrames++;
		cl.serverTime = clc.timeDemoBaseTime + clc.timeDemoFrames * 50;
	}

	//while ( cl.serverTime >= cl.snap.serverTime ) {
	while ( cl.serverTime - cl.snap.serverTime >= 0 ) {
		// feed another message, which should change
		// the contents of cl.snap
		CL_ReadDemoMessage();
		if ( cls.state != CA_ACTIVE ) {
			return; // end of demo
		}
	}
}
