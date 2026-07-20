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

#ifndef FNQL_SERVER_FACTORY_ROTATION_HPP
#define FNQL_SERVER_FACTORY_ROTATION_HPP

#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fnql::server::rotation {

// Retail uses a 0x4000-byte scratch buffer for each arena source and a fixed
// 1,024-record registry shared by arenas.txt and subsequently loaded files.
// A source whose length reaches the buffer size is rejected.
inline constexpr std::size_t MaximumArenaTextBytes = 0x4000;
inline constexpr std::size_t MaximumArenaRecords = 1024;
inline constexpr std::size_t MaximumArenaFields = 128;
// COM_Parse writes into MAX_TOKEN_CHARS (1,024 bytes) and silently keeps the
// first 1,023 bytes of a longer token.
inline constexpr std::size_t MaximumArenaTokenBytes = 1023;
inline constexpr std::size_t MaximumIdentifierBytes = 1023;
// Retail stores arena map/longname and resolved pool metadata in 0x400-byte,
// NUL-terminated fields. Arena source pairs first pass through a shared 0x400
// info string, so a pair that does not fit is omitted while parsing continues.
inline constexpr std::size_t MaximumArenaLongNameBytes = 1023;
inline constexpr std::size_t MaximumArenaTypeBytes = 1023;
inline constexpr std::size_t MaximumResolvedTitleBytes = 1023;

// The retail rotation loader rejects files at this size rather than silently
// accepting a prefix. Keeping the limit in this independent layer makes the
// future filesystem adapter deterministic on every platform.
inline constexpr std::size_t MaximumMapPoolTextBytes = 0x8000;
inline constexpr std::size_t MaximumMapPoolRows = 1024;

inline constexpr int MinimumRetailBaseGameType = 0;
inline constexpr int MaximumRetailBaseGameType = 12;

enum class ArenaSupport {
	Unknown,
	Supported,
	Unsupported
};

struct ArenaRecord {
	std::string map;
	std::string longName;
	std::string type;
};

class ArenaCatalog {
public:
	[[nodiscard]] bool Append( ArenaRecord record );
	void Clear() noexcept;

	[[nodiscard]] const std::vector<ArenaRecord> &Records() const noexcept;
	// Retail performs an exact case-sensitive scan and returns the first match.
	[[nodiscard]] const ArenaRecord *Find( std::string_view map ) const noexcept;
	[[nodiscard]] ArenaSupport SupportFor(
		std::string_view map, int baseGameType ) const noexcept;

private:
	std::vector<ArenaRecord> records_;
};

enum class DiagnosticCode {
	ArenaInputTooLarge,
	ArenaTokenTooLong,
	ArenaUnterminatedQuote,
	ArenaUnterminatedComment,
	ArenaUnexpectedToken,
	ArenaNestedBlock,
	ArenaMissingValue,
	ArenaUnterminatedBlock,
	ArenaTooManyFields,
	ArenaTooManyRecords,
	ArenaMissingMap,
	ArenaInvalidMap,
	ArenaFieldTooLong,

	MapPoolInputTooLarge,
	MapPoolMalformedRow,
	MapPoolInvalidMap,
	MapPoolInvalidFactory,
	MapPoolTooManyRows,

	ResolverUnavailable,
	MapNotFound,
	FactoryNotFound,
	FactoryHidden,
	InvalidResolvedMap,
	InvalidResolvedFactory,
	InvalidBaseGameType,
	ArenaUnsupported,
	ArenaUnknown
};

struct Diagnostic {
	DiagnosticCode code = DiagnosticCode::ArenaUnexpectedToken;
	std::size_t line = 0;
	std::size_t offset = 0;
};

struct ProcessingReport {
	std::size_t accepted = 0;
	std::size_t rejected = 0;
	bool fatal = false;

	[[nodiscard]] explicit operator bool() const noexcept { return !fatal; }
};

