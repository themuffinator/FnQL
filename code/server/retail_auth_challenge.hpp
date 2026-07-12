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

#ifndef FNQL_RETAIL_AUTH_CHALLENGE_HPP
#define FNQL_RETAIL_AUTH_CHALLENGE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fnql::server::auth {

inline constexpr std::size_t MaxTicketBytes = 4'096;
inline constexpr std::size_t MinTicketBytes = 11;
inline constexpr std::uint32_t CacheLifetimeMilliseconds = 32'768;

enum class ChallengePayloadKind : std::uint8_t {
	NotChallenge,
	Text,
	Retail,
	MalformedRetail
};

struct ParsedChallenge {
	ChallengePayloadKind kind = ChallengePayloadKind::NotChallenge;
	std::uint64_t steamId = 0;
	const std::uint8_t *ticket = nullptr;
	std::size_t ticketBytes = 0;
	std::uint32_t clientChallenge = 0;
	bool fnqlExtension = false;
};

[[nodiscard]] constexpr std::uint32_t ReadLittle32(
	const std::uint8_t *bytes ) noexcept {
	return static_cast<std::uint32_t>( bytes[0] ) |
		( static_cast<std::uint32_t>( bytes[1] ) << 8u ) |
		( static_cast<std::uint32_t>( bytes[2] ) << 16u ) |
		( static_cast<std::uint32_t>( bytes[3] ) << 24u );
}

[[nodiscard]] constexpr bool IsTextByte( std::uint8_t value ) noexcept {
	return value == '\t' || value == '\n' || value == '\r' ||
		( value >= 0x20u && value <= 0x7eu );
}

// Retail Quake Live sends:
//   ff ff ff ff "getchallenge " <SteamID little-endian u64> <auth ticket>
// Text requests are deliberately kept separate so FnQL can retain the
// ioquake3 client nonce that prevents connection-response hijacking.
[[nodiscard]] inline ParsedChallenge ParseChallengePayload(
	const std::uint8_t *packet, std::size_t packetBytes ) noexcept {
	static constexpr std::array<std::uint8_t, 4> marker{{ 0xff, 0xff, 0xff, 0xff }};
	static constexpr char command[] = "getchallenge";
	constexpr std::size_t commandBytes = sizeof( command ) - 1u;
	constexpr std::size_t payloadOffset = marker.size() + commandBytes + 1u;
	static constexpr std::array<std::uint8_t, 8> fnqlAuth{{
		'F', 'n', 'Q', 'L', 'A', 'u', 't', 'h'
	}};

	if ( !packet || packetBytes < marker.size() + commandBytes ) {
		return {};
	}
	for ( std::size_t i = 0; i < marker.size(); ++i ) {
		if ( packet[i] != marker[i] ) {
			return {};
		}
	}
	for ( std::size_t i = 0; i < commandBytes; ++i ) {
		if ( packet[marker.size() + i] != static_cast<std::uint8_t>( command[i] ) ) {
			return {};
		}
	}
	if ( packetBytes == marker.size() + commandBytes ) {
		return { ChallengePayloadKind::Text, 0, nullptr, 0 };
	}
	if ( packet[marker.size() + commandBytes] != static_cast<std::uint8_t>( ' ' ) ) {
		return { ChallengePayloadKind::NotChallenge, 0, nullptr, 0 };
	}

	bool text = true;
	for ( std::size_t i = payloadOffset; i < packetBytes; ++i ) {
		if ( !IsTextByte( packet[i] ) ) {
			text = false;
			break;
		}
	}
	if ( text ) {
		return { ChallengePayloadKind::Text, 0, nullptr, 0 };
	}

	// FnQL clients retain the retail SteamID/ticket representation but prefix
	// it with an explicit marker and nonce. This lets an authenticated FnQL
	// server echo the normal FnQL identity proof without changing the packet
	// shape accepted from an unmodified retail client.
	constexpr std::size_t fnqlChallengeOffset = payloadOffset + fnqlAuth.size();
	constexpr std::size_t fnqlSteamIdOffset = fnqlChallengeOffset + 4u;
	constexpr std::size_t fnqlTicketOffset = fnqlSteamIdOffset + 8u;
	bool hasFnqlMarker = packetBytes >= payloadOffset + fnqlAuth.size();
	for ( std::size_t i = 0; hasFnqlMarker && i < fnqlAuth.size(); ++i ) {
		hasFnqlMarker = packet[payloadOffset + i] == fnqlAuth[i];
	}
	if ( hasFnqlMarker ) {
		if ( packetBytes < fnqlTicketOffset + MinTicketBytes ||
			packetBytes - fnqlTicketOffset > MaxTicketBytes ) {
			return { ChallengePayloadKind::MalformedRetail, 0, nullptr, 0 };
		}
		const std::uint64_t steamId =
			static_cast<std::uint64_t>( ReadLittle32( packet + fnqlSteamIdOffset ) ) |
			( static_cast<std::uint64_t>( ReadLittle32(
				packet + fnqlSteamIdOffset + 4u ) ) << 32u );
		if ( steamId == 0 ) {
			return { ChallengePayloadKind::MalformedRetail, 0, nullptr, 0 };
		}
		return { ChallengePayloadKind::Retail, steamId,
			packet + fnqlTicketOffset, packetBytes - fnqlTicketOffset,
			ReadLittle32( packet + fnqlChallengeOffset ), true };
	}

	if ( packetBytes < payloadOffset + sizeof( std::uint64_t ) + MinTicketBytes ) {
		return { ChallengePayloadKind::MalformedRetail, 0, nullptr, 0 };
	}
	const std::size_t ticketOffset = payloadOffset + sizeof( std::uint64_t );
	const std::size_t ticketBytes = packetBytes - ticketOffset;
	if ( ticketBytes > MaxTicketBytes ) {
		return { ChallengePayloadKind::MalformedRetail, 0, nullptr, 0 };
	}

	const std::uint64_t steamId =
		static_cast<std::uint64_t>( ReadLittle32( packet + payloadOffset ) ) |
		( static_cast<std::uint64_t>( ReadLittle32( packet + payloadOffset + 4u ) ) << 32u );
	if ( steamId == 0 ) {
		return { ChallengePayloadKind::MalformedRetail, 0, nullptr, 0 };
	}

	return { ChallengePayloadKind::Retail, steamId, packet + ticketOffset, ticketBytes };
}

