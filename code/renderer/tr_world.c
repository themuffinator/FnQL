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
#include "tr_glx_compat.h"

#ifdef USE_LEGACY_DLIGHTS
static void R_GLXRecordProjectedDlights( const dlight_t *dlights, int count )
{
#ifdef RENDERER_GLX
	glxProjectedDlightRecord_t records[MAX_DLIGHTS];
	unsigned int cullFlags;
	int i;

	if ( count <= 0 || !dlights ) {
		GLX_CompatRecordProjectedDlights( NULL, 0 );
		return;
	}
	if ( count > MAX_DLIGHTS ) {
		count = MAX_DLIGHTS;
	}

	// mirror the legacy projected pass: backface fragments are culled only
	// when r_dlightBacks is disabled
	cullFlags = r_dlightBacks->integer ? 0u : GLX_PROJECTED_DLIGHT_FLAG_FRONT_CULL;

	for ( i = 0; i < count; i++ ) {
		VectorCopy( dlights[i].transformed, records[i].origin );
		records[i].radius = dlights[i].radius;
		VectorCopy( dlights[i].color, records[i].color );
		records[i].flags = cullFlags;
		if ( dlights[i].additive ) {
			records[i].flags |= GLX_PROJECTED_DLIGHT_FLAG_ADDITIVE;
		}
		if ( dlights[i].linear ) {
			records[i].flags |= GLX_PROJECTED_DLIGHT_FLAG_LINEAR;
		}
	}

	GLX_CompatRecordProjectedDlights( records, count );
#else
	(void)dlights;
	(void)count;
#endif
}

static int R_GLXStaticSurfaceItemIndex( const msurface_t *surf )
{
#ifdef RENDERER_GLX
	if ( !surf || !surf->data ) {
		return 0;
	}

	if ( *surf->data == SF_FACE ) {
		const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)surf->data;
		return face->vboItemIndex;
	} else if ( *surf->data == SF_TRIANGLES ) {
		const srfTriangles_t *tris = (const srfTriangles_t *)surf->data;
		return tris->vboItemIndex;
	} else if ( *surf->data == SF_GRID ) {
		const srfGridMesh_t *grid = (const srfGridMesh_t *)surf->data;
		return grid->vboItemIndex;
	}
#else
	(void)surf;
#endif

	return 0;
}
#endif


/*
=================
R_CullTriSurf

Returns true if the grid is completely culled away.
Also sets the clipped hint bit in tess
=================
*/
static qboolean	R_CullTriSurf( srfTriangles_t *cv ) {
	int 	boxCull;

	boxCull = R_CullLocalBox( cv->bounds );

	if ( boxCull == CULL_OUT ) {
		return qtrue;
	}
	return qfalse;
}

/*
=================
R_CullGrid

Returns true if the grid is completely culled away.
Also sets the clipped hint bit in tess
=================
*/
static qboolean	R_CullGrid( srfGridMesh_t *cv ) {
	int 	boxCull;
	int 	sphereCull;

	if ( r_nocurves->integer ) {
		return qtrue;
	}

	if ( tr.currentEntityNum != REFENTITYNUM_WORLD ) {
		sphereCull = R_CullLocalPointAndRadius( cv->localOrigin, cv->meshRadius );
	} else {
		sphereCull = R_CullPointAndRadius( cv->localOrigin, cv->meshRadius );
	}
	
	// check for trivial reject
	if ( sphereCull == CULL_OUT )
	{
		tr.pc.c_sphere_cull_patch_out++;
		return qtrue;
	}
	// check bounding box if necessary
	else if ( sphereCull == CULL_CLIP )
	{
		tr.pc.c_sphere_cull_patch_clip++;

		boxCull = R_CullLocalBox( cv->meshBounds );

		if ( boxCull == CULL_OUT ) 
		{
			tr.pc.c_box_cull_patch_out++;
			return qtrue;
		}
		else if ( boxCull == CULL_IN )
		{
			tr.pc.c_box_cull_patch_in++;
		}
		else
		{
			tr.pc.c_box_cull_patch_clip++;
		}
	}
	else
	{
		tr.pc.c_sphere_cull_patch_in++;
	}

	return qfalse;
}


/*
================
R_CullSurface

Tries to back face cull surfaces before they are lighted or
added to the sorting list.

This will also allow mirrors on both sides of a model without recursion.
================
*/
static qboolean	R_CullSurface( const surfaceType_t *surface, shader_t *shader ) {
	srfSurfaceFace_t *sface;
	float			d;

	if ( r_nocull->integer ) {
		return qfalse;
	}

	if ( *surface == SF_GRID ) {
		return R_CullGrid( (srfGridMesh_t *)surface );
	}

	if ( *surface == SF_TRIANGLES ) {
		return R_CullTriSurf( (srfTriangles_t *)surface );
	}

	if ( *surface != SF_FACE ) {
		return qfalse;
	}

	if ( shader->cullType == CT_TWO_SIDED ) {
		return qfalse;
	}

	// face culling
	if ( !r_facePlaneCull->integer ) {
		return qfalse;
	}

	sface = ( srfSurfaceFace_t * ) surface;
	d = DotProduct (tr.or.viewOrigin, sface->plane.normal);

	// don't cull exactly on the plane, because there are levels of rounding
	// through the BSP, ICD, and hardware that may cause pixel gaps if an
	// epsilon isn't allowed here 
	if ( shader->cullType == CT_FRONT_SIDED ) {
		if ( d < sface->plane.dist - 8 ) {
			return qtrue;
		}
	} else {
		if ( d > sface->plane.dist + 8 ) {
			return qtrue;
		}
	}

	return qfalse;
}


