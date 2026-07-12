/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/

// Optional Quake Live-compatible ZMQ server bridge.  FnQL deliberately does
// not bundle libzmq: the runtime is discovered only after an administrator
// opts in, and failure leaves the normal server path untouched.

#include "server.h"
#include "json_document.hpp"
#include "zmq_endpoint.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr int ZMQ_PUB = 1;
constexpr int ZMQ_REP = 4;
constexpr int ZMQ_ROUTER = 6;
constexpr int ZMQ_DONTWAIT = 1;
constexpr int ZMQ_SNDMORE = 2;
constexpr int ZMQ_POLLIN = 1;
constexpr int ZMQ_RCVMORE = 13;
constexpr int ZMQ_LINGER = 17;
constexpr int ZMQ_ROUTER_MANDATORY = 33;
constexpr int ZMQ_PLAIN_SERVER = 44;
constexpr int ZMQ_ZAP_DOMAIN = 55;

constexpr int MAX_RCON_PEERS = 64;
constexpr int MAX_RCON_COMMANDS_PER_FRAME = 16;
constexpr std::size_t MAX_ZMQ_IDENTITY = 255;
constexpr std::size_t MAX_ZMQ_PUBLICATION = 32768;
constexpr std::size_t MAX_ZMQ_RCON_REPLY = 65536;
constexpr const char *ZAP_ENDPOINT = "inproc://zeromq.zap.01";

#if defined(_WIN32)
using zmq_fd_t = std::uintptr_t;
#else
using zmq_fd_t = int;
#endif

struct PollItem {
	void *socket;
	zmq_fd_t fd;
	short events;
	short revents;
};

using CtxNewFn = void *( * )( void );
using CtxTermFn = int ( * )( void * );
using SocketFn = void *( * )( void *, int );
using CloseFn = int ( * )( void * );
using BindFn = int ( * )( void *, const char * );
using SendFn = int ( * )( void *, const void *, std::size_t, int );
using RecvFn = int ( * )( void *, void *, std::size_t, int );
using PollFn = int ( * )( PollItem *, int, long );
using SetSockOptFn = int ( * )( void *, int, const void *, std::size_t );
using GetSockOptFn = int ( * )( void *, int, void *, std::size_t * );
using ErrnoFn = int ( * )( void );
using StrErrorFn = const char *( * )( int );

struct Api {
	void *library = nullptr;
	CtxNewFn ctxNew = nullptr;
	CtxTermFn ctxTerm = nullptr;
	SocketFn socket = nullptr;
	CloseFn close = nullptr;
	BindFn bind = nullptr;
	SendFn send = nullptr;
	RecvFn recv = nullptr;
	PollFn poll = nullptr;
	SetSockOptFn setSockOpt = nullptr;
	GetSockOptFn getSockOpt = nullptr;
	ErrnoFn errorNumber = nullptr;
	StrErrorFn errorString = nullptr;
};

struct RconPeer {
	std::string identity;
	bool requestDelimiter = false;
};

struct Runtime {
	Api api;
	void *context = nullptr;
	void *authSocket = nullptr;
	void *rconSocket = nullptr;
	void *statsSocket = nullptr;
	std::vector<RconPeer> peers;
	std::thread::id ownerThread;
	bool broadcasting = false;
	bool loadAttempted = false;
	bool shutdownFailed = false;
	bool rconAttempted = false;
	bool statsAttempted = false;
	int rconPasswordRevision = -1;
	int statsPasswordRevision = -1;
	std::string rconPassword;
	std::string statsPassword;
	std::string rconEndpoint;
	std::string statsEndpoint;
	std::string commandOutput;
	bool commandOutputTruncated = false;
	bool selfTestCommandRegistered = false;
};

Runtime runtime;

cvar_t *zmqRconEnable = nullptr;
cvar_t *zmqStatsEnable = nullptr;
cvar_t *zmqLibrary = nullptr;
cvar_t *zmqRconIp = nullptr;
cvar_t *zmqRconPort = nullptr;
cvar_t *zmqStatsIp = nullptr;
cvar_t *zmqStatsPort = nullptr;
cvar_t *zmqRconPassword = nullptr;
cvar_t *zmqStatsPassword = nullptr;
cvar_t *zmqAllowInsecureRemote = nullptr;
cvar_t *zmqStatus = nullptr;
cvar_t *zmqRconEndpointStatus = nullptr;
cvar_t *zmqStatsEndpointStatus = nullptr;

void ScrubString( std::string &value ) {
	if ( !value.empty() ) {
		volatile char *bytes = &value[0];
		for ( std::size_t i = 0; i < value.size(); ++i ) {
			bytes[i] = '\0';
		}
	}
	value.clear();
}

void ReplaceSecret( std::string &destination, const char *source ) {
	ScrubString( destination );
	if ( source ) {
		destination.assign( source );
	}
}

template <typename Function>
bool Resolve( Function &out, const char *name ) {
	out = reinterpret_cast<Function>( Sys_LoadFunction( runtime.api.library, name ) );
	return out != nullptr;
}

