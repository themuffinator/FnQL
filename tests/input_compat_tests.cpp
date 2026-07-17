#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

// Match the include order used by Windows input sources when an external
// header has already supplied the legacy min/max macros.
#define min(a, b) fnql_test_min_macro( a, b )
#define max(a, b) fnql_test_max_macro( a, b )
#include "../code/client/input_compat.hpp"
#undef max
#undef min

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

bool Near( float actual, float expected, float tolerance = 0.0001f )
{
	return std::fabs( actual - expected ) <= tolerance;
}

#define CHECK(expression) Check( ( expression ), #expression, __LINE__ )

void TestRetailMouseMotion()
{
	fnql::input::RetailMouseParameters parameters;
	parameters.sensitivity = 4.0f;
	parameters.frameMilliseconds = 10;

	auto motion = fnql::input::TranslateRetailMouseMotion( 3.0f, -2.0f, parameters );
	CHECK( Near( motion.x, 12.0f ) );
	CHECK( Near( motion.y, -8.0f ) );
	CHECK( !motion.cpiEnabled );

	parameters.countsPerInch = 400.0f;
	motion = fnql::input::TranslateRetailMouseMotion( 400.0f, 0.0f, parameters );
	CHECK( motion.cpiEnabled );
	CHECK( Near( motion.x, 10.16f, 0.001f ) );
	CHECK( Near( fnql::input::RetailMouseAxisMultiplier( parameters.countsPerInch ),
		45.454545f, 0.0001f ) );

	parameters.countsPerInch = 0.0f;
	parameters.acceleration = 2.0f;
	parameters.accelerationPower = 2.0f;
	motion = fnql::input::TranslateRetailMouseMotion( 10.0f, 0.0f, parameters );
	CHECK( Near( motion.rate, 1.0f ) );
	CHECK( Near( motion.sensitivity, 6.0f ) );
	CHECK( Near( motion.x, 60.0f ) );

	parameters.acceleration = -2.0f;
	motion = fnql::input::TranslateRetailMouseMotion( 10.0f, 0.0f, parameters );
	CHECK( Near( motion.sensitivity, 2.0f ) );
	CHECK( Near( motion.x, 20.0f ) );

	parameters.acceleration = 2.0f;
	parameters.sensitivityCap = 5.0f;
	motion = fnql::input::TranslateRetailMouseMotion( 10.0f, 0.0f, parameters );
	CHECK( Near( motion.sensitivity, 5.0f ) );
	CHECK( Near( motion.x, 50.0f ) );

	parameters.sensitivity = std::numeric_limits<float>::infinity();
	parameters.acceleration = std::numeric_limits<float>::quiet_NaN();
	motion = fnql::input::TranslateRetailMouseMotion(
		std::numeric_limits<float>::infinity(), 1.0f, parameters );
	CHECK( Near( motion.x, 0.0f ) );
	CHECK( Near( motion.y, 0.0f ) );
}

void TestRetailViewFilter()
{
	fnql::input::RetailViewAngleFilter filter;
	auto begin = filter.Begin( { 0.0f, 0.0f }, 3 );
	CHECK( Near( begin.yaw, 0.0f ) );
	auto visible = filter.End( { 3.0f, -3.0f } );
	CHECK( Near( visible.yaw, 3.0f ) );
	CHECK( Near( visible.pitch, -3.0f ) );

	begin = filter.Begin( visible, 3 );
	CHECK( Near( begin.yaw, 3.0f ) );
	visible = filter.End( { 6.0f, -6.0f } );
	CHECK( Near( visible.yaw, 4.5f ) );

	begin = filter.Begin( visible, 3 );
	CHECK( Near( begin.yaw, 6.0f ) );
	visible = filter.End( { 9.0f, -9.0f } );
	CHECK( Near( visible.yaw, 6.0f ) );

	begin = filter.Begin( visible, 3 );
	CHECK( Near( begin.yaw, 9.0f ) );
	visible = filter.End( { 12.0f, -12.0f } );
	CHECK( Near( visible.yaw, 9.0f ) );

	begin = filter.Begin( visible, 1 );
	CHECK( Near( begin.yaw, visible.yaw ) );
	visible = filter.End( { 20.0f, 4.0f } );
	CHECK( Near( visible.yaw, 20.0f ) );
	CHECK( Near( visible.pitch, 4.0f ) );
}

void TestRetailJoystickMath()
{
	CHECK( fnql::input::RoundAwayFromZero(
		std::numeric_limits<float>::max() ) == std::numeric_limits<int>::max() );
	CHECK( fnql::input::RoundAwayFromZero(
		-std::numeric_limits<float>::max() ) == std::numeric_limits<int>::min() );
	CHECK( Near( fnql::input::NormaliseJoystickAxis( 0 ), -1.0f ) );
	CHECK( Near( fnql::input::NormaliseJoystickAxis( 32768 ), 0.0f ) );
	CHECK( Near( fnql::input::NormaliseJoystickAxis( 65535 ), 32767.0f / 32768.0f ) );
	CHECK( fnql::input::RetailJoystickMoveAxis( 0.5f, 0.5f, 1.0f ) == 0 );
	CHECK( fnql::input::RetailJoystickMoveAxis( 1.0f, 0.5f, 1.0f ) == 127 );
	CHECK( fnql::input::RetailJoystickMoveAxis( -1.0f, 0.5f, 2.0f ) == -127 );

	const int look = fnql::input::RetailJoystickLookDelta(
		1.0f, 0.15f, 20.0f, 1.7f, false );
	CHECK( look > 160 && look < 165 );
	CHECK( fnql::input::RetailJoystickLookDelta(
		1.0f, 0.15f, 20.0f, 1.7f, true ) == -look );
	CHECK( fnql::input::RetailJoystickLookDelta(
		0.1f, 0.15f, 20.0f, 1.7f, false ) == 0 );
}

void TestUnicodeTranslation()
{
	auto encoded = fnql::input::EncodeUtf8( 'A' );
	CHECK( encoded.size == 1 );
	CHECK( encoded.bytes[0] == 'A' );

	encoded = fnql::input::EncodeUtf8( 0x20acu );
	CHECK( encoded.size == 3 );
	CHECK( encoded.bytes[0] == 0xe2u );
	CHECK( encoded.bytes[1] == 0x82u );
	CHECK( encoded.bytes[2] == 0xacu );

	encoded = fnql::input::EncodeUtf8( 0x1f680u );
	CHECK( encoded.size == 4 );
	CHECK( encoded.bytes[0] == 0xf0u );
	CHECK( encoded.bytes[3] == 0x80u );
	CHECK( fnql::input::EncodeUtf8( 0xd800u ).size == 0 );
	CHECK( fnql::input::EncodeUtf8( 0x110000u ).size == 0 );

	fnql::input::Utf16Decoder decoder;
	CHECK( !decoder.Consume( 0xd83du ) );
	const auto codepoint = decoder.Consume( 0xde80u );
	CHECK( codepoint && *codepoint == 0x1f680u );
	CHECK( !decoder.Consume( 0xde80u ) );
	CHECK( decoder.Consume( 'x' ) == std::optional<std::uint32_t>( 'x' ) );
}

} // namespace

int main()
{
	TestRetailMouseMotion();
	TestRetailViewFilter();
	TestRetailJoystickMath();
	TestUnicodeTranslation();
	return failures == 0 ? 0 : 1;
}
