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
// tr_map.c

#include "tr_local.h"
#include "../qcommon/bsp_v43.h"

/*

Loads and prepares a map file for scene rendering.

A single entry point:

void RE_LoadWorldMap( const char *name );

*/

static	world_t		s_worldData;
static	byte		*fileBase;

static int	c_gridVerts;

#define	LUMP_ADVERTISEMENTS_QL	17

typedef struct {
	int		cellId;
	float	normal[3];
	float	points[4][3];
	char	model[MAX_QPATH];
} dAdvertisement_t;

//===============================================================================

static void HSVtoRGB( float h, float s, float v, float rgb[3] )
{
	int i;
	float f;
	float p, q, t;

	h *= 5;

	i = floor( h );
	f = h - i;

	p = v * ( 1 - s );
	q = v * ( 1 - s * f );
	t = v * ( 1 - s * ( 1 - f ) );

	switch ( i )
	{
	case 0:
		rgb[0] = v;
		rgb[1] = t;
		rgb[2] = p;
		break;
	case 1:
		rgb[0] = q;
		rgb[1] = v;
		rgb[2] = p;
		break;
	case 2:
		rgb[0] = p;
		rgb[1] = v;
		rgb[2] = t;
		break;
	case 3:
		rgb[0] = p;
		rgb[1] = q;
		rgb[2] = v;
		break;
	case 4:
		rgb[0] = t;
		rgb[1] = p;
		rgb[2] = v;
		break;
	case 5:
		rgb[0] = v;
		rgb[1] = p;
		rgb[2] = q;
		break;
	}
}


/*
===============
R_ClampDenorm

Clamp fp values that may result in denormalization after further multiplication
===============
*/
float R_ClampDenorm( float v ) {
	if ( fabsf( v ) > 0.0f && fabsf( v ) < 1e-9f ) {
		return 0.0f;
	} else {
		return v;
	}
}


/*
===============
R_ColorShiftLightingBytes
===============
*/
void R_ColorShiftLightingBytes( const byte in[4], byte out[4], qboolean hasAlpha ) {
	int		shift, r, g, b;

	// shift the color data based on overbright range
	shift = r_mapOverBrightBits->integer - tr.overbrightBits;

	// shift the data based on overbright range
	if ( shift >= 0 ) {
		r = in[0] << shift;
		g = in[1] << shift;
		b = in[2] << shift;
		// normalize by color instead of saturating to white
		if ( ( r | g | b ) > 255 ) {
			int max = r > g ? r : g;
			max = max > b ? max : b;
			r = r * 255 / max;
			g = g * 255 / max;
			b = b * 255 / max;
		}
	} else {
		r = in[0] >> -shift;
		g = in[1] >> -shift;
		b = in[2] >> -shift;
	}

	if ( r_mapGreyScale->integer ) {
		const byte luma = LUMA( r, g, b );
		out[0] = luma;
		out[1] = luma;
		out[2] = luma;
	} else if( r_mapGreyScale->value ) {
		const float scale = fabs( r_mapGreyScale->value );
		const float luma = LUMA( r, g, b );
		out[0] = LERP( r, luma, scale );
		out[1] = LERP( g, luma, scale );
		out[2] = LERP( b, luma, scale );
	} else {
		out[0] = r;
		out[1] = g;
		out[2] = b;
	}

	if ( hasAlpha ) {
		out[3] = in[3];
	}
}


#define LIGHTMAP_SIZE 128
#define LIGHTMAP_BORDER 2
#define LIGHTMAP_LEN (LIGHTMAP_SIZE + LIGHTMAP_BORDER*2)

static const int lightmapFlags = IMGFLAG_NOLIGHTSCALE | IMGFLAG_NO_COMPRESSION | IMGFLAG_LIGHTMAP | IMGFLAG_NOSCALE | IMGFLAG_COLORSPACE_LINEAR;

static int lightmapWidth;
static int lightmapHeight;
static int lightmapCountX;
static int lightmapCountY;

static void FillBorders( byte *img )
{
#define PIX(xx,yy,offs) img[((yy)*LIGHTMAP_LEN + (xx))*4+(offs)]
	int x0, y0;
	int x1, y1;
	int n, len, i;

	for ( n = LIGHTMAP_BORDER; n > 0; n-- )
	{
		x0 = n - 1; x1 = LIGHTMAP_LEN - n;
		y0 = n - 1; y1 = LIGHTMAP_LEN - n;
		len = LIGHTMAP_SIZE + (LIGHTMAP_BORDER*2 - n);
		for ( i = n; i < len; i++ ) 
		{
			PIX( i, y0, 0 ) = PIX( i, y0+1, 0 );
			PIX( i, y0, 1 ) = PIX( i, y0+1, 1 );
			PIX( i, y0, 2 ) = PIX( i, y0+1, 2 );
			PIX( i, y0, 3 ) = PIX( i, y0+1, 3 );

			PIX( x0, i, 0 ) = PIX( x0+1, i, 0 );
			PIX( x0, i, 1 ) = PIX( x0+1, i, 1 );
			PIX( x0, i, 2 ) = PIX( x0+1, i, 2 );
			PIX( x0, i, 3 ) = PIX( x0+1, i, 3 );

			PIX( i, y1, 0 ) = PIX( i, y1-1, 0 );
			PIX( i, y1, 1 ) = PIX( i, y1-1, 1 );
			PIX( i, y1, 2 ) = PIX( i, y1-1, 2 );
			PIX( i, y1, 3 ) = PIX( i, y1-1, 3 );

			PIX( x1, i, 0 ) = PIX( x1-1, i, 0 );
			PIX( x1, i, 1 ) = PIX( x1-1, i, 1 );
			PIX( x1, i, 2 ) = PIX( x1-1, i, 2 );
			PIX( x1, i, 3 ) = PIX( x1-1, i, 3 );
		}

		// interpolate corners
		PIX( x0, y0, 0 ) = (int)(PIX( x0, y0+1, 0 ) + PIX( x0+1, y0, 0 )) >> 1;
		PIX( x0, y0, 1 ) = (int)(PIX( x0, y0+1, 1 ) + PIX( x0+1, y0, 1 )) >> 1;
		PIX( x0, y0, 2 ) = (int)(PIX( x0, y0+1, 2 ) + PIX( x0+1, y0, 2 )) >> 1;
		PIX( x0, y0, 3 ) = (int)(PIX( x0, y0+1, 3 ) + PIX( x0+1, y0, 3 )) >> 1;
		
		PIX( x1, y0, 0 ) = (int)(PIX( x1-1, y0, 0 ) + PIX( x1, y0+1, 0 )) >> 1;
		PIX( x1, y0, 1 ) = (int)(PIX( x1-1, y0, 1 ) + PIX( x1, y0+1, 1 )) >> 1;
		PIX( x1, y0, 2 ) = (int)(PIX( x1-1, y0, 2 ) + PIX( x1, y0+1, 2 )) >> 1;
		PIX( x1, y0, 3 ) = (int)(PIX( x1-1, y0, 3 ) + PIX( x1, y0+1, 3 )) >> 1;
	
		PIX( x0, y1, 0 ) = (int)(PIX( x0, y1-1, 0 ) + PIX( x0+1, y1, 0 )) >> 1;
		PIX( x0, y1, 1 ) = (int)(PIX( x0, y1-1, 1 ) + PIX( x0+1, y1, 1 )) >> 1;
		PIX( x0, y1, 2 ) = (int)(PIX( x0, y1-1, 2 ) + PIX( x0+1, y1, 2 )) >> 1;
		PIX( x0, y1, 3 ) = (int)(PIX( x0, y1-1, 3 ) + PIX( x0+1, y1, 3 )) >> 1;

		PIX( x1, y1, 0 ) = (int)(PIX( x1, y1-1, 0 ) + PIX( x1-1, y1, 0 )) >> 1;
		PIX( x1, y1, 1 ) = (int)(PIX( x1, y1-1, 1 ) + PIX( x1-1, y1, 1 )) >> 1;
		PIX( x1, y1, 2 ) = (int)(PIX( x1, y1-1, 2 ) + PIX( x1-1, y1, 2 )) >> 1;
		PIX( x1, y1, 3 ) = (int)(PIX( x1, y1-1, 3 ) + PIX( x1-1, y1, 3 )) >> 1;
	}
}


/*
===============
R_ProcessLightmap

expand the 24 bit on-disk to 32 bit and return max.intensity
===============
*/
static float R_ProcessLightmap( byte *image, const byte *buf_p, float maxIntensity )
{
	int x, y;

	if ( 0 && r_lightmap->integer == 2 ) {
		int j;
		// color code by intensity as development tool	(FIXME: check range)
		for ( j = 0; j < LIGHTMAP_SIZE * LIGHTMAP_SIZE; j++ )
		{
			float r = buf_p[j*3+0];
			float g = buf_p[j*3+1];
			float b = buf_p[j*3+2];
			float intensity;
			float out[3] = {0.0, 0.0, 0.0};

			intensity = 0.33f * r + 0.685f * g + 0.063f * b;

			if ( intensity > 255 )
				intensity = 1.0f;
			else
				intensity /= 255.0f;

			if ( intensity > maxIntensity )
				maxIntensity = intensity;

			HSVtoRGB( intensity, 1.00, 0.50, out );

			image[j*4+0] = out[0] * 255;
			image[j*4+1] = out[1] * 255;
			image[j*4+2] = out[2] * 255;
			image[j*4+3] = 255;
		}
	} else {
		if ( tr.mergeLightmaps ) {
			for ( y = 0 ; y < LIGHTMAP_SIZE; y++ ) {
				for ( x = 0 ; x < LIGHTMAP_SIZE; x++ ) {
					byte *dst = &image[((y + LIGHTMAP_BORDER) * LIGHTMAP_LEN + x + LIGHTMAP_BORDER) * 4];
					R_ColorShiftLightingBytes( buf_p, dst, qfalse );
					dst[3] = 255;
					buf_p += 3;
				}
			}
			FillBorders( image );
		} else {
			// legacy path
			for ( y = 0 ; y < LIGHTMAP_SIZE; y++ ) {
				for ( x = 0 ; x < LIGHTMAP_SIZE; x++ ) {
					byte *dst = &image[(y * LIGHTMAP_SIZE + x) * 4];
					R_ColorShiftLightingBytes( buf_p, dst, qfalse );
					dst[3] = 255;
					buf_p += 3;
				}
			}
		}
	}

	return maxIntensity;
}

static void R_LightmapAverageColor( const byte *image, int stride, int x0, int y0, int width, int height, vec3_t color )
{
	double accum[3];
	int x, y;

	accum[0] = 0.0;
	accum[1] = 0.0;
	accum[2] = 0.0;

	for ( y = 0; y < height; y++ ) {
		const byte *src = image + ( ( y0 + y ) * stride + x0 ) * 4;

		for ( x = 0; x < width; x++, src += 4 ) {
			accum[0] += src[0];
			accum[1] += src[1];
			accum[2] += src[2];
		}
	}

	if ( width > 0 && height > 0 ) {
		const double scale = 1.0 / ( 255.0 * width * height );

		color[0] = (float)( accum[0] * scale );
		color[1] = (float)( accum[1] * scale );
		color[2] = (float)( accum[2] * scale );
	} else {
		VectorClear( color );
	}
}


static int SetLightmapParams( int numLightmaps, int maxTextureSize )
{
	lightmapWidth = log2pad( LIGHTMAP_LEN, 1 );
	lightmapHeight = log2pad( LIGHTMAP_LEN, 1 );

	lightmapCountX = 1;
	lightmapCountY = 1;

	while ( lightmapWidth < maxTextureSize && lightmapCountX * lightmapCountY < numLightmaps )
	{
		lightmapWidth = log2pad( lightmapWidth + LIGHTMAP_LEN, 1 );
		lightmapCountX = lightmapWidth / LIGHTMAP_LEN;
		if ( lightmapCountX * lightmapCountY >= numLightmaps )
			break;
		lightmapHeight = log2pad( lightmapHeight + LIGHTMAP_LEN, 1 );
		lightmapCountY = lightmapHeight / LIGHTMAP_LEN;
	}

	tr.lightmapMod = lightmapCountX * lightmapCountY;

	tr.lightmapScale[0] = (double)LIGHTMAP_SIZE / (double) lightmapWidth;
	tr.lightmapScale[1] = (double)LIGHTMAP_SIZE / (double) lightmapHeight;

	numLightmaps = ( numLightmaps + tr.lightmapMod - 1 ) / tr.lightmapMod;

	return numLightmaps;
}


int R_GetLightmapCoords( const int lightmapIndex, float *x, float *y )
{
	const int lightmapNum = lightmapIndex / tr.lightmapMod;
	const int cN = lightmapIndex % tr.lightmapMod;
	const int cX = cN % lightmapCountX;
	const int cY = cN / lightmapCountX;

	*x = (float)( LIGHTMAP_BORDER + cX * LIGHTMAP_LEN ) / (float) lightmapWidth;
	*y = (float)( LIGHTMAP_BORDER + cY * LIGHTMAP_LEN ) / (float) lightmapHeight;

	return lightmapNum;
}


/*
===============
R_LoadMergedLightmaps
===============
*/
static void R_LoadMergedLightmaps( const lump_t *l, byte *image )
{
	const byte	*buf;
	int			offs;
	int			i, x, y;
 	float		maxIntensity = 0;

	if ( l->filelen < LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3 )
		return;

	buf = fileBase + l->fileofs;

	// create all the lightmaps
	tr.numLightmaps = l->filelen / (LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3);

	// we are about to upload textures
	//R_IssuePendingRenderCommands();

	tr.numLightmaps = SetLightmapParams( tr.numLightmaps, glConfig.maxTextureSize );

	tr.lightmaps = ri.Hunk_Alloc( tr.numLightmaps * sizeof(image_t *), h_low );
	tr.lightmapAverageColors = ri.Hunk_Alloc( tr.numLightmaps * sizeof( *tr.lightmapAverageColors ), h_low );

	for ( offs = 0, i = 0 ; i < tr.numLightmaps; i++ ) {
		vec3_t averageAccum;
		int averageCount;

		VectorClear( averageAccum );
		averageCount = 0;

		tr.lightmaps[ i ] = R_CreateImage( va( "*mergedLightmap%d", i ), NULL, NULL,
			lightmapWidth, lightmapHeight, lightmapFlags | IMGFLAG_CLAMPTOBORDER );

		for ( y = 0; y < lightmapCountY; y++ ) {
			if ( offs >= l->filelen )
				break;

			for ( x = 0; x < lightmapCountX; x++ ) {
				if ( offs >= l->filelen )
					break;

				R_ProcessLightmap( image, buf + offs, maxIntensity );
				{
					vec3_t averageColor;

					R_LightmapAverageColor( image, LIGHTMAP_LEN, LIGHTMAP_BORDER, LIGHTMAP_BORDER,
						LIGHTMAP_SIZE, LIGHTMAP_SIZE, averageColor );
					VectorAdd( averageAccum, averageColor, averageAccum );
					averageCount++;
				}

				R_UploadSubImage( image, x * LIGHTMAP_LEN, y * LIGHTMAP_LEN,
					LIGHTMAP_LEN, LIGHTMAP_LEN, tr.lightmaps[ i ] );

				offs += LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3;
			}
		}
		ri.Printf( PRINT_DEVELOPER, "lightmaps[%i]=%i\n", i, tr.lightmaps[i]->texnum );
		if ( averageCount > 0 ) {
			VectorScale( averageAccum, 1.0f / (float)averageCount, tr.lightmapAverageColors[i] );
		} else {
			VectorClear( tr.lightmapAverageColors[i] );
		}
	}

	//if ( r_lightmap->integer == 2 )	{
	//	ri.Printf( PRINT_ALL, "Brightest lightmap value: %d\n", ( int ) ( maxIntensity * 255 ) );
	//}
}


/*
===============
R_LoadLightmaps
===============
*/
static void R_LoadLightmaps( const lump_t *l ) {
	const byte	*buf;
	byte		image[LIGHTMAP_LEN*LIGHTMAP_LEN*4];
	int			i, numLightmaps;
	float		maxIntensity = 0;

	tr.numLightmaps = 0;
	tr.lightmapAverageColors = NULL;
	tr.mergeLightmaps = qfalse;
	tr.lightmapScale[0] = 1.0f;
	tr.lightmapScale[1] = 1.0f;
	tr.lightmapOffset[0] = 0.0f;
	tr.lightmapOffset[1] = 0.0f;
	tr.lightmapMod = MAX_QINT;
	lightmapWidth = LIGHTMAP_SIZE;
	lightmapHeight = LIGHTMAP_SIZE;
	lightmapCountX = 1;
	lightmapCountY = 1;

	if ( l->filelen < LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3 ) {
		return;
	}

	// if we are in r_vertexLight mode, we don't need the lightmaps at all
	if ( r_vertexLight->integer || glConfig.hardwareType == GLHW_PERMEDIA2 ) {
		return;
	}

	numLightmaps = l->filelen / (LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3);

	if ( r_mergeLightmaps->integer && numLightmaps > 1 ) {
		// check for low texture sizes
		if ( glConfig.maxTextureSize >= LIGHTMAP_LEN * 2 ) {
			if ( textureBorderClampAvailable ) {
				tr.mergeLightmaps = qtrue;
				R_LoadMergedLightmaps( l, image ); // reuse stack space
				return;
			} else {
				ri.Printf( PRINT_DEVELOPER, "...GL_ARB_texture_border_clamp is not available, merged lighmaps disabled.\n" );
			}
		}
	}

	buf = fileBase + l->fileofs;

	// create all the lightmaps
	tr.numLightmaps = numLightmaps;

	// we are about to upload textures
	//R_IssuePendingRenderCommands();

	tr.lightmaps = ri.Hunk_Alloc( tr.numLightmaps * sizeof(image_t *), h_low );
	tr.lightmapAverageColors = ri.Hunk_Alloc( tr.numLightmaps * sizeof( *tr.lightmapAverageColors ), h_low );

	for ( i = 0 ; i < tr.numLightmaps ; i++ ) {
		maxIntensity = R_ProcessLightmap( image, buf + i * LIGHTMAP_SIZE * LIGHTMAP_SIZE * 3, maxIntensity );
		R_LightmapAverageColor( image, LIGHTMAP_SIZE, 0, 0, LIGHTMAP_SIZE, LIGHTMAP_SIZE,
			tr.lightmapAverageColors[i] );
		tr.lightmaps[i] = R_CreateImage( va( "*lightmap%d", i ), NULL, image, LIGHTMAP_SIZE, LIGHTMAP_SIZE,
			lightmapFlags | IMGFLAG_CLAMPTOEDGE );
	}

	//if ( r_lightmap->integer == 2 )	{
	//	ri.Printf( PRINT_ALL, "Brightest lightmap value: %d\n", ( int ) ( maxIntensity * 255 ) );
	//}
}


