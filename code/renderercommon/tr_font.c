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
// tr_font.c
// 
//
// The font system uses FreeType 2.x to render TrueType fonts for use within the game.
// As of this writing ( Nov, 2000 ) Team Arena uses these fonts for all of the ui and 
// about 90% of the cgame presentation. A few areas of the CGAME were left uses the old 
// fonts since the code is shared with standard Q3A.
//
// If you include this font rendering code in a commercial product you MUST include the
// following somewhere with your product, see www.freetype.org for specifics or changes.
// The Freetype code also uses some hinting techniques that MIGHT infringe on patents 
// held by apple so be aware of that also.
//
// As of Q3A 1.25+ and Team Arena, we are shipping the game with the font rendering code
// disabled. This removes any potential patent issues and it keeps us from having to 
// distribute an actual TrueTrype font which is 1. expensive to do and 2. seems to require
// an act of god to accomplish. 
//
// What we did was pre-render the fonts using FreeType ( which is why we leave the FreeType
// credit in the credits ) and then saved off the glyph data and then hand touched up the 
// font bitmaps so they scale a bit better in GL.
//
// There are limitations in the way fonts are saved and reloaded in that it is based on 
// point size and not name. So if you pre-render Helvetica in 18 point and Impact in 18 point
// you will end up with a single 18 point data file and image set. Typically you will want to 
// choose 3 sizes to best approximate the scaling you will be doing in the ui scripting system
// 
// In the UI Scripting code, a scale of 1.0 is equal to a 48 point font. In Team Arena, we
// use three or four scales, most of them exactly equaling the specific rendered size. We 
// rendered three sizes in Team Arena, 12, 16, and 20. 
//
// To generate new font data you need to go through the following steps.
// 1. delete the fontImage_x_xx.tga files and fontImage_xx.dat files from the fonts path.
// 2. in a ui script, specificy a font, smallFont, and bigFont keyword with font name and 
//    point size. the original TrueType fonts must exist in fonts at this point.
// 3. run the game, you should see things normally.
// 4. Exit the game and there will be three dat files and at least three tga files. The 
//    tga's are in 256x256 pages so if it takes three images to render a 24 point font you 
//    will end up with fontImage_0_24.tga through fontImage_2_24.tga
// 5. In future runs of the game, the system looks for these images and data files when a
//    specific point sized font is rendered and loads them for use. 
// 6. Because of the original beta nature of the FreeType code you will probably want to hand
//    touch the font bitmaps.
// 
// Currently a define in the project turns on or off the FreeType code which is currently 
// defined out. To pre-render new fonts you need enable the define ( BUILD_FREETYPE ) and 
// uncheck the exclude from build check box in the FreeType2 area of the Renderer project. 


#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "../renderercommon/tr_public.h"
#include "ql_font_text.h"
#include <stdio.h>

#if defined( _WIN32 )
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if defined(RENDERER_RTX)
#include "../rendererrtx/tr_common.h"
#elif defined(RENDERER_VULKAN)
#include "../renderervk/tr_common.h"
#else
#include "../renderer/tr_common.h"
#endif

extern cvar_t *r_saveFontData;

extern void R_IssuePendingRenderCommands( void );
extern qhandle_t RE_RegisterShaderNoMip( const char *name );
extern void R_InitHostFonts( void );
extern void R_DoneHostFonts( void );

#ifdef BUILD_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ERRORS_H
#include FT_SYSTEM_H
#include FT_IMAGE_H
#include FT_OUTLINE_H

#define _FLOOR(x)  ((x) & -64)
#define _CEIL(x)   (((x)+63) & -64)
#define _TRUNC(x)  ((x) >> 6)

FT_Library ftLibrary = NULL;  
#endif

#define MAX_FONTS 16
#define CLASSIC_FONT_GRID_SIZE 16
#define CLASSIC_FONT_CELL_SIZE 16
static int registeredFontCount = 0;
static fontInfo_t registeredFont[MAX_FONTS];

#if 0 /* Replaced by the retail FontStash/STB host lane in tr_font_stash.c. */
/*
 * Quake Live's native modules use a renderer-owned UTF-8 text service in
 * addition to the legacy fontInfo_t ABI.  Keep that service independent of
 * the classic 256-glyph cache so retail TTF fallback remains available.
 */
#define QL_HOST_FACE_COUNT 5
#define QL_HOST_ATLAS_INITIAL_WIDTH 512
#define QL_HOST_ATLAS_INITIAL_HEIGHT 512
#define QL_HOST_ATLAS_MAX_WIDTH 2048
#define QL_HOST_ATLAS_MAX_HEIGHT 1024
#define QL_HOST_ATLAS_PADDING 1
#define QL_HOST_GLYPH_BUCKETS 1024

typedef struct {
	const char *path;
	void *data;
	int dataLength;
	qboolean dataFromFileSystem;
#ifdef BUILD_FREETYPE
	FT_Face face;
#endif
} qlHostFace_t;

typedef struct {
	unsigned int codepoint;
	short faceIndex;
	short scaleTenths;
	int next;
	float left, right, top, bottom, advance;
	float s1, t1, s2, t2;
	int atlasX, atlasY, atlasWidth, atlasHeight;
	qhandle_t shader;
} qlHostGlyph_t;

typedef struct {
	image_t *image;
	qhandle_t shader;
	byte *alpha;
	int width, height;
	int x, y, rowHeight;
	int generation;
} qlHostAtlas_t;

typedef struct {
	qboolean ready;
	qlHostFace_t faces[QL_HOST_FACE_COUNT];
	qlHostAtlas_t atlas;
	qlHostGlyph_t *glyphs;
	int glyphCount;
	int glyphCapacity;
	int buckets[QL_HOST_GLYPH_BUCKETS];
} qlHostFonts_t;

#ifdef BUILD_FREETYPE
static qlHostFonts_t qlHostFonts;
#endif

extern void RE_SetColor( const float *rgba );
extern void RE_StretchPic( float x, float y, float w, float h,
	float s1, float t1, float s2, float t2, qhandle_t hShader );

#if ( defined( RENDERER_VULKAN ) || defined( RENDERER_RTX ) ) && defined( USE_VULKAN )
extern void vk_upload_image_data( image_t *image, int x, int y, int width,
	int height, int miplevels, byte *pixels, int size, qboolean update );
#endif

#ifdef BUILD_FREETYPE
static qboolean QL_CreateOrResizeHostAtlas( int width, int height,
	qboolean preserve );

static unsigned int QL_HostGlyphHash( int faceIndex, unsigned int codepoint, int scaleTenths ) {
	unsigned int hash = codepoint * 2654435761u;
	hash ^= (unsigned int)faceIndex * 2246822519u;
	hash ^= (unsigned int)scaleTenths * 3266489917u;
	return hash & ( QL_HOST_GLYPH_BUCKETS - 1 );
}

static void QL_ResetHostFonts( void ) {
	int i;

	for ( i = 0; i < QL_HOST_FACE_COUNT; ++i ) {
		if ( qlHostFonts.faces[i].face ) {
			FT_Done_Face( qlHostFonts.faces[i].face );
		}
	}
	for ( i = 0; i < QL_HOST_FACE_COUNT; ++i ) {
		if ( qlHostFonts.faces[i].data ) {
			if ( qlHostFonts.faces[i].dataFromFileSystem ) {
				ri.FS_FreeFile( qlHostFonts.faces[i].data );
			} else {
				ri.Free( qlHostFonts.faces[i].data );
			}
		}
	}
	if ( qlHostFonts.glyphs ) {
		ri.Free( qlHostFonts.glyphs );
	}
	if ( qlHostFonts.atlas.alpha ) {
		ri.Free( qlHostFonts.atlas.alpha );
	}
	Com_Memset( &qlHostFonts, 0, sizeof( qlHostFonts ) );
}

