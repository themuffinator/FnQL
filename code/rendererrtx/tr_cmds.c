/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
#include "tr_local.h"

/*
=====================
R_PerformanceCounters
=====================
*/
static void R_PrintShadowCorrectnessDebug( const char *backendName )
{
	const shadowCorrectnessDebug_t *debug = &tr.shadowCorrectnessDebug;
	const char *depthMode;
	const char *depthNdc;
	const char *depthCompare;
	const char *apiOrigin;
	const char *sampleOrigin;
	const char *clipY;
	int i;

	if ( !r_shadowCorrectness || !r_shadowCorrectness->integer ) {
		return;
	}

	if ( !debug->active ) {
		ri.Printf( PRINT_ALL,
			"shadow correctness mode:1 backend:%s active:0\n",
			backendName );
		return;
	}

	depthMode = debug->reversedDepth ? "reversed" : "forward";
	depthNdc = debug->depthZeroToOne ? "0..1" : "-1..1";
	depthCompare = debug->reversedDepth ? "gequal" : "lequal";
	apiOrigin = debug->apiViewportTopLeft ? "top-left" : "lower-left";
	sampleOrigin = debug->samplerTopLeft ? "top-left" : "lower-left";
	clipY = debug->clipYFlipped ? "flipped" : "native";

	ri.Printf( PRINT_ALL,
		"shadow correctness mode:1 backend:%s active:1 view:%i frame:%i atlas:%ix%i/%i pub:%i gen:%u depth:%s ndc:%s clear:%.1f compare:%s view-origin:lower-left api-origin:%s sample-origin:%s clip-y:%s bias receiver:%.2f caster-depth:%.2f caster-slope:%.2f caster-normal:%.2f filter requested:%s effective:%s taps:%i offsets:%.2f/%.2f faces:%i\n",
		backendName, debug->viewCount, debug->frameCount,
		debug->atlasWidth, debug->atlasHeight, debug->atlasFaceSize,
		debug->atlasPublished, debug->atlasGeneration, depthMode, depthNdc,
		debug->clearDepth, depthCompare, apiOrigin, sampleOrigin, clipY,
		debug->receiverBias, debug->casterDepthBias, debug->casterSlopeBias,
		debug->casterNormalBias,
		R_ShadowFilterModeName( debug->requestedFilterMode ),
		R_ShadowFilterModeName( debug->effectiveFilterMode ),
		debug->filterSampleCount, debug->filterInnerOffset,
		debug->filterOuterOffset,
		debug->faceCount );

	for ( i = 0; i < debug->faceCount && i < SHADOW_CORRECTNESS_MAX_FACE_RECORDS; i++ ) {
		const shadowCorrectnessFaceDebug_t *record = &debug->faces[i];

		if ( !record->valid ) {
			continue;
		}

		ri.Printf( PRINT_ALL,
			"shadow correctness face light:%i shadow:%i face:%i slot:%i base:%i atlas:%i,%i/%i viewport:%i,%i %ix%i scissor:%i,%i %ix%i api-viewport:%i,%i %ix%i api-scissor:%i,%i %ix%i z:%.2f..%.2f cached:%i rendered:%i surfs:%i\n",
			record->dlightIndex, record->shadowIndex, record->face,
			record->atlasBaseFace + record->face, record->atlasBaseFace,
			record->atlasX, record->atlasY, record->atlasFaceSize,
			record->viewportX, record->viewportY,
			record->viewportWidth, record->viewportHeight,
			record->scissorX, record->scissorY,
			record->scissorWidth, record->scissorHeight,
			record->apiViewportX, record->apiViewportY,
			record->apiViewportWidth, record->apiViewportHeight,
			record->apiScissorX, record->apiScissorY,
			record->apiScissorWidth, record->apiScissorHeight,
			record->zNear, record->zFar,
			record->cached, record->rendered, record->surfaces );

		ri.Printf( PRINT_ALL,
			"shadow correctness projection light:%i face:%i matrix:[%.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g]\n",
			record->dlightIndex, record->face,
			record->projectionMatrix[0], record->projectionMatrix[1],
			record->projectionMatrix[2], record->projectionMatrix[3],
			record->projectionMatrix[4], record->projectionMatrix[5],
			record->projectionMatrix[6], record->projectionMatrix[7],
			record->projectionMatrix[8], record->projectionMatrix[9],
			record->projectionMatrix[10], record->projectionMatrix[11],
			record->projectionMatrix[12], record->projectionMatrix[13],
			record->projectionMatrix[14], record->projectionMatrix[15] );

		ri.Printf( PRINT_ALL,
			"shadow correctness model light:%i face:%i matrix:[%.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g]\n",
			record->dlightIndex, record->face,
			record->modelMatrix[0], record->modelMatrix[1],
			record->modelMatrix[2], record->modelMatrix[3],
			record->modelMatrix[4], record->modelMatrix[5],
			record->modelMatrix[6], record->modelMatrix[7],
			record->modelMatrix[8], record->modelMatrix[9],
			record->modelMatrix[10], record->modelMatrix[11],
			record->modelMatrix[12], record->modelMatrix[13],
			record->modelMatrix[14], record->modelMatrix[15] );
	}
}