/*
=================
RE_SetWorldVisData

This is called by the clipmodel subsystem so we can share the 1.8 megs of
space in big maps...
=================
*/
void RE_SetWorldVisData( const byte *vis ) {
	tr.externalVisData = vis;
}


/*
=================
R_LoadVisibility
=================
*/
static void R_LoadVisibility( const lump_t *l ) {
	int		len;
	byte	*buf;

	len = PAD( s_worldData.numClusters, 64 );
	s_worldData.novis = ri.Hunk_Alloc( len, h_low );
	Com_Memset( s_worldData.novis, 0xff, len );

	len = l->filelen;
	if ( !len ) {
		return;
	}
	buf = fileBase + l->fileofs;

	s_worldData.numClusters = LittleLong( ((int *)buf)[0] );
	s_worldData.clusterBytes = LittleLong( ((int *)buf)[1] );

	// CM_Load should have given us the vis data to share, so
	// we don't need to allocate another copy
	if ( tr.externalVisData ) {
		s_worldData.vis = tr.externalVisData;
	} else {
		byte	*dest;

		dest = ri.Hunk_Alloc( len - 8, h_low );
		Com_Memcpy( dest, buf + 8, len - 8 );
		s_worldData.vis = dest;
	}
}

//===============================================================================


/*
===============
ShaderForShaderNum
===============
*/
static void R_ClearWorldSun( void )
{
	Com_Memset( &tr.worldSun, 0, sizeof( tr.worldSun ) );

	VectorSet( tr.worldSun.color, 1.0f, 1.0f, 1.0f );
	VectorSet( tr.worldSun.directionToSun, 0.45f, 0.3f, 0.9f );
	VectorNormalize( tr.worldSun.directionToSun );
	VectorScale( tr.worldSun.directionToSun, -1.0f, tr.worldSun.lightDirection );
	VectorClear( tr.worldSun.light );
	tr.worldSun.intensity = 0.0f;

	VectorCopy( tr.worldSun.directionToSun, tr.sunDirection );
	VectorCopy( tr.worldSun.color, tr.sunColor );
	VectorCopy( tr.worldSun.light, tr.sunLight );
	tr.sunIntensity = tr.worldSun.intensity;
	tr.sunParmsValid = qfalse;
}

static void R_SetWorldSunFromShader( const shader_t *shader )
{
	vec3_t directionToSun;

	if ( !shader || !shader->skySunValid || shader->skySunIntensity <= 0.0f ) {
		return;
	}
	if ( VectorNormalize2( shader->skySunDirection, directionToSun ) <= 0.0f ) {
		return;
	}

	Com_Memset( &tr.worldSun, 0, sizeof( tr.worldSun ) );
	tr.worldSun.valid = qtrue;
	Q_strncpyz( tr.worldSun.shaderName, shader->name, sizeof( tr.worldSun.shaderName ) );
	VectorCopy( shader->skySunColor, tr.worldSun.color );
	VectorCopy( directionToSun, tr.worldSun.directionToSun );
	VectorScale( directionToSun, -1.0f, tr.worldSun.lightDirection );
	VectorCopy( shader->skySunLight, tr.worldSun.light );
	tr.worldSun.intensity = shader->skySunIntensity;

	VectorCopy( tr.worldSun.color, tr.sunColor );
	VectorCopy( tr.worldSun.directionToSun, tr.sunDirection );
	VectorCopy( tr.worldSun.light, tr.sunLight );
	tr.sunIntensity = tr.worldSun.intensity;
	tr.sunParmsValid = qtrue;
}

static void R_ClearStaticMapLights( void )
{
	Com_Memset( &tr.staticMapLights, 0, sizeof( tr.staticMapLights ) );
}

static qboolean R_StaticMapLightsSkipCompound( const char **p, const char *openToken )
{
	const char *token;
	int objectDepth;
	int arrayDepth;

	objectDepth = ( openToken[0] == '{' ) ? 1 : 0;
	arrayDepth = ( openToken[0] == '[' ) ? 1 : 0;

	while ( objectDepth > 0 || arrayDepth > 0 ) {
		token = COM_ParseComplex( p, qtrue );
		if ( !*token ) {
			return qfalse;
		}
		if ( !strcmp( token, "{" ) ) {
			objectDepth++;
		} else if ( !strcmp( token, "}" ) ) {
			objectDepth--;
		} else if ( !strcmp( token, "[" ) ) {
			arrayDepth++;
		} else if ( !strcmp( token, "]" ) ) {
			arrayDepth--;
		}
	}

	return qtrue;
}

static qboolean R_StaticMapLightsSkipValue( const char **p )
{
	const char *token;

	token = COM_ParseComplex( p, qtrue );
	if ( !*token ) {
		return qfalse;
	}
	if ( !strcmp( token, "{" ) || !strcmp( token, "[" ) ) {
		return R_StaticMapLightsSkipCompound( p, token );
	}
	return qtrue;
}

static qboolean R_StaticMapLightsExpectToken( const char **p, const char *expected )
{
	const char *token;

	token = COM_ParseComplex( p, qtrue );
	return ( strcmp( token, expected ) == 0 ) ? qtrue : qfalse;
}

static qboolean R_StaticMapLightsParseFloat( const char **p, float *value )
{
	const char *token;

	token = COM_ParseComplex( p, qtrue );
	if ( !*token || strchr( "{}[],:", token[0] ) ) {
		return qfalse;
	}
	*value = Q_atof( token );
	return qtrue;
}

static qboolean R_StaticMapLightsParseInt( const char **p, int *value )
{
	float parsed;

	if ( !R_StaticMapLightsParseFloat( p, &parsed ) ) {
		return qfalse;
	}
	*value = (int)parsed;
	return qtrue;
}

static qboolean R_StaticMapLightsParseVec3( const char **p, vec3_t value )
{
	const char *token;
	int i;

	if ( !R_StaticMapLightsExpectToken( p, "[" ) ) {
		return qfalse;
	}
	for ( i = 0; i < 3; i++ ) {
		if ( !R_StaticMapLightsParseFloat( p, &value[i] ) ) {
			return qfalse;
		}
		token = COM_ParseComplex( p, qtrue );
		if ( i < 2 ) {
			if ( strcmp( token, "," ) != 0 ) {
				return qfalse;
			}
		} else if ( strcmp( token, "]" ) != 0 ) {
			return qfalse;
		}
	}

	return qtrue;
}

static qboolean R_StaticMapLightsParseBool( const char **p, qboolean *value )
{
	const char *token;

	token = COM_ParseComplex( p, qtrue );
	if ( !Q_stricmp( token, "true" ) || !Q_stricmp( token, "1" ) ) {
		*value = qtrue;
		return qtrue;
	}
	if ( !Q_stricmp( token, "false" ) || !Q_stricmp( token, "0" ) ) {
		*value = qfalse;
		return qtrue;
	}
	return qfalse;
}

static void R_StaticMapLightsNormalizeColor( vec3_t color )
{
	float maxComponent;
	int i;

	maxComponent = color[0];
	if ( color[1] > maxComponent ) {
		maxComponent = color[1];
	}
	if ( color[2] > maxComponent ) {
		maxComponent = color[2];
	}
	if ( maxComponent > 4.0f ) {
		VectorScale( color, 1.0f / 255.0f, color );
	}

	for ( i = 0; i < 3; i++ ) {
		color[i] = Com_Clamp( 0.0f, 4.0f, color[i] );
	}
}

static qboolean R_StaticMapLightsParseLightObject( const char **p, mapLightDef_t *light, qboolean *accepted, int *skipReason )
{
	const char *token;
	qboolean hasOrigin;
	qboolean hasDirection;
	qboolean unsupportedType;

	Com_Memset( light, 0, sizeof( *light ) );
	light->type = MAP_LIGHT_POINT;
	light->leafCluster = -1;
	light->leafArea = -1;
	VectorSet( light->direction, 0.0f, 0.0f, -1.0f );
	VectorSet( light->color, 1.0f, 1.0f, 1.0f );
	light->castsShadows = qtrue;
	light->designerPriority = 1.0f;
	light->resolution = 256;
	light->outerAngle = 45.0f;

	hasOrigin = qfalse;
	hasDirection = qfalse;
	unsupportedType = qfalse;
	*accepted = qfalse;
	*skipReason = 0;

	while ( 1 ) {
		char key[MAX_TOKEN_CHARS];

		token = COM_ParseComplex( p, qtrue );
		if ( !*token ) {
			return qfalse;
		}
		if ( !strcmp( token, "}" ) ) {
			break;
		}
		if ( !strcmp( token, "," ) ) {
			continue;
		}

		Q_strncpyz( key, token, sizeof( key ) );
		if ( !R_StaticMapLightsExpectToken( p, ":" ) ) {
			return qfalse;
		}

		if ( !Q_stricmp( key, "name" ) ) {
			token = COM_ParseComplex( p, qtrue );
			if ( !*token ) {
				return qfalse;
			}
			Q_strncpyz( light->name, token, sizeof( light->name ) );
		} else if ( !Q_stricmp( key, "type" ) ) {
			token = COM_ParseComplex( p, qtrue );
			if ( !*token ) {
				return qfalse;
			}
			if ( !Q_stricmp( token, "point" ) ) {
				light->type = MAP_LIGHT_POINT;
			} else if ( !Q_stricmp( token, "spot" ) ) {
				light->type = MAP_LIGHT_SPOT;
			} else {
				unsupportedType = qtrue;
			}
		} else if ( !Q_stricmp( key, "origin" ) ) {
			if ( !R_StaticMapLightsParseVec3( p, light->origin ) ) {
				return qfalse;
			}
			hasOrigin = qtrue;
		} else if ( !Q_stricmp( key, "direction" ) ) {
			if ( !R_StaticMapLightsParseVec3( p, light->direction ) ) {
				return qfalse;
			}
			hasDirection = ( VectorNormalize( light->direction ) > 0.0f ) ? qtrue : qfalse;
		} else if ( !Q_stricmp( key, "color" ) ) {
			if ( !R_StaticMapLightsParseVec3( p, light->color ) ) {
				return qfalse;
			}
		} else if ( !Q_stricmp( key, "intensity" ) ) {
			if ( !R_StaticMapLightsParseFloat( p, &light->intensity ) ) {
				return qfalse;
			}
		} else if ( !Q_stricmp( key, "radius" ) ) {
			if ( !R_StaticMapLightsParseFloat( p, &light->radius ) ) {
				return qfalse;
			}
		} else if ( !Q_stricmp( key, "innerAngle" ) ) {
			if ( !R_StaticMapLightsParseFloat( p, &light->innerAngle ) ) {
				return qfalse;
			}
		} else if ( !Q_stricmp( key, "outerAngle" ) ) {
			if ( !R_StaticMapLightsParseFloat( p, &light->outerAngle ) ) {
				return qfalse;
			}
		} else if ( !Q_stricmp( key, "castsShadows" ) ) {
			if ( !R_StaticMapLightsParseBool( p, &light->castsShadows ) ) {
				return qfalse;
			}
		} else if ( !Q_stricmp( key, "resolution" ) ) {
			if ( !R_StaticMapLightsParseInt( p, &light->resolution ) ) {
				return qfalse;
			}
		} else if ( !Q_stricmp( key, "priority" ) ) {
			if ( !R_StaticMapLightsParseFloat( p, &light->designerPriority ) ) {
				return qfalse;
			}
		} else if ( !Q_stricmp( key, "style" ) ) {
			if ( !R_StaticMapLightsParseInt( p, &light->style ) ) {
				return qfalse;
			}
		} else if ( !R_StaticMapLightsSkipValue( p ) ) {
			return qfalse;
		}
	}

	if ( unsupportedType ) {
		*skipReason = 1;
		return qtrue;
	}

	if ( light->radius <= 0.0f && light->intensity > 0.0f ) {
		light->radius = light->intensity;
	}
	if ( light->intensity <= 0.0f ) {
		light->intensity = light->radius;
	}

	R_StaticMapLightsNormalizeColor( light->color );
	light->radius = Com_Clamp( 1.0f, 8192.0f, light->radius );
	light->intensity = Com_Clamp( 1.0f, 8192.0f, light->intensity );
	light->designerPriority = Com_Clamp( 0.0f, 16.0f, light->designerPriority );
	light->resolution = (int)Com_Clamp( 64.0f, 1024.0f, (float)light->resolution );
	light->outerAngle = Com_Clamp( 1.0f, 179.0f, light->outerAngle );
	light->innerAngle = Com_Clamp( 0.0f, light->outerAngle, light->innerAngle );

	if ( !hasOrigin || light->designerPriority <= 0.0f ) {
		*skipReason = 2;
		return qtrue;
	}
	if ( light->type == MAP_LIGHT_SPOT && !hasDirection ) {
		*skipReason = 2;
		return qtrue;
	}

	*accepted = qtrue;
	return qtrue;
}

static qboolean R_StaticMapLightsParseLightsArray( const char **p )
{
	const char *token;

	if ( !R_StaticMapLightsExpectToken( p, "[" ) ) {
		return qfalse;
	}

	while ( 1 ) {
		token = COM_ParseComplex( p, qtrue );
		if ( !*token ) {
			return qfalse;
		}
		if ( !strcmp( token, "]" ) ) {
			break;
		}
		if ( !strcmp( token, "," ) ) {
			continue;
		}
		if ( strcmp( token, "{" ) != 0 ) {
			return qfalse;
		}

		if ( tr.staticMapLights.count >= MAX_STATIC_MAP_LIGHTS ) {
			tr.staticMapLights.skippedOverflow++;
			if ( !R_StaticMapLightsSkipCompound( p, "{" ) ) {
				return qfalse;
			}
		} else {
			mapLightDef_t light;
			qboolean accepted;
			int skipReason;

			if ( !R_StaticMapLightsParseLightObject( p, &light, &accepted, &skipReason ) ) {
				return qfalse;
			}
			if ( accepted ) {
				if ( R_PointLeafClusterArea( light.origin, &light.leafCluster, &light.leafArea ) ) {
					tr.staticMapLights.spatialized++;
				} else {
					tr.staticMapLights.spatialFallback++;
				}
				if ( light.type == MAP_LIGHT_SPOT ) {
					tr.staticMapLights.spotCount++;
				} else {
					tr.staticMapLights.pointCount++;
				}
				tr.staticMapLights.lights[tr.staticMapLights.count++] = light;
			} else if ( skipReason == 1 ) {
				tr.staticMapLights.skippedUnsupported++;
			} else {
				tr.staticMapLights.skippedInvalid++;
			}
		}
	}

	return qtrue;
}

static qboolean R_ParseStaticMapLights( const char *text )
{
	const char *p;
	const char *token;

	p = text;
	token = COM_ParseComplex( &p, qtrue );
	if ( strcmp( token, "{" ) != 0 ) {
		return qfalse;
	}

	while ( 1 ) {
		char key[MAX_TOKEN_CHARS];

		token = COM_ParseComplex( &p, qtrue );
		if ( !*token ) {
			return qfalse;
		}
		if ( !strcmp( token, "}" ) ) {
			break;
		}
		if ( !strcmp( token, "," ) ) {
			continue;
		}

		Q_strncpyz( key, token, sizeof( key ) );
		if ( !R_StaticMapLightsExpectToken( &p, ":" ) ) {
			return qfalse;
		}

		if ( !Q_stricmp( key, "version" ) ) {
			if ( !R_StaticMapLightsParseInt( &p, &tr.staticMapLights.version ) ) {
				return qfalse;
			}
		} else if ( !Q_stricmp( key, "lights" ) ) {
			if ( !R_StaticMapLightsParseLightsArray( &p ) ) {
				return qfalse;
			}
		} else if ( !R_StaticMapLightsSkipValue( &p ) ) {
			return qfalse;
		}
	}

	return qtrue;
}

