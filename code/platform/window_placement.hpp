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

#pragma once

#include <cstdint>
#include <limits>

namespace fnql::window {

struct Position {
	int x;
	int y;
};

struct Bounds {
	int x;
	int y;
	int width;
	int height;
};

struct Insets {
	int left;
	int top;
	int right;
	int bottom;
};

constexpr std::int64_t NonNegative( int value ) {
	return value > 0 ? static_cast<std::int64_t>( value ) : 0;
}

constexpr int SaturateToInt( std::int64_t value ) {
	return value < ( std::numeric_limits<int>::min )()
		? ( std::numeric_limits<int>::min )()
		: value > ( std::numeric_limits<int>::max )()
			? ( std::numeric_limits<int>::max )()
			: static_cast<int>( value );
}

constexpr std::int64_t ConstrainAxis( std::int64_t desired,
	std::int64_t boundsOrigin, std::int64_t boundsExtent,
	std::int64_t contentExtent, std::int64_t leadingInset,
	std::int64_t trailingInset ) {
	const std::int64_t minimum = boundsOrigin + leadingInset;
	const std::int64_t maximum = boundsOrigin + boundsExtent
		- contentExtent - trailingInset;

	// An oversized window cannot fit completely. Keeping its leading frame on
	// screen is the recoverable choice: the title bar remains reachable.
	if ( maximum < minimum ) {
		return minimum;
	}
	if ( desired < minimum ) {
		return minimum;
	}
	if ( desired > maximum ) {
		return maximum;
	}
	return desired;
}

// `desired` is the client-area origin used by SDL. Insets describe the native
// decorations around that client area, so constraining it keeps the complete
// outer window inside the OS usable bounds whenever it fits.
constexpr Position ConstrainClientOrigin( Position desired, int clientWidth,
	int clientHeight, Bounds usable, Insets decorations = {} ) {
	if ( usable.width <= 0 || usable.height <= 0 ) {
		return desired;
	}

	return {
		SaturateToInt( ConstrainAxis( desired.x, usable.x, usable.width,
			NonNegative( clientWidth ), NonNegative( decorations.left ),
			NonNegative( decorations.right ) ) ),
		SaturateToInt( ConstrainAxis( desired.y, usable.y, usable.height,
			NonNegative( clientHeight ), NonNegative( decorations.top ),
			NonNegative( decorations.bottom ) ) )
	};
}

} // namespace fnql::window
