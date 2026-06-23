/*
===========================================================================
Copyright (C) 2005-2006 Tim Angus

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
#include "client.h"
#include "audio/snd_local.h"
}

#include "client_cpp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <cstring>
#include <utility>

using fnql::FileRead;
using fnql::FileWrite;
using fnql::OpenHomeFileRead;
using fnql::ScopedFileHandle;
using fnql::ScopedZoneMemory;
using fnql::AllocateZoneMemory;

namespace {

constexpr char kIndexFileExtension[] = ".index.dat";
constexpr int kMaxRiffChunks = 16;
constexpr int kMaxAviBuffer = 2048;
constexpr int kMaxPackLen = 16;
constexpr unsigned int kAviSegmentSize = UINT_MAX;
constexpr int kPcmBufferSize = 44100;

struct audioFormat_t {
  int rate;
  int format;
  int channels;
  int bits;

  int sampleSize;
  unsigned int totalBytes;
};

struct aviFileData_t {
  bool          fileOpen;
  bool          pipe;
  ScopedFileHandle f;
  std::array<char, MAX_QPATH> fileName;
  unsigned int  fileSize;
  unsigned int  moviOffset;
  unsigned int  moviSize;

  ScopedFileHandle idxF;
  int           numIndices;

  int           frameRate;
  int           framePeriod;
  int           width, height;
  int           numVideoFrames;
  int           maxRecordSize;
  bool          motionJpeg;

  bool          audio;
  audioFormat_t a;
  int           numAudioFrames;
  int           audioFrameSize;

  std::array<int, kMaxRiffChunks> chunkStack;
  int           chunkStackTop;

  ScopedZoneMemory cBuffer, eBuffer;
};

static aviFileData_t afd;

static void CL_ResetAVIFileData( ScopedZoneMemory cBuffer = {}, ScopedZoneMemory eBuffer = {} )
{
  afd = {};
  afd.cBuffer = std::move( cBuffer );
  afd.eBuffer = std::move( eBuffer );
}

static std::array<byte, kMaxAviBuffer> buffer;
static int  bufIndex;

static std::array<byte, kPcmBufferSize> pcmCaptureBuffer;
static int  bytesInBuffer = 0;

} // namespace


/*
===============
SafeFS_Write
===============
*/
static ID_INLINE void SafeFS_Write( const void *buf, int len, fileHandle_t f )
{
  if ( FileWrite( f, buf, len ) < len )
		Com_Error( ERR_DROP, "Failed to write avi file" );
}


/*
===============
WRITE_STRING
===============
*/
static ID_INLINE void WRITE_STRING( const char *s )
{
  const std::size_t len = std::strlen( s );
  std::copy_n( reinterpret_cast<const byte *>( s ), len, buffer.data() + bufIndex );
  bufIndex += static_cast<int>( len );
}


/*
===============
WRITE_4BYTES
===============
*/
static ID_INLINE void WRITE_4BYTES( int x )
{
  buffer[ bufIndex + 0 ] = static_cast<byte>( ( x >>  0 ) & 0xFF );
  buffer[ bufIndex + 1 ] = static_cast<byte>( ( x >>  8 ) & 0xFF );
  buffer[ bufIndex + 2 ] = static_cast<byte>( ( x >> 16 ) & 0xFF );
  buffer[ bufIndex + 3 ] = static_cast<byte>( ( x >> 24 ) & 0xFF );
  bufIndex += 4;
}


/*
===============
WRITE_2BYTES
===============
*/
static ID_INLINE void WRITE_2BYTES( int x )
{
  buffer[ bufIndex + 0 ] = static_cast<byte>( ( x >>  0 ) & 0xFF );
  buffer[ bufIndex + 1 ] = static_cast<byte>( ( x >>  8 ) & 0xFF );
  bufIndex += 2;
}


/*
===============
START_CHUNK
===============
*/
static ID_INLINE void START_CHUNK( const char *s )
{
	if( afd.chunkStackTop >= kMaxRiffChunks )
	{
		Com_Error( ERR_DROP, "ERROR: Top of chunkstack breached" );
	} 
	else 
	{
		afd.chunkStack[ afd.chunkStackTop ] = bufIndex;
		afd.chunkStackTop++;
		WRITE_STRING( s );
		WRITE_4BYTES( 0 );
	}
}


