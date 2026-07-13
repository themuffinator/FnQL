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

#include "factory_catalog.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace fnql::server::factory {
namespace {

enum class JsonKind {
	Null,
	Boolean,
	SignedInteger,
	UnsignedInteger,
	Real,
	String,
	Array,
	Object
};

struct JsonValue {
	JsonValue() = default;
	JsonValue( const JsonValue & ) = delete;
	JsonValue &operator=( const JsonValue & ) = delete;
	JsonValue( JsonValue && ) noexcept = default;
	JsonValue &operator=( JsonValue && ) noexcept = default;

	~JsonValue() {
		// The parser accepts retail's full file-bounded nesting depth. Detach
		// descendants into a heap worklist so vector element destruction cannot
		// recurse once per JSON level on small-stack Windows threads.
		std::vector<JsonValue> pending = std::move( children );
		while ( !pending.empty() ) {
			JsonValue current = std::move( pending.back() );
			pending.pop_back();
			if ( current.children.empty() ) {
				continue;
			}
			pending.reserve( pending.size() + current.children.size() );
			for ( JsonValue &child : current.children ) {
				pending.push_back( std::move( child ) );
			}
			current.children.clear();
		}
	}

	JsonKind kind = JsonKind::Null;
	std::size_t byteOffset = 0;
	bool boolean = false;
	double real = 0.0;
	std::string text;
	std::vector<std::string> keys;
	std::vector<JsonValue> children;
};

class JsonParser {
public:
	explicit JsonParser( std::string_view input ) noexcept : input_( input ) {}

	[[nodiscard]] bool Parse( JsonValue &root ) {
		if ( !Trivia() ) {
			return false;
		}
		if ( offset_ == input_.size() ) {
			return Fail( "empty JSON document" );
		}

		std::vector<ContainerFrame> stack;
		stack.reserve( 32 );
		if ( !StartValue( root, stack ) ) {
			return false;
		}
		while ( !stack.empty() ) {
			ContainerFrame &frame = stack.back();
			switch ( frame.state ) {
			case ContainerState::ObjectFirst:
				if ( !Trivia() ) {
					return false;
				}
				if ( Take( '}' ) ) {
					stack.pop_back();
					break;
				}
				if ( !ObjectMember( frame, stack ) ) {
					return false;
				}
				break;

			case ContainerState::ObjectNext:
				if ( !Trivia() ) {
					return false;
				}
				if ( offset_ < input_.size() && input_[offset_] == '}' ) {
					return Fail( "trailing commas are not valid JSON" );
				}
				if ( !ObjectMember( frame, stack ) ) {
					return false;
				}
				break;

			case ContainerState::ObjectAfterValue:
				if ( !Trivia() ) {
					return false;
				}
				if ( Take( '}' ) ) {
					stack.pop_back();
					break;
				}
				if ( !Take( ',' ) ) {
					return Fail( "expected ',' or '}' in a JSON object" );
				}
				frame.state = ContainerState::ObjectNext;
				break;

			case ContainerState::ArrayFirst:
				// readArray recognizes an empty array after spaces only. Comments
				// instead enter readValue, where a following ']' is an error.
				WhitespaceOnly();
				if ( Take( ']' ) ) {
					stack.pop_back();
					break;
				}
				if ( !Trivia() || !ArrayElement( frame, stack ) ) {
					return false;
				}
				break;

			case ContainerState::ArrayNext:
				if ( !Trivia() || !ArrayElement( frame, stack ) ) {
					return false;
				}
				break;

			case ContainerState::ArrayAfterValue:
				if ( !Trivia() ) {
					return false;
				}
				if ( Take( ']' ) ) {
					stack.pop_back();
					break;
				}
				if ( !Take( ',' ) ) {
					return Fail( "expected ',' or ']' in a JSON array" );
				}
				frame.state = ContainerState::ArrayNext;
				break;
			}
		}

		// The JsonCpp Reader embedded in retail returns the first readValue()
		// result and does not require end-of-input. Preserve that permissive
		// root contract for community factory files.
		return true;
	}

	[[nodiscard]] std::size_t ErrorOffset() const noexcept { return errorOffset_; }
	[[nodiscard]] const std::string &Error() const noexcept { return error_; }

private:
	enum class ContainerState {
		ObjectFirst,
		ObjectNext,
		ObjectAfterValue,
		ArrayFirst,
		ArrayNext,
		ArrayAfterValue
	};

	struct ContainerFrame {
		JsonValue *value = nullptr;
		ContainerState state = ContainerState::ObjectFirst;
	};

	[[nodiscard]] bool StartValue( JsonValue &value,
			std::vector<ContainerFrame> &stack ) {
		if ( offset_ >= input_.size() ) {
			return Fail( "expected a JSON value" );
		}

		value.byteOffset = offset_;
		switch ( input_[offset_] ) {
		case '{':
			value.kind = JsonKind::Object;
			++offset_;
			stack.push_back( { &value, ContainerState::ObjectFirst } );
			return true;
		case '[':
			value.kind = JsonKind::Array;
			++offset_;
			stack.push_back( { &value, ContainerState::ArrayFirst } );
			return true;
		case '"':
			value.kind = JsonKind::String;
			return String( value.text );
		case 't':
			if ( !Literal( "true" ) ) {
				return false;
			}
			value.kind = JsonKind::Boolean;
			value.boolean = true;
			return true;
		case 'f':
			if ( !Literal( "false" ) ) {
				return false;
			}
			value.kind = JsonKind::Boolean;
			value.boolean = false;
			return true;
		case 'n':
			if ( !Literal( "null" ) ) {
				return false;
			}
			value.kind = JsonKind::Null;
			return true;
		default:
			return Number( value );
		}
	}

