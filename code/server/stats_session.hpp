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

#ifndef FNQL_SERVER_STATS_SESSION_HPP
#define FNQL_SERVER_STATS_SESSION_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fnql::server::stats {

inline constexpr std::size_t FieldCount = 88;
inline constexpr std::size_t AchievementCount = 59;

class Session {
public:
	void Begin( std::uint64_t identity ) noexcept {
		if ( !active_ || identity_ != identity ) {
			Reset();
			active_ = true;
			identity_ = identity;
		}
	}

	void Reset() noexcept {
		identity_ = 0;
		active_ = false;
		requestIssued_ = false;
		fields_.fill( 0 );
		fieldLoaded_.fill( false );
		fieldDirty_.fill( false );
		fieldGeneration_.fill( 0 );
		pendingFieldStore_.fill( false );
		pendingFieldGeneration_.fill( 0 );
		achievements_.fill( false );
		achievementLoaded_.fill( false );
		achievementDirty_.fill( false );
		achievementGeneration_.fill( 0 );
		pendingAchievementStore_.fill( false );
		pendingAchievementGeneration_.fill( 0 );
		storePending_ = false;
	}

	[[nodiscard]] bool AddField( int index, int delta ) noexcept {
		if ( !active_ || index < 0 || static_cast<std::size_t>( index ) >= FieldCount ) {
			return false;
		}
		const std::int64_t sum = static_cast<std::int64_t>( fields_[index] ) + delta;
		fields_[index] = static_cast<std::int32_t>(
			sum > ( std::numeric_limits<std::int32_t>::max )()
				? ( std::numeric_limits<std::int32_t>::max )()
				: sum < ( std::numeric_limits<std::int32_t>::min )()
					? ( std::numeric_limits<std::int32_t>::min )() : sum );
		fieldDirty_[index] = true;
		++fieldGeneration_[index];
		return true;
	}

	[[nodiscard]] bool UnlockAchievement( int index ) noexcept {
		if ( !active_ || index < 0 ||
			static_cast<std::size_t>( index ) >= AchievementCount ) {
			return false;
		}
		if ( !achievements_[index] ) {
			achievements_[index] = true;
			achievementDirty_[index] = true;
			++achievementGeneration_[index];
		}
		return true;
	}

	[[nodiscard]] bool LoadField( int index, std::int32_t value ) noexcept {
		if ( !active_ || index < 0 || static_cast<std::size_t>( index ) >= FieldCount ) {
			return false;
		}
		if ( !fieldLoaded_[index] ) {
			fields_[index] = SaturatingAdd( value, fields_[index] );
			fieldLoaded_[index] = true;
		}
		return true;
	}

	[[nodiscard]] bool LoadAchievement( int index, bool unlocked ) noexcept {
		if ( !active_ || index < 0 ||
			static_cast<std::size_t>( index ) >= AchievementCount ) {
			return false;
		}
		if ( !achievementLoaded_[index] ) {
			achievements_[index] = achievements_[index] || unlocked;
			achievementLoaded_[index] = true;
		}
		return true;
	}

	[[nodiscard]] bool HasAchievement( int index ) const noexcept {
		return active_ && index >= 0 &&
			static_cast<std::size_t>( index ) < AchievementCount &&
			achievements_[index];
	}

	[[nodiscard]] std::int32_t Field( int index ) const noexcept {
		return active_ && index >= 0 && static_cast<std::size_t>( index ) < FieldCount
			? fields_[index] : 0;
	}

	[[nodiscard]] bool FieldDirty( int index ) const noexcept {
		return active_ && index >= 0 && static_cast<std::size_t>( index ) < FieldCount
			&& fieldDirty_[index];
	}

	[[nodiscard]] bool FieldLoaded( int index ) const noexcept {
		return active_ && index >= 0 && static_cast<std::size_t>( index ) < FieldCount
			&& fieldLoaded_[index];
	}

	void MarkFieldStored( int index ) noexcept {
		if ( index >= 0 && static_cast<std::size_t>( index ) < FieldCount ) {
			fieldDirty_[index] = false;
		}
	}

