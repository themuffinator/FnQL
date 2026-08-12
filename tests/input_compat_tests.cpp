#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

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

void TestRetailGameplayCatcher()
{
	constexpr int console = 0x0001;
	constexpr int ui = 0x0002;
	constexpr int retailMousePass = 0x0010;

	CHECK( !fnql::input::CatcherBlocksGameplayInput( 0, retailMousePass ) );
	CHECK( !fnql::input::CatcherBlocksGameplayInput(
		retailMousePass, retailMousePass ) );
	CHECK( fnql::input::CatcherBlocksGameplayInput( console, retailMousePass ) );
	CHECK( fnql::input::CatcherBlocksGameplayInput(
		console | retailMousePass, retailMousePass ) );

	CHECK( !fnql::input::GameplayCatcherStateChanged(
		0, retailMousePass, retailMousePass ) );
	CHECK( !fnql::input::GameplayCatcherStateChanged(
		retailMousePass, 0, retailMousePass ) );
	CHECK( !fnql::input::GameplayCatcherStateChanged(
		ui, ui | retailMousePass, retailMousePass ) );
	CHECK( fnql::input::GameplayCatcherStateChanged(
		retailMousePass, ui | retailMousePass, retailMousePass ) );
	CHECK( fnql::input::GameplayCatcherStateChanged(
		console, ui, retailMousePass ) );
}

