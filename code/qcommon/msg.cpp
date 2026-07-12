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
#include "q_shared.h"
#include "qcommon.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

static int pcount[256];

namespace {

[[nodiscard]] bool MSG_ValidStorage( const msg_t *msg ) noexcept {
	return msg && msg->maxsize >= 0 && msg->maxbits >= 0 &&
		msg->maxsize <= std::numeric_limits<int>::max() / 8 &&
		msg->maxbits == msg->maxsize * 8 &&
		( msg->maxsize == 0 || msg->data != nullptr );
}

void MSG_SetOverflow( msg_t *msg ) noexcept {
	if ( msg ) {
		msg->overflowed = qtrue;
	}
}

[[nodiscard]] int MSG_ReadBitLimit( const msg_t *msg ) noexcept {
	if ( !MSG_ValidStorage( msg ) || msg->cursize < 0 ||
		msg->cursize > msg->maxsize ) {
		return 0;
	}

	return msg->cursize * 8;
}

void MSG_SetReadPastEnd( msg_t *msg ) noexcept {
	if ( !msg ) {
		return;
	}

	msg->bit = MSG_ReadBitLimit( msg );
	msg->readcount = msg->cursize >= 0 &&
		msg->cursize < std::numeric_limits<int>::max()
		? msg->cursize + 1 : std::numeric_limits<int>::max();
}

[[nodiscard]] bool MSG_CanAdvanceBits( int position, int amount,
	int limit ) noexcept {
	return position >= 0 && amount >= 0 && position <= limit &&
		amount <= limit - position;
}

} // namespace

/*
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/
void MSG_Init( msg_t *buf, byte *data, int length ) {
	if ( !buf ) {
		Com_Error( ERR_DROP, "MSG_Init: null message" );
		return;
	}

	Com_Memset (buf, 0, sizeof(*buf));
	if ( length < 0 || length > std::numeric_limits<int>::max() / 8 ||
		( length > 0 && !data ) ) {
		MSG_SetOverflow( buf );
		Com_Error( ERR_DROP, "MSG_Init: invalid buffer length %d", length );
		return;
	}
	buf->data = data;
	buf->maxsize = length;
	buf->maxbits = length * 8;
}


void MSG_InitOOB( msg_t *buf, byte *data, int length ) {
	MSG_Init( buf, data, length );
	buf->oob = qtrue;
}


void MSG_Clear( msg_t *buf ) {
	if ( !buf ) {
		return;
	}
	buf->cursize = 0;
	buf->overflowed = qfalse;
	buf->bit = 0;					//<- in bits
}


void MSG_Bitstream( msg_t *buf ) {
	if ( !buf ) {
		return;
	}
	buf->oob = qfalse;
	buf->rawbit = qfalse;
}


void MSG_RawBitstream( msg_t *buf ) {
	if ( !buf ) {
		return;
	}
	buf->oob = qfalse;
	buf->rawbit = qtrue;
}


void MSG_BeginReading( msg_t *msg ) {
	if ( !msg ) {
		return;
	}
	msg->readcount = 0;
	msg->bit = 0;
	msg->oob = qfalse;
	msg->rawbit = qfalse;
}


void MSG_BeginReadingOOB( msg_t *msg ) {
	if ( !msg ) {
		return;
	}
	msg->readcount = 0;
	msg->bit = 0;
	msg->oob = qtrue;
	msg->rawbit = qfalse;
}


void MSG_Copy(msg_t *buf, byte *data, int length, const msg_t *src)
{
	if ( !buf || !src || !MSG_ValidStorage( src ) || src->cursize < 0 ||
		src->cursize > src->maxsize || length < 0 || length < src->cursize ||
		length > std::numeric_limits<int>::max() / 8 ||
		( length > 0 && !data ) ) {
		Com_Error( ERR_DROP, "MSG_Copy: can't copy into a smaller msg_t buffer");
		return;
	}
	Com_Memcpy(buf, src, sizeof(msg_t));
	buf->data = data;
	buf->maxsize = length;
	buf->maxbits = length * 8;
	if ( src->cursize > 0 ) {
		Com_Memcpy(buf->data, src->data, src->cursize);
	}
}

/*
=============================================================================

bit functions
  
=============================================================================
*/

// negative bit values include signs
void MSG_WriteBits( msg_t *msg, int value, int bits ) {
	int i;
	int bitCount;
	std::uint32_t encodedValue;

	if ( bits == 0 || bits < -31 || bits > 32 ) {
		Com_Error( ERR_DROP, "MSG_WriteBits: bad bits %i", bits );
		return;
	}

	if ( !MSG_ValidStorage( msg ) || msg->bit < 0 || msg->cursize < 0 ||
		msg->cursize > msg->maxsize || msg->overflowed != qfalse ) {
		MSG_SetOverflow( msg );
		return;
	}

	if ( bits < 0 ) {
		bits = -bits;
	}
	bitCount = bits;
	encodedValue = static_cast<std::uint32_t>( value );
	if ( bitCount < 32 ) {
		encodedValue &= ( 1u << bitCount ) - 1u;
	}

	if (msg->oob) {
		if ( bitCount != 8 && bitCount != 16 && bitCount != 32 ) {
			Com_Error( ERR_DROP, "can't write %d bits", bitCount );
			return;
		}
		const int byteCount = bitCount / 8;
		if ( !MSG_CanAdvanceBits( msg->bit, bitCount, msg->maxbits ) ||
			msg->cursize > msg->maxsize ||
			byteCount > msg->maxsize - msg->cursize ) {
			MSG_SetOverflow( msg );
			return;
		}

		if ( bits == 8 ) {
			msg->data[msg->cursize] = static_cast<byte>( encodedValue );
			msg->cursize += 1;
			msg->bit += 8;
		} else if ( bits == 16 ) {
			short temp = static_cast<short>( encodedValue );
			
			CopyLittleShort(&msg->data[msg->cursize], &temp);
			msg->cursize += 2;
			msg->bit += 16;
		} else if ( bits==32 ) {
			int wireValue = 0;
			std::memcpy( &wireValue, &encodedValue, sizeof( wireValue ) );
			CopyLittleLong(&msg->data[msg->cursize], &wireValue);
			msg->cursize += 4;
			msg->bit += 32;
		}
	} else if ( msg->rawbit ) {
		const int nbits = bitCount;
		int bitIndex = msg->bit;

		if ( !MSG_CanAdvanceBits( bitIndex, nbits, msg->maxbits ) ) {
			MSG_SetOverflow( msg );
			return;
		}
		for ( i = 0; i < nbits; i++ ) {
			if ( encodedValue & ( 1u << i ) ) {
				msg->data[ bitIndex >> 3 ] |= 1u << ( bitIndex & 7 );
			} else {
				msg->data[ bitIndex >> 3 ] &= ~( 1u << ( bitIndex & 7 ) );
			}
			bitIndex++;
		}
		msg->bit = bitIndex;
		msg->cursize = (bitIndex + 7) >> 3;
	} else {
		const int lowBitCount = bitCount & 7;
		int requiredBits = lowBitCount;
		std::uint32_t remainingValue = encodedValue >> lowBitCount;

		for ( i = lowBitCount; i < bitCount; i += 8 ) {
			const int symbolBits = HuffmanSymbolBitCount(
				static_cast<int>( remainingValue & 0xffu ) );
			if ( symbolBits < 0 || requiredBits > msg->maxbits - symbolBits ) {
				MSG_SetOverflow( msg );
				return;
			}
			requiredBits += symbolBits;
			remainingValue >>= 8;
		}

		if ( !MSG_CanAdvanceBits( msg->bit, requiredBits, msg->maxbits ) ||
			( msg->bit + requiredBits ) / 8 + 1 > msg->maxsize ) {
			MSG_SetOverflow( msg );
			return;
		}

		if ( lowBitCount ) {
			const int nbits = lowBitCount;
			for ( i = 0; i < nbits ; i++ ) {
				HuffmanPutBit( msg->data, msg->bit,
					static_cast<int>( encodedValue & 1u ) );
				msg->bit++;
				encodedValue >>= 1;
			}
		}
		if ( bitCount > lowBitCount ) {
			for( i = lowBitCount; i < bitCount; i += 8 ) {
				const int written = HuffmanPutSymbolBounded( msg->data,
					static_cast<std::uint32_t>( msg->bit ),
					static_cast<std::uint32_t>( msg->maxbits ),
					static_cast<int>( encodedValue & 0xffu ) );
				if ( written < 0 ) {
					MSG_SetOverflow( msg );
					return;
				}
				msg->bit += written;
				encodedValue >>= 8;
			}
		}
		msg->cursize = (msg->bit>>3)+1;
	}
}