	[[nodiscard]] bool ObjectMember( ContainerFrame &frame,
			std::vector<ContainerFrame> &stack ) {
		if ( offset_ >= input_.size() || input_[offset_] != '"' ) {
			return Fail( "expected a JSON object member name" );
		}
		std::string key;
		if ( !String( key ) ) {
			return false;
		}
		const std::size_t nul = key.find( '\0' );
		if ( nul != std::string::npos ) {
			// Retail passes the decoded std::string through Json::Value's
			// const-char key overload, so insertion itself stops at NUL.
			key.resize( nul );
		}
		WhitespaceOnly();
		if ( !Take( ':' ) ) {
			return Fail( "expected ':' after a JSON object member name" );
		}
		if ( !Trivia() ) {
			return false;
		}

		JsonValue *object = frame.value;
		JsonValue *child = nullptr;
		const auto existing = std::find( object->keys.begin(),
			object->keys.end(), key );
		if ( existing == object->keys.end() ) {
			object->keys.push_back( std::move( key ) );
			object->children.emplace_back();
			child = &object->children.back();
		} else {
			const std::size_t index = static_cast<std::size_t>(
			existing - object->keys.begin() );
			object->children[index] = JsonValue{};
			child = &object->children[index];
		}
		frame.state = ContainerState::ObjectAfterValue;
		return StartValue( *child, stack );
	}

	[[nodiscard]] bool ArrayElement( ContainerFrame &frame,
			std::vector<ContainerFrame> &stack ) {
		JsonValue *array = frame.value;
		array->children.emplace_back();
		JsonValue &child = array->children.back();
		frame.state = ContainerState::ArrayAfterValue;
		if ( !StartValue( child, stack ) ) {
			array->children.pop_back();
			return false;
		}
		return true;
	}

	[[nodiscard]] bool String( std::string &output ) {
		if ( !Take( '"' ) ) {
			return Fail( "expected a JSON string" );
		}

		while ( offset_ < input_.size() ) {
			const std::uint8_t byte = static_cast<std::uint8_t>( input_[offset_] );
			if ( byte == '"' ) {
				++offset_;
				return true;
			}
			if ( byte == '\\' ) {
				++offset_;
				if ( offset_ >= input_.size() ) {
					return Fail( "unterminated JSON escape sequence" );
				}
				const char escape = input_[offset_++];
				switch ( escape ) {
				case '"': output.push_back( '"' ); break;
				case '\\': output.push_back( '\\' ); break;
				case '/': output.push_back( '/' ); break;
				case 'b': output.push_back( '\b' ); break;
				case 'f': output.push_back( '\f' ); break;
				case 'n': output.push_back( '\n' ); break;
				case 'r': output.push_back( '\r' ); break;
				case 't': output.push_back( '\t' ); break;
				case 'u':
					if ( !EscapedCodePoint( output ) ) {
						return false;
					}
					break;
				default:
					return Fail( "invalid JSON escape sequence" );
				}
				continue;
			}
			// Old JsonCpp copies raw bytes verbatim. This intentionally accepts
			// controls and non-UTF-8 data that appear in community factory files.
			output.push_back( static_cast<char>( byte ) );
			++offset_;
		}

		return Fail( "unterminated JSON string" );
	}

	[[nodiscard]] bool EscapedCodePoint( std::string &output ) {
		std::uint16_t first = 0;
		if ( !HexCodeUnit( first ) ) {
			return Fail( "invalid Unicode escape in a JSON string" );
		}

		std::uint32_t codePoint = first;
		if ( first >= 0xd800u && first <= 0xdbffu ) {
			if ( input_.size() - offset_ < 6 || input_[offset_] != '\\' ||
				input_[offset_ + 1] != 'u' ) {
				return Fail( "high surrogate is not followed by a low surrogate" );
			}
			offset_ += 2;
			std::uint16_t second = 0;
			if ( !HexCodeUnit( second ) ) {
				return Fail( "invalid second Unicode escape in a surrogate pair" );
			}
			codePoint = 0x10000u
				+ ( ( static_cast<std::uint32_t>( first ) - 0xd800u ) << 10u )
				+ ( static_cast<std::uint32_t>( second ) & 0x3ffu );
		}

		AppendUtf8( output, codePoint );
		return true;
	}

	[[nodiscard]] bool HexCodeUnit( std::uint16_t &value ) noexcept {
		if ( input_.size() - offset_ < 4 ) {
			return false;
		}
		std::uint16_t result = 0;
		for ( int index = 0; index < 4; ++index ) {
			const char digit = input_[offset_++];
			result = static_cast<std::uint16_t>( result << 4u );
			if ( digit >= '0' && digit <= '9' ) {
				result = static_cast<std::uint16_t>( result + digit - '0' );
			} else if ( digit >= 'a' && digit <= 'f' ) {
				result = static_cast<std::uint16_t>( result + digit - 'a' + 10 );
			} else if ( digit >= 'A' && digit <= 'F' ) {
				result = static_cast<std::uint16_t>( result + digit - 'A' + 10 );
			} else {
				return false;
			}
		}
		value = result;
		return true;
	}

