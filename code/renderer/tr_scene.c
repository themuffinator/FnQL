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
#include "../renderercommon/tr_cubemap.h"
#include "../renderercommon/tr_liquid.h"
#include "../renderercommon/tr_muzzle_flash_light.h"

static int			r_firstSceneDrawSurf;
#ifdef USE_PMLIGHT
static int			r_firstSceneLitSurf;
#endif

int			r_numdlights;
static int			r_firstSceneDlight;

static int			r_numentities;
static int			r_firstSceneEntity;

static int			r_numpolys;
static int			r_firstScenePoly;

static int			r_numpolyverts;

static liquidInteraction_t r_liquidInteractions[LIQUID_MAX_STORED_IMPULSES];
static int r_numLiquidInteractions;
static int r_liquidInteractionTime = -1;

static void R_PruneLiquidInteractions( int sceneTime )
{
	int readIndex;
	int writeIndex = 0;

	if ( r_liquidInteractionTime > sceneTime ) {
		r_numLiquidInteractions = 0;
	}
	r_liquidInteractionTime = sceneTime;
	for ( readIndex = 0; readIndex < r_numLiquidInteractions; readIndex++ ) {
		if ( !R_LiquidInteractionActive( &r_liquidInteractions[readIndex], sceneTime ) ) {
			continue;
		}
		if ( writeIndex != readIndex ) {
			r_liquidInteractions[writeIndex] = r_liquidInteractions[readIndex];
		}
		writeIndex++;
	}
	r_numLiquidInteractions = writeIndex;
}

void RE_AddLiquidInteractionToScene( const liquidInteraction_t *interaction )
{
#ifdef USE_FBO
	int i;

	if ( !interaction || !r_liquidRipples || r_liquidRipples->value <= 0.0f ||
		interaction->radius <= 0.0f || interaction->strength <= 0.0f ) {
		return;
	}

	R_PruneLiquidInteractions( interaction->time );
	for ( i = 0; i < r_numLiquidInteractions; i++ ) {
		const int64_t timeDelta = (int64_t)r_liquidInteractions[i].time -
			(int64_t)interaction->time;

		if ( timeDelta >= -80 && timeDelta <= 80 &&
			DistanceSquared( r_liquidInteractions[i].origin, interaction->origin ) <= Square( 20.0f ) ) {
			r_liquidInteractions[i].radius = MAX( r_liquidInteractions[i].radius, interaction->radius );
			r_liquidInteractions[i].strength = MAX( r_liquidInteractions[i].strength, interaction->strength );
			return;
		}
	}

	if ( r_numLiquidInteractions == LIQUID_MAX_STORED_IMPULSES ) {
		memmove( r_liquidInteractions, r_liquidInteractions + 1,
			( LIQUID_MAX_STORED_IMPULSES - 1 ) * sizeof( r_liquidInteractions[0] ) );
		r_numLiquidInteractions--;
	}
	r_liquidInteractions[r_numLiquidInteractions++] = *interaction;
#else
	(void)interaction;
#endif
}

static void R_CopyLiquidInteractionsToRefdef( int sceneTime )
{
	int first;
	int count;

	tr.refdef.numLiquidInteractions = 0;
#ifdef USE_FBO
	if ( !r_liquidRipples || r_liquidRipples->value <= 0.0f ) {
		Com_Memset( r_liquidInteractions, 0, sizeof( r_liquidInteractions ) );
		r_numLiquidInteractions = 0;
		r_liquidInteractionTime = sceneTime;
		return;
	}
	R_PruneLiquidInteractions( sceneTime );
	count = MIN( r_numLiquidInteractions, LIQUID_MAX_ACTIVE_IMPULSES );
	first = r_numLiquidInteractions - count;
	if ( count > 0 ) {
		Com_Memcpy( tr.refdef.liquidInteractions, r_liquidInteractions + first,
			count * sizeof( tr.refdef.liquidInteractions[0] ) );
		tr.refdef.numLiquidInteractions = count;
	}
#else
	(void)sceneTime;
	(void)first;
	(void)count;
#endif
}


/*
====================
R_InitNextFrame

====================
*/
void R_InitNextFrame( void ) {

	backEndData->commands.used = 0;

	r_firstSceneDrawSurf = 0;
#ifdef USE_PMLIGHT
	r_firstSceneLitSurf = 0;
#endif

	r_numdlights = 0;
	r_firstSceneDlight = 0;

	r_numentities = 0;
	r_firstSceneEntity = 0;

	r_numpolys = 0;
	r_firstScenePoly = 0;

	r_numpolyverts = 0;
}


/*
====================
RE_ClearScene

====================
*/
void RE_ClearScene( void ) {
	r_firstSceneDlight = r_numdlights;
	r_firstSceneEntity = r_numentities;
	r_firstScenePoly = r_numpolys;
	R_ClearMuzzleFlashLightCandidate();
}

/*
===========================================================================

DISCRETE POLYS

===========================================================================
*/

/*
=====================
R_AddPolygonSurfaces

Adds all the scene's polys into this view's drawsurf list
=====================
*/
void R_AddPolygonSurfaces( void ) {
	int			i;
	shader_t	*sh;
	const srfPoly_t	*poly;

	tr.currentEntityNum = REFENTITYNUM_WORLD;
	tr.shiftedEntityNum = tr.currentEntityNum << QSORT_REFENTITYNUM_SHIFT;

	for ( i = 0, poly = tr.refdef.polys; i < tr.refdef.numPolys ; i++, poly++ ) {
		sh = R_GetShaderByHandle( poly->hShader );
		R_AddDrawSurf( ( void * )poly, sh, poly->fogIndex, 0 );
	}
}