void SetRuntimeStatus( const char *status ) {
	if ( zmqStatus ) {
		Cvar_Set( "zmq_status", status ? status : "unavailable" );
	}
}

void SetEndpointStatus( cvar_t *statusCvar, const char *name, const std::string &endpoint ) {
	if ( statusCvar ) {
		Cvar_Set( name, endpoint.c_str() );
	}
}

const char *LastError() {
	if ( runtime.api.errorNumber && runtime.api.errorString ) {
		const char *message = runtime.api.errorString( runtime.api.errorNumber() );
		if ( message && *message ) {
			return message;
		}
	}
	return "unknown libzmq error";
}

void ClearApi() {
	runtime.api = {};
}

bool LoadApi() {
#if defined(__APPLE__)
	static constexpr const char *candidates[] = { "libzmq.5.dylib", "libzmq.dylib" };
#elif !defined(_WIN32)
	static constexpr const char *candidates[] = { "libzmq.so.5", "libzmq.so" };
#endif

	if ( runtime.api.library ) {
		return true;
	}
	if ( runtime.loadAttempted ) {
		return false;
	}
	runtime.loadAttempted = true;

	const char *configuredLibrary = zmqLibrary ? zmqLibrary->string : "";
	if ( configuredLibrary[0] ) {
#if defined(_WIN32)
		const bool absoluteLibraryPath =
			fnql::zmq::IsAbsoluteWindowsLibraryPath( configuredLibrary );
#else
		const bool absoluteLibraryPath =
			fnql::zmq::IsAbsoluteLibraryPath( configuredLibrary );
#endif
		if ( !absoluteLibraryPath ) {
			SetRuntimeStatus( "unavailable: zmq_library must be an absolute path" );
			return false;
		}
		runtime.api.library = Sys_LoadLibrary( configuredLibrary );
	}
#if defined(_WIN32)
	else {
		SetRuntimeStatus( "unavailable: set absolute zmq_library path" );
		return false;
	}
#else
	else {
		for ( const char *candidate : candidates ) {
			runtime.api.library = Sys_LoadLibrary( candidate );
			if ( runtime.api.library ) {
				break;
			}
		}
	}
#endif
	if ( !runtime.api.library ) {
		SetRuntimeStatus( "unavailable: libzmq not found" );
		return false;
	}

	Sys_LoadFunctionErrors();
	const bool complete =
		Resolve( runtime.api.ctxNew, "zmq_ctx_new" ) &&
		Resolve( runtime.api.ctxTerm, "zmq_ctx_term" ) &&
		Resolve( runtime.api.socket, "zmq_socket" ) &&
		Resolve( runtime.api.close, "zmq_close" ) &&
		Resolve( runtime.api.bind, "zmq_bind" ) &&
		Resolve( runtime.api.send, "zmq_send" ) &&
		Resolve( runtime.api.recv, "zmq_recv" ) &&
		Resolve( runtime.api.poll, "zmq_poll" ) &&
		Resolve( runtime.api.setSockOpt, "zmq_setsockopt" ) &&
		Resolve( runtime.api.getSockOpt, "zmq_getsockopt" ) &&
		Resolve( runtime.api.errorNumber, "zmq_errno" ) &&
		Resolve( runtime.api.errorString, "zmq_strerror" );
	Sys_LoadFunctionErrors();
	if ( !complete ) {
		Sys_UnloadLibrary( runtime.api.library );
		ClearApi();
		SetRuntimeStatus( "unavailable: incompatible libzmq" );
		return false;
	}

	return true;
}

bool EnsureContext() {
	if ( runtime.shutdownFailed ) {
		SetRuntimeStatus( "unavailable: prior context shutdown failed" );
		return false;
	}
	if ( runtime.context ) {
		return true;
	}
	if ( !LoadApi() ) {
		return false;
	}
	runtime.context = runtime.api.ctxNew();
	if ( !runtime.context ) {
		Com_Printf( "ZMQ: could not create context: %s\n", LastError() );
		SetRuntimeStatus( "unavailable: context creation failed" );
		return false;
	}
	runtime.ownerThread = std::this_thread::get_id();
	return true;
}

void CloseSocket( void *&socket ) {
	if ( socket && runtime.api.close ) {
		const int noLinger = 0;
		(void)runtime.api.setSockOpt( socket, ZMQ_LINGER, &noLinger, sizeof( noLinger ) );
		runtime.api.close( socket );
	}
	socket = nullptr;
}

bool SetSocketInt( void *socket, int option, int value ) {
	return socket &&
		runtime.api.setSockOpt( socket, option, &value, sizeof( value ) ) == 0;
}

bool SetSocketString( void *socket, int option, const char *value ) {
	return socket && value &&
		runtime.api.setSockOpt( socket, option, value, strlen( value ) ) == 0;
}

bool PollReadable( void *socket ) {
	PollItem item{ socket, 0, ZMQ_POLLIN, 0 };
	return socket && runtime.api.poll( &item, 1, 0 ) > 0 && ( item.revents & ZMQ_POLLIN ) != 0;
}