#ifdef USE_PMLIGHT
qboolean R_LightCullBounds( const dlight_t* dl, const vec3_t mins, const vec3_t maxs )
{
	if ( dl->linear ) {
		if (dl->transformed[0] - dl->radius > maxs[0] && dl->transformed2[0] - dl->radius > maxs[0] )
			return qtrue;
		if (dl->transformed[0] + dl->radius < mins[0] && dl->transformed2[0] + dl->radius < mins[0] )
			return qtrue;

		if (dl->transformed[1] - dl->radius > maxs[1] && dl->transformed2[1] - dl->radius > maxs[1] )
			return qtrue;
		if (dl->transformed[1] + dl->radius < mins[1] && dl->transformed2[1] + dl->radius < mins[1] )
			return qtrue;

		if (dl->transformed[2] - dl->radius > maxs[2] && dl->transformed2[2] - dl->radius > maxs[2] )
			return qtrue;
		if (dl->transformed[2] + dl->radius < mins[2] && dl->transformed2[2] + dl->radius < mins[2] )
			return qtrue;

		return qfalse;
	}

	if (dl->transformed[0] - dl->radius > maxs[0])
		return qtrue;
	if (dl->transformed[0] + dl->radius < mins[0])
		return qtrue;

	if (dl->transformed[1] - dl->radius > maxs[1])
		return qtrue;
	if (dl->transformed[1] + dl->radius < mins[1])
		return qtrue;

	if (dl->transformed[2] - dl->radius > maxs[2])
		return qtrue;
	if (dl->transformed[2] + dl->radius < mins[2])
		return qtrue;

	return qfalse;
}


static qboolean R_LightCullFace( const srfSurfaceFace_t* face, const dlight_t* dl )
{
	float d;

#if defined( USE_LEGACY_DLIGHTS ) || defined( USE_PMLIGHT )
	if ( R_LightCullBounds( dl, face->bounds[0], face->bounds[1] ) ) {
		return qtrue;
	}
#endif

	d = DotProduct( dl->transformed, face->plane.normal ) - face->plane.dist;
	if ( dl->linear )
	{
		float d2 = DotProduct( dl->transformed2, face->plane.normal ) - face->plane.dist;
		if ( (d < -dl->radius) && (d2 < -dl->radius) )
			return qtrue;
		if ( (d > dl->radius) && (d2 > dl->radius) ) 
			return qtrue;
	} 
	else 
	{
		if ( (d < -dl->radius) || (d > dl->radius) )
			return qtrue;
	}

	return qfalse;
}


static qboolean R_LightCullSurface( const surfaceType_t* surface, const dlight_t* dl )
{
	switch (*surface) {
	case SF_FACE:
		return R_LightCullFace( (const srfSurfaceFace_t*)surface, dl );
	case SF_GRID: {
		const srfGridMesh_t* grid = (const srfGridMesh_t*)surface;
		return R_LightCullBounds( dl, grid->meshBounds[0], grid->meshBounds[1] );
		}
	case SF_TRIANGLES: {
		const srfTriangles_t* tris = (const srfTriangles_t*)surface;
		return R_LightCullBounds( dl, tris->bounds[0], tris->bounds[1] );
		}
	default:
		return qfalse;
	};
}
#endif // USE_PMLIGHT


#ifdef USE_LEGACY_DLIGHTS
static void R_SetSurfaceDlightBits( surfaceType_t *surface, int dlightBits ) {
	if ( !surface ) {
		return;
	}

	switch ( *surface ) {
	case SF_FACE:
		((srfSurfaceFace_t *)surface)->dlightBits = dlightBits;
		break;
	case SF_GRID:
		((srfGridMesh_t *)surface)->dlightBits = dlightBits;
		break;
	case SF_TRIANGLES:
		((srfTriangles_t *)surface)->dlightBits = dlightBits;
		break;
	default:
		break;
	}
}


static qboolean R_LegacyDlightShaderAllowed( const shader_t *shader ) {
	if ( !shader ) {
		return qfalse;
	}
	if ( shader->sort > SS_OPAQUE ) {
		return qfalse;
	}
	if ( shader->surfaceFlags & ( SURF_NODLIGHT | SURF_SKY ) ) {
		return qfalse;
	}
	return qtrue;
}