static void R_PrintCSMCascadeDebug( const csmPlan_t *csm, const char *backendName,
	qboolean apiTopLeft, qboolean sampleYInverted, qboolean clipYFlipped,
	qboolean depthZeroToOne )
{
	const shadowManager_t *manager = &tr.shadowManager;
	const char *sampleY = sampleYInverted ? "inverted" : "native";
	const char *clipY = clipYFlipped ? "flipped" : "native";
	const char *ndc = depthZeroToOne ? "0..1" : "-1..1";
	int cascadeSize;
	int atlasHeight;
	int i;

	if ( !csm || csm->cascadeCount <= 0 ) {
		return;
	}

	cascadeSize = manager->csmAtlasPublication.tileSize > 0 ?
		manager->csmAtlasPublication.tileSize : manager->csmAtlasHeight;
	if ( cascadeSize <= 0 ) {
		cascadeSize = csm->resolution;
	}
	atlasHeight = manager->csmAtlasPublication.height > 0 ?
		manager->csmAtlasPublication.height : manager->csmAtlasHeight;
	if ( atlasHeight <= 0 ) {
		atlasHeight = cascadeSize;
	}

	for ( i = 0; i < csm->cascadeCount && i < CSM_MAX_CASCADES; i++ ) {
		const csmCascadePlan_t *cascade = &csm->cascades[i];
		int atlasX = i * cascadeSize;
		int atlasY = 0;
		int apiY = apiTopLeft ? atlasHeight - cascadeSize : 0;

		ri.Printf( PRINT_ALL,
			"csm cascade backend:%s index:%i split:%.0f..%.0f atlas:%i,%i/%i view:%i,%i %ix%i api:%i,%i %ix%i sample-y:%s clip-y:%s depth:forward ndc:%s clear:1.0 compare:lequal bounds x:%.0f..%.0f y:%.0f..%.0f z:%.0f..%.0f origin:%.0f %.0f %.0f texel:%.2f\n",
			backendName, i, cascade->splitNear, cascade->splitFar,
			atlasX, atlasY, cascadeSize,
			atlasX, atlasY, cascadeSize, cascadeSize,
			atlasX, apiY, cascadeSize, cascadeSize,
			sampleY, clipY, ndc,
			cascade->bounds[0][0], cascade->bounds[1][0],
			cascade->bounds[0][1], cascade->bounds[1][1],
			cascade->bounds[0][2], cascade->bounds[1][2],
			cascade->origin[0], cascade->origin[1], cascade->origin[2],
			cascade->texelSize );
	}
}

static int R_ShadowAtlasFillPercent( int records, int tileSize, int width, int height )
{
	int64_t usedPixels;
	int64_t atlasPixels;
	int fill;

	if ( records <= 0 || tileSize <= 0 || width <= 0 || height <= 0 ) {
		return 0;
	}

	usedPixels = (int64_t)records * tileSize * tileSize;
	atlasPixels = (int64_t)width * height;
	if ( atlasPixels <= 0 ) {
		return 0;
	}

	fill = (int)( usedPixels * 100 / atlasPixels );
	return Com_Clamp( 1, 100, fill );
}

static void R_PrintShadowAtlasContractLine( const char *backendName, const char *atlasName,
	qboolean active, int width, int height, int tileSize, int records, int fill,
	int filterMode, const char *allocation )
{
	ri.Printf( PRINT_ALL,
		"shadow atlas contract backend:%s atlas:%s active:%i tile:%i size:%ix%i records:%i fill:%i%% filter:%s pad:%i clamp:%i sampler:clamp-edge allocation:%s deterministic:1\n",
		backendName, atlasName, active,
		tileSize, width, height, records, fill,
		R_ShadowFilterModeName( filterMode ),
		R_ShadowFilterPadTexels( filterMode ),
		R_ShadowAtlasClampTexels( filterMode ),
		allocation );
}

static void R_PrintShadowAtlasContractDebug( const shadowManager_t *manager,
	const csmPlan_t *csm, const char *backendName )
{
	int requestedDlightFilter;
	int dlightFilter;
	int csmFilter;
	int csmWidth;
	int csmHeight;
	int csmTileSize;
	int csmRecords;
	int csmFill;

	if ( !manager ) {
		return;
	}

	requestedDlightFilter = r_dlightShadowFilter ?
		r_dlightShadowFilter->integer : SHADOW_FILTER_POISSON_4;
	dlightFilter = ( r_shadowCorrectness && r_shadowCorrectness->integer ) ?
		SHADOW_FILTER_HARD : R_ShadowClampFilterMode( requestedDlightFilter );
	csmFilter = ( csm && csm->enabled ) ? csm->filterMode :
		R_ShadowClampFilterMode( r_csmShadowFilter ?
			r_csmShadowFilter->integer : SHADOW_FILTER_POISSON_4 );
	if ( r_shadowCorrectness && r_shadowCorrectness->integer ) {
		csmFilter = SHADOW_FILTER_HARD;
	}

	R_PrintShadowAtlasContractLine( backendName, "point",
		( manager->pointAtlasPublication.published && manager->pointPlanCount > 0 ) ? qtrue : qfalse,
		manager->dlightAtlasWidth, manager->dlightAtlasHeight,
		manager->dlightAtlasFaceSize, manager->pointPlanCount,
		manager->dlightAtlasFill, dlightFilter, "priority-dlight-index" );

	R_PrintShadowAtlasContractLine( backendName, "spot",
		( manager->spotAtlasPublication.published && manager->spotPlanCount > 0 ) ? qtrue : qfalse,
		manager->spotAtlasWidth, manager->spotAtlasHeight,
		manager->spotAtlasTileSize, manager->spotPlanCount,
		manager->spotAtlasFill, dlightFilter, "priority-source-index" );

	csmWidth = manager->csmAtlasWidth;
	csmHeight = manager->csmAtlasHeight;
	csmTileSize = manager->csmResolution;
	if ( manager->csmAtlasPublication.width > 0 ) {
		csmWidth = manager->csmAtlasPublication.width;
	}
	if ( manager->csmAtlasPublication.height > 0 ) {
		csmHeight = manager->csmAtlasPublication.height;
	}
	if ( manager->csmAtlasPublication.tileSize > 0 ) {
		csmTileSize = manager->csmAtlasPublication.tileSize;
	}
	csmRecords = csm ? csm->cascadeCount : manager->csmCascadeCount;
	csmFill = R_ShadowAtlasFillPercent( csmRecords, csmTileSize, csmWidth, csmHeight );

	R_PrintShadowAtlasContractLine( backendName, "csm",
		( manager->csmAtlasPublication.published && csmRecords > 0 ) ? qtrue : qfalse,
		csmWidth, csmHeight, csmTileSize, csmRecords, csmFill,
		csmFilter, "cascade-index" );
}

