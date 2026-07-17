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
#include "net_address.hpp"
#include "netchan_safety.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

/*

packet header
-------------
4	outgoing sequence.  high bit will be set if this is a fragmented message
[2	qport (only for client to server)]
[2	fragment start byte]
[2	fragment length. if < FRAGMENT_SIZE, this is the last fragment]

if the sequence number is -1, the packet should be handled as an out-of-band
message instead of as part of a netcon.

All fragments will have the same sequence numbers.

The qport field is a workaround for bad address translating routers that
sometimes remap the client's source port on a packet during gameplay.

If the base part of the net address matches and the qport matches, then the
channel matches even if the IP port differs.  The IP port should be updated
to the new value before sending out any replies.

*/

namespace {

constexpr int FragmentSize = MAX_PACKETLEN - 100;
constexpr std::uint32_t FragmentBit = 1u << 31;

[[nodiscard]] constexpr int SequenceHeader( int sequence, bool fragmented ) noexcept {
	const std::uint32_t bits = static_cast<std::uint32_t>( sequence ) |
		( fragmented ? FragmentBit : 0u );
	return static_cast<int>( bits );
}

[[nodiscard]] constexpr int SequenceChecksum( int challenge,
	int sequence ) noexcept {
	return static_cast<int>( fnql::net::SequenceChecksum( challenge, sequence ) );
}

[[nodiscard]] bool IsConnectionlessPacket( const void *data, int length ) noexcept {
	if ( !data || length < static_cast<int>( sizeof( std::int32_t ) ) ) {
		return false;
	}

	std::int32_t header = 0;
	std::memcpy( &header, data, sizeof( header ) );
	return header == -1;
}

void ReportInvalidPacket( const char *operation, int length ) {
	Com_Printf( S_COLOR_YELLOW "%s: rejected invalid packet length %d\n",
		operation, length );
}

} // namespace

cvar_t		*showpackets;
cvar_t		*showdrop;
cvar_t		*qport;

static const char *netsrcString[2] = {
	"client",
	"server"
};

/*
===============
Netchan_Init

===============
*/
void Netchan_Init( int port ) {
	port &= 0xffff;
	showpackets = Cvar_Get ("showpackets", "0", CVAR_TEMP );
	Cvar_SetDescription( showpackets, "Toggles information of all packets sent and received." );
	showdrop = Cvar_Get ("showdrop", "0", CVAR_TEMP );
	Cvar_SetDescription( showdrop, "Toggles information of dropped packet traffic." );
	qport = Cvar_Get ("net_qport", va("%i", port), CVAR_INIT );
	Cvar_SetDescription( qport, "Set internal network port. This allows more than one person to play from behind a NAT router by using only one IP address." );
}


/*
==============
Netchan_Setup

called to open a channel to a remote system
==============
*/
void Netchan_Setup( netsrc_t sock, netchan_t *chan, const netadr_t *adr, int port,
	int challenge, netchanWireProfile_t wireProfile )
{
	if ( !chan || !adr ) {
		Com_Error( ERR_DROP, "%s: null channel or address", __func__ );
		return;
	}

	Com_Memset (chan, 0, sizeof(*chan));
	
	chan->sock = sock;
	chan->remoteAddress = *adr;
	chan->qport = port;
	chan->incomingSequence = 0;
	chan->outgoingSequence = 1;
	chan->challenge = challenge;
	chan->wireProfile = wireProfile;
	chan->isLANAddress = Sys_IsLANAddress( adr );
}


