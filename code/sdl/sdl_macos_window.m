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

#import <AppKit/AppKit.h>

#include <SDL3/SDL.h>

#include <limits.h>
#include <math.h>

#include "sdl_macos_window.h"

static int FNQL_MacInset( CGFloat value )
{
	if ( !isfinite( value ) || value <= 0.0 ) {
		return 0;
	}
	if ( value >= (CGFloat)INT_MAX ) {
		return INT_MAX;
	}
	// A fractional point still occupies part of the outer frame. Rounding it
	// down could leave that edge just outside the usable screen rectangle.
	return (int)ceil( value );
}

bool FNQL_MacGetWindowBordersSize( SDL_Window *window, int *top, int *left,
	int *bottom, int *right )
{
	if ( !window || !top || !left || !bottom || !right ) {
		return false;
	}

	@autoreleasepool {
		const SDL_PropertiesID properties = SDL_GetWindowProperties( window );
		NSWindow *nativeWindow = properties
			? (NSWindow *)SDL_GetPointerProperty( properties,
				SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL )
			: nil;
		if ( !nativeWindow ) {
			return false;
		}

		const NSRect frame = [nativeWindow frame];
		const NSRect content = [nativeWindow contentRectForFrameRect:frame];
		*left = FNQL_MacInset( NSMinX( content ) - NSMinX( frame ) );
		*top = FNQL_MacInset( NSMaxY( frame ) - NSMaxY( content ) );
		*right = FNQL_MacInset( NSMaxX( frame ) - NSMaxX( content ) );
		*bottom = FNQL_MacInset( NSMinY( content ) - NSMinY( frame ) );
		return true;
	}
}