/*
=====================
RE_AddPolyToScene

=====================
*/
void RE_AddPolyToScene( qhandle_t hShader, int numVerts, const polyVert_t *verts, int numPolys ) {
	srfPoly_t	*poly;
	int			i, j;
	int			fogIndex;
	const fog_t		*fog;
	vec3_t		bounds[2];

	if ( !tr.registered ) {
		return;
	}
#if 0
	if ( !hShader ) {
		ri.Printf( PRINT_WARNING, "WARNING: RE_AddPolyToScene: NULL poly shader\n");
		return;
	}
#endif
	for ( j = 0; j < numPolys; j++ ) {
		if ( r_numpolyverts + numVerts > max_polyverts || r_numpolys >= max_polys ) {
      /*
      NOTE TTimo this was initially a PRINT_WARNING
      but it happens a lot with high fighting scenes and particles
      since we don't plan on changing the const and making for room for those effects
      simply cut this message to developer only
      */
			ri.Printf( PRINT_DEVELOPER, "WARNING: RE_AddPolyToScene: r_max_polys or r_max_polyverts reached\n");
			return;
		}

		poly = &backEndData->polys[r_numpolys];
		poly->surfaceType = SF_POLY;
		poly->hShader = hShader;
		poly->numVerts = numVerts;
		poly->verts = &backEndData->polyVerts[r_numpolyverts];
		
		Com_Memcpy( poly->verts, &verts[numVerts*j], numVerts * sizeof( *verts ) );
#if 0
		if ( glConfig.hardwareType == GLHW_RAGEPRO ) {
			poly->verts->modulate[0] = 255;
			poly->verts->modulate[1] = 255;
			poly->verts->modulate[2] = 255;
			poly->verts->modulate[3] = 255;
		}
#endif
		// done.
		r_numpolys++;
		r_numpolyverts += numVerts;

		// if no world is loaded
		if ( tr.world == NULL ) {
			fogIndex = 0;
		}
		// see if it is in a fog volume
		else if ( tr.world->numfogs == 1 ) {
			fogIndex = 0;
		} else {
			// find which fog volume the poly is in
			VectorCopy( poly->verts[0].xyz, bounds[0] );
			VectorCopy( poly->verts[0].xyz, bounds[1] );
			for ( i = 1 ; i < poly->numVerts ; i++ ) {
				AddPointToBounds( poly->verts[i].xyz, bounds[0], bounds[1] );
			}
			for ( fogIndex = 1 ; fogIndex < tr.world->numfogs ; fogIndex++ ) {
				fog = &tr.world->fogs[fogIndex]; 
				if ( bounds[1][0] >= fog->bounds[0][0]
					&& bounds[1][1] >= fog->bounds[0][1]
					&& bounds[1][2] >= fog->bounds[0][2]
					&& bounds[0][0] <= fog->bounds[1][0]
					&& bounds[0][1] <= fog->bounds[1][1]
					&& bounds[0][2] <= fog->bounds[1][2] ) {
					break;
				}
			}
			if ( fogIndex == tr.world->numfogs ) {
				fogIndex = 0;
			}
		}
		poly->fogIndex = fogIndex;
	}
}


//=================================================================================

static int isnan_fp( const float *f )
{
	uint32_t u = *( (uint32_t*) f );
	u = 0x7F800000 - ( u & 0x7FFFFFFF );
	return (int)( u >> 31 );
}

static float R_DlightSrgbToLinear( float c )
{
	if ( c <= 0.0f ) {
		return 0.0f;
	}
	if ( c <= 0.04045f ) {
		return c / 12.92f;
	}
	if ( c < 1.0f ) {
		return powf( ( c + 0.055f ) / 1.055f, 2.4f );
	}
	return c;
}

static float R_DlightLinearToSrgb( float c )
{
	if ( c <= 0.0f ) {
		return 0.0f;
	}
	if ( c <= 0.0031308f ) {
		return c * 12.92f;
	}
	if ( c < 1.0f ) {
		return 1.055f * powf( c, 1.0f / 2.4f ) - 0.055f;
	}
	return c;
}

static void R_CompressDlightOverbrightGamut( vec3_t linearColor, float amount )
{
	float maxChannel;
	float linearLuma;
	float overbright;
	float chromaScale;

	if ( amount <= 0.0f ) {
		return;
	}

	maxChannel = MAX( linearColor[0], MAX( linearColor[1], linearColor[2] ) );
	if ( maxChannel <= 1.0f ) {
		return;
	}

	linearLuma = LUMA( linearColor[0], linearColor[1], linearColor[2] );
	if ( linearLuma <= 0.0f || maxChannel <= linearLuma ) {
		return;
	}

	overbright = maxChannel - 1.0f;
	chromaScale = 1.0f - amount * ( overbright / ( overbright + 1.0f ) );

	linearColor[0] = linearLuma + ( linearColor[0] - linearLuma ) * chromaScale;
	linearColor[1] = linearLuma + ( linearColor[1] - linearLuma ) * chromaScale;
	linearColor[2] = linearLuma + ( linearColor[2] - linearLuma ) * chromaScale;
}

static void R_ApplyDlightColorControls( float *r, float *g, float *b )
{
	float saturation;
	float overbrightGamut;
	qboolean needsSaturation;
	qboolean needsGamut;
	vec3_t linearColor;
	float linearLuma;

	saturation = r_dlightSaturation ? r_dlightSaturation->value : 1.0f;
	overbrightGamut = r_dlightOverbrightGamut ? r_dlightOverbrightGamut->value : 0.0f;
	needsSaturation = ( saturation != 1.0f );
	needsGamut = ( overbrightGamut > 0.0f && ( *r > 1.0f || *g > 1.0f || *b > 1.0f ) );

	if ( !needsSaturation && !needsGamut ) {
		return;
	}

	linearColor[0] = R_DlightSrgbToLinear( *r );
	linearColor[1] = R_DlightSrgbToLinear( *g );
	linearColor[2] = R_DlightSrgbToLinear( *b );

	if ( needsSaturation ) {
		linearLuma = LUMA( linearColor[0], linearColor[1], linearColor[2] );
		linearColor[0] = LERP( linearLuma, linearColor[0], saturation );
		linearColor[1] = LERP( linearLuma, linearColor[1], saturation );
		linearColor[2] = LERP( linearLuma, linearColor[2], saturation );
	}

	if ( needsGamut ) {
		R_CompressDlightOverbrightGamut( linearColor, overbrightGamut );
	}

	*r = R_DlightLinearToSrgb( linearColor[0] );
	*g = R_DlightLinearToSrgb( linearColor[1] );
	*b = R_DlightLinearToSrgb( linearColor[2] );
}


/*
=====================
RE_AddRefEntityToScene
=====================
*/
void RE_AddRefEntityToScene( const refEntity_t *ent, qboolean intShaderTime ) {
	if ( !tr.registered ) {
		return;
	}
	if ( r_numentities >= MAX_REFENTITIES ) {
		ri.Printf( PRINT_DEVELOPER, "RE_AddRefEntityToScene: Dropping refEntity, reached MAX_REFENTITIES\n" );
		return;
	}
	if ( isnan_fp( &ent->origin[0] ) || isnan_fp( &ent->origin[1] ) || isnan_fp( &ent->origin[2] ) ) {
		static qboolean first_time = qtrue;
		if ( first_time ) {
			first_time = qfalse;
			ri.Printf( PRINT_WARNING, "RE_AddRefEntityToScene passed a refEntity which has an origin with a NaN component\n" );
		}
		return;
	}
	if ( (unsigned)ent->reType >= RT_MAX_REF_ENTITY_TYPE ) {
		ri.Error( ERR_DROP, "RE_AddRefEntityToScene: bad reType %i", ent->reType );
	}

	R_TrackMuzzleFlashLightEntity( ent );
	backEndData->entities[r_numentities].e = *ent;
	backEndData->entities[r_numentities].lightingCalculated = qfalse;
	backEndData->entities[r_numentities].intShaderTime = intShaderTime;

	r_numentities++;
}