static qboolean R_LegacyDlightCullBounds( const dlight_t *dl, const vec3_t mins, const vec3_t maxs ) {
	if ( !dl ) {
		return qtrue;
	}

	if ( dl->linear ) {
		if ( dl->transformed[0] - dl->radius > maxs[0] && dl->transformed2[0] - dl->radius > maxs[0] ) {
			return qtrue;
		}
		if ( dl->transformed[0] + dl->radius < mins[0] && dl->transformed2[0] + dl->radius < mins[0] ) {
			return qtrue;
		}
		if ( dl->transformed[1] - dl->radius > maxs[1] && dl->transformed2[1] - dl->radius > maxs[1] ) {
			return qtrue;
		}
		if ( dl->transformed[1] + dl->radius < mins[1] && dl->transformed2[1] + dl->radius < mins[1] ) {
			return qtrue;
		}
		if ( dl->transformed[2] - dl->radius > maxs[2] && dl->transformed2[2] - dl->radius > maxs[2] ) {
			return qtrue;
		}
		if ( dl->transformed[2] + dl->radius < mins[2] && dl->transformed2[2] + dl->radius < mins[2] ) {
			return qtrue;
		}
		return qfalse;
	}

	if ( dl->transformed[0] - dl->radius > maxs[0] ) {
		return qtrue;
	}
	if ( dl->transformed[0] + dl->radius < mins[0] ) {
		return qtrue;
	}
	if ( dl->transformed[1] - dl->radius > maxs[1] ) {
		return qtrue;
	}
	if ( dl->transformed[1] + dl->radius < mins[1] ) {
		return qtrue;
	}
	if ( dl->transformed[2] - dl->radius > maxs[2] ) {
		return qtrue;
	}
	if ( dl->transformed[2] + dl->radius < mins[2] ) {
		return qtrue;
	}

	return qfalse;
}


static int R_DlightFace( srfSurfaceFace_t *face, int dlightBits ) {
	float		d;
	int			i;
	const dlight_t	*dl;

	for ( i = 0; i < tr.refdef.num_dlights; i++ ) {
		if ( ! ( dlightBits & ( 1 << i ) ) ) {
			continue;
		}
		dl = &tr.refdef.dlights[i];
		if ( R_LegacyDlightCullBounds( dl, face->bounds[0], face->bounds[1] ) ) {
			// dlight doesn't reach the finite face bounds
			dlightBits &= ~( 1 << i );
			continue;
		}
		d = DotProduct( dl->transformed, face->plane.normal ) - face->plane.dist;
		if ( d < -dl->radius || d > dl->radius ) {
			// dlight doesn't reach the plane
			dlightBits &= ~( 1 << i );
		}
	}

	if ( !dlightBits ) {
		tr.pc.c_dlightSurfacesCulled++;
	}

	R_SetSurfaceDlightBits( (surfaceType_t *)face, dlightBits );
	return dlightBits;
}


static int R_DlightGrid( srfGridMesh_t *grid, int dlightBits ) {
	int			i;
	const dlight_t	*dl;

	for ( i = 0 ; i < tr.refdef.num_dlights ; i++ ) {
		if ( ! ( dlightBits & ( 1 << i ) ) ) {
			continue;
		}
		dl = &tr.refdef.dlights[i];
		if ( R_LegacyDlightCullBounds( dl, grid->meshBounds[0], grid->meshBounds[1] ) ) {
			// dlight doesn't reach the bounds
			dlightBits &= ~( 1 << i );
		}
	}

	if ( !dlightBits ) {
		tr.pc.c_dlightSurfacesCulled++;
	}

	R_SetSurfaceDlightBits( (surfaceType_t *)grid, dlightBits );
	return dlightBits;
}


static int R_DlightTrisurf( srfTriangles_t *surf, int dlightBits ) {
	int			i;
	const dlight_t	*dl;

	for ( i = 0 ; i < tr.refdef.num_dlights ; i++ ) {
		if ( ! ( dlightBits & ( 1 << i ) ) ) {
			continue;
		}
		dl = &tr.refdef.dlights[i];
		if ( R_LegacyDlightCullBounds( dl, surf->bounds[0], surf->bounds[1] ) ) {
			// dlight doesn't reach the bounds
			dlightBits &= ~( 1 << i );
		}
	}

	if ( !dlightBits ) {
		tr.pc.c_dlightSurfacesCulled++;
	}

	R_SetSurfaceDlightBits( (surfaceType_t *)surf, dlightBits );
	return dlightBits;
}


/*
====================
R_DlightSurface

The given surface is going to be drawn, and it touches a leaf
that is touched by one or more dlights, so try to throw out
more dlights if possible.
====================
*/
static int R_DlightSurface( msurface_t *surf, int dlightBits ) {
	if ( *surf->data == SF_FACE ) {
		dlightBits = R_DlightFace( (srfSurfaceFace_t *)surf->data, dlightBits );
	} else if ( *surf->data == SF_GRID ) {
		dlightBits = R_DlightGrid( (srfGridMesh_t *)surf->data, dlightBits );
	} else if ( *surf->data == SF_TRIANGLES ) {
		dlightBits = R_DlightTrisurf( (srfTriangles_t *)surf->data, dlightBits );
	} else {
		dlightBits = 0;
	}

	if ( dlightBits ) {
		tr.pc.c_dlightSurfaces++;
	}

	return dlightBits;
}
#endif // USE_LEGACY_DLIGHTS

#ifdef USE_PMLIGHT
static unsigned int r_worldPMLightMask;
static int r_worldPMLightMaxStamp;
static int r_worldPMLightStamps[MAX_DLIGHTS];

static void R_AddWorldSurfacePMLights( msurface_t *surf, unsigned int pmlightBits );

