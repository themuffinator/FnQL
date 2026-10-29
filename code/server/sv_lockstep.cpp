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
// sv_lockstep.cpp -- deterministic dedicated-server lockstep tooling
//
// Default off.  `sv_lockstep 1` on a dedicated server (set on the command
// line) turns the server into a reproducible test rig for game-module
// differentials:
//
//   * every server frame advances exactly 1000 / sv_fps msec and the frames
//     run back to back, on a virtual clock that never reads the wall clock;
//   * every engine-owned seed and clock the game can observe is pinned
//     (GAME_INIT seed, the engine rand() seed, trap_Milliseconds,
//     trap_RealTime, the match GUID, the server id and the checksum feed);
//   * a scenario script declares ordinary, non-bot clients without a network
//     channel and drives them with a per-frame usercmd and command timeline;
//   * a recorder writes, after every game frame, the scripted clients'
//     playerStates, every client-visible entity, configstring changes and the
//     game's reliable server commands.
//
// The script grammar, the recording format and the determinism contract are
// documented in docs/fnql/SERVER_LOCKSTEP.md.  Nothing here runs unless the
// mode is enabled; every hook outside this file is a guarded call into it.

#include "server.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#endif

namespace {

// ---------------------------------------------------------------------------
// Constants (documented in docs/fnql/SERVER_LOCKSTEP.md)
// ---------------------------------------------------------------------------

constexpr int ScriptFormatVersion = 1;
constexpr std::uint32_t RecordFormatVersion = 1;
constexpr std::array<char, 8> RecordMagic = { 'F', 'N', 'Q', 'L', 'L', 'S', 'T', 'P' };

// The virtual clock (com_frameTime, trap_Milliseconds) starts here.
constexpr int VirtualClockOrigin = 1000000;
// trap_RealTime reports this UTC instant plus the elapsed virtual seconds:
// 2026-01-01T00:00:00Z.
constexpr std::int64_t VirtualRealTimeEpoch = 1767225600;

constexpr int ScriptByteLimit = 4 * 1024 * 1024;
constexpr std::size_t ScriptLineLimit = 4096;
constexpr int ScriptFrameLimit = 100000000;
constexpr std::size_t PendingServerCommandLimit = 65536;
constexpr std::size_t ClientIdLimit = 31;
constexpr std::size_t AddressLimit = 63;

enum class RecordTag : std::uint8_t {
	Frame = 'F',
	Input = 'U',
	Command = 'K',
	PlayerState = 'P',
	Entity = 'E',
	EntityRemoved = 'R',
	Configstring = 'C',
	ServerCommand = 'S',
	ServerCommandOverflow = 'O',
	FrameEnd = 'X',
	End = 'Q',
};

enum class CommandKind : std::uint8_t {
	Console = 0,
	Client = 1,
	Userinfo = 2,
};

enum class EndReason : std::uint32_t {
	FrameLimit = 0,
	Shutdown = 1,
};

// ---------------------------------------------------------------------------
// Cvars and process-wide pinned state
// ---------------------------------------------------------------------------

cvar_t *sv_lockstep;
cvar_t *sv_lockstepSeed;
cvar_t *sv_lockstepTraceRand;
cvar_t *sv_lockstepScript;
cvar_t *sv_lockstepRecord;
cvar_t *sv_lockstepFrames;

bool lockstepEnabled = false;
std::uint32_t lockstepSeed = 0;
int lockstepFrameLimit = 0;
std::uint32_t gameInitCount = 0;
std::uint32_t levelSeedCount = 0;
std::uint32_t matchGuidCount = 0;

[[noreturn]] void Fatal( const char *fmt, ... ) __attribute__ ((format (printf, 1, 2)));

void Fatal( const char *fmt, ... ) {
	std::array<char, 2048> message{};
	va_list argptr;
	va_start( argptr, fmt );
	Q_vsnprintf( message.data(), message.size(), fmt, argptr );
	va_end( argptr );
	Com_Error( ERR_FATAL, "sv_lockstep: %s", message.data() );
}

std::uint32_t Mix( std::uint32_t a, std::uint32_t b ) {
	std::uint32_t x = a ^ ( b * 0x9E3779B9u );
	x ^= x >> 16;
	x *= 0x7FEB352Du;
	x ^= x >> 15;
	x *= 0x846CA68Bu;
	x ^= x >> 16;
	return x;
}

bool ParseUnsigned( const char *text, unsigned long long maximum, unsigned long long &value ) {
	if ( !text || !*text ) {
		return false;
	}
	unsigned long long result = 0;
	for ( const char *p = text; *p; ++p ) {
		if ( *p < '0' || *p > '9' ) {
			return false;
		}
		const unsigned digit = static_cast<unsigned>( *p - '0' );
		if ( result > ( maximum - digit ) / 10u ) {
			return false;
		}
		result = result * 10u + digit;
	}
	value = result;
	return true;
}

bool ParseSigned( const char *text, long long minimum, long long maximum, long long &value ) {
	if ( !text || !*text ) {
		return false;
	}
	const bool negative = *text == '-';
	const char *digits = ( negative || *text == '+' ) ? text + 1 : text;
	unsigned long long magnitude = 0;
	const unsigned long long limit = negative
		? static_cast<unsigned long long>( -( minimum + 1 ) ) + 1u
		: static_cast<unsigned long long>( maximum );
	if ( !ParseUnsigned( digits, limit, magnitude ) ) {
		return false;
	}
	value = negative ? -static_cast<long long>( magnitude - 1u ) - 1 : static_cast<long long>( magnitude );
	return value >= minimum && value <= maximum;
}

bool ParseDouble( const char *text, double limit, double &value ) {
	if ( !text || !*text ) {
		return false;
	}
	char *end = nullptr;
	const double result = std::strtod( text, &end );
	if ( end == text || *end != '\0' || !std::isfinite( result ) || std::fabs( result ) > limit ) {
		return false;
	}
	value = result;
	return true;
}

// ---------------------------------------------------------------------------
// Scenario script
// ---------------------------------------------------------------------------

// Sticky per-client input state; `at` entries change fields from a frame on.
struct InputValues {
	std::array<double, 3> angles{};		// usercmd angles in degrees (pitch, yaw, roll)
	std::array<double, 3> rates{};		// degrees added after every frame
	int buttons = 0;
	int anyKey = -1;			// -1 auto (BUTTON_ANY while any input is held), 0 off, 1 on
	int weapon = -1;			// -1 auto (the playerState's current weapon)
	int weaponPrimary = 0;
	int fov = 0;
	std::array<int, 3> moves{};		// forward, right, up
};

enum InputField : unsigned {
	FieldPitch = 1u << 0,
	FieldYaw = 1u << 1,
	FieldRoll = 1u << 2,
	FieldPitchRate = 1u << 3,
	FieldYawRate = 1u << 4,
	FieldRollRate = 1u << 5,
	FieldButtons = 1u << 6,
	FieldAnyKey = 1u << 7,
	FieldWeapon = 1u << 8,
	FieldWeaponPrimary = 1u << 9,
	FieldFov = 1u << 10,
	FieldForward = 1u << 11,
	FieldRight = 1u << 12,
	FieldUp = 1u << 13,
};

struct InputChange {
	unsigned mask = 0;
	InputValues values;
};

struct ScriptClient {
	std::string id;
	std::vector<std::pair<std::string, std::string>> userinfo;
	std::string address;
	int ping = 0;
};

struct CommandEvent {
	int frame = 0;
	int line = 0;
	CommandKind kind = CommandKind::Console;
	int client = -1;		// script client index, -1 for console commands
	std::string text;		// command text, or the userinfo key
	std::string value;		// userinfo value
};

struct InputEvent {
	int frame = 0;
	int line = 0;
	int client = 0;
	InputChange change;
};

struct Script {
	std::vector<ScriptClient> clients;
	std::vector<CommandEvent> commands;
	std::vector<InputEvent> inputs;
};

struct ButtonName {
	const char *name;
	int bit;
};

constexpr std::array<ButtonName, 12> ButtonNames = { {
	{ "attack", BUTTON_ATTACK },
	{ "talk", BUTTON_TALK },
	{ "use", BUTTON_USE_HOLDABLE },
	{ "gesture", BUTTON_GESTURE },
	{ "walking", BUTTON_WALKING },
	{ "affirmative", BUTTON_AFFIRMATIVE },
	{ "negative", BUTTON_NEGATIVE },
	{ "getflag", BUTTON_GETFLAG },
	{ "guardbase", BUTTON_GUARDBASE },
	{ "patrol", BUTTON_PATROL },
	{ "followme", BUTTON_FOLLOWME },
	{ "any", BUTTON_ANY },
} };

bool IsInfoSafe( const std::string &text, std::size_t limit ) {
	if ( text.empty() || text.size() >= limit ) {
		return false;
	}
	for ( const char c : text ) {
		const unsigned char u = static_cast<unsigned char>( c );
		if ( u < 0x20 || u >= 0x7F || c == '\\' || c == '"' || c == ';' ) {
			return false;
		}
	}
	return true;
}

bool IsIdentifier( const std::string &text ) {
	if ( text.empty() || text.size() > ClientIdLimit ) {
		return false;
	}
	for ( const char c : text ) {
		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' ) ) {
			return false;
		}
	}
	return true;
}

