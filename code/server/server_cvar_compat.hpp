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
#ifndef FNQL_SERVER_CVAR_COMPAT_HPP
#define FNQL_SERVER_CVAR_COMPAT_HPP

#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace fnql::server::cvars {

[[nodiscard]] constexpr bool IsAsciiWhitespace( char character ) noexcept
{
	return character == ' ' || character == '\t' || character == '\n'
		|| character == '\r' || character == '\f' || character == '\v';
}

/*
Retail sv_serverType ("0", archived; the Start Match page labels the values
Offline, LAN and Internet). Observed in quakelive_steam.exe 0.1.0.739:
- SV_PacketEvent processes a packet only when the cvar is nonzero or the
  sender is the in-process loopback address (0x4e4518);
- getchallenge from a non-LAN address is answered with a refusal print while
  the cvar is exactly 1 (0x4df440).
*/
inline constexpr char kLanOnlyChallengeReply[] =
	"print\nServer is for LAN clients only.\n";

[[nodiscard]] constexpr bool ServerTypeAcceptsPacket(
	int serverType, bool loopbackSender ) noexcept
{
	return serverType != 0 || loopbackSender;
}

[[nodiscard]] constexpr bool ServerTypeAcceptsChallenge(
	int serverType, bool lanSender ) noexcept
{
	return serverType != 1 || lanSender;
}

enum class ExitLevelAction {
	Continue,
	Shutdown,
	Quit
};

[[nodiscard]] constexpr ExitLevelAction SelectExitLevelAction(
	int policy ) noexcept
{
	return policy <= 0 ? ExitLevelAction::Continue
		: policy == 1 ? ExitLevelAction::Shutdown : ExitLevelAction::Quit;
}

[[nodiscard]] constexpr bool ShouldEscalateError(
	int policy, bool serverRunning, bool recoverable ) noexcept
{
	return recoverable && ( policy >= 2 || ( policy == 1 && serverRunning ) );
}

/*
Unsigned subtraction makes the timer deterministic across the 32-bit engine
clock wrap. A fired timer remains one-shot until activity or disabling the
corresponding policy rearms it.
*/
class InactivityTimer {
public:
	[[nodiscard]] bool Poll(
		bool inactive, std::uint32_t now, int delaySeconds ) noexcept
	{
		if ( !inactive || delaySeconds <= 0 ) {
			Reset();
			return false;
		}
		if ( fired_ ) {
			return false;
		}
		if ( !armed_ ) {
			armed_ = true;
			started_ = now;
			return false;
		}

		constexpr std::uint64_t maximumDelay =
			( std::numeric_limits<std::uint32_t>::max )();
		const std::uint64_t requestedDelay =
			static_cast<std::uint64_t>( delaySeconds ) * 1000u;
		const std::uint32_t delay = static_cast<std::uint32_t>(
			requestedDelay > maximumDelay ? maximumDelay : requestedDelay );
		if ( static_cast<std::uint32_t>( now - started_ ) < delay ) {
			return false;
		}
		fired_ = true;
		return true;
	}

	void Reset() noexcept
	{
		armed_ = false;
		fired_ = false;
		started_ = 0;
	}

private:
	std::uint32_t started_ = 0;
	bool armed_ = false;
	bool fired_ = false;
};

[[nodiscard]] inline bool IsSafeEntityPathComponent(
	std::string_view value ) noexcept
{
	if ( value.empty() || value == "." || value == ".." ) {
		return false;
	}
	for ( const unsigned char character : value ) {
		if ( character < 0x20u || character == 0x7fu || character == '/'
			|| character == '\\' || character == ':' ) {
			return false;
		}
	}
	return true;
}

/*
Retail's override is rooted in the virtual filesystem. Keep that contract,
but reject absolute paths, traversal, and platform separators so a server cvar
cannot escape its configured game directory on any supported platform.
*/
[[nodiscard]] inline std::optional<std::string> BuildEntityFilePath(
	std::string_view directory, std::string_view mapName,
	std::size_t maximumBytes )
{
	if ( !IsSafeEntityPathComponent( mapName ) || directory.empty()
		|| maximumBytes == 0 ) {
		return std::nullopt;
	}

	std::string path;
	std::size_t start = 0;
	while ( start < directory.size() ) {
		const std::size_t separator = directory.find( '/', start );
		const std::size_t end = separator == std::string_view::npos
			? directory.size() : separator;
		const std::string_view component = directory.substr( start, end - start );
		if ( !IsSafeEntityPathComponent( component ) ) {
			return std::nullopt;
		}
		if ( !path.empty() ) {
			path.push_back( '/' );
		}
		path.append( component );
		if ( separator == std::string_view::npos ) {
			break;
		}
		start = separator + 1;
	}

	if ( path.empty() ) {
		return std::nullopt;
	}
	path.push_back( '/' );
	path.append( mapName );
	path.append( ".ent" );
	if ( path.size() + 1 > maximumBytes ) {
		return std::nullopt;
	}
	return path;
}

