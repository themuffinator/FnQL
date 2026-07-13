#include "factory_rotation.hpp"

#include <array>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rotation = fnql::server::rotation;

namespace {

#define CHECK( expression ) \
	do { \
		if ( !( expression ) ) { \
			std::cerr << __func__ << ':' << __LINE__ \
				<< ": check failed: " #expression "\n"; \
			return false; \
		} \
	} while ( false )

bool HasDiagnostic( const std::vector<rotation::Diagnostic> &diagnostics,
		rotation::DiagnosticCode code ) {
	for ( const rotation::Diagnostic &diagnostic : diagnostics ) {
		if ( diagnostic.code == code ) {
			return true;
		}
	}
	return false;
}

bool ParsesRetailShapedArenaBlocksAndAppendsDeterministically() {
	rotation::ArenaCatalog catalog;
	std::vector<rotation::Diagnostic> diagnostics;
	const std::string_view first = R"arena(
// Quoted keys and values remain valid legacy arena syntax.
{
  "map" "campgrounds"
  longname "Campgrounds"
  type "ffa duel tdm ca"
  ignored "metadata"
}
/* Only the two COM_Parse comment forms are skipped. */
{
  map bloodrun
  longname "Blood Run"
  type tourney
}
)arena";

	const rotation::ProcessingReport firstReport =
		rotation::ParseArenaText( first, catalog, &diagnostics );
	CHECK( firstReport );
	CHECK( firstReport.accepted == 2 );
	CHECK( firstReport.rejected == 0 );
	CHECK( diagnostics.empty() );
	CHECK( catalog.Records().size() == 2 );
	CHECK( catalog.Records()[0].map == "campgrounds" );
	CHECK( catalog.Records()[0].longName == "Campgrounds" );
	CHECK( catalog.Records()[0].type == "ffa duel tdm ca" );
	CHECK( catalog.Records()[1].map == "bloodrun" );

	const rotation::ProcessingReport appended = rotation::ParseArenaText(
		"/* another source file */ { map silentnight longname \"Silent Night\nArena\" type race }",
		catalog, &diagnostics );
	CHECK( appended );
	CHECK( appended.accepted == 1 );
	CHECK( catalog.Records().size() == 3 );
	CHECK( catalog.Records()[2].map == "silentnight" );
	CHECK( catalog.Records()[2].longName == "Silent Night\nArena" );
	CHECK( rotation::ParseArenaText(
		"{ map literal_backslash longname \"A\\\\B\" type ffa }", catalog ) );
	CHECK( catalog.Records().back().longName.empty() );

	rotation::ArenaCatalog hashCatalog;
	diagnostics.clear();
	const rotation::ProcessingReport hashStopsSource = rotation::ParseArenaText(
		"# ordinary-token\n{ map never_seen type ffa }", hashCatalog, &diagnostics );
	CHECK( hashStopsSource && hashStopsSource.accepted == 0 &&
		hashStopsSource.rejected == 1 );
	CHECK( hashCatalog.Records().empty() );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::ArenaUnexpectedToken ) );

	rotation::ArenaCatalog nulCatalog;
	std::string nulText = "{ map before_nul type ffa }";
	nulText.push_back( '\0' );
	nulText += "{ map after_nul type ffa }";
	CHECK( rotation::ParseArenaText( nulText, nulCatalog ) );
	CHECK( nulCatalog.Records().size() == 1 &&
		nulCatalog.Records()[0].map == "before_nul" );
	return true;
}

bool FindsFirstExactArenaRecordCaseSensitively() {
	rotation::ArenaCatalog catalog;
	CHECK( rotation::ParseArenaText(
		"{ map CaseMap longname First type ffa } "
		"{ map casemap longname Second type duel } "
		"{ map CaseMap longname Third type race }", catalog ) );

	const rotation::ArenaRecord *record = catalog.Find( "CaseMap" );
	CHECK( record && record->longName == "First" );
	record = catalog.Find( "casemap" );
	CHECK( record && record->longName == "Second" );
	CHECK( catalog.Find( "CASEMAP" ) == nullptr );
	CHECK( catalog.Find( "missing" ) == nullptr );
	return true;
}

