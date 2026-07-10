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

#ifndef FNQL_NETCHAN_CODEC_HPP
#define FNQL_NETCHAN_CODEC_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fnql {

inline std::string_view NetchanCommandView( const char *command,
	std::size_t capacity ) noexcept {
	std::size_t length = 0;

	if ( !command ) {
		return {};
	}

	while ( length < capacity && command[length] != '\0' ) {
		++length;
	}

	return { command, length };
}

inline void ApplyNetchanXor( std::uint8_t *data, std::size_t size,
	std::size_t begin, std::uint8_t key, std::string_view command ) noexcept {
	std::size_t commandIndex = 0;

	if ( !data || begin >= size ) {
		return;
	}

	for ( std::size_t offset = begin; offset < size; ++offset ) {
		std::uint8_t mixer = 0;

		if ( !command.empty() ) {
			mixer = static_cast<std::uint8_t>( command[commandIndex] );
			if ( ++commandIndex == command.size() ) {
				commandIndex = 0;
			}
		}

		if ( mixer > 127u || mixer == static_cast<std::uint8_t>( '%' ) ) {
			mixer = static_cast<std::uint8_t>( '.' );
		}

		key ^= static_cast<std::uint8_t>( mixer << ( offset & 1u ) );
		data[offset] ^= key;
	}
}

} // namespace fnql

#endif
