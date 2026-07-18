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

#ifndef FNQL_SERVER_STATS_EVENT_HPP
#define FNQL_SERVER_STATS_EVENT_HPP

#include "json_document.hpp"

#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace fnql::server::stats {

namespace detail {

constexpr bool IsSpace( char value ) noexcept {
	return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

inline std::size_t SkipSpace( std::string_view input, std::size_t offset ) noexcept {
	while ( offset < input.size() && IsSpace( input[offset] ) ) {
		++offset;
	}
	return offset;
}

inline std::size_t SkipString( std::string_view input, std::size_t offset ) noexcept {
	if ( offset >= input.size() || input[offset] != '"' ) {
		return std::string_view::npos;
	}
	for ( ++offset; offset < input.size(); ++offset ) {
		if ( input[offset] == '\\' ) {
			if ( ++offset >= input.size() ) {
				return std::string_view::npos;
			}
		} else if ( input[offset] == '"' ) {
			return offset + 1;
		}
	}
	return std::string_view::npos;
}

inline std::size_t SkipValue( std::string_view input, std::size_t offset,
		std::size_t depth = 0 ) noexcept {
	offset = SkipSpace( input, offset );
	if ( offset >= input.size() || depth > json::MaximumDepth ) {
		return std::string_view::npos;
	}
	if ( input[offset] == '"' ) {
		return SkipString( input, offset );
	}
	if ( input[offset] == '{' || input[offset] == '[' ) {
		const char close = input[offset] == '{' ? '}' : ']';
		++offset;
		for ( ;; ) {
			offset = SkipSpace( input, offset );
			if ( offset >= input.size() ) {
				return std::string_view::npos;
			}
			if ( input[offset] == close ) {
				return offset + 1;
			}
			if ( close == '}' ) {
				offset = SkipString( input, offset );
				if ( offset == std::string_view::npos ) {
					return offset;
				}
				offset = SkipSpace( input, offset );
				if ( offset >= input.size() || input[offset++] != ':' ) {
					return std::string_view::npos;
				}
			}
			offset = SkipValue( input, offset, depth + 1 );
			if ( offset == std::string_view::npos ) {
				return offset;
			}
			offset = SkipSpace( input, offset );
			if ( offset < input.size() && input[offset] == ',' ) {
				++offset;
				continue;
			}
			if ( offset < input.size() && input[offset] == close ) {
				return offset + 1;
			}
			return std::string_view::npos;
		}
	}
	while ( offset < input.size() && !IsSpace( input[offset] ) &&
		input[offset] != ',' && input[offset] != '}' && input[offset] != ']' ) {
		++offset;
	}
	return offset;
}

inline bool KeyEquals( std::string_view quoted, std::string_view key ) noexcept {
	return quoted.size() == key.size() + 2 && quoted.front() == '"' &&
		quoted.back() == '"' && quoted.substr( 1, key.size() ) == key;
}

// Floating-point std::from_chars is not available in the oldest libstdc++ and
// libc++ versions supported by the release builders. Parse the deliberately
// small JSON number grammar here instead so event decoding remains
// locale-independent on every platform.
inline bool ParseNumber( std::string_view value, double &target ) noexcept {
	if ( value.empty() ) {
		return false;
	}

	std::size_t offset = 0;
	const bool negative = value[offset] == '-';
	if ( negative && ++offset == value.size() ) {
		return false;
	}

	std::uint64_t significand = 0;
	int significantDigits = 0;
	int discardedDigits = 0;
	int fractionalDigits = 0;
	bool significant = false;
	bool exponentOutOfRange = false;

	const auto consumeDigit = [&]( char digit, bool fractional ) {
		if ( fractional ) {
			if ( fractionalDigits == 10000 ) {
				exponentOutOfRange = true;
			} else {
				++fractionalDigits;
			}
		}
		if ( !significant && digit == '0' ) {
			return;
		}
		significant = true;
		if ( significantDigits < 19 ) {
			significand = significand * 10 + static_cast<unsigned>( digit - '0' );
			++significantDigits;
		} else if ( discardedDigits == 10000 ) {
			exponentOutOfRange = true;
		} else {
			++discardedDigits;
		}
	};

	if ( offset >= value.size() || value[offset] < '0' || value[offset] > '9' ) {
		return false;
	}
	if ( value[offset] == '0' ) {
		consumeDigit( value[offset++], false );
		if ( offset < value.size() && value[offset] >= '0' && value[offset] <= '9' ) {
			return false;
		}
	} else {
		while ( offset < value.size() && value[offset] >= '0' && value[offset] <= '9' ) {
			consumeDigit( value[offset++], false );
		}
	}

	if ( offset < value.size() && value[offset] == '.' ) {
		const std::size_t fractionBegin = ++offset;
		while ( offset < value.size() && value[offset] >= '0' && value[offset] <= '9' ) {
			consumeDigit( value[offset++], true );
		}
		if ( offset == fractionBegin ) {
			return false;
		}
	}

	int explicitExponent = 0;
	if ( offset < value.size() && ( value[offset] == 'e' || value[offset] == 'E' ) ) {
		++offset;
		bool exponentNegative = false;
		if ( offset < value.size() && ( value[offset] == '+' || value[offset] == '-' ) ) {
			exponentNegative = value[offset++] == '-';
		}
		const std::size_t exponentBegin = offset;
		while ( offset < value.size() && value[offset] >= '0' && value[offset] <= '9' ) {
			if ( explicitExponent > 1000 ) {
				exponentOutOfRange = true;
			} else {
				explicitExponent = explicitExponent * 10 + ( value[offset] - '0' );
			}
			++offset;
		}
		if ( offset == exponentBegin ) {
			return false;
		}
		if ( exponentNegative ) {
			explicitExponent = -explicitExponent;
		}
	}
	if ( offset != value.size() ) {
		return false;
	}

	if ( !significant ) {
		target = negative ? -0.0 : 0.0;
		return true;
	}
	if ( exponentOutOfRange ) {
		return false;
	}

	const int decimalExponent = explicitExponent + discardedDigits - fractionalDigits;
	const int normalizedExponent = decimalExponent + significantDigits - 1;
	if ( normalizedExponent > 308 || normalizedExponent < -324 ) {
		return false;
	}

	double parsed = static_cast<double>( significand );
	for ( int digit = 1; digit < significantDigits; ++digit ) {
		parsed *= 0.1;
	}

	unsigned power = static_cast<unsigned>( normalizedExponent < 0
		? -normalizedExponent : normalizedExponent );
	double factor = normalizedExponent < 0 ? 0.1 : 10.0;
	while ( power != 0 ) {
		if ( power & 1U ) {
			parsed *= factor;
		}
		power >>= 1U;
		if ( power != 0 ) {
			factor *= factor;
		}
	}

	if ( !std::isfinite( parsed ) || parsed == 0.0 ) {
		return false;
	}
	target = negative ? -parsed : parsed;
	return true;
}

} // namespace detail

class JsonObjectView {
public:
	explicit JsonObjectView( std::string_view document, bool validate = true ) noexcept
		: document_( document ), valid_( !validate || json::DocumentIsValid( document ) ) {
		const std::size_t begin = detail::SkipSpace( document_, 0 );
		valid_ = valid_ && begin < document_.size() && document_[begin] == '{';
	}

