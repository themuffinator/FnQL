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

extern "C" {
#include "client.h"
}

#include "client_cpp.h"
#include "../qcommon/netchan_codec.hpp"
#include "../qcommon/netchan_safety.hpp"

using fnql::ReadUnaligned;

namespace {

class MsgReadState {
public:
	explicit MsgReadState( msg_t *msg )
		: msg_( msg ), readcount_( msg->readcount ), bit_( msg->bit ), oob_( msg->oob ) {
	}

	~MsgReadState() {
		Restore();
	}

	void Restore() {
		if ( !msg_ ) {
			return;
		}

		msg_->readcount = readcount_;
		msg_->bit = bit_;
		msg_->oob = oob_;
		msg_ = nullptr;
	}

private:
	msg_t *msg_;
	int readcount_;
	int bit_;
	qboolean oob_;
};

constexpr int kReliableCommandMask = MAX_RELIABLE_COMMANDS - 1;

byte CL_NetchanSequenceKey( const msg_t *msg ) {
	const int sequence = ReadUnaligned<int>( msg->data );
	return static_cast<byte>( clc.challenge ^ LittleLong( sequence ) );
}

/*
==============
CL_Netchan_Encode

	// first 12 bytes of the data are always:
	long serverId;
	long messageAcknowledge;
	long reliableAcknowledge;
==============
*/
void CL_Netchan_Encode( msg_t *msg ) {
	int serverId, messageAcknowledge, reliableAcknowledge;
	byte key;
	const char *command;

	if ( msg->cursize <= CL_ENCODE_START ) {
		return;
	}

	MsgReadState savedReadState( msg );

	msg->bit = 0;
	msg->readcount = 0;
	msg->oob = qfalse;

	serverId = MSG_ReadLong(msg);
	messageAcknowledge = MSG_ReadLong(msg);
	reliableAcknowledge = MSG_ReadLong(msg);
	savedReadState.Restore();

	command = clc.serverCommands[ reliableAcknowledge & kReliableCommandMask ];
	key = static_cast<byte>( clc.challenge ^ serverId ^ messageAcknowledge );
	fnql::ApplyNetchanXor( msg->data, static_cast<std::size_t>( msg->cursize ),
		CL_ENCODE_START, key,
		fnql::NetchanCommandView( command, MAX_STRING_CHARS ) );
}


/*
==============
CL_Netchan_Decode

	// first four bytes of the data are always:
	long reliableAcknowledge;
==============
*/
void CL_Netchan_Decode( msg_t *msg ) {
	int reliableAcknowledge;
	byte key;
	const char *command;

	MsgReadState savedReadState( msg );

	msg->oob = qfalse;

	reliableAcknowledge = MSG_ReadLong( msg );
	savedReadState.Restore();

	command = clc.reliableCommands[ reliableAcknowledge & kReliableCommandMask ];
	// xor the client challenge with the netchan sequence number (need something that changes every message)
	key = CL_NetchanSequenceKey( msg );
	fnql::ApplyNetchanXor( msg->data, static_cast<std::size_t>( msg->cursize ),
		static_cast<std::size_t>( msg->readcount + CL_DECODE_START ), key,
		fnql::NetchanCommandView( command, MAX_STRING_CHARS ) );
}


/*
=================
CL_Netchan_TransmitNextFragment
=================
*/
bool CL_Netchan_TransmitNextFragment( netchan_t *chan )
{
	if ( chan->unsentFragments )
	{
		Netchan_TransmitNextFragment( chan );
		return true;
	}
	
	return false;
}

} // namespace


/*
===============
CL_Netchan_Transmit
================
*/
extern "C" void CL_Netchan_Transmit( netchan_t *chan, msg_t *msg ) {

	if ( Netchan_WireHasFeature( chan->wireProfile,
		NETCHAN_FEATURE_RELIABLE_XOR ) )
		CL_Netchan_Encode( msg );

	Netchan_Transmit( chan, msg->cursize, msg->data );
	
	// Transmit all fragments without delay
	while ( CL_Netchan_TransmitNextFragment( chan ) ) {
		// might happen if server die silently but client continue adding/sending commands
		Com_DPrintf( S_COLOR_YELLOW "%s: unsent fragments\n", __func__ );
	}
}


/*
===============
CL_Netchan_Enqueue
================
*/
extern "C" void CL_Netchan_Enqueue( netchan_t *chan, msg_t *msg, int times ) {
	int i;

	// make sure we send all pending fragments to get correct chan->outgoingSequence
	while ( CL_Netchan_TransmitNextFragment( chan ) ) {
		;
	}

	if ( Netchan_WireHasFeature( chan->wireProfile,
		NETCHAN_FEATURE_RELIABLE_XOR ) ) {
		CL_Netchan_Encode( msg );
	}

	for ( i = 0; i < times; i++ ) {
		Netchan_Enqueue( chan, msg->cursize, msg->data );
	}

	chan->outgoingSequence = fnql::net::NextSequence( chan->outgoingSequence );
}


/*
=================
CL_Netchan_Process
=================
*/
extern "C" qboolean CL_Netchan_Process( netchan_t *chan, msg_t *msg ) {
	bool ret;

	ret = Netchan_Process( chan, msg );
	if ( !ret )
		return qfalse;

	if ( Netchan_WireHasFeature( chan->wireProfile,
		NETCHAN_FEATURE_RELIABLE_XOR ) )
		CL_Netchan_Decode( msg );

	return qtrue;
}
