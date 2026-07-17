#pragma once

#include <cstdint>

namespace fnql::net {

struct PackedIpv4Host {
	bool valid = false;
	std::uint32_t value = 0;
};

// Quake Live's WebUI identifies IPv4 servers with the host-order decimal
// value used by Steam's server-browser API. Keep that representation distinct
// from DNS names and dotted addresses, and reject overflow deterministically.
[[nodiscard]] constexpr PackedIpv4Host ParsePackedIpv4Host(
	const char *text ) noexcept {
	if ( !text || !text[0] ) {
		return {};
	}

	std::uint32_t value = 0;
	for ( const char *cursor = text; *cursor; ++cursor ) {
		if ( *cursor < '0' || *cursor > '9' ) {
			return {};
		}

		const std::uint32_t digit = static_cast<std::uint32_t>( *cursor - '0' );
		if ( value > ( 0xffffffffu - digit ) / 10u ) {
			return {};
		}
		value = value * 10u + digit;
	}

	return { true, value };
}

} // namespace fnql::net
