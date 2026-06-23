/*
===========================================================================
IBSP v43 compatibility

This translator is adapted from Spearmint's Q3 IHV BSP loader
(`code/qcommon/bsp_q3ihv.c`) so FnQL can feed q3test/IHV maps through
the existing retail Quake III collision and renderer loaders.
===========================================================================
*/
#ifndef __BSP_V43_H__
#define __BSP_V43_H__

#include <stdlib.h>
#include <string.h>

#include "qfiles.h"
#include "surfaceflags.h"

typedef struct {
	void	*data;
	int		length;
} bsp43_translation_t;

typedef struct {
	int firstSurface;
	int numSurfaces;
	int firstBrush;
	int numBrushes;
} bsp43_modelinfo_t;

static int BSP43_Align4( const int value ) {
	return ( value + 3 ) & ~3;
}

static void BSP43_SetError( char *error, const size_t errorSize, const char *message ) {
	if ( !error || !errorSize ) {
		return;
	}

	Q_strncpyz( error, message, (int)errorSize );
}

static qboolean BSP43_ValidateLump( const lump_t *lump, const int length ) {
	int32_t ofs;
	int32_t len;

	ofs = LittleLong( lump->fileofs );
	len = LittleLong( lump->filelen );

	if ( ofs < 0 || len < 0 ) {
		return qfalse;
	}
	if ( ofs > length || len > length ) {
		return qfalse;
	}
	if ( ofs + len < ofs || ofs + len > length ) {
		return qfalse;
	}

	return qtrue;
}

static void BSP43_SetPatchBounds( const dsurface43_t *surface, const drawVert_t *drawVerts, dsurface_t *outSurface ) {
	vec_t *mins;
	vec_t *maxs;
	int firstVert;
	int numVerts;
	int i;

	mins = outSurface->lightmapVecs[0];
	maxs = outSurface->lightmapVecs[1];
	firstVert = LittleLong( surface->firstVert );
	numVerts = LittleLong( surface->numVerts );

	/*
	 * Preserve Spearmint's q3ihv patch-bound behavior so the renderer sees
	 * the same LoD inputs that its native loader would produce.
	 */
	mins[0] = 0.0f;
	mins[1] = 0.0f;
	mins[2] = 0.0f;
	maxs[0] = 0.0f;
	maxs[1] = 0.0f;
	maxs[2] = 0.0f;

	for ( i = 0; i < numVerts; i++ ) {
		const drawVert_t *point = &drawVerts[firstVert + i];
		float x = LittleFloat( point->xyz[0] );
		float y = LittleFloat( point->xyz[1] );
		float z = LittleFloat( point->xyz[2] );

		if ( x < mins[0] ) {
			mins[0] = x;
		}
		if ( y < mins[1] ) {
			mins[1] = y;
		}
		if ( z < mins[2] ) {
			mins[2] = z;
		}

		if ( x > maxs[0] ) {
			maxs[0] = x;
		}
		if ( y > maxs[1] ) {
			maxs[1] = y;
		}
		if ( z > maxs[2] ) {
			maxs[2] = z;
		}
	}
}

