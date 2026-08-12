extern "C" {
#include "q_shared.h"
#include "qcommon.h"
}

#include <array>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <limits>

#define CHECK( expression ) do { if ( !( expression ) ) return __LINE__; } while ( false )

extern "C" void QDECL Com_Printf( const char *, ... ) {
}

extern "C" void QDECL Com_Error( errorParm_t, const char *, ... ) {
	std::abort();
}

namespace {

int TestBounds() {
	std::array<byte, 6> guarded{ 0xa5, 0, 0, 0, 0, 0x5a };
	msg_t writer{};
	MSG_InitOOB( &writer, guarded.data() + 1, 4 );
	MSG_WriteLong( &writer, 0x12345678 );
	CHECK( !writer.overflowed );
	CHECK( writer.cursize == 4 );
	MSG_WriteByte( &writer, 1 );
	CHECK( writer.overflowed );
	CHECK( writer.cursize == 4 );
	CHECK( guarded.front() == 0xa5 );
	CHECK( guarded.back() == 0x5a );

	msg_t reader{};
	MSG_InitOOB( &reader, guarded.data() + 1, 4 );
	reader.cursize = 1;
	CHECK( MSG_ReadLong( &reader ) == -1 );
	CHECK( reader.readcount > reader.cursize );
	CHECK( guarded.front() == 0xa5 );
	CHECK( guarded.back() == 0x5a );
	return 0;
}

int TestWireProfileUserCommandHash() {
	static const char percentCommand[] = "say 100% ready";
	CHECK( MSG_HashKeyForWireProfile(
		NETCHAN_WIRE_QL_RETAIL, percentCommand, 32 ) == 140014 );
	CHECK( MSG_HashKeyForWireProfile(
		NETCHAN_WIRE_LEGACY_Q3, percentCommand, 32 ) == 140893 );
	CHECK( MSG_HashKeyForWireProfile(
		NETCHAN_WIRE_IOQ3, percentCommand, 32 ) == 140893 );

	// Protocol 91 follows the signed-byte behavior of retail's Win32 build,
	// independent of the host compiler's default char signedness.
	static const char highBitCommand[] = { '\x80', '\xff', '%', '\0' };
	CHECK( MSG_HashKeyForWireProfile(
		NETCHAN_WIRE_QL_RETAIL, highBitCommand, 32 ) == -10865 );
	CHECK( MSG_HashKeyForWireProfile(
		NETCHAN_WIRE_LEGACY_Q3, highBitCommand, 32 ) == 16544 );

	std::array<byte, 64> storage{};
	usercmd_t baseline{};
	usercmd_t sent{};
	usercmd_t received{};
	sent.serverTime = 158600;
	sent.angles[PITCH] = 41556;
	sent.buttons = BUTTON_ATTACK;
	sent.forwardmove = 127;
	const int retailKey = 0x10203040 ^ MSG_HashKeyForWireProfile(
		NETCHAN_WIRE_QL_RETAIL, percentCommand, 32 );

	msg_t writer{};
	MSG_Init( &writer, storage.data(), static_cast<int>( storage.size() ) );
	MSG_WriteDeltaUsercmdKey( &writer, retailKey, &baseline, &sent );
	CHECK( !writer.overflowed );

	msg_t reader{};
	MSG_Init( &reader, storage.data(), static_cast<int>( storage.size() ) );
	reader.cursize = writer.cursize;
	MSG_BeginReading( &reader );
	MSG_ReadDeltaUsercmdKey( &reader, retailKey, &baseline, &received );
	CHECK( received.serverTime == sent.serverTime );
	CHECK( received.angles[PITCH] == sent.angles[PITCH] );
	CHECK( received.buttons == sent.buttons );
	CHECK( received.forwardmove == sent.forwardmove );
	return 0;
}

int TestWireProfileCommandStrings() {
	static const char command[] = { 'p', 'r', 'i', 'n', 't', ' ', '"',
		'c', 'a', 'f', '\xc3', '\xa9', ' ', '%', '"', '\0' };
	std::array<byte, 64> storage{};
	msg_t writer{};
	MSG_Init( &writer, storage.data(), static_cast<int>( storage.size() ) );
	MSG_WriteStringForWireProfile(
		&writer, command, NETCHAN_WIRE_QL_RETAIL );
	CHECK( !writer.overflowed );

	msg_t retailReader{};
	MSG_Init( &retailReader, storage.data(), static_cast<int>( storage.size() ) );
	retailReader.cursize = writer.cursize;
	MSG_BeginReading( &retailReader );
	const char *retail = MSG_ReadStringForWireProfile(
		&retailReader, NETCHAN_WIRE_QL_RETAIL );
	CHECK( static_cast<unsigned char>( retail[10] ) == 0xc3 );
	CHECK( static_cast<unsigned char>( retail[11] ) == 0xa9 );
	CHECK( retail[13] == '.' );
	CHECK( MSG_HashKeyForWireProfile(
		NETCHAN_WIRE_QL_RETAIL, retail, 32 ) == 109177 );

	msg_t commandReader{};
	MSG_Init( &commandReader, storage.data(), static_cast<int>( storage.size() ) );
	commandReader.cursize = writer.cursize;
	MSG_BeginReading( &commandReader );
	const char *wireCommand = MSG_ReadCommandStringForWireProfile(
		&commandReader, NETCHAN_WIRE_QL_RETAIL );
	CHECK( static_cast<unsigned char>( wireCommand[10] ) == 0xc3 );
	CHECK( static_cast<unsigned char>( wireCommand[11] ) == 0xa9 );
	CHECK( wireCommand[13] == '%' );
	CHECK( MSG_HashKeyForWireProfile(
		NETCHAN_WIRE_QL_RETAIL, wireCommand, 32 ) == 107782 );

	msg_t legacyReader{};
	MSG_Init( &legacyReader, storage.data(), static_cast<int>( storage.size() ) );
	legacyReader.cursize = writer.cursize;
	MSG_BeginReading( &legacyReader );
	const char *legacy = MSG_ReadStringForWireProfile(
		&legacyReader, NETCHAN_WIRE_LEGACY_Q3 );
	CHECK( legacy[10] == '.' );
	CHECK( legacy[11] == '.' );
	CHECK( legacy[13] == '.' );
	return 0;
}

int TestUserCommandRoundTrip() {
	std::array<byte, 256> storage{};
	msg_t writer{};
	usercmd_t from{};
	usercmd_t sent{};
	usercmd_t received{};

	from.serverTime = 1000;
	from.fov = 100;
	sent = from;
	sent.serverTime = 1008;
	sent.angles[0] = 1234;
	sent.angles[1] = 63191;
	sent.angles[2] = 32767;
	sent.buttons = 0x5a5a;
	sent.weapon = 9;
	sent.weaponPrimary = 3;
	sent.fov = 110;
	sent.forwardmove = -127;
	sent.rightmove = 64;
	sent.upmove = -12;

	MSG_Init( &writer, storage.data(), static_cast<int>( storage.size() ) );
	MSG_WriteDeltaUsercmdKey( &writer, 0x10203040, &from, &sent );
	CHECK( !writer.overflowed );

	msg_t reader{};
	MSG_Init( &reader, storage.data(), static_cast<int>( storage.size() ) );
	reader.cursize = writer.cursize;
	MSG_BeginReading( &reader );
	MSG_ReadDeltaUsercmdKey( &reader, 0x10203040, &from, &received );
	CHECK( sent.serverTime == received.serverTime );
	CHECK( sent.angles[0] == received.angles[0] );
	CHECK( sent.angles[1] == received.angles[1] );
	CHECK( sent.angles[2] == received.angles[2] );
	CHECK( sent.buttons == received.buttons );
	CHECK( sent.weapon == received.weapon );
	CHECK( sent.weaponPrimary == received.weaponPrimary );
	CHECK( sent.fov == received.fov );
	CHECK( sent.forwardmove == received.forwardmove );
	CHECK( sent.rightmove == received.rightmove );
	CHECK( sent.upmove == received.upmove );
	CHECK( reader.readcount <= reader.cursize );
	return 0;
}

int TestUserCommandClockWrap() {
	std::array<byte, 64> storage{};
	msg_t writer{};
	usercmd_t from{};
	usercmd_t sent{};
	usercmd_t received{};

	from.serverTime = ( std::numeric_limits<int>::max )() - 3;
	sent = from;
	// Eight milliseconds later in the protocol's wrapping 32-bit clock.
	sent.serverTime = ( std::numeric_limits<int>::min )() + 4;

	MSG_Init( &writer, storage.data(), static_cast<int>( storage.size() ) );
	MSG_WriteDeltaUsercmdKey( &writer, 0x10203040, &from, &sent );
	CHECK( !writer.overflowed );

	msg_t reader{};
	MSG_Init( &reader, storage.data(), static_cast<int>( storage.size() ) );
	reader.cursize = writer.cursize;
	MSG_BeginReading( &reader );
	MSG_ReadDeltaUsercmdKey( &reader, 0x10203040, &from, &received );
	CHECK( received.serverTime == sent.serverTime );
	CHECK( reader.readcount <= reader.cursize );
	return 0;
}

int TestEntityRoundTrip() {
	std::array<byte, MAX_MSGLEN_BUF> storage{};
	msg_t writer{};
	entityState_t baseline{};
	entityState_t sent{};
	entityState_t received{};

	sent.number = 17;
	sent.pos.trType = TR_QL_ACCEL;
	sent.pos.trTime = 0x10203040;
	sent.pos.trBase[0] = 123.5f;
	sent.pos.trDelta[2] = -42.25f;
	sent.pos.gravity = 800.0f;
	sent.event = 777;
	sent.eFlags = 0x54321;
	sent.weapon = 8;
	sent.health = 125;
	sent.armor = 75;
	sent.location = 63;
	sent.doubleJumped = 1;

	MSG_Init( &writer, storage.data(), static_cast<int>( storage.size() ) );
	MSG_WriteDeltaEntity( &writer, &baseline, &sent, qtrue );
	CHECK( !writer.overflowed );

	msg_t reader{};
	MSG_Init( &reader, storage.data(), static_cast<int>( storage.size() ) );
	reader.cursize = writer.cursize;
	MSG_BeginReading( &reader );
	const int entityNumber = MSG_ReadEntitynum( &reader );
	CHECK( entityNumber == sent.number );
	MSG_ReadDeltaEntity( &reader, &baseline, &received, entityNumber );
	CHECK( std::memcmp( &sent, &received, sizeof( sent ) ) == 0 );
	CHECK( reader.readcount <= reader.cursize );
	return 0;
}

int TestPlayerStateRoundTrip() {
	std::array<byte, MAX_MSGLEN_BUF> storage{};
	msg_t writer{};
	playerState_t baseline{};
	playerState_t sent{};
	playerState_t received{};

	sent.commandTime = 424242;
	sent.origin[0] = 12.5f;
	sent.velocity[2] = -320.0f;
	sent.weaponTime = -125;
	sent.pm_flags = 0x123456;
	sent.weapon = 7;
	sent.weaponPrimary = 2;
	sent.fov = 103;
	sent.location = 14;
	sent.forwardmove = -127;
	sent.rightmove = 100;
	sent.upmove = -4;
	sent.stats[3] = 321;
	sent.persistant[7] = -45;
	sent.ammo[6] = 200;
	sent.powerups[2] = 999999;

	MSG_Init( &writer, storage.data(), static_cast<int>( storage.size() ) );
	MSG_WriteDeltaPlayerstate( &writer, &baseline, &sent );
	CHECK( !writer.overflowed );

	msg_t reader{};
	MSG_Init( &reader, storage.data(), static_cast<int>( storage.size() ) );
	reader.cursize = writer.cursize;
	MSG_BeginReading( &reader );
	MSG_ReadDeltaPlayerstate( &reader, &baseline, &received );
	CHECK( std::memcmp( &sent, &received, sizeof( sent ) ) == 0 );
	CHECK( reader.readcount <= reader.cursize );
	return 0;
}

} // namespace

int main() {
	if ( const int result = TestBounds() ) return result;
	if ( const int result = TestWireProfileUserCommandHash() ) return result;
	if ( const int result = TestWireProfileCommandStrings() ) return result;
	if ( const int result = TestUserCommandRoundTrip() ) return result;
	if ( const int result = TestUserCommandClockWrap() ) return result;
	if ( const int result = TestEntityRoundTrip() ) return result;
	if ( const int result = TestPlayerStateRoundTrip() ) return result;
	return 0;
}