bool IsAddress( const std::string &text ) {
	if ( text.empty() || text.size() > AddressLimit ) {
		return false;
	}
	for ( const char c : text ) {
		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' )
			|| c == '.' || c == ':' || c == '[' || c == ']' || c == '-' || c == '_' ) ) {
			return false;
		}
	}
	return true;
}

class ScriptParser {
public:
	ScriptParser( const char *name, const char *text, int length )
		: name_( name ), text_( text ), length_( length ) {}

	bool Parse( Script &script );
	const std::string &Error() const { return error_; }

private:
	bool Fail( const char *fmt, ... ) __attribute__ ((format (printf, 2, 3)));
	bool NextToken( const char *&cursor, std::string &token );
	static const char *SkipSpace( const char *cursor );
	static std::string Rest( const char *cursor );
	bool ParseFrame( const std::string &token, int &frame );
	bool FindClient( const Script &script, const std::string &id, int &index );
	bool ParseLine( Script &script, const char *cursor );
	bool ParseClient( Script &script, const char *cursor );
	bool ParseAt( Script &script, const char *cursor );
	bool ParseField( const std::string &assignment, InputChange &change );
	bool ParseButtons( const std::string &value, int &buttons );
	bool ValidateUserinfo( const ScriptClient &client );

	const char *name_;
	const char *text_;
	int length_;
	int line_ = 0;
	bool sawVersion_ = false;
	std::string error_;
};

bool ScriptParser::Fail( const char *fmt, ... ) {
	std::array<char, 1024> message{};
	va_list argptr;
	va_start( argptr, fmt );
	Q_vsnprintf( message.data(), message.size(), fmt, argptr );
	va_end( argptr );
	error_ = std::string( name_ ) + ":" + std::to_string( line_ ) + ": " + message.data();
	return false;
}

const char *ScriptParser::SkipSpace( const char *cursor ) {
	while ( *cursor == ' ' || *cursor == '\t' ) {
		++cursor;
	}
	return cursor;
}

std::string ScriptParser::Rest( const char *cursor ) {
	cursor = SkipSpace( cursor );
	std::string rest( cursor );
	while ( !rest.empty() && ( rest.back() == ' ' || rest.back() == '\t' ) ) {
		rest.pop_back();
	}
	return rest;
}

// One whitespace-separated token; double quotes group a token (no escapes).
bool ScriptParser::NextToken( const char *&cursor, std::string &token ) {
	cursor = SkipSpace( cursor );
	token.clear();
	if ( *cursor == '\0' ) {
		return false;
	}
	if ( *cursor == '"' ) {
		const char *end = std::strchr( cursor + 1, '"' );
		if ( !end ) {
			Fail( "unterminated quoted token" );
			return false;
		}
		token.assign( cursor + 1, end );
		cursor = end + 1;
		if ( *cursor != '\0' && *cursor != ' ' && *cursor != '\t' ) {
			Fail( "a quoted token must be followed by whitespace" );
			return false;
		}
		return true;
	}
	const char *start = cursor;
	while ( *cursor && *cursor != ' ' && *cursor != '\t' ) {
		if ( *cursor == '"' ) {
			Fail( "unexpected quote inside a token" );
			return false;
		}
		++cursor;
	}
	token.assign( start, cursor );
	return true;
}

bool ScriptParser::ParseFrame( const std::string &token, int &frame ) {
	long long value = 0;
	if ( !ParseSigned( token.c_str(), 0, ScriptFrameLimit, value ) ) {
		return Fail( "frame '%s' must be an integer from 0 to %d", token.c_str(), ScriptFrameLimit );
	}
	frame = static_cast<int>( value );
	return true;
}

bool ScriptParser::FindClient( const Script &script, const std::string &id, int &index ) {
	for ( std::size_t i = 0; i < script.clients.size(); ++i ) {
		if ( script.clients[i].id == id ) {
			index = static_cast<int>( i );
			return true;
		}
	}
	return Fail( "unknown client '%s' (declare it with 'client' first)", id.c_str() );
}

bool ScriptParser::ValidateUserinfo( const ScriptClient &client ) {
	std::array<char, MAX_INFO_STRING> userinfo{};
	bool hasName = false;
	for ( const auto &pair : client.userinfo ) {
		hasName = hasName || pair.first == "name";
	}
	if ( !hasName && !Info_SetValueForKey( userinfo.data(), "name", client.id.c_str() ) ) {
		return Fail( "client '%s': userinfo does not fit", client.id.c_str() );
	}
	for ( const auto &pair : client.userinfo ) {
		if ( !Info_SetValueForKey( userinfo.data(), pair.first.c_str(), pair.second.c_str() ) ) {
			return Fail( "client '%s': userinfo key '%s' does not fit", client.id.c_str(), pair.first.c_str() );
		}
	}
	// leave room for the engine-owned "ip" and "tld" keys
	if ( std::strlen( userinfo.data() ) + 4 + AddressLimit + 9 >= userinfo.size() ) {
		return Fail( "client '%s': userinfo leaves no room for the engine's ip and tld keys", client.id.c_str() );
	}
	return true;
}

bool ScriptParser::ParseClient( Script &script, const char *cursor ) {
	std::string id;
	if ( !NextToken( cursor, id ) ) {
		return error_.empty() ? Fail( "usage: client <id> [<userinfo key> <value>]..." ) : false;
	}
	if ( !IsIdentifier( id ) ) {
		return Fail( "client id '%s' must be 1-%u characters of [A-Za-z0-9_-]", id.c_str(),
			static_cast<unsigned>( ClientIdLimit ) );
	}
	for ( const ScriptClient &existing : script.clients ) {
		if ( existing.id == id ) {
			return Fail( "client '%s' is declared twice", id.c_str() );
		}
	}
	if ( script.clients.size() >= MAX_CLIENTS ) {
		return Fail( "more than %d clients", MAX_CLIENTS );
	}
	ScriptClient client;
	client.id = id;
	std::string key;
	std::string value;
	while ( NextToken( cursor, key ) ) {
		if ( !NextToken( cursor, value ) ) {
			return error_.empty() ? Fail( "userinfo key '%s' has no value", key.c_str() ) : false;
		}
		if ( !IsInfoSafe( key, MAX_INFO_KEY ) || !IsInfoSafe( value, MAX_INFO_VALUE ) ) {
			return Fail( "userinfo '%s' '%s' is empty or contains \\ \" ; or control characters",
				key.c_str(), value.c_str() );
		}
		if ( !Q_stricmp( key.c_str(), "ip" ) || !Q_stricmp( key.c_str(), "tld" ) ) {
			return Fail( "userinfo key '%s' is owned by the engine; use 'address'", key.c_str() );
		}
		for ( const auto &pair : client.userinfo ) {
			if ( !Q_stricmp( pair.first.c_str(), key.c_str() ) ) {
				return Fail( "userinfo key '%s' is given twice", key.c_str() );
			}
		}
		client.userinfo.emplace_back( key, value );
	}
	if ( !error_.empty() ) {
		return false;
	}
	// RFC 5737 TEST-NET-1: documentation addresses that are never routed.
	client.address = "192.0.2." + std::to_string( script.clients.size() + 1 );
	if ( !ValidateUserinfo( client ) ) {
		return false;
	}
	script.clients.push_back( std::move( client ) );
	return true;
}