/*
=================
Netchan_TransmitNextFragment

Send one fragment of the current message
=================
*/
void Netchan_TransmitNextFragment( netchan_t *chan ) {
	msg_t		send;
	byte		send_buf[MAX_PACKETLEN+8];
	int			fragmentLength;
	int			outgoingSequence;

	if ( !chan || !chan->unsentFragments || chan->unsentFragmentStart < 0 ||
		chan->unsentLength < 0 || chan->unsentLength > MAX_MSGLEN ||
		chan->unsentFragmentStart > chan->unsentLength ) {
		Com_Error( ERR_DROP, "%s: invalid fragment state", __func__ );
		return;
	}

	// write the packet header
	MSG_InitOOB( &send, send_buf, sizeof(send_buf)-8 );

	outgoingSequence = SequenceHeader( chan->outgoingSequence, true );
	MSG_WriteLong( &send, outgoingSequence );

	// send the qport if we are a client
	if ( chan->sock == NS_CLIENT && Netchan_WireHasFeature(
		chan->wireProfile, NETCHAN_FEATURE_CLIENT_QPORT ) ) {
		MSG_WriteShort( &send, qport->integer );
	}

	if ( Netchan_WireHasFeature( chan->wireProfile,
		NETCHAN_FEATURE_SEQUENCE_CHECKSUM ) )
		MSG_WriteLong( &send, SequenceChecksum( chan->challenge,
			chan->outgoingSequence ) );

	// copy the reliable message to the packet first
	fragmentLength = FragmentSize;
	if ( chan->unsentFragmentStart + fragmentLength > chan->unsentLength ) {
		fragmentLength = chan->unsentLength - chan->unsentFragmentStart;
	}

	MSG_WriteShort( &send, chan->unsentFragmentStart );
	MSG_WriteShort( &send, fragmentLength );
	MSG_WriteData( &send, chan->unsentBuffer + chan->unsentFragmentStart, fragmentLength );

	// send the datagram
	NET_SendPacket( chan->sock, send.cursize, send.data, &chan->remoteAddress );

	// Store send time and size of this packet for rate control
	chan->lastSentTime = Sys_Milliseconds();
	chan->lastSentSize = send.cursize;

	if ( showpackets->integer ) {
		Com_Printf ("%s send %4i : s=%i fragment=%i,%i\n"
			, netsrcString[ chan->sock ]
			, send.cursize
			, chan->outgoingSequence
			, chan->unsentFragmentStart, fragmentLength);
	}

	chan->unsentFragmentStart += fragmentLength;

	// this exit condition is a little tricky, because a packet
	// that is exactly the fragment length still needs to send
	// a second packet of zero length so that the other side
	// can tell there aren't more to follow
	if ( chan->unsentFragmentStart == chan->unsentLength && fragmentLength != FragmentSize ) {
		chan->outgoingSequence = fnql::net::NextSequence( chan->outgoingSequence );
		chan->unsentFragments = qfalse;
	}
}


/*
=================
EnqueueFragments
=================
*/
static void Netchan_EnqueueFragments( const netchan_t *chan, const int length, const byte *data ) {
	msg_t		send;
	byte		send_buf[MAX_PACKETLEN + 8];
	int			fragmentLength;
	int			unsentFragmentStart = 0;

	for ( ;; ) {
		// write the packet header
		MSG_InitOOB( &send, send_buf, sizeof( send_buf ) - 8 );

		MSG_WriteLong( &send, SequenceHeader( chan->outgoingSequence, true ) );

		// send the qport if we are a client
		if ( chan->sock == NS_CLIENT && Netchan_WireHasFeature(
			chan->wireProfile, NETCHAN_FEATURE_CLIENT_QPORT ) ) {
			MSG_WriteShort( &send, qport->integer );
		}

		if ( Netchan_WireHasFeature( chan->wireProfile,
			NETCHAN_FEATURE_SEQUENCE_CHECKSUM ) ) {
			MSG_WriteLong( &send, SequenceChecksum( chan->challenge,
				chan->outgoingSequence ) );
		}

		// copy the reliable message to the packet first
		fragmentLength = FragmentSize;
		if ( unsentFragmentStart + fragmentLength > length ) {
			fragmentLength = length - unsentFragmentStart;
		}

		MSG_WriteShort( &send, unsentFragmentStart );
		MSG_WriteShort( &send, fragmentLength );
		MSG_WriteData( &send, data + unsentFragmentStart, fragmentLength );

		// enqueue the datagram
		NET_QueuePacket( chan->sock, send.cursize, send.data, &chan->remoteAddress, 0 /*offset*/ );

		// TODO: add showpackets debug info

		unsentFragmentStart += fragmentLength;

		// this exit condition is a little tricky, because a packet
		// that is exactly the fragment length still needs to send
		// a second packet of zero length so that the other side
		// can tell there aren't more to follow
		if ( unsentFragmentStart == length && fragmentLength != FragmentSize ) {
			break;
		}
	}
}


