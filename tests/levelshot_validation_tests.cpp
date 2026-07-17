#include "../code/qcommon/q_shared.h"
#include "../code/renderercommon/tr_levelshot.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <limits>

extern "C" {

void QDECL Com_Printf( const char *, ... )
{
}

float Q_atof( const char *value )
{
	return value ? std::strtof( value, nullptr ) : 0.0f;
}

void QDECL Com_Error( errorParm_t, const char *format, ... )
{
	std::va_list args;

	std::fputs( "unexpected Com_Error in levelshot validation test: ", stderr );
	va_start( args, format );
	std::vfprintf( stderr, format, args );
	va_end( args );
	std::fputc( '\n', stderr );
	std::abort();
}

} // extern "C"

#define CHECK( condition ) do { \
	if ( !( condition ) ) { \
		std::fprintf( stderr, "CHECK failed at %s:%d: %s\n", \
			__FILE__, __LINE__, #condition ); \
		return 1; \
	} \
} while ( false )

int main()
{
	int width = 0;
	int height = 0;
	float aspect = 0.0f;
	size_t bytes = 0;

	CHECK( R_ParseLevelshotSize( "512", &width, &height ) );
	CHECK( width == 512 && height == 512 );
	CHECK( R_ParseLevelshotSize( "1024x512", &width, &height ) );
	CHECK( width == 1024 && height == 512 );
	CHECK( R_ParseLevelshotSize( "4096X4096", &width, &height ) );
	CHECK( R_ParseLevelshotSize( "640:360", &width, &height ) );
	CHECK( R_ParseLevelshotSize( "640/360", &width, &height ) );
	CHECK( R_ParseLevelshotSize( "65535x1", &width, &height ) );
	CHECK( width == 65535 && height == 1 );
	CHECK( R_ParseLevelshotSize( "4729x4729", &width, &height ) );
	CHECK( !R_ParseLevelshotSize( "4730x4730", &width, &height ) );
	CHECK( !R_ParseLevelshotSize( "8192x8192", &width, &height ) );
	CHECK( !R_ParseLevelshotSize( "65536", &width, &height ) );
	CHECK( !R_ParseLevelshotSize( "512garbage", &width, &height ) );
	CHECK( !R_ParseLevelshotSize( "1x2x3", &width, &height ) );
	CHECK( !R_ParseLevelshotSize( "-1", &width, &height ) );
	CHECK( !R_ParseLevelshotSize( "0", &width, &height ) );
	CHECK( !R_ParseLevelshotSize( "none", &width, &height ) );
	CHECK( !R_ParseLevelshotSize( "SOURCE", &width, &height ) );
	CHECK( !R_ParseLevelshotSize( "viewport", &width, &height ) );
	CHECK( !R_ParseLevelshotSize( "default", &width, &height ) );
	CHECK( !R_ParseLevelshotSize( "", &width, &height ) );
	CHECK( !R_ParseLevelshotSize( nullptr, &width, &height ) );

	CHECK( R_LevelshotCheckedRgbBytes( 4096, 4096, &bytes ) );
	CHECK( bytes == (size_t)4096 * 4096 * 3 );
	CHECK( R_LevelshotCheckedRgbBytes( 65535, 1, &bytes ) );
	CHECK( !R_LevelshotCheckedRgbBytes( 8192, 8192, &bytes ) );
	CHECK( !R_LevelshotCheckedRgbBytes( 65536, 1, &bytes ) );

	CHECK( R_ParseLevelshotAspect( "16:9", &aspect ) );
	CHECK( aspect > 1.77f && aspect < 1.78f );
	CHECK( R_ParseLevelshotAspect( "1.5", &aspect ) );
	CHECK( aspect == 1.5f );
	CHECK( R_ParseLevelshotAspect( "4/3", &aspect ) );
	CHECK( R_ParseLevelshotAspect( "4X3", &aspect ) );
	CHECK( !R_ParseLevelshotAspect( "none", &aspect ) );
	CHECK( !R_ParseLevelshotAspect( "source", &aspect ) );
	CHECK( !R_ParseLevelshotAspect( "viewport", &aspect ) );
	CHECK( !R_ParseLevelshotAspect( "default", &aspect ) );
	CHECK( !R_ParseLevelshotAspect( "nan", &aspect ) );
	CHECK( !R_ParseLevelshotAspect( "inf", &aspect ) );
	CHECK( !R_ParseLevelshotAspect( "-inf", &aspect ) );
	CHECK( !R_ParseLevelshotAspect( "1:0", &aspect ) );
	CHECK( !R_ParseLevelshotAspect( "1:2:3", &aspect ) );
	CHECK( !R_ParseLevelshotAspect( "1e39", &aspect ) );
	CHECK( !R_ParseLevelshotAspect( "1e-50", &aspect ) );

	CHECK( R_LevelshotFloatIsFinite( 2.0f ) );
	CHECK( !R_LevelshotFloatIsFinite( std::numeric_limits<float>::infinity() ) );
	CHECK( !R_LevelshotFloatIsFinite( std::numeric_limits<float>::quiet_NaN() ) );

	return 0;
}
