#include "../code/server/retail_auth_challenge.hpp"
#include "../code/client/retail_challenge.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace auth = fnql::server::auth;
namespace clientAuth = fnql::client::auth;

namespace {

int failures = 0;

void Check( bool condition, const char *message ) {
	if ( !condition ) {
		std::fprintf( stderr, "FAIL: %s\n", message );
		++failures;
	}
}

std::array<std::uint8_t, 4 + 12 + 1 + 8 + 32> RetailPacket(
	std::uint64_t steamId ) {
	std::array<std::uint8_t, 4 + 12 + 1 + 8 + 32> packet{};
	packet[0] = packet[1] = packet[2] = packet[3] = 0xff;
	std::memcpy( packet.data() + 4, "getchallenge ", 12 + 1 );
	for ( std::size_t i = 0; i < 8; ++i ) {
		packet[4 + 13 + i] = static_cast<std::uint8_t>( steamId >> ( i * 8u ) );
	}
	for ( std::size_t i = 0; i < 32; ++i ) {
		packet[4 + 13 + 8 + i] = static_cast<std::uint8_t>( i ^ 0xa5u );
	}
	return packet;
}

auth::AddressKey Key( std::uint8_t value ) {
	auth::AddressKey key{};
	key.used = 3;
	key.bytes[0] = 4;
	key.bytes[1] = value;
	key.bytes[2] = 7;
	return key;
}

void TestParser() {
	constexpr std::uint64_t steamId = 76561198000000001ull;
	auto packet = RetailPacket( steamId );
	auto parsed = auth::ParseChallengePayload( packet.data(), packet.size() );
	Check( parsed.kind == auth::ChallengePayloadKind::Retail, "retail packet recognized" );
	Check( parsed.steamId == steamId, "little-endian SteamID decoded" );
	Check( parsed.ticketBytes == 32, "ticket extent decoded" );
	Check( parsed.ticket == packet.data() + 25, "ticket aliases bounded packet extent" );

	std::array<std::uint8_t, 4 + 13 + 8 + 4 + 8 + 32> fnqlPacket{};
	fnqlPacket[0] = fnqlPacket[1] = fnqlPacket[2] = fnqlPacket[3] = 0xff;
	std::memcpy( fnqlPacket.data() + 4, "getchallenge ", 13 );
	std::memcpy( fnqlPacket.data() + 17, "FnQLAuth", 8 );
	constexpr std::uint32_t nonce = 0xa8563412u;
	for ( std::size_t i = 0; i < 4; ++i ) {
		fnqlPacket[25 + i] = static_cast<std::uint8_t>( nonce >> ( i * 8u ) );
	}
	for ( std::size_t i = 0; i < 8; ++i ) {
		fnqlPacket[29 + i] = static_cast<std::uint8_t>( steamId >> ( i * 8u ) );
	}
	for ( std::size_t i = 0; i < 32; ++i ) {
		fnqlPacket[37 + i] = static_cast<std::uint8_t>( i ^ 0x5au );
	}
	parsed = auth::ParseChallengePayload( fnqlPacket.data(), fnqlPacket.size() );
	Check( parsed.kind == auth::ChallengePayloadKind::Retail && parsed.fnqlExtension,
		"FnQL authenticated extension recognized" );
	Check( parsed.clientChallenge == nonce, "FnQL client nonce decoded" );
	Check( parsed.steamId == steamId && parsed.ticketBytes == 32,
		"FnQL extension retains retail identity and ticket" );

	static constexpr std::uint8_t text[] =
		"\xff\xff\xff\xff" "getchallenge 1234 FnQL protocol91";
	parsed = auth::ParseChallengePayload( text, sizeof( text ) - 1 );
	Check( parsed.kind == auth::ChallengePayloadKind::Text, "text challenge remains text" );

	packet[17] = packet[18] = packet[19] = packet[20] =
		packet[21] = packet[22] = packet[23] = packet[24] = 0;
	parsed = auth::ParseChallengePayload( packet.data(), packet.size() );
	Check( parsed.kind == auth::ChallengePayloadKind::MalformedRetail, "zero SteamID rejected" );

	packet = RetailPacket( steamId );
	parsed = auth::ParseChallengePayload( packet.data(), 25 + auth::MinTicketBytes - 1 );
	Check( parsed.kind == auth::ChallengePayloadKind::MalformedRetail, "short ticket rejected" );
}

void TestClientPacketAndProtocolSelection() {
	constexpr std::uint64_t steamId = 0x1122334455667788ull;
	std::array<std::uint8_t, 16> ticket{};
	for ( std::size_t i = 0; i < ticket.size(); ++i ) {
		ticket[i] = static_cast<std::uint8_t>( 0x80u + i );
	}
	std::array<std::uint8_t,
		clientAuth::RetailSteamChallengeHeaderBytes + ticket.size()> packet{};
	const std::size_t packetBytes = clientAuth::BuildRetailSteamChallenge(
		packet.data(), packet.size(), steamId, ticket.data(), ticket.size() );
	Check( packetBytes == packet.size(), "client retail packet length" );
	Check( std::memcmp( packet.data(),
		"\xff\xff\xff\xff" "getchallenge ", 17 ) == 0,
		"client emits exact retail command prefix" );
	Check( packet[17] == 0x88 && packet[18] == 0x77 &&
		packet[23] == 0x22 && packet[24] == 0x11,
		"client emits little-endian SteamID" );
	Check( std::memcmp( packet.data() + 25, ticket.data(), ticket.size() ) == 0,
		"client appends opaque ticket without marker or terminator" );

	Check( clientAuth::SelectHandshakeProtocol(
		clientAuth::ChallengeRequestMode::RetailSteam, 0, 91, 68 ) == 91,
		"bare authenticated retail response selects protocol 91" );
	Check( clientAuth::SelectHandshakeProtocol(
		clientAuth::ChallengeRequestMode::RetailWithoutSteam, 0, 91, 68 ) == 91,
		"bare no-Steam retail response selects protocol 91" );
	Check( clientAuth::SelectHandshakeProtocol(
		clientAuth::ChallengeRequestMode::LegacyNonce, 0, 91, 68 ) == 68,
		"legacy challenge retains protocol 68 fallback" );
	Check( clientAuth::SelectHandshakeProtocol(
		clientAuth::ChallengeRequestMode::RetailSteam, 71, 91, 68 ) == 71,
		"explicit server protocol remains authoritative" );

	Check( clientAuth::BuildRetailSteamChallenge(
		packet.data(), packet.size() - 1, steamId,
		ticket.data(), ticket.size() ) == 0,
		"client packet builder rejects short output" );
}

void TestCache() {
	auth::AuthCache<2> cache;
	auth::AuthRecord out{};
	std::array<std::uint8_t, 16> ticket{};
	for ( std::size_t i = 0; i < ticket.size(); ++i ) {
		ticket[i] = static_cast<std::uint8_t>( i + 1 );
	}

	Check( cache.Store( Key( 1 ), 101, 11, ticket.data(), ticket.size(), 1000 ),
		"first cache insert" );
	Check( cache.Consume( Key( 1 ), 101, 1001, out ), "matching record consumed" );
	Check( out.steamId == 11 && out.ticketBytes == ticket.size(), "record copied out" );
	Check( !cache.Consume( Key( 1 ), 101, 1002, out ), "record cannot replay" );
	Check( out.steamId == 0 && out.ticketBytes == 0, "miss scrubs output" );

	Check( cache.Store( Key( 2 ), 202, 22, ticket.data(), ticket.size(), 0xfffffff0u ),
		"wraparound cache insert" );
	Check( cache.Consume( Key( 2 ), 202, 0x10u, out ), "clock wrap preserves live record" );

	Check( cache.Store( Key( 1 ), 1, 1, ticket.data(), ticket.size(), 0 ), "LRU insert one" );
	Check( cache.Store( Key( 2 ), 2, 2, ticket.data(), ticket.size(), 0 ), "LRU insert two" );
	Check( cache.Store( Key( 3 ), 3, 3, ticket.data(), ticket.size(), 0 ), "LRU replacement" );
	Check( !cache.Consume( Key( 1 ), 1, 1, out ), "oldest entry evicted" );
	Check( cache.Consume( Key( 3 ), 3, 1, out ), "new entry retained" );

	Check( cache.Store( Key( 4 ), 4, 4, ticket.data(), ticket.size(), 7 ), "expiry insert" );
	Check( !cache.Consume( Key( 4 ), 4, 7 + auth::CacheLifetimeMilliseconds + 1, out ),
		"expired record rejected and erased" );
}

} // namespace

int main() {
	TestParser();
	TestClientPacketAndProtocolSelection();
	TestCache();
	if ( failures != 0 ) {
		std::fprintf( stderr, "%d test(s) failed\n", failures );
		return 1;
	}
	std::puts( "retail auth challenge tests passed" );
	return 0;
}
