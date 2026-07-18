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

/*
 * Retail Quake Live owns a retained FontStash/STB text lane alongside the
 * classic fontInfo_t/FreeType lane. The pinned 2014 FontStash revision is the
 * implementation family embedded in quakelive_steam.exe; this adapter supplies
 * the renderer callbacks and QL's three-face fallback and text-control rules.
 */

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

#if defined( RENDERER_RTX )
#include "../rendererrtx/tr_common.h"
#elif defined( RENDERER_VULKAN )
#include "../renderervk/tr_common.h"
#else
#include "../renderer/tr_common.h"
#endif

extern void R_IssuePendingRenderCommands( void );
extern void RE_SetColor( const float *rgba );
extern void RE_StretchPic( float x, float y, float w, float h,
	float s1, float t1, float s2, float t2, qhandle_t hShader );

#if ( defined( RENDERER_VULKAN ) || defined( RENDERER_RTX ) ) && defined( USE_VULKAN )
extern void vk_upload_image_data( image_t *image, int x, int y, int width,
	int height, int miplevels, byte *pixels, int size, qboolean update );
#endif

#ifdef BUILD_FONTSTASH
/* quakelive_steam.exe allocates an exact 0x20000-byte STB work buffer. */
#define FONS_SCRATCH_BUF_SIZE ( 128 * 1024 )
#define FONTSTASH_IMPLEMENTATION
#if defined( _MSC_VER )
#pragma warning( push )
#pragma warning( disable : 4456 ) /* Historical stb_truetype local names. */
#endif
#include <fontstash.h>
#if defined( _MSC_VER )
#pragma warning( pop )
#endif

#define QL_HOST_FACE_COUNT 5
#define QL_HOST_FALLBACK_COUNT 3
#define QL_HOST_ATLAS_INITIAL_WIDTH 512
#define QL_HOST_ATLAS_INITIAL_HEIGHT 512
#define QL_HOST_ATLAS_MAX_WIDTH 2048
#define QL_HOST_ATLAS_MAX_HEIGHT 1024

typedef struct {
	void *data;
	int dataLength;
	qboolean dataFromFileSystem;
} qlHostFaceData_t;

typedef struct {
	FONScontext *stash;
	int faceIds[QL_HOST_FACE_COUNT];
	int fallbackIds[QL_HOST_FALLBACK_COUNT];
	qlHostFaceData_t faceData[QL_HOST_FACE_COUNT];
	image_t *atlasImage;
	qhandle_t atlasShader;
	int atlasWidth;
	int atlasHeight;
	int atlasGeneration;
	qboolean ready;
} qlHostFonts_t;

static qlHostFonts_t qlHostFonts;