bool ReceiveFrame( void *socket, std::string &frame, bool &more, std::size_t limit ) {
	std::array<char, MAX_STRING_CHARS + 1> buffer{};
	if ( limit > MAX_STRING_CHARS ) {
		frame.clear();
		more = false;
		return false;
	}
	const int received = runtime.api.recv( socket, buffer.data(), limit, ZMQ_DONTWAIT );
	if ( received < 0 ) {
		frame.clear();
		more = false;
		return false;
	}

	int hasMore = 0;
	std::size_t optionSize = sizeof( hasMore );
	if ( runtime.api.getSockOpt( socket, ZMQ_RCVMORE, &hasMore, &optionSize ) != 0 ) {
		std::fill( buffer.begin(), buffer.end(), '\0' );
		frame.clear();
		more = false;
		return false;
	}
	more = hasMore != 0;
	if ( static_cast<std::size_t>( received ) > limit ) {
		std::fill( buffer.begin(), buffer.end(), '\0' );
		frame.clear();
		return false;
	}
	frame.assign( buffer.data(), static_cast<std::size_t>( received ) );
	std::fill( buffer.begin(), buffer.end(), '\0' );
	return true;
}

bool DrainFrames( void *socket, bool more ) {
	std::string ignored;
	for ( int count = 0; more && count < 32; ++count ) {
		if ( !ReceiveFrame( socket, ignored, more, MAX_STRING_CHARS ) ) {
			ScrubString( ignored );
			return false;
		}
		ScrubString( ignored );
	}
	return !more;
}

void ResetSocketsAfterFramingFailure( const char *lane ) {
	Com_Printf( "ZMQ: resetting %s sockets after malformed multipart framing\n",
		lane ? lane : "service" );
	CloseSocket( runtime.rconSocket );
	CloseSocket( runtime.statsSocket );
	CloseSocket( runtime.authSocket );
	runtime.peers.clear();
	runtime.rconEndpoint.clear();
	runtime.statsEndpoint.clear();
	runtime.rconAttempted = false;
	runtime.statsAttempted = false;
	SetRuntimeStatus( "recovering: malformed multipart framing" );
	SetEndpointStatus( zmqRconEndpointStatus, "zmq_rcon_endpoint", runtime.rconEndpoint );
	SetEndpointStatus( zmqStatsEndpointStatus, "zmq_stats_endpoint", runtime.statsEndpoint );
}

bool SendFrame( void *socket, const std::string &frame, bool more, bool nonBlocking = false ) {
	int flags = more ? ZMQ_SNDMORE : 0;
	if ( nonBlocking ) {
		flags |= ZMQ_DONTWAIT;
	}
	return runtime.api.send( socket, frame.data(), frame.size(), flags ) >= 0;
}

bool ConstantTimeEqual( const std::string &left, const std::string &right ) {
	std::size_t difference = left.size() ^ right.size();
	const std::size_t count = std::max( left.size(), right.size() );
	for ( std::size_t i = 0; i < count; ++i ) {
		const unsigned char a = i < left.size() ? static_cast<unsigned char>( left[i] ) : 0;
		const unsigned char b = i < right.size() ? static_cast<unsigned char>( right[i] ) : 0;
		difference |= a ^ b;
	}
	return difference == 0;
}

bool IsLoopbackHost( const std::string &host ) {
	return host == "127.0.0.1" || host == "localhost" || host == "::1" || host == "[::1]";
}

bool BuildEndpoint( const char *host, int port, std::string &endpoint ) {
	return host && fnql::zmq::BuildTcpEndpoint( host, port, endpoint );
}

bool EnsureAuthSocket() {
	const bool needed = !runtime.rconPassword.empty() || !runtime.statsPassword.empty();
	if ( !needed ) {
		CloseSocket( runtime.authSocket );
		return true;
	}
	if ( runtime.authSocket ) {
		return true;
	}
	if ( !EnsureContext() ) {
		return false;
	}
	runtime.authSocket = runtime.api.socket( runtime.context, ZMQ_REP );
	if ( !runtime.authSocket || runtime.api.bind( runtime.authSocket, ZAP_ENDPOINT ) != 0 ) {
		Com_Printf( "ZMQ: could not start authentication endpoint: %s\n", LastError() );
		CloseSocket( runtime.authSocket );
		return false;
	}
	return true;
}

void SendZapResponse( const std::string &version, const std::string &requestId,
		bool allowed, const std::string &userId ) {
	SendFrame( runtime.authSocket, version.empty() ? "1.0" : version, true );
	SendFrame( runtime.authSocket, requestId, true );
	SendFrame( runtime.authSocket, allowed ? "200" : "400", true );
	SendFrame( runtime.authSocket, allowed ? "OK" : "No access", true );
	SendFrame( runtime.authSocket, allowed ? userId : std::string(), true );
	SendFrame( runtime.authSocket, std::string(), false );
}