bool MapsRetailBaseGameTypesToCaseSensitiveSubstrings() {
	rotation::ArenaCatalog catalog;
	CHECK( rotation::ParseArenaText(
		"{ map alltypes type \"ffa duel race tdm ca ctf oneflag overload har ft dom ad rr\" } "
		"{ map reconstructed_aliases type \"tourney hh\" } "
		"{ map substrings type \"xduely ctfextra harvester\" } "
		"{ map uppercase type \"FFA DUEL HAR\" } "
		"{ map unclassified longname \"No type metadata\" }", catalog ) );

	for ( int baseGameType = rotation::MinimumRetailBaseGameType;
			baseGameType <= rotation::MaximumRetailBaseGameType; ++baseGameType ) {
		CHECK( catalog.SupportFor( "alltypes", baseGameType ) ==
			rotation::ArenaSupport::Supported );
	}
	CHECK( catalog.SupportFor( "reconstructed_aliases", 1 ) ==
		rotation::ArenaSupport::Unsupported );
	CHECK( catalog.SupportFor( "reconstructed_aliases", 8 ) ==
		rotation::ArenaSupport::Unsupported );
	CHECK( catalog.SupportFor( "substrings", 1 ) ==
		rotation::ArenaSupport::Supported );
	CHECK( catalog.SupportFor( "substrings", 5 ) ==
		rotation::ArenaSupport::Supported );
	CHECK( catalog.SupportFor( "substrings", 8 ) ==
		rotation::ArenaSupport::Supported );
	CHECK( catalog.SupportFor( "uppercase", 0 ) ==
		rotation::ArenaSupport::Unsupported );
	CHECK( catalog.SupportFor( "uppercase", 1 ) ==
		rotation::ArenaSupport::Unsupported );
	CHECK( catalog.SupportFor( "uppercase", 8 ) ==
		rotation::ArenaSupport::Unsupported );
	CHECK( catalog.SupportFor( "unclassified", 0 ) ==
		rotation::ArenaSupport::Unsupported );
	CHECK( catalog.SupportFor( "missing", 0 ) == rotation::ArenaSupport::Unknown );
	CHECK( catalog.SupportFor( "alltypes", 13 ) == rotation::ArenaSupport::Unsupported );
	return true;
}

bool MatchesRetailLossyArenaInfoAndSourceStopRules() {
	rotation::ArenaCatalog catalog;
	std::vector<rotation::Diagnostic> diagnostics;
	const rotation::ProcessingReport report = rotation::ParseArenaText(
		"{ longname \"Missing map\" type ffa } "
		"{ map bad/path type ffa }", catalog, &diagnostics );
	CHECK( report && report.accepted == 1 && report.rejected == 1 );
	CHECK( catalog.Records().size() == 1 &&
		catalog.Records()[0].map == "bad/path" );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::ArenaMissingMap ) );

	diagnostics.clear();
	const std::string overlong( rotation::MaximumArenaTokenBytes + 1, 'x' );
	const rotation::ProcessingReport lossy = rotation::ParseArenaText(
		"{ map retained longname \"" + overlong + "\" type ffa } "
		"{ map " + overlong + " type duel } "
		"{ map recovered type race }", catalog, &diagnostics );
	CHECK( lossy && lossy.accepted == 2 && lossy.rejected == 1 );
	CHECK( catalog.Records()[1].map == "retained" );
	CHECK( catalog.Records()[1].longName.empty() );
	CHECK( catalog.Records()[1].type == "ffa" );
	CHECK( catalog.Records()[2].map == "recovered" );

	diagnostics.clear();
	const rotation::ProcessingReport unterminated = rotation::ParseArenaText(
		"{ map incomplete type ffa", catalog, &diagnostics );
	CHECK( unterminated && unterminated.accepted == 1 );
	CHECK( catalog.Records().back().map == "incomplete" );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::ArenaUnterminatedBlock ) );

	rotation::ArenaCatalog aggregateCatalog;
	std::string aggregate = "{ map first ";
	for ( int i = 0; i < 90; ++i ) {
		aggregate += "key" + std::to_string( i ) + " value" +
			std::to_string( i ) + " ";
	}
	aggregate += "type ffa longname Fits }";
	CHECK( rotation::ParseArenaText( aggregate, aggregateCatalog ) );
	CHECK( aggregateCatalog.Records().size() == 1 );
	CHECK( aggregateCatalog.Records()[0].map == "first" );
	// Arbitrary pairs crowd the 0x400 info string; later short pairs are kept
	// only when enough aggregate room remains.
	CHECK( aggregateCatalog.Records()[0].type.empty() ||
		aggregateCatalog.Records()[0].type == "ffa" );
	return true;
}