static qboolean QL_LoadHostFace( int index, const char *path ) {
	qlHostFace_t *hostFace = &qlHostFonts.faces[index];

	hostFace->path = path;
	hostFace->dataLength = ri.FS_ReadFile( path, &hostFace->data );
	if ( hostFace->dataLength <= 0 || !hostFace->data ) {
		ri.Printf( PRINT_WARNING, "QL fonts: unable to read %s\n", path );
		hostFace->data = NULL;
		return qfalse;
	}
	hostFace->dataFromFileSystem = qtrue;
	if ( FT_New_Memory_Face( ftLibrary, hostFace->data, hostFace->dataLength, 0,
		&hostFace->face ) != 0 ) {
		ri.Printf( PRINT_WARNING, "QL fonts: invalid face %s\n", path );
		ri.FS_FreeFile( hostFace->data );
		hostFace->data = NULL;
		return qfalse;
	}
	FT_Select_Charmap( hostFace->face, FT_ENCODING_UNICODE );
	return qtrue;
}

#if defined( _WIN32 )
static qboolean QL_LoadAbsoluteHostFace( int index, const char *path ) {
	qlHostFace_t *hostFace = &qlHostFonts.faces[index];
	FILE *file;
	long length;
	void *data;

	file = fopen( path, "rb" );
	if ( !file ) {
		return qfalse;
	}
	if ( fseek( file, 0, SEEK_END ) != 0 || ( length = ftell( file ) ) <= 0 ||
		length > 32 * 1024 * 1024 || fseek( file, 0, SEEK_SET ) != 0 ) {
		fclose( file );
		return qfalse;
	}
	data = ri.Malloc( (int)length );
	if ( !data || fread( data, 1, (size_t)length, file ) != (size_t)length ) {
		if ( data ) ri.Free( data );
		fclose( file );
		return qfalse;
	}
	fclose( file );
	if ( FT_New_Memory_Face( ftLibrary, data, (FT_Long)length, 0, &hostFace->face ) != 0 ) {
		ri.Free( data );
		return qfalse;
	}
	hostFace->path = "Windows Unicode fallback";
	hostFace->data = data;
	hostFace->dataLength = (int)length;
	hostFace->dataFromFileSystem = qfalse;
	FT_Select_Charmap( hostFace->face, FT_ENCODING_UNICODE );
	ri.Printf( PRINT_ALL, "QL fonts: using Windows fallback %s\n", path );
	return qtrue;
}

static void QL_LoadWindowsFallbackFace( void ) {
	char windowsDirectory[MAX_OSPATH];
	char path[MAX_OSPATH];
	static const char *names[] = { "ARIALUNI.TTF", "segoeui.ttf", "l_10646.ttf" };
	int i;

	if ( GetWindowsDirectoryA( windowsDirectory, sizeof( windowsDirectory ) ) == 0 ) {
		return;
	}
	for ( i = 0; i < (int)ARRAY_LEN( names ); ++i ) {
		Com_sprintf( path, sizeof( path ), "%s\\Fonts\\%s", windowsDirectory, names[i] );
		if ( QL_LoadAbsoluteHostFace( 4, path ) ) {
			return;
		}
	}
}
#endif

static void QL_InitHostFonts( void ) {
	int i;

	Com_Memset( &qlHostFonts, 0, sizeof( qlHostFonts ) );
	for ( i = 0; i < QL_HOST_GLYPH_BUCKETS; ++i ) {
		qlHostFonts.buckets[i] = -1;
	}
	if ( !ftLibrary ) {
		return;
	}

	QL_LoadHostFace( 0, "fonts/handelgothic.ttf" );
	QL_LoadHostFace( 1, "fonts/notosans-regular.ttf" );
	QL_LoadHostFace( 2, "fonts/droidsansmono.ttf" );
	QL_LoadHostFace( 3, "fonts/droidsansfallbackfull.ttf" );
#if defined( _WIN32 )
	QL_LoadWindowsFallbackFace();
#endif
	for ( i = 0; i < QL_HOST_FACE_COUNT; ++i ) {
		if ( qlHostFonts.faces[i].face ) {
			qlHostFonts.ready = qtrue;
			break;
		}
	}
	if ( qlHostFonts.ready ) {
		if ( !QL_CreateOrResizeHostAtlas( QL_HOST_ATLAS_INITIAL_WIDTH,
			QL_HOST_ATLAS_INITIAL_HEIGHT, qfalse ) ) {
			ri.Printf( PRINT_WARNING, "QL fonts: unable to create retained font atlas\n" );
		}
		ri.Printf( PRINT_ALL, "QL fonts: retail TTF host text enabled\n" );
	}
}

static image_t *QL_CreateHostAtlasImage( const char *name, byte *pixels,
	int width, int height ) {
	return R_CreateImage( name, NULL, pixels, width, height, IMGFLAG_CLAMPTOEDGE );
}

static void QL_UploadHostAtlas( image_t *image, byte *pixels, int x, int y,
	int width, int height ) {
#if ( defined( RENDERER_VULKAN ) || defined( RENDERER_RTX ) ) && defined( USE_VULKAN )
	vk_upload_image_data( image, x, y, width, height, 1, pixels,
		width * height * 4, qtrue );
#else
	R_UploadSubImage( pixels, x, y, width, height, image );
#endif
}

static byte *QL_BuildHostAtlasRgba( const byte *alpha, int width, int height ) {
	byte *rgba;
	int pixelCount;
	int i;

	if ( !alpha || width <= 0 || height <= 0 ) {
		return NULL;
	}
	pixelCount = width * height;
	rgba = ri.Malloc( pixelCount * 4 );
	if ( !rgba ) {
		return NULL;
	}
	for ( i = 0; i < pixelCount; ++i ) {
		rgba[i * 4 + 0] = 255;
		rgba[i * 4 + 1] = 255;
		rgba[i * 4 + 2] = 255;
		rgba[i * 4 + 3] = alpha[i];
	}
	return rgba;
}

static void QL_FlushHostAtlasCommands( void ) {
	R_IssuePendingRenderCommands();
}

static void QL_RefreshHostGlyphAtlasBindings( void ) {
	qlHostAtlas_t *atlas = &qlHostFonts.atlas;
	int i;

	if ( atlas->width <= 0 || atlas->height <= 0 ) {
		return;
	}
	for ( i = 0; i < qlHostFonts.glyphCount; ++i ) {
		qlHostGlyph_t *glyph = &qlHostFonts.glyphs[i];
		glyph->s1 = (float)glyph->atlasX / atlas->width;
		glyph->t1 = (float)glyph->atlasY / atlas->height;
		glyph->s2 = (float)( glyph->atlasX + glyph->atlasWidth ) / atlas->width;
		glyph->t2 = (float)( glyph->atlasY + glyph->atlasHeight ) / atlas->height;
		glyph->shader = atlas->shader;
	}
}

