#ifndef TR_CUBEMAP_H
#define TR_CUBEMAP_H

/*
 * Camera-relative cube face order used by screenshot export:
 * front, back, left, right, top, bottom.
 *
 * Quake axes are forward, left, up.  Every mapping below preserves that
 * handedness so adjacent 90-degree projections meet without a mirrored edge.
 */
static ID_INLINE void R_CubemapFaceAxis( const vec3_t baseAxis[3], int faceIndex,
	vec3_t outAxis[3] )
{
	switch ( faceIndex ) {
		case 0: // front
			VectorCopy( baseAxis[0], outAxis[0] );
			VectorCopy( baseAxis[1], outAxis[1] );
			VectorCopy( baseAxis[2], outAxis[2] );
			break;
		case 1: // back
			VectorNegate( baseAxis[0], outAxis[0] );
			VectorNegate( baseAxis[1], outAxis[1] );
			VectorCopy( baseAxis[2], outAxis[2] );
			break;
		case 2: // left
			VectorCopy( baseAxis[1], outAxis[0] );
			VectorNegate( baseAxis[0], outAxis[1] );
			VectorCopy( baseAxis[2], outAxis[2] );
			break;
		case 3: // right
			VectorNegate( baseAxis[1], outAxis[0] );
			VectorCopy( baseAxis[0], outAxis[1] );
			VectorCopy( baseAxis[2], outAxis[2] );
			break;
		case 4: // top
			VectorCopy( baseAxis[2], outAxis[0] );
			VectorCopy( baseAxis[1], outAxis[1] );
			VectorNegate( baseAxis[0], outAxis[2] );
			break;
		case 5: // bottom
		default:
			VectorNegate( baseAxis[2], outAxis[0] );
			VectorCopy( baseAxis[1], outAxis[1] );
			VectorCopy( baseAxis[0], outAxis[2] );
			break;
	}
}

#endif