static void R_LoadStaticMapLightsForWorld( void )
{
	union {
		char *c;
		void *v;
	} buffer;
	int size;
	char filename[MAX_QPATH];

	buffer.v = NULL;
	Com_sprintf( filename, sizeof( filename ), "maps/%s.lights.json", s_worldData.baseName );
	R_ClearStaticMapLights();
	Q_strncpyz( tr.staticMapLights.filename, filename, sizeof( tr.staticMapLights.filename ) );

	size = ri.FS_ReadFile( filename, &buffer.v );
	if ( !buffer.v || size <= 0 ) {
		if ( buffer.v ) {
			ri.FS_FreeFile( buffer.v );
		}
		return;
	}

	tr.staticMapLights.loaded = qtrue;
	if ( !R_ParseStaticMapLights( buffer.c ) ) {
		tr.staticMapLights.parseFailed = qtrue;
		tr.staticMapLights.count = 0;
		ri.Printf( PRINT_WARNING, "WARNING: failed to parse static map lights file %s\n", filename );
	} else if ( r_staticLightDebug && r_staticLightDebug->integer ) {
		ri.Printf( PRINT_ALL,
			"static lights file:%s version:%i loaded:%i point:%i spot:%i spatial:%i/%i skipped unsupported:%i invalid:%i overflow:%i\n",
			filename, tr.staticMapLights.version, tr.staticMapLights.count,
			tr.staticMapLights.pointCount, tr.staticMapLights.spotCount,
			tr.staticMapLights.spatialized, tr.staticMapLights.spatialFallback,
			tr.staticMapLights.skippedUnsupported, tr.staticMapLights.skippedInvalid,
			tr.staticMapLights.skippedOverflow );
	}

	ri.FS_FreeFile( buffer.v );
}

void R_StaticMapLightsReload_f( void )
{
	if ( !tr.worldMapLoaded || !tr.world ) {
		R_ClearStaticMapLights();
		ri.Printf( PRINT_ALL, "No world map loaded; static map lights cleared\n" );
		return;
	}

	R_LoadStaticMapLightsForWorld();
	ri.Printf( PRINT_ALL, "Reloaded %i static map lights from %s\n",
		tr.staticMapLights.count, tr.staticMapLights.filename );
}

static void R_ClearSurfaceLightProxies( void )
{
	Com_Memset( &tr.surfaceLightProxies, 0, sizeof( tr.surfaceLightProxies ) );
}

#define SURFACELIGHT_PROXY_SUBDIVIDE_MIN_SIZE 64.0f
#define SURFACELIGHT_PROXY_SUBDIVIDE_MAX_SIZE 1024.0f
#define SURFACELIGHT_PROXY_SUBDIVIDE_MAX_AXIS 4
#define SURFACELIGHT_PROXY_SUBDIVIDE_MAX_BUCKETS ( SURFACELIGHT_PROXY_SUBDIVIDE_MAX_AXIS * SURFACELIGHT_PROXY_SUBDIVIDE_MAX_AXIS )

typedef struct {
	vec3_t centroidAccum;
	vec3_t normalAccum;
	vec3_t colorAccum;
	vec3_t boundsMins;
	vec3_t boundsMaxs;
	float area;
} surfaceLightProxyAccum_t;

typedef struct {
	vec3_t axis[2];
	float mins[2];
	float maxs[2];
	float cellSpan[2];
	float targetSize;
	int cells[2];
} surfaceLightProxySubdivision_t;

static void R_ClearSurfaceLightProxyAccum( surfaceLightProxyAccum_t *accum )
{
	VectorClear( accum->centroidAccum );
	VectorClear( accum->normalAccum );
	VectorClear( accum->colorAccum );
	VectorSet( accum->boundsMins, 999999.0f, 999999.0f, 999999.0f );
	VectorSet( accum->boundsMaxs, -999999.0f, -999999.0f, -999999.0f );
	accum->area = 0.0f;
}

static void R_SurfaceLightAccumBoundsPoint( surfaceLightProxyAccum_t *accum, const vec3_t point )
{
	int axis;

	for ( axis = 0; axis < 3; axis++ ) {
		if ( point[axis] < accum->boundsMins[axis] ) {
			accum->boundsMins[axis] = point[axis];
		}
		if ( point[axis] > accum->boundsMaxs[axis] ) {
			accum->boundsMaxs[axis] = point[axis];
		}
	}
}

static void R_SurfaceLightAccumTriangleBounds( surfaceLightProxyAccum_t *accum,
	const vec3_t a, const vec3_t b, const vec3_t c )
{
	R_SurfaceLightAccumBoundsPoint( accum, a );
	R_SurfaceLightAccumBoundsPoint( accum, b );
	R_SurfaceLightAccumBoundsPoint( accum, c );
}

static void R_SurfaceLightAccumulateVertexColor( vec3_t colorAccum, float area,
	const vec3_t color0, const vec3_t color1, const vec3_t color2 )
{
	vec3_t color;

	VectorAdd( color0, color1, color );
	VectorAdd( color, color2, color );
	VectorScale( color, area / 3.0f, color );
	VectorAdd( colorAccum, color, colorAccum );
}

static void R_SurfaceLightPointColor( const float *point, vec3_t color )
{
	const byte *rgba = (const byte *)&point[7];

	color[0] = rgba[0] / 255.0f;
	color[1] = rgba[1] / 255.0f;
	color[2] = rgba[2] / 255.0f;
}

static void R_SurfaceLightDrawVertColor( const drawVert_t *vert, vec3_t color )
{
	color[0] = vert->color.rgba[0] / 255.0f;
	color[1] = vert->color.rgba[1] / 255.0f;
	color[2] = vert->color.rgba[2] / 255.0f;
}

static qboolean R_SurfaceLightColorIsUseful( const vec3_t color )
{
	float maxColor;

	maxColor = MAX( color[0], MAX( color[1], color[2] ) );
	return ( maxColor > 0.01f ) ? qtrue : qfalse;
}

static qboolean R_SurfaceLightLightmapColor( const shader_t *shader, vec3_t color )
{
	if ( !tr.lightmapAverageColors || shader->lightmapIndex < 0 || shader->lightmapIndex >= tr.numLightmaps ) {
		return qfalse;
	}

	VectorCopy( tr.lightmapAverageColors[shader->lightmapIndex], color );
	return R_SurfaceLightColorIsUseful( color );
}

static void R_SurfaceLightResolveColor( const shader_t *shader,
	const vec3_t colorAccum, float area, vec3_t color )
{
	vec3_t bakedColor;

	if ( shader->surfaceLightColorValid ) {
		VectorCopy( shader->surfaceLightColor, color );
		return;
	}

	if ( shader->surfaceLightImageColorValid &&
		R_SurfaceLightColorIsUseful( shader->surfaceLightImageColor ) ) {
		VectorCopy( shader->surfaceLightImageColor, color );
		return;
	}

	if ( R_SurfaceLightLightmapColor( shader, bakedColor ) ) {
		color[0] = Com_Clamp( 0.0f, 1.0f, bakedColor[0] );
		color[1] = Com_Clamp( 0.0f, 1.0f, bakedColor[1] );
		color[2] = Com_Clamp( 0.0f, 1.0f, bakedColor[2] );
		return;
	}

	if ( area > 1.0f ) {
		VectorScale( colorAccum, 1.0f / area, bakedColor );
		if ( R_SurfaceLightColorIsUseful( bakedColor ) ) {
			color[0] = Com_Clamp( 0.0f, 1.0f, bakedColor[0] );
			color[1] = Com_Clamp( 0.0f, 1.0f, bakedColor[1] );
			color[2] = Com_Clamp( 0.0f, 1.0f, bakedColor[2] );
			return;
		}
	}

	VectorCopy( shader->surfaceLightColor, color );
}

static qboolean R_SurfaceLightTriangleInfo( const vec3_t a, const vec3_t b, const vec3_t c,
	vec3_t centroid, vec3_t cross, float *area )
{
	vec3_t edge0;
	vec3_t edge1;
	float doubleArea;

	VectorSubtract( b, a, edge0 );
	VectorSubtract( c, a, edge1 );
	CrossProduct( edge0, edge1, cross );
	doubleArea = VectorLength( cross );
	if ( doubleArea <= 0.0f ) {
		return qfalse;
	}

	*area = doubleArea * 0.5f;
	VectorAdd( a, b, centroid );
	VectorAdd( centroid, c, centroid );
	VectorScale( centroid, 1.0f / 3.0f, centroid );
	return qtrue;
}

static void R_SurfaceLightAccumulateTriangle( const vec3_t a, const vec3_t b, const vec3_t c,
	const vec3_t color0, const vec3_t color1, const vec3_t color2, surfaceLightProxyAccum_t *accum )
{
	vec3_t cross;
	vec3_t centroid;
	float area;

	if ( !R_SurfaceLightTriangleInfo( a, b, c, centroid, cross, &area ) ) {
		return;
	}

	VectorMA( accum->centroidAccum, area, centroid, accum->centroidAccum );
	VectorAdd( accum->normalAccum, cross, accum->normalAccum );
	R_SurfaceLightAccumulateVertexColor( accum->colorAccum, area, color0, color1, color2 );
	R_SurfaceLightAccumTriangleBounds( accum, a, b, c );
	accum->area += area;
}

static float R_SurfaceLightProxyRadius( const shader_t *shader, float area )
{
	float extent;
	float radius;

	extent = sqrtf( area );
	radius = extent * 2.0f + sqrtf( shader->surfaceLight ) * 8.0f;
	if ( shader->surfaceLightSubdivide > 0.0f && radius < shader->surfaceLightSubdivide ) {
		radius = shader->surfaceLightSubdivide;
	}

	return Com_Clamp( 64.0f, 4096.0f, radius );
}

static float R_SurfaceLightProxyFootprintRadius( const surfaceLightProxyAccum_t *accum,
	const vec3_t normal )
{
	vec3_t axes[2];
	float mins[2];
	float maxs[2];
	float radius;
	float areaRadius;
	int corner;
	int axis;

	if ( accum->boundsMaxs[0] < accum->boundsMins[0] || accum->area <= 1.0f ) {
		return 16.0f;
	}

	MakeNormalVectors( normal, axes[0], axes[1] );
	mins[0] = mins[1] = 999999.0f;
	maxs[0] = maxs[1] = -999999.0f;

	for ( corner = 0; corner < 8; corner++ ) {
		vec3_t point;

		point[0] = ( corner & 1 ) ? accum->boundsMaxs[0] : accum->boundsMins[0];
		point[1] = ( corner & 2 ) ? accum->boundsMaxs[1] : accum->boundsMins[1];
		point[2] = ( corner & 4 ) ? accum->boundsMaxs[2] : accum->boundsMins[2];

		for ( axis = 0; axis < 2; axis++ ) {
			float coord = DotProduct( point, axes[axis] );
			if ( coord < mins[axis] ) {
				mins[axis] = coord;
			}
			if ( coord > maxs[axis] ) {
				maxs[axis] = coord;
			}
		}
	}

	radius = 0.5f * sqrtf( Square( maxs[0] - mins[0] ) +
		Square( maxs[1] - mins[1] ) );
	areaRadius = sqrtf( accum->area / (float)M_PI );
	return Com_Clamp( 16.0f, 2048.0f, MAX( radius, areaRadius ) );
}

static float R_SurfaceLightProxyShadowCasterRadius( const shader_t *shader,
	float footprintRadius, float proxyRadius )
{
	float radius;

	radius = footprintRadius * 1.75f + sqrtf( MAX( shader->surfaceLight, 1.0f ) ) * 6.0f;
	if ( shader->surfaceLightSubdivide > 0.0f ) {
		radius = MAX( radius, shader->surfaceLightSubdivide * 1.25f );
	}

	radius = Com_Clamp( 128.0f, 2048.0f, radius );
	if ( proxyRadius > 0.0f ) {
		radius = MIN( radius, proxyRadius );
	}
	return MAX( radius, 64.0f );
}

static float R_SurfaceLightProxyShadowConeAngle( float footprintRadius, float casterRadius )
{
	float supportRadius;
	float angle;

	supportRadius = MAX( 64.0f, footprintRadius * 1.5f );
	angle = atan2f( supportRadius, MAX( casterRadius, 64.0f ) ) * 180.0f /
		(float)M_PI;
	return Com_Clamp( 35.0f, 70.0f, angle );
}

static surfaceLightProxyProjection_t R_SurfaceLightProxyProjection( float area )
{
	if ( area <= Square( 96.0f ) ) {
		return SURFACE_LIGHT_PROXY_POINT;
	}
	return SURFACE_LIGHT_PROXY_SPOT;
}

static qboolean R_AddSurfaceLightProxy( int surfaceIndex, const shader_t *shader,
	const surfaceLightProxyAccum_t *accum )
{
	surfaceLightProxy_t *proxy;
	vec3_t origin;
	vec3_t normal;
	float offset;

	if ( accum->area <= 1.0f ) {
		tr.surfaceLightProxies.skippedInvalid++;
		return qfalse;
	}
	if ( VectorNormalize2( accum->normalAccum, normal ) <= 0.0f ) {
		tr.surfaceLightProxies.skippedInvalid++;
		return qfalse;
	}

	if ( tr.surfaceLightProxies.count >= MAX_SURFACELIGHT_PROXIES ) {
		tr.surfaceLightProxies.skippedOverflow++;
		return qfalse;
	}

	VectorScale( accum->centroidAccum, 1.0f / accum->area, origin );

	proxy = &tr.surfaceLightProxies.proxies[tr.surfaceLightProxies.count++];
	Com_Memset( proxy, 0, sizeof( *proxy ) );
	proxy->sourceSurface = surfaceIndex;
	proxy->projection = R_SurfaceLightProxyProjection( accum->area );
	proxy->leafCluster = -1;
	proxy->leafArea = -1;
	Q_strncpyz( proxy->shaderName, shader->name, sizeof( proxy->shaderName ) );
	proxy->area = accum->area;
	proxy->intensity = shader->surfaceLight;
	proxy->radius = R_SurfaceLightProxyRadius( shader, accum->area );
	proxy->footprintRadius = R_SurfaceLightProxyFootprintRadius( accum, normal );
	proxy->shadowCasterRadius = R_SurfaceLightProxyShadowCasterRadius( shader,
		proxy->footprintRadius, proxy->radius );
	proxy->shadowConeAngle = R_SurfaceLightProxyShadowConeAngle(
		proxy->footprintRadius, proxy->shadowCasterRadius );
	proxy->designerPriority = Com_Clamp( 0.25f, 4.0f, proxy->radius / 512.0f );
	proxy->castsShadows = qtrue;
	VectorCopy( normal, proxy->normal );
	R_SurfaceLightResolveColor( shader, accum->colorAccum, accum->area, proxy->color );
	if ( proxy->projection == SURFACE_LIGHT_PROXY_SPOT ) {
		tr.surfaceLightProxies.spotProjectionCount++;
	} else {
		tr.surfaceLightProxies.pointProjectionCount++;
	}

	offset = Com_Clamp( 8.0f, 64.0f, proxy->radius * 0.05f );
	VectorMA( origin, offset, normal, proxy->origin );
	if ( R_PointLeafClusterArea( proxy->origin, &proxy->leafCluster, &proxy->leafArea ) ) {
		tr.surfaceLightProxies.spatialized++;
	} else {
		tr.surfaceLightProxies.spatialFallback++;
	}
	return qtrue;
}

static qboolean R_SurfaceLightBeginSubdivision( const shader_t *shader,
	const surfaceLightProxyAccum_t *accum, surfaceLightProxySubdivision_t *subdiv )
{
	vec3_t normal;

	if ( shader->surfaceLightSubdivide <= 0.0f ||
		accum->area <= Square( SURFACELIGHT_PROXY_SUBDIVIDE_MIN_SIZE ) ) {
		return qfalse;
	}
	if ( VectorNormalize2( accum->normalAccum, normal ) <= 0.0f ) {
		return qfalse;
	}

	Com_Memset( subdiv, 0, sizeof( *subdiv ) );
	MakeNormalVectors( normal, subdiv->axis[0], subdiv->axis[1] );
	subdiv->targetSize = Com_Clamp( SURFACELIGHT_PROXY_SUBDIVIDE_MIN_SIZE,
		SURFACELIGHT_PROXY_SUBDIVIDE_MAX_SIZE, shader->surfaceLightSubdivide );
	subdiv->mins[0] = subdiv->mins[1] = 999999.0f;
	subdiv->maxs[0] = subdiv->maxs[1] = -999999.0f;
	return qtrue;
}

static void R_SurfaceLightSubdivisionAddPoint( surfaceLightProxySubdivision_t *subdiv, const vec3_t point )
{
	int axis;

	for ( axis = 0; axis < 2; axis++ ) {
		float coord;

		coord = DotProduct( point, subdiv->axis[axis] );
		if ( coord < subdiv->mins[axis] ) {
			subdiv->mins[axis] = coord;
		}
		if ( coord > subdiv->maxs[axis] ) {
			subdiv->maxs[axis] = coord;
		}
	}
}