void PumpAuthentication() {
	if ( !runtime.authSocket ) {
		return;
	}

	for ( int request = 0; request < MAX_RCON_COMMANDS_PER_FRAME && PollReadable( runtime.authSocket ); ++request ) {
		std::array<std::string, 8> fields;
		bool more = false;
		bool valid = true;
		std::size_t count = 0;
		for ( ; count < fields.size(); ++count ) {
			if ( !ReceiveFrame( runtime.authSocket, fields[count], more, MAX_STRING_CHARS ) ) {
				valid = false;
				break;
			}
			if ( !more ) {
				++count;
				break;
			}
		}
		const bool hadExtraFields = more;
		if ( !DrainFrames( runtime.authSocket, more ) ) {
			ResetSocketsAfterFramingFailure( "authentication" );
			return;
		}

		// ZAP: version, request id, domain, address, identity, mechanism,
		// then mechanism credentials.  FnQL accepts only explicit PLAIN.
		bool allowed = valid && !hadExtraFields && count == 8
			&& fields[0] == "1.0" && fields[5] == "PLAIN";
		const std::string *expectedPassword = nullptr;
		if ( fields[2] == "rcon" ) {
			expectedPassword = &runtime.rconPassword;
		} else if ( fields[2] == "stats" ) {
			expectedPassword = &runtime.statsPassword;
		}
		allowed = allowed && expectedPassword && !expectedPassword->empty() &&
			fields[6] == fields[2] && ConstantTimeEqual( fields[7], *expectedPassword );
		SendZapResponse( fields[0], fields[1], allowed, allowed ? fields[6] : std::string() );
		ScrubString( fields[7] );
	}
}

bool EndpointSecurityRejected( const char *host, const std::string &password, const char *lane ) {
	if ( IsLoopbackHost( host ? host : "" ) ) {
		return false;
	}
	if ( password.empty() ) {
		Com_Printf( "ZMQ: refusing non-loopback %s endpoint without a password\n", lane );
		return true;
	}
	if ( !zmqAllowInsecureRemote || !zmqAllowInsecureRemote->integer ) {
		Com_Printf( "ZMQ: refusing non-loopback %s endpoint because PLAIN authentication is not encrypted; set zmq_allow_insecure_remote 1 only on a trusted network\n", lane );
		return true;
	}
	Com_Printf( "WARNING: ZMQ %s is using unencrypted PLAIN authentication on a non-loopback endpoint\n", lane );
	return false;
}

bool EnsureRconSocket() {
	if ( !zmqRconEnable || !zmqRconEnable->integer ) {
		return false;
	}
	if ( runtime.rconSocket ) {
		return true;
	}
	if ( runtime.rconAttempted ) {
		return false;
	}
	runtime.rconAttempted = true;
	if ( EndpointSecurityRejected( zmqRconIp->string, runtime.rconPassword, "RCON" ) ||
		!BuildEndpoint( zmqRconIp->string, zmqRconPort->integer, runtime.rconEndpoint ) ||
		!EnsureContext() || !EnsureAuthSocket() ) {
		SetRuntimeStatus( "disabled: invalid or insecure RCON configuration" );
		return false;
	}

	void *socket = runtime.api.socket( runtime.context, ZMQ_ROUTER );
	if ( !socket ) {
		Com_Printf( "ZMQ: could not create RCON socket: %s\n", LastError() );
		return false;
	}
	if ( !SetSocketInt( socket, ZMQ_LINGER, 0 ) ||
		!SetSocketInt( socket, ZMQ_ROUTER_MANDATORY, 1 ) ) {
		Com_Printf( "ZMQ: could not configure required RCON socket options: %s\n", LastError() );
		CloseSocket( socket );
		return false;
	}
	if ( !runtime.rconPassword.empty() ) {
		if ( !SetSocketString( socket, ZMQ_ZAP_DOMAIN, "rcon" ) ||
			!SetSocketInt( socket, ZMQ_PLAIN_SERVER, 1 ) ) {
			Com_Printf( "ZMQ: could not enforce RCON authentication: %s\n", LastError() );
			CloseSocket( socket );
			return false;
		}
	}
	if ( runtime.api.bind( socket, runtime.rconEndpoint.c_str() ) != 0 ) {
		Com_Printf( "ZMQ: could not bind RCON endpoint %s: %s\n",
			runtime.rconEndpoint.c_str(), LastError() );
		CloseSocket( socket );
		return false;
	}
	runtime.rconSocket = socket;
	SetEndpointStatus( zmqRconEndpointStatus, "zmq_rcon_endpoint", runtime.rconEndpoint );
	SetRuntimeStatus( "active" );
	Com_Printf( "ZMQ RCON listening on %s\n", runtime.rconEndpoint.c_str() );
	return true;
}

std::string ResolveStatsHost() {
	if ( zmqStatsIp && zmqStatsIp->string[0] ) {
		return zmqStatsIp->string;
	}
	std::array<char, 128> host{};
	Cvar_VariableStringBuffer( "net_ip", host.data(), static_cast<int>( host.size() ) );
	if ( host[0] == '\0' || !Q_stricmp( host.data(), "localhost" ) ) {
		return "127.0.0.1";
	}
	return host.data();
}

