/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/

#ifndef FNQL_DEMO_STREAM_HPP
#define FNQL_DEMO_STREAM_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fnql::demo {

constexpr std::size_t kEnvelopeHeaderSize = 8;

enum class EnvelopeStatus : std::uint8_t {
	Message,
	EndOfStreamTrailer,
	TruncatedHeader,
	TruncatedPayload,
	NegativeLength,
	OversizeLength,
};

struct EnvelopeResult {
	EnvelopeStatus status = EnvelopeStatus::TruncatedHeader;
	std::int32_t sequence = 0;
	std::int32_t payloadLength = 0;
	std::size_t headerBytes = 0;
	std::size_t payloadBytes = 0;
};

namespace detail {

constexpr std::uint32_t DecodeLittleUnsigned32(
	const std::uint8_t *bytes ) noexcept {
	return static_cast<std::uint32_t>( bytes[0] ) |
		( static_cast<std::uint32_t>( bytes[1] ) << 8u ) |
		( static_cast<std::uint32_t>( bytes[2] ) << 16u ) |
		( static_cast<std::uint32_t>( bytes[3] ) << 24u );
}

constexpr std::int32_t DecodeLittleSigned32(
	const std::uint8_t *bytes ) noexcept {
	const std::uint32_t value = DecodeLittleUnsigned32( bytes );

	// Avoid relying on an implementation-defined unsigned-to-signed narrowing
	// conversion for negative lengths and sequence numbers.
	if ( value <= static_cast<std::uint32_t>(
		( std::numeric_limits<std::int32_t>::max )() ) ) {
		return static_cast<std::int32_t>( value );
	}

	return -1 - static_cast<std::int32_t>(
		( std::numeric_limits<std::uint32_t>::max )() - value );
}

template<typename Reader>
std::size_t ReadUpTo( Reader &reader, std::uint8_t *destination,
	std::size_t requested ) {
	std::size_t total = 0;

	while ( total < requested ) {
		const std::size_t remaining = requested - total;
		const auto count = reader( destination + total, remaining );

		if ( count <= 0 ) {
			break;
		}

		const std::size_t bytesRead = static_cast<std::size_t>( count );
		// Readers receive the remaining capacity and must not report more than
		// that. Treat an impossible over-report as a failed short read instead of
		// claiming that bytes the reader did not provide were initialized.
		if ( bytesRead > remaining ) {
			break;
		}
		if ( bytesRead == remaining ) {
			total = requested;
			break;
		}
		total += bytesRead;
	}

	return total;
}

} // namespace detail

/*
Read one Quake demo record without interpreting its message payload.

Retail Quake Live dm_91 recordings retain the Quake III envelope: a signed
little-endian sequence, a signed little-endian payload length, and then that
many opaque message bytes. Retail writers emit {-1, -1} at EOF. Playback has
historically treated the -1 length as the sentinel even when the sequence is
not -1, so that tolerant behavior remains intentional for legacy captures.

Reader must be callable as reader(destination, requestedBytes), return a
signed or unsigned byte count, and never write beyond requestedBytes.
*/
template<typename Reader>
EnvelopeResult ReadEnvelope( Reader &&reader, void *payload,
	std::size_t payloadCapacity ) {
	std::array<std::uint8_t, kEnvelopeHeaderSize> header{};
	EnvelopeResult result;
	auto &read = reader;

	result.headerBytes = detail::ReadUpTo( read, header.data(), header.size() );
	if ( result.headerBytes != header.size() ) {
		result.status = EnvelopeStatus::TruncatedHeader;
		return result;
	}

	result.sequence = detail::DecodeLittleSigned32( header.data() );
	result.payloadLength = detail::DecodeLittleSigned32( header.data() + 4 );

	if ( result.payloadLength == -1 ) {
		result.status = EnvelopeStatus::EndOfStreamTrailer;
		return result;
	}
	if ( result.payloadLength < 0 ) {
		result.status = EnvelopeStatus::NegativeLength;
		return result;
	}

	const std::size_t payloadSize =
		static_cast<std::size_t>( result.payloadLength );
	if ( payloadSize > payloadCapacity ) {
		result.status = EnvelopeStatus::OversizeLength;
		return result;
	}

	if ( payloadSize != 0 && payload == nullptr ) {
		result.status = EnvelopeStatus::TruncatedPayload;
		return result;
	}

	result.payloadBytes = detail::ReadUpTo( read,
		static_cast<std::uint8_t *>( payload ), payloadSize );
	if ( result.payloadBytes != payloadSize ) {
		result.status = EnvelopeStatus::TruncatedPayload;
		return result;
	}

	result.status = EnvelopeStatus::Message;
	return result;
}

} // namespace fnql::demo

#endif
