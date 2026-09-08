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
#ifndef FNQL_CLIENT_WEBUI_NATIVE_REQUEST_HPP
#define FNQL_CLIENT_WEBUI_NATIVE_REQUEST_HPP

#include "webui_backend.hpp"

namespace fnql::webui {

// The command consumer accepts MAX_STRING_CHARS (1024) UTF-8 bytes including
// NUL. No valid request can contain more than 1023 UTF-16 code units. Encoding
// those units as ASCII also preserves NUL and unpaired surrogates so the
// native validator can reject them; Awesomium's returned-string ABI alone
// cannot distinguish a NUL-containing request from a truncated command.
inline constexpr std::size_t kNativeRequestBytes = 1024;
inline constexpr std::size_t kNativeRequestTransportCapacity =
	4 * ( kNativeRequestBytes - 1 ) + 3;
static_assert( kNativeRequestTransportCapacity <= 4096,
	"native request encoding must fit the retail string callback" );

// One result-producing call pops one complete request. 'e' is an empty queue,
// 'o' rejects an oversized request, and 'r' plus hexadecimal UTF-16 plus '!'
// is a complete request. The terminal marker detects truncated transports.
inline constexpr char kPopNativeRequestScript[] =
	"(function(){var q=window.__qlr_native_requests||[];if(!q.length){return 'e';}"
	"var s=String(q.shift());if(s.length>1023){return 'o';}var a=['r'];"
	"for(var i=0;i<s.length;++i){var h=s.charCodeAt(i).toString(16);"
	"a.push(('0000'+h).slice(-4));}a.push('!');return a.join('');})()";

struct NativeRequestResult {
	BackendResult result{};
	bool hasRequest = false;
};

inline NativeRequestResult DecodeNativeRequest( std::string_view encoded,
	char *buffer, std::size_t capacity ) noexcept {
	if ( buffer && capacity > 0 ) {
		buffer[0] = '\0';
	}
	const auto reject = [buffer, capacity]( std::string_view reason ) {
		if ( buffer && capacity > 0 ) {
			buffer[0] = '\0';
		}
		return NativeRequestResult{
			BackendResult::Failure( BackendError::InvalidArgument, reason ), false };
	};
	if ( !buffer || capacity == 0 ) {
		return reject( "WebUI native request destination is invalid" );
	}
	if ( encoded == "e" ) {
		return {};
	}
	if ( encoded == "o" ) {
		return reject( "WebUI native request exceeded the bridge buffer" );
	}
	if ( encoded.size() < 2 || encoded.front() != 'r' || encoded.back() != '!'
		|| ( encoded.size() - 2 ) % 4 != 0
		|| encoded.size() >= kNativeRequestTransportCapacity ) {
		return reject( "WebUI native request has an incomplete or invalid envelope" );
	}
	encoded.remove_prefix( 1 );
	encoded.remove_suffix( 1 );
	const auto readUnit = [&encoded]( std::uint32_t &unit ) {
		if ( encoded.size() < 4 ) {
			return false;
		}
		unit = 0;
		for ( std::size_t index = 0; index < 4; ++index ) {
			const char digit = encoded[index];
			const unsigned int value = digit >= '0' && digit <= '9'
				? static_cast<unsigned int>( digit - '0' )
				: digit >= 'a' && digit <= 'f'
					? static_cast<unsigned int>( digit - 'a' + 10 ) : 16u;
			if ( value > 15 ) {
				return false;
			}
			unit = unit * 16u + value;
		}
		encoded.remove_prefix( 4 );
		return true;
	};
	std::size_t length = 0;
	while ( !encoded.empty() ) {
		std::uint32_t scalar;
		if ( !readUnit( scalar ) || scalar == 0 ) {
			return reject( "WebUI native request contains an invalid UTF-16 code unit" );
		}
		if ( scalar >= 0xd800u && scalar <= 0xdbffu ) {
			std::uint32_t low;
			if ( !readUnit( low ) || low < 0xdc00u || low > 0xdfffu ) {
				return reject( "WebUI native request contains malformed UTF-16" );
			}
			scalar = 0x10000u + ( ( scalar - 0xd800u ) << 10u ) + low - 0xdc00u;
		} else if ( scalar >= 0xdc00u && scalar <= 0xdfffu ) {
			return reject( "WebUI native request contains malformed UTF-16" );
		}
		char bytes[4];
		std::size_t count;
		if ( scalar <= 0x7fu ) {
			bytes[0] = static_cast<char>( scalar );
			count = 1;
		} else if ( scalar <= 0x7ffu ) {
			bytes[0] = static_cast<char>( 0xc0u | ( scalar >> 6u ) );
			bytes[1] = static_cast<char>( 0x80u | ( scalar & 0x3fu ) );
			count = 2;
		} else if ( scalar <= 0xffffu ) {
			bytes[0] = static_cast<char>( 0xe0u | ( scalar >> 12u ) );
			bytes[1] = static_cast<char>( 0x80u | ( ( scalar >> 6u ) & 0x3fu ) );
			bytes[2] = static_cast<char>( 0x80u | ( scalar & 0x3fu ) );
			count = 3;
		} else {
			bytes[0] = static_cast<char>( 0xf0u | ( scalar >> 18u ) );
			bytes[1] = static_cast<char>( 0x80u | ( ( scalar >> 12u ) & 0x3fu ) );
			bytes[2] = static_cast<char>( 0x80u | ( ( scalar >> 6u ) & 0x3fu ) );
			bytes[3] = static_cast<char>( 0x80u | ( scalar & 0x3fu ) );
			count = 4;
		}
		if ( count >= capacity - length ) {
			return reject( "WebUI native request exceeds the UTF-8 bridge buffer" );
		}
		std::memcpy( buffer + length, bytes, count );
		length += count;
	}
	buffer[length] = '\0';
	return { BackendResult::Success(), true };
}

} // namespace fnql::webui

#endif // FNQL_CLIENT_WEBUI_NATIVE_REQUEST_HPP