bool EnforcesArenaInputAndRecordLimitsWithoutPartialOverflow() {
	rotation::ArenaCatalog catalog;
	std::vector<rotation::Diagnostic> diagnostics;
	const std::string maximumAccepted( rotation::MaximumArenaTextBytes - 1, ' ' );
	const rotation::ProcessingReport acceptedBoundary =
		rotation::ParseArenaText( maximumAccepted, catalog, &diagnostics );
	CHECK( acceptedBoundary && acceptedBoundary.accepted == 0 );
	CHECK( diagnostics.empty() );

	const std::string oversized( rotation::MaximumArenaTextBytes, ' ' );
	const rotation::ProcessingReport inputLimit =
		rotation::ParseArenaText( oversized, catalog, &diagnostics );
	CHECK( !inputLimit );
	CHECK( inputLimit.fatal && inputLimit.accepted == 0 );
	CHECK( catalog.Records().empty() );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::ArenaInputTooLarge ) );

	diagnostics.clear();
	for ( std::size_t i = 0; i < rotation::MaximumArenaRecords; ++i ) {
		const std::string record =
			"{ map m" + std::to_string( i ) + " type ffa }";
		const rotation::ProcessingReport appended =
			rotation::ParseArenaText( record, catalog, &diagnostics );
		CHECK( appended && appended.accepted == 1 && appended.rejected == 0 );
	}
	diagnostics.clear();
	const rotation::ProcessingReport recordLimit =
		rotation::ParseArenaText( "{ map overflow type ffa }", catalog, &diagnostics );
	CHECK( !recordLimit );
	CHECK( recordLimit.accepted == 0 );
	CHECK( recordLimit.rejected == 1 );
	CHECK( catalog.Records().size() == rotation::MaximumArenaRecords );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::ArenaTooManyRecords ) );
	return true;
}

bool TruncatesArenaTokensAndCountsIntermediateBlocksLikeRetail() {
	rotation::ArenaCatalog catalog;
	std::vector<rotation::Diagnostic> diagnostics;
	const std::string maximumToken( rotation::MaximumArenaTokenBytes, 't' );
	CHECK( rotation::ParseArenaText(
		"{ map exact_token ignored \"" + maximumToken + "\" type ffa }",
		catalog, &diagnostics ) );
	CHECK( catalog.Records().size() == 1 &&
		catalog.Records()[0].map == "exact_token" );

	// A token longer than COM_Parse's output is consumed in full, but only its
	// first 1,023 bytes reach Info_SetValueForKey. The oversized pair is omitted,
	// not treated as a structural parser error.
	diagnostics.clear();
	CHECK( rotation::ParseArenaText(
		"{ map overlong_token ignored \"" + maximumToken + "tail\" type duel }",
		catalog, &diagnostics ) );
	CHECK( catalog.Records().back().map == "overlong_token" );
	CHECK( catalog.Records().back().type == "duel" );
	CHECK( !HasDiagnostic( diagnostics, rotation::DiagnosticCode::ArenaTokenTooLong ) );

	rotation::ArenaCatalog quotaCatalog;
	std::string quotaText;
	for ( std::size_t i = 0; i < rotation::MaximumArenaRecords; ++i ) {
		quotaText += "{ } ";
	}
	quotaText += "{ map suppressed type ffa }";
	CHECK( quotaText.size() < rotation::MaximumArenaTextBytes );
	const rotation::ProcessingReport quota = rotation::ParseArenaText(
		quotaText, quotaCatalog, &diagnostics );
	CHECK( !quota && quota.accepted == 0 );
	CHECK( quotaCatalog.Records().empty() );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::ArenaTooManyRecords ) );
	return true;
}

bool ParsesBoundedMapPoolsAndDiagnosesMalformedRows() {
	const std::string_view text =
		"# real comment\r\n"
		"\r\n"
		"  # literal map|factory\r\n"
		" map | ca \r\n"
		"sub/dir|mode|v2\n"
		"missing-separator\n"
		"|ffa\n"
		"map|\n";
	std::vector<rotation::MapPoolRow> rows;
	std::vector<rotation::Diagnostic> diagnostics;
	const rotation::ProcessingReport report =
		rotation::ParseMapPoolText( text, rows, &diagnostics );
	CHECK( report );
	CHECK( report.accepted == 5 );
	CHECK( report.rejected == 1 );
	CHECK( rows.size() == 5 );
	CHECK( rows[0].map == "  # literal map" && rows[0].factory == "factory" );
	CHECK( rows[0].sourceLine == 3 );
	CHECK( rows[1].map == " map " && rows[1].factory == " ca " );
	CHECK( rows[1].sourceLine == 4 );
	CHECK( rows[2].map == "sub/dir" && rows[2].factory == "mode|v2" );
	CHECK( rows[3].map.empty() && rows[3].factory == "ffa" );
	CHECK( rows[4].map == "map" && rows[4].factory.empty() );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::MapPoolMalformedRow ) );
	CHECK( !rotation::IdentifierIsValid( "" ) );
	CHECK( rotation::IdentifierIsValid( "bad..map" ) );
	CHECK( rotation::IdentifierIsValid( "sub/dir with spaces|and punctuation" ) );
	CHECK( rotation::IdentifierIsValid( "_training" ) );
	return true;
}