#ifdef USE_LEGACY_DLIGHTS
// R_GetDlightMode() cannot change mid-view; cache it per surface walk so the
// per-node and per-surface paths avoid a cross-module call in the hot loop
static int r_worldDlightMode;
#endif
#endif


/*
======================
R_AddWorldSurface
======================
*/
static void R_AddWorldSurface( msurface_t *surf, int dlightBits, unsigned int pmlightBits ) {
#ifndef USE_PMLIGHT
	(void)pmlightBits;
#endif

	if ( surf->viewCount == tr.viewCount ) {
#ifdef USE_PMLIGHT
#ifdef USE_LEGACY_DLIGHTS
		if ( r_worldDlightMode )
#endif
		{
			R_AddWorldSurfacePMLights( surf, pmlightBits );
		}
#endif // USE_PMLIGHT
		return;		// already in this view
	}

	surf->viewCount = tr.viewCount;
	// FIXME: bmodel fog?

	// try to cull before dlighting or adding
	if ( R_CullSurface( surf->data, surf->shader ) ) {
		return;
	}

#ifdef USE_PMLIGHT
#ifdef USE_LEGACY_DLIGHTS
	if ( r_worldDlightMode )
#endif
	{
#ifdef USE_LEGACY_DLIGHTS
		R_SetSurfaceDlightBits( surf->data, 0 );
#endif
		surf->vcVisible = tr.viewCount;
		R_AddDrawSurf( surf->data, surf->shader, surf->fogIndex, 0 );
		R_AddWorldSurfacePMLights( surf, pmlightBits );
		return;
	}
#endif // USE_PMLIGHT

#ifdef USE_LEGACY_DLIGHTS
	// check for dlighting
	if ( dlightBits ) {
		if ( R_LegacyDlightShaderAllowed( surf->shader ) ) {
			dlightBits = R_DlightSurface( surf, dlightBits );
			if ( dlightBits && tr.currentEntityNum == REFENTITYNUM_WORLD ) {
				GLX_CompatRecordProjectedDlightList( R_GLXStaticSurfaceItemIndex( surf ),
					(unsigned int)dlightBits );
			}
			dlightBits = ( dlightBits != 0 );
		} else {
			dlightBits = 0;
			R_SetSurfaceDlightBits( surf->data, 0 );
		}
	} else {
		R_SetSurfaceDlightBits( surf->data, 0 );
	}

	R_AddDrawSurf( surf->data, surf->shader, surf->fogIndex, dlightBits );
#endif // USE_LEGACY_DLIGHTS
}


/*
=============================================================
	PM LIGHTING
=============================================================
*/
#ifdef USE_PMLIGHT
static void R_AddLitSurface( msurface_t *surf, const dlight_t *light )
{
	// since we're not worried about offscreen lights casting into the frustum (ATM !!!)
	// only add the "lit" version of this surface if it was already added to the view
	//if ( surf->viewCount != tr.viewCount )
	//	return;

	// surfaces that were faceculled will still have the current viewCount in vcBSP
	// because that's set to indicate that it's BEEN vis tested at all, to avoid
	// repeated vis tests, not whether it actually PASSED the vis test or not
	// only light surfaces that are GENUINELY visible, as opposed to merely in a visible LEAF
	if ( surf->vcVisible != tr.viewCount ) {
		return;
	}

	if ( surf->shader->lightingStage < 0 ) {
		return;
	}

	if ( surf->lightCount == tr.lightCount )
		return;

	surf->lightCount = tr.lightCount;

	if ( R_LightCullSurface( surf->data, light ) ) {
		tr.pc.c_lit_culls++;
		return;
	}

	R_AddLitSurf( surf->data, surf->shader, surf->fogIndex );
}


static void R_AddWorldSurfacePMLights( msurface_t *surf, unsigned int pmlightBits )
{
	int i;

	if ( !r_worldPMLightMask || tr.currentEntityNum != REFENTITYNUM_WORLD ) {
		return;
	}
	if ( !surf || !surf->shader || surf->shader->lightingStage < 0 ||
		surf->shader->sort > SS_OPAQUE ||
		( surf->shader->surfaceFlags & ( SURF_NODLIGHT | SURF_SKY ) ) ) {
		return;
	}

	pmlightBits &= r_worldPMLightMask;
	if ( !pmlightBits ) {
		return;
	}

	// a surface shared by several visible leaves re-enters this function once
	// per leaf; surf->lightCount only remembers the last attempted light, so a
	// per-view mask of attempted lights is needed to keep one attempt per light
	// (multiple attempts would append duplicate litsurfs and double the
	// additive lighting contribution)
	if ( surf->pmLitFrame != tr.viewCount ) {
		surf->pmLitFrame = tr.viewCount;
		surf->pmLitMask = 0;
	}
	pmlightBits &= ~surf->pmLitMask;
	if ( !pmlightBits ) {
		return;
	}
	surf->pmLitMask |= pmlightBits;

	for ( i = 0; i < tr.viewParms.num_dlights && i < MAX_DLIGHTS; i++ ) {
		if ( !( pmlightBits & ( 1u << i ) ) ) {
			continue;
		}

		tr.light = &tr.viewParms.dlights[i];
		tr.lightCount = r_worldPMLightStamps[i];
		R_AddLitSurface( surf, tr.light );
	}
}
#endif // USE_PMLIGHT


