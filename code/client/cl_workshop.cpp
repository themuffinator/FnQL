/*
===========================================================================
Copyright (C) 2026 FnQL contributors

FnQL is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.
===========================================================================
*/

extern "C" {
#include "client.h"
}

#include "../platform/fnql_steam.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <system_error>

namespace {

constexpr std::size_t kMaxRequiredItems = 256;
constexpr std::size_t kInstallFolderCapacity = FNQL_STEAM_PATH_CAPACITY;
constexpr std::uint32_t kDownloadTimeoutMsec = 30u * 60u * 1000u;

enum class ItemDisposition : std::uint8_t {
	Unknown,
	Cached,
	Queued,
	Downloading,
	Complete,
	Failed
};

enum class QueuePhase : std::uint8_t {
	Idle,
	Downloading,
	WaitAfterRestart
};

struct RequiredItem {
	std::uint64_t id = 0;
	ItemDisposition disposition = ItemDisposition::Unknown;
};

struct ParseReport {
	std::size_t count = 0;
	std::size_t malformed = 0;
	std::size_t duplicates = 0;
	std::size_t excess = 0;
};

struct WorkshopQueue {
	std::array<RequiredItem, kMaxRequiredItems> items{};
	std::size_t itemCount = 0;
	std::size_t activeIndex = kMaxRequiredItems;
	QueuePhase phase = QueuePhase::Idle;
	bool initialized = false;
	bool eventSinkRegistered = false;
	bool providerStopped = false;
	bool callbackFailure = false;
	bool callbackCompletionHint = false;
	bool downloadAttempted = false;
	bool requestedAny = false;
	bool filesystemRestartRequired = false;
	std::uint32_t activeStartedAt = 0;
};

WorkshopQueue workshopQueue;

static bool IsAsciiWhitespace( char value ) {
	switch ( value ) {
		case ' ':
		case '\t':
		case '\n':
		case '\r':
		case '\f':
		case '\v':
			return true;
		default:
			return false;
	}
}

static ParseReport ParseRequiredItems( const char *text,
	std::uint64_t *items, std::size_t capacity ) {
	ParseReport report{};
	if ( !text || !items || capacity == 0 ) {
		return report;
	}

	const char *cursor = text;
	while ( *cursor ) {
		while ( *cursor && IsAsciiWhitespace( *cursor ) ) {
			++cursor;
		}
		if ( !*cursor ) {
			break;
		}

		const char *tokenBegin = cursor;
		while ( *cursor && !IsAsciiWhitespace( *cursor ) ) {
			++cursor;
		}

		std::uint64_t itemId = 0;
		const std::from_chars_result parsed = std::from_chars(
			tokenBegin, cursor, itemId, 10 );
		if ( parsed.ec != std::errc{} || parsed.ptr != cursor || itemId == 0 ) {
			++report.malformed;
			continue;
		}

		bool duplicate = false;
		for ( std::size_t i = 0; i < report.count; ++i ) {
			if ( items[i] == itemId ) {
				duplicate = true;
				break;
			}
		}
		if ( duplicate ) {
			++report.duplicates;
			continue;
		}

		if ( report.count == capacity ) {
			++report.excess;
			continue;
		}
		items[report.count++] = itemId;
	}

	return report;
}

static void SetQueueActive( bool active ) {
	Cvar_Set( "cl_workshopDownloadActive", active ? "1" : "0" );
}

static void ClearProgressCvars() {
	Cvar_Set( "cl_downloadItem", "" );
	Cvar_Set( "cl_downloadName", "" );
	Cvar_Set( "cl_downloadCount", "0" );
	Cvar_Set( "cl_downloadSize", "0" );
}

static void ResetQueueState() {
	const bool ownedProgress = workshopQueue.phase != QueuePhase::Idle;
	workshopQueue.items = {};
	workshopQueue.itemCount = 0;
	workshopQueue.activeIndex = kMaxRequiredItems;
	workshopQueue.phase = QueuePhase::Idle;
	workshopQueue.providerStopped = false;
	workshopQueue.callbackFailure = false;
	workshopQueue.callbackCompletionHint = false;
	workshopQueue.downloadAttempted = false;
	workshopQueue.requestedAny = false;
	workshopQueue.filesystemRestartRequired = false;
	workshopQueue.activeStartedAt = 0;
	SetQueueActive( false );
	if ( ownedProgress ) {
		ClearProgressCvars();
	}
}

static void SetProgressItem( std::size_t index ) {
	char itemId[32];
	char name[64];
	Com_sprintf( itemId, sizeof( itemId ), "%llu",
		static_cast<unsigned long long>( workshopQueue.items[index].id ) );
	Com_sprintf( name, sizeof( name ), "Workshop item %u of %u",
		static_cast<unsigned int>( index + 1 ),
		static_cast<unsigned int>( workshopQueue.itemCount ) );
	Cvar_Set( "cl_downloadItem", itemId );
	Cvar_Set( "cl_downloadName", name );
	Cvar_Set( "cl_downloadCount", "0" );
	Cvar_Set( "cl_downloadSize", "0" );
	Cvar_SetIntegerValue( "cl_downloadTime", cls.realtime );
}

static void UpdateProgress() {
	if ( workshopQueue.phase != QueuePhase::Downloading ||
		workshopQueue.activeIndex >= workshopQueue.itemCount ) {
		return;
	}

	std::uint64_t downloaded = 0;
	std::uint64_t total = 0;
	if ( FNQL_Steam_GetItemDownloadInfo(
		workshopQueue.items[workshopQueue.activeIndex].id,
		&downloaded, &total ) != FNQL_STEAM_RESULT_OK ) {
		return;
	}

	char downloadedText[32];
	char totalText[32];
	Com_sprintf( downloadedText, sizeof( downloadedText ), "%llu",
		static_cast<unsigned long long>( downloaded ) );
	Com_sprintf( totalText, sizeof( totalText ), "%llu",
		static_cast<unsigned long long>( total ) );
	Cvar_Set( "cl_downloadCount", downloadedText );
	Cvar_Set( "cl_downloadSize", totalText );
}

static bool RegisterInstalledItem( RequiredItem &item,
	std::uint32_t stateFlags ) {
	std::array<char, kInstallFolderCapacity> folder{};
	std::uint64_t sizeOnDisk = 0;
	std::uint32_t timestamp = 0;
	if ( FNQL_Steam_GetItemInstallInfo( item.id, folder.data(),
		static_cast<std::uint32_t>( folder.size() ), &sizeOnDisk,
		&timestamp ) != FNQL_STEAM_RESULT_OK ) {
		folder.back() = '\0';
		Com_Printf( S_COLOR_YELLOW
			"WARNING: Workshop item %llu is installed but its install folder is unavailable.\n",
			static_cast<unsigned long long>( item.id ) );
		return false;
	}
	folder.back() = '\0';
	if ( !folder[0] ) {
		Com_Printf( S_COLOR_YELLOW
			"WARNING: Workshop item %llu is installed but its install folder is unavailable.\n",
			static_cast<unsigned long long>( item.id ) );
		return false;
	}

	const fsWorkshopRegisterResult_t registration = FS_RegisterWorkshopInstall(
		item.id, folder.data(),
		( stateFlags & FNQL_STEAM_UGC_ITEM_STATE_SUBSCRIBED ) ? qtrue : qfalse );
	if ( registration == FS_WORKSHOP_REGISTER_REJECTED ) {
		Com_Printf( S_COLOR_YELLOW
			"WARNING: Workshop item %llu has an invalid or conflicting install folder.\n",
			static_cast<unsigned long long>( item.id ) );
		return false;
	}
	if ( registration == FS_WORKSHOP_REGISTER_CHANGED ) {
		workshopQueue.filesystemRestartRequired = true;
	}
	return true;
}

static bool QueryItemState( RequiredItem &item, std::uint32_t &stateFlags ) {
	stateFlags = 0;
	if ( FNQL_Steam_GetItemState( item.id, &stateFlags ) == FNQL_STEAM_RESULT_OK ) {
		return true;
	}
	Com_Printf( S_COLOR_YELLOW
		"WARNING: Unable to query Workshop item %llu; continuing with ordinary pak validation.\n",
		static_cast<unsigned long long>( item.id ) );
	return false;
}

static bool StartNextDownload() {
	for ( std::size_t i = 0; i < workshopQueue.itemCount; ++i ) {
		RequiredItem &item = workshopQueue.items[i];
		if ( item.disposition != ItemDisposition::Queued ) {
			continue;
		}

		Com_Printf( workshopQueue.downloadAttempted
			? "Workshop item %llu was queued, requesting download.\n"
			: "Workshop item %llu: requesting download.\n",
			static_cast<unsigned long long>( item.id ) );
		workshopQueue.downloadAttempted = true;
		const fnqlSteamResult_t result = FNQL_Steam_DownloadItem( item.id, qtrue );
		if ( result != FNQL_STEAM_RESULT_OK && result != FNQL_STEAM_RESULT_PENDING ) {
			Com_Printf( S_COLOR_YELLOW
				"WARNING: Workshop item %llu download request failed (%d).\n",
				static_cast<unsigned long long>( item.id ), result );
			item.disposition = ItemDisposition::Failed;
			continue;
		}

		workshopQueue.requestedAny = true;
		workshopQueue.activeIndex = i;
		workshopQueue.callbackFailure = false;
		workshopQueue.callbackCompletionHint = false;
		workshopQueue.activeStartedAt =
			static_cast<std::uint32_t>( Com_Milliseconds() );
		workshopQueue.phase = QueuePhase::Downloading;
		item.disposition = ItemDisposition::Downloading;
		SetProgressItem( i );
		return true;
	}

	return false;
}

static void ResumeLegacyDownloads() {
	ResetQueueState();
	CL_ResumeDownloadsAfterWorkshop();
}

static void FinishQueue() {
	workshopQueue.activeIndex = kMaxRequiredItems;
	ClearProgressCvars();
	if ( !workshopQueue.requestedAny &&
		!workshopQueue.filesystemRestartRequired ) {
		ResumeLegacyDownloads();
		return;
	}

	Com_Printf( "Steamworks downloads complete - FS restart is required\n" );
	FS_Restart( clc.checksumFeed );
	workshopQueue.phase = QueuePhase::WaitAfterRestart;
}

static bool PreflightQueue() {
	std::size_t missingCount = 0;
	for ( std::size_t i = 0; i < workshopQueue.itemCount; ++i ) {
		RequiredItem &item = workshopQueue.items[i];
		std::uint32_t stateFlags = 0;
		if ( !QueryItemState( item, stateFlags ) ) {
			return false;
		}
		if ( stateFlags & FNQL_STEAM_UGC_ITEM_STATE_INSTALLED ) {
			if ( !RegisterInstalledItem( item, stateFlags ) ) {
				return false;
			}
			item.disposition = ItemDisposition::Cached;
			Com_Printf( "Workshop item %llu in cache.\n",
				static_cast<unsigned long long>( item.id ) );
			continue;
		}

		item.disposition = ItemDisposition::Queued;
		if ( missingCount++ != 0 ) {
			Com_Printf( "Workshop item %llu: queueing download.\n",
				static_cast<unsigned long long>( item.id ) );
		}
	}

	if ( missingCount == 0 ) {
		return false;
	}
	return StartNextDownload();
}

static void WorkshopSteamEvent( const fnqlSteamEvent_t *event, void * ) {
	if ( !event || workshopQueue.phase == QueuePhase::Idle ) {
		return;
	}
	if ( event->type == FNQL_STEAM_EVENT_PROVIDER_STOPPED ) {
		workshopQueue.providerStopped = true;
		return;
	}
	if ( workshopQueue.phase != QueuePhase::Downloading ||
		workshopQueue.activeIndex >= workshopQueue.itemCount ||
		event->subject_id != workshopQueue.items[workshopQueue.activeIndex].id ) {
		return;
	}
	if ( event->type == FNQL_STEAM_EVENT_UGC_DOWNLOAD_COMPLETE &&
		event->result != FNQL_STEAM_RESULT_OK ) {
		workshopQueue.callbackFailure = true;
		return;
	}
	if ( ( event->type == FNQL_STEAM_EVENT_UGC_DOWNLOAD_COMPLETE ||
		event->type == FNQL_STEAM_EVENT_UGC_ITEM_INSTALLED ) &&
		event->result == FNQL_STEAM_RESULT_OK ) {
		// Treat completion callbacks only as a prompt to poll authoritative state.
		workshopQueue.callbackCompletionHint = true;
	}
}

} // namespace

