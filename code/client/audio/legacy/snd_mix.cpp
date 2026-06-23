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
// snd_mix.cpp -- portable code to mix sounds for snd_dma.cpp

extern "C" {
#include "../../client.h"
#include "../snd_local.h"
}

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr int kMinSample16 = -32768;
constexpr int kMaxSample16 = 32767;

static std::array<portable_samplepair_t, PAINTBUFFER_SIZE> paintbuffer;
static int snd_vol;

static short ClampToSample16( int value )
{
	return static_cast<short>( std::clamp( value, kMinSample16, kMaxSample16 ) );
}

} // namespace

// bk001119 - these not static, required by unix/snd_mixa.s
extern "C" {
int		*snd_p;
int		snd_linear_count;
short	*snd_out;

void S_WriteLinearBlastStereo16( void )
{
	int		i;
	int		val;
	int		*src = snd_p;
	short	*dst = snd_out;
	for ( i = 0; i < snd_linear_count; i++, src++, dst++ )
	{
		val = *src >> 8;
		*dst = ClampToSample16( val );
	}
}
}

#if id386 && defined (_MSC_VER)

extern "C" void S_WriteLinearBlastStereo16_MMX( void );
extern "C" void S_WriteLinearBlastStereo16_SSE( void );

#ifdef _WIN32

extern "C" void S_WriteLinearBlastStereo16_MMX( void ) 
{
__asm {
	push ebx
	push esi
	push edi
	mov esi,snd_p
	mov edi,snd_out
	mov ebx,snd_linear_count
	test ebx,ebx
	jz LExit
	mov ecx,esi
	and ecx,63
	jz LMain
	and ecx,3
	jnz LTail
	shr ecx,2
	not ecx
	add ecx,17
LClamp1:
	mov eax,[esi]
	sar eax,8
	cmp eax,32767
	jg LClampHigh1
	cmp eax,-32768
	jnl LClampDone1
	mov eax,-32768
	jmp LClampDone1
LClampHigh1:
	mov eax,32767
LClampDone1:
	mov [edi],ax
	add esi,4
	add edi,2
	dec ebx
	jz LExit
	dec ecx
	jnz LClamp1
LMain:
	mov ecx,ebx
	shr ecx,4
	jz  LTail
	and ebx,15
LAgain:
	movq mm0, qword ptr [esi+ 0]
	movq mm1, qword ptr [esi+ 8]
	movq mm2, qword ptr [esi+16]
	movq mm3, qword ptr [esi+24]
	movq mm4, qword ptr [esi+32]
	movq mm5, qword ptr [esi+40]
	movq mm6, qword ptr [esi+48]
	movq mm7, qword ptr [esi+56]
	psrad mm0,8
	psrad mm1,8
	psrad mm2,8
	psrad mm3,8
	psrad mm4,8
	psrad mm5,8
	psrad mm6,8
	psrad mm7,8
	packssdw mm0, mm1
	packssdw mm2, mm3
	packssdw mm4, mm5
	packssdw mm6, mm7
	movq qword ptr [edi+ 0], mm0
	movq qword ptr [edi+ 8], mm2
	movq qword ptr [edi+16], mm4
	movq qword ptr [edi+24], mm6
	add esi, 64
	add edi, 32
	dec ecx
	jnz LAgain
LTail:
	test ebx, ebx
	jz LEnd
LClamp2:
	mov eax,[esi]
	sar eax,8
	cmp eax,32767
	jg LClampHigh2
	cmp eax,-32768
	jnl LClampDone2
	mov eax,-32768
	jmp LClampDone2
LClampHigh2:
	mov eax,32767
LClampDone2:
	mov [edi],ax
	add esi,4
	add edi,2
	dec ebx
	jnz	LClamp2
LEnd:
	emms
LExit:
	pop edi
	pop esi
	pop ebx
} // __asm
}


