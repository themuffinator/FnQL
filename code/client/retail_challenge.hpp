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

#ifndef FNQL_CLIENT_RETAIL_CHALLENGE_HPP
#define FNQL_CLIENT_RETAIL_CHALLENGE_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fnql::client::auth {

enum class ChallengeRequestMode : std::uint8_t {
	None,
	LegacyNonce,
	RetailWithoutSteam,
	RetailSteam
};

[[nodiscard]] constexpr bool IsRetailRequest(
	ChallengeRequestMode mode ) noexcept {
	return mode == ChallengeRequestMode::RetailWithoutSteam ||
		mode == ChallengeRequestMode::RetailSteam;
}

[[nodiscard]] constexpr int SelectHandshakeProtocol(
	ChallengeRequestMode mode, int advertisedProtocol,
	int retailProtocol, int legacyProtocol ) noexcept {
	if ( advertisedProtocol > 0 ) {
		return advertisedProtocol;
	}
	return IsRetailRequest( mode ) ? retailProtocol : legacyProtocol;
}

inline void WriteLittle32( std::uint8_t *target, std::uint32_t value ) noexcept {
	target[0] = static_cast<std::uint8_t>( value );
	target[1] = static_cast<std::uint8_t>( value >> 8u );
	target[2] = static_cast<std::uint8_t>( value >> 16u );
	target[3] = static_cast<std::uint8_t>( value >> 24u );
}

// Retail-observed protocol-91 challenge packet:
//   ff ff ff ff "getchallenge " <SteamID little-endian u64> <auth ticket>
// No text terminator, client nonce, or FnQL marker is present on this wire path.
[[nodiscard]] inline std::size_t BuildRetailSteamChallenge(
	std::uint8_t *packet, std::size_t capacity, std::uint64_t steamId,
	const std::uint8_t *ticket, std::size_t ticketBytes ) noexcept {
	static constexpr std::uint8_t command[] = {
		0xff, 0xff, 0xff, 0xff,
		'g', 'e', 't', 'c', 'h', 'a', 'l', 'l', 'e', 'n', 'g', 'e', ' '
	};
	constexpr std::size_t steamIdOffset = sizeof( command );
	constexpr std::size_t ticketOffset = steamIdOffset + sizeof( std::uint64_t );

	if ( !packet || !ticket || steamId == 0 || ticketBytes == 0 ||
		ticketBytes > capacity || ticketOffset > capacity - ticketBytes ) {
		return 0;
	}

	std::memcpy( packet, command, sizeof( command ) );
	WriteLittle32( packet + steamIdOffset,
		static_cast<std::uint32_t>( steamId ) );
	WriteLittle32( packet + steamIdOffset + 4u,
		static_cast<std::uint32_t>( steamId >> 32u ) );
	std::memcpy( packet + ticketOffset, ticket, ticketBytes );
	return ticketOffset + ticketBytes;
}

inline constexpr std::size_t RetailSteamChallengeHeaderBytes = 4u + 13u + 8u;

} // namespace fnql::client::auth

#endif