int ResolveStatsPort() {
	if ( zmqStatsPort && zmqStatsPort->string[0] ) {
		return zmqStatsPort->integer;
	}
	const int netPort = Cvar_VariableIntegerValue( "net_port" );
	return netPort > 0 ? netPort : PORT_SERVER;
}

bool EnsureStatsSocket() {
	if ( !zmqStatsEnable || !zmqStatsEnable->integer ) {
		return false;
	}
	if ( runtime.statsSocket ) {
		return true;
	}
	if ( runtime.statsAttempted ) {
		return false;
	}
	runtime.statsAttempted = true;
	const std::string host = ResolveStatsHost();
	if ( EndpointSecurityRejected( host.c_str(), runtime.statsPassword, "stats" ) ||
		!BuildEndpoint( host.c_str(), ResolveStatsPort(), runtime.statsEndpoint ) ||
		!EnsureContext() || !EnsureAuthSocket() ) {
		SetRuntimeStatus( "disabled: invalid or insecure stats configuration" );
		return false;
	}

	void *socket = runtime.api.socket( runtime.context, ZMQ_PUB );
	if ( !socket ) {
		Com_Printf( "ZMQ: could not create stats socket: %s\n", LastError() );
		return false;
	}
	if ( !SetSocketInt( socket, ZMQ_LINGER, 0 ) ) {
		Com_Printf( "ZMQ: could not configure required stats socket options: %s\n", LastError() );
		CloseSocket( socket );
		return false;
	}
	if ( !runtime.statsPassword.empty() ) {
		if ( !SetSocketString( socket, ZMQ_ZAP_DOMAIN, "stats" ) ||
			!SetSocketInt( socket, ZMQ_PLAIN_SERVER, 1 ) ) {
			Com_Printf( "ZMQ: could not enforce stats authentication: %s\n", LastError() );
			CloseSocket( socket );
			return false;
		}
	}
	if ( runtime.api.bind( socket, runtime.statsEndpoint.c_str() ) != 0 ) {
		Com_Printf( "ZMQ: could not bind stats endpoint %s: %s\n",
			runtime.statsEndpoint.c_str(), LastError() );
		CloseSocket( socket );
		return false;
	}
	runtime.statsSocket = socket;
	SetEndpointStatus( zmqStatsEndpointStatus, "zmq_stats_endpoint", runtime.statsEndpoint );
	SetRuntimeStatus( "active" );
	Com_Printf( "ZMQ stats publishing on %s\n", runtime.statsEndpoint.c_str() );
	return true;
}

std::vector<RconPeer>::iterator FindPeer( const std::string &identity ) {
	return std::find_if( runtime.peers.begin(), runtime.peers.end(),
		[&identity]( const RconPeer &peer ) { return peer.identity == identity; } );
}

bool SendToPeer( const std::string &identity, const char *message,
		bool requestDelimiter ) {
	if ( !SendFrame( runtime.rconSocket, identity, true, true ) ) {
		return false;
	}
	if ( requestDelimiter &&
		!SendFrame( runtime.rconSocket, std::string(), true, true ) ) {
		return false;
	}
	return SendFrame( runtime.rconSocket, message ? message : "", false, true );
}

bool ValidCommand( const std::string &command ) {
	if ( command.empty() || command.size() >= MAX_STRING_CHARS ||
		command.find( '\0' ) != std::string::npos ) {
		return false;
	}
	for ( unsigned char value : command ) {
		if ( value < 32 && value != '\t' && value != '\n' && value != '\r' ) {
			return false;
		}
	}
	return true;
}

void CollectRconCommandOutput( const char *message ) {
	if ( !message || !message[0] || runtime.commandOutputTruncated ) {
		return;
	}
	const std::size_t bytes = strlen( message );
	if ( bytes > MAX_ZMQ_RCON_REPLY - runtime.commandOutput.size() ) {
		static constexpr std::string_view marker = "\n[FnQL ZMQ RCON output truncated]\n";
		const std::size_t room = MAX_ZMQ_RCON_REPLY - runtime.commandOutput.size();
		if ( room > marker.size() ) {
			runtime.commandOutput.append( message, room - marker.size() );
			runtime.commandOutput.append( marker );
		} else if ( room > 0 ) {
			runtime.commandOutput.append( marker.data(), room );
		}
		runtime.commandOutputTruncated = true;
		return;
	}
	runtime.commandOutput.append( message, bytes );
}

std::string EscapeJsonString( const char *text ) {
	std::string result;
	if ( !text ) {
		return result;
	}
	const unsigned char *cursor = reinterpret_cast<const unsigned char *>( text );
	for ( std::size_t count = 0; cursor[count] && count < 128; ++count ) {
		switch ( cursor[count] ) {
		case '\\': result += "\\\\"; break;
		case '"': result += "\\\""; break;
		case '\n': result += "\\n"; break;
		case '\r': result += "\\r"; break;
		case '\t': result += "\\t"; break;
		default:
			if ( cursor[count] >= 32 ) {
				result += static_cast<char>( cursor[count] );
			}
			break;
		}
	}
	return result;
}