void TestDeferredInputCommandGenerationHelpers()
{
	CHECK( fnql::input::IsCanonicalCommandSegment(
		"+forward", "+forward" ) );
	CHECK( fnql::input::IsCanonicalCommandSegment(
		"+FoRwArD \t", "+forward" ) );
	CHECK( !fnql::input::IsCanonicalCommandSegment(
		"+forward extra", "+forward" ) );
	CHECK( !fnql::input::IsCanonicalCommandSegment(
		"+scores", "+forward" ) );
	CHECK( !fnql::input::IsCanonicalCommandSegment(
		nullptr, "+forward" ) );

	auto parsed =
		fnql::input::ParseUnsignedInputCommandArgument( "0" );
	CHECK( parsed && *parsed == 0u );
	const std::string maximum =
		std::to_string( ( std::numeric_limits<unsigned>::max )() );
	parsed =
		fnql::input::ParseUnsignedInputCommandArgument( maximum.c_str() );
	CHECK( parsed && *parsed ==
		( std::numeric_limits<unsigned>::max )() );
	CHECK( !fnql::input::ParseUnsignedInputCommandArgument(
		( maximum + "0" ).c_str() ) );
	CHECK( !fnql::input::ParseUnsignedInputCommandArgument( "" ) );
	CHECK( !fnql::input::ParseUnsignedInputCommandArgument( nullptr ) );
	CHECK( !fnql::input::ParseUnsignedInputCommandArgument( "-1" ) );
	CHECK( !fnql::input::ParseUnsignedInputCommandArgument( "+1" ) );
	CHECK( !fnql::input::ParseUnsignedInputCommandArgument( "1x" ) );

	auto signedParsed =
		fnql::input::ParseSignedInputCommandArgument( "-1" );
	CHECK( signedParsed && *signedParsed == -1 );
	signedParsed = fnql::input::ParseSignedInputCommandArgument( "+1" );
	CHECK( signedParsed && *signedParsed == 1 );
	signedParsed = fnql::input::ParseSignedInputCommandArgument( "2147483647" );
	CHECK( signedParsed &&
		*signedParsed == ( std::numeric_limits<int>::max )() );
	signedParsed = fnql::input::ParseSignedInputCommandArgument(
		std::to_string( ( std::numeric_limits<int>::min )() ).c_str() );
	CHECK( signedParsed &&
		*signedParsed == ( std::numeric_limits<int>::min )() );
	CHECK( !fnql::input::ParseSignedInputCommandArgument( "1x" ) );
	CHECK( !fnql::input::ParseSignedInputCommandArgument( "2147483648" ) );
	CHECK( !fnql::input::ParseSignedInputCommandArgument( "-2147483649" ) );
	CHECK( !fnql::input::ParseSignedInputCommandArgument( "" ) );
	CHECK( fnql::input::SaturatingAddUnsigned(
		( std::numeric_limits<unsigned>::max )() - 1u, 2u ) ==
		( std::numeric_limits<unsigned>::max )() );
	CHECK( fnql::input::BoundedInputElapsedMilliseconds(
		( std::numeric_limits<unsigned>::max )() - 2u, 3u, 20u ) == 6u );
	CHECK( fnql::input::BoundedInputElapsedMilliseconds( 100u, 90u, 20u ) == 20u );

	auto tag = fnql::input::ParseInputCommandGenerationTag(
		"fnql-gen:42" );
	CHECK( tag.tagged && tag.valid && tag.value == 42u );
	tag = fnql::input::ParseInputCommandGenerationTag( "27" );
	CHECK( !tag.tagged && !tag.valid );
	tag = fnql::input::ParseInputCommandGenerationTag( "" );
	CHECK( !tag.tagged && !tag.valid );
	tag = fnql::input::ParseInputCommandGenerationTag( "fnql-gen:" );
	CHECK( tag.tagged && !tag.valid );
	tag = fnql::input::ParseInputCommandGenerationTag(
		( std::string( "fnql-gen:" ) + maximum + "0" ).c_str() );
	CHECK( tag.tagged && !tag.valid );

	std::array<int, 2> sources{ 200, 17 };
	CHECK( fnql::input::RemoveHeldInputSource( sources, 200 ) );
	CHECK( sources[0] == 17 && sources[1] == 0 );
	CHECK( !fnql::input::RemoveHeldInputSource( sources, 200 ) );
	CHECK( !fnql::input::RemoveHeldInputSource( sources, 0 ) );
	sources = { -1, 17 };
	CHECK( fnql::input::RemoveHeldInputSource( sources, 17 ) );
	CHECK( sources[0] == -1 && sources[1] == 0 );
}

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

	begin = filter.Begin( { 25.0f, -6.0f }, 1 );
	CHECK( Near( begin.yaw, 25.0f ) );
	CHECK( Near( begin.pitch, -6.0f ) );
	visible = filter.End( { 27.0f, -4.0f } );
	CHECK( Near( visible.yaw, 27.0f ) );
	CHECK( Near( visible.pitch, -4.0f ) );

	filter.Reset( { 0.0f, 0.0f } );
	begin = filter.Begin( { 0.0f, 0.0f }, 3 );
	visible = filter.End( { 3.0f, -3.0f } );
	begin = filter.Begin( { visible.yaw + 10.0f, visible.pitch + 20.0f }, 3 );
	CHECK( Near( begin.yaw, 13.0f ) );
	CHECK( Near( begin.pitch, 17.0f ) );
	visible = filter.End( { 16.0f, 14.0f } );
	CHECK( Near( visible.yaw, 14.5f ) );
	CHECK( Near( visible.pitch, 15.5f ) );
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
	CHECK( fnql::input::RetailJoystickLookDelta(
		-1.0f, 0.0f, std::numeric_limits<float>::max(), 1.0f, false ) == -32767 );
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

	const unsigned char euro[] = { 0xe2u, 0x82u, 0xacu };
	auto decoded = fnql::input::DecodeUtf8( euro, sizeof( euro ) );
	CHECK( decoded.valid && decoded.size == 3 && decoded.codepoint == 0x20acu );

	const unsigned char rocket[] = { 0xf0u, 0x9fu, 0x9au, 0x80u };
	decoded = fnql::input::DecodeUtf8( rocket, sizeof( rocket ) );
	CHECK( decoded.valid && decoded.size == 4 && decoded.codepoint == 0x1f680u );

	const unsigned char truncated[] = { 0xe2u, 0x82u };
	decoded = fnql::input::DecodeUtf8( truncated, sizeof( truncated ) );
	CHECK( !decoded.valid && decoded.size == 1 );

	const unsigned char badContinuation[] = { 0xe2u, 'A', 0xacu };
	decoded = fnql::input::DecodeUtf8( badContinuation, sizeof( badContinuation ) );
	CHECK( !decoded.valid && decoded.size == 1 );

	const unsigned char overlong[] = { 0xc0u, 0xafu };
	decoded = fnql::input::DecodeUtf8( overlong, sizeof( overlong ) );
	CHECK( !decoded.valid && decoded.size == 1 );

	const unsigned char surrogate[] = { 0xedu, 0xa0u, 0x80u };
	decoded = fnql::input::DecodeUtf8( surrogate, sizeof( surrogate ) );
	CHECK( !decoded.valid && decoded.size == 1 );

	const unsigned char tooLarge[] = { 0xf4u, 0x90u, 0x80u, 0x80u };
	decoded = fnql::input::DecodeUtf8( tooLarge, sizeof( tooLarge ) );
	CHECK( !decoded.valid && decoded.size == 1 );
}

