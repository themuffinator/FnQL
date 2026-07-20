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
// sv_factory.cpp -- retail Quake Live factory, arena, and rotation ownership

#include "server.h"
#include "factory_catalog.hpp"
#include "factory_rotation.hpp"

#include <array>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef DEDICATED
extern "C" void CL_WebHost_InvalidateFactoryCatalog( void );
extern "C" void CL_WebHost_InvalidateMapCatalog( void );
#endif

namespace {

namespace factory = fnql::server::factory;
namespace rotation = fnql::server::rotation;

constexpr std::size_t FactoryFileByteLimit = 0x8000;
constexpr std::size_t FactoryFileListBytes = 0x400;

struct ActiveFactory {
	factory::Definition definition;
	std::uint64_t generation = 0;
	std::size_t ordinal = 0;
	bool settingsApplied = false;
};

factory::Catalog factoryCatalog;
rotation::ArenaCatalog arenaCatalog;
std::vector<rotation::RotationEntry> mapPool;
std::optional<ActiveFactory> activeFactory;
std::uint64_t factoryGeneration = 0;
bool factoryRuntimeInitialized = false;

cvar_t *svMapPoolFile = nullptr;
cvar_t *svIncludeCurrentMapInVote = nullptr;
cvar_t *svServerType = nullptr;

enum class TextFileResult {
	Loaded,
	Missing,
	TooLarge,
	ReadFailed
};

TextFileResult ReadTextFile( const char *path, std::size_t byteLimit,
	std::string &text ) {
	void *data = nullptr;
	const int length = FS_FOpenFileRead( path, nullptr, qfalse );

	text.clear();
	if ( length < 0 ) {
		return TextFileResult::Missing;
	}
	if ( static_cast<std::size_t>( length ) >= byteLimit ) {
		return TextFileResult::TooLarge;
	}

	const int readLength = FS_ReadFile( path, &data );
	if ( readLength != length || ( readLength > 0 && !data ) ) {
		if ( data ) {
			FS_FreeFile( data );
		}
		return TextFileResult::ReadFailed;
	}

	if ( readLength > 0 ) {
		text.assign( static_cast<const char *>( data ),
			static_cast<std::size_t>( readLength ) );
	}
	if ( data ) {
		FS_FreeFile( data );
	}
	return TextFileResult::Loaded;
}

void PrintFactoryDiagnostics( const factory::AppendResult &result ) {
	for ( const factory::Diagnostic &diagnostic : result.diagnostics ) {
		const char *color = diagnostic.level == factory::DiagnosticLevel::Error
			? S_COLOR_RED : S_COLOR_YELLOW;
		Com_Printf( "%s%s%s\n", color, diagnostic.message.c_str(), S_COLOR_WHITE );
	}
}

bool AppendFactoryFile( factory::Catalog &catalog, const char *path,
	bool reportMissing ) {
	std::string document;
	const TextFileResult fileResult = ReadTextFile(
		path, FactoryFileByteLimit, document );

	switch ( fileResult ) {
		case TextFileResult::Missing:
			if ( reportMissing ) {
				Com_Printf( S_COLOR_RED "file not found: %s\n" S_COLOR_WHITE, path );
			}
			return false;
		case TextFileResult::TooLarge:
			Com_Printf( S_COLOR_RED "file too large: %s, max allowed is %i\n" S_COLOR_WHITE,
				path, static_cast<int>( FactoryFileByteLimit - 1 ) );
			return false;
		case TextFileResult::ReadFailed:
			Com_Printf( S_COLOR_RED "could not read factories file %s\n" S_COLOR_WHITE,
				path );
			return false;
		case TextFileResult::Loaded:
			break;
	}

	factory::AppendResult append = catalog.AppendDocument( path, document );
	PrintFactoryDiagnostics( append );
	if ( !append ) {
		Com_Printf( S_COLOR_RED "factories file %s was not a valid JSON document."
			S_COLOR_WHITE "\n", path );
		return false;
	}

	Com_Printf( "loaded factories from %s, total %i\n", path,
		static_cast<int>( catalog.Size() ) );
	return true;
}

template <typename Callback>
void ForEachScriptFile( const char *extension, Callback callback ) {
	std::array<char, FactoryFileListBytes> fileList{};
	const int count = FS_GetFileList( "scripts", extension, fileList.data(),
		static_cast<int>( fileList.size() ) );
	const char *cursor = fileList.data();
	const char *const end = fileList.data() + fileList.size();

	for ( int index = 0; index < count && cursor < end && *cursor; ++index ) {
		const void *terminator = memchr( cursor, '\0',
			static_cast<std::size_t>( end - cursor ) );
		if ( !terminator ) {
			Com_Printf( S_COLOR_YELLOW "truncated scripts file list for %s\n",
				extension );
			break;
		}
		const std::size_t nameLength = static_cast<const char *>( terminator ) - cursor;
		if ( nameLength + sizeof( "scripts/" ) <= MAX_QPATH ) {
			std::string path = "scripts/";
			path.append( cursor, nameLength );
			callback( path.c_str() );
		} else {
			Com_Printf( S_COLOR_YELLOW "ignoring overlong scripts filename: %s\n",
				cursor );
		}
		cursor = static_cast<const char *>( terminator ) + 1;
	}
}

void LoadFactoryRegistry() {
	const std::optional<std::size_t> retainedSlot = activeFactory
		? std::optional<std::size_t>( activeFactory->ordinal ) : std::nullopt;
	factory::Catalog replacement( retainedSlot );

	AppendFactoryFile( replacement, "scripts/factories.txt", true );
	ForEachScriptFile( ".factories", [&]( const char *path ) {
		AppendFactoryFile( replacement, path, false );
	} );

	// FnQ3/FnQL briefly documented the singular suffix.  Load it last as a
	// compatibility extension; retail definitions retain first-match lookup.
	ForEachScriptFile( ".factory", [&]( const char *path ) {
		AppendFactoryFile( replacement, path, false );
	} );
	if ( activeFactory && !replacement.InsertRetained( activeFactory->definition ) ) {
		Com_Printf( S_COLOR_RED "could not retain active factory during reload; "
			"keeping the previous registry\n" S_COLOR_WHITE );
		return;
	}

	factoryCatalog = std::move( replacement );
	++factoryGeneration;
	if ( activeFactory ) {
		activeFactory->generation = factoryGeneration;
	}

#ifndef DEDICATED
	CL_WebHost_InvalidateFactoryCatalog();
#endif
}

void PrintArenaDiagnostics( const char *path,
	const std::vector<rotation::Diagnostic> &diagnostics ) {
	for ( const rotation::Diagnostic &diagnostic : diagnostics ) {
		Com_Printf( S_COLOR_YELLOW "%s:%i: ignored invalid arena metadata (%i)"
			S_COLOR_WHITE "\n", path, static_cast<int>( diagnostic.line ),
			static_cast<int>( diagnostic.code ) );
	}
}

void AppendArenaFile( rotation::ArenaCatalog &catalog, const char *path,
	bool reportMissing ) {
	std::string text;
	const TextFileResult fileResult = ReadTextFile(
		path, rotation::MaximumArenaTextBytes, text );

	if ( fileResult == TextFileResult::Missing ) {
		if ( reportMissing ) {
			Com_Printf( S_COLOR_RED "file not found: %s\n" S_COLOR_WHITE, path );
		}
		return;
	}
	if ( fileResult == TextFileResult::TooLarge ) {
		Com_Printf( S_COLOR_RED "file too large: %s, max allowed is %i\n"
			S_COLOR_WHITE, path,
			static_cast<int>( rotation::MaximumArenaTextBytes - 1 ) );
		return;
	}
	if ( fileResult != TextFileResult::Loaded ) {
		Com_Printf( S_COLOR_RED "could not read arena file %s\n" S_COLOR_WHITE,
			path );
		return;
	}

	std::vector<rotation::Diagnostic> diagnostics;
	const rotation::ProcessingReport report =
		rotation::ParseArenaText( text, catalog, &diagnostics );
	PrintArenaDiagnostics( path, diagnostics );
	if ( !report ) {
		Com_Printf( S_COLOR_YELLOW "%s reached a retail arena parsing limit; "
			"retaining %i valid records\n" S_COLOR_WHITE, path,
			static_cast<int>( report.accepted ) );
	}
}

void LoadArenaRegistry() {
	rotation::ArenaCatalog replacement;
	AppendArenaFile( replacement, "scripts/arenas.txt", true );
	ForEachScriptFile( ".arena", [&]( const char *path ) {
		AppendArenaFile( replacement, path, false );
	} );
	arenaCatalog = std::move( replacement );
}

bool MapFileExists( std::string_view mapName ) {
	if ( mapName.empty() || mapName.size() >= MAX_QPATH ) {
		return false;
	}

	std::array<char, MAX_QPATH> path{};
	Com_sprintf( path.data(), static_cast<int>( path.size() ), "maps/%.*s.bsp",
		static_cast<int>( mapName.size() ), mapName.data() );
	FS_BypassPure();
	const int length = FS_FOpenFileRead( path.data(), nullptr, qfalse );
	FS_RestorePure();
	return length >= 0;
}

rotation::RotationResolver BuildRotationResolver() {
	rotation::RotationResolver resolver;
	resolver.map = []( std::string_view requested )
		-> std::optional<rotation::MapMetadata> {
		// Retail's content-build mode accepts rotation rows before their BSP is
		// present; normal hosts still validate the mounted map immediately.
		if ( !Cvar_VariableIntegerValue( "com_build" ) &&
			!Cvar_VariableIntegerValue( "com_buildScript" ) &&
			!MapFileExists( requested ) ) {
			return std::nullopt;
		}
		rotation::MapMetadata metadata;
		metadata.map.assign( requested );
		const rotation::ArenaRecord *arena = arenaCatalog.Find( requested );
		metadata.title = arena && !arena->longName.empty()
			? arena->longName : metadata.map;
		return metadata;
	};
	resolver.factory = []( std::string_view requested )
		-> std::optional<rotation::FactoryMetadata> {
		const factory::Definition *definition = factoryCatalog.FindFirst( requested );
		if ( !definition ) {
			return std::nullopt;
		}
		rotation::FactoryMetadata metadata;
		metadata.factory = definition->id;
		metadata.title = definition->title;
		metadata.baseGameType = static_cast<int>( definition->baseGametype );
		metadata.hidden = !definition->id.empty() && definition->id.front() == '_';
		return metadata;
	};
	resolver.arenaSupport = []( std::string_view map, int baseGameType ) {
		return arenaCatalog.SupportFor( map, baseGameType );
	};
	return resolver;
}

void PrintMapPoolDiagnostics( const std::vector<rotation::Diagnostic> &diagnostics ) {
	for ( const rotation::Diagnostic &diagnostic : diagnostics ) {
		const int line = static_cast<int>( diagnostic.line );
		switch ( diagnostic.code ) {
			case rotation::DiagnosticCode::MapNotFound:
				Com_Printf( S_COLOR_RED "map doesn't exist, skipping line %i\n"
					S_COLOR_WHITE, line );
				break;
			case rotation::DiagnosticCode::FactoryNotFound:
			case rotation::DiagnosticCode::FactoryHidden:
				Com_Printf( S_COLOR_RED "invalid factory found in rotation, skipping line %i\n"
					S_COLOR_WHITE, line );
				break;
			case rotation::DiagnosticCode::ArenaUnsupported:
				Com_Printf( S_COLOR_RED "map isn't valid for factory gametype, skipping line %i\n"
					S_COLOR_WHITE, line );
				break;
			case rotation::DiagnosticCode::ArenaUnknown:
				// Unknown custom maps retain FnQL's permissive legacy path.
				break;
			default:
				Com_Printf( S_COLOR_RED "map rotation item is invalid, skipping line %i (%i)\n"
					S_COLOR_WHITE, line, static_cast<int>( diagnostic.code ) );
				break;
		}
	}
}

void LoadMapPool() {
	std::vector<rotation::MapPoolRow> rows;
	std::vector<rotation::RotationEntry> replacement;
	std::vector<rotation::Diagnostic> diagnostics;
	std::string text;
	const char *path = svMapPoolFile && svMapPoolFile->string[0]
		? svMapPoolFile->string : "mappool.txt";
	const int length = FS_FOpenFileRead( path, nullptr, qfalse );

#ifndef DEDICATED
	CL_WebHost_InvalidateMapCatalog();
#endif

	if ( length < 0 ) {
		Com_Printf( S_COLOR_RED "rotation file not found: %s\n" S_COLOR_WHITE,
			path );
		mapPool.clear();
		if ( svMapPoolFile ) {
			svMapPoolFile->modified = qfalse;
		}
		return;
	}
	if ( static_cast<std::size_t>( length ) >= rotation::MaximumMapPoolTextBytes ) {
		Com_Printf( S_COLOR_RED "rotations file too large: %s is %i, max allowed is %i"
			S_COLOR_WHITE "\n", path, length,
			static_cast<int>( rotation::MaximumMapPoolTextBytes - 1 ) );
		mapPool.clear();
		return;
	}
	if ( ReadTextFile( path, rotation::MaximumMapPoolTextBytes, text )
		!= TextFileResult::Loaded ) {
		Com_Printf( S_COLOR_RED "could not read rotation file: %s\n" S_COLOR_WHITE,
			path );
		mapPool.clear();
		return;
	}

	const rotation::ProcessingReport parseReport =
		rotation::ParseMapPoolText( text, rows, &diagnostics );
	const rotation::ProcessingReport resolveReport = rotation::ResolveMapPool(
		rows, BuildRotationResolver(), replacement, &diagnostics );
	PrintMapPoolDiagnostics( diagnostics );
	if ( !parseReport || !resolveReport ) {
		Com_Printf( S_COLOR_YELLOW "map pool %s reached a compatibility limit; "
			"retaining %i valid entries\n" S_COLOR_WHITE, path,
			static_cast<int>( replacement.size() ) );
	}
	mapPool = std::move( replacement );
	if ( svMapPoolFile ) {
		svMapPoolFile->modified = qfalse;
	}
	Com_Printf( "loaded %i maps into the map pool\n",
		static_cast<int>( mapPool.size() ) );
}

std::size_t RuntimeRandomIndex() {
	// Retail combines two process-global rand() draws with the current engine
	// millisecond counter.  Perform the shift in unsigned space to avoid the
	// signed-overflow UB present in a literal transcription.
	const std::uint32_t upper = static_cast<std::uint32_t>( std::rand() ) << 16u;
	const std::uint32_t lower = static_cast<std::uint32_t>( std::rand() );
	const std::uint32_t mixed = upper ^ lower ^
		static_cast<std::uint32_t>( Com_Milliseconds() );
	// Retail normalizes the signed result with (x ^ sign) - sign. Express the
	// INT_MIN case in unsigned arithmetic so the result is defined everywhere.
	const std::uint32_t normalized = ( mixed & 0x80000000u )
		? 0u - mixed : mixed;
	return static_cast<std::size_t>( normalized );
}

std::optional<rotation::RotationEntry> FallbackRotationEntry() {
	rotation::RotationEntry entry;
	entry.requestedMap = entry.map = "campgrounds";
	entry.mapTitle = "campgrounds";
	return entry;
}

std::optional<rotation::RotationEntry> SelectStartRotationEntry() {
	if ( mapPool.empty() ) {
		return FallbackRotationEntry();
	}
	const std::optional<std::size_t> selected = rotation::SelectRotationIndex(
		mapPool, RuntimeRandomIndex(), {}, false );
	return selected ? std::optional<rotation::RotationEntry>( mapPool[*selected] )
		: std::nullopt;
}

void SetRotationInfoEntry( char *nextMaps, int slot,
	const rotation::RotationEntry &entry ) {
	Info_SetValueForKey( nextMaps, va( "map_%i", slot ), entry.map.c_str() );
	Info_SetValueForKey( nextMaps, va( "title_%i", slot ), entry.mapTitle.c_str() );
	Info_SetValueForKey( nextMaps, va( "cfg_%i", slot ), entry.factory.c_str() );
	Info_SetValueForKey( nextMaps, va( "gt_%i", slot ), entry.factoryTitle.c_str() );
}

void UpdateRotationCvars() {
	const char *currentMap = Cvar_VariableString( "mapname" );

	if ( mapPool.empty() ) {
		Cvar_Set( "nextmap", "map_restart 0" );
	} else {
		const std::optional<std::size_t> selected = rotation::SelectRotationIndex(
			mapPool, RuntimeRandomIndex(), {}, false );
		if ( selected ) {
			const rotation::RotationEntry &next = mapPool[*selected];
			Cvar_Set( "nextmap", va( "map %s %s", next.map.c_str(),
				next.factory.c_str() ) );
		} else {
			Cvar_Set( "nextmap", "map_restart 0" );
		}
	}

	std::array<char, BIG_INFO_STRING> nextMaps{};
	int slot = 0;

	// Retail prepends the running map when arena metadata resolves, regardless
	// of whether that map/factory pair appears in the pool. Pool selection does
	// not de-duplicate it.
	if ( svIncludeCurrentMapInVote && svIncludeCurrentMapInVote->integer &&
		activeFactory ) {
		const rotation::ArenaRecord *arena = arenaCatalog.Find( currentMap );
		if ( arena ) {
			rotation::RotationEntry current;
			current.map = currentMap;
			current.mapTitle = arena->longName.empty() ? current.map : arena->longName;
			current.factory = activeFactory->definition.id;
			current.factoryTitle = activeFactory->definition.title;
			SetRotationInfoEntry( nextMaps.data(), slot++, current );
		}
	}

	rotation::RotationModel model = rotation::BuildRotationModel(
		mapPool, static_cast<std::size_t>( slot ), []() {
			return RuntimeRandomIndex();
		} );

	for ( std::size_t modelSlot = 0;
		modelSlot < model.count && slot < static_cast<int>( rotation::RotationSlotCount );
		++modelSlot ) {
		const std::size_t index = model.indices[modelSlot];
		if ( index == rotation::InvalidRotationIndex || index >= mapPool.size() ) {
			continue;
		}
		SetRotationInfoEntry( nextMaps.data(), slot++, mapPool[index] );
	}
	Cvar_Set( "nextmaps", nextMaps.data() );
}

void RestoreActiveFactorySettings() {
	if ( !activeFactory || !activeFactory->settingsApplied ) {
		return;
	}
	for ( const factory::Setting &setting : activeFactory->definition.settings ) {
		Cvar_RestoreFactoryValue( setting.name.c_str() );
	}
	activeFactory->settingsApplied = false;
}

bool FactoryIsHidden( const factory::Definition &definition ) {
	return !definition.id.empty() && definition.id.front() == '_';
}

} // namespace

