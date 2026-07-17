#include "netchan_profile.h"
#include "net_address.hpp"

static_assert( fnql::net::ParsePackedIpv4Host( "2825167223" ).valid );
static_assert( fnql::net::ParsePackedIpv4Host( "2825167223" ).value == 0xa864a177u );
static_assert( fnql::net::ParsePackedIpv4Host( "0" ).valid );
static_assert( fnql::net::ParsePackedIpv4Host( "4294967295" ).value == 0xffffffffu );
static_assert( !fnql::net::ParsePackedIpv4Host( "4294967296" ).valid );
static_assert( !fnql::net::ParsePackedIpv4Host( "168.100.161.119" ).valid );
static_assert( !fnql::net::ParsePackedIpv4Host( "-1" ).valid );
static_assert( !fnql::net::ParsePackedIpv4Host( "" ).valid );

static_assert( Netchan_SelectWireProfile( 68, 1 ) == NETCHAN_WIRE_LEGACY_Q3 );
static_assert( Netchan_SelectWireProfile( 71, 0 ) == NETCHAN_WIRE_IOQ3 );
static_assert( Netchan_SelectWireProfile( 71, 1 ) == NETCHAN_WIRE_LEGACY_Q3 );
static_assert( Netchan_SelectWireProfile( NETCHAN_QL_RETAIL_PROTOCOL_VERSION, 0 ) ==
	NETCHAN_WIRE_QL_RETAIL );
static_assert( Netchan_SelectWireProfile( NETCHAN_QL_RETAIL_PROTOCOL_VERSION, 1 ) ==
	NETCHAN_WIRE_QL_RETAIL );

static_assert( Netchan_WireHasFeature( NETCHAN_WIRE_LEGACY_Q3,
	NETCHAN_FEATURE_CLIENT_QPORT ) );
static_assert( Netchan_WireHasFeature( NETCHAN_WIRE_IOQ3,
	NETCHAN_FEATURE_CLIENT_QPORT ) );
static_assert( Netchan_WireHasFeature( NETCHAN_WIRE_QL_RETAIL,
	NETCHAN_FEATURE_CLIENT_QPORT ) );

static_assert( !Netchan_WireHasFeature( NETCHAN_WIRE_LEGACY_Q3,
	NETCHAN_FEATURE_SEQUENCE_CHECKSUM ) );
static_assert( Netchan_WireHasFeature( NETCHAN_WIRE_IOQ3,
	NETCHAN_FEATURE_SEQUENCE_CHECKSUM ) );
static_assert( !Netchan_WireHasFeature( NETCHAN_WIRE_QL_RETAIL,
	NETCHAN_FEATURE_SEQUENCE_CHECKSUM ) );

static_assert( Netchan_WireHasFeature( NETCHAN_WIRE_LEGACY_Q3,
	NETCHAN_FEATURE_RELIABLE_XOR ) );
static_assert( !Netchan_WireHasFeature( NETCHAN_WIRE_IOQ3,
	NETCHAN_FEATURE_RELIABLE_XOR ) );
static_assert( Netchan_WireHasFeature( NETCHAN_WIRE_QL_RETAIL,
	NETCHAN_FEATURE_RELIABLE_XOR ) );

int main() {
	return 0;
}