static int MSG_ReadBits( msg_t *msg, int bits ) {
	bool isSigned;
	int bitCount;
	const byte *buffer;
	int bitLimit;
	std::uint32_t value = 0;

	if ( bits == 0 || bits < -31 || bits > 32 ) {
		Com_Error( ERR_DROP, "MSG_ReadBits: bad bits %i", bits );
		return 0;
	}
	isSigned = bits < 0;
	bitCount = isSigned ? -bits : bits;
	if ( !MSG_ValidStorage( msg ) || msg->readcount < 0 || msg->bit < 0 ||
		msg->cursize < 0 || msg->cursize > msg->maxsize ) {
		MSG_SetReadPastEnd( msg );
		return 0;
	}

	buffer = msg->data;
	bitLimit = MSG_ReadBitLimit( msg );

	if ( msg->oob ) {
		if ( bitCount != 8 && bitCount != 16 && bitCount != 32 ) {
			Com_Error( ERR_DROP, "can't read %d bits", bitCount );
			return 0;
		}
		if ( !MSG_CanAdvanceBits( msg->bit, bitCount, bitLimit ) ||
			msg->readcount > msg->cursize ||
			bitCount / 8 > msg->cursize - msg->readcount ) {
			MSG_SetReadPastEnd( msg );
			return 0;
		}

		if ( bitCount == 8 ) {
			value = buffer[msg->readcount];
			msg->readcount += 1;
			msg->bit += 8;
		} else if ( bitCount == 16 ) {
			short temp = 0;
			CopyLittleShort( &temp, buffer + msg->readcount );
			value = static_cast<std::uint16_t>( temp );
			msg->readcount += 2;
			msg->bit += 16;
		} else {
			int wireValue = 0;
			CopyLittleLong( &wireValue, buffer + msg->readcount );
			std::memcpy( &value, &wireValue, sizeof( value ) );
			msg->readcount += 4;
			msg->bit += 32;
		}
	} else if ( msg->rawbit ) {
		int bitIndex = msg->bit;
		if ( !MSG_CanAdvanceBits( bitIndex, bitCount, bitLimit ) ) {
			MSG_SetReadPastEnd( msg );
			return 0;
		}

		for ( int i = 0; i < bitCount; ++i, ++bitIndex ) {
			value |= static_cast<std::uint32_t>(
				( buffer[bitIndex >> 3] >> ( bitIndex & 7 ) ) & 1u ) << i;
		}
		msg->bit = bitIndex;
		msg->readcount = ( bitIndex + 7 ) >> 3;
	} else {
		const int lowBitCount = bitCount & 7;
		int bitIndex = msg->bit;

		if ( !MSG_CanAdvanceBits( bitIndex, lowBitCount, bitLimit ) ) {
			MSG_SetReadPastEnd( msg );
			return 0;
		}
		for ( int i = 0; i < lowBitCount; ++i, ++bitIndex ) {
			value |= static_cast<std::uint32_t>(
				HuffmanGetBit( buffer, bitIndex ) ) << i;
		}

		for ( int i = lowBitCount; i < bitCount; i += 8 ) {
			unsigned int symbol = 0;
			const int consumed = HuffmanGetSymbolBounded( &symbol, buffer,
				bitIndex, bitLimit );
			if ( consumed < 0 ) {
				MSG_SetReadPastEnd( msg );
				return 0;
			}
			bitIndex += consumed;
			value |= static_cast<std::uint32_t>( symbol ) << i;
		}
		msg->bit = bitIndex;
		msg->readcount = ( bitIndex >> 3 ) + 1;
	}

	if ( isSigned && bitCount < 32 ) {
		const std::uint32_t signBit = 1u << ( bitCount - 1 );
		if ( value & signBit ) {
			value |= ~0u << bitCount;
		}
	}

	int result = 0;
	std::memcpy( &result, &value, sizeof( result ) );
	return result;
}



//================================================================================

//
// writing functions
//

void MSG_WriteChar( msg_t *sb, int c ) {
#ifdef PARANOID
	if (c < -128 || c > 127)
		Com_Error (ERR_FATAL, "MSG_WriteChar: range error");
#endif

	MSG_WriteBits( sb, c, 8 );
}

void MSG_WriteByte( msg_t *sb, int c ) {
#ifdef PARANOID
	if (c < 0 || c > 255)
		Com_Error (ERR_FATAL, "MSG_WriteByte: range error");
#endif

	MSG_WriteBits( sb, c, 8 );
}

void MSG_WriteData( msg_t *buf, const void *data, int length ) {
	if ( length < 0 || ( length > 0 && !data ) ) {
		MSG_SetOverflow( buf );
		return;
	}

	const auto *bytes = static_cast<const byte *>( data );
	for ( int i = 0; i < length; ++i ) {
		MSG_WriteByte( buf, bytes[i] );
		if ( buf->overflowed ) {
			return;
		}
	}
}

void MSG_WriteShort( msg_t *sb, int c ) {
#ifdef PARANOID
	if (c < ((short)0x8000) || c > (short)0x7fff)
		Com_Error (ERR_FATAL, "MSG_WriteShort: range error");
#endif

	MSG_WriteBits( sb, c, 16 );
}

void MSG_WriteLong( msg_t *sb, int c ) {
	MSG_WriteBits( sb, c, 32 );
}

void MSG_WriteFloat( msg_t *sb, float f ) {
	floatint_t dat;
	dat.f = f;
	MSG_WriteBits( sb, dat.i, 32 );
}

void MSG_WriteString( msg_t *sb, const char *s ) {
	int l, i;
	char v;

	l = s ? strlen( s ) : 0;
	if ( l >= MAX_STRING_CHARS ) {
		Com_Printf( "MSG_WriteString: MAX_STRING_CHARS\n" );
		l = 0; 
	}

	for ( i = 0 ; i < l; i++ ) {
		// get rid of 0x80+ and '%' chars, because old clients don't like them
		if ( s[i] & 0x80 || s[i] == '%' )
			v = '.';
		else
			v = s[i];
		MSG_WriteChar( sb, v );
	}

	MSG_WriteChar( sb, '\0' );
}

void MSG_WriteBigString( msg_t *sb, const char *s ) {
	int l, i;
	char v;

	l = s ? strlen( s ) : 0;
	if ( l >= BIG_INFO_STRING ) {
		Com_Printf( "MSG_WriteBigString: BIG_INFO_STRING\n" );
		l = 0; 
	}

	for ( i = 0 ; i < l ; i++ ) {
		// get rid of 0x80+ and '%' chars, because old clients don't like them
		if ( s[i] & 0x80 || s[i] == '%' )
			v = '.';
		else
			v = s[i];
		MSG_WriteChar( sb, v );
	}

	MSG_WriteChar( sb, '\0' );
}

void MSG_WriteAngle( msg_t *sb, float f ) {
	MSG_WriteByte (sb, (int)(f*256/360) & 255);
}

void MSG_WriteAngle16( msg_t *sb, float f ) {
	MSG_WriteShort (sb, ANGLE2SHORT(f));
}


//============================================================

//
// reading functions
//

// returns -1 if no more characters are available
int MSG_ReadChar (msg_t *msg ) {
	int	c;
	
	c = (signed char)MSG_ReadBits( msg, 8 );
	if ( msg->readcount > msg->cursize ) {
		c = -1;
	}	
	
	return c;
}

int MSG_ReadByte( msg_t *msg ) {
	int	c;
	
	c = (unsigned char)MSG_ReadBits( msg, 8 );
	if ( msg->readcount > msg->cursize ) {
		c = -1;
	}	
	return c;
}

int MSG_ReadShort( msg_t *msg ) {
	int	c;
	
	c = (short)MSG_ReadBits( msg, 16 );
	if ( msg->readcount > msg->cursize ) {
		c = -1;
	}	

	return c;
}

int MSG_ReadLong( msg_t *msg ) {
	int	c;
	
	c = MSG_ReadBits( msg, 32 );
	if ( msg->readcount > msg->cursize ) {
		c = -1;
	}	
	
	return c;
}

float MSG_ReadFloat( msg_t *msg ) {
	floatint_t dat;
	
	dat.i = MSG_ReadBits( msg, 32 );
	if ( msg->readcount > msg->cursize ) {
		dat.f = -1;
	}	
	
	return dat.f;	
}


const char *MSG_ReadString( msg_t *msg ) {
	static char	string[MAX_STRING_CHARS];
	size_t	l;
	int	c;
	
	l = 0;
	do {
		c = MSG_ReadByte( msg ); // use ReadByte so -1 is out of bounds
		if ( c <= 0 /*c == -1 || c == 0 */ || l >= sizeof(string)-1 ) {
			break;
		}
		// translate all fmt spec to avoid crash bugs
		if ( c == '%' ) {
			c = '.';
		} else
		// don't allow higher ascii values
		if ( c > 127 ) {
			c = '.';
		}
		string[ l++ ] = c;
	} while ( qtrue );
	
	string[ l ] = '\0';
	
	return string;
}


const char *MSG_ReadBigString( msg_t *msg ) {
	static char	string[ BIG_INFO_STRING ];
	size_t	l;
	int	c;
	
	l = 0;
	do {
		c = MSG_ReadByte( msg ); // use ReadByte so -1 is out of bounds
		if ( c <= 0 /*c == -1 || c == 0*/ || l >= sizeof(string)-1 ) {
			break;
		}
		// translate all fmt spec to avoid crash bugs
		if ( c == '%' ) {
			c = '.';
		} else
		// don't allow higher ascii values
		if ( c > 127 ) {
			c = '.';
		}
		string[ l++ ] = c;
	} while ( qtrue );
	
	string[ l ] = '\0';
	
	return string;
}


