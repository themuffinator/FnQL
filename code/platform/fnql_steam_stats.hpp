/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/

#ifndef FNQL_STEAM_STATS_HPP
#define FNQL_STEAM_STATS_HPP

#include <array>
#include <string_view>

namespace fnql::steam::stats {

// Retail names are shared ABI data for the client readback and authoritative
// GameServer update lanes. They are not a reconstruction of game logic.
inline constexpr std::array<std::string_view, 88> FieldNames{{
	"version", "kill_gauntlet", "kill_machinegun", "kill_shotgun",
	"kill_grenade", "kill_rocket", "kill_lightning", "kill_railgun",
	"kill_plasma", "kill_bfg", "kill_nailgun", "kill_proxmine",
	"kill_chaingun", "kill_hmg", "hits_machinegun", "hits_shotgun",
	"hits_grenade", "hits_rocket", "hits_lightning", "hits_railgun",
	"hits_plasma", "hits_bfg", "hits_nailgun", "hits_proxmine",
	"hits_chaingun", "hits_hmg", "shots_machinegun", "shots_shotgun",
	"shots_grenade", "shots_rocket", "shots_lightning", "shots_railgun",
	"shots_plasma", "shots_bfg", "shots_nailgun", "shots_proxmine",
	"shots_chaingun", "shots_hmg", "mod_shotgun", "mod_gauntlet",
	"mod_machinegun", "mod_grenade", "mod_rocket", "mod_plasma",
	"mod_railgun", "mod_lightning", "mod_bfg", "mod_water", "mod_slime",
	"mod_lava", "mod_crush", "mod_telefrag", "mod_laser", "BROKEN1",
	"mod_nailgun", "mod_chaingun", "mod_proxmine", "mod_kamikaze",
	"mod_juiced", "mod_suicide", "mod_falling", "mod_grapple", "mod_hmg",
	"mod_lightning_discharge", "mod_other", "medal_firstfrag",
	"medal_gauntlet", "medal_excellent", "medal_revenge", "medal_combokill",
	"medal_midair", "medal_perforated", "medal_rampage", "medal_impressive",
	"medal_capture", "medal_assist", "medal_defense", "medal_headshot",
	"medal_quadgod", "medal_perfect", "medal_accuracy", "wins", "losses",
	"played", "BROKEN2", "mod_hurt", "total_kills", "total_deaths"
}};

inline constexpr std::array<std::string_view, 59> AchievementNames{{
	"AW_MIDAIR", "AW_SPEED_KILLS", "AW_TRAINING_1_1", "AW_TRAINING_1_2",
	"AW_TRAINING_1_3", "AW_TRAINING_2_1", "AW_TRAINING_2_2",
	"AW_TRAINING_2_3", "AW_TRAINING_3_1", "AW_TRAINING_3_2",
	"AW_FIRST_FRAG", "AW_TESTING", "AW_BIG_TIME", "AW_PRIZE_FIGHTER",
	"AW_WICKED", "AW_BANDIT", "AW_CAMPER", "AW_PSYCHIC",
	"AW_WTF_WAS_THAT", "AW_OVERKILL", "AW_RAPTOR", "AW_PLUS_ONE",
	"AW_KILLJOY", "AW_HAT_TRICK", "AW_MIRACLE_MAKER", "AW_BRAWLER",
	"AW_AIR_HAMMER", "AW_AIM_BOT", "AW_SUCKER_PUNCH", "AW_RESOURCE_HOG",
	"AW_NINJA_CAP", "AW_MISSED_OPPORTUNITY", "AW_SKULL_TRUMPET",
	"AW_FIGHT_CLUB", "AW_GUARDIAN", "AW_SIDEKICK", "AW_COLOR_GUARD",
	"AW_2_IN_2", "AW_ASSASSIN", "AW_EVIL_EYE", "AW_VICTORY",
	"AW_POINT_DENIED", "AW_FIRST_TASTE", "AW_HOOKED", "AW_FEAR_ME",
	"AW_VADRIGAR", "AW_MVP", "AW_SMACK_DOWN", "AW_HERE_GOES_NOTHING",
	"AW_LAST_HOPE", "AW_PUNCH_OUT", "AW_NADE_SPAM", "AW_ROCKET_MAN",
	"AW_PULL", "AW_CLUTCH", "AW_JESSE_JAMES", "AW_FULL_HOUSE",
	"AW_TRIFECTA", "AW_MAX"
}};

} // namespace fnql::steam::stats

#endif
