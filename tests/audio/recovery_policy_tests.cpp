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

#include "AudioDeviceRecovery.h"

#include <cstdio>

namespace {

namespace adr = fnql_audio_device_recovery;

constexpr int kPollMs = 1000;
constexpr int kRetryMs = 3000;

bool Check( bool condition, const char *test, int line, const char *expression ) {
	if ( condition ) {
		return true;
	}
	std::fprintf( stderr, "%s:%d: check failed: %s\n", test, line, expression );
	return false;
}

#define CHECK( expression ) do { if ( !Check( ( expression ), __func__, __LINE__, #expression ) ) return false; } while ( 0 )

bool PollWaitsUntilIntervalAndDecaysRetryTimer() {
	adr::DeviceRecoveryState state;
	state.retryMs = 2500;

	adr::DevicePollDecision decision = adr::AdvanceDevicePoll( state, 250, true, kPollMs );
	CHECK( !decision.shouldPoll );
	CHECK( decision.connected );
	CHECK( state.pollMs == 250 );
	CHECK( state.retryMs == 2250 );

	decision = adr::AdvanceDevicePoll( state, -100, false, kPollMs );
	CHECK( !decision.shouldPoll );
	CHECK( !decision.connected );
	CHECK( state.pollMs == 250 );
	CHECK( state.retryMs == 2250 );

	decision = adr::AdvanceDevicePoll( state, 750, true, kPollMs );
	CHECK( decision.shouldPoll );
	CHECK( state.pollMs == 0 );
	CHECK( state.retryMs == 1500 );
	return true;
}

bool DisconnectWarnsOnceAndSchedulesAutoRecoveryRetry() {
	adr::DeviceRecoveryState state;
	adr::DevicePollResult result = adr::FinishDevicePoll( state, true, false, true, kRetryMs );
	CHECK( !result.connected );
	CHECK( result.printDisconnectedWarning );
	CHECK( result.shouldAttemptRecovery );
	CHECK( state.warningPrinted );
	CHECK( state.retryMs == kRetryMs );

	result = adr::FinishDevicePoll( state, true, false, true, kRetryMs );
	CHECK( !result.connected );
	CHECK( !result.printDisconnectedWarning );
	CHECK( !result.shouldAttemptRecovery );
	CHECK( state.warningPrinted );
	CHECK( state.retryMs == kRetryMs );
	return true;
}

bool DisabledAutoRecoveryStillWarnsButDoesNotAttempt() {
	adr::DeviceRecoveryState state;
	adr::DevicePollResult result = adr::FinishDevicePoll( state, true, false, false, kRetryMs );
	CHECK( !result.connected );
	CHECK( result.printDisconnectedWarning );
	CHECK( !result.shouldAttemptRecovery );
	CHECK( state.warningPrinted );
	CHECK( state.retryMs == 0 );
	return true;
}

bool SuccessfulRecoveryClearsWarningAndRetry() {
	adr::DeviceRecoveryState state;
	state.warningPrinted = true;
	state.retryMs = kRetryMs;

	adr::FinishRecoveryAttempt( state, false );
	CHECK( state.warningPrinted );
	CHECK( state.retryMs == kRetryMs );

	adr::FinishRecoveryAttempt( state, true );
	CHECK( !state.warningPrinted );
	CHECK( state.retryMs == 0 );
	return true;
}

bool ReconnectedPollPrintsOnceAndClearsRetry() {
	adr::DeviceRecoveryState state;
	state.warningPrinted = true;
	state.retryMs = 1200;

	adr::DevicePollResult result = adr::FinishDevicePoll( state, true, true, true, kRetryMs );
	CHECK( result.connected );
	CHECK( result.printReconnected );
	CHECK( !result.printDisconnectedWarning );
	CHECK( !result.shouldAttemptRecovery );
	CHECK( !state.warningPrinted );
	CHECK( state.retryMs == 0 );

	result = adr::FinishDevicePoll( state, true, true, true, kRetryMs );
	CHECK( result.connected );
	CHECK( !result.printReconnected );
	return true;
}

bool RefreshFailureKeepsBackendOptimisticallyConnected() {
	adr::DeviceRecoveryState state;
	state.warningPrinted = true;
	state.retryMs = 1000;

	const adr::DevicePollResult result = adr::FinishDevicePoll( state, false, false, true, kRetryMs );
	CHECK( result.connected );
	CHECK( !result.printReconnected );
	CHECK( !result.printDisconnectedWarning );
	CHECK( !result.shouldAttemptRecovery );
	CHECK( state.warningPrinted );
	CHECK( state.retryMs == 1000 );
	return true;
}

bool ManualRecoveryStartDecisionCoversForceAndConnectionState() {
	CHECK( adr::PlanRecoveryStart( false, false, false, false ) == adr::RecoveryStartDecision::Unavailable );
	CHECK( adr::PlanRecoveryStart( true, false, true, true ) == adr::RecoveryStartDecision::SkipConnected );
	CHECK( adr::PlanRecoveryStart( true, true, true, true ) == adr::RecoveryStartDecision::Attempt );
	CHECK( adr::PlanRecoveryStart( true, false, true, false ) == adr::RecoveryStartDecision::Attempt );
	CHECK( adr::PlanRecoveryStart( true, false, false, true ) == adr::RecoveryStartDecision::Attempt );
	return true;
}

struct TestCase {
	const char *name;
	bool ( *run )();
};

} // namespace

int main() {
	const TestCase tests[] = {
		{ "PollWaitsUntilIntervalAndDecaysRetryTimer", PollWaitsUntilIntervalAndDecaysRetryTimer },
		{ "DisconnectWarnsOnceAndSchedulesAutoRecoveryRetry", DisconnectWarnsOnceAndSchedulesAutoRecoveryRetry },
		{ "DisabledAutoRecoveryStillWarnsButDoesNotAttempt", DisabledAutoRecoveryStillWarnsButDoesNotAttempt },
		{ "SuccessfulRecoveryClearsWarningAndRetry", SuccessfulRecoveryClearsWarningAndRetry },
		{ "ReconnectedPollPrintsOnceAndClearsRetry", ReconnectedPollPrintsOnceAndClearsRetry },
		{ "RefreshFailureKeepsBackendOptimisticallyConnected", RefreshFailureKeepsBackendOptimisticallyConnected },
		{ "ManualRecoveryStartDecisionCoversForceAndConnectionState", ManualRecoveryStartDecisionCoversForceAndConnectionState }
	};

	for ( const TestCase &test : tests ) {
		if ( !test.run() ) {
			std::fprintf( stderr, "%s failed\n", test.name );
			return 1;
		}
		std::printf( "%s passed\n", test.name );
	}
	return 0;
}