/*
=============================================================

	BRUSH MODELS

=============================================================
*/

/*
=================
R_AddBrushModelSurfaces
=================
*/
void R_AddBrushModelSurfaces ( trRefEntity_t *ent ) {
	bmodel_t	*bmodel;
	int			clip;
	const model_t		*pModel;
	int			i;

	pModel = R_GetModelByHandle( ent->e.hModel );

	bmodel = pModel->bmodel;

	clip = R_CullLocalBox( bmodel->bounds );
	if ( clip == CULL_OUT ) {
		return;
	}

#ifdef USE_PMLIGHT
#ifdef USE_LEGACY_DLIGHTS
	r_worldDlightMode = R_GetDlightMode();
	if ( r_worldDlightMode )
#endif
	{
		dlight_t *dl;
		int s;

		for ( s = 0; s < bmodel->numSurfaces; s++ ) {
			R_AddWorldSurface( bmodel->firstSurface + s, 0, 0 );
		}

		R_SetupEntityLighting( &tr.refdef, ent );
		
		R_TransformDlights( tr.viewParms.num_dlights, tr.viewParms.dlights, &tr.or );

		for ( i = 0; i < tr.viewParms.num_dlights; i++ ) {
			dl = &tr.viewParms.dlights[i];
			if ( !R_LightCullBounds( dl, bmodel->bounds[0], bmodel->bounds[1] ) ) {
				tr.lightCount++;
				tr.light = dl;
				for ( s = 0; s < bmodel->numSurfaces; s++ ) {
					R_AddLitSurface( bmodel->firstSurface + s, dl );
				}
			}
		}
		return;
	}
#endif // USE_PMLIGHT

#ifdef USE_LEGACY_DLIGHTS
	R_SetupEntityLighting( &tr.refdef, ent );
	R_DlightBmodel( bmodel );

	for ( i = 0 ; i < bmodel->numSurfaces ; i++ ) {
		R_AddWorldSurface( bmodel->firstSurface + i, tr.currentEntity->needDlights, 0 );
	}
#endif
}


/*
=============================================================

	WORLD MODEL

=============================================================
*/