static void R_PerformanceCounters( void ) {
	qboolean shadowDebug;
	qboolean csmDebug;
	qboolean spotShadowDebug;
	qboolean staticLightDebug;
	qboolean surfaceLightDebug;
	qboolean shadowCorrectnessDebug;

	shadowDebug = ( r_dlightShadowDebug && r_dlightShadowDebug->integer ) ? qtrue : qfalse;
	csmDebug = ( r_csmDebug && r_csmDebug->integer ) ? qtrue : qfalse;
	spotShadowDebug = ( r_spotShadowDebug && r_spotShadowDebug->integer ) ? qtrue : qfalse;
	staticLightDebug = ( r_staticLightDebug && r_staticLightDebug->integer ) ? qtrue : qfalse;
	surfaceLightDebug = ( r_surfaceLightProxyDebug && r_surfaceLightProxyDebug->integer ) ? qtrue : qfalse;
	shadowCorrectnessDebug = ( r_shadowCorrectness && r_shadowCorrectness->integer ) ? qtrue : qfalse;

	if ( ri.SetClientMessageRendererNodeCount ) {
		ri.SetClientMessageRendererNodeCount( tr.pc.c_leafs );
	}

	if ( !r_speeds->integer && !shadowDebug && !csmDebug && !spotShadowDebug && !staticLightDebug && !surfaceLightDebug && !shadowCorrectnessDebug ) {
		// clear the counters even if we aren't printing
		Com_Memset( &tr.pc, 0, sizeof( tr.pc ) );
		Com_Memset( &tr.shadowManager, 0, sizeof( tr.shadowManager ) );
		Com_Memset( &tr.shadowCorrectnessDebug, 0, sizeof( tr.shadowCorrectnessDebug ) );
		Com_Memset( &tr.csmDebugPlan, 0, sizeof( tr.csmDebugPlan ) );
		Com_Memset( &backEnd.pc, 0, sizeof( backEnd.pc ) );
		return;
	}

	if (r_speeds->integer == 1) {
		ri.Printf (PRINT_ALL, "%i/%i shaders/surfs %i leafs %i verts %i/%i tris %.2f mtex\n",
			backEnd.pc.c_shaders, backEnd.pc.c_surfaces, tr.pc.c_leafs, backEnd.pc.c_vertexes, 
			backEnd.pc.c_indexes/3, backEnd.pc.c_totalIndexes/3, R_SumOfUsedImages()/1000000.0); 
	} else if (r_speeds->integer == 2) {
		ri.Printf (PRINT_ALL, "(patch) %i sin %i sclip  %i sout %i bin %i bclip %i bout\n",
			tr.pc.c_sphere_cull_patch_in, tr.pc.c_sphere_cull_patch_clip, tr.pc.c_sphere_cull_patch_out, 
			tr.pc.c_box_cull_patch_in, tr.pc.c_box_cull_patch_clip, tr.pc.c_box_cull_patch_out );
		ri.Printf (PRINT_ALL, "(md3) %i sin %i sclip  %i sout %i bin %i bclip %i bout\n",
			tr.pc.c_sphere_cull_md3_in, tr.pc.c_sphere_cull_md3_clip, tr.pc.c_sphere_cull_md3_out, 
			tr.pc.c_box_cull_md3_in, tr.pc.c_box_cull_md3_clip, tr.pc.c_box_cull_md3_out );
	} else if (r_speeds->integer == 3) {
		ri.Printf (PRINT_ALL, "viewcluster: %i\n", tr.viewCluster );
	} else if (r_speeds->integer == 4) {
		if ( backEnd.pc.c_dlightVertexes ) {
			ri.Printf (PRINT_ALL, "dlight srf:%i  culled:%i  verts:%i  tris:%i\n", 
				tr.pc.c_dlightSurfaces, tr.pc.c_dlightSurfacesCulled,
				backEnd.pc.c_dlightVertexes, backEnd.pc.c_dlightIndexes / 3 );
		}
	} 
	else if (r_speeds->integer == 5 )
	{
		ri.Printf( PRINT_ALL, "zFar: %.0f\n", tr.viewParms.zFar );
	}
	else if (r_speeds->integer == 6 )
	{
		ri.Printf( PRINT_ALL, "flare adds:%i tests:%i renders:%i\n", 
			backEnd.pc.c_flareAdds, backEnd.pc.c_flareTests, backEnd.pc.c_flareRenders );
	}

	if ( ( r_speeds->integer == 4 || shadowDebug || csmDebug || spotShadowDebug ) && tr.shadowManager.planned ) {
		const shadowManager_t *manager = &tr.shadowManager;

		ri.Printf( PRINT_ALL,
			"shadow manager view:%i frame:%i noworld:%i sched:%i mask:%x p:%i s:%i ca:%i cr:%i pub p:%i s:%i c:%i inputs dlight:%i point:%i/%i cand:%i records:%i/%i atlas:%ix%i/%i fill:%i%% gen:%u spot:%i/%i src static:%i/%i surface:%i/%i atlas:%ix%i/%i fill:%i%% gen:%u csm:%i atlas:%ix%i gen:%u\n",
			manager->viewCount, manager->frameCount, manager->noWorldModel,
			manager->scheduledPasses, manager->scheduledPassMask,
			manager->pointAtlasScheduled, manager->spotAtlasScheduled,
			manager->csmAtlasScheduled, manager->csmReceiverScheduled,
			manager->pointAtlasPublication.published, manager->spotAtlasPublication.published,
			manager->csmAtlasPublication.published,
			manager->inputDlights,
			manager->dlightPlannedCount, manager->dlightConsidered,
			manager->dlightCandidates,
			manager->pointPlanCount, manager->pointCandidateCount,
			manager->dlightAtlasWidth, manager->dlightAtlasHeight,
			manager->dlightAtlasFaceSize, manager->dlightAtlasFill,
			manager->pointAtlasPublication.generation,
			manager->spotPlanCount, manager->spotCandidateCount,
			manager->spotStaticPlanCount, manager->spotStaticCandidateCount,
			manager->spotSurfacePlanCount, manager->spotSurfaceCandidateCount,
			manager->spotAtlasWidth, manager->spotAtlasHeight,
			manager->spotAtlasTileSize, manager->spotAtlasFill,
			manager->spotAtlasPublication.generation,
			manager->csmCascadeCount,
			manager->csmAtlasWidth, manager->csmAtlasHeight,
			manager->csmAtlasPublication.generation );
		R_PrintShadowAtlasContractDebug( manager, &manager->csmPlan, "rtx" );

		if ( r_speeds->integer == 4 || spotShadowDebug || surfaceLightDebug ) {
			ri.Printf( PRINT_ALL,
				"surfacelight spot plan cand:%i plan:%i req:%i-%i foot:%i-%i caster:%i-%i planreq:%i-%i tile:%i-%i cone:%i-%i/%i-%i alloc:%i atlas:%ix%i/%i fill:%i%% reject weak:%i offview:%i budget:%i malformed:%i\n",
				manager->spotSurfaceCandidateCount, manager->spotSurfacePlanCount,
				manager->spotSurfaceCandidateTileMin, manager->spotSurfaceCandidateTileMax,
				manager->spotSurfaceFootprintMin, manager->spotSurfaceFootprintMax,
				manager->spotSurfaceCasterRadiusMin, manager->spotSurfaceCasterRadiusMax,
				manager->spotSurfacePlanRequestedTileMin,
				manager->spotSurfacePlanRequestedTileMax,
				manager->spotSurfacePlanTileMin, manager->spotSurfacePlanTileMax,
				manager->spotSurfaceConeInnerMin, manager->spotSurfaceConeInnerMax,
				manager->spotSurfaceConeOuterMin, manager->spotSurfaceConeOuterMax,
				manager->spotSurfacePlanAllocatedCount,
				manager->spotAtlasWidth, manager->spotAtlasHeight,
				manager->spotAtlasTileSize, manager->spotAtlasFill,
				manager->spotSurfaceRejectedWeak,
				manager->spotSurfaceRejectedOffView,
				manager->spotSurfaceRejectedOverBudget,
				manager->spotSurfaceRejectedMalformed );
		}

		if ( ( r_speeds->integer == 4 || spotShadowDebug ) &&
			( backEnd.pc.c_spotShadowAtlasCacheHits ||
				backEnd.pc.c_spotShadowAtlasCacheMisses ||
				backEnd.pc.c_spotShadowAtlasCacheUncacheable ) ) {
			ri.Printf( PRINT_ALL, "spot shadow atlas cache h/m/u:%i/%i/%i\n",
				backEnd.pc.c_spotShadowAtlasCacheHits,
				backEnd.pc.c_spotShadowAtlasCacheMisses,
				backEnd.pc.c_spotShadowAtlasCacheUncacheable );
		}
	}

	if ( ( r_speeds->integer == 4 || staticLightDebug ) &&
		( tr.staticMapLights.loaded || tr.staticMapLights.parseFailed ) ) {
		ri.Printf( PRINT_ALL,
			"static lights file:%s loaded:%i parsefail:%i count:%i point:%i spot:%i spatial:%i/%i promoted:%i shadow:%i skip disabled:%i pvs:%i budget:%i unsupported:%i invalid:%i overflow:%i\n",
			tr.staticMapLights.filename[0] ? tr.staticMapLights.filename : "<none>",
			tr.staticMapLights.loaded, tr.staticMapLights.parseFailed,
			tr.staticMapLights.count, tr.staticMapLights.pointCount,
			tr.staticMapLights.spotCount, tr.staticMapLights.spatialized,
			tr.staticMapLights.spatialFallback, tr.staticMapLights.promotedThisFrame,
			tr.staticMapLights.shadowEligibleThisFrame,
			tr.staticMapLights.skippedDisabledThisFrame,
			tr.staticMapLights.skippedPVSThisFrame,
			tr.staticMapLights.skippedBudgetThisFrame,
			tr.staticMapLights.skippedUnsupported,
			tr.staticMapLights.skippedInvalid,
			tr.staticMapLights.skippedOverflow );
	}

	if ( ( r_speeds->integer == 4 || surfaceLightDebug ) && tr.surfaceLightProxies.built ) {
		ri.Printf( PRINT_ALL,
			"surfacelight proxies sources:%i built:%i point:%i spot:%i subdiv:%i/%i spatial:%i/%i promoted:%i shadow:%i spotdefer:%i skip sky:%i invalid:%i disabled:%i pvs:%i budget:%i overflow:%i\n",
			tr.surfaceLightProxies.sourceSurfaces,
			tr.surfaceLightProxies.count,
			tr.surfaceLightProxies.pointProjectionCount,
			tr.surfaceLightProxies.spotProjectionCount,
			tr.surfaceLightProxies.subdividedSurfaces,
			tr.surfaceLightProxies.subdivisionProxies,
			tr.surfaceLightProxies.spatialized,
			tr.surfaceLightProxies.spatialFallback,
			tr.surfaceLightProxies.promotedThisFrame,
			tr.surfaceLightProxies.shadowEligibleThisFrame,
			tr.surfaceLightProxies.spotShadowDeferredThisFrame,
			tr.surfaceLightProxies.skippedSky,
			tr.surfaceLightProxies.skippedInvalid,
			tr.surfaceLightProxies.skippedDisabledThisFrame,
			tr.surfaceLightProxies.skippedPVSThisFrame,
			tr.surfaceLightProxies.skippedBudgetThisFrame,
			tr.surfaceLightProxies.skippedOverflow );
	}

#ifdef USE_PMLIGHT
	if ( ( r_speeds->integer == 4 || shadowDebug ) &&
		( tr.pc.c_dlightShadowConsidered || tr.pc.c_dlightShadowSkippedDisabled ) ) {
		int requestedFilter = r_dlightShadowFilter ?
			r_dlightShadowFilter->integer : SHADOW_FILTER_POISSON_4;
		int effectiveFilter = ( r_shadowCorrectness && r_shadowCorrectness->integer ) ?
			SHADOW_FILTER_HARD : R_ShadowClampFilterMode( requestedFilter );
		float filterInner = 0.0f;
		float filterOuter = 0.0f;

		R_ShadowFilterOffsets( effectiveFilter, &filterInner, &filterOuter );

		ri.Printf( PRINT_ALL,
			"dlight shadows plan:%i/%i cand:%i atlas:%ix%i/%i fill:%i%% filter:%s taps:%i offsets:%.2f/%.2f render lights:%i faces:%i batches:%i draws:%i surfs:%i cpu:%ims skip disabled:%i linear:%i nosurf:%i proj:%i budget:%i lowvalue:%i\n",
			tr.pc.c_dlightShadowPlanned, tr.pc.c_dlightShadowConsidered,
			tr.pc.c_dlightShadowCandidates,
			tr.pc.c_dlightShadowAtlasWidth, tr.pc.c_dlightShadowAtlasHeight,
			tr.pc.c_dlightShadowAtlasFaceSize,
			tr.pc.c_dlightShadowAtlasFill,
			R_ShadowFilterModeName( effectiveFilter ),
			R_ShadowFilterSampleCount( effectiveFilter ),
			filterInner, filterOuter,
			backEnd.pc.c_dlightShadowAtlasLights,
			backEnd.pc.c_dlightShadowAtlasFaces,
			backEnd.pc.c_dlightShadowAtlasBatches,
			backEnd.pc.c_dlightShadowAtlasDraws,
			backEnd.pc.c_dlightShadowAtlasSurfaces,
			backEnd.pc.c_dlightShadowAtlasMsec,
			tr.pc.c_dlightShadowSkippedDisabled,
			tr.pc.c_dlightShadowSkippedLinear, tr.pc.c_dlightShadowSkippedNoSurfaces,
			tr.pc.c_dlightShadowSkippedProjection, tr.pc.c_dlightShadowSkippedBudget,
			tr.pc.c_dlightShadowSkippedLowValue );
	}
#endif

	if ( shadowCorrectnessDebug ) {
		R_PrintShadowCorrectnessDebug( "rtx" );
	}

	if ( ( r_speeds->integer == 4 || csmDebug ) &&
		( tr.csmDebugPlan.enabled || ( csmDebug && r_csmShadows && r_csmShadows->integer ) ) ) {
		if ( tr.csmDebugPlan.enabled && tr.csmDebugPlan.cascadeCount > 0 ) {
			const csmPlan_t *csm = &tr.csmDebugPlan;
			float splitFar[CSM_MAX_CASCADES] = { 0.0f };
			float texelSize[CSM_MAX_CASCADES] = { 0.0f };
			float depthCenter[CSM_MAX_CASCADES] = { 0.0f };
			const char *skyShaderName;
			int i;

			for ( i = 0; i < csm->cascadeCount && i < CSM_MAX_CASCADES; i++ ) {
				splitFar[i] = csm->cascades[i].splitFar;
				texelSize[i] = csm->cascades[i].texelSize;
				depthCenter[i] = 0.5f * ( csm->cascades[i].bounds[0][0] + csm->cascades[i].bounds[1][0] );
			}
			skyShaderName = csm->skyShaderName[0] ? csm->skyShaderName : "<unknown>";

			ri.Printf( PRINT_ALL,
				"csm shadows sky:%s cascades:%i res:%i max:%i lambda:%.2f filter:%s strength:%.2f rbias:%.2f cbias:%.2f/%.2f/%.2f light-dir:%.2f %.2f %.2f to-sun:%.2f %.2f %.2f split far:%.0f %.0f %.0f %.0f texel:%.2f %.2f %.2f %.2f depth center:%.0f %.0f %.0f %.0f caster:%i cache h/m/u:%i/%i/%i cpu:%ims recv world:%i ent:%i\n",
				skyShaderName,
				tr.pc.c_csmCascades, tr.pc.c_csmResolution, tr.pc.c_csmMaxDistance,
				csm->splitLambda, R_ShadowFilterModeName( csm->filterMode ),
				csm->shadowStrength, csm->receiverBias, csm->casterDepthBias, csm->casterSlopeBias,
				csm->casterNormalBias,
				csm->lightDirection[0], csm->lightDirection[1], csm->lightDirection[2],
				csm->directionToSun[0], csm->directionToSun[1], csm->directionToSun[2],
				splitFar[0], splitFar[1], splitFar[2], splitFar[3],
				texelSize[0], texelSize[1], texelSize[2], texelSize[3],
				depthCenter[0], depthCenter[1], depthCenter[2], depthCenter[3],
				backEnd.pc.c_csmShadowAtlasSurfaces,
				backEnd.pc.c_csmShadowAtlasCacheHits,
				backEnd.pc.c_csmShadowAtlasCacheMisses,
				backEnd.pc.c_csmShadowAtlasCacheUncacheable,
				backEnd.pc.c_csmShadowAtlasMsec,
				backEnd.pc.c_csmShadowReceiverWorldSurfaces,
				backEnd.pc.c_csmShadowReceiverEntitySurfaces );
			R_PrintCSMCascadeDebug( csm, "vulkan", qtrue, qtrue, qtrue, qtrue );
		} else if ( tr.pc.c_csmSkippedNoWorldRef ) {
			ri.Printf( PRINT_ALL, "csm plan cascades:0 skip no-world\n" );
		} else if ( tr.pc.c_csmSkippedNoSun ) {
			ri.Printf( PRINT_ALL, "csm plan cascades:0 skip no-sky-sun\n" );
		} else if ( tr.pc.c_csmSkippedProjection ) {
			ri.Printf( PRINT_ALL, "csm plan cascades:0 skip projection\n" );
		} else if ( tr.pc.c_csmSkippedAtlas ) {
			ri.Printf( PRINT_ALL, "csm plan cascades:0 skip atlas\n" );
		} else if ( tr.pc.c_csmSkippedStrength ) {
			ri.Printf( PRINT_ALL, "csm plan cascades:0 skip strength\n" );
		} else if ( tr.pc.c_csmSkippedZeroCascades ) {
			ri.Printf( PRINT_ALL, "csm plan cascades:0 skip zero-cascade\n" );
		} else if ( tr.pc.c_csmSkippedDisabled && !tr.pc.c_csmSkippedNoWorldRef ) {
			ri.Printf( PRINT_ALL, "csm plan cascades:0 skip disabled\n" );
		}
	}

	Com_Memset( &tr.pc, 0, sizeof( tr.pc ) );
	Com_Memset( &tr.shadowManager, 0, sizeof( tr.shadowManager ) );
	Com_Memset( &tr.shadowCorrectnessDebug, 0, sizeof( tr.shadowCorrectnessDebug ) );
	Com_Memset( &tr.csmDebugPlan, 0, sizeof( tr.csmDebugPlan ) );
	Com_Memset( &backEnd.pc, 0, sizeof( backEnd.pc ) );
}