const char *MSG_ReadStringLine( msg_t *msg ) {
	static char	string[MAX_STRING_CHARS];
	size_t	l;
	int	c;

	l = 0;
	do {
		c = MSG_ReadByte( msg ); // use ReadByte so -1 is out of bounds
		if ( c <= 0 /*c == -1 || c == 0*/ || c == '\n' || l >= sizeof(string)-1 ) {
			break;
		}
		// translate all fmt spec to avoid crash bugs
		if ( c == '%' ) {
			c = '.';
		} else
		// don't allow higher ascii values
		if ( c > 127 ) {
			c = '.';
		}
		string[ l++ ] = c;
	} while ( qtrue );
	
	string[ l ] = '\0';
	
	return string;
}


float MSG_ReadAngle16( msg_t *msg ) {
	return SHORT2ANGLE(MSG_ReadShort(msg));
}


void MSG_ReadData( msg_t *msg, void *data, int len ) {
	if ( len < 0 || ( len > 0 && !data ) ) {
		MSG_SetReadPastEnd( msg );
		return;
	}

	auto *bytes = static_cast<byte *>( data );
	for ( int i = 0; i < len; ++i ) {
		const int value = MSG_ReadByte( msg );
		if ( value < 0 ) {
			std::memset( bytes + i, 0, static_cast<std::size_t>( len - i ) );
			return;
		}
		bytes[i] = static_cast<byte>( value );
	}
}


int MSG_ReadEntitynum( msg_t *msg ) {
	const int num = MSG_ReadBits( msg, GENTITYNUM_BITS );
	if ( msg->readcount > msg->cursize ) {
		return -1;
	} else {
		return num;
	}
	
}


// a string hasher which gives the same hash value even if the
// string is later modified via the legacy MSG read/write code
int MSG_HashKey(const char *string, int maxlen) {
	int hash, i;
	if ( !string || maxlen <= 0 ) {
		return 0;
	}

	hash = 0;
	for (i = 0; i < maxlen && string[i] != '\0'; i++) {
		if (string[i] & 0x80 || string[i] == '%')
			hash += '.' * (119 + i);
		else
			hash += string[i] * (119 + i);
	}
	hash = (hash ^ (hash >> 10) ^ (hash >> 20));
	return hash;
}

#ifndef DEDICATED
extern "C" cvar_t *cl_shownet;
#define	LOG(x) if( cl_shownet && cl_shownet->integer == 4 ) { Com_Printf("%s ", x ); };
#else
#define	LOG(x)
#endif

/*
=============================================================================

delta functions with keys
  
=============================================================================
*/

static const std::uint32_t kbitmask[32] = {
	0x00000001, 0x00000003, 0x00000007, 0x0000000F,
	0x0000001F,	0x0000003F,	0x0000007F,	0x000000FF,
	0x000001FF,	0x000003FF,	0x000007FF,	0x00000FFF,
	0x00001FFF,	0x00003FFF,	0x00007FFF,	0x0000FFFF,
	0x0001FFFF,	0x0003FFFF,	0x0007FFFF,	0x000FFFFF,
	0x001FFFFf,	0x003FFFFF,	0x007FFFFF,	0x00FFFFFF,
	0x01FFFFFF,	0x03FFFFFF,	0x07FFFFFF,	0x0FFFFFFF,
	0x1FFFFFFF,	0x3FFFFFFF,	0x7FFFFFFF,	0xFFFFFFFF,
};


static void MSG_WriteDeltaKey( msg_t *msg, int key, int oldV, int newV, int bits ) {
	if ( oldV == newV ) {
		MSG_WriteBits( msg, 0, 1 );
		return;
	}
	MSG_WriteBits( msg, 1, 1 );
	MSG_WriteBits( msg, newV ^ key, bits );
}


static int MSG_ReadDeltaKey( msg_t *msg, int key, int oldV, int bits ) {
	if ( MSG_ReadBits( msg, 1 ) ) {
		return MSG_ReadBits( msg, bits ) ^ (key & kbitmask[ bits - 1 ]);
	}
	return oldV;
}


/*
============================================================================

usercmd_t communication

============================================================================
*/

/*
=====================
MSG_WriteDeltaUsercmdKey
=====================
*/
void MSG_WriteDeltaUsercmdKey( msg_t *msg, int key, const usercmd_t *from, const usercmd_t *to ) {
	if ( (unsigned)(to->serverTime - from->serverTime) < 256 ) {
		MSG_WriteBits( msg, 1, 1 );
		MSG_WriteBits( msg, to->serverTime - from->serverTime, 8 );
	} else {
		MSG_WriteBits( msg, 0, 1 );
		MSG_WriteBits( msg, to->serverTime, 32 );
	}
	if (from->angles[0] == to->angles[0] &&
		from->angles[1] == to->angles[1] &&
		from->angles[2] == to->angles[2] &&
		from->forwardmove == to->forwardmove &&
		from->rightmove == to->rightmove &&
		from->upmove == to->upmove &&
		from->buttons == to->buttons &&
		from->weapon == to->weapon &&
		from->weaponPrimary == to->weaponPrimary &&
		from->fov == to->fov) {
			MSG_WriteBits( msg, 0, 1 );				// no change
			return;
	}
	key ^= to->serverTime;
	MSG_WriteBits( msg, 1, 1 );
	MSG_WriteDeltaKey( msg, key, from->angles[0], to->angles[0], 16 );
	MSG_WriteDeltaKey( msg, key, from->angles[1], to->angles[1], 16 );
	MSG_WriteDeltaKey( msg, key, from->angles[2], to->angles[2], 16 );
	MSG_WriteDeltaKey( msg, key, from->forwardmove, to->forwardmove, 8 );
	MSG_WriteDeltaKey( msg, key, from->rightmove, to->rightmove, 8 );
	MSG_WriteDeltaKey( msg, key, from->upmove, to->upmove, 8 );
	MSG_WriteDeltaKey( msg, key, from->buttons, to->buttons, 16 );
	MSG_WriteDeltaKey( msg, key, from->weapon, to->weapon, 8 );
	MSG_WriteDeltaKey( msg, key, from->weaponPrimary, to->weaponPrimary, 8 );
	MSG_WriteDeltaKey( msg, key, from->fov, to->fov, 8 );
}


/*
=====================
MSG_ReadDeltaUsercmdKey
=====================
*/
void MSG_ReadDeltaUsercmdKey( msg_t *msg, int key, const usercmd_t *from, usercmd_t *to ) {
	if ( MSG_ReadBits( msg, 1 ) ) {
		to->serverTime = from->serverTime + MSG_ReadBits( msg, 8 );
	} else {
		to->serverTime = MSG_ReadBits( msg, 32 );
	}
	if ( MSG_ReadBits( msg, 1 ) ) {
		key ^= to->serverTime;
		to->angles[0] = MSG_ReadDeltaKey( msg, key, from->angles[0], 16);
		to->angles[1] = MSG_ReadDeltaKey( msg, key, from->angles[1], 16);
		to->angles[2] = MSG_ReadDeltaKey( msg, key, from->angles[2], 16);
		to->forwardmove = MSG_ReadDeltaKey( msg, key, from->forwardmove, 8);
		if( to->forwardmove == -128 )
			to->forwardmove = -127;
		to->rightmove = MSG_ReadDeltaKey( msg, key, from->rightmove, 8);
		if( to->rightmove == -128 )
			to->rightmove = -127;
		to->upmove = MSG_ReadDeltaKey( msg, key, from->upmove, 8);
		if( to->upmove == -128 )
			to->upmove = -127;
		to->buttons = MSG_ReadDeltaKey( msg, key, from->buttons, 16);
		to->weapon = MSG_ReadDeltaKey( msg, key, from->weapon, 8);
		to->weaponPrimary = MSG_ReadDeltaKey( msg, key, from->weaponPrimary, 8);
		to->fov = MSG_ReadDeltaKey( msg, key, from->fov, 8);
	} else {
		to->angles[0] = from->angles[0];
		to->angles[1] = from->angles[1];
		to->angles[2] = from->angles[2];
		to->forwardmove = from->forwardmove;
		to->rightmove = from->rightmove;
		to->upmove = from->upmove;
		to->buttons = from->buttons;
		to->weapon = from->weapon;
		to->weaponPrimary = from->weaponPrimary;
		to->fov = from->fov;
	}
}

/*
=============================================================================

entityState_t communication
  
=============================================================================
*/

/*
=================
MSG_ReportChangeVectors_f

Prints out a table from the current statistics for copying to code
=================
*/
void MSG_ReportChangeVectors_f( void ) {
	int i;
	for(i=0;i<256;i++) {
		if (pcount[i]) {
			Com_Printf("%d used %d\n", i, pcount[i]);
		}
	}
}

typedef struct {
	const char	*name;
	const std::size_t offset;
	const int	bits;	// 0 = float
} netField_t;

// using the stringizing operator to save typing...
#define	NETF(x) #x, offsetof( entityState_t, x )

