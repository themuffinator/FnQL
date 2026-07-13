#include "factory_catalog.hpp"
#include "json_document.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fnql::server::factory::AppendResult;
using fnql::server::factory::BaseGametype;
using fnql::server::factory::Catalog;
using fnql::server::factory::Definition;
using fnql::server::factory::DiagnosticCode;
using fnql::server::factory::DiagnosticLevel;
using fnql::server::factory::MaximumDefinitions;
using fnql::server::factory::MaximumSettings;
using fnql::server::factory::MaximumTags;
using fnql::server::factory::Setting;

#define CHECK( expression ) \
	do { \
		if ( !( expression ) ) { \
			std::cerr << __func__ << ':' << __LINE__ \
				<< ": check failed: " #expression "\n"; \
			return false; \
		} \
	} while ( false )

[[nodiscard]] bool HasDiagnostic( const AppendResult &result,
	DiagnosticCode code, DiagnosticLevel level ) {
	for ( const auto &diagnostic : result.diagnostics ) {
		if ( diagnostic.code == code && diagnostic.level == level ) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] const Setting *FindSetting( const Definition &definition,
	std::string_view name ) {
	for ( const Setting &setting : definition.settings ) {
		if ( setting.name == name ) {
			return &setting;
		}
	}
	return nullptr;
}

[[nodiscard]] std::string FactoryDocument( std::string_view id,
	std::string_view title = "Factory", std::string_view basegt = "ffa",
	std::string_view cvars = "{}" ) {
	std::string document = "{\"id\":\"";
	document.append( id.data(), id.size() );
	document += "\",\"basegt\":\"";
	document.append( basegt.data(), basegt.size() );
	document += "\",\"title\":\"";
	document.append( title.data(), title.size() );
	document += "\",\"cvars\":";
	document.append( cvars.data(), cvars.size() );
	document += '}';
	return document;
}

bool TestTypedDefinitionAndSettings() {
	Catalog catalog;
	const AppendResult result = catalog.AppendDocument( "scripts/base.factories", R"json(
{
  "id": "typed",
  "basegt": "tdm",
  "title": "Typed Factory",
  "author": "FnQL",
  "description": "Conversion coverage",
  "tags": ["a","b",7,"c","d","e","f","g","h","ignored"],
  "cvars": {
    "text": "line\nvalue",
    "signed": -12,
    "unsigned": 12,
    "maximum": 2147483647,
    "minimum": -2147483648,
    "enabled": true,
    "disabled": false,
    "real": 1.23456789,
    "exponent": 2e2,
    "negativeZero": -0.0,
    "leadingZero": 01,
    "trailingDot": 2.,
    "incompleteExponent": 3e,
    "operatorSuffix": 4+5,
    "bareMinus": -,
    "negativeFraction": -.5,
    "nullValue": null,
    "arrayValue": [],
    "objectValue": {},
    "unsignedOverflow": 2147483648,
    "signedOverflow": -2147483649,
    "realOverflow": 1e309,
    "signed": -13
  }
}
)json" );

	CHECK( result.success );
	CHECK( result.definitionsAdded == 1 );
	CHECK( HasDiagnostic( result, DiagnosticCode::IgnoredOptionalField,
		DiagnosticLevel::Warning ) );
	CHECK( HasDiagnostic( result, DiagnosticCode::UnsupportedSettingValue,
		DiagnosticLevel::Warning ) );
	CHECK( catalog.Size() == 1 );
	const Definition &definition = catalog.Definitions().front();
	CHECK( definition.id == "typed" );
	CHECK( definition.title == "Typed Factory" );
	CHECK( definition.author && *definition.author == "FnQL" );
	CHECK( definition.description && *definition.description == "Conversion coverage" );
	CHECK( definition.baseGametype == BaseGametype::TeamDeathmatch );
	CHECK( definition.sourcePath == "scripts/base.factories" );
	CHECK( definition.tags.size() == MaximumTags - 1 );
	CHECK( definition.tags.front() == "a" );
	CHECK( definition.tags.back() == "g" );
	CHECK( FindSetting( definition, "text" )->value == "line\nvalue" );
	CHECK( FindSetting( definition, "signed" )->value == "-13" );
	CHECK( FindSetting( definition, "unsigned" )->value == "12" );
	CHECK( FindSetting( definition, "maximum" )->value == "2147483647" );
	CHECK( FindSetting( definition, "minimum" )->value == "-2147483648" );
	CHECK( FindSetting( definition, "enabled" )->value == "1" );
	CHECK( FindSetting( definition, "disabled" )->value == "0" );
	CHECK( FindSetting( definition, "real" )->value == "1.234568" );
	CHECK( FindSetting( definition, "exponent" )->value == "200.000000" );
	CHECK( FindSetting( definition, "negativeZero" )->value == "-0.000000" );
	CHECK( FindSetting( definition, "leadingZero" )->value == "1" );
	CHECK( FindSetting( definition, "trailingDot" )->value == "2.000000" );
	CHECK( FindSetting( definition, "incompleteExponent" )->value == "3.000000" );
	CHECK( FindSetting( definition, "operatorSuffix" )->value == "4.000000" );
	CHECK( FindSetting( definition, "bareMinus" )->value == "0" );
	CHECK( FindSetting( definition, "negativeFraction" )->value == "-0.500000" );
	CHECK( FindSetting( definition, "nullValue" ) != nullptr );
	CHECK( !FindSetting( definition, "nullValue" )->hasValue );
	CHECK( FindSetting( definition, "arrayValue" ) != nullptr );
	CHECK( !FindSetting( definition, "arrayValue" )->hasValue );
	CHECK( FindSetting( definition, "objectValue" ) != nullptr );
	CHECK( !FindSetting( definition, "objectValue" )->hasValue );
	CHECK( FindSetting( definition, "unsignedOverflow" ) != nullptr );
	CHECK( !FindSetting( definition, "unsignedOverflow" )->hasValue );
	CHECK( FindSetting( definition, "signedOverflow" )->value
		== "-2147483649.000000" );
	CHECK( FindSetting( definition, "realOverflow" )->value == "1.#INF00" );
	CHECK( catalog.FindFirst( "typed" ) == &definition );
	CHECK( catalog.FindFirst( "Typed" ) == nullptr );
	return true;
}

bool TestBaseGametypeTableIsExact() {
	static constexpr std::array<std::string_view, 13> Tokens{
		"ffa", "duel", "race", "tdm", "ca", "ctf", "oneflag",
		"overload", "har", "ft", "dom", "ad", "rr"
	};
	for ( std::size_t index = 0; index < Tokens.size(); ++index ) {
		const auto parsed = fnql::server::factory::ParseBaseGametype( Tokens[index] );
		CHECK( parsed );
		CHECK( static_cast<int>( *parsed ) == static_cast<int>( index ) );
		CHECK( fnql::server::factory::BaseGametypeToken( *parsed ) == Tokens[index] );
	}
	CHECK( !fnql::server::factory::ParseBaseGametype( "FFA" ) );
	CHECK( !fnql::server::factory::ParseBaseGametype( "tourney" ) );
	CHECK( !fnql::server::factory::ParseBaseGametype( "hh" ) );
	CHECK( fnql::server::factory::BaseGametypeToken(
		static_cast<BaseGametype>( 99 ) ).empty() );

	Catalog catalog;
	for ( std::size_t index = 0; index < Tokens.size(); ++index ) {
		const std::string id = "gt" + std::to_string( index );
		const AppendResult result = catalog.AppendDocument( "table",
			FactoryDocument( id, "GT", Tokens[index] ) );
		CHECK( result.success );
		CHECK( static_cast<int>( catalog.FindFirst( id )->baseGametype )
			== static_cast<int>( index ) );
	}
	const std::size_t before = catalog.Size();
	const AppendResult wrongCase = catalog.AppendDocument( "table",
		FactoryDocument( "wrong", "Wrong", "CA" ) );
	CHECK( !wrongCase.success );
	CHECK( HasDiagnostic( wrongCase, DiagnosticCode::InvalidBaseGametype,
		DiagnosticLevel::Error ) );
	CHECK( catalog.Size() == before );
	return true;
}

bool TestStrictDocumentsAndAtomicAppend() {
	Catalog catalog;
	CHECK( catalog.AppendDocument( "good", FactoryDocument( "kept" ) ).success );
	const std::size_t initialSize = catalog.Size();

	const std::vector<std::string> invalidDocuments{
		"",
		"null",
		"\"object\"",
		R"json({"id":"x","basegt":"ffa","title":"X","cvars":{},})json",
		R"json({"ID":"x","basegt":"ffa","title":"X","cvars":{}})json",
		R"json({"id":7,"basegt":"ffa","title":"X","cvars":{}})json",
		R"json({"id":"x","basegt":0,"title":"X","cvars":{}})json",
		R"json({"id":"x","basegt":"ffa","title":false,"cvars":{}})json",
		R"json({"id":"x","basegt":"ffa","title":"X","cvars":[]})json"
	};
	for ( const std::string &document : invalidDocuments ) {
		const AppendResult result = catalog.AppendDocument( "invalid", document );
		CHECK( !result.success );
		CHECK( result.definitionsAdded == 0 );
		CHECK( catalog.Size() == initialSize );
	}

	const AppendResult emptyArray = catalog.AppendDocument( "empty", "[]" );
	CHECK( emptyArray.success );
	CHECK( emptyArray.definitionsAdded == 0 );
	CHECK( catalog.Size() == initialSize );

	const AppendResult ignoredSuffix = catalog.AppendDocument( "suffix",
		FactoryDocument( "suffix" ) + " non-JSON bytes /* unterminated" );
	CHECK( ignoredSuffix.success );
	CHECK( ignoredSuffix.definitionsAdded == 1 );
	CHECK( catalog.FindFirst( "suffix" ) != nullptr );

	const std::size_t partialBegin = catalog.Size();
	const AppendResult partialArray = catalog.AppendDocument( "partial", R"json([
{"id":"valid-a","basegt":"ffa","title":"Valid A","cvars":{}},
7,
{"id":"bad","basegt":"ffa","cvars":{}},
{"id":"valid-b","basegt":"duel","title":"Valid B","cvars":{}}
])json" );
	CHECK( partialArray.success );
	CHECK( partialArray.definitionsAdded == 2 );
	CHECK( HasDiagnostic( partialArray, DiagnosticCode::InvalidDefinition,
		DiagnosticLevel::Error ) );
	CHECK( catalog.Size() == partialBegin + 2 );
	CHECK( catalog.Definitions()[partialBegin].id == "valid-a" );
	CHECK( catalog.Definitions()[partialBegin + 1].id == "valid-b" );

	const AppendResult ignoredOptionals = catalog.AppendDocument( "optional", R"json(
{"id":"optional","basegt":"ffa","title":"Optional","author":7,
 "description":false,"tags":"not-an-array","unknown":{"nested":[1,2,3]},"cvars":{}}
)json" );
	CHECK( ignoredOptionals.success );
	CHECK( HasDiagnostic( ignoredOptionals, DiagnosticCode::IgnoredOptionalField,
		DiagnosticLevel::Warning ) );
	const Definition *optional = catalog.FindFirst( "optional" );
	CHECK( optional != nullptr );
	CHECK( !optional->author );
	CHECK( !optional->description );
	CHECK( optional->tags.empty() );
	return true;
}