bool ScriptParser::ParseButtons( const std::string &value, int &buttons ) {
	long long numeric = 0;
	if ( ParseSigned( value.c_str(), 0, 0xFFFF, numeric ) ) {
		buttons = static_cast<int>( numeric );
		return true;
	}
	if ( value == "none" ) {
		buttons = 0;
		return true;
	}
	int result = 0;
	std::size_t start = 0;
	while ( start <= value.size() ) {
		const std::size_t plus = value.find( '+', start );
		const std::string name = value.substr( start, plus == std::string::npos ? std::string::npos : plus - start );
		bool found = false;
		for ( const ButtonName &button : ButtonNames ) {
			if ( name == button.name ) {
				result |= button.bit;
				found = true;
				break;
			}
		}
		if ( !found ) {
			return Fail( "unknown button '%s' (use an integer or attack, talk, use, gesture, walking, "
				"affirmative, negative, getflag, guardbase, patrol, followme, any joined with '+')", name.c_str() );
		}
		if ( plus == std::string::npos ) {
			break;
		}
		start = plus + 1;
	}
	buttons = result;
	return true;
}

bool ScriptParser::ParseField( const std::string &assignment, InputChange &change ) {
	const std::size_t equals = assignment.find( '=' );
	if ( equals == std::string::npos || equals == 0 || equals + 1 >= assignment.size() ) {
		return Fail( "expected <field>=<value>, got '%s'", assignment.c_str() );
	}
	const std::string field = assignment.substr( 0, equals );
	const std::string value = assignment.substr( equals + 1 );
	InputValues &v = change.values;
	unsigned bit = 0;
	long long integer = 0;

	const auto angle = [&]( int axis, unsigned flag ) {
		bit = flag;
		if ( !ParseDouble( value.c_str(), 1.0e6, v.angles[axis] ) ) {
			return Fail( "%s=%s: expected degrees (a finite number within +-1000000)", field.c_str(), value.c_str() );
		}
		return true;
	};
	const auto rate = [&]( int axis, unsigned flag ) {
		bit = flag;
		if ( !ParseDouble( value.c_str(), 360.0, v.rates[axis] ) ) {
			return Fail( "%s=%s: expected degrees per frame within +-360", field.c_str(), value.c_str() );
		}
		return true;
	};
	const auto byteValue = [&]( int &out, unsigned flag ) {
		bit = flag;
		if ( !ParseSigned( value.c_str(), 0, 255, integer ) ) {
			return Fail( "%s=%s: expected an integer from 0 to 255", field.c_str(), value.c_str() );
		}
		out = static_cast<int>( integer );
		return true;
	};
	const auto move = [&]( int axis, unsigned flag ) {
		bit = flag;
		if ( !ParseSigned( value.c_str(), -128, 127, integer ) ) {
			return Fail( "%s=%s: expected an integer from -128 to 127", field.c_str(), value.c_str() );
		}
		v.moves[axis] = static_cast<int>( integer );
		return true;
	};

	bool ok;
	if ( field == "pitch" ) {
		ok = angle( PITCH, FieldPitch );
	} else if ( field == "yaw" ) {
		ok = angle( YAW, FieldYaw );
	} else if ( field == "roll" ) {
		ok = angle( ROLL, FieldRoll );
	} else if ( field == "dpitch" ) {
		ok = rate( PITCH, FieldPitchRate );
	} else if ( field == "dyaw" ) {
		ok = rate( YAW, FieldYawRate );
	} else if ( field == "droll" ) {
		ok = rate( ROLL, FieldRollRate );
	} else if ( field == "buttons" ) {
		bit = FieldButtons;
		ok = ParseButtons( value, v.buttons );
	} else if ( field == "anykey" ) {
		bit = FieldAnyKey;
		ok = true;
		if ( value == "auto" ) {
			v.anyKey = -1;
		} else if ( value == "0" || value == "1" ) {
			v.anyKey = value[0] - '0';
		} else {
			ok = Fail( "anykey=%s: expected auto, 0 or 1", value.c_str() );
		}
	} else if ( field == "weapon" ) {
		if ( value == "auto" ) {
			bit = FieldWeapon;
			v.weapon = -1;
			ok = true;
		} else {
			ok = byteValue( v.weapon, FieldWeapon );
		}
	} else if ( field == "weaponprimary" ) {
		ok = byteValue( v.weaponPrimary, FieldWeaponPrimary );
	} else if ( field == "fov" ) {
		ok = byteValue( v.fov, FieldFov );
	} else if ( field == "forward" ) {
		ok = move( 0, FieldForward );
	} else if ( field == "right" ) {
		ok = move( 1, FieldRight );
	} else if ( field == "up" ) {
		ok = move( 2, FieldUp );
	} else {
		return Fail( "unknown input field '%s' (pitch yaw roll dpitch dyaw droll buttons anykey weapon "
			"weaponprimary fov forward right up)", field.c_str() );
	}
	if ( !ok ) {
		return false;
	}
	if ( change.mask & bit ) {
		return Fail( "field '%s' is given twice on one line", field.c_str() );
	}
	change.mask |= bit;
	return true;
}

bool ScriptParser::ParseAt( Script &script, const char *cursor ) {
	std::string frameToken;
	std::string id;
	if ( !NextToken( cursor, frameToken ) || !NextToken( cursor, id ) ) {
		return error_.empty() ? Fail( "usage: at <frame> <client> <field>=<value>..." ) : false;
	}
	InputEvent event;
	event.line = line_;
	if ( !ParseFrame( frameToken, event.frame ) || !FindClient( script, id, event.client ) ) {
		return false;
	}
	std::string assignment;
	while ( NextToken( cursor, assignment ) ) {
		if ( !ParseField( assignment, event.change ) ) {
			return false;
		}
	}
	if ( !error_.empty() ) {
		return false;
	}
	if ( event.change.mask == 0 ) {
		return Fail( "'at' needs at least one <field>=<value>" );
	}
	script.inputs.push_back( event );
	return true;
}

bool ScriptParser::ParseLine( Script &script, const char *cursor ) {
	std::string directive;
	if ( !NextToken( cursor, directive ) ) {
		return error_.empty();
	}
	if ( !sawVersion_ ) {
		std::string version;
		if ( directive != "fnql-lockstep" || !NextToken( cursor, version ) ) {
			return Fail( "the first directive must be 'fnql-lockstep %d'", ScriptFormatVersion );
		}
		long long number = 0;
		if ( !ParseSigned( version.c_str(), 0, 1000, number ) || number != ScriptFormatVersion ) {
			return Fail( "unsupported script version '%s' (this engine reads version %d)", version.c_str(),
				ScriptFormatVersion );
		}
		if ( !Rest( cursor ).empty() ) {
			return Fail( "unexpected text after the version" );
		}
		sawVersion_ = true;
		return true;
	}

	if ( directive == "client" ) {
		if ( !script.commands.empty() || !script.inputs.empty() ) {
			return Fail( "declare every client before the timeline" );
		}
		return ParseClient( script, cursor );
	}
	if ( directive == "ping" || directive == "address" ) {
		std::string id;
		std::string value;
		int index = 0;
		if ( !NextToken( cursor, id ) || !NextToken( cursor, value ) ) {
			return error_.empty() ? Fail( "usage: %s <client> <value>", directive.c_str() ) : false;
		}
		if ( !Rest( cursor ).empty() ) {
			return Fail( "unexpected text after the value" );
		}
		if ( !FindClient( script, id, index ) ) {
			return false;
		}
		if ( directive == "ping" ) {
			long long ping = 0;
			if ( !ParseSigned( value.c_str(), 0, 999, ping ) ) {
				return Fail( "ping '%s' must be an integer from 0 to 999", value.c_str() );
			}
			script.clients[index].ping = static_cast<int>( ping );
		} else {
			if ( !IsAddress( value ) ) {
				return Fail( "address '%s' must be 1-%u characters of [A-Za-z0-9.:[]_-]", value.c_str(),
					static_cast<unsigned>( AddressLimit ) );
			}
			script.clients[index].address = value;
		}
		return true;
	}
	if ( directive == "at" ) {
		return ParseAt( script, cursor );
	}
	if ( directive == "cmd" || directive == "clientcmd" || directive == "userinfo" ) {
		CommandEvent event;
		event.line = line_;
		std::string frameToken;
		if ( !NextToken( cursor, frameToken ) || !ParseFrame( frameToken, event.frame ) ) {
			return error_.empty() ? Fail( "usage: %s <frame> ...", directive.c_str() ) : false;
		}
		if ( directive != "cmd" ) {
			std::string id;
			if ( !NextToken( cursor, id ) ) {
				return error_.empty() ? Fail( "usage: %s <frame> <client> ...", directive.c_str() ) : false;
			}
			if ( !FindClient( script, id, event.client ) ) {
				return false;
			}
		}
		if ( directive == "userinfo" ) {
			event.kind = CommandKind::Userinfo;
			if ( !NextToken( cursor, event.text ) || !NextToken( cursor, event.value ) ) {
				return error_.empty() ? Fail( "usage: userinfo <frame> <client> <key> <value>" ) : false;
			}
			if ( !Rest( cursor ).empty() ) {
				return Fail( "unexpected text after the userinfo value" );
			}
			if ( !IsInfoSafe( event.text, MAX_INFO_KEY ) || !IsInfoSafe( event.value, MAX_INFO_VALUE ) ) {
				return Fail( "userinfo '%s' '%s' is empty or contains \\ \" ; or control characters",
					event.text.c_str(), event.value.c_str() );
			}
			if ( !Q_stricmp( event.text.c_str(), "ip" ) || !Q_stricmp( event.text.c_str(), "tld" ) ) {
				return Fail( "userinfo key '%s' is owned by the engine", event.text.c_str() );
			}
		} else {
			event.kind = directive == "cmd" ? CommandKind::Console : CommandKind::Client;
			event.text = Rest( cursor );
			if ( event.text.empty() ) {
				return Fail( "%s needs a command", directive.c_str() );
			}
			if ( event.text.size() >= MAX_STRING_CHARS - 1 ) {
				return Fail( "command is longer than %d characters", MAX_STRING_CHARS - 2 );
			}
			if ( event.text.find( ';' ) != std::string::npos ) {
				return Fail( "one command per line: ';' is not allowed" );
			}
		}
		script.commands.push_back( std::move( event ) );
		return true;
	}
	return Fail( "unknown directive '%s'", directive.c_str() );
}

