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

#ifndef TR_PORTAL_PLAN_H
#define TR_PORTAL_PLAN_H

/*
 * Shared portal-view planning for the legacy GL/GLX renderer and Vulkan
 * renderer.  This file is intentionally included from renderer-private
 * tr_main.c after tr_local.h, so it can use the renderer's private draw-surface
 * and view state without creating a new runtime ABI.
 */

typedef struct {
	cplane_t	originalPlane;
	cplane_t	plane;
} portalSurfacePlane_t;

typedef struct {
	const drawSurf_t	*drawSurf;
	int					surfaceEntityNum;
	int					portalEntityNum;
	qboolean			isMirror;
	orientation_t		surface;
	orientation_t		camera;
	viewParms_t			parentParms;
	viewParms_t			viewParms;
} portalViewPlan_t;

typedef struct {
	portalViewPlan_t	*plans;
	int					maxPlans;
	int					numPlans;
	qboolean			portalOnly;
} portalViewQueue_t;

#define PORTAL_VIEW_STACK_PLANS 4

#ifdef USE_PMLIGHT
extern int r_numdlights;
#endif

static void R_PortalMirrorPoint( const vec3_t in, const orientation_t *surface, const orientation_t *camera, vec3_t out ) {
	int		i;
	vec3_t	local;
	vec3_t	transformed;

	VectorSubtract( in, surface->origin, local );
	VectorClear( transformed );
	for ( i = 0 ; i < 3 ; i++ ) {
		float d;

		d = DotProduct( local, surface->axis[i] );
		VectorMA( transformed, d, camera->axis[i], transformed );
	}

	VectorAdd( transformed, camera->origin, out );
}

static void R_PortalMirrorVector( const vec3_t in, const orientation_t *surface, const orientation_t *camera, vec3_t out ) {
	int		i;
	float	d;

	VectorClear( out );
	for ( i = 0 ; i < 3 ; i++ ) {
		d = DotProduct( in, surface->axis[i] );
		VectorMA( out, d, camera->axis[i], out );
	}
}

static void R_PortalSurfacePlanes( const drawSurf_t *drawSurf, int entityNum, portalSurfacePlane_t *planes ) {
	R_PlaneForSurface( drawSurf->surface, &planes->originalPlane );

	if ( entityNum != REFENTITYNUM_WORLD ) {
		tr.currentEntityNum = entityNum;
		tr.currentEntity = &tr.refdef.entities[entityNum];

		R_RotateForEntity( tr.currentEntity, &tr.viewParms, &tr.or );

		/*
		 * Keep the rotated plane for view setup, but preserve the original
		 * plane normal for matching RT_PORTALSURFACE entities.  This mirrors
		 * the long-standing id Tech 3 behavior exactly.
		 */
		R_LocalNormalToWorld( planes->originalPlane.normal, planes->plane.normal );
		planes->plane.dist = planes->originalPlane.dist + DotProduct( planes->plane.normal, tr.or.origin );
		planes->originalPlane.dist = planes->originalPlane.dist + DotProduct( planes->originalPlane.normal, tr.or.origin );
	} else {
		planes->plane = planes->originalPlane;
	}
}

static qboolean R_PortalEntityIsMirror( const trRefEntity_t *entity ) {
	return ( entity->e.oldorigin[0] == entity->e.origin[0] &&
		entity->e.oldorigin[1] == entity->e.origin[1] &&
		entity->e.oldorigin[2] == entity->e.origin[2] ) ? qtrue : qfalse;
}

static const trRefEntity_t *R_PortalFindSurfaceEntity( const portalSurfacePlane_t *planes, int *portalEntityNum ) {
	int i;

	for ( i = 0 ; i < tr.refdef.num_entities ; i++ ) {
		const trRefEntity_t *entity = &tr.refdef.entities[i];
		float d;

		if ( entity->e.reType != RT_PORTALSURFACE ) {
			continue;
		}

		d = DotProduct( entity->e.origin, planes->originalPlane.normal ) - planes->originalPlane.dist;
		if ( d > 64 || d < -64 ) {
			continue;
		}

		if ( portalEntityNum ) {
			*portalEntityNum = i;
		}
		return entity;
	}

	if ( portalEntityNum ) {
		*portalEntityNum = -1;
	}
	return NULL;
}