/*
====================
R_IssueRenderCommands
====================
*/
static void R_IssueRenderCommands( void ) {
	renderCommandList_t	*cmdList;

	cmdList = &backEndData->commands;

	// add an end-of-list command
	*(int *)(cmdList->cmds + cmdList->used) = RC_END_OF_LIST;

	// clear it out, in case this is a sync and not a buffer flip
	cmdList->used = 0;

	if ( backEnd.screenshotMask == 0 && !backEnd.levelshotPending ) {
		if ( ri.CL_IsMinimized() )
			return; // skip backend when minimized
		if ( backEnd.throttle )
			return; // or throttled on demand
	} else {
#ifdef USE_VULKAN
		if ( ri.CL_IsMinimized() && !RE_CanMinimize() ) {
			backEnd.screenshotMask = 0;
			backEnd.levelshotPending = qfalse;
			ri.Cvar_Set( "cl_captureActive", "0" );
			return;
		}
#endif
	}

	// actually start the commands going
	if ( !r_skipBackEnd->integer ) {
		// let it start on the new batch
		RB_ExecuteRenderCommands( cmdList->cmds );
	}
}


/*
====================
R_IssuePendingRenderCommands

Drain queued front-end quads before a retained resource is resized or reset.
====================
*/
void R_IssuePendingRenderCommands( void ) {
	if ( !tr.registered ) {
		return;
	}
	R_IssueRenderCommands();
#ifdef USE_VULKAN
	vk_wait_idle();
#endif
}