	[[nodiscard]] bool Valid() const noexcept { return valid_; }

	[[nodiscard]] std::string_view Member( std::string_view key ) const noexcept {
		if ( !valid_ || key.empty() ) {
			return {};
		}
		std::size_t offset = detail::SkipSpace( document_, 0 ) + 1;
		for ( ;; ) {
			offset = detail::SkipSpace( document_, offset );
			if ( offset >= document_.size() || document_[offset] == '}' ) {
				return {};
			}
			const std::size_t keyEnd = detail::SkipString( document_, offset );
			if ( keyEnd == std::string_view::npos ) {
				return {};
			}
			const bool match = detail::KeyEquals(
				document_.substr( offset, keyEnd - offset ), key );
			offset = detail::SkipSpace( document_, keyEnd );
			if ( offset >= document_.size() || document_[offset++] != ':' ) {
				return {};
			}
			offset = detail::SkipSpace( document_, offset );
			const std::size_t valueBegin = offset;
			const std::size_t valueEnd = detail::SkipValue( document_, offset );
			if ( valueEnd == std::string_view::npos ) {
				return {};
			}
			if ( match ) {
				return document_.substr( valueBegin, valueEnd - valueBegin );
			}
			offset = detail::SkipSpace( document_, valueEnd );
			if ( offset < document_.size() && document_[offset] == ',' ) {
				++offset;
				continue;
			}
			return {};
		}
	}

