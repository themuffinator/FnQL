/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL and is distributed under the terms of the GNU
General Public License version 2 or (at your option) any later version.
===========================================================================
*/

#ifndef FNQL_SNAPSHOT_HISTORY_HPP
#define FNQL_SNAPSHOT_HISTORY_HPP

#include "../qcommon/netchan_safety.hpp"

namespace fnql::snapshot {

enum class LookupStatus { Available, Unavailable, Future };

template<typename Snapshot>
struct LookupResult {
	LookupStatus status;
	const Snapshot *snapshot = nullptr;
};

// Packet sequences wrap at 31 bits; the entity cursor uses all 32 bits.
// Both native retail and legacy VM imports must query the same history.
template<typename Snapshot, std::size_t Count>
[[nodiscard]] LookupResult<Snapshot> Lookup( int requested, int current,
	const Snapshot ( &history )[Count], int entityCursor,
	std::uint32_t entityCapacity ) noexcept {
	static_assert( Count > 0 && ( Count & ( Count - 1 ) ) == 0,
		"snapshot history must be a power of two" );
	if ( net::IsNewerSequence( requested, current ) ) {
		return { LookupStatus::Future };
	}
	if ( net::SequenceDistance( current, requested ) >= Count ) {
		return { LookupStatus::Unavailable };
	}
	const Snapshot &stored = history[static_cast<std::uint32_t>( requested ) & ( Count - 1 )];
	const std::uint32_t entityAge = net::CounterDistance( entityCursor, stored.parseEntitiesNum );
	if ( !stored.valid || stored.messageNum != requested ||
		entityAge >= entityCapacity || stored.numEntities < 0 ||
		static_cast<std::uint32_t>( stored.numEntities ) > entityAge ) {
		return { LookupStatus::Unavailable };
	}
	return { LookupStatus::Available, &stored };
}

[[nodiscard]] inline constexpr std::uint32_t EntityIndex( int cursor,
	int offset, std::uint32_t mask ) noexcept {
	return ( static_cast<std::uint32_t>( cursor ) +
		static_cast<std::uint32_t>( offset ) ) & mask;
}

// SV_EmitPacketEntities merges strictly sorted entity lists. QLSRP's
// CL_ParsePacketEntities has the same merge contract. Removal records also
// consume their entity number; only the end marker is exempt from this check.
[[nodiscard]] inline constexpr bool EntityFollows( int previous,
	int number, int endMarker ) noexcept {
	return number >= 0 && number < endMarker && number > previous;
}

} // namespace fnql::snapshot

#endif