/*
================
R_RecursiveWorldNode
================
*/
static void R_RecursiveWorldNode( mnode_t *node, unsigned int planeBits, unsigned int dlightBits,
		unsigned int pmlightBits ) {
#ifndef USE_PMLIGHT
	(void)pmlightBits;
#endif

	do {
		unsigned int newDlights[2];
#ifdef USE_PMLIGHT
		unsigned int newPMLights[2];
#endif

		// if the node wasn't marked as potentially visible, exit
		if (node->visframe != tr.visCount) {
			return;
		}

		// if the bounding volume is outside the frustum, nothing
		// inside can be visible OPTIMIZE: don't do this all the way to leafs?

		if ( !r_nocull->integer ) {
			int		r;

			if ( planeBits & 1 ) {
				r = BoxOnPlaneSide(node->mins, node->maxs, &tr.viewParms.frustum[0]);
				if (r == 2) {
					return;						// culled
				}
				if ( r == 1 ) {
					planeBits &= ~1;			// all descendants will also be in front
				}
			}

			if ( planeBits & 2 ) {
				r = BoxOnPlaneSide(node->mins, node->maxs, &tr.viewParms.frustum[1]);
				if (r == 2) {
					return;						// culled
				}
				if ( r == 1 ) {
					planeBits &= ~2;			// all descendants will also be in front
				}
			}

			if ( planeBits & 4 ) {
				r = BoxOnPlaneSide(node->mins, node->maxs, &tr.viewParms.frustum[2]);
				if (r == 2) {
					return;						// culled
				}
				if ( r == 1 ) {
					planeBits &= ~4;			// all descendants will also be in front
				}
			}

			if ( planeBits & 8 ) {
				r = BoxOnPlaneSide(node->mins, node->maxs, &tr.viewParms.frustum[3]);
				if (r == 2) {
					return;						// culled
				}
				if ( r == 1 ) {
					planeBits &= ~8;			// all descendants will also be in front
				}
			}

		}

		// Refine active dlight masks against the current node bounds before
		// splitting them by plane. This keeps broad, view-visible lights from
		// being tested against every surface in unrelated BSP branches.
#ifdef USE_LEGACY_DLIGHTS
#ifdef USE_PMLIGHT
		if ( !r_worldDlightMode )
#endif
		if ( dlightBits ) {
			int i;

			for ( i = 0; i < tr.refdef.num_dlights && i < MAX_DLIGHTS; i++ ) {
				if ( ( dlightBits & ( 1u << i ) ) &&
					R_LegacyDlightCullBounds( &tr.refdef.dlights[i], node->mins, node->maxs ) ) {
					dlightBits &= ~( 1u << i );
				}
			}
		}
#endif // USE_LEGACY_DLIGHTS

#ifdef USE_PMLIGHT
		if ( pmlightBits ) {
			int i;

			for ( i = 0; i < tr.viewParms.num_dlights && i < MAX_DLIGHTS; i++ ) {
				if ( ( pmlightBits & ( 1u << i ) ) &&
					R_LightCullBounds( &tr.viewParms.dlights[i], node->mins, node->maxs ) ) {
					pmlightBits &= ~( 1u << i );
				}
			}
		}
#endif // USE_PMLIGHT

		if ( node->contents != CONTENTS_NODE ) {
			break;
		}

		// node is just a decision point, so go down both sides
		// since we don't care about sort orders, just go positive to negative

		// determine which dlights are needed
		newDlights[0] = 0;
		newDlights[1] = 0;
#ifdef USE_PMLIGHT
		newPMLights[0] = 0;
		newPMLights[1] = 0;
#endif
#ifdef USE_LEGACY_DLIGHTS
#ifdef USE_PMLIGHT
		if ( !r_worldDlightMode )
#endif
		if ( dlightBits ) {
			int	i;

			for ( i = 0 ; i < tr.refdef.num_dlights ; i++ ) {
				const dlight_t	*dl;
				float		dist;

				if ( dlightBits & ( 1 << i ) ) {
					dl = &tr.refdef.dlights[i];
					dist = DotProduct( dl->origin, node->plane->normal ) - node->plane->dist;
					
					if ( dist > -dl->radius ) {
						newDlights[0] |= ( 1 << i );
					}
					if ( dist < dl->radius ) {
						newDlights[1] |= ( 1 << i );
					}
				}
			}
		}
#endif // USE_LEGACY_DLIGHTS

#ifdef USE_PMLIGHT
		if ( pmlightBits ) {
			int	i;

			for ( i = 0 ; i < tr.viewParms.num_dlights && i < MAX_DLIGHTS ; i++ ) {
				const dlight_t	*dl;
				float		dist;

				if ( !( pmlightBits & ( 1u << i ) ) ) {
					continue;
				}

				dl = &tr.viewParms.dlights[i];
				dist = DotProduct( dl->origin, node->plane->normal ) - node->plane->dist;
				if ( dl->linear ) {
					float dist2 = DotProduct( dl->origin2, node->plane->normal ) - node->plane->dist;

					if ( dist > -dl->radius || dist2 > -dl->radius ) {
						newPMLights[0] |= ( 1u << i );
					}
					if ( dist < dl->radius || dist2 < dl->radius ) {
						newPMLights[1] |= ( 1u << i );
					}
				} else {
					if ( dist > -dl->radius ) {
						newPMLights[0] |= ( 1u << i );
					}
					if ( dist < dl->radius ) {
						newPMLights[1] |= ( 1u << i );
					}
				}
			}
		}
#endif // USE_PMLIGHT

		// recurse down the children, front side first
		R_RecursiveWorldNode( node->children[0], planeBits, newDlights[0],
#ifdef USE_PMLIGHT
			newPMLights[0]
#else
			0
#endif
		);

		// tail recurse
		node = node->children[1];
#ifdef USE_LEGACY_DLIGHTS
		dlightBits = newDlights[1];
#endif
#ifdef USE_PMLIGHT
		pmlightBits = newPMLights[1];
#endif
	} while ( 1 );

	{
		// leaf node, so add mark surfaces
		int			c;
		msurface_t	*surf, **mark;

		tr.pc.c_leafs++;

		// add to z buffer bounds
		if ( node->mins[0] < tr.viewParms.visBounds[0][0] ) {
			tr.viewParms.visBounds[0][0] = node->mins[0];
		}
		if ( node->mins[1] < tr.viewParms.visBounds[0][1] ) {
			tr.viewParms.visBounds[0][1] = node->mins[1];
		}
		if ( node->mins[2] < tr.viewParms.visBounds[0][2] ) {
			tr.viewParms.visBounds[0][2] = node->mins[2];
		}

		if ( node->maxs[0] > tr.viewParms.visBounds[1][0] ) {
			tr.viewParms.visBounds[1][0] = node->maxs[0];
		}
		if ( node->maxs[1] > tr.viewParms.visBounds[1][1] ) {
			tr.viewParms.visBounds[1][1] = node->maxs[1];
		}
		if ( node->maxs[2] > tr.viewParms.visBounds[1][2] ) {
			tr.viewParms.visBounds[1][2] = node->maxs[2];
		}

		// add the individual surfaces
		mark = node->firstmarksurface;
		c = node->nummarksurfaces;
		while (c--) {
			// the surface may have already been added if it
			// spans multiple leafs
			surf = *mark;
			R_AddWorldSurface( surf, dlightBits, pmlightBits );
			mark++;
		}
	}
}


/*
===============
R_PointInLeaf
===============
*/
static mnode_t *R_PointInLeaf( const vec3_t p ) {
	mnode_t		*node;
	float		d;
	const cplane_t	*plane;
	
	if ( !tr.world ) {
		ri.Error (ERR_DROP, "R_PointInLeaf: bad model");
	}

	node = tr.world->nodes;
	while( 1 ) {
		if (node->contents != CONTENTS_NODE ) {
			break;
		}
		plane = node->plane;
		d = DotProduct (p,plane->normal) - plane->dist;
		if (d > 0) {
			node = node->children[0];
		} else {
			node = node->children[1];
		}
	}
	
	return node;
}

