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

// The SteamID the engine reports to the game module (import slot 200).  The
// game keys admin, mod and ban entries by it, so no two connections without a
// Steam identity may share a value, and bots, decided by the server alone,
// stay at retail's 0.

#include "../code/server/game_identity.hpp"

#include <cstdint>
#include <cstdio>
#include <set>

namespace identity = fnql::server::identity;

namespace {

int failures = 0;

void Check( bool condition, const char *message ) {
	if ( !condition ) {
		std::fprintf( stderr, "FAIL: %s\n", message );
		++failures;
	}
}

// A SteamID64 for a public individual account, as Steam verifies them.
constexpr std::uint64_t Individual( std::uint32_t account ) {
	return ( identity::UniversePublic << identity::UniverseShift ) |
		( identity::AccountTypeIndividual << identity::AccountTypeShift ) |
		( std::uint64_t{ 1 } << 32 ) | account;
}

static_assert( Individual( 22202 ) == 76561197960287930ull, "individual SteamID64 layout" );
static_assert( !identity::IsAnonymous( Individual( 1 ) ), "an individual id is never anonymous" );
static_assert( identity::IsAnonymous( identity::MakeAnonymous( 0 ) ), "serial 0 is still a non-zero anonymous id" );
static_assert( identity::MakeAnonymous( 0 ) != 0, "an anonymous id is never 0" );

// The access-list hazard: Steam-less humans all used to report 0.
void TestSteamLessHumansDoNotShareAnId() {
	identity::AnonymousIdAllocator allocator;
	allocator.Seed( 0 );
	std::set<std::uint64_t> seen;
	for ( int i = 0; i < 4096; ++i ) {
		const std::uint64_t connectionId = identity::UsableConnectionId( false, 0 );
		Check( identity::NeedsAnonymousId( false, connectionId ), "a Steam-less human needs an anonymous id" );
		const std::uint64_t anonymousId = allocator.Next();
		const std::uint64_t reported = identity::GameSteamId( false, connectionId, anonymousId );
		Check( reported != 0, "a Steam-less human never reports 0" );
		Check( identity::IsAnonymous( reported ), "a Steam-less human reports an anonymous-type id" );
		Check( identity::AccountType( reported ) != identity::AccountTypeIndividual,
			"an anonymous id is not an individual (verifiable) account" );
		Check( seen.insert( reported ).second, "no two Steam-less humans share an id" );
	}
}

// Bot-ness comes from the server; nothing a client sends changes it, and a
// bot keeps retail's 0 even though the engine holds a Steam id for it.
void TestBotsAreZero() {
	Check( !identity::NeedsAnonymousId( true, 0 ), "a bot takes no anonymous id" );
	Check( identity::GameSteamId( true, 0, 0 ) == 0, "a bot reports 0" );
	Check( identity::GameSteamId( true, Individual( 7 ), identity::MakeAnonymous( 3 ) ) == 0,
		"a bot reports 0 whatever ids its slot holds" );
	// A human whose userinfo claims to be a bot is still a human: the slot
	// is not NA_BOT, so it is not answered as one.
	Check( identity::GameSteamId( false, 0, identity::MakeAnonymous( 9 ) ) == identity::MakeAnonymous( 9 ),
		"a non-bot slot is answered with its own id" );
}

// Steam-authenticated players see exactly what they did before.
void TestConnectionIdentityIsUnchanged() {
	const std::uint64_t player = Individual( 123456 );
	const std::uint64_t connectionId = identity::UsableConnectionId( true, player );
	Check( connectionId == player, "a ticket or local-provider id is used as is" );
	Check( !identity::NeedsAnonymousId( false, connectionId ), "a Steam player takes no anonymous id" );
	Check( identity::GameSteamId( false, connectionId, 0 ) == player, "a Steam player reports its own id" );
}

// Only the connection may establish an identity; userinfo cannot, and no
// connection can claim the anonymous range to alias another player.
void TestClaimsCannotAlias() {
	identity::AnonymousIdAllocator allocator;
	allocator.Seed( 0 );
	const std::uint64_t victim = allocator.Next();

	Check( identity::UsableConnectionId( false, Individual( 42 ) ) == 0,
		"a userinfo steamid is not a connection identity" );
	Check( identity::UsableConnectionId( true, victim ) == 0,
		"a connection cannot claim an anonymous id" );
	Check( identity::UsableConnectionId( true, 0 ) == 0, "0 is no identity" );

	const std::uint64_t forgedConnection = identity::UsableConnectionId( true, victim );
	Check( identity::NeedsAnonymousId( false, forgedConnection ), "a forged anonymous claim gets its own id" );
	const std::uint64_t own = allocator.Next();
	Check( identity::GameSteamId( false, forgedConnection, own ) == own, "the claimant reports its own id" );
	Check( own != victim, "the claimant does not alias the victim" );

	// A slot that never connected (or was dropped) answers 0, not a stale id.
	Check( identity::GameSteamId( false, 0, 0 ) == 0, "an unassigned slot reports 0" );
	Check( identity::GameSteamId( false, 0, Individual( 5 ) ) == 0,
		"only an anonymous-range value is accepted as a slot's anonymous id" );
}

void TestAllocatorSeedingAndWrap() {
	identity::AnonymousIdAllocator a;
	identity::AnonymousIdAllocator b;
	Check( !a.Seeded(), "an allocator starts unseeded" );
	a.Seed( 77 );
	b.Seed( 77 );
	Check( a.Seeded(), "Seed marks the allocator seeded" );
	for ( int i = 0; i < 16; ++i ) {
		Check( a.Next() == b.Next(), "equal seeds give equal sequences (sv_lockstep determinism)" );
	}

	// The serial wraps inside the 52-bit space without leaving the
	// anonymous range or producing 0.
	identity::AnonymousIdAllocator edge;
	edge.Seed( ~std::uint64_t{ 0 } );
	const std::uint64_t last = edge.Next();
	const std::uint64_t wrapped = edge.Next();
	Check( last == identity::MakeAnonymous( identity::AnonymousSerialMask ), "a seed is masked to the serial space" );
	Check( wrapped == identity::MakeAnonymous( 0 ), "the serial wraps to 0" );
	Check( identity::IsAnonymous( last ) && identity::IsAnonymous( wrapped ), "wrapped ids stay anonymous" );
	Check( last != wrapped && wrapped != 0, "wrapping produces neither a repeat nor 0" );
}

} // namespace

int main() {
	TestSteamLessHumansDoNotShareAnId();
	TestBotsAreZero();
	TestConnectionIdentityIsUnchanged();
	TestClaimsCannotAlias();
	TestAllocatorSeedingAndWrap();
	if ( failures != 0 ) {
		std::fprintf( stderr, "%d test(s) failed\n", failures );
		return 1;
	}
	std::puts( "game identity tests passed" );
	return 0;
}