bool EnforcesMapPoolFileAndRowLimits() {
	std::vector<rotation::MapPoolRow> rows{{ "stale", "stale", 99 }};
	std::vector<rotation::Diagnostic> diagnostics;
	const std::string oversized( rotation::MaximumMapPoolTextBytes, ' ' );
	const rotation::ProcessingReport inputLimit =
		rotation::ParseMapPoolText( oversized, rows, &diagnostics );
	CHECK( !inputLimit );
	CHECK( rows.empty() );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::MapPoolInputTooLarge ) );

	diagnostics.clear();
	const std::string maximumComment = "#" +
		std::string( rotation::MaximumMapPoolTextBytes - 2, 'x' );
	const rotation::ProcessingReport lineAtFileBoundary =
		rotation::ParseMapPoolText( maximumComment, rows, &diagnostics );
	CHECK( lineAtFileBoundary && rows.empty() && diagnostics.empty() );

	const std::string newlineComment = "#" + std::string( 1023, 'x' );
	diagnostics.clear();
	const rotation::ProcessingReport crlfBoundary = rotation::ParseMapPoolText(
		newlineComment + "\r\nfirst|ffa\rsecond|ffa\n", rows, &diagnostics );
	CHECK( crlfBoundary );
	CHECK( crlfBoundary.accepted == 1 && crlfBoundary.rejected == 0 );
	CHECK( rows[0].sourceLine == 2 && rows[0].map == "first" );

	std::string nulTerminated = "before|ffa\n";
	nulTerminated.push_back( '\0' );
	nulTerminated += "after|duel\n";
	diagnostics.clear();
	CHECK( rotation::ParseMapPoolText( nulTerminated, rows, &diagnostics ) );
	CHECK( rows.size() == 1 && rows[0].map == "before" );

	std::string manyRows;
	for ( std::size_t i = 0; i <= rotation::MaximumMapPoolRows; ++i ) {
		manyRows += "m|f\n";
	}
	CHECK( manyRows.size() < rotation::MaximumMapPoolTextBytes );
	diagnostics.clear();
	const rotation::ProcessingReport parsedRows =
		rotation::ParseMapPoolText( manyRows, rows, &diagnostics );
	CHECK( parsedRows );
	CHECK( parsedRows.accepted == rotation::MaximumMapPoolRows + 1 );
	CHECK( rows.size() == rotation::MaximumMapPoolRows + 1 );
	CHECK( !HasDiagnostic( diagnostics, rotation::DiagnosticCode::MapPoolTooManyRows ) );
	return true;
}

