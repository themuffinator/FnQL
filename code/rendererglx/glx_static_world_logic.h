#ifndef GLX_STATIC_WORLD_LOGIC_H
#define GLX_STATIC_WORLD_LOGIC_H

#include "../qcommon/q_shared.h"

#include <cctype>
#include <cstring>

namespace glx {

enum class StaticWorldPacketMatch {
	NoMatch,
	Partial,
	Full,
	ItemMismatch
};

struct StaticWorldRunPacket {
	StaticWorldPacketMatch match;
	int packetIndex;
};

enum class StaticWorldDrawPolicy {
	FullPackets,
	ContainedPackets,
	AllRuns
};

struct StaticWorldPacketView {
	const char *shaderName;
	int sort;
	int firstItem;
	int itemCount;
	int indexOffset;
	int indexBytes;
};

static ID_INLINE int GLX_StaticWorld_LogicStricmp( const char *lhs, const char *rhs )
{
	if ( !lhs ) {
		lhs = "";
	}
	if ( !rhs ) {
		rhs = "";
	}

	while ( *lhs || *rhs ) {
		const int l = std::tolower( static_cast<unsigned char>( *lhs ) );
		const int r = std::tolower( static_cast<unsigned char>( *rhs ) );
		if ( l != r ) {
			return l - r;
		}
		if ( *lhs ) {
			lhs++;
		}
		if ( *rhs ) {
			rhs++;
		}
	}

	return 0;
}

static ID_INLINE StaticWorldDrawPolicy GLX_StaticWorld_DrawPolicyFromString( const char *value )
{
	if ( !value || !*value || !GLX_StaticWorld_LogicStricmp( value, "full" ) ) {
		return StaticWorldDrawPolicy::FullPackets;
	}
	if ( !GLX_StaticWorld_LogicStricmp( value, "contained" ) ||
		!GLX_StaticWorld_LogicStricmp( value, "packet" ) ) {
		return StaticWorldDrawPolicy::ContainedPackets;
	}
	if ( !GLX_StaticWorld_LogicStricmp( value, "all" ) ||
		!GLX_StaticWorld_LogicStricmp( value, "legacy" ) ) {
		return StaticWorldDrawPolicy::AllRuns;
	}

	return StaticWorldDrawPolicy::FullPackets;
}

static ID_INLINE const char *GLX_StaticWorld_DrawPolicyName( StaticWorldDrawPolicy policy )
{
	switch ( policy ) {
	case StaticWorldDrawPolicy::AllRuns:
		return "all";
	case StaticWorldDrawPolicy::ContainedPackets:
		return "contained";
	case StaticWorldDrawPolicy::FullPackets:
	default:
		return "full";
	}
}

static ID_INLINE qboolean GLX_StaticWorld_DrawPolicyAllows( StaticWorldDrawPolicy policy,
	StaticWorldPacketMatch match )
{
	switch ( policy ) {
	case StaticWorldDrawPolicy::AllRuns:
		return qtrue;
	case StaticWorldDrawPolicy::ContainedPackets:
		return match == StaticWorldPacketMatch::Full ||
			match == StaticWorldPacketMatch::Partial ? qtrue : qfalse;
	case StaticWorldDrawPolicy::FullPackets:
	default:
		return match == StaticWorldPacketMatch::Full ? qtrue : qfalse;
	}
}

static ID_INLINE StaticWorldRunPacket GLX_StaticWorld_ClassifyRunAgainstPacketView(
	const StaticWorldPacketView &packet, int packetIndex, int offsetBytes, int runBytes,
	int firstItem, int itemCount, const char *shaderName, int sort )
{
	StaticWorldRunPacket result { StaticWorldPacketMatch::NoMatch, -1 };
	const long long runStart = offsetBytes;
	const long long runEnd = runStart + runBytes;
	const long long packetStart = packet.indexOffset;
	const long long packetEnd = packetStart + packet.indexBytes;

	if ( packet.indexBytes <= 0 ) {
		return result;
	}
	if ( runStart < packetStart || runEnd > packetEnd ) {
		return result;
	}
	if ( packet.sort != sort ) {
		return result;
	}
	if ( shaderName && *shaderName && packet.shaderName && *packet.shaderName &&
		std::strcmp( packet.shaderName, shaderName ) != 0 ) {
		return result;
	}

	if ( firstItem > 0 && itemCount > 0 && packet.firstItem > 0 && packet.itemCount > 0 ) {
		const long long runFirstItem = firstItem;
		const long long runLastItem = runFirstItem + itemCount - 1;
		const long long packetFirstItem = packet.firstItem;
		const long long packetLastItem = packetFirstItem + packet.itemCount - 1;

		if ( runFirstItem < packetFirstItem || runLastItem > packetLastItem ) {
			result.match = StaticWorldPacketMatch::ItemMismatch;
			result.packetIndex = packetIndex;
			return result;
		}
		if ( runFirstItem == packetFirstItem && itemCount == packet.itemCount &&
			runStart == packetStart && runEnd == packetEnd ) {
			result.match = StaticWorldPacketMatch::Full;
			result.packetIndex = packetIndex;
			return result;
		}
	} else if ( runStart == packetStart && runEnd == packetEnd ) {
		result.match = StaticWorldPacketMatch::Full;
		result.packetIndex = packetIndex;
		return result;
	}

	result.match = StaticWorldPacketMatch::Partial;
	result.packetIndex = packetIndex;
	return result;
}

} // namespace glx

#endif // GLX_STATIC_WORLD_LOGIC_H