/*
===============
Netchan_Transmit

Sends a message to a connection, fragmenting if necessary
A 0 length will still generate a packet.
================
*/
void Netchan_Transmit( netchan_t *chan, int length, const byte *data ) {
	msg_t		send;
	byte		send_buf[MAX_PACKETLEN+8];

	if ( !fnql::net::IsValidPayload( data, length, MAX_MSGLEN ) ) {
		Com_Error( ERR_DROP, "%s: length = %i", __func__, length );
		return;
	}

	chan->unsentFragmentStart = 0;

	// fragment large reliable messages
	if ( length >= FragmentSize ) {
		chan->unsentFragments = qtrue;
		chan->unsentLength = length;
		Com_Memcpy( chan->unsentBuffer, data, length );

		// only send the first fragment now
		Netchan_TransmitNextFragment( chan );
		return;
	}

	// write the packet header
	MSG_InitOOB( &send, send_buf, sizeof(send_buf)-8 );

	MSG_WriteLong( &send, chan->outgoingSequence );

	// send the qport if we are a client
	if ( chan->sock == NS_CLIENT && Netchan_WireHasFeature(
		chan->wireProfile, NETCHAN_FEATURE_CLIENT_QPORT ) )
		MSG_WriteShort( &send, qport->integer );

	if ( Netchan_WireHasFeature( chan->wireProfile,
		NETCHAN_FEATURE_SEQUENCE_CHECKSUM ) )
		MSG_WriteLong( &send, SequenceChecksum( chan->challenge,
			chan->outgoingSequence ) );

	chan->outgoingSequence = fnql::net::NextSequence( chan->outgoingSequence );

	MSG_WriteData( &send, data, length );

	// send the datagram
	NET_SendPacket( chan->sock, send.cursize, send.data, &chan->remoteAddress );

	// Store send time and size of this packet for rate control
	chan->lastSentTime = Sys_Milliseconds();
	chan->lastSentSize = send.cursize;

	if ( showpackets->integer ) {
		Com_Printf( "%s send %4i : s=%i ack=%i\n"
			, netsrcString[ chan->sock ]
			, send.cursize
			, chan->outgoingSequence - 1
			, chan->incomingSequence );
	}
}


/*
===============
Netchan_Enqueue

Enqueue a message to a queue#1, fragmenting if necessary
A 0 length will still generate a packet.
================
*/
void Netchan_Enqueue( netchan_t *chan, int length, const byte *data ) {
	byte		send_buf[MAX_PACKETLEN + 8];
	msg_t		send;

	if ( !fnql::net::IsValidPayload( data, length, MAX_MSGLEN ) ) {
		Com_Error( ERR_DROP, "%s: length = %i", __func__, length );
		return;
	}

	// fragment large reliable messages
	if ( length >= FragmentSize ) {
		Netchan_EnqueueFragments( chan, length, data );
		return;
	}

	// write the packet header
	MSG_InitOOB( &send, send_buf, sizeof( send_buf ) - 8 );

	MSG_WriteLong( &send, chan->outgoingSequence );

	// send the qport if we are a client
	if ( chan->sock == NS_CLIENT && Netchan_WireHasFeature(
		chan->wireProfile, NETCHAN_FEATURE_CLIENT_QPORT ) )
		MSG_WriteShort( &send, qport->integer );

	if ( Netchan_WireHasFeature( chan->wireProfile,
		NETCHAN_FEATURE_SEQUENCE_CHECKSUM ) )
		MSG_WriteLong( &send, SequenceChecksum( chan->challenge,
			chan->outgoingSequence ) );

	MSG_WriteData( &send, data, length );

	// enqueue the datagram
	NET_QueuePacket( chan->sock, send.cursize, send.data, &chan->remoteAddress, 0 /*offset*/ );

	// TODO: add showpackets debug info
}