/*
============
R_GetCommandBufferReserved

make sure there is enough command space
============
*/
static void *R_GetCommandBufferReserved( int bytes, int reservedBytes ) {
	renderCommandList_t	*cmdList;

	cmdList = &backEndData->commands;
	bytes = PAD(bytes, sizeof(void *));

	// always leave room for the end of list command
	if ( cmdList->used + bytes + sizeof( int ) + reservedBytes > MAX_RENDER_COMMANDS ) {
		if ( bytes > MAX_RENDER_COMMANDS - sizeof( int ) ) {
			ri.Error( ERR_FATAL, "R_GetCommandBuffer: bad size %i", bytes );
		}
		// if we run out of room, just start dropping commands
		return NULL;
	}

	cmdList->used += bytes;

	return cmdList->cmds + cmdList->used - bytes;
}


/*
=============
R_GetCommandBuffer
returns NULL if there is not enough space for important commands
=============
*/
void *R_GetCommandBuffer( int bytes ) {
#ifdef USE_VULKAN
	tr.lastRenderCommand = RC_END_OF_LIST;
#endif
	return R_GetCommandBufferReserved( bytes, PAD( sizeof( swapBuffersCommand_t ), sizeof(void *) ) );
}


/*
=============
R_AddDrawSurfCmd
=============
*/
void R_AddDrawSurfCmd( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	drawSurfsCommand_t	*cmd;

	cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_DRAW_SURFS;

	cmd->drawSurfs = drawSurfs;
	cmd->numDrawSurfs = numDrawSurfs;

	cmd->refdef = tr.refdef;
	cmd->viewParms = tr.viewParms;
	cmd->csm = tr.csm;
	cmd->shadowManager = tr.shadowManager;

#ifdef USE_VULKAN
	tr.numDrawSurfCmds++;
	if ( tr.drawSurfCmd == NULL ) {
		tr.drawSurfCmd = cmd;
	}
#endif
}