bool ScriptParser::Parse( Script &script ) {
	int offset = 0;
	while ( offset < length_ ) {
		++line_;
		int end = offset;
		while ( end < length_ && text_[end] != '\n' ) {
			++end;
		}
		int lineEnd = end;
		if ( lineEnd > offset && text_[lineEnd - 1] == '\r' ) {
			--lineEnd;
		}
		const std::size_t lineLength = static_cast<std::size_t>( lineEnd - offset );
		if ( lineLength >= ScriptLineLimit ) {
			return Fail( "line is longer than %u characters", static_cast<unsigned>( ScriptLineLimit - 1 ) );
		}
		std::string line( text_ + offset, lineLength );
		offset = end + 1;
		for ( const char c : line ) {
			const unsigned char u = static_cast<unsigned char>( c );
			if ( ( u < 0x20 && c != '\t' ) || u == 0x7F ) {
				return Fail( "control character 0x%02x", u );
			}
		}
		const char *cursor = SkipSpace( line.c_str() );
		if ( *cursor == '\0' || *cursor == '#' ) {
			continue;
		}
		if ( !ParseLine( script, cursor ) ) {
			return false;
		}
	}
	if ( !sawVersion_ ) {
		++line_;
		return Fail( "missing 'fnql-lockstep %d' header", ScriptFormatVersion );
	}
	const auto byFrame = []( const auto &a, const auto &b ) { return a.frame < b.frame; };
	std::stable_sort( script.commands.begin(), script.commands.end(), byFrame );
	std::stable_sort( script.inputs.begin(), script.inputs.end(), byFrame );
	return true;
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

class RecordWriter {
public:
	bool Open( const char *path ) {
		handle_ = FS_SV_FOpenFileWrite( path );
		return handle_ != FS_INVALID_HANDLE;
	}
	bool IsOpen() const { return handle_ != FS_INVALID_HANDLE; }

	void U8( std::uint8_t value ) { buffer_.push_back( value ); }
	void U32( std::uint32_t value ) {
		for ( int shift = 0; shift < 32; shift += 8 ) {
			buffer_.push_back( static_cast<std::uint8_t>( ( value >> shift ) & 0xFFu ) );
		}
	}
	void I32( int value ) { U32( static_cast<std::uint32_t>( value ) ); }
	void Tag( RecordTag tag ) { U8( static_cast<std::uint8_t>( tag ) ); }
	void Bytes( const void *data, std::size_t length ) {
		const std::uint8_t *bytes = static_cast<const std::uint8_t *>( data );
		buffer_.insert( buffer_.end(), bytes, bytes + length );
	}
	void String( const char *text, std::size_t length ) {
		U32( static_cast<std::uint32_t>( length ) );
		Bytes( text, length );
	}
	void String( const std::string &text ) { String( text.data(), text.size() ); }

	void Flush() {
		if ( !IsOpen() || buffer_.empty() ) {
			buffer_.clear();
			return;
		}
		std::size_t written = 0;
		while ( written < buffer_.size() ) {
			const std::size_t chunk = std::min<std::size_t>( buffer_.size() - written, 1u << 20 );
			const int result = FS_Write( buffer_.data() + written, static_cast<int>( chunk ), handle_ );
			if ( result <= 0 ) {
				Close();
				Fatal( "writing the recording failed (disk full or path not writable)" );
			}
			written += static_cast<std::size_t>( result );
		}
		buffer_.clear();
	}

	void Close() {
		if ( IsOpen() ) {
			FS_FCloseFile( handle_ );
			handle_ = FS_INVALID_HANDLE;
		}
		buffer_.clear();
	}

private:
	fileHandle_t handle_ = FS_INVALID_HANDLE;
	std::vector<std::uint8_t> buffer_;
};

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------

struct ScriptedClient {
	int slot = -1;
	bool owned = false;		// the slot still belongs to this scripted client
	InputValues input;
	usercmd_t command;
	bool commandBuilt = false;
};

struct EntityCache {
	bool present = false;
	int svFlags = 0;
	entityState_t state;
};

struct PendingServerCommand {
	int target;
	std::string text;
};

struct ExecutedCommand {
	CommandKind kind;
	int clientNum;
	std::string text;
};

struct Session {
	bool started = false;
	bool finished = false;
	bool recording = false;		// sv_lockstepRecord was given; server commands are captured
	int frame = 0;
	int commandsRunForFrame = -1;
	std::string scriptName;
	Script script;
	std::vector<ScriptedClient> clients;
	std::array<int, MAX_CLIENTS> slotOwner{};
	std::size_t nextCommand = 0;
	std::size_t nextInput = 0;
	RecordWriter record;
	std::vector<EntityCache> entities;
	std::vector<std::string> configstrings;
	std::vector<PendingServerCommand> serverCommands;
	std::uint32_t droppedServerCommands = 0;
	std::vector<ExecutedCommand> executed;
};

Session session;

bool Running() {
	return lockstepEnabled && session.started && !session.finished;
}

client_t *OwnedClient( int scriptIndex ) {
	if ( scriptIndex < 0 || static_cast<std::size_t>( scriptIndex ) >= session.clients.size() || !svs.clients ) {
		return nullptr;
	}
	const ScriptedClient &scripted = session.clients[static_cast<std::size_t>( scriptIndex )];
	if ( !scripted.owned || !SV_IsClientIndex( scripted.slot ) ) {
		return nullptr;
	}
	client_t &client = SV_ClientForIndex( scripted.slot );
	// a freed slot taken over by a bot or a network client is not ours
	if ( client.state != CS_FREE && client.netchan.remoteAddress.type != NA_BAD ) {
		return nullptr;
	}
	return &client;
}

// Gives up a scripted client's slot once the engine has freed it or handed it
// to someone else; the timeline keeps running without it.
void ReleaseIfGone( std::size_t scriptIndex ) {
	ScriptedClient &scripted = session.clients[scriptIndex];
	if ( !scripted.owned ) {
		return;
	}
	const client_t *cl = OwnedClient( static_cast<int>( scriptIndex ) );
	if ( cl && cl->state != CS_FREE ) {
		return;
	}
	Com_Printf( "sv_lockstep: scripted client '%s' left slot %d\n",
		session.script.clients[scriptIndex].id.c_str(), scripted.slot );
	if ( scripted.slot >= 0 && scripted.slot < MAX_CLIENTS ) {
		session.slotOwner[static_cast<std::size_t>( scripted.slot )] = -1;
	}
	scripted.owned = false;
}

int ScriptIndexForClient( const client_t *client ) {
	if ( !lockstepEnabled || !session.started || !client || !svs.clients ) {
		return -1;
	}
	const int slot = SV_ClientIndex( client );
	if ( slot < 0 || slot >= MAX_CLIENTS || slot >= sv.maxclients ) {
		return -1;
	}
	const int owner = session.slotOwner[static_cast<std::size_t>( slot )];
	if ( owner < 0 || client->netchan.remoteAddress.type != NA_BAD ) {
		return -1;
	}
	return owner;
}

void LoadScript( const char *path ) {
	void *buffer = nullptr;
	const int length = FS_ReadFile( path, &buffer );
	if ( length < 0 || !buffer ) {
		Fatal( "cannot read sv_lockstepScript '%s' from the game path", path );
	}
	if ( length > ScriptByteLimit ) {
		FS_FreeFile( buffer );
		Fatal( "sv_lockstepScript '%s' is larger than %d bytes", path, ScriptByteLimit );
	}
	ScriptParser parser( path, static_cast<const char *>( buffer ), length );
	const bool parsed = parser.Parse( session.script );
	FS_FreeFile( buffer );
	if ( !parsed ) {
		Fatal( "%s", parser.Error().c_str() );
	}
}

void BuildUserinfo( const ScriptClient &client, char *userinfo, int size ) {
	userinfo[0] = '\0';
	bool hasName = false;
	for ( const auto &pair : client.userinfo ) {
		hasName = hasName || pair.first == "name";
	}
	if ( !hasName ) {
		Info_SetValueForKey_s( userinfo, size, "name", client.id.c_str() );
	}
	for ( const auto &pair : client.userinfo ) {
		Info_SetValueForKey_s( userinfo, size, pair.first.c_str(), pair.second.c_str() );
	}
}

// Mirrors the accepted-connection half of SV_DirectConnect for a client with
// no network channel: a zeroed slot, a never-routed NA_BAD address (so nothing
// is ever transmitted), the retail protocol-91 wire profile, and an ordinary
// non-bot GAME_CLIENT_CONNECT.
void ConnectScriptedClient( std::size_t scriptIndex ) {
	const ScriptClient &declared = session.script.clients[scriptIndex];
	client_t *cl = nullptr;
	for ( client_t &candidate : SV_Clients() ) {
		if ( candidate.state == CS_FREE ) {
			cl = &candidate;
			break;
		}
	}
	if ( !cl ) {
		Fatal( "no free client slot for scripted client '%s' (sv_maxclients %d)", declared.id.c_str(), sv.maxclients );
	}
	const int clientNum = SV_ClientIndex( cl );

	std::memset( cl, 0, sizeof( *cl ) );
	netadr_t address;
	std::memset( &address, 0, sizeof( address ) );
	address.type = NA_BAD;
	Netchan_Setup( NS_SERVER, &cl->netchan, &address, clientNum + 1, 0,
		Netchan_SelectWireProfile( QL_RETAIL_PROTOCOL_VERSION, qfalse ) );
	cl->netchan_end_queue = &cl->netchan_start_queue;
	cl->compat = qfalse;
	cl->longstr = qfalse;
	cl->tld[0] = '\0';
	cl->country = "Unknown Location";
	BuildUserinfo( declared, cl->userinfo, static_cast<int>( sizeof( cl->userinfo ) ) );

	ScriptedClient &scripted = session.clients[scriptIndex];
	scripted.slot = clientNum;
	scripted.owned = true;
	session.slotOwner[static_cast<std::size_t>( clientNum )] = static_cast<int>( scriptIndex );

	SV_UserinfoChanged( cl, qtrue, qfalse );

	const intptr_t denied = VM_Call( gvm, 3, GAME_CLIENT_CONNECT, clientNum, qtrue, qfalse );
	if ( denied ) {
		const char *reason = static_cast<const char *>( GVM_ArgPtr( denied ) );
		const std::string copy = reason ? reason : "(no reason)";
		session.slotOwner[static_cast<std::size_t>( clientNum )] = -1;
		scripted.owned = false;
		SV_FreeClient( cl );
		std::memset( cl, 0, sizeof( *cl ) );
		Fatal( "the game rejected scripted client '%s': %s", declared.id.c_str(), copy.c_str() );
	}

	SV_PrintClientStateChange( cl, CS_CONNECTED );
	cl->state = CS_CONNECTED;
	cl->gamestateAck = GSA_INIT;
	cl->lastSnapshotTime = svs.time - 9999;
	cl->lastPacketTime = svs.time;
	cl->lastConnectTime = svs.time;
	cl->lastDisconnectTime = svs.time;
	cl->justConnected = qfalse;
	Com_Printf( "sv_lockstep: scripted client '%s' connected in slot %d\n", declared.id.c_str(), clientNum );
}

// The state half of SV_SendClientGameState, without building or sending a
// message: the scripted client "receives" the gamestate instantly.
void DeliverGamestate( client_t &cl ) {
	SV_PrintClientStateChange( &cl, CS_PRIMED );
	cl.state = CS_PRIMED;
	cl.downloading = qfalse;
	cl.pureAuthentic = qtrue;
	cl.gentity = SV_GentityNum( SV_ClientIndex( &cl ) );
	cl.gamestateMessageNum = cl.netchan.outgoingSequence;
	cl.lastUsercmd.serverTime = sv.time - 1;
	for ( int index = 0; index < MAX_CONFIGSTRINGS; ++index ) {
		cl.csUpdated[index] = qfalse;
	}
	cl.gamestateAck = GSA_SENT_ONCE;
}

void WriteHeader() {
	RecordWriter &out = session.record;
	out.Bytes( RecordMagic.data(), RecordMagic.size() );
	out.U32( RecordFormatVersion );
	out.U32( static_cast<std::uint32_t>( sizeof( playerState_t ) ) );
	out.U32( static_cast<std::uint32_t>( sizeof( entityState_t ) ) );
	out.U32( static_cast<std::uint32_t>( sizeof( usercmd_t ) ) );
	out.U32( MAX_CONFIGSTRINGS );
	out.U32( MAX_GENTITIES );
	out.U32( static_cast<std::uint32_t>( sv.maxclients ) );
	out.U32( static_cast<std::uint32_t>( sv_fps->integer ) );
	out.U32( lockstepSeed );
	out.U32( static_cast<std::uint32_t>( lockstepFrameLimit ) );
	out.String( std::string( Cvar_VariableString( "mapname" ) ) );
	out.String( std::string( Cvar_VariableString( "vm_gameProvider" ) ) );
	out.String( session.scriptName );
	out.U32( static_cast<std::uint32_t>( session.clients.size() ) );
	for ( std::size_t i = 0; i < session.clients.size(); ++i ) {
		out.I32( session.clients[i].slot );
		out.String( session.script.clients[i].id );
	}
	out.Flush();
}

void Finish( EndReason reason ) {
	if ( session.record.IsOpen() ) {
		session.record.Tag( RecordTag::End );
		session.record.U32( static_cast<std::uint32_t>( session.frame ) );
		session.record.U32( static_cast<std::uint32_t>( reason ) );
		session.record.Flush();
		session.record.Close();
	}
	session.finished = true;
	session.serverCommands.clear();
	session.executed.clear();
	Com_Printf( "sv_lockstep: session ended after %d frame(s) (%s)\n", session.frame,
		reason == EndReason::FrameLimit ? "frame limit" : "server shutdown" );
}

void StartSession() {
	const char *recordPath = sv_lockstepRecord->string;
	session.started = true;
	session.recording = *recordPath != '\0';
	session.slotOwner.fill( -1 );
	session.scriptName = sv_lockstepScript->string;

	if ( session.scriptName.size() >= MAX_QPATH ) {
		Fatal( "sv_lockstepScript '%s' is longer than %d characters", session.scriptName.c_str(), MAX_QPATH - 1 );
	}
	if ( !session.scriptName.empty() ) {
		LoadScript( session.scriptName.c_str() );
	}
	if ( session.script.clients.size() > static_cast<std::size_t>( sv.maxclients ) ) {
		Fatal( "the script declares %u clients but sv_maxclients is %d",
			static_cast<unsigned>( session.script.clients.size() ), sv.maxclients );
	}
	bool hasClientCommands = false;
	for ( const CommandEvent &event : session.script.commands ) {
		hasClientCommands = hasClientCommands || event.kind == CommandKind::Client;
	}
	if ( hasClientCommands && sv_floodProtect && sv_floodProtect->integer ) {
		Com_Printf( S_COLOR_YELLOW "sv_lockstep: sv_floodProtect is on and measures wall-clock time; "
			"set sv_floodProtect 0 so scripted client commands are never dropped nondeterministically\n" );
	}

	session.clients.assign( session.script.clients.size(), ScriptedClient{} );
	session.entities.assign( MAX_GENTITIES, EntityCache{} );
	session.configstrings.assign( MAX_CONFIGSTRINGS, std::string() );
	// connecting issues server commands; they are captured into frame 0
	for ( std::size_t i = 0; i < session.script.clients.size(); ++i ) {
		ConnectScriptedClient( i );
	}

	if ( session.recording ) {
		if ( !session.record.Open( recordPath ) ) {
			Fatal( "cannot open sv_lockstepRecord '%s' under fs_homepath for writing", recordPath );
		}
		WriteHeader();
	}

	Com_Printf( "sv_lockstep: session started on %s: seed %u, %u scripted client(s), %s%s, frame limit %d\n",
		Cvar_VariableString( "mapname" ), lockstepSeed, static_cast<unsigned>( session.clients.size() ),
		*recordPath ? "recording to " : "not recording", recordPath, lockstepFrameLimit );
}

void ApplyInputs( int frame ) {
	const std::vector<InputEvent> &inputs = session.script.inputs;
	while ( session.nextInput < inputs.size() && inputs[session.nextInput].frame <= frame ) {
		const InputEvent &event = inputs[session.nextInput++];
		InputValues &state = session.clients[static_cast<std::size_t>( event.client )].input;
		const InputValues &v = event.change.values;
		const unsigned mask = event.change.mask;
		if ( mask & FieldPitch ) state.angles[PITCH] = v.angles[PITCH];
		if ( mask & FieldYaw ) state.angles[YAW] = v.angles[YAW];
		if ( mask & FieldRoll ) state.angles[ROLL] = v.angles[ROLL];
		if ( mask & FieldPitchRate ) state.rates[PITCH] = v.rates[PITCH];
		if ( mask & FieldYawRate ) state.rates[YAW] = v.rates[YAW];
		if ( mask & FieldRollRate ) state.rates[ROLL] = v.rates[ROLL];
		if ( mask & FieldButtons ) state.buttons = v.buttons;
		if ( mask & FieldAnyKey ) state.anyKey = v.anyKey;
		if ( mask & FieldWeapon ) state.weapon = v.weapon;
		if ( mask & FieldWeaponPrimary ) state.weaponPrimary = v.weaponPrimary;
		if ( mask & FieldFov ) state.fov = v.fov;
		if ( mask & FieldForward ) state.moves[0] = v.moves[0];
		if ( mask & FieldRight ) state.moves[1] = v.moves[1];
		if ( mask & FieldUp ) state.moves[2] = v.moves[2];
	}
}

usercmd_t BuildCommand( const ScriptedClient &scripted, const client_t &cl ) {
	usercmd_t cmd;
	std::memset( &cmd, 0, sizeof( cmd ) );
	const InputValues &input = scripted.input;
	cmd.serverTime = sv.time;
	for ( int axis = 0; axis < 3; ++axis ) {
		cmd.angles[axis] = ANGLE2SHORT( input.angles[static_cast<std::size_t>( axis )] );
	}
	int buttons = input.buttons;
	const bool anyInput = buttons != 0 || input.moves[0] != 0 || input.moves[1] != 0 || input.moves[2] != 0;
	if ( input.anyKey == 1 || ( input.anyKey < 0 && anyInput ) ) {
		buttons |= BUTTON_ANY;
	}
	cmd.buttons = buttons;
	int weapon = input.weapon;
	if ( weapon < 0 ) {
		weapon = ( cl.state == CS_ACTIVE && sv.gameClients ) ? SV_GameClientNum( scripted.slot )->weapon : 0;
	}
	cmd.weapon = static_cast<byte>( weapon & 0xFF );
	cmd.weaponPrimary = static_cast<byte>( input.weaponPrimary );
	cmd.fov = static_cast<byte>( input.fov );
	cmd.forwardmove = static_cast<signed char>( input.moves[0] );
	cmd.rightmove = static_cast<signed char>( input.moves[1] );
	cmd.upmove = static_cast<signed char>( input.moves[2] );
	return cmd;
}

void AdvanceAngles( InputValues &input ) {
	for ( std::size_t axis = 0; axis < 3; ++axis ) {
		if ( input.rates[axis] != 0.0 ) {
			input.angles[axis] = std::fmod( input.angles[axis] + input.rates[axis], 360.0 );
		}
	}
}

void RunCommands( int frame ) {
	if ( session.commandsRunForFrame == frame ) {
		return;
	}
	session.commandsRunForFrame = frame;
	const std::vector<CommandEvent> &commands = session.script.commands;
	while ( Running() && session.nextCommand < commands.size()
		&& commands[session.nextCommand].frame <= frame ) {
		const CommandEvent event = commands[session.nextCommand++];
		if ( event.kind == CommandKind::Console ) {
			Com_Printf( "sv_lockstep: frame %d: %s\n", frame, event.text.c_str() );
			session.executed.push_back( { event.kind, -1, event.text } );
			Cmd_ExecuteString( event.text.c_str() );
			continue;
		}
		client_t *cl = OwnedClient( event.client );
		const char *id = session.script.clients[static_cast<std::size_t>( event.client )].id.c_str();
		if ( !cl || cl->state < CS_PRIMED ) {
			Com_Printf( S_COLOR_YELLOW "sv_lockstep: frame %d: script line %d skipped: client '%s' is not in the game\n",
				frame, event.line, id );
			continue;
		}
		const int clientNum = SV_ClientIndex( cl );
		if ( event.kind == CommandKind::Client ) {
			session.executed.push_back( { event.kind, clientNum, event.text } );
			SV_ExecuteClientCommand( cl, event.text.c_str() );
			continue;
		}
		session.executed.push_back( { event.kind, clientNum, "\\" + event.text + "\\" + event.value } );
		if ( !Info_SetValueForKey( cl->userinfo, event.text.c_str(), event.value.c_str() ) ) {
			Fatal( "script line %d: userinfo for '%s' does not fit", event.line, id );
		}
		SV_UserinfoChanged( cl, qtrue, qtrue );
		if ( cl->state >= CS_CONNECTED ) {
			VM_Call( gvm, 1, GAME_CLIENT_USERINFO_CHANGED, clientNum );
		}
	}
}

void WriteFrame() {
	RecordWriter &out = session.record;
	out.Tag( RecordTag::Frame );
	out.U32( static_cast<std::uint32_t>( session.frame ) );
	out.I32( sv.time );
	out.I32( SV_LockstepFrameMsec() );

	for ( const ExecutedCommand &command : session.executed ) {
		out.Tag( RecordTag::Command );
		out.U8( static_cast<std::uint8_t>( command.kind ) );
		out.I32( command.clientNum );
		out.String( command.text );
	}

	for ( const ScriptedClient &scripted : session.clients ) {
		if ( scripted.commandBuilt ) {
			out.Tag( RecordTag::Input );
			out.I32( scripted.slot );
			out.Bytes( &scripted.command, sizeof( scripted.command ) );
		}
	}
	for ( std::size_t i = 0; i < session.clients.size(); ++i ) {
		const client_t *cl = OwnedClient( static_cast<int>( i ) );
		if ( cl && cl->state == CS_ACTIVE && sv.gameClients ) {
			out.Tag( RecordTag::PlayerState );
			out.I32( session.clients[i].slot );
			out.Bytes( SV_GameClientNum( session.clients[i].slot ), sizeof( playerState_t ) );
		}
	}

	for ( int number = 0; number < MAX_GENTITIES; ++number ) {
		EntityCache &cache = session.entities[static_cast<std::size_t>( number )];
		const sharedEntity_t *ent = nullptr;
		if ( sv.gentities && number < sv.num_entities ) {
			const sharedEntity_t *candidate = SV_GentityNum( number );
			if ( candidate->r.linked && !( candidate->r.svFlags & SVF_NOCLIENT ) ) {
				ent = candidate;
			}
		}
		if ( ent ) {
			if ( !cache.present || cache.svFlags != ent->r.svFlags
				|| std::memcmp( &cache.state, &ent->s, sizeof( entityState_t ) ) != 0 ) {
				cache.present = true;
				cache.svFlags = ent->r.svFlags;
				std::memcpy( &cache.state, &ent->s, sizeof( entityState_t ) );
				out.Tag( RecordTag::Entity );
				out.I32( number );
				out.I32( ent->r.svFlags );
				out.Bytes( &ent->s, sizeof( entityState_t ) );
			}
		} else if ( cache.present ) {
			cache.present = false;
			out.Tag( RecordTag::EntityRemoved );
			out.I32( number );
		}
	}

	for ( int index = 0; index < MAX_CONFIGSTRINGS; ++index ) {
		const char *current = sv.configstrings[index] ? sv.configstrings[index] : "";
		std::string &cached = session.configstrings[static_cast<std::size_t>( index )];
		if ( cached != current ) {
			cached = current;
			out.Tag( RecordTag::Configstring );
			out.I32( index );
			out.String( cached );
		}
	}

	for ( const PendingServerCommand &command : session.serverCommands ) {
		out.Tag( RecordTag::ServerCommand );
		out.I32( command.target );
		out.String( command.text );
	}
	if ( session.droppedServerCommands ) {
		out.Tag( RecordTag::ServerCommandOverflow );
		out.U32( session.droppedServerCommands );
	}

	out.Tag( RecordTag::FrameEnd );
	out.U32( static_cast<std::uint32_t>( session.frame ) );
	out.Flush();
}

void CivilFromDays( std::int64_t days, int &year, int &month, int &day ) {
	// proleptic Gregorian calendar, days since 1970-01-01
	days += 719468;
	const std::int64_t era = ( days >= 0 ? days : days - 146096 ) / 146097;
	const std::int64_t dayOfEra = days - era * 146097;
	const std::int64_t yearOfEra = ( dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096 ) / 365;
	const std::int64_t dayOfYear = dayOfEra - ( 365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100 );
	const std::int64_t mp = ( 5 * dayOfYear + 2 ) / 153;
	day = static_cast<int>( dayOfYear - ( 153 * mp + 2 ) / 5 + 1 );
	month = static_cast<int>( mp < 10 ? mp + 3 : mp - 9 );
	year = static_cast<int>( yearOfEra + era * 400 + ( month <= 2 ? 1 : 0 ) );
}

bool IsLeapYear( int year ) {
	return ( year % 4 == 0 && year % 100 != 0 ) || year % 400 == 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Engine-facing interface (server.h / qcommon.h)
// ---------------------------------------------------------------------------

void SV_LockstepInit( void ) {
	sv_lockstep = Cvar_Get( "sv_lockstep", "0", CVAR_INIT | CVAR_PROTECTED );
	Cvar_CheckRange( sv_lockstep, "0", "1", CV_INTEGER );
	Cvar_SetDescription( sv_lockstep,
		"Deterministic lockstep tooling for game-module differentials; dedicated servers only, command line only. "
		"See docs/fnql/SERVER_LOCKSTEP.md." );
	sv_lockstepSeed = Cvar_Get( "sv_lockstepSeed", "1", CVAR_INIT | CVAR_PROTECTED );
	Cvar_SetDescription( sv_lockstepSeed, "sv_lockstep: unsigned 32-bit seed for every pinned engine random source." );
	sv_lockstepScript = Cvar_Get( "sv_lockstepScript", "", CVAR_INIT | CVAR_PROTECTED );
	Cvar_SetDescription( sv_lockstepScript, "sv_lockstep: scenario script read through the game filesystem." );
	sv_lockstepRecord = Cvar_Get( "sv_lockstepRecord", "", CVAR_INIT | CVAR_PROTECTED );
	Cvar_SetDescription( sv_lockstepRecord, "sv_lockstep: per-frame recording written under fs_homepath." );
	sv_lockstepFrames = Cvar_Get( "sv_lockstepFrames", "0", CVAR_INIT | CVAR_PROTECTED );
	Cvar_SetDescription( sv_lockstepFrames, "sv_lockstep: quit after this many recorded frames; 0 runs until stopped." );
	sv_lockstepTraceRand = Cvar_Get( "sv_lockstepTraceRand", "0", CVAR_INIT | CVAR_PROTECTED );
	Cvar_CheckRange( sv_lockstepTraceRand, "0", "1", CV_INTEGER );
	Cvar_SetDescription( sv_lockstepTraceRand,
		"sv_lockstep: log every rand() a game module draws from a shared C runtime, with its call site "
		"(Windows; modules with a static runtime are not seen)." );

	if ( !sv_lockstep->integer ) {
		return;
	}
	if ( !com_dedicated || !com_dedicated->integer ) {
		Com_Printf( S_COLOR_YELLOW "sv_lockstep is ignored: it only runs on a dedicated server\n" );
		return;
	}

	unsigned long long value = 0;
	if ( !ParseUnsigned( sv_lockstepSeed->string, 0xFFFFFFFFull, value ) ) {
		Fatal( "sv_lockstepSeed '%s' must be an unsigned 32-bit decimal integer", sv_lockstepSeed->string );
	}
	lockstepSeed = static_cast<std::uint32_t>( value );
	if ( !ParseUnsigned( sv_lockstepFrames->string, static_cast<unsigned long long>( ScriptFrameLimit ), value ) ) {
		Fatal( "sv_lockstepFrames '%s' must be an integer from 0 to %d", sv_lockstepFrames->string, ScriptFrameLimit );
	}
	lockstepFrameLimit = static_cast<int>( value );
	if ( std::strlen( sv_lockstepRecord->string ) >= MAX_OSPATH ) {
		Fatal( "sv_lockstepRecord is longer than %d characters", MAX_OSPATH - 1 );
	}

	lockstepEnabled = true;
	session.slotOwner.fill( -1 );
	Com_Printf( "sv_lockstep: enabled: fixed 1000/sv_fps frames run back to back on a virtual clock, "
		"seed %u\n", lockstepSeed );
}

void SV_LockstepShutdown( const char *reason ) {
	(void)reason;
	if ( !lockstepEnabled || !session.started ) {
		return;
	}
	if ( !session.finished ) {
		Finish( EndReason::Shutdown );
	}
	// the client array is about to be freed; nothing is owned any more
	session.slotOwner.fill( -1 );
	for ( ScriptedClient &scripted : session.clients ) {
		scripted.owned = false;
	}
}

qboolean SV_LockstepActive( void ) {
	return lockstepEnabled ? qtrue : qfalse;
}

int SV_LockstepFrameMsec( void ) {
	if ( !lockstepEnabled || !sv_fps || sv_fps->integer <= 0 ) {
		return 0;
	}
	return 1000 / sv_fps->integer;
}

int SV_LockstepClockOrigin( void ) {
	return VirtualClockOrigin;
}

void SV_LockstepBeginFrame( void ) {
	if ( !lockstepEnabled || session.finished ) {
		return;
	}
	if ( !session.started ) {
		if ( sv.state != SS_GAME || !gvm ) {
			return;
		}
		StartSession();
	}
	RunCommands( session.frame );
}

void SV_LockstepClientThink( void ) {
	if ( !Running() ) {
		return;
	}
	ApplyInputs( session.frame );
	for ( std::size_t i = 0; i < session.clients.size(); ++i ) {
		ScriptedClient &scripted = session.clients[i];
		scripted.commandBuilt = false;
		ReleaseIfGone( i );
		client_t *cl = OwnedClient( static_cast<int>( i ) );
		if ( cl && cl->state != CS_ZOMBIE ) {
			scripted.command = BuildCommand( scripted, *cl );
			scripted.commandBuilt = true;
			cl->lastPacketTime = svs.time;
			if ( cl->state == CS_CONNECTED ) {
				DeliverGamestate( *cl );
			}
			if ( cl->state == CS_PRIMED ) {
				// like a real client's first usercmd: it anchors the spawn and is
				// not executed as a move
				SV_ClientEnterWorld( cl, &scripted.command );
			} else if ( cl->state == CS_ACTIVE ) {
				usercmd_t cmd = scripted.command;
				SV_ClientThink( cl, &cmd );
			}
		}
		AdvanceAngles( scripted.input );
	}
}

void SV_LockstepEndFrame( void ) {
	if ( !Running() ) {
		return;
	}
	if ( session.record.IsOpen() ) {
		WriteFrame();
	}
	// scripted clients acknowledge every reliable command at once
	for ( std::size_t i = 0; i < session.clients.size(); ++i ) {
		client_t *cl = OwnedClient( static_cast<int>( i ) );
		if ( cl && cl->state >= CS_CONNECTED ) {
			cl->reliableAcknowledge = cl->reliableSequence;
		}
	}
	session.executed.clear();
	session.serverCommands.clear();
	session.droppedServerCommands = 0;
	++session.frame;
	if ( lockstepFrameLimit > 0 && session.frame >= lockstepFrameLimit ) {
		Finish( EndReason::FrameLimit );
		// the same path as typing "quit": shut the server down and exit 0
		Cbuf_ExecuteText( EXEC_NOW, "quit\n" );
	}
}

// ---------------------------------------------------------------------------
// rand() tracing (sv_lockstepTraceRand)
//
// A module that takes rand() from a shared C runtime DLL (the retail qagame
// imports it from MSVCR100.dll) has its import slot pointed at a wrapper that
// logs each draw with the frame and the caller's offset in the module, so a
// differential can tell where two modules' random streams part.
// ---------------------------------------------------------------------------

#ifdef _WIN32
namespace {

using CrtRand = int( __cdecl * )( void );
CrtRand tracedRand = nullptr;
std::uintptr_t tracedModuleBase = 0;
unsigned tracedDraws = 0;

int __cdecl TraceRand( void ) {
	const int value = tracedRand();
	const std::uintptr_t caller = reinterpret_cast<std::uintptr_t>( _ReturnAddress() );
	Com_Printf( "sv_lockstep: rand #%u = %d at frame %d from +0x%x\n", tracedDraws++, value, session.frame,
		static_cast<unsigned>( caller - tracedModuleBase ) );
	return value;
}

bool HookModuleRand( void *addressInModule ) {
	MEMORY_BASIC_INFORMATION info{};
	if ( !addressInModule || !VirtualQuery( addressInModule, &info, sizeof( info ) ) || !info.AllocationBase ) {
		return false;
	}
	auto *const base = static_cast<std::uint8_t *>( info.AllocationBase );
	const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>( base );
	if ( dos->e_magic != IMAGE_DOS_SIGNATURE ) {
		return false;
	}
	const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>( base + dos->e_lfanew );
	const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if ( nt->Signature != IMAGE_NT_SIGNATURE || !dir.VirtualAddress ) {
		return false;
	}
	for ( auto *imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>( base + dir.VirtualAddress ); imp->Name; ++imp ) {
		if ( Q_stricmpn( reinterpret_cast<const char *>( base + imp->Name ), "msvcr", 5 ) || !imp->OriginalFirstThunk ) {
			continue;
		}
		auto *names = reinterpret_cast<IMAGE_THUNK_DATA *>( base + imp->OriginalFirstThunk );
		auto *slots = reinterpret_cast<IMAGE_THUNK_DATA *>( base + imp->FirstThunk );
		for ( ; names->u1.AddressOfData; ++names, ++slots ) {
			if ( IMAGE_SNAP_BY_ORDINAL( names->u1.Ordinal ) ) {
				continue;
			}
			const auto *byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>( base + names->u1.AddressOfData );
			if ( std::strcmp( reinterpret_cast<const char *>( byName->Name ), "rand" ) ) {
				continue;
			}
			if ( slots->u1.Function == reinterpret_cast<decltype( slots->u1.Function )>( &TraceRand ) ) {
				return true;	// this image is already traced
			}
			DWORD protect = 0;
			if ( !VirtualProtect( &slots->u1.Function, sizeof( slots->u1.Function ), PAGE_READWRITE, &protect ) ) {
				return false;
			}
			tracedRand = reinterpret_cast<CrtRand>( slots->u1.Function );
			slots->u1.Function = reinterpret_cast<decltype( slots->u1.Function )>( &TraceRand );
			VirtualProtect( &slots->u1.Function, sizeof( slots->u1.Function ), protect, &protect );
			tracedModuleBase = reinterpret_cast<std::uintptr_t>( base );
			return true;
		}
	}
	return false;
}

} // namespace
#endif

void SV_LockstepTraceGameRand( void *addressInModule ) {
	if ( !lockstepEnabled || !sv_lockstepTraceRand || !sv_lockstepTraceRand->integer ) {
		return;
	}
#ifdef _WIN32
	// every GAME_INIT checks again: a map_restart may load a fresh image of
	// the module, and the draw count carries on across it
	if ( HookModuleRand( addressInModule ) ) {
		Com_Printf( "sv_lockstep: tracing the game module's rand() (module base 0x%x)\n",
			static_cast<unsigned>( tracedModuleBase ) );
	} else {
		Com_Printf( "sv_lockstep: the game module takes no rand() from a shared C runtime; nothing to trace\n" );
	}
#else
	(void)addressInModule;
	Com_Printf( "sv_lockstep: rand() tracing is only available on Windows\n" );
#endif
}

int SV_LockstepGameSeed( void ) {
	return static_cast<int>( lockstepSeed + gameInitCount++ );
}

void SV_LockstepSeedLevel( int *checksumFeed ) {
	const std::uint32_t levelSeed = Mix( lockstepSeed, levelSeedCount++ );
	srand( levelSeed );
	if ( checksumFeed ) {
		*checksumFeed = static_cast<int>( Mix( levelSeed, 0x46454544u ) );
	}
}

int SV_LockstepMilliseconds( void ) {
	return com_frameTime;
}

int SV_LockstepRealTime( qtime_t *qtime ) {
	const std::int64_t elapsedMsec = static_cast<std::int64_t>( com_frameTime ) - VirtualClockOrigin;
	const std::int64_t now = VirtualRealTimeEpoch + ( elapsedMsec > 0 ? elapsedMsec / 1000 : 0 );
	if ( qtime ) {
		const std::int64_t days = now / 86400;
		const std::int64_t seconds = now % 86400;
		int year = 0;
		int month = 0;
		int day = 0;
		CivilFromDays( days, year, month, day );
		static constexpr std::array<int, 12> monthStart = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
		qtime->tm_sec = static_cast<int>( seconds % 60 );
		qtime->tm_min = static_cast<int>( ( seconds / 60 ) % 60 );
		qtime->tm_hour = static_cast<int>( seconds / 3600 );
		qtime->tm_mday = day;
		qtime->tm_mon = month - 1;
		qtime->tm_year = year - 1900;
		qtime->tm_wday = static_cast<int>( ( days + 4 ) % 7 );	// 1970-01-01 was a Thursday
		qtime->tm_yday = monthStart[static_cast<std::size_t>( month - 1 )] + day - 1
			+ ( month > 2 && IsLeapYear( year ) ? 1 : 0 );
		qtime->tm_isdst = 0;
	}
	return static_cast<int>( now );
}

void SV_LockstepMatchGuid( char *buffer, int bufferSize ) {
	if ( !buffer || bufferSize <= 0 ) {
		return;
	}
	std::array<std::uint8_t, 16> bytes{};
	const std::uint32_t index = matchGuidCount++;
	for ( std::size_t word = 0; word < 4; ++word ) {
		const std::uint32_t value = Mix( lockstepSeed ^ 0x47554944u, index * 4u + static_cast<std::uint32_t>( word ) );
		for ( std::size_t b = 0; b < 4; ++b ) {
			bytes[word * 4 + b] = static_cast<std::uint8_t>( ( value >> ( 24 - 8 * b ) ) & 0xFFu );
		}
	}
	bytes[6] = static_cast<std::uint8_t>( ( bytes[6] & 0x0Fu ) | 0x40u );	// version 4 layout
	bytes[8] = static_cast<std::uint8_t>( ( bytes[8] & 0x3Fu ) | 0x80u );	// RFC 4122 variant
	Com_sprintf( buffer, bufferSize,
		"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
		bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15] );
}

void SV_LockstepRecordServerCommand( int clientNum, const char *text ) {
	if ( !Running() || !session.recording || !text ) {
		return;
	}
	if ( session.serverCommands.size() >= PendingServerCommandLimit ) {
		++session.droppedServerCommands;
		return;
	}
	session.serverCommands.push_back( { clientNum, std::string( text ) } );
}

qboolean SV_LockstepIsScriptedClient( const client_t *client ) {
	return ScriptIndexForClient( client ) >= 0 ? qtrue : qfalse;
}

const char *SV_LockstepClientAddress( const client_t *client ) {
	const int index = ScriptIndexForClient( client );
	if ( index < 0 || static_cast<std::size_t>( index ) >= session.script.clients.size() ) {
		return nullptr;
	}
	return session.script.clients[static_cast<std::size_t>( index )].address.c_str();
}

qboolean SV_LockstepClientPing( const client_t *client, int *ping ) {
	const int index = ScriptIndexForClient( client );
	if ( index < 0 || static_cast<std::size_t>( index ) >= session.script.clients.size() ) {
		return qfalse;
	}
	if ( ping ) {
		*ping = session.script.clients[static_cast<std::size_t>( index )].ping;
	}
	return qtrue;
}
