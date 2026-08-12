/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/
#ifndef FNQL_INPUT_COMPAT_HPP
#define FNQL_INPUT_COMPAT_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace fnql::input {

/*
Retail catcher bit 0x10 is transparent to gameplay input.  In particular,
retail Quake Live keeps movement active while a held scoreboard uses that bit.
Keep the pass-through mask as a parameter so q_shared.h remains the canonical
owner of the ABI value.
*/
[[nodiscard]] constexpr int GameplayCatcherBits(
	int catcher, int passThroughMask ) noexcept
{
	return catcher & ~passThroughMask;
}

[[nodiscard]] constexpr bool CatcherBlocksGameplayInput(
	int catcher, int passThroughMask ) noexcept
{
	return GameplayCatcherBits( catcher, passThroughMask ) != 0;
}

[[nodiscard]] constexpr bool GameplayCatcherStateChanged(
	int previous, int next, int passThroughMask ) noexcept
{
	return GameplayCatcherBits( previous, passThroughMask ) !=
		GameplayCatcherBits( next, passThroughMask );
}

/*
Deferred engine-owned +/- commands carry a per-source generation. Keep both
the command-segment match and numeric parsing locale-independent and exact so
ordinary cgame, UI, and server +commands retain their retail command text.
*/
[[nodiscard]] constexpr bool IsAsciiCommandWhitespace( char ch ) noexcept
{
	return ch == ' ' || ch == '\t' || ch == '\r' ||
		ch == '\n' || ch == '\v' || ch == '\f';
}

[[nodiscard]] constexpr char AsciiCommandLower( char ch ) noexcept
{
	return ch >= 'A' && ch <= 'Z'
		? static_cast<char>( ch + ( 'a' - 'A' ) )
		: ch;
}

[[nodiscard]] constexpr bool IsCanonicalCommandSegment(
	const char* segment, const char* command ) noexcept
{
	if ( !segment || !command || !segment[0] || !command[0] ) {
		return false;
	}

	std::size_t index = 0;
	while ( segment[index] &&
		!IsAsciiCommandWhitespace( segment[index] ) ) {
		if ( !command[index] ||
			AsciiCommandLower( segment[index] ) !=
				AsciiCommandLower( command[index] ) ) {
			return false;
		}
		++index;
	}
	if ( command[index] ) {
		return false;
	}

	while ( IsAsciiCommandWhitespace( segment[index] ) ) {
		++index;
	}
	return segment[index] == '\0';
}

[[nodiscard]] inline std::optional<unsigned>
ParseUnsignedInputCommandArgument( const char* text ) noexcept
{
	if ( !text || !text[0] ) {
		return std::nullopt;
	}

	unsigned value = 0;
	for ( const char* cursor = text; *cursor; ++cursor ) {
		if ( *cursor < '0' || *cursor > '9' ) {
			return std::nullopt;
		}
		const unsigned digit = static_cast<unsigned>( *cursor - '0' );
		if ( value >
			( ( std::numeric_limits<unsigned>::max )() - digit ) / 10u ) {
			return std::nullopt;
		}
		value = value * 10u + digit;
	}
	return value;
}

[[nodiscard]] inline std::optional<int>
ParseSignedInputCommandArgument( const char* text ) noexcept
{
	if ( !text || !text[0] ) {
		return std::nullopt;
	}

	const char* cursor = text;
	bool negative = false;
	if ( *cursor == '+' || *cursor == '-' ) {
		negative = *cursor == '-';
		++cursor;
		if ( !*cursor ) {
			return std::nullopt;
		}
	}

	const unsigned positiveLimit = static_cast<unsigned>(
		( std::numeric_limits<int>::max )() );
	const unsigned limit = negative ? positiveLimit + 1u : positiveLimit;
	unsigned magnitude = 0;
	for ( ; *cursor; ++cursor ) {
		if ( *cursor < '0' || *cursor > '9' ) {
			return std::nullopt;
		}
		const unsigned digit = static_cast<unsigned>( *cursor - '0' );
		if ( magnitude > ( limit - digit ) / 10u ) {
			return std::nullopt;
		}
		magnitude = magnitude * 10u + digit;
	}

	if ( !negative ) {
		return static_cast<int>( magnitude );
	}
	if ( magnitude == positiveLimit + 1u ) {
		return ( std::numeric_limits<int>::min )();
	}
	return -static_cast<int>( magnitude );
}