/*
=============
RE_SetColor

Passing NULL will set the color to white
=============
*/
void RE_SetColor( const float *rgba ) {
	setColorCommand_t	*cmd;

	if ( !tr.registered ) {
		return;
	}
	cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_SET_COLOR;
	if ( !rgba ) {
		rgba = colorWhite;
	}

	cmd->color[0] = rgba[0];
	cmd->color[1] = rgba[1];
	cmd->color[2] = rgba[2];
	cmd->color[3] = rgba[3];
}


/*
=============
RE_StretchPic
=============
*/
void RE_StretchPic( float x, float y, float w, float h,
					float s1, float t1, float s2, float t2, qhandle_t hShader ) {
	stretchPicCommand_t	*cmd;

	if ( !tr.registered ) {
		return;
	}
	cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_STRETCH_PIC;
	cmd->shader = R_GetShaderByHandle( hShader );
	cmd->x = x;
	cmd->y = y;
	cmd->w = w;
	cmd->h = h;
	cmd->s1 = s1;
	cmd->t1 = t1;
	cmd->s2 = s2;
	cmd->t2 = t2;
}

#define MODE_RED_CYAN	1
#define MODE_RED_BLUE	2
#define MODE_RED_GREEN	3
#define MODE_GREEN_MAGENTA 4
#define MODE_MAX	MODE_GREEN_MAGENTA