bool TestJsonCppCommentTrivia() {
	Catalog catalog;
	const AppendResult result = catalog.AppendDocument( "comments", R"json(
/* JsonCpp Reader enables comments for the retail factory load. */
{
  // Comments are trivia anywhere whitespace is accepted.
  /* member */ "id": "commented",
  "basegt": "ca", // line ending
  "title": "literal // and /* text",
  "cvars": {
    "sv_fps": /* value */ 40
  }
}
// trailing comment
)json" );
	CHECK( result.success );
	CHECK( result.definitionsAdded == 1 );
	const Definition *definition = catalog.FindFirst( "commented" );
	CHECK( definition != nullptr );
	CHECK( definition->title == "literal // and /* text" );
	CHECK( FindSetting( *definition, "sv_fps" )->value == "40" );

	const std::size_t beforePlacementChecks = catalog.Size();
	const AppendResult keyColonComment = catalog.AppendDocument( "comments", R"json(
{"id" /* rejected here */ : "bad","basegt":"ffa","title":"Bad","cvars":{}}
)json" );
	CHECK( !keyColonComment.success );
	const AppendResult commentedEmptyArray = catalog.AppendDocument( "comments",
		"[/* not retail-empty */]" );
	CHECK( !commentedEmptyArray.success );
	CHECK( catalog.Size() == beforePlacementChecks );

	const std::size_t before = catalog.Size();
	const AppendResult unterminated = catalog.AppendDocument( "comments", R"json(
{"id": /* unterminated
)json" );
	CHECK( !unterminated.success );
	CHECK( HasDiagnostic( unterminated, DiagnosticCode::InvalidJson,
		DiagnosticLevel::Error ) );
	CHECK( catalog.Size() == before );
	return true;
}

bool TestDeepUnusedMetadataUsesHeapStackAndIterativeTeardown() {
	std::string document =
		"{\"id\":\"deep\",\"basegt\":\"ffa\",\"title\":\"Deep\",\"ignored\":";
	// Two delimiters per level keep this just below the runtime's 0x8000-byte
	// factory-file cap while far exceeding the default Windows thread stack.
	constexpr std::size_t Depth = 16000;
	document.append( Depth, '[' );
	document += '0';
	document.append( Depth, ']' );
	document += ",\"cvars\":{}}";

	Catalog catalog;
	const AppendResult result = catalog.AppendDocument( "deep", document );
	CHECK( result.success );
	CHECK( result.definitionsAdded == 1 );
	CHECK( catalog.FindFirst( "deep" ) != nullptr );
	return true;
}

bool TestDuplicatesRemainOrderedAndFindFirstWins() {
	Catalog catalog;
	CHECK( catalog.AppendDocument( "first", FactoryDocument(
		"duplicate", "First", "ffa", "{\"value\":1}" ) ).success );
	const AppendResult duplicate = catalog.AppendDocument( "second", FactoryDocument(
		"duplicate", "Second", "ca", "{\"value\":2}" ) );
	CHECK( duplicate.success );
	CHECK( HasDiagnostic( duplicate, DiagnosticCode::DuplicateId,
		DiagnosticLevel::Warning ) );
	const AppendResult differentCase = catalog.AppendDocument( "third",
		FactoryDocument( "Duplicate", "Different case" ) );
	CHECK( differentCase.success );
	CHECK( !HasDiagnostic( differentCase, DiagnosticCode::DuplicateId,
		DiagnosticLevel::Warning ) );

	CHECK( catalog.Size() == 3 );
	CHECK( catalog.Definitions()[0].title == "First" );
	CHECK( catalog.Definitions()[1].title == "Second" );
	CHECK( catalog.FindFirst( "duplicate" ) == &catalog.Definitions()[0] );
	CHECK( catalog.FindFirst( "duplicate" )->title == "First" );
	CHECK( catalog.FindFirst( "Duplicate" )->title == "Different case" );
	return true;
}

bool TestSettingsAndDefinitionBoundaries() {
	std::ostringstream settings;
	settings << '{';
	for ( std::size_t index = 0; index <= MaximumSettings; ++index ) {
		if ( index != 0 ) {
			settings << ',';
		}
		settings << "\"S" << index << "\":" << index;
	}
	settings << '}';

	Catalog settingsCatalog;
	const AppendResult settingResult = settingsCatalog.AppendDocument( "settings-limit",
		FactoryDocument( "settings", "Settings", "ffa", settings.str() ) );
	CHECK( settingResult.success );
	CHECK( HasDiagnostic( settingResult, DiagnosticCode::TooManySettings,
		DiagnosticLevel::Warning ) );
	const Definition *definition = settingsCatalog.FindFirst( "settings" );
	CHECK( definition != nullptr );
	CHECK( definition->settings.size() == MaximumSettings );
	CHECK( FindSetting( *definition, "S255" ) != nullptr );
	CHECK( FindSetting( *definition, "S256" ) != nullptr );
	CHECK( FindSetting( *definition, "S98" ) != nullptr );
	CHECK( FindSetting( *definition, "S99" ) == nullptr );

	Catalog definitionCatalog;
	for ( std::size_t index = 0; index < MaximumDefinitions; ++index ) {
		const std::string id = "factory-" + std::to_string( index );
		const AppendResult result = definitionCatalog.AppendDocument( "definition-limit",
			FactoryDocument( id ) );
		CHECK( result.success );
	}
	CHECK( definitionCatalog.Size() == MaximumDefinitions );
	const AppendResult overflow = definitionCatalog.AppendDocument( "definition-limit",
		FactoryDocument( "one-too-many" ) );
	CHECK( !overflow.success );
	CHECK( HasDiagnostic( overflow, DiagnosticCode::TooManyDefinitions,
		DiagnosticLevel::Error ) );
	CHECK( definitionCatalog.Size() == MaximumDefinitions );
	CHECK( definitionCatalog.FindFirst( "one-too-many" ) == nullptr );
	return true;
}

bool TestUnicodeEscapesAndUtf8Validation() {
	Catalog catalog;
	const AppendResult good = catalog.AppendDocument( "unicode", R"json(
{"id":"music-\uD834\uDD1E\u0000","basegt":"race",
 "title":"quote=\" slash=\/ line=\n euro=\u20ac","cvars":{"Path":"a\\b"}}
)json" );
	CHECK( good.success );

	std::string id = "music-";
	id.append( "\xf0\x9d\x84\x9e", 4 );
	const Definition *definition = catalog.FindFirst( id );
	CHECK( definition != nullptr );
	CHECK( definition->title.find( "quote=\"" ) == 0 );
	CHECK( definition->title.find( "\xe2\x82\xac" ) != std::string::npos );
	CHECK( FindSetting( *definition, "Path" )->value == "a\\b" );

	const auto serialized = catalog.SerializeWebUi( 4096 );
	CHECK( serialized.success );
	CHECK( fnql::server::json::DocumentIsValid( serialized.json ) );
	CHECK( serialized.json.find( "\\u0000" ) == std::string::npos );
	CHECK( serialized.json.find( "line=\\n" ) != std::string::npos );
	CHECK( serialized.json.find( "quote=\\\"" ) != std::string::npos );

	Catalog cStringCatalog;
	CHECK( cStringCatalog.AppendDocument( "c-string", R"json(
{"id":"nul-id\u0000suffix","basegt":"ffa","title":"Nul\u0000Title",
	 "cvars":{"Name\u0000Suffix":"value\u0000tail",
	          "Name\u0000Tail":"second"}}
)json" ).success );
	const Definition *cStringDefinition = cStringCatalog.FindFirst( "nul-id" );
	CHECK( cStringDefinition && cStringDefinition->title == "Nul" );
	CHECK( FindSetting( *cStringDefinition, "Name" ) &&
		FindSetting( *cStringDefinition, "Name" )->value == "second" );
	std::size_t truncatedNameCount = 0;
	for ( const Setting &setting : cStringDefinition->settings ) {
		if ( setting.name == "Name" ) {
			++truncatedNameCount;
		}
	}
	CHECK( truncatedNameCount == 1 );
	CHECK( cStringCatalog.FindFirst(
		std::string_view( "nul-id\0suffix", 13 ) ) == nullptr );

	Catalog nulFieldCatalog;
	CHECK( nulFieldCatalog.AppendDocument( "nul-field", R"json(
{"id":"first","id\u0000ignored":"last","basegt":"ffa",
 "title":"Nul field","cvars":{}}
)json" ).success );
	CHECK( nulFieldCatalog.FindFirst( "first" ) == nullptr );
	CHECK( nulFieldCatalog.FindFirst( "last" ) != nullptr );

	Catalog javascriptSafe;
	CHECK( javascriptSafe.AppendDocument( "javascript-safe", R"json(
{"id":"line-separators","basegt":"ffa","title":"a\u2028b\u2029c","cvars":{}}
)json" ).success );
	const auto javascriptSafeJson = javascriptSafe.SerializeWebUi( 4096 );
	CHECK( javascriptSafeJson.success );
	CHECK( javascriptSafeJson.json.find( "\\u2028" ) != std::string::npos );
	CHECK( javascriptSafeJson.json.find( "\\u2029" ) != std::string::npos );
	CHECK( javascriptSafeJson.json.find( "\xe2\x80\xa8" ) == std::string::npos );
	CHECK( javascriptSafeJson.json.find( "\xe2\x80\xa9" ) == std::string::npos );

	const AppendResult permissiveSurrogates = catalog.AppendDocument(
		"permissive-surrogates", R"json([
{"id":"low-\uDD1E","basegt":"ffa","title":"Low","cvars":{}},
{"id":"mixed-\uD834\u0041","basegt":"ffa","title":"Mixed","cvars":{}}
])json" );
	CHECK( permissiveSurrogates.success );
	CHECK( permissiveSurrogates.definitionsAdded == 2 );
	std::string lowId = "low-";
	lowId.append( "\xed\xb4\x9e", 3 );
	CHECK( catalog.FindFirst( lowId ) != nullptr );
	std::string mixedId = "mixed-";
	mixedId.append( "\xf0\x9d\x81\x81", 4 );
	CHECK( catalog.FindFirst( mixedId ) != nullptr );

	const std::vector<std::string> invalidEscapes{
		R"json({"id":"\uD834","basegt":"ffa","title":"X","cvars":{}})json",
		R"json({"id":"\u12xz","basegt":"ffa","title":"X","cvars":{}})json",
		R"json({"id":"bad\xescape","basegt":"ffa","title":"X","cvars":{}})json"
	};
	const std::size_t afterPermissiveSurrogates = catalog.Size();
	for ( const std::string &document : invalidEscapes ) {
		const AppendResult result = catalog.AppendDocument( "bad-escape", document );
		CHECK( !result.success );
		CHECK( HasDiagnostic( result, DiagnosticCode::InvalidJson,
			DiagnosticLevel::Error ) );
		CHECK( catalog.Size() == afterPermissiveSurrogates );
	}

	std::string rawBytes = "{\"id\":\"raw-";
	rawBytes.push_back( static_cast<char>( 0xc0 ) );
	rawBytes.push_back( static_cast<char>( 0x80 ) );
	rawBytes += "\",\"basegt\":\"ffa\",\"title\":\"line\n\tvalue\",\"cvars\":{}}";
	const AppendResult rawByteResult = catalog.AppendDocument( "raw-bytes", rawBytes );
	CHECK( rawByteResult.success );
	std::string rawId = "raw-";
	rawId.push_back( static_cast<char>( 0xc0 ) );
	rawId.push_back( static_cast<char>( 0x80 ) );
	const Definition *rawDefinition = catalog.FindFirst( rawId );
	CHECK( rawDefinition != nullptr );
	CHECK( rawDefinition->title == "line\n\tvalue" );
	return true;
}

bool TestReloadReservationPreservesPhysicalLookupOrder() {
	Catalog catalog( 2 );
	CHECK( catalog.AppendDocument( "reload", R"json([
{"id":"before-a","basegt":"ffa","title":"A","cvars":{}},
{"id":"before-b","basegt":"ffa","title":"B","cvars":{}},
{"id":"active","basegt":"ca","title":"New duplicate","cvars":{}},
{"id":"after","basegt":"duel","title":"After","cvars":{}}
])json" ).success );
	CHECK( catalog.Definitions()[0].registrySlot == 0 );
	CHECK( catalog.Definitions()[1].registrySlot == 1 );
	CHECK( catalog.Definitions()[2].registrySlot == 3 );

	Definition retained;
	retained.id = "active";
	retained.title = "Retained";
	retained.baseGametype = BaseGametype::ClanArena;
	CHECK( catalog.InsertRetained( std::move( retained ) ) );
	CHECK( catalog.Size() == 5 );
	CHECK( catalog.Definitions()[2].registrySlot == 2 );
	CHECK( catalog.Definitions()[2].title == "Retained" );
	CHECK( catalog.FindFirst( "active" )->title == "Retained" );
	CHECK( !catalog.InsertRetained( Definition{} ) );

	Catalog shallowReload( 5 );
	CHECK( shallowReload.AppendDocument( "reload", R"json([
{"id":"zero","basegt":"ffa","title":"Zero","cvars":{}},
{"id":"one","basegt":"ffa","title":"One","cvars":{}}
])json" ).success );
	Definition shallowRetained;
	shallowRetained.id = "active-outside-count";
	shallowRetained.title = "Retained";
	CHECK( shallowReload.InsertRetained( std::move( shallowRetained ) ) );
	CHECK( shallowReload.FindFirst( "active-outside-count" ) != nullptr );
	CHECK( !shallowReload.FindFirst( "active-outside-count" )->enumerated );
	const auto shallowJson = shallowReload.SerializeWebUi( 4096 );
	CHECK( shallowJson.success );
	CHECK( shallowJson.json.find( "active-outside-count" ) == std::string::npos );
	return true;
}

bool TestRetailWebUiShapeAndOutputCap() {
	Catalog catalog;
	CHECK( catalog.AppendDocument( "first", R"json(
{"id":"visible","basegt":"ffa","title":"First","author":"old",
 "tags":["not-exported"],"cvars":{"old":1}}
)json" ).success );
	CHECK( catalog.AppendDocument( "hidden", R"json(
{"id":"_private","basegt":"duel","title":"Private","cvars":{"secret":1}}
)json" ).success );
	CHECK( catalog.AppendDocument( "last", R"json(
{"id":"visible","basegt":"ca","title":"Last","author":"",
 "description":"desc","tags":["still-not-exported"],
 "cvars":{"Sv_FPS":40,"Foo":"first","foo":"second"}}
)json" ).success );

	const std::string expected =
		"{\"visible\":{\"id\":\"visible\",\"title\":\"Last\",\"basegt\":4,"
		"\"author\":\"\",\"description\":\"desc\",\"settings\":{"
		"\"foo\":\"second\",\"sv_fps\":\"40\"}}}";
	const auto serialized = catalog.SerializeWebUi( expected.size() );
	CHECK( serialized.success );
	CHECK( serialized.json == expected );
	CHECK( fnql::server::json::DocumentIsValid( serialized.json ) );
	CHECK( serialized.json.find( "sysname" ) == std::string::npos );
	CHECK( serialized.json.find( "tags" ) == std::string::npos );
	CHECK( serialized.json.find( "_private" ) == std::string::npos );
	CHECK( serialized.json.find( "\"basegt\":4" ) != std::string::npos );
	CHECK( serialized.json.find( "\"basegt\":\"ca\"" ) == std::string::npos );

	const auto capped = catalog.SerializeWebUi( expected.size() - 1 );
	CHECK( !capped.success );
	CHECK( capped.json.empty() );
	CHECK( !capped.error.empty() );

	Catalog empty;
	const auto emptySuccess = empty.SerializeWebUi( 2 );
	CHECK( emptySuccess.success );
	CHECK( emptySuccess.json == "{}" );
	const auto emptyFailure = empty.SerializeWebUi( 1 );
	CHECK( !emptyFailure.success );
	CHECK( emptyFailure.json.empty() );

	Catalog specialKeys;
	CHECK( specialKeys.AppendDocument( "special", R"json(
{"id":"length","basegt":"ffa","title":"Length",
 "cvars":{"__proto__":"ordinary","length":7}}
)json" ).success );
	const auto specialJson = specialKeys.SerializeWebUi( 4096 );
	CHECK( specialJson.success && fnql::server::json::DocumentIsValid( specialJson.json ) );
	CHECK( specialJson.json.find( "\"length\":{" ) != std::string::npos );
	CHECK( specialJson.json.find( "\"__proto__\":\"ordinary\"" ) != std::string::npos );
	CHECK( specialJson.json.find( "\"length\":\"7\"" ) != std::string::npos );

	Catalog memberOrdering;
	CHECK( memberOrdering.AppendDocument( "member-ordering", R"json(
{"id":"member-ordering","basegt":"ffa","title":"Member ordering",
 "cvars":{"\u0080":2,"z":1,"":0}}
)json" ).success );
	const Definition *ordered = memberOrdering.FindFirst( "member-ordering" );
	CHECK( ordered != nullptr && ordered->settings.size() == 3 );
	CHECK( ordered->settings[0].name.empty() );
	CHECK( ordered->settings[1].name == "z" );
	CHECK( ordered->settings[2].name == std::string( "\xc2\x80", 2 ) );

	Catalog terminatedSettings;
	CHECK( terminatedSettings.AppendDocument( "terminator", R"json(
{"id":"terminator","basegt":"ffa","title":"Terminator",
 "cvars":{"z":4,"b":"replaced","a":1,"b":null,"c":3}}
)json" ).success );
	const Definition *terminated = terminatedSettings.FindFirst( "terminator" );
	CHECK( terminated != nullptr );
	CHECK( terminated->settings.size() == 4 );
	CHECK( terminated->settings[0].name == "a" );
	CHECK( terminated->settings[1].name == "b" );
	CHECK( !terminated->settings[1].hasValue );
	CHECK( terminated->settings[2].name == "c" );
	CHECK( terminated->settings[3].name == "z" );
	const auto terminatedJson = terminatedSettings.SerializeWebUi( 4096 );
	CHECK( terminatedJson.success );
	CHECK( terminatedJson.json ==
		"{\"terminator\":{\"id\":\"terminator\",\"title\":\"Terminator\","
		"\"basegt\":0,\"settings\":{\"a\":\"1\"}}}" );
	return true;
}

} // namespace

int main() {
	const std::array tests{
		TestTypedDefinitionAndSettings,
		TestBaseGametypeTableIsExact,
		TestStrictDocumentsAndAtomicAppend,
		TestJsonCppCommentTrivia,
		TestDeepUnusedMetadataUsesHeapStackAndIterativeTeardown,
		TestDuplicatesRemainOrderedAndFindFirstWins,
		TestSettingsAndDefinitionBoundaries,
		TestUnicodeEscapesAndUtf8Validation,
		TestReloadReservationPreservesPhysicalLookupOrder,
		TestRetailWebUiShapeAndOutputCap
	};
	for ( const auto test : tests ) {
		if ( !test() ) {
			return 1;
		}
	}
	return 0;
}