[[nodiscard]] constexpr unsigned SaturatingAddUnsigned(
	unsigned lhs, unsigned rhs ) noexcept
{
	const unsigned maximum = ( std::numeric_limits<unsigned>::max )();
	return lhs > maximum - rhs ? maximum : lhs + rhs;
}

/*
Input timestamps use the engine's wrapping 32-bit millisecond clock. Perform
the subtraction in unsigned space, then bound it to the usercmd interval so a
stale, malformed, or future timestamp cannot turn one transition into an
unbounded movement sample.
*/
[[nodiscard]] constexpr unsigned BoundedInputElapsedMilliseconds(
	unsigned start, unsigned end, unsigned maximum ) noexcept
{
	const unsigned elapsed = end - start;
	return elapsed < maximum ? elapsed : maximum;
}

struct InputCommandGenerationTag {
	bool tagged = false;
	bool valid = false;
	unsigned value = 0;
};

[[nodiscard]] inline InputCommandGenerationTag ParseInputCommandGenerationTag(
	const char* text ) noexcept
{
	static constexpr char prefix[] = "fnql-gen:";
	if ( !text ) {
		return {};
	}

	const char* cursor = text;
	for ( const char expected : prefix ) {
		if ( expected == '\0' ) {
			break;
		}
		if ( *cursor != expected ) {
			return {};
		}
		++cursor;
	}

	const std::optional<unsigned> value =
		ParseUnsignedInputCommandArgument( cursor );
	return value
		? InputCommandGenerationTag{ true, true, *value }
		: InputCommandGenerationTag{ true, false, 0 };
}

/*
Remove one physical source from a two-source engine button without disturbing
the other source or aggregate per-frame state.
*/
[[nodiscard]] constexpr bool RemoveHeldInputSource(
	std::array<int, 2>& sources, int source ) noexcept
{
	if ( source == 0 ) {
		return false;
	}

	bool removed = false;
	for ( int& heldSource : sources ) {
		if ( heldSource == source ) {
			heldSource = 0;
			removed = true;
		}
	}
	if ( sources[0] == 0 && sources[1] != 0 ) {
		sources[0] = sources[1];
		sources[1] = 0;
	}
	return removed;
}

/*
Pointer ownership.

Gameplay, the engine console, and retail's absolute-input overlays (native UI,
cgame, and the WebUI browser) each want different pointer handling, and every
platform backend used to derive that decision with its own predicate. Resolve
it once so the SDL, Win32, and X11 backends agree on the owner for a given key
catcher and cannot drift apart again.

Console toggling preserves any underlying menu catcher, so the console overlay
is tested first. A backend that cannot present an absolute console cursor for
the current display mode passes consoleUsesAbsolutePointer=false, which leaves
the pointer in its established relative gameplay mode. q_shared.h stays the
canonical owner of the catcher bit values, so the masks are parameters.
*/
enum class PointerOwner {
	Gameplay, // relative motion drives the view
	Console,  // the engine console draws its own cursor over the window
	Menu      // a retail UI/cgame/browser overlay consumes absolute positions
};

struct PointerOwnerInputs {
	int catcher = 0;
	int consoleMask = 0;
	int menuMask = 0;
	bool consoleUsesAbsolutePointer = false;
};

[[nodiscard]] constexpr PointerOwner ResolvePointerOwner(
	const PointerOwnerInputs& inputs ) noexcept
{
	if ( inputs.catcher & inputs.consoleMask ) {
		return inputs.consoleUsesAbsolutePointer ? PointerOwner::Console
		                                         : PointerOwner::Gameplay;
	}
	return ( inputs.catcher & inputs.menuMask ) ? PointerOwner::Menu
	                                            : PointerOwner::Gameplay;
}

[[nodiscard]] constexpr bool PointerOwnerReportsAbsolute( PointerOwner owner ) noexcept
{
	return owner != PointerOwner::Gameplay;
}

/*
Pointer presentation.

Confinement, relative motion, and OS cursor visibility are independent axes.
Binding them to a single "grabbed" flag is what left fullscreen menus with an
unconfined pointer that could be moved onto another display and clicked, which
drops the game out of focus mid-menu. Windowed overlays keep the free pointer
retail exposes, because there the desktop has to stay reachable.

An unfocused or minimized window drives no pointer input at all: it must not
confine the pointer, hide the cursor, or keep reporting positions into a menu
that the user is no longer looking at.
*/
struct PointerModeInputs {
	PointerOwner owner = PointerOwner::Gameplay;
	bool focused = true;
	bool minimized = false;
	bool fullscreen = false;
	bool relativeAvailable = true; // in_mouse selects a relative device
};