/*
===============
END_CHUNK
===============
*/
static ID_INLINE void END_CHUNK( void )
{
	int endIndex = bufIndex;

	if( afd.chunkStackTop <= 0 )
	{
		Com_Error( ERR_DROP, "ERROR: Bottom of chunkstack breached" );
	} 
	else
	{
		afd.chunkStackTop--;
		bufIndex = afd.chunkStack[ afd.chunkStackTop ];
		bufIndex += 4;
		WRITE_4BYTES( endIndex - bufIndex - 4 );
		bufIndex = endIndex;
		bufIndex = PAD( bufIndex, 2 );
	}
}


/*
===============
CL_WriteAVIHeader
===============
*/
static void CL_WriteAVIHeader( void )
{
  bufIndex = 0;
  afd.chunkStackTop = 0;

  START_CHUNK( "RIFF" );
  {
    WRITE_STRING( "AVI " );
    {
      START_CHUNK( "LIST" );
      {
        WRITE_STRING( "hdrl" );
        WRITE_STRING( "avih" );
        WRITE_4BYTES( 56 );                     //"avih" "chunk" size
        WRITE_4BYTES( afd.framePeriod );        //dwMicroSecPerFrame
        WRITE_4BYTES( afd.maxRecordSize * afd.frameRate ); //dwMaxBytesPerSec
        WRITE_4BYTES( 0 );                      //dwReserved1
        if ( afd.pipe )
          WRITE_4BYTES( 0x100 );                //dwFlags bits IS_INTERLEAVED=0x100
        else
          WRITE_4BYTES( 0x110 );                //dwFlags bits HAS_INDEX=0x10 and IS_INTERLEAVED=0x100
        WRITE_4BYTES( afd.numVideoFrames );     //dwTotalFrames
        WRITE_4BYTES( 0 );                      //dwInitialFrame

        if( afd.audio )                         //dwStreams
          WRITE_4BYTES( 2 );
        else
          WRITE_4BYTES( 1 );

        WRITE_4BYTES( afd.maxRecordSize );      //dwSuggestedBufferSize
        WRITE_4BYTES( afd.width );              //dwWidth
        WRITE_4BYTES( afd.height );             //dwHeight
        WRITE_4BYTES( 0 );                      //dwReserved[ 0 ]
        WRITE_4BYTES( 0 );                      //dwReserved[ 1 ]
        WRITE_4BYTES( 0 );                      //dwReserved[ 2 ]
        WRITE_4BYTES( 0 );                      //dwReserved[ 3 ]

        START_CHUNK( "LIST" );
        {
          WRITE_STRING( "strl" );
          WRITE_STRING( "strh" );
          WRITE_4BYTES( 56 );                   //"strh" "chunk" size
          WRITE_STRING( "vids" );

          if ( afd.motionJpeg && !afd.pipe )
            WRITE_STRING( "MJPG" );
          else
            WRITE_4BYTES( 0 );                  // BI_RGB

          WRITE_4BYTES( 0 );                    //dwFlags
          WRITE_4BYTES( 0 );                    //dwPriority
          WRITE_4BYTES( 0 );                    //dwInitialFrame

          WRITE_4BYTES( 1 );                    //dwTimescale
          WRITE_4BYTES( afd.frameRate );        //dwDataRate
          WRITE_4BYTES( 0 );                    //dwStartTime
          WRITE_4BYTES( afd.numVideoFrames );   //dwDataLength

          WRITE_4BYTES( afd.maxRecordSize );    //dwSuggestedBufferSize
          WRITE_4BYTES( -1 );                   //dwQuality
          WRITE_4BYTES( 0 );                    //dwSampleSize
          WRITE_2BYTES( 0 );                    //rcFrame
          WRITE_2BYTES( 0 );                    //rcFrame
          WRITE_2BYTES( afd.width );            //rcFrame
          WRITE_2BYTES( afd.height );           //rcFrame

          WRITE_STRING( "strf" );
          WRITE_4BYTES( 40 );                   //"strf" "chunk" size
          WRITE_4BYTES( 40 );                   //biSize
          WRITE_4BYTES( afd.width );            //biWidth
          WRITE_4BYTES( afd.height );           //biHeight
          WRITE_2BYTES( 1 );                    //biPlanes
          WRITE_2BYTES( 24 );                   //biBitCount

          if( afd.motionJpeg && !afd.pipe )     //biCompression
          {
            WRITE_STRING( "MJPG" );
            WRITE_4BYTES( afd.width *
              afd.height );                     //biSizeImage
          }
          else
          {
            WRITE_4BYTES( 0 );                  // BI_RGB
            WRITE_4BYTES( afd.width *
                afd.height * 3 );               //biSizeImage
          }

          WRITE_4BYTES( 0 );                    //biXPelsPetMeter
          WRITE_4BYTES( 0 );                    //biYPelsPetMeter
          WRITE_4BYTES( 0 );                    //biClrUsed
          WRITE_4BYTES( 0 );                    //biClrImportant
        }
        END_CHUNK( );

        if( afd.audio )
        {
          START_CHUNK( "LIST" );
          {
            WRITE_STRING( "strl" );
            WRITE_STRING( "strh" );
            WRITE_4BYTES( 56 );                 //"strh" "chunk" size
            WRITE_STRING( "auds" );
            WRITE_4BYTES( 0 );                  //FCC
            WRITE_4BYTES( 0 );                  //dwFlags
            WRITE_4BYTES( 0 );                  //dwPriority
            WRITE_4BYTES( 0 );                  //dwInitialFrame

            WRITE_4BYTES( afd.a.sampleSize );   //dwTimescale
            WRITE_4BYTES( afd.a.sampleSize *
                afd.a.rate );                   //dwDataRate
            WRITE_4BYTES( 0 );                  //dwStartTime
            WRITE_4BYTES( afd.a.totalBytes /
                afd.a.sampleSize );             //dwDataLength

            WRITE_4BYTES( 0 );                  //dwSuggestedBufferSize
            WRITE_4BYTES( -1 );                 //dwQuality
            WRITE_4BYTES( afd.a.sampleSize );   //dwSampleSize
            WRITE_2BYTES( 0 );                  //rcFrame
            WRITE_2BYTES( 0 );                  //rcFrame
            WRITE_2BYTES( 0 );                  //rcFrame
            WRITE_2BYTES( 0 );                  //rcFrame

            WRITE_STRING( "strf" );
            WRITE_4BYTES( 18 );                 //"strf" "chunk" size
            WRITE_2BYTES( afd.a.format );       //wFormatTag
            WRITE_2BYTES( afd.a.channels );     //nChannels
            WRITE_4BYTES( afd.a.rate );         //nSamplesPerSec
            WRITE_4BYTES( afd.a.sampleSize *
                afd.a.rate );                   //nAvgBytesPerSec
            WRITE_2BYTES( afd.a.sampleSize );   //nBlockAlign
            WRITE_2BYTES( afd.a.bits );         //wBitsPerSample
            WRITE_2BYTES( 0 );                  //cbSize
          }
          END_CHUNK( );
        }
      }
      END_CHUNK( );

      afd.moviOffset = bufIndex;

      START_CHUNK( "LIST" );
      {
        WRITE_STRING( "movi" );
      }
    }
  }
}