#ifndef USE_VULKAN
static void R_SetColorMode(GLboolean *rgba, stereoFrame_t stereoFrame, int colormode)
{
	rgba[0] = rgba[1] = rgba[2] = rgba[3] = GL_TRUE;

	if(colormode > MODE_MAX)
	{
		if(stereoFrame == STEREO_LEFT)
			stereoFrame = STEREO_RIGHT;
		else if(stereoFrame == STEREO_RIGHT)
			stereoFrame = STEREO_LEFT;

		colormode -= MODE_MAX;
	}

	if(colormode == MODE_GREEN_MAGENTA)
	{
		if(stereoFrame == STEREO_LEFT)
			rgba[0] = rgba[2] = GL_FALSE;
		else if(stereoFrame == STEREO_RIGHT)
			rgba[1] = GL_FALSE;
	}
	else
	{
		if(stereoFrame == STEREO_LEFT)
			rgba[1] = rgba[2] = GL_FALSE;
		else if(stereoFrame == STEREO_RIGHT)
		{
			rgba[0] = GL_FALSE;

			if(colormode == MODE_RED_BLUE)
				rgba[1] = GL_FALSE;
			else if(colormode == MODE_RED_GREEN)
				rgba[2] = GL_FALSE;
		}
	}
}
#endif


