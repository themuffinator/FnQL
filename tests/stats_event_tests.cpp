#include "../code/server/stats_event.hpp"

#include <cassert>
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
	std::cout << "stats event tests passed\n";
}
