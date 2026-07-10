#include "../code/qcommon/q_shared.h"
#include "../code/qcommon/qfiles.h"
#include "../code/qcommon/ql_bsp.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

#define CHECK( condition ) do { \
	if ( !( condition ) ) { \
		std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__ << ": " #condition "\n"; \
		std::exit( 1 ); \
	} \
} while ( false )

void WriteLE32( unsigned char *destination, std::uint32_t value ) {
	destination[0] = static_cast<unsigned char>( value );
	destination[1] = static_cast<unsigned char>( value >> 8 );
	destination[2] = static_cast<unsigned char>( value >> 16 );
	destination[3] = static_cast<unsigned char>( value >> 24 );
}

void TestAdvertisementLumpBounds() {
	const std::size_t payloadOffset = sizeof( dheader_t ) + sizeof( lump_t );
	std::vector<unsigned char> file( payloadOffset + sizeof( qlBspAdvertisementDisk_t ), 0 );
	unsigned char *extra = file.data() + sizeof( dheader_t );
	lump_t lump{};

	WriteLE32( extra + offsetof( lump_t, fileofs ), static_cast<std::uint32_t>( payloadOffset ) );
	WriteLE32( extra + offsetof( lump_t, filelen ), sizeof( qlBspAdvertisementDisk_t ) );
	CHECK( QLBSP_ReadAdvertisementLump( file.data(), file.size(), &lump ) == QL_BSP_LUMP_OK );
	CHECK( lump.fileofs == static_cast<int>( payloadOffset ) );
	CHECK( lump.filelen == static_cast<int>( sizeof( qlBspAdvertisementDisk_t ) ) );
	CHECK( QLBSP_AdvertisementLumpShapeValid( &lump ) == qtrue );
	lump.filelen--;
	CHECK( QLBSP_AdvertisementLumpShapeValid( &lump ) == qfalse );
	lump.filelen++;

	CHECK( QLBSP_ReadAdvertisementLump( file.data(), sizeof( dheader_t ), &lump ) ==
		QL_BSP_LUMP_TRUNCATED_HEADER );

	WriteLE32( extra + offsetof( lump_t, fileofs ), 0xffffffffu );
	CHECK( QLBSP_ReadAdvertisementLump( file.data(), file.size(), &lump ) == QL_BSP_LUMP_INVALID_RANGE );

	WriteLE32( extra + offsetof( lump_t, fileofs ), static_cast<std::uint32_t>( file.size() ) );
	WriteLE32( extra + offsetof( lump_t, filelen ), 1u );
	CHECK( QLBSP_ReadAdvertisementLump( file.data(), file.size(), &lump ) == QL_BSP_LUMP_INVALID_RANGE );

	WriteLE32( extra + offsetof( lump_t, filelen ), 0u );
	CHECK( QLBSP_ReadAdvertisementLump( file.data(), file.size(), &lump ) == QL_BSP_LUMP_OK );
}

void TestHostLumpBounds() {
	lump_t lump{};
	lump.fileofs = 32;
	lump.filelen = 64;
	CHECK( QLBSP_LumpRangeValid( &lump, 96 ) == qtrue );

	lump.fileofs = -1;
	CHECK( QLBSP_LumpRangeValid( &lump, 96 ) == qfalse );
	lump.fileofs = 32;
	lump.filelen = -1;
	CHECK( QLBSP_LumpRangeValid( &lump, 96 ) == qfalse );
	lump.fileofs = INT_MAX;
	lump.filelen = INT_MAX;
	CHECK( QLBSP_LumpRangeValid( &lump, 96 ) == qfalse );
}

void TestBoundedModelParsing() {
	std::array<char, MAX_QPATH> model{};
	int index = -1;

	std::memcpy( model.data(), "*12", 4 );
	CHECK( QLBSP_ParseAdvertisementModel( model.data(), &index ) == qtrue );
	CHECK( index == 12 );

	model.fill( '0' );
	model[0] = '*';
	model.back() = '7'; // deliberately no NUL in the fixed-width disk field
	CHECK( QLBSP_ParseAdvertisementModel( model.data(), &index ) == qtrue );
	CHECK( index == 7 );

	model.fill( '\0' );
	std::memcpy( model.data(), "*-1", 4 );
	CHECK( QLBSP_ParseAdvertisementModel( model.data(), &index ) == qfalse );
	CHECK( index == -1 );

	std::memcpy( model.data(), "*2147483648", 12 );
	CHECK( QLBSP_ParseAdvertisementModel( model.data(), &index ) == qfalse );

	model.fill( '\0' );
	model[0] = '*';
	CHECK( QLBSP_ParseAdvertisementModel( model.data(), &index ) == qfalse );
}

void TestFiniteGeometryValidation() {
	qlBspAdvertisementDisk_t record{};
	CHECK( QLBSP_AdvertisementGeometryFinite( &record ) == qtrue );

	const std::uint32_t infinity = 0x7f800000u;
	WriteLE32( reinterpret_cast<unsigned char *>( &record.normal[1] ), infinity );
	CHECK( QLBSP_AdvertisementGeometryFinite( &record ) == qfalse );

	record = {};
	const std::uint32_t quietNan = 0x7fc00000u;
	WriteLE32( reinterpret_cast<unsigned char *>( &record.points[3][2] ), quietNan );
	CHECK( QLBSP_AdvertisementGeometryFinite( &record ) == qfalse );
}

} // namespace

int main() {
	static_assert( BSP_VERSION_QL == 47, "retail Quake Live BSP version changed" );
	static_assert( sizeof( qlBspAdvertisementDisk_t ) == 128, "advertisement disk ABI changed" );
	TestAdvertisementLumpBounds();
	TestHostLumpBounds();
	TestBoundedModelParsing();
	TestFiniteGeometryValidation();
	std::cout << "QL BSP contract tests passed\n";
	return 0;
}