static qboolean BSP43_TranslateToV46(
	const void *inputData,
	const int inputLength,
	void *(*allocFn)( int ),
	void (*freeFn)( void * ),
	bsp43_translation_t *translation,
	char *error,
	const size_t errorSize )
{
	const dheader43_t *header43;
	const lump_t *lumps43;
	const dnode_t *nodes43;
	const dleaf_t *leafs43;
	const int *leafSurfaces43;
	const int *leafBrushes43;
	const dmodel43_t *models43;
	const dbrush43_t *brushes43;
	const dbrushside43_t *brushSides43;
	const drawVert_t *drawVerts43;
	const dsurface43_t *surfaces43;
	const dfog43_t *fogs43;
	bsp43_modelinfo_t *modelInfo = NULL;
	int *brushSideContents = NULL;
	int *brushSideFirstSurface = NULL;
	int *surfaceShaderNum = NULL;
	int *surfaceFirstIndex = NULL;
	int *surfaceNumIndexes = NULL;
	byte *output = NULL;
	dheader_t *header46;
	int offsets[HEADER_LUMPS];
	int lengths[HEADER_LUMPS];
	int i, j;
	int planeCount, nodeCount, leafCount, leafSurfaceCount, leafBrushCount;
	int modelCount, brushCount, brushSideCount, drawVertCount, surfaceCount, fogCount;
	int lightmapCount, totalIndexes;
	int brushShaderBase;
	int brushSideShaderBase;
	int surfaceShaderBase;
	int totalShaders;
	int outputLength;

	if ( !translation ) {
		BSP43_SetError( error, errorSize, "IBSP v43 translation target is missing" );
		return qfalse;
	}

	Com_Memset( translation, 0, sizeof( *translation ) );

	if ( !allocFn ) {
		BSP43_SetError( error, errorSize, "IBSP v43 allocator is missing" );
		return qfalse;
	}
	if ( !freeFn ) {
		BSP43_SetError( error, errorSize, "IBSP v43 free callback is missing" );
		return qfalse;
	}

	if ( inputLength < (int)sizeof( dheader43_t ) ) {
		BSP43_SetError( error, errorSize, "IBSP v43 header is truncated" );
		return qfalse;
	}

	header43 = (const dheader43_t *)inputData;
	if ( LittleLong( header43->ident ) != BSP_IDENT || LittleLong( header43->version ) != BSP_VERSION_IHV ) {
		BSP43_SetError( error, errorSize, "IBSP v43 translator received the wrong map format" );
		return qfalse;
	}

	lumps43 = header43->lumps;
	for ( i = 0; i < HEADER_LUMPS_43; i++ ) {
		if ( !BSP43_ValidateLump( &lumps43[i], inputLength ) ) {
			BSP43_SetError( error, errorSize, "IBSP v43 has an invalid lump directory" );
			return qfalse;
		}
	}

	planeCount = LittleLong( lumps43[LUMP43_PLANES].filelen ) / (int)sizeof( dplane43_t );
	nodeCount = LittleLong( lumps43[LUMP43_NODES].filelen ) / (int)sizeof( dnode_t );
	leafCount = LittleLong( lumps43[LUMP43_LEAFS].filelen ) / (int)sizeof( dleaf_t );
	leafSurfaceCount = LittleLong( lumps43[LUMP43_LEAFSURFACES].filelen ) / (int)sizeof( int32_t );
	leafBrushCount = LittleLong( lumps43[LUMP43_LEAFBRUSHES].filelen ) / (int)sizeof( int32_t );
	modelCount = LittleLong( lumps43[LUMP43_MODELS].filelen ) / (int)sizeof( dmodel43_t );
	brushCount = LittleLong( lumps43[LUMP43_BRUSHES].filelen ) / (int)sizeof( dbrush43_t );
	brushSideCount = LittleLong( lumps43[LUMP43_BRUSHSIDES].filelen ) / (int)sizeof( dbrushside43_t );
	drawVertCount = LittleLong( lumps43[LUMP43_DRAWVERTS].filelen ) / (int)sizeof( drawVert_t );
	surfaceCount = LittleLong( lumps43[LUMP43_SURFACES].filelen ) / (int)sizeof( dsurface43_t );
	fogCount = LittleLong( lumps43[LUMP43_FOGS].filelen ) / (int)sizeof( dfog43_t );
	lightmapCount = LittleLong( lumps43[LUMP43_LIGHTMAPS].filelen ) / ( LIGHTMAP_WIDTH * LIGHTMAP_HEIGHT * 3 );

	if ( LittleLong( lumps43[LUMP43_PLANES].filelen ) % sizeof( dplane43_t )
		|| LittleLong( lumps43[LUMP43_NODES].filelen ) % sizeof( dnode_t )
		|| LittleLong( lumps43[LUMP43_LEAFS].filelen ) % sizeof( dleaf_t )
		|| LittleLong( lumps43[LUMP43_LEAFSURFACES].filelen ) % sizeof( int32_t )
		|| LittleLong( lumps43[LUMP43_LEAFBRUSHES].filelen ) % sizeof( int32_t )
		|| LittleLong( lumps43[LUMP43_MODELS].filelen ) % sizeof( dmodel43_t )
		|| LittleLong( lumps43[LUMP43_BRUSHES].filelen ) % sizeof( dbrush43_t )
		|| LittleLong( lumps43[LUMP43_BRUSHSIDES].filelen ) % sizeof( dbrushside43_t )
		|| LittleLong( lumps43[LUMP43_DRAWVERTS].filelen ) % sizeof( drawVert_t )
		|| LittleLong( lumps43[LUMP43_SURFACES].filelen ) % sizeof( dsurface43_t )
		|| LittleLong( lumps43[LUMP43_FOGS].filelen ) % sizeof( dfog43_t ) ) {
		BSP43_SetError( error, errorSize, "IBSP v43 has a malformed lump size" );
		goto cleanup;
	}

	if ( modelCount < 1 ) {
		BSP43_SetError( error, errorSize, "IBSP v43 map has no models" );
		goto cleanup;
	}
	if ( lightmapCount * LIGHTMAP_WIDTH * LIGHTMAP_HEIGHT * 3 != LittleLong( lumps43[LUMP43_LIGHTMAPS].filelen ) ) {
		BSP43_SetError( error, errorSize, "IBSP v43 lightmap lump is malformed" );
		goto cleanup;
	}

	nodes43 = (const dnode_t *)( (const byte *)inputData + LittleLong( lumps43[LUMP43_NODES].fileofs ) );
	leafs43 = (const dleaf_t *)( (const byte *)inputData + LittleLong( lumps43[LUMP43_LEAFS].fileofs ) );
	leafSurfaces43 = (const int *)( (const byte *)inputData + LittleLong( lumps43[LUMP43_LEAFSURFACES].fileofs ) );
	leafBrushes43 = (const int *)( (const byte *)inputData + LittleLong( lumps43[LUMP43_LEAFBRUSHES].fileofs ) );
	models43 = (const dmodel43_t *)( (const byte *)inputData + LittleLong( lumps43[LUMP43_MODELS].fileofs ) );
	brushes43 = (const dbrush43_t *)( (const byte *)inputData + LittleLong( lumps43[LUMP43_BRUSHES].fileofs ) );
	brushSides43 = (const dbrushside43_t *)( (const byte *)inputData + LittleLong( lumps43[LUMP43_BRUSHSIDES].fileofs ) );
	drawVerts43 = (const drawVert_t *)( (const byte *)inputData + LittleLong( lumps43[LUMP43_DRAWVERTS].fileofs ) );
	surfaces43 = (const dsurface43_t *)( (const byte *)inputData + LittleLong( lumps43[LUMP43_SURFACES].fileofs ) );
	fogs43 = (const dfog43_t *)( (const byte *)inputData + LittleLong( lumps43[LUMP43_FOGS].fileofs ) );

	modelInfo = (bsp43_modelinfo_t *)malloc( modelCount * sizeof( *modelInfo ) );
	brushSideContents = (int *)malloc( MAX( brushSideCount, 1 ) * sizeof( *brushSideContents ) );
	brushSideFirstSurface = (int *)malloc( MAX( brushSideCount, 1 ) * sizeof( *brushSideFirstSurface ) );
	surfaceShaderNum = (int *)malloc( MAX( surfaceCount, 1 ) * sizeof( *surfaceShaderNum ) );
	surfaceFirstIndex = (int *)malloc( MAX( surfaceCount, 1 ) * sizeof( *surfaceFirstIndex ) );
	surfaceNumIndexes = (int *)malloc( MAX( surfaceCount, 1 ) * sizeof( *surfaceNumIndexes ) );

	if ( !modelInfo || !brushSideContents || !brushSideFirstSurface
		|| !surfaceShaderNum || !surfaceFirstIndex || !surfaceNumIndexes ) {
		BSP43_SetError( error, errorSize, "IBSP v43 scratch allocation failed" );
		goto cleanup;
	}

	Com_Memset( modelInfo, 0, modelCount * sizeof( *modelInfo ) );
	Com_Memset( brushSideContents, 0, MAX( brushSideCount, 1 ) * sizeof( *brushSideContents ) );
	for ( i = 0; i < brushSideCount; i++ ) {
		brushSideFirstSurface[i] = -1;
	}

	for ( i = 0; i < nodeCount; i++ ) {
		int planeNum;
		int child0;
		int child1;

		planeNum = LittleLong( nodes43[i].planeNum );
		child0 = LittleLong( nodes43[i].children[0] );
		child1 = LittleLong( nodes43[i].children[1] );

		if ( planeNum < 0 || planeNum >= planeCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 node references an invalid plane" );
			goto cleanup;
		}
		if ( child0 >= nodeCount || child1 >= nodeCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 node references an invalid child node" );
			goto cleanup;
		}
		if ( child0 < 0 && ( -child0 - 1 < 0 || -child0 - 1 >= leafCount ) ) {
			BSP43_SetError( error, errorSize, "IBSP v43 node references an invalid child leaf" );
			goto cleanup;
		}
		if ( child1 < 0 && ( -child1 - 1 < 0 || -child1 - 1 >= leafCount ) ) {
			BSP43_SetError( error, errorSize, "IBSP v43 node references an invalid child leaf" );
			goto cleanup;
		}
	}

	for ( i = 0; i < leafCount; i++ ) {
		int firstLeafSurface;
		int numLeafSurfaces;
		int firstLeafBrush;
		int numLeafBrushes;

		firstLeafSurface = LittleLong( leafs43[i].firstLeafSurface );
		numLeafSurfaces = LittleLong( leafs43[i].numLeafSurfaces );
		firstLeafBrush = LittleLong( leafs43[i].firstLeafBrush );
		numLeafBrushes = LittleLong( leafs43[i].numLeafBrushes );

		if ( firstLeafSurface < 0 || numLeafSurfaces < 0 || firstLeafSurface + numLeafSurfaces > leafSurfaceCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 leaf surface list is out of bounds" );
			goto cleanup;
		}
		if ( firstLeafBrush < 0 || numLeafBrushes < 0 || firstLeafBrush + numLeafBrushes > leafBrushCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 leaf brush list is out of bounds" );
			goto cleanup;
		}
	}

	for ( i = 0; i < leafSurfaceCount; i++ ) {
		int surfaceNum;

		surfaceNum = LittleLong( leafSurfaces43[i] );
		if ( surfaceNum < 0 || surfaceNum >= surfaceCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 leaf references an invalid surface" );
			goto cleanup;
		}
	}

	for ( i = 0; i < leafBrushCount; i++ ) {
		int brushNum;

		brushNum = LittleLong( leafBrushes43[i] );
		if ( brushNum < 0 || brushNum >= brushCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 leaf references an invalid brush" );
			goto cleanup;
		}
	}

	for ( i = 0; i < brushCount; i++ ) {
		int firstSide;
		int numSides;
		int contents;

		firstSide = LittleLong( brushes43[i].firstSide );
		numSides = LittleLong( brushes43[i].numSides );
		contents = LittleLong( brushes43[i].contentFlags );

		if ( firstSide < 0 || numSides < 0 || firstSide + numSides > brushSideCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 brush side range is out of bounds" );
			goto cleanup;
		}

		for ( j = 0; j < numSides; j++ ) {
			brushSideContents[firstSide + j] = contents;
		}
	}

	for ( i = 0; i < brushSideCount; i++ ) {
		int planeNum;

		planeNum = LittleLong( brushSides43[i].planeNum );
		if ( planeNum < 0 || planeNum >= planeCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 brush side references an invalid plane" );
			goto cleanup;
		}
	}

	totalIndexes = 0;
	for ( i = 0; i < surfaceCount; i++ ) {
		int brushSideNum;
		int firstVert;
		int numVerts;
		int patchWidth;
		int patchHeight;
		int fogNum;
		int lightmapNum;

		brushSideNum = LittleLong( surfaces43[i].brushSideNum );
		firstVert = LittleLong( surfaces43[i].firstVert );
		numVerts = LittleLong( surfaces43[i].numVerts );
		patchWidth = LittleLong( surfaces43[i].patchWidth );
		patchHeight = LittleLong( surfaces43[i].patchHeight );
		fogNum = LittleLong( surfaces43[i].fogNum );
		lightmapNum = LittleLong( surfaces43[i].lightmapNum );

		if ( firstVert < 0 || numVerts < 0 || firstVert + numVerts > drawVertCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 surface vertex range is out of bounds" );
			goto cleanup;
		}
		if ( fogNum < -1 || fogNum >= fogCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 surface references an invalid fog" );
			goto cleanup;
		}
		if ( lightmapNum < -1 || lightmapNum >= lightmapCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 surface references an invalid lightmap" );
			goto cleanup;
		}
		if ( brushSideNum < -1 || brushSideNum >= brushSideCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 surface references an invalid brush side" );
			goto cleanup;
		}

		surfaceShaderNum[i] = 0;
		surfaceFirstIndex[i] = totalIndexes;

		if ( patchWidth > 0 && patchHeight > 0 ) {
			surfaceNumIndexes[i] = 0;
		} else if ( numVerts >= 3 ) {
			surfaceNumIndexes[i] = ( numVerts - 2 ) * 3;
			totalIndexes += surfaceNumIndexes[i];
		} else {
			surfaceNumIndexes[i] = 0;
		}
	}

	for ( i = 0; i < modelCount; i++ ) {
		int firstFace;
		int numFaces;
		int nodeNum;
		int leafNum;

		firstFace = LittleLong( models43[i].firstFace );
		numFaces = LittleLong( models43[i].numFaces );
		nodeNum = LittleLong( models43[i].headnode );
		leafNum = -1 - nodeNum;

		if ( firstFace < 0 || numFaces < 0 || firstFace + numFaces > surfaceCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 model surface range is out of bounds" );
			goto cleanup;
		}
		if ( nodeNum >= nodeCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 model has an invalid headnode" );
			goto cleanup;
		}
		if ( nodeNum < 0 && ( leafNum < 0 || leafNum >= leafCount ) ) {
			BSP43_SetError( error, errorSize, "IBSP v43 model has an invalid headnode" );
			goto cleanup;
		}
	}

	for ( i = 0; i < fogCount; i++ ) {
		int brushNum;

		brushNum = LittleLong( fogs43[i].brushNum );
		if ( brushNum < 0 || brushNum >= brushCount ) {
			BSP43_SetError( error, errorSize, "IBSP v43 fog references an invalid brush" );
			goto cleanup;
		}
	}

	modelInfo[0].firstSurface = 0;
	modelInfo[0].numSurfaces = surfaceCount;
	modelInfo[0].firstBrush = 0;
	modelInfo[0].numBrushes = brushCount;

	for ( i = 1; i < modelCount; i++ ) {
		int nodeNum;
		int leafNum;

		nodeNum = LittleLong( models43[i].headnode );
		leafNum = -1 - nodeNum;

		if ( nodeNum < 0 && leafNum >= 0 && leafNum < leafCount ) {
			const dleaf_t *leaf;
			int first;
			int last;
			int firstLeafBrush;
			int numLeafBrushes;
			int firstLeafSurface;
			int numLeafSurfaces;

			leaf = &leafs43[leafNum];
			firstLeafBrush = LittleLong( leaf->firstLeafBrush );
			numLeafBrushes = LittleLong( leaf->numLeafBrushes );
			firstLeafSurface = LittleLong( leaf->firstLeafSurface );
			numLeafSurfaces = LittleLong( leaf->numLeafSurfaces );

			if ( numLeafBrushes > 0 ) {
				first = LittleLong( leafBrushes43[firstLeafBrush] );
				last = LittleLong( leafBrushes43[firstLeafBrush + numLeafBrushes - 1] );

				modelInfo[i].firstBrush = MIN( first, last );
				modelInfo[i].numBrushes = numLeafBrushes;

				if ( modelInfo[i].numBrushes > 0 && modelInfo[i].firstBrush < modelInfo[0].numBrushes ) {
					modelInfo[0].numBrushes = modelInfo[i].firstBrush;
				}
			}

			if ( numLeafSurfaces > 0 ) {
				first = LittleLong( leafSurfaces43[firstLeafSurface] );
				last = LittleLong( leafSurfaces43[firstLeafSurface + numLeafSurfaces - 1] );

				modelInfo[i].firstSurface = MIN( first, last );
				modelInfo[i].numSurfaces = numLeafSurfaces;

				if ( modelInfo[i].numSurfaces > 0 && modelInfo[i].firstSurface < modelInfo[0].numSurfaces ) {
					modelInfo[0].numSurfaces = modelInfo[i].firstSurface;
				}
			}
		}
	}

	brushShaderBase = 0;
	brushSideShaderBase = brushShaderBase + brushCount;
	surfaceShaderBase = brushSideShaderBase + brushSideCount;
	totalShaders = surfaceShaderBase + surfaceCount;

	lengths[LUMP_ENTITIES] = LittleLong( lumps43[LUMP43_ENTITIES].filelen );
	lengths[LUMP_SHADERS] = totalShaders * (int)sizeof( dshader_t );
	lengths[LUMP_PLANES] = planeCount * (int)sizeof( dplane_t );
	lengths[LUMP_NODES] = LittleLong( lumps43[LUMP43_NODES].filelen );
	lengths[LUMP_LEAFS] = LittleLong( lumps43[LUMP43_LEAFS].filelen );
	lengths[LUMP_LEAFSURFACES] = LittleLong( lumps43[LUMP43_LEAFSURFACES].filelen );
	lengths[LUMP_LEAFBRUSHES] = LittleLong( lumps43[LUMP43_LEAFBRUSHES].filelen );
	lengths[LUMP_MODELS] = modelCount * (int)sizeof( dmodel_t );
	lengths[LUMP_BRUSHES] = brushCount * (int)sizeof( dbrush_t );
	lengths[LUMP_BRUSHSIDES] = brushSideCount * (int)sizeof( dbrushside_t );
	lengths[LUMP_DRAWVERTS] = LittleLong( lumps43[LUMP43_DRAWVERTS].filelen );
	lengths[LUMP_DRAWINDEXES] = totalIndexes * (int)sizeof( int32_t );
	lengths[LUMP_FOGS] = fogCount * (int)sizeof( dfog_t );
	lengths[LUMP_SURFACES] = surfaceCount * (int)sizeof( dsurface_t );
	lengths[LUMP_LIGHTMAPS] = LittleLong( lumps43[LUMP43_LIGHTMAPS].filelen );
	lengths[LUMP_LIGHTGRID] = 0;
	lengths[LUMP_VISIBILITY] = LittleLong( lumps43[LUMP43_VISIBILITY].filelen );

	outputLength = sizeof( dheader_t );
	for ( i = 0; i < HEADER_LUMPS; i++ ) {
		outputLength = BSP43_Align4( outputLength );
		offsets[i] = outputLength;
		outputLength += lengths[i];
	}

	output = (byte *)allocFn( outputLength );
	if ( !output ) {
		BSP43_SetError( error, errorSize, "IBSP v43 output allocation failed" );
		goto cleanup;
	}
	Com_Memset( output, 0, outputLength );

	header46 = (dheader_t *)output;
	header46->ident = LittleLong( BSP_IDENT );
	header46->version = LittleLong( BSP_VERSION );
	for ( i = 0; i < HEADER_LUMPS; i++ ) {
		header46->lumps[i].fileofs = LittleLong( offsets[i] );
		header46->lumps[i].filelen = LittleLong( lengths[i] );
	}

	Com_Memcpy( output + offsets[LUMP_ENTITIES],
		(const byte *)inputData + LittleLong( lumps43[LUMP43_ENTITIES].fileofs ),
		lengths[LUMP_ENTITIES] );

	{
		dshader_t *outShaders;

		outShaders = (dshader_t *)( output + offsets[LUMP_SHADERS] );

		for ( i = 0; i < brushCount; i++ ) {
			int shaderIndex;

			shaderIndex = brushShaderBase + i;
			Q_strncpyz( outShaders[shaderIndex].shader, "*default", sizeof( outShaders[shaderIndex].shader ) );
			outShaders[shaderIndex].surfaceFlags = LittleLong( 0 );
			outShaders[shaderIndex].contentFlags = LittleLong( LittleLong( brushes43[i].contentFlags ) );
		}

		for ( i = 0; i < brushSideCount; i++ ) {
			int shaderIndex;

			shaderIndex = brushSideShaderBase + i;
			Q_strncpyz( outShaders[shaderIndex].shader, "*default", sizeof( outShaders[shaderIndex].shader ) );
			outShaders[shaderIndex].surfaceFlags = LittleLong( LittleLong( brushSides43[i].surfaceFlags ) );
			outShaders[shaderIndex].contentFlags = LittleLong( brushSideContents[i] );
		}

		for ( i = 0; i < surfaceCount; i++ ) {
			int shaderIndex;
			int brushSideNum;
			int patchWidth;
			int patchHeight;
			int surfaceFlags;
			int contentFlags;

			shaderIndex = surfaceShaderBase + i;
			brushSideNum = LittleLong( surfaces43[i].brushSideNum );
			patchWidth = LittleLong( surfaces43[i].patchWidth );
			patchHeight = LittleLong( surfaces43[i].patchHeight );
			surfaceFlags = 0;
			contentFlags = 0;

			if ( brushSideNum >= 0 && brushSideNum < brushSideCount ) {
				surfaceFlags = LittleLong( brushSides43[brushSideNum].surfaceFlags );
				contentFlags = brushSideContents[brushSideNum];

				if ( patchWidth > 0 && patchHeight > 0 ) {
					surfaceFlags &= ~SURF_NODRAW;
					outShaders[brushSideShaderBase + brushSideNum].surfaceFlags = LittleLong( surfaceFlags );
				}
			}

			Q_strncpyz( outShaders[shaderIndex].shader, surfaces43[i].shader, sizeof( outShaders[shaderIndex].shader ) );
			outShaders[shaderIndex].surfaceFlags = LittleLong( surfaceFlags );
			outShaders[shaderIndex].contentFlags = LittleLong( contentFlags );
			surfaceShaderNum[i] = shaderIndex;

			if ( brushSideNum >= 0 && brushSideNum < brushSideCount && brushSideFirstSurface[brushSideNum] < 0 ) {
				brushSideFirstSurface[brushSideNum] = i;
				Q_strncpyz( outShaders[brushSideShaderBase + brushSideNum].shader, surfaces43[i].shader,
					sizeof( outShaders[brushSideShaderBase + brushSideNum].shader ) );
			}
		}
	}

	{
		const dplane43_t *inPlanes;
		dplane_t *outPlanes;

		inPlanes = (const dplane43_t *)( (const byte *)inputData + LittleLong( lumps43[LUMP43_PLANES].fileofs ) );
		outPlanes = (dplane_t *)( output + offsets[LUMP_PLANES] );

		for ( i = 0; i < planeCount; i++ ) {
			outPlanes[i].normal[0] = inPlanes[i].normal[0];
			outPlanes[i].normal[1] = inPlanes[i].normal[1];
			outPlanes[i].normal[2] = inPlanes[i].normal[2];
			outPlanes[i].dist = inPlanes[i].dist;
		}
	}

	Com_Memcpy( output + offsets[LUMP_NODES],
		(const byte *)inputData + LittleLong( lumps43[LUMP43_NODES].fileofs ),
		lengths[LUMP_NODES] );
	Com_Memcpy( output + offsets[LUMP_LEAFS],
		(const byte *)inputData + LittleLong( lumps43[LUMP43_LEAFS].fileofs ),
		lengths[LUMP_LEAFS] );
	Com_Memcpy( output + offsets[LUMP_LEAFSURFACES],
		(const byte *)inputData + LittleLong( lumps43[LUMP43_LEAFSURFACES].fileofs ),
		lengths[LUMP_LEAFSURFACES] );
	Com_Memcpy( output + offsets[LUMP_LEAFBRUSHES],
		(const byte *)inputData + LittleLong( lumps43[LUMP43_LEAFBRUSHES].fileofs ),
		lengths[LUMP_LEAFBRUSHES] );

	{
		dmodel_t *outModels;

		outModels = (dmodel_t *)( output + offsets[LUMP_MODELS] );

		for ( i = 0; i < modelCount; i++ ) {
			for ( j = 0; j < 3; j++ ) {
				outModels[i].mins[j] = models43[i].mins[j];
				outModels[i].maxs[j] = models43[i].maxs[j];
			}

			outModels[i].firstSurface = LittleLong( modelInfo[i].firstSurface );
			outModels[i].numSurfaces = LittleLong( modelInfo[i].numSurfaces );
			outModels[i].firstBrush = LittleLong( modelInfo[i].firstBrush );
			outModels[i].numBrushes = LittleLong( modelInfo[i].numBrushes );
		}
	}

	{
		dbrush_t *outBrushes;

		outBrushes = (dbrush_t *)( output + offsets[LUMP_BRUSHES] );

		for ( i = 0; i < brushCount; i++ ) {
			outBrushes[i].firstSide = LittleLong( LittleLong( brushes43[i].firstSide ) );
			outBrushes[i].numSides = LittleLong( LittleLong( brushes43[i].numSides ) );
			outBrushes[i].shaderNum = LittleLong( brushShaderBase + i );
		}
	}

	{
		dbrushside_t *outBrushSides;

		outBrushSides = (dbrushside_t *)( output + offsets[LUMP_BRUSHSIDES] );

		for ( i = 0; i < brushSideCount; i++ ) {
			outBrushSides[i].planeNum = LittleLong( LittleLong( brushSides43[i].planeNum ) );
			outBrushSides[i].shaderNum = LittleLong( brushSideShaderBase + i );
		}
	}

	Com_Memcpy( output + offsets[LUMP_DRAWVERTS],
		(const byte *)inputData + LittleLong( lumps43[LUMP43_DRAWVERTS].fileofs ),
		lengths[LUMP_DRAWVERTS] );

	{
		int *outIndexes;
		int cursor;

		outIndexes = (int *)( output + offsets[LUMP_DRAWINDEXES] );
		cursor = 0;

		for ( i = 0; i < surfaceCount; i++ ) {
			int numTris;
			int patchWidth;
			int patchHeight;

			patchWidth = LittleLong( surfaces43[i].patchWidth );
			patchHeight = LittleLong( surfaces43[i].patchHeight );
			if ( patchWidth > 0 && patchHeight > 0 ) {
				continue;
			}

			numTris = LittleLong( surfaces43[i].numVerts ) - 2;
			for ( j = 0; j < numTris; j++ ) {
				outIndexes[cursor++] = LittleLong( 0 );
				outIndexes[cursor++] = LittleLong( j + 1 );
				outIndexes[cursor++] = LittleLong( j + 2 );
			}
		}
	}

	{
		dfog_t *outFogs;

		outFogs = (dfog_t *)( output + offsets[LUMP_FOGS] );

		for ( i = 0; i < fogCount; i++ ) {
			Q_strncpyz( outFogs[i].shader, fogs43[i].shader, sizeof( outFogs[i].shader ) );
			outFogs[i].brushNum = LittleLong( LittleLong( fogs43[i].brushNum ) );
			outFogs[i].visibleSide = LittleLong( 5 );
		}
	}

	{
		dsurface_t *outSurfaces;

		outSurfaces = (dsurface_t *)( output + offsets[LUMP_SURFACES] );

		for ( i = 0; i < surfaceCount; i++ ) {
			int patchWidth;
			int patchHeight;

			patchWidth = LittleLong( surfaces43[i].patchWidth );
			patchHeight = LittleLong( surfaces43[i].patchHeight );

			outSurfaces[i].shaderNum = LittleLong( surfaceShaderNum[i] );
			outSurfaces[i].fogNum = LittleLong( LittleLong( surfaces43[i].fogNum ) );
			outSurfaces[i].surfaceType = LittleLong( ( patchWidth > 0 && patchHeight > 0 ) ? MST_PATCH : MST_PLANAR );
			outSurfaces[i].firstVert = LittleLong( LittleLong( surfaces43[i].firstVert ) );
			outSurfaces[i].numVerts = LittleLong( LittleLong( surfaces43[i].numVerts ) );
			outSurfaces[i].firstIndex = LittleLong( surfaceFirstIndex[i] );
			outSurfaces[i].numIndexes = LittleLong( surfaceNumIndexes[i] );
			outSurfaces[i].lightmapNum = LittleLong( LittleLong( surfaces43[i].lightmapNum ) );
			outSurfaces[i].lightmapX = LittleLong( LittleLong( surfaces43[i].lightmapX ) );
			outSurfaces[i].lightmapY = LittleLong( LittleLong( surfaces43[i].lightmapY ) );
			outSurfaces[i].lightmapWidth = LittleLong( LittleLong( surfaces43[i].lightmapWidth ) );
			outSurfaces[i].lightmapHeight = LittleLong( LittleLong( surfaces43[i].lightmapHeight ) );

			for ( j = 0; j < 3; j++ ) {
				outSurfaces[i].lightmapOrigin[j] = surfaces43[i].lightmapOrigin[j];
				outSurfaces[i].lightmapVecs[0][j] = surfaces43[i].lightmapVecs[0][j];
				outSurfaces[i].lightmapVecs[1][j] = surfaces43[i].lightmapVecs[1][j];
				outSurfaces[i].lightmapVecs[2][j] = surfaces43[i].lightmapVecs[2][j];
			}

			outSurfaces[i].patchWidth = LittleLong( patchWidth );
			outSurfaces[i].patchHeight = LittleLong( patchHeight );

			if ( patchWidth > 0 && patchHeight > 0 ) {
				BSP43_SetPatchBounds( &surfaces43[i], drawVerts43, &outSurfaces[i] );
			}
		}
	}

	Com_Memcpy( output + offsets[LUMP_LIGHTMAPS],
		(const byte *)inputData + LittleLong( lumps43[LUMP43_LIGHTMAPS].fileofs ),
		lengths[LUMP_LIGHTMAPS] );
	Com_Memcpy( output + offsets[LUMP_VISIBILITY],
		(const byte *)inputData + LittleLong( lumps43[LUMP43_VISIBILITY].fileofs ),
		lengths[LUMP_VISIBILITY] );

	translation->data = output;
	translation->length = outputLength;
	output = NULL;

cleanup:
	if ( output ) {
		freeFn( output );
	}

	free( modelInfo );
	free( brushSideContents );
	free( brushSideFirstSurface );
	free( surfaceShaderNum );
	free( surfaceFirstIndex );
	free( surfaceNumIndexes );

	return translation->data != NULL;
}

#endif
