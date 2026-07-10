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
// webpak_format.h -- bounded Chromium DataPack layout inspection

// Quake Live's retail web.pak is a Chromium DataPack v4 archive.  Keep the
// byte-level parser independent from the engine allocator and filesystem so
// malformed external assets can be rejected before any table pointer is used.

#ifndef FNQL_CLIENT_WEBPAK_FORMAT_H
#define FNQL_CLIENT_WEBPAK_FORMAT_H

#include <cstddef>
#include <cstdint>
#include <limits>

namespace fnql::webui {

enum class DataPackError {
	None,
	MissingData,
	TruncatedHeader,
	UnsupportedVersion,
	InvalidResourceCount,
	TableSizeOverflow,
	TruncatedTables,
	UnsortedResourceIds,
	InvalidResourceOffset,
	UnsortedAliasIds,
	InvalidAliasTarget
};

struct DataPackLayout {
	std::uint32_t version = 0;
	std::uint32_t resourceCount = 0;
	std::uint16_t aliasCount = 0;
	std::size_t headerSize = 0;
	std::size_t entryTableOffset = 0;
	std::size_t aliasTableOffset = 0;
	std::size_t payloadOffset = 0;
	std::size_t byteLength = 0;
};

inline std::uint16_t DataPackReadU16( const std::uint8_t *cursor ) noexcept {
	return static_cast<std::uint16_t>( cursor[0] )
		| static_cast<std::uint16_t>( static_cast<std::uint16_t>( cursor[1] ) << 8 );
}

inline std::uint32_t DataPackReadU32( const std::uint8_t *cursor ) noexcept {
	return static_cast<std::uint32_t>( cursor[0] )
		| ( static_cast<std::uint32_t>( cursor[1] ) << 8 )
		| ( static_cast<std::uint32_t>( cursor[2] ) << 16 )
		| ( static_cast<std::uint32_t>( cursor[3] ) << 24 );
}

inline bool DataPackCheckedAdd( std::size_t lhs, std::size_t rhs, std::size_t *result ) noexcept {
	if ( !result || rhs > ( std::numeric_limits<std::size_t>::max )() - lhs ) {
		return false;
	}

	*result = lhs + rhs;
	return true;
}

inline bool DataPackCheckedMultiply( std::size_t lhs, std::size_t rhs, std::size_t *result ) noexcept {
	if ( !result || ( lhs != 0 && rhs > ( std::numeric_limits<std::size_t>::max )() / lhs ) ) {
		return false;
	}

	*result = lhs * rhs;
	return true;
}

inline void DataPackSetError( DataPackError *error, DataPackError value ) noexcept {
	if ( error ) {
		*error = value;
	}
}

inline bool InspectDataPack( const std::uint8_t *bytes, std::size_t byteLength,
	DataPackLayout *layout, DataPackError *error = nullptr ) noexcept {
	DataPackLayout parsed;
	std::size_t entryTableBytes;
	std::size_t aliasTableBytes;
	std::size_t entryCount;

	DataPackSetError( error, DataPackError::None );
	if ( layout ) {
		*layout = {};
	}
	if ( !bytes || !layout ) {
		DataPackSetError( error, DataPackError::MissingData );
		return false;
	}
	if ( byteLength < sizeof( std::uint32_t ) ) {
		DataPackSetError( error, DataPackError::TruncatedHeader );
		return false;
	}

	parsed.version = DataPackReadU32( bytes );
	if ( parsed.version == 4u ) {
		// v4: version (u32), resource count (u32), text encoding (u8).
		parsed.headerSize = 9;
		if ( byteLength < parsed.headerSize ) {
			DataPackSetError( error, DataPackError::TruncatedHeader );
			return false;
		}
		parsed.resourceCount = DataPackReadU32( bytes + 4 );
	} else if ( parsed.version == 5u ) {
		// v5: version/encoding/padding followed by u16 resource and alias counts.
		parsed.headerSize = 12;
		if ( byteLength < parsed.headerSize ) {
			DataPackSetError( error, DataPackError::TruncatedHeader );
			return false;
		}
		parsed.resourceCount = DataPackReadU16( bytes + 8 );
		parsed.aliasCount = DataPackReadU16( bytes + 10 );
	} else {
		DataPackSetError( error, DataPackError::UnsupportedVersion );
		return false;
	}

	// Resource identifiers and alias targets are 16-bit in both layouts.
	if ( parsed.resourceCount == 0 || parsed.resourceCount > ( std::numeric_limits<std::uint16_t>::max )() ) {
		DataPackSetError( error, DataPackError::InvalidResourceCount );
		return false;
	}

	entryCount = static_cast<std::size_t>( parsed.resourceCount ) + 1u; // trailing offset sentinel
	if ( !DataPackCheckedMultiply( entryCount, 6u, &entryTableBytes )
		|| !DataPackCheckedMultiply( parsed.aliasCount, 4u, &aliasTableBytes ) ) {
		DataPackSetError( error, DataPackError::TableSizeOverflow );
		return false;
	}

	parsed.entryTableOffset = parsed.headerSize;
	if ( !DataPackCheckedAdd( parsed.entryTableOffset, entryTableBytes, &parsed.aliasTableOffset )
		|| !DataPackCheckedAdd( parsed.aliasTableOffset, aliasTableBytes, &parsed.payloadOffset ) ) {
		DataPackSetError( error, DataPackError::TableSizeOverflow );
		return false;
	}
	if ( parsed.payloadOffset > byteLength ) {
		DataPackSetError( error, DataPackError::TruncatedTables );
		return false;
	}

	std::uint16_t previousResourceId = 0;
	std::uint32_t previousOffset = 0;
	for ( std::size_t index = 0; index < entryCount; ++index ) {
		const std::uint8_t *entry = bytes + parsed.entryTableOffset + index * 6u;
		const std::uint16_t resourceId = DataPackReadU16( entry );
		const std::uint32_t offset = DataPackReadU32( entry + 2 );

		// The final entry is an offset sentinel whose identifier is not ordered.
		if ( index < parsed.resourceCount ) {
			if ( index > 0 && resourceId <= previousResourceId ) {
				DataPackSetError( error, DataPackError::UnsortedResourceIds );
				return false;
			}
			previousResourceId = resourceId;
		}

		if ( offset > byteLength
			|| ( index == 0 && offset < parsed.payloadOffset )
			|| ( index > 0 && offset < previousOffset ) ) {
			DataPackSetError( error, DataPackError::InvalidResourceOffset );
			return false;
		}
		previousOffset = offset;
	}

	std::uint16_t previousAliasId = 0;
	for ( std::size_t index = 0; index < parsed.aliasCount; ++index ) {
		const std::uint8_t *alias = bytes + parsed.aliasTableOffset + index * 4u;
		const std::uint16_t aliasId = DataPackReadU16( alias );
		const std::uint16_t targetIndex = DataPackReadU16( alias + 2 );

		if ( index > 0 && aliasId <= previousAliasId ) {
			DataPackSetError( error, DataPackError::UnsortedAliasIds );
			return false;
		}
		if ( targetIndex >= parsed.resourceCount ) {
			DataPackSetError( error, DataPackError::InvalidAliasTarget );
			return false;
		}
		previousAliasId = aliasId;
	}

	parsed.byteLength = byteLength;
	*layout = parsed;
	return true;
}

inline bool DataPackResourceBounds( const std::uint8_t *bytes, const DataPackLayout &layout,
	std::size_t entryIndex, std::size_t *offset, std::size_t *length ) noexcept {
	std::size_t entryOffset;
	std::size_t entryBytes;
	std::size_t tableEnd;

	if ( offset ) {
		*offset = 0;
	}
	if ( length ) {
		*length = 0;
	}
	if ( !bytes || !offset || !length || entryIndex >= layout.resourceCount
		|| layout.entryTableOffset < layout.headerSize
		|| layout.payloadOffset > layout.byteLength
		|| !DataPackCheckedMultiply( static_cast<std::size_t>( layout.resourceCount ) + 1u,
			6u, &entryBytes )
		|| !DataPackCheckedAdd( layout.entryTableOffset, entryBytes, &tableEnd )
		|| tableEnd > layout.payloadOffset
		|| !DataPackCheckedMultiply( entryIndex, 6u, &entryOffset )
		|| !DataPackCheckedAdd( layout.entryTableOffset, entryOffset, &entryOffset )
		|| entryOffset > layout.byteLength || layout.byteLength - entryOffset < 12u ) {
		return false;
	}

	const std::uint8_t *entry = bytes + entryOffset;
	const std::uint8_t *nextEntry = entry + 6u;
	const std::size_t start = DataPackReadU32( entry + 2 );
	const std::size_t end = DataPackReadU32( nextEntry + 2 );
	if ( start > end || end > layout.byteLength ) {
		return false;
	}

	*offset = start;
	*length = end - start;
	return true;
}

inline const char *DataPackErrorString( DataPackError error ) noexcept {
	switch ( error ) {
		case DataPackError::None: return "no error";
		case DataPackError::MissingData: return "missing parser input";
		case DataPackError::TruncatedHeader: return "truncated DataPack header";
		case DataPackError::UnsupportedVersion: return "unsupported DataPack version";
		case DataPackError::InvalidResourceCount: return "invalid DataPack resource count";
		case DataPackError::TableSizeOverflow: return "DataPack table size overflow";
		case DataPackError::TruncatedTables: return "truncated DataPack tables";
		case DataPackError::UnsortedResourceIds: return "unsorted DataPack resource identifiers";
		case DataPackError::InvalidResourceOffset: return "invalid DataPack resource offset";
		case DataPackError::UnsortedAliasIds: return "unsorted DataPack alias identifiers";
		case DataPackError::InvalidAliasTarget: return "invalid DataPack alias target";
	}

	return "unknown DataPack error";
}

} // namespace fnql::webui

#endif // FNQL_CLIENT_WEBPAK_FORMAT_H
