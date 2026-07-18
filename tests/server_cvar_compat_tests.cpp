#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include "../code/server/server_cvar_compat.hpp"

namespace {

int failures;

void Check( bool condition, const char *expression, int line )
{
	if ( condition ) return;
	std::cerr << "line " << line << ": check failed: " << expression << '\n';
	++failures;
}

#define CHECK(expression) Check( ( expression ), #expression, __LINE__ )

void TestPolicies()
{
	using namespace fnql::server::cvars;
	CHECK( SelectExitLevelAction( 0 ) == ExitLevelAction::Continue );
	CHECK( SelectExitLevelAction( 1 ) == ExitLevelAction::Shutdown );
	CHECK( SelectExitLevelAction( 2 ) == ExitLevelAction::Quit );
	CHECK( SelectExitLevelAction( 99 ) == ExitLevelAction::Quit );
	CHECK( !ShouldEscalateError( 0, true, true ) );
	CHECK( ShouldEscalateError( 1, true, true ) );
	CHECK( !ShouldEscalateError( 1, false, true ) );
	CHECK( ShouldEscalateError( 2, false, true ) );
	CHECK( !ShouldEscalateError( 2, true, false ) );
}

void TestInactivityTimer()
{
	using fnql::server::cvars::InactivityTimer;
	InactivityTimer timer;
	CHECK( !timer.Poll( true, 100u, 1 ) );
	CHECK( !timer.Poll( true, 1099u, 1 ) );
	CHECK( timer.Poll( true, 1100u, 1 ) );
	CHECK( !timer.Poll( true, 2100u, 1 ) );
	CHECK( !timer.Poll( false, 2200u, 1 ) );
	CHECK( !timer.Poll( true, 2300u, 1 ) );
	CHECK( timer.Poll( true, 3300u, 1 ) );

	timer.Reset();
	constexpr std::uint32_t nearWrap =
		( std::numeric_limits<std::uint32_t>::max )() - 500u;
	CHECK( !timer.Poll( true, nearWrap, 1 ) );
	CHECK( timer.Poll( true, 499u, 1 ) );
	CHECK( !timer.Poll( true, 1500u, 0 ) );
}

void TestEntityPaths()
{
	using fnql::server::cvars::BuildEntityFilePath;
	CHECK( BuildEntityFilePath( "noHMG", "campgrounds", 64 )
		== std::optional<std::string>( "noHMG/campgrounds.ent" ) );
	CHECK( BuildEntityFilePath( "custom/entities", "qzdm6", 64 )
		== std::optional<std::string>( "custom/entities/qzdm6.ent" ) );
	CHECK( !BuildEntityFilePath( "", "qzdm6", 64 ) );
	CHECK( !BuildEntityFilePath( "../outside", "qzdm6", 64 ) );
	CHECK( !BuildEntityFilePath( "/absolute", "qzdm6", 64 ) );
	CHECK( !BuildEntityFilePath( "C:/absolute", "qzdm6", 64 ) );
	CHECK( !BuildEntityFilePath( "safe\\windows", "qzdm6", 64 ) );
	CHECK( !BuildEntityFilePath( "safe", "../qzdm6", 64 ) );
	CHECK( !BuildEntityFilePath( "safe", "qzdm6", 8 ) );
}

void TestSteamTags()
{
	using namespace fnql::server::cvars;
	SteamTagInputs inputs;
	inputs.gametype = 12;
	inputs.cheats = true;
	inputs.instagib = true;
	inputs.gravity = 700.0f;
	inputs.vampiric = true;
	inputs.infected = true;
	inputs.quadhog = true;
	inputs.fnqlKeywords = "fnql, fast";
	inputs.retailTags = " custom,ranked,";
	CHECK( BuildSteamGameTags( inputs, 512 ) ==
		"redrover,cheats,instagib,lowgrav,vampiric,infected,fnql,fast,custom,ranked" );

	inputs = {};
	inputs.gametype = 0;
	inputs.quadhog = true;
	CHECK( BuildSteamGameTags( inputs, 512 ) == "ffa,quadhog" );
	CHECK( BuildSteamGameTags( inputs, 8 ) == "ffa,qua" );
	CHECK( BuildSteamGameTags( inputs, 1 ).empty() );
}

} // namespace

int main()
{
	TestPolicies();
	TestInactivityTimer();
	TestEntityPaths();
	TestSteamTags();
	return failures == 0 ? 0 : 1;
}
