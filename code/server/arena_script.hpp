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
#ifndef FNQL_SERVER_ARENA_SCRIPT_HPP
#define FNQL_SERVER_ARENA_SCRIPT_HPP

#include <cstddef>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/*
Parser for the retail `scripts/<name>.sp_arena` training descriptors that the
`arena` server command launches. Retail ships easy, medium, hard and nightmare:

	{
		map		"qzwarmup"
		timelimit	"0"
		fraglimit	"20"
		bot_skill 	"1"
		bots		"trainer"
	}

Observed in quakelive_steam.exe 0.1.0.739 (0x4de050-0x4de64f): the file must be
smaller than 1024 bytes, open with "{", and hold only the five keys above,
matched without regard to case. An out-of-range fraglimit becomes 20, timelimit
0 and bot_skill 4, each with a console notice; a missing value, an unknown key
or a missing map abandons the launch.
*/
namespace fnql::server::arena {

inline constexpr std::size_t kScriptByteLimit = 1024;
inline constexpr int kDefaultFragLimit = 20;
inline constexpr int kDefaultTimeLimit = 0;
inline constexpr int kFallbackBotSkill = 4;

struct Script {
	std::string map;
	std::string bots;
	int fragLimit = kDefaultFragLimit;
	int timeLimit = kDefaultTimeLimit;
	// Empty when the script leaves the skill to the g_spSkill cvar.
	std::optional<int> botSkill;
};

struct ParseResult {
	std::optional<Script> script;
	std::string error;
	std::vector<std::string> notices;
};

namespace detail {

[[nodiscard]] constexpr bool IsSpace( char c ) noexcept
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
		c == '\f' || c == '\v';
}

[[nodiscard]] constexpr char LowerAscii( char c ) noexcept
{
	return c >= 'A' && c <= 'Z' ? static_cast<char>( c - 'A' + 'a' ) : c;
}

[[nodiscard]] constexpr bool EqualsIgnoreCase(
	std::string_view left, std::string_view right ) noexcept
{
	if ( left.size() != right.size() ) {
		return false;
	}
	for ( std::size_t i = 0; i < left.size(); ++i ) {
		if ( LowerAscii( left[i] ) != LowerAscii( right[i] ) ) {
			return false;
		}
	}
	return true;
}

struct Token {
	std::string_view text;
	bool present = false;
};

/*
Reads the next token. With `sameLine` set, the value must sit on the current
line, as the retail parser demands for the value that follows a key.
*/
[[nodiscard]] inline Token NextToken(
	std::string_view text, std::size_t &cursor, bool sameLine ) noexcept
{
	for ( ;; ) {
		while ( cursor < text.size() && IsSpace( text[cursor] ) ) {
			if ( sameLine && text[cursor] == '\n' ) {
				return {};
			}
			++cursor;
		}
		if ( cursor + 1 < text.size() && text[cursor] == '/' &&
			text[cursor + 1] == '/' ) {
			while ( cursor < text.size() && text[cursor] != '\n' ) {
				++cursor;
			}
			continue;
		}
		if ( cursor + 1 < text.size() && text[cursor] == '/' &&
			text[cursor + 1] == '*' ) {
			cursor += 2;
			while ( cursor + 1 < text.size() &&
				!( text[cursor] == '*' && text[cursor + 1] == '/' ) ) {
				++cursor;
			}
			cursor = cursor + 1 < text.size() ? cursor + 2 : text.size();
			continue;
		}
		break;
	}

	if ( cursor >= text.size() ) {
		return {};
	}

	if ( text[cursor] == '"' ) {
		const std::size_t start = ++cursor;
		while ( cursor < text.size() && text[cursor] != '"' ) {
			++cursor;
		}
		const Token token{ text.substr( start, cursor - start ), true };
		if ( cursor < text.size() ) {
			++cursor;
		}
		return token;
	}

	const std::size_t start = cursor;
	while ( cursor < text.size() && !IsSpace( text[cursor] ) ) {
		++cursor;
	}
	return { text.substr( start, cursor - start ), true };
}

[[nodiscard]] inline int ToInt( std::string_view text )
{
	return std::atoi( std::string( text ).c_str() );
}

} // namespace detail

