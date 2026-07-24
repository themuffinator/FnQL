#include "../code/qcommon/cm_model_handles.h"
#include "../code/qcommon/cm_trace_contract.h"
#include "../code/server/sv_collision_model.hpp"

#include <cmath>
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

	// Ordinary point/box traces keep the target box.
	CHECK( !UseCapsuleEntityModel( false, true ) );
	CHECK( !UseCapsuleEntityModel( false, false ) );
	// Capsule-vs-capsule math is selected only when both sides request it.
	CHECK( UseCapsuleEntityModel( true, true ) );
	CHECK( !UseCapsuleEntityModel( true, false ) );
}

void TestRetailCapsuleProfile()
{
	const vec3_t mins = { -15.0f, -15.0f, -24.0f };
	const vec3_t maxs = { 15.0f, 15.0f, 32.0f };
	cm_retailCapsuleProfile_t profile{};

	static_assert( CONTENTS_HEAD == 0x0400 );
	static_assert( CM_RETAIL_CAPSULE_HEAD_RADIUS_SCALE == 0.7f );

	CM_BuildRetailCapsuleProfile( mins, maxs, 15.0f, &profile );

	CHECK( profile.bodyOrigin[0] == 0.0f );
	CHECK( profile.bodyOrigin[1] == 0.0f );
	CHECK( profile.bodyOrigin[2] == 4.0f );
	CHECK( profile.bodyRadius == 30.0f );
	CHECK( profile.bodyHalfheight == 28.0f );
	CHECK( profile.headOrigin[0] == 0.0f );
	CHECK( profile.headOrigin[1] == 0.0f );
	CHECK( profile.headOrigin[2] == 32.0f );
	CHECK( std::fabs( profile.headRadius - 21.0f ) < 0.0001f );

	CHECK( CM_RetailCapsuleHitContents(
		CONTENTS_BODY, 0.5f, 0.25f ) == ( CONTENTS_BODY | CONTENTS_HEAD ) );
	CHECK( CM_RetailCapsuleHitContents(
		CONTENTS_BODY, 0.25f, 0.5f ) == CONTENTS_BODY );
	CHECK( CM_RetailCapsuleHitContents(
		CONTENTS_BODY, 0.5f, 0.5f ) == CONTENTS_BODY );
}

void TestTracePlaneContract()
{
	trace_t trace{};

	trace.fraction = 1.0f;
	CHECK( CM_TraceResultHasValidPlaneContract( &trace ) );

	// Retail can return no plane for an overlap at the starting point.
	trace.fraction = 0.0f;
	trace.startsolid = qtrue;
	CHECK( CM_TraceResultHasValidPlaneContract( &trace ) );

	// Its one-unit analytic epsilon can also produce a sub-unit start plane.
	trace.startsolid = qfalse;
	trace.plane.normal[0] = 0.95f;
	CHECK( CM_TraceResultHasValidPlaneContract( &trace ) );

	trace.fraction = 0.5f;
	CHECK( !CM_TraceResultHasValidPlaneContract( &trace ) );
	VectorSet( trace.plane.normal, 1.0f, 0.0f, 0.0f );
	CHECK( CM_TraceResultHasValidPlaneContract( &trace ) );

	trace.plane.normal[0] = 1.1f;
	CHECK( !CM_TraceResultHasValidPlaneContract( &trace ) );
	trace.fraction = NAN;
	CHECK( !CM_TraceResultHasValidPlaneContract( &trace ) );
	trace.fraction = 1.01f;
	CHECK( !CM_TraceResultHasValidPlaneContract( &trace ) );
	CHECK( !CM_TraceResultHasValidPlaneContract( nullptr ) );
}

} // namespace

int main()
{
	TestReservedHandleContract();
	TestRetailEntityShapeSelection();
	TestRetailCapsuleProfile();
	TestTracePlaneContract();
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