extern "C" void S_WriteLinearBlastStereo16_SSE( void ) 
{
__asm {
	push ebx
	push esi
	push edi
	mov esi,snd_p
	mov edi,snd_out
	mov ebx,snd_linear_count
	test ebx,ebx
	jz	LExit
	mov ecx,esi
	and ecx,63
	jz LMain
	and ecx,3
	jnz LTail
	shr ecx,2
	not ecx
	add ecx,17
LClamp1:
	mov eax,[esi]
	sar eax,8
	cmp eax,32767
	jg	LClampHigh1
	cmp eax,-32768
	jnl LClampDone1
	mov eax,-32768
	jmp LClampDone1
LClampHigh1:
	mov eax,32767
LClampDone1:
	mov [edi],ax
	add esi,4
	add edi,2
	dec ebx
	jz	LExit
	dec ecx
	jnz	LClamp1
LMain:
	mov ecx,ebx
	shr ecx,4
	jz  LTail
	and ebx,15
LAgain:
	movq mm0, qword ptr [esi+ 0]
	movq mm1, qword ptr [esi+ 8]
	movq mm2, qword ptr [esi+16]
	movq mm3, qword ptr [esi+24]
	movq mm4, qword ptr [esi+32]
	movq mm5, qword ptr [esi+40]
	movq mm6, qword ptr [esi+48]
	movq mm7, qword ptr [esi+56]
	psrad mm0,8
	psrad mm1,8
	psrad mm2,8
	psrad mm3,8
	psrad mm4,8
	psrad mm5,8
	psrad mm6,8
	psrad mm7,8
	packssdw mm0, mm1
	packssdw mm2, mm3
	packssdw mm4, mm5
	packssdw mm6, mm7
	movntq qword ptr [edi+ 0], mm0
	movntq qword ptr [edi+ 8], mm2
	movntq qword ptr [edi+16], mm4
	movntq qword ptr [edi+24], mm6
	add esi, 64
	add edi, 32
	dec ecx
	jnz LAgain
LTail:
	test ebx, ebx
	jz	LEnd
LClamp2:
	mov eax,[esi]
	sar eax,8
	cmp eax,32767
	jg	LClampHigh2
	cmp eax,-32768
	jnl LClampDone2
	mov eax,-32768
	jmp LClampDone2
LClampHigh2:
	mov eax,32767
LClampDone2:
	mov [edi],ax
	add esi,4
	add edi,2
	dec ebx
	jnz	LClamp2
LEnd:
	sfence
	emms
LExit:
	pop edi
	pop esi
	pop ebx
} // __asm
}

#endif // _WIN32

#endif // id386

#if idx64 && (!defined (_MSC_VER) || defined(USE_WIN32_ASM))
extern "C" void S_WriteLinearBlastStereo16_SSE_x64( int*, short*, int );
#endif

static void S_TransferStereo16( unsigned long *pbuf, int endtime )
{
	int		lpos;
	int		ls_paintedtime;

	snd_p = reinterpret_cast<int *>( paintbuffer.data() );
	ls_paintedtime = s_paintedtime;

	while (ls_paintedtime < endtime)
	{
		// handle recirculating buffer issues
		lpos = ls_paintedtime & ((dma.samples>>1)-1);

		snd_out = reinterpret_cast<short *>( pbuf ) + (lpos<<1);

		snd_linear_count = (dma.samples>>1) - lpos;
		if (ls_paintedtime + snd_linear_count > endtime)
			snd_linear_count = endtime - ls_paintedtime;

		snd_linear_count <<= 1;

		// write a linear blast of samples
#if id386 && defined (_MSC_VER)  && defined (USE_WIN32_ASM)
		if ( CPU_Flags & CPU_SSE )
			S_WriteLinearBlastStereo16_SSE();
		else
		if ( CPU_Flags & CPU_MMX )
			S_WriteLinearBlastStereo16_MMX();
		else
#endif
#if idx64 && (!defined (_MSC_VER) || defined (USE_WIN32_ASM))
		S_WriteLinearBlastStereo16_SSE_x64( snd_p, snd_out, snd_linear_count );
#else
		S_WriteLinearBlastStereo16();
#endif
		snd_p += snd_linear_count;
		ls_paintedtime += (snd_linear_count>>1);
	}
}