static qboolean QL_CreateOrResizeHostAtlas( int width, int height,
	qboolean preserve ) {
	qlHostAtlas_t *atlas = &qlHostFonts.atlas;
	byte *alpha;
	byte *rgba;
	image_t *image;
	qhandle_t shader;
	char name[MAX_QPATH];
	int copyWidth;
	int copyHeight;
	int row;

	if ( width <= 0 || height <= 0 || width > QL_HOST_ATLAS_MAX_WIDTH ||
		height > QL_HOST_ATLAS_MAX_HEIGHT ) {
		return qfalse;
	}
	alpha = ri.Malloc( width * height );
	if ( !alpha ) {
		return qfalse;
	}
	Com_Memset( alpha, 0, width * height );
	if ( preserve && atlas->alpha ) {
		copyWidth = atlas->width < width ? atlas->width : width;
		copyHeight = atlas->height < height ? atlas->height : height;
		for ( row = 0; row < copyHeight; ++row ) {
			Com_Memcpy( alpha + row * width, atlas->alpha + row * atlas->width,
				copyWidth );
		}
	}
	rgba = QL_BuildHostAtlasRgba( alpha, width, height );
	if ( !rgba ) {
		ri.Free( alpha );
		return qfalse;
	}
	if ( atlas->generation == 0 ) {
		Q_strncpyz( name, "*fontstash", sizeof( name ) );
	} else {
		Com_sprintf( name, sizeof( name ), "*fontstash_%d", atlas->generation );
	}
	image = QL_CreateHostAtlasImage( name, rgba, width, height );
	ri.Free( rgba );
	if ( !image ) {
		ri.Free( alpha );
		return qfalse;
	}
	shader = RE_RegisterShaderFromImage( name, LIGHTMAP_2D, image, qfalse );
	if ( !shader ) {
		ri.Free( alpha );
		return qfalse;
	}

	if ( atlas->alpha ) {
		ri.Free( atlas->alpha );
	}
	atlas->alpha = alpha;
	atlas->image = image;
	atlas->shader = shader;
	atlas->width = width;
	atlas->height = height;
	if ( atlas->generation == 0 ) {
		atlas->x = QL_HOST_ATLAS_PADDING;
		atlas->y = QL_HOST_ATLAS_PADDING;
		atlas->rowHeight = 0;
	}
	++atlas->generation;
	QL_RefreshHostGlyphAtlasBindings();
	ri.Printf( PRINT_DEVELOPER, "QL fonts: retained atlas %dx%d (generation %d)\n",
		width, height, atlas->generation );
	return qtrue;
}

static void QL_ClearHostGlyphCache( void ) {
	int i;

	qlHostFonts.glyphCount = 0;
	for ( i = 0; i < QL_HOST_GLYPH_BUCKETS; ++i ) {
		qlHostFonts.buckets[i] = -1;
	}
}

static qboolean QL_ResetHostAtlas( void ) {
	qlHostAtlas_t *atlas = &qlHostFonts.atlas;
	byte *rgba;

	if ( !atlas->alpha || !atlas->image ) {
		return qfalse;
	}
	QL_FlushHostAtlasCommands();
	Com_Memset( atlas->alpha, 0, atlas->width * atlas->height );
	atlas->x = QL_HOST_ATLAS_PADDING;
	atlas->y = QL_HOST_ATLAS_PADDING;
	atlas->rowHeight = 0;
	QL_ClearHostGlyphCache();
	rgba = QL_BuildHostAtlasRgba( atlas->alpha, atlas->width, atlas->height );
	if ( !rgba ) {
		return qfalse;
	}
	QL_UploadHostAtlas( atlas->image, rgba, 0, 0, atlas->width, atlas->height );
	ri.Free( rgba );
	ri.Printf( PRINT_DEVELOPER, "QL fonts: max retained atlas size, cache flushed\n" );
	return qtrue;
}

static qboolean QL_ReserveHostAtlas( int width, int height,
	int *outX, int *outY ) {
	qlHostAtlas_t *atlas = &qlHostFonts.atlas;
	int grownWidth;
	int grownHeight;

	if ( width <= 0 || height <= 0 ||
		width + QL_HOST_ATLAS_PADDING * 2 > QL_HOST_ATLAS_MAX_WIDTH ||
		height + QL_HOST_ATLAS_PADDING * 2 > QL_HOST_ATLAS_MAX_HEIGHT ) {
		return qfalse;
	}
	if ( !atlas->alpha && !QL_CreateOrResizeHostAtlas(
		QL_HOST_ATLAS_INITIAL_WIDTH, QL_HOST_ATLAS_INITIAL_HEIGHT, qfalse ) ) {
		return qfalse;
	}
	if ( atlas->x + width + QL_HOST_ATLAS_PADDING > atlas->width ) {
		atlas->x = QL_HOST_ATLAS_PADDING;
		atlas->y += atlas->rowHeight + QL_HOST_ATLAS_PADDING;
		atlas->rowHeight = 0;
	}
	if ( atlas->y + height + QL_HOST_ATLAS_PADDING > atlas->height ) {
		grownWidth = atlas->width * 2;
		grownHeight = atlas->height * 2;
		if ( grownWidth > QL_HOST_ATLAS_MAX_WIDTH ) grownWidth = QL_HOST_ATLAS_MAX_WIDTH;
		if ( grownHeight > QL_HOST_ATLAS_MAX_HEIGHT ) grownHeight = QL_HOST_ATLAS_MAX_HEIGHT;
		if ( grownWidth != atlas->width || grownHeight != atlas->height ) {
			QL_FlushHostAtlasCommands();
			if ( !QL_CreateOrResizeHostAtlas( grownWidth, grownHeight, qtrue ) ) {
				return qfalse;
			}
		} else if ( !QL_ResetHostAtlas() ) {
			return qfalse;
		}
	}
	*outX = atlas->x;
	*outY = atlas->y;
	atlas->x += width + QL_HOST_ATLAS_PADDING;
	if ( height > atlas->rowHeight ) {
		atlas->rowHeight = height;
	}
	return qtrue;
}

static qboolean QL_GrowHostGlyphs( void ) {
	qlHostGlyph_t *grown;
	int capacity = qlHostFonts.glyphCapacity ? qlHostFonts.glyphCapacity * 2 : 256;

	if ( capacity > 8192 ) {
		capacity = 8192;
	}
	if ( capacity <= qlHostFonts.glyphCapacity ) {
		return qfalse;
	}
	grown = ri.Malloc( capacity * sizeof( *grown ) );
	if ( !grown ) {
		return qfalse;
	}
	if ( qlHostFonts.glyphs ) {
		Com_Memcpy( grown, qlHostFonts.glyphs,
			qlHostFonts.glyphCount * sizeof( *grown ) );
		ri.Free( qlHostFonts.glyphs );
	}
	qlHostFonts.glyphs = grown;
	qlHostFonts.glyphCapacity = capacity;
	return qtrue;
}

static qlHostGlyph_t *QL_FindHostGlyph( int faceIndex, unsigned int codepoint,
	int scaleTenths ) {
	unsigned int bucket = QL_HostGlyphHash( faceIndex, codepoint, scaleTenths );
	int index;

	for ( index = qlHostFonts.buckets[bucket]; index >= 0;
		index = qlHostFonts.glyphs[index].next ) {
		qlHostGlyph_t *glyph = &qlHostFonts.glyphs[index];
		if ( glyph->faceIndex == faceIndex && glyph->codepoint == codepoint &&
			glyph->scaleTenths == scaleTenths ) {
			return glyph;
		}
	}
	return NULL;
}

static qboolean QL_SetHostFaceSize( FT_Face face, int scaleTenths ) {
	int charSize;

	if ( !face ) {
		return qfalse;
	}
	charSize = QL_FontFaceCharSize26Dot6( scaleTenths, face->units_per_EM,
		face->ascender, face->descender );
	return FT_Set_Char_Size( face, 0, (FT_F26Dot6)charSize, 72, 72 ) == 0;
}

static int QL_RoundHostMetric( FT_Pos value ) {
	if ( value >= 0 ) {
		return (int)( ( value + 32 ) >> 6 );
	}
	return -(int)( ( ( -value ) + 32 ) >> 6 );
}