static bool CL_ValidatePipeFormat( const char *s )
{
	while ( *s != '\0' ) 
	{
		if ( *s == '.' && *(s+1) == '.' && ( *(s+2) == '/' || *(s+2) == '\\' ) )
			return false;
		if ( *s == ':' && *(s+1) == ':' )
			return false;
		if ( *s == '>' || *s == '|' || *s == '&' )
			return false;
		s++;
	}
	return true;
}


/*
===============
CL_OpenAVIForWriting

Creates an AVI file and gets it into a state where
writing the actual data can begin
===============
*/
qboolean CL_OpenAVIForWriting( const char *fileName, qboolean pipe, qboolean reopen )
{
	if ( afd.fileOpen )
		return qfalse;

	if ( reopen )
	{
		// keep currently allocated buffers
		ScopedZoneMemory cBuffer = std::move( afd.cBuffer );
		ScopedZoneMemory eBuffer = std::move( afd.eBuffer );
		CL_ResetAVIFileData( std::move( cBuffer ), std::move( eBuffer ) );
	}
	else
	{
		CL_ResetAVIFileData();
	}

	if ( pipe )
	{
		std::array<char, MAX_OSPATH * 4> cmd;
		const char *cmd_fmt = "ffmpeg -f avi -i - -threads 0 -y %s \"%s\" 2> \"%s-log.txt\"";
		const char *ospath;

		if ( !CL_ValidatePipeFormat( cl_aviPipeFormat->string ) ) {
			Com_Printf( S_COLOR_YELLOW "Invalid pipe format: %s\n", cl_aviPipeFormat->string );
			return qfalse;
		}

		ospath = FS_BuildOSPath( Cvar_VariableString( "fs_homepath" ), "", fileName );
		Com_sprintf( cmd.data(), static_cast<int>( cmd.size() ), cmd_fmt, cl_aviPipeFormat->string, ospath, ospath );
		ScopedFileHandle pipeFile( FS_PipeOpenWrite( cmd.data(), fileName ) );
		if ( !pipeFile )
			return qfalse;
		afd.f = std::move( pipeFile );
	}
	else
	{
		ScopedFileHandle aviFile( FS_FOpenFileWrite( fileName ) );
		if ( !aviFile )
			return qfalse;

		ScopedFileHandle indexFile( FS_FOpenFileWrite( va( "%s%s", fileName, kIndexFileExtension ) ) );
		if ( !indexFile ) {
			return qfalse;
		}

		afd.f = std::move( aviFile );
		afd.idxF = std::move( indexFile );
	}

	Q_strncpyz( afd.fileName.data(), fileName, static_cast<int>( afd.fileName.size() ) );

	afd.frameRate = cl_aviFrameRate->integer;
	afd.framePeriod = static_cast<int>( 1000000.0 / afd.frameRate );

	afd.width = cls.captureWidth;
	afd.height = cls.captureHeight;

	if ( cl_aviMotionJpeg->integer && !pipe )
		afd.motionJpeg = true;
	else
		afd.motionJpeg = false;

	if ( !reopen )
	{
		// Buffers only need to store RGB pixels.
		// Allocate a bit more space for the capture buffer to account for possible
		// padding at the end of pixel lines, and padding for alignment
		afd.cBuffer = AllocateZoneMemory(
			( afd.width * afd.height * 4 ) + kMaxPackLen - 1,
			"avi capture rgba buffer",
			__FILE__,
			__LINE__ ); // allocate for RGBA storage
		// raw avi files have pixel lines start on 4-byte boundaries
		afd.eBuffer = AllocateZoneMemory(
			PAD( afd.width * 3, AVI_LINE_PADDING ) * afd.height,
			"avi encoded frame buffer",
			__FILE__,
			__LINE__ );
	}

	afd.a.rate = dma.speed;
	afd.a.format = dma.isfloat ? WAVE_FORMAT_IEEE_FLOAT : WAV_FORMAT_PCM;
	afd.a.channels = dma.channels;
	afd.a.bits = dma.samplebits;
	afd.a.sampleSize = (afd.a.bits * afd.a.channels) / 8;

	afd.audioFrameSize = static_cast<int>(
		std::ceil( static_cast<float>( afd.a.rate * afd.a.sampleSize ) / static_cast<float>( afd.frameRate ) ) );

	if ( Cvar_VariableIntegerValue( "s_initsound" ) == 0 )
	{
		afd.audio = false;
	}
	else
	{
		afd.audio = true;
	}

	// This doesn't write a real header, but allocates the
	// correct amount of space at the beginning of the file
	CL_WriteAVIHeader();

	if ( pipe )
	{
		afd.pipe = true;
		SafeFS_Write( buffer.data(), bufIndex, afd.f.get() );
		bufIndex = 0;
	}
	else
	{
		SafeFS_Write( buffer.data(), bufIndex, afd.f.get() );
		afd.fileSize = bufIndex;

		bufIndex = 0;
		START_CHUNK( "idx1" );
		SafeFS_Write( buffer.data(), bufIndex, afd.idxF.get() );

		afd.moviSize = 4; // For the "movi"
	}

	afd.fileOpen = true;

	return qtrue;
}