static qboolean R_PortalSurfaceIsMirror( const drawSurf_t *drawSurf, int entityNum ) {
	portalSurfacePlane_t planes;
	const trRefEntity_t *entity;

	R_PortalSurfacePlanes( drawSurf, entityNum, &planes );
	entity = R_PortalFindSurfaceEntity( &planes, NULL );
	if ( !entity ) {
		return qfalse;
	}

	return R_PortalEntityIsMirror( entity );
}

static void R_PortalApplyCameraSpin( const trRefEntity_t *entity, orientation_t *camera ) {
	float d;
	vec3_t transformed;

	if ( entity->e.oldframe ) {
		if ( entity->e.frame ) {
			d = ( tr.refdef.time / 1000.0f ) * entity->e.frame;
		} else {
			d = sin( tr.refdef.time * 0.003f );
			d = entity->e.skinNum + d * 4;
		}
	} else if ( entity->e.skinNum ) {
		d = entity->e.skinNum;
	} else {
		return;
	}

	VectorCopy( camera->axis[1], transformed );
	RotatePointAroundVector( camera->axis[1], camera->axis[0], transformed, d );
	CrossProduct( camera->axis[0], camera->axis[1], camera->axis[2] );
}

static qboolean R_PortalResolveOrientations( const drawSurf_t *drawSurf, int entityNum,
	orientation_t *surface, orientation_t *camera, vec3_t pvsOrigin,
	portalView_t *portalView, int *portalEntityNum ) {
	portalSurfacePlane_t planes;
	const trRefEntity_t *entity;
	float d;

	R_PortalSurfacePlanes( drawSurf, entityNum, &planes );

	VectorCopy( planes.plane.normal, surface->axis[0] );
	PerpendicularVector( surface->axis[1], surface->axis[0] );
	CrossProduct( surface->axis[0], surface->axis[1], surface->axis[2] );

	entity = R_PortalFindSurfaceEntity( &planes, portalEntityNum );
	if ( !entity ) {
		return qfalse;
	}

	VectorCopy( entity->e.oldorigin, pvsOrigin );

	if ( R_PortalEntityIsMirror( entity ) ) {
		VectorScale( planes.plane.normal, planes.plane.dist, surface->origin );
		VectorCopy( surface->origin, camera->origin );
		VectorSubtract( vec3_origin, surface->axis[0], camera->axis[0] );
		VectorCopy( surface->axis[1], camera->axis[1] );
		VectorCopy( surface->axis[2], camera->axis[2] );

		*portalView = PV_MIRROR;
		return qtrue;
	}

	d = DotProduct( entity->e.origin, planes.plane.normal ) - planes.plane.dist;
	VectorMA( entity->e.origin, -d, surface->axis[0], surface->origin );

	VectorCopy( entity->e.oldorigin, camera->origin );
	VectorCopy( entity->e.axis[0], camera->axis[0] );
	VectorCopy( entity->e.axis[1], camera->axis[1] );
	VectorCopy( entity->e.axis[2], camera->axis[2] );
	VectorSubtract( vec3_origin, camera->axis[0], camera->axis[0] );
	VectorSubtract( vec3_origin, camera->axis[1], camera->axis[1] );

	R_PortalApplyCameraSpin( entity, camera );

	*portalView = PV_PORTAL;
	return qtrue;
}