/*
=================
Netchan_Process

Returns qfalse if the message should not be processed due to being
out of order or a fragment.

Msg must be large enough to hold MAX_MSGLEN, because if this is the
final fragment of a multi-part message, the entire thing will be
copied out.
=================
*/
qboolean Netchan_Process( netchan_t *chan, msg_t *msg ) {
	int			sequence;
	int			fragmentStart, fragmentLength;
	qboolean	fragmented;

	if ( !chan || !msg || !msg->data || msg->maxsize < 0 || msg->cursize < 4 ||
		msg->cursize > msg->maxsize ) {
		return qfalse;
	}

	// XOR unscramble all data in the packet after the header
//	Netchan_UnScramblePacket( msg );

	// get sequence numbers		
	MSG_BeginReadingOOB( msg );
	sequence = MSG_ReadLong( msg );

	// check for fragment information
	if ( static_cast<std::uint32_t>( sequence ) & FragmentBit ) {
		sequence = static_cast<int>(
			static_cast<std::uint32_t>( sequence ) & fnql::net::SequenceMask );
		fragmented = qtrue;
	} else {
		fragmented = qfalse;
	}

	// read the qport if we are a server
	if ( chan->sock == NS_SERVER && Netchan_WireHasFeature(
		chan->wireProfile, NETCHAN_FEATURE_CLIENT_QPORT ) ) {
		/*qport=*/ MSG_ReadShort( msg );
	}

	if ( Netchan_WireHasFeature( chan->wireProfile,
		NETCHAN_FEATURE_SEQUENCE_CHECKSUM ) ) {
		int checksum = MSG_ReadLong( msg );

		// UDP spoofing protection
		if ( msg->readcount > msg->cursize ||
			SequenceChecksum( chan->challenge, sequence ) != checksum )
			return qfalse;
	}
	else if ( msg->readcount > msg->cursize ) {
		return qfalse;
	}

	// read the fragment information
	if ( fragmented ) {
		fragmentStart = MSG_ReadShort( msg );
		fragmentLength = MSG_ReadShort( msg );
		if ( msg->readcount > msg->cursize ) {
			return qfalse;
		}
	} else {
		fragmentStart = 0;		// stop warning message
		fragmentLength = 0;
	}

	if ( showpackets->integer ) {
		if ( fragmented ) {
			Com_Printf( "%s recv %4i : s=%i fragment=%i,%i\n"
				, netsrcString[ chan->sock ]
				, msg->cursize
				, sequence
				, fragmentStart, fragmentLength );
		} else {
			Com_Printf( "%s recv %4i : s=%i\n"
				, netsrcString[ chan->sock ]
				, msg->cursize
				, sequence );
		}
	}

	//
	// discard out of order or duplicated packets
	//
	if ( !fnql::net::IsNewerSequence( sequence, chan->incomingSequence ) ) {
		if ( showdrop->integer || showpackets->integer ) {
			Com_Printf( "%s:Out of order packet %i at %i\n"
				, NET_AdrToString( &chan->remoteAddress )
				,  sequence
				, chan->incomingSequence );
		}
		return qfalse;
	}

	//
	// dropped packets don't keep the message from being used
	//
	chan->dropped = static_cast<int>(
		fnql::net::SequenceDistance( sequence, chan->incomingSequence ) - 1u );
	if ( chan->dropped > 0 ) {
		if ( showdrop->integer || showpackets->integer ) {
			Com_Printf( "%s:Dropped %i packets at %i\n"
			, NET_AdrToString( &chan->remoteAddress )
			, chan->dropped
			, sequence );
		}
	}
	

	//
	// if this is the final fragment of a reliable message,
	// bump incoming_reliable_sequence 
	//
	if ( fragmented ) {
		// TTimo
		// make sure we add the fragments in correct order
		// either a packet was dropped, or we received this one too soon
		// we don't reconstruct the fragments. we will wait till this fragment gets to us again
		// (NOTE: we could probably try to rebuild by out of order chunks if needed)
		if ( sequence != chan->fragmentSequence ) {
			chan->fragmentSequence = sequence;
			chan->fragmentLength = 0;
		}

		// if we missed a fragment, dump the message
		if ( fragmentStart != chan->fragmentLength ) {
			if ( showdrop->integer || showpackets->integer ) {
				Com_Printf( "%s:Dropped a message fragment\n"
				, NET_AdrToString( &chan->remoteAddress ));
			}
			// we can still keep the part that we have so far,
			// so we don't need to clear chan->fragmentLength
			return qfalse;
		}

		// copy the fragment to the fragment buffer
		if ( !fnql::net::FragmentFits( msg->readcount, msg->cursize,
			chan->fragmentLength, sizeof( chan->fragmentBuffer ), fragmentLength ) ) {
			if ( showdrop->integer || showpackets->integer ) {
				Com_Printf ("%s:illegal fragment length\n"
				, NET_AdrToString( &chan->remoteAddress ) );
			}
			return qfalse;
		}

		Com_Memcpy( chan->fragmentBuffer + chan->fragmentLength, 
			msg->data + msg->readcount, fragmentLength );

		chan->fragmentLength += fragmentLength;

		// if this wasn't the last fragment, don't process anything
		if ( fragmentLength == FragmentSize ) {
			return qfalse;
		}

		if ( chan->fragmentLength > msg->maxsize ) {
			Com_Printf( "%s:fragmentLength %i > msg->maxsize\n"
				, NET_AdrToString( &chan->remoteAddress ),
				chan->fragmentLength );
			return qfalse;
		}

		// copy the full message over the partial fragment

		// make sure the sequence number is still there
		const std::int32_t sequenceHeader = LittleLong( sequence );
		Com_Memcpy( msg->data, &sequenceHeader, sizeof( sequenceHeader ) );

		Com_Memcpy( msg->data + 4, chan->fragmentBuffer, chan->fragmentLength );
		msg->cursize = chan->fragmentLength + 4;
		chan->fragmentLength = 0;
		msg->readcount = 4;	// past the sequence number
		msg->bit = 32;	// past the sequence number

		// TTimo
		// clients were not acking fragmented messages
		chan->incomingSequence = sequence;
		
		return qtrue;
	}

	//
	// the message can now be read from the current message pointer
	//
	chan->incomingSequence = sequence;

	return qtrue;
}


