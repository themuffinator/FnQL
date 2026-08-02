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

#include "../qcommon/q_shared.h"
#include "../renderercommon/tr_public.h"

typedef struct
{
	uint32_t fileSize;
	uint32_t bitmapDataOffset;
	uint32_t bitmapHeaderSize;
	int32_t width;
	int32_t height;
	uint16_t planes;
	uint16_t bitsPerPixel;
	uint32_t compression;
	uint32_t bitmapDataSize;
	uint32_t colors;
} BMPHeader_t;

static uint16_t R_BMPReadLittle16( const byte *bytes )
{
	return (uint16_t)( (uint16_t)bytes[0] |
		( (uint16_t)bytes[1] << 8u ) );
}

static uint32_t R_BMPReadLittle32( const byte *bytes )
{
	return (uint32_t)bytes[0] |
		( (uint32_t)bytes[1] << 8u ) |
		( (uint32_t)bytes[2] << 16u ) |
		( (uint32_t)bytes[3] << 24u );
}

static int32_t R_BMPReadLittleSigned32( const byte *bytes )
{
	const uint32_t value = R_BMPReadLittle32( bytes );

	if ( value <= (uint32_t)INT_MAX ) {
		return (int32_t)value;
	}
	/* Decode two's-complement values without an out-of-range unsigned cast. */
	return -1 - (int32_t)( 0xffffffffu - value );
}

