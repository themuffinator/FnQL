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

#include "factory_rotation.hpp"

#include <algorithm>
#include <utility>

namespace fnql::server::rotation {

namespace {

[[nodiscard]] constexpr char AsciiLower( char value ) noexcept {
	return value >= 'A' && value <= 'Z'
		? static_cast<char>( value + ( 'a' - 'A' ) ) : value;
}

[[nodiscard]] bool AsciiEqualNoCase(
		std::string_view left, std::string_view right ) noexcept {
	if ( left.size() != right.size() ) {
		return false;
	}
	for ( std::size_t i = 0; i < left.size(); ++i ) {
		if ( AsciiLower( left[i] ) != AsciiLower( right[i] ) ) {
			return false;
		}
	}
	return true;
}

void AddDiagnostic( std::vector<Diagnostic> *diagnostics, DiagnosticCode code,
		std::size_t line, std::size_t offset ) {
	if ( diagnostics ) {
		diagnostics->push_back( { code, line, offset } );
	}
}

[[nodiscard]] bool TypeContainsRetailSubstring(
		std::string_view type, std::string_view wanted ) noexcept {
	return type.find( wanted ) != std::string_view::npos;
}

[[nodiscard]] bool RecordSupportsBaseGameType(
		const ArenaRecord &record, int baseGameType ) noexcept {
	// The retail host populates these 13 flags with a direct, case-sensitive
	// strstr call for each table entry. Embedded matches are intentional.
	switch ( baseGameType ) {
		case 0: return TypeContainsRetailSubstring( record.type, "ffa" );
		case 1: return TypeContainsRetailSubstring( record.type, "duel" );
		case 2: return TypeContainsRetailSubstring( record.type, "race" );
		case 3: return TypeContainsRetailSubstring( record.type, "tdm" );
		case 4: return TypeContainsRetailSubstring( record.type, "ca" );
		case 5: return TypeContainsRetailSubstring( record.type, "ctf" );
		case 6: return TypeContainsRetailSubstring( record.type, "oneflag" );
		case 7: return TypeContainsRetailSubstring( record.type, "overload" );
		case 8: return TypeContainsRetailSubstring( record.type, "har" );
		case 9: return TypeContainsRetailSubstring( record.type, "ft" );
		case 10: return TypeContainsRetailSubstring( record.type, "dom" );
		case 11: return TypeContainsRetailSubstring( record.type, "ad" );
		case 12: return TypeContainsRetailSubstring( record.type, "rr" );
		default: return false;
	}
}

enum class TokenKind {
	End,
	Word
};

struct Token {
	TokenKind kind = TokenKind::End;
	std::string value;
	std::size_t line = 1;
	std::size_t offset = 0;
};

class ArenaTokenizer {
public:
	ArenaTokenizer( std::string_view input, std::vector<Diagnostic> *diagnostics )
		: input_( input ) { (void)diagnostics; }