/*
===================
S_TransferPaintBuffer

===================
*/
static void S_TransferPaintBuffer( int endtime, byte *buffer )
{
	int 	out_idx;
	int 	i, count;
	int 	out_mask;
	int 	*p;
	int 	step;
	int		val;
	unsigned long *pbuf;

	pbuf = reinterpret_cast<unsigned long *>( buffer );

	if ( s_testsound->integer ) {
		// write a fixed sine wave
		count = (endtime - s_paintedtime);
		for (i=0 ; i<count ; i++)
			paintbuffer[i].left = paintbuffer[i].right =
				static_cast<int>( std::sin( (s_paintedtime+i) * 0.1 ) * 20000 * 256 );
	}

	if ( dma.samplebits == 16 && dma.channels == 2 )
	{	// optimized case
		S_TransferStereo16( pbuf, endtime );
	}
	else
	{	// general case
		p = reinterpret_cast<int *>( paintbuffer.data() );
		count = (endtime - s_paintedtime) * dma.channels;
		out_mask = dma.samples - 1; 
		out_idx = ( s_paintedtime * dma.channels ) & out_mask;
		step = 3 - dma.channels;

		if ( dma.samplebits == 32 && dma.isfloat )
		{
			const float rdiv = 1.0f / (32768.0f * 256.0f - 128.0f); // 8388480.0f
			float *out = reinterpret_cast<float *>( pbuf );
			while ( count-- > 0 )
			{
				val = *p;
				p += step;
				val = std::clamp( val, -32768 * 256, 0x7fff00 );
				out[out_idx] = static_cast<float>( val + 128 ) * rdiv;
				out_idx = (out_idx + 1) & out_mask;
			}
		}
		else if (dma.samplebits == 16)
		{
			short *out = reinterpret_cast<short *>( pbuf );
			while ( count-- > 0 )
			{
				val = *p >> 8;
				p += step;
				out[out_idx] = ClampToSample16( val );
				out_idx = (out_idx + 1) & out_mask;
			}
		}
		else if (dma.samplebits == 8)
		{
			unsigned char *out = reinterpret_cast<unsigned char *>( pbuf );
			while ( count-- > 0 )
			{
				val = *p >> 8;
				p += step;
				val = std::clamp( val, kMinSample16, kMaxSample16 );
				out[out_idx] = (val>>8) + 128;
				out_idx = (out_idx + 1) & out_mask;
			}
		}
	}

	if ( CL_VideoRecording() ) {
		//count = (endtime - s_paintedtime) * dma.channels;
		count = (clc.aviFrameEndTime - s_paintedtime) * dma.channels;
		out_idx = ( s_paintedtime * dma.channels ) % dma.samples;
		while ( count > 0 ) {
			const int dmaSamples = static_cast<int>( dma.samples );
			int n = count;
			if ( n + out_idx > dmaSamples )
				n = dmaSamples - out_idx;
			CL_WriteAVIAudioFrame( buffer + out_idx * dma.samplebits / 8, n * dma.samplebits / 8 );
			out_idx = (out_idx + n) % dmaSamples;
			count -= n;
		}
	}
}


