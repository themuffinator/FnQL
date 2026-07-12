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

#ifndef FNQL_SERVER_STATS_CONTRACT_HPP
#define FNQL_SERVER_STATS_CONTRACT_HPP

#include "stats_session.hpp"
#include "../platform/fnql_steam_stats.hpp"

namespace fnql::server::stats {

// Preserve the server-facing names while keeping one shared retail ABI table.
inline constexpr auto &FieldNames = fnql::steam::stats::FieldNames;
inline constexpr auto &AchievementNames = fnql::steam::stats::AchievementNames;

static_assert( FieldNames.size() == FieldCount );
static_assert( AchievementNames.size() == AchievementCount );

} // namespace fnql::server::stats

#endif