	static void AppendUtf8( std::string &output, std::uint32_t codePoint ) {
		if ( codePoint <= 0x7fu ) {
			output.push_back( static_cast<char>( codePoint ) );
		} else if ( codePoint <= 0x7ffu ) {
			output.push_back( static_cast<char>( 0xc0u | ( codePoint >> 6u ) ) );
			output.push_back( static_cast<char>( 0x80u | ( codePoint & 0x3fu ) ) );
		} else if ( codePoint <= 0xffffu ) {
			output.push_back( static_cast<char>( 0xe0u | ( codePoint >> 12u ) ) );
			output.push_back( static_cast<char>( 0x80u | ( ( codePoint >> 6u ) & 0x3fu ) ) );
			output.push_back( static_cast<char>( 0x80u | ( codePoint & 0x3fu ) ) );
		} else {
			output.push_back( static_cast<char>( 0xf0u | ( codePoint >> 18u ) ) );
			output.push_back( static_cast<char>( 0x80u | ( ( codePoint >> 12u ) & 0x3fu ) ) );
			output.push_back( static_cast<char>( 0x80u | ( ( codePoint >> 6u ) & 0x3fu ) ) );
			output.push_back( static_cast<char>( 0x80u | ( codePoint & 0x3fu ) ) );
		}
	}

	[[nodiscard]] bool Number( JsonValue &value ) {
		const std::size_t begin = offset_;
		if ( offset_ >= input_.size() ||
			( input_[offset_] != '-' && !Digit( input_[offset_] ) ) ) {
			return Fail( "invalid JSON value" );
		}
		while ( offset_ < input_.size() ) {
			const char character = input_[offset_];
			if ( !Digit( character ) && character != '.' && character != 'e' &&
				character != 'E' && character != '+' && character != '-' ) {
				break;
			}
			++offset_;
		}

		const std::string_view token = input_.substr( begin, offset_ - begin );
		const bool negative = !token.empty() && token.front() == '-';
		bool decodeAsReal = false;
		for ( std::size_t index = 0; index < token.size(); ++index ) {
			const char character = token[index];
			if ( character == '.' || character == 'e' || character == 'E' ||
				character == '+' || ( character == '-' && index != 0 ) ) {
				decodeAsReal = true;
				break;
			}
		}

		if ( !decodeAsReal ) {
			std::uint64_t magnitude = 0;
			const std::uint64_t limit = negative ? UINT64_C( 0x80000000 )
				: UINT64_C( 0xffffffff );
			bool overflow = false;
			for ( std::size_t index = negative ? 1u : 0u;
				index < token.size(); ++index ) {
				const unsigned digit = static_cast<unsigned>( token[index] - '0' );
				if ( magnitude > ( limit - digit ) / 10u ) {
					overflow = true;
					break;
				}
				magnitude = magnitude * 10u + digit;
			}
			if ( !overflow ) {
				if ( negative ) {
					value.kind = JsonKind::SignedInteger;
					value.text = std::to_string( -static_cast<std::int64_t>( magnitude ) );
				} else {
					value.kind = magnitude <= static_cast<std::uint64_t>(
						( std::numeric_limits<std::int32_t>::max )() )
						? JsonKind::SignedInteger : JsonKind::UnsignedInteger;
					value.text = std::to_string( magnitude );
				}
				return true;
			}
		}

		// JsonCpp's old Reader sends permissive tokens to sscanf("%lf"). Find
		// the same decimal prefix so suffixes such as "1e" and "1+2" retain
		// the successfully decoded leading value.
		std::size_t cursor = negative ? 1u : 0u;
		const std::size_t integerBegin = cursor;
		while ( cursor < token.size() && Digit( token[cursor] ) ) {
			++cursor;
		}
		bool hasDigits = cursor != integerBegin;
		std::size_t prefixEnd = cursor;
		if ( cursor < token.size() && token[cursor] == '.' ) {
			++cursor;
			const std::size_t fractionBegin = cursor;
			while ( cursor < token.size() && Digit( token[cursor] ) ) {
				++cursor;
			}
			hasDigits = hasDigits || cursor != fractionBegin;
			prefixEnd = cursor;
		}
		if ( !hasDigits ) {
			return FailAt( begin, "invalid JSON number" );
		}
		if ( cursor < token.size() &&
			( token[cursor] == 'e' || token[cursor] == 'E' ) ) {
			std::size_t exponent = cursor + 1;
			if ( exponent < token.size() &&
				( token[exponent] == '+' || token[exponent] == '-' ) ) {
				++exponent;
			}
			const std::size_t exponentDigits = exponent;
			while ( exponent < token.size() && Digit( token[exponent] ) ) {
				++exponent;
			}
			if ( exponent != exponentDigits ) {
				prefixEnd = exponent;
			}
		}

		std::string_view prefix = token.substr( 0, prefixEnd );
		if ( !prefix.empty() && prefix.back() == '.' ) {
			prefix.remove_suffix( 1 );
		}
		// Retail decodes this permissive prefix with sscanf("%lf"). strtod
		// preserves the same C-locale overflow and underflow values while also
		// supporting the documented Visual Studio 2017 toolchain, whose
		// standard library has no floating-point from_chars overload.
		const std::string terminatedPrefix( prefix );
		char *end = nullptr;
		const double parsed = std::strtod( terminatedPrefix.c_str(), &end );
		if ( end != terminatedPrefix.c_str() + terminatedPrefix.size() ) {
			return FailAt( begin, "invalid JSON number" );
		}
		value.kind = JsonKind::Real;
		value.real = parsed;
		value.text.assign( token );
		return true;
	}

