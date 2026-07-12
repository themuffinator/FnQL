#include "../code/server/json_document.hpp"

#include <string>

#define CHECK(value) do { if ( !( value ) ) return __LINE__; } while ( false )

int main() {
	using fnql::server::json::DocumentIsValid;
	CHECK( DocumentIsValid( "null" ) );
	CHECK( DocumentIsValid( " {\"TYPE\":\"MATCH_REPORT\",\"DATA\":{\"score\":12.5e-1,\"ok\":true}} \n" ) );
	CHECK( DocumentIsValid( "[false,null,\"escaped\\ntext\",\"\\u20ac\"]" ) );
	CHECK( DocumentIsValid( std::string_view( "\"\xe2\x82\xac\"", 5 ) ) );
	CHECK( !DocumentIsValid( "" ) );
	CHECK( !DocumentIsValid( "{} trailing" ) );
	CHECK( !DocumentIsValid( "{\"x\":}" ) );
	CHECK( !DocumentIsValid( "[1,]" ) );
	CHECK( !DocumentIsValid( "01" ) );
	CHECK( !DocumentIsValid( "1." ) );
	CHECK( !DocumentIsValid( "\"bad\\xescape\"" ) );
	CHECK( !DocumentIsValid( std::string_view( "\"\xc0\x80\"", 4 ) ) );
	CHECK( !DocumentIsValid( std::string( 33'000, '0' ) ) );

	std::string nested;
	for ( std::size_t i = 0; i < fnql::server::json::MaximumDepth + 2; ++i ) {
		nested.push_back( '[' );
	}
	nested += '0';
	for ( std::size_t i = 0; i < fnql::server::json::MaximumDepth + 2; ++i ) {
		nested.push_back( ']' );
	}
	CHECK( !DocumentIsValid( nested ) );
	return 0;
}
