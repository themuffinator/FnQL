#include "../code/qcommon/q_shared.h"
#include "../code/renderercommon/tr_menu_blur.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>

extern "C" {

void QDECL Com_Printf( const char *, ... )
{
}

void QDECL Com_Error( errorParm_t, const char *format, ... )
{
	std::va_list args;

	std::fputs( "unexpected Com_Error in menu blur test: ", stderr );
	va_start( args, format );
	std::vfprintf( stderr, format, args );
	va_end( args );
	std::fputc( '\n', stderr );
	std::abort();
}

float Q_atof( const char *text )
{
	return std::strtof( text, nullptr );
}

} // extern "C"

namespace {

int g_failures;

#define CHECK( condition ) do { \
	if ( !( condition ) ) { \
		std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__ \
			<< ": " << #condition << '\n'; \
		++g_failures; \
	} \
} while ( 0 )

bool NearlyEqual( float a, float b, float tolerance )
{
	return std::fabs( a - b ) <= tolerance;
}

float TotalSigma( const menuBlurPlan_t &plan )
{
	float variance = 0.0f;

	for ( int i = 0; i < plan.passCount; ++i ) {
		variance += plan.passSigma[i] * plan.passSigma[i];
	}
	return std::sqrt( variance );
}

/*
A disabled plan must be inert rather than an error: every backend treats it as
"leave the finished frame alone".
*/
void TestDisabledInputsProduceAnInertPlan()
{
	menuBlurPlan_t plan;

	CHECK( !R_MenuBlur_Plan( 0.0f, 1920, 1080, &plan ) );
	CHECK( !plan.enabled );
	CHECK( plan.passCount == 0 );
	CHECK( plan.alpha == 0.0f );
	CHECK( plan.levelWidth == 0 && plan.levelHeight == 0 );

	CHECK( !R_MenuBlur_Plan( -1.0f, 1920, 1080, &plan ) );
	CHECK( !plan.enabled );

	// A strength below the visible-composite floor is not worth eight passes.
	CHECK( !R_MenuBlur_Plan( MENU_BLUR_MIN_ALPHA * 0.5f, 1920, 1080, &plan ) );
	CHECK( !plan.enabled );

	// Non-finite strength must not survive as an enabled plan.
	CHECK( !R_MenuBlur_Plan( std::numeric_limits<float>::quiet_NaN(), 1920, 1080, &plan ) );
	CHECK( !plan.enabled );

	CHECK( !R_MenuBlur_Plan( 1.0f, 0, 1080, &plan ) );
	CHECK( !plan.enabled );
	CHECK( !R_MenuBlur_Plan( 1.0f, 1920, 0, &plan ) );
	CHECK( !plan.enabled );
	CHECK( !R_MenuBlur_Plan( 1.0f, -1920, -1080, &plan ) );
	CHECK( !plan.enabled );

	// Too small to hold a quarter-resolution level.
	CHECK( !R_MenuBlur_Plan( 1.0f, 3, 480, &plan ) );
	CHECK( !plan.enabled );
	CHECK( !R_MenuBlur_Plan( 1.0f, 640, 3, &plan ) );
	CHECK( !plan.enabled );

	CHECK( !R_MenuBlur_Plan( 1.0f, 1920, 1080, NULL ) );
}

/*
The pyramid steps through 1/2 before 1/4; a single bilinear step to 1/4 drops
three of every four texels and makes the backdrop crawl as the scene animates.
*/
void TestPyramidHalvesTwice()
{
	menuBlurPlan_t plan;

	CHECK( R_MenuBlur_Plan( 1.0f, 1920, 1080, &plan ) );
	CHECK( plan.enabled );
	CHECK( plan.halfWidth == 960 );
	CHECK( plan.halfHeight == 540 );
	CHECK( plan.levelWidth == 480 );
	CHECK( plan.levelHeight == 270 );

	// Odd extents truncate but never collapse to zero.
	CHECK( R_MenuBlur_Plan( 1.0f, 1365, 767, &plan ) );
	CHECK( plan.halfWidth == 682 );
	CHECK( plan.halfHeight == 383 );
	CHECK( plan.levelWidth == 341 );
	CHECK( plan.levelHeight == 191 );

	CHECK( R_MenuBlur_Plan( 1.0f, 5, 4, &plan ) );
	CHECK( plan.enabled );
	CHECK( plan.halfWidth == 2 );
	CHECK( plan.halfHeight == 2 );
	CHECK( plan.levelWidth == 1 );
	CHECK( plan.levelHeight == 1 );
}

