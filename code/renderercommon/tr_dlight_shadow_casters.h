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

#ifndef TR_DLIGHT_SHADOW_CASTERS_H
#define TR_DLIGHT_SHADOW_CASTERS_H

#ifdef USE_PMLIGHT

qboolean R_DlightShadowFrontendCasterAllowed( const shader_t *shader,
	const surfaceType_t *surface )
{
	int i;

	if ( !shader || !surface ) {
		return qfalse;
	}
	if ( shader->remappedShader ) {
		shader = shader->remappedShader;
	}

	if ( shader->sort != SS_OPAQUE || shader->isSky || shader->polygonOffset ||
		shader->numDeforms > 0 ||
		( shader->surfaceFlags & ( SURF_SKY | SURF_NODLIGHT ) ) ) {
		return qfalse;
	}

	// The point-shadow depth pass does not evaluate material alpha. Treating an
	// alpha-tested grate as solid is worse than omitting its unsupported shadow.
	for ( i = 0; i < shader->numUnfoggedPasses; i++ ) {
		const shaderStage_t *stage = shader->stages[i];
		if ( stage && ( stage->stateBits & GLS_ATEST_BITS ) ) {
			return qfalse;
		}
	}

	switch ( *surface ) {
		case SF_FACE:
		case SF_TRIANGLES:
		case SF_MD3:
		case SF_MDR:
		case SF_IQM:
			return qtrue;
		case SF_GRID:
			return ( !r_nocurves || !r_nocurves->integer ) ? qtrue : qfalse;
		default:
			return qfalse;
	}
}

static qboolean R_DlightShadowEntityTransformValid( const trRefEntity_t *ent )
{
	int axis;
	int component;

	if ( !ent ) {
		return qfalse;
	}

	for ( component = 0; component < 3; component++ ) {
		if ( !R_DlightShadowFloatIsFinite( ent->e.origin[component] ) ) {
			return qfalse;
		}
	}
	for ( axis = 0; axis < 3; axis++ ) {
		for ( component = 0; component < 3; component++ ) {
			if ( !R_DlightShadowFloatIsFinite(
				ent->e.axis[axis][component] ) ) {
				return qfalse;
			}
		}
	}

	return qtrue;
}

qboolean R_AddDlightShadowEntityCasterSurface(
	dlightShadowCasterContext_t *context, surfaceType_t *surface,
	shader_t *shader )
{
	int previousCount;

	if ( !context || context->overflow ) {
		return qfalse;
	}
	if ( !context->targetLight ) {
		context->overflow = qtrue;
		return qfalse;
	}
	if ( !R_DlightShadowFrontendCasterAllowed( shader, surface ) ) {
		return qtrue;
	}

	previousCount = tr.refdef.numLitSurfs;
	tr.light = context->targetLight;
	R_AddLitSurfFlags( surface, shader, 0, LSF_SHADOW_CASTER_ONLY );
	if ( tr.refdef.numLitSurfs == previousCount ) {
		context->overflow = qtrue;
		return qfalse;
	}

	return qtrue;
}

