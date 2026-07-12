extern "C" {
#include "q_shared.h"
#include "qcommon.h"
}

#include <array>
#include <cstdarg>
#include <cstdlib>
#include <cstring>

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
	if ( const int result = TestUserCommandRoundTrip() ) return result;
	if ( const int result = TestEntityRoundTrip() ) return result;
	if ( const int result = TestPlayerStateRoundTrip() ) return result;
	return 0;
}