	[[nodiscard]] Token Next( bool allowLineBreaks = true ) {
		bool stoppedAtLineBreak = false;
		SkipTrivia( allowLineBreaks, stoppedAtLineBreak );

		Token token;
		token.line = line_;
		token.offset = offset_;
		if ( offset_ >= input_.size() ) {
			return token;
		}
		if ( stoppedAtLineBreak ) {
			token.kind = TokenKind::Word;
			return token;
		}

		const char first = input_[offset_];
		if ( first == '"' ) {
			++offset_;
			while ( offset_ < input_.size() ) {
				const char value = input_[offset_++];
				if ( value == '"' ) {
					token.kind = TokenKind::Word;
					return token;
				}
				if ( value == '\n' ) {
					++line_;
				}
				if ( token.value.size() < MaximumArenaTokenBytes ) {
					token.value.push_back( value );
				}
			}
			// COM_Parse returns the accumulated token at EOF, even without a
			// closing quote.
			token.kind = TokenKind::Word;
			return token;
		}

		while ( offset_ < input_.size() ) {
			const unsigned char value = static_cast<unsigned char>( input_[offset_] );
			if ( value <= static_cast<unsigned char>( ' ' ) ) {
				break;
			}
			++offset_;
			if ( token.value.size() < MaximumArenaTokenBytes ) {
				token.value.push_back( static_cast<char>( value ) );
			}
		}
		token.kind = TokenKind::Word;
		return token;
	}

private:
	void SkipTrivia( bool allowLineBreaks, bool &stoppedAtLineBreak ) {
		while ( offset_ < input_.size() ) {
			bool crossedLine = false;
			while ( offset_ < input_.size() &&
				static_cast<unsigned char>( input_[offset_] ) <=
					static_cast<unsigned char>( ' ' ) ) {
				if ( input_[offset_] == '\n' ) {
					++line_;
					crossedLine = true;
				}
				++offset_;
			}
			if ( crossedLine && !allowLineBreaks ) {
				stoppedAtLineBreak = true;
				return;
			}
			if ( offset_ >= input_.size() ) {
				return;
			}

			const char value = input_[offset_];
			if ( value == '/' && offset_ + 1 < input_.size() &&
				input_[offset_ + 1] == '/' ) {
				offset_ += 2;
				while ( offset_ < input_.size() && input_[offset_] != '\n' ) {
					++offset_;
				}
				continue;
			}
			if ( value == '/' && offset_ + 1 < input_.size() &&
					input_[offset_ + 1] == '*' ) {
				offset_ += 2;
				while ( offset_ < input_.size() ) {
					if ( input_[offset_] == '*' && offset_ + 1 < input_.size() &&
							input_[offset_ + 1] == '/' ) {
						offset_ += 2;
						break;
					}
					if ( input_[offset_] == '\n' ) {
						++line_;
					}
					++offset_;
				}
				continue;
			}
			break;
		}
	}

