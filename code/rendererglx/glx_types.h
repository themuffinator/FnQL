/*
===========================================================================
Copyright (C) 2026

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2 of the License, or (at your option)
any later version.
===========================================================================
*/

#ifndef GLX_TYPES_H
#define GLX_TYPES_H

#include "../qcommon/q_shared.h"

namespace glx {

enum class RenderProductTier {
	GL12,
	GL2X,
	GL3X,
	GL41,
	GL46
};

enum class CapabilityHint {
	FixedFunction,
	Programmable,
	Modern,
	HighEnd
};

enum class StreamStrategy {
	OrphanSubData,
	MapBufferRange,
	PersistentMapped
};

struct FeatureSet {
	qboolean mapBufferRange;
	qboolean uniformBufferObject;
	qboolean instancedArrays;
	qboolean bufferStorage;
	qboolean syncObjects;
	qboolean drawIndirect;
	qboolean multiDrawIndirect;
	qboolean directStateAccess;
	qboolean debugContext;
	qboolean debugOutput;
	qboolean khrDebug;
	qboolean timerQuery;
};

static ID_INLINE const char *GLX_RenderProductTierName( RenderProductTier tier )
{
	switch ( tier ) {
	case RenderProductTier::GL12:
		return "GL12";
	case RenderProductTier::GL2X:
		return "GL2X";
	case RenderProductTier::GL3X:
		return "GL3X";
	case RenderProductTier::GL41:
		return "GL41";
	case RenderProductTier::GL46:
		return "GL46";
	default:
		return "unknown";
	}
}

static ID_INLINE const char *GLX_CapabilityHintName( CapabilityHint hint )
{
	switch ( hint ) {
	case CapabilityHint::FixedFunction:
		return "fixed-function";
	case CapabilityHint::Programmable:
		return "programmable";
	case CapabilityHint::Modern:
		return "modern";
	case CapabilityHint::HighEnd:
		return "high-end";
	default:
		return "unknown";
	}
}

static ID_INLINE RenderProductTier GLX_RenderProductTierForVersionAndFeatures( int major,
	int minor, const FeatureSet &features )
{
	(void)features;

	if ( major > 4 || ( major == 4 && minor >= 6 ) ) {
		return RenderProductTier::GL46;
	}
	if ( major == 4 && minor >= 1 ) {
		return RenderProductTier::GL41;
	}
	if ( major >= 3 ) {
		return RenderProductTier::GL3X;
	}
	if ( major >= 2 ) {
		return RenderProductTier::GL2X;
	}
	return RenderProductTier::GL12;
}

static ID_INLINE CapabilityHint GLX_CapabilityHintForTierAndFeatures( RenderProductTier tier,
	const FeatureSet &features )
{
	switch ( tier ) {
	case RenderProductTier::GL46:
		if ( features.bufferStorage && features.syncObjects && features.multiDrawIndirect ) {
			return CapabilityHint::HighEnd;
		}
		return CapabilityHint::Modern;
	case RenderProductTier::GL41:
	case RenderProductTier::GL3X:
		return CapabilityHint::Modern;
	case RenderProductTier::GL2X:
		if ( features.mapBufferRange && features.uniformBufferObject && features.instancedArrays ) {
			return CapabilityHint::Modern;
		}
		return CapabilityHint::Programmable;
	case RenderProductTier::GL12:
	default:
		return CapabilityHint::FixedFunction;
	}
}

} // namespace glx

#endif // GLX_TYPES_H
