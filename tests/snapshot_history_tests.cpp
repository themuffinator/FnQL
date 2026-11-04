extern "C" {
#include "../code/client/client.h"
}
#include "../code/client/snapshot_history.hpp"
#include "../code/client/demo_stream.hpp"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <memory>

#define CHECK( condition ) do { if ( !( condition ) ) { \
	std::fprintf( stderr, "line %d: %s\n", __LINE__, #condition ); \
	return 1; \
} } while ( false )

extern "C" void QDECL Com_Printf( const char *, ... ) {
}

extern "C" void QDECL Com_Error( errorParm_t, const char *format, ... ) {
	va_list args;
	va_start( args, format );
	std::vfprintf( stderr, format, args );
	va_end( args );
	std::fputc( '\n', stderr );
	std::exit( 1 );
}

// Optional read-only fixture scan. Production codecs consume field changes;
// no game module, renderer, Steam service, input device, or writable copy of
// the retail installation is needed. This checks wire order, not game state.
static int ValidateRetailDemo( const char *path ) {
	std::unique_ptr<FILE, decltype( &std::fclose )> file( std::fopen( path, "rb" ), &std::fclose );
	CHECK( file );
	std::array<byte, MAX_MSGLEN_BUF> bytes{};
	entityState_t baseline{};
	int messages = 0, snapshots = 0, records = 0, removals = 0;
	for ( ;; ) {
		const auto envelope = fnql::demo::ReadEnvelope(
			[&]( std::uint8_t *destination, std::size_t count ) {
				return std::fread( destination, 1, count, file.get() );
			}, bytes.data(), MAX_MSGLEN );
		if ( envelope.status == fnql::demo::EnvelopeStatus::EndOfStreamTrailer ) {
			break;
		}
		if ( envelope.status != fnql::demo::EnvelopeStatus::Message ) {
			std::fprintf( stderr,
				"retail demo incomplete/invalid after %d messages, %d snapshots, "
				"%d ordered records (%d removals): status %d, sequence %d, "
				"header %zu/8 bytes, payload %zu/%d bytes\n",
				messages, snapshots, records, removals,
				static_cast<int>( envelope.status ), envelope.sequence,
				envelope.headerBytes, envelope.payloadBytes, envelope.payloadLength );
			return 2;
		}
		++messages;
		msg_t msg{};
		MSG_Init( &msg, bytes.data(), MAX_MSGLEN );
		msg.cursize = envelope.payloadLength;
		MSG_BeginReading( &msg );
		MSG_ReadLong( &msg ); // reliable acknowledgement
		for ( ;; ) {
			const int command = MSG_ReadByte( &msg );
			CHECK( msg.readcount <= msg.cursize );
			if ( command == svc_EOF ) {
				break;
			}
			if ( command == svc_nop ) {
				continue;
			}
			if ( command == svc_serverCommand ) {
				MSG_ReadLong( &msg );
				MSG_ReadCommandStringForWireProfile( &msg, NETCHAN_WIRE_QL_RETAIL );
			} else if ( command == svc_gamestate ) {
				MSG_ReadLong( &msg );
				for ( ;; ) {
					const int item = MSG_ReadByte( &msg );
					CHECK( msg.readcount <= msg.cursize );
					if ( item == svc_EOF ) {
						break;
					}
					if ( item == svc_configstring ) {
						const int index = MSG_ReadShort( &msg );
						CHECK( index >= 0 && index < MAX_CONFIGSTRINGS );
						MSG_ReadBigStringForWireProfile( &msg, NETCHAN_WIRE_QL_RETAIL );
					} else {
						CHECK( item == svc_baseline );
						const int number = MSG_ReadEntitynum( &msg );
						entityState_t entity{};
						MSG_ReadDeltaEntity( &msg, &baseline, &entity, number );
					}
				}
				MSG_ReadLong( &msg ); // client number
				MSG_ReadLong( &msg ); // checksum feed
			} else {
				CHECK( command == svc_snapshot );
				MSG_ReadLong( &msg ); // server time
				MSG_ReadByte( &msg ); // delta distance
				MSG_ReadByte( &msg ); // flags
				const int areaBytes = MSG_ReadByte( &msg );
				std::array<byte, MAX_MAP_AREA_BYTES> area{};
				CHECK( areaBytes >= 0 && areaBytes <= MAX_MAP_AREA_BYTES );
				MSG_ReadData( &msg, area.data(), areaBytes );
				playerState_t player{};
				MSG_ReadDeltaPlayerstate( &msg, nullptr, &player );
				int previous = -1;
				for ( ;; ) {
					const int number = MSG_ReadEntitynum( &msg );
					CHECK( msg.readcount <= msg.cursize );
					if ( number == MAX_GENTITIES - 1 ) {
						break;
					}
					CHECK( fnql::snapshot::EntityFollows( previous, number, MAX_GENTITIES - 1 ) );
					previous = number;
					entityState_t entity{};
					MSG_ReadDeltaEntity( &msg, &baseline, &entity, number );
					++records;
					removals += entity.number == MAX_GENTITIES - 1;
				}
				++snapshots;
			}
			CHECK( msg.readcount <= msg.cursize );
		}
	}
	CHECK( !std::ferror( file.get() ) );
	CHECK( snapshots > 0 && records > 0 );
	std::printf( "retail demo: %d messages, %d snapshots, %d ordered records (%d removals)\n",
		messages, snapshots, records, removals );
	return 0;
}

