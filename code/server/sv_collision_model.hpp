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

#ifndef FNQL_SERVER_SV_COLLISION_MODEL_HPP
#define FNQL_SERVER_SV_COLLISION_MODEL_HPP

namespace fnql::server::collision {

/*
 * QLSRP records the retail QL contract: SVF_CAPSULE changes an entity hull
 * only for capsule traces.  Point/box traces, including hitscan weapon fire,
 * continue to test the entity's axis-aligned box.
 */
[[nodiscard]] constexpr bool UseCapsuleEntityModel(
	bool traceUsesCapsule, bool entitySupportsCapsule ) noexcept
{
	return traceUsesCapsule && entitySupportsCapsule;
}

} // namespace fnql::server::collision

#endif