/*
Softness has to read the same at every resolution, so the total sigma is a
fraction of target height and the per-pass sigmas combine in quadrature to it.
*/
void TestTotalSigmaTracksTargetHeight()
{
	menuBlurPlan_t plan;
	const float expected1080 =
		MENU_BLUR_SIGMA_FRACTION * 1080.0f / (float)MENU_BLUR_LEVEL_DIVISOR;
	const float expected2160 =
		MENU_BLUR_SIGMA_FRACTION * 2160.0f / (float)MENU_BLUR_LEVEL_DIVISOR;

	CHECK( R_MenuBlur_Plan( 1.0f, 1920, 1080, &plan ) );
	CHECK( plan.passCount == MENU_BLUR_PASS_COUNT );
	CHECK( NearlyEqual( TotalSigma( plan ), expected1080, 0.001f ) );

	// Level texels grow with the target, so sigma in level texels doubles when
	// the resolution doubles: the blur covers the same share of the screen.
	CHECK( R_MenuBlur_Plan( 1.0f, 3840, 2160, &plan ) );
	CHECK( NearlyEqual( TotalSigma( plan ), expected2160, 0.001f ) );
	CHECK( NearlyEqual( expected2160, expected1080 * 2.0f, 0.001f ) );

	// Width does not enter the softness, only height.
	menuBlurPlan_t wide;
	CHECK( R_MenuBlur_Plan( 1.0f, 2560, 1080, &wide ) );
	CHECK( R_MenuBlur_Plan( 1.0f, 1920, 1080, &plan ) );
	CHECK( NearlyEqual( TotalSigma( wide ), TotalSigma( plan ), 0.0001f ) );
}

/*
Per-pass sigmas ramp 1,2,3,4. The tight first pass removes the frequencies that
would show sparse later taps as banding.
*/
void TestPassSigmasRampAndStayOrdered()
{
	menuBlurPlan_t plan;

	CHECK( R_MenuBlur_Plan( 1.0f, 1920, 1080, &plan ) );
	CHECK( plan.passCount == 4 );
	for ( int i = 1; i < plan.passCount; ++i ) {
		CHECK( plan.passSigma[i] > plan.passSigma[i - 1] );
		CHECK( NearlyEqual( plan.passSigma[i],
			plan.passSigma[0] * (float)( i + 1 ), 0.0005f ) );
	}
	CHECK( plan.passSigma[0] > 0.0f );
}

/*
Strength is a focus pull: it scales sigma as well as the composite weight, so a
partial value is a gently softened frame, not a ghosted double image.
*/
void TestStrengthScalesSigmaAndAlphaTogether()
{
	menuBlurPlan_t full;
	menuBlurPlan_t half;

	CHECK( R_MenuBlur_Plan( 1.0f, 1920, 1080, &full ) );
	CHECK( R_MenuBlur_Plan( 0.5f, 1920, 1080, &half ) );

	CHECK( NearlyEqual( full.alpha, 1.0f, 0.0001f ) );
	CHECK( NearlyEqual( half.alpha, 0.5f, 0.0001f ) );
	CHECK( NearlyEqual( TotalSigma( half ), TotalSigma( full ) * 0.5f, 0.001f ) );

	// The pass count is constant so a fade cannot pop as the ramp crosses a
	// pass-count threshold.
	CHECK( half.passCount == full.passCount );

	// Over-range strength clamps instead of over-blurring.
	menuBlurPlan_t clamped;
	CHECK( R_MenuBlur_Plan( 4.0f, 1920, 1080, &clamped ) );
	CHECK( NearlyEqual( clamped.alpha, 1.0f, 0.0001f ) );
	CHECK( NearlyEqual( TotalSigma( clamped ), TotalSigma( full ), 0.0001f ) );
}