[[nodiscard]] inline ParseResult ParseScript( std::string_view text )
{
	ParseResult result;
	Script script;
	std::size_t cursor = 0;

	if ( text.size() >= kScriptByteLimit ) {
		result.error = "file too large";
		return result;
	}

	const detail::Token open = detail::NextToken( text, cursor, false );
	if ( !open.present ) {
		result.error = "file is empty";
		return result;
	}
	if ( open.text != "{" ) {
		result.error = "missing initial \"{\"";
		return result;
	}

	for ( ;; ) {
		const detail::Token key = detail::NextToken( text, cursor, false );
		if ( !key.present ) {
			result.error = "unexpected end of file";
			return result;
		}
		if ( key.text == "}" ) {
			break;
		}

		const detail::Token value = detail::NextToken( text, cursor, true );
		if ( detail::EqualsIgnoreCase( key.text, "fraglimit" ) ) {
			if ( !value.present || value.text.empty() ) {
				result.error = "no matching value given for fraglimit";
				return result;
			}
			script.fragLimit = detail::ToInt( value.text );
			if ( script.fragLimit < 1 ) {
				result.notices.emplace_back(
					"Fraglimit was an invalid value! Valid values are 1 or greater. Setting to 20." );
				script.fragLimit = kDefaultFragLimit;
			}
		} else if ( detail::EqualsIgnoreCase( key.text, "timelimit" ) ) {
			if ( !value.present || value.text.empty() ) {
				result.error = "no matching value given for timelimit";
				return result;
			}
			script.timeLimit = detail::ToInt( value.text );
			if ( script.timeLimit < 0 ) {
				result.notices.emplace_back(
					"timelimit was an invalid value! Valid values are 0 or greater. Setting to 0." );
				script.timeLimit = kDefaultTimeLimit;
			}
		} else if ( detail::EqualsIgnoreCase( key.text, "bot_skill" ) ) {
			if ( !value.present || value.text.empty() ) {
				result.error = "no matching value given for bot_skill";
				return result;
			}
			int skill = detail::ToInt( value.text );
			if ( skill < 1 || skill > 5 ) {
				result.notices.emplace_back(
					"bot_skill was an invalid value! Valid values are 1 - 5. Setting to 4." );
				skill = kFallbackBotSkill;
			}
			script.botSkill = skill;
		} else if ( detail::EqualsIgnoreCase( key.text, "map" ) ) {
			if ( !value.present || value.text.empty() ) {
				result.error = "no map name given";
				return result;
			}
			script.map.assign( value.text );
		} else if ( detail::EqualsIgnoreCase( key.text, "bots" ) ) {
			if ( !value.present || value.text.empty() ) {
				result.error = "no bot names given for bots";
				return result;
			}
			script.bots.assign( value.text );
		} else {
			result.error = "unknown value \"" + std::string( key.text ) + "\"";
			return result;
		}
	}

	if ( script.map.empty() ) {
		result.error = "no map name given";
		return result;
	}

	result.script = std::move( script );
	return result;
}

/*
An arena or map name becomes part of a filesystem path, so it is limited to
the characters retail asset names use.
*/
[[nodiscard]] constexpr bool IsSafeName( std::string_view name ) noexcept
{
	if ( name.empty() || name.size() > 48 ) {
		return false;
	}
	for ( const char c : name ) {
		const bool ok = ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) ||
			( c >= '0' && c <= '9' ) || c == '_' || c == '-';
		if ( !ok ) {
			return false;
		}
	}
	return true;
}

} // namespace fnql::server::arena

#endif // FNQL_SERVER_ARENA_SCRIPT_HPP