void R_LoadBMP( const char *name, byte **pic, int *width, int *height )
{
	union {
		byte *b;
		void *v;
	} buffer;
	BMPHeader_t bmpHeader;
	const byte *palette = NULL;
	const byte *bitmapData;
	byte *bmpRGBA;
	size_t fileBytes;
	size_t metadataEnd;
	size_t paletteEntries = 0;
	size_t paletteBytes = 0;
	size_t bytesPerPixel;
	size_t packedRowBytes;
	size_t paddedRowBytes;
	size_t sourceRowBytes = 0;
	size_t packedDataBytes;
	size_t paddedDataBytes;
	size_t availableDataBytes;
	size_t outputBytes;
	uint32_t rows;
	int columns;
	qboolean topDown;
	int length;
	uint32_t sourceRow;

#define BMP_ERROR( ... ) do { \
		ri.FS_FreeFile( buffer.v ); \
		ri.Error( ERR_DROP, __VA_ARGS__ ); \
		return; \
	} while ( 0 )

	*pic = NULL;
	buffer.v = NULL;

	if ( width ) {
		*width = 0;
	}
	if ( height ) {
		*height = 0;
	}

	length = ri.FS_ReadFile( name, &buffer.v );
	if ( !buffer.b || length < 0 ) {
		if ( buffer.b ) {
			ri.FS_FreeFile( buffer.v );
		}
		return;
	}

	fileBytes = (size_t)length;
	if ( fileBytes < 54u ) {
		BMP_ERROR( "LoadBMP: header too short (%s)", name );
	}
	if ( buffer.b[0] != 'B' || buffer.b[1] != 'M' ) {
		BMP_ERROR( "LoadBMP: only Windows-style BMP files supported (%s)", name );
	}

	bmpHeader.fileSize = R_BMPReadLittle32( buffer.b + 2 );
	bmpHeader.bitmapDataOffset = R_BMPReadLittle32( buffer.b + 10 );
	bmpHeader.bitmapHeaderSize = R_BMPReadLittle32( buffer.b + 14 );
	bmpHeader.width = R_BMPReadLittleSigned32( buffer.b + 18 );
	bmpHeader.height = R_BMPReadLittleSigned32( buffer.b + 22 );
	bmpHeader.planes = R_BMPReadLittle16( buffer.b + 26 );
	bmpHeader.bitsPerPixel = R_BMPReadLittle16( buffer.b + 28 );
	bmpHeader.compression = R_BMPReadLittle32( buffer.b + 30 );
	bmpHeader.bitmapDataSize = R_BMPReadLittle32( buffer.b + 34 );
	bmpHeader.colors = R_BMPReadLittle32( buffer.b + 46 );

	if ( bmpHeader.fileSize != (uint32_t)fileBytes ) {
		BMP_ERROR( "LoadBMP: header size does not match file size (%u vs. %u) (%s)",
			bmpHeader.fileSize, (unsigned)fileBytes, name );
	}
	if ( bmpHeader.bitmapHeaderSize < 40u ||
		bmpHeader.bitmapHeaderSize > fileBytes - 14u ) {
		BMP_ERROR( "LoadBMP: unsupported or truncated DIB header (%s)", name );
	}
	metadataEnd = 14u + (size_t)bmpHeader.bitmapHeaderSize;
	if ( (size_t)bmpHeader.bitmapDataOffset < metadataEnd ||
		(size_t)bmpHeader.bitmapDataOffset > fileBytes ) {
		BMP_ERROR( "LoadBMP: invalid offset value in header (%s)", name );
	}
	if ( bmpHeader.planes != 1u ) {
		BMP_ERROR( "LoadBMP: invalid color-plane count (%s)", name );
	}
	if ( bmpHeader.compression != 0u ) {
		BMP_ERROR( "LoadBMP: only uncompressed BMP files supported (%s)", name );
	}

	switch ( bmpHeader.bitsPerPixel ) {
	case 8:
	case 16:
	case 24:
	case 32:
		break;
	default:
		BMP_ERROR( "LoadBMP: illegal pixel_size '%hu' in file '%s'",
			bmpHeader.bitsPerPixel, name );
	}

	if ( bmpHeader.width <= 0 || bmpHeader.height == 0 ||
		bmpHeader.height == INT_MIN ) {
		BMP_ERROR( "LoadBMP: %s has an invalid image size", name );
	}
	columns = bmpHeader.width;
	topDown = bmpHeader.height < 0 ? qtrue : qfalse;
	rows = topDown ? (uint32_t)( -( bmpHeader.height + 1 ) ) + 1u :
		(uint32_t)bmpHeader.height;

	/* ri.Malloc takes an int, so reject dimensions it cannot represent. */
	if ( (size_t)columns > (size_t)INT_MAX / 4u / (size_t)rows ) {
		BMP_ERROR( "LoadBMP: %s has an invalid image size", name );
	}
	outputBytes = (size_t)columns * (size_t)rows * 4u;
	bytesPerPixel = (size_t)bmpHeader.bitsPerPixel / 8u;
	packedRowBytes = (size_t)columns * bytesPerPixel;
	paddedRowBytes = ( packedRowBytes + 3u ) & ~(size_t)3u;
	packedDataBytes = packedRowBytes * (size_t)rows;
	paddedDataBytes = paddedRowBytes * (size_t)rows;
	availableDataBytes = fileBytes - (size_t)bmpHeader.bitmapDataOffset;

	/*
	 * Windows BMP scanlines are DWORD-aligned.  Retain the inherited loader's
	 * tightly-packed extension only when the header or exact file extent makes
	 * that intent unambiguous, so older mod assets keep working.
	 */
	if ( paddedRowBytes == packedRowBytes ) {
		sourceRowBytes = packedRowBytes;
	} else if ( availableDataBytes == paddedDataBytes ) {
		sourceRowBytes = paddedRowBytes;
	} else if ( availableDataBytes == packedDataBytes ) {
		sourceRowBytes = packedRowBytes;
	} else if ( bmpHeader.bitmapDataSize == paddedDataBytes &&
		availableDataBytes >= paddedDataBytes ) {
		sourceRowBytes = paddedRowBytes;
	} else if ( bmpHeader.bitmapDataSize == packedDataBytes &&
		availableDataBytes >= packedDataBytes ) {
		sourceRowBytes = packedRowBytes;
	} else if ( availableDataBytes >= paddedDataBytes ) {
		sourceRowBytes = paddedRowBytes;
	} else {
		BMP_ERROR( "LoadBMP: file truncated (%s)", name );
	}

	if ( bmpHeader.bitsPerPixel == 8u ) {
		paletteEntries = bmpHeader.colors ? (size_t)bmpHeader.colors : 256u;
		if ( paletteEntries > 256u ) {
			BMP_ERROR( "LoadBMP: invalid palette size (%s)", name );
		}
		paletteBytes = paletteEntries * 4u;
		if ( paletteBytes > (size_t)bmpHeader.bitmapDataOffset - metadataEnd ) {
			BMP_ERROR( "LoadBMP: truncated palette (%s)", name );
		}
		palette = buffer.b + metadataEnd;
	}

	bitmapData = buffer.b + (size_t)bmpHeader.bitmapDataOffset;
	if ( bmpHeader.bitsPerPixel == 8u ) {
		for ( sourceRow = 0; sourceRow < rows; ++sourceRow ) {
			const byte *source = bitmapData + (size_t)sourceRow * sourceRowBytes;
			int column;

			for ( column = 0; column < columns; ++column ) {
				if ( (size_t)source[column] >= paletteEntries ) {
					BMP_ERROR( "LoadBMP: palette index out of range (%s)", name );
				}
			}
		}
	}

	bmpRGBA = ri.Malloc( (int)outputBytes );
	if ( !bmpRGBA ) {
		BMP_ERROR( "LoadBMP: out of memory loading %s", name );
	}

	for ( sourceRow = 0; sourceRow < rows; ++sourceRow ) {
		const uint32_t destinationRow = topDown ? sourceRow : rows - 1u - sourceRow;
		const byte *source = bitmapData + (size_t)sourceRow * sourceRowBytes;
		byte *destination = bmpRGBA + (size_t)destinationRow * (size_t)columns * 4u;
		int column;

		for ( column = 0; column < columns; ++column ) {
			unsigned red;
			unsigned green;
			unsigned blue;
			unsigned alpha;
			unsigned shortPixel;

			switch ( bmpHeader.bitsPerPixel ) {
			case 8: {
				const size_t paletteOffset = (size_t)( *source++ ) * 4u;
				*destination++ = palette[paletteOffset + 2u];
				*destination++ = palette[paletteOffset + 1u];
				*destination++ = palette[paletteOffset];
				*destination++ = 0xff;
				break;
			}
			case 16:
				shortPixel = (unsigned)source[0] | ( (unsigned)source[1] << 8u );
				source += 2;
				*destination++ = (byte)( ( shortPixel & ( 31u << 10u ) ) >> 7u );
				*destination++ = (byte)( ( shortPixel & ( 31u << 5u ) ) >> 2u );
				*destination++ = (byte)( ( shortPixel & 31u ) << 3u );
				*destination++ = 0xff;
				break;
			case 24:
				blue = *source++;
				green = *source++;
				red = *source++;
				*destination++ = (byte)red;
				*destination++ = (byte)green;
				*destination++ = (byte)blue;
				*destination++ = 0xff;
				break;
			case 32:
				blue = *source++;
				green = *source++;
				red = *source++;
				alpha = *source++;
				*destination++ = (byte)red;
				*destination++ = (byte)green;
				*destination++ = (byte)blue;
				*destination++ = (byte)alpha;
				break;
			}
		}
	}

	if ( width ) {
		*width = columns;
	}
	if ( height ) {
		*height = (int)rows;
	}
	*pic = bmpRGBA;
	ri.FS_FreeFile( buffer.v );

#undef BMP_ERROR
}