void SV_FactoryInit( void ) {
	if ( factoryRuntimeInitialized ) {
		return;
	}
	factoryRuntimeInitialized = true;

	LoadArenaRegistry();
	LoadFactoryRegistry();
	svMapPoolFile = Cvar_Get( "sv_mapPoolFile", "mappool.txt", CVAR_ARCHIVE );
	svIncludeCurrentMapInVote = Cvar_Get( "sv_includeCurrentMapInVote", "0",
		CVAR_TEMP );
	svServerType = Cvar_Get( "sv_serverType", "0", CVAR_ARCHIVE );
	Cvar_SetDescription( svMapPoolFile,
		"Map-pool file containing retail map|factory rotation entries." );
	Cvar_SetDescription( svIncludeCurrentMapInVote,
		"Include the current map and factory in the three nextmaps vote slots." );
	Cvar_SetDescription( svServerType,
		"Retail server visibility type: 0 offline, 1 LAN, 2 Internet." );
	LoadMapPool();
}

void SV_FactoryShutdown( void ) {
	RestoreActiveFactorySettings();
}

void SV_FactoryDeactivate( void ) {
	RestoreActiveFactorySettings();
	activeFactory.reset();
	Cvar_Set( "g_factory", "" );
	Cvar_Set( "g_factoryTitle", "" );
}