/*
Retail Steam game-server identity (quakelive_steam.exe 0.1.0.739). Observed:
- SteamGameServer_Init receives the build string "1069" (0x466fc1);
- SetProduct("Quake Live") and SetModDir("baseq3") (0x467090, 0x4670a0);
- SetGameDescription is the long name of g_gametype from the table at
  0x560e50, or "Unknown Gametype" beyond it (0x46650b);
- SetPasswordProtected follows g_needpass, the flag qagame raises when a join
  password is set, not the private-slot password (0x4662a7).
The Steam server browser shows the description in its Game column.
*/
inline constexpr char kRetailSteamServerVersion[] = "1069";
inline constexpr char kRetailSteamServerProduct[] = "Quake Live";

[[nodiscard]] constexpr std::string_view SteamGameDescription(
	int gametype ) noexcept
{
	constexpr std::string_view names[] = {
		"Free For All", "Duel", "Race", "Team Deathmatch", "Clan Arena",
		"Capture the Flag", "One Flag CTF", "Overload", "Harvester",
		"Freeze Tag", "Domination", "Attack and Defend", "Red Rover"
	};
	return gametype >= 0 && gametype < static_cast<int>( std::size( names ) )
		? names[gametype] : std::string_view( "Unknown Gametype" );
}

struct SteamTagInputs {
	int gametype = 0;
	bool cheats = false;
	bool instagib = false;
	float gravity = 800.0f;
	bool vampiric = false;
	bool infected = false;
	bool quadhog = false;
	std::string_view fnqlKeywords;
	std::string_view retailTags;
	std::string_view factoryTags;
};

[[nodiscard]] inline std::string BuildSteamGameTags(
	const SteamTagInputs& inputs, std::size_t maximumBytes )
{
	static constexpr std::string_view gametypeTags[] = {
		"ffa", "duel", "race", "tdm", "clanarena", "ctf", "oneflag",
		"overload", "harvester", "freezetag", "domination", "a&d",
		"redrover"
	};
	std::string tags;
	if ( maximumBytes == 0 ) {
		return tags;
	}

	const auto append = [&]( std::string_view value ) {
		std::size_t start = 0;
		while ( start < value.size() ) {
			const std::size_t comma = value.find( ',', start );
			const std::size_t end = comma == std::string_view::npos
				? value.size() : comma;
			std::string_view tag = value.substr( start, end - start );
			while ( !tag.empty() && IsAsciiWhitespace( tag.front() ) ) {
				tag.remove_prefix( 1 );
			}
			while ( !tag.empty() && IsAsciiWhitespace( tag.back() ) ) {
				tag.remove_suffix( 1 );
			}
			if ( !tag.empty() ) {
				const std::size_t separator = tags.empty() ? 0u : 1u;
				const std::size_t available = maximumBytes - 1u - tags.size();
				if ( available > separator ) {
					if ( separator ) tags.push_back( ',' );
					tags.append( tag.substr( 0, available - separator ) );
				}
			}
			if ( comma == std::string_view::npos ) break;
			start = comma + 1;
		}
	};

	if ( inputs.gametype >= 0 && static_cast<std::size_t>( inputs.gametype )
		< std::size( gametypeTags ) ) {
		append( gametypeTags[inputs.gametype] );
	}
	if ( inputs.cheats ) append( "cheats" );
	if ( inputs.instagib ) append( "instagib" );
	if ( inputs.gravity < 800.0f ) append( "lowgrav" );
	else if ( inputs.gravity > 800.0f ) append( "highgrav" );
	if ( inputs.vampiric ) append( "vampiric" );
	if ( inputs.gametype == 12 && inputs.infected ) append( "infected" );
	if ( inputs.gametype == 0 && inputs.quadhog ) append( "quadhog" );
	append( inputs.fnqlKeywords );
	append( inputs.retailTags );
	// Retail order: sv_tags, then the active factory's tags (0x466744).
	append( inputs.factoryTags );
	return tags;
}

} // namespace fnql::server::cvars

#endif // FNQL_SERVER_CVAR_COMPAT_HPP
