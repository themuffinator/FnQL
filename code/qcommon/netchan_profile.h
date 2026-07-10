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

#ifndef FNQL_NETCHAN_PROFILE_H
#define FNQL_NETCHAN_PROFILE_H

/*
 * Netchan framing is not implied by a protocol number's age.  In particular,
 * retail Quake Live protocol 91 kept the Quake III reliable XOR codec and did
 * not adopt ioquake3 protocol 71's extra sequence checksum.  Keep those wire
 * choices explicit so legacy Quake III, existing FnQ3/ioq3, and retail QL
 * connections can coexist without overloading the broader "compat" policy.
 */
#define NETCHAN_QL_RETAIL_PROTOCOL_VERSION 91

typedef enum {
	NETCHAN_WIRE_LEGACY_Q3,
	NETCHAN_WIRE_IOQ3,
	NETCHAN_WIRE_QL_RETAIL
} netchanWireProfile_t;

typedef enum {
	NETCHAN_FEATURE_CLIENT_QPORT = 1u << 0,
	NETCHAN_FEATURE_SEQUENCE_CHECKSUM = 1u << 1,
	NETCHAN_FEATURE_RELIABLE_XOR = 1u << 2
} netchanWireFeature_t;

#ifdef __cplusplus
#define NETCHAN_PROFILE_INLINE inline constexpr
#else
#define NETCHAN_PROFILE_INLINE static inline
#endif

NETCHAN_PROFILE_INLINE netchanWireProfile_t Netchan_SelectWireProfile(
	int protocol, int legacyCompat ) {
	if ( protocol == NETCHAN_QL_RETAIL_PROTOCOL_VERSION ) {
		return NETCHAN_WIRE_QL_RETAIL;
	}

	return legacyCompat ? NETCHAN_WIRE_LEGACY_Q3 : NETCHAN_WIRE_IOQ3;
}

NETCHAN_PROFILE_INLINE unsigned int Netchan_WireFeatures(
	netchanWireProfile_t profile ) {
	switch ( profile ) {
	case NETCHAN_WIRE_IOQ3:
		return NETCHAN_FEATURE_CLIENT_QPORT |
			NETCHAN_FEATURE_SEQUENCE_CHECKSUM;
	case NETCHAN_WIRE_QL_RETAIL:
	case NETCHAN_WIRE_LEGACY_Q3:
	default:
		return NETCHAN_FEATURE_CLIENT_QPORT |
			NETCHAN_FEATURE_RELIABLE_XOR;
	}
}

NETCHAN_PROFILE_INLINE int Netchan_WireHasFeature(
	netchanWireProfile_t profile, netchanWireFeature_t feature ) {
	return ( Netchan_WireFeatures( profile ) & (unsigned int)feature ) != 0u;
}

#undef NETCHAN_PROFILE_INLINE

#endif
