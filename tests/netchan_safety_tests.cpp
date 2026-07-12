#include "netchan_safety.hpp"

#include <cstdint>
#include <limits>

#define CHECK( expression ) do { if ( !( expression ) ) return __LINE__; } while ( false )

int main() {
	using namespace fnql::net;

	std::uint8_t byte = 0;
	CHECK( IsValidPayload( nullptr, 0, 0 ) );
	CHECK( IsValidPayload( &byte, 1, 1 ) );
	CHECK( !IsValidPayload( nullptr, 1, 1 ) );
	CHECK( !IsValidPayload( &byte, -1, 1 ) );
	CHECK( !IsValidPayload( &byte, 2, 1 ) );

	CHECK( NextSequence( 0 ) == 1 );
	CHECK( NextSequence( std::numeric_limits<int>::max() ) == 0 );
	CHECK( PreviousSequence( 0 ) == std::numeric_limits<int>::max() );
	CHECK( RetreatSequence( 2, 4u ) == std::numeric_limits<int>::max() - 1 );
	CHECK( AdvanceSequence( std::numeric_limits<int>::max(), 2u ) == 1 );
	CHECK( SequenceDistance( 7, 4 ) == 3u );
	CHECK( SequenceDistance( 0, std::numeric_limits<int>::max() ) == 1u );
	CHECK( IsNewerSequence( 7, 4 ) );
	CHECK( IsNewerSequence( 0, std::numeric_limits<int>::max() ) );
	CHECK( !IsNewerSequence( 4, 4 ) );
	CHECK( !IsNewerSequence( std::numeric_limits<int>::max(), 0 ) );

	CHECK( NextCounter( std::numeric_limits<int>::max() ) ==
		std::numeric_limits<int>::min() );
	CHECK( CounterAdd( std::numeric_limits<int>::max(), 2u ) ==
		std::numeric_limits<int>::min() + 1 );
	CHECK( CounterDistance( std::numeric_limits<int>::min(),
		std::numeric_limits<int>::max() ) == 1u );
	CHECK( IsNewerCounter( std::numeric_limits<int>::min(),
		std::numeric_limits<int>::max() ) );
	std::uint32_t pending = 0;
	CHECK( PendingCounterCount( 10, 7, pending ) && pending == 3u );
	CHECK( PendingCounterCount( std::numeric_limits<int>::min(),
		std::numeric_limits<int>::max(), pending ) && pending == 1u );
	CHECK( !PendingCounterCount( 7, 10, pending ) );

	CHECK( FragmentFits( 4, 12, 8, 16, 8 ) );
	CHECK( FragmentFits( 12, 12, 16, 16, 0 ) );
	CHECK( !FragmentFits( -1, 12, 0, 16, 1 ) );
	CHECK( !FragmentFits( 13, 12, 0, 16, 0 ) );
	CHECK( !FragmentFits( 4, 12, 8, 16, 9 ) );
	CHECK( !FragmentFits( 4, 13, 12, 16, 5 ) );
	CHECK( !FragmentFits( 4, 13, -1, 16, 1 ) );
	CHECK( !FragmentFits( 4, 13, 0, 16, -1 ) );

	CHECK( SequenceChecksum( 0x12345678, 0 ) == 0x12345678u );
	CHECK( SequenceChecksum( -1, 2 ) == 1u );

	return 0;
}