//==============================================================================


/*
=============================================================================

LOOPBACK BUFFERS FOR LOCAL PLAYER

=============================================================================
*/
#ifndef DEDICATED

// there needs to be enough loopback messages to hold a complete
// gamestate of maximum size
#define	MAX_LOOPBACK	32

typedef struct {
	byte	data[MAX_MSGLEN];
	int		datalen;
} loopmsg_t;

typedef struct {
	loopmsg_t	msgs[MAX_LOOPBACK];
	std::uint32_t	get, send;
} loopback_t;

static loopback_t loopbacks[2]; // NS_CLIENT, NS_SERVER


qboolean NET_GetLoopPacket( netsrc_t sock, netadr_t *net_from, msg_t *net_message )
{
	int		i;
	loopback_t	*loop;

	if ( sock < NS_CLIENT || sock > NS_SERVER || !net_from || !net_message ||
		!net_message->data || net_message->maxsize < 0 ) {
		return qfalse;
	}

	loop = &loopbacks[sock];

	if ( loop->send - loop->get > MAX_LOOPBACK )
		loop->get = loop->send - MAX_LOOPBACK;

	if ( loop->send == loop->get )
		return qfalse;

	i = loop->get & (MAX_LOOPBACK-1);
	loop->get++;

	if ( loop->msgs[i].datalen < 0 ||
		loop->msgs[i].datalen > net_message->maxsize ) {
		Com_Printf( S_COLOR_YELLOW
			"NET_GetLoopPacket: dropped oversized loopback packet (%d > %d)\n",
			loop->msgs[i].datalen, net_message->maxsize );
		return qfalse;
	}

	Com_Memcpy (net_message->data, loop->msgs[i].data, loop->msgs[i].datalen);
	net_message->cursize = loop->msgs[i].datalen;
	Com_Memset (net_from, 0, sizeof(*net_from));
	net_from->type = NA_LOOPBACK;
	return qtrue;
}


static void NET_SendLoopPacket( netsrc_t sock, int length, const void *data )
{
	int		i;
	loopback_t	*loop;

	if ( sock < NS_CLIENT || sock > NS_SERVER ||
		!fnql::net::IsValidPayload( data, length, MAX_MSGLEN ) ) {
		ReportInvalidPacket( "NET_SendLoopPacket", length );
		return;
	}

	loop = &loopbacks[sock^1];

	i = loop->send & (MAX_LOOPBACK-1);
	loop->send++;

	if ( length > 0 ) {
		Com_Memcpy (loop->msgs[i].data, data, length);
	}
	loop->msgs[i].datalen = length;
}

#endif // !DEDICATED

//=============================================================================

typedef struct packetQueue_s {
		struct packetQueue_s *next;
		struct packetQueue_s *prev;
		int length;
		byte *data;
		netadr_t to;
		netsrc_t sock;
		std::uint32_t release;
} packetQueue_t;

static packetQueue_t *packetQueue = NULL;