	[[nodiscard]] JsonObjectView Object( std::string_view key ) const noexcept {
		return JsonObjectView( Member( key ), false );
	}

	[[nodiscard]] bool Bool( std::string_view key, bool fallback = false ) const noexcept {
		const std::string_view value = Member( key );
		return value == "true" ? true : value == "false" ? false : fallback;
	}

	[[nodiscard]] std::int32_t Int( std::string_view key,
			std::int32_t fallback = 0 ) const noexcept {
		const std::string_view value = Member( key );
		if ( value.empty() ) {
			return fallback;
		}
		std::int64_t parsed = 0;
		const auto result = std::from_chars( value.data(), value.data() + value.size(), parsed );
		if ( result.ec != std::errc{} || result.ptr != value.data() + value.size() ) {
			return fallback;
		}
		if ( parsed > ( std::numeric_limits<std::int32_t>::max )() ) {
			return ( std::numeric_limits<std::int32_t>::max )();
		}
		if ( parsed < ( std::numeric_limits<std::int32_t>::min )() ) {
			return ( std::numeric_limits<std::int32_t>::min )();
		}
		return static_cast<std::int32_t>( parsed );
	}

	[[nodiscard]] double Number( std::string_view key, double fallback = 0.0 ) const noexcept {
		const std::string_view value = Member( key );
		double parsed = fallback;
		return detail::ParseNumber( value, parsed ) ? parsed : fallback;
	}

