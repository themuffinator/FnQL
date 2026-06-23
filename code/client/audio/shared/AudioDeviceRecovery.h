/*
===========================================================================
Copyright (C) 2026

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
===========================================================================
*/

#pragma once

#include <algorithm>

namespace fnql_audio_device_recovery {

struct DeviceRecoveryState {
	int pollMs = 0;
	int retryMs = 0;
	bool warningPrinted = false;
};

struct DevicePollDecision {
	bool shouldPoll = false;
	bool connected = true;
};

struct DevicePollResult {
	bool connected = true;
	bool printReconnected = false;
	bool printDisconnectedWarning = false;
	bool shouldAttemptRecovery = false;
};

enum class RecoveryStartDecision {
	Unavailable,
	SkipConnected,
	Attempt
};

inline DevicePollDecision AdvanceDevicePoll( DeviceRecoveryState &state, int elapsedMs, bool connectedBeforePoll, int pollIntervalMs ) {
	const int elapsed = ( std::max )( elapsedMs, 0 );
	state.pollMs += elapsed;
	state.retryMs = ( std::max )( 0, state.retryMs - elapsed );

	if ( state.pollMs < pollIntervalMs ) {
		return { false, connectedBeforePoll };
	}

	state.pollMs = 0;
	return { true, true };
}

inline DevicePollResult FinishDevicePoll( DeviceRecoveryState &state, bool refreshSucceeded, bool connectedAfterRefresh, bool autoRecover, int recoveryRetryMs ) {
	if ( !refreshSucceeded ) {
		return { true, false, false, false };
	}

	if ( connectedAfterRefresh ) {
		const bool printReconnected = state.warningPrinted;
		state.warningPrinted = false;
		state.retryMs = 0;
		return { true, printReconnected, false, false };
	}

	const bool printDisconnectedWarning = !state.warningPrinted;
	state.warningPrinted = true;

	const bool shouldAttemptRecovery = autoRecover && state.retryMs <= 0;
	if ( shouldAttemptRecovery ) {
		state.retryMs = recoveryRetryMs;
	}
	return { false, false, printDisconnectedWarning, shouldAttemptRecovery };
}

inline void FinishRecoveryAttempt( DeviceRecoveryState &state, bool recovered ) {
	if ( recovered ) {
		state.warningPrinted = false;
		state.retryMs = 0;
	}
}

inline RecoveryStartDecision PlanRecoveryStart( bool backendReady, bool force, bool connectedKnown, bool connected ) {
	if ( !backendReady ) {
		return RecoveryStartDecision::Unavailable;
	}
	if ( !force && connectedKnown && connected ) {
		return RecoveryStartDecision::SkipConnected;
	}
	return RecoveryStartDecision::Attempt;
}

} // namespace fnql_audio_device_recovery
