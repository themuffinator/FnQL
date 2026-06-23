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

/*****************************************************************************
 * name:		be_ai_weap.c
 *
 * desc:		weapon AI
 *
 * $Archive: /MissionPack/code/botlib/be_ai_weap.c $
 *
 *****************************************************************************/

#include "../qcommon/q_shared.h"
#include "l_libvar.h"
#include "l_log.h"
#include "l_memory.h"
#include "l_utils.h"
#include "l_script.h"
#include "l_precomp.h"
#include "l_struct.h"
#include "aasfile.h"
#include "botlib.h"
#include "be_aas.h"
#include "be_aas_funcs.h"
#include "be_interface.h"
#include "be_ai_weight.h"		//fuzzy weights
#include "be_ai_weap.h"

//#define DEBUG_AI_WEAP

//weapon definition
static const fielddef_t weaponinfo_fields[] =
{
{"number", offsetof(weaponinfo_t, number), FT_INT},						//weapon number
{"name", offsetof(weaponinfo_t, name), FT_STRING},							//name of the weapon
{"level", offsetof(weaponinfo_t, level), FT_INT},
{"model", offsetof(weaponinfo_t, model), FT_STRING},						//model of the weapon
{"weaponindex", offsetof(weaponinfo_t, weaponindex), FT_INT},			//index of weapon in inventory
{"flags", offsetof(weaponinfo_t, flags), FT_INT},							//special flags
{"projectile", offsetof(weaponinfo_t, projectile), FT_STRING},			//projectile used by the weapon
{"numprojectiles", offsetof(weaponinfo_t, numprojectiles), FT_INT},	//number of projectiles
{"hspread", offsetof(weaponinfo_t, hspread), FT_FLOAT},					//horizontal spread of projectiles (degrees from middle)
{"vspread", offsetof(weaponinfo_t, vspread), FT_FLOAT},					//vertical spread of projectiles (degrees from middle)
{"speed", offsetof(weaponinfo_t, speed), FT_FLOAT},						//speed of the projectile (0 = instant hit)
{"acceleration", offsetof(weaponinfo_t, acceleration), FT_FLOAT},		//"acceleration" * time (in seconds) + "speed" = projectile speed
{"recoil", offsetof(weaponinfo_t, recoil), FT_FLOAT|FT_ARRAY, 3},		//amount of recoil the player gets from the weapon
{"offset", offsetof(weaponinfo_t, offset), FT_FLOAT|FT_ARRAY, 3},		//projectile start offset relative to eye and view angles
{"angleoffset", offsetof(weaponinfo_t, angleoffset), FT_FLOAT|FT_ARRAY, 3},//offset of the shoot angles relative to the view angles
{"extrazvelocity", offsetof(weaponinfo_t, extrazvelocity), FT_FLOAT},//extra z velocity the projectile gets
{"ammoamount", offsetof(weaponinfo_t, ammoamount), FT_INT},				//ammo amount used per shot
{"ammoindex", offsetof(weaponinfo_t, ammoindex), FT_INT},				//index of ammo in inventory
{"activate", offsetof(weaponinfo_t, activate), FT_FLOAT},				//time it takes to select the weapon
{"reload", offsetof(weaponinfo_t, reload), FT_FLOAT},						//time it takes to reload the weapon
{"spinup", offsetof(weaponinfo_t, spinup), FT_FLOAT},						//time it takes before first shot
{"spindown", offsetof(weaponinfo_t, spindown), FT_FLOAT},				//time it takes before weapon stops firing
{nullptr, 0, 0, 0}
};

//projectile definition
static const fielddef_t projectileinfo_fields[] =
{
{"name", offsetof(projectileinfo_t, name), FT_STRING},					//name of the projectile
{"model", offsetof(projectileinfo_t, model), FT_STRING},					//model of the projectile
{"flags", offsetof(projectileinfo_t, flags), FT_INT},						//special flags
{"gravity", offsetof(projectileinfo_t, gravity), FT_FLOAT},				//amount of gravity applied to the projectile [0,1]
{"damage", offsetof(projectileinfo_t, damage), FT_INT},					//damage of the projectile
{"radius", offsetof(projectileinfo_t, radius), FT_FLOAT},				//radius of damage
{"visdamage", offsetof(projectileinfo_t, visdamage), FT_INT},			//damage of the projectile to visible entities
{"damagetype", offsetof(projectileinfo_t, damagetype), FT_INT},		//type of damage (combination of the DAMAGETYPE_? flags)
{"healthinc", offsetof(projectileinfo_t, healthinc), FT_INT},			//health increase the owner gets
{"push", offsetof(projectileinfo_t, push), FT_FLOAT},						//amount a player is pushed away from the projectile impact
{"detonation", offsetof(projectileinfo_t, detonation), FT_FLOAT},		//time before projectile explodes after fire pressed
{"bounce", offsetof(projectileinfo_t, bounce), FT_FLOAT},				//amount the projectile bounces
{"bouncefric", offsetof(projectileinfo_t, bouncefric), FT_FLOAT}, 	//amount the bounce decreases per bounce
{"bouncestop", offsetof(projectileinfo_t, bouncestop), FT_FLOAT},		//minimum bounce value before bouncing stops
//recursive projectile definition??
{nullptr, 0, 0, 0}
};