	std::string_view input_;
	std::size_t offset_ = 0;
	std::size_t line_ = 1;
};

struct RetailInfoPair {
	std::string key;
	std::string value;
};

[[nodiscard]] bool RetailInfoTextIsValid( std::string_view text ) noexcept {
	return text.find_first_of( "\\\";" ) == std::string_view::npos;
}

void RetailInfoSet( std::vector<RetailInfoPair> &pairs, std::string key,
		std::string value ) {
	if ( key.empty() || !RetailInfoTextIsValid( key ) ||
		!RetailInfoTextIsValid( value ) ) {
		return;
	}

	auto existing = std::find_if( pairs.begin(), pairs.end(),
		[&key]( const RetailInfoPair &pair ) {
			return AsciiEqualNoCase( pair.key, key );
		} );
	if ( existing != pairs.end() ) {
		pairs.erase( existing );
	}
	if ( value.empty() ) {
		return;
	}

	std::size_t currentBytes = 0;
	for ( const RetailInfoPair &pair : pairs ) {
		currentBytes += pair.key.size() + pair.value.size() + 2;
	}
	const std::size_t appendedBytes = key.size() + value.size() + 2;
	if ( currentBytes + appendedBytes >= 0x400u ) {
		return;
	}
	pairs.push_back( { std::move( key ), std::move( value ) } );
}

[[nodiscard]] std::string RetailInfoValue(
		const std::vector<RetailInfoPair> &pairs, std::string_view key ) {
	for ( const RetailInfoPair &pair : pairs ) {
		if ( AsciiEqualNoCase( pair.key, key ) ) {
			return pair.value;
		}
	}
	return {};
}

[[nodiscard]] std::string RetailStoredText( std::string_view value ) {
	const std::size_t nul = value.find( '\0' );
	if ( nul != std::string_view::npos ) {
		value = value.substr( 0, nul );
	}
	value = value.substr( 0, ( std::min )( value.size(),
		MaximumResolvedTitleBytes ) );
	return std::string( value );
}

[[nodiscard]] std::vector<std::size_t> SelectionCandidates(
		const std::vector<RotationEntry> &entries, std::string_view currentMap,
		bool excludeCurrentMap ) {
	std::vector<std::size_t> candidates;
	candidates.reserve( entries.size() );

	bool hasAlternative = false;
	if ( excludeCurrentMap && !currentMap.empty() ) {
		for ( const RotationEntry &entry : entries ) {
			if ( !AsciiEqualNoCase( entry.map, currentMap ) ) {
				hasAlternative = true;
				break;
			}
		}
	}

	for ( std::size_t i = 0; i < entries.size(); ++i ) {
		if ( hasAlternative && AsciiEqualNoCase( entries[i].map, currentMap ) ) {
			continue;
		}
		candidates.push_back( i );
	}
	return candidates;
}

} // namespace

bool IdentifierIsValid( std::string_view identifier ) noexcept {
	return !identifier.empty() && identifier.size() <= MaximumIdentifierBytes &&
		identifier.find( '\0' ) == std::string_view::npos;
}

bool ArenaCatalog::Append( ArenaRecord record ) {
	if ( records_.size() >= MaximumArenaRecords ||
		!IdentifierIsValid( record.map ) ||
		record.longName.size() > MaximumArenaLongNameBytes ||
		record.type.size() > MaximumArenaTypeBytes ) {
		return false;
	}
	records_.push_back( std::move( record ) );
	return true;
}

void ArenaCatalog::Clear() noexcept {
	records_.clear();
}

const std::vector<ArenaRecord> &ArenaCatalog::Records() const noexcept {
	return records_;
}

const ArenaRecord *ArenaCatalog::Find( std::string_view map ) const noexcept {
	for ( const ArenaRecord &record : records_ ) {
		if ( record.map == map ) {
			return &record;
		}
	}
	return nullptr;
}

ArenaSupport ArenaCatalog::SupportFor(
		std::string_view map, int baseGameType ) const noexcept {
	const ArenaRecord *record = Find( map );
	if ( !record ) {
		return ArenaSupport::Unknown;
	}
	return RecordSupportsBaseGameType( *record, baseGameType )
		? ArenaSupport::Supported : ArenaSupport::Unsupported;
}

ProcessingReport ParseArenaText( std::string_view text, ArenaCatalog &catalog,
		std::vector<Diagnostic> *diagnostics ) {
	ProcessingReport report;
	if ( text.size() >= MaximumArenaTextBytes ) {
		AddDiagnostic( diagnostics, DiagnosticCode::ArenaInputTooLarge, 1, 0 );
		report.rejected = 1;
		report.fatal = true;
		return report;
	}
	const std::size_t nul = text.find( '\0' );
	if ( nul != std::string_view::npos ) {
		text = text.substr( 0, nul );
	}

	ArenaTokenizer tokenizer( text, diagnostics );
	const std::size_t perFileCapacity = MaximumArenaRecords - catalog.Records().size();
	std::size_t parsedBlocks = 0;
	for ( ;; ) {
		const Token opening = tokenizer.Next();
		if ( opening.kind == TokenKind::End || opening.value.empty() ) {
			return report;
		}
		if ( opening.value != "{" ) {
			AddDiagnostic( diagnostics, DiagnosticCode::ArenaUnexpectedToken,
				opening.line, opening.offset );
			++report.rejected;
			// SV_ParseInfos stops the current source at the first non-'{' token.
			return report;
		}
		if ( parsedBlocks == perFileCapacity ) {
			AddDiagnostic( diagnostics, DiagnosticCode::ArenaTooManyRecords,
				opening.line, opening.offset );
			++report.rejected;
			report.fatal = true;
			return report;
		}
		++parsedBlocks;

		std::vector<RetailInfoPair> pairs;
		bool reachedEnd = false;
		for ( ;; ) {
			const Token key = tokenizer.Next();
			if ( key.kind == TokenKind::End || key.value.empty() ) {
				AddDiagnostic( diagnostics, DiagnosticCode::ArenaUnterminatedBlock,
					opening.line, opening.offset );
				reachedEnd = true;
				break;
			}
			if ( key.value == "}" ) {
				break;
			}
			const Token value = tokenizer.Next( false );
			RetailInfoSet( pairs, key.value,
				value.kind == TokenKind::End || value.value.empty()
					? "<NULL>" : value.value );
		}

		ArenaRecord record;
		record.map = RetailInfoValue( pairs, "map" );
		record.longName = RetailInfoValue( pairs, "longname" );
		record.type = RetailInfoValue( pairs, "type" );
		if ( record.map.empty() ) {
			AddDiagnostic( diagnostics, DiagnosticCode::ArenaMissingMap,
				opening.line, opening.offset );
			++report.rejected;
		} else if ( !catalog.Append( std::move( record ) ) ) {
			AddDiagnostic( diagnostics, DiagnosticCode::ArenaTooManyRecords,
				opening.line, opening.offset );
			++report.rejected;
			report.fatal = true;
			return report;
		} else {
			++report.accepted;
		}
		if ( reachedEnd ) {
			return report;
		}
	}
}

ProcessingReport ParseMapPoolText( std::string_view text,
		std::vector<MapPoolRow> &rows, std::vector<Diagnostic> *diagnostics ) {
	ProcessingReport report;
	rows.clear();
	if ( text.size() >= MaximumMapPoolTextBytes ) {
		AddDiagnostic( diagnostics, DiagnosticCode::MapPoolInputTooLarge, 1, 0 );
		report.rejected = 1;
		report.fatal = true;
		return report;
	}
	const std::size_t nul = text.find( '\0' );
	if ( nul != std::string_view::npos ) {
		text = text.substr( 0, nul );
	}

	std::size_t line = 1;
	std::size_t offset = 0;
	while ( offset < text.size() ) {
		const std::size_t lineOffset = offset;
		const std::size_t newline = text.find( '\n', offset );
		const std::size_t lineEnd = newline == std::string_view::npos
			? text.size() : newline;
		std::string_view value = text.substr( offset, lineEnd - offset );
		const std::size_t carriageReturn = value.find( '\r' );
		if ( carriageReturn != std::string_view::npos ) {
			value = value.substr( 0, carriageReturn );
		}
		offset = newline == std::string_view::npos ? text.size() : newline + 1;

		if ( value.empty() || value.front() == '#' ) {
			++line;
			continue;
		}

		const std::size_t separator = value.find( '|' );
		if ( separator == std::string_view::npos ) {
			AddDiagnostic( diagnostics, DiagnosticCode::MapPoolMalformedRow,
				line, lineOffset );
			++report.rejected;
			++line;
			continue;
		}

		const std::string_view map = value.substr( 0, separator );
		const std::string_view factory = value.substr( separator + 1 );
		rows.push_back( { std::string( map ), std::string( factory ), line } );
		++report.accepted;
		++line;
	}
	return report;
}

ProcessingReport ResolveMapPool( const std::vector<MapPoolRow> &rows,
		const RotationResolver &resolver, std::vector<RotationEntry> &entries,
		std::vector<Diagnostic> *diagnostics, ResolveOptions options ) {
	ProcessingReport report;
	entries.clear();
	if ( !resolver.map || !resolver.factory || !resolver.arenaSupport ) {
		AddDiagnostic( diagnostics, DiagnosticCode::ResolverUnavailable, 0, 0 );
		report.rejected = rows.size();
		report.fatal = true;
		return report;
	}

	entries.reserve( ( std::min )( rows.size(), MaximumMapPoolRows ) );
	for ( std::size_t i = 0; i < rows.size(); ++i ) {
		const MapPoolRow &row = rows[i];
		if ( entries.size() == MaximumMapPoolRows ) {
			AddDiagnostic( diagnostics, DiagnosticCode::MapPoolTooManyRows,
				row.sourceLine, 0 );
			report.rejected += rows.size() - i;
			break;
		}
		std::optional<MapMetadata> map;
		std::optional<FactoryMetadata> factory;
		ArenaSupport support = ArenaSupport::Unknown;
		try {
			factory = resolver.factory( row.factory );
		} catch ( ... ) {
			AddDiagnostic( diagnostics, DiagnosticCode::ResolverUnavailable,
				row.sourceLine, 0 );
			entries.clear();
			report.accepted = 0;
			report.rejected = rows.size();
			report.fatal = true;
			return report;
		}

		if ( !factory ) {
			AddDiagnostic( diagnostics, DiagnosticCode::FactoryNotFound,
				row.sourceLine, 0 );
			++report.rejected;
			continue;
		}
		if ( factory->hidden && !options.includeHiddenFactories ) {
			AddDiagnostic( diagnostics, DiagnosticCode::FactoryHidden,
				row.sourceLine, 0 );
			++report.rejected;
			continue;
		}
		if ( factory->baseGameType < MinimumRetailBaseGameType ||
			factory->baseGameType > MaximumRetailBaseGameType ) {
			AddDiagnostic( diagnostics, DiagnosticCode::InvalidBaseGameType,
				row.sourceLine, 0 );
			++report.rejected;
			continue;
		}
		try {
			map = resolver.map( row.map );
		} catch ( ... ) {
			AddDiagnostic( diagnostics, DiagnosticCode::ResolverUnavailable,
				row.sourceLine, 0 );
			entries.clear();
			report.accepted = 0;
			report.rejected = rows.size();
			report.fatal = true;
			return report;
		}
		if ( !map ) {
			AddDiagnostic( diagnostics, DiagnosticCode::MapNotFound,
				row.sourceLine, 0 );
			++report.rejected;
			continue;
		}

		try {
			support = resolver.arenaSupport( map->map, factory->baseGameType );
		} catch ( ... ) {
			AddDiagnostic( diagnostics, DiagnosticCode::ResolverUnavailable,
				row.sourceLine, 0 );
			entries.clear();
			report.accepted = 0;
			report.rejected = rows.size();
			report.fatal = true;
			return report;
		}
		if ( support == ArenaSupport::Unsupported ) {
			AddDiagnostic( diagnostics, DiagnosticCode::ArenaUnsupported,
				row.sourceLine, 0 );
			++report.rejected;
			continue;
		}
		if ( support == ArenaSupport::Unknown && !options.acceptUnknownArenas ) {
			AddDiagnostic( diagnostics, DiagnosticCode::ArenaUnknown,
				row.sourceLine, 0 );
			++report.rejected;
			continue;
		}

		RotationEntry entry;
		entry.requestedMap = row.map;
		entry.requestedFactory = row.factory;
		entry.map = RetailStoredText( map->map );
		entry.factory = RetailStoredText( factory->factory );
		entry.mapTitle = RetailStoredText( map->title );
		entry.factoryTitle = RetailStoredText( factory->title );
		entry.baseGameType = factory->baseGameType;
		entry.arenaSupport = support;
		entry.sourceLine = row.sourceLine;
		entries.push_back( std::move( entry ) );
		++report.accepted;
	}
	return report;
}

std::optional<std::size_t> SelectRotationIndex(
		const std::vector<RotationEntry> &entries, std::size_t randomIndex,
		std::string_view currentMap, bool excludeCurrentMap ) {
	const std::vector<std::size_t> candidates = SelectionCandidates(
		entries, currentMap, excludeCurrentMap );
	if ( candidates.empty() ) {
		return std::nullopt;
	}
	return candidates[randomIndex % candidates.size()];
}

RotationModel BuildRotationModel( const std::vector<RotationEntry> &entries,
		std::size_t initialSlot,
		const std::function<std::size_t()> &nextRandomIndex ) {
	RotationModel model;
	if ( initialSlot >= RotationSlotCount || entries.empty() ) {
		return model;
	}

	if ( entries.size() < 4 ) {
		for ( std::size_t index = initialSlot;
			index < entries.size() && initialSlot + model.count < RotationSlotCount;
			++index ) {
			model.indices[model.count++] = index;
		}
		return model;
	}

	if ( !nextRandomIndex ) {
		return model;
	}
	std::vector<bool> selected( entries.size(), false );
	std::size_t collisionCount = 0;
	while ( initialSlot + model.count < RotationSlotCount ) {
		const std::size_t index = nextRandomIndex() % entries.size();
		if ( selected[index] ) {
			// Retail rerolls collisions. Bound a hostile deterministic callback so
			// pure callers cannot hang, then select the first remaining entry.
			if ( ++collisionCount < entries.size() * 4096u ) {
				continue;
			}
			auto free = std::find( selected.begin(), selected.end(), false );
			if ( free == selected.end() ) {
				break;
			}
			model.indices[model.count++] = static_cast<std::size_t>(
				std::distance( selected.begin(), free ) );
			*free = true;
			collisionCount = 0;
			continue;
		}
		selected[index] = true;
		model.indices[model.count++] = index;
		collisionCount = 0;
	}
	return model;
}

} // namespace fnql::server::rotation