// Mirrors q_shared.h, which stays the canonical owner of the ABI values.
constexpr int kCatchConsole = 0x0001;
constexpr int kCatchUi = 0x0002;
constexpr int kCatchMessage = 0x0004;
constexpr int kCatchCgame = 0x0008;
constexpr int kCatchMousePass = 0x0010;
constexpr int kCatchBrowser = 0x0020;
constexpr int kCatchMenus = kCatchUi | kCatchCgame | kCatchBrowser;

fnql::input::PointerOwner ResolveOwner( int catcher, bool consoleAbsolute )
{
	fnql::input::PointerOwnerInputs inputs;
	inputs.catcher = catcher;
	inputs.consoleMask = kCatchConsole;
	inputs.menuMask = kCatchMenus;
	inputs.consoleUsesAbsolutePointer = consoleAbsolute;
	return fnql::input::ResolvePointerOwner( inputs );
}

fnql::input::PointerMode ResolveMode( fnql::input::PointerOwner owner,
	bool fullscreen, bool focused = true, bool minimized = false,
	bool relativeAvailable = true )
{
	fnql::input::PointerModeInputs inputs;
	inputs.owner = owner;
	inputs.fullscreen = fullscreen;
	inputs.focused = focused;
	inputs.minimized = minimized;
	inputs.relativeAvailable = relativeAvailable;
	return fnql::input::ResolvePointerMode( inputs );
}

void TestPointerOwnership()
{
	using fnql::input::PointerOwner;

	CHECK( ResolveOwner( 0, true ) == PointerOwner::Gameplay );
	CHECK( ResolveOwner( kCatchMessage, true ) == PointerOwner::Gameplay );
	CHECK( ResolveOwner( kCatchMousePass, true ) == PointerOwner::Gameplay );

	CHECK( ResolveOwner( kCatchUi, true ) == PointerOwner::Menu );
	CHECK( ResolveOwner( kCatchCgame, true ) == PointerOwner::Menu );
	CHECK( ResolveOwner( kCatchBrowser, true ) == PointerOwner::Menu );
	CHECK( ResolveOwner( kCatchUi | kCatchCgame | kCatchBrowser, true ) ==
		PointerOwner::Menu );

	// The console is an overlay: it preserves the underlying menu catcher and
	// takes ownership from it while it can present an absolute cursor.
	CHECK( ResolveOwner( kCatchConsole, true ) == PointerOwner::Console );
	CHECK( ResolveOwner( kCatchConsole | kCatchUi, true ) == PointerOwner::Console );
	CHECK( ResolveOwner( kCatchConsole | kCatchBrowser, true ) == PointerOwner::Console );

	// A backend that cannot present an absolute console cursor (fullscreen)
	// keeps the relative gameplay pointer, even over an open menu.
	CHECK( ResolveOwner( kCatchConsole, false ) == PointerOwner::Gameplay );
	CHECK( ResolveOwner( kCatchConsole | kCatchUi, false ) == PointerOwner::Gameplay );

	CHECK( !fnql::input::PointerOwnerReportsAbsolute( PointerOwner::Gameplay ) );
	CHECK( fnql::input::PointerOwnerReportsAbsolute( PointerOwner::Console ) );
	CHECK( fnql::input::PointerOwnerReportsAbsolute( PointerOwner::Menu ) );
}

