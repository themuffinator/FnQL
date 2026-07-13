#include "../code/renderercommon/ql_font_text.h"
#include "../code/client/ql_font_bridge.hpp"

#include <cassert>
#include <cstring>

int main() {
	{
		const char text[] = "A";
		const qlFontUtf8Result_t result = QL_FontDecodeUtf8( text, text + 1 );
		assert( result.codepoint == 'A' && result.bytes == 1 );
	}
	{
		const char text[] = "\xE2\x82\xAC";
		const qlFontUtf8Result_t result = QL_FontDecodeUtf8( text, text + 3 );
		assert( result.codepoint == 0x20ac && result.bytes == 3 );
	}
	{
		const char text[] = "\xF0\x9F\x98\x80";
		const qlFontUtf8Result_t result = QL_FontDecodeUtf8( text, text + 4 );
		assert( result.codepoint == 0x1f600 && result.bytes == 4 );
	}
	{
		const char malformed[] = "\xC0\xAF";
		const qlFontUtf8Result_t result = QL_FontDecodeUtf8( malformed, malformed + 2 );
		assert( result.codepoint == 0xc0 && result.bytes == 1 );
	}
	{
		const char truncated[] = "\xE2\x82";
		const qlFontUtf8Result_t result = QL_FontDecodeUtf8( truncated, truncated + 2 );
		assert( result.codepoint == 0xe2 && result.bytes == 1 );
	}
	{
		int color = -1;
		assert( QL_FontColorEscape( "^7white", nullptr, &color ) == 2 );
		assert( color == 7 );
		assert( QL_FontColorEscape( "^8literal", nullptr, &color ) == 0 );
		assert( QL_FontColorEscape( "^^literal", nullptr, &color ) == 0 );
	}
	assert( QL_FontScaleTenths( 12.34f ) == 123 );
	assert( QL_FontScaleTenths( 0.0f ) == 480 );
	assert( QL_FontScaleTenths( 10000.0f ) == 32767 );
	assert( QL_FontFaceCharSize26Dot6( 240, 2048, 1900, -500 ) == 1311 );
	assert( QL_FontFaceCharSize26Dot6( 240, 0, 1900, -500 ) == 1536 );
	assert( QL_FontFaceMetric( 240, 1900, 1900, -500 ) > 18.99f );
	assert( QL_FontFaceMetric( 240, -500, 1900, -500 ) < -4.99f );
	{
		const float measured[5] = { -1.25f, -7.0f, 9.25f, 2.0f, 7.75f };
		float bounds[5] = {};
		fnql::font::CopyMeasureBounds( bounds, measured );
		assert( bounds[0] == -1.25f );
		assert( bounds[1] == -7.0f );
		assert( bounds[2] == 9.25f );
		assert( bounds[3] == 2.0f );
		assert( bounds[4] == 7.75f );
		fnql::font::CopyMeasureBounds( nullptr, measured );
		fnql::font::CopyMeasureBounds( bounds, nullptr );
	}
	{
		const char text[] = "ab\xE2\x82\xAC" "cd";
		assert( fnql::font::ClampUtf8Boundary( text, 3 ) == 2 );
		assert( fnql::font::PreviousUtf8Boundary( text, 5 ) == 2 );
		assert( fnql::font::NextUtf8Boundary( text, 2 ) == 5 );
		assert( fnql::font::CountUtf8Characters( text, 0, 7 ) == 5 );

		const fnql::font::Utf8FieldWindow tail =
			fnql::font::GetUtf8FieldWindow( text, 7, 3 );
		assert( tail.startByte == 2 && tail.endByte == 7 );
		assert( tail.cursorByte == 7 && tail.characters == 3 );

		const fnql::font::Utf8FieldWindow middle =
			fnql::font::GetUtf8FieldWindow( text, 1, 3 );
		assert( middle.startByte == 1 && middle.endByte == 6 );
		assert( middle.cursorByte == 1 && middle.characters == 3 );

		const fnql::font::Utf8FieldWindow head =
			fnql::font::GetUtf8FieldWindow( text, 0, 3 );
		assert( head.startByte == 0 && head.endByte == 5 );
		assert( head.cursorByte == 0 && head.characters == 3 );
	}
	return 0;
}