struct PointerMode {
	bool driveInput = false;      // sample or report the pointer at all
	bool reportAbsolute = false;  // SE_MOUSE_ABSOLUTE lane
	bool relativeMotion = false;  // SE_MOUSE lane from a captured pointer
	bool confineToWindow = false; // clip/grab the pointer to the client area
	bool showSystemCursor = true; // OS cursor over the client area
	bool recenterPointer = false; // warp to the window centre when entered
};

[[nodiscard]] constexpr bool operator==( const PointerMode& a, const PointerMode& b ) noexcept
{
	return a.driveInput == b.driveInput &&
		a.reportAbsolute == b.reportAbsolute &&
		a.relativeMotion == b.relativeMotion &&
		a.confineToWindow == b.confineToWindow &&
		a.showSystemCursor == b.showSystemCursor &&
		a.recenterPointer == b.recenterPointer;
}

[[nodiscard]] constexpr bool operator!=( const PointerMode& a, const PointerMode& b ) noexcept
{
	return !( a == b );
}

/*
Absolute pointer coordinate space.

Every absolute consumer works in renderer drawable pixels, not host-window
coordinates:

- retail's _UI_MouseEvent divides by uiInfo.uiDC.glconfig.vidWidth/vidHeight to
  reach its 640x480 cursor space, and drops the event entirely when the result
  leaves that range, so a mismatched space makes an in-game menu completely
  unresponsive rather than merely inaccurate;
- retail's CG_MouseEvent divides by cgs.glconfig.vidWidth/vidHeight and clamps;
- the engine console and the WebUI browser address drawable pixels directly.

The two spaces differ whenever the renderer resolution is not the window size,
and on SDL they also differ on a scaled desktop, where motion events are
reported in logical window coordinates. Backends project once, here, so no
consumer has to guess which space it was handed.

Truncation is deliberate: a host coordinate strictly inside the window stays
strictly inside the drawable, which keeps the retail UI's upper-bound test from
rejecting the last row and column.
*/
struct PointerProjection {
	int hostWidth = 0;
	int hostHeight = 0;
	int drawableWidth = 0;
	int drawableHeight = 0;
};

struct PointerPosition {
	int x = 0;
	int y = 0;
};

[[nodiscard]] constexpr int SaturatingIntFromInt64( std::int64_t value ) noexcept
{
	return value > ( std::numeric_limits<int>::max )()
		? ( std::numeric_limits<int>::max )()
		: value < ( std::numeric_limits<int>::min )()
			? ( std::numeric_limits<int>::min )()
			: static_cast<int>( value );
}

[[nodiscard]] constexpr int SaturatingAddInt( int lhs, int rhs ) noexcept
{
	return SaturatingIntFromInt64(
		static_cast<std::int64_t>( lhs ) + static_cast<std::int64_t>( rhs ) );
}

[[nodiscard]] inline float FiniteOr( float value, float fallback ) noexcept
{
	return std::isfinite( value ) ? value : fallback;
}

[[nodiscard]] inline int TruncateFiniteFloatToInt( float value ) noexcept
{
	if ( !std::isfinite( value ) ) {
		return 0;
	}
	if ( value >= static_cast<float>( ( std::numeric_limits<int>::max )() ) ) {
		return ( std::numeric_limits<int>::max )();
	}
	if ( value <= static_cast<float>( ( std::numeric_limits<int>::min )() ) ) {
		return ( std::numeric_limits<int>::min )();
	}
	return static_cast<int>( value );
}

/*
ANGLE2SHORT's legacy float-to-int cast is correct for ordinary accumulated
view angles, but undefined for NaN, infinity, or a finite value whose scaled
form is outside int. Preserve the ordinary value byte-for-byte; only reduce a
pathological finite extreme to its equivalent turn before the legacy macro.
*/
[[nodiscard]] inline float FiniteAngleForShort( float angle ) noexcept
{
	angle = FiniteOr( angle, 0.0f );
	const float scaled = angle * 65536.0f / 360.0f;
	if ( std::isfinite( scaled ) &&
		scaled < static_cast<float>( ( std::numeric_limits<int>::max )() ) &&
		scaled >= static_cast<float>( ( std::numeric_limits<int>::min )() ) ) {
		return angle;
	}
	return std::fmod( angle, 360.0f );
}

