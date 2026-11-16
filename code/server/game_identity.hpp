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

#ifndef FNQL_SERVER_GAME_IDENTITY_HPP
#define FNQL_SERVER_GAME_IDENTITY_HPP

// The SteamID the engine reports to the game module (retail native import
// slot 200).  Retail qagame keys its access list -- admin, mod and ban
// entries, complaint tempbans included -- by this value and keeps an entry
// for 0 like any other (FnQL-game's G3 lockstep gates tdm_admin, tdm_access
// and tdm_complaints hold its module to that).  Reporting 0 for every human
// without a Steam identity therefore let one `ban` or `addadmin` reach all of
// them.  The engine instead gives each such connection its own anonymous id.
//
// An anonymous id is not an authenticated identity: it uses the SteamID64
// "anonymous user" account type, which no Steam-verified player has, it
// lives only as long as the connection, and nothing about it changes what
// the engine's Steam authentication calls answer.  Privileges stored for one
// therefore end with that connection instead of passing to another client.

#include <cstdint>

namespace fnql::server::identity {

// SteamID64: universe (8 bits) | account type (4) | instance (20) | account (32).
inline constexpr unsigned AccountTypeShift = 52;
inline constexpr unsigned UniverseShift = 56;
inline constexpr std::uint64_t UniversePublic = 1;
inline constexpr std::uint64_t AccountTypeIndividual = 1;
inline constexpr std::uint64_t AccountTypeAnonUser = 10;
inline constexpr std::uint64_t AnonymousSerialMask = ( std::uint64_t{ 1 } << AccountTypeShift ) - 1u;
inline constexpr std::uint64_t AnonymousPrefix =
	( UniversePublic << UniverseShift ) | ( AccountTypeAnonUser << AccountTypeShift );

constexpr std::uint64_t AccountType( std::uint64_t steamId ) {
	return ( steamId >> AccountTypeShift ) & 0xfu;
}

constexpr bool IsAnonymous( std::uint64_t steamId ) {
	return ( steamId & ~AnonymousSerialMask ) == AnonymousPrefix;
}

constexpr std::uint64_t MakeAnonymous( std::uint64_t serial ) {
	return AnonymousPrefix | ( serial & AnonymousSerialMask );
}

// Hands out anonymous ids from a 52-bit serial space, so a process never
// reuses one (2^52 connections would be needed to wrap).  The server seeds it
// from random bytes so ids persisted by one run (the game saves its access
// list) do not land on a different player in the next; deterministic tooling
// seeds it with a constant instead.
class AnonymousIdAllocator {
public:
	void Seed( std::uint64_t base ) {
		next_ = base & AnonymousSerialMask;
		seeded_ = true;
	}

	bool Seeded() const { return seeded_; }

	std::uint64_t Next() {
		const std::uint64_t id = MakeAnonymous( next_ );
		next_ = ( next_ + 1u ) & AnonymousSerialMask;
		return id;
	}

private:
	std::uint64_t next_ = 0;
	bool seeded_ = false;
};

// The identity a connection established itself: a session ticket's SteamID or
// the local provider's.  Userinfo is client-controlled and never counts.  A
// value in the anonymous range is refused too, so no connection can claim
// another connection's anonymous id.
constexpr std::uint64_t UsableConnectionId( bool fromConnection, std::uint64_t steamId ) {
	return fromConnection && steamId != 0 && !IsAnonymous( steamId ) ? steamId : 0;
}

// Whether a connecting slot needs an anonymous id.  Bots never do: the server
// alone decides what a bot is (its NA_BOT address), never userinfo.
constexpr bool NeedsAnonymousId( bool serverBot, std::uint64_t connectionId ) {
	return !serverBot && connectionId == 0;
}

// What slot 200 answers.  A bot is 0, as in retail.  A connection identity is
// reported unchanged, so Steam-authenticated players see exactly what they
// did before.  Anything else is the slot's anonymous id (0 only for a slot
// that never connected).
constexpr std::uint64_t GameSteamId( bool serverBot, std::uint64_t connectionId,
		std::uint64_t anonymousId ) {
	if ( serverBot ) {
		return 0;
	}
	if ( connectionId != 0 ) {
		return connectionId;
	}
	return IsAnonymous( anonymousId ) ? anonymousId : 0;
}

} // namespace fnql::server::identity

#endif // FNQL_SERVER_GAME_IDENTITY_HPP
