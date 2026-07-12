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

#ifndef FNQL_SERVER_JSON_DOCUMENT_HPP
#define FNQL_SERVER_JSON_DOCUMENT_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fnql::server::json {

inline constexpr std::size_t MaximumDepth = 32;
inline constexpr std::size_t MaximumDocumentBytes = 32'768;

class Reader {
public:
	explicit Reader( std::string_view input ) noexcept : input_( input ) {}

	[[nodiscard]] bool Document() noexcept {
		if ( input_.empty() || input_.size() > MaximumDocumentBytes ) {
			return false;
		}
		Whitespace();
		if ( !Value( 0 ) ) {
			return false;
		}
		Whitespace();
		return offset_ == input_.size();
	}

private:
	[[nodiscard]] bool Value( std::size_t depth ) noexcept {
		if ( depth > MaximumDepth || offset_ >= input_.size() ) {
			return false;
		}
		switch ( input_[offset_] ) {
		case '{': return Object( depth );
		case '[': return Array( depth );
		case '"': return String();
		case 't': return Literal( "true" );
		case 'f': return Literal( "false" );
		case 'n': return Literal( "null" );
		default: return Number();
		}
	}

	[[nodiscard]] bool Object( std::size_t depth ) noexcept {
		++offset_;
		Whitespace();
		if ( Take( '}' ) ) {
			return true;
		}
		for ( ;; ) {
			if ( !String() ) {
				return false;
			}
			Whitespace();
			if ( !Take( ':' ) ) {
				return false;
			}
			Whitespace();
			if ( !Value( depth + 1 ) ) {
				return false;
			}
			Whitespace();
			if ( Take( '}' ) ) {
				return true;
			}
			if ( !Take( ',' ) ) {
				return false;
			}
			Whitespace();
		}
	}

	[[nodiscard]] bool Array( std::size_t depth ) noexcept {
		++offset_;
		Whitespace();
		if ( Take( ']' ) ) {
			return true;
		}
		for ( ;; ) {
			if ( !Value( depth + 1 ) ) {
				return false;
			}
			Whitespace();
			if ( Take( ']' ) ) {
				return true;
			}
			if ( !Take( ',' ) ) {
				return false;
			}
			Whitespace();
		}
	}

	[[nodiscard]] bool String() noexcept {
		if ( !Take( '"' ) ) {
			return false;
		}
		while ( offset_ < input_.size() ) {
			const std::uint8_t value = static_cast<std::uint8_t>( input_[offset_++] );
			if ( value == '"' ) {
				return true;
			}
			if ( value < 0x20u ) {
				return false;
			}
			if ( value == '\\' ) {
				if ( offset_ >= input_.size() ) {
					return false;
				}
				const char escape = input_[offset_++];
				if ( escape == 'u' ) {
					if ( !HexCodeUnit() ) {
						return false;
					}
				} else if ( escape != '"' && escape != '\\' && escape != '/' &&
					escape != 'b' && escape != 'f' && escape != 'n' &&
					escape != 'r' && escape != 't' ) {
					return false;
				}
				continue;
			}
			if ( value >= 0x80u && !Utf8Tail( value ) ) {
				return false;
			}
		}
		return false;
	}

	[[nodiscard]] bool HexCodeUnit() noexcept {
		if ( input_.size() - offset_ < 4 ) {
			return false;
		}
		for ( int i = 0; i < 4; ++i ) {
			const char value = input_[offset_++];
			if ( !( value >= '0' && value <= '9' ) &&
				!( value >= 'a' && value <= 'f' ) &&
				!( value >= 'A' && value <= 'F' ) ) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] bool Utf8Tail( std::uint8_t lead ) noexcept {
		std::size_t tails = 0;
		std::uint32_t codePoint = 0;
		std::uint32_t minimum = 0;
		if ( lead >= 0xc2u && lead <= 0xdfu ) {
			tails = 1; codePoint = lead & 0x1fu; minimum = 0x80u;
		} else if ( lead >= 0xe0u && lead <= 0xefu ) {
			tails = 2; codePoint = lead & 0x0fu; minimum = 0x800u;
		} else if ( lead >= 0xf0u && lead <= 0xf4u ) {
			tails = 3; codePoint = lead & 0x07u; minimum = 0x10000u;
		} else {
			return false;
		}
		if ( input_.size() - offset_ < tails ) {
			return false;
		}
		for ( std::size_t i = 0; i < tails; ++i ) {
			const std::uint8_t tail = static_cast<std::uint8_t>( input_[offset_++] );
			if ( ( tail & 0xc0u ) != 0x80u ) {
				return false;
			}
			codePoint = ( codePoint << 6u ) | ( tail & 0x3fu );
		}
		return codePoint >= minimum && codePoint <= 0x10ffffu &&
			!( codePoint >= 0xd800u && codePoint <= 0xdfffu );
	}

	[[nodiscard]] bool Number() noexcept {
		const std::size_t begin = offset_;
		(void)Take( '-' );
		if ( Take( '0' ) ) {
			if ( offset_ < input_.size() && Digit( input_[offset_] ) ) {
				return false;
			}
		} else {
			if ( offset_ >= input_.size() || input_[offset_] < '1' || input_[offset_] > '9' ) {
				return false;
			}
			while ( offset_ < input_.size() && Digit( input_[offset_] ) ) {
				++offset_;
			}
		}
		if ( Take( '.' ) ) {
			if ( offset_ >= input_.size() || !Digit( input_[offset_] ) ) {
				return false;
			}
			while ( offset_ < input_.size() && Digit( input_[offset_] ) ) {
				++offset_;
			}
		}
		if ( offset_ < input_.size() &&
			( input_[offset_] == 'e' || input_[offset_] == 'E' ) ) {
			++offset_;
			if ( offset_ < input_.size() &&
				( input_[offset_] == '+' || input_[offset_] == '-' ) ) {
				++offset_;
			}
			if ( offset_ >= input_.size() || !Digit( input_[offset_] ) ) {
				return false;
			}
			while ( offset_ < input_.size() && Digit( input_[offset_] ) ) {
				++offset_;
			}
		}
		return offset_ > begin;
	}

	[[nodiscard]] bool Literal( std::string_view literal ) noexcept {
		if ( input_.size() - offset_ < literal.size() ||
			input_.substr( offset_, literal.size() ) != literal ) {
			return false;
		}
		offset_ += literal.size();
		return true;
	}

	void Whitespace() noexcept {
		while ( offset_ < input_.size() &&
			( input_[offset_] == ' ' || input_[offset_] == '\t' ||
				input_[offset_] == '\n' || input_[offset_] == '\r' ) ) {
			++offset_;
		}
	}

	[[nodiscard]] bool Take( char expected ) noexcept {
		if ( offset_ < input_.size() && input_[offset_] == expected ) {
			++offset_;
			return true;
		}
		return false;
	}

	[[nodiscard]] static constexpr bool Digit( char value ) noexcept {
		return value >= '0' && value <= '9';
	}

	std::string_view input_;
	std::size_t offset_ = 0;
};

[[nodiscard]] inline bool DocumentIsValid( std::string_view document ) noexcept {
	return Reader( document ).Document();
}

} // namespace fnql::server::json

#endif