/*
==============
R_ClusterPVS
==============
*/
static const byte *R_ClusterPVS (int cluster) {
	if ( !tr.world->vis || cluster < 0 || cluster >= tr.world->numClusters ) {
		return tr.world->novis;
	}

	return tr.world->vis + cluster * tr.world->clusterBytes;
}

/*
=====================
R_PointLeafClusterArea
=====================
*/
qboolean R_PointLeafClusterArea( const vec3_t point, int *cluster, int *area ) {
	const mnode_t *leaf;

	if ( cluster ) {
		*cluster = -1;
	}
	if ( area ) {
		*area = -1;
	}
	if ( !tr.world || !tr.world->nodes ) {
		return qfalse;
	}

	leaf = R_PointInLeaf( point );
	if ( cluster ) {
		*cluster = leaf->cluster;
	}
	if ( area ) {
		*area = leaf->area;
	}

	return ( leaf->cluster >= 0 && leaf->cluster < tr.world->numClusters ) ? qtrue : qfalse;
}

/*
=================
R_inPVS
=================
*/
qboolean R_inPVS( const vec3_t p1, const vec3_t p2 ) {
	const mnode_t *leaf;
	const byte	*vis;

	leaf = R_PointInLeaf( p1 );
	vis = ri.CM_ClusterPVS( leaf->cluster );
	leaf = R_PointInLeaf( p2 );

	if ( !(vis[leaf->cluster>>3] & (1<<(leaf->cluster&7))) ) {
		return qfalse;
	}
	return qtrue;
}

/*
=====================
R_LeafClusterInCurrentPVS
=====================
*/
qboolean R_LeafClusterInCurrentPVS( const vec3_t vieworg, int cluster, int area ) {
	static vec3_t cachedViewOrg;
	static const world_t *cachedWorld;
	static int cachedViewCluster;
	static int cachedFrameCount;
	const byte *vis;
	int viewCluster;

	if ( !tr.world || !tr.world->nodes || ( r_novis && r_novis->integer ) ) {
		return qtrue;
	}

	// the view-origin leaf descent is loop-invariant across the many
	// per-frame candidate tests (static map lights, surface-light proxies,
	// spot shadow planning), so memoize it per frame/origin
	if ( cachedFrameCount != tr.frameCount || cachedWorld != tr.world ||
		!VectorCompare( cachedViewOrg, vieworg ) ) {
		const mnode_t *viewLeaf = R_PointInLeaf( vieworg );

		cachedViewCluster = viewLeaf->cluster;
		VectorCopy( vieworg, cachedViewOrg );
		cachedWorld = tr.world;
		cachedFrameCount = tr.frameCount;
	}
	viewCluster = cachedViewCluster;
	if ( viewCluster < 0 || cluster < 0 ||
		viewCluster >= tr.world->numClusters || cluster >= tr.world->numClusters ) {
		return qtrue;
	}

	vis = R_ClusterPVS( viewCluster );
	if ( !( vis[cluster >> 3] & ( 1 << ( cluster & 7 ) ) ) ) {
		return qfalse;
	}

	if ( area >= 0 && area < MAX_MAP_AREA_BYTES * 8 &&
		( tr.refdef.areamask[area >> 3] & ( 1 << ( area & 7 ) ) ) ) {
		return qfalse;
	}

	return qtrue;
}

/*
=====================
R_PointInCurrentPVS
=====================
*/
qboolean R_PointInCurrentPVS( const vec3_t vieworg, const vec3_t point ) {
	const mnode_t *leaf;

	if ( !tr.world || !tr.world->nodes || ( r_novis && r_novis->integer ) ) {
		return qtrue;
	}

	leaf = R_PointInLeaf( point );
	return R_LeafClusterInCurrentPVS( vieworg, leaf->cluster, leaf->area );
}

/*
===============
R_MarkLeaves

Mark the leaves and nodes that are in the PVS for the current
cluster
===============
*/
static void R_MarkLeaves (void) {
	const byte	*vis;
	mnode_t	*leaf, *parent;
	int		i;
	int		cluster;

	// lockpvs lets designers walk around to determine the
	// extent of the current pvs
	if ( r_lockpvs->integer ) {
		return;
	}

	// current viewcluster
	leaf = R_PointInLeaf( tr.viewParms.pvsOrigin );
	cluster = leaf->cluster;

	// if the cluster is the same and the area visibility matrix
	// hasn't changed, we don't need to mark everything again

	// if r_showcluster was just turned on, remark everything 
	if ( tr.viewCluster == cluster && !tr.refdef.areamaskModified 
		&& !r_showcluster->modified ) {
		return;
	}

	if ( r_showcluster->modified || r_showcluster->integer ) {
		r_showcluster->modified = qfalse;
		if ( r_showcluster->integer ) {
			ri.Printf( PRINT_ALL, "cluster:%i  area:%i\n", cluster, leaf->area );
		}
	}

	tr.visCount++;
	tr.viewCluster = cluster;

	if ( r_novis->integer || tr.viewCluster == -1 ) {
		for (i=0 ; i<tr.world->numnodes ; i++) {
			if (tr.world->nodes[i].contents != CONTENTS_SOLID) {
				tr.world->nodes[i].visframe = tr.visCount;
			}
		}
		return;
	}

	vis = R_ClusterPVS (tr.viewCluster);
	
	for (i=0,leaf=tr.world->nodes ; i<tr.world->numnodes ; i++, leaf++) {
		cluster = leaf->cluster;
		if ( cluster < 0 || cluster >= tr.world->numClusters ) {
			continue;
		}

		// check general pvs
		if ( !(vis[cluster>>3] & (1<<(cluster&7))) ) {
			continue;
		}

		// check for door connection
		if ( (tr.refdef.areamask[leaf->area>>3] & (1<<(leaf->area&7)) ) ) {
			continue;		// not visible
		}

		parent = leaf;
		do {
			if (parent->visframe == tr.visCount)
				break;
			parent->visframe = tr.visCount;
			parent = parent->parent;
		} while (parent);
	}
}