	[[nodiscard]] bool Literal( std::string_view literal ) {
		if ( input_.size() - offset_ < literal.size() ||
			input_.substr( offset_, literal.size() ) != literal ) {
			return Fail( "invalid JSON literal" );
		}
		offset_ += literal.size();
		return true;
	}

	[[nodiscard]] bool Trivia() {
		while ( offset_ < input_.size() ) {
			const char value = input_[offset_];
			if ( value == ' ' || value == '\t' || value == '\n' || value == '\r' ) {
				++offset_;
				continue;
			}
			if ( value != '/' || offset_ + 1 >= input_.size() ) {
				return true;
			}

			const char marker = input_[offset_ + 1];
			if ( marker == '/' ) {
				offset_ += 2;
				while ( offset_ < input_.size() && input_[offset_] != '\n' &&
					input_[offset_] != '\r' ) {
					++offset_;
				}
				continue;
			}
			if ( marker != '*' ) {
				return true;
			}

			const std::size_t commentOffset = offset_;
			offset_ += 2;
			while ( offset_ + 1 < input_.size() &&
				( input_[offset_] != '*' || input_[offset_ + 1] != '/' ) ) {
				++offset_;
			}
			if ( offset_ + 1 >= input_.size() ) {
				return FailAt( commentOffset, "unterminated JSON block comment" );
			}
			offset_ += 2;
		}
		return true;
	}