static const netField_t entityStateFields[] =
{
{ NETF(pos.trTime), 32 },
{ NETF(pos.trBase[0]), 0 },
{ NETF(pos.trBase[1]), 0 },
{ NETF(pos.trDelta[0]), 0 },
{ NETF(pos.trDelta[1]), 0 },
{ NETF(pos.trBase[2]), 0 },
{ NETF(apos.trBase[1]), 0 },
{ NETF(pos.trDelta[2]), 0 },
{ NETF(apos.trBase[0]), 0 },
{ NETF(pos.gravity), 32 },
{ NETF(event), 10 },
{ NETF(angles2[1]), 0 },
{ NETF(eType), 8 },
{ NETF(torsoAnim), 8 },
{ NETF(eventParm), 8 },
{ NETF(legsAnim), 8 },
{ NETF(groundEntityNum), GENTITYNUM_BITS },
{ NETF(pos.trType), 8 },
{ NETF(eFlags), 19 },
{ NETF(otherEntityNum), GENTITYNUM_BITS },
{ NETF(weapon), 8 },
{ NETF(clientNum), 8 },
{ NETF(angles[1]), 0 },
{ NETF(pos.trDuration), 32 },
{ NETF(apos.trType), 8 },
{ NETF(origin[0]), 0 },
{ NETF(origin[1]), 0 },
{ NETF(origin[2]), 0 },
{ NETF(solid), 24 },
{ NETF(powerups), 16 },
{ NETF(modelindex), 8 },
{ NETF(otherEntityNum2), GENTITYNUM_BITS },
{ NETF(loopSound), 8 },
{ NETF(generic1), 8 },
{ NETF(origin2[2]), 0 },
{ NETF(origin2[0]), 0 },
{ NETF(origin2[1]), 0 },
{ NETF(modelindex2), 8 },
{ NETF(angles[0]), 0 },
{ NETF(time), 32 },
{ NETF(apos.trTime), 32 },
{ NETF(apos.trDuration), 32 },
{ NETF(apos.trBase[2]), 0 },
{ NETF(apos.trDelta[0]), 0 },
{ NETF(apos.trDelta[1]), 0 },
{ NETF(apos.trDelta[2]), 0 },
{ NETF(apos.gravity), 32 },
{ NETF(time2), 32 },
{ NETF(angles[2]), 0 },
{ NETF(angles2[0]), 0 },
{ NETF(angles2[2]), 0 },
{ NETF(constantLight), 32 },
{ NETF(frame), 16 },
{ NETF(jumpTime), 32 },
{ NETF(doubleJumped), 1 },
{ NETF(health), 16 },
{ NETF(armor), 16 },
{ NETF(location), 8 }
};

typedef struct {
	unsigned int	words[2];
} legacyChangeVector_t;

/*
Protocol 43-48 demo delta layouts cross-checked against WolfcamQL and the
UberDemoTools field tables. The 32-entry change-mask dictionary is shared by
dm3 and dm_48; protocols 46-48 expand the field list.
*/
static const netField_t legacyEntityStateFields43[] =
{
{ NETF(eType), 8 },
{ NETF(eFlags), 16 },
{ NETF(pos.trType), 8 },
{ NETF(pos.trTime), 32 },
{ NETF(pos.trDuration), 32 },
{ NETF(pos.trBase[0]), 0 },
{ NETF(pos.trBase[1]), 0 },
{ NETF(pos.trBase[2]), 0 },
{ NETF(pos.trDelta[0]), 0 },
{ NETF(pos.trDelta[1]), 0 },
{ NETF(pos.trDelta[2]), 0 },
{ NETF(apos.trType), 8 },
{ NETF(apos.trTime), 32 },
{ NETF(apos.trDuration), 32 },
{ NETF(apos.trBase[0]), 0 },
{ NETF(apos.trBase[1]), 0 },
{ NETF(apos.trBase[2]), 0 },
{ NETF(apos.trDelta[0]), 0 },
{ NETF(apos.trDelta[1]), 0 },
{ NETF(apos.trDelta[2]), 0 },
{ NETF(time), 32 },
{ NETF(time2), 32 },
{ NETF(origin[0]), 0 },
{ NETF(origin[1]), 0 },
{ NETF(origin[2]), 0 },
{ NETF(origin2[0]), 0 },
{ NETF(origin2[1]), 0 },
{ NETF(origin2[2]), 0 },
{ NETF(angles[0]), 0 },
{ NETF(angles[1]), 0 },
{ NETF(angles[2]), 0 },
{ NETF(angles2[0]), 0 },
{ NETF(angles2[1]), 0 },
{ NETF(angles2[2]), 0 },
{ NETF(otherEntityNum), GENTITYNUM_BITS },
{ NETF(otherEntityNum2), GENTITYNUM_BITS },
{ NETF(groundEntityNum), GENTITYNUM_BITS },
{ NETF(loopSound), 8 },
{ NETF(constantLight), 32 },
{ NETF(modelindex), 8 },
{ NETF(modelindex2), 8 },
{ NETF(frame), 16 },
{ NETF(clientNum), 8 },
{ NETF(solid), 24 },
{ NETF(event), 10 },
{ NETF(eventParm), 8 },
{ NETF(powerups), MAX_POWERUPS },
{ NETF(weapon), 8 },
{ NETF(legsAnim), 8 },
{ NETF(torsoAnim), 8 }
};

static const netField_t legacyEntityStateFields46[] =
{
{ NETF(eType), 8 },
{ NETF(eFlags), 19 },
{ NETF(pos.trType), 8 },
{ NETF(pos.trTime), 32 },
{ NETF(pos.trDuration), 32 },
{ NETF(pos.trBase[0]), 0 },
{ NETF(pos.trBase[1]), 0 },
{ NETF(pos.trBase[2]), 0 },
{ NETF(pos.trDelta[0]), 0 },
{ NETF(pos.trDelta[1]), 0 },
{ NETF(pos.trDelta[2]), 0 },
{ NETF(apos.trType), 8 },
{ NETF(apos.trTime), 32 },
{ NETF(apos.trDuration), 32 },
{ NETF(apos.trBase[0]), 0 },
{ NETF(apos.trBase[1]), 0 },
{ NETF(apos.trBase[2]), 0 },
{ NETF(apos.trDelta[0]), 0 },
{ NETF(apos.trDelta[1]), 0 },
{ NETF(apos.trDelta[2]), 0 },
{ NETF(time), 32 },
{ NETF(time2), 32 },
{ NETF(origin[0]), 0 },
{ NETF(origin[1]), 0 },
{ NETF(origin[2]), 0 },
{ NETF(origin2[0]), 0 },
{ NETF(origin2[1]), 0 },
{ NETF(origin2[2]), 0 },
{ NETF(angles[0]), 0 },
{ NETF(angles[1]), 0 },
{ NETF(angles[2]), 0 },
{ NETF(angles2[0]), 0 },
{ NETF(angles2[1]), 0 },
{ NETF(angles2[2]), 0 },
{ NETF(otherEntityNum), GENTITYNUM_BITS },
{ NETF(otherEntityNum2), GENTITYNUM_BITS },
{ NETF(groundEntityNum), GENTITYNUM_BITS },
{ NETF(loopSound), 8 },
{ NETF(constantLight), 32 },
{ NETF(modelindex), 8 },
{ NETF(modelindex2), 8 },
{ NETF(frame), 16 },
{ NETF(clientNum), 8 },
{ NETF(solid), 24 },
{ NETF(event), 10 },
{ NETF(eventParm), 8 },
{ NETF(powerups), MAX_POWERUPS },
{ NETF(weapon), 8 },
{ NETF(legsAnim), 8 },
{ NETF(torsoAnim), 8 },
{ NETF(generic1), 8 }
};

static const netField_t legacyEntityStateFields48[] =
{
{ NETF(eType), 8 },
{ NETF(eFlags), 19 },
{ NETF(pos.trType), 8 },
{ NETF(pos.trTime), 32 },
{ NETF(pos.trDuration), 32 },
{ NETF(pos.trBase[0]), 0 },
{ NETF(pos.trBase[1]), 0 },
{ NETF(pos.trBase[2]), 0 },
{ NETF(pos.trDelta[0]), 0 },
{ NETF(pos.trDelta[1]), 0 },
{ NETF(pos.trDelta[2]), 0 },
{ NETF(apos.trType), 8 },
{ NETF(apos.trTime), 32 },
{ NETF(apos.trDuration), 32 },
{ NETF(apos.trBase[0]), 0 },
{ NETF(apos.trBase[1]), 0 },
{ NETF(apos.trBase[2]), 0 },
{ NETF(apos.trDelta[0]), 0 },
{ NETF(apos.trDelta[1]), 0 },
{ NETF(apos.trDelta[2]), 0 },
{ NETF(time), 32 },
{ NETF(time2), 32 },
{ NETF(origin[0]), 0 },
{ NETF(origin[1]), 0 },
{ NETF(origin[2]), 0 },
{ NETF(origin2[0]), 0 },
{ NETF(origin2[1]), 0 },
{ NETF(origin2[2]), 0 },
{ NETF(angles[0]), 0 },
{ NETF(angles[1]), 0 },
{ NETF(angles[2]), 0 },
{ NETF(angles2[0]), 0 },
{ NETF(angles2[1]), 0 },
{ NETF(angles2[2]), 0 },
{ NETF(otherEntityNum), GENTITYNUM_BITS },
{ NETF(otherEntityNum2), GENTITYNUM_BITS },
{ NETF(groundEntityNum), GENTITYNUM_BITS },
{ NETF(loopSound), 8 },
{ NETF(constantLight), 32 },
{ NETF(modelindex), 8 },
{ NETF(modelindex2), 8 },
{ NETF(frame), 16 },
{ NETF(clientNum), 8 },
{ NETF(solid), 24 },
{ NETF(event), 10 },
{ NETF(eventParm), 8 },
{ NETF(powerups), MAX_POWERUPS },
{ NETF(weapon), 8 },
{ NETF(legsAnim), 8 },
{ NETF(torsoAnim), 8 },
{ NETF(generic1), 8 }
};