	template<std::size_t Size>
	[[nodiscard]] bool String( std::string_view key, std::array<char, Size> &target ) const noexcept {
		static_assert( Size > 0 );
		target.fill( '\0' );
		const std::string_view value = Member( key );
		if ( value.size() < 2 || value.front() != '"' || value.back() != '"' ) {
			return false;
		}
		std::size_t out = 0;
		for ( std::size_t in = 1; in + 1 < value.size(); ++in ) {
			char current = value[in];
			if ( current == '\\' ) {
				if ( ++in + 1 >= value.size() || value[in] == 'u' ) {
					return false;
				}
				switch ( value[in] ) {
				case '"': current = '"'; break;
				case '\\': current = '\\'; break;
				case '/': current = '/'; break;
				case 'b': current = '\b'; break;
				case 'f': current = '\f'; break;
				case 'n': current = '\n'; break;
				case 'r': current = '\r'; break;
				case 't': current = '\t'; break;
				default: return false;
				}
			}
			if ( out + 1 >= Size ) {
				target.fill( '\0' );
				return false;
			}
			target[out++] = current;
		}
		return true;
	}

private:
	std::string_view document_{};
	bool valid_ = false;
};

enum class PlayerEventKind : std::uint8_t {
	Unknown,
	Stats,
	Kill,
	Death,
	Medal
};

struct ParsedPlayerEvent {
	bool valid = false;
	bool ignored = false;
	PlayerEventKind kind = PlayerEventKind::Unknown;
	std::int32_t wins = 0;
	std::int32_t losses = 0;
	std::int32_t score = 0;
	int mappedStat = -1;
	double speed = 0.0;
};

struct TokenStat {
	std::string_view token;
	int stat;
};

inline constexpr std::array KillStats{
	TokenStat{"GAUNTLET", 0x01}, TokenStat{"MACHINEGUN", 0x02},
	TokenStat{"SHOTGUN", 0x03}, TokenStat{"GRENADE", 0x04},
	TokenStat{"ROCKET", 0x05}, TokenStat{"LIGHTNING", 0x06},
	TokenStat{"RAILGUN", 0x07}, TokenStat{"PLASMA", 0x08},
	TokenStat{"BFG", 0x09}, TokenStat{"NAILGUN", 0x0a},
	TokenStat{"PROXMINE", 0x0b}, TokenStat{"CHAINGUN", 0x0c},
	TokenStat{"HMG", 0x0d}
};

inline constexpr std::array DeathStats{
	TokenStat{"SHOTGUN", 0x26}, TokenStat{"GAUNTLET", 0x27},
	TokenStat{"MACHINEGUN", 0x28}, TokenStat{"GRENADE", 0x29},
	TokenStat{"GRENADE_SPLASH", 0x29}, TokenStat{"ROCKET", 0x2a},
	TokenStat{"ROCKET_SPLASH", 0x2a}, TokenStat{"PLASMA", 0x2b},
	TokenStat{"PLASMA_SPLASH", 0x2b}, TokenStat{"RAILGUN", 0x2c},
	TokenStat{"LIGHTNING", 0x2d}, TokenStat{"BFG", 0x2e},
	TokenStat{"BFG_SPLASH", 0x2e}, TokenStat{"WATER", 0x2f},
	TokenStat{"SLIME", 0x30}, TokenStat{"LAVA", 0x31},
	TokenStat{"CRUSH", 0x32}, TokenStat{"TELEFRAG", 0x33},
	TokenStat{"LASER", 0x34}, TokenStat{"HURT", 0x55},
	TokenStat{"NAILGUN", 0x36}, TokenStat{"CHAINGUN", 0x37},
	TokenStat{"PROXMINE", 0x38}, TokenStat{"KAMIKAZE", 0x39},
	TokenStat{"JUICED", 0x3a}, TokenStat{"SUICIDE", 0x3b},
	TokenStat{"FALLING", 0x3c}, TokenStat{"GRAPPLE", 0x3d},
	TokenStat{"SWITCHTEAM", 0}, TokenStat{"THAW", 0},
	TokenStat{"HMG", 0x3e}, TokenStat{"LIGHTNING_DISCHARGE", 0x3f},
	TokenStat{"OTHER_DEATH", 0x40}
};

inline constexpr std::array MedalStats{
	TokenStat{"FIRSTFRAG", 0x41}, TokenStat{"GAUNTLET", 0x42},
	TokenStat{"EXCELLENT", 0x43}, TokenStat{"REVENGE", 0x44},
	TokenStat{"COMBOKILL", 0x45}, TokenStat{"MIDAIR", 0x46},
	TokenStat{"PERFORATED", 0x47}, TokenStat{"RAMPAGE", 0x48},
	TokenStat{"IMPRESSIVE", 0x49}, TokenStat{"CAPTURE", 0x4a},
	TokenStat{"ASSIST", 0x4b}, TokenStat{"DEFENSE", 0x4c},
	TokenStat{"HEADSHOT", 0x4d}, TokenStat{"QUADGOD", 0x4e},
	TokenStat{"PERFECT", 0x4f}, TokenStat{"ACCURACY", 0x50}
};

static_assert( KillStats.size() == 13 );
static_assert( DeathStats.size() == 33 );
static_assert( MedalStats.size() == 16 );

template<std::size_t Size>
[[nodiscard]] constexpr int MapToken( const std::array<TokenStat, Size> &map,
		std::string_view token ) noexcept {
	for ( const TokenStat &entry : map ) {
		if ( entry.token == token ) {
			return entry.stat;
		}
	}
	return -1;
}

[[nodiscard]] inline ParsedPlayerEvent ParsePlayerEvent(
		std::string_view eventName, std::string_view document ) noexcept {
	ParsedPlayerEvent result{};
	JsonObjectView root( document );
	if ( !root.Valid() ) {
		return result;
	}
	result.valid = true;
	result.ignored = root.Bool( "WARMUP" ) || root.Bool( "ABORTED" );
	if ( eventName == "PLAYER_STATS" ) {
		result.kind = PlayerEventKind::Stats;
		result.wins = root.Int( "WIN" );
		result.losses = root.Int( "LOSE" );
		result.score = root.Int( "SCORE" );
	} else if ( eventName == "PLAYER_KILL" ) {
		result.kind = PlayerEventKind::Kill;
		result.ignored = result.ignored || root.Bool( "TEAMKILL" ) || root.Bool( "SUICIDE" );
		std::array<char, 64> token{};
		const JsonObjectView killer = root.Object( "KILLER" );
		if ( killer.Valid() && killer.String( "WEAPON", token ) ) {
			result.mappedStat = MapToken( KillStats, token.data() );
		}
		result.speed = killer.Number( "SPEED" );
	} else if ( eventName == "PLAYER_DEATH" ) {
		result.kind = PlayerEventKind::Death;
		result.ignored = result.ignored || root.Bool( "TEAMKILL" );
		std::array<char, 64> token{};
		if ( root.String( "MOD", token ) ) {
			result.mappedStat = MapToken( DeathStats, token.data() );
		}
	} else if ( eventName == "PLAYER_MEDAL" ) {
		result.kind = PlayerEventKind::Medal;
		std::array<char, 64> token{};
		if ( root.String( "MEDAL", token ) ) {
			result.mappedStat = MapToken( MedalStats, token.data() );
		}
	}
	return result;
}

} // namespace fnql::server::stats

#endif