static packetQueue_t *list_remove( packetQueue_t *head, packetQueue_t *item ) {
	if ( item->next != item ) {
		item->next->prev = item->prev;
		item->prev->next = item->next;
	} else {
		item->next = item->prev = NULL;
	}
	return item == head ? item->next : head;
}


static packetQueue_t *list_insert( packetQueue_t *head, packetQueue_t *item )
{
	if ( head ) {
		packetQueue_t *prev = head->prev;
		packetQueue_t *next = head;
		prev->next = item;
		next->prev = item;
		item->prev = prev;
		item->next = next;
		return head;
	} else {
		item->prev = item->next = item;
		return item;
	}
}


static packetQueue_t *list_process( packetQueue_t *head, const int time_diff )
{
	packetQueue_t *item = head;
	int do_break = 0;
	std::uint32_t now;
	const std::uint32_t minimumAge = static_cast<std::uint32_t>(
		std::max( time_diff, 0 ) );
	do {
		if ( head == NULL ) {
			break;
		}
		if ( head->prev == item ) {
			do_break = 1;
		}
		now = static_cast<std::uint32_t>( Sys_Milliseconds() );
		const std::uint32_t elapsed = now - item->release;
		if ( elapsed < fnql::net::SequenceHalfRange && elapsed >= minimumAge ) {
			packetQueue_t *next = item->next;
#ifndef DEDICATED
			if ( item->to.type == NA_LOOPBACK )
				NET_SendLoopPacket( item->sock, item->length, item->data );
			else
#endif
				Sys_SendPacket( item->length, item->data, &item->to );
			head = list_remove( head, item );
			Z_Free( item );
			item = next;
		} else {
			item = item->next;
		}
	} while ( do_break == 0 );

	return head;
}


void NET_QueuePacket( netsrc_t sock, int length, const void *data, const netadr_t *to, int offset )
{
	packetQueue_t *queuedPacket;

	if ( !to || sock < NS_CLIENT || sock > NS_SERVER ||
		!fnql::net::IsValidPayload( data, length,
			fnql::net::MaximumUdpPayload ) ) {
		ReportInvalidPacket( "NET_QueuePacket", length );
		return;
	}

	if ( to->type == NA_BOT ) {
		return;
	}
	if ( to->type == NA_BAD ) {
		return;
	}

	offset = std::clamp( offset, 0, 999 );

	const std::size_t allocationSize = sizeof( *queuedPacket ) +
		static_cast<std::size_t>( length );
	queuedPacket = static_cast<packetQueue_t *>( S_Malloc( allocationSize ) );
	queuedPacket->data = reinterpret_cast<byte *>( queuedPacket + 1 );
	if ( length > 0 ) {
		Com_Memcpy( queuedPacket->data, data, length );
	}
	queuedPacket->length = length;
	queuedPacket->to = *to;
	queuedPacket->sock = sock;
	queuedPacket->release = static_cast<std::uint32_t>( Sys_Milliseconds() ) +
		static_cast<std::uint32_t>( offset );
	queuedPacket->next = nullptr;

	packetQueue = list_insert( packetQueue, queuedPacket );
}


void NET_FlushPacketQueue( int time_diff )
{
	packetQueue = list_process( packetQueue, time_diff );
}


void NET_SendPacket( netsrc_t sock, int length, const void *data, const netadr_t *to ) {
	if ( !to || sock < NS_CLIENT || sock > NS_SERVER ||
		!fnql::net::IsValidPayload( data, length,
			fnql::net::MaximumUdpPayload ) ) {
		ReportInvalidPacket( "NET_SendPacket", length );
		return;
	}

	// sequenced packets are shown in netchan, so just show oob
	if ( showpackets && showpackets->integer &&
		IsConnectionlessPacket( data, length ) ) {
		Com_Printf ("send packet %4i\n", length);
	}

	if ( to->type == NA_BOT ) {
		return;
	}
	if ( to->type == NA_BAD ) {
		return;
	}
#ifndef DEDICATED
	if ( sock == NS_CLIENT && cl_packetdelay->integer > 0 ) {
		NET_QueuePacket( sock, length, data, to, cl_packetdelay->integer );
	} else
#endif
	if ( sock == NS_SERVER && sv_packetdelay->integer > 0 ) {
		NET_QueuePacket( sock, length, data, to, sv_packetdelay->integer );
	}
#ifndef DEDICATED
	else if ( to->type == NA_LOOPBACK ) {
		NET_SendLoopPacket( sock, length, data );
	}
#endif
	else {
		Sys_SendPacket( length, data, to );
	}
}


