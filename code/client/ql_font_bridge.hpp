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

#ifndef FNQL_CLIENT_QL_FONT_BRIDGE_HPP
#define FNQL_CLIENT_QL_FONT_BRIDGE_HPP

#include <cstring>

namespace fnql::font {

inline void CopyMeasureBounds( float *destination, const float *source ) noexcept {
	if ( !destination || !source ) {
		return;
	}

	std::memcpy( destination, source, sizeof( float ) * 5 );
}

inline bool IsUtf8ContinuationByte( unsigned char byte ) noexcept {
	return ( byte & 0xc0 ) == 0x80;
}

inline int ClampUtf8Boundary( const char *text, int index ) noexcept {
	if ( !text ) {
		return 0;
	}

	const int length = static_cast<int>( std::strlen( text ) );
	if ( index < 0 ) {
		index = 0;
	} else if ( index > length ) {
		index = length;
	}

	while ( index > 0 && index < length &&
		IsUtf8ContinuationByte( static_cast<unsigned char>( text[ index ] ) ) ) {
		--index;
	}
	return index;
}

inline int PreviousUtf8Boundary( const char *text, int index ) noexcept {
	index = ClampUtf8Boundary( text, index );
	if ( !text || index <= 0 ) {
		return 0;
	}

	--index;
	while ( index > 0 &&
		IsUtf8ContinuationByte( static_cast<unsigned char>( text[ index ] ) ) ) {
		--index;
	}
	return index;
}

inline int NextUtf8Boundary( const char *text, int index ) noexcept {
	if ( !text ) {
		return 0;
	}

	const int length = static_cast<int>( std::strlen( text ) );
	index = ClampUtf8Boundary( text, index );
	if ( index >= length ) {
		return length;
	}

	++index;
	while ( index < length &&
		IsUtf8ContinuationByte( static_cast<unsigned char>( text[ index ] ) ) ) {
		++index;
	}
	return index;
}

inline int CountUtf8Characters( const char *text, int begin, int end ) noexcept {
	if ( !text ) {
		return 0;
	}

	begin = ClampUtf8Boundary( text, begin );
	end = ClampUtf8Boundary( text, end );
	if ( end < begin ) {
		return 0;
	}

	int count = 0;
	for ( int index = begin; index < end; index = NextUtf8Boundary( text, index ) ) {
		++count;
	}
	return count;
}

struct Utf8FieldWindow {
	int startByte;
	int endByte;
	int cursorByte;
	int characters;
};

// Retail fields retain byte offsets in field_t, but choose their visible span
// in Unicode characters. Anchor the window at the end of the value, shifting
// it left only as far as required to keep an earlier cursor visible.
inline Utf8FieldWindow GetUtf8FieldWindow( const char *text, int cursor,
	int maxCharacters ) noexcept {
	if ( !text ) {
		return { 0, 0, 0, 0 };
	}

	const int length = static_cast<int>( std::strlen( text ) );
	cursor = ClampUtf8Boundary( text, cursor );
	if ( maxCharacters <= 0 ) {
		return { cursor, cursor, cursor, 0 };
	}

	int start = length;
	int end = length;
	int characters = 0;
	while ( characters < maxCharacters && start > 0 ) {
		start = PreviousUtf8Boundary( text, start );
		++characters;

		if ( characters == maxCharacters && cursor < start ) {
			end = PreviousUtf8Boundary( text, end );
			--characters;
		}
	}

	if ( end < start ) {
		end = start;
	}
	return { start, end, cursor, characters };
}

} // namespace fnql::font

#endif