qboolean SV_FactoryExists( const char *factoryName ) {
	if ( !factoryRuntimeInitialized ) {
		SV_FactoryInit();
	}
	if ( !factoryName || !factoryName[0] ) {
		return qfalse;
	}
	return factoryCatalog.FindFirst( factoryName ) ? qtrue : qfalse;
}

qboolean SV_FactoryHasActive( void ) {
	if ( !factoryRuntimeInitialized ) {
		SV_FactoryInit();
	}
	return activeFactory ? qtrue : qfalse;
}

void SV_FactoryPrintMapUsage( const char *commandName ) {
	Com_Printf( "%s (map) (factory)\n",
		commandName && commandName[0] ? commandName : "map" );
	Com_Printf( "Valid factories: " );
	for ( const factory::Definition &definition : factoryCatalog.Definitions() ) {
		if ( definition.enumerated && !FactoryIsHidden( definition ) ) {
			Com_Printf( "%s ", definition.id.c_str() );
		}
	}
	Com_Printf( "\n" );
}

qboolean SV_FactoryPrepareMap( const char *mapName, const char *factoryName,
	qboolean factoryArgumentPresent, qboolean developerMap ) {
	if ( !factoryRuntimeInitialized ) {
		SV_FactoryInit();
	}

	const factory::Definition *selected = nullptr;
	std::size_t selectedOrdinal = 0;
	std::uint64_t selectedGeneration = factoryGeneration;
	const bool explicitFactory = ( factoryName && factoryName[0] ) ||
		( !activeFactory && factoryArgumentPresent );

	if ( explicitFactory ) {
		selected = factoryCatalog.FindFirst( factoryName ? factoryName : "" );
		if ( selected ) {
			selectedOrdinal = selected->registrySlot;
		}
	} else if ( activeFactory ) {
		selected = &activeFactory->definition;
		selectedOrdinal = activeFactory->ordinal;
		selectedGeneration = activeFactory->generation;
	} else {
		SV_FactoryPrintMapUsage( Cmd_Argv( 0 ) );
		return qfalse;
	}

	if ( !selected ) {
		Com_Printf( "Invalid factory specified.\n" );
		return qfalse;
	}
	if ( FactoryIsHidden( *selected ) && svServerType && svServerType->integer > 0 ) {
		SV_FactoryPrintMapUsage( Cmd_Argv( 0 ) );
		return qfalse;
	}

	const rotation::ArenaSupport support = arenaCatalog.SupportFor(
		mapName ? mapName : "", static_cast<int>( selected->baseGametype ) );
	// Retail's compatibility bypass is keyed to an explicitly supplied hidden
	// id. Reusing an already-active hidden factory does not broaden it.
	const bool explicitHiddenFactory = explicitFactory && factoryName &&
		factoryName[0] == '_';
	if ( !developerMap && !explicitHiddenFactory &&
		support == rotation::ArenaSupport::Unsupported ) {
		Com_Printf( "Map not supported for this gametype.\n" );
		return qfalse;
	}

	const bool selectionChanged = !activeFactory ||
		activeFactory->generation != selectedGeneration ||
		activeFactory->ordinal != selectedOrdinal;
	if ( selectionChanged || !activeFactory->settingsApplied ) {
		factory::Definition selectedCopy = *selected;
		RestoreActiveFactorySettings();
		for ( const factory::Setting &setting : selectedCopy.settings ) {
			if ( !setting.hasValue ) {
				break;
			}
			Cvar_SaveFactoryValue( setting.name.c_str() );
			Cvar_Set( setting.name.c_str(), setting.value.c_str() );
		}
		Cvar_SetIntegerValue( "g_gametype",
			static_cast<int>( selectedCopy.baseGametype ) );
		Cvar_Set( "g_factory", selectedCopy.id.c_str() );
		Cvar_Set( "g_factoryTitle", selectedCopy.title.c_str() );
		activeFactory = ActiveFactory{
			std::move( selectedCopy ), selectedGeneration, selectedOrdinal, true
		};
	}

	return qtrue;
}

