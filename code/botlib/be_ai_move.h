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

#pragma once

/*****************************************************************************
 * name:		be_ai_move.h
 *
 * desc:		movement AI
 *
 * $Archive: /source/code/botlib/be_ai_move.h $
 *
 *****************************************************************************/

//movement types
static constexpr int MOVE_WALK = 1;
static constexpr int MOVE_CROUCH = 2;
static constexpr int MOVE_JUMP = 4;
static constexpr int MOVE_GRAPPLE = 8;
static constexpr int MOVE_ROCKETJUMP = 16;
static constexpr int MOVE_BFGJUMP = 32;
//move flags
static constexpr int MFL_BARRIERJUMP = 1;		//bot is performing a barrier jump
static constexpr int MFL_ONGROUND = 2;			//bot is in the ground
static constexpr int MFL_SWIMMING = 4;			//bot is swimming
static constexpr int MFL_AGAINSTLADDER = 8;		//bot is against a ladder
static constexpr int MFL_WATERJUMP = 16;		//bot is waterjumping
static constexpr int MFL_TELEPORTED = 32;		//bot is being teleported
static constexpr int MFL_GRAPPLEPULL = 64;		//bot is being pulled by the grapple
static constexpr int MFL_ACTIVEGRAPPLE = 128;	//bot is using the grapple hook
static constexpr int MFL_GRAPPLERESET = 256;	//bot has reset the grapple
static constexpr int MFL_WALK = 512;			//bot should walk slowly
// move result flags
static constexpr int MOVERESULT_MOVEMENTVIEW = 1;		//bot uses view for movement
static constexpr int MOVERESULT_SWIMVIEW = 2;			//bot uses view for swimming
static constexpr int MOVERESULT_WAITING = 4;			//bot is waiting for something
static constexpr int MOVERESULT_MOVEMENTVIEWSET = 8;	//bot has set the view in movement code
static constexpr int MOVERESULT_MOVEMENTWEAPON = 16;	//bot uses weapon for movement
static constexpr int MOVERESULT_ONTOPOFOBSTACLE = 32;	//bot is ontop of obstacle
static constexpr int MOVERESULT_ONTOPOF_FUNCBOB = 64;	//bot is ontop of a func_bobbing
static constexpr int MOVERESULT_ONTOPOF_ELEVATOR = 128;	//bot is ontop of an elevator (func_plat)
static constexpr int MOVERESULT_BLOCKEDBYAVOIDSPOT = 256;	//bot is blocked by an avoid spot
//
static constexpr int MAX_AVOIDREACH = 1;
static constexpr int MAX_AVOIDSPOTS = 32;
// avoid spot types
static constexpr int AVOID_CLEAR = 0;			//clear all avoid spots
static constexpr int AVOID_ALWAYS = 1;			//avoid always
static constexpr int AVOID_DONTBLOCK = 2;		//never totally block
// restult types
static constexpr int RESULTTYPE_ELEVATORUP = 1;			//elevator is up
static constexpr int RESULTTYPE_WAITFORFUNCBOBBING = 2;	//waiting for func bobbing to arrive
static constexpr int RESULTTYPE_BADGRAPPLEPATH = 4;		//grapple path is obstructed
static constexpr int RESULTTYPE_INSOLIDAREA = 8;		//stuck in solid area, this is bad

//structure used to initialize the movement state
//the or_moveflags MFL_ONGROUND, MFL_TELEPORTED and MFL_WATERJUMP come from the playerstate
struct bot_initmove_s
{
	vec3_t origin;				//origin of the bot
	vec3_t velocity;			//velocity of the bot
	vec3_t viewoffset;			//view offset
	int entitynum;				//entity number of the bot
	int client;					//client number of the bot
	float thinktime;			//time the bot thinks
	int presencetype;			//presencetype of the bot
	vec3_t viewangles;			//view angles of the bot
	int or_moveflags;			//values ored to the movement flags
};
using bot_initmove_t = bot_initmove_s;

//NOTE: the ideal_viewangles are only valid if MFL_MOVEMENTVIEW is set
struct bot_moveresult_s
{
	int failure;				//true if movement failed all together
	int type;					//failure or blocked type
	int blocked;				//true if blocked by an entity
	int blockentity;			//entity blocking the bot
	int traveltype;				//last executed travel type
	int flags;					//result flags
	int weapon;					//weapon used for movement
	vec3_t movedir;				//movement direction
	vec3_t ideal_viewangles;	//ideal viewangles for the movement
};
using bot_moveresult_t = bot_moveresult_s;

struct bot_avoidspot_s
{
	vec3_t origin;
	float radius;
	int type;
};
using bot_avoidspot_t = bot_avoidspot_s;

//resets the whole move state
void BotResetMoveState(int movestate);
//moves the bot to the given goal
void BotMoveToGoal(bot_moveresult_t *result, int movestate, bot_goal_t *goal, int travelflags);
//moves the bot in the specified direction using the specified type of movement
int BotMoveInDirection(int movestate, vec3_t dir, float speed, int type);
//reset avoid reachability
void BotResetAvoidReach(int movestate);
//resets the last avoid reachability
void BotResetLastAvoidReach(int movestate);
//returns a reachability area if the origin is in one
int BotReachabilityArea(vec3_t origin, int client);
//view target based on movement
int BotMovementViewTarget(int movestate, bot_goal_t *goal, int travelflags, float lookahead, vec3_t target);
//predict the position of a player based on movement towards a goal
int BotPredictVisiblePosition(vec3_t origin, int areanum, bot_goal_t *goal, int travelflags, vec3_t target);
//returns the handle of a newly allocated movestate
int BotAllocMoveState(void);
//frees the movestate with the given handle
void BotFreeMoveState(int handle);
//initialize movement state before performing any movement
void BotInitMoveState(int handle, bot_initmove_t *initmove);
//add a spot to avoid (if type == AVOID_CLEAR all spots are removed)
void BotAddAvoidSpot(int movestate, const vec3_t origin, float radius, int type);
//must be called every map change
void BotSetBrushModelTypes(void);
//setup movement AI
int BotSetupMoveAI(void);
//shutdown movement AI
void BotShutdownMoveAI(void);