static qlHostGlyph_t *QL_CacheHostGlyph( int faceIndex, unsigned int codepoint,
	int scaleTenths ) {
	qlHostFace_t *hostFace = &qlHostFonts.faces[faceIndex];
	FT_GlyphSlot slot;
	FT_Bitmap *bitmap;
	qlHostGlyph_t *glyph;
	byte *rgba = NULL;
	unsigned int bucket;
	int atlasX = 0, atlasY = 0;
	int width, height, row, col;

	if ( !hostFace->face || !FT_Get_Char_Index( hostFace->face, codepoint ) ) {
		return NULL;
	}
	if ( !QL_SetHostFaceSize( hostFace->face, scaleTenths ) ||
		FT_Load_Char( hostFace->face, codepoint, FT_LOAD_DEFAULT ) != 0 ||
		FT_Render_Glyph( hostFace->face->glyph, FT_RENDER_MODE_NORMAL ) != 0 ) {
		return NULL;
	}
	slot = hostFace->face->glyph;
	bitmap = &slot->bitmap;
	width = (int)bitmap->width;
	height = (int)bitmap->rows;
	if ( ( width > 0 && height > 0 ) &&
		!QL_ReserveHostAtlas( width, height, &atlasX, &atlasY ) ) {
		return NULL;
	}
	if ( qlHostFonts.glyphCount == qlHostFonts.glyphCapacity && !QL_GrowHostGlyphs() ) {
		return NULL;
	}

	if ( width > 0 && height > 0 ) {
		rgba = ri.Malloc( width * height * 4 );
		if ( !rgba ) {
			return NULL;
		}
		for ( row = 0; row < height; ++row ) {
			const byte *source;
			int pitch = bitmap->pitch < 0 ? -bitmap->pitch : bitmap->pitch;
			if ( bitmap->pitch < 0 ) {
				source = bitmap->buffer + ( height - 1 - row ) * pitch;
			} else {
				source = bitmap->buffer + row * pitch;
			}
			for ( col = 0; col < width; ++col ) {
				byte alpha;
				if ( bitmap->pixel_mode == FT_PIXEL_MODE_MONO ) {
					alpha = ( source[col >> 3] & ( 0x80 >> ( col & 7 ) ) ) ? 255 : 0;
				} else {
					alpha = source[col];
				}
				rgba[( row * width + col ) * 4 + 0] = 255;
				rgba[( row * width + col ) * 4 + 1] = 255;
				rgba[( row * width + col ) * 4 + 2] = 255;
				rgba[( row * width + col ) * 4 + 3] = alpha;
			}
		}
		for ( row = 0; row < height; ++row ) {
			for ( col = 0; col < width; ++col ) {
				qlHostFonts.atlas.alpha[( atlasY + row ) * qlHostFonts.atlas.width +
					atlasX + col] = rgba[( row * width + col ) * 4 + 3];
			}
		}
		QL_UploadHostAtlas( qlHostFonts.atlas.image, rgba, atlasX, atlasY, width, height );
		ri.Free( rgba );
	}

	glyph = &qlHostFonts.glyphs[qlHostFonts.glyphCount];
	Com_Memset( glyph, 0, sizeof( *glyph ) );
	glyph->codepoint = codepoint;
	glyph->faceIndex = (short)faceIndex;
	glyph->scaleTenths = (short)scaleTenths;
	glyph->left = (float)slot->bitmap_left;
	glyph->right = glyph->left + width;
	glyph->top = (float)slot->bitmap_top;
	glyph->bottom = glyph->top - height;
	glyph->advance = (float)QL_RoundHostMetric( slot->metrics.horiAdvance );
	if ( width > 0 && height > 0 ) {
		glyph->atlasX = atlasX;
		glyph->atlasY = atlasY;
		glyph->atlasWidth = width;
		glyph->atlasHeight = height;
		glyph->s1 = (float)atlasX / qlHostFonts.atlas.width;
		glyph->t1 = (float)atlasY / qlHostFonts.atlas.height;
		glyph->s2 = (float)( atlasX + width ) / qlHostFonts.atlas.width;
		glyph->t2 = (float)( atlasY + height ) / qlHostFonts.atlas.height;
		glyph->shader = qlHostFonts.atlas.shader;
	}
	bucket = QL_HostGlyphHash( faceIndex, codepoint, scaleTenths );
	glyph->next = qlHostFonts.buckets[bucket];
	qlHostFonts.buckets[bucket] = qlHostFonts.glyphCount++;
	return glyph;
}

static int QL_ResolveHostFaceIndex( int requestedFace ) {
	int resolvedFace;

	switch ( requestedFace ) {
	case 1:
	case 2:
	case 3:
		resolvedFace = requestedFace;
		break;
	case 4:
		resolvedFace = qlHostFonts.faces[4].face ? 4 : 3;
		break;
	case 0:
	default:
		resolvedFace = 0;
		break;
	}
	if ( qlHostFonts.faces[resolvedFace].face ) return resolvedFace;
	if ( qlHostFonts.faces[0].face ) return 0;
	if ( qlHostFonts.faces[1].face ) return 1;
	if ( qlHostFonts.faces[3].face ) return 3;
	if ( qlHostFonts.faces[4].face ) return 4;
	return resolvedFace;
}

static void QL_AppendHostFaceIndex( int *chain, int *count, int faceIndex ) {
	int i;

	if ( !chain || !count || *count >= 4 || faceIndex < 0 ||
		faceIndex >= QL_HOST_FACE_COUNT ) {
		return;
	}
	for ( i = 0; i < *count; ++i ) {
		if ( chain[i] == faceIndex ) return;
	}
	chain[(*count)++] = faceIndex;
}

static qlHostGlyph_t *QL_GetHostGlyph( int requestedFace, unsigned int codepoint,
	int scaleTenths ) {
	int chain[4];
	int count = 0;
	int i;

	requestedFace = QL_ResolveHostFaceIndex( requestedFace );
	QL_AppendHostFaceIndex( chain, &count, requestedFace );
	QL_AppendHostFaceIndex( chain, &count, 1 );
	QL_AppendHostFaceIndex( chain, &count, 3 );
	QL_AppendHostFaceIndex( chain, &count,
		qlHostFonts.faces[4].face ? 4 : 3 );
	for ( i = 0; i < count; ++i ) {
		qlHostGlyph_t *glyph = QL_FindHostGlyph( chain[i], codepoint, scaleTenths );
		if ( glyph ) {
			return glyph;
		}
		glyph = QL_CacheHostGlyph( chain[i], codepoint, scaleTenths );
		if ( glyph ) {
			return glyph;
		}
	}
	return NULL;
}
#endif /* BUILD_FREETYPE */

qboolean RE_GetScaledFontMetrics( int fontHandle, float scale, float *outAscent,
	float *outDescent, float *outLineHeight ) {
	if ( outAscent ) *outAscent = 0.0f;
	if ( outDescent ) *outDescent = 0.0f;
	if ( outLineHeight ) *outLineHeight = 0.0f;
#ifdef BUILD_FREETYPE
	if ( qlHostFonts.ready ) {
		int index = QL_ResolveHostFaceIndex( fontHandle );
		FT_Face face = qlHostFonts.faces[index].face;
		int scaleTenths = QL_FontScaleTenths( scale );
		int fontHeight;
		int lineHeight;
		fontHeight = face ? face->ascender - face->descender : 0;
		lineHeight = face ? face->height : 0;
		if ( face && fontHeight > 0 && QL_SetHostFaceSize( face, scaleTenths ) ) {
			if ( lineHeight <= 0 ) lineHeight = fontHeight;
			if ( outAscent ) *outAscent = QL_FontFaceMetric( scaleTenths, face->ascender, face->ascender, face->descender );
			if ( outDescent ) *outDescent = QL_FontFaceMetric( scaleTenths, face->descender, face->ascender, face->descender );
			if ( outLineHeight ) *outLineHeight = QL_FontFaceMetric( scaleTenths, lineHeight, face->ascender, face->descender );
			return qtrue;
		}
	}
#endif
	return qfalse;
}

