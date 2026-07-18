#include "../code/server/stats_event.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace fnql::server::stats;

int main() {
	{
		const auto event = ParsePlayerEvent( "PLAYER_KILL",
			R"({"TEAMKILL":false,"SUICIDE":false,"KILLER":{"WEAPON":"ROCKET","SPEED":500.25}})" );
		assert( event.valid && !event.ignored );
		assert( event.kind == PlayerEventKind::Kill );
		assert( event.mappedStat == 0x05 );
		assert( event.speed > 500.2 && event.speed < 500.3 );
	}
	{
		const auto event = ParsePlayerEvent( "PLAYER_DEATH",
			R"({"note":"\"TEAMKILL\":true","TEAMKILL":false,"MOD":"ROCKET_SPLASH"})" );
		assert( event.valid && !event.ignored && event.mappedStat == 0x2a );
	}
	{
		const auto event = ParsePlayerEvent( "PLAYER_MEDAL",
			R"({"MEDAL":"GAUNTLET"})" );
		assert( event.valid && event.mappedStat == 0x42 );
	}
	assert( ParsePlayerEvent( "PLAYER_MEDAL",
		R"({"MEDAL":"HUMILIATION"})" ).mappedStat == -1 );
	{
		const auto event = ParsePlayerEvent( "PLAYER_STATS",
			R"({"WIN":2147483648,"LOSE":-2147483649,"SCORE":666})" );
		assert( event.valid );
		assert( event.wins == 2147483647 );
		assert( event.losses == -2147483647 - 1 );
		assert( event.score == 666 );
	}
	assert( !ParsePlayerEvent( "PLAYER_STATS", R"({"WIN":1,})" ).valid );
	assert( ParsePlayerEvent( "PLAYER_KILL",
		R"({"WARMUP":true,"KILLER":{"WEAPON":"HMG"}})" ).ignored );
	{
		const JsonObjectView numbers(
			R"({"negative":-12.5,"positiveExponent":1.25e2,"negativeExponent":5E-2,"zero":-0.0,"tiny":1e-400,"huge":1e309,"text":"1.0"})" );
		assert( numbers.Number( "negative", 99.0 ) == -12.5 );
		assert( numbers.Number( "positiveExponent", 99.0 ) == 125.0 );
		assert( numbers.Number( "negativeExponent", 99.0 ) > 0.049 &&
			numbers.Number( "negativeExponent", 99.0 ) < 0.051 );
		assert( numbers.Number( "zero", 99.0 ) == 0.0 );
		assert( std::signbit( numbers.Number( "zero", 99.0 ) ) );
		assert( numbers.Number( "tiny", 99.0 ) == 99.0 );
		assert( numbers.Number( "huge", 99.0 ) == 99.0 );
		assert( numbers.Number( "text", 99.0 ) == 99.0 );
	}
	{
		double parsed = 0.0;
		assert( !detail::ParseNumber( "01", parsed ) );
		assert( !detail::ParseNumber( "1.", parsed ) );
		assert( !detail::ParseNumber( "1e", parsed ) );
		assert( !detail::ParseNumber( "1e10001", parsed ) );
	}
	std::cout << "stats event tests passed\n";
}