/*
=============
R_AddWorldSurfaces
=============
*/
void R_AddWorldSurfaces( void ) {
#ifdef USE_PMLIGHT
	dlight_t* dl;
	int i;
#endif
#ifdef USE_LEGACY_DLIGHTS
	unsigned int dlightBits;
#endif

#if defined( USE_PMLIGHT ) && defined( USE_LEGACY_DLIGHTS )
	r_worldDlightMode = R_GetDlightMode();
#endif

	if ( !r_drawworld->integer ) {
		return;
	}

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return;
	}

	tr.currentEntityNum = REFENTITYNUM_WORLD;
	tr.shiftedEntityNum = tr.currentEntityNum << QSORT_REFENTITYNUM_SHIFT;

	// determine which leaves are in the PVS / areamask
	R_MarkLeaves ();

	// clear out the visible min/max
	ClearBounds( tr.viewParms.visBounds[0], tr.viewParms.visBounds[1] );

	// perform frustum culling and add all the potentially visible surfaces
	if ( tr.refdef.num_dlights > MAX_DLIGHTS ) {
		tr.refdef.num_dlights = MAX_DLIGHTS;
	}

#ifdef USE_PMLIGHT
	r_worldPMLightMask = 0;
	r_worldPMLightMaxStamp = tr.lightCount;
#ifdef USE_LEGACY_DLIGHTS
	if ( r_worldDlightMode )
#endif
	{
		// Populate transformed world-space light positions once, then let the normal
		// visible-surface walk append surfaces to each light. This avoids a second
		// BSP traversal for every dynamic light in GLX per-pixel dlight mode.
		R_TransformDlights( tr.viewParms.num_dlights, tr.viewParms.dlights, &tr.viewParms.world );
		for ( i = 0; i < tr.viewParms.num_dlights && i < MAX_DLIGHTS; i++ )
		{
			dl = &tr.viewParms.dlights[i];
			if ( R_CullDlight( dl ) == CULL_OUT ) {
				tr.pc.c_light_cull_out++;
				continue;
			}
			tr.pc.c_light_cull_in++;
			r_worldPMLightStamps[i] = ++tr.lightCount;
			r_worldPMLightMaxStamp = tr.lightCount;
			r_worldPMLightMask |= 1u << i;
		}
	}
#endif // USE_PMLIGHT

#ifdef USE_LEGACY_DLIGHTS
	dlightBits = 0;
	R_GLXRecordProjectedDlights( NULL, 0 );
#ifdef USE_PMLIGHT
	if ( !r_worldDlightMode )
#endif
	{
#ifdef USE_PMLIGHT
		int lightIndex;

		R_TransformDlights( tr.refdef.num_dlights, tr.refdef.dlights, &tr.viewParms.world );
		R_GLXRecordProjectedDlights( tr.refdef.dlights, tr.refdef.num_dlights );
		for ( lightIndex = 0; lightIndex < tr.refdef.num_dlights && lightIndex < MAX_DLIGHTS; lightIndex++ ) {
			if ( R_CullDlight( &tr.refdef.dlights[lightIndex] ) == CULL_OUT ) {
				tr.pc.c_light_cull_out++;
				continue;
			}
			tr.pc.c_light_cull_in++;
			dlightBits |= 1u << lightIndex;
		}
#else
		if ( tr.refdef.num_dlights >= MAX_DLIGHTS ) {
			dlightBits = ~0u;
		} else if ( tr.refdef.num_dlights > 0 ) {
			dlightBits = ( 1u << tr.refdef.num_dlights ) - 1u;
		}
#endif
	}
	R_RecursiveWorldNode( tr.world->nodes, 15, dlightBits,
#ifdef USE_PMLIGHT
		r_worldPMLightMask
#else
		0
#endif
	);
#else
	R_RecursiveWorldNode( tr.world->nodes, 15, 0,
#ifdef USE_PMLIGHT
		r_worldPMLightMask
#else
		0
#endif
	);
#endif

#ifdef USE_PMLIGHT
	if ( r_worldPMLightMask ) {
		tr.lightCount = r_worldPMLightMaxStamp;
	}
#endif

#ifdef USE_PMLIGHT
#ifdef USE_LEGACY_DLIGHTS
	if ( !r_worldDlightMode )
		return;
#endif // USE_LEGACY_DLIGHTS
#endif // USE_PMLIGHT
}
