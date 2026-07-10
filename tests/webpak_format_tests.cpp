#include "webpak_format.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

namespace {

using fnql::webui::DataPackError;
using fnql::webui::DataPackLayout;

void WriteU16( std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint16_t value ) {
	bytes[offset] = static_cast<std::uint8_t>( value );
	bytes[offset + 1] = static_cast<std::uint8_t>( value >> 8 );
}

void WriteU32( std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value ) {
	bytes[offset] = static_cast<std::uint8_t>( value );
	bytes[offset + 1] = static_cast<std::uint8_t>( value >> 8 );
	bytes[offset + 2] = static_cast<std::uint8_t>( value >> 16 );
	bytes[offset + 3] = static_cast<std::uint8_t>( value >> 24 );
}

void WriteEntry( std::vector<std::uint8_t> &bytes, std::size_t offset,
	std::uint16_t resourceId, std::uint32_t payloadOffset ) {
	WriteU16( bytes, offset, resourceId );
	WriteU32( bytes, offset + 2, payloadOffset );
}

std::vector<std::uint8_t> MakeV4Pack() {
	// Header (9) + three entries (18) + two four-byte resources.
	std::vector<std::uint8_t> bytes( 35, 0 );
	WriteU32( bytes, 0, 4 );
	WriteU32( bytes, 4, 2 );
	WriteEntry( bytes, 9, 0, 27 );
	WriteEntry( bytes, 15, 1, 31 );
	WriteEntry( bytes, 21, 0, 35 );
	return bytes;
}

std::vector<std::uint8_t> MakeV5Pack() {
	// Header (12) + three entries (18) + one alias (4) + payload (8).
	std::vector<std::uint8_t> bytes( 42, 0 );
	WriteU32( bytes, 0, 5 );
	WriteU16( bytes, 8, 2 );
	WriteU16( bytes, 10, 1 );
	WriteEntry( bytes, 12, 1, 34 );
	WriteEntry( bytes, 18, 2, 38 );
	WriteEntry( bytes, 24, 0, 42 );
	WriteU16( bytes, 30, 7 );
	WriteU16( bytes, 32, 1 );
	return bytes;
}

#define CHECK( expression ) \
	do { \
		if ( !( expression ) ) { \
			std::cerr << __func__ << ':' << __LINE__ << ": check failed: " #expression "\n"; \
			return false; \
		} \
	} while ( false )

bool AcceptsRetailV4Shape() {
	auto bytes = MakeV4Pack();
	DataPackLayout layout;
	DataPackError error = DataPackError::MissingData;
	CHECK( fnql::webui::InspectDataPack( bytes.data(), bytes.size(), &layout, &error ) );
	CHECK( error == DataPackError::None );
	CHECK( layout.version == 4 );
	CHECK( layout.resourceCount == 2 );
	CHECK( layout.aliasCount == 0 );
	CHECK( layout.payloadOffset == 27 );

	std::size_t offset = 0;
	std::size_t length = 0;
	CHECK( fnql::webui::DataPackResourceBounds( bytes.data(), layout, 1, &offset, &length ) );
	CHECK( offset == 31 );
	CHECK( length == 4 );
	CHECK( !fnql::webui::DataPackResourceBounds( bytes.data(), layout, 2, &offset, &length ) );
	return true;
}

bool AcceptsV5AliasShape() {
	auto bytes = MakeV5Pack();
	DataPackLayout layout;
	CHECK( fnql::webui::InspectDataPack( bytes.data(), bytes.size(), &layout ) );
	CHECK( layout.version == 5 );
	CHECK( layout.resourceCount == 2 );
	CHECK( layout.aliasCount == 1 );
	CHECK( layout.aliasTableOffset == 30 );
	CHECK( layout.payloadOffset == 34 );
	return true;
}

bool RejectsTruncatedHeadersAndTables() {
	DataPackLayout layout{ 99, 99, 99, 99, 99, 99, 99, 99 };
	DataPackError error = DataPackError::None;
	auto bytes = MakeV4Pack();
	bytes.resize( 8 );
	CHECK( !fnql::webui::InspectDataPack( bytes.data(), bytes.size(), &layout, &error ) );
	CHECK( error == DataPackError::TruncatedHeader );
	CHECK( layout.version == 0 && layout.byteLength == 0 );

	bytes = MakeV4Pack();
	bytes.resize( 20 );
	CHECK( !fnql::webui::InspectDataPack( bytes.data(), bytes.size(), &layout, &error ) );
	CHECK( error == DataPackError::TruncatedTables );
	return true;
}

bool RejectsForgedResourceLayouts() {
	auto bytes = MakeV4Pack();
	DataPackLayout layout;
	CHECK( fnql::webui::InspectDataPack( bytes.data(), bytes.size(), &layout ) );

	std::size_t offset = 1;
	std::size_t length = 1;
	layout.entryTableOffset = layout.byteLength;
	CHECK( !fnql::webui::DataPackResourceBounds( bytes.data(), layout, 0, &offset, &length ) );
	CHECK( offset == 0 && length == 0 );

	CHECK( fnql::webui::InspectDataPack( bytes.data(), bytes.size(), &layout ) );
	layout.payloadOffset = layout.entryTableOffset;
	CHECK( !fnql::webui::DataPackResourceBounds( bytes.data(), layout, 0, &offset, &length ) );
	return true;
}

bool RejectsUnsafeResourceTables() {
	DataPackLayout layout;
	DataPackError error = DataPackError::None;
	auto bytes = MakeV4Pack();
	WriteU16( bytes, 15, 0 );
	CHECK( !fnql::webui::InspectDataPack( bytes.data(), bytes.size(), &layout, &error ) );
	CHECK( error == DataPackError::UnsortedResourceIds );

	bytes = MakeV4Pack();
	WriteU32( bytes, 11, 26 );
	CHECK( !fnql::webui::InspectDataPack( bytes.data(), bytes.size(), &layout, &error ) );
	CHECK( error == DataPackError::InvalidResourceOffset );

	bytes = MakeV4Pack();
	WriteU32( bytes, 17, 26 );
	CHECK( !fnql::webui::InspectDataPack( bytes.data(), bytes.size(), &layout, &error ) );
	CHECK( error == DataPackError::InvalidResourceOffset );
	return true;
}

bool RejectsUnsafeAliasTables() {
	DataPackLayout layout;
	DataPackError error = DataPackError::None;
	auto bytes = MakeV5Pack();
	WriteU16( bytes, 32, 2 );
	CHECK( !fnql::webui::InspectDataPack( bytes.data(), bytes.size(), &layout, &error ) );
	CHECK( error == DataPackError::InvalidAliasTarget );

	// Expand the valid v5 shape to two descending alias identifiers.
	bytes.assign( 46, 0 );
	WriteU32( bytes, 0, 5 );
	WriteU16( bytes, 8, 2 );
	WriteU16( bytes, 10, 2 );
	WriteEntry( bytes, 12, 1, 38 );
	WriteEntry( bytes, 18, 2, 42 );
	WriteEntry( bytes, 24, 0, 46 );
	WriteU16( bytes, 30, 8 );
	WriteU16( bytes, 32, 0 );
	WriteU16( bytes, 34, 7 );
	WriteU16( bytes, 36, 1 );
	CHECK( !fnql::webui::InspectDataPack( bytes.data(), bytes.size(), &layout, &error ) );
	CHECK( error == DataPackError::UnsortedAliasIds );
	return true;
}

bool ProbeExternalDataPack( const char *path ) {
	std::ifstream stream( path, std::ios::binary | std::ios::ate );
	CHECK( stream );

	const std::streamoff length = stream.tellg();
	CHECK( length > 0 );
	CHECK( static_cast<std::uintmax_t>( length ) <= ( std::numeric_limits<std::size_t>::max )() );

	std::vector<std::uint8_t> bytes( static_cast<std::size_t>( length ) );
	stream.seekg( 0, std::ios::beg );
	CHECK( stream.read( reinterpret_cast<char *>( bytes.data() ), length ) );

	DataPackLayout layout;
	DataPackError error = DataPackError::None;
	if ( !fnql::webui::InspectDataPack( bytes.data(), bytes.size(), &layout, &error ) ) {
		std::cerr << path << ": " << fnql::webui::DataPackErrorString( error ) << '\n';
		return false;
	}

	std::cout << path << ": DataPack v" << layout.version
		<< ", " << layout.resourceCount << " resources, "
		<< layout.aliasCount << " aliases\n";
	return true;
}

} // namespace

int main( int argc, char **argv ) {
	if ( argc > 2 ) {
		std::cerr << "usage: fnql_webpak_format_tests [external-web.pak]\n";
		return 2;
	}

	bool passed = AcceptsRetailV4Shape()
		&& AcceptsV5AliasShape()
		&& RejectsTruncatedHeadersAndTables()
		&& RejectsForgedResourceLayouts()
		&& RejectsUnsafeResourceTables()
		&& RejectsUnsafeAliasTables();
	if ( passed && argc == 2 ) {
		passed = ProbeExternalDataPack( argv[1] );
	}
	return passed ? 0 : 1;
}