static qboolean R_PortalSurfaceIsOffscreen( const drawSurf_t *drawSurf, qboolean *isMirror ) {
	float shortest = 100000000;
	int entityNum;
	int numTriangles;
	shader_t *shader;
	int fogNum;
	int dlighted;
	vec4_t clip, eye;
	int i;
	unsigned int pointAnd = (unsigned int)~0;

	*isMirror = qfalse;

	R_RotateForViewer();

	R_DecomposeSort( drawSurf->sort, &entityNum, &shader, &fogNum, &dlighted );
	RB_BeginSurface( shader, fogNum );
#ifdef USE_VBO
	tess.allowVBO = qfalse;
#endif
#ifdef USE_TESS_NEEDS_NORMAL
	tess.needsNormal = qtrue;
#endif
	rb_surfaceTable[ *drawSurf->surface ]( drawSurf->surface );

	for ( i = 0; i < tess.numVertexes; i++ ) {
		int j;
		unsigned int pointFlags = 0;

		R_TransformModelToClip( tess.xyz[i], tr.or.modelMatrix, tr.viewParms.projectionMatrix, eye, clip );

		for ( j = 0; j < 3; j++ ) {
			if ( clip[j] >= clip[3] ) {
				pointFlags |= ( 1 << ( j * 2 ) );
			} else if ( clip[j] <= -clip[3] ) {
				pointFlags |= ( 1 << ( j * 2 + 1 ) );
			}
		}
		pointAnd &= pointFlags;
	}

	if ( pointAnd ) {
		tess.numIndexes = 0;
		return qtrue;
	}

	numTriangles = tess.numIndexes / 3;
	for ( i = 0; i < tess.numIndexes; i += 3 ) {
		vec3_t normal;
		float len;

		VectorSubtract( tess.xyz[tess.indexes[i]], tr.viewParms.or.origin, normal );

		len = VectorLengthSquared( normal );
		if ( len < shortest ) {
			shortest = len;
		}

		if ( DotProduct( normal, tess.normal[tess.indexes[i]] ) >= 0 ) {
			numTriangles--;
		}
	}

	tess.numIndexes = 0;
	if ( !numTriangles ) {
		return qtrue;
	}

	if ( R_PortalSurfaceIsMirror( drawSurf, entityNum ) ) {
		*isMirror = qtrue;
		return qfalse;
	}

	if ( shortest > ( tess.shader->portalRange * tess.shader->portalRange ) ) {
		return qtrue;
	}

	return qfalse;
}

static void R_PortalModelViewBounds( int *mins, int *maxs ) {
	float minn[2];
	float maxn[2];
	float norm[2];
	float mvp[16];
	float dist[4];
	vec4_t clip;
	int i, j;

	minn[0] = minn[1] =  1.0;
	maxn[0] = maxn[1] = -1.0;

	myGlMultMatrix( tr.or.modelMatrix, tr.viewParms.projectionMatrix, mvp );

	for ( i = 0; i < tess.numVertexes; i++ ) {
		R_TransformModelToClipMVP( tess.xyz[i], mvp, clip );
		if ( clip[3] <= 0.0 ) {
			dist[0] = DotProduct( tess.xyz[i], tr.viewParms.frustum[0].normal ) - tr.viewParms.frustum[0].dist;
			dist[1] = DotProduct( tess.xyz[i], tr.viewParms.frustum[1].normal ) - tr.viewParms.frustum[1].dist;
			dist[2] = DotProduct( tess.xyz[i], tr.viewParms.frustum[2].normal ) - tr.viewParms.frustum[2].dist;
			dist[3] = DotProduct( tess.xyz[i], tr.viewParms.frustum[3].normal ) - tr.viewParms.frustum[3].dist;
			if ( dist[0] <= 0 && dist[1] <= 0 ) {
				if ( dist[0] < dist[1] ) {
					maxn[0] =  1.0f;
				} else {
					minn[0] = -1.0f;
				}
			} else {
				if ( dist[0] <= 0 ) maxn[0] =  1.0f;
				if ( dist[1] <= 0 ) minn[0] = -1.0f;
			}
			if ( dist[2] <= 0 && dist[3] <= 0 ) {
				if ( dist[2] < dist[3] ) {
					minn[1] = -1.0f;
				} else {
					maxn[1] =  1.0f;
				}
			} else {
				if ( dist[2] <= 0 ) minn[1] = -1.0f;
				if ( dist[3] <= 0 ) maxn[1] =  1.0f;
			}
		} else {
			for ( j = 0; j < 2; j++ ) {
				if ( clip[j] >  clip[3] ) clip[j] =  clip[3]; else
				if ( clip[j] < -clip[3] ) clip[j] = -clip[3];
			}
			norm[0] = clip[0] / clip[3];
			norm[1] = clip[1] / clip[3];
			for ( j = 0; j < 2; j++ ) {
				if ( norm[j] < minn[j] ) minn[j] = norm[j];
				if ( norm[j] > maxn[j] ) maxn[j] = norm[j];
			}
		}
	}

	mins[0] = (int)( -0.5 + 0.5 * ( 1.0 + minn[0] ) * tr.viewParms.viewportWidth );
	mins[1] = (int)( -0.5 + 0.5 * ( 1.0 + minn[1] ) * tr.viewParms.viewportHeight );
	maxs[0] = (int)(  0.5 + 0.5 * ( 1.0 + maxn[0] ) * tr.viewParms.viewportWidth );
	maxs[1] = (int)(  0.5 + 0.5 * ( 1.0 + maxn[1] ) * tr.viewParms.viewportHeight );
}

