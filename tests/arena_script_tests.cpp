#include <iostream>
#include <string>
#include <string_view>

#include "../code/server/arena_script.hpp"

namespace {

int failures;

void Check( bool condition, const char *expression, int line )
{
	if ( condition ) {
		return;
	}
	std::cerr << "line " << line << ": check failed: " << expression << '\n';
	++failures;
}

#define CHECK(expression) Check( ( expression ), #expression, __LINE__ )

using fnql::server::arena::IsSafeName;
using fnql::server::arena::ParseScript;

// Byte-for-byte shape of the retail descriptors (pak00.pk3 scripts/*.sp_arena).
constexpr std::string_view kRetailEasy =
	"{\n\tmap\t\t\"qzwarmup\"\n\ttimelimit\t\"0\"\n\tfraglimit\t\"20\"\n"
	"\tbot_skill \t\"1\"\n\tbots\t\t\"trainer\"\t\n}\n";
constexpr std::string_view kRetailNightmare =
	"{\n\tmap\t\t\"qzwarmup\"\n\ttimelimit\t\"15\"\n\tfraglimit\t\"15\"\n"
	"\tbot_skill \t\"5\"\n\tbots\t\t\"trainer\"\t\n}\n";

void TestRetailDescriptors()
{
	auto parsed = ParseScript( kRetailEasy );
	CHECK( parsed.script.has_value() );
	CHECK( parsed.error.empty() );
	CHECK( parsed.notices.empty() );
	if ( parsed.script ) {
		CHECK( parsed.script->map == "qzwarmup" );
		CHECK( parsed.script->bots == "trainer" );
		CHECK( parsed.script->fragLimit == 20 );
		CHECK( parsed.script->timeLimit == 0 );
		CHECK( parsed.script->botSkill.has_value() && *parsed.script->botSkill == 1 );
	}

	parsed = ParseScript( kRetailNightmare );
	CHECK( parsed.script.has_value() );
	if ( parsed.script ) {
		CHECK( parsed.script->fragLimit == 15 );
		CHECK( parsed.script->timeLimit == 15 );
		CHECK( parsed.script->botSkill.has_value() && *parsed.script->botSkill == 5 );
	}
}

void TestDefaultsAndCase()
{
	// Keys match without regard to case; unquoted values and comments work;
	// absent keys keep the retail defaults and leave the skill to g_spSkill.
	const auto parsed = ParseScript(
		"// training\n{ MAP campgrounds /* inline */\n}\n" );
	CHECK( parsed.script.has_value() );
	if ( parsed.script ) {
		CHECK( parsed.script->map == "campgrounds" );
		CHECK( parsed.script->bots.empty() );
		CHECK( parsed.script->fragLimit == 20 );
		CHECK( parsed.script->timeLimit == 0 );
		CHECK( !parsed.script->botSkill.has_value() );
	}
}

void TestRangeRepairs()
{
	const auto parsed = ParseScript(
		"{ map a fraglimit 0 timelimit -5 bot_skill 9 }" );
	CHECK( parsed.script.has_value() );
	CHECK( parsed.notices.size() == 3 );
	if ( parsed.script ) {
		CHECK( parsed.script->fragLimit == 20 );
		CHECK( parsed.script->timeLimit == 0 );
		CHECK( parsed.script->botSkill.has_value() && *parsed.script->botSkill == 4 );
	}

	const auto low = ParseScript( "{ map a bot_skill 0 }" );
	CHECK( low.script.has_value() && low.script->botSkill.has_value() &&
		*low.script->botSkill == 4 );
}

void TestRejections()
{
	CHECK( !ParseScript( "" ).script.has_value() );
	CHECK( !ParseScript( "   \n\t" ).script.has_value() );
	CHECK( !ParseScript( "map a }" ).script.has_value() );
	CHECK( !ParseScript( "{ map a" ).script.has_value() );
	CHECK( !ParseScript( "{ fraglimit 10 }" ).script.has_value() );
	CHECK( !ParseScript( "{ map a gravity 100 }" ).script.has_value() );
	CHECK( !ParseScript( "{ map }" ).script.has_value() );
	CHECK( !ParseScript( "{ map a bots }" ).script.has_value() );
	CHECK( !ParseScript( "{ map a fraglimit }" ).script.has_value() );
	// A key's value must sit on the key's own line.
	CHECK( !ParseScript( "{ map\n a }" ).script.has_value() );
	CHECK( !ParseScript( "{ map \"\" }" ).script.has_value() );

	// Retail reads the file into a 1024-byte buffer and refuses anything that
	// would not leave room for the terminator.
	std::string large = "{ map a ";
	large.append( 1024, ' ' );
	large += "}";
	CHECK( !ParseScript( large ).script.has_value() );
	std::string fits = "{ map a ";
	fits.append( 1023 - fits.size() - 1, ' ' );
	fits += "}";
	CHECK( fits.size() == 1023 );
	CHECK( ParseScript( fits ).script.has_value() );

	// An unterminated quote or comment ends the file without a closing brace.
	CHECK( !ParseScript( "{ map \"a" ).script.has_value() );
	CHECK( !ParseScript( "{ map a /* }" ).script.has_value() );
}

void TestSafeNames()
{
	CHECK( IsSafeName( "easy" ) );
	CHECK( IsSafeName( "qzwarmup" ) );
	CHECK( IsSafeName( "my-arena_2" ) );
	CHECK( !IsSafeName( "" ) );
	CHECK( !IsSafeName( "../easy" ) );
	CHECK( !IsSafeName( "a/b" ) );
	CHECK( !IsSafeName( "a\\b" ) );
	CHECK( !IsSafeName( "a.b" ) );
	CHECK( !IsSafeName( "a b" ) );
	CHECK( !IsSafeName( std::string( 49, 'a' ) ) );
	CHECK( IsSafeName( std::string( 48, 'a' ) ) );
}

} // namespace

int main()
{
	TestRetailDescriptors();
	TestDefaultsAndCase();
	TestRangeRepairs();
	TestRejections();
	TestSafeNames();
	return failures == 0 ? 0 : 1;
}