void Publish( const char *type, const char *json ) {
	if ( !type || !*type ) {
		return;
	}
	std::string_view jsonDocument;
	if ( json && *json ) {
		std::size_t jsonBytes = 0;
		while ( jsonBytes <= fnql::server::json::MaximumDocumentBytes &&
			json[jsonBytes] != '\0' ) {
			++jsonBytes;
		}
		if ( jsonBytes > fnql::server::json::MaximumDocumentBytes ) {
			Com_Printf( "ZMQ: dropped unterminated or oversized %s JSON publication\n", type );
			return;
		}
		jsonDocument = { json, jsonBytes };
		if ( !fnql::server::json::DocumentIsValid( jsonDocument ) ) {
			Com_Printf( "ZMQ: dropped malformed %s JSON publication\n", type );
			return;
		}
	}
	if ( !EnsureStatsSocket() ) {
		return;
	}
	std::string message = "{\"TYPE\":\"" + EscapeJsonString( type ) + "\",\"DATA\":";
	if ( jsonDocument.empty() ) {
		message += "null";
	} else {
		message.append( jsonDocument.data(), jsonDocument.size() );
	}
	message += '}';
	if ( message.size() > MAX_ZMQ_PUBLICATION ) {
		Com_Printf( "ZMQ: dropped oversized %s publication (%zu bytes)\n", type, message.size() );
		return;
	}
	runtime.api.send( runtime.statsSocket, message.data(), message.size(), ZMQ_DONTWAIT );
}

void Zmq_SelfTest_f() {
	if ( !com_developer || !com_developer->integer ) {
		return;
	}
	Publish( "FNQL_ZMQ_SELFTEST", "{\"ok\":true,\"protocol\":91}" );
	Com_Printf( "ZMQ self-test publication queued\n" );
}

void RecreateAuthenticatedSockets() {
	CloseSocket( runtime.rconSocket );
	CloseSocket( runtime.statsSocket );
	CloseSocket( runtime.authSocket );
	runtime.peers.clear();
	runtime.rconEndpoint.clear();
	runtime.statsEndpoint.clear();
	runtime.rconAttempted = false;
	runtime.statsAttempted = false;
	SetEndpointStatus( zmqRconEndpointStatus, "zmq_rcon_endpoint", runtime.rconEndpoint );
	SetEndpointStatus( zmqStatsEndpointStatus, "zmq_stats_endpoint", runtime.statsEndpoint );
	EnsureRconSocket();
	EnsureStatsSocket();
}

} // namespace

void Zmq_RegisterCvarsAndInitRcon( void ) {
	zmqRconEnable = Cvar_Get( "zmq_rcon_enable", "0", CVAR_INIT );
	zmqStatsEnable = Cvar_Get( "zmq_stats_enable", "0", CVAR_INIT );
	zmqLibrary = Cvar_Get( "zmq_library", "", CVAR_INIT | CVAR_PROTECTED | CVAR_PRIVATE );
	zmqRconIp = Cvar_Get( "zmq_rcon_ip", "0.0.0.0", CVAR_INIT );
	zmqRconPort = Cvar_Get( "zmq_rcon_port", "28960", CVAR_INIT );
	zmqStatsIp = Cvar_Get( "zmq_stats_ip", "", CVAR_INIT );
	zmqStatsPort = Cvar_Get( "zmq_stats_port", "", CVAR_INIT );
	zmqRconPassword = Cvar_Get( "zmq_rcon_password", "", CVAR_TEMP | CVAR_PRIVATE );
	zmqStatsPassword = Cvar_Get( "zmq_stats_password", "", CVAR_TEMP | CVAR_PRIVATE );
	zmqAllowInsecureRemote = Cvar_Get( "zmq_allow_insecure_remote", "0", CVAR_INIT );
	zmqStatus = Cvar_Get( "zmq_status", "disabled", CVAR_ROM );
	zmqRconEndpointStatus = Cvar_Get( "zmq_rcon_endpoint", "", CVAR_ROM );
	zmqStatsEndpointStatus = Cvar_Get( "zmq_stats_endpoint", "", CVAR_ROM );
	Cvar_SetDescription( Cvar_Get( "zmq_stats_payload_policy", "validated-provider-json", CVAR_ROM ),
		"Retail Json::Value payloads are accepted only through the bounded provider serializer ABI, then strictly validated before publication." );

	Cvar_CheckRange( zmqRconEnable, "0", "1", CV_INTEGER );
	Cvar_CheckRange( zmqStatsEnable, "0", "1", CV_INTEGER );
	Cvar_CheckRange( zmqAllowInsecureRemote, "0", "1", CV_INTEGER );
	Cvar_CheckRange( zmqRconPort, "1", "65535", CV_INTEGER );
	if ( zmqStatsPort->string[0] ) {
		Cvar_CheckRange( zmqStatsPort, "1", "65535", CV_INTEGER );
	}
	Cvar_SetDescription( zmqRconEnable, "Enable the optional Quake Live-compatible ZMQ RCON bridge. Default off; requires external libzmq." );
	Cvar_SetDescription( zmqStatsEnable, "Enable the optional Quake Live-compatible ZMQ stats publisher. Default off; requires external libzmq." );
	Cvar_SetDescription( zmqLibrary, "Absolute path to an administrator-provided libzmq runtime. Required on Windows to avoid unsafe DLL search paths." );
	Cvar_SetDescription( zmqRconPassword, "Transient ZMQ RCON PLAIN password. Required for any explicitly allowed non-loopback bind." );
	Cvar_SetDescription( zmqStatsPassword, "Transient ZMQ stats PLAIN password. Required for any explicitly allowed non-loopback bind." );
	Cvar_SetDescription( zmqAllowInsecureRemote, "Allow password-authenticated ZMQ PLAIN sockets outside loopback despite the unencrypted transport. Default off; use only behind a trusted tunnel or network." );
	if ( com_developer && com_developer->integer && !runtime.selfTestCommandRegistered ) {
		Cmd_AddCommand( "zmq_selftest", Zmq_SelfTest_f );
		runtime.selfTestCommandRegistered = true;
	}

	ReplaceSecret( runtime.rconPassword, zmqRconPassword->string );
	ReplaceSecret( runtime.statsPassword, zmqStatsPassword->string );
	runtime.rconPasswordRevision = zmqRconPassword->modificationCount;
	runtime.statsPasswordRevision = zmqStatsPassword->modificationCount;
	if ( !zmqRconEnable->integer && !zmqStatsEnable->integer ) {
		SetRuntimeStatus( "disabled" );
		return;
	}
	EnsureRconSocket();
}