qboolean SV_FactoryBuildWebCatalogJson( char *buffer, int bufferSize ) {
	if ( !buffer || bufferSize < 3 ) {
		return qfalse;
	}
	if ( !factoryRuntimeInitialized ) {
		SV_FactoryInit();
	}

	const factory::SerializeResult serialized = factoryCatalog.SerializeWebUi(
		static_cast<std::size_t>( bufferSize - 1 ) );
	if ( !serialized || serialized.json.size() >= static_cast<std::size_t>( bufferSize ) ) {
		Q_strncpyz( buffer, "{}", bufferSize );
		return qfalse;
	}
	memcpy( buffer, serialized.json.data(), serialized.json.size() );
	buffer[serialized.json.size()] = '\0';
	return qtrue;
}

int SV_FactoryWebCatalogJsonSize( void ) {
	if ( !factoryRuntimeInitialized ) {
		SV_FactoryInit();
	}
	const factory::SerializeResult serialized = factoryCatalog.SerializeWebUi(
		static_cast<std::size_t>( ( std::numeric_limits<int>::max )() - 1 ) );
	if ( !serialized || serialized.json.size() >=
		static_cast<std::size_t>( ( std::numeric_limits<int>::max )() ) ) {
		return 0;
	}
	return static_cast<int>( serialized.json.size() + 1 );
}