/*
===============
CL_CheckFileSize
===============
*/
static bool CL_CheckFileSize( int bytesToAdd )
{
	unsigned int newFileSize;

	if ( afd.pipe )
	{
		return false;
	}

	newFileSize =
		afd.fileSize +                // Current file size
		bytesToAdd +                  // What we want to add
		( afd.numIndices * 16 ) +     // The index
		4;                            // The index size

	// I assume all the operating systems
	// we target can handle a 2Gb file
	//if( newFileSize > INT_MAX )
	if ( newFileSize >= kAviSegmentSize || newFileSize < afd.fileSize )
	{
		// Close the current file...
		CL_CloseAVI( qtrue );

		// ...And open a new one
		CL_OpenAVIForWriting( va( "%s-%02d.avi", clc.videoName, ++clc.videoIndex ), qfalse, qtrue );

		return true;
	}

	return false;
}


/*
===============
CL_WriteAVIVideoFrame
===============
*/
void CL_WriteAVIVideoFrame( const byte *imageBuffer, int size )
{
	unsigned int chunkOffset;
	int		chunkSize = 8 + size;
	int		paddingSize = PADLEN( size, 2 );
	std::array<byte, 4> padding{};

	if ( !afd.fileOpen )
		return;

	// Chunk header + contents + padding
	CL_CheckFileSize( chunkSize + paddingSize );

	chunkOffset = afd.fileSize - afd.moviOffset - 8;

	bufIndex = 0;
	WRITE_STRING( "00dc" );
	WRITE_4BYTES( size );

	SafeFS_Write( buffer.data(), 8, afd.f.get() );
	SafeFS_Write( imageBuffer, size, afd.f.get() );
	SafeFS_Write( padding.data(), paddingSize, afd.f.get() );

	afd.numVideoFrames++;

	if ( size > afd.maxRecordSize )
		afd.maxRecordSize = size;

	if ( afd.pipe )
		return;

	afd.fileSize += ( chunkSize + paddingSize );
	afd.moviSize += ( chunkSize + paddingSize );

	// Index
	bufIndex = 0;
	WRITE_STRING( "00dc" );           //dwIdentifier
	WRITE_4BYTES( 0x00000010 );       //dwFlags (all frames are KeyFrames)
	WRITE_4BYTES( chunkOffset );      //dwOffset
	WRITE_4BYTES( size );             //dwLength
	SafeFS_Write( buffer.data(), 16, afd.idxF.get() );

	afd.numIndices++;
}


