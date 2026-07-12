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

#ifndef FNQL_SERVER_STATS_REPORT_HPP
#define FNQL_SERVER_STATS_REPORT_HPP

#include "stats_event.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace fnql::server::stats {

class FixedJsonWriter {
public:
	FixedJsonWriter( char *storage, std::size_t capacity ) noexcept
		: storage_( storage ), capacity_( capacity ) {
		if ( storage_ && capacity_ ) {
			storage_[0] = '\0';
		}
	}

	[[nodiscard]] bool Append( std::string_view value ) noexcept {
		if ( failed_ || !storage_ || value.size() >= capacity_ -
			( size_ < capacity_ ? size_ : capacity_ ) ) {
			failed_ = true;
			return false;
		}
		std::memcpy( storage_ + size_, value.data(), value.size() );
		size_ += value.size();
		storage_[size_] = '\0';
		return true;
	}

	[[nodiscard]] bool AppendInt( std::int32_t value ) noexcept {
		std::array<char, 16> text{};
		const auto converted = std::to_chars( text.data(), text.data() + text.size(), value );
		return converted.ec == std::errc{} &&
			Append( std::string_view( text.data(),
				static_cast<std::size_t>( converted.ptr - text.data() ) ) );
	}

	[[nodiscard]] bool Good() const noexcept { return !failed_; }
	[[nodiscard]] std::size_t Size() const noexcept { return size_; }
	[[nodiscard]] std::string_view View() const noexcept {
		return storage_ ? std::string_view( storage_, size_ ) : std::string_view{};
	}

private:
	char *storage_ = nullptr;
	std::size_t capacity_ = 0;
	std::size_t size_ = 0;
	bool failed_ = false;
};

class ReportAccumulator {
public:
	static constexpr std::size_t MaximumDeathEventBytes = 2'048;

	void Reset() noexcept {
		playerStats_.fill( '\0' );
		playerDeaths_.fill( '\0' );
		playerStatsBytes_ = 0;
		playerDeathBytes_ = 0;
		playerStatsCount_ = 0;
		playerDeathCount_ = 0;
	}

	[[nodiscard]] bool CachePlayerStats( std::string_view document ) noexcept {
		JsonObjectView root( document );
		return root.Valid() && AppendFragment( playerStats_, playerStatsBytes_,
			playerStatsCount_, Trim( document ) );
	}

	[[nodiscard]] bool CachePlayerDeath( std::string_view document ) noexcept {
		JsonObjectView root( document );
		if ( !root.Valid() ) {
			return false;
		}

		std::array<char, MaximumDeathEventBytes + 1> eventStorage{};
		FixedJsonWriter event( eventStorage.data(), eventStorage.size() );
		const JsonObjectView killer = root.Object( "KILLER" );
		const JsonObjectView victim = root.Object( "VICTIM" );

		(void)event.Append( "{\"TIME\":" );
		(void)event.AppendInt( root.Int( "TIME" ) );
		(void)event.Append( ",\"MOD\":" );
		(void)event.Append( JsonStringOr( root.Member( "MOD" ), "\"UNKNOWN\"" ) );
		(void)event.Append( ",\"KILLER\":{\"NAME\":" );
		(void)event.Append( JsonStringOr( killer.Member( "NAME" ), "\"\"" ) );
		(void)event.Append( ",\"ID\":" );
		(void)event.Append( JsonStringOr( killer.Member( "STEAM_ID" ), "\"\"" ) );
		(void)event.Append( ",\"TEAM\":" );
		(void)event.AppendInt( killer.Int( "TEAM", -1 ) );
		(void)event.Append( ",\"POWERUPS\":" );
		(void)event.Append( JsonArrayOrEmpty( killer.Member( "POWERUPS" ) ) );
		(void)event.Append( "},\"VICTIM\":{\"NAME\":" );
		(void)event.Append( JsonStringOr( victim.Member( "NAME" ), "\"\"" ) );
		(void)event.Append( ",\"ID\":" );
		(void)event.Append( JsonStringOr( victim.Member( "STEAM_ID" ), "\"\"" ) );
		(void)event.Append( ",\"TEAM\":" );
		(void)event.AppendInt( victim.Int( "TEAM", -1 ) );
		(void)event.Append( ",\"POWERUPS\":" );
		(void)event.Append( JsonArrayOrEmpty( victim.Member( "POWERUPS" ) ) );
		(void)event.Append( "}}" );

		return event.Good() && AppendFragment( playerDeaths_, playerDeathBytes_,
			playerDeathCount_, event.View() );
	}