/*
====================
RE_BeginFrame

If running in stereo, RE_BeginFrame will be called twice
for each RE_EndFrame
====================
*/
void RE_BeginFrame( stereoFrame_t stereoFrame ) {
	drawBufferCommand_t *cmd;

	if ( !tr.registered ) {
		return;
	}

	glState.finishCalled = qfalse;

#ifdef USE_VULKAN
	backEnd.doneBloom = qfalse;
	backEnd.doneMotionBlur = qfalse;
#endif
	backEnd.bloomProtectHighlights = qfalse;

	backEnd.color2D.u32 = ~0U;

	tr.frameCount++;
	tr.frameSceneNum = 0;

	if ( ( cmd = R_GetCommandBuffer( sizeof( *cmd ) ) ) == NULL )
		return;

	cmd->commandId = RC_DRAW_BUFFER;

#ifdef USE_VULKAN
	tr.lastRenderCommand = RC_DRAW_BUFFER;
#endif

	if ( glConfig.stereoEnabled ) {
		if ( stereoFrame == STEREO_LEFT ) {
			cmd->buffer = (int)GL_BACK_LEFT;
		} else if ( stereoFrame == STEREO_RIGHT ) {
			cmd->buffer = (int)GL_BACK_RIGHT;
		} else {
			ri.Error( ERR_FATAL, "RE_BeginFrame: Stereo is enabled, but stereoFrame was %i", stereoFrame );
		}
	} else {
		if ( stereoFrame != STEREO_CENTER ) {
			ri.Error( ERR_FATAL, "RE_BeginFrame: Stereo is disabled, but stereoFrame was %i", stereoFrame );
		}

#ifdef USE_VULKAN
		cmd->buffer = 0;
#else
		if ( !Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) )
			cmd->buffer = (int)GL_FRONT;
		else
			cmd->buffer = (int)GL_BACK;
#endif
	}

#ifndef USE_BUFFER_CLEAR
#ifdef USE_VULKAN
	if ( r_fastsky->integer && vk.clearAttachment ) {
#else
	if ( r_fastsky->integer ) {
#endif
		if ( stereoFrame != STEREO_RIGHT ) {
			clearColorCommand_t *clrcmd; 
			if ( ( clrcmd = R_GetCommandBuffer( sizeof( *clrcmd ) ) ) == NULL )
				return;
			clrcmd->commandId = RC_CLEARCOLOR;
		}
	}
#endif // USE_BUFFER_CLEAR

	tr.refdef.stereoFrame = stereoFrame;
}


/*
=============
RE_EndFrame

Returns the number of msec spent in the back end
=============
*/
void RE_EndFrame( int *frontEndMsec, int *backEndMsec ) {

	swapBuffersCommand_t *cmd;

	if ( !tr.registered ) {
		return;
	}

	cmd = R_GetCommandBufferReserved( sizeof( *cmd ), 0 );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_SWAP_BUFFERS;

	R_IssueRenderCommands();

	R_PerformanceCounters();

	R_InitNextFrame();

	if ( frontEndMsec ) {
		*frontEndMsec = tr.frontEndMsec;
	}
	tr.frontEndMsec = 0;

	if ( backEndMsec ) {
		*backEndMsec = backEnd.pc.msec;
	}
	backEnd.pc.msec = 0;

	backEnd.throttle = qfalse;

	// recompile GPU shaders if needed
	if ( ri.Cvar_CheckGroup( CVG_RENDERER ) ) {

		// texturemode stuff
		if ( r_textureMode->modified ) {
			GL_TextureMode( r_textureMode->string );
		}

		// gamma stuff
		if ( r_gamma->modified ) {
			R_SetColorMappings();
		}

#ifdef USE_VULKAN
		vk_update_post_process_pipelines();
#endif

		ri.Cvar_ResetGroup( CVG_RENDERER, qtrue /* reset modified flags */ );
	}
}


/*
=============
RE_TakeVideoFrame
=============
*/
void RE_TakeVideoFrame( int width, int height,
		byte *captureBuffer, byte *encodeBuffer, qboolean motionJpeg )
{
	videoFrameCommand_t	*cmd;

	if( !tr.registered ) {
		return;
	}

	backEnd.screenshotMask |= SCREENSHOT_AVI;

	cmd = &backEnd.vcmd;

	//cmd->commandId = RC_VIDEOFRAME;

	cmd->width = width;
	cmd->height = height;
	cmd->captureBuffer = captureBuffer;
	cmd->encodeBuffer = encodeBuffer;
	cmd->motionJpeg = motionJpeg;
}


void RE_ThrottleBackend( void )
{
	backEnd.throttle = qtrue;
}


void RE_FinishBloom( void )
{
	finishBloomCommand_t *cmd;

	if ( !tr.registered ) {
		return;
	}

	cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}

	cmd->commandId = RC_FINISHBLOOM;
}


void RE_DrawMenuDepthOfField( float amount )
{
	/*
	 * Keep the renderer ABI aligned with Vulkan.  The Vulkan implementation
	 * currently treats this as an optional no-op as well; retaining the hook
	 * lets the client use the same feature probing path for both renderers.
	 */
	(void)amount;
}


qboolean RE_CanMinimize( void )
{
#ifdef USE_VULKAN
	if ( vk.fboActive || vk.offscreenRender )
		return qtrue;
#endif
	return qfalse;
}


const glconfig_t *RE_GetConfig( void )
{
	return &glConfig;
}


void RE_VertexLighting( qboolean allowed )
{
	tr.vertexLightingAllowed = allowed;
}
