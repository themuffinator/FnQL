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

#ifndef FNQL_SERVER_FACTORY_CATALOG_HPP
#define FNQL_SERVER_FACTORY_CATALOG_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fnql::server::factory {

inline constexpr std::size_t MaximumDefinitions = 1024;
inline constexpr std::size_t MaximumSettings = 256;
inline constexpr std::size_t MaximumTags = 8;

enum class BaseGametype : int {
	FreeForAll = 0,
	Duel = 1,
	Race = 2,
	TeamDeathmatch = 3,
	ClanArena = 4,
	CaptureTheFlag = 5,
	OneFlag = 6,
	Overload = 7,
	Harvester = 8,
	FreezeTag = 9,
	Domination = 10,
	AttackDefend = 11,
	RedRover = 12
};

struct Setting {
	std::string name;
	std::string value;
	// Retail stores a null value pointer for JSON kinds that cannot be formatted
	// as a CVar. That entry still occupies a sorted settings slot and terminates
	// runtime application; FnQL's WebUI serializer stops safely at the same slot.
	bool hasValue = true;
};

struct Definition {
	std::string id;
	std::string title;
	std::optional<std::string> author;
	std::optional<std::string> description;
	BaseGametype baseGametype = BaseGametype::FreeForAll;
	std::vector<std::string> tags;
	std::vector<Setting> settings;
	std::string sourcePath;
	// Retail stores definitions in a fixed 1,024-slot table.  The physical slot
	// matters when a reload preserves the active definition while repopulating
	// every other slot.
	std::size_t registrySlot = 0;
	// A retained active slot remains lookup-visible across reload, but retail's
	// logical enumeration count includes it only when new registration scans
	// beyond that physical slot.
	bool enumerated = true;
};

enum class DiagnosticLevel {
	Warning,
	Error
};

enum class DiagnosticCode {
	InvalidJson,
	InvalidRoot,
	InvalidDefinition,
	InvalidBaseGametype,
	IgnoredOptionalField,
	UnsupportedSettingValue,
	TooManySettings,
	TooManyDefinitions,
	DuplicateId
};

struct Diagnostic {
	DiagnosticLevel level = DiagnosticLevel::Error;
	DiagnosticCode code = DiagnosticCode::InvalidJson;
	std::string sourcePath;
	std::size_t byteOffset = 0;
	std::string message;
};

struct AppendResult {
	bool success = false;
	std::size_t definitionsAdded = 0;
	std::vector<Diagnostic> diagnostics;

	[[nodiscard]] explicit operator bool() const noexcept { return success; }
};

struct SerializeResult {
	bool success = false;
	std::string json;
	std::string error;

	[[nodiscard]] explicit operator bool() const noexcept { return success; }
};

[[nodiscard]] std::optional<BaseGametype> ParseBaseGametype(
	std::string_view token ) noexcept;
[[nodiscard]] std::string_view BaseGametypeToken( BaseGametype gametype ) noexcept;

class Catalog {
public:
	explicit Catalog( std::optional<std::size_t> reservedSlot = std::nullopt ) noexcept;

	// Syntax/root failures and invalid single-definition documents do not change
	// the catalog. In a retail root array, invalid members are diagnosed and
	// skipped while valid siblings retain their source order.
	[[nodiscard]] AppendResult AppendDocument( std::string_view sourcePath,
		std::string_view document );

	void Clear() noexcept;
	// Called after a reload has populated every non-active slot.  The retained
	// definition is inserted at its original physical slot so first-match lookup
	// and WebUI assignment order remain retail-compatible.
	[[nodiscard]] bool InsertRetained( Definition definition );

	[[nodiscard]] const Definition *FindFirst( std::string_view id ) const noexcept;
	[[nodiscard]] const std::vector<Definition> &Definitions() const noexcept {
		return definitions_;
	}
	[[nodiscard]] std::size_t Size() const noexcept { return definitions_.size(); }

	// Matches retail's GetFactoryList object contract. Duplicate ids retain the
	// last descriptor assigned to the WebUI object, while FindFirst deliberately
	// retains the first-match engine lookup contract.
	[[nodiscard]] SerializeResult SerializeWebUi( std::size_t outputByteCap ) const;

private:
	std::vector<Definition> definitions_;
	std::optional<std::size_t> reservedSlot_;
	bool retainedInserted_ = false;
};

} // namespace fnql::server::factory

#endif
