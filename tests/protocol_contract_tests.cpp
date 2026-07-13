#include "protocol_contract.hpp"

using namespace fnql::protocol;

static_assert( Find( 91 ) == &QuakeLive );
static_assert( Find( 68 ) == &LegacyQuake3 );
static_assert( Find( 71 ) == &Ioquake3 );
static_assert( Find( 999 ) == nullptr );
static_assert( &ForWireProfile( NETCHAN_WIRE_QL_RETAIL ) == &QuakeLive );
static_assert( &ForWireProfile( NETCHAN_WIRE_IOQ3 ) == &Ioquake3 );

static_assert( QuakeLive.wireProfile == NETCHAN_WIRE_QL_RETAIL );
static_assert( QuakeLive.Has( Capability::ReliableXor ) );
static_assert( !QuakeLive.Has( Capability::SequenceChecksum ) );
static_assert( QuakeLive.Has( Capability::PlatformAuthentication ) );
static_assert( QuakeLive.Has( Capability::WorkshopContent ) );
static_assert( QuakeLive.Has( Capability::RetailClientsMayJoin ) );
static_assert( QuakeLive.limits.downloadWindow == 8 );
static_assert( QuakeLive.limits.downloadBlockBytes == 2048 );

static_assert( !LegacyQuake3.Has( Capability::WorkshopContent ) );
static_assert( Ioquake3.Has( Capability::SequenceChecksum ) );
static_assert( !Ioquake3.Has( Capability::ReliableXor ) );
static_assert( !Ioquake3.Has( Capability::WorkshopContent ) );
static_assert( ModernizedQ3Limits.downloadWindow == 48 );
static_assert( ModernizedQ3Limits.downloadBlockBytes == 1024 );

static_assert( QuakeLive.commands.getChallenge == "getchallenge" );
static_assert( QuakeLive.commands.pureChecksums == "cp" );
static_assert( QuakeLive.infoKeys.serverId == "sv_serverid" );

int main() {
	return 0;
}
