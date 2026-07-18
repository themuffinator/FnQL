/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/

#ifndef TR_QL_AD_DEBUG_H
#define TR_QL_AD_DEBUG_H

/* Include this after tr_local.h so qlAdvertisement_t and tr are available. */
static ID_INLINE void R_QLDebugAdvertisements( void )
{
	static int reportedModificationCount = -1;
	static char reportedWorldName[MAX_QPATH];
	char label[256];
	int count;
	int displayState;
	int i;

	if ( !qlRendererCvars.debugAds || !qlRendererCvars.debugAds->integer
		|| !tr.world || tr.world->numAdvertisements <= 0
		|| tr.viewParms.frameSceneNum != 1
		|| ( reportedModificationCount == qlRendererCvars.debugAds->modificationCount
			&& !Q_stricmp( reportedWorldName, tr.world->name ) ) ) {
		return;
	}

	reportedModificationCount = qlRendererCvars.debugAds->modificationCount;
	Q_strncpyz( reportedWorldName, tr.world->name, sizeof( reportedWorldName ) );
	ri.Printf( PRINT_ALL, "r_debugAds: world=%s loaded=%d\n",
		tr.world->name, tr.world->numAdvertisements );

	for ( i = 0; i < tr.world->numAdvertisements; ++i ) {
		const qlAdvertisement_t *advertisement = &tr.world->advertisements[i];

		displayState = ri.AdvertisementBridge_GetCellDisplayState
			? ri.AdvertisementBridge_GetCellDisplayState( advertisement->cellId ) : 0;
		label[0] = '\0';
		if ( ri.AdvertisementBridge_GetCellLabel ) {
			ri.AdvertisementBridge_GetCellLabel( advertisement->cellId,
				label, sizeof( label ) );
			label[sizeof( label ) - 1] = '\0';
		}

		ri.Printf( PRINT_ALL,
			"r_debugAds: [%d] cell=%d source=%d display=%d cull=%d area=%d "
			"projectedNormal=(%.3f %.3f) label=%s\n",
			i, advertisement->cellId, advertisement->sourceIndex, displayState,
			advertisement->cullState, advertisement->viewArea,
			advertisement->projectedNormalX, advertisement->projectedNormalY,
			label[0] ? label : "<none>" );
	}

	if ( ri.AdvertisementBridge_GetLabelList1Count
		&& ri.AdvertisementBridge_GetLabelList1Entry ) {
		count = Com_Clamp( 0, 128, ri.AdvertisementBridge_GetLabelList1Count() );
		for ( i = 0; i < count; ++i ) {
			label[0] = '\0';
			ri.AdvertisementBridge_GetLabelList1Entry( i, label, sizeof( label ) );
			label[sizeof( label ) - 1] = '\0';
			ri.Printf( PRINT_ALL, "r_debugAds: list1[%d]=%s\n",
				i, label[0] ? label : "<none>" );
		}
	}

	if ( ri.AdvertisementBridge_GetLabelList2Count
		&& ri.AdvertisementBridge_GetLabelList2Entry ) {
		count = Com_Clamp( 0, 128, ri.AdvertisementBridge_GetLabelList2Count() );
		for ( i = 0; i < count; ++i ) {
			label[0] = '\0';
			ri.AdvertisementBridge_GetLabelList2Entry( i, label, sizeof( label ) );
			label[sizeof( label ) - 1] = '\0';
			ri.Printf( PRINT_ALL, "r_debugAds: list2[%d]=%s\n",
				i, label[0] ? label : "<none>" );
		}
	}
}

#endif /* TR_QL_AD_DEBUG_H */