static qboolean R_SurfaceLightFinalizeSubdivision( surfaceLightProxySubdivision_t *subdiv )
{
	int axis;

	for ( axis = 0; axis < 2; axis++ ) {
		float span;
		int cells;

		if ( subdiv->maxs[axis] < subdiv->mins[axis] ) {
			return qfalse;
		}

		span = subdiv->maxs[axis] - subdiv->mins[axis];
		if ( span <= subdiv->targetSize * 1.1f ) {
			cells = 1;
		} else {
			cells = (int)ceilf( span / subdiv->targetSize );
			if ( cells < 1 ) {
				cells = 1;
			} else if ( cells > SURFACELIGHT_PROXY_SUBDIVIDE_MAX_AXIS ) {
				cells = SURFACELIGHT_PROXY_SUBDIVIDE_MAX_AXIS;
			}
		}

		subdiv->cells[axis] = cells;
		subdiv->cellSpan[axis] = ( span > 1.0f ) ? ( span / (float)cells ) : subdiv->targetSize;
	}

	return ( subdiv->cells[0] * subdiv->cells[1] > 1 ) ? qtrue : qfalse;
}

static int R_SurfaceLightSubdivisionBucketForCentroid( const surfaceLightProxySubdivision_t *subdiv,
	const vec3_t centroid )
{
	int cell[2];
	int axis;

	for ( axis = 0; axis < 2; axis++ ) {
		float coord;

		coord = ( DotProduct( centroid, subdiv->axis[axis] ) - subdiv->mins[axis] ) /
			subdiv->cellSpan[axis];
		cell[axis] = (int)floorf( coord );
		if ( cell[axis] < 0 ) {
			cell[axis] = 0;
		} else if ( cell[axis] >= subdiv->cells[axis] ) {
			cell[axis] = subdiv->cells[axis] - 1;
		}
	}

	return cell[1] * subdiv->cells[0] + cell[0];
}

static void R_SurfaceLightAccumulateTriangleForSubdivision( const vec3_t a, const vec3_t b, const vec3_t c,
	const vec3_t color0, const vec3_t color1, const vec3_t color2,
	const surfaceLightProxySubdivision_t *subdiv, surfaceLightProxyAccum_t *buckets )
{
	surfaceLightProxyAccum_t *accum;
	vec3_t cross;
	vec3_t centroid;
	float area;
	int bucket;

	if ( !R_SurfaceLightTriangleInfo( a, b, c, centroid, cross, &area ) ) {
		return;
	}

	bucket = R_SurfaceLightSubdivisionBucketForCentroid( subdiv, centroid );
	accum = &buckets[bucket];
	VectorMA( accum->centroidAccum, area, centroid, accum->centroidAccum );
	VectorAdd( accum->normalAccum, cross, accum->normalAccum );
	R_SurfaceLightAccumulateVertexColor( accum->colorAccum, area, color0, color1, color2 );
	R_SurfaceLightAccumTriangleBounds( accum, a, b, c );
	accum->area += area;
}

static qboolean R_AddSurfaceLightBucketedProxies( int surfaceIndex, const shader_t *shader,
	const surfaceLightProxyAccum_t *total, surfaceLightProxyAccum_t *buckets, int bucketCount )
{
	qboolean added;
	int activeBuckets;
	int i;

	activeBuckets = 0;
	for ( i = 0; i < bucketCount; i++ ) {
		if ( buckets[i].area > 1.0f ) {
			activeBuckets++;
		}
	}
	if ( activeBuckets <= 1 ) {
		return R_AddSurfaceLightProxy( surfaceIndex, shader, total );
	}

	tr.surfaceLightProxies.subdividedSurfaces++;
	added = qfalse;
	for ( i = 0; i < bucketCount; i++ ) {
		if ( buckets[i].area <= 1.0f ) {
			continue;
		}
		if ( R_AddSurfaceLightProxy( surfaceIndex, shader, &buckets[i] ) ) {
			tr.surfaceLightProxies.subdivisionProxies++;
			added = qtrue;
		}
	}

	return added;
}

static qboolean R_BuildSurfaceLightFaceProxy( int surfaceIndex, const shader_t *shader, const srfSurfaceFace_t *face )
{
	const int *indices;
	surfaceLightProxyAccum_t accum;
	surfaceLightProxySubdivision_t subdiv;
	qboolean subdivide;
	int i;

	if ( !face || face->numPoints <= 0 || face->numIndices < 3 ) {
		tr.surfaceLightProxies.skippedInvalid++;
		return qfalse;
	}

	indices = (const int *)( (const byte *)face + face->ofsIndices );
	R_ClearSurfaceLightProxyAccum( &accum );

	for ( i = 0; i + 2 < face->numIndices; i += 3 ) {
		int i0 = indices[i + 0];
		int i1 = indices[i + 1];
		int i2 = indices[i + 2];
		vec3_t color0;
		vec3_t color1;
		vec3_t color2;

		if ( i0 < 0 || i0 >= face->numPoints || i1 < 0 || i1 >= face->numPoints ||
			i2 < 0 || i2 >= face->numPoints ) {
			continue;
		}
		R_SurfaceLightPointColor( face->points[i0], color0 );
		R_SurfaceLightPointColor( face->points[i1], color1 );
		R_SurfaceLightPointColor( face->points[i2], color2 );
		R_SurfaceLightAccumulateTriangle( face->points[i0], face->points[i1], face->points[i2],
			color0, color1, color2, &accum );
	}

	if ( VectorLengthSquared( accum.normalAccum ) <= 0.0f ) {
		VectorCopy( face->plane.normal, accum.normalAccum );
		VectorScale( accum.normalAccum, accum.area, accum.normalAccum );
	}

	subdivide = R_SurfaceLightBeginSubdivision( shader, &accum, &subdiv );
	if ( subdivide ) {
		surfaceLightProxyAccum_t buckets[SURFACELIGHT_PROXY_SUBDIVIDE_MAX_BUCKETS];
		int bucketCount;

		for ( i = 0; i < face->numPoints; i++ ) {
			R_SurfaceLightSubdivisionAddPoint( &subdiv, face->points[i] );
		}
		subdivide = R_SurfaceLightFinalizeSubdivision( &subdiv );
		if ( subdivide ) {
			for ( i = 0; i < SURFACELIGHT_PROXY_SUBDIVIDE_MAX_BUCKETS; i++ ) {
				R_ClearSurfaceLightProxyAccum( &buckets[i] );
			}
			for ( i = 0; i + 2 < face->numIndices; i += 3 ) {
				int i0 = indices[i + 0];
				int i1 = indices[i + 1];
				int i2 = indices[i + 2];
				vec3_t color0;
				vec3_t color1;
				vec3_t color2;

				if ( i0 < 0 || i0 >= face->numPoints || i1 < 0 || i1 >= face->numPoints ||
					i2 < 0 || i2 >= face->numPoints ) {
					continue;
				}
				R_SurfaceLightPointColor( face->points[i0], color0 );
				R_SurfaceLightPointColor( face->points[i1], color1 );
				R_SurfaceLightPointColor( face->points[i2], color2 );
				R_SurfaceLightAccumulateTriangleForSubdivision( face->points[i0], face->points[i1],
					face->points[i2], color0, color1, color2, &subdiv, buckets );
			}
			bucketCount = subdiv.cells[0] * subdiv.cells[1];
			return R_AddSurfaceLightBucketedProxies( surfaceIndex, shader, &accum, buckets, bucketCount );
		}
	}

	return R_AddSurfaceLightProxy( surfaceIndex, shader, &accum );
}

static qboolean R_BuildSurfaceLightGridProxy( int surfaceIndex, const shader_t *shader, const srfGridMesh_t *grid )
{
	surfaceLightProxyAccum_t accum;
	surfaceLightProxySubdivision_t subdiv;
	qboolean subdivide;
	int x, y;

	if ( !grid || grid->width < 2 || grid->height < 2 ) {
		tr.surfaceLightProxies.skippedInvalid++;
		return qfalse;
	}

	R_ClearSurfaceLightProxyAccum( &accum );

	for ( y = 0; y < grid->height - 1; y++ ) {
		for ( x = 0; x < grid->width - 1; x++ ) {
			const drawVert_t *v00 = &grid->verts[y * grid->width + x];
			const drawVert_t *v10 = &grid->verts[y * grid->width + x + 1];
			const drawVert_t *v01 = &grid->verts[( y + 1 ) * grid->width + x];
			const drawVert_t *v11 = &grid->verts[( y + 1 ) * grid->width + x + 1];
			vec3_t color00;
			vec3_t color10;
			vec3_t color01;
			vec3_t color11;

			R_SurfaceLightDrawVertColor( v00, color00 );
			R_SurfaceLightDrawVertColor( v10, color10 );
			R_SurfaceLightDrawVertColor( v01, color01 );
			R_SurfaceLightDrawVertColor( v11, color11 );
			R_SurfaceLightAccumulateTriangle( v00->xyz, v10->xyz, v11->xyz,
				color00, color10, color11, &accum );
			R_SurfaceLightAccumulateTriangle( v00->xyz, v11->xyz, v01->xyz,
				color00, color11, color01, &accum );
		}
	}

	if ( VectorLengthSquared( accum.normalAccum ) <= 0.0f ) {
		int i;

		for ( i = 0; i < grid->width * grid->height; i++ ) {
			VectorAdd( accum.normalAccum, grid->verts[i].normal, accum.normalAccum );
		}
	}

	subdivide = R_SurfaceLightBeginSubdivision( shader, &accum, &subdiv );
	if ( subdivide ) {
		surfaceLightProxyAccum_t buckets[SURFACELIGHT_PROXY_SUBDIVIDE_MAX_BUCKETS];
		int bucketCount;
		int i;

		for ( i = 0; i < grid->width * grid->height; i++ ) {
			R_SurfaceLightSubdivisionAddPoint( &subdiv, grid->verts[i].xyz );
		}
		subdivide = R_SurfaceLightFinalizeSubdivision( &subdiv );
		if ( subdivide ) {
			for ( i = 0; i < SURFACELIGHT_PROXY_SUBDIVIDE_MAX_BUCKETS; i++ ) {
				R_ClearSurfaceLightProxyAccum( &buckets[i] );
			}
			for ( y = 0; y < grid->height - 1; y++ ) {
				for ( x = 0; x < grid->width - 1; x++ ) {
					const drawVert_t *v00 = &grid->verts[y * grid->width + x];
					const drawVert_t *v10 = &grid->verts[y * grid->width + x + 1];
					const drawVert_t *v01 = &grid->verts[( y + 1 ) * grid->width + x];
					const drawVert_t *v11 = &grid->verts[( y + 1 ) * grid->width + x + 1];
					vec3_t color00;
					vec3_t color10;
					vec3_t color01;
					vec3_t color11;

					R_SurfaceLightDrawVertColor( v00, color00 );
					R_SurfaceLightDrawVertColor( v10, color10 );
					R_SurfaceLightDrawVertColor( v01, color01 );
					R_SurfaceLightDrawVertColor( v11, color11 );
					R_SurfaceLightAccumulateTriangleForSubdivision( v00->xyz, v10->xyz, v11->xyz,
						color00, color10, color11, &subdiv, buckets );
					R_SurfaceLightAccumulateTriangleForSubdivision( v00->xyz, v11->xyz, v01->xyz,
						color00, color11, color01, &subdiv, buckets );
				}
			}
			bucketCount = subdiv.cells[0] * subdiv.cells[1];
			return R_AddSurfaceLightBucketedProxies( surfaceIndex, shader, &accum, buckets, bucketCount );
		}
	}

	return R_AddSurfaceLightProxy( surfaceIndex, shader, &accum );
}

static qboolean R_BuildSurfaceLightTriProxy( int surfaceIndex, const shader_t *shader, const srfTriangles_t *tri )
{
	surfaceLightProxyAccum_t accum;
	surfaceLightProxySubdivision_t subdiv;
	qboolean subdivide;
	int i;

	if ( !tri || tri->numVerts <= 0 || tri->numIndexes < 3 ) {
		tr.surfaceLightProxies.skippedInvalid++;
		return qfalse;
	}

	R_ClearSurfaceLightProxyAccum( &accum );

	for ( i = 0; i + 2 < tri->numIndexes; i += 3 ) {
		int i0 = tri->indexes[i + 0];
		int i1 = tri->indexes[i + 1];
		int i2 = tri->indexes[i + 2];
		vec3_t color0;
		vec3_t color1;
		vec3_t color2;

		if ( i0 < 0 || i0 >= tri->numVerts || i1 < 0 || i1 >= tri->numVerts ||
			i2 < 0 || i2 >= tri->numVerts ) {
			continue;
		}
		R_SurfaceLightDrawVertColor( &tri->verts[i0], color0 );
		R_SurfaceLightDrawVertColor( &tri->verts[i1], color1 );
		R_SurfaceLightDrawVertColor( &tri->verts[i2], color2 );
		R_SurfaceLightAccumulateTriangle( tri->verts[i0].xyz, tri->verts[i1].xyz, tri->verts[i2].xyz,
			color0, color1, color2, &accum );
	}

	if ( VectorLengthSquared( accum.normalAccum ) <= 0.0f ) {
		for ( i = 0; i < tri->numVerts; i++ ) {
			VectorAdd( accum.normalAccum, tri->verts[i].normal, accum.normalAccum );
		}
	}

	subdivide = R_SurfaceLightBeginSubdivision( shader, &accum, &subdiv );
	if ( subdivide ) {
		surfaceLightProxyAccum_t buckets[SURFACELIGHT_PROXY_SUBDIVIDE_MAX_BUCKETS];
		int bucketCount;

		for ( i = 0; i < tri->numVerts; i++ ) {
			R_SurfaceLightSubdivisionAddPoint( &subdiv, tri->verts[i].xyz );
		}
		subdivide = R_SurfaceLightFinalizeSubdivision( &subdiv );
		if ( subdivide ) {
			for ( i = 0; i < SURFACELIGHT_PROXY_SUBDIVIDE_MAX_BUCKETS; i++ ) {
				R_ClearSurfaceLightProxyAccum( &buckets[i] );
			}
			for ( i = 0; i + 2 < tri->numIndexes; i += 3 ) {
				int i0 = tri->indexes[i + 0];
				int i1 = tri->indexes[i + 1];
				int i2 = tri->indexes[i + 2];
				vec3_t color0;
				vec3_t color1;
				vec3_t color2;

				if ( i0 < 0 || i0 >= tri->numVerts || i1 < 0 || i1 >= tri->numVerts ||
					i2 < 0 || i2 >= tri->numVerts ) {
					continue;
				}
				R_SurfaceLightDrawVertColor( &tri->verts[i0], color0 );
				R_SurfaceLightDrawVertColor( &tri->verts[i1], color1 );
				R_SurfaceLightDrawVertColor( &tri->verts[i2], color2 );
				R_SurfaceLightAccumulateTriangleForSubdivision( tri->verts[i0].xyz, tri->verts[i1].xyz,
					tri->verts[i2].xyz, color0, color1, color2, &subdiv, buckets );
			}
			bucketCount = subdiv.cells[0] * subdiv.cells[1];
			return R_AddSurfaceLightBucketedProxies( surfaceIndex, shader, &accum, buckets, bucketCount );
		}
	}

	return R_AddSurfaceLightProxy( surfaceIndex, shader, &accum );
}

static void R_BuildSurfaceLightProxyForSurface( int surfaceIndex, const msurface_t *surf )
{
	const shader_t *shader;
	const surfaceType_t *surfaceType;

	if ( !surf || !surf->shader || !surf->shader->surfaceLightValid ) {
		return;
	}

	shader = surf->shader;
	tr.surfaceLightProxies.sourceSurfaces++;
	if ( shader->isSky || ( shader->surfaceFlags & SURF_SKY ) ) {
		tr.surfaceLightProxies.skippedSky++;
		return;
	}

	surfaceType = surf->data;
	if ( !surfaceType || *surfaceType == SF_SKIP ) {
		tr.surfaceLightProxies.skippedInvalid++;
		return;
	}

	switch ( *surfaceType ) {
	case SF_FACE:
		R_BuildSurfaceLightFaceProxy( surfaceIndex, shader, (const srfSurfaceFace_t *)surfaceType );
		break;
	case SF_GRID:
		R_BuildSurfaceLightGridProxy( surfaceIndex, shader, (const srfGridMesh_t *)surfaceType );
		break;
	case SF_TRIANGLES:
		R_BuildSurfaceLightTriProxy( surfaceIndex, shader, (const srfTriangles_t *)surfaceType );
		break;
	default:
		tr.surfaceLightProxies.skippedInvalid++;
		break;
	}
}

static void R_BuildSurfaceLightProxiesForWorld( void )
{
	int i;

	R_ClearSurfaceLightProxies();
	tr.surfaceLightProxies.built = qtrue;

	if ( !s_worldData.surfaces || s_worldData.numsurfaces <= 0 ) {
		return;
	}

	for ( i = 0; i < s_worldData.numsurfaces; i++ ) {
		R_BuildSurfaceLightProxyForSurface( i, &s_worldData.surfaces[i] );
	}

	if ( r_surfaceLightProxyDebug && r_surfaceLightProxyDebug->integer ) {
		ri.Printf( PRINT_ALL,
			"surfacelight proxies sources:%i built:%i point:%i spot:%i subdiv:%i/%i spatial:%i/%i skip sky:%i invalid:%i overflow:%i\n",
			tr.surfaceLightProxies.sourceSurfaces, tr.surfaceLightProxies.count,
			tr.surfaceLightProxies.pointProjectionCount,
			tr.surfaceLightProxies.spotProjectionCount,
			tr.surfaceLightProxies.subdividedSurfaces,
			tr.surfaceLightProxies.subdivisionProxies,
			tr.surfaceLightProxies.spatialized, tr.surfaceLightProxies.spatialFallback,
			tr.surfaceLightProxies.skippedSky, tr.surfaceLightProxies.skippedInvalid,
			tr.surfaceLightProxies.skippedOverflow );
	}
}