/*
===============================================================================

CHANNEL MIXING

===============================================================================
*/
static void S_PaintChannelFrom16_scalar( channel_t *ch, const sfx_t *sc, int count, int sampleOffset, int bufferOffset ) {
	int						data, aoff, boff;
	int						leftvol, rightvol;
	int						i, j;
	portable_samplepair_t	*samp;
	sndBuffer				*chunk;
	short					*samples;
	float					ooff, fdata[2], fdiv, fleftvol, frightvol;

	if (sc->soundChannels <= 0) {
		return;
	}

	samp = &paintbuffer[ bufferOffset ];

	if (ch->doppler) {
		sampleOffset = sampleOffset*ch->oldDopplerScale;
	}

	if ( sc->soundChannels == 2 ) {
		sampleOffset *= sc->soundChannels;

		if ( sampleOffset & 1 ) {
			sampleOffset &= ~1;
		}
	}

	chunk = sc->soundData;
	while (sampleOffset>=SND_CHUNK_SIZE) {
		chunk = chunk->next;
		sampleOffset -= SND_CHUNK_SIZE;
		if (!chunk) {
			chunk = sc->soundData;
		}
	}

	if (!ch->doppler || ch->dopplerScale==1.0f) {
		leftvol = ch->leftvol*snd_vol;
		rightvol = ch->rightvol*snd_vol;
		samples = chunk->sndChunk;
		for ( i=0 ; i<count ; i++ ) {
			data  = samples[sampleOffset++];
			samp[i].left += (data * leftvol)>>8;

			if ( sc->soundChannels == 2 ) {
				data = samples[sampleOffset++];
			}
			samp[i].right += (data * rightvol)>>8;

			if (sampleOffset == SND_CHUNK_SIZE) {
				chunk = chunk->next;
				if (!chunk) {
					chunk = sc->soundData;
				}
				samples = chunk->sndChunk;
				sampleOffset = 0;
			}
		}
	} else {
		fleftvol = ch->leftvol*snd_vol;
		frightvol = ch->rightvol*snd_vol;

		ooff = sampleOffset;
		samples = chunk->sndChunk;

		for ( i=0 ; i<count ; i++ ) {

			aoff = ooff;
			ooff = ooff + ch->dopplerScale * sc->soundChannels;
			boff = ooff;
			fdata[0] = fdata[1] = 0;
			for (j=aoff; j<boff; j += sc->soundChannels) {
				if (j == SND_CHUNK_SIZE) {
					chunk = chunk->next;
					if (!chunk) {
						chunk = sc->soundData;
					}
					samples = chunk->sndChunk;
					ooff -= SND_CHUNK_SIZE;
				}
				if ( sc->soundChannels == 2 ) {
					fdata[0] += samples[j&(SND_CHUNK_SIZE-1)];
					fdata[1] += samples[(j+1)&(SND_CHUNK_SIZE-1)];
				} else {
					fdata[0] += samples[j&(SND_CHUNK_SIZE-1)];
					fdata[1] += samples[j&(SND_CHUNK_SIZE-1)];
				}
			}
			fdiv = 256 * (boff-aoff) / sc->soundChannels;
			samp[i].left += (fdata[0] * fleftvol)/fdiv;
			samp[i].right += (fdata[1] * frightvol)/fdiv;
		}
	}
}


static void S_PaintChannelFrom16( channel_t *ch, const sfx_t *sc, int count, int sampleOffset, int bufferOffset ) 
{
	S_PaintChannelFrom16_scalar( ch, sc, count, sampleOffset, bufferOffset );
}


static void S_PaintChannelFromWavelet( channel_t *ch, sfx_t *sc, int count, int sampleOffset, int bufferOffset ) {
	int						data;
	int						leftvol, rightvol;
	int						i;
	portable_samplepair_t	*samp;
	sndBuffer				*chunk;
	short					*samples;

	leftvol = ch->leftvol*snd_vol;
	rightvol = ch->rightvol*snd_vol;

	i = 0;
	samp = &paintbuffer[ bufferOffset ];
	chunk = sc->soundData;
	while (sampleOffset>=(SND_CHUNK_SIZE_FLOAT*4)) {
		chunk = chunk->next;
		sampleOffset -= (SND_CHUNK_SIZE_FLOAT*4);
		i++;
	}

	if (i!=sfxScratchIndex || sfxScratchPointer != sc) {
		S_AdpcmGetSamples( chunk, sfxScratchBuffer );
		sfxScratchIndex = i;
		sfxScratchPointer = sc;
	}

	samples = sfxScratchBuffer;

	for ( i=0 ; i<count ; i++ ) {
		data  = samples[sampleOffset++];
		samp[i].left += (data * leftvol)>>8;
		samp[i].right += (data * rightvol)>>8;

		if (sampleOffset == SND_CHUNK_SIZE*2) {
			chunk = chunk->next;
			decodeWavelet(chunk, sfxScratchBuffer);
			sfxScratchIndex++;
			sampleOffset = 0;
		}
	}
}