[[nodiscard]] constexpr int ProjectPointerCoordinate(
	int value, int hostExtent, int drawableExtent ) noexcept
{
	if ( hostExtent <= 0 || drawableExtent <= 0 ) {
		return value;
	}

	// int is at most 32 bits on every supported FnQL target. The product
	// therefore fits in int64_t, and integer division preserves the deliberate
	// truncate-toward-zero mapping without an out-of-range float-to-int cast.
	const std::int64_t scaled = static_cast<std::int64_t>( value ) *
		static_cast<std::int64_t>( drawableExtent ) /
		static_cast<std::int64_t>( hostExtent );
	return SaturatingIntFromInt64( scaled );
}

[[nodiscard]] inline int ProjectPointerCoordinate(
	float value, int hostExtent, int drawableExtent ) noexcept
{
	if ( !std::isfinite( value ) ) {
		return 0;
	}

	// SDL preserves subpixel window coordinates. Scale in a wider type before
	// the one deliberate truncate so a logical 10.5 coordinate at 2x becomes
	// drawable pixel 21, rather than losing the half-pixel before projection.
	double scaled = static_cast<double>( value );
	if ( hostExtent > 0 && drawableExtent > 0 ) {
		scaled = scaled * static_cast<double>( drawableExtent ) /
			static_cast<double>( hostExtent );
	}
	if ( scaled >= static_cast<double>(
			( std::numeric_limits<int>::max )() ) ) {
		return ( std::numeric_limits<int>::max )();
	}
	if ( scaled <= static_cast<double>(
			( std::numeric_limits<int>::min )() ) ) {
		return ( std::numeric_limits<int>::min )();
	}
	return static_cast<int>( scaled );
}

[[nodiscard]] inline PointerPosition ProjectPointerToDrawable(
	int x, int y, const PointerProjection& projection ) noexcept
{
	return {
		ProjectPointerCoordinate(
			x, projection.hostWidth, projection.drawableWidth ),
		ProjectPointerCoordinate(
			y, projection.hostHeight, projection.drawableHeight )
	};
}

[[nodiscard]] inline PointerPosition ProjectPointerToDrawable(
	float x, float y, const PointerProjection& projection ) noexcept
{
	return {
		ProjectPointerCoordinate(
			x, projection.hostWidth, projection.drawableWidth ),
		ProjectPointerCoordinate(
			y, projection.hostHeight, projection.drawableHeight )
	};
}

[[nodiscard]] constexpr PointerMode ResolvePointerMode(
	const PointerModeInputs& inputs ) noexcept
{
	PointerMode mode;

	if ( !inputs.focused || inputs.minimized ) {
		return mode;
	}

	mode.driveInput = true;
	switch ( inputs.owner ) {
		case PointerOwner::Console:
			// The console draws its own cursor, so the OS pointer stays hidden
			// over the client area and is mirrored through the absolute lane.
			mode.reportAbsolute = true;
			mode.showSystemCursor = false;
			mode.confineToWindow = inputs.fullscreen;
			break;

		case PointerOwner::Menu:
			mode.reportAbsolute = true;
			mode.showSystemCursor = true;
			mode.confineToWindow = inputs.fullscreen;
			break;

		case PointerOwner::Gameplay:
		default:
			if ( !inputs.relativeAvailable ) {
				// Retail keeps absolute menus usable with in_mouse 0, but does
				// not capture or hide the desktop pointer for gameplay when no
				// relative source is enabled.
				return PointerMode{};
			}
			mode.relativeMotion = inputs.relativeAvailable;
			mode.confineToWindow = true;
			mode.showSystemCursor = false;
			mode.recenterPointer = true;
			break;
	}

	return mode;
}

constexpr std::size_t kRetailMouseFilterCapacity = 32;
constexpr int kRetailMouseFilterMaximum =
	static_cast<int>( kRetailMouseFilterCapacity ) - 1;

struct RetailMouseParameters {
	float sensitivity = 0.0f;
	float acceleration = 0.0f;
	float accelerationOffset = 0.0f;
	float accelerationPower = 2.0f;
	float sensitivityCap = 0.0f;
	float countsPerInch = 0.0f;
	int frameMilliseconds = 1;
};

struct RetailMouseMotion {
	float sampleX = 0.0f;
	float sampleY = 0.0f;
	float x = 0.0f;
	float y = 0.0f;
	float sensitivity = 0.0f;
	float rate = 0.0f;
	float accelerationBase = 0.0f;
	float accelerationExponent = 0.0f;
	bool cpiEnabled = false;
};