static const legacyChangeVector_t legacyEntityStateChangeVectors[] =
{
	{ { 0x00008060u, 0x00000000u } },
	{ { 0x00000060u, 0x00000000u } },
	{ { 0x0000c060u, 0x00000000u } },
	{ { 0x000000e1u, 0x00002000u } },
	{ { 0x00008060u, 0x00001000u } },
	{ { 0x000080e0u, 0x00000000u } },
	{ { 0x0000c0e0u, 0x00000000u } },
	{ { 0x00000000u, 0x00001000u } },
	{ { 0x00008040u, 0x00000000u } },
	{ { 0x00008020u, 0x00000000u } },
	{ { 0x00008060u, 0x00000001u } },
	{ { 0x000007edu, 0x00008000u } },
	{ { 0x000000e0u, 0x00000000u } },
	{ { 0x000007edu, 0x00003000u } },
	{ { 0x00000080u, 0x00000000u } },
	{ { 0x00000040u, 0x00000000u } },
	{ { 0x0000c0e0u, 0x00001000u } },
	{ { 0x00000060u, 0x00001000u } },
	{ { 0x00000020u, 0x00000000u } },
	{ { 0x000000e1u, 0x00002004u } },
	{ { 0x01c000e1u, 0x00002020u } },
	{ { 0x0000c0e0u, 0x00000001u } },
	{ { 0x00004060u, 0x00000000u } },
	{ { 0x0000c040u, 0x00000000u } },
	{ { 0x0000c060u, 0x00000001u } },
	{ { 0x0000c060u, 0x00001000u } },
	{ { 0x00008060u, 0x00010001u } },
	{ { 0x00008060u, 0x00003000u } },
	{ { 0x000080e0u, 0x00001000u } },
	{ { 0x0000c020u, 0x00000000u } },
	{ { 0x00008060u, 0x00020000u } }
};

static void MSG_GetLegacyEntityFields( int protocol, const netField_t **field, int *numFields ) {
	if ( protocol < 46 ) {
		*field = legacyEntityStateFields43;
		*numFields = ARRAY_LEN( legacyEntityStateFields43 );
	} else if ( protocol == 46 ) {
		*field = legacyEntityStateFields46;
		*numFields = ARRAY_LEN( legacyEntityStateFields46 );
	} else {
		*field = legacyEntityStateFields48;
		*numFields = ARRAY_LEN( legacyEntityStateFields48 );
	}
}


// if (int)f == f and (int)f + ( 1<<(FLOAT_INT_BITS-1) ) < ( 1 << FLOAT_INT_BITS )
// the float will be sent with FLOAT_INT_BITS, otherwise all 32 bits will be sent
#define	FLOAT_INT_BITS	13
#define	FLOAT_INT_BIAS	(1<<(FLOAT_INT_BITS-1))

/*
==================
MSG_WriteDeltaEntity

Writes part of a packetentities message, including the entity number.
Can delta from either a baseline or a previous packet_entity
If to is NULL, a remove entity update will be sent
If force is not set, then nothing at all will be generated if the entity is
identical, under the assumption that the in-order delta code will catch it.
==================
*/
void MSG_WriteDeltaEntity( msg_t *msg, const entityState_t *from, const entityState_t *to, qboolean force ) {
	int			i, lc;
	int			numFields;
	const netField_t *field;
	int			trunc;
	float		fullFloat;
	const int	*fromF, *toF;
	size_t		serializedBytes;

	numFields = ARRAY_LEN( entityStateFields );

	serializedBytes = sizeof( from->number );
	for ( i = 0; i < numFields; i++ ) {
		assert( ( entityStateFields[i].offset & 3 ) == 0 );
		if ( entityStateFields[i].offset + 4 > serializedBytes ) {
			serializedBytes = entityStateFields[i].offset + 4;
		}
	}
	assert( serializedBytes <= sizeof( *from ) );

	// a NULL to is a delta remove message
	if ( to == NULL ) {
		if ( from == NULL ) {
			return;
		}
		MSG_WriteBits( msg, from->number, GENTITYNUM_BITS );
		MSG_WriteBits( msg, 1, 1 );
		return;
	}

	if ( to->number < 0 || to->number >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "MSG_WriteDeltaEntity: Bad entity number: %i", to->number );
	}

	lc = 0;
	// build the change vector as bytes so it is endian independent
	for ( i = 0, field = entityStateFields ; i < numFields ; i++, field++ ) {
		fromF = (int *)( (byte *)from + field->offset );
		toF = (int *)( (byte *)to + field->offset );
		if ( *fromF != *toF ) {
			lc = i+1;
		}
	}

	if ( lc == 0 ) {
		// nothing at all changed
		if ( !force ) {
			return;		// nothing at all
		}
		// write two bits for no change
		MSG_WriteBits( msg, to->number, GENTITYNUM_BITS );
		MSG_WriteBits( msg, 0, 1 );		// not removed
		MSG_WriteBits( msg, 0, 1 );		// no delta
		return;
	}

	MSG_WriteBits( msg, to->number, GENTITYNUM_BITS );
	MSG_WriteBits( msg, 0, 1 );			// not removed
	MSG_WriteBits( msg, 1, 1 );			// we have a delta

	MSG_WriteByte( msg, lc );	// # of changes

	for ( i = 0, field = entityStateFields ; i < lc ; i++, field++ ) {
		fromF = (int *)( (byte *)from + field->offset );
		toF = (int *)( (byte *)to + field->offset );

		if ( *fromF == *toF ) {
			MSG_WriteBits( msg, 0, 1 );	// no change
			continue;
		}

		MSG_WriteBits( msg, 1, 1 );	// changed

		if ( field->bits == 0 ) {
			// float
			fullFloat = *(const float *)toF;
			trunc = (int)fullFloat;

			if (fullFloat == 0.0f) {
				MSG_WriteBits( msg, 0, 1 );
			} else {
				MSG_WriteBits( msg, 1, 1 );
				if ( trunc == fullFloat && trunc + FLOAT_INT_BIAS >= 0 && 
					trunc + FLOAT_INT_BIAS < ( 1 << FLOAT_INT_BITS ) ) {
					// send as small integer
					MSG_WriteBits( msg, 0, 1 );
					MSG_WriteBits( msg, trunc + FLOAT_INT_BIAS, FLOAT_INT_BITS );
				} else {
					// send as full floating point value
					MSG_WriteBits( msg, 1, 1 );
					MSG_WriteBits( msg, *toF, 32 );
				}
			}
		} else {
			if (*toF == 0) {
				MSG_WriteBits( msg, 0, 1 );
			} else {
				MSG_WriteBits( msg, 1, 1 );
				// integer
				MSG_WriteBits( msg, *toF, field->bits );
			}
		}
	}
}

/*
==================
MSG_ReadDeltaEntity

The entity number has already been read from the message, which
is how the from state is identified.

If the delta removes the entity, entityState_t->number will be set to MAX_GENTITIES-1

Can go from either a baseline or a previous packet_entity
==================
*/
void MSG_ReadDeltaEntity( msg_t *msg, const entityState_t *from, entityState_t *to, int number ) {
	int			i, lc;
	int			numFields;
	const netField_t *field;
	const int	*fromF;
	int			*toF;
	int			print;
	int			trunc;
	int			startBit, endBit;

	if ( number < 0 || number >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "Bad delta entity number: %i", number );
	}

	if ( msg->bit == 0 ) {
		startBit = msg->readcount * 8 - GENTITYNUM_BITS;
	} else {
		startBit = ( msg->readcount - 1 ) * 8 + msg->bit - GENTITYNUM_BITS;
	}

	// check for a remove
	if ( MSG_ReadBits( msg, 1 ) == 1 ) {
		Com_Memset( to, 0, sizeof( *to ) );	
		to->number = MAX_GENTITIES - 1;
#ifndef DEDICATED
		if ( cl_shownet && ( cl_shownet->integer >= 2 || cl_shownet->integer == -1 ) ) {
			Com_Printf( "%3i: #%-3i remove\n", msg->readcount, number );
		}
#endif
		return;
	}

	// check for no delta
	if ( MSG_ReadBits( msg, 1 ) == 0 ) {
		*to = *from;
		to->number = number;
		return;
	}

	numFields = ARRAY_LEN( entityStateFields );
	lc = MSG_ReadByte(msg);

	if ( lc > numFields || lc < 0 ) {
		Com_Error( ERR_DROP, "invalid entityState field count" );
	}

	to->number = number;

#ifndef DEDICATED
	// shownet 2/3 will interleave with other printed info, -1 will
	// just print the delta records
	if ( cl_shownet && ( cl_shownet->integer >= 2 || cl_shownet->integer == -1 ) ) {
		print = 1;
		Com_Printf( "%3i: #%-3i ", msg->readcount, to->number );
	} else {
		print = 0;
	}
#else
		print = 0;
