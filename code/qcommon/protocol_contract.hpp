/*
===========================================================================
Copyright (C) 2026 FnQL contributors

This file is part of FnQL.

FnQL is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/

#ifndef FNQL_PROTOCOL_CONTRACT_HPP
#define FNQL_PROTOCOL_CONTRACT_HPP

#include "netchan_profile.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fnql::protocol {

// Text challenge responses carry an engine identity marker. Retail Quake Live
// does not emit it, so FnQL clients can honor the project policy of hosting
// retail clients without accidentally joining retail-operated servers.
inline constexpr std::string_view FnqlHandshakeMarker = "FnQL";

enum class Family : std::uint8_t {
	LegacyQuake3,
	Ioquake3,
	QuakeLive
};

enum class Capability : std::uint32_t {
	ClientQport = 1u << 0,
	ReliableXor = 1u << 1,
	SequenceChecksum = 1u << 2,
	CompressedConnect = 1u << 3,
	PlatformAuthentication = 1u << 4,
	WorkshopContent = 1u << 5,
	RetailClientsMayJoin = 1u << 6
};

[[nodiscard]] constexpr std::uint32_t Bit( Capability capability ) noexcept {
	return static_cast<std::uint32_t>( capability );
}

struct Commands {
	std::string_view getChallenge;
	std::string_view challengeResponse;
	std::string_view connect;
	std::string_view connectResponse;
	std::string_view getInfo;
	std::string_view infoResponse;
	std::string_view getStatus;
	std::string_view statusResponse;
	std::string_view disconnect;
	std::string_view print;
	std::string_view echo;
	std::string_view rcon;
	std::string_view download;
	std::string_view nextDownload;
	std::string_view stopDownload;
	std::string_view doneDownload;
	std::string_view pureChecksums;
	std::string_view pureReset;
	std::string_view userinfo;
};

struct InfoKeys {
	std::string_view protocol;
	std::string_view qport;
	std::string_view challenge;
	std::string_view serverId;
	std::string_view paks;
	std::string_view pakNames;
	std::string_view referencedPaks;
	std::string_view referencedPakNames;
	std::string_view fsGame;
	std::string_view systemPure;
};

struct Limits {
	std::size_t packetBytes;
	std::size_t messageBytes;
	std::size_t downloadWindow;
	std::size_t downloadBlockBytes;
	std::size_t reliableCommands;
	std::size_t packetBackup;
};

struct Contract {
	Family family;
	int protocol;
	int demoProtocol;
	netchanWireProfile_t wireProfile;
	std::uint32_t capabilities;
	Commands commands;
	InfoKeys infoKeys;
	Limits limits;

	[[nodiscard]] constexpr bool Has( Capability capability ) const noexcept {
		return ( capabilities & Bit( capability ) ) != 0u;
	}
};

inline constexpr Commands QuakeCommands{
	"getchallenge", "challengeResponse", "connect", "connectResponse",
	"getinfo", "infoResponse", "getstatus", "statusResponse",
	"disconnect", "print", "echo", "rcon", "download", "nextdl",
	"stopdl", "donedl", "cp", "vdr", "userinfo"
};

inline constexpr InfoKeys QuakeInfoKeys{
	"protocol", "qport", "challenge", "sv_serverid", "sv_paks",
	"sv_pakNames", "sv_referencedPaks", "sv_referencedPakNames",
	"fs_game", "sv_pure"
};

inline constexpr Limits ModernizedQ3Limits{
	1'400u, 16'384u, 48u, 1'024u, 64u, 32u
};

// QLSRP and the retail-facing download path identify an eight-block window
// of 2048-byte chunks. Storage may be larger, but the QL sender contract must
// retain these runtime limits.
inline constexpr Limits QuakeLiveLimits{
	1'400u, 16'384u, 8u, 2'048u, 64u, 32u
};

inline constexpr Contract LegacyQuake3{
	Family::LegacyQuake3, 68, 68, NETCHAN_WIRE_LEGACY_Q3,
	Bit( Capability::ClientQport ) | Bit( Capability::ReliableXor ) |
		Bit( Capability::CompressedConnect ),
	QuakeCommands, QuakeInfoKeys, ModernizedQ3Limits
};

inline constexpr Contract Ioquake3{
	Family::Ioquake3, 71, 71, NETCHAN_WIRE_IOQ3,
	Bit( Capability::ClientQport ) | Bit( Capability::SequenceChecksum ) |
		Bit( Capability::CompressedConnect ),
	QuakeCommands, QuakeInfoKeys, ModernizedQ3Limits
};

inline constexpr Contract QuakeLive{
	Family::QuakeLive, NETCHAN_QL_RETAIL_PROTOCOL_VERSION,
	NETCHAN_QL_RETAIL_PROTOCOL_VERSION, NETCHAN_WIRE_QL_RETAIL,
	Bit( Capability::ClientQport ) | Bit( Capability::ReliableXor ) |
		Bit( Capability::CompressedConnect ) |
		Bit( Capability::PlatformAuthentication ) |
		Bit( Capability::WorkshopContent ) |
		Bit( Capability::RetailClientsMayJoin ),
	QuakeCommands, QuakeInfoKeys, QuakeLiveLimits
};

[[nodiscard]] constexpr const Contract *Find( int protocol,
	bool legacyCompatibility = false ) noexcept {
	if ( protocol == QuakeLive.protocol ) {
		return &QuakeLive;
	}
	if ( protocol == LegacyQuake3.protocol || legacyCompatibility ) {
		return &LegacyQuake3;
	}
	if ( protocol == Ioquake3.protocol ) {
		return &Ioquake3;
	}
	return nullptr;
}

[[nodiscard]] constexpr const Contract &ForWireProfile(
	netchanWireProfile_t profile ) noexcept {
	switch ( profile ) {
	case NETCHAN_WIRE_QL_RETAIL:
		return QuakeLive;
	case NETCHAN_WIRE_IOQ3:
		return Ioquake3;
	case NETCHAN_WIRE_LEGACY_Q3:
	default:
		return LegacyQuake3;
	}
}

} // namespace fnql::protocol

#endif
