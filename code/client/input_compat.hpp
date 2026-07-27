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

[[nodiscard]] inline PointerPosition ProjectPointerToDrawable(
	int x, int y, const PointerProjection& projection ) noexcept
{
	PointerPosition projected{ x, y };

	if ( projection.hostWidth > 0 && projection.drawableWidth > 0 ) {
		projected.x = static_cast<int>( static_cast<float>( x ) *
			( static_cast<float>( projection.drawableWidth ) /
				static_cast<float>( projection.hostWidth ) ) );
	}
	if ( projection.hostHeight > 0 && projection.drawableHeight > 0 ) {
		projected.y = static_cast<int>( static_cast<float>( y ) *
			( static_cast<float>( projection.drawableHeight ) /
				static_cast<float>( projection.hostHeight ) ) );
	}

	return projected;
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

[[nodiscard]] inline float FiniteOr( float value, float fallback ) noexcept
{
	return std::isfinite( value ) ? value : fallback;
}

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

		ViewAngles sum{};
		std::size_t index = next_;
		for ( std::size_t i = 0; i < count_; ++i ) {
			sum.yaw += history_[index].yaw;
			sum.pitch += history_[index].pitch;
			index = index == 0 ? history_.size() - 1 : index - 1;
		}

		next_ = ( next_ + 1 ) % history_.size();
		const float divisor = static_cast<float>( count_ );
		return { sum.yaw / divisor, sum.pitch / divisor };
	}

	void Reset( ViewAngles current = {} ) noexcept
	{
		history_.fill( {} );
		next_ = 0;
		count_ = 0;
		sampleLimit_ = 0;
		active_ = false;
		raw_ = current;
	}

private:
	std::array<ViewAngles, kRetailMouseFilterCapacity> history_{};
	std::size_t next_ = 0;
	std::size_t count_ = 0;
	int sampleLimit_ = 0;
	bool active_ = false;
	ViewAngles raw_{};
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
	float accelerated = std::pow( static_cast<float>( std::abs( linear ) ), exponent );
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
