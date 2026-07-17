#include "../code/qcommon/q_shared.h"
#include "../code/renderercommon/tr_global_fog.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

extern "C" {

void QDECL Com_Printf( const char *, ... )
{
}

void QDECL Com_Error( errorParm_t, const char *format, ... )
{
	std::va_list args;

	std::fputs( "unexpected Com_Error in global fog parser test: ", stderr );
	va_start( args, format );
	std::vfprintf( stderr, format, args );
	va_end( args );
	std::fputc( '\n', stderr );
	std::abort();
}

float Q_atof( const char *text )
{
	return std::strtof( text, nullptr );
}

} // extern "C"

namespace {

#define CHECK( condition ) do { \
	if ( !( condition ) ) { \
		std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__ \
			<< ": " #condition "\n"; \
		std::exit( 1 ); \
	} \
} while ( false )

qboolean Parse( const char *text, int length, globalFog_t *fog, std::string *message = nullptr )
{
	char error[128] = {};
	const qboolean result = R_GlobalFogParse( fog, text, length, error, sizeof( error ) );

	if ( message ) {
		*message = error;
	}
	return result;
}

qboolean Parse( const std::string &text, globalFog_t *fog, std::string *message = nullptr )
{
	return Parse( text.data(), static_cast<int>( text.size() ), fog, message );
}

void CheckRejected( const std::string &text )
{
	globalFog_t fog{};
	std::string error;

	CHECK( Parse( text, &fog, &error ) == qfalse );
	CHECK( fog.loaded == qfalse );
	CHECK( !error.empty() );
}

void TestDefaultsAndComments()
{
	const std::string text =
		"// atmospheric grade\n"
		"color 0.25 0.5 0.75 // normalized RGB\n"
		"density 0.001\n";
	globalFog_t fog{};

	CHECK( Parse( text, &fog ) == qtrue );
	CHECK( fog.loaded == qtrue );
	CHECK( fog.mode == GLOBAL_FOG_EXP2 );
	CHECK( fog.color[0] == 0.25f );
	CHECK( fog.color[1] == 0.5f );
	CHECK( fog.color[2] == 0.75f );
	CHECK( fog.density > 0.00099f && fog.density < 0.00101f );
	CHECK( fog.start == 0.0f );
	CHECK( fog.end == 0.0f );
	CHECK( fog.opacity == 1.0f );
	CHECK( fog.sky == qtrue );
}

void TestExplicitModesAndBooleans()
{
	globalFog_t fog{};

	CHECK( Parse( "color 1 0 0 mode exp density .1 start 32 opacity 0 sky no",
		&fog ) == qtrue );
	CHECK( fog.mode == GLOBAL_FOG_EXP );
	CHECK( fog.start == 32.0f );
	CHECK( fog.opacity == 0.0f );
	CHECK( fog.sky == qfalse );

	CHECK( Parse( "color 0 1 0 mode linear density .0001 start 10 end 20 sky YES",
		&fog ) == qtrue );
	CHECK( fog.mode == GLOBAL_FOG_LINEAR );
	CHECK( fog.end == 20.0f );
	CHECK( fog.sky == qtrue );
}

void TestRequiredFieldsAndRanges()
{
	CheckRejected( "" );
	CheckRejected( "density .001" );
	CheckRejected( "color .1 .2 .3" );
	CheckRejected( "color -1 0 0 density .001" );
	CheckRejected( "color 0 0 2 density .001" );
	CheckRejected( "color 0 0 0 density 0" );
	CheckRejected( "color 0 0 0 density .1001" );
	CheckRejected( "color 0 0 0 density nan" );
	CheckRejected( "color 0 0 0 density inf" );
	CheckRejected( "color 0 0 0 density .001 start -1" );
	CheckRejected( "color 0 0 0 density .001 opacity 1.1" );
	CheckRejected( "color 0 0 0 density .001 sky maybe" );
	CheckRejected( "color 0 0 0 mode linear density .001" );
	CheckRejected( "color 0 0 0 mode linear density .001 start 20 end 20" );
}

void TestStrictDirectivesAndBounds()
{
	static const char valid[] = "color 0 0 0 density .001";
	std::string longToken( GLOBAL_FOG_TOKEN_MAX_BYTES, 'x' );
	std::string oversized( GLOBAL_FOG_SIDECAR_MAX_BYTES + 1, ' ' );
	globalFog_t fog{};
	const char embeddedNul[] = "color 0 0 0\0 density .001";

	CheckRejected( "color 0 0 0 color 1 1 1 density .001" );
	CheckRejected( "color 0 0 0 density .001 density .002" );
	CheckRejected( "color 0 0 0 density .001 mystery 1" );
	CheckRejected( "color 0 0 0 density .001 " + longToken );
	CHECK( Parse( oversized, &fog ) == qfalse );
	CHECK( Parse( embeddedNul, sizeof( embeddedNul ) - 1, &fog ) == qfalse );
	CHECK( Parse( nullptr, 0, &fog ) == qfalse );
	CHECK( R_GlobalFogParse( &fog, valid, sizeof( valid ) - 1, nullptr, 0 ) == qtrue );
	CHECK( R_GlobalFogParse( &fog, "unknown 1", 9, nullptr, 0 ) == qfalse );
	CHECK( R_GlobalFogParse( nullptr, valid, sizeof( valid ) - 1, nullptr, 0 ) == qfalse );
}

} // namespace

int main()
{
	TestDefaultsAndComments();
	TestExplicitModesAndBooleans();
	TestRequiredFieldsAndRanges();
	TestStrictDirectivesAndBounds();
	return 0;
}