bool ResolvesAliasesAndRejectsUnavailableOrIncompatibleRows() {
	std::vector<rotation::MapPoolRow> rows;
	CHECK( rotation::ParseMapPoolText(
		"aliasmap|duelalias\n"
		"missing|ffa\n"
		"hiddenmap|hidden\n"
		"unknownfactorymap|notthere\n"
		"unsupported|ffa\n"
		"unknownarena|ffa\n"
		"valid|ffa\n"
		"badgt|badgt\n", rows ) );

	rotation::RotationResolver resolver;
	resolver.map = []( std::string_view name ) -> std::optional<rotation::MapMetadata> {
		if ( name == "missing" ) {
			return std::nullopt;
		}
		if ( name == "aliasmap" ) {
			return rotation::MapMetadata{ "campgrounds", "Campgrounds" };
		}
		return rotation::MapMetadata{
			std::string( name ), std::string( name ) + " title" };
	};
	resolver.factory = []( std::string_view name )
			-> std::optional<rotation::FactoryMetadata> {
		if ( name == "notthere" ) {
			return std::nullopt;
		}
		if ( name == "duelalias" ) {
			return rotation::FactoryMetadata{ "duel", "Duel", 1, false };
		}
		if ( name == "hidden" ) {
			return rotation::FactoryMetadata{ "_training", "Training", 0, true };
		}
		if ( name == "badgt" ) {
			return rotation::FactoryMetadata{ "badgt", "Bad", 13, false };
		}
		return rotation::FactoryMetadata{ "ffa", "Free For All", 0, false };
	};
	resolver.arenaSupport = []( std::string_view map, int ) {
		if ( map == "unsupported" ) {
			return rotation::ArenaSupport::Unsupported;
		}
		if ( map == "unknownarena" ) {
			return rotation::ArenaSupport::Unknown;
		}
		return rotation::ArenaSupport::Supported;
	};

	std::vector<rotation::RotationEntry> entries( 1 );
	entries[0].map = "stale";
	std::vector<rotation::Diagnostic> diagnostics;
	const rotation::ProcessingReport report =
		rotation::ResolveMapPool( rows, resolver, entries, &diagnostics );
	CHECK( report );
	CHECK( report.accepted == 3 && report.rejected == 5 );
	CHECK( entries.size() == 3 );
	CHECK( entries[0].requestedMap == "aliasmap" );
	CHECK( entries[0].requestedFactory == "duelalias" );
	CHECK( entries[0].map == "campgrounds" );
	CHECK( entries[0].factory == "duel" );
	CHECK( entries[0].mapTitle == "Campgrounds" );
	CHECK( entries[0].factoryTitle == "Duel" );
	CHECK( entries[0].baseGameType == 1 );
	CHECK( entries[1].map == "unknownarena" );
	CHECK( entries[1].arenaSupport == rotation::ArenaSupport::Unknown );
	CHECK( entries[2].map == "valid" );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::MapNotFound ) );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::FactoryHidden ) );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::FactoryNotFound ) );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::ArenaUnsupported ) );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::InvalidBaseGameType ) );

	diagnostics.clear();
	rotation::ResolveOptions strict;
	strict.acceptUnknownArenas = false;
	const rotation::ProcessingReport strictReport =
		rotation::ResolveMapPool( rows, resolver, entries, &diagnostics, strict );
	CHECK( strictReport.accepted == 2 && strictReport.rejected == 6 );
	CHECK( entries.size() == 2 );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::ArenaUnknown ) );

	diagnostics.clear();
	rotation::ResolveOptions includeHidden;
	includeHidden.includeHiddenFactories = true;
	const rotation::ProcessingReport hiddenReport =
		rotation::ResolveMapPool( rows, resolver, entries, &diagnostics, includeHidden );
	CHECK( hiddenReport.accepted == 4 && hiddenReport.rejected == 4 );
	CHECK( entries.size() == 4 );
	CHECK( entries[1].factory == "_training" );
	return true;
}

bool RequiresAllResolversAndHandlesResolverFailureDeterministically() {
	const std::vector<rotation::MapPoolRow> rows{{ "map", "ffa", 7 }};
	std::vector<rotation::RotationEntry> entries;
	std::vector<rotation::Diagnostic> diagnostics;
	rotation::RotationResolver resolver;
	const rotation::ProcessingReport missing =
		rotation::ResolveMapPool( rows, resolver, entries, &diagnostics );
	CHECK( !missing && missing.rejected == 1 );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::ResolverUnavailable ) );

	std::size_t mapCalls = 0;
	resolver.map = [&mapCalls]( std::string_view name )
			-> std::optional<rotation::MapMetadata> {
		if ( ++mapCalls == 2 ) {
			throw 1;
		}
		return rotation::MapMetadata{ std::string( name ), "Map" };
	};
	resolver.factory = []( std::string_view )
		-> std::optional<rotation::FactoryMetadata> {
		return rotation::FactoryMetadata{ "ffa", "FFA", 0, false };
	};
	resolver.arenaSupport = []( std::string_view, int ) {
		return rotation::ArenaSupport::Supported;
	};
	diagnostics.clear();
	const std::vector<rotation::MapPoolRow> failureRows{
		{ "first", "ffa", 1 }, { "second", "ffa", 2 }
	};
	const rotation::ProcessingReport failed =
		rotation::ResolveMapPool( failureRows, resolver, entries, &diagnostics );
	CHECK( !failed && failed.accepted == 0 && failed.rejected == 2 );
	CHECK( entries.empty() );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::ResolverUnavailable ) );

	resolver.map = []( std::string_view name )
			-> std::optional<rotation::MapMetadata> {
		return rotation::MapMetadata{ std::string( name ), "Map" };
	};
	std::size_t factoryCalls = 0;
	resolver.factory = [&factoryCalls]( std::string_view )
			-> std::optional<rotation::FactoryMetadata> {
		if ( ++factoryCalls == 2 ) {
			throw 1;
		}
		return rotation::FactoryMetadata{ "ffa", "FFA", 0, false };
	};
	diagnostics.clear();
	const rotation::ProcessingReport factoryFailed =
		rotation::ResolveMapPool( failureRows, resolver, entries, &diagnostics );
	CHECK( !factoryFailed && factoryFailed.accepted == 0 &&
		factoryFailed.rejected == failureRows.size() );
	CHECK( entries.empty() );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::ResolverUnavailable ) );

	resolver.factory = []( std::string_view )
			-> std::optional<rotation::FactoryMetadata> {
		return rotation::FactoryMetadata{ "ffa", "FFA", 0, false };
	};
	std::size_t supportCalls = 0;
	resolver.arenaSupport = [&supportCalls]( std::string_view, int ) {
		if ( ++supportCalls == 2 ) {
			throw 1;
		}
		return rotation::ArenaSupport::Supported;
	};
	diagnostics.clear();
	const rotation::ProcessingReport supportFailed =
		rotation::ResolveMapPool( failureRows, resolver, entries, &diagnostics );
	CHECK( !supportFailed && supportFailed.accepted == 0 &&
		supportFailed.rejected == failureRows.size() );
	CHECK( entries.empty() );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::ResolverUnavailable ) );
	return true;
}