/*
===============
CL_FlushAudioBuffer
===============
*/
static void CL_FlushCaptureBuffer( void ) 
{
	unsigned int chunkOffset = afd.fileSize - afd.moviOffset - 8;
	int   chunkSize = 8 + bytesInBuffer;
	int   paddingSize = PADLEN( bytesInBuffer, 2 );
	std::array<byte, 4> padding{};

	if ( !bytesInBuffer )
		return;

	bufIndex = 0;
	WRITE_STRING( "01wb" );
	WRITE_4BYTES( bytesInBuffer );

	SafeFS_Write( buffer.data(), 8, afd.f.get() );
	SafeFS_Write( pcmCaptureBuffer.data(), bytesInBuffer, afd.f.get() );
	SafeFS_Write( padding.data(), paddingSize, afd.f.get() );

	afd.numAudioFrames++;

	if ( !afd.pipe )
	{
		afd.fileSize += ( chunkSize + paddingSize );
		afd.moviSize += ( chunkSize + paddingSize );
		afd.a.totalBytes += bytesInBuffer;
		// Index
		bufIndex = 0;
		WRITE_STRING( "01wb" );           //dwIdentifier
		WRITE_4BYTES( 0 );                //dwFlags
		WRITE_4BYTES( chunkOffset );      //dwOffset
		WRITE_4BYTES( bytesInBuffer );    //dwLength
		SafeFS_Write( buffer.data(), 16, afd.idxF.get() );
		afd.numIndices++;
	}

	bytesInBuffer = 0;
}


