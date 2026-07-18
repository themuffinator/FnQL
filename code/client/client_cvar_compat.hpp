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
#ifndef FNQL_CLIENT_CVAR_COMPAT_HPP
#define FNQL_CLIENT_CVAR_COMPAT_HPP

#include <cmath>
#include <limits>

namespace fnql::client::cvars {

constexpr int kRetailMinimumTimeNudge = -20;
constexpr int kRetailMaximumTimeNudge = 0;

[[nodiscard]] constexpr int ClampRetailTimeNudge( int value ) noexcept
{
	return value < kRetailMinimumTimeNudge ? kRetailMinimumTimeNudge
		: value > kRetailMaximumTimeNudge ? kRetailMaximumTimeNudge : value;
}

struct TimeNudgeInputs {
	bool spectating = false;
	bool localServer = false;
	bool retailAutomatic = false;
	int snapshotPing = 0;
	int manual = 0;
	float fnqlAutomaticFactor = 0.0f;
	float fnqlAveragePing = 0.0f;
};

/*
Retail QL forces spectators and local connections to zero, bounds the manual
setting to [-20, 0], and uses a retained half-ping value for its boolean auto
mode. FnQL's fractional median-ping mode remains an explicit extension and is
therefore kept unchanged when cl_autoNudge is selected.
*/
[[nodiscard]] inline int SelectTimeNudge(
	const TimeNudgeInputs& inputs, int& previousRetailAutomatic ) noexcept
{
	if ( inputs.spectating || inputs.localServer ) {
		previousRetailAutomatic = 0;
		return 0;
	}

	if ( inputs.retailAutomatic ) {
		int selected = -( inputs.snapshotPing / 2 );
		if ( previousRetailAutomatic != 0 &&
			previousRetailAutomatic != selected ) {
			selected = previousRetailAutomatic;
		}
		selected = ClampRetailTimeNudge( selected );
		previousRetailAutomatic = selected;
		return selected;
	}

	if ( std::isfinite( inputs.fnqlAutomaticFactor ) &&
		inputs.fnqlAutomaticFactor > 0.0f &&
		std::isfinite( inputs.fnqlAveragePing ) &&
		inputs.fnqlAveragePing > 0.0f ) {
		previousRetailAutomatic = 0;
		const double magnitude =
			static_cast<double>( inputs.fnqlAveragePing ) *
			static_cast<double>( inputs.fnqlAutomaticFactor ) + 0.5;
		if ( magnitude >= static_cast<double>(
			( std::numeric_limits<int>::max )() ) ) {
			return ( std::numeric_limits<int>::min )() + 1;
		}
		return -static_cast<int>( magnitude );
	}

	const int selected = ClampRetailTimeNudge( inputs.manual );
	previousRetailAutomatic = selected;
	return selected;
}

struct AvidemoInputs {
	int activeFramesPerSecond = 0;
	int latchedFramesPerSecond = 0;
	int minimumServerTime = 0;
	int maximumServerTime = 0;
	int serverTime = 0;
	int frameMilliseconds = 0;
	float timeScale = 1.0f;
	bool canCapture = false;
};

struct AvidemoPlan {
	int activeFramesPerSecond = 0;
	int frameMilliseconds = 0;
	bool clearLatch = false;
	bool captureScreenshot = false;
	bool disconnect = false;
	bool timingActive = false;
};

/*
Plan the retail screenshot-sequence capture before mutating engine state. This
keeps invalid rates and time scales deterministic while retaining retail's
strict latch boundary, inclusive capture start, and exclusive stop boundary.
*/
[[nodiscard]] inline AvidemoPlan PlanAvidemoFrame(
	const AvidemoInputs& inputs ) noexcept
{
	AvidemoPlan plan;
	plan.activeFramesPerSecond = inputs.activeFramesPerSecond;
	plan.frameMilliseconds = inputs.frameMilliseconds;

	if ( inputs.latchedFramesPerSecond > 0 &&
		( inputs.minimumServerTime <= 0 ||
			inputs.serverTime > inputs.minimumServerTime ) ) {
		plan.activeFramesPerSecond = inputs.latchedFramesPerSecond;
		plan.clearLatch = true;
	}

	if ( plan.activeFramesPerSecond <= 0 || inputs.frameMilliseconds == 0 ) {
		return plan;
	}
	plan.timingActive = true;

	if ( inputs.canCapture ) {
		if ( inputs.maximumServerTime > 0 &&
			inputs.serverTime > inputs.maximumServerTime ) {
			plan.disconnect = true;
			return plan;
		}
		plan.captureScreenshot = inputs.minimumServerTime <= 0 ||
			inputs.serverTime >= inputs.minimumServerTime;
	}

	const int baseMilliseconds = 1000 / plan.activeFramesPerSecond;
	const double scaledMilliseconds = static_cast<double>( baseMilliseconds ) *
		static_cast<double>( inputs.timeScale );
	if ( !std::isfinite( scaledMilliseconds ) || scaledMilliseconds < 1.0 ) {
		plan.frameMilliseconds = 1;
	} else if ( scaledMilliseconds >= static_cast<double>(
		( std::numeric_limits<int>::max )() ) ) {
		plan.frameMilliseconds = ( std::numeric_limits<int>::max )();
	} else {
		plan.frameMilliseconds = static_cast<int>( scaledMilliseconds );
	}
	return plan;
}

} // namespace fnql::client::cvars

#endif // FNQL_CLIENT_CVAR_COMPAT_HPP