static qboolean R_PortalUseSurfaceScissor( void ) {
	if ( tess.numVertexes <= 2 || !r_fastsky->integer ) {
		return qfalse;
	}

#if defined( USE_VULKAN ) && !defined( USE_BUFFER_CLEAR )
	if ( !vk.clearAttachment ) {
		return qfalse;
	}
#endif

	return qtrue;
}

static qboolean R_PortalAllowMultipleViews( void ) {
	if ( !r_fastsky->integer ) {
		return qfalse;
	}

#if defined( USE_VULKAN ) && !defined( USE_BUFFER_CLEAR )
	if ( !vk.clearAttachment ) {
		return qfalse;
	}
#endif

	return qtrue;
}

static void R_PortalCopyDlightsForPlan( portalViewPlan_t *plan ) {
#ifdef USE_PMLIGHT
	if ( r_numdlights + plan->parentParms.num_dlights <= ARRAY_LEN( backEndData->dlights ) ) {
		int i;

		plan->viewParms.dlights = plan->parentParms.dlights + plan->parentParms.num_dlights;
		plan->viewParms.num_dlights = plan->parentParms.num_dlights;
		r_numdlights += plan->parentParms.num_dlights;
		for ( i = 0; i < plan->parentParms.num_dlights; i++ ) {
			plan->viewParms.dlights[i] = plan->parentParms.dlights[i];
		}
	}
#else
	(void)plan;
#endif
}

static qboolean R_BuildPortalViewPlan( const drawSurf_t *drawSurf, int entityNum, portalViewPlan_t *plan ) {
	int mins[2], maxs[2];
	qboolean isMirror;

	if ( !plan ) {
		return qfalse;
	}

	Com_Memset( plan, 0, sizeof( *plan ) );
	plan->drawSurf = drawSurf;
	plan->surfaceEntityNum = entityNum;
	plan->portalEntityNum = -1;

	if ( R_ViewPassIsPortal( &tr.viewParms ) ) {
		ri.Printf( PRINT_DEVELOPER, "WARNING: recursive mirror/portal found\n" );
		return qfalse;
	}

	if ( r_noportals->integer > 1 ) {
		return qfalse;
	}

	if ( R_PortalSurfaceIsOffscreen( drawSurf, &isMirror ) ) {
		return qfalse;
	}

	if ( !isMirror && r_noportals->integer ) {
		return qfalse;
	}

	plan->isMirror = isMirror;
	plan->parentParms = tr.viewParms;
	plan->viewParms = tr.viewParms;
	plan->viewParms.portalView = PV_NONE;
	plan->viewParms.passFlags = 0;

	if ( !R_PortalResolveOrientations( drawSurf, entityNum, &plan->surface, &plan->camera,
		plan->viewParms.pvsOrigin, &plan->viewParms.portalView, &plan->portalEntityNum ) ) {
		return qfalse;
	}
	R_FinalizeViewPassFlags( &plan->viewParms );

	R_PortalCopyDlightsForPlan( plan );

	if ( R_PortalUseSurfaceScissor() ) {
		R_PortalModelViewBounds( mins, maxs );
		plan->viewParms.scissorX = plan->viewParms.viewportX + mins[0];
		plan->viewParms.scissorY = plan->viewParms.viewportY + mins[1];
		plan->viewParms.scissorWidth = maxs[0] - mins[0];
		plan->viewParms.scissorHeight = maxs[1] - mins[1];
	}

	R_PortalMirrorPoint( plan->parentParms.or.origin, &plan->surface, &plan->camera, plan->viewParms.or.origin );

	VectorSubtract( vec3_origin, plan->camera.axis[0], plan->viewParms.portalPlane.normal );
	plan->viewParms.portalPlane.dist = DotProduct( plan->camera.origin, plan->viewParms.portalPlane.normal );

	R_PortalMirrorVector( plan->parentParms.or.axis[0], &plan->surface, &plan->camera, plan->viewParms.or.axis[0] );
	R_PortalMirrorVector( plan->parentParms.or.axis[1], &plan->surface, &plan->camera, plan->viewParms.or.axis[1] );
	R_PortalMirrorVector( plan->parentParms.or.axis[2], &plan->surface, &plan->camera, plan->viewParms.or.axis[2] );

	return qtrue;
}