void Zmq_UpdatePasswords( void ) {
	if ( !zmqRconPassword || !zmqStatsPassword ) {
		return;
	}
	if ( runtime.rconPasswordRevision == zmqRconPassword->modificationCount &&
		runtime.statsPasswordRevision == zmqStatsPassword->modificationCount ) {
		return;
	}
	ReplaceSecret( runtime.rconPassword, zmqRconPassword->string );
	ReplaceSecret( runtime.statsPassword, zmqStatsPassword->string );
	runtime.rconPasswordRevision = zmqRconPassword->modificationCount;
	runtime.statsPasswordRevision = zmqStatsPassword->modificationCount;
	RecreateAuthenticatedSockets();
	Com_Printf( "ZMQ credentials updated; active sockets were rebound\n" );
}

void Zmq_InitStatsPublisher( void ) {
	EnsureStatsSocket();
}

void Zmq_ShutdownStatsPublisher( void ) {
	CloseSocket( runtime.statsSocket );
	runtime.statsEndpoint.clear();
	runtime.statsAttempted = false;
	SetEndpointStatus( zmqStatsEndpointStatus, "zmq_stats_endpoint", runtime.statsEndpoint );
}

void Zmq_SubmitMatchReport( const void *report ) {
	/* Retail passes an opaque Json::Value-like object here.  Its ABI is not an
	 * engine contract, so do not reinterpret or dereference it.  Publication
	 * retains the event envelope with a null payload until an owned serializer
	 * exists at the game-module boundary. */
	(void)report;
	Publish( "MATCH_REPORT", nullptr );
}

void Zmq_SubmitMatchReportJson( const char *json ) {
	Publish( "MATCH_REPORT", json );
}

void Zmq_SubmitMatchSummaryJson( const char *json ) {
	Publish( "MATCH_SUMMARY", json );
}

void Zmq_ReportPlayerEvent( unsigned int steamIdLow, unsigned int steamIdHigh,
		const void *clientStats, const char *eventName, const void *payload ) {
	(void)steamIdLow;
	(void)steamIdHigh;
	(void)clientStats;
	(void)payload;
	Publish( eventName, nullptr );
}

void Zmq_ReportPlayerEventJson( const char *eventName, const char *json ) {
	Publish( eventName, json );
}

void Zmq_BroadcastRconOutput( const char *message ) {
	if ( !runtime.rconSocket || runtime.broadcasting || runtime.peers.empty() ||
		std::this_thread::get_id() != runtime.ownerThread ) {
		return;
	}
	runtime.broadcasting = true;
	for ( auto peer = runtime.peers.begin(); peer != runtime.peers.end(); ) {
		// REQ sockets may only receive exactly one reply after each request.
		// Unsolicited console streaming is therefore limited to DEALER peers.
		if ( peer->requestDelimiter ) {
			++peer;
			continue;
		}
		if ( SendToPeer( peer->identity, message, peer->requestDelimiter ) ) {
			++peer;
		} else {
			peer = runtime.peers.erase( peer );
		}
	}
	runtime.broadcasting = false;
}