#endif

	for ( i = 0, field = entityStateFields ; i < lc ; i++, field++ ) {
		fromF = (const int *)( (const byte *)from + field->offset );
		toF = (int *)( (byte *)to + field->offset );

		if ( ! MSG_ReadBits( msg, 1 ) ) {
			// no change
			*toF = *fromF;
		} else {
			if ( field->bits == 0 ) {
				// float
				if ( MSG_ReadBits( msg, 1 ) == 0 ) {
						*(float *)toF = 0.0f; 
				} else {
					if ( MSG_ReadBits( msg, 1 ) == 0 ) {
						// integral float
						trunc = MSG_ReadBits( msg, FLOAT_INT_BITS );
						// bias to allow equal parts positive and negative
						trunc -= FLOAT_INT_BIAS;
						*(float *)toF = trunc; 
						if ( print ) {
							Com_Printf( "%s:%i ", field->name, trunc );
						}
					} else {
						// full floating point value
						*toF = MSG_ReadBits( msg, 32 );
						if ( print ) {
							Com_Printf( "%s:%f ", field->name, *(float *)toF );
						}
					}
				}
			} else {
				if ( MSG_ReadBits( msg, 1 ) == 0 ) {
					*toF = 0;
				} else {
					// integer
					*toF = MSG_ReadBits( msg, field->bits );
					if ( print ) {
						Com_Printf( "%s:%i ", field->name, *toF );
					}
				}
			}
//			pcount[i]++;
		}
	}
	for ( i = lc, field = &entityStateFields[lc] ; i < numFields ; i++, field++ ) {
		fromF = (int *)( (byte *)from + field->offset );
		toF = (int *)( (byte *)to + field->offset );
		// no change
		*toF = *fromF;
	}

	if ( print ) {
		if ( msg->bit == 0 ) {
			endBit = msg->readcount * 8 - GENTITYNUM_BITS;
		} else {
			endBit = ( msg->readcount - 1 ) * 8 + msg->bit - GENTITYNUM_BITS;
		}
		Com_Printf( " (%i bits)\n", endBit - startBit  );
	}
}

static qboolean MSG_LegacyChangeVectorHasBit( const legacyChangeVector_t *changeVector, int index ) {
	return ( changeVector->words[index >> 5] & ( 1u << ( index & 31 ) ) ) != 0
		? qtrue : qfalse;
}

void MSG_ReadDeltaEntityLegacy( msg_t *msg, const entityState_t *from, entityState_t *to, int number, int protocol ) {
	const netField_t *field;
	const legacyChangeVector_t *changeVector;
	legacyChangeVector_t rawChangeVector;
	entityState_t dummy;
	int i;
	int changeVectorNum;
	int numFields;
	int trunc;
	int *toF;

	if ( number < 0 || number >= MAX_GENTITIES ) {
		Com_Error( ERR_DROP, "Bad delta entity number: %i", number );
	}

	if ( !from ) {
		Com_Memset( &dummy, 0, sizeof( dummy ) );
		from = &dummy;
	}

	if ( MSG_ReadBits( msg, 1 ) == 1 ) {
		Com_Memset( to, 0, sizeof( *to ) );
		to->number = MAX_GENTITIES - 1;
		return;
	}

	if ( MSG_ReadBits( msg, 1 ) == 0 ) {
		*to = *from;
		to->number = number;
		return;
	}

	*to = *from;
	to->number = number;

	MSG_GetLegacyEntityFields( protocol, &field, &numFields );

	changeVectorNum = MSG_ReadBits( msg, 5 );
	if ( changeVectorNum == ARRAY_LEN( legacyEntityStateChangeVectors ) ) {
		Com_Memset( &rawChangeVector, 0, sizeof( rawChangeVector ) );
		for ( i = 0; i < numFields; i++ ) {
			if ( MSG_ReadBits( msg, 1 ) ) {
				rawChangeVector.words[i >> 5] |= 1u << ( i & 31 );
			}
		}
		changeVector = &rawChangeVector;
	} else {
		changeVector = &legacyEntityStateChangeVectors[ changeVectorNum ];
	}

	for ( i = 0; i < numFields; i++, field++ ) {
		if ( !MSG_LegacyChangeVectorHasBit( changeVector, i ) ) {
			continue;
		}

		toF = (int *)( (byte *)to + field->offset );
		if ( field->bits == 0 ) {
			if ( MSG_ReadBits( msg, 1 ) == 0 ) {
				trunc = MSG_ReadBits( msg, FLOAT_INT_BITS );
				trunc -= FLOAT_INT_BIAS;
				*(float *)toF = trunc;
			} else {
				*toF = MSG_ReadBits( msg, 32 );
			}
		} else {
			*toF = MSG_ReadBits( msg, field->bits );
		}
	}
}


/*
============================================================================

plyer_state_t communication

============================================================================
*/

// using the stringizing operator to save typing...
#define	PSF_OFFSET(x) offsetof( playerState_t, x )
#define	PSF(x) #x,PSF_OFFSET(x)

static const netField_t playerStateFields[] = 
{
{ PSF(commandTime), 32 },
{ PSF(origin[0]), 0 },
{ PSF(origin[1]), 0 },
{ PSF(bobCycle), 8 },
{ PSF(velocity[0]), 0 },
{ PSF(velocity[1]), 0 },
{ PSF(viewangles[1]), 0 },
{ PSF(viewangles[0]), 0 },
{ PSF(weaponTime), -16 },
{ PSF(origin[2]), 0 },
{ PSF(velocity[2]), 0 },
{ PSF(legsTimer), 8 },
{ PSF(pm_time), -16 },
{ PSF(eventSequence), 16 },
{ PSF(torsoAnim), 8 },
{ PSF(movementDir), 4 },
{ PSF(events[0]), 8 },
{ PSF(legsAnim), 8 },
{ PSF(events[1]), 8 },
{ PSF(pm_flags), 24 },
{ PSF(groundEntityNum), GENTITYNUM_BITS },
{ PSF(weaponstate), 4 },
{ PSF(eFlags), 16 },
{ PSF(externalEvent), 10 },
{ PSF(gravity), 16 },
{ PSF(speed), 16 },
{ PSF(delta_angles[1]), 16 },
{ PSF(externalEventParm), 8 },
{ PSF(viewheight), -8 },
{ PSF(damageEvent), 8 },
{ PSF(damageYaw), 8 },
{ PSF(damagePitch), 8 },
{ PSF(damageCount), 8 },
{ PSF(generic1), 8 },
{ PSF(pm_type), 8 },					
{ PSF(delta_angles[0]), 16 },
{ PSF(delta_angles[2]), 16 },
{ PSF(torsoTimer), 12 },
{ PSF(eventParms[0]), 8 },
{ PSF(eventParms[1]), 8 },
{ PSF(clientNum), 8 },
{ PSF(weapon), 5 },
{ PSF(weaponPrimary), 8 },
{ PSF(viewangles[2]), 0 },
{ PSF(grapplePoint[0]), 0 },
{ PSF(grapplePoint[1]), 0 },
{ PSF(grapplePoint[2]), 0 },
{ PSF(jumppad_ent), 10 },
{ PSF(loopSound), 16 },
{ PSF(jumpTime), 32 },
{ PSF(doubleJumped), 1 },
{ PSF(crouchTime), 32 },
{ PSF(crouchSlideTime), 32 },
{ PSF(location), 8 },
{ PSF(fov), 8 },
{ PSF(forwardmove), 8 },
{ PSF(rightmove), 8 },
{ PSF(upmove), 8 }
};

/*
===================
MSG_PlayerStateFieldIsSignedByte
===================
*/
static qboolean MSG_PlayerStateFieldIsSignedByte( const netField_t *field ) {
	return (qboolean)(
		field->offset == PSF_OFFSET(forwardmove) ||
		field->offset == PSF_OFFSET(rightmove) ||
		field->offset == PSF_OFFSET(upmove) );
}

/*
===================
MSG_PlayerStateFieldValue
===================
*/
static int MSG_PlayerStateFieldValue( const playerState_t *ps, const netField_t *field ) {
	if ( MSG_PlayerStateFieldIsSignedByte( field ) ) {
		return *(const signed char *)( (const byte *)ps + field->offset );
	}

	return *(const int *)( (const byte *)ps + field->offset );
}

/*
===================
MSG_PlayerStateFieldNetworkValue
===================
*/
static int MSG_PlayerStateFieldNetworkValue( const playerState_t *ps, const netField_t *field ) {
	const int value = MSG_PlayerStateFieldValue( ps, field );

	if ( MSG_PlayerStateFieldIsSignedByte( field ) && field->bits > 0 ) {
		return (unsigned char)value;
	}

	return value;
}

/*
===================
MSG_SetPlayerStateFieldValue
===================
*/
static void MSG_SetPlayerStateFieldValue( playerState_t *ps, const netField_t *field, int value ) {
	if ( MSG_PlayerStateFieldIsSignedByte( field ) ) {
		*(signed char *)( (byte *)ps + field->offset ) = (signed char)value;
		return;
	}

	*(int *)( (byte *)ps + field->offset ) = value;
}