	void WhitespaceOnly() noexcept {
		while ( offset_ < input_.size() ) {
			const char value = input_[offset_];
			if ( value != ' ' && value != '\t' && value != '\n' && value != '\r' ) {
				return;
			}
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

	[[nodiscard]] bool Fail( std::string message ) {
		return FailAt( offset_, std::move( message ) );
	}

	[[nodiscard]] bool FailAt( std::size_t byteOffset, std::string message ) {
		if ( error_.empty() ) {
			errorOffset_ = byteOffset;
			error_ = std::move( message );
		}
		return false;
	}

	std::string_view input_;
	std::size_t offset_ = 0;
	std::size_t errorOffset_ = 0;
	std::string error_;
};

[[nodiscard]] const JsonValue *FindLastMember( const JsonValue &object,
	std::string_view name ) noexcept {
	if ( object.kind != JsonKind::Object ) {
		return nullptr;
	}
	for ( std::size_t index = object.keys.size(); index > 0; --index ) {
		if ( object.keys[index - 1] == name ) {
			return &object.children[index - 1];
		}
	}
	return nullptr;
}

[[nodiscard]] bool JsonCppMemberNameLess( std::string_view left,
	std::string_view right ) noexcept {
	const std::size_t shared = ( std::min )( left.size(), right.size() );
	for ( std::size_t index = 0; index < shared; ++index ) {
		const auto leftByte = static_cast<unsigned char>( left[index] );
		const auto rightByte = static_cast<unsigned char>( right[index] );
		if ( leftByte != rightByte ) {
			return leftByte < rightByte;
		}
	}
	return left.size() < right.size();
}

[[nodiscard]] std::vector<std::size_t> JsonCppObjectMemberIndexes(
	const JsonValue &object ) {
	std::vector<std::size_t> indexes;
	indexes.reserve( object.keys.size() );
	for ( std::size_t index = 0; index < object.keys.size(); ++index ) {
		indexes.push_back( index );
	}
	std::sort( indexes.begin(), indexes.end(),
		[&object]( std::size_t left, std::size_t right ) {
			if ( JsonCppMemberNameLess( object.keys[left], object.keys[right] ) ) {
				return true;
			}
			if ( JsonCppMemberNameLess( object.keys[right], object.keys[left] ) ) {
				return false;
			}
			return left < right;
		} );

	std::size_t uniqueCount = 0;
	for ( const std::size_t index : indexes ) {
		if ( uniqueCount != 0 &&
			object.keys[index] == object.keys[indexes[uniqueCount - 1]] ) {
			// Exact duplicates are adjacent and source-ordered, so replacement
			// leaves the last JsonCpp assignment for this member name.
			indexes[uniqueCount - 1] = index;
		} else {
			indexes[uniqueCount++] = index;
		}
	}
	indexes.resize( uniqueCount );
	return indexes;
}

void AddDiagnostic( AppendResult &result, DiagnosticLevel level,
	DiagnosticCode code, std::string_view sourcePath, std::size_t byteOffset,
	std::string message ) {
	Diagnostic diagnostic;
	diagnostic.level = level;
	diagnostic.code = code;
	diagnostic.sourcePath.assign( sourcePath.data(), sourcePath.size() );
	diagnostic.byteOffset = byteOffset;
	diagnostic.message = std::move( message );
	result.diagnostics.push_back( std::move( diagnostic ) );
}

[[nodiscard]] std::string CStringPrefix( std::string_view value ) {
	const std::size_t nul = value.find( '\0' );
	if ( nul != std::string_view::npos ) {
		value = value.substr( 0, nul );
	}
	return std::string( value );
}

[[nodiscard]] bool FormatSettingValue( const JsonValue &value,
	std::string &output ) {
	switch ( value.kind ) {
	case JsonKind::String:
		output = CStringPrefix( value.text );
		return true;
	case JsonKind::Boolean:
		output = value.boolean ? "1" : "0";
		return true;
	case JsonKind::SignedInteger: {
		std::int64_t parsed = 0;
		const char *begin = value.text.data();
		const char *end = begin + value.text.size();
		const auto conversion = std::from_chars( begin, end, parsed );
		if ( conversion.ec != std::errc{} || conversion.ptr != end ||
			parsed < ( std::numeric_limits<std::int32_t>::min )() ||
			parsed > ( std::numeric_limits<std::int32_t>::max )() ) {
			return false;
		}
		output = std::to_string( static_cast<std::int32_t>( parsed ) );
		return true;
	}
	case JsonKind::UnsignedInteger: {
		std::uint64_t parsed = 0;
		const char *begin = value.text.data();
		const char *end = begin + value.text.size();
		const auto conversion = std::from_chars( begin, end, parsed );
		if ( conversion.ec != std::errc{} || conversion.ptr != end ||
			parsed > static_cast<std::uint64_t>(
				( std::numeric_limits<std::int32_t>::max )() ) ) {
			// Retail's JsonCpp asInt() lets an exception escape for this range.
			// Return the null-value sentinel so loose content fails safely.
			return false;
		}
		output = std::to_string( static_cast<std::int32_t>( parsed ) );
		return true;
	}
	case JsonKind::Real: {
		if ( std::isinf( value.real ) ) {
			// MSVCR100's retail "%f" spelling is stable and part of the CVar
			// contract even when FnQL is built against a different C runtime.
			output = std::signbit( value.real ) ? "-1.#INF00" : "1.#INF00";
			return true;
		}
		if ( std::isnan( value.real ) ) {
			// The retail JSON lexer cannot construct NaN. Keep manually hostile
			// input from reaching CVar and WebUI boundaries if that ever changes.
			return false;
		}
		std::ostringstream formatted;
		formatted.imbue( std::locale::classic() );
		formatted << std::fixed << std::setprecision( 6 ) << value.real;
		if ( !formatted ) {
			return false;
		}
		output = formatted.str();
		return true;
	}
	case JsonKind::Null:
	case JsonKind::Array:
	case JsonKind::Object:
		return false;
	}
	return false;
}

[[nodiscard]] bool ExtractDefinition( const JsonValue &object,
	std::string_view sourcePath, Definition &definition, AppendResult &result ) {
	if ( object.kind != JsonKind::Object ) {
		AddDiagnostic( result, DiagnosticLevel::Error,
			DiagnosticCode::InvalidDefinition, sourcePath, object.byteOffset,
			"factory array elements must be JSON objects" );
		return false;
	}

	const JsonValue *id = FindLastMember( object, "id" );
	const JsonValue *basegt = FindLastMember( object, "basegt" );
	const JsonValue *title = FindLastMember( object, "title" );
	const JsonValue *cvars = FindLastMember( object, "cvars" );
	if ( !id || id->kind != JsonKind::String ) {
		AddDiagnostic( result, DiagnosticLevel::Error,
			DiagnosticCode::InvalidDefinition, sourcePath,
			id ? id->byteOffset : object.byteOffset,
			"factory requires an exact string 'id' field" );
		return false;
	}
	if ( !basegt || basegt->kind != JsonKind::String ) {
		AddDiagnostic( result, DiagnosticLevel::Error,
			DiagnosticCode::InvalidDefinition, sourcePath,
			basegt ? basegt->byteOffset : object.byteOffset,
			"factory requires an exact string 'basegt' field" );
		return false;
	}
	if ( !title || title->kind != JsonKind::String ) {
		AddDiagnostic( result, DiagnosticLevel::Error,
			DiagnosticCode::InvalidDefinition, sourcePath,
			title ? title->byteOffset : object.byteOffset,
			"factory requires an exact string 'title' field" );
		return false;
	}
	if ( !cvars || cvars->kind != JsonKind::Object ) {
		AddDiagnostic( result, DiagnosticLevel::Error,
			DiagnosticCode::InvalidDefinition, sourcePath,
			cvars ? cvars->byteOffset : object.byteOffset,
			"factory requires an exact object 'cvars' field" );
		return false;
	}

	const std::string baseGametypeText = CStringPrefix( basegt->text );
	const std::optional<BaseGametype> parsedGametype = ParseBaseGametype(
		baseGametypeText );
	if ( !parsedGametype ) {
		AddDiagnostic( result, DiagnosticLevel::Error,
			DiagnosticCode::InvalidBaseGametype, sourcePath, basegt->byteOffset,
			"factory 'basegt' is not one of the retail case-sensitive tokens" );
		return false;
	}

	definition.id = CStringPrefix( id->text );
	definition.title = CStringPrefix( title->text );
	definition.baseGametype = *parsedGametype;
	definition.sourcePath.assign( sourcePath.data(), sourcePath.size() );

	const JsonValue *author = FindLastMember( object, "author" );
	if ( author ) {
		if ( author->kind == JsonKind::String ) {
			definition.author = CStringPrefix( author->text );
		} else {
			AddDiagnostic( result, DiagnosticLevel::Warning,
				DiagnosticCode::IgnoredOptionalField, sourcePath, author->byteOffset,
				"ignored non-string optional factory 'author' field" );
		}
	}
	const JsonValue *description = FindLastMember( object, "description" );
	if ( description ) {
		if ( description->kind == JsonKind::String ) {
			definition.description = CStringPrefix( description->text );
		} else {
			AddDiagnostic( result, DiagnosticLevel::Warning,
				DiagnosticCode::IgnoredOptionalField, sourcePath,
				description->byteOffset,
				"ignored non-string optional factory 'description' field" );
		}
	}

	const JsonValue *tags = FindLastMember( object, "tags" );
	if ( tags ) {
		if ( tags->kind == JsonKind::Array ) {
			bool warnedAboutType = false;
			const std::size_t tagCount = ( std::min )(
				tags->children.size(), MaximumTags );
			for ( std::size_t index = 0; index < tagCount; ++index ) {
				const JsonValue &tag = tags->children[index];
				if ( tag.kind != JsonKind::String ) {
					if ( !warnedAboutType ) {
						AddDiagnostic( result, DiagnosticLevel::Warning,
							DiagnosticCode::IgnoredOptionalField, sourcePath,
							tag.byteOffset,
							"ignored non-string factory tag" );
						warnedAboutType = true;
					}
					continue;
				}
				definition.tags.push_back( CStringPrefix( tag.text ) );
			}
		} else {
			AddDiagnostic( result, DiagnosticLevel::Warning,
				DiagnosticCode::IgnoredOptionalField, sourcePath, tags->byteOffset,
				"ignored non-array optional factory 'tags' field" );
		}
	}

	const std::vector<std::size_t> settingIndexes =
		JsonCppObjectMemberIndexes( *cvars );
	if ( settingIndexes.size() > MaximumSettings ) {
		const JsonValue &firstExcluded = cvars->children[
			settingIndexes[MaximumSettings]];
		AddDiagnostic( result, DiagnosticLevel::Warning,
			DiagnosticCode::TooManySettings, sourcePath, firstExcluded.byteOffset,
			"factory settings after the retail 256-setting limit were ignored" );
	}
	const std::size_t settingCount = ( std::min )(
		settingIndexes.size(), MaximumSettings );
	definition.settings.reserve( settingCount );
	for ( std::size_t ordinal = 0; ordinal < settingCount; ++ordinal ) {
		const std::size_t index = settingIndexes[ordinal];
		const std::string name = CStringPrefix( cvars->keys[index] );
		const JsonValue &value = cvars->children[index];
		Setting setting;
		setting.name = name;
		std::string formatted;
		if ( !FormatSettingValue( value, formatted ) ) {
			AddDiagnostic( result, DiagnosticLevel::Warning,
				DiagnosticCode::UnsupportedSettingValue, sourcePath,
				value.byteOffset,
				"factory setting '" + name
					+ "' has no retail CVar representation and terminates application" );
			setting.hasValue = false;
		} else {
			setting.value = std::move( formatted );
		}
		definition.settings.push_back( std::move( setting ) );
	}
	return true;
}

[[nodiscard]] std::string AsciiLower( std::string_view input ) {
	std::string output( input );
	for ( char &value : output ) {
		if ( value >= 'A' && value <= 'Z' ) {
			value = static_cast<char>( value - 'A' + 'a' );
		}
	}
	return output;
}

class BoundedJsonWriter {
public:
	explicit BoundedJsonWriter( std::size_t byteCap ) noexcept : byteCap_( byteCap ) {}

	[[nodiscard]] bool Append( std::string_view text ) {
		if ( failed_ || text.size() > byteCap_ || output_.size() > byteCap_ - text.size() ) {
			failed_ = true;
			return false;
		}
		output_.append( text.data(), text.size() );
		return true;
	}

	[[nodiscard]] bool Character( char value ) {
		return Append( std::string_view( &value, 1 ) );
	}

	[[nodiscard]] bool JsonString( std::string_view value ) {
		static constexpr char Hex[] = "0123456789abcdef";
		if ( !Character( '"' ) ) {
			return false;
		}
		for ( std::size_t index = 0; index < value.size(); ++index ) {
			const unsigned char byte = static_cast<unsigned char>( value[index] );
			// JSON permits U+2028/U+2029, but the Chromium generation embedded in
			// retail Awesomium parses them as JavaScript line terminators when this
			// JSON is injected as source text.
			if ( byte == 0xe2u && index + 2 < value.size() &&
				static_cast<unsigned char>( value[index + 1] ) == 0x80u &&
				( static_cast<unsigned char>( value[index + 2] ) == 0xa8u ||
					static_cast<unsigned char>( value[index + 2] ) == 0xa9u ) ) {
				if ( !Append( static_cast<unsigned char>( value[index + 2] ) == 0xa8u
					? "\\u2028" : "\\u2029" ) ) {
					return false;
				}
				index += 2;
				continue;
			}
			switch ( byte ) {
			case '"': if ( !Append( "\\\"" ) ) return false; break;
			case '\\': if ( !Append( "\\\\" ) ) return false; break;
			case '\b': if ( !Append( "\\b" ) ) return false; break;
			case '\f': if ( !Append( "\\f" ) ) return false; break;
			case '\n': if ( !Append( "\\n" ) ) return false; break;
			case '\r': if ( !Append( "\\r" ) ) return false; break;
			case '\t': if ( !Append( "\\t" ) ) return false; break;
			default:
				if ( byte < 0x20u ) {
					char escape[6] = { '\\', 'u', '0', '0',
						Hex[( byte >> 4u ) & 0x0fu], Hex[byte & 0x0fu] };
					if ( !Append( std::string_view( escape, sizeof( escape ) ) ) ) {
						return false;
					}
				} else if ( !Character( static_cast<char>( byte ) ) ) {
					return false;
				}
				break;
			}
		}
		return Character( '"' );
	}

	[[nodiscard]] bool Failed() const noexcept { return failed_; }
	[[nodiscard]] std::string Take() { return std::move( output_ ); }

private:
	std::size_t byteCap_ = 0;
	bool failed_ = false;
	std::string output_;
};

} // namespace

Catalog::Catalog( std::optional<std::size_t> reservedSlot ) noexcept
	: reservedSlot_( reservedSlot && *reservedSlot < MaximumDefinitions
		? reservedSlot : std::nullopt ) {}

std::optional<BaseGametype> ParseBaseGametype( std::string_view token ) noexcept {
	static constexpr std::array<std::string_view, 13> Tokens{
		"ffa", "duel", "race", "tdm", "ca", "ctf", "oneflag",
		"overload", "har", "ft", "dom", "ad", "rr"
	};
	for ( std::size_t index = 0; index < Tokens.size(); ++index ) {
		if ( Tokens[index] == token ) {
			return static_cast<BaseGametype>( index );
		}
	}
	return std::nullopt;
}

std::string_view BaseGametypeToken( BaseGametype gametype ) noexcept {
	static constexpr std::array<std::string_view, 13> Tokens{
		"ffa", "duel", "race", "tdm", "ca", "ctf", "oneflag",
		"overload", "har", "ft", "dom", "ad", "rr"
	};
	const int index = static_cast<int>( gametype );
	return index >= 0 && static_cast<std::size_t>( index ) < Tokens.size()
		? Tokens[static_cast<std::size_t>( index )] : std::string_view{};
}

AppendResult Catalog::AppendDocument( std::string_view sourcePath,
	std::string_view document ) {
	AppendResult result;
	const std::size_t nul = document.find( '\0' );
	if ( nul != std::string_view::npos ) {
		document = document.substr( 0, nul );
	}
	JsonValue root;
	JsonParser parser( document );
	if ( !parser.Parse( root ) ) {
		AddDiagnostic( result, DiagnosticLevel::Error, DiagnosticCode::InvalidJson,
			sourcePath, parser.ErrorOffset(), parser.Error() );
		return result;
	}

	std::vector<const JsonValue *> objects;
	const bool arrayRoot = root.kind == JsonKind::Array;
	if ( root.kind == JsonKind::Object ) {
		objects.push_back( &root );
	} else if ( root.kind == JsonKind::Array ) {
		objects.reserve( root.children.size() );
		for ( const JsonValue &child : root.children ) {
			objects.push_back( &child );
		}
	} else {
		AddDiagnostic( result, DiagnosticLevel::Error, DiagnosticCode::InvalidRoot,
			sourcePath, root.byteOffset,
			"factory document root must be a JSON object or array" );
		return result;
	}

	const std::size_t loadCapacity = MaximumDefinitions
		- ( reservedSlot_ ? 1u : 0u );
	const std::size_t loadedDefinitions = definitions_.size()
		- ( retainedInserted_ ? 1u : 0u );
	const std::size_t availableDefinitions = loadedDefinitions < loadCapacity
		? loadCapacity - loadedDefinitions : 0u;
	std::vector<Definition> pending;
	pending.reserve( ( std::min )( objects.size(),
		availableDefinitions ) );
	for ( const JsonValue *object : objects ) {
		Definition definition;
		if ( !ExtractDefinition( *object, sourcePath, definition, result ) ) {
			if ( !arrayRoot ) {
				return result;
			}
			continue;
		}
		if ( pending.size() == availableDefinitions ) {
			AddDiagnostic( result, DiagnosticLevel::Error,
				DiagnosticCode::TooManyDefinitions, sourcePath, object->byteOffset,
				"factory definition exceeds the retail 1024-definition limit" );
			if ( !arrayRoot ) {
				return result;
			}
			continue;
		}
		pending.push_back( std::move( definition ) );
	}

	for ( std::size_t index = 0; index < pending.size(); ++index ) {
		const std::string &id = pending[index].id;
		bool duplicate = FindFirst( id ) != nullptr;
		if ( !duplicate ) {
			for ( std::size_t prior = 0; prior < index; ++prior ) {
				if ( pending[prior].id == id ) {
					duplicate = true;
					break;
				}
			}
		}
		if ( duplicate ) {
			AddDiagnostic( result, DiagnosticLevel::Warning,
				DiagnosticCode::DuplicateId, sourcePath, 0,
				"factory with id '" + id
					+ "' already exists; retaining the duplicate in retail order" );
		}
	}

	result.definitionsAdded = pending.size();
	for ( std::size_t index = 0; index < pending.size(); ++index ) {
		Definition &definition = pending[index];
		definition.registrySlot = loadedDefinitions + index;
		if ( reservedSlot_ && definition.registrySlot >= *reservedSlot_ ) {
			++definition.registrySlot;
		}
		const auto position = std::lower_bound( definitions_.begin(), definitions_.end(),
			definition.registrySlot,
			[]( const Definition &candidate, std::size_t slot ) {
				return candidate.registrySlot < slot;
			} );
		definitions_.insert( position, std::move( definition ) );
	}
	result.success = true;
	return result;
}

void Catalog::Clear() noexcept {
	definitions_.clear();
	retainedInserted_ = false;
}

bool Catalog::InsertRetained( Definition definition ) {
	if ( !reservedSlot_ || retainedInserted_ ||
		definitions_.size() >= MaximumDefinitions ) {
		return false;
	}
	definition.registrySlot = *reservedSlot_;
	definition.enumerated = definitions_.size() > *reservedSlot_;
	const auto position = std::lower_bound( definitions_.begin(), definitions_.end(),
		definition.registrySlot,
		[]( const Definition &candidate, std::size_t slot ) {
			return candidate.registrySlot < slot;
		} );
	definitions_.insert( position, std::move( definition ) );
	retainedInserted_ = true;
	return true;
}

const Definition *Catalog::FindFirst( std::string_view id ) const noexcept {
	for ( const Definition &definition : definitions_ ) {
		if ( definition.id == id ) {
			return &definition;
		}
	}
	return nullptr;
}

SerializeResult Catalog::SerializeWebUi( std::size_t outputByteCap ) const {
	std::vector<const Definition *> visible;
	visible.reserve( definitions_.size() );
	for ( const Definition &definition : definitions_ ) {
		if ( !definition.enumerated ||
			( !definition.id.empty() && definition.id.front() == '_' ) ) {
			continue;
		}
		auto existing = std::find_if( visible.begin(), visible.end(),
			[&definition]( const Definition *candidate ) {
				return candidate->id == definition.id;
			} );
		if ( existing == visible.end() ) {
			visible.push_back( &definition );
		} else {
			*existing = &definition;
		}
	}

	BoundedJsonWriter writer( outputByteCap );
	(void)writer.Character( '{' );
	bool firstDefinition = true;
	for ( const Definition *definition : visible ) {
		if ( !firstDefinition ) {
			(void)writer.Character( ',' );
		}
		firstDefinition = false;
		(void)writer.JsonString( definition->id );
		(void)writer.Append( ":{\"id\":" );
		(void)writer.JsonString( definition->id );
		(void)writer.Append( ",\"title\":" );
		(void)writer.JsonString( definition->title );
		(void)writer.Append( ",\"basegt\":" );
		(void)writer.Append( std::to_string(
			static_cast<int>( definition->baseGametype ) ) );
		if ( definition->author ) {
			(void)writer.Append( ",\"author\":" );
			(void)writer.JsonString( *definition->author );
		}
		if ( definition->description ) {
			(void)writer.Append( ",\"description\":" );
			(void)writer.JsonString( *definition->description );
		}
		(void)writer.Append( ",\"settings\":{" );

		struct SerializedSetting {
			std::string name;
			const std::string *value = nullptr;
		};
		std::vector<SerializedSetting> settings;
		settings.reserve( definition->settings.size() );
		for ( const Setting &setting : definition->settings ) {
			if ( !setting.hasValue ) {
				break;
			}
			std::string name = AsciiLower( setting.name );
			auto existing = std::find_if( settings.begin(), settings.end(),
				[&name]( const SerializedSetting &candidate ) {
					return candidate.name == name;
				} );
			if ( existing == settings.end() ) {
				SerializedSetting serialized;
				serialized.name = std::move( name );
				serialized.value = &setting.value;
				settings.push_back( std::move( serialized ) );
			} else {
				existing->value = &setting.value;
			}
		}

		bool firstSetting = true;
		for ( const SerializedSetting &setting : settings ) {
			if ( !firstSetting ) {
				(void)writer.Character( ',' );
			}
			firstSetting = false;
			(void)writer.JsonString( setting.name );
			(void)writer.Character( ':' );
			(void)writer.JsonString( *setting.value );
		}
		(void)writer.Append( "}}" );
	}
	(void)writer.Character( '}' );

	SerializeResult result;
	if ( writer.Failed() ) {
		result.error = "factory WebUI JSON exceeds the configured output byte cap";
		return result;
	}
	result.success = true;
	result.json = writer.Take();
	return result;
}

} // namespace fnql::server::factory
