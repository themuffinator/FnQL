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

#include <stdbool.h>

typedef struct SDL_Window SDL_Window;

#ifdef __cplusplus
extern "C" {
#endif

// SDL 3.4 does not expose Cocoa frame insets through
// SDL_GetWindowBordersSize. Query the live NSWindow so placement follows the
// current style and display scale without hard-coded title-bar dimensions.
bool FNQL_MacGetWindowBordersSize( SDL_Window *window, int *top, int *left,
	int *bottom, int *right );

#ifdef __cplusplus
}
#endif