static const netField_t legacyPlayerStateFields43[] =
{
{ PSF(commandTime), 32 },
{ PSF(pm_type), 8 },
{ PSF(bobCycle), 8 },
{ PSF(pm_flags), 16 },
{ PSF(pm_time), 16 },
{ PSF(origin[0]), 0 },
{ PSF(origin[1]), 0 },
{ PSF(origin[2]), 0 },
{ PSF(velocity[0]), 0 },
{ PSF(velocity[1]), 0 },
{ PSF(velocity[2]), 0 },
{ PSF(weaponTime), 16 },
{ PSF(gravity), 16 },
{ PSF(speed), 16 },
{ PSF(delta_angles[0]), 16 },
{ PSF(delta_angles[1]), 16 },
{ PSF(delta_angles[2]), 16 },
{ PSF(groundEntityNum), GENTITYNUM_BITS },
{ PSF(legsTimer), 8 },
{ PSF(torsoTimer), 12 },
{ PSF(legsAnim), 8 },
{ PSF(torsoAnim), 8 },
{ PSF(movementDir), 4 },
{ PSF(eFlags), 16 },
{ PSF(eventSequence), 16 },
{ PSF(events[0]), 8 },
{ PSF(events[1]), 8 },
{ PSF(eventParms[0]), 8 },
{ PSF(eventParms[1]), 8 },
{ PSF(externalEvent), 8 },
{ PSF(externalEventParm), 8 },
{ PSF(clientNum), 8 },
{ PSF(weapon), 5 },
{ PSF(weaponstate), 4 },
{ PSF(viewangles[0]), 0 },
{ PSF(viewangles[1]), 0 },
{ PSF(viewangles[2]), 0 },
{ PSF(viewheight), 8 },
{ PSF(damageEvent), 8 },
{ PSF(damageYaw), 8 },
{ PSF(damagePitch), 8 },
{ PSF(damageCount), 8 },
{ PSF(grapplePoint[0]), 0 },
{ PSF(grapplePoint[1]), 0 },
{ PSF(grapplePoint[2]), 0 }
};

static const netField_t legacyPlayerStateFields46[] =
{
{ PSF(commandTime), 32 },
{ PSF(pm_type), 8 },
{ PSF(bobCycle), 8 },
{ PSF(pm_flags), 16 },
{ PSF(pm_time), 16 },
{ PSF(origin[0]), 0 },
{ PSF(origin[1]), 0 },
{ PSF(origin[2]), 0 },
{ PSF(velocity[0]), 0 },
{ PSF(velocity[1]), 0 },
{ PSF(velocity[2]), 0 },
{ PSF(weaponTime), 16 },
{ PSF(gravity), 16 },
{ PSF(speed), 16 },
{ PSF(delta_angles[0]), 16 },
{ PSF(delta_angles[1]), 16 },
{ PSF(delta_angles[2]), 16 },
{ PSF(groundEntityNum), GENTITYNUM_BITS },
{ PSF(legsTimer), 8 },
{ PSF(torsoTimer), 12 },
{ PSF(legsAnim), 8 },
{ PSF(torsoAnim), 8 },
{ PSF(movementDir), 4 },
{ PSF(eFlags), 16 },
{ PSF(eventSequence), 16 },
{ PSF(events[0]), 8 },
{ PSF(events[1]), 8 },
{ PSF(eventParms[0]), 8 },
{ PSF(eventParms[1]), 8 },
{ PSF(externalEvent), 10 },
{ PSF(externalEventParm), 8 },
{ PSF(clientNum), 8 },
{ PSF(weapon), 5 },
{ PSF(weaponstate), 4 },
{ PSF(viewangles[0]), 0 },
{ PSF(viewangles[1]), 0 },
{ PSF(viewangles[2]), 0 },
{ PSF(viewheight), -8 },
{ PSF(damageEvent), 8 },
{ PSF(damageYaw), 8 },
{ PSF(damagePitch), 8 },
{ PSF(damageCount), 8 },
{ PSF(grapplePoint[0]), 0 },
{ PSF(grapplePoint[1]), 0 },
{ PSF(grapplePoint[2]), 0 },
{ PSF(jumppad_ent), GENTITYNUM_BITS },
{ PSF(loopSound), 16 }
};

static const netField_t legacyPlayerStateFields48[] =
{
{ PSF(commandTime), 32 },
{ PSF(pm_type), 8 },
{ PSF(bobCycle), 8 },
{ PSF(pm_flags), 16 },
{ PSF(pm_time), -16 },
{ PSF(origin[0]), 0 },
{ PSF(origin[1]), 0 },
{ PSF(origin[2]), 0 },
{ PSF(velocity[0]), 0 },
{ PSF(velocity[1]), 0 },
{ PSF(velocity[2]), 0 },
{ PSF(weaponTime), -16 },
{ PSF(gravity), 16 },
{ PSF(speed), 16 },
{ PSF(delta_angles[0]), 16 },
{ PSF(delta_angles[1]), 16 },
{ PSF(delta_angles[2]), 16 },
{ PSF(groundEntityNum), GENTITYNUM_BITS },
{ PSF(legsTimer), 8 },
{ PSF(torsoTimer), 12 },
{ PSF(legsAnim), 8 },
{ PSF(torsoAnim), 8 },
{ PSF(movementDir), 4 },
{ PSF(eFlags), 16 },
{ PSF(eventSequence), 16 },
{ PSF(events[0]), 8 },
{ PSF(events[1]), 8 },
{ PSF(eventParms[0]), 8 },
{ PSF(eventParms[1]), 8 },
{ PSF(externalEvent), 10 },
{ PSF(externalEventParm), 8 },
{ PSF(clientNum), 8 },
{ PSF(weapon), 5 },
{ PSF(weaponstate), 4 },
{ PSF(viewangles[0]), 0 },
{ PSF(viewangles[1]), 0 },
{ PSF(viewangles[2]), 0 },
{ PSF(viewheight), -8 },
{ PSF(damageEvent), 8 },
{ PSF(damageYaw), 8 },
{ PSF(damagePitch), 8 },
{ PSF(damageCount), 8 },
{ PSF(grapplePoint[0]), 0 },
{ PSF(grapplePoint[1]), 0 },
{ PSF(grapplePoint[2]), 0 },
{ PSF(jumppad_ent), GENTITYNUM_BITS },
{ PSF(loopSound), 16 },
{ PSF(generic1), 8 }
};

static void MSG_GetLegacyPlayerFields( int protocol, const netField_t **field, int *numFields ) {
	if ( protocol < 46 ) {
		*field = legacyPlayerStateFields43;
		*numFields = ARRAY_LEN( legacyPlayerStateFields43 );
	} else if ( protocol == 46 ) {
		*field = legacyPlayerStateFields46;
		*numFields = ARRAY_LEN( legacyPlayerStateFields46 );
	} else {
		*field = legacyPlayerStateFields48;
		*numFields = ARRAY_LEN( legacyPlayerStateFields48 );
	}
}

/*
=============
MSG_WriteDeltaPlayerstate

=============
*/
void MSG_WriteDeltaPlayerstate( msg_t *msg, const playerState_t *from, const playerState_t *to ) {
	static const playerState_t dummy{};
	int				i;
	int				statsbits;
	int				persistantbits;
	int				ammobits;
	int				powerupbits;
	int				numFields;
	const netField_t *field;
	const int		*toF;
	int				fromValue, toValue;
	float			fullFloat;
	int				trunc, lc;

	if ( !from ) {
		from = &dummy;
	}

	numFields = ARRAY_LEN( playerStateFields );

	lc = 0;
	for ( i = 0, field = playerStateFields ; i < numFields ; i++, field++ ) {
		fromValue = MSG_PlayerStateFieldValue( from, field );
		toValue = MSG_PlayerStateFieldValue( to, field );
		if ( fromValue != toValue ) {
			lc = i+1;
		}
	}

	MSG_WriteByte( msg, lc );	// # of changes

	for ( i = 0, field = playerStateFields ; i < lc ; i++, field++ ) {
		fromValue = MSG_PlayerStateFieldValue( from, field );
		toValue = MSG_PlayerStateFieldValue( to, field );

		if ( fromValue == toValue ) {
			MSG_WriteBits( msg, 0, 1 );	// no change
			continue;
		}

		MSG_WriteBits( msg, 1, 1 );	// changed
//		pcount[i]++;

		if ( field->bits == 0 ) {
			// float
			toF = (const int *)( (byte *)to + field->offset );
			fullFloat = *(const float *)toF;
			trunc = (int)fullFloat;

			if ( trunc == fullFloat && trunc + FLOAT_INT_BIAS >= 0 && 
				trunc + FLOAT_INT_BIAS < ( 1 << FLOAT_INT_BITS ) ) {
				// send as small integer
				MSG_WriteBits( msg, 0, 1 );
				MSG_WriteBits( msg, trunc + FLOAT_INT_BIAS, FLOAT_INT_BITS );
			} else {
				// send as full floating point value
				MSG_WriteBits( msg, 1, 1 );
				MSG_WriteBits( msg, *toF, 32 );
			}
		} else {
			// integer
			MSG_WriteBits( msg, MSG_PlayerStateFieldNetworkValue( to, field ), field->bits );
		}
	}


	//
	// send the arrays
	//
	statsbits = 0;
	for (i=0 ; i<MAX_STATS ; i++) {
		if (to->stats[i] != from->stats[i]) {
			statsbits |= 1<<i;
		}
	}
	persistantbits = 0;
	for (i=0 ; i<MAX_PERSISTANT ; i++) {
		if (to->persistant[i] != from->persistant[i]) {
			persistantbits |= 1<<i;
		}
	}
	ammobits = 0;
	for (i=0 ; i<MAX_WEAPONS ; i++) {
		if (to->ammo[i] != from->ammo[i]) {
			ammobits |= 1<<i;
		}
	}
	powerupbits = 0;
	for (i=0 ; i<MAX_POWERUPS ; i++) {
		if (to->powerups[i] != from->powerups[i]) {
			powerupbits |= 1<<i;
		}
	}

	if (!statsbits && !persistantbits && !ammobits && !powerupbits) {
		MSG_WriteBits( msg, 0, 1 );	// no change
		return;
	}
	MSG_WriteBits( msg, 1, 1 );	// changed

	if ( statsbits ) {
		MSG_WriteBits( msg, 1, 1 );	// changed
		MSG_WriteBits( msg, statsbits, MAX_STATS );
		for (i=0 ; i<MAX_STATS ; i++)
			if (statsbits & (1<<i) )
				MSG_WriteShort (msg, to->stats[i]);
	} else {
		MSG_WriteBits( msg, 0, 1 );	// no change
	}


	if ( persistantbits ) {
		MSG_WriteBits( msg, 1, 1 );	// changed
		MSG_WriteBits( msg, persistantbits, MAX_PERSISTANT );
		for (i=0 ; i<MAX_PERSISTANT ; i++)
			if (persistantbits & (1<<i) )
				MSG_WriteShort (msg, to->persistant[i]);
	} else {
		MSG_WriteBits( msg, 0, 1 );	// no change
	}


	if ( ammobits ) {
		MSG_WriteBits( msg, 1, 1 );	// changed
		MSG_WriteBits( msg, ammobits, MAX_WEAPONS );
		for (i=0 ; i<MAX_WEAPONS ; i++)
			if (ammobits & (1<<i) )
				MSG_WriteShort (msg, to->ammo[i]);
	} else {
		MSG_WriteBits( msg, 0, 1 );	// no change
	}


	if ( powerupbits ) {
		MSG_WriteBits( msg, 1, 1 );	// changed
		MSG_WriteBits( msg, powerupbits, MAX_POWERUPS );
		for (i=0 ; i<MAX_POWERUPS ; i++)
			if (powerupbits & (1<<i) )
				MSG_WriteLong( msg, to->powerups[i] );
	} else {
		MSG_WriteBits( msg, 0, 1 );	// no change
	}
}


