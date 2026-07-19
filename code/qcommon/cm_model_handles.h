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

#ifndef FNQL_QCOMMON_CM_MODEL_HANDLES_H
#define FNQL_QCOMMON_CM_MODEL_HANDLES_H

#include "q_shared.h"

/*
 * Quake Live reserves the final two values in the 8-bit inline-model range
 * for the shared temporary hull.  The handle selects box/capsule trace math;
 * both handles intentionally resolve to the same bounds-owning cmodel_t.
 */
#define MAX_SUBMODELS            256
#define CAPSULE_MODEL_HANDLE     ( MAX_SUBMODELS - 2 )
#define BOX_MODEL_HANDLE         ( MAX_SUBMODELS - 1 )

static ID_INLINE qboolean CM_IsTemporaryModelHandle( clipHandle_t handle ) {
	return handle == BOX_MODEL_HANDLE || handle == CAPSULE_MODEL_HANDLE
		? qtrue : qfalse;
}

static ID_INLINE clipHandle_t CM_TemporaryModelHandle( qboolean capsule ) {
	return capsule != qfalse ? CAPSULE_MODEL_HANDLE : BOX_MODEL_HANDLE;
}

#endif