qboolean R_DlightShadowEntityBoundsCulled(
	const dlightShadowCasterContext_t *context, const trRefEntity_t *ent,
	const vec3_t mins, const vec3_t maxs )
{
	vec3_t center;
	vec3_t localCorner;
	vec3_t worldCenter;
	vec3_t worldCorner;
	vec3_t delta;
	float modelRadiusSquared;
	float distanceSquared;
	float influence;
	int corner;
	int i;

	if ( !context || !ent ||
		!R_DlightShadowFloatIsFinite( context->cullLight.radius ) ||
		context->cullLight.radius <= 0.0f ) {
		return qtrue;
	}
	if ( !R_DlightShadowEntityTransformValid( ent ) ) {
		return qtrue;
	}

	for ( i = 0; i < 3; i++ ) {
		if ( !R_DlightShadowFloatIsFinite( mins[i] ) ||
			!R_DlightShadowFloatIsFinite( maxs[i] ) ||
			mins[i] > maxs[i] ||
			!R_DlightShadowFloatIsFinite( context->cullLight.origin[i] ) ) {
			return qtrue;
		}
		center[i] = 0.5f * mins[i] + 0.5f * maxs[i];
		if ( !R_DlightShadowFloatIsFinite( center[i] ) ) {
			return qtrue;
		}
	}

	VectorCopy( ent->e.origin, worldCenter );
	VectorMA( worldCenter, center[0], ent->e.axis[0], worldCenter );
	VectorMA( worldCenter, center[1], ent->e.axis[1], worldCenter );
	VectorMA( worldCenter, center[2], ent->e.axis[2], worldCenter );
	for ( i = 0; i < 3; i++ ) {
		if ( !R_DlightShadowFloatIsFinite( worldCenter[i] ) ) {
			return qtrue;
		}
	}

	// Derive a conservative world-space sphere from all eight transformed box
	// corners. This remains correct for non-normalized entity axes, unlike a
	// local radius used directly as a world-space distance.
	modelRadiusSquared = 0.0f;
	for ( corner = 0; corner < 8; corner++ ) {
		localCorner[0] = ( corner & 1 ) ? maxs[0] : mins[0];
		localCorner[1] = ( corner & 2 ) ? maxs[1] : mins[1];
		localCorner[2] = ( corner & 4 ) ? maxs[2] : mins[2];
		VectorCopy( ent->e.origin, worldCorner );
		VectorMA( worldCorner, localCorner[0], ent->e.axis[0], worldCorner );
		VectorMA( worldCorner, localCorner[1], ent->e.axis[1], worldCorner );
		VectorMA( worldCorner, localCorner[2], ent->e.axis[2], worldCorner );
		for ( i = 0; i < 3; i++ ) {
			if ( !R_DlightShadowFloatIsFinite( worldCorner[i] ) ) {
				return qtrue;
			}
		}
		VectorSubtract( worldCorner, worldCenter, delta );
		modelRadiusSquared = MAX( modelRadiusSquared,
			DotProduct( delta, delta ) );
		if ( !R_DlightShadowFloatIsFinite( modelRadiusSquared ) ) {
			return qtrue;
		}
	}

	VectorSubtract( context->cullLight.origin, worldCenter, delta );
	distanceSquared = DotProduct( delta, delta );
	influence = context->cullLight.radius + sqrtf( modelRadiusSquared );
	if ( !R_DlightShadowFloatIsFinite( distanceSquared ) ||
		!R_DlightShadowFloatIsFinite( influence ) ||
		!R_DlightShadowFloatIsFinite( influence * influence ) ) {
		return qtrue;
	}
	return distanceSquared > influence * influence ? qtrue : qfalse;
}

static qboolean R_DlightShadowWorldCasterCandidate( const msurface_t *surf )
{
	if ( !surf || !surf->data ||
		!R_DlightShadowFrontendCasterAllowed( surf->shader, surf->data ) ) {
		return qfalse;
	}

	switch ( *surf->data ) {
		case SF_FACE:
		case SF_GRID:
		case SF_TRIANGLES:
			return qtrue;
		default:
			return qfalse;
	}
}

static qboolean R_AddDlightShadowWorldCaster( msurface_t *surf,
	const dlight_t *shadowLight, int dlightIndex )
{
	uint64_t lightBit;
	int previousCount;

	if ( dlightIndex < 0 || dlightIndex >= MAX_REAL_DLIGHTS ||
		dlightIndex >= (int)( sizeof( lightBit ) * 8 ) ) {
		return qfalse;
	}

	lightBit = 1ULL << dlightIndex;
	if ( surf->pmShadowFrame != tr.viewCount ) {
		surf->pmShadowFrame = tr.viewCount;
		surf->pmShadowMask = 0;
	}
	if ( surf->pmShadowMask & lightBit ) {
		return qtrue;
	}

	if ( !R_DlightShadowWorldCasterCandidate( surf ) ||
		R_LightCullSurface( surf->data, shadowLight ) ) {
		surf->pmShadowMask |= lightBit;
		return qtrue;
	}

	previousCount = tr.refdef.numLitSurfs;
	R_AddLitSurfFlags( surf->data, surf->shader, surf->fogIndex,
		LSF_SHADOW_CASTER_ONLY );
	if ( tr.refdef.numLitSurfs == previousCount ) {
		return qfalse;
	}

	surf->pmShadowMask |= lightBit;
	return qtrue;
}