int CL_ParseRequiredWorkshopItems( const char *text,
	unsigned long long *items, int capacity ) {
	if ( capacity <= 0 || !items ) {
		return 0;
	}
	const std::size_t boundedCapacity = std::min<std::size_t>(
		static_cast<std::size_t>( capacity ), kMaxRequiredItems );
	std::array<std::uint64_t, kMaxRequiredItems> parsed{};
	const ParseReport report = ParseRequiredItems( text, parsed.data(), boundedCapacity );
	for ( std::size_t i = 0; i < report.count; ++i ) {
		items[i] = static_cast<unsigned long long>( parsed[i] );
	}
	return static_cast<int>( report.count );
}

void CL_Workshop_Init( void ) {
	if ( workshopQueue.initialized ) {
		return;
	}
	workshopQueue.initialized = true;
	Cvar_Get( "cl_workshopDownloadActive", "0",
		CVAR_ROM | CVAR_NORESTART | CVAR_NOTABCOMPLETE );
	SetQueueActive( false );
	workshopQueue.eventSinkRegistered =
		FNQL_Steam_AddEventSink( WorkshopSteamEvent, nullptr ) != qfalse;
	if ( !workshopQueue.eventSinkRegistered ) {
		Com_Printf( S_COLOR_YELLOW
			"WARNING: Workshop download event observer is unavailable.\n" );
	}
}