[[nodiscard]] inline int RoundAwayFromZero( float value ) noexcept
{
	if ( !std::isfinite( value ) ) {
		return 0;
	}

	const double expanded = value;
	if ( expanded >= static_cast<double>( ( std::numeric_limits<int>::max )() ) - 0.5 ) {
		return ( std::numeric_limits<int>::max )();
	}
	if ( expanded <= static_cast<double>( ( std::numeric_limits<int>::min )() ) + 0.5 ) {
		return ( std::numeric_limits<int>::min )();
	}
	return expanded < 0.0 ? static_cast<int>( expanded - 0.5 )
	                      : static_cast<int>( expanded + 0.5 );
}

/*
Retail Quake Live makes CPI opt-in. In that mode raw counts are converted to
centimetres before acceleration, while view-axis application later restores
the retail degrees-per-count convention with kRetailCpiAxisMultiplier.
*/
constexpr float kCentimetresPerInch = 2.54f;
constexpr float kRetailCpiRateMultiplier = 1000.0f;
constexpr float kRetailCpiAxisMultiplier = 45.45454545454546f;

[[nodiscard]] inline bool RetailCpiEnabled( float countsPerInch ) noexcept
{
	return std::isfinite( countsPerInch ) && countsPerInch > 0.0f;
}

[[nodiscard]] inline float RetailMouseAxisMultiplier( float countsPerInch ) noexcept
{
	return RetailCpiEnabled( countsPerInch ) ? kRetailCpiAxisMultiplier : 1.0f;
}

[[nodiscard]] inline RetailMouseMotion TranslateRetailMouseMotion(
	float rawX, float rawY, const RetailMouseParameters& parameters ) noexcept
{
	RetailMouseMotion result;
	result.x = FiniteOr( rawX, 0.0f );
	result.y = FiniteOr( rawY, 0.0f );
	result.sensitivity = FiniteOr( parameters.sensitivity, 0.0f );
	result.cpiEnabled = RetailCpiEnabled( parameters.countsPerInch );

	if ( result.cpiEnabled ) {
		const float countScale = kCentimetresPerInch / parameters.countsPerInch;
		result.x *= countScale;
		result.y *= countScale;
	}
	result.sampleX = result.x;
	result.sampleY = result.y;

	const float acceleration = FiniteOr( parameters.acceleration, 0.0f );
	if ( acceleration != 0.0f ) {
		const int frameMilliseconds = ( std::max )( parameters.frameMilliseconds, 1 );
		result.rate = std::hypot( result.x, result.y ) /
			static_cast<float>( frameMilliseconds );
		if ( result.cpiEnabled ) {
			result.rate *= kRetailCpiRateMultiplier;
		}

		const float offset = ( std::max )( FiniteOr( parameters.accelerationOffset, 0.0f ), 0.0f );
		const float rateAboveOffset = result.rate - offset;
		if ( rateAboveOffset > 0.0f ) {
			result.accelerationBase = std::fabs( acceleration ) * rateAboveOffset;
			result.accelerationExponent = ( std::max )(
				FiniteOr( parameters.accelerationPower, 2.0f ) - 1.0f, 0.0f );

			const float gain = std::pow(
				result.accelerationBase, result.accelerationExponent );
			if ( std::isfinite( gain ) ) {
				result.sensitivity += std::copysign( gain, acceleration );
			}
		}

		const float cap = FiniteOr( parameters.sensitivityCap, 0.0f );
		if ( cap > 0.0f && result.sensitivity > cap ) {
			result.sensitivity = cap;
		}
	}

	if ( !std::isfinite( result.sensitivity ) ) {
		result.sensitivity = FiniteOr( parameters.sensitivity, 0.0f );
	}

	result.x *= result.sensitivity;
	result.y *= result.sensitivity;
	if ( !std::isfinite( result.x ) ) {
		result.x = 0.0f;
	}
	if ( !std::isfinite( result.y ) ) {
		result.y = 0.0f;
	}
	return result;
}

struct ViewAngles {
	float yaw = 0.0f;
	float pitch = 0.0f;
};

/*
QL's m_filter is a bounded moving average of completed view angles, not the
two-frame raw-delta average inherited by ioquake3. The unfiltered angle is
retained separately so each new input sample starts from the true view.
*/
class RetailViewAngleFilter {
public:
	[[nodiscard]] ViewAngles Begin( ViewAngles current, int requestedSamples ) noexcept
	{
		const int samples = std::clamp(
			requestedSamples, 0, kRetailMouseFilterMaximum );
		if ( samples <= 0 ) {
			Reset( current );
			return current;
		}

		if ( samples != sampleLimit_ || !active_ ) {
			Reset( current );
			active_ = true;
			sampleLimit_ = samples;
		} else {
			Synchronize( current );
		}
		return raw_;
	}

