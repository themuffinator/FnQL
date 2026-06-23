/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2005 Stuart Dalton (badcdev@gmail.com)

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

extern "C" {
#include "../../client.h"
#include "snd_codec.h"
}

#include "../../client_cpp.h"

#include <array>

using fnql::FileRead;
using fnql::FileReadObject;
using fnql::OpenFileRead;
using fnql::ScopedFileHandle;
using fnql::ScopedTempMemory;

namespace {

/*
=================
FGetLittleLong
=================
*/
int FGetLittleLong( fileHandle_t f ) {
	int v;

	FileReadObject( f, v );

	return LittleLong( v );
}

/*
=================
FGetLittleShort
=================
*/
short FGetLittleShort( fileHandle_t f ) {
	short v;

	FileReadObject( f, v );

	return LittleShort( v );
}

/*
=================
S_ReadChunkInfo
=================
*/
int S_ReadChunkInfo( fileHandle_t f, char *name ) {
	name[4] = '\0';

	const int r = FileRead( f, name, 4 );
	if ( r != 4 ) {
		return -1;
	}

	const int len = FGetLittleLong( f );
	if ( len < 0 ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: Negative chunk length\n" );
		return -1;
	}

	return len;
}

/*
=================
S_FindRIFFChunk

Returns the length of the data in the chunk, or -1 if not found
=================
*/
int S_FindRIFFChunk( fileHandle_t f, const char *chunk ) {
	std::array<char, 5> name;
	int len;

	while ( ( len = S_ReadChunkInfo( f, name.data() ) ) >= 0 ) {
		if ( !Q_strncmp( name.data(), chunk, 4 ) ) {
			return len;
		}

		len = PAD( len, 2 );

		FS_Seek( f, len, FS_SEEK_CUR );
	}

	return -1;
}

/*
=================
S_ByteSwapRawSamples

If raw data has been loaded in little endian binary form, this must be done.
If raw data was calculated, as with ADPCM, this should not be called.
=================
*/
void S_ByteSwapRawSamples( int samples, int width, int s_channels, byte *data ) {
	if ( width != 2 ) {
		return;
	}
	if ( LittleShort( 256 ) == 256 ) {
		return;
	}

	if ( s_channels == 2 ) {
		samples <<= 1;
	}

	auto *sampleData = reinterpret_cast<short *>( data );
	for ( int i = 0; i < samples; i++ ) {
		sampleData[i] = LittleShort( sampleData[i] );
	}
}

/*
=================
S_ReadRIFFHeader
=================
*/
qboolean S_ReadRIFFHeader( fileHandle_t file, snd_info_t *info ) {
	std::array<char, 16> dump{};

	FileRead( file, dump.data(), 12 );

	int fmtlen = S_FindRIFFChunk( file, "fmt " );
	if ( fmtlen < 0 ) {
		Com_Printf( S_COLOR_RED "ERROR: Couldn't find \"fmt\" chunk\n" );
		return qfalse;
	}

	FGetLittleShort( file ); // wav_format
	info->channels = FGetLittleShort( file );
	info->rate = FGetLittleLong( file );
	FGetLittleLong( file );
	FGetLittleShort( file );
	const int bits = FGetLittleShort( file );

	if ( bits < 8 ) {
		Com_Printf( S_COLOR_RED "ERROR: Less than 8 bit sound is not supported\n" );
		return qfalse;
	}

	info->width = bits / 8;
	info->dataofs = 0;

	if ( fmtlen > 16 ) {
		fmtlen -= 16;
		FS_Seek( file, fmtlen, FS_SEEK_CUR );
	}

	info->size = S_FindRIFFChunk( file, "data" );
	if ( info->size < 0 ) {
		Com_Printf( S_COLOR_RED "ERROR: Couldn't find \"data\" chunk\n" );
		return qfalse;
	}
	info->samples = ( info->size / info->width ) / info->channels;

	return qtrue;
}

} // namespace

// WAV codec
snd_codec_t wav_codec = {
	"wav",
	S_WAV_CodecLoad,
	S_WAV_CodecOpenStream,
	S_WAV_CodecReadStream,
	S_WAV_CodecCloseStream,
	nullptr
};

/*
=================
S_WAV_CodecLoad
=================
*/
extern "C" void *S_WAV_CodecLoad( const char *filename, snd_info_t *info ) {
	ScopedFileHandle file;
	OpenFileRead( filename, file, qtrue );
	if ( !file ) {
		return nullptr;
	}

	if ( !S_ReadRIFFHeader( file.get(), info ) ) {
		Com_Printf( S_COLOR_RED "ERROR: Incorrect/unsupported format in \"%s\"\n",
			filename );
		return nullptr;
	}

	auto bufferStorage = ScopedTempMemory::Allocate( info->size );
	byte *buffer = bufferStorage.as<byte>();
	if ( !buffer ) {
		Com_Printf( S_COLOR_RED "ERROR: Out of memory reading \"%s\"\n",
			filename );
		return nullptr;
	}

	FileRead( file.get(), buffer, info->size );
	S_ByteSwapRawSamples( info->samples, info->width, info->channels, buffer );

	return bufferStorage.release();
}

/*
=================
S_WAV_CodecOpenStream
=================
*/
extern "C" snd_stream_t *S_WAV_CodecOpenStream( const char *filename ) {
	snd_stream_t *rv = S_CodecUtilOpen( filename, &wav_codec );
	if ( !rv ) {
		return nullptr;
	}

	if ( !S_ReadRIFFHeader( rv->file, &rv->info ) ) {
		S_CodecUtilClose( &rv );
		return nullptr;
	}

	return rv;
}

/*
=================
S_WAV_CodecCloseStream
=================
*/
extern "C" void S_WAV_CodecCloseStream( snd_stream_t *stream ) {
	S_CodecUtilClose( &stream );
}

/*
=================
S_WAV_CodecReadStream
=================
*/
extern "C" int S_WAV_CodecReadStream( snd_stream_t *stream, int bytes, void *buffer ) {
	const int remaining = stream->info.size - stream->pos;
	if ( remaining <= 0 ) {
		return 0;
	}
	if ( bytes > remaining ) {
		bytes = remaining;
	}

	stream->pos += bytes;
	const int samples = ( bytes / stream->info.width ) / stream->info.channels;
	FileRead( stream->file, buffer, bytes );
	S_ByteSwapRawSamples( samples, stream->info.width, stream->info.channels, static_cast<byte *>( buffer ) );
	return bytes;
}