bool CapsAcceptedResolverEntriesAndValidatesFactoryFirst() {
	std::vector<rotation::MapPoolRow> rows;
	for ( std::size_t i = 0; i < rotation::MaximumMapPoolRows + 32; ++i ) {
		rows.push_back( { "missing-map", "missing-factory", i + 1 } );
	}
	for ( std::size_t i = 0; i < rotation::MaximumMapPoolRows + 1; ++i ) {
		rows.push_back( { "map" + std::to_string( i ), "ffa",
			rotation::MaximumMapPoolRows + 33 + i } );
	}
	std::size_t mapCalls = 0;
	std::size_t factoryCalls = 0;
	rotation::RotationResolver resolver;
	resolver.map = [&mapCalls]( std::string_view name )
			-> std::optional<rotation::MapMetadata> {
		++mapCalls;
		return rotation::MapMetadata{ std::string( name ), "Map" };
	};
	resolver.factory = [&factoryCalls]( std::string_view name )
			-> std::optional<rotation::FactoryMetadata> {
		++factoryCalls;
		if ( name == "missing-factory" ) {
			return std::nullopt;
		}
		return rotation::FactoryMetadata{ std::string( name ), "Factory", 0, false };
	};
	resolver.arenaSupport = []( std::string_view, int ) {
		return rotation::ArenaSupport::Supported;
	};

	std::vector<rotation::RotationEntry> entries( 1 );
	entries[0].map = "stale";
	std::vector<rotation::Diagnostic> diagnostics;
	const rotation::ProcessingReport report =
		rotation::ResolveMapPool( rows, resolver, entries, &diagnostics );
	CHECK( report && report.accepted == rotation::MaximumMapPoolRows );
	CHECK( entries.size() == rotation::MaximumMapPoolRows );
	CHECK( report.rejected == rotation::MaximumMapPoolRows + 33 );
	CHECK( mapCalls == rotation::MaximumMapPoolRows );
	CHECK( factoryCalls == ( rotation::MaximumMapPoolRows + 32 ) +
		rotation::MaximumMapPoolRows );
	CHECK( HasDiagnostic( diagnostics, rotation::DiagnosticCode::MapPoolTooManyRows ) );
	return true;
}