qboolean SV_MapPoolBuildWebCatalogJson( char *buffer, int bufferSize ) {
	if ( !buffer || bufferSize < 3 ) {
		return qfalse;
	}
	if ( !factoryRuntimeInitialized ) {
		SV_FactoryInit();
	}
	const rotation::WebMapCatalogResult serialized =
		rotation::SerializeWebMapCatalog( mapPool,
			static_cast<std::size_t>( bufferSize - 1 ) );
	if ( !serialized || serialized.json.size() >=
		static_cast<std::size_t>( bufferSize ) ) {
		Q_strncpyz( buffer, "[]", bufferSize );
		return qfalse;
	}
	memcpy( buffer, serialized.json.data(), serialized.json.size() );
	buffer[serialized.json.size()] = '\0';
	return qtrue;
}

int SV_MapPoolWebCatalogJsonSize( void ) {
	if ( !factoryRuntimeInitialized ) {
		SV_FactoryInit();
	}
	const rotation::WebMapCatalogResult serialized =
		rotation::SerializeWebMapCatalog( mapPool,
			static_cast<std::size_t>( ( std::numeric_limits<int>::max )() - 1 ) );
	if ( !serialized || serialized.json.size() >=
		static_cast<std::size_t>( ( std::numeric_limits<int>::max )() ) ) {
		return 0;
	}
	return static_cast<int>( serialized.json.size() + 1 );
}

