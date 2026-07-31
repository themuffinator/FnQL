#include "../../code/client/audio/shared/AudioInitPolicy.h"

#include <cstddef>
#include <iostream>
#include <limits>

namespace {

int failures = 0;

#define CHECK(expression) \
	do { \
		if ( !( expression ) ) { \
			std::cerr << "CHECK failed at line " << __LINE__ << ": " #expression "\n"; \
			++failures; \
		} \
	} while ( false )

void TestDmaValidation()
{
	std::size_t bytes = 0;
	CHECK( fnql_audio_init::DmaConfigurationIsUsable(
		2, 16, false, 48000, 65536, 32768, bytes ) );
	CHECK( bytes == 131072u );

	CHECK( fnql_audio_init::DmaConfigurationIsUsable(
		1, 8, false, 22050, 32768, 32768, bytes ) );
	CHECK( bytes == 32768u );

	CHECK( fnql_audio_init::DmaConfigurationIsUsable(
		2, 32, true, 48000, 65536, 32768, bytes ) );
	CHECK( bytes == 262144u );

	CHECK( !fnql_audio_init::DmaConfigurationIsUsable(
		6, 16, false, 48000, 65532, 10922, bytes ) );
	CHECK( !fnql_audio_init::DmaConfigurationIsUsable(
		2, 32, false, 48000, 65536, 32768, bytes ) );
	CHECK( !fnql_audio_init::DmaConfigurationIsUsable(
		2, 16, false, 0, 65536, 32768, bytes ) );
	CHECK( !fnql_audio_init::DmaConfigurationIsUsable(
		2, 16, false, 48000, 65535, 32767, bytes ) );
	CHECK( !fnql_audio_init::DmaConfigurationIsUsable(
		2, 16, false, 48000, 65536, 1, bytes ) );

	CHECK( !fnql_audio_init::DmaBufferByteCount(
		( std::numeric_limits<int>::max )(), 7, bytes ) );
}

void TestFrameRateScaling()
{
	int frames = 0;
	CHECK( fnql_audio_init::ScaleFrameCount(
		1024, 48000, 24000, 32768, frames ) );
	CHECK( frames == 512 );

	CHECK( fnql_audio_init::ScaleFrameCount(
		1024, 44100, 48000, 32768, frames ) );
	CHECK( frames == 1115 );

	CHECK( fnql_audio_init::ScaleFrameCount(
		( std::numeric_limits<int>::max )(), 1, 384000, 32768, frames ) );
	CHECK( frames == 32768 );

	CHECK( !fnql_audio_init::ScaleFrameCount(
		0, 48000, 48000, 32768, frames ) );
	CHECK( !fnql_audio_init::ScaleFrameCount(
		1024, 0, 48000, 32768, frames ) );
}

} // namespace

int main()
{
	TestDmaValidation();
	TestFrameRateScaling();

	if ( failures != 0 ) {
		std::cerr << failures << " audio init policy test(s) failed\n";
		return 1;
	}

	std::cout << "audio init policy tests passed\n";
	return 0;
}