	[[nodiscard]] bool Build( std::string_view report, char *output,
			std::size_t outputCapacity ) const noexcept {
		JsonObjectView root( report );
		if ( !root.Valid() || !root.Member( "PLYR_STATS" ).empty() ||
			!root.Member( "PLYR_EVENTS" ).empty() ) {
			return false;
		}

		const std::string_view trimmed = Trim( report );
		if ( trimmed.size() < 2 || trimmed.front() != '{' || trimmed.back() != '}' ) {
			return false;
		}
		const std::string_view body = trimmed.substr( 0, trimmed.size() - 1 );
		const bool hasFields = detail::SkipSpace( trimmed, 1 ) != trimmed.size() - 1;

		FixedJsonWriter writer( output, outputCapacity );
		(void)writer.Append( body );
		if ( hasFields ) {
			(void)writer.Append( "," );
		}
		(void)writer.Append( "\"PLYR_STATS\":[" );
		(void)writer.Append( std::string_view( playerStats_.data(), playerStatsBytes_ ) );
		(void)writer.Append( "],\"PLYR_EVENTS\":[" );
		(void)writer.Append( std::string_view( playerDeaths_.data(), playerDeathBytes_ ) );
		(void)writer.Append( "]}" );
		return writer.Good() && writer.Size() <= json::MaximumDocumentBytes &&
			json::DocumentIsValid( writer.View() );
	}

	[[nodiscard]] std::size_t PlayerStatsCount() const noexcept {
		return playerStatsCount_;
	}
	[[nodiscard]] std::size_t PlayerDeathCount() const noexcept {
		return playerDeathCount_;
	}

	[[nodiscard]] static bool MatchSummaryAllowed(
			std::string_view report ) noexcept {
		JsonObjectView root( report );
		if ( !root.Valid() || Trim( root.Member( "TRAINING" ) ) != "false" ) {
			return false;
		}
		return Trim( root.Member( "ABORTED" ) ) != "true";
	}

private:
	template<std::size_t Capacity>
	[[nodiscard]] static bool AppendFragment( std::array<char, Capacity> &storage,
			std::size_t &bytes, std::size_t &count, std::string_view fragment ) noexcept {
		const std::size_t separator = count ? 1u : 0u;
		if ( fragment.empty() || fragment.size() + separator >= Capacity - bytes ) {
			return false;
		}
		if ( separator ) {
			storage[bytes++] = ',';
		}
		std::memcpy( storage.data() + bytes, fragment.data(), fragment.size() );
		bytes += fragment.size();
		storage[bytes] = '\0';
		++count;
		return true;
	}

	[[nodiscard]] static std::string_view Trim( std::string_view value ) noexcept {
		std::size_t begin = 0;
		while ( begin < value.size() && detail::IsSpace( value[begin] ) ) {
			++begin;
		}
		std::size_t end = value.size();
		while ( end > begin && detail::IsSpace( value[end - 1] ) ) {
			--end;
		}
		return value.substr( begin, end - begin );
	}

	[[nodiscard]] static std::string_view JsonStringOr( std::string_view value,
			std::string_view fallback ) noexcept {
		value = Trim( value );
		return value.size() >= 2 && value.front() == '"' && value.back() == '"'
			? value : fallback;
	}

	[[nodiscard]] static std::string_view JsonArrayOrEmpty(
			std::string_view value ) noexcept {
		value = Trim( value );
		return value.size() >= 2 && value.front() == '[' && value.back() == ']'
			? value : std::string_view( "[]" );
	}

	std::array<char, json::MaximumDocumentBytes + 1> playerStats_{};
	std::array<char, json::MaximumDocumentBytes + 1> playerDeaths_{};
	std::size_t playerStatsBytes_ = 0;
	std::size_t playerDeathBytes_ = 0;
	std::size_t playerStatsCount_ = 0;
	std::size_t playerDeathCount_ = 0;
};

} // namespace fnql::server::stats

#endif
