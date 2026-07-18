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
#include <cstring>

using fnql::AllocateZoneMemory;
using fnql::FileRead;
using fnql::FileReadObject;
using fnql::OpenFileRead;
using fnql::ScopedFileHandle;
using fnql::ScopedTempMemory;
using fnql::ScopedZoneMemory;

namespace {

// The rest of the sound pipeline only consumes 8/16-bit integer PCM, so
// 24-bit integer and 32-bit float sources are converted to 16-bit here.
constexpr int kWavFormatPCM = 0x0001;
constexpr int kWavFormatIEEEFloat = 0x0003;
constexpr int kWavFormatExtensible = 0xFFFE;

struct WavSourceFormat {
	int format = kWavFormatPCM;		// resolved format tag (extensible unwrapped)
	int width = 0;					// bytes per sample as stored in the file
	bool needsConversion = false;	// decoded to 16-bit PCM on read
};

// Stream-side conversion state stashed in snd_stream_t::ptr; the field stays
// null for sources that can be read straight through.
struct WavStreamState {
	int format;
	int width;
};

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

int FGetLittleUnsignedShort( fileHandle_t f ) {
	return static_cast<int>( static_cast<unsigned short>( FGetLittleShort( f ) ) );
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

short ConvertWavSampleToPCM16( const byte *src, const WavSourceFormat &source ) {
	if ( source.format == kWavFormatIEEEFloat ) {
		unsigned int bits = static_cast<unsigned int>( src[0] ) |
			( static_cast<unsigned int>( src[1] ) << 8 ) |
			( static_cast<unsigned int>( src[2] ) << 16 ) |
			( static_cast<unsigned int>( src[3] ) << 24 );
		float value;
		std::memcpy( &value, &bits, sizeof( value ) );
		if ( !( value >= -1.0f ) ) {	// also catches NaN
			value = ( value < 0.0f ) ? -1.0f : 0.0f;
		} else if ( value > 1.0f ) {
			value = 1.0f;
		}
		return static_cast<short>( value * 32767.0f );
	}

	// 24-bit little-endian signed PCM: keep the top 16 bits.
	int value = static_cast<int>( src[0] ) |
		( static_cast<int>( src[1] ) << 8 ) |
		( static_cast<int>( src[2] ) << 16 );
	if ( value & 0x800000 ) {
		value -= 0x1000000;
	}
	return static_cast<short>( value >> 8 );
}

void ConvertWavBlockToPCM16( short *dst, const byte *src, int sampleCount, const WavSourceFormat &source ) {
	for ( int i = 0; i < sampleCount; ++i ) {
		dst[i] = ConvertWavSampleToPCM16( src + static_cast<size_t>( i ) * static_cast<size_t>( source.width ), source );
	}
}

/*
=================
S_ReadRIFFHeader

Fills info with the decoded (8/16-bit PCM) stream description and source
with the on-disk sample format.
=================
*/
qboolean S_ReadRIFFHeader( fileHandle_t file, snd_info_t *info, WavSourceFormat *source ) {
	std::array<char, 16> dump{};

	FileRead( file, dump.data(), 12 );

	const int fmtlen = S_FindRIFFChunk( file, "fmt " );
	if ( fmtlen < 16 ) {
		Com_Printf( S_COLOR_RED "ERROR: Couldn't find \"fmt\" chunk\n" );
		return qfalse;
	}

	int format = FGetLittleUnsignedShort( file );
	info->channels = FGetLittleShort( file );
	info->rate = FGetLittleLong( file );
	FGetLittleLong( file );		// byte rate
	FGetLittleShort( file );	// block align
	const int bits = FGetLittleShort( file );
	int consumed = 16;

	if ( format == kWavFormatExtensible && fmtlen >= 40 ) {
		FGetLittleShort( file );	// cbSize
		FGetLittleShort( file );	// valid bits per sample
		FGetLittleLong( file );		// channel mask
		// The first two bytes of the SubFormat GUID hold the real format tag.
		format = FGetLittleUnsignedShort( file );
		std::array<char, 14> guidRemainder;
		FileRead( file, guidRemainder.data(), static_cast<int>( guidRemainder.size() ) );
		consumed = 40;
	}

	int remaining = fmtlen - consumed;
	if ( fmtlen & 1 ) {
		++remaining;
	}
	if ( remaining > 0 ) {
		FS_Seek( file, remaining, FS_SEEK_CUR );
	}

	if ( format == kWavFormatPCM ) {
		if ( bits != 8 && bits != 16 && bits != 24 ) {
			Com_Printf( S_COLOR_RED "ERROR: %d bit PCM sound is not supported\n", bits );
			return qfalse;
		}
	} else if ( format == kWavFormatIEEEFloat ) {
		if ( bits != 32 ) {
			Com_Printf( S_COLOR_RED "ERROR: %d bit float sound is not supported\n", bits );
			return qfalse;
		}
	} else {
		Com_Printf( S_COLOR_RED "ERROR: unsupported WAV format tag %d (only PCM and IEEE float are supported)\n", format );
		return qfalse;
	}

	if ( info->channels <= 0 || info->rate <= 0 ) {
		Com_Printf( S_COLOR_RED "ERROR: invalid WAV channel count or sample rate\n" );
		return qfalse;
	}

	source->format = format;
	source->width = bits / 8;
	source->needsConversion = ( format == kWavFormatIEEEFloat || bits == 24 );

	const int dataSize = S_FindRIFFChunk( file, "data" );
	if ( dataSize < 0 ) {
		Com_Printf( S_COLOR_RED "ERROR: Couldn't find \"data\" chunk\n" );
		return qfalse;
	}

	const int frameBytes = source->width * info->channels;
	info->samples = dataSize / frameBytes;
	info->width = source->needsConversion ? 2 : source->width;
	info->size = info->samples * info->width * info->channels;
	info->dataofs = 0;

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

	WavSourceFormat source;
	if ( !S_ReadRIFFHeader( file.get(), info, &source ) ) {
		Com_Printf( S_COLOR_RED "ERROR: Incorrect/unsupported format in \"%s\"\n",
			filename );
		return nullptr;
	}

	// Temp hunk memory is LIFO: allocate the returned buffer first so the
	// source scratch buffer can be released before this function returns.
	auto bufferStorage = ScopedTempMemory::Allocate( info->size );
	byte *buffer = bufferStorage.as<byte>();
	if ( !buffer ) {
		Com_Printf( S_COLOR_RED "ERROR: Out of memory reading \"%s\"\n",
			filename );
		return nullptr;
	}

	if ( !source.needsConversion ) {
		FileRead( file.get(), buffer, info->size );
		S_ByteSwapRawSamples( info->samples, info->width, info->channels, buffer );
		return bufferStorage.release();
	}

	const int sampleCount = info->samples * info->channels;
	const int sourceBytes = sampleCount * source.width;
	auto sourceStorage = ScopedTempMemory::Allocate( sourceBytes );
	byte *sourceBuffer = sourceStorage.as<byte>();
	if ( !sourceBuffer ) {
		Com_Printf( S_COLOR_RED "ERROR: Out of memory reading \"%s\"\n",
			filename );
		return nullptr;
	}

	const int bytesRead = FileRead( file.get(), sourceBuffer, sourceBytes );
	const int samplesRead = bytesRead / source.width;
	ConvertWavBlockToPCM16( reinterpret_cast<short *>( buffer ), sourceBuffer, samplesRead, source );
	if ( samplesRead < sampleCount ) {
		std::memset( buffer + static_cast<size_t>( samplesRead ) * sizeof( short ), 0,
			static_cast<size_t>( sampleCount - samplesRead ) * sizeof( short ) );
	}

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

	WavSourceFormat source;
	if ( !S_ReadRIFFHeader( rv->file, &rv->info, &source ) ) {
		S_CodecUtilClose( &rv );
		return nullptr;
	}

	if ( source.needsConversion ) {
		ScopedZoneMemory stateStorage = AllocateZoneMemory( sizeof( WavStreamState ), "WavStreamState", __FILE__, __LINE__ );
		auto *state = stateStorage.as<WavStreamState>();
		if ( !state ) {
			S_CodecUtilClose( &rv );
			return nullptr;
		}
		state->format = source.format;
		state->width = source.width;
		rv->ptr = stateStorage.release();
	}

	return rv;
}

/*
=================
S_WAV_CodecCloseStream
=================
*/
extern "C" void S_WAV_CodecCloseStream( snd_stream_t *stream ) {
	if ( stream->ptr != nullptr ) {
		Z_Free( stream->ptr );
		stream->ptr = nullptr;
	}
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

	const auto *state = static_cast<const WavStreamState *>( stream->ptr );
	if ( state == nullptr ) {
		stream->pos += bytes;
		const int samples = ( bytes / stream->info.width ) / stream->info.channels;
		FileRead( stream->file, buffer, bytes );
		S_ByteSwapRawSamples( samples, stream->info.width, stream->info.channels, static_cast<byte *>( buffer ) );
		return bytes;
	}

	// Decode 24-bit or float source samples to 16-bit through a bounded
	// scratch buffer; stream->pos tracks decoded bytes.
	WavSourceFormat source;
	source.format = state->format;
	source.width = state->width;
	source.needsConversion = true;

	std::array<byte, 12288> sourceChunk;
	const int requestedSamples = bytes / static_cast<int>( sizeof( short ) );
	auto *out = static_cast<short *>( buffer );
	int decodedSamples = 0;

	while ( decodedSamples < requestedSamples ) {
		const int samplesThisPass = ( std::min )( requestedSamples - decodedSamples,
			static_cast<int>( sourceChunk.size() ) / source.width );
		const int sourceBytes = samplesThisPass * source.width;
		const int bytesRead = FileRead( stream->file, sourceChunk.data(), sourceBytes );
		const int samplesRead = bytesRead / source.width;
		if ( samplesRead <= 0 ) {
			break;
		}
		ConvertWavBlockToPCM16( out + decodedSamples, sourceChunk.data(), samplesRead, source );
		decodedSamples += samplesRead;
		if ( samplesRead < samplesThisPass ) {
			break;
		}
	}

	const int decodedBytes = decodedSamples * static_cast<int>( sizeof( short ) );
	stream->pos += decodedBytes;
	return decodedBytes;
}