/*
===============
CL_WriteAVIAudioFrame
===============
*/
void CL_WriteAVIAudioFrame( const byte *pcmBuffer, int size )
{
	if ( !afd.audio )
		return;

	if ( !afd.fileOpen )
		return;

	// Chunk header + contents + padding
	CL_CheckFileSize( 8 + bytesInBuffer + size + 2 );

	if ( bytesInBuffer + size > kPcmBufferSize )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: Audio capture buffer overflow -- truncating\n" );
		size = kPcmBufferSize - bytesInBuffer;
	}

	// Only write if we have a frame's worth of audio
	if ( bytesInBuffer >= afd.audioFrameSize )
	{
		CL_FlushCaptureBuffer();
	}

	if ( pcmBuffer ) 
	{
		std::copy_n( pcmBuffer, size, pcmCaptureBuffer.data() + bytesInBuffer );
		bytesInBuffer += size;
	}
}


/*
===============
CL_TakeVideoFrame
===============
*/
void CL_TakeVideoFrame( void )
{
	// AVI file isn't open
	if( !afd.fileOpen )
		return;

	re.TakeVideoFrame( afd.width, afd.height,
		afd.cBuffer.as<byte>(), afd.eBuffer.as<byte>(), afd.motionJpeg ? qtrue : qfalse );
}


/*
===============
CL_CloseAVI

Closes the AVI file and writes an index chunk
===============
*/
qboolean CL_CloseAVI( qboolean reopen )
{
	int indexRemainder;
	int indexSize;
	const char *idxFileName;

	// AVI file isn't open
	if ( !afd.fileOpen )
	{
		return qfalse;
	}

	CL_FlushCaptureBuffer();

	if ( !reopen )
	{
		afd.cBuffer.reset();
		afd.eBuffer.reset();
	}

	if ( afd.pipe )
	{
		Com_Printf( "Wrote %d:%d frames to pipe:%s\n", afd.numVideoFrames, afd.numAudioFrames, afd.fileName.data() );
		afd.f.reset();
		afd.fileOpen = false;
		afd.pipe = false;
		return qtrue;
	}

	idxFileName = va( "%s%s", afd.fileName.data(), kIndexFileExtension );
	indexSize = afd.numIndices * 16;

	afd.fileOpen = false;

	FS_Seek( afd.idxF.get(), 4, FS_SEEK_SET );
	bufIndex = 0;
	WRITE_4BYTES( indexSize );
	SafeFS_Write( buffer.data(), bufIndex, afd.idxF.get() );
	afd.idxF.reset();

	// Write index

	// Open the temp index file
	indexSize = OpenHomeFileRead( idxFileName, afd.idxF );
	if ( indexSize <= 0 )
	{
		afd.idxF.reset();
		afd.f.reset();
		return qfalse;
	}

	indexRemainder = indexSize;

	// Append index to end of avi file
	while ( indexRemainder > kMaxAviBuffer )
	{
		FileRead( afd.idxF.get(), buffer.data(), kMaxAviBuffer );
		SafeFS_Write( buffer.data(), kMaxAviBuffer, afd.f.get() );
		afd.fileSize += kMaxAviBuffer;
		indexRemainder -= kMaxAviBuffer;
	}

	FileRead( afd.idxF.get(), buffer.data(), indexRemainder );
	SafeFS_Write( buffer.data(), indexRemainder, afd.f.get() );
	afd.fileSize += indexRemainder;
	afd.idxF.reset();

	// Remove temp index file
	FS_HomeRemove( idxFileName );

	// Write the real header
	FS_Seek( afd.f.get(), 0, FS_SEEK_SET );
	CL_WriteAVIHeader();

	bufIndex = 4;
	WRITE_4BYTES( afd.fileSize - 8 ); // "RIFF" size

	bufIndex = afd.moviOffset + 4;    // Skip "LIST"
	WRITE_4BYTES( afd.moviSize );

	SafeFS_Write( buffer.data(), bufIndex, afd.f.get() );

	afd.f.reset();

	Com_DPrintf( "Wrote %d:%d frames to %s\n", afd.numVideoFrames, afd.numAudioFrames, afd.fileName.data() );

	return qtrue;
}


/*
===============
CL_VideoRecording
===============
*/
qboolean CL_VideoRecording( void )
{
	return afd.fileOpen ? qtrue : qfalse;
}
