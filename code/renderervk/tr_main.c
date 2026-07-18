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
// tr_main.c -- main control flow for each frame

#include "tr_local.h"

#include <string.h> // memcpy

trGlobals_t		tr;

static const float s_flipMatrix[16] = {
	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	0, 0, -1, 0,
	-1, 0, 0, 0,
	0, 1, 0, 0,
	0, 0, 0, 1
};


refimport_t	ri;
qlRendererCvars_t qlRendererCvars;

// entities that will have procedurally generated surfaces will just
// point at this for their sorting surface
static surfaceType_t entitySurface = SF_ENTITY;

/*
=================
R_CullLocalBox

Returns CULL_IN, CULL_CLIP, or CULL_OUT
=================
*/
int R_CullLocalBox( const vec3_t bounds[2] ) {
	int		i, j;
	vec3_t	transformed[8];
	float	dists[8];
	vec3_t	v;
	cplane_t	*frust;
	int			anyBack;
	int			front, back;

	if ( r_nocull->integer ) {
		return CULL_CLIP;
	}

	// transform into world space
	for (i = 0 ; i < 8 ; i++) {
		v[0] = bounds[i&1][0];
		v[1] = bounds[(i>>1)&1][1];
		v[2] = bounds[(i>>2)&1][2];

		VectorCopy( tr.or.origin, transformed[i] );
		VectorMA( transformed[i], v[0], tr.or.axis[0], transformed[i] );
		VectorMA( transformed[i], v[1], tr.or.axis[1], transformed[i] );
		VectorMA( transformed[i], v[2], tr.or.axis[2], transformed[i] );
	}

	// check against frustum planes
	anyBack = 0;
	for (i = 0 ; i < 4 ; i++) {
		frust = &tr.viewParms.frustum[i];

		front = back = 0;
		for (j = 0 ; j < 8 ; j++) {
			dists[j] = DotProduct(transformed[j], frust->normal);
			if ( dists[j] > frust->dist ) {
				front = 1;
				if ( back ) {
					break;		// a point is in front
				}
			} else {
				back = 1;
			}
		}
		if ( !front ) {
			// all points were behind one of the planes
			return CULL_OUT;
		}
		anyBack |= back;
	}

	if ( !anyBack ) {
		return CULL_IN;		// completely inside frustum
	}

	return CULL_CLIP;		// partially clipped
}


/*
** R_CullLocalPointAndRadius
*/
int R_CullLocalPointAndRadius( const vec3_t pt, float radius )
{
	vec3_t transformed;

	R_LocalPointToWorld( pt, transformed );

	return R_CullPointAndRadius( transformed, radius );
}


/*
** R_CullPointAndRadius
*/
int R_CullPointAndRadius( const vec3_t pt, float radius )
{
	int		i;
	float	dist;
	const cplane_t	*frust;
	qboolean mightBeClipped = qfalse;

	if ( r_nocull->integer ) {
		return CULL_CLIP;
	}

	// check against frustum planes
	for (i = 0 ; i < 4 ; i++) 
	{
		frust = &tr.viewParms.frustum[i];

		dist = DotProduct( pt, frust->normal) - frust->dist;
		if ( dist < -radius )
		{
			return CULL_OUT;
		}
		else if ( dist <= radius ) 
		{
			mightBeClipped = qtrue;
		}
	}

	if ( mightBeClipped )
	{
		return CULL_CLIP;
	}

	return CULL_IN;		// completely inside frustum
}


/*
** R_CullDlight
*/
int R_CullDlight( const dlight_t* dl )
{
	int		i;
	float	dist, dist2;
	cplane_t	*frust;
	qboolean mightBeClipped = qfalse;

	if ( r_nocull->integer )
		return CULL_CLIP;

	if ( dl->linear ) {
		for ( i = 0 ; i < 4 ; i++ ) {
			frust = &tr.viewParms.frustum[i];
			dist = DotProduct( dl->transformed, frust->normal) - frust->dist;
			dist2 = DotProduct( dl->transformed2, frust->normal) - frust->dist;
			if ( dist < -dl->radius && dist2 < -dl->radius )
				return CULL_OUT;
			else if ( dist <= dl->radius || dist2 <= dl->radius ) 
				mightBeClipped = qtrue;
		}
	} 
	else
	// check against frustum planes
	for ( i = 0 ; i < 4 ; i++ ) {
		frust = &tr.viewParms.frustum[i];
		dist = DotProduct( dl->transformed, frust->normal) - frust->dist;
		if ( dist < -dl->radius )
			return CULL_OUT;
		else if ( dist <= dl->radius ) 
			mightBeClipped = qtrue;
	}

	if ( mightBeClipped )
		return CULL_CLIP;

	return CULL_IN;	// completely inside frustum
}


/*
=================
R_LocalNormalToWorld
=================
*/
static void R_LocalNormalToWorld( const vec3_t local, vec3_t world ) {
	world[0] = local[0] * tr.or.axis[0][0] + local[1] * tr.or.axis[1][0] + local[2] * tr.or.axis[2][0];
	world[1] = local[0] * tr.or.axis[0][1] + local[1] * tr.or.axis[1][1] + local[2] * tr.or.axis[2][1];
	world[2] = local[0] * tr.or.axis[0][2] + local[1] * tr.or.axis[1][2] + local[2] * tr.or.axis[2][2];
}


/*
=================
R_LocalPointToWorld
=================
*/
void R_LocalPointToWorld( const vec3_t local, vec3_t world ) {
	world[0] = local[0] * tr.or.axis[0][0] + local[1] * tr.or.axis[1][0] + local[2] * tr.or.axis[2][0] + tr.or.origin[0];
	world[1] = local[0] * tr.or.axis[0][1] + local[1] * tr.or.axis[1][1] + local[2] * tr.or.axis[2][1] + tr.or.origin[1];
	world[2] = local[0] * tr.or.axis[0][2] + local[1] * tr.or.axis[1][2] + local[2] * tr.or.axis[2][2] + tr.or.origin[2];
}


/*
=================
R_WorldToLocal
=================
*/
void R_WorldToLocal( const vec3_t world, vec3_t local ) {
	local[0] = DotProduct( world, tr.or.axis[0] );
	local[1] = DotProduct( world, tr.or.axis[1] );
	local[2] = DotProduct( world, tr.or.axis[2] );
}


/*
==========================
R_TransformModelToClip
==========================
*/
void R_TransformModelToClip( const vec3_t src, const float *modelMatrix, const float *projectionMatrix,
							vec4_t eye, vec4_t dst ) {
	int i;

	for ( i = 0 ; i < 4 ; i++ ) {
		eye[i] = 
			src[0] * modelMatrix[ i + 0 * 4 ] +
			src[1] * modelMatrix[ i + 1 * 4 ] +
			src[2] * modelMatrix[ i + 2 * 4 ] +
			1 * modelMatrix[ i + 3 * 4 ];
	}

	for ( i = 0 ; i < 4 ; i++ ) {
		dst[i] = 
			eye[0] * projectionMatrix[ i + 0 * 4 ] +
			eye[1] * projectionMatrix[ i + 1 * 4 ] +
			eye[2] * projectionMatrix[ i + 2 * 4 ] +
			eye[3] * projectionMatrix[ i + 3 * 4 ];
	}
}


/*
==========================
R_TransformModelToClipMVP
==========================
*/
static void R_TransformModelToClipMVP( const vec3_t src, const float *mvp, vec4_t clip ) {
	int i;

	for ( i = 0 ; i < 4 ; i++ ) {
		clip[i] = 
			src[0] * mvp[ i + 0 * 4 ] +
			src[1] * mvp[ i + 1 * 4 ] +
			src[2] * mvp[ i + 2 * 4 ] +
			1 * mvp[ i + 3 * 4 ];
	}
}


/*
==========================
R_TransformClipToWindow
==========================
*/
void R_TransformClipToWindow( const vec4_t clip, const viewParms_t *view, vec4_t normalized, vec4_t window ) {
	normalized[0] = clip[0] / clip[3];
	normalized[1] = clip[1] / clip[3];
	normalized[2] = ( clip[2] + clip[3] ) / ( 2 * clip[3] );

	window[0] = 0.5f * ( 1.0f + normalized[0] ) * view->viewportWidth;
	window[1] = 0.5f * ( 1.0f + normalized[1] ) * view->viewportHeight;
	window[2] = normalized[2];

	window[0] = (int) ( window[0] + 0.5 );
	window[1] = (int) ( window[1] + 0.5 );
}


/*
==========================
myGlMultMatrix
==========================
*/
void myGlMultMatrix( const float *a, const float *b, float *out ) {
	int		i, j;

	for ( i = 0 ; i < 4 ; i++ ) {
		for ( j = 0 ; j < 4 ; j++ ) {
			out[ i * 4 + j ] =
				a [ i * 4 + 0 ] * b [ 0 * 4 + j ]
				+ a [ i * 4 + 1 ] * b [ 1 * 4 + j ]
				+ a [ i * 4 + 2 ] * b [ 2 * 4 + j ]
				+ a [ i * 4 + 3 ] * b [ 3 * 4 + j ];
		}
	}
}


/*
=================
R_RotateForEntity

Generates an orientation for an entity and viewParms
Does NOT produce any GL calls
Called by both the front end and the back end
=================
*/
void R_RotateForEntity( const trRefEntity_t *ent, const viewParms_t *viewParms,
					   orientationr_t *or ) {
	float	glMatrix[16];
	vec3_t	delta;
	float	axisLength;

	if ( ent->e.reType != RT_MODEL ) {
		*or = viewParms->world;
		return;
	}

	VectorCopy( ent->e.origin, or->origin );

	VectorCopy( ent->e.axis[0], or->axis[0] );
	VectorCopy( ent->e.axis[1], or->axis[1] );
	VectorCopy( ent->e.axis[2], or->axis[2] );

	glMatrix[0] = or->axis[0][0];
	glMatrix[4] = or->axis[1][0];
	glMatrix[8] = or->axis[2][0];
	glMatrix[12] = or->origin[0];

	glMatrix[1] = or->axis[0][1];
	glMatrix[5] = or->axis[1][1];
	glMatrix[9] = or->axis[2][1];
	glMatrix[13] = or->origin[1];

	glMatrix[2] = or->axis[0][2];
	glMatrix[6] = or->axis[1][2];
	glMatrix[10] = or->axis[2][2];
	glMatrix[14] = or->origin[2];

	glMatrix[3] = 0;
	glMatrix[7] = 0;
	glMatrix[11] = 0;
	glMatrix[15] = 1;

	myGlMultMatrix( glMatrix, viewParms->world.modelMatrix, or->modelMatrix );

	// calculate the viewer origin in the model's space
	// needed for fog, specular, and environment mapping
	VectorSubtract( viewParms->or.origin, or->origin, delta );

	// compensate for scale in the axes if necessary
	if ( ent->e.nonNormalizedAxes ) {
		axisLength = VectorLength( ent->e.axis[0] );
		if ( !axisLength ) {
			axisLength = 0;
		} else {
			axisLength = 1.0f / axisLength;
		}
	} else {
		axisLength = 1.0f;
	}

	or->viewOrigin[0] = DotProduct( delta, or->axis[0] ) * axisLength;
	or->viewOrigin[1] = DotProduct( delta, or->axis[1] ) * axisLength;
	or->viewOrigin[2] = DotProduct( delta, or->axis[2] ) * axisLength;
}


/*
=================
R_RotateForViewer

Sets up the modelview matrix for a given viewParm
=================
*/
static void R_RotateForViewer( void )
{
	float	viewerMatrix[16];
	vec3_t	origin;

	Com_Memset (&tr.or, 0, sizeof(tr.or));
	tr.or.axis[0][0] = 1;
	tr.or.axis[1][1] = 1;
	tr.or.axis[2][2] = 1;
	VectorCopy (tr.viewParms.or.origin, tr.or.viewOrigin);

	// transform by the camera placement
	VectorCopy( tr.viewParms.or.origin, origin );

	viewerMatrix[0] = tr.viewParms.or.axis[0][0];
	viewerMatrix[4] = tr.viewParms.or.axis[0][1];
	viewerMatrix[8] = tr.viewParms.or.axis[0][2];
	viewerMatrix[12] = -origin[0] * viewerMatrix[0] + -origin[1] * viewerMatrix[4] + -origin[2] * viewerMatrix[8];

	viewerMatrix[1] = tr.viewParms.or.axis[1][0];
	viewerMatrix[5] = tr.viewParms.or.axis[1][1];
	viewerMatrix[9] = tr.viewParms.or.axis[1][2];
	viewerMatrix[13] = -origin[0] * viewerMatrix[1] + -origin[1] * viewerMatrix[5] + -origin[2] * viewerMatrix[9];

	viewerMatrix[2] = tr.viewParms.or.axis[2][0];
	viewerMatrix[6] = tr.viewParms.or.axis[2][1];
	viewerMatrix[10] = tr.viewParms.or.axis[2][2];
	viewerMatrix[14] = -origin[0] * viewerMatrix[2] + -origin[1] * viewerMatrix[6] + -origin[2] * viewerMatrix[10];

	viewerMatrix[3] = 0;
	viewerMatrix[7] = 0;
	viewerMatrix[11] = 0;
	viewerMatrix[15] = 1;

	// convert from our coordinate system (looking down X)
	// to OpenGL's coordinate system (looking down -Z)
	myGlMultMatrix( viewerMatrix, s_flipMatrix, tr.or.modelMatrix );

	tr.viewParms.world = tr.or;
}


