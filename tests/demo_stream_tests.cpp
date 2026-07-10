#include "demo_stream.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

#define CHECK( condition ) do { \
	if ( !( condition ) ) { \
		std::cerr << "CHECK failed at " << __FILE__ << ':' << __LINE__ \
			<< ": " #condition "\n"; \
		std::exit( 1 ); \
	} \
} while ( false )

class MemoryReader {
public:
	MemoryReader( const std::uint8_t *data, std::size_t size,
		std::size_t maxChunk = static_cast<std::size_t>( -1 ) ) noexcept
		: data_( data ), size_( size ), maxChunk_( maxChunk ) {
	}

	std::ptrdiff_t operator()( std::uint8_t *destination,
		std::size_t requested ) {
		const std::size_t available = size_ - position_;
		const std::size_t count = std::min( { requested, available, maxChunk_ } );

		if ( count == 0 ) {
			return 0;
		}
		std::memcpy( destination, data_ + position_, count );
		position_ += count;
		return static_cast<std::ptrdiff_t>( count );
	}

	[[nodiscard]] std::size_t position() const noexcept {
		return position_;
	}

private:
	const std::uint8_t *data_;
	std::size_t size_;
	std::size_t maxChunk_;
	std::size_t position_ = 0;
};

template<std::size_t Size>
MemoryReader ReaderFor( const std::array<std::uint8_t, Size> &bytes,
	std::size_t maxChunk = static_cast<std::size_t>( -1 ) ) {
	return MemoryReader( bytes.data(), bytes.size(), maxChunk );
}

void TestRetailEnvelopeGoldenStream() {
	// Two opaque records followed by the canonical {-1, -1} retail trailer.
	// Payload bytes deliberately carry no svc/snapshot meaning in this test.
	constexpr std::array<std::uint8_t, 28> golden = {
		0x78, 0x56, 0x34, 0x12,  0x04, 0x00, 0x00, 0x00,
		0x00, 0x7f, 0x80, 0xff,
		0x02, 0x00, 0x00, 0x00,  0x00, 0x00, 0x00, 0x00,
		0xff, 0xff, 0xff, 0xff,  0xff, 0xff, 0xff, 0xff,
	};
	MemoryReader reader( golden.data(), golden.size(), 3 );
	std::array<std::uint8_t, 4> payload{};

	auto result = fnql::demo::ReadEnvelope( reader, payload.data(), payload.size() );
	CHECK( result.status == fnql::demo::EnvelopeStatus::Message );
	CHECK( result.sequence == 0x12345678 );
	CHECK( result.payloadLength == 4 );
	CHECK( result.headerBytes == fnql::demo::kEnvelopeHeaderSize );
	CHECK( result.payloadBytes == payload.size() );
	CHECK( ( payload == std::array<std::uint8_t, 4>{ 0x00, 0x7f, 0x80, 0xff } ) );

	result = fnql::demo::ReadEnvelope( reader, payload.data(), payload.size() );
	CHECK( result.status == fnql::demo::EnvelopeStatus::Message );
	CHECK( result.sequence == 2 );
	CHECK( result.payloadLength == 0 );
	CHECK( result.payloadBytes == 0 );

	result = fnql::demo::ReadEnvelope( reader, payload.data(), payload.size() );
	CHECK( result.status == fnql::demo::EnvelopeStatus::EndOfStreamTrailer );
	CHECK( result.sequence == -1 );
	CHECK( result.payloadLength == -1 );
	CHECK( reader.position() == golden.size() );
}

void TestTruncatedHeader() {
	constexpr std::array<std::uint8_t, 7> bytes = {
		0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
	};
	auto reader = ReaderFor( bytes, 2 );
	std::array<std::uint8_t, 4> payload{};
	const auto result = fnql::demo::ReadEnvelope(
		reader, payload.data(), payload.size() );

	CHECK( result.status == fnql::demo::EnvelopeStatus::TruncatedHeader );
	CHECK( result.headerBytes == bytes.size() );
	CHECK( result.payloadBytes == 0 );
}