qhandle_t RE_GetFontAtlasDebugShader( int *outWidth, int *outHeight ) {
	if ( outWidth ) *outWidth = 0;
	if ( outHeight ) *outHeight = 0;
#ifdef BUILD_FREETYPE
	if ( qlHostFonts.atlas.shader ) {
		if ( outWidth ) *outWidth = qlHostFonts.atlas.width;
		if ( outHeight ) *outHeight = qlHostFonts.atlas.height;
		return qlHostFonts.atlas.shader;
	}
#endif
	return 0;
}

void RE_DrawScaledText( int x, int y, const char *text, int fontHandle,
	float scale, int limit, float *maxX, qboolean forceColor, const float *baseColor ) {
#ifdef BUILD_FREETYPE
	const char *cursor;
	const char *end;
	vec4_t originalColor;
	float penX = (float)x;
	float clipX = maxX ? *maxX : 0.0f;
	qboolean clipping = maxX && clipX > 0.0f;
	int remaining = limit;
	int scaleTenths = QL_FontScaleTenths( scale );

	if ( !qlHostFonts.ready || !text ) return;
	if ( baseColor ) Com_Memcpy( originalColor, baseColor, sizeof( originalColor ) );
	else Vector4Set( originalColor, 1, 1, 1, 1 );
	if ( maxX && !clipping ) *maxX = penX;
	end = text + strlen( text );
	RE_SetColor( originalColor );
	for ( cursor = text; cursor < end && *cursor && ( limit <= 0 || remaining > 0 ); ) {
		qlFontUtf8Result_t decoded;
		qlHostGlyph_t *glyph;
		int colorIndex;
		int colorBytes = QL_FontColorEscape( cursor, end, &colorIndex );
		float extent;
		if ( colorBytes ) {
			if ( !forceColor ) {
				vec4_t color;
				Com_Memcpy( color, g_color_table[colorIndex], sizeof( color ) );
				color[3] = originalColor[3];
				RE_SetColor( color );
			}
			cursor += colorBytes;
			continue;
		}
		decoded = QL_FontDecodeUtf8( cursor, end );
		if ( decoded.bytes <= 0 ) break;
		glyph = QL_GetHostGlyph( fontHandle, decoded.codepoint, scaleTenths );
		cursor += decoded.bytes;
		if ( !glyph ) continue;
		extent = penX + glyph->advance;
		if ( penX + glyph->right > extent ) extent = penX + glyph->right;
		if ( clipping && extent > clipX ) {
			*maxX = penX;
			break;
		}
		if ( glyph->shader && glyph->right > glyph->left && glyph->top > glyph->bottom ) {
			RE_StretchPic( penX + glyph->left, (float)y - glyph->top,
				glyph->right - glyph->left, glyph->top - glyph->bottom,
				glyph->s1, glyph->t1, glyph->s2, glyph->t2, glyph->shader );
		}
		penX += glyph->advance;
		if ( maxX && !clipping ) *maxX = extent;
		if ( limit > 0 ) --remaining;
	}
	RE_SetColor( originalColor );
#else
	(void)x; (void)y; (void)text; (void)fontHandle; (void)scale; (void)limit;
	(void)maxX; (void)forceColor; (void)baseColor;
#endif
}

void RE_MeasureScaledText( const char *text, const char *end, int fontHandle,
	float scale, int limit, float *outWidth, float *outHeight, float *outLeft ) {
	if ( outWidth ) *outWidth = 0.0f;
	if ( outHeight ) *outHeight = 0.0f;
	if ( outLeft ) *outLeft = 0.0f;
#ifdef BUILD_FREETYPE
	if ( qlHostFonts.ready && text ) {
		const char *cursor = text;
		float penX = 0.0f, minLeft = 0.0f, maxRight = 0.0f;
		float minTop = 0.0f, maxBottom = 0.0f, ascent = 0.0f;
		qboolean haveBounds = qfalse;
		int remaining = limit;
		int scaleTenths = QL_FontScaleTenths( scale );
		while ( *cursor && ( !end || cursor < end ) && ( limit <= 0 || remaining > 0 ) ) {
			qlFontUtf8Result_t decoded;
			qlHostGlyph_t *glyph;
			int colorBytes = QL_FontColorEscape( cursor, end, NULL );
			float left, right;
			if ( colorBytes ) { cursor += colorBytes; continue; }
			decoded = QL_FontDecodeUtf8( cursor, end );
			if ( decoded.bytes <= 0 ) break;
			glyph = QL_GetHostGlyph( fontHandle, decoded.codepoint, scaleTenths );
			cursor += decoded.bytes;
			if ( !glyph ) continue;
			left = penX + glyph->left;
			right = penX + glyph->right;
			haveBounds = qtrue;
			if ( left < minLeft ) minLeft = left;
			if ( right > maxRight ) maxRight = right;
			if ( -glyph->top < minTop ) minTop = -glyph->top;
			if ( -glyph->bottom > maxBottom ) maxBottom = -glyph->bottom;
			penX += glyph->advance;
			if ( limit > 0 ) --remaining;
		}
		if ( outWidth && haveBounds ) *outWidth = maxRight - minLeft;
		if ( outLeft && haveBounds ) *outLeft = minLeft;
		if ( RE_GetScaledFontMetrics( fontHandle, scale, &ascent, NULL, NULL ) ) {
			if ( outHeight ) *outHeight = ascent;
		} else if ( outHeight && haveBounds ) {
			*outHeight = maxBottom - minTop;
		}
	}
#else
	(void)text; (void)end; (void)fontHandle; (void)scale; (void)limit;
#endif
}
#endif /* retired FreeType host approximation */

#ifdef BUILD_FREETYPE
void R_GetGlyphInfo(FT_GlyphSlot glyph, int *left, int *right, int *width, int *top, int *bottom, int *height, int *pitch) {
	*left  = _FLOOR( glyph->metrics.horiBearingX );
	*right = _CEIL( glyph->metrics.horiBearingX + glyph->metrics.width );
	*width = _TRUNC(*right - *left);

	*top    = _CEIL( glyph->metrics.horiBearingY );
	*bottom = _FLOOR( glyph->metrics.horiBearingY - glyph->metrics.height );
	*height = _TRUNC( *top - *bottom );
	*pitch  = ( qtrue ? (*width+3) & -4 : (*width+7) >> 3 );
}


FT_Bitmap *R_RenderGlyph(FT_GlyphSlot glyph, glyphInfo_t* glyphOut) {
	FT_Bitmap  *bit2;
	int left, right, width, top, bottom, height, pitch, size;

	R_GetGlyphInfo(glyph, &left, &right, &width, &top, &bottom, &height, &pitch);

	if ( glyph->format == ft_glyph_format_outline ) {
		size   = pitch*height; 

		bit2 = ri.Malloc(sizeof(FT_Bitmap));

		bit2->width      = width;
		bit2->rows       = height;
		bit2->pitch      = pitch;
		bit2->pixel_mode = ft_pixel_mode_grays;
		//bit2->pixel_mode = ft_pixel_mode_mono;
		bit2->buffer     = ri.Malloc(pitch*height);
		bit2->num_grays = 256;

		Com_Memset( bit2->buffer, 0, size );

		FT_Outline_Translate( &glyph->outline, -left, -bottom );

		FT_Outline_Get_Bitmap( ftLibrary, &glyph->outline, bit2 );

		glyphOut->height = height;
		glyphOut->pitch = pitch;
		glyphOut->top = (glyph->metrics.horiBearingY >> 6) + 1;
		glyphOut->bottom = bottom;

		return bit2;
	} else {
		ri.Printf(PRINT_ALL, "Non-outline fonts are not supported\n");
	}
	return NULL;
}