struct AddressKey {
	std::array<std::uint8_t, 24> bytes{};
	std::uint8_t used = 0;

	[[nodiscard]] friend constexpr bool operator==(
		const AddressKey &left, const AddressKey &right ) noexcept {
		if ( left.used != right.used ) {
			return false;
		}
		for ( std::size_t i = 0; i < left.used; ++i ) {
			if ( left.bytes[i] != right.bytes[i] ) {
				return false;
			}
		}
		return true;
	}
};

struct AuthRecord {
	std::uint64_t steamId = 0;
	std::array<std::uint8_t, MaxTicketBytes> ticket{};
	std::size_t ticketBytes = 0;
};

inline void SecureErase( void *memory, std::size_t bytes ) noexcept {
	volatile std::uint8_t *out = static_cast<volatile std::uint8_t *>( memory );
	while ( bytes-- > 0 ) {
		*out++ = 0;
	}
}

template<std::size_t Capacity>
class AuthCache {
	static_assert( Capacity > 0, "auth cache must have storage" );

	struct Entry {
		AddressKey address{};
		int challenge = 0;
		std::uint32_t createdAt = 0;
		std::uint64_t generation = 0;
		bool occupied = false;
		AuthRecord auth{};
	};

public:
	AuthCache() = default;
	AuthCache( const AuthCache & ) = delete;
	AuthCache &operator=( const AuthCache & ) = delete;

	~AuthCache() noexcept {
		Clear();
	}

	[[nodiscard]] bool Store( const AddressKey &address, int challenge,
		std::uint64_t steamId, const std::uint8_t *ticket,
		std::size_t ticketBytes, std::uint32_t now ) noexcept {
		if ( steamId == 0 || !ticket || ticketBytes < MinTicketBytes ||
			ticketBytes > MaxTicketBytes ) {
			return false;
		}

		Entry *target = nullptr;
		Entry *oldest = &entries_[0];
		for ( Entry &entry : entries_ ) {
			if ( entry.occupied && entry.address == address ) {
				target = &entry;
				break;
			}
			if ( !entry.occupied && !target ) {
				target = &entry;
			}
			if ( entry.generation < oldest->generation ) {
				oldest = &entry;
			}
		}
		if ( !target ) {
			target = oldest;
		}

		EraseEntry( *target );
		target->address = address;
		target->challenge = challenge;
		target->createdAt = now;
		target->generation = NextGeneration();
		target->occupied = true;
		target->auth.steamId = steamId;
		target->auth.ticketBytes = ticketBytes;
		for ( std::size_t i = 0; i < ticketBytes; ++i ) {
			target->auth.ticket[i] = ticket[i];
		}
		return true;
	}

	[[nodiscard]] bool Consume( const AddressKey &address, int challenge,
		std::uint32_t now, AuthRecord &out ) noexcept {
		SecureErase( &out, sizeof( out ) );
		for ( Entry &entry : entries_ ) {
			if ( !entry.occupied || !( entry.address == address ) ||
				entry.challenge != challenge ) {
				continue;
			}
			const bool live = static_cast<std::uint32_t>( now - entry.createdAt ) <=
				CacheLifetimeMilliseconds;
			if ( live ) {
				out.steamId = entry.auth.steamId;
				out.ticketBytes = entry.auth.ticketBytes;
				for ( std::size_t i = 0; i < out.ticketBytes; ++i ) {
					out.ticket[i] = entry.auth.ticket[i];
				}
			}
			EraseEntry( entry );
			return live;
		}
		return false;
	}

	void Clear() noexcept {
		for ( Entry &entry : entries_ ) {
			EraseEntry( entry );
		}
		generation_ = 0;
	}

private:
	[[nodiscard]] std::uint64_t NextGeneration() noexcept {
		if ( generation_ == ( std::numeric_limits<std::uint64_t>::max )() ) {
			std::uint64_t next = 1;
			for ( Entry &entry : entries_ ) {
				entry.generation = entry.occupied ? next++ : 0;
			}
			generation_ = next;
		}
		return ++generation_;
	}

	static void EraseEntry( Entry &entry ) noexcept {
		SecureErase( &entry.auth, sizeof( entry.auth ) );
		entry.address = {};
		entry.challenge = 0;
		entry.createdAt = 0;
		entry.generation = 0;
		entry.occupied = false;
	}

	std::array<Entry, Capacity> entries_{};
	std::uint64_t generation_ = 0;
};

} // namespace fnql::server::auth

#endif