/*
** SetFarClip
*/
static void R_SetFarClip( void )
{
	float	farthestCornerDistance;
	int		i;

	// if not rendering the world (icons, menus, etc)
	// set a 2k far clip plane
	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		tr.viewParms.zFar = 2048;
		return;
	}

	//
	// set far clipping planes dynamically
	//
	farthestCornerDistance = 0;
	for ( i = 0; i < 8; i++ )
	{
		vec3_t v;
		vec3_t vecTo;
		float distance;

		v[0] = tr.viewParms.visBounds[(i>>0)&1][0];
		v[1] = tr.viewParms.visBounds[(i>>1)&1][1];
		v[2] = tr.viewParms.visBounds[(i>>2)&1][2];

		VectorSubtract( v, tr.viewParms.or.origin, vecTo );

		distance = DotProduct( vecTo, vecTo );

		if ( distance > farthestCornerDistance )
		{
			farthestCornerDistance = distance;
		}
	}

	tr.viewParms.zFar = sqrt( farthestCornerDistance );
}


/*
=================
R_SetupFrustum

Set up the culling frustum planes for the current view using the results we got from computing the first two rows of
the projection matrix.
=================
*/
static void R_SetupFrustum( viewParms_t *dest, float xmin, float xmax, float ymax, float zProj, float stereoSep )
{
	vec3_t ofsorigin;
	float oppleg, adjleg, length;
	int i;
	
	if(stereoSep == 0 && xmin == -xmax)
	{
		// symmetric case can be simplified
		VectorCopy(dest->or.origin, ofsorigin);

		length = sqrt(xmax * xmax + zProj * zProj);
		oppleg = xmax / length;
		adjleg = zProj / length;

		VectorScale(dest->or.axis[0], oppleg, dest->frustum[0].normal);
		VectorMA(dest->frustum[0].normal, adjleg, dest->or.axis[1], dest->frustum[0].normal);

		VectorScale(dest->or.axis[0], oppleg, dest->frustum[1].normal);
		VectorMA(dest->frustum[1].normal, -adjleg, dest->or.axis[1], dest->frustum[1].normal);
	}
	else
	{
		// In stereo rendering, due to the modification of the projection matrix, dest->or.origin is not the
		// actual origin that we're rendering so offset the tip of the view pyramid.
		VectorMA(dest->or.origin, stereoSep, dest->or.axis[1], ofsorigin);
	
		oppleg = xmax + stereoSep;
		length = sqrt(oppleg * oppleg + zProj * zProj);
		VectorScale(dest->or.axis[0], oppleg / length, dest->frustum[0].normal);
		VectorMA(dest->frustum[0].normal, zProj / length, dest->or.axis[1], dest->frustum[0].normal);

		oppleg = xmin + stereoSep;
		length = sqrt(oppleg * oppleg + zProj * zProj);
		VectorScale(dest->or.axis[0], -oppleg / length, dest->frustum[1].normal);
		VectorMA(dest->frustum[1].normal, -zProj / length, dest->or.axis[1], dest->frustum[1].normal);
	}

	length = sqrt(ymax * ymax + zProj * zProj);
	oppleg = ymax / length;
	adjleg = zProj / length;

	VectorScale(dest->or.axis[0], oppleg, dest->frustum[2].normal);
	VectorMA(dest->frustum[2].normal, adjleg, dest->or.axis[2], dest->frustum[2].normal);

	VectorScale(dest->or.axis[0], oppleg, dest->frustum[3].normal);
	VectorMA(dest->frustum[3].normal, -adjleg, dest->or.axis[2], dest->frustum[3].normal);
	
	for (i=0 ; i<4 ; i++) {
		dest->frustum[i].type = PLANE_NON_AXIAL;
		dest->frustum[i].dist = DotProduct (ofsorigin, dest->frustum[i].normal);
		SetPlaneSignbits( &dest->frustum[i] );
	}

	// near clipping plane
	VectorCopy( dest->or.axis[0], dest->frustum[4].normal );
	dest->frustum[4].type = PLANE_NON_AXIAL;
	dest->frustum[4].dist = DotProduct( ofsorigin, dest->frustum[4].normal ) + r_znear->value;
	SetPlaneSignbits( &dest->frustum[4] );
}


/*
===============
R_SetupProjection
===============
*/
void R_SetupProjection( viewParms_t *dest, float zProj, qboolean computeFrustum )
{
	float	xmin, xmax, ymin, ymax;
	float	width, height, stereoSep = r_stereoSeparation->value;

	/*
	 * offset the view origin of the viewer for stereo rendering 
	 * by setting the projection matrix appropriately.
	 */

	if ( stereoSep != 0 )
	{
		if ( dest->stereoFrame == STEREO_LEFT )
			stereoSep = zProj / stereoSep;
		else if ( dest->stereoFrame == STEREO_RIGHT )
			stereoSep = zProj / -stereoSep;
		else
			stereoSep = 0;
	}

	ymax = zProj * tan(dest->fovY * M_PI / 360.0f);
	ymin = -ymax;

	xmax = zProj * tan(dest->fovX * M_PI / 360.0f);
	xmin = -xmax;

	width = xmax - xmin;
	height = ymax - ymin;
	
	dest->projectionMatrix[0] = 2 * zProj / width;
	dest->projectionMatrix[4] = 0;
	dest->projectionMatrix[8] = (xmax + xmin + 2 * stereoSep) / width;
	dest->projectionMatrix[12] = 2 * zProj * stereoSep / width;

	dest->projectionMatrix[1] = 0;
	dest->projectionMatrix[5] = 2 * zProj / height;
	dest->projectionMatrix[9] = ( ymax + ymin ) / height;	// normally 0
	dest->projectionMatrix[13] = 0;

	dest->projectionMatrix[3] = 0;
	dest->projectionMatrix[7] = 0;
	dest->projectionMatrix[11] = -1;
	dest->projectionMatrix[15] = 0;
	
	// Now that we have all the data for the projection matrix we can also setup the view frustum.
	if ( computeFrustum )
		R_SetupFrustum( dest, xmin, xmax, ymax, zProj, stereoSep );
}


/*
===============
R_SetupProjectionZ

Sets the z-component transformation part in the projection matrix
===============
*/
static void R_SetupProjectionZ( viewParms_t *dest )
{
	const float zNear = r_znear->value;
	const float zFar = dest->zFar;
	const float depth = zFar - zNear;

	dest->projectionMatrix[2] = 0;
	dest->projectionMatrix[6] = 0;
#ifdef USE_VULKAN
#ifdef USE_REVERSED_DEPTH
	dest->projectionMatrix[10] = zNear / depth;
	dest->projectionMatrix[14] = zFar * zNear / depth;
#else
	dest->projectionMatrix[10] = - zFar / depth;
	dest->projectionMatrix[14] = - zFar * zNear / depth;
#endif
#else
	dest->projectionMatrix[10] = -( zFar + zNear ) / depth;
	dest->projectionMatrix[14] = -2 * zFar * zNear / depth;
#endif

	if ( R_ViewPassIsPortal( dest ) )
	{
		float	plane[4];
		float	plane2[4];
		vec4_t q, c;

#ifdef USE_VULKAN
#ifdef USE_REVERSED_DEPTH
		dest->projectionMatrix[10] = - zFar / depth;
		dest->projectionMatrix[14] = - zFar * zNear / depth;
#endif
#endif
		// transform portal plane into camera space
		plane[0] = dest->portalPlane.normal[0];
		plane[1] = dest->portalPlane.normal[1];
		plane[2] = dest->portalPlane.normal[2];
		plane[3] = dest->portalPlane.dist;

		plane2[0] = -DotProduct( dest->or.axis[1], plane );
		plane2[1] =  DotProduct( dest->or.axis[2], plane );
		plane2[2] = -DotProduct( dest->or.axis[0], plane );
		plane2[3] =  DotProduct( plane, dest->or.origin) - plane[3];

		// Lengyel, Eric. "Modifying the Projection Matrix to Perform Oblique Near-plane Clipping".
		// Terathon Software 3D Graphics Library, 2004. http://www.terathon.com/code/oblique.html
		q[0] = (SGN(plane2[0]) + dest->projectionMatrix[8]) / dest->projectionMatrix[0];
		q[1] = (SGN(plane2[1]) + dest->projectionMatrix[9]) / dest->projectionMatrix[5];
		q[2] = -1.0f;
#ifdef USE_VULKAN
		q[3] = - dest->projectionMatrix[10] / dest->projectionMatrix[14];
#else
		q[3] = (1.0f + dest->projectionMatrix[10]) / dest->projectionMatrix[14];
#endif
		VectorScale4( plane2, 2.0f / DotProduct4(plane2, q), c );

		dest->projectionMatrix[2]  = c[0];
		dest->projectionMatrix[6]  = c[1];
#ifdef USE_VULKAN
		dest->projectionMatrix[10] = c[2];
#else
		dest->projectionMatrix[10] = c[2] + 1.0f;
#endif
		dest->projectionMatrix[14] = c[3];

#ifdef USE_REVERSED_DEPTH
		dest->projectionMatrix[2] = -dest->projectionMatrix[2];
		dest->projectionMatrix[6] = -dest->projectionMatrix[6];
		dest->projectionMatrix[10] = -(dest->projectionMatrix[10] + 1.0);
		dest->projectionMatrix[14] = -dest->projectionMatrix[14];
#endif
	}
}

static void R_ClearCSMPlan( void )
{
	Com_Memset( &tr.csm, 0, sizeof( tr.csm ) );
}

static float R_CSMSnapLightCoord( float value, float texelSize )
{
	if ( texelSize <= 0.0f ) {
		return value;
	}

	return floorf( value / texelSize + 0.5f ) * texelSize;
}

static void R_CSMBuildLightAxis( const vec3_t lightDirection, vec3_t axis[3] )
{
	VectorCopy( lightDirection, axis[0] );
	PerpendicularVector( axis[1], axis[0] );
	CrossProduct( axis[0], axis[1], axis[2] );
	VectorNormalize( axis[1] );
	VectorNormalize( axis[2] );
}

static int R_ShadowFloorPowerOfTwo( int value )
{
	int result = 1;

	while ( result <= value / 2 ) {
		result <<= 1;
	}

	return result;
}

qboolean R_CSMShadowAtlasLayout( int cascadeCount, int requestedCascadeSize, int maxTextureSize,
	csmShadowAtlasLayout_t *layout )
{
	int cascadeSize;

	if ( !layout || cascadeCount <= 0 || requestedCascadeSize <= 0 || maxTextureSize < 128 ) {
		return qfalse;
	}

	Com_Memset( layout, 0, sizeof( *layout ) );
	cascadeCount = (int)Com_Clamp( 1, CSM_MAX_CASCADES, cascadeCount );
	cascadeSize = R_ShadowFloorPowerOfTwo( requestedCascadeSize );
	cascadeSize = (int)Com_Clamp( 128, 4096, cascadeSize );

	while ( cascadeCount > 1 && cascadeSize * cascadeCount > maxTextureSize ) {
		cascadeSize >>= 1;
	}
	while ( cascadeSize * cascadeCount > maxTextureSize && cascadeSize > 128 ) {
		cascadeSize >>= 1;
	}
	if ( cascadeSize < 128 || cascadeSize * cascadeCount > maxTextureSize ) {
		return qfalse;
	}

	layout->cascadeCount = cascadeCount;
	layout->cascadeSize = cascadeSize;
	layout->width = cascadeSize * cascadeCount;
	layout->height = cascadeSize;
	return qtrue;
}