static shader_t *ShaderForShaderNum( const int shaderNum, int lightmapNum ) {
	shader_t	*shader;
	const dshader_t *dsh;

	if ( shaderNum < 0 || shaderNum >= s_worldData.numShaders ) {
		ri.Error( ERR_DROP, "ShaderForShaderNum: bad num %i", shaderNum );
	}

	dsh = &s_worldData.shaders[ shaderNum ];

	if ( ( r_vertexLight->integer && tr.vertexLightingAllowed ) || glConfig.hardwareType == GLHW_PERMEDIA2 ) {
		lightmapNum = LIGHTMAP_BY_VERTEX;
	}

	if ( r_fullbright->integer ) {
		lightmapNum = LIGHTMAP_WHITEIMAGE;
	}

	shader = R_FindShader( dsh->shader, lightmapNum, qtrue );

	// if the shader had errors, just use default shader
	if ( shader->defaultShader ) {
		return tr.defaultShader;
	}

	R_SetWorldSunFromShader( shader );

	if ( r_singleShader->integer && !shader->isSky ) {
		return tr.defaultShader;
	}

	return shader;
}


#ifdef USE_PMLIGHT
static void GenerateNormals( srfSurfaceFace_t *face )
{
	vec3_t ba, ca, cross;
	float *v1, *v2, *v3, *n1, *n2, *n3;
	int i, *indices, i0, i1, i2;

	indices = ((int *)((byte *)face + face->ofsIndices));

	// store as vec4_t so we can simply use memcpy() during tesselation
	face->normals = ri.Hunk_Alloc( face->numPoints * sizeof( tess.normal[0] ), h_low );

	for ( i = 0; i < face->numIndices; i += 3 ) {
		i0 = indices[i+0];
		i1 = indices[i+1];
		i2 = indices[i+2];
		if ( i0 >= face->numPoints || i1 >= face->numPoints || i2 >= face->numPoints )
			continue;
		v1 = face->points[i0];
		v2 = face->points[i1];
		v3 = face->points[i2];
		VectorSubtract( v3, v1, ca );
		VectorSubtract( v2, v1, ba );
		CrossProduct( ca, ba, cross );
		n1 = face->normals + indices[i+0]*4;
		n2 = face->normals + indices[i+1]*4;
		n3 = face->normals + indices[i+2]*4;
		VectorAdd( n1, cross, n1 );
		VectorAdd( n2, cross, n2 );
		VectorAdd( n3, cross, n3 );
	}

	for ( i = 0; i < face->numPoints; i++ ) {
		n1 = face->normals + i*4;
		VectorNormalize2( n1, n1 );
		for ( i0 = 0; i0 < 3; i0++ ) {
			n1[i0] = R_ClampDenorm( n1[i0] );
		}
	}
}
#endif // USE_PMLIGHT


/*
===============
ParseFace
===============
*/
static void ParseFace( const dsurface_t *ds, const drawVert_t *verts, msurface_t *surf, int *indexes ) {
	int			i, j;
	srfSurfaceFace_t	*cv;
	int			numPoints, numIndexes;
	int			lightmapNum;
	float		lightmapX, lightmapY;
	int			sfaceSize, ofsIndexes;
	// get fog volume
	surf->fogIndex = LittleLong( ds->fogNum ) + 1;

	lightmapNum = LittleLong( ds->lightmapNum );
	if ( lightmapNum >= 0 && tr.mergeLightmaps ) {
		lightmapNum = R_GetLightmapCoords( lightmapNum, &lightmapX, &lightmapY );
	} else {
		lightmapX = lightmapY = 0.0f;
	}

	tr.lightmapOffset[0] = lightmapX;
	tr.lightmapOffset[1] = lightmapY;

	// get shader value
	surf->shader = ShaderForShaderNum( LittleLong( ds->shaderNum ), lightmapNum );

	numPoints = LittleLong( ds->numVerts );
	if (numPoints > MAX_FACE_POINTS) {
		ri.Printf( PRINT_WARNING, "WARNING: MAX_FACE_POINTS exceeded: %i\n", numPoints);
		numPoints = MAX_FACE_POINTS;
		surf->shader = tr.defaultShader;
	}

	numIndexes = LittleLong( ds->numIndexes );

	// create the srfSurfaceFace_t
	sfaceSize = sizeof( *cv ) - sizeof( cv->points ) + sizeof( cv->points[0] ) * numPoints;
	ofsIndexes = sfaceSize;
	sfaceSize += sizeof( int ) * numIndexes;

	cv = ri.Hunk_Alloc( sfaceSize, h_low );
	cv->surfaceType = SF_FACE;
	cv->numPoints = numPoints;
	cv->numIndices = numIndexes;
	cv->ofsIndices = ofsIndexes;
#if defined( USE_LEGACY_DLIGHTS ) || defined( USE_PMLIGHT )
	ClearBounds( cv->bounds[0], cv->bounds[1] );
#endif

	verts += LittleLong( ds->firstVert );
	for ( i = 0 ; i < numPoints ; i++ ) {
		for ( j = 0 ; j < 3 ; j++ ) {
			cv->points[i][j] = LittleFloat( verts[i].xyz[j] );
		}
#if defined( USE_LEGACY_DLIGHTS ) || defined( USE_PMLIGHT )
		AddPointToBounds( cv->points[i], cv->bounds[0], cv->bounds[1] );
#endif
		for ( j = 0 ; j < 2 ; j++ ) {
			cv->points[i][3+j] = LittleFloat( verts[i].st[j] );
			cv->points[i][5+j] = LittleFloat( verts[i].lightmap[j] );
		}
		R_ColorShiftLightingBytes( verts[i].color.rgba, (byte *)&cv->points[i][7], qtrue );
		if ( lightmapNum >= 0 && tr.mergeLightmaps ) {
			// adjust lightmap coords
			cv->points[i][5] = cv->points[i][5] * tr.lightmapScale[0] + lightmapX;
			cv->points[i][6] = cv->points[i][6] * tr.lightmapScale[1] + lightmapY;
		}
	}

	indexes += LittleLong( ds->firstIndex );
	for ( i = 0 ; i < numIndexes ; i++ ) {
		((int *)((byte *)cv + cv->ofsIndices ))[i] = LittleLong( indexes[ i ] );
	}

	// take the plane information from the lightmap vector
	for ( i = 0 ; i < 3 ; i++ ) {
		cv->plane.normal[i] = LittleFloat( ds->lightmapVecs[2][i] );
	}

#ifdef USE_PMLIGHT
	if ( surf->shader->numUnfoggedPasses && surf->shader->lightingStage >= 0 ) {
		if ( fabsf( cv->plane.normal[0] ) < 0.01f && fabsf( cv->plane.normal[1] ) < 0.01f && fabsf( cv->plane.normal[2] ) < 0.01f ) {
			// Zero-normals case:
			// might happen if surface contains multiple non-coplanar faces for terrain simulation
			// like in 'Pyramid of the Magician', 'tvy-bench' or 'terrast' maps
			// which results in non-working new per-pixel dynamic lighting.
			// So we will try to regenerate normals and apply smooth shading
			// for normals that is shared between multiple faces.
			// It is not a big problem for incorrectly (negative) generated normals
			// because it is unlikely for shared ones and will result in the same non-working lighting.
			// Also we will NOT update existing face->plane.normal to avoid potential surface culling issues
			GenerateNormals( cv );
		}
	}
#endif

	for ( i = 0; i < 3; i++ ) {
		cv->plane.normal[i] = R_ClampDenorm( cv->plane.normal[i] );
	}

	cv->plane.dist = DotProduct( cv->points[0], cv->plane.normal );
	SetPlaneSignbits( &cv->plane );
	cv->plane.type = PlaneTypeForNormal( cv->plane.normal );

	surf->data = (surfaceType_t *)cv;
}


/*
===============
ParseMesh
===============
*/
static void ParseMesh( const dsurface_t *ds, const drawVert_t *verts, msurface_t *surf ) {
	srfGridMesh_t	*grid;
	int				i, j;
	int				width, height, numPoints;
	drawVert_t points[MAX_PATCH_SIZE*MAX_PATCH_SIZE];
	int				lightmapNum;
	float			lightmapX, lightmapY;
	vec3_t			bounds[2];
	vec3_t			tmpVec;
	static surfaceType_t	skipData = SF_SKIP;

	// get fog volume
	surf->fogIndex = LittleLong( ds->fogNum ) + 1;

	lightmapNum = LittleLong( ds->lightmapNum );
	if ( lightmapNum >= 0 && tr.mergeLightmaps ) {
		lightmapNum = R_GetLightmapCoords( lightmapNum, &lightmapX, &lightmapY );
	} else {
		lightmapX = lightmapY = 0.0f;
	}

	tr.lightmapOffset[0] = lightmapX;
	tr.lightmapOffset[1] = lightmapY;

	// get shader value
	surf->shader = ShaderForShaderNum( LittleLong( ds->shaderNum ), lightmapNum );

	// we may have a nodraw surface, because they might still need to
	// be around for movement clipping
	if ( s_worldData.shaders[ LittleLong( ds->shaderNum ) ].surfaceFlags & SURF_NODRAW ) {
		surf->data = &skipData;
		return;
	}

	width = LittleLong( ds->patchWidth );
	height = LittleLong( ds->patchHeight );

	verts += LittleLong( ds->firstVert );
	numPoints = width * height;
	for ( i = 0 ; i < numPoints ; i++ ) {
		for ( j = 0 ; j < 3 ; j++ ) {
			points[i].xyz[j] = LittleFloat( verts[i].xyz[j] );
			points[i].normal[j] = R_ClampDenorm( LittleFloat( verts[i].normal[j] ) );
		}
		for ( j = 0 ; j < 2 ; j++ ) {
			points[i].st[j] = LittleFloat( verts[i].st[j] );
			points[i].lightmap[j] = LittleFloat( verts[i].lightmap[j] );
		}
		R_ColorShiftLightingBytes( verts[i].color.rgba, points[i].color.rgba, qtrue );
		if ( lightmapNum >= 0 && tr.mergeLightmaps ) {
			// adjust lightmap coords
			points[i].lightmap[0] = points[i].lightmap[0] * tr.lightmapScale[0] + lightmapX;
			points[i].lightmap[1] = points[i].lightmap[1] * tr.lightmapScale[1] + lightmapY;
		}
	}

	// pre-tesseleate
	grid = R_SubdividePatchToGrid( width, height, points );
	surf->data = (surfaceType_t *)grid;

	// copy the level of detail origin, which is the center
	// of the group of all curves that must subdivide the same
	// to avoid cracking
	for ( i = 0 ; i < 3 ; i++ ) {
		bounds[0][i] = LittleFloat( ds->lightmapVecs[0][i] );
		bounds[1][i] = LittleFloat( ds->lightmapVecs[1][i] );
	}
	VectorAdd( bounds[0], bounds[1], bounds[1] );
	VectorScale( bounds[1], 0.5f, grid->lodOrigin );
	VectorSubtract( bounds[0], grid->lodOrigin, tmpVec );
	grid->lodRadius = VectorLength( tmpVec );
}


/*
===============
ParseTriSurf
===============
*/
static void ParseTriSurf( const dsurface_t *ds, const drawVert_t *verts, msurface_t *surf, int *indexes ) {
	srfTriangles_t	*tri;
	int				i, j;
	int				numVerts, numIndexes;
	int				lightmapNum;
	float			lightmapX, lightmapY;

	// get fog volume
	surf->fogIndex = LittleLong( ds->fogNum ) + 1;

	lightmapNum = LittleLong( ds->lightmapNum );
	if ( lightmapNum >= 0 && tr.mergeLightmaps ) {
		lightmapNum = R_GetLightmapCoords( lightmapNum, &lightmapX, &lightmapY );
	} else {
		lightmapX = lightmapY = 0.0f;
	}

	tr.lightmapOffset[0] = lightmapX;
	tr.lightmapOffset[1] = lightmapY;

	// get shader
	surf->shader = ShaderForShaderNum( LittleLong( ds->shaderNum ), LIGHTMAP_BY_VERTEX );

	numVerts = LittleLong( ds->numVerts );
	numIndexes = LittleLong( ds->numIndexes );

	tri = ri.Hunk_Alloc( sizeof( *tri ) + numVerts * sizeof( tri->verts[0] ) 
		+ numIndexes * sizeof( tri->indexes[0] ), h_low );
	tri->surfaceType = SF_TRIANGLES;
	tri->numVerts = numVerts;
	tri->numIndexes = numIndexes;
	tri->verts = (drawVert_t *)(tri + 1);
	tri->indexes = (int *)(tri->verts + tri->numVerts );

	surf->data = (surfaceType_t *)tri;

	// copy vertexes
	ClearBounds( tri->bounds[0], tri->bounds[1] );
	verts += LittleLong( ds->firstVert );
	for ( i = 0 ; i < numVerts ; i++ ) {
		for ( j = 0 ; j < 3 ; j++ ) {
			tri->verts[i].xyz[j] = LittleFloat( verts[i].xyz[j] );
			tri->verts[i].normal[j] = R_ClampDenorm( LittleFloat( verts[i].normal[j] ) );
		}
		AddPointToBounds( tri->verts[i].xyz, tri->bounds[0], tri->bounds[1] );
		for ( j = 0 ; j < 2 ; j++ ) {
			tri->verts[i].st[j] = LittleFloat( verts[i].st[j] );
			tri->verts[i].lightmap[j] = LittleFloat( verts[i].lightmap[j] );
		}

		R_ColorShiftLightingBytes( verts[i].color.rgba, tri->verts[i].color.rgba, qtrue );
		if ( lightmapNum >= 0 && tr.mergeLightmaps ) {
			// adjust lightmap coords
			tri->verts[i].lightmap[0] = tri->verts[i].lightmap[0] * tr.lightmapScale[0] + lightmapX;
			tri->verts[i].lightmap[1] = tri->verts[i].lightmap[1] * tr.lightmapScale[1] + lightmapY;
		}
	}

	// copy indexes
	indexes += LittleLong( ds->firstIndex );
	for ( i = 0 ; i < numIndexes ; i++ ) {
		tri->indexes[i] = LittleLong( indexes[i] );
		if ( tri->indexes[i] < 0 || tri->indexes[i] >= numVerts ) {
			ri.Error( ERR_DROP, "Bad index in triangle surface" );
		}
	}
}


/*
===============
ParseFlare
===============
*/
static void ParseFlare( const dsurface_t *ds, const drawVert_t *verts, msurface_t *surf, int *indexes ) {
	srfFlare_t		*flare;
	int				i;

	// get fog volume
	surf->fogIndex = LittleLong( ds->fogNum ) + 1;

	// get shader
	surf->shader = ShaderForShaderNum( LittleLong( ds->shaderNum ), LIGHTMAP_BY_VERTEX );

	flare = ri.Hunk_Alloc( sizeof( *flare ), h_low );
	flare->surfaceType = SF_FLARE;

	surf->data = (surfaceType_t *)flare;

	for ( i = 0 ; i < 3 ; i++ ) {
		flare->origin[i] = LittleFloat( ds->lightmapOrigin[i] );
		flare->color[i] = LittleFloat( ds->lightmapVecs[0][i] );
		flare->normal[i] = R_ClampDenorm( LittleFloat( ds->lightmapVecs[2][i] ) );
	}
}


/*
=================
R_MergedWidthPoints

returns qtrue if there are grid points merged on a width edge
=================
*/
static qboolean R_MergedWidthPoints( const srfGridMesh_t *grid, int offset ) {
	int i, j;

	for (i = 1; i < grid->width-1; i++) {
		for (j = i + 1; j < grid->width-1; j++) {
			if ( fabs(grid->verts[i + offset].xyz[0] - grid->verts[j + offset].xyz[0]) > .1) continue;
			if ( fabs(grid->verts[i + offset].xyz[1] - grid->verts[j + offset].xyz[1]) > .1) continue;
			if ( fabs(grid->verts[i + offset].xyz[2] - grid->verts[j + offset].xyz[2]) > .1) continue;
			return qtrue;
		}
	}
	return qfalse;
}


/*
=================
R_MergedHeightPoints

returns qtrue if there are grid points merged on a height edge
=================
*/
static qboolean R_MergedHeightPoints( const srfGridMesh_t *grid, int offset ) {
	int i, j;

	for (i = 1; i < grid->height-1; i++) {
		for (j = i + 1; j < grid->height-1; j++) {
			if ( fabs(grid->verts[grid->width * i + offset].xyz[0] - grid->verts[grid->width * j + offset].xyz[0]) > .1) continue;
			if ( fabs(grid->verts[grid->width * i + offset].xyz[1] - grid->verts[grid->width * j + offset].xyz[1]) > .1) continue;
			if ( fabs(grid->verts[grid->width * i + offset].xyz[2] - grid->verts[grid->width * j + offset].xyz[2]) > .1) continue;
			return qtrue;
		}
	}
	return qfalse;
}


