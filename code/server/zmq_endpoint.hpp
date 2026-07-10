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

#ifndef FNQL_SERVER_ZMQ_ENDPOINT_HPP
#define FNQL_SERVER_ZMQ_ENDPOINT_HPP

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace fnql::zmq {

inline bool ValidHostCharacter( unsigned char character ) noexcept {
	return std::isalnum( character ) != 0 || character == '.' ||
		character == ':' || character == '-' || character == '_' ||
		character == '[' || character == ']' || character == '*' ||
		character == '%';
}

inline bool IsAbsoluteLibraryPath( std::string_view path ) noexcept {
	if ( path.empty() || path.size() >= 1024 ) {
		return false;
	}
	for ( const unsigned char character : path ) {
		if ( character == 0 || character < 0x20 || character == 0x7f ) {
			return false;
		}
	}

	if ( path.front() == '/' ) {
		return true;
	}
	if ( path.size() >= 3 && std::isalpha( static_cast<unsigned char>( path[0] ) )
		&& path[1] == ':' && ( path[2] == '/' || path[2] == '\\' ) ) {
		return true;
	}
	return path.size() >= 5 && path[0] == '\\' && path[1] == '\\'
		&& path[2] != '\\' && path[2] != '/';
}

inline bool IsAbsoluteWindowsLibraryPath( std::string_view path ) noexcept {
	if ( !IsAbsoluteLibraryPath( path ) || path.size() < 3 ) {
		return false;
	}
	return ( std::isalpha( static_cast<unsigned char>( path[0] ) )
		&& path[1] == ':' && ( path[2] == '/' || path[2] == '\\' ) )
		|| ( path.size() >= 5 && path[0] == '\\' && path[1] == '\\'
			&& path[2] != '\\' && path[2] != '/' );
}

/*
Normalize a host-only cvar value for a ZeroMQ TCP endpoint. ZeroMQ requires
IPv6 literals to be bracketed so their colons cannot be confused with the
endpoint port separator. Ports and URI schemes are deliberately rejected;
the caller owns the port as a separately validated integer.
*/
inline bool NormalizeTcpHost( std::string_view host,
		std::string &normalized ) {
	normalized.clear();
	if ( host.empty() || host.size() >= 128 ||
		host.find( "://" ) != std::string_view::npos ) {
		return false;
	}

	for ( const unsigned char character : host ) {
		if ( !ValidHostCharacter( character ) ) {
			return false;
		}
	}

	const bool opensBracket = host.front() == '[';
	const bool closesBracket = host.back() == ']';
	const std::size_t firstOpen = host.find( '[' );
	const std::size_t firstClose = host.find( ']' );
	if ( opensBracket || closesBracket || firstOpen != std::string_view::npos ||
		firstClose != std::string_view::npos ) {
		if ( !opensBracket || !closesBracket || host.size() < 4 ||
			firstOpen != 0 || firstClose != host.size() - 1 ||
			host.find( '[', 1 ) != std::string_view::npos ||
			host.find( ']', firstClose + 1 ) != std::string_view::npos ) {
			return false;
		}

		const std::string_view literal = host.substr( 1, host.size() - 2 );
		if ( literal.find( ':' ) == std::string_view::npos ||
			literal.find( ':', literal.find( ':' ) + 1 ) == std::string_view::npos ) {
			return false;
		}
		normalized.assign( host.data(), host.size() );
		return true;
	}

	const std::size_t firstColon = host.find( ':' );
	if ( firstColon == std::string_view::npos ) {
		normalized.assign( host.data(), host.size() );
		return true;
	}

	// A single colon is a forbidden embedded port or malformed IPv6 literal.
	if ( host.find( ':', firstColon + 1 ) == std::string_view::npos ) {
		return false;
	}

	normalized.reserve( host.size() + 2 );
	normalized.push_back( '[' );
	normalized.append( host.data(), host.size() );
	normalized.push_back( ']' );
	return true;
}

inline bool BuildTcpEndpoint( std::string_view host, int port,
		std::string &endpoint ) {
	std::string normalizedHost;
	endpoint.clear();
	if ( port < 1 || port > 65535 ||
		!NormalizeTcpHost( host, normalizedHost ) ) {
		return false;
	}

	endpoint.reserve( normalizedHost.size() + 16 );
	endpoint = "tcp://";
	endpoint += normalizedHost;
	endpoint.push_back( ':' );
	endpoint += std::to_string( port );
	if ( endpoint.size() >= 256 ) {
		endpoint.clear();
		return false;
	}
	return true;
}

} // namespace fnql::zmq

#endif