static void S_PaintChannelFromADPCM( channel_t *ch, sfx_t *sc, int count, int sampleOffset, int bufferOffset ) {
	int						data;
	int						leftvol, rightvol;
	int						i;
	portable_samplepair_t	*samp;
	sndBuffer				*chunk;
	short					*samples;

	leftvol = ch->leftvol*snd_vol;
	rightvol = ch->rightvol*snd_vol;

	i = 0;
	samp = &paintbuffer[ bufferOffset ];
	chunk = sc->soundData;

	if (ch->doppler) {
		sampleOffset = sampleOffset*ch->oldDopplerScale;
	}

	while (sampleOffset>=(SND_CHUNK_SIZE*4)) {
		chunk = chunk->next;
		sampleOffset -= (SND_CHUNK_SIZE*4);
		i++;
	}

	if (i!=sfxScratchIndex || sfxScratchPointer != sc) {
		S_AdpcmGetSamples( chunk, sfxScratchBuffer );
		sfxScratchIndex = i;
		sfxScratchPointer = sc;
	}

	samples = sfxScratchBuffer;

	for ( i=0 ; i<count ; i++ ) {
		data  = samples[sampleOffset++];
		samp[i].left += (data * leftvol)>>8;
		samp[i].right += (data * rightvol)>>8;

		if (sampleOffset == SND_CHUNK_SIZE*4) {
			chunk = chunk->next;
			S_AdpcmGetSamples( chunk, sfxScratchBuffer);
			sampleOffset = 0;
			sfxScratchIndex++;
		}
	}
}


static void S_PaintChannelFromMuLaw( channel_t *ch, sfx_t *sc, int count, int sampleOffset, int bufferOffset ) {
	int						data;
	int						leftvol, rightvol;
	int						i;
	portable_samplepair_t	*samp;
	sndBuffer				*chunk;
	byte					*samples;
	float					ooff;

	leftvol = ch->leftvol*snd_vol;
	rightvol = ch->rightvol*snd_vol;

	samp = &paintbuffer[ bufferOffset ];
	chunk = sc->soundData;
	while (sampleOffset>=(SND_CHUNK_SIZE*2)) {
		chunk = chunk->next;
		sampleOffset -= (SND_CHUNK_SIZE*2);
		if (!chunk) {
			chunk = sc->soundData;
		}
	}

	if (!ch->doppler) {
		samples = reinterpret_cast<byte *>( chunk->sndChunk ) + sampleOffset;
		for ( i=0 ; i<count ; i++ ) {
			data  = mulawToShort[*samples];
			samp[i].left += (data * leftvol)>>8;
			samp[i].right += (data * rightvol)>>8;
			samples++;
			if ( chunk != nullptr && samples == reinterpret_cast<byte *>( chunk->sndChunk ) + ( SND_CHUNK_SIZE * 2 ) ) {
				chunk = chunk->next;
				samples = reinterpret_cast<byte *>( chunk->sndChunk );
			}
		}
	} else {
		ooff = sampleOffset;
		samples = reinterpret_cast<byte *>( chunk->sndChunk );
		for ( i=0 ; i<count ; i++ ) {
			data  = mulawToShort[samples[static_cast<int>( ooff )]];
			ooff = ooff + ch->dopplerScale;
			samp[i].left += (data * leftvol)>>8;
			samp[i].right += (data * rightvol)>>8;
			if (ooff >= SND_CHUNK_SIZE*2) {
				chunk = chunk->next;
				if (!chunk) {
					chunk = sc->soundData;
				}
				samples = reinterpret_cast<byte *>( chunk->sndChunk );
				ooff = 0.0;
			}
		}
	}
}