void SV_FactoryRefreshMountedContent( void ) {
	if ( !factoryRuntimeInitialized ) {
		return;
	}
	LoadArenaRegistry();
	LoadFactoryRegistry();
	LoadMapPool();
}

void SV_FactoryReload_f( void ) {
	if ( activeFactory ) {
		Com_Printf( "not clearing currently loaded factory id %s, continuing\n",
			activeFactory->definition.id.c_str() );
	}
	LoadFactoryRegistry();
}

void SV_ArenaReload_f( void ) {
	Com_Printf( "reloading arena definitions...\n" );
	LoadArenaRegistry();
}

void SV_MapPoolReload_f( void ) {
	Com_Printf( "reloading map pool...\n" );
	LoadMapPool();
}

void SV_StartRandomMap_f( void ) {
	if ( !factoryRuntimeInitialized ) {
		SV_FactoryInit();
	}

	const std::optional<rotation::RotationEntry> selected =
		SelectStartRotationEntry();
	if ( !selected ) {
		Com_Printf( S_COLOR_RED "No valid map and factory are available.\n"
			S_COLOR_WHITE );
		return;
	}
	Cbuf_AddText( va( "map %s %s\n", selected->map.c_str(),
		selected->factory.c_str() ) );
}

void SV_MapPoolRefreshCvars( void ) {
	if ( !factoryRuntimeInitialized ) {
		return;
	}
	UpdateRotationCvars();
}