static qboolean R_CollectDlightShadowWorldCasters_r( const mnode_t *node,
	const dlight_t *shadowLight, int dlightIndex )
{
	float distance;

	while ( node && (unsigned int)node->contents == CONTENTS_NODE ) {
		distance = DotProduct( shadowLight->origin, node->plane->normal ) -
			node->plane->dist;
		if ( distance > shadowLight->radius ) {
			node = node->children[0];
			continue;
		}
		if ( distance < -shadowLight->radius ) {
			node = node->children[1];
			continue;
		}

		if ( !R_CollectDlightShadowWorldCasters_r( node->children[0],
			shadowLight, dlightIndex ) ) {
			return qfalse;
		}
		node = node->children[1];
	}

	if ( node ) {
		msurface_t **mark = node->firstmarksurface;
		int count = node->nummarksurfaces;

		while ( count-- > 0 ) {
			if ( !R_AddDlightShadowWorldCaster( *mark, shadowLight,
				dlightIndex ) ) {
				return qfalse;
			}
			mark++;
		}
	}

	return qtrue;
}

static void R_DiscardDlightShadowPlansFrom( int firstPlan )
{
	shadowManager_t *manager = &tr.shadowManager;
	int discarded;
	int i;

	if ( firstPlan < 0 || firstPlan >= manager->pointPlanCount ) {
		return;
	}

	discarded = manager->pointPlanCount - firstPlan;
	for ( i = firstPlan; i < manager->pointPlanCount; i++ ) {
		const int dlightIndex = manager->pointPlans[i].dlightIndex;
		dlight_t *dl;

		if ( dlightIndex < 0 ||
			(unsigned int)dlightIndex >= tr.viewParms.num_dlights ) {
			continue;
		}
		dl = &tr.viewParms.dlights[dlightIndex];
		dl->shadowPlanned = qfalse;
		dl->shadowIndex = -1;
		dl->shadowAtlasBaseFace = -1;
		dl->shadowAtlasFaceSize = 0;
		Com_Memset( dl->shadowAtlasX, 0, sizeof( dl->shadowAtlasX ) );
		Com_Memset( dl->shadowAtlasY, 0, sizeof( dl->shadowAtlasY ) );
	}

	manager->pointPlanCount = firstPlan;
	tr.pc.c_dlightShadowPlanned =
		MAX( 0, tr.pc.c_dlightShadowPlanned - discarded );
	tr.pc.c_dlightShadowSkippedCasterOverflow += discarded;

	if ( firstPlan <= 0 || !manager->pointAtlasReady ) {
		tr.pc.c_dlightShadowAtlasFill = 0;
	} else {
		const int64_t usedPixels = (int64_t)firstPlan * DLIGHT_SHADOW_FACES *
			manager->pointAtlasLayout.faceSize * manager->pointAtlasLayout.faceSize;
		const int64_t atlasPixels = (int64_t)manager->pointAtlasLayout.width *
			manager->pointAtlasLayout.height;
		tr.pc.c_dlightShadowAtlasFill = atlasPixels > 0 ?
			Com_Clamp( 1, 100, (int)( usedPixels * 100 / atlasPixels ) ) : 0;
	}
}

static qboolean R_CollectDlightShadowEntityCasters(
	dlightShadowCasterContext_t *context )
{
	trRefEntity_t *savedEntity;
	model_t *savedModel;
	dlight_t *savedLight;
	orientationr_t savedOrientation;
	int savedEntityNum;
	int savedShiftedEntityNum;
	int entityNum;
	qboolean success;

	if ( !context || !context->targetLight || !r_drawentities->integer ) {
		return qtrue;
	}

	savedEntity = tr.currentEntity;
	savedModel = tr.currentModel;
	savedLight = tr.light;
	savedOrientation = tr.or;
	savedEntityNum = tr.currentEntityNum;
	savedShiftedEntityNum = tr.shiftedEntityNum;
	success = qtrue;

	for ( entityNum = 0; entityNum < tr.refdef.num_entities; entityNum++ ) {
		trRefEntity_t *ent = &tr.refdef.entities[entityNum];
		model_t *model;

		if ( ent->e.reType != RT_MODEL ||
			( ent->e.renderfx &
				( RF_NOSHADOW | RF_FIRST_PERSON | RF_DEPTHHACK ) ) ||
			!R_DlightShadowEntityTransformValid( ent ) ) {
			continue;
		}

		model = R_GetModelByHandle( ent->e.hModel );
		if ( !model ) {
			continue;
		}
		switch ( model->type ) {
			case MOD_MESH:
			case MOD_MDR:
			case MOD_IQM:
			case MOD_BRUSH:
				break;
			default:
				continue;
		}

		tr.currentEntityNum = entityNum;
		tr.shiftedEntityNum =
			entityNum << QSORT_REFENTITYNUM_SHIFT;
		tr.currentEntity = ent;
		tr.currentModel = model;
		tr.light = context->targetLight;
		R_RotateForEntity( ent, &tr.viewParms, &tr.or );

		switch ( model->type ) {
			case MOD_MESH:
				R_AddMD3Surfaces( ent, context );
				break;
			case MOD_MDR:
				R_MDRAddAnimSurfaces( ent, context );
				break;
			case MOD_IQM:
				R_AddIQMSurfaces( ent, context );
				break;
			case MOD_BRUSH:
				R_AddBrushModelSurfaces( ent, context );
				break;
			default:
				break;
		}

		if ( context->overflow ) {
			success = qfalse;
			break;
		}
	}

	tr.currentEntity = savedEntity;
	tr.currentModel = savedModel;
	tr.light = savedLight;
	tr.or = savedOrientation;
	tr.currentEntityNum = savedEntityNum;
	tr.shiftedEntityNum = savedShiftedEntityNum;
	return success;
}