static structdef_t weaponinfo_struct =
{
	sizeof(weaponinfo_t), weaponinfo_fields
};
static structdef_t projectileinfo_struct =
{
	sizeof(projectileinfo_t), projectileinfo_fields
};

//weapon configuration: set of weapons with projectiles
struct weaponconfig_t
{
	int numweapons;
	int numprojectiles;
	projectileinfo_t *projectileinfo;
	weaponinfo_t *weaponinfo;
};

//the bot weapon state
struct bot_weaponstate_t
{
	weightconfig_t *weaponweightconfig;				//weapon weight configuration
	int *weaponweightindex;							//weapon weight index
};

static bot_weaponstate_t *botweaponstates[MAX_CLIENTS+1];
static weaponconfig_t *weaponconfig;

//========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//========================================================================
static int BotValidWeaponNumber(int weaponnum)
{
	if (weaponnum <= 0 || weaponnum > weaponconfig->numweapons)
	{
		botimport.Print(PRT_ERROR, "weapon number out of range\n");
		return qfalse;
	} //end if
	return qtrue;
} //end of the function BotValidWeaponNumber
//========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//========================================================================
static bot_weaponstate_t *BotWeaponStateFromHandle(int handle)
{
	if (handle <= 0 || handle > MAX_CLIENTS)
	{
		botimport.Print(PRT_FATAL, "weapon state handle %d out of range\n", handle);
		return nullptr;
	} //end if
	if (!botweaponstates[handle])
	{
		botimport.Print(PRT_FATAL, "invalid weapon state %d\n", handle);
		return nullptr;
	} //end if
	return botweaponstates[handle];
} //end of the function BotWeaponStateFromHandle
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
#ifdef DEBUG_AI_WEAP
static void DumpWeaponConfig(weaponconfig_t *wc)
{
	FILE *fp;
	int i;

	fp = Log_FileStruct();
	if (!fp) return;
	for (i = 0; i < wc->numprojectiles; i++)
	{
		WriteStructure(fp, &projectileinfo_struct, &wc->projectileinfo[i]);
		Log_Flush();
	} //end for
	for (i = 0; i < wc->numweapons; i++)
	{
		WriteStructure(fp, &weaponinfo_struct, &wc->weaponinfo[i]);
		Log_Flush();
	} //end for
} //end of the function DumpWeaponConfig
#endif //DEBUG_AI_WEAP
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
static weaponconfig_t *LoadWeaponConfig(const char *filename)
{
	int max_weaponinfo, max_projectileinfo;
	token_t token;
	char path[MAX_PATH];
	int i, j;
	source_t *source;
	weaponconfig_t *wc;
	weaponinfo_t weaponinfo;

	max_weaponinfo = (int) LibVarValue("max_weaponinfo", "32");
	if (max_weaponinfo < 0)
	{
		botimport.Print(PRT_ERROR, "max_weaponinfo = %d\n", max_weaponinfo);
		max_weaponinfo = 32;
		LibVarSet("max_weaponinfo", "32");
	} //end if
	max_projectileinfo = (int) LibVarValue("max_projectileinfo", "32");
	if (max_projectileinfo < 0)
	{
		botimport.Print(PRT_ERROR, "max_projectileinfo = %d\n", max_projectileinfo);
		max_projectileinfo = 32;
		LibVarSet("max_projectileinfo", "32");
	} //end if
	Q_strncpyz( path, filename, sizeof( path ) );
	PC_SetBaseFolder(BOTFILESBASEFOLDER);
	source = LoadSourceFile(path);
	if (!source)
	{
		botimport.Print(PRT_ERROR, "couldn't load %s\n", path);
		return nullptr;
	} //end if
	//initialize weapon config
	wc = botlib::ClearedHunkAllocBytes<weaponconfig_t>(sizeof(weaponconfig_t) +
										max_weaponinfo * sizeof(weaponinfo_t) +
										max_projectileinfo * sizeof(projectileinfo_t));
	wc->weaponinfo = reinterpret_cast<weaponinfo_t *>(reinterpret_cast<char *>(wc) + sizeof(weaponconfig_t));
	wc->projectileinfo = reinterpret_cast<projectileinfo_t *>(reinterpret_cast<char *>(wc->weaponinfo) +
										max_weaponinfo * sizeof(weaponinfo_t));
	wc->numweapons = max_weaponinfo;
	wc->numprojectiles = 0;
	//parse the source file
	while(PC_ReadToken(source, &token))
	{
		if (!strcmp(token.string, "weaponinfo"))
		{
			weaponinfo = {};
			if (!ReadStructure(source, &weaponinfo_struct, &weaponinfo))
			{
				FreeMemory(wc);
				FreeSource(source);
				return nullptr;
			} //end if
			if (weaponinfo.number < 0 || weaponinfo.number >= max_weaponinfo)
			{
				botimport.Print(PRT_ERROR, "weapon info number %d out of range in %s\n", weaponinfo.number, path);
				FreeMemory(wc);
				FreeSource(source);
				return nullptr;
			} //end if
			wc->weaponinfo[weaponinfo.number] = weaponinfo;
			wc->weaponinfo[weaponinfo.number].valid = qtrue;
		} //end if
		else if (!strcmp(token.string, "projectileinfo"))
		{
			if (wc->numprojectiles >= max_projectileinfo)
			{
				botimport.Print(PRT_ERROR, "more than %d projectiles defined in %s\n", max_projectileinfo, path);
				FreeMemory(wc);
				FreeSource(source);
				return nullptr;
			} //end if
			wc->projectileinfo[wc->numprojectiles] = {};
			if (!ReadStructure(source, &projectileinfo_struct, &wc->projectileinfo[wc->numprojectiles]))
			{
				FreeMemory(wc);
				FreeSource(source);
				return nullptr;
			} //end if
			wc->numprojectiles++;
		} //end if
		else
		{
			botimport.Print(PRT_ERROR, "unknown definition %s in %s\n", token.string, path);
			FreeMemory(wc);
			FreeSource(source);
			return nullptr;
		} //end else
	} //end while
	FreeSource(source);
	//fix up weapons
	for (i = 0; i < wc->numweapons; i++)
	{
		if (!wc->weaponinfo[i].valid) continue;
		if (!wc->weaponinfo[i].name[0])
		{
			botimport.Print(PRT_ERROR, "weapon %d has no name in %s\n", i, path);
			FreeMemory(wc);
			return nullptr;
		} //end if
		if (!wc->weaponinfo[i].projectile[0])
		{
			botimport.Print(PRT_ERROR, "weapon %s has no projectile in %s\n", wc->weaponinfo[i].name, path);
			FreeMemory(wc);
			return nullptr;
		} //end if
		//find the projectile info and copy it to the weapon info
		for (j = 0; j < wc->numprojectiles; j++)
		{
			if (!strcmp(wc->projectileinfo[j].name, wc->weaponinfo[i].projectile))
			{
				wc->weaponinfo[i].proj = wc->projectileinfo[j];
				break;
			} //end if
		} //end for
		if (j == wc->numprojectiles)
		{
			botimport.Print(PRT_ERROR, "weapon %s uses undefined projectile in %s\n", wc->weaponinfo[i].name, path);
			FreeMemory(wc);
			return nullptr;
		} //end if
	} //end for
	if (!wc->numweapons) botimport.Print(PRT_WARNING, "no weapon info loaded\n");
	botimport.Print(PRT_MESSAGE, "loaded %s\n", path);
	return wc;
} //end of the function LoadWeaponConfig
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
static int *WeaponWeightIndex(const weightconfig_t *wwc, const weaponconfig_t *wc)
{
	int *index, i;

	//initialize item weight index
	index = botlib::ClearedAlloc<int>(wc->numweapons);

	for (i = 0; i < wc->numweapons; i++)
	{
		index[i] = FindFuzzyWeight(wwc, wc->weaponinfo[i].name);
	} //end for
	return index;
} //end of the function WeaponWeightIndex
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
static void BotFreeWeaponWeights(int weaponstate)
{
	bot_weaponstate_t *ws;

	ws = BotWeaponStateFromHandle(weaponstate);
	if (!ws) return;
	if (ws->weaponweightconfig) FreeWeightConfig(ws->weaponweightconfig);
	if (ws->weaponweightindex) FreeMemory(ws->weaponweightindex);
} //end of the function BotFreeWeaponWeights
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int BotLoadWeaponWeights(int weaponstate, const char *filename)
{
	bot_weaponstate_t *ws;

	ws = BotWeaponStateFromHandle(weaponstate);
	if (!ws) return BLERR_CANNOTLOADWEAPONWEIGHTS;
	BotFreeWeaponWeights(weaponstate);
	//
	ws->weaponweightconfig = ReadWeightConfig(filename);
	if (!ws->weaponweightconfig)
	{
		botimport.Print(PRT_FATAL, "couldn't load weapon config %s\n", filename);
		return BLERR_CANNOTLOADWEAPONWEIGHTS;
	} //end if
	if (!weaponconfig) return BLERR_CANNOTLOADWEAPONCONFIG;
	ws->weaponweightindex = WeaponWeightIndex(ws->weaponweightconfig, weaponconfig);
	return BLERR_NOERROR;
} //end of the function BotLoadWeaponWeights
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotGetWeaponInfo(int weaponstate, int weapon, weaponinfo_t *weaponinfo)
{
	bot_weaponstate_t *ws;

	if (!BotValidWeaponNumber(weapon)) return;
	ws = BotWeaponStateFromHandle(weaponstate);
	if (!ws) return;
	if (!weaponconfig) return;
	*weaponinfo = weaponconfig->weaponinfo[weapon];
} //end of the function BotGetWeaponInfo
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int BotChooseBestFightWeapon(int weaponstate, int *inventory)
{
	int i, index, bestweapon;
	float weight, bestweight;
	weaponconfig_t *wc;
	bot_weaponstate_t *ws;

	ws = BotWeaponStateFromHandle(weaponstate);
	if (!ws) return 0;
	wc = weaponconfig;
	if (!weaponconfig) return 0;

	//if the bot has no weapon weight configuration
	if (!ws->weaponweightconfig) return 0;

	bestweight = 0;
	bestweapon = 0;
	for (i = 0; i < wc->numweapons; i++)
	{
		if (!wc->weaponinfo[i].valid) continue;
		index = ws->weaponweightindex[i];
		if (index < 0) continue;
		weight = FuzzyWeight(inventory, ws->weaponweightconfig, index);
		if (weight > bestweight)
		{
			bestweight = weight;
			bestweapon = i;
		} //end if
	} //end for
	return bestweapon;
} //end of the function BotChooseBestFightWeapon
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotResetWeaponState(int weaponstate)
{
} //end of the function BotResetWeaponState
//========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//========================================================================
int BotAllocWeaponState(void)
{
	int i;

	for (i = 1; i <= MAX_CLIENTS; i++)
	{
		if (!botweaponstates[i])
		{
			botweaponstates[i] = botlib::ClearedAlloc<bot_weaponstate_t>();
			return i;
		} //end if
	} //end for
	return 0;
} //end of the function BotAllocWeaponState
//========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//========================================================================
void BotFreeWeaponState(int handle)
{
	if (handle <= 0 || handle > MAX_CLIENTS)
	{
		botimport.Print(PRT_FATAL, "weapon state handle %d out of range\n", handle);
		return;
	} //end if
	if (!botweaponstates[handle])
	{
		botimport.Print(PRT_FATAL, "invalid weapon state %d\n", handle);
		return;
	} //end if
	BotFreeWeaponWeights(handle);
	FreeMemory(botweaponstates[handle]);
	botweaponstates[handle] = nullptr;
} //end of the function BotFreeWeaponState
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int BotSetupWeaponAI(void)
{
	const char *file;

	file = LibVarString("weaponconfig", "weapons.c");
	weaponconfig = LoadWeaponConfig(file);
	if (!weaponconfig)
	{
		botimport.Print(PRT_FATAL, "couldn't load the weapon config\n");
		return BLERR_CANNOTLOADWEAPONCONFIG;
	} //end if

#ifdef DEBUG_AI_WEAP
	DumpWeaponConfig(weaponconfig);
#endif //DEBUG_AI_WEAP
	//
	return BLERR_NOERROR;
} //end of the function BotSetupWeaponAI
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotShutdownWeaponAI(void)
{
	int i;

	if (weaponconfig) FreeMemory(weaponconfig);
	weaponconfig = nullptr;

	for (i = 1; i <= MAX_CLIENTS; i++)
	{
		if (botweaponstates[i])
		{
			BotFreeWeaponState(i);
		} //end if
	} //end for
} //end of the function BotShutdownWeaponAI