/*
=================
R_FixSharedVertexLodError_r

NOTE: never sync LoD through grid edges with merged points!

FIXME: write generalized version that also avoids cracks between a patch and one that meets half way?
=================
*/
static void R_FixSharedVertexLodError_r( int start, srfGridMesh_t *grid1 ) {
	int j, k, l, m, n, offset1, offset2, touch;
	srfGridMesh_t *grid2;

	for ( j = start; j < s_worldData.numsurfaces; j++ ) {
		//
		grid2 = (srfGridMesh_t *) s_worldData.surfaces[j].data;
		// if this surface is not a grid
		if ( grid2->surfaceType != SF_GRID ) continue;
		// if the LOD errors are already fixed for this patch
		if ( grid2->lodFixed == 2 ) continue;
		// grids in the same LOD group should have the exact same lod radius
		if ( grid1->lodRadius != grid2->lodRadius ) continue;
		// grids in the same LOD group should have the exact same lod origin
		if ( grid1->lodOrigin[0] != grid2->lodOrigin[0] ) continue;
		if ( grid1->lodOrigin[1] != grid2->lodOrigin[1] ) continue;
		if ( grid1->lodOrigin[2] != grid2->lodOrigin[2] ) continue;
		//
		touch = qfalse;
		for (n = 0; n < 2; n++) {
			//
			if (n) offset1 = (grid1->height-1) * grid1->width;
			else offset1 = 0;
			if (R_MergedWidthPoints(grid1, offset1)) continue;
			for (k = 1; k < grid1->width-1; k++) {
				for (m = 0; m < 2; m++) {

					if (m) offset2 = (grid2->height-1) * grid2->width;
					else offset2 = 0;
					if (R_MergedWidthPoints(grid2, offset2)) continue;
					for ( l = 1; l < grid2->width-1; l++) {
					//
						if ( fabs(grid1->verts[k + offset1].xyz[0] - grid2->verts[l + offset2].xyz[0]) > .1) continue;
						if ( fabs(grid1->verts[k + offset1].xyz[1] - grid2->verts[l + offset2].xyz[1]) > .1) continue;
						if ( fabs(grid1->verts[k + offset1].xyz[2] - grid2->verts[l + offset2].xyz[2]) > .1) continue;
						// ok the points are equal and should have the same lod error
						grid2->widthLodError[l] = grid1->widthLodError[k];
						touch = qtrue;
					}
				}
				for (m = 0; m < 2; m++) {

					if (m) offset2 = grid2->width-1;
					else offset2 = 0;
					if (R_MergedHeightPoints(grid2, offset2)) continue;
					for ( l = 1; l < grid2->height-1; l++) {
					//
						if ( fabs(grid1->verts[k + offset1].xyz[0] - grid2->verts[grid2->width * l + offset2].xyz[0]) > .1) continue;
						if ( fabs(grid1->verts[k + offset1].xyz[1] - grid2->verts[grid2->width * l + offset2].xyz[1]) > .1) continue;
						if ( fabs(grid1->verts[k + offset1].xyz[2] - grid2->verts[grid2->width * l + offset2].xyz[2]) > .1) continue;
						// ok the points are equal and should have the same lod error
						grid2->heightLodError[l] = grid1->widthLodError[k];
						touch = qtrue;
					}
				}
			}
		}
		for (n = 0; n < 2; n++) {
			//
			if (n) offset1 = grid1->width-1;
			else offset1 = 0;
			if (R_MergedHeightPoints(grid1, offset1)) continue;
			for (k = 1; k < grid1->height-1; k++) {
				for (m = 0; m < 2; m++) {

					if (m) offset2 = (grid2->height-1) * grid2->width;
					else offset2 = 0;
					if (R_MergedWidthPoints(grid2, offset2)) continue;
					for ( l = 1; l < grid2->width-1; l++) {
					//
						if ( fabs(grid1->verts[grid1->width * k + offset1].xyz[0] - grid2->verts[l + offset2].xyz[0]) > .1) continue;
						if ( fabs(grid1->verts[grid1->width * k + offset1].xyz[1] - grid2->verts[l + offset2].xyz[1]) > .1) continue;
						if ( fabs(grid1->verts[grid1->width * k + offset1].xyz[2] - grid2->verts[l + offset2].xyz[2]) > .1) continue;
						// ok the points are equal and should have the same lod error
						grid2->widthLodError[l] = grid1->heightLodError[k];
						touch = qtrue;
					}
				}
				for (m = 0; m < 2; m++) {

					if (m) offset2 = grid2->width-1;
					else offset2 = 0;
					if (R_MergedHeightPoints(grid2, offset2)) continue;
					for ( l = 1; l < grid2->height-1; l++) {
					//
						if ( fabs(grid1->verts[grid1->width * k + offset1].xyz[0] - grid2->verts[grid2->width * l + offset2].xyz[0]) > .1) continue;
						if ( fabs(grid1->verts[grid1->width * k + offset1].xyz[1] - grid2->verts[grid2->width * l + offset2].xyz[1]) > .1) continue;
						if ( fabs(grid1->verts[grid1->width * k + offset1].xyz[2] - grid2->verts[grid2->width * l + offset2].xyz[2]) > .1) continue;
						// ok the points are equal and should have the same lod error
						grid2->heightLodError[l] = grid1->heightLodError[k];
						touch = qtrue;
					}
				}
			}
		}
		if (touch) {
			grid2->lodFixed = 2;
			R_FixSharedVertexLodError_r ( start, grid2 );
			//NOTE: this would be correct but makes things really slow
			//grid2->lodFixed = 1;
		}
	}
}


/*
=================
R_FixSharedVertexLodError

This function assumes that all patches in one group are nicely stitched together for the highest LoD.
If this is not the case this function will still do its job but won't fix the highest LoD cracks.
=================
*/
static void R_FixSharedVertexLodError( void ) {
	int i;
	srfGridMesh_t *grid1;

	for ( i = 0; i < s_worldData.numsurfaces; i++ ) {
		//
		grid1 = (srfGridMesh_t *) s_worldData.surfaces[i].data;
		// if this surface is not a grid
		if ( grid1->surfaceType != SF_GRID )
			continue;
		//
		if ( grid1->lodFixed )
			continue;
		//
		grid1->lodFixed = 2;
		// recursively fix other patches in the same LOD group
		R_FixSharedVertexLodError_r( i + 1, grid1);
	}
}