void TestPointerPresentation()
{
	using fnql::input::PointerOwner;

	// Gameplay confines and hides the pointer and re-centres it on entry.
	const auto gameplay = ResolveMode( PointerOwner::Gameplay, false );
	CHECK( gameplay.driveInput );
	CHECK( gameplay.relativeMotion );
	CHECK( gameplay.confineToWindow );
	CHECK( !gameplay.showSystemCursor );
	CHECK( !gameplay.reportAbsolute );
	CHECK( gameplay.recenterPointer );

	// Retail leaves the desktop pointer alone when gameplay has no enabled
	// relative source; absolute menus remain independently usable.
	const auto noDevice = ResolveMode( PointerOwner::Gameplay, false, true, false, false );
	CHECK( !noDevice.driveInput );
	CHECK( !noDevice.relativeMotion );
	CHECK( !noDevice.confineToWindow );
	CHECK( noDevice.showSystemCursor );

	// A windowed menu leaves the pointer free so the desktop stays reachable.
	const auto windowedMenu = ResolveMode( PointerOwner::Menu, false );
	CHECK( windowedMenu.driveInput );
	CHECK( windowedMenu.reportAbsolute );
	CHECK( !windowedMenu.relativeMotion );
	CHECK( !windowedMenu.confineToWindow );
	CHECK( windowedMenu.showSystemCursor );
	CHECK( !windowedMenu.recenterPointer );

	// A fullscreen menu confines it: an unconfined pointer can be clicked onto
	// another display and drop the game out of focus mid-menu.
	const auto fullscreenMenu = ResolveMode( PointerOwner::Menu, true );
	CHECK( fullscreenMenu.confineToWindow );
	CHECK( fullscreenMenu.reportAbsolute );
	CHECK( fullscreenMenu.showSystemCursor );
	CHECK( !fullscreenMenu.relativeMotion );
	CHECK( !fullscreenMenu.recenterPointer );

	// The console draws its own cursor, so the OS cursor stays hidden while it
	// receives the mirrored absolute position.
	const auto console = ResolveMode( PointerOwner::Console, false );
	CHECK( console.reportAbsolute );
	CHECK( !console.showSystemCursor );
	CHECK( !console.confineToWindow );
	const auto fullscreenConsole = ResolveMode( PointerOwner::Console, true );
	CHECK( fullscreenConsole.confineToWindow );
	CHECK( !fullscreenConsole.showSystemCursor );

	// Nothing is driven, confined, or hidden while the window is not usable.
	for ( const PointerOwner owner : { PointerOwner::Gameplay, PointerOwner::Console,
		PointerOwner::Menu } ) {
		for ( const bool fullscreen : { false, true } ) {
			const auto unfocused = ResolveMode( owner, fullscreen, false, false );
			const auto minimized = ResolveMode( owner, fullscreen, true, true );
			for ( const auto& mode : { unfocused, minimized } ) {
				CHECK( !mode.driveInput );
				CHECK( !mode.reportAbsolute );
				CHECK( !mode.relativeMotion );
				CHECK( !mode.confineToWindow );
				CHECK( !mode.recenterPointer );
				CHECK( mode.showSystemCursor );
			}
		}
	}

	// Backends latch the applied mode, so equality has to compare every axis.
	auto changed = fullscreenMenu;
	CHECK( changed == fullscreenMenu );
	changed.confineToWindow = !changed.confineToWindow;
	CHECK( changed != fullscreenMenu );
	CHECK( ResolveMode( PointerOwner::Menu, true ) != ResolveMode( PointerOwner::Menu, false ) );
}