void CL_Workshop_Shutdown( void ) {
	if ( workshopQueue.eventSinkRegistered ) {
		FNQL_Steam_RemoveEventSink( WorkshopSteamEvent, nullptr );
	}
	ResetQueueState();
	workshopQueue.initialized = false;
	workshopQueue.eventSinkRegistered = false;
}

void CL_Workshop_Reset( void ) {
	ResetQueueState();
}

qboolean CL_WorkshopDownloadsActive( void ) {
	return workshopQueue.phase != QueuePhase::Idle ? qtrue : qfalse;
}

qboolean CL_Workshop_BeginRequiredDownloads( const char *requiredItems ) {
	ResetQueueState();
	if ( !requiredItems || !requiredItems[0] ) {
		return qfalse;
	}

	std::array<std::uint64_t, kMaxRequiredItems> parsed{};
	const ParseReport report = ParseRequiredItems(
		requiredItems, parsed.data(), parsed.size() );
	if ( report.malformed != 0 ) {
		Com_Printf( S_COLOR_YELLOW
			"WARNING: Ignored %u malformed Workshop item ID%s from the server.\n",
			static_cast<unsigned int>( report.malformed ),
			report.malformed == 1 ? "" : "s" );
	}
	if ( report.duplicates != 0 ) {
		Com_DPrintf( "Ignored %u duplicate Workshop item ID%s from the server.\n",
			static_cast<unsigned int>( report.duplicates ),
			report.duplicates == 1 ? "" : "s" );
	}
	if ( report.excess != 0 ) {
		Com_Printf( S_COLOR_YELLOW
			"WARNING: Server Workshop item list exceeds the %u-item compatibility limit; extra IDs were ignored.\n",
			static_cast<unsigned int>( kMaxRequiredItems ) );
	}
	if ( report.count == 0 ) {
		return qfalse;
	}

	Com_Printf( "Server requires the following workshop items: %s\n",
		requiredItems );
	if ( !FNQL_Steam_Available( FNQL_STEAM_CAP_UGC ) ) {
		Com_Printf( S_COLOR_YELLOW
			"WARNING: Steam Workshop downloads are unavailable; continuing with ordinary pak validation.\n" );
		return qfalse;
	}
	Com_WorkshopClaimConnectionDownloads();

	workshopQueue.itemCount = report.count;
	for ( std::size_t i = 0; i < report.count; ++i ) {
		workshopQueue.items[i].id = parsed[i];
	}
	workshopQueue.phase = QueuePhase::Downloading;
	SetQueueActive( true );
	cls.state = CA_CONNECTED;

	if ( !PreflightQueue() ) {
		if ( workshopQueue.filesystemRestartRequired ) {
			FinishQueue();
			return qtrue;
		}
		ResetQueueState();
		return qfalse;
	}
	return qtrue;
}

