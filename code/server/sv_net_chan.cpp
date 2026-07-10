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

#include "server.h"
#include "../qcommon/netchan_codec.hpp"

/*
==============
SV_Netchan_Encode

	// first four bytes of the data are always:
	long reliableAcknowledge;

==============
*/
static void SV_Netchan_Encode(client_t *client, msg_t *msg, const char *clientCommandString)
{
	byte key;

	if ( msg->cursize < SV_ENCODE_START ) {
		return;
	}

	// xor the client challenge with the netchan sequence number
	key = client->challenge ^ client->netchan.outgoingSequence;
	fnql::ApplyNetchanXor( msg->data, static_cast<std::size_t>( msg->cursize ),
		SV_ENCODE_START, key,
		fnql::NetchanCommandView( clientCommandString, MAX_STRING_CHARS ) );
}

/*
==============
SV_Netchan_Decode

	// first 12 bytes of the data are always:
	long serverId;
	long messageAcknowledge;
	long reliableAcknowledge;

==============
*/
static void SV_Netchan_Decode( client_t *client, msg_t *msg ) {
	int serverId, messageAcknowledge, reliableAcknowledge;
	int srdc, sbit;
	bool soob;
	byte key;
	const char *command;

	srdc = msg->readcount;
	sbit = msg->bit;
	soob = SV_AsBool( msg->oob );

	msg->oob = qfalse;

	serverId = MSG_ReadLong(msg);
	messageAcknowledge = MSG_ReadLong(msg);
	reliableAcknowledge = MSG_ReadLong(msg);

	msg->oob = SV_QBool( soob );
	msg->bit = sbit;
	msg->readcount = srdc;

	command = client->reliableCommands[ reliableAcknowledge & ( MAX_RELIABLE_COMMANDS - 1 ) ];
	key = client->challenge ^ serverId ^ messageAcknowledge;
	fnql::ApplyNetchanXor( msg->data, static_cast<std::size_t>( msg->cursize ),
		static_cast<std::size_t>( msg->readcount + SV_DECODE_START ), key,
		fnql::NetchanCommandView( command, MAX_STRING_CHARS ) );
}


/*
=================
SV_Netchan_FreeQueue
=================
*/
void SV_Netchan_FreeQueue(client_t *client)
{
	netchan_buffer_t *netbuf, *next;
	
	for(netbuf = client->netchan_start_queue; netbuf; netbuf = next)
	{
		next = netbuf->next;
		SV_ZFree( netbuf );
	}
	
	client->netchan_start_queue = nullptr;
	client->netchan_end_queue = &client->netchan_start_queue;
}

/*
=================
SV_Netchan_TransmitNextInQueue
=================
*/
static void SV_Netchan_TransmitNextInQueue(client_t *client)
{
	netchan_buffer_t *netbuf;
		
	Com_DPrintf("#462 Netchan_TransmitNextFragment: popping a queued message for transmit\n");
	netbuf = client->netchan_start_queue;

	if ( Netchan_WireHasFeature( client->netchan.wireProfile,
		NETCHAN_FEATURE_RELIABLE_XOR ) )
		SV_Netchan_Encode(client, &netbuf->msg, netbuf->clientCommandString);

	Netchan_Transmit(&client->netchan, netbuf->msg.cursize, netbuf->msg.data);

	// pop from queue
	client->netchan_start_queue = netbuf->next;
	if(!client->netchan_start_queue)
	{
		Com_DPrintf("#462 Netchan_TransmitNextFragment: emptied queue\n");
		client->netchan_end_queue = &client->netchan_start_queue;
	}
	else
		Com_DPrintf("#462 Netchan_TransmitNextFragment: remaining queued message\n");

	SV_ZFree( netbuf );
}

/*
=================
SV_Netchan_TransmitNextFragment
Transmit the next fragment and the next queued packet
Return number of ms until next message can be sent based on throughput given by client rate,
-1 if no packet was sent.
=================
*/

int SV_Netchan_TransmitNextFragment(client_t *client)
{
	if(client->netchan.unsentFragments)
	{
		Netchan_TransmitNextFragment(&client->netchan);
		return SV_RateMsec(client);
	}
	else if(client->netchan_start_queue)
	{
		SV_Netchan_TransmitNextInQueue(client);
		return SV_RateMsec(client);
	}
	
	return -1;
}


/*
===============
SV_Netchan_Transmit
TTimo
https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=462
if there are some unsent fragments (which may happen if the snapshots
and the gamestate are fragmenting, and collide on send for instance)
then buffer them and make sure they get sent in correct order
================
*/

void SV_Netchan_Transmit( client_t *client, msg_t *msg)
{
	MSG_WriteByte( msg, svc_EOF );

	if(client->netchan.unsentFragments || client->netchan_start_queue)
	{
		netchan_buffer_t *netbuf;
		Com_DPrintf("#462 SV_Netchan_Transmit: unsent fragments, stacked\n");
		netbuf = SV_ZMalloc<netchan_buffer_t>();
		// store the msg, we can't store it encoded, as the encoding depends on stuff we still have to finish sending
		MSG_Copy(&netbuf->msg, netbuf->msgBuffer, SV_ArraySize( netbuf->msgBuffer ), msg);
		if ( Netchan_WireHasFeature( client->netchan.wireProfile,
			NETCHAN_FEATURE_RELIABLE_XOR ) )
		{
			Q_strncpyz(netbuf->clientCommandString, client->lastClientCommandString,
			SV_ArraySize(netbuf->clientCommandString));
		}
		netbuf->next = nullptr;
		// insert it in the queue, the message will be encoded and sent later
		*client->netchan_end_queue = netbuf;
		client->netchan_end_queue = &(*client->netchan_end_queue)->next;
	}
	else
	{
		if ( Netchan_WireHasFeature( client->netchan.wireProfile,
			NETCHAN_FEATURE_RELIABLE_XOR ) )
			SV_Netchan_Encode(client, msg, client->lastClientCommandString);
		Netchan_Transmit( &client->netchan, msg->cursize, msg->data );
	}
}

/*
=================
Netchan_SV_Process
=================
*/
qboolean SV_Netchan_Process( client_t *client, msg_t *msg ) {
	if ( !Netchan_Process( &client->netchan, msg ) )
		return SV_QBool( false );

	if ( Netchan_WireHasFeature( client->netchan.wireProfile,
		NETCHAN_FEATURE_RELIABLE_XOR ) )
		SV_Netchan_Decode( client, msg );

	return SV_QBool( true );
}