int main( int argc, char **argv ) {
	if ( argc == 2 ) {
		return ValidateRetailDemo( argv[1] );
	}
	CHECK( argc == 1 );
	using namespace fnql::snapshot;
	using namespace fnql::net;
	clSnapshot_t history[PACKET_BACKUP]{};
	constexpr int lastSequence = ( std::numeric_limits<int>::max )();
	constexpr int firstNegative = ( std::numeric_limits<int>::min )();

	// Walk ordinary, packet-sequence rollover, and entity-cursor rollover
	// sessions. Every retained frame must remain addressable through both
	// imports' shared lookup, including retail's 384-entity native snapshot.
	for ( int firstSequence : { 100, lastSequence - 3 } ) {
		for ( int firstEntity : { 0, lastSequence - 200, -500 } ) {
			for ( auto &entry : history ) {
				entry = {};
			}
			int cursor = firstEntity;
			for ( int frame = 0; frame < PACKET_BACKUP * 2; ++frame ) {
				const int sequence = AdvanceSequence( firstSequence, frame );
				auto &stored = history[sequence & PACKET_MASK];
				stored = {};
				stored.valid = qtrue;
				stored.messageNum = sequence;
				stored.parseEntitiesNum = cursor;
				stored.numEntities = ( frame % 3 == 0 ) ? 384 : 1;
				cursor = CounterAdd( cursor, stored.numEntities );
				for ( int age = 0; age <= frame && age < PACKET_BACKUP; ++age ) {
					const int requested = RetreatSequence( sequence, age );
					const auto result = Lookup( requested, sequence, history, cursor, MAX_PARSE_ENTITIES );
					CHECK( result.status == LookupStatus::Available );
					CHECK( result.snapshot == &history[requested & PACKET_MASK] );
					CHECK( result.snapshot->messageNum == requested );
				}
				CHECK( Lookup( NextSequence( sequence ), sequence, history, cursor,
					MAX_PARSE_ENTITIES ).status == LookupStatus::Future );
				CHECK( Lookup( RetreatSequence( sequence, PACKET_BACKUP ), sequence,
					history, cursor, MAX_PARSE_ENTITIES ).status == LookupStatus::Unavailable );
			}
		}
	}

	// A reused slot cannot satisfy a different message number even when it
	// still carries a valid bit (e.g. an out-of-order demo message).
	auto &stored = history[10 & PACKET_MASK];
	stored = {};
	stored.valid = qtrue;
	stored.messageNum = 10 + PACKET_BACKUP;
	CHECK( Lookup( 10, 10, history, 0, MAX_PARSE_ENTITIES ).status == LookupStatus::Unavailable );
	stored.messageNum = 10;
	stored.valid = qfalse;
	CHECK( Lookup( 10, 10, history, 0, MAX_PARSE_ENTITIES ).status == LookupStatus::Unavailable );
	stored.valid = qtrue;
	CHECK( Lookup( 10, 10, history, 0, MAX_PARSE_ENTITIES ).status == LookupStatus::Available );

	// Preserve the existing expiration boundary, including after 32-bit wrap.
	stored.parseEntitiesNum = lastSequence - 10;
	stored.numEntities = 384;
	CHECK( Lookup( 10, 10, history, CounterAdd( stored.parseEntitiesNum, MAX_PARSE_ENTITIES - 1 ),
		MAX_PARSE_ENTITIES ).status == LookupStatus::Available );
	CHECK( Lookup( 10, 10, history, CounterAdd( stored.parseEntitiesNum, MAX_PARSE_ENTITIES ),
		MAX_PARSE_ENTITIES ).status == LookupStatus::Unavailable );
	CHECK( Lookup( 10, 10, history, CounterSubtract( stored.parseEntitiesNum, 1 ),
		MAX_PARSE_ENTITIES ).status == LookupStatus::Unavailable );
	CHECK( Lookup( 10, 10, history, CounterAdd( stored.parseEntitiesNum, 383 ),
		MAX_PARSE_ENTITIES ).status == LookupStatus::Unavailable );
	stored.numEntities = -1;
	CHECK( Lookup( 10, 10, history, CounterAdd( stored.parseEntitiesNum, 384 ),
		MAX_PARSE_ENTITIES ).status == LookupStatus::Unavailable );

	CHECK( EntityIndex( lastSequence, 1, MAX_PARSE_ENTITIES - 1 ) == 0 );
	CHECK( EntityIndex( firstNegative, 1, MAX_PARSE_ENTITIES - 1 ) == 1 );
	CHECK( EntityIndex( -1, 1, MAX_PARSE_ENTITIES - 1 ) == 0 );
	CHECK( EntityIndex( 100, 3, MAX_PARSE_ENTITIES - 1 ) == 103 );

	// The wire admits every non-sentinel entity, not just the engine's
	// 256-entity server budget or either module's presentation limit.
	int previous = -1;
	for ( int entity = 0; entity < MAX_GENTITIES - 1; ++entity ) {
		CHECK( EntityFollows( previous, entity, MAX_GENTITIES - 1 ) );
		previous = entity;
	}
	CHECK( EntityFollows( 4, 800, MAX_GENTITIES - 1 ) );
	CHECK( !EntityFollows( 4, 4, MAX_GENTITIES - 1 ) );
	CHECK( !EntityFollows( 4, 3, MAX_GENTITIES - 1 ) );
	CHECK( !EntityFollows( -1, -1, MAX_GENTITIES - 1 ) );
	CHECK( !EntityFollows( 4, MAX_GENTITIES - 1, MAX_GENTITIES - 1 ) );
	CHECK( !EntityFollows( 4, MAX_GENTITIES, MAX_GENTITIES - 1 ) );
	std::puts( "snapshot history tests passed" );
	return 0;
}