// Diagnostics are appended so callers can combine parsing and resolution into
// one report. Arena records are likewise appended to support the legacy
// scripts/arenas.txt followed by scripts/*.arena loading order.
[[nodiscard]] ProcessingReport ParseArenaText(
	std::string_view text, ArenaCatalog &catalog,
	std::vector<Diagnostic> *diagnostics = nullptr );

[[nodiscard]] bool IdentifierIsValid( std::string_view identifier ) noexcept;

struct MapPoolRow {
	std::string map;
	std::string factory;
	std::size_t sourceLine = 0;
};

// A map-pool parse represents one file, so rows are cleared before parsing.
[[nodiscard]] ProcessingReport ParseMapPoolText(
	std::string_view text, std::vector<MapPoolRow> &rows,
	std::vector<Diagnostic> *diagnostics = nullptr );

struct MapMetadata {
	std::string map;
	std::string title;
};

struct FactoryMetadata {
	std::string factory;
	std::string title;
	int baseGameType = MinimumRetailBaseGameType;
	bool hidden = false;
};

struct RotationResolver {
	std::function<std::optional<MapMetadata>( std::string_view )> map;
	std::function<std::optional<FactoryMetadata>( std::string_view )> factory;
	std::function<ArenaSupport( std::string_view, int )> arenaSupport;
};

struct ResolveOptions {
	bool includeHiddenFactories = false;
	bool acceptUnknownArenas = true;
};

struct RotationEntry {
	std::string requestedMap;
	std::string requestedFactory;
	std::string map;
	std::string factory;
	std::string mapTitle;
	std::string factoryTitle;
	int baseGameType = MinimumRetailBaseGameType;
	ArenaSupport arenaSupport = ArenaSupport::Unknown;
	std::size_t sourceLine = 0;
};

struct WebMapCatalogResult {
	bool success = false;
	std::string json;

	[[nodiscard]] explicit operator bool() const noexcept { return success; }
};

// The retail Start Match document indexes this array by map sysname and filters
// it by the 13 base-gametype flags. Pool rows may repeat a map for different
// factories, so serialization folds those rows into one descriptor while
// preserving first appearance order and title.
[[nodiscard]] WebMapCatalogResult SerializeWebMapCatalog(
	const std::vector<RotationEntry> &entries, std::size_t outputByteCap );

// Resolution represents one snapshot and clears entries before processing.
// Valid entries retain file order. Unknown arena metadata is accepted by
// default because retail permits a BSP with no arena record; an existing
// record with a zero support flag is always rejected. Resolver lookups own row
// validity, matching retail's factory-first then BSP lookup order.
[[nodiscard]] ProcessingReport ResolveMapPool(
	const std::vector<MapPoolRow> &rows, const RotationResolver &resolver,
	std::vector<RotationEntry> &entries,
	std::vector<Diagnostic> *diagnostics = nullptr,
	ResolveOptions options = {} );

inline constexpr std::size_t RotationSlotCount = 3;
inline constexpr std::size_t InvalidRotationIndex =
	std::numeric_limits<std::size_t>::max();

struct RotationModel {
	std::array<std::size_t, RotationSlotCount> indices{{
		InvalidRotationIndex, InvalidRotationIndex, InvalidRotationIndex
	}};
	std::size_t count = 0;
};

// randomIndex is supplied by the caller; this layer never reads clocks,
// process-global PRNGs, or engine state.
[[nodiscard]] std::optional<std::size_t> SelectRotationIndex(
	const std::vector<RotationEntry> &entries, std::size_t randomIndex,
	std::string_view currentMap = {}, bool excludeCurrentMap = true );

// Returns only pool-backed slots; initialSlot is the number of slots already
// occupied (normally zero or one current-map entry). Retail starts file-order
// selection at that same index for pools smaller than four. Larger pools draw
// against the original pool size and reroll collisions, so the callback may be
// consumed more than once per returned slot.
[[nodiscard]] RotationModel BuildRotationModel(
	const std::vector<RotationEntry> &entries,
	std::size_t initialSlot, const std::function<std::size_t()> &nextRandomIndex );

} // namespace fnql::server::rotation

#endif // FNQL_SERVER_FACTORY_ROTATION_HPP
