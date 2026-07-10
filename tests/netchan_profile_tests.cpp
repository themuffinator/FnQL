#include "netchan_profile.h"

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