void R_AddPlannedDlightShadowCasters( void )
{
	shadowManager_t *manager = &tr.shadowManager;
	dlight_t *savedLight;
	qboolean collectWorld;
	int savedEntityNum;
	int savedShiftedEntityNum;
	int i;

	if ( manager->pointPlanCount <= 0 ) {
		return;
	}

	collectWorld = ( tr.world && tr.world->nodes && r_drawworld->integer &&
		!( tr.refdef.rdflags & RDF_NOWORLDMODEL ) ) ? qtrue : qfalse;
	savedEntityNum = tr.currentEntityNum;
	savedShiftedEntityNum = tr.shiftedEntityNum;
	savedLight = tr.light;
	tr.currentEntityNum = REFENTITYNUM_WORLD;
	tr.shiftedEntityNum = REFENTITYNUM_WORLD << QSORT_REFENTITYNUM_SHIFT;

	for ( i = 0; i < manager->pointPlanCount; i++ ) {
		const shadowPointLightPlan_t *plan = &manager->pointPlans[i];
		struct litSurf_s *planStartHead;
		struct litSurf_s *planStartTail;
		dlightShadowCasterContext_t context;
		dlight_t shadowLight;
		dlight_t *dl;
		int planStartLitSurfCount;
		qboolean collected;

		if ( !plan->atlasAllocated ) {
			continue;
		}
		if ( plan->dlightIndex < 0 ||
			(unsigned int)plan->dlightIndex >= tr.viewParms.num_dlights ) {
			continue;
		}

		dl = &tr.viewParms.dlights[plan->dlightIndex];
		planStartHead = dl->head;
		planStartTail = dl->tail;
		planStartLitSurfCount = tr.refdef.numLitSurfs;
		shadowLight = *dl;
		shadowLight.radius = plan->projectionFar;
		VectorCopy( shadowLight.origin, shadowLight.transformed );
		tr.light = dl;
		context.targetLight = dl;
		context.cullLight = shadowLight;
		context.overflow = qfalse;

		collected = !collectWorld ||
			R_CollectDlightShadowWorldCasters_r( tr.world->nodes,
				&shadowLight, plan->dlightIndex );
		if ( collected ) {
			collected = R_CollectDlightShadowEntityCasters( &context );
		}
		if ( !collected ) {
			const int addedCount =
				tr.refdef.numLitSurfs - planStartLitSurfCount;

			// Collection is transactional for each plan. A partial caster list
			// must not consume the shared per-view lit-surface arena after the
			// plan and all later atlas slots are discarded.
			dl->head = planStartHead;
			dl->tail = planStartTail;
			if ( planStartTail ) {
				planStartTail->next = NULL;
			}
			tr.refdef.numLitSurfs = planStartLitSurfCount;
			tr.pc.c_lit_surfs = MAX( 0,
				tr.pc.c_lit_surfs - addedCount );
			R_DiscardDlightShadowPlansFrom( i );
			break;
		}
	}

	tr.currentEntityNum = savedEntityNum;
	tr.shiftedEntityNum = savedShiftedEntityNum;
	tr.light = savedLight;
}

#endif // USE_PMLIGHT

#endif // TR_DLIGHT_SHADOW_CASTERS_H