	[[nodiscard]] ViewAngles End( ViewAngles unfiltered ) noexcept
	{
		if ( !active_ || sampleLimit_ <= 0 ) {
			Reset( unfiltered );
			return unfiltered;
		}

		history_[next_] = unfiltered;
		count_ = ( std::min )( count_ + 1, static_cast<std::size_t>( sampleLimit_ ) );
		raw_ = unfiltered;

		double yaw = 0.0;
		double pitch = 0.0;
		std::size_t index = next_;
		for ( std::size_t i = 0; i < count_; ++i ) {
			yaw += static_cast<double>( history_[index].yaw );
			pitch += static_cast<double>( history_[index].pitch );
			index = index == 0 ? history_.size() - 1 : index - 1;
		}

		next_ = ( next_ + 1 ) % history_.size();
		const double divisor = static_cast<double>( count_ );
		visible_ = {
			static_cast<float>( yaw / divisor ),
			static_cast<float>( pitch / divisor )
		};
		if ( !std::isfinite( visible_.yaw ) ||
			!std::isfinite( visible_.pitch ) ) {
			Reset( unfiltered );
			return unfiltered;
		}
		return visible_;
	}

	/*
	The filter owns an unfiltered base while the client exposes the averaged
	view. Keyboard look, joystick look, centerview, cgame adjustments, and the
	per-command pitch guard can all change that exposed view between mouse
	samples. Translate the complete filter state by the same delta so those
	changes are retained instead of being overwritten on the next Begin().
	*/
	void Synchronize( ViewAngles current ) noexcept
	{
		if ( !active_ ) {
			raw_ = current;
			visible_ = current;
			return;
		}
		if ( current.yaw == visible_.yaw &&
			current.pitch == visible_.pitch ) {
			return;
		}

		const float yawDelta = current.yaw - visible_.yaw;
		const float pitchDelta = current.pitch - visible_.pitch;
		if ( !std::isfinite( yawDelta ) || !std::isfinite( pitchDelta ) ) {
			Reset( current );
			return;
		}

		const auto shift = [yawDelta, pitchDelta]( ViewAngles angle ) noexcept {
			return ViewAngles{
				angle.yaw + yawDelta,
				angle.pitch + pitchDelta
			};
		};
		const ViewAngles shiftedRaw = shift( raw_ );
		if ( !std::isfinite( shiftedRaw.yaw ) ||
			!std::isfinite( shiftedRaw.pitch ) ) {
			Reset( current );
			return;
		}
		for ( ViewAngles& angle : history_ ) {
			angle = shift( angle );
			if ( !std::isfinite( angle.yaw ) ||
				!std::isfinite( angle.pitch ) ) {
				Reset( current );
				return;
			}
		}

		raw_ = shiftedRaw;
		visible_ = current;
	}

	void Reset( ViewAngles current = {} ) noexcept
	{
		history_.fill( {} );
		next_ = 0;
		count_ = 0;
		sampleLimit_ = 0;
		active_ = false;
		raw_ = current;
		visible_ = current;
	}

private:
	std::array<ViewAngles, kRetailMouseFilterCapacity> history_{};
	std::size_t next_ = 0;
	std::size_t count_ = 0;
	int sampleLimit_ = 0;
	bool active_ = false;
	ViewAngles raw_{};
	ViewAngles visible_{};
};

[[nodiscard]] inline float NormaliseJoystickAxis( long value ) noexcept
{
	constexpr float centre = 32768.0f;
	return std::clamp( ( static_cast<float>( value ) - centre ) / centre, -1.0f, 1.0f );
}

[[nodiscard]] inline int RetailJoystickMoveAxis(
	float axis, float deadzone, float scale ) noexcept
{
	axis = std::clamp( FiniteOr( axis, 0.0f ), -1.0f, 1.0f );
	deadzone = std::clamp( FiniteOr( deadzone, 0.0f ), 0.0f, 1.0f );
	if ( std::fabs( axis ) <= deadzone ) {
		return 0;
	}

	const float movement = axis * FiniteOr( scale, 1.0f ) * 127.0f;
	return std::clamp( RoundAwayFromZero( movement ), -127, 127 );
}

[[nodiscard]] inline float FiniteJoystickDeadzone( float deadzone ) noexcept
{
	return std::clamp( FiniteOr( deadzone, 1.0f ), 0.0f, 1.0f );
}