/*
The backends run different kernels, so they agree on sigma and convert it to
their own tap spacing. A backend whose kernel is wider per unit of spacing has
to step less far.
*/
void TestTapSpacingConvertsSigmaPerKernel()
{
	const float sigma = 2.0f;
	const float binomial6 =
		R_MenuBlur_TapSpacing( sigma, MENU_BLUR_KERNEL_VARIANCE_BINOMIAL6 );
	const float linear3 =
		R_MenuBlur_TapSpacing( sigma, MENU_BLUR_KERNEL_VARIANCE_LINEAR3 );

	// spacing * sqrt(unit variance) reproduces the requested sigma.
	CHECK( NearlyEqual( binomial6 * std::sqrt( MENU_BLUR_KERNEL_VARIANCE_BINOMIAL6 ),
		sigma, 0.0005f ) );
	CHECK( NearlyEqual( linear3 * std::sqrt( MENU_BLUR_KERNEL_VARIANCE_LINEAR3 ),
		sigma, 0.0005f ) );

	// The 6-tap binomial kernel is the wider of the two at unit spacing.
	CHECK( MENU_BLUR_KERNEL_VARIANCE_BINOMIAL6 > MENU_BLUR_KERNEL_VARIANCE_LINEAR3 );
	CHECK( binomial6 < linear3 );

	// Degenerate inputs collapse to "do not step", not to a NaN offset.
	CHECK( R_MenuBlur_TapSpacing( 0.0f, MENU_BLUR_KERNEL_VARIANCE_LINEAR3 ) == 0.0f );
	CHECK( R_MenuBlur_TapSpacing( -1.0f, MENU_BLUR_KERNEL_VARIANCE_LINEAR3 ) == 0.0f );
	CHECK( R_MenuBlur_TapSpacing( sigma, 0.0f ) == 0.0f );
	CHECK( R_MenuBlur_TapSpacing( std::numeric_limits<float>::quiet_NaN(),
		MENU_BLUR_KERNEL_VARIANCE_LINEAR3 ) == 0.0f );
}

/*
Every backend converts the same plan, so the tap spacing each derives must land
on the same sigma. This is the property that keeps GL, Vulkan, and RTX matched.
*/
void TestBackendsReachTheSameSoftness()
{
	menuBlurPlan_t plan;
	float glVariance = 0.0f;
	float vkVariance = 0.0f;

	CHECK( R_MenuBlur_Plan( 0.75f, 1920, 1080, &plan ) );
	for ( int i = 0; i < plan.passCount; ++i ) {
		const float gl = R_MenuBlur_TapSpacing( plan.passSigma[i],
			MENU_BLUR_KERNEL_VARIANCE_BINOMIAL6 );
		const float vk = R_MenuBlur_TapSpacing( plan.passSigma[i],
			MENU_BLUR_KERNEL_VARIANCE_LINEAR3 );

		glVariance += gl * gl * MENU_BLUR_KERNEL_VARIANCE_BINOMIAL6;
		vkVariance += vk * vk * MENU_BLUR_KERNEL_VARIANCE_LINEAR3;
	}
	CHECK( NearlyEqual( std::sqrt( glVariance ), TotalSigma( plan ), 0.001f ) );
	CHECK( NearlyEqual( std::sqrt( vkVariance ), TotalSigma( plan ), 0.001f ) );
	CHECK( NearlyEqual( std::sqrt( glVariance ), std::sqrt( vkVariance ), 0.001f ) );
}

} // namespace

int main()
{
	TestDisabledInputsProduceAnInertPlan();
	TestPyramidHalvesTwice();
	TestTotalSigmaTracksTargetHeight();
	TestPassSigmasRampAndStayOrdered();
	TestStrengthScalesSigmaAndAlphaTogether();
	TestTapSpacingConvertsSigmaPerKernel();
	TestBackendsReachTheSameSoftness();

	if ( g_failures != 0 ) {
		std::cerr << g_failures << " menu blur check(s) failed\n";
		return 1;
	}
	std::cout << "menu blur tests passed\n";
	return 0;
}