static void WriteTGA (const char *filename, byte *data, int width, int height) {
	byte			*buffer;
	int				i, c;
	int             row;
	unsigned char  *flip;
	unsigned char  *src, *dst;

	buffer = ri.Malloc(width*height*4 + 18);
	Com_Memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = width&255;
	buffer[13] = width>>8;
	buffer[14] = height&255;
	buffer[15] = height>>8;
	buffer[16] = 32;	// pixel size

	// swap rgb to bgr
	c = 18 + width * height * 4;
	for (i=18 ; i<c ; i+=4)
	{
		buffer[i] = data[i-18+2];		// blue
		buffer[i+1] = data[i-18+1];		// green
		buffer[i+2] = data[i-18+0];		// red
		buffer[i+3] = data[i-18+3];		// alpha
	}

	// flip upside down
	flip = (unsigned char *)ri.Malloc(width*4);
	for(row = 0; row < height/2; row++)
	{
		src = buffer + 18 + row * 4 * width;
		dst = buffer + 18 + (height - row - 1) * 4 * width;

		Com_Memcpy(flip, src, width*4);
		Com_Memcpy(src, dst, width*4);
		Com_Memcpy(dst, flip, width*4);
	}
	ri.Free(flip);

	ri.FS_WriteFile(filename, buffer, c);

	//f = fopen (filename, "wb");
	//fwrite (buffer, 1, c, f);
	//fclose (f);

	ri.Free (buffer);
}

static glyphInfo_t *RE_ConstructGlyphInfo(unsigned char *imageOut, int *xOut, int *yOut, int *maxHeight, FT_Face face, const unsigned char c, qboolean calcHeight) {
	int i;
	static glyphInfo_t glyph;
	unsigned char *src, *dst;
	float scaled_width, scaled_height;
	FT_Bitmap *bitmap = NULL;

	Com_Memset(&glyph, 0, sizeof(glyphInfo_t));
	// make sure everything is here
	if (face != NULL) {
		FT_Load_Glyph(face, FT_Get_Char_Index( face, c), FT_LOAD_DEFAULT );
		bitmap = R_RenderGlyph(face->glyph, &glyph);
		if (bitmap) {
			glyph.xSkip = (face->glyph->metrics.horiAdvance >> 6) + 1;
		} else {
			return &glyph;
		}

		if (glyph.height > *maxHeight) {
			*maxHeight = glyph.height;
		}

		if (calcHeight) {
			ri.Free(bitmap->buffer);
			ri.Free(bitmap);
			return &glyph;
		}

/*
		// need to convert to power of 2 sizes so we do not get 
		// any scaling from the gl upload
		for (scaled_width = 1 ; scaled_width < glyph.pitch ; scaled_width<<=1)
			;
		for (scaled_height = 1 ; scaled_height < glyph.height ; scaled_height<<=1)
			;
*/

		scaled_width = glyph.pitch;
		scaled_height = glyph.height;

		// we need to make sure we fit
		if (*xOut + scaled_width + 1 >= 255) {
			*xOut = 0;
			*yOut += *maxHeight + 1;
		}

		if (*yOut + *maxHeight + 1 >= 255) {
			*yOut = -1;
			*xOut = -1;
			ri.Free(bitmap->buffer);
			ri.Free(bitmap);
			return &glyph;
		}


		src = bitmap->buffer;
		dst = imageOut + (*yOut * 256) + *xOut;

		if (bitmap->pixel_mode == ft_pixel_mode_mono) {
			for (i = 0; i < glyph.height; i++) {
				int j;
				unsigned char *_src = src;
				unsigned char *_dst = dst;
				unsigned char mask = 0x80;
				unsigned char val = *_src;
				for (j = 0; j < glyph.pitch; j++) {
					if (mask == 0x80) {
						val = *_src++;
					}
					if (val & mask) {
						*_dst = 0xff;
					}
					mask >>= 1;

					if ( mask == 0 ) {
						mask = 0x80;
					}
					_dst++;
				}

				src += glyph.pitch;
				dst += 256;
			}
		} else {
			for (i = 0; i < glyph.height; i++) {
				Com_Memcpy(dst, src, glyph.pitch);
				src += glyph.pitch;
				dst += 256;
			}
		}

		// we now have an 8 bit per pixel grey scale bitmap 
		// that is width wide and pf->ftSize->metrics.y_ppem tall

		glyph.imageHeight = scaled_height;
		glyph.imageWidth = scaled_width;
		glyph.s = (float)*xOut / 256;
		glyph.t = (float)*yOut / 256;
		glyph.s2 = glyph.s + (float)scaled_width / 256;
		glyph.t2 = glyph.t + (float)scaled_height / 256;

		*xOut += scaled_width + 1;

		ri.Free(bitmap->buffer);
		ri.Free(bitmap);
	}

	return &glyph;
}
#endif

static int fdOffset;
static byte	*fdFile;

static int readInt( void ) {
	int i = ((unsigned int)fdFile[fdOffset] | ((unsigned int)fdFile[fdOffset+1]<<8) | ((unsigned int)fdFile[fdOffset+2]<<16) | ((unsigned int)fdFile[fdOffset+3]<<24));
	fdOffset += 4;
	return i;
}

typedef union {
	byte	fred[4];
	float	ffred;
} poor;

float readFloat( void ) {
	poor	me;
#if defined Q3_BIG_ENDIAN
	me.fred[0] = fdFile[fdOffset+3];
	me.fred[1] = fdFile[fdOffset+2];
	me.fred[2] = fdFile[fdOffset+1];
	me.fred[3] = fdFile[fdOffset+0];
#elif defined Q3_LITTLE_ENDIAN
	me.fred[0] = fdFile[fdOffset+0];
	me.fred[1] = fdFile[fdOffset+1];
	me.fred[2] = fdFile[fdOffset+2];
	me.fred[3] = fdFile[fdOffset+3];
#endif
	fdOffset += 4;
	return me.ffred;
}

/*
=================
R_ResolveRetailFontPath

Retail UI menus use logical font aliases rather than filenames. Resolve those
aliases at the renderer boundary while leaving explicit third-party paths
unchanged.
=================
*/
static const char *R_ResolveRetailFontPath( const char *fontName ) {
	if ( !fontName || !fontName[0] || !Q_stricmp( fontName, "fonts/font" ) ||
		!Q_stricmp( fontName, "fonts/bigfont" ) || !Q_stricmp( fontName, "normal" ) ) {
		return "fonts/handelgothic.ttf";
	}

	if ( !Q_stricmp( fontName, "fonts/smallfont" ) || !Q_stricmp( fontName, "sans" ) ) {
		return "fonts/notosans-regular.ttf";
	}

	if ( !Q_stricmp( fontName, "fonts/monofont" ) || !Q_stricmp( fontName, "mono" ) ) {
		return "fonts/droidsansmono.ttf";
	}

	if ( !Q_stricmp( fontName, "sans-fallback" ) || !Q_stricmp( fontName, "sans-windows-fallback" ) ) {
		return "fonts/droidsansfallbackfull.ttf";
	}

	return fontName;
}