/*
SDL gamepad axes smoothly ramp from the configured deadzone to full scale.
A threshold of one has no non-dead region and is therefore neutral, avoiding
the legacy division by zero. The -32768 endpoint remains representable.
*/
[[nodiscard]] inline int ApplyJoystickDeadzone(
	int axis, float deadzone ) noexcept
{
	axis = std::clamp( axis, -32768, 32767 );
	deadzone = FiniteJoystickDeadzone( deadzone );
	if ( axis == 0 || deadzone >= 1.0f ) {
		return 0;
	}

	const float magnitude =
		static_cast<float>( axis < 0 ? -axis : axis ) / 32767.0f;
	const float ramp =
		( std::max )( ( magnitude - deadzone ) / ( 1.0f - deadzone ), 0.0f );
	const float signedValue =
		32767.0f * ( axis < 0 ? -ramp : ramp );
	return std::clamp(
		TruncateFiniteFloatToInt( signedValue ), -32768, 32767 );
}

[[nodiscard]] constexpr int StrongerJoystickAxis(
	int current, int candidate ) noexcept
{
	const std::int64_t currentMagnitude = current < 0
		? -static_cast<std::int64_t>( current )
		: static_cast<std::int64_t>( current );
	const std::int64_t candidateMagnitude = candidate < 0
		? -static_cast<std::int64_t>( candidate )
		: static_cast<std::int64_t>( candidate );
	return candidateMagnitude > currentMagnitude ? candidate : current;
}

[[nodiscard]] inline int RetailJoystickLookDelta(
	float axis, float deadzone, float sensitivity, float exponent, bool invert ) noexcept
{
	axis = std::clamp( FiniteOr( axis, 0.0f ), -1.0f, 1.0f );
	deadzone = std::clamp( FiniteOr( deadzone, 0.0f ), 0.0f, 1.0f );
	if ( std::fabs( axis ) <= deadzone ) {
		return 0;
	}

	const int linear = RoundAwayFromZero(
		axis * FiniteOr( sensitivity, 0.0f ) );
	if ( linear == 0 ) {
		return 0;
	}

	exponent = ( std::max )( FiniteOr( exponent, 1.0f ), 0.0f );
	// Converting to float before taking the magnitude also covers INT_MIN,
	// whose positive counterpart is not representable as an int.
	float accelerated = std::pow(
		std::fabs( static_cast<float>( linear ) ), exponent );
	if ( !std::isfinite( accelerated ) ) {
		return 0;
	}
	accelerated = ( std::min )( accelerated, 32767.0f );
	if ( linear < 0 ) {
		accelerated = -accelerated;
	}
	if ( invert ) {
		accelerated = -accelerated;
	}
	return RoundAwayFromZero( accelerated );
}

struct Utf8Codepoint {
	std::array<unsigned char, 4> bytes{};
	std::size_t size = 0;
};

struct Utf8DecodeResult {
	std::uint32_t codepoint = 0;
	std::size_t size = 0;
	bool valid = false;
};

[[nodiscard]] constexpr bool IsUtf8ContinuationByte( unsigned char value ) noexcept
{
	return ( value & 0xc0u ) == 0x80u;
}