	[[nodiscard]] bool AchievementDirty( int index ) const noexcept {
		return active_ && index >= 0 &&
			static_cast<std::size_t>( index ) < AchievementCount &&
			achievementDirty_[index];
	}

	[[nodiscard]] bool AchievementLoaded( int index ) const noexcept {
		return active_ && index >= 0 &&
			static_cast<std::size_t>( index ) < AchievementCount &&
			achievementLoaded_[index];
	}

	void MarkAchievementStored( int index ) noexcept {
		if ( index >= 0 && static_cast<std::size_t>( index ) < AchievementCount ) {
			achievementDirty_[index] = false;
		}
	}

	void BeginStorePending( const std::array<bool, FieldCount> &fields,
			const std::array<bool, AchievementCount> &achievements ) noexcept {
		storePending_ = true;
		for ( std::size_t index = 0; index < FieldCount; ++index ) {
			pendingFieldStore_[index] = fields[index];
			pendingFieldGeneration_[index] = fieldGeneration_[index];
		}
		for ( std::size_t index = 0; index < AchievementCount; ++index ) {
			pendingAchievementStore_[index] = achievements[index];
			pendingAchievementGeneration_[index] = achievementGeneration_[index];
		}
	}

	void CompletePendingStore( bool success ) noexcept {
		if ( !storePending_ ) {
			return;
		}
		if ( success ) {
			for ( std::size_t index = 0; index < FieldCount; ++index ) {
				if ( pendingFieldStore_[index] &&
					pendingFieldGeneration_[index] == fieldGeneration_[index] ) {
					fieldDirty_[index] = false;
				}
			}
			for ( std::size_t index = 0; index < AchievementCount; ++index ) {
				if ( pendingAchievementStore_[index] &&
					pendingAchievementGeneration_[index] == achievementGeneration_[index] ) {
					achievementDirty_[index] = false;
				}
			}
		}
		pendingFieldStore_.fill( false );
		pendingAchievementStore_.fill( false );
		storePending_ = false;
	}

	[[nodiscard]] bool Active() const noexcept { return active_; }
	[[nodiscard]] std::uint64_t Identity() const noexcept { return identity_; }
	[[nodiscard]] bool RequestIssued() const noexcept { return requestIssued_; }
	[[nodiscard]] bool StorePending() const noexcept { return storePending_; }
	void MarkRequestIssued() noexcept { requestIssued_ = true; }
	void ClearRequestIssued() noexcept { requestIssued_ = false; }

private:
	[[nodiscard]] static constexpr std::int32_t SaturatingAdd(
		std::int32_t left, std::int32_t right ) noexcept {
		const std::int64_t sum = static_cast<std::int64_t>( left ) + right;
		return static_cast<std::int32_t>(
			sum > ( std::numeric_limits<std::int32_t>::max )()
				? ( std::numeric_limits<std::int32_t>::max )()
				: sum < ( std::numeric_limits<std::int32_t>::min )()
					? ( std::numeric_limits<std::int32_t>::min )() : sum );
	}

	std::uint64_t identity_ = 0;
	bool active_ = false;
	bool requestIssued_ = false;
	bool storePending_ = false;
	std::array<std::int32_t, FieldCount> fields_{};
	std::array<bool, FieldCount> fieldLoaded_{};
	std::array<bool, FieldCount> fieldDirty_{};
	std::array<std::uint32_t, FieldCount> fieldGeneration_{};
	std::array<bool, FieldCount> pendingFieldStore_{};
	std::array<std::uint32_t, FieldCount> pendingFieldGeneration_{};
	std::array<bool, AchievementCount> achievements_{};
	std::array<bool, AchievementCount> achievementLoaded_{};
	std::array<bool, AchievementCount> achievementDirty_{};
	std::array<std::uint32_t, AchievementCount> achievementGeneration_{};
	std::array<bool, AchievementCount> pendingAchievementStore_{};
	std::array<std::uint32_t, AchievementCount> pendingAchievementGeneration_{};
};

} // namespace fnql::server::stats

#endif