/*
===================
MSG_ReadDeltaPlayerstate
===================
*/
void MSG_ReadDeltaPlayerstate( msg_t *msg, const playerState_t *from, playerState_t *to ) {
	int			i, lc;
	int			bits;
	const netField_t *field;
	int			numFields;
	int			startBit, endBit;
	int			print;
	int			*toF;
	int			value;
	int			trunc;
	playerState_t	dummy;

	if ( !from ) {
		from = &dummy;
		Com_Memset( &dummy, 0, sizeof( dummy ) );
	}
	*to = *from;

	if ( msg->bit == 0 ) {
		startBit = msg->readcount * 8 - GENTITYNUM_BITS;
	} else {
		startBit = ( msg->readcount - 1 ) * 8 + msg->bit - GENTITYNUM_BITS;
	}

#ifndef DEDICATED	
	// shownet 2/3 will interleave with other printed info, -2 will
	// just print the delta records
	if ( cl_shownet && ( cl_shownet->integer >= 2 || cl_shownet->integer == -2 ) ) {
		print = 1;
		Com_Printf( "%3i: playerstate ", msg->readcount );
	} else {
		print = 0;
	}
#else
		print = 0;
#endif

	numFields = ARRAY_LEN( playerStateFields );
	lc = MSG_ReadByte(msg);

	if ( lc > numFields || lc < 0 ) {
		Com_Error( ERR_DROP, "invalid playerState field count" );
	}

	for ( i = 0, field = playerStateFields ; i < lc ; i++, field++ ) {
		if ( ! MSG_ReadBits( msg, 1 ) ) {
			// no change
			MSG_SetPlayerStateFieldValue( to, field, MSG_PlayerStateFieldValue( from, field ) );
		} else {
			if ( field->bits == 0 ) {
				// float
				toF = (int *)( (byte *)to + field->offset );
				if ( MSG_ReadBits( msg, 1 ) == 0 ) {
					// integral float
					trunc = MSG_ReadBits( msg, FLOAT_INT_BITS );
					// bias to allow equal parts positive and negative
					trunc -= FLOAT_INT_BIAS;
					*(float *)toF = trunc; 
					if ( print ) {
						Com_Printf( "%s:%i ", field->name, trunc );
					}
				} else {
					// full floating point value
					*toF = MSG_ReadBits( msg, 32 );
					if ( print ) {
						Com_Printf( "%s:%f ", field->name, *(float *)toF );
					}
				}
			} else {
				// integer
				value = MSG_ReadBits( msg, field->bits );
				MSG_SetPlayerStateFieldValue( to, field, value );
				if ( print ) {
					Com_Printf( "%s:%i ", field->name, MSG_PlayerStateFieldValue( to, field ) );
				}
			}
		}
	}
	for ( i=lc,field = &playerStateFields[lc];i<numFields; i++, field++) {
		// no change
		MSG_SetPlayerStateFieldValue( to, field, MSG_PlayerStateFieldValue( from, field ) );
	}


	// read the arrays
	if (MSG_ReadBits( msg, 1 ) ) {
		// parse stats
		if ( MSG_ReadBits( msg, 1 ) ) {
			LOG("PS_STATS");
			bits = MSG_ReadBits (msg, MAX_STATS);
			for (i=0 ; i<MAX_STATS ; i++) {
				if (bits & (1<<i) ) {
					to->stats[i] = MSG_ReadShort(msg);
				}
			}
		}

		// parse persistant stats
		if ( MSG_ReadBits( msg, 1 ) ) {
			LOG("PS_PERSISTANT");
			bits = MSG_ReadBits (msg, MAX_PERSISTANT);
			for (i=0 ; i<MAX_PERSISTANT ; i++) {
				if (bits & (1<<i) ) {
					to->persistant[i] = MSG_ReadShort(msg);
				}
			}
		}

		// parse ammo
		if ( MSG_ReadBits( msg, 1 ) ) {
			LOG("PS_AMMO");
			bits = MSG_ReadBits (msg, MAX_WEAPONS);
			for (i=0 ; i<MAX_WEAPONS ; i++) {
				if (bits & (1<<i) ) {
					to->ammo[i] = MSG_ReadShort(msg);
				}
			}
		}

		// parse powerups
		if ( MSG_ReadBits( msg, 1 ) ) {
			LOG("PS_POWERUPS");
			bits = MSG_ReadBits (msg, MAX_POWERUPS);
			for (i=0 ; i<MAX_POWERUPS ; i++) {
				if (bits & (1<<i) ) {
					to->powerups[i] = MSG_ReadLong(msg);
				}
			}
		}
	}

	if ( print ) {
		if ( msg->bit == 0 ) {
			endBit = msg->readcount * 8 - GENTITYNUM_BITS;
		} else {
			endBit = ( msg->readcount - 1 ) * 8 + msg->bit - GENTITYNUM_BITS;
		}
		Com_Printf( " (%i bits)\n", endBit - startBit  );
	}
}

void MSG_ReadDeltaPlayerstateLegacy( msg_t *msg, const playerState_t *from, playerState_t *to, int protocol ) {
	int i;
	int trunc;
	int *toF;
	const netField_t *field;
	int numFields;
	int mask;
	playerState_t dummy;

	if ( !from ) {
		from = &dummy;
		Com_Memset( &dummy, 0, sizeof( dummy ) );
	}
	*to = *from;

	MSG_GetLegacyPlayerFields( protocol, &field, &numFields );

	for ( i = 0; i < numFields; i++, field++ ) {
		if ( !MSG_ReadBits( msg, 1 ) ) {
			continue;
		}

		toF = (int *)( (byte *)to + field->offset );
		if ( field->bits == 0 ) {
			if ( MSG_ReadBits( msg, 1 ) == 0 ) {
				trunc = MSG_ReadBits( msg, FLOAT_INT_BITS );
				trunc -= FLOAT_INT_BIAS;
				*(float *)toF = trunc;
			} else {
				*toF = MSG_ReadBits( msg, 32 );
			}
		} else {
			*toF = MSG_ReadBits( msg, field->bits );
		}
	}

	if ( MSG_ReadBits( msg, 1 ) ) {
		mask = MSG_ReadBits( msg, MAX_STATS );
		for ( i = 0; i < MAX_STATS; i++ ) {
			if ( mask & ( 1 << i ) ) {
				to->stats[i] = MSG_ReadBits( msg, -16 );
			}
		}
	}

	if ( MSG_ReadBits( msg, 1 ) ) {
		mask = MSG_ReadBits( msg, MAX_PERSISTANT );
		for ( i = 0; i < MAX_PERSISTANT; i++ ) {
			if ( mask & ( 1 << i ) ) {
				to->persistant[i] = MSG_ReadBits( msg, 16 );
			}
		}
	}

	if ( MSG_ReadBits( msg, 1 ) ) {
		mask = MSG_ReadBits( msg, MAX_WEAPONS );
		for ( i = 0; i < MAX_WEAPONS; i++ ) {
			if ( mask & ( 1 << i ) ) {
				to->ammo[i] = MSG_ReadBits( msg, 16 );
			}
		}
	}

	if ( MSG_ReadBits( msg, 1 ) ) {
		mask = MSG_ReadBits( msg, MAX_POWERUPS );
		for ( i = 0; i < MAX_POWERUPS; i++ ) {
			if ( mask & ( 1 << i ) ) {
				to->powerups[i] = MSG_ReadBits( msg, 32 );
			}
		}
	}
}

//===========================================================================