static image_t *QL_CreateHostAtlasImage( const char *name, byte *pixels,
	int width, int height ) {
	const int flags = IMGFLAG_CLAMPTOEDGE | IMGFLAG_NO_COMPRESSION |
		IMGFLAG_NOLIGHTSCALE | IMGFLAG_NOSCALE;
	return R_CreateImage( name, NULL, pixels, width, height, flags );
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

static int QL_FonsRenderCreate( void *userPtr, int width, int height ) {
	qlHostFonts_t *host = (qlHostFonts_t *)userPtr;
	byte *rgba;
	image_t *image;
	qhandle_t shader;
	char name[MAX_QPATH];
	int i;

	if ( !host || width <= 0 || height <= 0 ||
		width > QL_HOST_ATLAS_MAX_WIDTH || height > QL_HOST_ATLAS_MAX_HEIGHT ) {
		return 0;
	}
	// fonsResetAtlas invokes renderResize even when the dimensions do not
	// change.  Recreating an engine image for that reset leaks Vulkan world
	// image chunks until map loading aborts.  The resize caller has already
	// completed queued draws, and later glyph updates overwrite every sampled
	// slot, so the retained image needs neither recreation nor a full clear.
	if ( host->atlasImage && host->atlasWidth == width
		&& host->atlasHeight == height ) {
		return 1;
	}
	rgba = ri.Malloc( width * height * 4 );
	if ( !rgba ) return 0;
	for ( i = 0; i < width * height; ++i ) {
		rgba[i * 4 + 0] = 255;
		rgba[i * 4 + 1] = 255;
		rgba[i * 4 + 2] = 255;
		rgba[i * 4 + 3] = 0;
	}
	if ( host->atlasGeneration == 0 ) {
		Q_strncpyz( name, "*fontstash", sizeof( name ) );
	} else {
		Com_sprintf( name, sizeof( name ), "*fontstash_%d", host->atlasGeneration );
	}
	image = QL_CreateHostAtlasImage( name, rgba, width, height );
	ri.Free( rgba );
	if ( !image ) return 0;
	shader = RE_RegisterShaderFromImage( name, LIGHTMAP_2D, image, qfalse );
	if ( !shader ) return 0;
	host->atlasImage = image;
	host->atlasShader = shader;
	host->atlasWidth = width;
	host->atlasHeight = height;
	++host->atlasGeneration;
	return 1;
}

static int QL_FonsRenderResize( void *userPtr, int width, int height ) {
	R_IssuePendingRenderCommands();
	return QL_FonsRenderCreate( userPtr, width, height );
}

static void QL_FonsRenderUpdate( void *userPtr, int *rect,
	const unsigned char *data ) {
	qlHostFonts_t *host = (qlHostFonts_t *)userPtr;
	byte *rgba;
	int width;
	int height;
	int x;
	int y;

	if ( !host || !host->atlasImage || !rect || !data ) return;
	width = rect[2] - rect[0];
	height = rect[3] - rect[1];
	if ( width <= 0 || height <= 0 ) return;
	rgba = ri.Malloc( width * height * 4 );
	if ( !rgba ) return;
	for ( y = 0; y < height; ++y ) {
		for ( x = 0; x < width; ++x ) {
			const byte alpha = data[( rect[1] + y ) * host->atlasWidth + rect[0] + x];
			const int pixel = ( y * width + x ) * 4;
			rgba[pixel + 0] = 255;
			rgba[pixel + 1] = 255;
			rgba[pixel + 2] = 255;
			rgba[pixel + 3] = alpha;
		}
	}
	QL_UploadHostAtlas( host->atlasImage, rgba, rect[0], rect[1], width, height );
	ri.Free( rgba );
}

static void QL_FonsRenderDraw( void *userPtr, const float *verts,
	const float *tcoords, const unsigned int *colors, int vertexCount ) {
	qlHostFonts_t *host = (qlHostFonts_t *)userPtr;
	int i;

	(void)colors; /* Retail uses the renderer's current RE_SetColor state. */
	if ( !host || !host->atlasShader || !verts || !tcoords ) return;
	for ( i = 0; i + 5 < vertexCount; i += 6 ) {
		const float x0 = verts[i * 2 + 0];
		const float y0 = verts[i * 2 + 1];
		const float x1 = verts[( i + 1 ) * 2 + 0];
		const float y1 = verts[( i + 1 ) * 2 + 1];
		RE_StretchPic( x0, y0, x1 - x0, y1 - y0,
			tcoords[i * 2 + 0], tcoords[i * 2 + 1],
			tcoords[( i + 1 ) * 2 + 0], tcoords[( i + 1 ) * 2 + 1],
			host->atlasShader );
	}
}

static void QL_FonsRenderDelete( void *userPtr ) {
	qlHostFonts_t *host = (qlHostFonts_t *)userPtr;
	if ( host ) host->ready = qfalse;
}

static void QL_FonsErrorCallback( void *userPtr, int error, int value ) {
	qlHostFonts_t *host = (qlHostFonts_t *)userPtr;
	int width;
	int height;
	int grownWidth;
	int grownHeight;

	ri.Printf( PRINT_DEVELOPER, "R_fonsErrorCallback: error %d val %d\n", error, value );
	if ( !host || !host->stash || error != FONS_ATLAS_FULL ) return;
	fonsGetAtlasSize( host->stash, &width, &height );
	grownWidth = width * 2;
	grownHeight = height * 2;
	if ( grownWidth > QL_HOST_ATLAS_MAX_WIDTH ) grownWidth = QL_HOST_ATLAS_MAX_WIDTH;
	if ( grownHeight > QL_HOST_ATLAS_MAX_HEIGHT ) grownHeight = QL_HOST_ATLAS_MAX_HEIGHT;
	if ( grownWidth == width && grownHeight == height ) {
		ri.Printf( PRINT_DEVELOPER, "Max font atlas size, flushing\n" );
		fonsResetAtlas( host->stash, width, height );
	} else {
		ri.Printf( PRINT_DEVELOPER, "Expand font atlas to %dx%d\n", grownWidth, grownHeight );
		fonsExpandAtlas( host->stash, grownWidth, grownHeight );
	}
}

static qboolean QL_AddHostFaceData( int index, const char *name, void *data,
	int length, qboolean fromFileSystem ) {
	int faceId;

	if ( index < 0 || index >= QL_HOST_FACE_COUNT || !data || length <= 0 ||
		!qlHostFonts.stash ) return qfalse;
	faceId = fonsAddFontMem( qlHostFonts.stash, name, (unsigned char *)data,
		length, 0 );
	if ( faceId == FONS_INVALID ) return qfalse;
	qlHostFonts.faceIds[index] = faceId;
	qlHostFonts.faceData[index].data = data;
	qlHostFonts.faceData[index].dataLength = length;
	qlHostFonts.faceData[index].dataFromFileSystem = fromFileSystem;
	return qtrue;
}

static qboolean QL_LoadHostFace( int index, const char *name, const char *path ) {
	void *data = NULL;
	int length = ri.FS_ReadFile( path, &data );

	if ( length <= 0 || !data ) {
		ri.Printf( PRINT_WARNING, "QL fonts: unable to read %s\n", path );
		return qfalse;
	}
	if ( !QL_AddHostFaceData( index, name, data, length, qtrue ) ) {
		ri.Printf( PRINT_WARNING, "QL fonts: invalid face %s\n", path );
		ri.FS_FreeFile( data );
		return qfalse;
	}
	return qtrue;
}

#if defined( _WIN32 )
static qboolean QL_LoadAbsoluteHostFace( int index, const char *name,
	const char *path ) {
	FILE *file;
	long length;
	void *data;

	file = fopen( path, "rb" );
	if ( !file ) return qfalse;
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
	if ( !QL_AddHostFaceData( index, name, data, (int)length, qfalse ) ) {
		ri.Free( data );
		return qfalse;
	}
	ri.Printf( PRINT_ALL, "QL fonts: using Windows fallback %s\n", path );
	return qtrue;
}

static void QL_LoadWindowsFallbackFace( void ) {
	char windowsDirectory[MAX_OSPATH];
	char path[MAX_OSPATH];
	static const char *names[] = { "ARIALUNI.TTF", "segoeui.ttf", "l_10646.ttf" };
	int i;

	if ( GetWindowsDirectoryA( windowsDirectory, sizeof( windowsDirectory ) ) == 0 ) return;
	for ( i = 0; i < (int)ARRAY_LEN( names ); ++i ) {
		Com_sprintf( path, sizeof( path ), "%s\\Fonts\\%s", windowsDirectory, names[i] );
		if ( QL_LoadAbsoluteHostFace( 4, "sans-windows-fallback", path ) ) return;
	}
}
#endif

static int QL_ResolveHostFaceId( int fontHandle ) {
	int i;

	if ( fontHandle >= 0 && fontHandle < QL_HOST_FACE_COUNT &&
		qlHostFonts.faceIds[fontHandle] != FONS_INVALID ) {
		return qlHostFonts.faceIds[fontHandle];
	}
	for ( i = 0; i < QL_HOST_FACE_COUNT; ++i ) {
		if ( qlHostFonts.faceIds[i] != FONS_INVALID ) return qlHostFonts.faceIds[i];
	}
	return FONS_INVALID;
}

static FONSglyph *QL_FindCachedHostGlyph( FONSfont *font,
	unsigned int codepoint, short isize, short iblur ) {
	unsigned int hash;
	int index;

	if ( !font ) return NULL;
	hash = fons__hashint( codepoint ) & ( FONS_HASH_LUT_SIZE - 1 );
	for ( index = font->lut[hash]; index != -1; index = font->glyphs[index].next ) {
		FONSglyph *glyph = &font->glyphs[index];
		if ( glyph->codepoint == codepoint && glyph->size == isize &&
			glyph->blur == iblur ) return glyph;
	}
	return NULL;
}

static FONSglyph *QL_GetHostGlyph( FONSfont *requestedFont,
	unsigned int codepoint, short isize, short iblur ) {
	FONSfont *fonts[QL_HOST_FALLBACK_COUNT];
	FONSglyph *cachedGlyphs[QL_HOST_FALLBACK_COUNT] = { NULL };
	unsigned char cached[QL_HOST_FALLBACK_COUNT] = { 0 };
	unsigned char supported[QL_HOST_FALLBACK_COUNT] = { 0 };
	FONSfont *selectedFont;
	int count = 0;
	int selectedIndex;
	int i;

	if ( !requestedFont || !qlHostFonts.stash ) return NULL;

	/*
	 * Observed retail order is requested cache, requested cmap, all fallback
	 * caches, then fallback cmaps. In particular, a cached fallback must not
	 * override a glyph that the requested face can rasterize.
	 */
	{
		FONSglyph *glyph = QL_FindCachedHostGlyph( requestedFont, codepoint,
			isize, iblur );
		if ( glyph ) return glyph;
	}
	if ( fons__tt_getGlyphIndex( &requestedFont->font, codepoint ) != 0 ) {
		return fons__getGlyph( qlHostFonts.stash, requestedFont, codepoint,
			isize, iblur );
	}

	for ( i = 0; i < QL_HOST_FALLBACK_COUNT; ++i ) {
		const int faceId = qlHostFonts.fallbackIds[i];
		FONSfont *font;
		int j;
		qboolean duplicate = qfalse;
		if ( faceId < 0 || faceId >= qlHostFonts.stash->nfonts ) continue;
		font = qlHostFonts.stash->fonts[faceId];
		if ( font == requestedFont ) continue;
		for ( j = 0; j < count; ++j ) {
			if ( fonts[j] == font ) duplicate = qtrue;
		}
		if ( !duplicate ) fonts[count++] = font;
	}
	for ( i = 0; i < count; ++i ) {
		cachedGlyphs[i] = QL_FindCachedHostGlyph( fonts[i], codepoint,
			isize, iblur );
		cached[i] = cachedGlyphs[i] != NULL;
	}
	selectedIndex = QL_FontSelectFallbackFace( count, cached, supported );
	if ( selectedIndex >= 0 ) {
		return cachedGlyphs[selectedIndex];
	}
	for ( i = 0; i < count; ++i ) {
		supported[i] = fons__tt_getGlyphIndex( &fonts[i]->font, codepoint ) != 0;
	}
	selectedIndex = QL_FontSelectFallbackFace( count, cached, supported );
	if ( selectedIndex < 0 ) {
		/* Preserve FontStash's requested-face missing-glyph behavior. */
		selectedFont = requestedFont;
	} else {
		selectedFont = fonts[selectedIndex];
	}
	return fons__getGlyph( qlHostFonts.stash, selectedFont, codepoint, isize, iblur );
}

static qboolean QL_NextHostCodepoint( const char **cursor, const char *end,
	unsigned int *outCodepoint ) {
	unsigned int state = FONS_UTF8_ACCEPT;
	unsigned int codepoint = 0;
	const char *current;

	if ( !cursor || !( current = *cursor ) || !outCodepoint ) return qfalse;
	while ( current < end ) {
		const unsigned int result = fons__decutf8( &state, &codepoint,
			*(const unsigned char *)current++ );
		if ( result == FONS_UTF8_ACCEPT ) {
			*cursor = current;
			*outCodepoint = codepoint;
			return qtrue;
		}
		if ( result == FONS_UTF8_REJECT ) {
			*cursor = current;
			*outCodepoint = 0xfffd;
			return qtrue;
		}
	}
	*cursor = current;
	return qfalse;
}

static float QL_FonsTextBounds( float x, float y, const char *text,
	const char *end, int limit, float *bounds ) {
	FONScontext *stash = qlHostFonts.stash;
	FONSstate *state;
	FONSfont *font;
	FONSglyph *glyph;
	FONSquad quad;
	const char *cursor = text;
	short isize;
	short iblur;
	float scale;
	float startX;
	float minX;
	float minY;
	float maxX;
	float maxY;
	int previousGlyph = -1;
	int remaining = limit;

	if ( !stash || !text ) return 0.0f;
	state = fons__getState( stash );
	if ( state->font < 0 || state->font >= stash->nfonts ) return 0.0f;
	font = stash->fonts[state->font];
	if ( !font || !font->data ) return 0.0f;
	if ( !end ) end = text + strlen( text );
	isize = (short)( state->size * 10.0f );
	iblur = (short)state->blur;
	scale = fons__tt_getPixelHeightScale( &font->font, (float)isize / 10.0f );
	y += fons__getVertAlign( stash, font, state->align, isize );
	startX = x;
	minX = maxX = x;
	minY = maxY = y;
	while ( cursor < end && remaining != 0 ) {
		unsigned int codepoint;
		int colorIndex;
		const int colorBytes = QL_FontColorEscape( cursor, end, &colorIndex );
		if ( colorBytes ) {
			RE_SetColor( g_color_table[colorIndex] );
			cursor += colorBytes;
			continue;
		}
		if ( !QL_NextHostCodepoint( &cursor, end, &codepoint ) ) break;
		if ( remaining > 0 ) --remaining;
		glyph = QL_GetHostGlyph( font, codepoint, isize, iblur );
		if ( glyph ) {
			fons__getQuad( stash, font, previousGlyph, glyph, scale,
				state->spacing, &x, &y, &quad );
			if ( quad.x0 < minX ) minX = quad.x0;
			if ( quad.x1 > maxX ) maxX = quad.x1;
			if ( quad.y0 < minY ) minY = quad.y0;
			if ( quad.y1 > maxY ) maxY = quad.y1;
		}
		previousGlyph = glyph ? glyph->index : -1;
	}
	if ( !( state->align & FONS_ALIGN_LEFT ) ) {
		const float advance = x - startX;
		if ( state->align & FONS_ALIGN_RIGHT ) {
			minX -= advance;
			maxX -= advance;
		} else if ( state->align & FONS_ALIGN_CENTER ) {
			minX -= advance * 0.5f;
			maxX -= advance * 0.5f;
		}
	}
	if ( bounds ) {
		bounds[0] = minX;
		bounds[1] = minY;
		bounds[2] = maxX;
		bounds[3] = maxY;
	}
	return x - startX;
}

static float QL_FonsDrawText( float x, float y, const char *text,
	const char *end, int limit, float *maxX, qboolean forceColor ) {
	FONScontext *stash = qlHostFonts.stash;
	FONSstate *state;
	FONSfont *font;
	FONSglyph *glyph;
	FONSquad quad;
	const char *cursor = text;
	short isize;
	short iblur;
	float scale;
	float originalX = x;
	int previousGlyph = -1;
	int remaining = limit;

	if ( !stash || !text ) return x;
	state = fons__getState( stash );
	if ( state->font < 0 || state->font >= stash->nfonts ) return x;
	font = stash->fonts[state->font];
	if ( !font || !font->data ) return x;
	if ( !end ) end = text + strlen( text );
	isize = (short)( state->size * 10.0f );
	iblur = (short)state->blur;
	scale = fons__tt_getPixelHeightScale( &font->font, (float)isize / 10.0f );
	if ( state->align & FONS_ALIGN_RIGHT ) {
		x -= QL_FonsTextBounds( x, y, text, end, limit, NULL );
	} else if ( state->align & FONS_ALIGN_CENTER ) {
		x -= QL_FonsTextBounds( x, y, text, end, limit, NULL ) * 0.5f;
	}
	y += fons__getVertAlign( stash, font, state->align, isize );
	while ( cursor < end && remaining != 0 ) {
		unsigned int codepoint;
		int colorIndex;
		const int colorBytes = QL_FontColorEscape( cursor, end, &colorIndex );
		if ( colorBytes ) {
			if ( !forceColor ) {
				fons__flush( stash );
				RE_SetColor( g_color_table[colorIndex] );
			}
			cursor += colorBytes;
			continue;
		}
		if ( !QL_NextHostCodepoint( &cursor, end, &codepoint ) ) break;
		if ( remaining > 0 ) --remaining;
		glyph = QL_GetHostGlyph( font, codepoint, isize, iblur );
		if ( glyph ) {
			const float oldX = x;
			fons__getQuad( stash, font, previousGlyph, glyph, scale,
				state->spacing, &x, &y, &quad );
			if ( maxX && *maxX < x ) {
				*maxX = oldX;
				break;
			}
			if ( stash->nverts + 6 > FONS_VERTEX_COUNT ) fons__flush( stash );
			fons__vertex( stash, quad.x0, quad.y0, quad.s0, quad.t0, state->color );
			fons__vertex( stash, quad.x1, quad.y1, quad.s1, quad.t1, state->color );
			fons__vertex( stash, quad.x1, quad.y0, quad.s1, quad.t0, state->color );
			fons__vertex( stash, quad.x0, quad.y0, quad.s0, quad.t0, state->color );
			fons__vertex( stash, quad.x0, quad.y1, quad.s0, quad.t1, state->color );
			fons__vertex( stash, quad.x1, quad.y1, quad.s1, quad.t1, state->color );
		}
		previousGlyph = glyph ? glyph->index : -1;
	}
	fons__flush( stash );
	return ( state->align & FONS_ALIGN_LEFT ) ? x : originalX;
}
#endif /* BUILD_FONTSTASH */

void R_InitHostFonts( void ) {
#ifdef BUILD_FONTSTASH
	FONSparams params;
	int i;

	Com_Memset( &qlHostFonts, 0, sizeof( qlHostFonts ) );
	for ( i = 0; i < QL_HOST_FACE_COUNT; ++i ) qlHostFonts.faceIds[i] = FONS_INVALID;
	for ( i = 0; i < QL_HOST_FALLBACK_COUNT; ++i ) qlHostFonts.fallbackIds[i] = FONS_INVALID;
	Com_Memset( &params, 0, sizeof( params ) );
	params.width = QL_HOST_ATLAS_INITIAL_WIDTH;
	params.height = QL_HOST_ATLAS_INITIAL_HEIGHT;
	params.flags = FONS_ZERO_TOPLEFT;
	params.userPtr = &qlHostFonts;
	params.renderCreate = QL_FonsRenderCreate;
	params.renderResize = QL_FonsRenderResize;
	params.renderUpdate = QL_FonsRenderUpdate;
	params.renderDraw = QL_FonsRenderDraw;
	params.renderDelete = QL_FonsRenderDelete;
	qlHostFonts.stash = fonsCreateInternal( &params );
	if ( !qlHostFonts.stash ) {
		ri.Printf( PRINT_WARNING, "QL fonts: unable to create retail FontStash context\n" );
		return;
	}
	fonsSetErrorCallback( qlHostFonts.stash, QL_FonsErrorCallback, &qlHostFonts );
	QL_LoadHostFace( 0, "normal", "fonts/handelgothic.ttf" );
	QL_LoadHostFace( 1, "sans", "fonts/notosans-regular.ttf" );
	QL_LoadHostFace( 2, "mono", "fonts/droidsansmono.ttf" );
	QL_LoadHostFace( 3, "sans-fallback", "fonts/droidsansfallbackfull.ttf" );
#if defined( _WIN32 )
	QL_LoadWindowsFallbackFace();
#endif
	qlHostFonts.fallbackIds[0] = qlHostFonts.faceIds[1];
	qlHostFonts.fallbackIds[1] = qlHostFonts.faceIds[3];
	qlHostFonts.fallbackIds[2] = qlHostFonts.faceIds[4];
	for ( i = 0; i < QL_HOST_FACE_COUNT; ++i ) {
		if ( qlHostFonts.faceIds[i] != FONS_INVALID ) {
			qlHostFonts.ready = qtrue;
			break;
		}
	}
	if ( qlHostFonts.ready ) {
		ri.Printf( PRINT_ALL, "QL fonts: retail FontStash/STB host text enabled\n" );
	}
#endif
}

void R_DoneHostFonts( void ) {
#ifdef BUILD_FONTSTASH
	int i;

	if ( qlHostFonts.stash ) fonsDeleteInternal( qlHostFonts.stash );
	for ( i = 0; i < QL_HOST_FACE_COUNT; ++i ) {
		if ( qlHostFonts.faceData[i].data ) {
			if ( qlHostFonts.faceData[i].dataFromFileSystem ) {
				ri.FS_FreeFile( qlHostFonts.faceData[i].data );
			} else {
				ri.Free( qlHostFonts.faceData[i].data );
			}
		}
	}
	Com_Memset( &qlHostFonts, 0, sizeof( qlHostFonts ) );
#endif
}

qboolean RE_GetScaledFontMetrics( int fontHandle, float scale, float *outAscent,
	float *outDescent, float *outLineHeight ) {
	if ( outAscent ) *outAscent = 0.0f;
	if ( outDescent ) *outDescent = 0.0f;
	if ( outLineHeight ) *outLineHeight = 0.0f;
#ifdef BUILD_FONTSTASH
	if ( qlHostFonts.ready && qlHostFonts.stash && scale > 0.0f ) {
		const int faceId = QL_ResolveHostFaceId( fontHandle );
		if ( faceId != FONS_INVALID ) {
			fonsSetSize( qlHostFonts.stash, scale );
			fonsSetFont( qlHostFonts.stash, faceId );
			fonsVertMetrics( qlHostFonts.stash, outAscent, outDescent, outLineHeight );
			return qtrue;
		}
	}
#else
	(void)fontHandle;
	(void)scale;
#endif
	return qfalse;
}

qhandle_t RE_GetFontAtlasDebugShader( int *outWidth, int *outHeight ) {
	if ( outWidth ) *outWidth = 0;
	if ( outHeight ) *outHeight = 0;
#ifdef BUILD_FONTSTASH
	if ( qlHostFonts.atlasShader ) {
		if ( outWidth ) *outWidth = qlHostFonts.atlasWidth;
		if ( outHeight ) *outHeight = qlHostFonts.atlasHeight;
		return qlHostFonts.atlasShader;
	}
#endif
	return 0;
}

void RE_DrawScaledText( int x, int y, const char *text, int fontHandle,
	float scale, int limit, float *maxX, qboolean forceColor ) {
#ifdef BUILD_FONTSTASH
	if ( qlHostFonts.ready && qlHostFonts.stash && text ) {
		const int faceId = QL_ResolveHostFaceId( fontHandle );
		if ( faceId != FONS_INVALID ) {
			fonsSetSize( qlHostFonts.stash, scale );
			fonsSetFont( qlHostFonts.stash, faceId );
			QL_FonsDrawText( (float)x, (float)y, text, NULL, limit, maxX, forceColor );
		}
	}
#else
	(void)x; (void)y; (void)text; (void)fontHandle; (void)scale; (void)limit;
	(void)maxX; (void)forceColor;
#endif
}

void RE_MeasureScaledText( const char *text, const char *end, int fontHandle,
	float scale, int limit, float *bounds ) {
	int i;
	if ( bounds ) {
		for ( i = 0; i < 5; ++i ) bounds[i] = 0.0f;
	}
#ifdef BUILD_FONTSTASH
	if ( qlHostFonts.ready && qlHostFonts.stash && text ) {
		const int faceId = QL_ResolveHostFaceId( fontHandle );
		if ( faceId != FONS_INVALID ) {
			fonsSetSize( qlHostFonts.stash, scale );
			fonsSetFont( qlHostFonts.stash, faceId );
			QL_FonsTextBounds( 0.0f, 0.0f, text, end, limit, bounds );
			if ( bounds ) fonsVertMetrics( qlHostFonts.stash, bounds + 4, NULL, NULL );
		}
	}
#else
	(void)text; (void)end; (void)fontHandle; (void)scale; (void)limit;
#endif
}
