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

#ifndef FNQL_NETCHAN_SAFETY_HPP
#define FNQL_NETCHAN_SAFETY_HPP

#include <cstddef>
#include <cstdint>
#include <limits>

namespace fnql::net {

inline constexpr std::uint32_t SequenceMask = 0x7fffffffu;
inline constexpr std::uint32_t SequenceHalfRange = 0x40000000u;
inline constexpr std::uint32_t CounterHalfRange = 0x80000000u;
inline constexpr std::size_t MaximumUdpPayload = 65'507u;

[[nodiscard]] inline constexpr bool IsValidPayload( const void *data,
	int length, std::size_t maximum ) noexcept {
	return length >= 0 && static_cast<std::size_t>( length ) <= maximum &&
		( length == 0 || data != nullptr );
}

[[nodiscard]] inline constexpr int NextSequence( int sequence ) noexcept {
	return static_cast<int>(
		( static_cast<std::uint32_t>( sequence ) + 1u ) & SequenceMask );
}

[[nodiscard]] inline constexpr int PreviousSequence( int sequence ) noexcept {
	return static_cast<int>(
		( static_cast<std::uint32_t>( sequence ) - 1u ) & SequenceMask );
}

[[nodiscard]] inline constexpr int RetreatSequence( int sequence,
	std::uint32_t amount ) noexcept {
	return static_cast<int>(
		( static_cast<std::uint32_t>( sequence ) - amount ) & SequenceMask );
}

[[nodiscard]] inline constexpr int AdvanceSequence( int sequence,
	std::uint32_t amount ) noexcept {
	return static_cast<int>(
		( static_cast<std::uint32_t>( sequence ) + amount ) & SequenceMask );
}

[[nodiscard]] inline constexpr std::uint32_t SequenceDistance( int newer,
	int older ) noexcept {
	return ( static_cast<std::uint32_t>( newer ) -
		static_cast<std::uint32_t>( older ) ) & SequenceMask;
}

[[nodiscard]] inline constexpr bool IsNewerSequence( int candidate,
	int current ) noexcept {
	const std::uint32_t distance = SequenceDistance( candidate, current );
	return distance != 0u && distance < SequenceHalfRange;
}

[[nodiscard]] inline constexpr int CounterAdd( int value,
	std::uint32_t amount ) noexcept {
	const std::uint32_t bits = static_cast<std::uint32_t>( value ) + amount;
	return bits <= static_cast<std::uint32_t>( ( std::numeric_limits<int>::max )() )
		? static_cast<int>( bits )
		: -1 - static_cast<int>( ( std::numeric_limits<std::uint32_t>::max )() - bits );
}

[[nodiscard]] inline constexpr int NextCounter( int value ) noexcept {
	return CounterAdd( value, 1u );
}

[[nodiscard]] inline constexpr std::uint32_t CounterDistance( int newer,
	int older ) noexcept {
	return static_cast<std::uint32_t>( newer ) - static_cast<std::uint32_t>( older );
}

[[nodiscard]] inline constexpr bool IsNewerCounter( int candidate,
	int current ) noexcept {
	const std::uint32_t distance = CounterDistance( candidate, current );
	return distance != 0u && distance < CounterHalfRange;
}

// Reliable command counters use all 32 bits. This accepts an equal or older
// acknowledgement and rejects future/ambiguous values without signed math.
[[nodiscard]] inline constexpr bool PendingCounterCount( int current,
	int acknowledgement, std::uint32_t &pending ) noexcept {
	pending = CounterDistance( current, acknowledgement );
	return pending < CounterHalfRange;
}

[[nodiscard]] inline constexpr bool FragmentFits( int readOffset,
	int messageSize, int assembledSize, std::size_t assemblyCapacity,
	int fragmentSize ) noexcept {
	if ( readOffset < 0 || messageSize < 0 || readOffset > messageSize ||
		assembledSize < 0 || fragmentSize < 0 ) {
		return false;
	}

	const auto readable = static_cast<std::size_t>( messageSize - readOffset );
	const auto assembled = static_cast<std::size_t>( assembledSize );
	const auto fragment = static_cast<std::size_t>( fragmentSize );
	return fragment <= readable && assembled <= assemblyCapacity &&
		fragment <= assemblyCapacity - assembled;
}

[[nodiscard]] inline constexpr std::uint32_t SequenceChecksum( int challenge,
	int sequence ) noexcept {
	const auto challengeBits = static_cast<std::uint32_t>( challenge );
	const auto sequenceBits = static_cast<std::uint32_t>( sequence );
	return challengeBits ^ ( sequenceBits * challengeBits );
}

} // namespace fnql::net

#endif