/*
===============
R_StitchPatches
===============
*/
static int R_StitchPatches( int grid1num, int grid2num ) {
	float *v1, *v2;
	srfGridMesh_t *grid1, *grid2;
	int k, l, m, n, offset1, offset2, row, column;

	grid1 = (srfGridMesh_t *) s_worldData.surfaces[grid1num].data;
	grid2 = (srfGridMesh_t *) s_worldData.surfaces[grid2num].data;
	for (n = 0; n < 2; n++) {
		//
		if (n) offset1 = (grid1->height-1) * grid1->width;
		else offset1 = 0;
		if (R_MergedWidthPoints(grid1, offset1))
			continue;
		for (k = 0; k < grid1->width-2; k += 2) {

			for (m = 0; m < 2; m++) {

				if ( grid2->width >= MAX_GRID_SIZE )
					break;
				if (m) offset2 = (grid2->height-1) * grid2->width;
				else offset2 = 0;
				for ( l = 0; l < grid2->width-1; l++) {
				//
					v1 = grid1->verts[k + offset1].xyz;
					v2 = grid2->verts[l + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;

					v1 = grid1->verts[k + 2 + offset1].xyz;
					v2 = grid2->verts[l + 1 + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;
					//
					v1 = grid2->verts[l + offset2].xyz;
					v2 = grid2->verts[l + 1 + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) < .01 &&
							fabs(v1[1] - v2[1]) < .01 &&
							fabs(v1[2] - v2[2]) < .01)
						continue;
					//
					//ri.Printf( PRINT_ALL, "found highest LoD crack between two patches\n" );
					// insert column into grid2 right after column l
					if (m) row = grid2->height-1;
					else row = 0;
					grid2 = R_GridInsertColumn( grid2, l+1, row,
									grid1->verts[k + 1 + offset1].xyz, grid1->widthLodError[k+1]);
					grid2->lodStitched = qfalse;
					s_worldData.surfaces[grid2num].data = (void *) grid2;
					return qtrue;
				}
			}
			for (m = 0; m < 2; m++) {

				if (grid2->height >= MAX_GRID_SIZE)
					break;
				if (m) offset2 = grid2->width-1;
				else offset2 = 0;
				for ( l = 0; l < grid2->height-1; l++) {
					//
					v1 = grid1->verts[k + offset1].xyz;
					v2 = grid2->verts[grid2->width * l + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;

					v1 = grid1->verts[k + 2 + offset1].xyz;
					v2 = grid2->verts[grid2->width * (l + 1) + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;
					//
					v1 = grid2->verts[grid2->width * l + offset2].xyz;
					v2 = grid2->verts[grid2->width * (l + 1) + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) < .01 &&
							fabs(v1[1] - v2[1]) < .01 &&
							fabs(v1[2] - v2[2]) < .01)
						continue;
					//
					//ri.Printf( PRINT_ALL, "found highest LoD crack between two patches\n" );
					// insert row into grid2 right after row l
					if (m) column = grid2->width-1;
					else column = 0;
					grid2 = R_GridInsertRow( grid2, l+1, column,
										grid1->verts[k + 1 + offset1].xyz, grid1->widthLodError[k+1]);
					grid2->lodStitched = qfalse;
					s_worldData.surfaces[grid2num].data = (void *) grid2;
					return qtrue;
				}
			}
		}
	}
	for (n = 0; n < 2; n++) {
		//
		if (n) offset1 = grid1->width-1;
		else offset1 = 0;
		if (R_MergedHeightPoints(grid1, offset1))
			continue;
		for (k = 0; k < grid1->height-2; k += 2) {
			for (m = 0; m < 2; m++) {

				if ( grid2->width >= MAX_GRID_SIZE )
					break;
				if (m) offset2 = (grid2->height-1) * grid2->width;
				else offset2 = 0;
				for ( l = 0; l < grid2->width-1; l++) {
				//
					v1 = grid1->verts[grid1->width * k + offset1].xyz;
					v2 = grid2->verts[l + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;

					v1 = grid1->verts[grid1->width * (k + 2) + offset1].xyz;
					v2 = grid2->verts[l + 1 + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;
					//
					v1 = grid2->verts[l + offset2].xyz;
					v2 = grid2->verts[(l + 1) + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) < .01 &&
							fabs(v1[1] - v2[1]) < .01 &&
							fabs(v1[2] - v2[2]) < .01)
						continue;
					//
					//ri.Printf( PRINT_ALL, "found highest LoD crack between two patches\n" );
					// insert column into grid2 right after column l
					if (m) row = grid2->height-1;
					else row = 0;
					grid2 = R_GridInsertColumn( grid2, l+1, row,
									grid1->verts[grid1->width * (k + 1) + offset1].xyz, grid1->heightLodError[k+1]);
					grid2->lodStitched = qfalse;
					s_worldData.surfaces[grid2num].data = (void *) grid2;
					return qtrue;
				}
			}
			for (m = 0; m < 2; m++) {

				if (grid2->height >= MAX_GRID_SIZE)
					break;
				if (m) offset2 = grid2->width-1;
				else offset2 = 0;
				for ( l = 0; l < grid2->height-1; l++) {
				//
					v1 = grid1->verts[grid1->width * k + offset1].xyz;
					v2 = grid2->verts[grid2->width * l + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;

					v1 = grid1->verts[grid1->width * (k + 2) + offset1].xyz;
					v2 = grid2->verts[grid2->width * (l + 1) + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;
					//
					v1 = grid2->verts[grid2->width * l + offset2].xyz;
					v2 = grid2->verts[grid2->width * (l + 1) + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) < .01 &&
							fabs(v1[1] - v2[1]) < .01 &&
							fabs(v1[2] - v2[2]) < .01)
						continue;
					//
					//ri.Printf( PRINT_ALL, "found highest LoD crack between two patches\n" );
					// insert row into grid2 right after row l
					if (m) column = grid2->width-1;
					else column = 0;
					grid2 = R_GridInsertRow( grid2, l+1, column,
									grid1->verts[grid1->width * (k + 1) + offset1].xyz, grid1->heightLodError[k+1]);
					grid2->lodStitched = qfalse;
					s_worldData.surfaces[grid2num].data = (void *) grid2;
					return qtrue;
				}
			}
		}
	}
	for (n = 0; n < 2; n++) {
		//
		if (n) offset1 = (grid1->height-1) * grid1->width;
		else offset1 = 0;
		if (R_MergedWidthPoints(grid1, offset1))
			continue;
		for (k = grid1->width-1; k > 1; k -= 2) {

			for (m = 0; m < 2; m++) {

				if ( !grid2 || grid2->width >= MAX_GRID_SIZE )
					break;
				if (m) offset2 = (grid2->height-1) * grid2->width;
				else offset2 = 0;
				for ( l = 0; l < grid2->width-1; l++) {
				//
					v1 = grid1->verts[k + offset1].xyz;
					v2 = grid2->verts[l + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;

					v1 = grid1->verts[k - 2 + offset1].xyz;
					v2 = grid2->verts[l + 1 + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;
					//
					v1 = grid2->verts[l + offset2].xyz;
					v2 = grid2->verts[(l + 1) + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) < .01 &&
							fabs(v1[1] - v2[1]) < .01 &&
							fabs(v1[2] - v2[2]) < .01)
						continue;
					//
					//ri.Printf( PRINT_ALL, "found highest LoD crack between two patches\n" );
					// insert column into grid2 right after column l
					if (m) row = grid2->height-1;
					else row = 0;
					grid2 = R_GridInsertColumn( grid2, l+1, row,
										grid1->verts[k - 1 + offset1].xyz, grid1->widthLodError[k+1]);
					grid2->lodStitched = qfalse;
					s_worldData.surfaces[grid2num].data = (void *) grid2;
					return qtrue;
				}
			}
			for (m = 0; m < 2; m++) {

				if (!grid2 || grid2->height >= MAX_GRID_SIZE)
					break;
				if (m) offset2 = grid2->width-1;
				else offset2 = 0;
				for ( l = 0; l < grid2->height-1; l++) {
				//
					v1 = grid1->verts[k + offset1].xyz;
					v2 = grid2->verts[grid2->width * l + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;

					v1 = grid1->verts[k - 2 + offset1].xyz;
					v2 = grid2->verts[grid2->width * (l + 1) + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;
					//
					v1 = grid2->verts[grid2->width * l + offset2].xyz;
					v2 = grid2->verts[grid2->width * (l + 1) + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) < .01 &&
							fabs(v1[1] - v2[1]) < .01 &&
							fabs(v1[2] - v2[2]) < .01)
						continue;
					//
					//ri.Printf( PRINT_ALL, "found highest LoD crack between two patches\n" );
					// insert row into grid2 right after row l
					if (m) column = grid2->width-1;
					else column = 0;
					grid2 = R_GridInsertRow( grid2, l+1, column,
										grid1->verts[k - 1 + offset1].xyz, grid1->widthLodError[k+1]);
					if (!grid2)
						break;
					grid2->lodStitched = qfalse;
					s_worldData.surfaces[grid2num].data = (void *) grid2;
					return qtrue;
				}
			}
		}
	}
	for (n = 0; n < 2; n++) {
		//
		if (n) offset1 = grid1->width-1;
		else offset1 = 0;
		if (R_MergedHeightPoints(grid1, offset1))
			continue;
		for (k = grid1->height-1; k > 1; k -= 2) {
			for (m = 0; m < 2; m++) {

				if ( !grid2 || grid2->width >= MAX_GRID_SIZE )
					break;
				if (m) offset2 = (grid2->height-1) * grid2->width;
				else offset2 = 0;
				for ( l = 0; l < grid2->width-1; l++) {
				//
					v1 = grid1->verts[grid1->width * k + offset1].xyz;
					v2 = grid2->verts[l + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;

					v1 = grid1->verts[grid1->width * (k - 2) + offset1].xyz;
					v2 = grid2->verts[l + 1 + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;
					//
					v1 = grid2->verts[l + offset2].xyz;
					v2 = grid2->verts[(l + 1) + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) < .01 &&
							fabs(v1[1] - v2[1]) < .01 &&
							fabs(v1[2] - v2[2]) < .01)
						continue;
					//
					//ri.Printf( PRINT_ALL, "found highest LoD crack between two patches\n" );
					// insert column into grid2 right after column l
					if (m) row = grid2->height-1;
					else row = 0;
					grid2 = R_GridInsertColumn( grid2, l+1, row,
										grid1->verts[grid1->width * (k - 1) + offset1].xyz, grid1->heightLodError[k+1]);
					grid2->lodStitched = qfalse;
					s_worldData.surfaces[grid2num].data = (void *) grid2;
					return qtrue;
				}
			}
			for (m = 0; m < 2; m++) {

				if (!grid2 || grid2->height >= MAX_GRID_SIZE)
					break;
				if (m) offset2 = grid2->width-1;
				else offset2 = 0;
				for ( l = 0; l < grid2->height-1; l++) {
				//
					v1 = grid1->verts[grid1->width * k + offset1].xyz;
					v2 = grid2->verts[grid2->width * l + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;

					v1 = grid1->verts[grid1->width * (k - 2) + offset1].xyz;
					v2 = grid2->verts[grid2->width * (l + 1) + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) > .1)
						continue;
					if ( fabs(v1[1] - v2[1]) > .1)
						continue;
					if ( fabs(v1[2] - v2[2]) > .1)
						continue;
					//
					v1 = grid2->verts[grid2->width * l + offset2].xyz;
					v2 = grid2->verts[grid2->width * (l + 1) + offset2].xyz;
					if ( fabs(v1[0] - v2[0]) < .01 &&
							fabs(v1[1] - v2[1]) < .01 &&
							fabs(v1[2] - v2[2]) < .01)
						continue;
					//
					//ri.Printf( PRINT_ALL, "found highest LoD crack between two patches\n" );
					// insert row into grid2 right after row l
					if (m) column = grid2->width-1;
					else column = 0;
					grid2 = R_GridInsertRow( grid2, l+1, column,
										grid1->verts[grid1->width * (k - 1) + offset1].xyz, grid1->heightLodError[k+1]);
					grid2->lodStitched = qfalse;
					s_worldData.surfaces[grid2num].data = (void *) grid2;
					return qtrue;
				}
			}
		}
	}
	return qfalse;
}


/*
===============
R_TryStitchPatch

This function will try to stitch patches in the same LoD group together for the highest LoD.

Only single missing vertex cracks will be fixed.

Vertices will be joined at the patch side a crack is first found, at the other side
of the patch (on the same row or column) the vertices will not be joined and cracks
might still appear at that side.
===============
*/
static int R_TryStitchingPatch( int grid1num ) {
	int j, numstitches;
	srfGridMesh_t *grid1, *grid2;

	numstitches = 0;
	grid1 = (srfGridMesh_t *) s_worldData.surfaces[grid1num].data;
	for ( j = 0; j < s_worldData.numsurfaces; j++ ) {
		//
		grid2 = (srfGridMesh_t *) s_worldData.surfaces[j].data;
		// if this surface is not a grid
		if ( grid2->surfaceType != SF_GRID ) continue;
		// grids in the same LOD group should have the exact same lod radius
		if ( grid1->lodRadius != grid2->lodRadius ) continue;
		// grids in the same LOD group should have the exact same lod origin
		if ( grid1->lodOrigin[0] != grid2->lodOrigin[0] ) continue;
		if ( grid1->lodOrigin[1] != grid2->lodOrigin[1] ) continue;
		if ( grid1->lodOrigin[2] != grid2->lodOrigin[2] ) continue;
		//
		while (R_StitchPatches(grid1num, j))
		{
			numstitches++;
		}
	}
	return numstitches;
}


/*
===============
R_StitchAllPatches
===============
*/
static void R_StitchAllPatches( void ) {
	int i, stitched, numstitches;
	srfGridMesh_t *grid1;

	numstitches = 0;
	do
	{
		stitched = qfalse;
		for ( i = 0; i < s_worldData.numsurfaces; i++ ) {
			//
			grid1 = (srfGridMesh_t *) s_worldData.surfaces[i].data;
			// if this surface is not a grid
			if ( grid1->surfaceType != SF_GRID )
				continue;
			//
			if ( grid1->lodStitched )
				continue;
			//
			grid1->lodStitched = qtrue;
			stitched = qtrue;
			//
			numstitches += R_TryStitchingPatch( i );
		}
	}
	while (stitched);
	ri.Printf( PRINT_ALL, "stitched %d LoD cracks\n", numstitches );
}


/*
===============
R_MovePatchSurfacesToHunk
===============
*/
static void R_MovePatchSurfacesToHunk( void ) {
	int i, j, k, n, size;
	srfGridMesh_t *grid, *hunkgrid;

	for ( i = 0; i < s_worldData.numsurfaces; i++ ) {
		//
		grid = (srfGridMesh_t *) s_worldData.surfaces[i].data;
		// if this surface is not a grid
		if ( grid->surfaceType != SF_GRID )
			continue;
		//
		n = grid->width * grid->height - 1;
		size = n * sizeof( drawVert_t ) + sizeof( *grid );

		for (j = 0; j < n; j++) {
			for (k = 0; k < 3; k++) {
				grid->verts[j].normal[k] = R_ClampDenorm( grid->verts[j].normal[k] );
			}
		}

		hunkgrid = ri.Hunk_Alloc( size, h_low );
		Com_Memcpy(hunkgrid, grid, size);

		hunkgrid->widthLodError = ri.Hunk_Alloc( grid->width * 4, h_low );
		Com_Memcpy( hunkgrid->widthLodError, grid->widthLodError, grid->width * 4 );

		hunkgrid->heightLodError = ri.Hunk_Alloc( grid->height * 4, h_low );
		Com_Memcpy( hunkgrid->heightLodError, grid->heightLodError, grid->height * 4 );

		R_FreeSurfaceGridMesh( grid );

		s_worldData.surfaces[i].data = (void *) hunkgrid;
	}
}


/*
===============
R_LoadSurfaces
===============
*/
static void R_LoadSurfaces( const lump_t *surfs, const lump_t *verts, const lump_t *indexLump ) {
	const dsurface_t *in;
	msurface_t	*out;
	const drawVert_t *dv;
	int			*indexes;
	int			count;
	int			numFaces, numMeshes, numTriSurfs, numFlares;
	int			i;

	numFaces = 0;
	numMeshes = 0;
	numTriSurfs = 0;
	numFlares = 0;

	in = (void *)(fileBase + surfs->fileofs);
	if (surfs->filelen % sizeof(*in))
		ri.Error( ERR_DROP, "%s(): funny lump size in %s", __func__, s_worldData.name );
	count = surfs->filelen / sizeof(*in);

	dv = (void *)(fileBase + verts->fileofs);
	if (verts->filelen % sizeof(*dv))
		ri.Error( ERR_DROP, "%s(): funny lump size in %s", __func__, s_worldData.name );

	indexes = (void *)(fileBase + indexLump->fileofs);
	if ( indexLump->filelen % sizeof(*indexes))
		ri.Error( ERR_DROP, "%s(): funny lump size in %s", __func__, s_worldData.name );

	out = ri.Hunk_Alloc( count * sizeof(*out), h_low );

	s_worldData.surfaces = out;
	s_worldData.numsurfaces = count;

	for ( i = 0 ; i < count ; i++, in++, out++ ) {
		switch ( LittleLong( in->surfaceType ) ) {
		case MST_PATCH:
			ParseMesh( in, dv, out );
			numMeshes++;
			break;
		case MST_TRIANGLE_SOUP:
			ParseTriSurf( in, dv, out, indexes );
			numTriSurfs++;
			break;
		case MST_PLANAR:
			ParseFace( in, dv, out, indexes );
			numFaces++;
			break;
		case MST_FLARE:
			ParseFlare( in, dv, out, indexes );
			numFlares++;
			break;
		default:
			ri.Error( ERR_DROP, "Bad surfaceType %i", LittleLong( in->surfaceType ) );
		}
	}

#ifdef PATCH_STITCHING
	R_StitchAllPatches();
#endif

	R_FixSharedVertexLodError();

#ifdef PATCH_STITCHING
	R_MovePatchSurfacesToHunk();
#endif

	ri.Printf( PRINT_ALL, "...loaded %d faces, %i meshes, %i trisurfs, %i flares\n", 
		numFaces, numMeshes, numTriSurfs, numFlares );
}


/*
=================
R_LoadSubmodels
=================
*/
static void R_LoadSubmodels( const lump_t *l ) {
	const dmodel_t *in;
	bmodel_t	*out;
	int			i, j, count;

	in = (void *)(fileBase + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Error( ERR_DROP, "%s(): funny lump size in %s", __func__, s_worldData.name );
	count = l->filelen / sizeof(*in);

	s_worldData.numBmodels = count;
	s_worldData.bmodels = out = ri.Hunk_Alloc( count * sizeof(*out), h_low );

	for ( i=0 ; i<count ; i++, in++, out++ ) {
		model_t *model;

		model = R_AllocModel();

		if ( model == NULL ) {
			ri.Error( ERR_DROP, "R_LoadSubmodels: R_AllocModel() failed" );
		}

		model->type = MOD_BRUSH;
		model->bmodel = out;
		Com_sprintf( model->name, sizeof( model->name ), "*%d", i );

		for (j=0 ; j<3 ; j++) {
			out->bounds[0][j] = LittleFloat (in->mins[j]);
			out->bounds[1][j] = LittleFloat (in->maxs[j]);
		}

		out->firstSurface = s_worldData.surfaces + LittleLong( in->firstSurface );
		out->numSurfaces = LittleLong( in->numSurfaces );
	}
}

/*
=================
R_LoadAdvertisements
=================
*/
static void R_LoadAdvertisements( const lump_t *l ) {
	const dAdvertisement_t	*in;
	qlAdvertisement_t		*out;
	bmodel_t				*bmodel;
	int						count;
	int						i;
	int						j;
	int						modelNum;

	s_worldData.numAdvertisements = 0;
	s_worldData.advertisements = NULL;

	if ( !l->filelen ) {
		return;
	}

	if ( l->filelen % sizeof( *in ) ) {
		ri.Error( ERR_DROP, "R_LoadAdvertisements: funny lump size\n" );
	}

	count = l->filelen / sizeof( *in );
	if ( count >= MAX_MAP_ADVERTISEMENTS ) {
		ri.Error( ERR_DROP, "R_LoadAdvertisements: number of advertisements exceeds level limit.\n" );
	}

	in = (const dAdvertisement_t *)( fileBase + l->fileofs );
	out = ri.Hunk_Alloc( count * sizeof( *out ), h_low );
	s_worldData.advertisements = out;

	for ( i = 0; i < count; i++, in++ ) {
		modelNum = in->model[0] == '*' ? atoi( in->model + 1 ) : -1;
		if ( modelNum < 0 || modelNum >= s_worldData.numBmodels ) {
			bmodel = NULL;
		} else {
			bmodel = &s_worldData.bmodels[modelNum];
		}

		if ( !bmodel ) {
			ri.Printf( PRINT_DEVELOPER,
				"cell ID %d has no brush model. It has been ignored.\n",
				LittleLong( in->cellId ) );
			continue;
		}

		if ( bmodel->numSurfaces > 1 ) {
			ri.Printf( PRINT_DEVELOPER,
				"cell ID %d has multiple surfaces. It has been ignored.\n",
				LittleLong( in->cellId ) );
			continue;
		}

		out[s_worldData.numAdvertisements].cellId = LittleLong( in->cellId );
		out[s_worldData.numAdvertisements].bmodel = bmodel;
		VectorAdd( bmodel->bounds[0], bmodel->bounds[1], out[s_worldData.numAdvertisements].center );
		VectorScale( out[s_worldData.numAdvertisements].center, 0.5f, out[s_worldData.numAdvertisements].center );
		for ( j = 0; j < 3; j++ ) {
			out[s_worldData.numAdvertisements].normal[j] = LittleFloat( in->normal[j] );
		}
		for ( j = 0; j < 4; j++ ) {
			out[s_worldData.numAdvertisements].points[j][0] = LittleFloat( in->points[j][0] );
			out[s_worldData.numAdvertisements].points[j][1] = LittleFloat( in->points[j][1] );
			out[s_worldData.numAdvertisements].points[j][2] = LittleFloat( in->points[j][2] );
		}
		out[s_worldData.numAdvertisements].cullState = CULL_OUT;
		out[s_worldData.numAdvertisements].occlusionQueryIds[0] = 0;
		out[s_worldData.numAdvertisements].occlusionQueryIds[1] = 0;
		if ( qglGenQueriesARB ) {
			qglGenQueriesARB( 2, out[s_worldData.numAdvertisements].occlusionQueryIds );
		}
		out[s_worldData.numAdvertisements].queryListIndex = -1;
		out[s_worldData.numAdvertisements].viewArea = 0;
		out[s_worldData.numAdvertisements].projectedNormalX = 0.0f;
		out[s_worldData.numAdvertisements].projectedNormalY = 0.0f;
		out[s_worldData.numAdvertisements].sourceIndex = i;

		s_worldData.numAdvertisements++;
	}
}



//==================================================================

/*
=================
R_SetParent
=================
*/
static void R_SetParent( mnode_t *node, mnode_t *parent )
{
	node->parent = parent;
	if ( node->contents != CONTENTS_NODE )
		return;
	R_SetParent( node->children[0], node );
	R_SetParent( node->children[1], node );
}


/*
=================
R_LoadNodesAndLeafs
=================
*/
static void R_LoadNodesAndLeafs( const lump_t *nodeLump, const lump_t *leafLump ) {
	int			i, j, p;
	const dnode_t		*in;
	dleaf_t		*inLeaf;
	mnode_t 	*out;
	int			numNodes, numLeafs;

	in = (void *)(fileBase + nodeLump->fileofs);
	if (nodeLump->filelen % sizeof(dnode_t) ||
		leafLump->filelen % sizeof(dleaf_t) ) {
		ri.Error( ERR_DROP, "%s(): funny lump size in %s", __func__, s_worldData.name );
	}
	numNodes = nodeLump->filelen / sizeof(dnode_t);
	numLeafs = leafLump->filelen / sizeof(dleaf_t);

	out = ri.Hunk_Alloc ( (numNodes + numLeafs) * sizeof(*out), h_low);	

	s_worldData.nodes = out;
	s_worldData.numnodes = numNodes + numLeafs;
	s_worldData.numDecisionNodes = numNodes;

	// load nodes
	for ( i=0 ; i<numNodes; i++, in++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->mins[j] = LittleLong (in->mins[j]);
			out->maxs[j] = LittleLong (in->maxs[j]);
		}
	
		p = LittleLong(in->planeNum);
		out->plane = s_worldData.planes + p;

		out->contents = CONTENTS_NODE;	// differentiate from leafs

		for (j=0 ; j<2 ; j++)
		{
			p = LittleLong (in->children[j]);
			if (p >= 0)
				out->children[j] = s_worldData.nodes + p;
			else
				out->children[j] = s_worldData.nodes + numNodes + (-1 - p);
		}
	}
	
	// load leafs
	inLeaf = (void *)(fileBase + leafLump->fileofs);
	for ( i=0 ; i<numLeafs ; i++, inLeaf++, out++)
	{
		for (j=0 ; j<3 ; j++)
		{
			out->mins[j] = LittleLong (inLeaf->mins[j]);
			out->maxs[j] = LittleLong (inLeaf->maxs[j]);
		}

		out->cluster = LittleLong(inLeaf->cluster);
		out->area = LittleLong(inLeaf->area);

		if ( out->cluster >= s_worldData.numClusters ) {
			s_worldData.numClusters = out->cluster + 1;
		}

		out->firstmarksurface = s_worldData.marksurfaces +
			LittleLong(inLeaf->firstLeafSurface);
		out->nummarksurfaces = LittleLong(inLeaf->numLeafSurfaces);
	}	

	// chain descendants
	R_SetParent (s_worldData.nodes, NULL);
}

//=============================================================================


/*
=================
R_ReplaceShaders

replaces some buggy map shaders
=================
*/
static void R_ReplaceMapShaders( dshader_t *out, int count ) 
{
	if ( Q_stricmp( s_worldData.baseName, "mapel4b" ) == 0 && count == 86 ) {
		if ( crc32_buffer( (const byte*)out, count*sizeof(*out) ) == 0x1593623C ) {
			if ( strcmp( out[72].shader, "textures/mapel4/crate1_top3" ) == 0 ) {
				strcpy( out[72].shader, "textures/mapel4/crate1_top2" );
			}
		}
	}
}


/*
=================
R_LoadShaders
=================
*/
static void R_LoadShaders( const lump_t *l ) {
	int		i, count;
	dshader_t	*in, *out;
	
	in = (void *)(fileBase + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Error( ERR_DROP, "%s(): funny lump size in %s", __func__, s_worldData.name );
	count = l->filelen / sizeof(*in);
	out = ri.Hunk_Alloc ( count*sizeof(*out), h_low );

	s_worldData.shaders = out;
	s_worldData.numShaders = count;

	Com_Memcpy( out, in, count*sizeof(*out) );

	R_ReplaceMapShaders( out, count );

	for ( i=0 ; i<count ; i++ ) {
		out[i].surfaceFlags = LittleLong( out[i].surfaceFlags );
		out[i].contentFlags = LittleLong( out[i].contentFlags );
	}
}


/*
=================
R_LoadMarksurfaces
=================
*/
static void R_LoadMarksurfaces( const lump_t *l )
{	
	int		i, j, count;
	int		*in;
	msurface_t **out;
	
	in = (void *)(fileBase + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Error( ERR_DROP, "%s(): funny lump size in %s", __func__, s_worldData.name );
	count = l->filelen / sizeof(*in);
	out = ri.Hunk_Alloc ( count*sizeof(*out), h_low);

	s_worldData.marksurfaces = out;
	s_worldData.nummarksurfaces = count;

	for ( i=0 ; i<count ; i++)
	{
		j = LittleLong(in[i]);
		out[i] = s_worldData.surfaces + j;
	}
}


/*
=================
R_LoadPlanes
=================
*/
static	void R_LoadPlanes( const lump_t *l ) {
	int			i, j;
	cplane_t	*out;
	const dplane_t 	*in;
	int			count;
	int			bits;

	in = (void *)(fileBase + l->fileofs);
	if (l->filelen % sizeof(*in))
		ri.Error( ERR_DROP, "%s(): funny lump size in %s", __func__, s_worldData.name );
	count = l->filelen / sizeof(*in);
	out = ri.Hunk_Alloc( count*2*sizeof(*out), h_low );

	s_worldData.planes = out;
	s_worldData.numplanes = count;

	for ( i=0 ; i<count ; i++, in++, out++) {
		bits = 0;
		for (j=0 ; j<3 ; j++) {
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0) {
				bits |= 1<<j;
			}
		}

		out->dist = LittleFloat (in->dist);
		out->type = PlaneTypeForNormal( out->normal );
		out->signbits = bits;
	}
}


/*
=================
R_LoadFogs
=================
*/
static void R_LoadFogs( const lump_t *l, const lump_t *brushesLump, const lump_t *sidesLump ) {
	int			i, n;
	fog_t		*out;
	const dfog_t		*fogs;
	const dbrush_t 	*brushes, *brush;
	const dbrushside_t	*sides;
	int			count, brushesCount, sidesCount;
	int			sideNum;
	int			planeNum;
	shader_t	*shader;
	float		d;
	int			firstSide;
	vec3_t		fogColor;

	fogs = (void *)(fileBase + l->fileofs);
	if (l->filelen % sizeof(*fogs)) {
		ri.Error( ERR_DROP, "%s(): funny lump size in %s", __func__, s_worldData.name );
	}
	count = l->filelen / sizeof(*fogs);

	// create fog structures for them
	s_worldData.numfogs = count + 1;
	s_worldData.fogs = ri.Hunk_Alloc( s_worldData.numfogs*sizeof(*out), h_low);
	out = s_worldData.fogs + 1;

	if ( !count ) {
		return;
	}

	brushes = (void *)(fileBase + brushesLump->fileofs);
	if (brushesLump->filelen % sizeof(*brushes)) {
		ri.Error( ERR_DROP, "%s(): funny lump size in %s", __func__, s_worldData.name );
	}
	brushesCount = brushesLump->filelen / sizeof(*brushes);

	sides = (void *)(fileBase + sidesLump->fileofs);
	if (sidesLump->filelen % sizeof(*sides)) {
		ri.Error( ERR_DROP, "%s(): funny lump size in %s", __func__, s_worldData.name );
	}
	sidesCount = sidesLump->filelen / sizeof(*sides);

	for ( i=0 ; i<count ; i++, fogs++) {
		out->originalBrushNumber = LittleLong( fogs->brushNum );

		if ( (unsigned)out->originalBrushNumber >= brushesCount ) {
			ri.Error( ERR_DROP, "fog brushNumber out of range" );
		}
		brush = brushes + out->originalBrushNumber;

		firstSide = LittleLong( brush->firstSide );

		if ( (unsigned)firstSide > sidesCount - 6 ) {
			ri.Error( ERR_DROP, "fog brush sideNumber out of range" );
		}

		// brushes are always sorted with the axial sides first
		sideNum = firstSide + 0;
		planeNum = LittleLong( sides[ sideNum ].planeNum );
		out->bounds[0][0] = -s_worldData.planes[ planeNum ].dist;

		sideNum = firstSide + 1;
		planeNum = LittleLong( sides[ sideNum ].planeNum );
		out->bounds[1][0] = s_worldData.planes[ planeNum ].dist;

		sideNum = firstSide + 2;
		planeNum = LittleLong( sides[ sideNum ].planeNum );
		out->bounds[0][1] = -s_worldData.planes[ planeNum ].dist;

		sideNum = firstSide + 3;
		planeNum = LittleLong( sides[ sideNum ].planeNum );
		out->bounds[1][1] = s_worldData.planes[ planeNum ].dist;

		sideNum = firstSide + 4;
		planeNum = LittleLong( sides[ sideNum ].planeNum );
		out->bounds[0][2] = -s_worldData.planes[ planeNum ].dist;

		sideNum = firstSide + 5;
		planeNum = LittleLong( sides[ sideNum ].planeNum );
		out->bounds[1][2] = s_worldData.planes[ planeNum ].dist;

		// get information from the shader for fog parameters
		shader = R_FindShader( fogs->shader, LIGHTMAP_NONE, qtrue );
	
		VectorCopy( shader->fogParms.color, fogColor );

		if ( r_mapGreyScale->value > 0 ) {
			float luminance;
			luminance = LUMA( fogColor[0], fogColor[1], fogColor[2] );
			fogColor[0] = LERP( fogColor[0], luminance, r_mapGreyScale->value );
			fogColor[1] = LERP( fogColor[1], luminance, r_mapGreyScale->value );
			fogColor[2] = LERP( fogColor[2], luminance, r_mapGreyScale->value );
		}

		out->parms = shader->fogParms;

		out->colorInt.rgba[0] = ( fogColor[0] * tr.identityLight ) * 255.0f;
		out->colorInt.rgba[1] = ( fogColor[1] * tr.identityLight ) * 255.0f;
		out->colorInt.rgba[2] = ( fogColor[2] * tr.identityLight ) * 255.0f;
		out->colorInt.rgba[3] = 255;

		for ( n = 0; n < 4; n++ )
			out->color[ n ] = (float) out->colorInt.rgba[ n ] / 255.0f;

		d = shader->fogParms.depthForOpaque < 1 ? 1 : shader->fogParms.depthForOpaque;
		out->tcScale = 1.0f / ( d * 8 );

		// set the gradient vector
		sideNum = LittleLong( fogs->visibleSide );

		if ( sideNum == -1 ) {
			out->hasSurface = qfalse;
		} else {
			int sideOffset = firstSide + sideNum;
			if ( (unsigned)sideOffset >= sidesCount ) {
				ri.Printf( PRINT_WARNING, "bad fog side offset %i\n", sideOffset );
				out->hasSurface = qfalse;
			} else {
				out->hasSurface = qtrue;
				planeNum = LittleLong( sides[ sideOffset ].planeNum );
				VectorSubtract( vec3_origin, s_worldData.planes[ planeNum ].normal, out->surface );
				out->surface[3] = -s_worldData.planes[ planeNum ].dist;
			}
		}

		out++;
	}

}


/*
================
R_LoadLightGrid
================
*/
static void R_LoadLightGrid( const lump_t *l ) {
	int		i;
	vec3_t	maxs;
	int		numGridPoints;
	world_t	*w;
	float	*wMins, *wMaxs;

	w = &s_worldData;

	w->lightGridInverseSize[0] = 1.0f / w->lightGridSize[0];
	w->lightGridInverseSize[1] = 1.0f / w->lightGridSize[1];
	w->lightGridInverseSize[2] = 1.0f / w->lightGridSize[2];

	wMins = w->bmodels[0].bounds[0];
	wMaxs = w->bmodels[0].bounds[1];

	for ( i = 0 ; i < 3 ; i++ ) {
		w->lightGridOrigin[i] = w->lightGridSize[i] * ceil( wMins[i] / w->lightGridSize[i] );
		maxs[i] = w->lightGridSize[i] * floor( wMaxs[i] / w->lightGridSize[i] );
		w->lightGridBounds[i] = (maxs[i] - w->lightGridOrigin[i])/w->lightGridSize[i] + 1;
	}

	numGridPoints = w->lightGridBounds[0] * w->lightGridBounds[1] * w->lightGridBounds[2];

	if ( l->filelen != numGridPoints * 8 ) {
		ri.Printf( PRINT_WARNING, "WARNING: light grid mismatch\n" );
		w->lightGridData = NULL;
		return;
	}

	w->lightGridData = ri.Hunk_Alloc( l->filelen, h_low );
	Com_Memcpy( w->lightGridData, (void *)(fileBase + l->fileofs), l->filelen );

	// deal with overbright bits
	for ( i = 0 ; i < numGridPoints ; i++ ) {
		R_ColorShiftLightingBytes( &w->lightGridData[i*8], &w->lightGridData[i*8], qfalse );
		R_ColorShiftLightingBytes( &w->lightGridData[i*8+3], &w->lightGridData[i*8+3], qfalse );
	}
}


/*
================
R_LoadEntities
================
*/
static void R_LoadEntities( const lump_t *l ) {
	const char *p, *token, *s;
	char keyname[MAX_TOKEN_CHARS];
	char value[MAX_TOKEN_CHARS], *v[3];
	world_t	*w;

	w = &s_worldData;
	w->lightGridSize[0] = 64;
	w->lightGridSize[1] = 64;
	w->lightGridSize[2] = 128;

	p = (const char *)(fileBase + l->fileofs);

	// store for reference by the cgame
	w->entityString = ri.Hunk_Alloc( l->filelen + 1, h_low );
	strcpy( w->entityString, p );
	w->entityParsePoint = w->entityString;

	token = COM_ParseExt( &p, qtrue );
	if (*token != '{') {
		return;
	}

	// only parse the world spawn
	while ( 1 ) {	
		// parse key
		token = COM_ParseExt( &p, qtrue );

		if ( !*token || *token == '}' ) {
			break;
		}
		Q_strncpyz(keyname, token, sizeof(keyname));

		// parse value
		token = COM_ParseExt( &p, qtrue );

		if ( !*token || *token == '}' ) {
			break;
		}
		Q_strncpyz(value, token, sizeof(value));

		// check for remapping of shaders for vertex lighting
		s = "vertexremapshader";
		if (!Q_strncmp(keyname, s, strlen(s)) ) {
			char *vs = strchr(value, ';');
			if (!vs) {
				ri.Printf( PRINT_WARNING, "WARNING: no semi colon in vertexshaderremap '%s'\n", value );
				break;
			}
			*vs++ = '\0';
			if ( r_vertexLight->integer && tr.vertexLightingAllowed ) {
				RE_RemapShader(value, s, "0");
			}
			continue;
		}
		// check for remapping of shaders
		s = "remapshader";
		if (!Q_strncmp(keyname, s, (int)strlen(s)) ) {
			char *vs = strchr(value, ';');
			if (!vs) {
				ri.Printf( PRINT_WARNING, "WARNING: no semi colon in shaderremap '%s'\n", value );
				break;
			}
			*vs++ = '\0';
			RE_RemapShader(value, s, "0");
			continue;
		}
		// check for a different grid size
		if (!Q_stricmp(keyname, "gridsize")) {
			//sscanf(value, "%f %f %f", &w->lightGridSize[0], &w->lightGridSize[1], &w->lightGridSize[2] );
			Com_Split( value, v, 3, ' ' );
			w->lightGridSize[0] = Q_atof( v[0] );
			w->lightGridSize[1] = Q_atof( v[1] );
			w->lightGridSize[2] = Q_atof( v[2] );
			continue;
		}
	}
}


/*
=================
RE_GetEntityToken
=================
*/
qboolean RE_GetEntityToken( char *buffer, int size ) {
	const char	*s;

	s = COM_Parse( &s_worldData.entityParsePoint );
	Q_strncpyz( buffer, s, size );
	if ( !s_worldData.entityParsePoint && !s[0] ) {
		s_worldData.entityParsePoint = s_worldData.entityString;
		return qfalse;
	} else {
		return qtrue;
	}
}


/*
=================
RE_LoadWorldMap

Called directly from cgame
=================
*/
void RE_LoadWorldMap( const char *name ) {
	int			i;
	int32_t		size;
	dheader_t	*header;
	bsp43_translation_t translated = { 0 };
	qboolean	translatedMap = qfalse;
	union {
		byte *b;
		void *v;
	} buffer;
	byte		*startMarker;
	lump_t		qlAdvertisementsLump = { 0, 0 };

	if ( tr.worldMapLoaded ) {
		ri.Error( ERR_DROP, "ERROR: attempted to redundantly load world map" );
	}

	// set default sun direction to be used if it isn't overridden by a shader
	R_ClearWorldSun();
	R_ClearStaticMapLights();
	R_ClearSurfaceLightProxies();

	tr.worldMapLoaded = qtrue;

	// load it
	size = ri.FS_ReadFile( name, &buffer.v );
	if ( !buffer.b ) {
		ri.Error( ERR_DROP, "%s: couldn't load %s", __func__, name );
	}
	if ( size < sizeof( dheader_t ) ) {
		ri.Error( ERR_DROP, "%s: %s has truncated header", __func__, name );
	}
	if ( size >= (int)sizeof( dheader43_t ) ) {
		const dheader43_t *rawHeader = (const dheader43_t *)buffer.b;

		if ( LittleLong( rawHeader->ident ) == BSP_IDENT && LittleLong( rawHeader->version ) == BSP_VERSION_IHV ) {
			char translateError[128];

			if ( !BSP43_TranslateToV46( buffer.b, size, ri.Malloc, ri.Free, &translated, translateError, sizeof( translateError ) ) ) {
				ri.Error( ERR_DROP, "%s: %s could not translate IBSP v43: %s", __func__, name, translateError );
			}

			ri.FS_FreeFile( buffer.v );
			buffer.v = translated.data;
			size = translated.length;
			translatedMap = qtrue;
		}
	}

	tr.mapLoading = qtrue;

	// clear tr.world so if the level fails to load, the next
	// try will not look at the partially loaded version
	R_ShutdownAdvertisements();
	tr.world = NULL;

	Com_Memset( &s_worldData, 0, sizeof( s_worldData ) );
	Q_strncpyz( s_worldData.name, name, sizeof( s_worldData.name ) );

	Q_strncpyz( s_worldData.baseName, COM_SkipPath( s_worldData.name ), sizeof( s_worldData.name ) );
	COM_StripExtension(s_worldData.baseName, s_worldData.baseName, sizeof(s_worldData.baseName));

	startMarker = ri.Hunk_Alloc(0, h_low);
	c_gridVerts = 0;

	header = (dheader_t *)buffer.b;
	fileBase = (byte *)header;

	// swap all the lumps
	for ( i = 0; i < sizeof( dheader_t ) / 4; i++ ) {
		( (int32_t *)header )[i] = LittleLong( ( (int32_t *)header )[i] );
	}

	if ( header->version != BSP_VERSION && header->version != BSP_VERSION_QL ) {
		ri.Error( ERR_DROP, "%s: %s has wrong version number (%i should be %i or %i)",
			__func__, name, header->version, BSP_VERSION, BSP_VERSION_QL );
	}

	for ( i = 0; i < HEADER_LUMPS; i++ ) {
		int32_t ofs = header->lumps[i].fileofs;
		int32_t len = header->lumps[i].filelen;
		if ( (uint32_t)ofs > MAX_QINT || (uint32_t)len > MAX_QINT || ofs + len > size || ofs + len < 0 ) {
			ri.Error( ERR_DROP, "%s: %s has wrong lump[%i] size/offset", __func__, name, i );
		}
	}

	if ( header->version == BSP_VERSION_QL ) {
		const lump_t *extraLump;

		if ( size < (int)( sizeof( dheader_t ) + sizeof( lump_t ) ) ) {
			ri.Error( ERR_DROP, "%s: %s has truncated QL extra lump header", __func__, name );
		}

		extraLump = (const lump_t *)( (byte *)header + sizeof( dheader_t ) );
		qlAdvertisementsLump.fileofs = LittleLong( extraLump[LUMP_ADVERTISEMENTS_QL - HEADER_LUMPS].fileofs );
		qlAdvertisementsLump.filelen = LittleLong( extraLump[LUMP_ADVERTISEMENTS_QL - HEADER_LUMPS].filelen );
		if ( (uint32_t)qlAdvertisementsLump.fileofs > MAX_QINT ||
			(uint32_t)qlAdvertisementsLump.filelen > MAX_QINT ||
			qlAdvertisementsLump.fileofs + qlAdvertisementsLump.filelen > size ||
			qlAdvertisementsLump.fileofs + qlAdvertisementsLump.filelen < 0 ) {
			ri.Error( ERR_DROP, "%s: %s has wrong QL advertisement lump size/offset", __func__, name );
		}
	}

	// load into heap
	R_LoadLightmaps( &header->lumps[LUMP_LIGHTMAPS] );
	R_LoadShaders( &header->lumps[LUMP_SHADERS] );
	R_LoadPlanes( &header->lumps[LUMP_PLANES] );
	R_LoadFogs( &header->lumps[LUMP_FOGS], &header->lumps[LUMP_BRUSHES], &header->lumps[LUMP_BRUSHSIDES] );
	R_LoadSurfaces( &header->lumps[LUMP_SURFACES], &header->lumps[LUMP_DRAWVERTS], &header->lumps[LUMP_DRAWINDEXES] );
	R_LoadMarksurfaces( &header->lumps[LUMP_LEAFSURFACES] );
	R_LoadNodesAndLeafs( &header->lumps[LUMP_NODES], &header->lumps[LUMP_LEAFS] );
	R_LoadSubmodels( &header->lumps[LUMP_MODELS] );
	if ( header->version == BSP_VERSION_QL ) {
		R_LoadAdvertisements( &qlAdvertisementsLump );
	}
	R_LoadVisibility( &header->lumps[LUMP_VISIBILITY] );
	R_LoadEntities( &header->lumps[LUMP_ENTITIES] );
	R_LoadLightGrid( &header->lumps[LUMP_LIGHTGRID] );

#ifdef USE_VBO
	R_BuildWorldVBO( s_worldData.surfaces, s_worldData.numsurfaces );
#endif

	tr.mapLoading = qfalse;

	s_worldData.dataSize = (byte *)ri.Hunk_Alloc(0, h_low) - startMarker;

	// only set tr.world now that we know the entire level has loaded properly
	tr.world = &s_worldData;
	R_BuildSurfaceLightProxiesForWorld();
	R_LoadStaticMapLightsForWorld();

	if ( translatedMap ) {
		ri.Free( buffer.v );
	} else {
		ri.FS_FreeFile( buffer.v );
	}
}
