#include "../code/qcommon/cm_model_handles.h"
#include "../code/server/sv_collision_model.hpp"

#include <cstdlib>
#include <iostream>

namespace {

int failures;

void Check( bool condition, const char *expression, int line )
{
	if ( condition ) return;
	std::cerr << "line " << line << ": check failed: " << expression << '\n';
	++failures;
}

#define CHECK(expression) Check( ( expression ), #expression, __LINE__ )

void TestReservedHandleContract()
{
	static_assert( MAX_SUBMODELS == 256 );
	static_assert( CAPSULE_MODEL_HANDLE == 254 );
	static_assert( BOX_MODEL_HANDLE == 255 );

	CHECK( CM_TemporaryModelHandle( qfalse ) == BOX_MODEL_HANDLE );
	CHECK( CM_TemporaryModelHandle( qtrue ) == CAPSULE_MODEL_HANDLE );
	CHECK( CM_IsTemporaryModelHandle( BOX_MODEL_HANDLE ) == qtrue );
	CHECK( CM_IsTemporaryModelHandle( CAPSULE_MODEL_HANDLE ) == qtrue );
	CHECK( CM_IsTemporaryModelHandle( 0 ) == qfalse );
	CHECK( CM_IsTemporaryModelHandle( 13 ) == qfalse );
	CHECK( CM_IsTemporaryModelHandle( 14 ) == qfalse );
	CHECK( CM_IsTemporaryModelHandle( CAPSULE_MODEL_HANDLE - 1 ) == qfalse );
	CHECK( CM_IsTemporaryModelHandle( MAX_SUBMODELS ) == qfalse );
	CHECK( CM_IsTemporaryModelHandle( -1 ) == qfalse );
}

void TestRetailEntityShapeSelection()
{
	using fnql::server::collision::UseCapsuleEntityModel;

	// Shotgun and other G_TRACE hitscan paths keep the target box.
	CHECK( !UseCapsuleEntityModel( false, true ) );
	CHECK( !UseCapsuleEntityModel( false, false ) );
	// Capsule-vs-capsule math is selected only when both sides request it.
	CHECK( UseCapsuleEntityModel( true, true ) );
	CHECK( !UseCapsuleEntityModel( true, false ) );
}

} // namespace

int main()
{
	TestReservedHandleContract();
	TestRetailEntityShapeSelection();
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