static void R_BuildClassicFontStem( const char *fontName, char *stem, int stemSize ) {
	char resolvedPath[MAX_QPATH];
	char stripped[MAX_QPATH];
	const char *baseName;
	int inputIndex;
	int outputIndex;

	if ( !stem || stemSize <= 0 ) {
		return;
	}
	stem[0] = '\0';
	Q_strncpyz( resolvedPath, R_ResolveRetailFontPath( fontName ), sizeof( resolvedPath ) );
	baseName = COM_SkipPath( resolvedPath );
	COM_StripExtension( baseName, stripped, sizeof( stripped ) );
	Q_strlwr( stripped );

	for ( inputIndex = 0, outputIndex = 0;
		stripped[inputIndex] && outputIndex < stemSize - 1; ++inputIndex ) {
		const char ch = stripped[inputIndex];
		stem[outputIndex++] = ( ( ch >= 'a' && ch <= 'z' ) ||
			( ch >= '0' && ch <= '9' ) ) ? ch : '_';
	}
	stem[outputIndex] = '\0';
}

static void R_BuildClassicFontCacheName( const char *fontName, int pointSize,
	char *name, int nameSize ) {
	char stem[MAX_QPATH];

	R_BuildClassicFontStem( fontName, stem, sizeof( stem ) );
	if ( stem[0] ) {
		Com_sprintf( name, nameSize, "fonts/fontImage_%s_%i.dat", stem, pointSize );
	} else {
		Com_sprintf( name, nameSize, "fonts/fontImage_%i.dat", pointSize );
	}
}

static void R_BuildLegacyClassicFontCacheName( int pointSize, char *name,
	int nameSize ) {
	Com_sprintf( name, nameSize, "fonts/fontImage_%i.dat", pointSize );
}

#ifdef BUILD_FREETYPE
static void R_BuildClassicFontPageName( const char *fontName, int pointSize,
	int page, char *name, int nameSize ) {
	char stem[MAX_QPATH];

	R_BuildClassicFontStem( fontName, stem, sizeof( stem ) );
	if ( stem[0] ) {
		Com_sprintf( name, nameSize, "fonts/fontImage_%s_%i_%i.tga",
			stem, page, pointSize );
	} else {
		Com_sprintf( name, nameSize, "fonts/fontImage_%i_%i.tga", page, pointSize );
	}
}
#endif

static int R_FindRegisteredClassicFont( const char *cacheName ) {
	int i;

	for ( i = 0; i < registeredFontCount; ++i ) {
		if ( !Q_stricmp( cacheName, registeredFont[i].name ) ) {
			return i;
		}
	}
	return -1;
}

static const char *R_FindClassicFontCache( const char *cacheName,
	const char *legacyCacheName ) {
	if ( ri.FS_ReadFile( cacheName, NULL ) == sizeof( fontInfo_t ) ) {
		return cacheName;
	}
	if ( Q_stricmp( cacheName, legacyCacheName ) &&
		ri.FS_ReadFile( legacyCacheName, NULL ) == sizeof( fontInfo_t ) ) {
		return legacyCacheName;
	}
	return NULL;
}

static void R_BindClassicFontCacheShaders( fontInfo_t *font ) {
	int i;

	for ( i = GLYPH_START; i <= GLYPH_END; ++i ) {
		font->glyphs[i].glyph = RE_RegisterShaderNoMip( font->glyphs[i].shaderName );
	}
}

static qboolean R_RegisterClassicFontFallback( const char *cacheName,
	float glyphScale, fontInfo_t *font ) {
	const char *shaderName = "gfx/2d/bigchars";
	qhandle_t shader = RE_RegisterShaderNoMip( shaderName );
	const float texelCell = 1.0f / CLASSIC_FONT_GRID_SIZE;
	int i;

	if ( !shader ) {
		shaderName = "white";
		shader = RE_RegisterShaderNoMip( shaderName );
	}
	if ( !shader || !font || registeredFontCount >= MAX_FONTS ) {
		return qfalse;
	}

	Com_Memset( font, 0, sizeof( *font ) );
	for ( i = GLYPH_START; i <= GLYPH_END; ++i ) {
		glyphInfo_t *glyph = &font->glyphs[i];
		const int row = ( i >> 4 ) & ( CLASSIC_FONT_GRID_SIZE - 1 );
		const int column = i & ( CLASSIC_FONT_GRID_SIZE - 1 );

		glyph->height = CLASSIC_FONT_CELL_SIZE;
		glyph->top = CLASSIC_FONT_CELL_SIZE;
		glyph->bottom = 0;
		glyph->pitch = CLASSIC_FONT_CELL_SIZE;
		glyph->xSkip = CLASSIC_FONT_CELL_SIZE;
		glyph->imageWidth = CLASSIC_FONT_CELL_SIZE;
		glyph->imageHeight = CLASSIC_FONT_CELL_SIZE;
		glyph->s = column * texelCell;
		glyph->t = row * texelCell;
		glyph->s2 = glyph->s + texelCell;
		glyph->t2 = glyph->t + texelCell;
		glyph->glyph = shader;
		Q_strncpyz( glyph->shaderName, shaderName, sizeof( glyph->shaderName ) );
	}
	font->glyphScale = glyphScale;
	Q_strncpyz( font->name, cacheName, sizeof( font->name ) );
	Com_Memcpy( &registeredFont[registeredFontCount++], font, sizeof( *font ) );
	ri.Printf( PRINT_WARNING, "RE_RegisterFont: using bitmap fallback for %s\n", cacheName );
	return qtrue;
}