/*
=====================
RE_AddDynamicLightToSceneWithShadowEligibility
=====================
*/
static void RE_AddDynamicLightToSceneWithShadowEligibility( const vec3_t org,
	float intensity, float r, float g, float b, int additive,
	qboolean shadowEligible ) {
	dlight_t	*dl;
#ifdef USE_PMLIGHT
	float		shadowProjectionFar = 0.0f;
#endif

	(void)shadowEligible;
	if ( !tr.registered ) {
		return;
	}
	if ( r_numdlights >= ARRAY_LEN( backEndData->dlights ) ) {
		return;
	}
	if ( intensity <= 0 ) {
		return;
	}
	// these cards don't have the correct blend mode
	if ( glConfig.hardwareType == GLHW_RIVA128 || glConfig.hardwareType == GLHW_PERMEDIA2 ) {
		return;
	}
#ifdef USE_PMLIGHT
#ifdef USE_LEGACY_DLIGHTS
	if ( R_GetDlightMode() )
#endif
	{
		r *= r_dlightIntensity->value;
		g *= r_dlightIntensity->value;
		b *= r_dlightIntensity->value;
		shadowProjectionFar =
			R_DlightShadowProjectionFarForRadius( intensity, r_dlightScale->value );
		intensity *= r_dlightScale->value;
	}
#endif

	R_ApplyDlightColorControls( &r, &g, &b );

	dl = &backEndData->dlights[r_numdlights++];
	VectorCopy( org, dl->origin );
	dl->radius = intensity;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
	dl->additive = additive;
	dl->linear = qfalse;
#ifdef USE_PMLIGHT
	dl->shadowEligible = shadowEligible;
	dl->shadowPlanned = qfalse;
	dl->shadowIndex = -1;
	dl->shadowAtlasBaseFace = -1;
	dl->shadowAtlasFaceSize = 0;
	Com_Memset( dl->shadowAtlasX, 0, sizeof( dl->shadowAtlasX ) );
	Com_Memset( dl->shadowAtlasY, 0, sizeof( dl->shadowAtlasY ) );
	dl->shadowReceiverCount = 0;
	dl->shadowPriority = 0.0f;
	dl->shadowPriorityMultiplier = 1.0f;
	dl->shadowProjectionFar = shadowProjectionFar;
	dl->shadowSpotSource = -1;
	dl->shadowSpotSourceIndex = -1;
#endif
}

static void RE_AddDynamicLightToScene( const vec3_t org, float intensity,
	float r, float g, float b, int additive ) {
	RE_AddDynamicLightToSceneWithShadowEligibility( org, intensity, r, g, b,
		additive, qtrue );
}


/*
=====================
RE_AddLinearLightToScene
=====================
*/
void RE_AddLinearLightToScene( const vec3_t start, const vec3_t end, float intensity, float r, float g, float b  ) {
	dlight_t	*dl;
	if ( VectorCompare( start, end ) ) {
		RE_AddDynamicLightToScene( start, intensity, r, g, b, 0 );
		return;
	}
	if ( !tr.registered ) {
		return;
	}
	if ( r_numdlights >= ARRAY_LEN( backEndData->dlights ) ) {
		return;
	}
	if ( intensity <= 0 ) {
		return;
	}
#ifdef USE_PMLIGHT
#ifdef USE_LEGACY_DLIGHTS
	if ( R_GetDlightMode() )
#endif
	{
		r *= r_dlightIntensity->value;
		g *= r_dlightIntensity->value;
		b *= r_dlightIntensity->value;
		intensity *= r_dlightScale->value;
	}
#endif

	R_ApplyDlightColorControls( &r, &g, &b );

	dl = &backEndData->dlights[ r_numdlights++ ];
	VectorCopy( start, dl->origin );
	VectorCopy( end, dl->origin2 );
	dl->radius = intensity;
	dl->color[0] = r;
	dl->color[1] = g;
	dl->color[2] = b;
	dl->additive = 0;
	dl->linear = qtrue;
#ifdef USE_PMLIGHT
	dl->shadowEligible = qtrue;
	dl->shadowPlanned = qfalse;
	dl->shadowIndex = -1;
	dl->shadowAtlasBaseFace = -1;
	dl->shadowAtlasFaceSize = 0;
	Com_Memset( dl->shadowAtlasX, 0, sizeof( dl->shadowAtlasX ) );
	Com_Memset( dl->shadowAtlasY, 0, sizeof( dl->shadowAtlasY ) );
	dl->shadowReceiverCount = 0;
	dl->shadowPriority = 0.0f;
	dl->shadowPriorityMultiplier = 1.0f;
	dl->shadowProjectionFar = 0.0f;
	dl->shadowSpotSource = -1;
	dl->shadowSpotSourceIndex = -1;
#endif
}