/*
Decode one strictly formed UTF-8 scalar. Malformed input consumes one byte so
callers can always make progress and decide whether to ignore or replace it.
*/
[[nodiscard]] constexpr Utf8DecodeResult DecodeUtf8(
	const unsigned char *bytes, std::size_t available ) noexcept
{
	Utf8DecodeResult decoded;
	if ( !bytes || available == 0 ) {
		return decoded;
	}

	const unsigned char lead = bytes[0];
	decoded.size = 1;
	if ( lead <= 0x7fu ) {
		decoded.codepoint = lead;
		decoded.valid = true;
		return decoded;
	}

	if ( lead >= 0xc2u && lead <= 0xdfu ) {
		if ( available < 2 || !IsUtf8ContinuationByte( bytes[1] ) ) {
			return decoded;
		}
		decoded.codepoint =
			( static_cast<std::uint32_t>( lead & 0x1fu ) << 6 ) |
			static_cast<std::uint32_t>( bytes[1] & 0x3fu );
		decoded.size = 2;
		decoded.valid = true;
		return decoded;
	}

	if ( lead >= 0xe0u && lead <= 0xefu ) {
		if ( available < 3 || !IsUtf8ContinuationByte( bytes[1] ) ||
			!IsUtf8ContinuationByte( bytes[2] ) ) {
			return decoded;
		}
		if ( ( lead == 0xe0u && bytes[1] < 0xa0u ) ||
			( lead == 0xedu && bytes[1] > 0x9fu ) ) {
			return decoded; // overlong form or UTF-16 surrogate
		}
		decoded.codepoint =
			( static_cast<std::uint32_t>( lead & 0x0fu ) << 12 ) |
			( static_cast<std::uint32_t>( bytes[1] & 0x3fu ) << 6 ) |
			static_cast<std::uint32_t>( bytes[2] & 0x3fu );
		decoded.size = 3;
		decoded.valid = true;
		return decoded;
	}

	if ( lead >= 0xf0u && lead <= 0xf4u ) {
		if ( available < 4 || !IsUtf8ContinuationByte( bytes[1] ) ||
			!IsUtf8ContinuationByte( bytes[2] ) ||
			!IsUtf8ContinuationByte( bytes[3] ) ) {
			return decoded;
		}
		if ( ( lead == 0xf0u && bytes[1] < 0x90u ) ||
			( lead == 0xf4u && bytes[1] > 0x8fu ) ) {
			return decoded; // overlong form or above U+10FFFF
		}
		decoded.codepoint =
			( static_cast<std::uint32_t>( lead & 0x07u ) << 18 ) |
			( static_cast<std::uint32_t>( bytes[1] & 0x3fu ) << 12 ) |
			( static_cast<std::uint32_t>( bytes[2] & 0x3fu ) << 6 ) |
			static_cast<std::uint32_t>( bytes[3] & 0x3fu );
		decoded.size = 4;
		decoded.valid = true;
	}
	return decoded;
}

[[nodiscard]] inline Utf8Codepoint EncodeUtf8( std::uint32_t codepoint ) noexcept
{
	Utf8Codepoint encoded;
	if ( codepoint > 0x10ffffu || ( codepoint >= 0xd800u && codepoint <= 0xdfffu ) ) {
		return encoded;
	}

	if ( codepoint <= 0x7fu ) {
		encoded.bytes[0] = static_cast<unsigned char>( codepoint );
		encoded.size = 1;
	} else if ( codepoint <= 0x7ffu ) {
		encoded.bytes[0] = static_cast<unsigned char>( 0xc0u | ( codepoint >> 6 ) );
		encoded.bytes[1] = static_cast<unsigned char>( 0x80u | ( codepoint & 0x3fu ) );
		encoded.size = 2;
	} else if ( codepoint <= 0xffffu ) {
		encoded.bytes[0] = static_cast<unsigned char>( 0xe0u | ( codepoint >> 12 ) );
		encoded.bytes[1] = static_cast<unsigned char>( 0x80u | ( ( codepoint >> 6 ) & 0x3fu ) );
		encoded.bytes[2] = static_cast<unsigned char>( 0x80u | ( codepoint & 0x3fu ) );
		encoded.size = 3;
	} else {
		encoded.bytes[0] = static_cast<unsigned char>( 0xf0u | ( codepoint >> 18 ) );
		encoded.bytes[1] = static_cast<unsigned char>( 0x80u | ( ( codepoint >> 12 ) & 0x3fu ) );
		encoded.bytes[2] = static_cast<unsigned char>( 0x80u | ( ( codepoint >> 6 ) & 0x3fu ) );
		encoded.bytes[3] = static_cast<unsigned char>( 0x80u | ( codepoint & 0x3fu ) );
		encoded.size = 4;
	}
	return encoded;
}

class Utf16Decoder {
public:
	[[nodiscard]] std::optional<std::uint32_t> Consume( std::uint32_t value ) noexcept
	{
		if ( value >= 0xd800u && value <= 0xdbffu ) {
			pendingHighSurrogate_ = value;
			return std::nullopt;
		}

		if ( value >= 0xdc00u && value <= 0xdfffu ) {
			if ( pendingHighSurrogate_ == 0 ) {
				return std::nullopt;
			}
			const std::uint32_t codepoint = 0x10000u +
				( ( pendingHighSurrogate_ - 0xd800u ) << 10 ) +
				( value - 0xdc00u );
			pendingHighSurrogate_ = 0;
			return codepoint;
		}

		pendingHighSurrogate_ = 0;
		if ( value > 0x10ffffu ) {
			return std::nullopt;
		}
		return value;
	}

	void Reset() noexcept
	{
		pendingHighSurrogate_ = 0;
	}

private:
	std::uint32_t pendingHighSurrogate_ = 0;
};

} // namespace fnql::input

#endif // FNQL_INPUT_COMPAT_HPP