/*
===================
S_PaintChannels
===================
*/
extern "C" void S_PaintChannels( int endtime ) {
	static bool muted = false;
	int 	i;
	int 	end;
	channel_t *ch;
	sfx_t	*sc;
	int		ltime, count;
	int		sampleOffset;
	byte	*buffer;

	snd_vol = s_volume->value * 255;

	if ( (!gw_active && !gw_minimized && s_muteWhenUnfocused->integer) || (gw_minimized && s_muteWhenMinimized->integer) ) {
		buffer = dma_buffer2;
		if ( !muted ) {
			// switching to muted, clear hardware buffer
			std::fill_n( dma.buffer, dma.samples * dma.samplebits / 8, byte{} );
		}
		muted = true;
	} else {
		buffer = dma.buffer;
		// switching to unmuted, clear both buffers
		if ( muted ) {
			std::fill_n( dma.buffer, dma.samples * dma.samplebits / 8, byte{} );
			std::fill_n( dma_buffer2, dma.samples * dma.samplebits / 8, byte{} );
		}
		muted = false;
	}

	//Com_Printf ("%i to %i\n", s_paintedtime, endtime);
	while ( endtime - s_paintedtime > 0 ) {
		// if paintbuffer is smaller than DMA buffer
		// we may need to fill it multiple times
		end = endtime;
		if ( endtime - s_paintedtime > PAINTBUFFER_SIZE ) {
			end = s_paintedtime + PAINTBUFFER_SIZE;
		}

		// clear the paint buffer and mix any raw samples...
		paintbuffer.fill( portable_samplepair_t{} );
		if ( s_rawend - s_paintedtime >= 0 ) {
			// copy from the streaming sound source
			const int stop = (end < s_rawend) ? end : s_rawend;
			for ( i = s_paintedtime; i < stop; i++ ) {
				const int s = i&(MAX_RAW_SAMPLES-1);
				paintbuffer[i-s_paintedtime].left += s_rawsamples[s].left;
				paintbuffer[i-s_paintedtime].right += s_rawsamples[s].right;
			}
		}

		// paint in the channels.
		ch = s_channels;
		for ( i = 0; i < MAX_CHANNELS ; i++, ch++ ) {
			if ( !ch->thesfx || (!ch->leftvol && !ch->rightvol) ) {
				continue;
			}

			ltime = s_paintedtime;
			sc = ch->thesfx;

			if ( sc->soundData == nullptr || sc->soundLength == 0 ) {
				continue;
			}

			sampleOffset = ltime - ch->startSample;
			count = end - ltime;
			if ( sampleOffset + count > sc->soundLength ) {
				count = sc->soundLength - sampleOffset;
			}

			if ( count > 0 ) {
				if( sc->soundCompressionMethod == 1) {
					S_PaintChannelFromADPCM		(ch, sc, count, sampleOffset, ltime - s_paintedtime);
				} else if( sc->soundCompressionMethod == 2) {
					S_PaintChannelFromWavelet	(ch, sc, count, sampleOffset, ltime - s_paintedtime);
				} else if( sc->soundCompressionMethod == 3) {
					S_PaintChannelFromMuLaw	(ch, sc, count, sampleOffset, ltime - s_paintedtime);
				} else {
					S_PaintChannelFrom16		(ch, sc, count, sampleOffset, ltime - s_paintedtime);
				}
			}
		}

		// paint in the looped channels.
		ch = loop_channels;
		for ( i = 0; i < numLoopChannels ; i++, ch++ ) {
			if ( !ch->thesfx || (!ch->leftvol && !ch->rightvol )) {
				continue;
			}

			ltime = s_paintedtime;
			sc = ch->thesfx;

			if (sc->soundData==nullptr || sc->soundLength==0) {
				continue;
			}
			// we might have to make two passes if it
			// is a looping sound effect and the end of
			// the sample is hit
			do {
				sampleOffset = (ltime % sc->soundLength);

				count = end - ltime;
				if ( sampleOffset + count > sc->soundLength ) {
					count = sc->soundLength - sampleOffset;
				}

				if ( count > 0 ) {
					if( sc->soundCompressionMethod == 1) {
						S_PaintChannelFromADPCM		(ch, sc, count, sampleOffset, ltime - s_paintedtime);
					} else if( sc->soundCompressionMethod == 2) {
						S_PaintChannelFromWavelet	(ch, sc, count, sampleOffset, ltime - s_paintedtime);
					} else if( sc->soundCompressionMethod == 3) {
						S_PaintChannelFromMuLaw		(ch, sc, count, sampleOffset, ltime - s_paintedtime);
					} else {
						S_PaintChannelFrom16		(ch, sc, count, sampleOffset, ltime - s_paintedtime);
					}
					ltime += count;
				}
			} while ( ltime - end < 0 );
		}

		// transfer out according to DMA format
		S_TransferPaintBuffer( end, buffer );
		s_paintedtime = end;
	}
}