void TestTruncatedPayload() {
	constexpr std::array<std::uint8_t, 11> bytes = {
		0x07, 0x00, 0x00, 0x00,  0x04, 0x00, 0x00, 0x00,
		0xaa, 0xbb, 0xcc,
	};
	auto reader = ReaderFor( bytes, 2 );
	std::array<std::uint8_t, 4> payload{};
	const auto result = fnql::demo::ReadEnvelope(
		reader, payload.data(), payload.size() );

	CHECK( result.status == fnql::demo::EnvelopeStatus::TruncatedPayload );
	CHECK( result.sequence == 7 );
	CHECK( result.payloadLength == 4 );
	CHECK( result.payloadBytes == 3 );
}

void TestNegativeLength() {
	constexpr std::array<std::uint8_t, 8> bytes = {
		0x09, 0x00, 0x00, 0x00,  0xfe, 0xff, 0xff, 0xff,
	};
	auto reader = ReaderFor( bytes );
	std::array<std::uint8_t, 4> payload{};
	const auto result = fnql::demo::ReadEnvelope(
		reader, payload.data(), payload.size() );

	CHECK( result.status == fnql::demo::EnvelopeStatus::NegativeLength );
	CHECK( result.sequence == 9 );
	CHECK( result.payloadLength == -2 );
	CHECK( result.payloadBytes == 0 );
}

void TestOversizeLength() {
	constexpr std::array<std::uint8_t, 13> bytes = {
		0x0a, 0x00, 0x00, 0x00,  0x05, 0x00, 0x00, 0x00,
		0x11, 0x22, 0x33, 0x44, 0x55,
	};
	auto reader = ReaderFor( bytes );
	std::array<std::uint8_t, 4> payload{};
	const auto result = fnql::demo::ReadEnvelope(
		reader, payload.data(), payload.size() );

	CHECK( result.status == fnql::demo::EnvelopeStatus::OversizeLength );
	CHECK( result.payloadLength == 5 );
	CHECK( result.payloadBytes == 0 );
	CHECK( reader.position() == fnql::demo::kEnvelopeHeaderSize );
}

void TestLegacyLengthSentinelTolerance() {
	constexpr std::array<std::uint8_t, 8> bytes = {
		0x63, 0x00, 0x00, 0x00,  0xff, 0xff, 0xff, 0xff,
	};
	auto reader = ReaderFor( bytes );
	std::array<std::uint8_t, 1> payload{};
	const auto result = fnql::demo::ReadEnvelope(
		reader, payload.data(), payload.size() );

	CHECK( result.status == fnql::demo::EnvelopeStatus::EndOfStreamTrailer );
	CHECK( result.sequence == 99 );
	CHECK( result.payloadLength == -1 );
}

void TestReaderOverReportFailsClosed() {
	std::array<std::uint8_t, 1> payload{};
	auto reader = []( std::uint8_t *, std::size_t requested ) {
		return static_cast<std::ptrdiff_t>( requested + 1 );
	};
	const auto result = fnql::demo::ReadEnvelope(
		reader, payload.data(), payload.size() );

	CHECK( result.status == fnql::demo::EnvelopeStatus::TruncatedHeader );
	CHECK( result.headerBytes == 0 );
	CHECK( result.payloadBytes == 0 );
}

} // namespace

int main() {
	static_assert( fnql::demo::kEnvelopeHeaderSize == 8,
		"Quake Live demo envelope size changed" );
	TestRetailEnvelopeGoldenStream();
	TestTruncatedHeader();
	TestTruncatedPayload();
	TestNegativeLength();
	TestOversizeLength();
	TestLegacyLengthSentinelTolerance();
	TestReaderOverReportFailsClosed();
	std::cout << "demo stream tests passed\n";
	return 0;
}