bool PreservesRawRowsAndTruncatesResolverMetadataLikeRetail() {
	const std::vector<rotation::MapPoolRow> directRows{
		{ "bad/map", "ffa", 1 },
		{ "valid", "bad/factory", 2 },
		{ "good", "ffa", 3 }
	};
	std::size_t mapCalls = 0;
	std::size_t factoryCalls = 0;
	rotation::RotationResolver resolver;
	resolver.map = [&mapCalls]( std::string_view name )
			-> std::optional<rotation::MapMetadata> {
		++mapCalls;
		return rotation::MapMetadata{ std::string( name ), "Safe title" };
	};
	resolver.factory = [&factoryCalls]( std::string_view name )
			-> std::optional<rotation::FactoryMetadata> {
		++factoryCalls;
		return rotation::FactoryMetadata{
			std::string( name ), "Free For All", 0, false };
	};
	resolver.arenaSupport = []( std::string_view, int ) {
		return rotation::ArenaSupport::Supported;
	};

	std::vector<rotation::RotationEntry> entries;
	std::vector<rotation::Diagnostic> diagnostics;
	const rotation::ProcessingReport report =
		rotation::ResolveMapPool( directRows, resolver, entries, &diagnostics );
	CHECK( report && report.accepted == 3 && report.rejected == 0 );
	CHECK( entries.size() == 3 && entries[0].map == "bad/map" &&
		entries[1].factory == "bad/factory" );
	CHECK( mapCalls == 3 && factoryCalls == 3 );
	CHECK( diagnostics.empty() );

	const std::vector<rotation::MapPoolRow> mapTitleRows{
		{ "backslash", "ffa", 1 }, { "quote", "ffa", 2 },
		{ "semicolon", "ffa", 3 }, { "control", "ffa", 4 },
		{ "nul", "ffa", 5 }, { "overlong", "ffa", 6 },
		{ "badcanonical", "ffa", 7 }, { "exact", "ffa", 8 },
		{ "utf8", "ffa", 9 }
	};
	mapCalls = 0;
	factoryCalls = 0;
	resolver.map = [&mapCalls]( std::string_view name )
			-> std::optional<rotation::MapMetadata> {
		++mapCalls;
		std::string title;
		if ( name == "backslash" ) title = "Bad\\Title";
		else if ( name == "quote" ) title = "Bad\"Title";
		else if ( name == "semicolon" ) title = "Bad;Title";
		else if ( name == "control" ) title = std::string( "Bad\x1f" "Title", 9 );
		else if ( name == "nul" ) title = std::string( "Bad\0Title", 9 );
		else if ( name == "overlong" ) {
			title.assign( rotation::MaximumResolvedTitleBytes + 1, 'x' );
		} else if ( name == "exact" ) {
			title.assign( rotation::MaximumResolvedTitleBytes, 'x' );
		} else {
			title = "Caf\xc3\xa9";
		}
		const std::string canonical = name == "badcanonical"
			? "bad/canonical" : std::string( name );
		return rotation::MapMetadata{ canonical, std::move( title ) };
	};
	diagnostics.clear();
	const rotation::ProcessingReport mapTitles =
		rotation::ResolveMapPool( mapTitleRows, resolver, entries, &diagnostics );
	CHECK( mapTitles && mapTitles.accepted == mapTitleRows.size() &&
		mapTitles.rejected == 0 );
	CHECK( entries.size() == mapTitleRows.size() );
	CHECK( entries[4].mapTitle == "Bad" );
	CHECK( entries[5].mapTitle.size() == rotation::MaximumResolvedTitleBytes );
	CHECK( entries[6].map == "bad/canonical" );
	CHECK( mapCalls == mapTitleRows.size() &&
		factoryCalls == mapTitleRows.size() );
	CHECK( diagnostics.empty() );

	const std::vector<rotation::MapPoolRow> factoryTitleRows{
		{ "map1", "backslash", 1 }, { "map2", "quote", 2 },
		{ "map3", "semicolon", 3 }, { "map4", "control", 4 },
		{ "map5", "nul", 5 }, { "map6", "overlong", 6 },
		{ "map7", "badcanonical", 7 }, { "map8", "exact", 8 },
		{ "map9", "utf8", 9 }
	};
	resolver.map = []( std::string_view name )
			-> std::optional<rotation::MapMetadata> {
		return rotation::MapMetadata{ std::string( name ), "Map" };
	};
	factoryCalls = 0;
	resolver.factory = [&factoryCalls]( std::string_view name )
			-> std::optional<rotation::FactoryMetadata> {
		++factoryCalls;
		std::string title;
		if ( name == "backslash" ) title = "Bad\\Title";
		else if ( name == "quote" ) title = "Bad\"Title";
		else if ( name == "semicolon" ) title = "Bad;Title";
		else if ( name == "control" ) title = std::string( "Bad\x1f" "Title", 9 );
		else if ( name == "nul" ) title = std::string( "Bad\0Title", 9 );
		else if ( name == "overlong" ) {
			title.assign( rotation::MaximumResolvedTitleBytes + 1, 'x' );
		} else if ( name == "exact" ) {
			title.assign( rotation::MaximumResolvedTitleBytes, 'x' );
		} else {
			title = "F\xc3\xa1" "brica";
		}
		const std::string canonical = name == "badcanonical"
			? "bad/canonical" : std::string( name );
		return rotation::FactoryMetadata{
			canonical, std::move( title ), 0, false };
	};
	diagnostics.clear();
	const rotation::ProcessingReport factoryTitles =
		rotation::ResolveMapPool( factoryTitleRows, resolver, entries, &diagnostics );
	CHECK( factoryTitles && factoryTitles.accepted == factoryTitleRows.size() &&
		factoryTitles.rejected == 0 );
	CHECK( entries.size() == factoryTitleRows.size() );
	CHECK( entries[4].factoryTitle == "Bad" );
	CHECK( entries[5].factoryTitle.size() == rotation::MaximumResolvedTitleBytes );
	CHECK( entries[6].factory == "bad/canonical" );
	CHECK( factoryCalls == factoryTitleRows.size() );
	CHECK( diagnostics.empty() );
	return true;
}

