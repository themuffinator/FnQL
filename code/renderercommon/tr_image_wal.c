/*
===========================================================================
Copyright (C) 2026

This file is part of FnQL source code.
===========================================================================
*/

#include "../qcommon/q_shared.h"
#include "../renderercommon/tr_public.h"

#define WAL_MIPLEVELS 4
#define WAL_PALETTE_BYTES 768

typedef struct {
	char		name[32];
	uint32_t	width;
	uint32_t	height;
	uint32_t	offsets[WAL_MIPLEVELS];
	char		animname[32];
	uint32_t	flags;
	uint32_t	contents;
	uint32_t	value;
} walHeader_t;

typedef struct {
	char			manufacturer;
	char			version;
	char			encoding;
	char			bits_per_pixel;
	unsigned short	xmin, ymin, xmax, ymax;
	unsigned short	hres, vres;
	unsigned char	palette[48];
	char			reserved;
	char			color_planes;
	unsigned short	bytes_per_line;
	unsigned short	palette_type;
	unsigned short	hscreensize, vscreensize;
	char			filler[54];
} pcxHeader_t;

static byte walPalette[ WAL_PALETTE_BYTES ];
static qboolean walPaletteLoaded = qfalse;
static qboolean walPaletteWarned = qfalse;

static byte R_ResolveTransparentWALIndex( const byte *in, int x, int y, int width, int height )
{
	if ( y > 0 && *( in - width ) != 255 ) {
		return *( in - width );
	}
	if ( y < height - 1 && *( in + width ) != 255 ) {
		return *( in + width );
	}
	if ( x > 0 && *( in - 1 ) != 255 ) {
		return *( in - 1 );
	}
	if ( x < width - 1 && *( in + 1 ) != 255 ) {
		return *( in + 1 );
	}
	if ( y > 0 && x > 0 && *( in - width - 1 ) != 255 ) {
		return *( in - width - 1 );
	}
	if ( y > 0 && x < width - 1 && *( in - width + 1 ) != 255 ) {
		return *( in - width + 1 );
	}
	if ( y < height - 1 && x > 0 && *( in + width - 1 ) != 255 ) {
		return *( in + width - 1 );
	}
	if ( y < height - 1 && x < width - 1 && *( in + width + 1 ) != 255 ) {
		return *( in + width + 1 );
	}

	return 0;
}

static qboolean R_LoadWALPalette( void )
{
	union {
		byte *b;
		void *v;
	} raw;
	const pcxHeader_t *pcx;
	int len;

	if ( walPaletteLoaded ) {
		return qtrue;
	}

	raw.v = NULL;
	len = ri.FS_ReadFile( "pics/colormap.pcx", &raw.v );
	if ( !raw.b || len < (int)sizeof( pcxHeader_t ) + WAL_PALETTE_BYTES + 1 ) {
		if ( raw.v ) {
			ri.FS_FreeFile( raw.v );
		}
		if ( !walPaletteWarned ) {
			ri.Printf( PRINT_WARNING, "R_LoadWAL: couldn't load pics/colormap.pcx\n" );
			walPaletteWarned = qtrue;
		}
		return qfalse;
	}

	pcx = (const pcxHeader_t *)raw.b;
	if ( pcx->manufacturer != 0x0a
		|| pcx->version != 5
		|| pcx->encoding != 1
		|| pcx->bits_per_pixel != 8
		|| raw.b[ len - WAL_PALETTE_BYTES - 1 ] != 0x0c ) {
		ri.FS_FreeFile( raw.v );
		if ( !walPaletteWarned ) {
			ri.Printf( PRINT_WARNING, "R_LoadWAL: invalid pics/colormap.pcx palette\n" );
			walPaletteWarned = qtrue;
		}
		return qfalse;
	}

	Com_Memcpy( walPalette, raw.b + len - WAL_PALETTE_BYTES, WAL_PALETTE_BYTES );
	ri.FS_FreeFile( raw.v );
	walPaletteWarned = qfalse;
	walPaletteLoaded = qtrue;
	return qtrue;
}

void R_LoadWAL( const char *name, byte **pic, int *width, int *height )
{
	union {
		byte *b;
		void *v;
	} raw;
	const walHeader_t *wal;
	const byte *data;
	byte *out;
	uint64_t pixelCount;
	uint32_t w;
	uint32_t h;
	uint32_t offset;
	int len;
	int x;
	int y;

	if ( width ) {
		*width = 0;
	}
	if ( height ) {
		*height = 0;
	}
	*pic = NULL;

	raw.v = NULL;
	len = ri.FS_ReadFile( name, &raw.v );
	if ( !raw.b || len < (int)sizeof( walHeader_t ) ) {
		if ( raw.v ) {
			ri.FS_FreeFile( raw.v );
		}
		return;
	}

	if ( !R_LoadWALPalette() ) {
		ri.FS_FreeFile( raw.v );
		return;
	}

	wal = (const walHeader_t *)raw.b;
	w = (uint32_t)LittleLong( (int)wal->width );
	h = (uint32_t)LittleLong( (int)wal->height );
	offset = (uint32_t)LittleLong( (int)wal->offsets[0] );
	pixelCount = (uint64_t)w * h;

	if ( w == 0 || h == 0 || w > 4096 || h > 4096 ) {
		ri.Printf( PRINT_WARNING, "R_LoadWAL: bad dimensions for %s (%ux%u)\n", name, w, h );
		ri.FS_FreeFile( raw.v );
		return;
	}

	if ( offset < sizeof( walHeader_t ) || (uint64_t)offset + pixelCount > (uint64_t)len ) {
		ri.Printf( PRINT_WARNING, "R_LoadWAL: truncated data in %s\n", name );
		ri.FS_FreeFile( raw.v );
		return;
	}

	out = (byte *)ri.Malloc( (int)( pixelCount * 4 ) );
	data = raw.b + offset;

	for ( y = 0; y < (int)h; y++ ) {
		for ( x = 0; x < (int)w; x++ ) {
			size_t pixelIndex;
			size_t outIndex;
			const byte *src;
			byte index;
			byte alpha;

			pixelIndex = (size_t)y * (size_t)w + (size_t)x;
			outIndex = pixelIndex * 4;
			src = data + pixelIndex;
			index = *src;
			alpha = 255;

			if ( index == 255 ) {
				index = R_ResolveTransparentWALIndex( src, x, y, (int)w, (int)h );
				alpha = 0;
			}

			out[ outIndex + 0 ] = walPalette[ index * 3 + 0 ];
			out[ outIndex + 1 ] = walPalette[ index * 3 + 1 ];
			out[ outIndex + 2 ] = walPalette[ index * 3 + 2 ];
			out[ outIndex + 3 ] = alpha;
		}
	}

	if ( width ) {
		*width = (int)w;
	}
	if ( height ) {
		*height = (int)h;
	}

	*pic = out;
	ri.FS_FreeFile( raw.v );
}