void TestPointerProjection()
{
	fnql::input::PointerProjection projection;
	projection.hostWidth = 1280;
	projection.hostHeight = 720;
	projection.drawableWidth = 1920;
	projection.drawableHeight = 1080;

	auto position = fnql::input::ProjectPointerToDrawable( 0, 0, projection );
	CHECK( position.x == 0 && position.y == 0 );

	position = fnql::input::ProjectPointerToDrawable( 640, 360, projection );
	CHECK( position.x == 960 && position.y == 540 );

	// A host coordinate strictly inside the window must stay strictly inside the
	// drawable, or retail's UI rejects the event and the menu stops responding.
	position = fnql::input::ProjectPointerToDrawable( 1279, 719, projection );
	CHECK( position.x < projection.drawableWidth );
	CHECK( position.y < projection.drawableHeight );

	// Renderer smaller than the window: this is the case that used to send the
	// retail UI virtual coordinates beyond 640x480, where it drops the event.
	projection.hostWidth = 1920;
	projection.hostHeight = 1080;
	projection.drawableWidth = 1024;
	projection.drawableHeight = 768;
	position = fnql::input::ProjectPointerToDrawable( 1919, 1079, projection );
	CHECK( position.x < projection.drawableWidth );
	CHECK( position.y < projection.drawableHeight );

	// Matching spaces are an identity, so backends that already agreed keep
	// byte-identical behaviour.
	projection.hostWidth = projection.drawableWidth = 1600;
	projection.hostHeight = projection.drawableHeight = 900;
	position = fnql::input::ProjectPointerToDrawable( 733, 411, projection );
	CHECK( position.x == 733 && position.y == 411 );

	// Unknown geometry passes through rather than collapsing to zero.
	projection = fnql::input::PointerProjection{};
	position = fnql::input::ProjectPointerToDrawable( 42, 24, projection );
	CHECK( position.x == 42 && position.y == 24 );

	// A drag can leave the window; negatives must survive for the owner to clamp.
	projection.hostWidth = 800;
	projection.hostHeight = 600;
	projection.drawableWidth = 1600;
	projection.drawableHeight = 1200;
	position = fnql::input::ProjectPointerToDrawable( -10, -20, projection );
	CHECK( position.x == -20 && position.y == -40 );

	// Host APIs are not trusted to stay in their nominal window range. Extreme
	// coordinates saturate instead of overflowing or invoking float-cast UB.
	projection.hostWidth = 1;
	projection.hostHeight = 1;
	projection.drawableWidth = std::numeric_limits<int>::max();
	projection.drawableHeight = std::numeric_limits<int>::max();
	position = fnql::input::ProjectPointerToDrawable(
		std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), projection );
	CHECK( position.x == std::numeric_limits<int>::max() );
	CHECK( position.y == std::numeric_limits<int>::min() );

	CHECK( fnql::input::SaturatingAddInt(
		std::numeric_limits<int>::max(), 1 ) == std::numeric_limits<int>::max() );
	CHECK( fnql::input::SaturatingAddInt(
		std::numeric_limits<int>::min(), -1 ) == std::numeric_limits<int>::min() );
	CHECK( fnql::input::SaturatingAddInt( 40, 2 ) == 42 );

	CHECK( fnql::input::TruncateFiniteFloatToInt(
		std::numeric_limits<float>::infinity() ) == 0 );
	CHECK( fnql::input::TruncateFiniteFloatToInt( 42.75f ) == 42 );

	// SDL supplies float window coordinates. Preserve their fractional part
	// through drawable scaling, then truncate exactly once.
	projection.hostWidth = projection.hostHeight = 100;
	projection.drawableWidth = projection.drawableHeight = 200;
	position = fnql::input::ProjectPointerToDrawable(
		10.5f, -10.5f, projection );
	CHECK( position.x == 21 && position.y == -21 );

	projection.hostWidth = projection.drawableWidth = 100;
	projection.hostHeight = projection.drawableHeight = 100;
	position = fnql::input::ProjectPointerToDrawable(
		10.75f, -10.75f, projection );
	CHECK( position.x == 10 && position.y == -10 );

	position = fnql::input::ProjectPointerToDrawable(
		std::numeric_limits<float>::quiet_NaN(),
		std::numeric_limits<float>::infinity(), projection );
	CHECK( position.x == 0 && position.y == 0 );

	projection.hostWidth = projection.hostHeight = 1;
	projection.drawableWidth = projection.drawableHeight =
		std::numeric_limits<int>::max();
	position = fnql::input::ProjectPointerToDrawable(
		( std::numeric_limits<float>::max )(),
		-( std::numeric_limits<float>::max )(), projection );
	CHECK( position.x == std::numeric_limits<int>::max() );
	CHECK( position.y == std::numeric_limits<int>::min() );
}