void Zmq_PumpRcon( void ) {
	PumpAuthentication();
	if ( !EnsureRconSocket() ) {
		return;
	}

	for ( int commandCount = 0;
		commandCount < MAX_RCON_COMMANDS_PER_FRAME && PollReadable( runtime.rconSocket );
		++commandCount ) {
		std::string identity;
		std::string command;
		bool more = false;
		bool requestDelimiter = false;
		if ( !ReceiveFrame( runtime.rconSocket, identity, more, MAX_ZMQ_IDENTITY ) || identity.empty() || !more ||
			!ReceiveFrame( runtime.rconSocket, command, more, MAX_STRING_CHARS - 1 ) ) {
			if ( !DrainFrames( runtime.rconSocket, more ) ) {
				ResetSocketsAfterFramingFailure( "RCON" );
				return;
			}
			continue;
		}
		// REQ sockets insert an empty delimiter after the ROUTER identity.
		if ( command.empty() && more ) {
			requestDelimiter = true;
			if ( !ReceiveFrame( runtime.rconSocket, command, more, MAX_STRING_CHARS - 1 ) ) {
				if ( !DrainFrames( runtime.rconSocket, more ) ) {
					ResetSocketsAfterFramingFailure( "RCON" );
					return;
				}
				continue;
			}
		}
		const bool hadExtraFrames = more;
		if ( !DrainFrames( runtime.rconSocket, more ) ) {
			ResetSocketsAfterFramingFailure( "RCON" );
			return;
		}
		if ( hadExtraFrames ) {
			SendToPeer( identity, "invalid multipart framing\n", requestDelimiter );
			continue;
		}
		if ( !ValidCommand( command ) ) {
			SendToPeer( identity, "invalid command\n", requestDelimiter );
			continue;
		}

		auto peer = FindPeer( identity );
		if ( peer == runtime.peers.end() ) {
			if ( runtime.peers.size() >= MAX_RCON_PEERS ) {
				SendToPeer( identity, "RCON peer limit reached\n", requestDelimiter );
				continue;
			}
			runtime.peers.push_back( { identity, requestDelimiter } );
			SendToPeer( identity, "FnQL ZMQ RCON ready\n", requestDelimiter );
			continue;
		}
		peer->requestDelimiter = requestDelimiter;
		Com_DPrintf( "ZMQ RCON command received from authenticated peer\n" );
		std::array<char, MAX_STRING_CHARS> redirectBuffer{};
		runtime.commandOutput.clear();
		runtime.commandOutputTruncated = false;
		Com_BeginRedirect( redirectBuffer.data(), static_cast<int>( redirectBuffer.size() ),
			CollectRconCommandOutput );
		Cmd_ExecuteString( command.c_str() );
		Com_EndRedirect();
		if ( runtime.commandOutput.empty() ) {
			runtime.commandOutput = "\n";
		}
		if ( !SendToPeer( identity, runtime.commandOutput.c_str(), requestDelimiter ) ) {
			runtime.peers.erase( FindPeer( identity ) );
		}
		ScrubString( runtime.commandOutput );
		runtime.commandOutputTruncated = false;
	}
}

qboolean Zmq_RconActive( void ) {
	return runtime.rconSocket ? qtrue : qfalse;
}

void Zmq_ShutdownRuntime( void ) {
	bool contextTerminated = true;

	CloseSocket( runtime.rconSocket );
	CloseSocket( runtime.statsSocket );
	CloseSocket( runtime.authSocket );
	runtime.peers.clear();
	if ( runtime.context && runtime.api.ctxTerm ) {
		int result;
		do {
			result = runtime.api.ctxTerm( runtime.context );
		} while ( result != 0 && runtime.api.errorNumber &&
			runtime.api.errorNumber() == EINTR );
		if ( result != 0 ) {
			contextTerminated = false;
			runtime.shutdownFailed = true;
			Com_Printf( "ZMQ: context termination failed; retaining the runtime library safely: %s\n",
				LastError() );
		}
	}
	if ( contextTerminated ) {
		runtime.context = nullptr;
		if ( runtime.api.library ) {
			Sys_UnloadLibrary( runtime.api.library );
		}
		ClearApi();
		runtime.shutdownFailed = false;
	}
	ScrubString( runtime.rconPassword );
	ScrubString( runtime.statsPassword );
	ScrubString( runtime.commandOutput );
	if ( runtime.selfTestCommandRegistered ) {
		Cmd_RemoveCommand( "zmq_selftest" );
		runtime.selfTestCommandRegistered = false;
	}
	runtime.rconEndpoint.clear();
	runtime.statsEndpoint.clear();
	SetEndpointStatus( zmqRconEndpointStatus, "zmq_rcon_endpoint", runtime.rconEndpoint );
	SetEndpointStatus( zmqStatsEndpointStatus, "zmq_stats_endpoint", runtime.statsEndpoint );
	runtime.rconPasswordRevision = -1;
	runtime.statsPasswordRevision = -1;
	runtime.ownerThread = {};
	if ( contextTerminated ) {
		runtime.loadAttempted = false;
	}
	runtime.rconAttempted = false;
	runtime.statsAttempted = false;
	SetRuntimeStatus( contextTerminated ? "disabled" :
		"unavailable: context termination failed" );
}