void RE_RegisterFont(const char *fontName, int pointSize, fontInfo_t *font) {
#ifdef BUILD_FREETYPE
	FT_Face face;
	int j, k, xOut, yOut, lastStart, imageNumber;
	int scaledSize, newSize, maxHeight, left;
	unsigned char *out, *imageBuff;
	glyphInfo_t *glyph = NULL;
	image_t *image;
	qhandle_t h;
	float max;
	float dpi = 72;
	const char *resolvedFontName;
	int len;
	char name[MAX_QPATH];
#endif
	void *faceData = NULL;
	const char *loadName;
	float glyphScale;
	int registeredIndex;
	int i;
	char cacheName[MAX_QPATH];
	char legacyCacheName[MAX_QPATH];

	if ( !font ) {
		ri.Printf( PRINT_WARNING, "RE_RegisterFont: called with null output\n" );
		return;
	}
	if ( !fontName || !fontName[0] ) {
		fontName = "fonts/font";
	}

	if (pointSize <= 0) {
		pointSize = 12;
	}
	glyphScale = 48.0f / pointSize;

	R_BuildClassicFontCacheName( fontName, pointSize, cacheName, sizeof( cacheName ) );
	R_BuildLegacyClassicFontCacheName( pointSize, legacyCacheName, sizeof( legacyCacheName ) );

	registeredIndex = R_FindRegisteredClassicFont( cacheName );
	if ( registeredIndex >= 0 ) {
		Com_Memcpy( font, &registeredFont[registeredIndex], sizeof( *font ) );
		return;
	}
	if ( registeredFontCount >= MAX_FONTS ) {
		ri.Printf(PRINT_WARNING, "RE_RegisterFont: Too many fonts registered already.\n");
		return;
	}

	loadName = R_FindClassicFontCache( cacheName, legacyCacheName );
	if ( loadName && ri.FS_ReadFile( loadName, &faceData ) == sizeof( fontInfo_t ) && faceData ) {
		fdOffset = 0;
		fdFile = faceData;
		for(i=0; i<GLYPHS_PER_FONT; i++) {
			font->glyphs[i].height		= readInt();
			font->glyphs[i].top			= readInt();
			font->glyphs[i].bottom		= readInt();
			font->glyphs[i].pitch		= readInt();
			font->glyphs[i].xSkip		= readInt();
			font->glyphs[i].imageWidth	= readInt();
			font->glyphs[i].imageHeight = readInt();
			font->glyphs[i].s			= readFloat();
			font->glyphs[i].t			= readFloat();
			font->glyphs[i].s2			= readFloat();
			font->glyphs[i].t2			= readFloat();
			font->glyphs[i].glyph		= readInt();
			Q_strncpyz(font->glyphs[i].shaderName, (const char *)&fdFile[fdOffset], sizeof(font->glyphs[i].shaderName));
			fdOffset += sizeof(font->glyphs[i].shaderName);
		}
		font->glyphScale = readFloat();
		Com_Memcpy(font->name, &fdFile[fdOffset], MAX_QPATH);

//		Com_Memcpy(font, faceData, sizeof(fontInfo_t));
		Q_strncpyz(font->name, cacheName, sizeof(font->name));
		R_BindClassicFontCacheShaders( font );
		Com_Memcpy(&registeredFont[registeredFontCount++], font, sizeof(fontInfo_t));
		ri.FS_FreeFile(faceData);
		return;
	}
	if ( faceData ) {
		ri.FS_FreeFile( faceData );
		faceData = NULL;
	}

#ifndef BUILD_FREETYPE
	if ( !R_RegisterClassicFontFallback( cacheName, glyphScale, font ) ) {
		ri.Printf( PRINT_WARNING, "RE_RegisterFont: no FreeType or bitmap fallback for %s\n", fontName );
	}
#else
	if (ftLibrary == NULL) {
		ri.Printf(PRINT_WARNING, "RE_RegisterFont: FreeType not initialized.\n");
		R_RegisterClassicFontFallback( cacheName, glyphScale, font );
		return;
	}

	resolvedFontName = R_ResolveRetailFontPath( fontName );
	len = ri.FS_ReadFile(resolvedFontName, &faceData);
	if (len <= 0) {
		ri.Printf(PRINT_WARNING, "RE_RegisterFont: Unable to read font file '%s'\n", resolvedFontName);
		R_RegisterClassicFontFallback( cacheName, glyphScale, font );
		return;
	}

	// allocate on the stack first in case we fail
	if (FT_New_Memory_Face( ftLibrary, faceData, len, 0, &face )) {
		ri.Printf(PRINT_WARNING, "RE_RegisterFont: FreeType, unable to allocate new face.\n");
		ri.FS_FreeFile( faceData );
		R_RegisterClassicFontFallback( cacheName, glyphScale, font );
		return;
	}


	if (FT_Set_Char_Size( face, pointSize << 6, pointSize << 6, dpi, dpi)) {
		ri.Printf(PRINT_WARNING, "RE_RegisterFont: FreeType, unable to set face char size.\n");
		FT_Done_Face( face );
		ri.FS_FreeFile( faceData );
		R_RegisterClassicFontFallback( cacheName, glyphScale, font );
		return;
	}

	//*font = &registeredFonts[registeredFontCount++];

	// make a 256x256 image buffer, once it is full, register it, clean it and keep going 
	// until all glyphs are rendered

	out = ri.Malloc(256*256);
	if (out == NULL) {
		ri.Printf(PRINT_WARNING, "RE_RegisterFont: ri.Malloc failure during output image creation.\n");
		FT_Done_Face( face );
		ri.FS_FreeFile( faceData );
		R_RegisterClassicFontFallback( cacheName, glyphScale, font );
		return;
	}
	Com_Memset(out, 0, 256*256);

	maxHeight = 0;

	for (i = GLYPH_START; i <= GLYPH_END; i++) {
		RE_ConstructGlyphInfo(out, &xOut, &yOut, &maxHeight, face, (unsigned char)i, qtrue);
	}

	xOut = 0;
	yOut = 0;
	i = GLYPH_START;
	lastStart = i;
	imageNumber = 0;

	while ( i <= GLYPH_END + 1 ) {

		if ( i == GLYPH_END + 1 ) {
			// upload/save current image buffer
			xOut = yOut = -1;
		} else {
			glyph = RE_ConstructGlyphInfo(out, &xOut, &yOut, &maxHeight, face, (unsigned char)i, qfalse);
		}

		if (xOut == -1 || yOut == -1)  {
			// ran out of room
			// we need to create an image from the bitmap, set all the handles in the glyphs to this point
			// 

			scaledSize = 256*256;
			newSize = scaledSize * 4;
			imageBuff = ri.Malloc(newSize);
			left = 0;
			max = 0;
			for ( k = 0; k < (scaledSize) ; k++ ) {
				if (max < out[k]) {
					max = out[k];
				}
			}

			if (max > 0) {
				max = 255/max;
			}

			for ( k = 0; k < (scaledSize) ; k++ ) {
				imageBuff[left++] = 255;
				imageBuff[left++] = 255;
				imageBuff[left++] = 255;

				imageBuff[left++] = ((float)out[k] * max);
			}

			R_BuildClassicFontPageName( fontName, pointSize, imageNumber++, name, sizeof( name ) );
			if ( r_saveFontData && r_saveFontData->integer ) {
				WriteTGA(name, imageBuff, 256, 256);
			}

			//Com_sprintf (name, sizeof(name), "fonts/fontImage_%i_%i", imageNumber++, pointSize);
			image = R_CreateImage( name, NULL, imageBuff, 256, 256, IMGFLAG_CLAMPTOEDGE );
			h = RE_RegisterShaderFromImage(name, LIGHTMAP_2D, image, qfalse);
			for (j = lastStart; j < i; j++) {
				font->glyphs[j].glyph = h;
				Q_strncpyz(font->glyphs[j].shaderName, name, sizeof(font->glyphs[j].shaderName));
			}
			lastStart = i;
			Com_Memset(out, 0, 256*256);
			xOut = 0;
			yOut = 0;
			ri.Free(imageBuff);
			if ( i == GLYPH_END + 1 )
				i++;
		} else if ( glyph ) {
			Com_Memcpy(&font->glyphs[i], glyph, sizeof(glyphInfo_t));
			i++;
		} else {
			ri.Printf( PRINT_WARNING, "RE_RegisterFont: failed to construct glyph %d\n", i );
			break;
		}
	}

	font->glyphScale = glyphScale;
	Q_strncpyz( font->name, cacheName, sizeof( font->name ) );
	Com_Memcpy(&registeredFont[registeredFontCount++], font, sizeof(fontInfo_t));

	if ( r_saveFontData && r_saveFontData->integer ) {
		ri.FS_WriteFile( cacheName, font, sizeof(fontInfo_t));
	}

	ri.Free(out);

	FT_Done_Face( face );
	ri.FS_FreeFile(faceData);
#endif
}



void R_InitFreeType(void) {
#ifdef BUILD_FREETYPE
	if (FT_Init_FreeType( &ftLibrary )) {
		ri.Printf(PRINT_WARNING, "R_InitFreeType: Unable to initialize FreeType.\n");
	}
#endif
	R_InitHostFonts();
	registeredFontCount = 0;
}


void R_DoneFreeType(void) {
	R_DoneHostFonts();
#ifdef BUILD_FREETYPE
	if (ftLibrary) {
		FT_Done_FreeType( ftLibrary );
		ftLibrary = NULL;
	}
#endif
	registeredFontCount = 0;
}

