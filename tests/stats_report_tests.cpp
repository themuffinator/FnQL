#include "stats_report.hpp"

#include <array>
#include <cassert>
#include <cstring>

#define CHECK(value) assert(value)

int main() {
	using fnql::server::stats::ReportAccumulator;
	ReportAccumulator reports;
	std::array<char, fnql::server::json::MaximumDocumentBytes + 1> output{};

	CHECK( reports.CachePlayerStats(
		R"({"WIN":1,"LOSE":0,"NAME":"player"})" ) );
	CHECK( reports.CachePlayerDeath(
		R"({"TIME":42,"MOD":"ROCKET","KILLER":{"NAME":"attacker","STEAM_ID":"11","TEAM":1,"POWERUPS":["QUAD"]},"VICTIM":{"NAME":"v","STEAM_ID":"22","TEAM":2,"POWERUPS":[]}})" ) );
	CHECK( reports.PlayerStatsCount() == 1 );
	CHECK( reports.PlayerDeathCount() == 1 );
	CHECK( reports.Build( R"({"MATCH_GUID":"abc"})", output.data(), output.size() ) );
	CHECK( fnql::server::json::DocumentIsValid( output.data() ) );
	CHECK( std::strstr( output.data(), "\"PLYR_STATS\":[{\"WIN\":1" ) );
	CHECK( std::strstr( output.data(), "\"PLYR_EVENTS\":[{\"TIME\":42" ) );
	CHECK( std::strstr( output.data(), "\"ID\":\"11\"" ) );
	CHECK( !std::strstr( output.data(), "STEAM_ID" ) );
	CHECK( ReportAccumulator::MatchSummaryAllowed(
		R"({"TRAINING":false,"ABORTED":false})" ) );
	CHECK( !ReportAccumulator::MatchSummaryAllowed( R"({"ABORTED":false})" ) );
	CHECK( !ReportAccumulator::MatchSummaryAllowed(
		R"({"TRAINING":true,"ABORTED":false})" ) );
	CHECK( !ReportAccumulator::MatchSummaryAllowed(
		R"({"TRAINING":false,"ABORTED":true})" ) );

	reports.Reset();
	CHECK( reports.Build( "{}", output.data(), output.size() ) );
	CHECK( std::strcmp( output.data(),
		"{\"PLYR_STATS\":[],\"PLYR_EVENTS\":[]}" ) == 0 );
	CHECK( !reports.CachePlayerStats( R"({"WIN":1} trailing)" ) );
	CHECK( !reports.CachePlayerDeath( "[]" ) );
	CHECK( !reports.Build( R"({"PLYR_STATS":[]})", output.data(), output.size() ) );

	std::array<char, 16> tiny{};
	CHECK( !reports.Build( "{}", tiny.data(), tiny.size() ) );

	CHECK( reports.CachePlayerDeath(
		R"({"MOD":7,"KILLER":{},"VICTIM":{}})" ) );
	CHECK( reports.Build( "{}", output.data(), output.size() ) );
	CHECK( std::strstr( output.data(),
		"{\"TIME\":0,\"MOD\":\"UNKNOWN\",\"KILLER\":{\"NAME\":\"\",\"ID\":\"\",\"TEAM\":-1,\"POWERUPS\":[]}" ) );

	return 0;
}