void TestFiniteInputConversions()
{
	const float infinity = std::numeric_limits<float>::infinity();
	const float quietNaN = std::numeric_limits<float>::quiet_NaN();
	const float maximum = ( std::numeric_limits<float>::max )();
	const int intMinimum = ( std::numeric_limits<int>::min )();
	const int intMaximum = ( std::numeric_limits<int>::max )();

	CHECK( fnql::input::FiniteOr( 12.5f, 3.0f ) == 12.5f );
	CHECK( fnql::input::FiniteOr( infinity, 3.0f ) == 3.0f );
	CHECK( fnql::input::FiniteOr( quietNaN, 3.0f ) == 3.0f );

	CHECK( fnql::input::TruncateFiniteFloatToInt( quietNaN ) == 0 );
	CHECK( fnql::input::TruncateFiniteFloatToInt( infinity ) == 0 );
	CHECK( fnql::input::TruncateFiniteFloatToInt( -infinity ) == 0 );
	CHECK( fnql::input::TruncateFiniteFloatToInt( maximum ) ==
		( std::numeric_limits<int>::max )() );
	CHECK( fnql::input::TruncateFiniteFloatToInt( -maximum ) ==
		intMinimum );
	CHECK( fnql::input::TruncateFiniteFloatToInt( -42.75f ) == -42 );

	// Ordinary angles must take the exact legacy path, while pathological
	// finite values are reduced before ANGLE2SHORT's float-to-int cast.
	CHECK( fnql::input::FiniteAngleForShort( 720.25f ) == 720.25f );
	CHECK( fnql::input::FiniteAngleForShort( quietNaN ) == 0.0f );
	CHECK( fnql::input::FiniteAngleForShort( infinity ) == 0.0f );
	const float reducedAngle = fnql::input::FiniteAngleForShort( maximum );
	CHECK( std::isfinite( reducedAngle ) );
	CHECK( std::fabs( reducedAngle ) < 360.0f );

	CHECK( Near( fnql::input::FiniteJoystickDeadzone( -1.0f ), 0.0f ) );
	CHECK( Near( fnql::input::FiniteJoystickDeadzone( 2.0f ), 1.0f ) );
	CHECK( Near( fnql::input::FiniteJoystickDeadzone( quietNaN ), 1.0f ) );
	CHECK( fnql::input::ApplyJoystickDeadzone( 3000, 0.15f ) == 0 );
	CHECK( fnql::input::ApplyJoystickDeadzone( 32767, 0.15f ) == 32767 );
	CHECK( fnql::input::ApplyJoystickDeadzone( -32768, 0.15f ) == -32768 );
	CHECK( fnql::input::ApplyJoystickDeadzone( 32767, 1.0f ) == 0 );
	CHECK( fnql::input::ApplyJoystickDeadzone( 32767, quietNaN ) == 0 );

	CHECK( fnql::input::StrongerJoystickAxis( 100, -99 ) == 100 );
	CHECK( fnql::input::StrongerJoystickAxis( 100, -101 ) == -101 );
	CHECK( fnql::input::StrongerJoystickAxis( -100, 100 ) == -100 );
	CHECK( fnql::input::StrongerJoystickAxis( 1, intMinimum ) == intMinimum );
	CHECK( fnql::input::SaturatingIntFromInt64(
		static_cast<std::int64_t>( intMaximum ) * 2 ) == intMaximum );
	CHECK( fnql::input::SaturatingIntFromInt64(
		static_cast<std::int64_t>( intMinimum ) * 2 ) == intMinimum );
}

} // namespace

int main()
{
	TestRetailGameplayCatcher();
	TestDeferredInputCommandGenerationHelpers();
	TestPointerOwnership();
	TestPointerPresentation();
	TestPointerProjection();
	TestRetailMouseMotion();
	TestRetailViewFilter();
	TestRetailJoystickMath();
	TestUnicodeTranslation();
	TestFiniteInputConversions();
	return failures == 0 ? 0 : 1;
}