#ifdef USE_PMLIGHT
static qboolean R_LightCandidateVisibleInPVS( const refdef_t *fd, const vec3_t origin,
	float radius, int leafCluster, int leafArea )
{
	vec3_t sample;
	float sampleRadius;
	int axis;

	if ( !tr.world || ( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		return qtrue;
	}
	if ( leafCluster >= 0 ) {
		if ( R_LeafClusterInCurrentPVS( fd->vieworg, leafCluster, leafArea ) ) {
			return qtrue;
		}
	} else if ( R_PointInCurrentPVS( fd->vieworg, origin ) ) {
		return qtrue;
	}
	if ( radius <= 32.0f ) {
		return qfalse;
	}
	if ( radius > 1024.0f ) {
		return qtrue;
	}

	sampleRadius = Com_Clamp( 32.0f, 512.0f, radius );
	for ( axis = 0; axis < 3; axis++ ) {
		VectorCopy( origin, sample );
		sample[axis] += sampleRadius;
		if ( R_PointInCurrentPVS( fd->vieworg, sample ) ) {
			return qtrue;
		}
		sample[axis] -= sampleRadius * 2.0f;
		if ( R_PointInCurrentPVS( fd->vieworg, sample ) ) {
			return qtrue;
		}
	}

	return qfalse;
}

static float R_StaticMapLightScenePriority( const mapLightDef_t *light, const refdef_t *fd )
{
	vec3_t delta;
	float brightness;
	float dist2;
	float radius2;
	float directionalWeight;

	brightness = light->color[0];
	if ( light->color[1] > brightness ) {
		brightness = light->color[1];
	}
	if ( light->color[2] > brightness ) {
		brightness = light->color[2];
	}
	if ( brightness <= 0.0f || light->radius <= 0.0f || light->designerPriority <= 0.0f ) {
		return 0.0f;
	}

	VectorSubtract( light->origin, fd->vieworg, delta );
	dist2 = DotProduct( delta, delta );
	radius2 = Square( light->radius );
	directionalWeight = 1.0f;
	if ( light->type == MAP_LIGHT_SPOT ) {
		vec3_t toView;
		float facing;

		VectorSubtract( fd->vieworg, light->origin, toView );
		if ( VectorNormalize2( toView, toView ) > 1.0f ) {
			facing = DotProduct( light->direction, toView );
			directionalWeight = Com_Clamp( 0.15f, 1.0f, 0.35f + 0.65f * facing );
		}
	}

	return brightness * directionalWeight * light->designerPriority * light->intensity * radius2 /
		( dist2 + radius2 + 1.0f );
}

static void R_StaticMapLightSpotEnd( const mapLightDef_t *light, vec3_t end )
{
	VectorMA( light->origin, light->radius, light->direction, end );
}

static qboolean R_StaticMapLightVisibleInPVS( const refdef_t *fd, const mapLightDef_t *light )
{
	if ( R_LightCandidateVisibleInPVS( fd, light->origin, light->radius,
		light->leafCluster, light->leafArea ) ) {
		return qtrue;
	}
	if ( light->type == MAP_LIGHT_SPOT ) {
		vec3_t end;

		R_StaticMapLightSpotEnd( light, end );
		if ( R_LightCandidateVisibleInPVS( fd, end, light->radius * 0.25f, -1, -1 ) ) {
			return qtrue;
		}
	}

	return qfalse;
}

static float R_SurfaceLightProxyHemisphereWeight( const surfaceLightProxy_t *proxy, const refdef_t *fd )
{
	vec3_t toView;
	float facing;

	if ( proxy->area <= Square( 64.0f ) ) {
		return 1.0f;
	}

	VectorSubtract( fd->vieworg, proxy->origin, toView );
	if ( VectorNormalize2( toView, toView ) <= 1.0f ) {
		return 1.0f;
	}

	facing = DotProduct( proxy->normal, toView );
	if ( facing <= -0.35f ) {
		return 0.05f;
	}

	return Com_Clamp( 0.15f, 1.0f, 0.35f + 0.65f * facing );
}

static float R_SurfaceLightProxyViewWeight( const surfaceLightProxy_t *proxy, const refdef_t *fd,
	float tanHalfFovX, float tanHalfFovY )
{
	vec3_t delta;
	float forward;
	float side;
	float up;
	float radius;
	float sideLimit;
	float upLimit;
	float sideRatio;
	float upRatio;
	float edgeRatio;

	VectorSubtract( proxy->origin, fd->vieworg, delta );
	forward = DotProduct( delta, fd->viewaxis[0] );
	radius = Com_Clamp( 32.0f, 1024.0f, proxy->radius );
	if ( forward <= -radius ) {
		return 0.08f;
	}
	if ( forward <= radius ) {
		return 0.75f;
	}

	side = fabsf( DotProduct( delta, fd->viewaxis[1] ) );
	up = fabsf( DotProduct( delta, fd->viewaxis[2] ) );
	sideLimit = forward * tanHalfFovX + radius;
	upLimit = forward * tanHalfFovY + radius;
	if ( sideLimit <= 1.0f || upLimit <= 1.0f ) {
		return 1.0f;
	}

	sideRatio = side / sideLimit;
	upRatio = up / upLimit;
	edgeRatio = MAX( sideRatio, upRatio );
	if ( edgeRatio <= 1.0f ) {
		return 1.0f;
	}
	if ( edgeRatio >= 2.0f ) {
		return 0.12f;
	}

	return Com_Clamp( 0.12f, 1.0f, 1.0f - ( edgeRatio - 1.0f ) * 0.65f );
}

static void R_ResetStaticMapLightFrameCounters( void )
{
	tr.staticMapLights.promotedThisFrame = 0;
	tr.staticMapLights.shadowEligibleThisFrame = 0;
	tr.staticMapLights.skippedDisabledThisFrame = 0;
	tr.staticMapLights.skippedPVSThisFrame = 0;
	tr.staticMapLights.skippedBudgetThisFrame = 0;
}

static void R_AddStaticMapLightsToScene( const refdef_t *fd )
{
	qboolean selected[MAX_STATIC_MAP_LIGHTS];
	qboolean visible[MAX_STATIC_MAP_LIGHTS];
	float priorities[MAX_STATIC_MAP_LIGHTS];
	int budget;
	int shadowBudget;
	int shadowPromoted;
	int visibleCount;
	int pass;
	int i;

	R_ResetStaticMapLightFrameCounters();

	if ( !tr.staticMapLights.loaded || tr.staticMapLights.parseFailed || tr.staticMapLights.count <= 0 ) {
		return;
	}
	if ( !r_staticLights || !r_staticLights->integer || ( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		tr.staticMapLights.skippedDisabledThisFrame = tr.staticMapLights.count;
		return;
	}

	budget = r_staticLightMaxLights ? r_staticLightMaxLights->integer : 8;
	budget = (int)Com_Clamp( 0.0f, (float)MAX_DLIGHTS, (float)budget );
	if ( budget > (int)ARRAY_LEN( backEndData->dlights ) - r_numdlights ) {
		budget = (int)ARRAY_LEN( backEndData->dlights ) - r_numdlights;
	}
	if ( budget <= 0 ) {
		tr.staticMapLights.skippedBudgetThisFrame = tr.staticMapLights.count;
		return;
	}

	shadowBudget = r_staticLightShadowMaxLights ? r_staticLightShadowMaxLights->integer : 2;
	shadowBudget = (int)Com_Clamp( 0.0f, (float)MAX_DLIGHTS, (float)shadowBudget );
	Com_Memset( selected, 0, sizeof( selected ) );
	Com_Memset( visible, 0, sizeof( visible ) );
	shadowPromoted = 0;
	visibleCount = 0;

	for ( i = 0; i < tr.staticMapLights.count; i++ ) {
		const mapLightDef_t *light = &tr.staticMapLights.lights[i];

		if ( R_StaticMapLightVisibleInPVS( fd, light ) ) {
			visible[i] = qtrue;
			visibleCount++;
			// priority only depends on (light, refdef), so compute it once
			// instead of re-evaluating per selection pass
			priorities[i] = R_StaticMapLightScenePriority( light, fd );
		} else {
			tr.staticMapLights.skippedPVSThisFrame++;
		}
	}
	if ( visibleCount <= 0 ) {
		return;
	}

	for ( pass = 0; pass < budget; pass++ ) {
		const mapLightDef_t *light;
		dlight_t *dl;
		float bestPriority;
		int bestIndex;
		int before;

		bestPriority = 0.0f;
		bestIndex = -1;
		for ( i = 0; i < tr.staticMapLights.count; i++ ) {
			if ( selected[i] || !visible[i] ) {
				continue;
			}
			if ( priorities[i] > bestPriority ) {
				bestPriority = priorities[i];
				bestIndex = i;
			}
		}

		if ( bestIndex < 0 || bestPriority <= 0.0f || r_numdlights >= ARRAY_LEN( backEndData->dlights ) ) {
			break;
		}

		selected[bestIndex] = qtrue;
		light = &tr.staticMapLights.lights[bestIndex];
		before = r_numdlights;
		if ( light->type == MAP_LIGHT_SPOT ) {
			vec3_t end;

			R_StaticMapLightSpotEnd( light, end );
			RE_AddLinearLightToScene( light->origin, end, light->radius,
				light->color[0], light->color[1], light->color[2] );
		} else {
			RE_AddDynamicLightToScene( light->origin, light->radius,
				light->color[0], light->color[1], light->color[2], qfalse );
		}
		if ( r_numdlights <= before ) {
			continue;
		}

		dl = &backEndData->dlights[before];
		if ( !dl->linear ) {
			dl->shadowProjectionFar =
				R_DlightShadowExactProjectionFarForRadius( dl->radius );
		}
		if ( light->type == MAP_LIGHT_SPOT ) {
			dl->shadowSpotSource = SHADOW_SPOT_SOURCE_STATIC_MAP;
			dl->shadowSpotSourceIndex = bestIndex;
		}
		dl->shadowEligible = ( light->type == MAP_LIGHT_POINT &&
			r_staticLightShadows && r_staticLightShadows->integer &&
			light->castsShadows && shadowPromoted < shadowBudget ) ? qtrue : qfalse;
		dl->shadowPriorityMultiplier = light->designerPriority *
			Com_Clamp( 0.25f, 4.0f, light->intensity / light->radius );

		tr.staticMapLights.promotedThisFrame++;
		if ( dl->shadowEligible ) {
			tr.staticMapLights.shadowEligibleThisFrame++;
			shadowPromoted++;
		}
	}

	if ( visibleCount > tr.staticMapLights.promotedThisFrame ) {
		tr.staticMapLights.skippedBudgetThisFrame =
			visibleCount - tr.staticMapLights.promotedThisFrame;
	}
}

static float R_SurfaceLightProxyScenePriority( const surfaceLightProxy_t *proxy, const refdef_t *fd,
	float tanHalfFovX, float tanHalfFovY )
{
	vec3_t delta;
	float brightness;
	float dist2;
	float radius2;
	float hemisphereWeight;
	float viewWeight;

	brightness = proxy->color[0];
	if ( proxy->color[1] > brightness ) {
		brightness = proxy->color[1];
	}
	if ( proxy->color[2] > brightness ) {
		brightness = proxy->color[2];
	}
	if ( brightness <= 0.0f || proxy->radius <= 0.0f ||
		proxy->intensity <= 0.0f || proxy->designerPriority <= 0.0f ) {
		return 0.0f;
	}
	hemisphereWeight = R_SurfaceLightProxyHemisphereWeight( proxy, fd );
	if ( hemisphereWeight <= 0.0f ) {
		return 0.0f;
	}
	viewWeight = R_SurfaceLightProxyViewWeight( proxy, fd, tanHalfFovX, tanHalfFovY );
	if ( viewWeight <= 0.0f ) {
		return 0.0f;
	}

	VectorSubtract( proxy->origin, fd->vieworg, delta );
	dist2 = DotProduct( delta, delta );
	radius2 = Square( proxy->radius );

	return brightness * proxy->designerPriority * proxy->intensity * hemisphereWeight * viewWeight * radius2 /
		( dist2 + radius2 + 1.0f );
}

static void R_ResetSurfaceLightProxyFrameCounters( void )
{
	tr.surfaceLightProxies.promotedThisFrame = 0;
	tr.surfaceLightProxies.shadowEligibleThisFrame = 0;
	tr.surfaceLightProxies.spotShadowDeferredThisFrame = 0;
	tr.surfaceLightProxies.skippedDisabledThisFrame = 0;
	tr.surfaceLightProxies.skippedPVSThisFrame = 0;
	tr.surfaceLightProxies.skippedBudgetThisFrame = 0;
}

static void R_SurfaceLightProxySpotEnd( const surfaceLightProxy_t *proxy, vec3_t end )
{
	VectorMA( proxy->origin, proxy->radius, proxy->normal, end );
}

static void R_AddSurfaceLightProxiesToScene( const refdef_t *fd )
{
	qboolean selected[MAX_SURFACELIGHT_PROXIES];
	qboolean visible[MAX_SURFACELIGHT_PROXIES];
	float priorities[MAX_SURFACELIGHT_PROXIES];
	int budget;
	int shadowBudget;
	int shadowPromoted;
	int visibleCount;
	int pass;
	int i;
	float tanHalfFovX;
	float tanHalfFovY;

	R_ResetSurfaceLightProxyFrameCounters();

	if ( !tr.surfaceLightProxies.built || tr.surfaceLightProxies.count <= 0 ) {
		return;
	}
	if ( !r_surfaceLightProxies || !r_surfaceLightProxies->integer || ( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		tr.surfaceLightProxies.skippedDisabledThisFrame = tr.surfaceLightProxies.count;
		return;
	}

	budget = r_surfaceLightProxyMaxLights ? r_surfaceLightProxyMaxLights->integer : 4;
	budget = (int)Com_Clamp( 0.0f, (float)MAX_DLIGHTS, (float)budget );
	if ( budget > (int)ARRAY_LEN( backEndData->dlights ) - r_numdlights ) {
		budget = (int)ARRAY_LEN( backEndData->dlights ) - r_numdlights;
	}
	if ( budget <= 0 ) {
		tr.surfaceLightProxies.skippedBudgetThisFrame = tr.surfaceLightProxies.count;
		return;
	}

	shadowBudget = r_surfaceLightProxyShadowMaxLights ? r_surfaceLightProxyShadowMaxLights->integer : 1;
	shadowBudget = (int)Com_Clamp( 0.0f, (float)MAX_DLIGHTS, (float)shadowBudget );
	tanHalfFovX = tanf( Com_Clamp( 30.0f, 160.0f, fd->fov_x ) * M_PI / 360.0f );
	tanHalfFovY = tanf( Com_Clamp( 30.0f, 160.0f, fd->fov_y ) * M_PI / 360.0f );
	Com_Memset( selected, 0, sizeof( selected ) );
	Com_Memset( visible, 0, sizeof( visible ) );
	shadowPromoted = 0;
	visibleCount = 0;

	for ( i = 0; i < tr.surfaceLightProxies.count; i++ ) {
		const surfaceLightProxy_t *proxy = &tr.surfaceLightProxies.proxies[i];

		if ( R_LightCandidateVisibleInPVS( fd, proxy->origin, proxy->radius,
			proxy->leafCluster, proxy->leafArea ) ) {
			visible[i] = qtrue;
			visibleCount++;
			// priority only depends on (proxy, refdef), so compute it once
			// instead of re-evaluating per selection pass
			priorities[i] = R_SurfaceLightProxyScenePriority( proxy, fd,
				tanHalfFovX, tanHalfFovY );
		} else {
			tr.surfaceLightProxies.skippedPVSThisFrame++;
		}
	}
	if ( visibleCount <= 0 ) {
		return;
	}

	for ( pass = 0; pass < budget; pass++ ) {
		const surfaceLightProxy_t *proxy;
		dlight_t *dl;
		float bestPriority;
		float hemisphereWeight;
		float viewWeight;
		int bestIndex;
		int before;

		bestPriority = 0.0f;
		bestIndex = -1;
		for ( i = 0; i < tr.surfaceLightProxies.count; i++ ) {
			if ( selected[i] || !visible[i] ) {
				continue;
			}
			if ( priorities[i] > bestPriority ) {
				bestPriority = priorities[i];
				bestIndex = i;
			}
		}

		if ( bestIndex < 0 || bestPriority <= 0.0f || r_numdlights >= ARRAY_LEN( backEndData->dlights ) ) {
			break;
		}

		selected[bestIndex] = qtrue;
		proxy = &tr.surfaceLightProxies.proxies[bestIndex];
		before = r_numdlights;
		if ( proxy->projection == SURFACE_LIGHT_PROXY_SPOT ) {
			vec3_t end;

			R_SurfaceLightProxySpotEnd( proxy, end );
			RE_AddLinearLightToScene( proxy->origin, end, proxy->radius,
				proxy->color[0], proxy->color[1], proxy->color[2] );
		} else {
			RE_AddDynamicLightToScene( proxy->origin, proxy->radius,
				proxy->color[0], proxy->color[1], proxy->color[2], qfalse );
		}
		if ( r_numdlights <= before ) {
			continue;
		}

		dl = &backEndData->dlights[before];
		if ( !dl->linear ) {
			dl->shadowProjectionFar =
				R_DlightShadowExactProjectionFarForRadius( dl->radius );
		}
		if ( proxy->projection == SURFACE_LIGHT_PROXY_SPOT ) {
			dl->shadowSpotSource = SHADOW_SPOT_SOURCE_SURFACELIGHT_PROXY;
			dl->shadowSpotSourceIndex = bestIndex;
		}
		hemisphereWeight = R_SurfaceLightProxyHemisphereWeight( proxy, fd );
		viewWeight = R_SurfaceLightProxyViewWeight( proxy, fd, tanHalfFovX, tanHalfFovY );
		dl->shadowEligible = qfalse;
		if ( r_surfaceLightProxyShadows && r_surfaceLightProxyShadows->integer &&
			proxy->castsShadows && hemisphereWeight >= 0.25f && viewWeight >= 0.25f &&
			shadowPromoted < shadowBudget ) {
			if ( proxy->projection == SURFACE_LIGHT_PROXY_POINT ) {
				dl->shadowEligible = qtrue;
			} else {
				tr.surfaceLightProxies.spotShadowDeferredThisFrame++;
			}
		}
		dl->shadowPriorityMultiplier = proxy->designerPriority *
			Com_Clamp( 0.25f, 4.0f, proxy->intensity / 400.0f ) *
			Com_Clamp( 0.25f, 1.0f, hemisphereWeight ) *
			Com_Clamp( 0.25f, 1.0f, viewWeight );

		tr.surfaceLightProxies.promotedThisFrame++;
		if ( dl->shadowEligible ) {
			tr.surfaceLightProxies.shadowEligibleThisFrame++;
			shadowPromoted++;
		}
	}

	if ( visibleCount > tr.surfaceLightProxies.promotedThisFrame ) {
		tr.surfaceLightProxies.skippedBudgetThisFrame =
			visibleCount - tr.surfaceLightProxies.promotedThisFrame;
	}
}

typedef struct {
	qboolean active;
	int count;
	float intensity;
	float distance;
	float height;
	int endTime;
} dlightTestState_t;

static dlightTestState_t r_dlightTestState;


static void R_DlightTestStatus( void )
{
	ri.Printf( PRINT_ALL,
		"r_dlightTest: %s count:%i intensity:%.1f distance:%.1f height:%.1f %s\n",
		r_dlightTestState.active ? "on" : "off",
		r_dlightTestState.count,
		r_dlightTestState.intensity,
		r_dlightTestState.distance,
		r_dlightTestState.height,
		r_dlightTestState.endTime ? "timed" : "persistent" );
	ri.Printf( PRINT_ALL,
		"usage: r_dlightTest <count> [intensity] [distance] [height] [seconds], or r_dlightTest off\n" );
}


void R_DlightTest_f( void )
{
	int argc;
	const char *arg;
	float seconds;

	argc = ri.Cmd_Argc();
	if ( argc <= 1 ) {
		R_DlightTestStatus();
		return;
	}

	arg = ri.Cmd_Argv( 1 );
	if ( !Q_stricmp( arg, "off" ) || !Q_stricmp( arg, "0" ) ) {
		Com_Memset( &r_dlightTestState, 0, sizeof( r_dlightTestState ) );
		ri.Printf( PRINT_ALL, "r_dlightTest disabled\n" );
		return;
	}

	r_dlightTestState.active = qtrue;
	r_dlightTestState.count = (int)Com_Clamp( 1.0f, (float)MAX_DLIGHTS, (float)atoi( arg ) );
	r_dlightTestState.intensity = ( argc > 2 ) ? Q_atof( ri.Cmd_Argv( 2 ) ) : 320.0f;
	r_dlightTestState.distance = ( argc > 3 ) ? Q_atof( ri.Cmd_Argv( 3 ) ) : 192.0f;
	r_dlightTestState.height = ( argc > 4 ) ? Q_atof( ri.Cmd_Argv( 4 ) ) : 24.0f;
	seconds = ( argc > 5 ) ? Q_atof( ri.Cmd_Argv( 5 ) ) : 15.0f;

	r_dlightTestState.intensity = Com_Clamp( 1.0f, 2048.0f, r_dlightTestState.intensity );
	r_dlightTestState.distance = Com_Clamp( 0.0f, 2048.0f, r_dlightTestState.distance );
	r_dlightTestState.height = Com_Clamp( -1024.0f, 1024.0f, r_dlightTestState.height );
	r_dlightTestState.endTime = ( seconds > 0.0f ) ?
		ri.Milliseconds() + (int)( seconds * 1000.0f ) : 0;

	R_DlightTestStatus();
}


static void R_AddDlightTestLightsToScene( const refdef_t *fd )
{
	static const vec3_t colors[] = {
		{ 1.00f, 0.30f, 0.18f },
		{ 0.20f, 0.65f, 1.00f },
		{ 0.35f, 1.00f, 0.35f },
		{ 1.00f, 0.85f, 0.25f },
		{ 0.90f, 0.35f, 1.00f },
		{ 0.35f, 1.00f, 0.95f }
	};
	vec3_t base;
	float phase;
	int count;
	int i;

	if ( !r_dlightTestState.active || !fd || ( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		return;
	}
	if ( r_dlightTestState.endTime && ri.Milliseconds() > r_dlightTestState.endTime ) {
		Com_Memset( &r_dlightTestState, 0, sizeof( r_dlightTestState ) );
		ri.Printf( PRINT_ALL, "r_dlightTest finished\n" );
		return;
	}

	count = (int)Com_Clamp( 1.0f, (float)MAX_DLIGHTS, (float)r_dlightTestState.count );
	phase = fd->time * 0.001f;
	VectorMA( fd->vieworg, r_dlightTestState.distance, fd->viewaxis[0], base );

	for ( i = 0; i < count && r_numdlights < ARRAY_LEN( backEndData->dlights ); i++ ) {
		const float angle = phase * 0.7f + ( 6.28318530718f * i ) / (float)count;
		const float side = cosf( angle ) * r_dlightTestState.distance * 0.75f;
		const float lift = r_dlightTestState.height + sinf( angle ) * r_dlightTestState.distance * 0.35f;
		const vec_t *color = colors[ i % ARRAY_LEN( colors ) ];
		vec3_t org;

		VectorMA( base, side, fd->viewaxis[1], org );
		VectorMA( org, lift, fd->viewaxis[2], org );
		RE_AddDynamicLightToScene( org, r_dlightTestState.intensity,
			color[0], color[1], color[2], qfalse );
	}
}
#endif



/*
=====================
RE_AddLightToScene

=====================
*/
void RE_AddLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	vec3_t adjustedOrigin;
	qboolean muzzleFlash = R_PrepareMuzzleFlashLight( org, adjustedOrigin );

	RE_AddDynamicLightToSceneWithShadowEligibility( adjustedOrigin, intensity,
		r, g, b, qfalse,
		R_MuzzleFlashLightShadowEligible( muzzleFlash ) );
}


/*
=====================
RE_AddAdditiveLightToScene

=====================
*/
void RE_AddAdditiveLightToScene( const vec3_t org, float intensity, float r, float g, float b ) {
	vec3_t adjustedOrigin;
	qboolean muzzleFlash = R_PrepareMuzzleFlashLight( org, adjustedOrigin );

	RE_AddDynamicLightToSceneWithShadowEligibility( adjustedOrigin, intensity,
		r, g, b, qtrue,
		R_MuzzleFlashLightShadowEligible( muzzleFlash ) );
}

/*
=====================
AdvertisementBridge_UpdateLoadingViewParameters

Mirrors the retail no-argument renderer export used by loading-screen updates.
=====================
*/
void AdvertisementBridge_UpdateLoadingViewParameters( void ) {
	if ( ri.AdvertisementBridge_RefreshLoadingViewParameters ) {
		ri.AdvertisementBridge_RefreshLoadingViewParameters();
	}
}

static void R_CancelScreenshotCubemap( const char *reason )
{
	tr.cubemapDrawSurfLimit = 0;
	tr.cubemapDrawSurfLimitHit = qfalse;
	backEnd.screenshotCubeFailed = qtrue;
	backEnd.screenshotCubeFrontPending = qfalse;
	backEnd.screenshotCubeActive = qfalse;
	ri.Cvar_Set( "cl_captureActive", "0" );

	if ( reason && reason[0] ) {
		ri.Printf( PRINT_WARNING, "WARNING: screenshot cubemap cancelled: %s\n", reason );
	}
}

static void R_RenderScreenshotCubemapViews( void )
{
	trRefdef_t originalRefdef;
	trRefdef_t cubeRefdef;
	viewParms_t cubeParms;
	vec3_t baseAxis[3];
	int faceSize;
	int captureSize;
	int captureX;
	int captureY;
	int i;

	originalRefdef = tr.refdef;
	faceSize = MIN( glConfig.vidWidth, glConfig.vidHeight );
	captureSize = MIN( gls.captureWidth, gls.captureHeight );
	if ( faceSize <= 0 || captureSize <= 0 ) {
		R_CancelScreenshotCubemap( "the capture target has invalid dimensions" );
		return;
	}
	captureX = 0;
	captureY = 0;

	cubeRefdef = tr.refdef;
	cubeRefdef.x = 0;
	cubeRefdef.y = glConfig.vidHeight - faceSize;
	cubeRefdef.width = faceSize;
	cubeRefdef.height = faceSize;
	cubeRefdef.fov_x = 90.0f;
	cubeRefdef.fov_y = 90.0f;

	AxisCopy( tr.refdef.viewaxis, baseAxis );

	for ( i = 1; i < 6; i++ ) {
		const int facesRemaining = 7 - i;
		const int surfacesRemaining = MAX_DRAWSURFS - cubeRefdef.numDrawSurfs;

		if ( surfacesRemaining < facesRemaining ) {
			tr.refdef = originalRefdef;
			R_CancelScreenshotCubemap( "the frame draw-surface buffer is full" );
			return;
		}

		tr.cubemapDrawSurfLimit = cubeRefdef.numDrawSurfs + surfacesRemaining / facesRemaining;
		tr.cubemapDrawSurfLimitHit = qfalse;
		tr.frameSceneNum++;
		tr.sceneCount++;
		tr.refdef = cubeRefdef;
		tr.refdef.rdflags |= RDF_NOFIRSTPERSON;

		R_CubemapFaceAxis( baseAxis, i, tr.refdef.viewaxis );

		Com_Memset( &cubeParms, 0, sizeof( cubeParms ) );
		cubeParms.viewportX = cubeRefdef.x;
		cubeParms.viewportY = glConfig.vidHeight - ( cubeRefdef.y + cubeRefdef.height );
		cubeParms.viewportWidth = cubeRefdef.width;
		cubeParms.viewportHeight = cubeRefdef.height;
		cubeParms.scissorX = cubeParms.viewportX;
		cubeParms.scissorY = cubeParms.viewportY;
		cubeParms.scissorWidth = cubeParms.viewportWidth;
		cubeParms.scissorHeight = cubeParms.viewportHeight;
		cubeParms.portalView = PV_NONE;
		cubeParms.fovX = 90.0f;
		cubeParms.fovY = 90.0f;
		cubeParms.stereoFrame = tr.refdef.stereoFrame;
#ifdef USE_PMLIGHT
		cubeParms.dlights = tr.refdef.dlights;
		cubeParms.num_dlights = tr.refdef.num_dlights;
#endif
		VectorCopy( tr.refdef.vieworg, cubeParms.or.origin );
		VectorCopy( tr.refdef.viewaxis[0], cubeParms.or.axis[0] );
		VectorCopy( tr.refdef.viewaxis[1], cubeParms.or.axis[1] );
		VectorCopy( tr.refdef.viewaxis[2], cubeParms.or.axis[2] );
		VectorCopy( tr.refdef.vieworg, cubeParms.pvsOrigin );

		R_RenderView( &cubeParms );
		if ( tr.cubemapDrawSurfLimitHit || backEnd.screenshotCubeFailed ||
			!R_AddScreenshotCmd( captureX, captureY, captureSize, captureSize,
				backEnd.screenshotCubeFormat, backEnd.screenshotCubeNames[i],
				backEnd.screenshotCubeSilent, qfalse, i ) ) {
			tr.refdef = originalRefdef;
			R_CancelScreenshotCubemap( tr.cubemapDrawSurfLimitHit ?
				"a face exceeded its safe draw-surface budget" :
				"the render-command buffer is full" );
			return;
		}

		VectorCopy( tr.refdef.vieworg, backEnd.screenshotCubeVieworg[i] );
		AxisCopy( tr.refdef.viewaxis, backEnd.screenshotCubeViewaxis[i] );

		/*
		=============
		R_RenderScreenshotCubemapViews draw surf carry-over

		Keep per-view draw surface counts monotonic so queued draw commands for
		each cubemap face reference stable, non-overlapping ranges.
		=============
		*/
		cubeRefdef.numDrawSurfs = tr.refdef.numDrawSurfs;
#ifdef USE_PMLIGHT
		cubeRefdef.numLitSurfs = tr.refdef.numLitSurfs;
#endif
	}

	tr.refdef = cubeRefdef;
	AxisCopy( baseAxis, tr.refdef.viewaxis );
	VectorCopy( tr.refdef.vieworg, backEnd.screenshotCubeVieworg[0] );
	AxisCopy( tr.refdef.viewaxis, backEnd.screenshotCubeViewaxis[0] );
	backEnd.screenshotCubeFrontX = captureX;
	backEnd.screenshotCubeFrontY = captureY;
	backEnd.screenshotCubeFrontSize = captureSize;
	backEnd.screenshotCubeFrontPending = qtrue;
	tr.cubemapDrawSurfLimit = MAX_DRAWSURFS;
	tr.cubemapDrawSurfLimitHit = qfalse;
}


/*
@@@@@@@@@@@@@@@@@@@@@
RE_RenderScene

Draw a 3D view into a part of the window, then return
to 2D drawing.

Rendering a scene may require multiple views to be rendered
to handle mirrors,
@@@@@@@@@@@@@@@@@@@@@
*/
void RE_RenderScene( const refdef_t *fd ) {
	viewParms_t		parms;
	int				startTime;

	if ( !tr.registered ) {
		return;
	}

	if ( r_norefresh->integer ) {
		return;
	}

	startTime = ri.Milliseconds();

	if (!tr.world && !( fd->rdflags & RDF_NOWORLDMODEL ) ) {
		ri.Error (ERR_DROP, "R_RenderScene: NULL worldmodel");
	}

	Com_Memcpy( tr.refdef.text, fd->text, sizeof( tr.refdef.text ) );

	tr.refdef.x = fd->x;
	tr.refdef.y = fd->y;
	tr.refdef.width = fd->width;
	tr.refdef.height = fd->height;
	tr.refdef.fov_x = fd->fov_x;
	tr.refdef.fov_y = fd->fov_y;

	VectorCopy( fd->vieworg, tr.refdef.vieworg );
	VectorCopy( fd->viewaxis[0], tr.refdef.viewaxis[0] );
	VectorCopy( fd->viewaxis[1], tr.refdef.viewaxis[1] );
	VectorCopy( fd->viewaxis[2], tr.refdef.viewaxis[2] );

	tr.refdef.time = fd->time;
	tr.refdef.rdflags = fd->rdflags;

	// copy the areamask data over and note if it has changed, which
	// will force a reset of the visible leafs even if the view hasn't moved
	tr.refdef.areamaskModified = qfalse;
	if ( ! (tr.refdef.rdflags & RDF_NOWORLDMODEL) ) {
		int		areaDiff;
		int		i;

		// compare the area bits
		areaDiff = 0;
		for ( i = 0; i < MAX_MAP_AREA_BYTES/sizeof(int); i++ ) {
			areaDiff |= ((int *)tr.refdef.areamask)[i] ^ ((int *)fd->areamask)[i];
			((int *)tr.refdef.areamask)[i] = ((int *)fd->areamask)[i];
		}

		if ( areaDiff ) {
			// a door just opened or something
			tr.refdef.areamaskModified = qtrue;
		}
	}

#ifdef USE_PMLIGHT
	R_AddDlightTestLightsToScene( fd );
	R_AddStaticMapLightsToScene( fd );
	R_AddSurfaceLightProxiesToScene( fd );
#endif


	// derived info

	tr.refdef.floatTime = (double)tr.refdef.time * 0.001; // -EC-: cast to double

	tr.refdef.numDrawSurfs = r_firstSceneDrawSurf;
	tr.refdef.drawSurfs = backEndData->drawSurfs;

#ifdef USE_PMLIGHT
	tr.refdef.numLitSurfs = r_firstSceneLitSurf;
	tr.refdef.litSurfs = backEndData->litSurfs;
#endif

	tr.refdef.num_entities = r_numentities - r_firstSceneEntity;
	tr.refdef.entities = &backEndData->entities[r_firstSceneEntity];

	tr.refdef.num_dlights = r_numdlights - r_firstSceneDlight;
	tr.refdef.dlights = &backEndData->dlights[r_firstSceneDlight];
	R_CopyLiquidInteractionsToRefdef( tr.refdef.time );

	tr.refdef.numPolys = r_numpolys - r_firstScenePoly;
	tr.refdef.polys = &backEndData->polys[r_firstScenePoly];

	// turn off dynamic lighting globally by clearing all the
	// dlights if it needs to be disabled
	if ( r_dynamiclight->integer == 0 || glConfig.hardwareType == GLHW_PERMEDIA2 ) {
		tr.refdef.num_dlights = 0;
	}

	if ( backEnd.screenshotCubeActive && tr.frameCount > 1 &&
		!( tr.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		R_RenderScreenshotCubemapViews();
	}

	// a single frame may have multiple scenes draw inside it --
	// a 3D game view, 3D status bar renderings, 3D menus, etc.
	// They need to be distinguished by the light flare code, because
	// the visibility state for a given surface may be different in
	// each scene / view.
	tr.frameSceneNum++;
	tr.sceneCount++;

	// setup view parms for the initial view
	//
	// set up viewport
	// The refdef takes 0-at-the-top y coordinates, so
	// convert to GL's 0-at-the-bottom space
	//
	Com_Memset( &parms, 0, sizeof( parms ) );
	parms.viewportX = tr.refdef.x;
	parms.viewportY = glConfig.vidHeight - ( tr.refdef.y + tr.refdef.height );
	parms.viewportWidth = tr.refdef.width;
	parms.viewportHeight = tr.refdef.height;

	parms.scissorX = parms.viewportX;
	parms.scissorY = parms.viewportY;
	parms.scissorWidth = parms.viewportWidth;
	parms.scissorHeight = parms.viewportHeight;

	parms.portalView = PV_NONE;

#ifdef USE_PMLIGHT
	parms.dlights = tr.refdef.dlights;
	parms.num_dlights = tr.refdef.num_dlights;
#endif

	parms.fovX = tr.refdef.fov_x;
	parms.fovY = tr.refdef.fov_y;
	
	parms.stereoFrame = tr.refdef.stereoFrame;

	VectorCopy( fd->vieworg, parms.or.origin );
	VectorCopy( fd->viewaxis[0], parms.or.axis[0] );
	VectorCopy( fd->viewaxis[1], parms.or.axis[1] );
	VectorCopy( fd->viewaxis[2], parms.or.axis[2] );

	VectorCopy( fd->vieworg, parms.pvsOrigin );

	R_RenderView( &parms );
	if ( backEnd.screenshotCubeFrontPending ) {
		tr.cubemapDrawSurfLimit = 0;
		if ( tr.cubemapDrawSurfLimitHit || backEnd.screenshotCubeFailed ) {
			R_CancelScreenshotCubemap( tr.cubemapDrawSurfLimitHit ?
				"the front face exceeded the remaining draw-surface budget" :
				"the render-command buffer is full" );
		}
		tr.cubemapDrawSurfLimitHit = qfalse;
	}

	// the next scene rendered in this frame will tack on after this one
	r_firstSceneDrawSurf = tr.refdef.numDrawSurfs;
#ifdef USE_PMLIGHT
	r_firstSceneLitSurf = tr.refdef.numLitSurfs;
#endif

	r_firstSceneEntity = r_numentities;
	r_firstSceneDlight = r_numdlights;
	r_firstScenePoly = r_numpolys;

	tr.frontEndMsec += ri.Milliseconds() - startTime;
}