/*
===============
NET_OutOfBandPrint

Sends a text message in an out-of-band datagram
================
*/
void QDECL NET_OutOfBandPrint( netsrc_t sock, const netadr_t *adr, const char *format, ... ) {
	va_list		argptr;
	char		string[ MAX_PACKETLEN ];
	int			len;

	if ( !adr || !format ) {
		ReportInvalidPacket( "NET_OutOfBandPrint", -1 );
		return;
	}

	// set the header
	string[0] = -1;
	string[1] = -1;
	string[2] = -1;
	string[3] = -1;

	va_start( argptr, format );
	Q_vsnprintf( string+4, sizeof(string)-4, format, argptr );
	va_end( argptr );
	len = 4 + static_cast<int>( std::strlen( string + 4 ) );

	// send the datagram
	NET_SendPacket( sock, len, string, adr );
}


/*
===============
NET_OutOfBandCompress

Sends a compressed message in an out-of-band datagram (only used for "connect")
================
*/
void NET_OutOfBandCompress( netsrc_t sock, const netadr_t *adr, const byte *data, int len ) {
	// Huff_Compress writes its worst-case temporary stream at the requested
	// offset, so reserve both the codec bound and its 12-byte connect prefix.
	byte		string[MAX_INFO_STRING * 4 + 2 + 256 + 12];
	msg_t		mbuf;

	if ( !adr || !fnql::net::IsValidPayload( data, len,
		MAX_INFO_STRING * 2 ) ) {
		ReportInvalidPacket( "NET_OutOfBandCompress", len );
		return;
	}

	// set the header
	string[0] = 0xff;
	string[1] = 0xff;
	string[2] = 0xff;
	string[3] = 0xff;

	if ( len > 0 ) {
		Com_Memcpy( string + 4, data, len );
	}

	MSG_InitOOB( &mbuf, string, sizeof( string ) );
	mbuf.cursize = len+4;
	Huff_Compress( &mbuf, 12 );

	// send the datagram
	NET_SendPacket( sock, mbuf.cursize, mbuf.data, adr );
}


/*
=============
NET_StringToAdr

Traps "localhost" for loopback, passes everything else to system
return 0 on address not found, 1 on address found with port, 2 on address found without port.
=============
*/
int NET_StringToAdr( const char *s, netadr_t *a, netadrtype_t family )
{
	char	base[MAX_STRING_CHARS], *search;
	char	*port = NULL;

	if (!strcmp (s, "localhost")) {
		Com_Memset (a, 0, sizeof(*a));
		a->type = NA_LOOPBACK;
		// as NA_LOOPBACK doesn't require ports report port was given.
		return 1;
	}

	Q_strncpyz( base, s, sizeof( base ) );
	
	if(*base == '[' || Q_CountChar(base, ':') > 1)
	{
		// This is an ipv6 address, handle it specially.
		search = strchr(base, ']');
		if(search)
		{
			*search = '\0';
			search++;

			if(*search == ':')
				port = search + 1;
		}
		
		if(*base == '[')
			search = base + 1;
		else
			search = base;
	}
	else
	{
		// look for a port number
		port = strchr( base, ':' );
		
		if ( port ) {
			*port = '\0';
			port++;
		}
		
		search = base;
	}

	const fnql::net::PackedIpv4Host packedHost =
		fnql::net::ParsePackedIpv4Host( search );
	if ( packedHost.valid && ( family == NA_UNSPEC || family == NA_IP ) )
	{
		Com_Memset( a, 0, sizeof( *a ) );
		a->type = NA_IP;
		a->ipv._4[0] = static_cast<byte>( packedHost.value >> 24u );
		a->ipv._4[1] = static_cast<byte>( packedHost.value >> 16u );
		a->ipv._4[2] = static_cast<byte>( packedHost.value >> 8u );
		a->ipv._4[3] = static_cast<byte>( packedHost.value );
	}
	else if(!Sys_StringToAdr(search, a, family))
	{
		a->type = NA_BAD;
		return 0;
	}

	if(port)
	{
		a->port = BigShort((short) atoi(port));
		return 1;
	}
	else
	{
		a->port = BigShort(PORT_SERVER);
		return 2;
	}
}
