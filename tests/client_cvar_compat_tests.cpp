#include <cmath>
#include <iostream>
#include <limits>

#include "../code/client/client_cvar_compat.hpp"

namespace {

int failures;

void Check( bool condition, const char* expression, int line )
{
	if ( condition ) {
		return;
	}
	std::cerr << "line " << line << ": check failed: " << expression << '\n';
	++failures;
}

#define CHECK(expression) Check( ( expression ), #expression, __LINE__ )

void TestRetailTimeNudge()
{
	using fnql::client::cvars::SelectTimeNudge;
	using fnql::client::cvars::TimeNudgeInputs;

	int previous = 0;
	TimeNudgeInputs inputs;
	inputs.manual = -9;
	CHECK( SelectTimeNudge( inputs, previous ) == -9 );
	inputs.manual = -30;
	CHECK( SelectTimeNudge( inputs, previous ) == -20 );
	inputs.manual = 12;
	CHECK( SelectTimeNudge( inputs, previous ) == 0 );

	inputs = {};
	inputs.retailAutomatic = true;
	inputs.snapshotPing = 18;
	previous = 0;
	CHECK( SelectTimeNudge( inputs, previous ) == -9 );
	inputs.snapshotPing = 30;
	CHECK( SelectTimeNudge( inputs, previous ) == -9 );
	inputs.spectating = true;
	CHECK( SelectTimeNudge( inputs, previous ) == 0 );
	CHECK( previous == 0 );

	inputs = {};
	inputs.manual = -10;
	inputs.localServer = true;
	CHECK( SelectTimeNudge( inputs, previous ) == 0 );

	inputs = {};
	inputs.fnqlAutomaticFactor = 0.5f;
	inputs.fnqlAveragePing = 100.0f;
	CHECK( SelectTimeNudge( inputs, previous ) == -50 );
	CHECK( previous == 0 );
}

void TestRetailAvidemo()
{
	using fnql::client::cvars::AvidemoInputs;
	using fnql::client::cvars::PlanAvidemoFrame;

	AvidemoInputs inputs;
	inputs.latchedFramesPerSecond = 50;
	inputs.minimumServerTime = 1000;
	inputs.serverTime = 1000;
	inputs.frameMilliseconds = 16;
	auto plan = PlanAvidemoFrame( inputs );
	CHECK( !plan.clearLatch );
	CHECK( !plan.timingActive );

	inputs.serverTime = 1001;
	inputs.canCapture = true;
	plan = PlanAvidemoFrame( inputs );
	CHECK( plan.clearLatch );
	CHECK( plan.activeFramesPerSecond == 50 );
	CHECK( plan.captureScreenshot );
	CHECK( plan.frameMilliseconds == 20 );

	inputs = {};
	inputs.activeFramesPerSecond = 25;
	inputs.minimumServerTime = 1000;
	inputs.maximumServerTime = 2000;
	inputs.serverTime = 1000;
	inputs.frameMilliseconds = 16;
	inputs.timeScale = 0.5f;
	inputs.canCapture = true;
	plan = PlanAvidemoFrame( inputs );
	CHECK( plan.captureScreenshot );
	CHECK( !plan.disconnect );
	CHECK( plan.frameMilliseconds == 20 );

	inputs.serverTime = 2001;
	plan = PlanAvidemoFrame( inputs );
	CHECK( plan.disconnect );
	CHECK( !plan.captureScreenshot );

	inputs.maximumServerTime = 0;
	inputs.activeFramesPerSecond = 2000;
	inputs.timeScale = std::numeric_limits<float>::infinity();
	plan = PlanAvidemoFrame( inputs );
	CHECK( plan.frameMilliseconds == 1 );

	inputs.frameMilliseconds = 0;
	plan = PlanAvidemoFrame( inputs );
	CHECK( !plan.timingActive );
	CHECK( !plan.captureScreenshot );
}

void TestRetailConsoleAccess()
{
	using fnql::client::cvars::ConsoleCommandMayToggle;
	using fnql::client::cvars::ConsoleHotkeyMayToggle;

	// toggleconsole works only at exactly 1.
	CHECK( ConsoleCommandMayToggle( 1 ) );
	CHECK( !ConsoleCommandMayToggle( 0 ) );
	CHECK( !ConsoleCommandMayToggle( 2 ) );
	CHECK( !ConsoleCommandMayToggle( -1 ) );
	CHECK( !ConsoleCommandMayToggle( 3 ) );

	// An open console always closes with the key.
	for ( int value = -1; value <= 3; ++value ) {
		CHECK( ConsoleHotkeyMayToggle( value, true, false, false ) );
	}

	// Closed console: 1 opens, 2 never does, 0 needs Ctrl and Alt together.
	CHECK( ConsoleHotkeyMayToggle( 1, false, false, false ) );
	CHECK( !ConsoleHotkeyMayToggle( 2, false, false, false ) );
	CHECK( !ConsoleHotkeyMayToggle( 2, false, true, true ) );
	CHECK( !ConsoleHotkeyMayToggle( 0, false, false, false ) );
	CHECK( !ConsoleHotkeyMayToggle( 0, false, true, false ) );
	CHECK( !ConsoleHotkeyMayToggle( 0, false, false, true ) );
	CHECK( ConsoleHotkeyMayToggle( 0, false, true, true ) );

	// Retail tests only for 0 and 2; any other value leaves the key live.
	CHECK( ConsoleHotkeyMayToggle( 3, false, false, false ) );
	CHECK( ConsoleHotkeyMayToggle( -1, false, false, false ) );
}

} // namespace

int main()
{
	TestRetailTimeNudge();
	TestRetailAvidemo();
	TestRetailConsoleAccess();
	return failures == 0 ? 0 : 1;
}