static void R_CSMBuildFrustumSliceCorners( float splitNear, float splitFar, vec3_t corners[8] )
{
	const viewParms_t *view;
	float tanHalfFovX;
	float tanHalfFovY;
	int distanceIndex;
	int xSign;
	int ySign;
	int corner;

	view = &tr.viewParms;
	tanHalfFovX = tanf( view->fovX * M_PI / 360.0f );
	tanHalfFovY = tanf( view->fovY * M_PI / 360.0f );
	corner = 0;

	for ( distanceIndex = 0; distanceIndex < 2; distanceIndex++ ) {
		float distance = distanceIndex ? splitFar : splitNear;
		float halfWidth = distance * tanHalfFovX;
		float halfHeight = distance * tanHalfFovY;
		vec3_t center;

		VectorMA( view->or.origin, distance, view->or.axis[0], center );

		for ( xSign = -1; xSign <= 1; xSign += 2 ) {
			for ( ySign = -1; ySign <= 1; ySign += 2 ) {
				VectorCopy( center, corners[corner] );
				VectorMA( corners[corner], xSign * halfWidth, view->or.axis[1], corners[corner] );
				VectorMA( corners[corner], ySign * halfHeight, view->or.axis[2], corners[corner] );
				corner++;
			}
		}
	}
}

static void R_CSMPlanCascade( int cascadeIndex, float splitNear, float splitFar,
	const vec3_t lightAxis[3], int resolution )
{
	csmCascadePlan_t *cascade;
	vec3_t corners[8];
	vec3_t center;
	vec3_t lightCenter;
	float radiusSquared;
	float halfExtent;
	float depthHalfExtent;
	int i;

	cascade = &tr.csm.cascades[cascadeIndex];
	R_CSMBuildFrustumSliceCorners( splitNear, splitFar, corners );

	VectorClear( center );
	for ( i = 0; i < 8; i++ ) {
		VectorAdd( center, corners[i], center );
	}
	VectorScale( center, 1.0f / 8.0f, center );

	radiusSquared = 0.0f;
	for ( i = 0; i < 8; i++ ) {
		vec3_t delta;
		float cornerRadiusSquared;

		VectorSubtract( corners[i], center, delta );
		cornerRadiusSquared = VectorLengthSquared( delta );
		if ( cornerRadiusSquared > radiusSquared ) {
			radiusSquared = cornerRadiusSquared;
		}
	}

	cascade->splitNear = splitNear;
	cascade->splitFar = splitFar;
	cascade->radius = MAX( sqrtf( radiusSquared ), 1.0f );
	cascade->texelSize = ( cascade->radius * 2.0f ) / (float)resolution;
	halfExtent = ceilf( ( cascade->radius + cascade->texelSize * 0.5f ) /
		cascade->texelSize ) * cascade->texelSize;
	cascade->texelSize = ( halfExtent * 2.0f ) / (float)resolution;

	/* Keep atlas X/Y stable while leaving light depth continuous for casters. */
	depthHalfExtent = halfExtent * 2.0f;

	for ( i = 0; i < 3; i++ ) {
		lightCenter[i] = DotProduct( center, lightAxis[i] );
		VectorCopy( lightAxis[i], cascade->axis[i] );
	}
	lightCenter[1] = R_CSMSnapLightCoord( lightCenter[1], cascade->texelSize );
	lightCenter[2] = R_CSMSnapLightCoord( lightCenter[2], cascade->texelSize );

	VectorClear( cascade->origin );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( cascade->origin, lightCenter[i], lightAxis[i], cascade->origin );
	}

	cascade->bounds[0][0] = lightCenter[0] - depthHalfExtent;
	cascade->bounds[1][0] = lightCenter[0] + depthHalfExtent;
	cascade->bounds[0][1] = lightCenter[1] - halfExtent;
	cascade->bounds[1][1] = lightCenter[1] + halfExtent;
	cascade->bounds[0][2] = lightCenter[2] - halfExtent;
	cascade->bounds[1][2] = lightCenter[2] + halfExtent;
}

static void R_PlanCascadedShadows( void )
{
	csmShadowAtlasLayout_t atlasLayout;
	vec3_t lightDirection;
	vec3_t lightAxis[3];
	vec3_t lightColor;
	float zNear;
	float maxDistance;
	float splitLambda;
	float receiverBias;
	float casterDepthBias;
	float casterSlopeBias;
	float casterNormalBias;
	float shadowStrength;
	float splitNear;
	int filterMode;
	int cascadeCount;
	int resolution;
	int debugFallback;
	int i;

	R_ClearCSMPlan();
	debugFallback = r_csmDebugFallback ? r_csmDebugFallback->integer : 0;

	if ( r_shadowCorrectness && r_shadowCorrectness->integer ) {
		tr.pc.c_csmSkippedDisabled++;
		return;
	}

	if ( !r_csmShadows || !r_csmShadows->integer ) {
		tr.pc.c_csmSkippedDisabled++;
		return;
	}

	if ( debugFallback == 1 || ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		tr.pc.c_csmSkippedNoWorldRef++;
		return;
	}

	if ( debugFallback == 2 ) {
		tr.pc.c_csmSkippedNoSun++;
		return;
	}

	if ( !tr.worldSun.valid || !tr.sunParmsValid || tr.sunIntensity <= 0.0f ) {
		tr.pc.c_csmSkippedNoSun++;
		return;
	}

	if ( VectorNormalize2( tr.worldSun.lightDirection, lightDirection ) <= 0.0f ) {
		tr.pc.c_csmSkippedNoSun++;
		return;
	}
	if ( VectorNormalize2( tr.sunColor, lightColor ) <= 0.0f ) {
		VectorSet( lightColor, 1.0f, 1.0f, 1.0f );
	}

	zNear = MAX( r_znear->value, 1.0f );
	maxDistance = r_csmMaxDistance ? r_csmMaxDistance->value : 2048.0f;
	if ( tr.viewParms.zFar > zNear ) {
		maxDistance = MIN( maxDistance, tr.viewParms.zFar );
	}
	if ( maxDistance <= zNear + 1.0f ) {
		tr.pc.c_csmSkippedProjection++;
		return;
	}
	if ( debugFallback == 4 ) {
		tr.pc.c_csmSkippedZeroCascades++;
		return;
	}

	cascadeCount = r_csmCascadeCount ? (int)Com_Clamp( 1, CSM_MAX_CASCADES, r_csmCascadeCount->integer ) : CSM_MAX_CASCADES;
	resolution = r_csmResolution ? (int)Com_Clamp( 128, 4096, r_csmResolution->integer ) : 1024;
	if ( debugFallback == 3 ) {
		tr.pc.c_csmSkippedAtlas++;
		return;
	}
	if ( !R_CSMShadowAtlasLayout( cascadeCount, resolution, glConfig.maxTextureSize, &atlasLayout ) ) {
		tr.pc.c_csmSkippedAtlas++;
		return;
	}
	cascadeCount = atlasLayout.cascadeCount;
	resolution = atlasLayout.cascadeSize;
	splitLambda = r_csmSplitLambda ? Com_Clamp( 0.0f, 1.0f, r_csmSplitLambda->value ) : 0.65f;
	filterMode = R_ShadowClampFilterMode( r_csmShadowFilter ? r_csmShadowFilter->integer : SHADOW_FILTER_POISSON_4 );
	receiverBias = R_ShadowClampReceiverBias( r_csmShadowBias ? r_csmShadowBias->value : 8.0f );
	casterDepthBias = R_ShadowClampCasterDepthBias( r_csmCasterDepthBias ? r_csmCasterDepthBias->value : 1.5f );
	casterSlopeBias = R_ShadowClampCasterSlopeBias( r_csmCasterSlopeBias ? r_csmCasterSlopeBias->value : 1.5f );
	casterNormalBias = R_ShadowClampCasterNormalBias( r_csmCasterNormalBias ? r_csmCasterNormalBias->value : 0.5f );
	shadowStrength = r_csmShadowStrength ? Com_Clamp( 0.0f, 1.0f, r_csmShadowStrength->value ) : 1.0f;
	if ( shadowStrength <= 0.0f ) {
		tr.pc.c_csmSkippedStrength++;
		return;
	}

	R_CSMBuildLightAxis( lightDirection, lightAxis );

	tr.csm.enabled = qtrue;
	tr.csm.skySun = qtrue;
	tr.csm.cascadeCount = cascadeCount;
	tr.csm.resolution = resolution;
	tr.csm.maxDistance = maxDistance;
	tr.csm.splitLambda = splitLambda;
	tr.csm.filterMode = filterMode;
	tr.csm.receiverBias = receiverBias;
	tr.csm.casterDepthBias = casterDepthBias;
	tr.csm.casterSlopeBias = casterSlopeBias;
	tr.csm.casterNormalBias = casterNormalBias;
	tr.csm.shadowStrength = shadowStrength;
	VectorCopy( lightDirection, tr.csm.lightDirection );
	VectorCopy( lightColor, tr.csm.lightColor );
	VectorCopy( tr.worldSun.directionToSun, tr.csm.directionToSun );
	Q_strncpyz( tr.csm.skyShaderName, tr.worldSun.shaderName, sizeof( tr.csm.skyShaderName ) );

	splitNear = zNear;
	for ( i = 0; i < cascadeCount; i++ ) {
		float splitFar;

		if ( i == cascadeCount - 1 ) {
			splitFar = maxDistance;
		} else {
			float part = (float)( i + 1 ) / (float)cascadeCount;
			float linear = zNear + ( maxDistance - zNear ) * part;
			float logarithmic = zNear * powf( maxDistance / zNear, part );

			splitFar = linear * ( 1.0f - splitLambda ) + logarithmic * splitLambda;
			splitFar = Com_Clamp( splitNear + 1.0f, maxDistance, splitFar );
		}

		R_CSMPlanCascade( i, splitNear, splitFar, lightAxis, resolution );
		splitNear = splitFar;
	}

	tr.pc.c_csmCascades = cascadeCount;
	tr.pc.c_csmResolution = resolution;
	tr.pc.c_csmMaxDistance = (int)maxDistance;
	tr.csmDebugPlan = tr.csm;
}


/*
=============
R_PlaneForSurface
=============
*/
static void R_PlaneForSurface( const surfaceType_t *surfType, cplane_t *plane ) {
	srfTriangles_t	*tri;
	srfPoly_t		*poly;
	drawVert_t		*v1, *v2, *v3;
	vec4_t			plane4;

	if (!surfType) {
		Com_Memset (plane, 0, sizeof(*plane));
		plane->normal[0] = 1;
		return;
	}
	switch (*surfType) {
	case SF_FACE:
		*plane = ((srfSurfaceFace_t *)surfType)->plane;
		return;
	case SF_TRIANGLES:
		tri = (srfTriangles_t *)surfType;
		v1 = tri->verts + tri->indexes[0];
		v2 = tri->verts + tri->indexes[1];
		v3 = tri->verts + tri->indexes[2];
		PlaneFromPoints( plane4, v1->xyz, v2->xyz, v3->xyz );
		VectorCopy( plane4, plane->normal ); 
		plane->dist = plane4[3];
		return;
	case SF_POLY:
		poly = (srfPoly_t *)surfType;
		PlaneFromPoints( plane4, poly->verts[0].xyz, poly->verts[1].xyz, poly->verts[2].xyz );
		VectorCopy( plane4, plane->normal ); 
		plane->dist = plane4[3];
		return;
	default:
		Com_Memset (plane, 0, sizeof(*plane));
		plane->normal[0] = 1;
		return;
	}
}

#include "../renderercommon/tr_portal_plan.h"


/*
=================
R_SpriteFogNum

See if a sprite is inside a fog volume
=================
*/
static int R_SpriteFogNum( const trRefEntity_t *ent ) {
	int				i, j;
	const fog_t		*fog;

	if ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) {
		return 0;
	}

	if ( ent->e.renderfx & RF_CROSSHAIR ) {
		return 0;
	}

	for ( i = 1 ; i < tr.world->numfogs ; i++ ) {
		fog = &tr.world->fogs[i];
		for ( j = 0 ; j < 3 ; j++ ) {
			if ( ent->e.origin[j] - ent->e.radius >= fog->bounds[1][j] ) {
				break;
			}
			if ( ent->e.origin[j] + ent->e.radius <= fog->bounds[0][j] ) {
				break;
			}
		}
		if ( j == 3 ) {
			return i;
		}
	}

	return 0;
}

/*
==========================================================================================

DRAWSURF SORTING

==========================================================================================
*/