rotation::RotationEntry Entry( std::string map ) {
	rotation::RotationEntry entry;
	entry.map = std::move( map );
	entry.factory = "ffa";
	return entry;
}

bool SelectsDeterministicallyAndExcludesTheCurrentMapWhenPossible() {
	const std::vector<rotation::RotationEntry> entries{
		Entry( "current" ), Entry( "alpha" ), Entry( "beta" ), Entry( "gamma" )
	};
	std::optional<std::size_t> selected =
		rotation::SelectRotationIndex( entries, 4, "CURRENT", true );
	CHECK( selected && *selected == 2 );
	selected = rotation::SelectRotationIndex( entries, 4, "current", false );
	CHECK( selected && *selected == 0 );

	const std::vector<rotation::RotationEntry> currentOnly{
		Entry( "current" ), Entry( "CURRENT" )
	};
	selected = rotation::SelectRotationIndex( currentOnly, 3, "Current", true );
	CHECK( selected && *selected == 1 );
	CHECK( !rotation::SelectRotationIndex( {}, 0 ) );
	return true;
}

bool BuildsRetailSlotsWithCurrentOffsetAndCollisionRerolls() {
	const std::vector<rotation::RotationEntry> entries{
		Entry( "current" ), Entry( "alpha" ), Entry( "beta" ), Entry( "gamma" )
	};
	const std::array<std::size_t, 4> draws{{ 1, 1, 3, 2 }};
	std::size_t draw = 0;
	const rotation::RotationModel model = rotation::BuildRotationModel(
		entries, 0, [&]() { return draws[draw++]; } );
	CHECK( model.count == 3 );
	CHECK( model.indices[0] == 1 );
	CHECK( model.indices[1] == 3 );
	CHECK( model.indices[2] == 2 );
	CHECK( draw == draws.size() );

	const std::array<std::size_t, 3> currentDraws{{ 2, 2, 0 }};
	draw = 0;
	const rotation::RotationModel withCurrent = rotation::BuildRotationModel(
		entries, 1, [&]() { return currentDraws[draw++]; } );
	CHECK( withCurrent.count == 2 );
	CHECK( withCurrent.indices[0] == 2 && withCurrent.indices[1] == 0 );
	CHECK( draw == currentDraws.size() );

	const std::vector<rotation::RotationEntry> two{
		Entry( "alpha" ), Entry( "beta" )
	};
	const rotation::RotationModel shortModel = rotation::BuildRotationModel(
		two, 0, []() { return 99; } );
	CHECK( shortModel.count == 2 );
	CHECK( shortModel.indices[0] == 0 && shortModel.indices[1] == 1 );
	CHECK( shortModel.indices[2] == rotation::InvalidRotationIndex );

	const std::vector<rotation::RotationEntry> three{
		Entry( "alpha" ), Entry( "beta" ), Entry( "gamma" )
	};
	const rotation::RotationModel orderedModel = rotation::BuildRotationModel(
		three, 1, []() { return 99; } );
	CHECK( orderedModel.count == 2 );
	CHECK( orderedModel.indices[0] == 1 );
	CHECK( orderedModel.indices[1] == 2 );
	CHECK( orderedModel.indices[2] == rotation::InvalidRotationIndex );

	const std::vector<rotation::RotationEntry> one{ Entry( "only" ) };
	const rotation::RotationModel currentConsumesOnlySlot =
		rotation::BuildRotationModel( one, 1, []() { return 99; } );
	CHECK( currentConsumesOnlySlot.count == 0 );
	return true;
}

} // namespace

int main() {
	return ParsesRetailShapedArenaBlocksAndAppendsDeterministically() &&
		FindsFirstExactArenaRecordCaseSensitively() &&
		MapsRetailBaseGameTypesToCaseSensitiveSubstrings() &&
		MatchesRetailLossyArenaInfoAndSourceStopRules() &&
		EnforcesArenaInputAndRecordLimitsWithoutPartialOverflow() &&
		TruncatesArenaTokensAndCountsIntermediateBlocksLikeRetail() &&
		ParsesBoundedMapPoolsAndDiagnosesMalformedRows() &&
		EnforcesMapPoolFileAndRowLimits() &&
		ResolvesAliasesAndRejectsUnavailableOrIncompatibleRows() &&
		RequiresAllResolversAndHandlesResolverFailureDeterministically() &&
		CapsAcceptedResolverEntriesAndValidatesFactoryFirst() &&
		PreservesRawRowsAndTruncatesResolverMetadataLikeRetail() &&
		SelectsDeterministicallyAndExcludesTheCurrentMapWhenPossible() &&
		BuildsRetailSlotsWithCurrentOffsetAndCollisionRerolls() ? 0 : 1;
}