void CL_Workshop_Frame( void ) {
	if ( workshopQueue.phase == QueuePhase::Idle ) {
		return;
	}
	if ( workshopQueue.phase == QueuePhase::WaitAfterRestart ) {
		Com_Printf( "Download completed for all steamworks items\n" );
		ResumeLegacyDownloads();
		return;
	}
	if ( workshopQueue.providerStopped ||
		!FNQL_Steam_Available( FNQL_STEAM_CAP_UGC ) ) {
		Com_Printf( S_COLOR_YELLOW
			"WARNING: Steam Workshop provider stopped during download; continuing with ordinary pak validation.\n" );
		FinishQueue();
		return;
	}
	if ( workshopQueue.activeIndex >= workshopQueue.itemCount ) {
		FinishQueue();
		return;
	}

	RequiredItem &item = workshopQueue.items[workshopQueue.activeIndex];
	std::uint32_t stateFlags = 0;
	if ( !QueryItemState( item, stateFlags ) ) {
		FinishQueue();
		return;
	}
	if ( stateFlags & FNQL_STEAM_UGC_ITEM_STATE_INSTALLED ) {
		if ( !RegisterInstalledItem( item, stateFlags ) ) {
			FinishQueue();
			return;
		}
		Com_Printf( "Workshop item %llu: download complete.\n",
			static_cast<unsigned long long>( item.id ) );
		item.disposition = ItemDisposition::Complete;
		workshopQueue.activeIndex = kMaxRequiredItems;
		workshopQueue.callbackFailure = false;
		workshopQueue.callbackCompletionHint = false;
		if ( !StartNextDownload() ) {
			FinishQueue();
		}
		return;
	}
	if ( workshopQueue.callbackFailure ) {
		Com_Printf( S_COLOR_YELLOW
			"WARNING: Workshop item %llu download failed; advancing the queue.\n",
			static_cast<unsigned long long>( item.id ) );
		item.disposition = ItemDisposition::Failed;
		workshopQueue.activeIndex = kMaxRequiredItems;
		workshopQueue.callbackFailure = false;
		workshopQueue.callbackCompletionHint = false;
		if ( !StartNextDownload() ) {
			FinishQueue();
		}
		return;
	}
	const std::uint32_t now = static_cast<std::uint32_t>( Com_Milliseconds() );
	if ( static_cast<std::uint32_t>( now - workshopQueue.activeStartedAt )
		>= kDownloadTimeoutMsec ) {
		Com_Printf( S_COLOR_YELLOW
			"WARNING: Workshop item %llu download timed out; advancing the queue.\n",
			static_cast<unsigned long long>( item.id ) );
		item.disposition = ItemDisposition::Failed;
		workshopQueue.activeIndex = kMaxRequiredItems;
		workshopQueue.callbackFailure = false;
		workshopQueue.callbackCompletionHint = false;
		workshopQueue.activeStartedAt = 0;
		if ( !StartNextDownload() ) {
			FinishQueue();
		}
		return;
	}

	UpdateProgress();
}