/*
===============
R_Radix
===============
*/
static ID_INLINE void R_Radix( int byte, int size, const drawSurf_t *source, drawSurf_t *dest )
{
  int           count[ 256 ] = { 0 };
  int           index[ 256 ];
  int           i;
  unsigned char *sortKey;
  unsigned char *end;

  sortKey = ( (unsigned char *)&source[ 0 ].sort ) + byte;
  end = sortKey + ( size * sizeof( drawSurf_t ) );
  for( ; sortKey < end; sortKey += sizeof( drawSurf_t ) )
    ++count[ *sortKey ];

  index[ 0 ] = 0;

  for( i = 1; i < 256; ++i )
    index[ i ] = index[ i - 1 ] + count[ i - 1 ];

  sortKey = ( (unsigned char *)&source[ 0 ].sort ) + byte;
  for( i = 0; i < size; ++i, sortKey += sizeof( drawSurf_t ) )
    dest[ index[ *sortKey ]++ ] = source[ i ];
}


/*
===============
R_RadixSort

Radix sort with 4 byte size buckets
===============
*/
static void R_RadixSort( drawSurf_t *source, int size )
{
  static drawSurf_t scratch[ MAX_DRAWSURFS ];
#ifdef Q3_LITTLE_ENDIAN
  R_Radix( 0, size, source, scratch );
  R_Radix( 1, size, scratch, source );
  R_Radix( 2, size, source, scratch );
  R_Radix( 3, size, scratch, source );
#else
  R_Radix( 3, size, source, scratch );
  R_Radix( 2, size, scratch, source );
  R_Radix( 1, size, source, scratch );
  R_Radix( 0, size, scratch, source );
#endif //Q3_LITTLE_ENDIAN
}


#ifdef USE_PMLIGHT

typedef struct litSurf_tape_s {
	struct litSurf_s *first;
	struct litSurf_s *last;
	unsigned count;
} litSurf_tape_t;

// Philip Erdelsky gets all the credit for this one...

static void R_SortLitsurfs( dlight_t* dl )
{
	litSurf_tape_t tape[ 4 ];
	int				base;
	litSurf_t		*p;
	litSurf_t		*next;
	unsigned		block_size;
	litSurf_tape_t	*tape0;
	litSurf_tape_t	*tape1;
	int				dest;
	litSurf_tape_t	*output_tape;
	litSurf_tape_t	*chosen_tape;
	unsigned		n0, n1;

	// distribute the records alternately to tape[0] and tape[1]

	tape[0].count = tape[1].count = 0;
	tape[0].first = tape[1].first = NULL;

	base = 0;
	p = dl->head;

	while ( p ) {
		next = p->next;
		p->next = tape[base].first;
		tape[base].first = p;
		tape[base].count++;
		p = next;
		base ^= 1;
	}

	// merge from the two active tapes into the two idle ones
	// doubling the number of records and pingponging the tape sets as we go

	block_size = 1;
	for ( base = 0; tape[base+1].count; base ^= 2, block_size <<= 1 )
	{
		tape0 = tape + base;
		tape1 = tape + base + 1;
		dest = base ^ 2;

		tape[dest].count = tape[dest+1].count = 0;
		for (; tape0->count; dest ^= 1)
		{
			output_tape = tape + dest;
			n0 = n1 = block_size;

			while (1)
			{
				if (n0 == 0 || tape0->count == 0)
				{
					if (n1 == 0 || tape1->count == 0)
						break;
					chosen_tape = tape1;
					n1--;
				}
				else if (n1 == 0 || tape1->count == 0)
				{
					chosen_tape = tape0;
					n0--;
				}
				else if (tape0->first->sort > tape1->first->sort)
				{
					chosen_tape = tape1;
					n1--;
				}
				else
				{
					chosen_tape = tape0;
					n0--;
				}
				chosen_tape->count--;
				p = chosen_tape->first;
				chosen_tape->first = p->next;
				if (output_tape->count == 0)
					output_tape->first = p;
				else
					output_tape->last->next = p;
				output_tape->last = p;
				output_tape->count++;
			}
		}
	}

	if (tape[base].count > 1)
		tape[base].last->next = NULL;

	dl->head = tape[base].first;
}


/*
=================
R_AddLitSurfFlags
=================
*/
void R_AddLitSurfFlags( surfaceType_t *surface, shader_t *shader, int fogIndex, unsigned int flags )
{
	struct litSurf_s *litsurf;

	if ( tr.refdef.numLitSurfs >= ARRAY_LEN( backEndData->litSurfs ) )
	{
		if ( backEnd.screenshotCubeActive ) {
			tr.cubemapDrawSurfLimitHit = qtrue;
		}
		return;
	}

	tr.pc.c_lit_surfs++;

	litsurf = &tr.refdef.litSurfs[ tr.refdef.numLitSurfs++ ];

	litsurf->sort = (shader->sortedIndex << QSORT_SHADERNUM_SHIFT) 
		| tr.shiftedEntityNum | ( fogIndex << QSORT_FOGNUM_SHIFT );
	litsurf->surface = surface;
	litsurf->flags = flags;

	if ( !tr.light->head )
		tr.light->head = litsurf;
	if ( tr.light->tail )
		tr.light->tail->next = litsurf;

	tr.light->tail = litsurf;
	tr.light->tail->next = NULL;
}

/*
=================
R_AddLitSurf
=================
*/
void R_AddLitSurf( surfaceType_t *surface, shader_t *shader, int fogIndex )
{
	R_AddLitSurfFlags( surface, shader, fogIndex, 0 );
}


/*
=================
R_DecomposeLitSort
=================
*/
void R_DecomposeLitSort( unsigned sort, int *entityNum, shader_t **shader, int *fogNum ) {
	*fogNum = ( sort >> QSORT_FOGNUM_SHIFT ) & FOGNUM_MASK;
	*shader = tr.sortedShaders[ ( sort >> QSORT_SHADERNUM_SHIFT ) & SHADERNUM_MASK ];
	*entityNum = ( sort >> QSORT_REFENTITYNUM_SHIFT ) & REFENTITYNUM_MASK;
}

#endif // USE_PMLIGHT


#ifdef USE_PMLIGHT
#define DLIGHT_SHADOW_MIN_FACE_SIZE 64
#define DLIGHT_SHADOW_MAX_FACE_SIZE 1024
// Under overload, reject candidates far below the strongest shadow candidate.
#define DLIGHT_SHADOW_LOW_VALUE_FRACTION 0.0625f

static int R_DlightShadowFloorPowerOfTwo( int value )
{
	int power;

	power = 1;
	while ( power <= ( 1 << 29 ) && power * 2 <= value ) {
		power *= 2;
	}

	return power;
}


qboolean R_DlightShadowAtlasLayout( int maxLights, int requestedFaceSize, int maxTextureSize, dlightShadowAtlasLayout_t *layout )
{
	int faceSize;
	int totalFaces;

	if ( layout ) {
		Com_Memset( layout, 0, sizeof( *layout ) );
	}
	if ( !layout || maxTextureSize < DLIGHT_SHADOW_MIN_FACE_SIZE ) {
		return qfalse;
	}

	maxLights = Com_Clamp( 0, MAX_DLIGHTS, maxLights );
	if ( maxLights <= 0 ) {
		return qfalse;
	}

	if ( requestedFaceSize <= 0 ) {
		requestedFaceSize = 256;
	}
	requestedFaceSize = Com_Clamp( DLIGHT_SHADOW_MIN_FACE_SIZE, DLIGHT_SHADOW_MAX_FACE_SIZE, requestedFaceSize );
	requestedFaceSize = MIN( requestedFaceSize, maxTextureSize );
	faceSize = R_DlightShadowFloorPowerOfTwo( requestedFaceSize );
	if ( faceSize < DLIGHT_SHADOW_MIN_FACE_SIZE ) {
		faceSize = DLIGHT_SHADOW_MIN_FACE_SIZE;
	}

	totalFaces = maxLights * DLIGHT_SHADOW_FACES;
	while ( faceSize >= DLIGHT_SHADOW_MIN_FACE_SIZE ) {
		int columns;
		int rows;
		int width;
		int height;

		columns = maxTextureSize / faceSize;
		if ( columns <= 0 ) {
			faceSize /= 2;
			continue;
		}
		if ( columns > totalFaces ) {
			columns = totalFaces;
		}
		rows = ( totalFaces + columns - 1 ) / columns;
		width = columns * faceSize;
		height = rows * faceSize;

		if ( width <= maxTextureSize && height <= maxTextureSize ) {
			layout->maxLights = maxLights;
			layout->totalFaces = totalFaces;
			layout->faceSize = faceSize;
			layout->columns = columns;
			layout->rows = rows;
			layout->width = width;
			layout->height = height;
			return qtrue;
		}

		faceSize /= 2;
	}

	return qfalse;
}


qboolean R_SpotShadowAtlasLayout( int maxLights, int requestedTileSize, int maxTextureSize, spotShadowAtlasLayout_t *layout )
{
	int tileSize;

	if ( layout ) {
		Com_Memset( layout, 0, sizeof( *layout ) );
	}
	if ( !layout || maxTextureSize < SPOT_SHADOW_MIN_TILE_SIZE ) {
		return qfalse;
	}

	maxLights = Com_Clamp( 0, SPOT_SHADOW_MAX_LIGHTS, maxLights );
	if ( maxLights <= 0 ) {
		return qfalse;
	}

	if ( requestedTileSize <= 0 ) {
		requestedTileSize = 256;
	}
	requestedTileSize = Com_Clamp( SPOT_SHADOW_MIN_TILE_SIZE, SPOT_SHADOW_MAX_TILE_SIZE, requestedTileSize );
	requestedTileSize = MIN( requestedTileSize, maxTextureSize );
	tileSize = R_DlightShadowFloorPowerOfTwo( requestedTileSize );
	if ( tileSize < SPOT_SHADOW_MIN_TILE_SIZE ) {
		tileSize = SPOT_SHADOW_MIN_TILE_SIZE;
	}

	while ( tileSize >= SPOT_SHADOW_MIN_TILE_SIZE ) {
		int columns;
		int rows;
		int width;
		int height;

		columns = maxTextureSize / tileSize;
		if ( columns <= 0 ) {
			tileSize /= 2;
			continue;
		}
		if ( columns > maxLights ) {
			columns = maxLights;
		}
		rows = ( maxLights + columns - 1 ) / columns;
		width = columns * tileSize;
		height = rows * tileSize;

		if ( width <= maxTextureSize && height <= maxTextureSize ) {
			layout->maxLights = maxLights;
			layout->tileSize = tileSize;
			layout->columns = columns;
			layout->rows = rows;
			layout->width = width;
			layout->height = height;
			return qtrue;
		}

		tileSize /= 2;
	}

	return qfalse;
}


static void R_ClearDlightShadowPlan( dlight_t *dl )
{
	dl->shadowPlanned = qfalse;
	dl->shadowIndex = -1;
	dl->shadowAtlasBaseFace = -1;
	dl->shadowAtlasFaceSize = 0;
	Com_Memset( dl->shadowAtlasX, 0, sizeof( dl->shadowAtlasX ) );
	Com_Memset( dl->shadowAtlasY, 0, sizeof( dl->shadowAtlasY ) );
	dl->shadowReceiverCount = 0;
	dl->shadowPriority = 0.0f;
}


static int R_CountDlightShadowReceivers( const dlight_t *dl )
{
	const struct litSurf_s *surf;
	int count;

	count = 0;
	for ( surf = dl->head; surf != NULL; surf = surf->next ) {
		count++;
	}

	return count;
}


static qboolean R_DlightShadowProjectionValid( const dlight_t *dl )
{
	vec4_t eye, clip, normalized, window;

	R_TransformModelToClip( dl->origin, tr.viewParms.world.modelMatrix, tr.viewParms.projectionMatrix, eye, clip );
	if ( clip[3] <= 0.0f ) {
		return qfalse;
	}

	R_TransformClipToWindow( clip, &tr.viewParms, normalized, window );
	return ( normalized[2] >= 0.0f && normalized[2] <= 1.0f ) ? qtrue : qfalse;
}


static float R_DlightShadowPriority( const dlight_t *dl, int receivers )
{
	vec3_t delta;
	float brightness;
	float dist2;
	float radius2;
	float receiverScale;
	float priorityMultiplier;

	brightness = dl->color[0];
	if ( dl->color[1] > brightness ) {
		brightness = dl->color[1];
	}
	if ( dl->color[2] > brightness ) {
		brightness = dl->color[2];
	}
	if ( brightness <= 0.0f || dl->radius <= 0.0f ) {
		return 0.0f;
	}

	VectorSubtract( dl->origin, tr.viewParms.or.origin, delta );
	dist2 = DotProduct( delta, delta );
	radius2 = Square( dl->radius );
	receiverScale = 1.0f + 0.03125f * Com_Clamp( 0.0f, 64.0f, (float)receivers );
	priorityMultiplier = ( dl->shadowPriorityMultiplier > 0.0f ) ? dl->shadowPriorityMultiplier : 1.0f;

	return brightness * receiverScale * radius2 * priorityMultiplier / ( dist2 + radius2 + 1.0f );
}

