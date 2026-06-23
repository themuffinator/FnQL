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

using fnql::CloseFile;
using fnql::AllocateZoneMemory;
using fnql::OpenFileRead;
using fnql::ScopedFileHandle;
using fnql::ScopedZoneMemory;

namespace {

snd_codec_t *codecs = nullptr;

void S_CodecRegister( snd_codec_t *codec ) {
	codec->next = codecs;
	codecs = codec;
}

/*
=================
S_CodecGetSound

Opens/loads a sound, tries codec based on the sound's file extension
then tries all supported codecs.
=================
*/
void *S_CodecGetSound( const char *filename, snd_info_t *info ) {
	snd_codec_t *codec;
	snd_codec_t *orgCodec = nullptr;
	bool orgNameFailed = false;
	std::array<char, MAX_QPATH> localName;
	const char *ext;
	std::array<char, MAX_QPATH> altName;
	void *rtn = nullptr;

	Q_strncpyz( localName.data(), filename, static_cast<int>( localName.size() ) );

	ext = COM_GetExtension( localName.data() );

	if ( *ext ) {
		// Look for the correct loader and use it.
		for ( codec = codecs; codec; codec = codec->next ) {
			if ( !Q_stricmp( ext, codec->ext ) ) {
				if ( info ) {
					rtn = codec->load( localName.data(), info );
				} else {
					rtn = codec->open( localName.data() );
				}
				break;
			}
		}

		if ( codec ) {
			if ( !rtn ) {
				// Loader failed, most likely because the file is not present;
				// try again without the extension.
				orgNameFailed = true;
				orgCodec = codec;
				COM_StripExtension( filename, localName.data(), static_cast<int>( localName.size() ) );
			} else {
				return rtn;
			}
		}
	}

	// Try all supported codecs.
	for ( codec = codecs; codec; codec = codec->next ) {
		if ( codec == orgCodec ) {
			continue;
		}

		Com_sprintf( altName.data(), static_cast<int>( altName.size() ), "%s.%s", localName.data(), codec->ext );

		if ( info ) {
			rtn = codec->load( altName.data(), info );
		} else {
			rtn = codec->open( altName.data() );
		}

		if ( rtn ) {
			if ( orgNameFailed ) {
				Com_DPrintf( S_COLOR_YELLOW "WARNING: %s not present, using %s instead\n",
					filename, altName.data() );
			}

			return rtn;
		}
	}

	Com_DPrintf( S_COLOR_YELLOW "WARNING: Failed to %s sound %s!\n", info ? "load" : "open", filename );

	return nullptr;
}

} // namespace

/*
=================
S_CodecInit
=================
*/
extern "C" void S_CodecInit( void ) {
	codecs = nullptr;

#ifdef USE_OGG_VORBIS
	S_CodecRegister( &ogg_codec );
#endif

	// Register wav last so it is tried first when a file extension was not found.
	S_CodecRegister( &wav_codec );
}

/*
=================
S_CodecShutdown
=================
*/
extern "C" void S_CodecShutdown( void ) {
	codecs = nullptr;
}

/*
=================
S_CodecLoad
=================
*/
extern "C" void *S_CodecLoad( const char *filename, snd_info_t *info ) {
	return S_CodecGetSound( filename, info );
}

/*
=================
S_CodecOpenStream
=================
*/
extern "C" snd_stream_t *S_CodecOpenStream( const char *filename ) {
	return static_cast<snd_stream_t *>( S_CodecGetSound( filename, nullptr ) );
}

/*
=================
S_CodecCloseStream
=================
*/
extern "C" void S_CodecCloseStream( snd_stream_t *stream ) {
	stream->codec->close( stream );
}

/*
=================
S_CodecReadStream
=================
*/
extern "C" int S_CodecReadStream( snd_stream_t *stream, int bytes, void *buffer ) {
	return stream->codec->read( stream, bytes, buffer );
}

//=======================================================================
// Util functions (used by codecs)

/*
=================
S_CodecUtilOpen
=================
*/
extern "C" snd_stream_t *S_CodecUtilOpen( const char *filename, snd_codec_t *codec ) {
	ScopedFileHandle file;
	const int length = OpenFileRead( filename, file, qtrue );
	if ( !file ) {
		Com_DPrintf( "Can't read sound file %s\n", filename );
		return nullptr;
	}

	ScopedZoneMemory streamStorage = AllocateZoneMemory( sizeof( snd_stream_t ), "snd_stream_t", __FILE__, __LINE__ );
	auto *stream = streamStorage.as<snd_stream_t>();
	if ( !stream ) {
		return nullptr;
	}

	stream->codec = codec;
	stream->file = file.release();
	stream->length = length;
	return static_cast<snd_stream_t *>( streamStorage.release() );
}

/*
=================
S_CodecUtilClose
=================
*/
extern "C" void S_CodecUtilClose( snd_stream_t **stream ) {
	CloseFile( ( *stream )->file );
	Z_Free( *stream );
	*stream = nullptr;
}