static void R_InitPortalViewQueue( portalViewQueue_t *queue, portalViewPlan_t *plans, int maxPlans ) {
	queue->plans = plans;
	queue->maxPlans = maxPlans;
	queue->numPlans = 0;
	queue->portalOnly = qfalse;
}

static int R_MaxPortalViewPlans( int numPortalDrawSurfs ) {
	if ( numPortalDrawSurfs <= 0 ) {
		return 0;
	}

	if ( r_portalOnly->integer || !R_PortalAllowMultipleViews() ) {
		return 1;
	}

	return numPortalDrawSurfs;
}

static int R_CountPortalDrawSurfs( const drawSurf_t *drawSurfs, int numDrawSurfs ) {
	shader_t	*shader;
	int			fogNum;
	int			entityNum;
	int			dlighted;
	int			i;

	for ( i = 0 ; i < numDrawSurfs ; i++ ) {
		R_DecomposeSort( drawSurfs[i].sort, &entityNum, &shader, &fogNum, &dlighted );

		if ( shader->sort > SS_PORTAL ) {
			break;
		}

		// no shader should ever have this sort type
		if ( shader->sort == SS_BAD ) {
			ri.Error (ERR_DROP, "Shader '%s'with sort == SS_BAD", shader->name );
		}
	}

	return i;
}

static qboolean R_QueuePortalViewBySurface( portalViewQueue_t *queue, const drawSurf_t *drawSurf, int entityNum ) {
	if ( queue->numPlans >= queue->maxPlans ) {
		return qfalse;
	}

	if ( !R_BuildPortalViewPlan( drawSurf, entityNum, &queue->plans[queue->numPlans] ) ) {
		return qfalse;
	}

	queue->numPlans++;

	return qtrue;
}

static void R_QueuePortalViewPlans( const drawSurf_t *drawSurfs, int numPortalDrawSurfs, portalViewQueue_t *queue ) {
	shader_t	*shader;
	int			fogNum;
	int			entityNum;
	int			dlighted;
	int			i;

	for ( i = 0 ; i < numPortalDrawSurfs ; i++ ) {
		R_DecomposeSort( drawSurfs[i].sort, &entityNum, &shader, &fogNum, &dlighted );

		if ( R_QueuePortalViewBySurface( queue, &drawSurfs[i], entityNum ) ) {
			// this is a debug option to see exactly what is being mirrored
			if ( r_portalOnly->integer ) {
				queue->portalOnly = qtrue;
				break;
			}
			if ( !R_PortalAllowMultipleViews() ) {
				break;	// only one mirror view at a time
			}
		}
	}
}

static void R_RenderPortalViewPlan( const portalViewPlan_t *plan ) {
	R_RenderView( &plan->viewParms );
	tr.viewParms = plan->parentParms;
}

static void R_RenderPortalViewQueue( const portalViewQueue_t *queue ) {
	int i;

	for ( i = 0 ; i < queue->numPlans ; i++ ) {
		R_RenderPortalViewPlan( &queue->plans[i] );
	}
}

#endif // TR_PORTAL_PLAN_H