static void R_ShadowManagerStorePointLightRecord( shadowPointLightPlan_t *record,
	int dlightIndex, const dlight_t *dl )
{
	Com_Memset( record, 0, sizeof( *record ) );
	record->dlightIndex = dlightIndex;
	record->shadowIndex = -1;
	record->atlasBaseFace = -1;
	record->atlasFaceSize = 0;
	record->receiverCount = dl->shadowReceiverCount;
	record->priority = dl->shadowPriority;
	VectorCopy( dl->origin, record->origin );
	VectorCopy( dl->color, record->color );
	record->radius = dl->radius;
	record->atlasAllocated = qfalse;
}

static void R_ShadowManagerAddPointCandidate( int dlightIndex, const dlight_t *dl )
{
	shadowManager_t *manager = &tr.shadowManager;

	if ( manager->pointCandidateCount >= ARRAY_LEN( manager->pointCandidates ) ) {
		return;
	}

	R_ShadowManagerStorePointLightRecord( &manager->pointCandidates[manager->pointCandidateCount++],
		dlightIndex, dl );
}

static shadowPointLightPlan_t *R_ShadowManagerAddPointPlan( const shadowPointLightPlan_t *source )
{
	shadowManager_t *manager = &tr.shadowManager;
	shadowPointLightPlan_t *record;

	if ( manager->pointPlanCount >= ARRAY_LEN( manager->pointPlans ) ) {
		return NULL;
	}

	record = &manager->pointPlans[manager->pointPlanCount++];
	Com_Memcpy( record, source, sizeof( *record ) );
	return record;
}

static qboolean R_ShadowPointCandidateBetter( const shadowPointLightPlan_t *candidate,
	int candidateIndex, const shadowPointLightPlan_t *best, int bestIndex )
{
	if ( !candidate || candidate->priority <= 0.0f ) {
		return qfalse;
	}
	if ( !best || bestIndex < 0 ) {
		return qtrue;
	}
	if ( candidate->priority > best->priority ) {
		return qtrue;
	}
	if ( candidate->priority < best->priority ) {
		return qfalse;
	}
	if ( candidate->dlightIndex != best->dlightIndex ) {
		return candidate->dlightIndex < best->dlightIndex ? qtrue : qfalse;
	}
	return candidateIndex < bestIndex ? qtrue : qfalse;
}

static void R_ShadowManagerAssignPointAtlas( shadowPointLightPlan_t *record, int shadowIndex,
	const dlightShadowAtlasLayout_t *atlasLayout )
{
	int face;

	record->shadowIndex = shadowIndex;
	record->atlasBaseFace = -1;
	record->atlasFaceSize = 0;
	record->atlasAllocated = qfalse;
	Com_Memset( record->atlasX, 0, sizeof( record->atlasX ) );
	Com_Memset( record->atlasY, 0, sizeof( record->atlasY ) );

	if ( !atlasLayout || atlasLayout->faceSize <= 0 || atlasLayout->columns <= 0 ) {
		return;
	}

	record->atlasBaseFace = record->shadowIndex * DLIGHT_SHADOW_FACES;
	record->atlasFaceSize = atlasLayout->faceSize;
	for ( face = 0; face < DLIGHT_SHADOW_FACES; face++ ) {
		int tile = record->atlasBaseFace + face;
		record->atlasX[face] = ( tile % atlasLayout->columns ) * atlasLayout->faceSize;
		record->atlasY[face] = ( tile / atlasLayout->columns ) * atlasLayout->faceSize;
	}
	record->atlasAllocated = qtrue;
}

static qboolean R_ShadowManagerPlanPointAtlas( int maxLights, int requestedFaceSize, int maxTextureSize )
{
	shadowManager_t *manager = &tr.shadowManager;

	manager->pointAtlasReady = R_DlightShadowAtlasLayout( maxLights, requestedFaceSize,
		maxTextureSize, &manager->pointAtlasLayout );
	if ( manager->pointAtlasReady ) {
		tr.pc.c_dlightShadowAtlasWidth = manager->pointAtlasLayout.width;
		tr.pc.c_dlightShadowAtlasHeight = manager->pointAtlasLayout.height;
		tr.pc.c_dlightShadowAtlasFaceSize = manager->pointAtlasLayout.faceSize;
	}
	return manager->pointAtlasReady;
}

static qboolean R_ShadowManagerPlanSpotAtlas( int maxLights, int requestedTileSize, int maxTextureSize )
{
	shadowManager_t *manager = &tr.shadowManager;

	manager->spotAtlasReady = R_SpotShadowAtlasLayout( maxLights, requestedTileSize,
		maxTextureSize, &manager->spotAtlasLayout );
	return manager->spotAtlasReady;
}

#define SPOT_SHADOW_SURFACELIGHT_INNER_ANGLE 0.0f
#define SPOT_SHADOW_SURFACELIGHT_OUTER_ANGLE 75.0f

static float R_ShadowSpotBrightness( const vec3_t color )
{
	float brightness;

	brightness = color[0];
	if ( color[1] > brightness ) {
		brightness = color[1];
	}
	if ( color[2] > brightness ) {
		brightness = color[2];
	}
	return brightness;
}

static void R_ShadowSpotTelemetryRange( int *minValue, int *maxValue, int value )
{
	if ( value <= 0 || !minValue || !maxValue ) {
		return;
	}
	if ( *minValue <= 0 || value < *minValue ) {
		*minValue = value;
	}
	if ( value > *maxValue ) {
		*maxValue = value;
	}
}

static int R_ShadowSpotTelemetryAngle( float angle )
{
	return (int)Com_Clamp( 0.0f, 180.0f, angle + 0.5f );
}

static qboolean R_ShadowSpotSurfaceMalformed( const surfaceLightProxy_t *proxy )
{
	if ( !proxy || proxy->radius <= 0.0f || proxy->area <= 1.0f ||
		proxy->shadowCasterRadius <= 0.0f || proxy->shadowConeAngle <= 0.0f ||
		DotProduct( proxy->normal, proxy->normal ) <= 0.0f ) {
		return qtrue;
	}
	return qfalse;
}

static qboolean R_ShadowSpotVisibleInPVS( const vec3_t origin, float radius,
	int leafCluster, int leafArea )
{
	vec3_t sample;
	float sampleRadius;
	int axis;

	if ( !tr.world || ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		return qtrue;
	}
	if ( leafCluster >= 0 ) {
		if ( R_LeafClusterInCurrentPVS( tr.refdef.vieworg, leafCluster, leafArea ) ) {
			return qtrue;
		}
	} else if ( R_PointInCurrentPVS( tr.refdef.vieworg, origin ) ) {
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
		if ( R_PointInCurrentPVS( tr.refdef.vieworg, sample ) ) {
			return qtrue;
		}
		sample[axis] -= sampleRadius * 2.0f;
		if ( R_PointInCurrentPVS( tr.refdef.vieworg, sample ) ) {
			return qtrue;
		}
	}

	return qfalse;
}

static void R_ShadowSpotStaticEnd( const mapLightDef_t *light, vec3_t end )
{
	VectorMA( light->origin, light->radius, light->direction, end );
}

static qboolean R_ShadowSpotStaticVisible( const mapLightDef_t *light )
{
	vec3_t end;

	if ( R_ShadowSpotVisibleInPVS( light->origin, light->radius,
		light->leafCluster, light->leafArea ) ) {
		return qtrue;
	}

	R_ShadowSpotStaticEnd( light, end );
	return R_ShadowSpotVisibleInPVS( end, light->radius * 0.25f, -1, -1 );
}

static float R_ShadowSpotStaticPriority( const mapLightDef_t *light )
{
	vec3_t delta;
	vec3_t toView;
	float brightness;
	float dist2;
	float radius2;
	float directionalWeight;

	brightness = R_ShadowSpotBrightness( light->color );
	if ( brightness <= 0.0f || light->radius <= 0.0f ||
		light->intensity <= 0.0f || light->designerPriority <= 0.0f ) {
		return 0.0f;
	}

	VectorSubtract( light->origin, tr.refdef.vieworg, delta );
	dist2 = DotProduct( delta, delta );
	radius2 = Square( light->radius );
	directionalWeight = 1.0f;
	VectorSubtract( tr.refdef.vieworg, light->origin, toView );
	if ( VectorNormalize2( toView, toView ) > 1.0f ) {
		float facing = DotProduct( light->direction, toView );
		directionalWeight = Com_Clamp( 0.15f, 1.0f, 0.35f + 0.65f * facing );
	}

	return brightness * directionalWeight * light->designerPriority * light->intensity * radius2 /
		( dist2 + radius2 + 1.0f );
}

static float R_ShadowSpotSurfaceHemisphereWeight( const surfaceLightProxy_t *proxy )
{
	vec3_t toView;
	float facing;

	if ( proxy->area <= Square( 64.0f ) ) {
		return 1.0f;
	}

	VectorSubtract( tr.refdef.vieworg, proxy->origin, toView );
	if ( VectorNormalize2( toView, toView ) <= 1.0f ) {
		return 1.0f;
	}

	facing = DotProduct( proxy->normal, toView );
	if ( facing <= -0.35f ) {
		return 0.05f;
	}

	return Com_Clamp( 0.15f, 1.0f, 0.35f + 0.65f * facing );
}

static float R_ShadowSpotSurfaceViewWeight( const surfaceLightProxy_t *proxy,
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

	VectorSubtract( proxy->origin, tr.refdef.vieworg, delta );
	forward = DotProduct( delta, tr.refdef.viewaxis[0] );
	radius = ( proxy->shadowCasterRadius > 0.0f ) ?
		proxy->shadowCasterRadius : proxy->radius;
	radius = Com_Clamp( 32.0f, 1024.0f, radius );
	if ( forward <= -radius ) {
		return 0.08f;
	}
	if ( forward <= radius ) {
		return 0.75f;
	}

	side = fabsf( DotProduct( delta, tr.refdef.viewaxis[1] ) );
	up = fabsf( DotProduct( delta, tr.refdef.viewaxis[2] ) );
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

static float R_ShadowSpotSurfacePriority( const surfaceLightProxy_t *proxy,
	float tanHalfFovX, float tanHalfFovY, float *hemisphereWeight, float *viewWeight )
{
	vec3_t delta;
	float brightness;
	float dist2;
	float priorityRadius;
	float radius2;

	*hemisphereWeight = R_ShadowSpotSurfaceHemisphereWeight( proxy );
	*viewWeight = R_ShadowSpotSurfaceViewWeight( proxy, tanHalfFovX, tanHalfFovY );

	brightness = R_ShadowSpotBrightness( proxy->color );
	if ( brightness <= 0.0f || proxy->radius <= 0.0f ||
		proxy->intensity <= 0.0f || proxy->designerPriority <= 0.0f ||
		*hemisphereWeight <= 0.0f || *viewWeight <= 0.0f ) {
		return 0.0f;
	}

	VectorSubtract( proxy->origin, tr.refdef.vieworg, delta );
	dist2 = DotProduct( delta, delta );
	priorityRadius = ( proxy->shadowCasterRadius > 0.0f ) ?
		proxy->shadowCasterRadius : proxy->radius;
	radius2 = Square( priorityRadius );

	return brightness * proxy->designerPriority * proxy->intensity *
		*hemisphereWeight * *viewWeight * radius2 / ( dist2 + radius2 + 1.0f );
}

static int R_ShadowSpotSurfaceRequestedTileSize( const surfaceLightProxy_t *proxy,
	float hemisphereWeight, float viewWeight )
{
	int tileSize;
	float importance;

	tileSize = r_spotShadowResolution ? r_spotShadowResolution->integer : 256;
	if ( !proxy ) {
		return tileSize;
	}

	importance = Com_Clamp( 0.0f, 1.0f, hemisphereWeight ) *
		Com_Clamp( 0.0f, 1.0f, viewWeight );
	if ( importance < 0.45f || proxy->designerPriority < 0.75f ||
		proxy->radius <= 384.0f || proxy->area <= Square( 192.0f ) ) {
		tileSize = MIN( tileSize, 128 );
	} else if ( importance >= 0.85f && proxy->designerPriority >= 1.0f &&
		( proxy->radius >= 768.0f || proxy->area >= Square( 384.0f ) ) ) {
		tileSize = MAX( tileSize, 512 );
	}

	return Com_Clamp( SPOT_SHADOW_MIN_TILE_SIZE, SPOT_SHADOW_MAX_TILE_SIZE, tileSize );
}

static void R_ShadowManagerStoreSpotRecord( shadowSpotLightPlan_t *record,
	shadowSpotLightSource_t source, int sourceIndex, const vec3_t origin, const vec3_t direction,
	const vec3_t color, float radius, float intensity, float innerAngle, float outerAngle,
	int requestedTileSize, float priority )
{
	Com_Memset( record, 0, sizeof( *record ) );
	record->source = source;
	record->sourceIndex = sourceIndex;
	record->shadowIndex = -1;
	record->atlasTileSize = 0;
	record->requestedTileSize = Com_Clamp( SPOT_SHADOW_MIN_TILE_SIZE,
		SPOT_SHADOW_MAX_TILE_SIZE, requestedTileSize );
	record->priority = priority;
	VectorCopy( origin, record->origin );
	VectorCopy( direction, record->direction );
	if ( VectorNormalize2( record->direction, record->direction ) <= 0.0f ) {
		VectorSet( record->direction, 0.0f, 0.0f, -1.0f );
	}
	VectorCopy( color, record->color );
	record->radius = radius;
	record->intensity = intensity;
	record->innerAngle = innerAngle;
	record->outerAngle = outerAngle;
	record->atlasAllocated = qfalse;
}

static void R_ShadowManagerRecordSurfaceSpotCandidateTelemetry(
	const shadowSpotLightPlan_t *record )
{
	shadowManager_t *manager = &tr.shadowManager;

	if ( !record || record->source != SHADOW_SPOT_SOURCE_SURFACELIGHT_PROXY ) {
		return;
	}

	R_ShadowSpotTelemetryRange( &manager->spotSurfaceCandidateTileMin,
		&manager->spotSurfaceCandidateTileMax, record->requestedTileSize );
	if ( record->sourceIndex >= 0 &&
		record->sourceIndex < tr.surfaceLightProxies.count ) {
		const surfaceLightProxy_t *proxy =
			&tr.surfaceLightProxies.proxies[record->sourceIndex];

		R_ShadowSpotTelemetryRange( &manager->spotSurfaceFootprintMin,
			&manager->spotSurfaceFootprintMax, (int)( proxy->footprintRadius + 0.5f ) );
	}
	R_ShadowSpotTelemetryRange( &manager->spotSurfaceCasterRadiusMin,
		&manager->spotSurfaceCasterRadiusMax, (int)( record->radius + 0.5f ) );
	R_ShadowSpotTelemetryRange( &manager->spotSurfaceConeInnerMin,
		&manager->spotSurfaceConeInnerMax,
		R_ShadowSpotTelemetryAngle( record->innerAngle ) );
	R_ShadowSpotTelemetryRange( &manager->spotSurfaceConeOuterMin,
		&manager->spotSurfaceConeOuterMax,
		R_ShadowSpotTelemetryAngle( record->outerAngle ) );
}

static void R_ShadowManagerRecordSurfaceSpotPlanTelemetry(
	const shadowSpotLightPlan_t *record )
{
	shadowManager_t *manager = &tr.shadowManager;

	if ( !record || record->source != SHADOW_SPOT_SOURCE_SURFACELIGHT_PROXY ) {
		return;
	}

	R_ShadowSpotTelemetryRange( &manager->spotSurfacePlanRequestedTileMin,
		&manager->spotSurfacePlanRequestedTileMax, record->requestedTileSize );
	if ( record->atlasAllocated && record->atlasTileSize > 0 ) {
		manager->spotSurfacePlanAllocatedCount++;
		R_ShadowSpotTelemetryRange( &manager->spotSurfacePlanTileMin,
			&manager->spotSurfacePlanTileMax, record->atlasTileSize );
	}
}

static void R_ShadowManagerAddSpotCandidate( shadowSpotLightSource_t source, int sourceIndex,
	const vec3_t origin, const vec3_t direction, const vec3_t color, float radius,
	float intensity, float innerAngle, float outerAngle, int requestedTileSize, float priority )
{
	shadowManager_t *manager = &tr.shadowManager;
	shadowSpotLightPlan_t *record;

	if ( manager->spotCandidateCount >= ARRAY_LEN( manager->spotCandidates ) ) {
		return;
	}

	record = &manager->spotCandidates[manager->spotCandidateCount++];
	R_ShadowManagerStoreSpotRecord( record,
		source, sourceIndex, origin, direction, color, radius, intensity, innerAngle,
		outerAngle, requestedTileSize, priority );
	if ( source == SHADOW_SPOT_SOURCE_STATIC_MAP ) {
		manager->spotStaticCandidateCount++;
	} else if ( source == SHADOW_SPOT_SOURCE_SURFACELIGHT_PROXY ) {
		manager->spotSurfaceCandidateCount++;
		R_ShadowManagerRecordSurfaceSpotCandidateTelemetry( record );
	}
}

static shadowSpotLightPlan_t *R_ShadowManagerAddSpotPlan( const shadowSpotLightPlan_t *source )
{
	shadowManager_t *manager = &tr.shadowManager;
	shadowSpotLightPlan_t *record;

	if ( manager->spotPlanCount >= ARRAY_LEN( manager->spotPlans ) ) {
		return NULL;
	}

	record = &manager->spotPlans[manager->spotPlanCount++];
	Com_Memcpy( record, source, sizeof( *record ) );
	if ( source->source == SHADOW_SPOT_SOURCE_STATIC_MAP ) {
		manager->spotStaticPlanCount++;
	} else if ( source->source == SHADOW_SPOT_SOURCE_SURFACELIGHT_PROXY ) {
		manager->spotSurfacePlanCount++;
	}
	return record;
}

static qboolean R_ShadowSpotCandidateBetter( const shadowSpotLightPlan_t *candidate,
	int candidateIndex, const shadowSpotLightPlan_t *best, int bestIndex )
{
	if ( !candidate || candidate->priority <= 0.0f ) {
		return qfalse;
	}
	if ( !best || bestIndex < 0 ) {
		return qtrue;
	}
	if ( candidate->priority > best->priority ) {
		return qtrue;
	}
	if ( candidate->priority < best->priority ) {
		return qfalse;
	}
	if ( candidate->source != best->source ) {
		return candidate->source < best->source ? qtrue : qfalse;
	}
	if ( candidate->sourceIndex != best->sourceIndex ) {
		return candidate->sourceIndex < best->sourceIndex ? qtrue : qfalse;
	}
	return candidateIndex < bestIndex ? qtrue : qfalse;
}

static int R_ShadowSpotEffectiveTileSize( int requestedTileSize,
	const spotShadowAtlasLayout_t *atlasLayout )
{
	int tileSize;

	if ( !atlasLayout || atlasLayout->tileSize <= 0 ) {
		return 0;
	}
	if ( requestedTileSize <= 0 ) {
		return atlasLayout->tileSize;
	}

	requestedTileSize = Com_Clamp( SPOT_SHADOW_MIN_TILE_SIZE,
		SPOT_SHADOW_MAX_TILE_SIZE, requestedTileSize );
	tileSize = R_DlightShadowFloorPowerOfTwo( requestedTileSize );
	if ( tileSize < SPOT_SHADOW_MIN_TILE_SIZE ) {
		tileSize = SPOT_SHADOW_MIN_TILE_SIZE;
	}

	return MIN( tileSize, atlasLayout->tileSize );
}

static void R_ShadowManagerAssignSpotAtlas( shadowSpotLightPlan_t *record, int shadowIndex,
	const spotShadowAtlasLayout_t *atlasLayout )
{
	int cellX;
	int cellY;
	int tileSize;

	record->shadowIndex = shadowIndex;
	record->atlasX = 0;
	record->atlasY = 0;
	record->atlasTileSize = 0;
	record->atlasAllocated = qfalse;

	if ( !atlasLayout || atlasLayout->tileSize <= 0 || atlasLayout->columns <= 0 ) {
		return;
	}

	tileSize = R_ShadowSpotEffectiveTileSize( record->requestedTileSize, atlasLayout );
	if ( tileSize <= 0 ) {
		return;
	}

	cellX = ( shadowIndex % atlasLayout->columns ) * atlasLayout->tileSize;
	cellY = ( shadowIndex / atlasLayout->columns ) * atlasLayout->tileSize;
	record->atlasX = cellX + ( atlasLayout->tileSize - tileSize ) / 2;
	record->atlasY = cellY + ( atlasLayout->tileSize - tileSize ) / 2;
	record->atlasTileSize = tileSize;
	record->atlasAllocated = qtrue;
}

static void R_ShadowManagerCollectStaticSpotCandidates( void )
{
	int i;

	if ( !r_staticLights || !r_staticLights->integer ||
		!r_staticLightShadows || !r_staticLightShadows->integer ||
		!tr.staticMapLights.loaded || tr.staticMapLights.parseFailed ) {
		return;
	}

	for ( i = 0; i < tr.staticMapLights.count; i++ ) {
		const mapLightDef_t *light = &tr.staticMapLights.lights[i];
		float priority;

		if ( light->type != MAP_LIGHT_SPOT || !light->castsShadows ||
			!R_ShadowSpotStaticVisible( light ) ) {
			continue;
		}

		priority = R_ShadowSpotStaticPriority( light );
		if ( priority <= 0.0f ) {
			continue;
		}

		R_ShadowManagerAddSpotCandidate( SHADOW_SPOT_SOURCE_STATIC_MAP, i,
			light->origin, light->direction, light->color, light->radius, light->intensity,
			light->innerAngle, light->outerAngle, light->resolution, priority );
	}
}

static void R_ShadowManagerCollectSurfaceSpotCandidates( void )
{
	float tanHalfFovX;
	float tanHalfFovY;
	int i;

	if ( !r_surfaceLightProxies || !r_surfaceLightProxies->integer ||
		!r_surfaceLightProxyShadows || !r_surfaceLightProxyShadows->integer ||
		!tr.surfaceLightProxies.built ) {
		return;
	}

	tanHalfFovX = tanf( Com_Clamp( 30.0f, 160.0f, tr.refdef.fov_x ) * M_PI / 360.0f );
	tanHalfFovY = tanf( Com_Clamp( 30.0f, 160.0f, tr.refdef.fov_y ) * M_PI / 360.0f );

	for ( i = 0; i < tr.surfaceLightProxies.count; i++ ) {
		const surfaceLightProxy_t *proxy = &tr.surfaceLightProxies.proxies[i];
		float hemisphereWeight;
		float viewWeight;
		float casterRadius;
		float outerAngle;
		float priority;
		int requestedTileSize;

		if ( proxy->projection != SURFACE_LIGHT_PROXY_SPOT || !proxy->castsShadows ) {
			continue;
		}
		if ( R_ShadowSpotSurfaceMalformed( proxy ) ) {
			tr.shadowManager.spotSurfaceRejectedMalformed++;
			continue;
		}
		if ( R_ShadowSpotBrightness( proxy->color ) <= 0.0f ||
			proxy->intensity <= 0.0f || proxy->designerPriority <= 0.0f ) {
			tr.shadowManager.spotSurfaceRejectedWeak++;
			continue;
		}
		if ( !R_ShadowSpotVisibleInPVS( proxy->origin, proxy->radius,
				proxy->leafCluster, proxy->leafArea ) ) {
			tr.shadowManager.spotSurfaceRejectedOffView++;
			continue;
		}

		priority = R_ShadowSpotSurfacePriority( proxy, tanHalfFovX, tanHalfFovY,
			&hemisphereWeight, &viewWeight );
		if ( hemisphereWeight < 0.25f || viewWeight < 0.25f ) {
			tr.shadowManager.spotSurfaceRejectedOffView++;
			continue;
		}
		if ( priority <= 0.0f ) {
			tr.shadowManager.spotSurfaceRejectedWeak++;
			continue;
		}

		requestedTileSize = R_ShadowSpotSurfaceRequestedTileSize( proxy,
			hemisphereWeight, viewWeight );
		casterRadius = ( proxy->shadowCasterRadius > 0.0f ) ?
			proxy->shadowCasterRadius : proxy->radius;
		outerAngle = ( proxy->shadowConeAngle > 0.0f ) ?
			proxy->shadowConeAngle : SPOT_SHADOW_SURFACELIGHT_OUTER_ANGLE;
		R_ShadowManagerAddSpotCandidate( SHADOW_SPOT_SOURCE_SURFACELIGHT_PROXY, i,
			proxy->origin, proxy->normal, proxy->color, casterRadius, proxy->intensity,
			SPOT_SHADOW_SURFACELIGHT_INNER_ANGLE, outerAngle, requestedTileSize, priority );
	}
}

static void R_ShadowManagerSelectSpotPlans( int maxLights )
{
	qboolean selected[MAX_STATIC_MAP_LIGHTS + MAX_SURFACELIGHT_PROXIES];
	int pass;

	Com_Memset( selected, 0, sizeof( selected ) );
	for ( pass = 0; pass < maxLights; pass++ ) {
		const shadowSpotLightPlan_t *candidate;
		const shadowSpotLightPlan_t *bestCandidate;
		shadowSpotLightPlan_t *plan;
		float bestPriority;
		int bestIndex;
		int i;

		bestPriority = 0.0f;
		bestIndex = -1;
		bestCandidate = NULL;
		for ( i = 0; i < tr.shadowManager.spotCandidateCount; i++ ) {
			candidate = &tr.shadowManager.spotCandidates[i];
			if ( !selected[i] &&
				R_ShadowSpotCandidateBetter( candidate, i, bestCandidate, bestIndex ) ) {
				bestPriority = candidate->priority;
				bestIndex = i;
				bestCandidate = candidate;
			}
		}

		if ( bestIndex < 0 || bestPriority <= 0.0f ) {
			break;
		}

		selected[bestIndex] = qtrue;
		candidate = &tr.shadowManager.spotCandidates[bestIndex];
		plan = R_ShadowManagerAddSpotPlan( candidate );
		if ( !plan ) {
			break;
		}
		R_ShadowManagerAssignSpotAtlas( plan, pass,
			tr.shadowManager.spotAtlasReady ? &tr.shadowManager.spotAtlasLayout : NULL );
		R_ShadowManagerRecordSurfaceSpotPlanTelemetry( plan );
	}
	if ( tr.shadowManager.spotSurfaceCandidateCount > tr.shadowManager.spotSurfacePlanCount ) {
		tr.shadowManager.spotSurfaceRejectedOverBudget =
			tr.shadowManager.spotSurfaceCandidateCount - tr.shadowManager.spotSurfacePlanCount;
	}
}

static void R_ShadowManagerCollectSpotCandidates( void )
{
	R_ShadowManagerCollectStaticSpotCandidates();
	R_ShadowManagerCollectSurfaceSpotCandidates();
}


static void R_PlanSpotShadows( void )
{
	shadowManager_t *manager = &tr.shadowManager;
	int maxLights;

	if ( ( r_shadowCorrectness && r_shadowCorrectness->integer ) ||
		!r_spotShadows || !r_spotShadows->integer ||
		( tr.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		return;
	}

	maxLights = r_spotShadowMaxLights ? r_spotShadowMaxLights->integer : 16;
	maxLights = Com_Clamp( 0, SPOT_SHADOW_MAX_LIGHTS, maxLights );
	if ( maxLights <= 0 ) {
		return;
	}

	R_ShadowManagerPlanSpotAtlas( maxLights,
		r_spotShadowResolution ? r_spotShadowResolution->integer : 256,
		glConfig.maxTextureSize );
	R_ShadowManagerCollectSpotCandidates();
	if ( manager->spotCandidateCount <= 0 ) {
		return;
	}
	R_ShadowManagerSelectSpotPlans( maxLights );
}


static void R_PlanDlightShadows( void )
{
	dlight_t *dl;
	qboolean selected[MAX_REAL_DLIGHTS];
	int i, pass;
	int maxLights;
	int candidates;
	int planned;
	float strongestPriority;
	float throttleFloor;
	qboolean correctnessMode;

	candidates = 0;
	planned = 0;
	strongestPriority = 0.0f;
	throttleFloor = 0.0f;
	correctnessMode = ( r_shadowCorrectness && r_shadowCorrectness->integer ) ? qtrue : qfalse;
	maxLights = r_dlightShadowMaxLights ? r_dlightShadowMaxLights->integer : 4;
	if ( maxLights < 0 ) {
		maxLights = 0;
	}
	if ( maxLights > MAX_DLIGHTS ) {
		maxLights = MAX_DLIGHTS;
	}
	if ( correctnessMode ) {
		maxLights = 1;
	}

	for ( i = 0; i < tr.viewParms.num_dlights; i++ ) {
		R_ClearDlightShadowPlan( &tr.viewParms.dlights[i] );
	}
	Com_Memset( selected, 0, sizeof( selected ) );

	if ( ( !correctnessMode && ( !r_dlightShadows || !r_dlightShadows->integer ) ) ||
		!r_dlightMode || !r_dlightMode->integer ||
		( tr.refdef.rdflags & RDF_NOWORLDMODEL ) ) {
		tr.pc.c_dlightShadowSkippedDisabled += tr.viewParms.num_dlights;
		return;
	}

	R_ShadowManagerPlanPointAtlas( maxLights,
		r_dlightShadowResolution ? r_dlightShadowResolution->integer : 256,
		glConfig.maxTextureSize );

	for ( i = 0; i < tr.viewParms.num_dlights; i++ ) {
		int receivers;

		dl = &tr.viewParms.dlights[i];
		tr.pc.c_dlightShadowConsidered++;

		if ( !dl->shadowEligible ) {
			tr.pc.c_dlightShadowSkippedDisabled++;
			continue;
		}
		if ( dl->linear ) {
			tr.pc.c_dlightShadowSkippedLinear++;
			continue;
		}
		if ( dl->head == NULL ) {
			tr.pc.c_dlightShadowSkippedNoSurfaces++;
			continue;
		}
		if ( !R_DlightShadowProjectionValid( dl ) ) {
			tr.pc.c_dlightShadowSkippedProjection++;
			continue;
		}

		receivers = R_CountDlightShadowReceivers( dl );
		if ( receivers <= 0 ) {
			tr.pc.c_dlightShadowSkippedNoSurfaces++;
			continue;
		}

		dl->shadowReceiverCount = receivers;
		dl->shadowPriority = R_DlightShadowPriority( dl, receivers );
		if ( dl->shadowPriority <= 0.0f ) {
			tr.pc.c_dlightShadowSkippedDisabled++;
			continue;
		}
		if ( dl->shadowPriority > strongestPriority ) {
			strongestPriority = dl->shadowPriority;
		}

		tr.pc.c_dlightShadowCandidates++;
		R_ShadowManagerAddPointCandidate( i, dl );
		candidates++;
	}

	if ( maxLights > 0 && candidates > maxLights && strongestPriority > 0.0f ) {
		throttleFloor = strongestPriority * DLIGHT_SHADOW_LOW_VALUE_FRACTION;
	}

	for ( pass = 0; pass < maxLights; pass++ ) {
		const shadowPointLightPlan_t *candidate;
		const shadowPointLightPlan_t *bestCandidate;
		shadowPointLightPlan_t *plan;
		float bestPriority;
		int bestIndex;

		bestPriority = 0.0f;
		bestIndex = -1;
		bestCandidate = NULL;
		for ( i = 0; i < tr.shadowManager.pointCandidateCount; i++ ) {
			candidate = &tr.shadowManager.pointCandidates[i];
			if ( !selected[i] &&
				R_ShadowPointCandidateBetter( candidate, i, bestCandidate, bestIndex ) ) {
				bestPriority = candidate->priority;
				bestIndex = i;
				bestCandidate = candidate;
			}
		}

		if ( bestIndex < 0 ) {
			break;
		}
		if ( throttleFloor > 0.0f && bestPriority < throttleFloor ) {
			break;
		}

		selected[bestIndex] = qtrue;
		candidate = &tr.shadowManager.pointCandidates[bestIndex];
		if ( candidate->dlightIndex < 0 || candidate->dlightIndex >= tr.viewParms.num_dlights ) {
			continue;
		}
		plan = R_ShadowManagerAddPointPlan( candidate );
		if ( !plan ) {
			break;
		}
		R_ShadowManagerAssignPointAtlas( plan, planned,
			tr.shadowManager.pointAtlasReady ? &tr.shadowManager.pointAtlasLayout : NULL );
		planned++;
		tr.pc.c_dlightShadowPlanned++;
	}

	if ( candidates > planned ) {
		int budgetSkipped;
		int lowValueSkipped;

		budgetSkipped = 0;
		lowValueSkipped = 0;
		for ( i = 0; i < tr.shadowManager.pointCandidateCount; i++ ) {
			const shadowPointLightPlan_t *candidate = &tr.shadowManager.pointCandidates[i];

			if ( selected[i] || candidate->priority <= 0.0f ) {
				continue;
			}
			if ( throttleFloor > 0.0f && candidate->priority < throttleFloor ) {
				lowValueSkipped++;
			} else {
				budgetSkipped++;
			}
		}

		tr.pc.c_dlightShadowSkippedBudget += budgetSkipped;
		tr.pc.c_dlightShadowSkippedLowValue += lowValueSkipped;
	}

	if ( tr.shadowManager.pointAtlasReady &&
		tr.shadowManager.pointAtlasLayout.width > 0 &&
		tr.shadowManager.pointAtlasLayout.height > 0 && planned > 0 ) {
		int64_t usedPixels;
		int64_t atlasPixels;
		int fill;

		usedPixels = (int64_t)planned * DLIGHT_SHADOW_FACES *
			tr.shadowManager.pointAtlasLayout.faceSize * tr.shadowManager.pointAtlasLayout.faceSize;
		atlasPixels = (int64_t)tr.shadowManager.pointAtlasLayout.width *
			tr.shadowManager.pointAtlasLayout.height;
		fill = (int)( usedPixels * 100 / atlasPixels );
		tr.pc.c_dlightShadowAtlasFill = Com_Clamp( 1, 100, fill );
	}
}
#endif // USE_PMLIGHT

static void R_ClearShadowManager( void )
{
	Com_Memset( &tr.shadowManager, 0, sizeof( tr.shadowManager ) );
	Com_Memset( &tr.shadowCorrectnessDebug, 0, sizeof( tr.shadowCorrectnessDebug ) );
	tr.shadowManager.frameSceneNum = tr.viewParms.frameSceneNum;
	tr.shadowManager.frameCount = tr.viewParms.frameCount;
	tr.shadowManager.viewCount = tr.viewCount;
	tr.shadowManager.inputDlights = tr.viewParms.num_dlights;
	tr.shadowManager.noWorldModel =
		( ( tr.refdef.rdflags & RDF_NOWORLDMODEL ) ||
			( r_csmDebugFallback && r_csmDebugFallback->integer == 1 ) ) ? qtrue : qfalse;
}

static void R_ShadowManagerSchedulePass( shadowManager_t *manager, shadowManagerPass_t pass,
	qboolean *scheduled, qboolean condition )
{
	if ( !manager || !scheduled ) {
		return;
	}

	*scheduled = condition ? qtrue : qfalse;
	if ( *scheduled ) {
		manager->scheduledPassMask |= (unsigned int)pass;
		if ( manager->scheduledPasses < SHADOW_MANAGER_MAX_SCHEDULED_PASSES ) {
			manager->scheduledPassOrder[manager->scheduledPasses] = pass;
		}
		manager->scheduledPasses++;
	}
}

static void R_UpdateShadowManagerSummary( void )
{
	shadowManager_t *manager = &tr.shadowManager;

	manager->planned = qtrue;
	manager->csmPlanned = ( tr.csm.enabled && tr.csm.cascadeCount > 0 ) ? qtrue : qfalse;
	manager->csmCascadeCount = tr.csm.cascadeCount;
	manager->csmResolution = tr.csm.resolution;
	manager->csmPlan = tr.csm;
	if ( manager->csmPlanned ) {
		manager->csmAtlasWidth = tr.csm.cascadeCount * tr.csm.resolution;
		manager->csmAtlasHeight = tr.csm.resolution;
	}
	R_ShadowManagerPublishCSMAtlas( manager, qfalse, 0 );
	manager->scheduledPasses = 0;
	manager->scheduledPassMask = 0;
	R_ShadowManagerSchedulePass( manager, SHADOW_MANAGER_PASS_CSM_ATLAS,
		&manager->csmAtlasScheduled, manager->csmPlanned );
	R_ShadowManagerSchedulePass( manager, SHADOW_MANAGER_PASS_CSM_RECEIVER,
		&manager->csmReceiverScheduled, manager->csmPlanned );

#ifdef USE_PMLIGHT
	manager->dlightConsidered = tr.pc.c_dlightShadowConsidered;
	manager->dlightCandidates = manager->pointCandidateCount;
	manager->dlightPlannedCount = manager->pointPlanCount;
	manager->dlightPlanned = ( manager->pointPlanCount > 0 ) ? qtrue : qfalse;
	if ( manager->pointAtlasReady ) {
		manager->dlightAtlasWidth = manager->pointAtlasLayout.width;
		manager->dlightAtlasHeight = manager->pointAtlasLayout.height;
		manager->dlightAtlasFaceSize = manager->pointAtlasLayout.faceSize;
	}
	manager->dlightAtlasFill = tr.pc.c_dlightShadowAtlasFill;
	R_ShadowManagerPublishPointAtlas( manager, qfalse, 0 );
	R_ShadowManagerSchedulePass( manager, SHADOW_MANAGER_PASS_POINT_ATLAS,
		&manager->pointAtlasScheduled,
		( manager->pointPlanCount > 0 && manager->pointAtlasReady ) ? qtrue : qfalse );
	if ( manager->spotAtlasReady ) {
		manager->spotAtlasWidth = manager->spotAtlasLayout.width;
		manager->spotAtlasHeight = manager->spotAtlasLayout.height;
		manager->spotAtlasTileSize = manager->spotAtlasLayout.tileSize;
	}
	R_ShadowManagerPublishSpotAtlas( manager, qfalse, 0 );
	if ( manager->spotAtlasReady &&
		manager->spotAtlasLayout.width > 0 &&
		manager->spotAtlasLayout.height > 0 &&
		manager->spotPlanCount > 0 ) {
		int64_t usedPixels = 0;
		int64_t atlasPixels = (int64_t)manager->spotAtlasLayout.width *
			manager->spotAtlasLayout.height;
		int i;
		int fill;

		for ( i = 0; i < manager->spotPlanCount; i++ ) {
			const shadowSpotLightPlan_t *plan = &manager->spotPlans[i];
			if ( plan->atlasAllocated && plan->atlasTileSize > 0 ) {
				usedPixels += (int64_t)plan->atlasTileSize * plan->atlasTileSize;
			}
		}
		fill = (int)( usedPixels * 100 / atlasPixels );
		manager->spotAtlasFill = Com_Clamp( 1, 100, fill );
	}
	manager->spotAtlasScheduled =
		( manager->spotPlanCount > 0 && manager->spotAtlasReady &&
			vk_spot_shadow_atlas_available() ) ? qtrue : qfalse;
	R_ShadowManagerSchedulePass( manager, SHADOW_MANAGER_PASS_SPOT_ATLAS,
		&manager->spotAtlasScheduled, manager->spotAtlasScheduled );
#endif
}

static void R_PlanFrameShadows( void )
{
	R_ClearShadowManager();
	R_PlanCascadedShadows();
#ifdef USE_PMLIGHT
	R_PlanDlightShadows();
	R_PlanSpotShadows();
#endif
	R_UpdateShadowManagerSummary();
}


//==========================================================================================

/*
=================
R_AddDrawSurfFlags
=================
*/
void R_AddDrawSurfFlags( surfaceType_t *surface, shader_t *shader,
				   int fogIndex, int dlightMap, unsigned int flags ) {
	int			index;

	if ( tr.cubemapDrawSurfLimit > 0 && tr.refdef.numDrawSurfs >= tr.cubemapDrawSurfLimit ) {
		tr.cubemapDrawSurfLimitHit = qtrue;
		return;
	}

	// instead of checking for overflow, we just mask the index
	// so it wraps around
	index = tr.refdef.numDrawSurfs & DRAWSURF_MASK;
	// the sort data is packed into a single 32 bit value so it can be
	// compared quickly during the qsorting process
	tr.refdef.drawSurfs[index].sort = (shader->sortedIndex << QSORT_SHADERNUM_SHIFT) 
		| tr.shiftedEntityNum | ( fogIndex << QSORT_FOGNUM_SHIFT ) | (int)dlightMap;
	tr.refdef.drawSurfs[index].surface = surface;
	tr.refdef.drawSurfs[index].flags = flags;
	tr.refdef.numDrawSurfs++;
}

/*
=================
R_AddDrawSurf
=================
*/
void R_AddDrawSurf( surfaceType_t *surface, shader_t *shader,
				   int fogIndex, int dlightMap ) {
	R_AddDrawSurfFlags( surface, shader, fogIndex, dlightMap, 0 );
}


/*
=================
R_DecomposeSort
=================
*/
void R_DecomposeSort( unsigned sort, int *entityNum, shader_t **shader, 
					 int *fogNum, int *dlightMap ) {
	*fogNum = ( sort >> QSORT_FOGNUM_SHIFT ) & FOGNUM_MASK;
	*shader = tr.sortedShaders[ ( sort >> QSORT_SHADERNUM_SHIFT ) & SHADERNUM_MASK ];
	*entityNum = ( sort >> QSORT_REFENTITYNUM_SHIFT ) & REFENTITYNUM_MASK;
	*dlightMap = sort & DLIGHT_MASK;
}


/*
=================
R_SortDrawSurfs
=================
*/
static void R_SortDrawSurfs( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	portalViewQueue_t	portalViews;
	portalViewPlan_t	*portalPlans;
	portalViewPlan_t	portalStackPlans[PORTAL_VIEW_STACK_PLANS];
	qboolean			portalOnly;
	qboolean			freePortalPlans;
	int					maxPortalPlans;
	int					numPortalDrawSurfs;
#ifdef USE_PMLIGHT
	int				i;
#endif

	// it is possible for some views to not have any surfaces
	if ( numDrawSurfs < 1 ) {
		// we still need to add it for hyperspace cases
		R_AddDrawSurfCmd( drawSurfs, numDrawSurfs );
		return;
	}

	// sort the drawsurfs by sort type, then orientation, then shader
	R_RadixSort( drawSurfs, numDrawSurfs );

	// Plan portal child views as explicit passes before queuing the parent view.
	numPortalDrawSurfs = R_CountPortalDrawSurfs( drawSurfs, numDrawSurfs );
	if ( numPortalDrawSurfs > 0 ) {
		maxPortalPlans = R_MaxPortalViewPlans( numPortalDrawSurfs );
		freePortalPlans = qfalse;
		if ( maxPortalPlans <= PORTAL_VIEW_STACK_PLANS ) {
			portalPlans = portalStackPlans;
		} else {
			portalPlans = ri.Malloc( maxPortalPlans * sizeof( *portalPlans ) );
			if ( !portalPlans ) {
				ri.Error( ERR_DROP, "R_SortDrawSurfs: portal view plan allocation failed" );
			}
			freePortalPlans = qtrue;
		}

		R_InitPortalViewQueue( &portalViews, portalPlans, maxPortalPlans );
		R_QueuePortalViewPlans( drawSurfs, numPortalDrawSurfs, &portalViews );
		R_RenderPortalViewQueue( &portalViews );

		portalOnly = portalViews.portalOnly;
		if ( freePortalPlans ) {
			ri.Free( portalPlans );
		}
		if ( portalOnly ) {
			return;
		}
	}

#ifdef USE_PMLIGHT
#ifdef USE_LEGACY_DLIGHTS
	if ( r_dlightMode->integer ) 
#endif
	{
		dlight_t *dl;
		// all the lit surfaces are in a single queue
		// but each light's surfaces are sorted within its subsection
		for ( i = 0; i < tr.refdef.num_dlights; ++i ) { 
			dl = &tr.refdef.dlights[ i ];
			if ( dl->head ) {
				R_SortLitsurfs( dl );
			}
		}
	}
#endif // USE_PMLIGHT

	R_AddDrawSurfCmd( drawSurfs, numDrawSurfs );
}


/*
=============
R_AddEntitySurfaces
=============
*/
static void R_AddEntitySurfaces( void ) {
	trRefEntity_t	*ent;
	shader_t		*shader;

	if ( !r_drawentities->integer ) {
		return;
	}

	for ( tr.currentEntityNum = 0;
			tr.currentEntityNum < tr.refdef.num_entities;
			tr.currentEntityNum++ ) {
		ent = tr.currentEntity = &tr.refdef.entities[tr.currentEntityNum];
#ifdef USE_LEGACY_DLIGHTS
		ent->needDlights = 0;
#endif
		// preshift the value we are going to OR into the drawsurf sort
		tr.shiftedEntityNum = tr.currentEntityNum << QSORT_REFENTITYNUM_SHIFT;

		//
		// the weapon model must be handled special --
		// we don't want the hacked first person weapon position showing in 
		// mirrors, because the true body position will already be drawn
		//
		if ( (ent->e.renderfx & RF_FIRST_PERSON) &&
			( R_ViewPassIsPortal( &tr.viewParms ) || ( tr.refdef.rdflags & RDF_NOFIRSTPERSON ) ) ) {
			continue;
		}

		// simple generated models, like sprites and beams, are not culled
		switch ( ent->e.reType ) {
		case RT_PORTALSURFACE:
			break;		// don't draw anything
		case RT_SPRITE:
		case RT_BEAM:
		case RT_LIGHTNING:
		case RT_RAIL_CORE:
		case RT_RAIL_RINGS:
			// self blood sprites, talk balloons, etc should not be drawn in the primary
			// view.  We can't just do this check for all entities, because md3
			// entities may still want to cast shadows from them
			if ( (ent->e.renderfx & RF_THIRD_PERSON) && !R_ViewPassIsPortal( &tr.viewParms ) ) {
				continue;
			}
			shader = R_GetShaderByHandle( ent->e.customShader );
			R_AddDrawSurf( &entitySurface, shader, R_SpriteFogNum( ent ), 0 );
			break;

		case RT_MODEL:
			// we must set up parts of tr.or for model culling
			R_RotateForEntity( ent, &tr.viewParms, &tr.or );

			tr.currentModel = R_GetModelByHandle( ent->e.hModel );
			if (!tr.currentModel) {
				R_AddDrawSurf( &entitySurface, tr.defaultShader, 0, 0 );
			} else {
				switch ( tr.currentModel->type ) {
				case MOD_MESH:
					R_AddMD3Surfaces( ent );
					break;
				case MOD_MDR:
					R_MDRAddAnimSurfaces( ent );
					break;
				case MOD_IQM:
					R_AddIQMSurfaces( ent );
					break;
				case MOD_BRUSH:
					R_AddBrushModelSurfaces( ent );
					break;
				case MOD_BAD:		// null model axis
					if ( (ent->e.renderfx & RF_THIRD_PERSON) && !R_ViewPassIsPortal( &tr.viewParms ) ) {
						break;
					}
					R_AddDrawSurf( &entitySurface, tr.defaultShader, 0, 0 );
					break;
				default:
					ri.Error( ERR_DROP, "R_AddEntitySurfaces: Bad modeltype" );
					break;
				}
			}
			break;
		default:
			ri.Error( ERR_DROP, "R_AddEntitySurfaces: Bad reType" );
		}
	}

}


/*
====================
R_GenerateDrawSurfs
====================
*/
static void R_GenerateDrawSurfs( void ) {
	R_AddWorldSurfaces ();

	R_UpdateAdvertisements();

	R_AddPolygonSurfaces();

	// set the projection matrix with the minimum zfar
	// now that we have the world bounded
	// this needs to be done before entities are
	// added, because they use the projection
	// matrix for lod calculation

	// dynamically compute far clip plane distance
	R_SetFarClip();

	// we know the size of the clipping volume. Now set the rest of the projection matrix.
	R_SetupProjectionZ( &tr.viewParms );

	R_AddEntitySurfaces();

	R_PlanFrameShadows();
}


/*
================
R_RenderView

A view may be either the actual camera view,
or a mirror / remote location
================
*/
void R_RenderView( const viewParms_t *parms ) {
	int		firstDrawSurf;
	int		numDrawSurfs;

	if ( parms->viewportWidth <= 0 || parms->viewportHeight <= 0 ) {
		return;
	}

	tr.viewCount++;

	tr.viewParms = *parms;
	tr.viewParms.frameSceneNum = tr.frameSceneNum;
	tr.viewParms.frameCount = tr.frameCount;
	R_FinalizeViewPassFlags( &tr.viewParms );
	tr.viewParms.passFlags |= VPF_CLEAR_STENCIL;

	firstDrawSurf = tr.refdef.numDrawSurfs;

	// set viewParms.world
	R_RotateForViewer();

	R_SetupProjection( &tr.viewParms, r_zproj->value, qtrue );

	R_GenerateDrawSurfs();

	// if we overflowed MAX_DRAWSURFS, the drawsurfs
	// wrapped around in the buffer and we will be missing
	// the first surfaces, not the last ones
	numDrawSurfs = tr.refdef.numDrawSurfs;
	if ( numDrawSurfs > MAX_DRAWSURFS ) {
		numDrawSurfs = MAX_DRAWSURFS;
	}

	R_SortDrawSurfs( tr.refdef.drawSurfs + firstDrawSurf, numDrawSurfs - firstDrawSurf );
}
