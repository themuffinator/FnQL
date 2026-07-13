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

#include "fnql_steam.h"
#include "../qcommon/qcommon.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace {

constexpr std::size_t kRetailWorkshopLimit = 256;
constexpr std::uint32_t kSubscriptionSettleMsec = 1000;
constexpr std::uint32_t kSubscriptionTimeoutMsec = 30u * 60u * 1000u;
constexpr std::uint32_t kManualDownloadTimeoutMsec = 30u * 60u * 1000u;
constexpr std::uint32_t kSnapshotRetryMsec = 5000;
constexpr std::uint32_t kSnapshotPollMsec = 30u * 1000u;
constexpr std::uint32_t kMaxProviderSnapshot = 65536;

struct ManualDownloadState {
	std::uint64_t itemId{};
	std::uint32_t startedAt{};
	bool active{};
	bool completionHint{};
	bool failureHint{};
};

enum class InstalledItemStatus {
	Unavailable,
	NotInstalled,
	Installed
};

enum class InstalledRegistrationStatus {
	Unavailable,
	Rejected,
	Unchanged,
	Changed
};

enum class SnapshotRefreshResult {
	Retry,
	WaitingForInstall,
	Complete
};

struct SnapshotInstall {
	std::uint64_t itemId{};
	std::array<char, FNQL_STEAM_PATH_CAPACITY> folder{};
};

enum class SubscriptionAction {
	Subscribe,
	Unsubscribe
};

struct PendingSubscriptionAction {
	std::uint64_t itemId{};
	std::uint32_t startedAt{};
	std::uint32_t nextAt{};
	SubscriptionAction action{SubscriptionAction::Subscribe};
	bool active{};
};

struct SnapshotRefreshState {
	std::uint32_t nextAt{};
	bool active{};
	bool fallbackPoll{};
	bool restartFilesystem{};
};

ManualDownloadState manualDownload;
bool workshopInitialized;
bool eventSinkRegistered;
std::array<PendingSubscriptionAction, kRetailWorkshopLimit>
	pendingSubscriptionActions;
SnapshotRefreshState snapshotRefresh;

bool ResultStarted(fnqlSteamResult_t result) {
	return result == FNQL_STEAM_RESULT_OK || result == FNQL_STEAM_RESULT_PENDING;
}

void ClearProgressCvars() {
	Cvar_Set("cl_downloadName", "");
	Cvar_Set("cl_downloadItem", "");
	Cvar_Set("cl_downloadCount", "0");
	Cvar_Set("cl_downloadSize", "0");
}

void ResetManualDownload(bool clearProgress) {
	const bool ownedProgress = manualDownload.active || manualDownload.itemId != 0;
	manualDownload = {};
	if (clearProgress && ownedProgress) {
		ClearProgressCvars();
	}
}

void ResetRefreshState() {
	pendingSubscriptionActions = {};
	snapshotRefresh = {};
}

InstalledItemStatus ReadInstalledItem(std::uint64_t itemId,
	std::uint32_t *stateFlags,
	char *folder, std::uint32_t folderCapacity) {
	std::uint32_t flags = 0;
	std::uint64_t sizeOnDisk = 0;
	std::uint32_t timestamp = 0;

	if (!itemId || !stateFlags || !folder || folderCapacity == 0) {
		return InstalledItemStatus::Unavailable;
	}
	*stateFlags = 0;
	folder[0] = '\0';
	if (FNQL_Steam_GetItemState(itemId, &flags) != FNQL_STEAM_RESULT_OK) {
		return InstalledItemStatus::Unavailable;
	}
	*stateFlags = flags;
	if (!(flags & FNQL_STEAM_UGC_ITEM_STATE_INSTALLED)) {
		return InstalledItemStatus::NotInstalled;
	}
	const fnqlSteamResult_t installResult = FNQL_Steam_GetItemInstallInfo(
		itemId, folder, folderCapacity, &sizeOnDisk, &timestamp);
	folder[folderCapacity - 1] = '\0';
	if (installResult != FNQL_STEAM_RESULT_OK || !folder[0]) {
		return InstalledItemStatus::Unavailable;
	}
	return InstalledItemStatus::Installed;
}

InstalledRegistrationStatus RegisterInstalledItem(std::uint64_t itemId) {
	std::array<char, FNQL_STEAM_PATH_CAPACITY> folder{};
	std::uint32_t stateFlags = 0;
	if (ReadInstalledItem(itemId, &stateFlags, folder.data(),
		static_cast<std::uint32_t>(folder.size()))
		!= InstalledItemStatus::Installed) {
		return InstalledRegistrationStatus::Unavailable;
	}

	const fsWorkshopRegisterResult_t registration = FS_RegisterWorkshopInstall(
		itemId, folder.data(),
		(stateFlags & FNQL_STEAM_UGC_ITEM_STATE_SUBSCRIBED) ? qtrue : qfalse);
	if (registration == FS_WORKSHOP_REGISTER_REJECTED) {
		return InstalledRegistrationStatus::Rejected;
	}
	return registration == FS_WORKSHOP_REGISTER_CHANGED
		? InstalledRegistrationStatus::Changed
		: InstalledRegistrationStatus::Unchanged;
}

void SetManualProgress(std::uint64_t itemId) {
	Cvar_Set("cl_downloadItem", va("%llu",
		static_cast<unsigned long long>(itemId)));
	Cvar_Set("cl_downloadName", "Workshop item 1 of 1");
	Cvar_Set("cl_downloadCount", "0");
	Cvar_Set("cl_downloadSize", "0");
	Cvar_SetIntegerValue("cl_downloadTime", Com_Milliseconds());
}

void RefreshManualProgress() {
	std::uint64_t downloaded = 0;
	std::uint64_t total = 0;
	if (!manualDownload.active
		|| FNQL_Steam_GetItemDownloadInfo(manualDownload.itemId, &downloaded,
			&total) != FNQL_STEAM_RESULT_OK) {
		return;
	}
	Cvar_Set("cl_downloadCount", va("%llu",
		static_cast<unsigned long long>(downloaded)));
	Cvar_Set("cl_downloadSize", va("%llu",
		static_cast<unsigned long long>(total)));
}

bool SnapshotItems(std::vector<std::uint64_t> *items) {
	std::array<std::uint64_t, kRetailWorkshopLimit> fixed{};
	std::uint32_t count = 0;
	fnqlSteamResult_t result = FNQL_Steam_GetSubscribedItems(fixed.data(),
		static_cast<std::uint32_t>(fixed.size()), &count);

	if (result == FNQL_STEAM_RESULT_OK) {
		items->assign(fixed.begin(), fixed.begin()
			+ std::min<std::size_t>(count, fixed.size()));
		return true;
	}
	if (result != FNQL_STEAM_RESULT_BUFFER_TOO_SMALL || count == 0
		|| count > kMaxProviderSnapshot) {
		return false;
	}

	/* The provider ABI reports the complete required capacity.  Fetch it so
	 * providers with more than retail's 256 entries can still be truncated in
	 * the same deterministic order at the engine boundary. */
	std::vector<std::uint64_t> complete(count);
	result = FNQL_Steam_GetSubscribedItems(complete.data(), count, &count);
	if (result != FNQL_STEAM_RESULT_OK) {
		return false;
	}
	complete.resize(std::min<std::size_t>(count, complete.size()));
	if (complete.size() > kRetailWorkshopLimit) {
		Com_Printf(S_COLOR_YELLOW
			"WARNING: Steam reports %u subscribed Workshop items; mounting the retail limit of %u.\n",
			static_cast<unsigned int>(complete.size()),
			static_cast<unsigned int>(kRetailWorkshopLimit));
		complete.resize(kRetailWorkshopLimit);
	}
	items->swap(complete);
	return true;
}

SnapshotRefreshResult RefreshSubscribedSnapshot(bool restartFilesystem) {
	std::vector<std::uint64_t> itemIds;
	std::vector<SnapshotInstall> installs;
	bool waitingForInstall = false;
	/* A missing or temporarily unhealthy provider is not evidence that the
	 * user's subscriptions disappeared.  Preserve the last good snapshot; an
	 * authoritative successful empty result is what clears it. */
	if (!FNQL_Steam_Available(FNQL_STEAM_CAP_UGC) || !SnapshotItems(&itemIds)) {
		return SnapshotRefreshResult::Retry;
	}

	for (const std::uint64_t itemId : itemIds) {
		if (!itemId) {
			return SnapshotRefreshResult::Retry;
		}
		if (std::find_if(installs.begin(), installs.end(),
			[itemId](const SnapshotInstall &install) {
				return install.itemId == itemId;
			}) != installs.end()) {
			continue;
		}

		SnapshotInstall install{};
		install.itemId = itemId;
		std::uint32_t stateFlags = 0;
		const InstalledItemStatus status = ReadInstalledItem(itemId,
			&stateFlags, install.folder.data(),
			static_cast<std::uint32_t>(install.folder.size()));
		if (status == InstalledItemStatus::Unavailable) {
			return SnapshotRefreshResult::Retry;
		}
		if (status == InstalledItemStatus::NotInstalled) {
			waitingForInstall = true;
			continue;
		}
		installs.push_back(install);
	}

	FS_BeginWorkshopUpdate();
	for (const SnapshotInstall &install : installs) {
		if (FS_RegisterWorkshopInstall(install.itemId, install.folder.data(), qtrue)
			== FS_WORKSHOP_REGISTER_REJECTED) {
			FS_CancelWorkshopUpdate();
			return SnapshotRefreshResult::Retry;
		}
	}

	const bool changed = FS_EndWorkshopUpdate() != qfalse;
	if (changed && restartFilesystem && FS_Initialized()) {
		Com_Printf("Workshop subscription set changed; restarting the filesystem.\n");
		FS_Reload();
	}
	return waitingForInstall
		? SnapshotRefreshResult::WaitingForInstall
		: SnapshotRefreshResult::Complete;
}

void ScheduleSnapshotRefresh(bool restartFilesystem, std::uint32_t delayMsec) {
	const std::uint32_t now = static_cast<std::uint32_t>(Com_Milliseconds());
	const std::uint32_t scheduledAt = now + delayMsec;
	if (snapshotRefresh.fallbackPoll) {
		snapshotRefresh = {};
	}
	if (!snapshotRefresh.active) {
		snapshotRefresh.active = true;
		snapshotRefresh.nextAt = scheduledAt;
	} else if (!snapshotRefresh.nextAt
		|| static_cast<std::int32_t>(scheduledAt - snapshotRefresh.nextAt) < 0) {
		snapshotRefresh.nextAt = scheduledAt;
	}
	snapshotRefresh.restartFilesystem |= restartFilesystem;
}

void ScheduleFallbackSnapshotPoll(std::uint32_t delayMsec) {
	if (snapshotRefresh.active) {
		return;
	}
	const std::uint32_t now = static_cast<std::uint32_t>(Com_Milliseconds());
	snapshotRefresh.active = true;
	snapshotRefresh.fallbackPoll = true;
	snapshotRefresh.restartFilesystem = true;
	snapshotRefresh.nextAt = now + delayMsec;
}

bool CanTrackSubscriptionAction(std::uint64_t itemId) {
	return std::any_of(pendingSubscriptionActions.begin(),
		pendingSubscriptionActions.end(),
		[itemId](const PendingSubscriptionAction &pending) {
			return !pending.active || pending.itemId == itemId;
		});
}

void ScheduleSubscriptionRefresh(std::uint64_t itemId,
	SubscriptionAction action) {
	const std::uint32_t now = static_cast<std::uint32_t>(Com_Milliseconds());
	PendingSubscriptionAction *slot = nullptr;
	for (PendingSubscriptionAction &pending : pendingSubscriptionActions) {
		if (pending.active && pending.itemId == itemId) {
			slot = &pending;
			break;
		}
		if (!slot && !pending.active) {
			slot = &pending;
		}
	}
	if (!slot) {
		return;
	}
	*slot = {};
	slot->itemId = itemId;
	slot->startedAt = now;
	slot->nextAt = now + kSubscriptionSettleMsec;
	slot->action = action;
	slot->active = true;
}

bool FinishManualDownload() {
	const std::uint64_t itemId = manualDownload.itemId;
	const InstalledRegistrationStatus registration = RegisterInstalledItem(itemId);
	if (registration == InstalledRegistrationStatus::Unavailable) {
		return false;
	}
	if (registration == InstalledRegistrationStatus::Rejected) {
		Com_Printf(S_COLOR_YELLOW
			"WARNING: Workshop item %llu has an invalid or conflicting install folder.\n",
			static_cast<unsigned long long>(itemId));
		ResetManualDownload(true);
		return false;
	}
	Com_Printf("Steamworks download complete: %llu\n",
		static_cast<unsigned long long>(itemId));
	ResetManualDownload(true);
	/* FS_Reload preserves the current pure checksum feed and lets the retained
	 * registration take the same startup mount path as subscribed content. */
	if (registration == InstalledRegistrationStatus::Changed) {
		FS_Reload();
	}
	return true;
}

void WorkshopProviderEvent(const fnqlSteamEvent_t *event, void *) {
	if (!event || event->size < sizeof(*event)) {
		return;
	}
	if (event->type == FNQL_STEAM_EVENT_PROVIDER_STOPPED) {
		ResetManualDownload(true);
		ResetRefreshState();
		return;
	}
	if (event->type == FNQL_STEAM_EVENT_PROVIDER_READY) {
		ScheduleSnapshotRefresh(true, 1u);
		return;
	}
	if (event->type == FNQL_STEAM_EVENT_GAME_SERVER_CONNECTED
		&& event->result == FNQL_STEAM_RESULT_OK) {
		ScheduleSnapshotRefresh(true, 1u);
		return;
	}
	if (event->type == FNQL_STEAM_EVENT_UGC_ITEM_INSTALLED) {
		if (manualDownload.active && event->subject_id == manualDownload.itemId) {
			if (event->result == FNQL_STEAM_RESULT_OK) {
				manualDownload.completionHint = true;
			} else {
				manualDownload.failureHint = true;
			}
			return;
		}
		if (event->result == FNQL_STEAM_RESULT_OK) {
			ScheduleSnapshotRefresh(true, 1u);
		}
		return;
	}
	if (manualDownload.active && event->subject_id == manualDownload.itemId
		&& event->type == FNQL_STEAM_EVENT_UGC_DOWNLOAD_COMPLETE) {
		if (event->result == FNQL_STEAM_RESULT_OK) {
			manualDownload.completionHint = true;
		} else {
			manualDownload.failureHint = true;
		}
	}
}

} // namespace

extern "C" {

void Com_WorkshopInit(void) {
	if (workshopInitialized) {
		return;
	}
	if (!eventSinkRegistered) {
		eventSinkRegistered = FNQL_Steam_AddEventSink(WorkshopProviderEvent, nullptr)
			!= qfalse;
		if (!eventSinkRegistered) {
			Com_Printf(S_COLOR_YELLOW
				"Workshop lifecycle could not register for provider events; polling remains active.\n");
		}
	}
	workshopInitialized = true;
	const SnapshotRefreshResult result = RefreshSubscribedSnapshot(true);
	if (result != SnapshotRefreshResult::Complete) {
		ScheduleSnapshotRefresh(true, kSubscriptionSettleMsec);
	} else {
		ScheduleFallbackSnapshotPoll(kSnapshotPollMsec);
	}
}

void Com_WorkshopProviderChanged(void) {
	ResetManualDownload(true);
	ResetRefreshState();
	if (!workshopInitialized) {
		return;
	}
	ScheduleSnapshotRefresh(true, 1u);
}

void Com_WorkshopShutdown(void) {
	if (eventSinkRegistered) {
		FNQL_Steam_RemoveEventSink(WorkshopProviderEvent, nullptr);
	}
	eventSinkRegistered = false;
	workshopInitialized = false;
	ResetRefreshState();
	ResetManualDownload(true);
}

void Com_WorkshopFrame(void) {
	if (!workshopInitialized) {
		return;
	}
	const std::uint32_t now = static_cast<std::uint32_t>(Com_Milliseconds());
	for (PendingSubscriptionAction &pending : pendingSubscriptionActions) {
		if (!pending.active || !pending.nextAt
			|| static_cast<std::int32_t>(now - pending.nextAt) < 0) {
			continue;
		}

		std::uint32_t stateFlags = 0;
		bool settled = false;
		if (FNQL_Steam_GetItemState(pending.itemId, &stateFlags)
			== FNQL_STEAM_RESULT_OK) {
			settled = pending.action == SubscriptionAction::Subscribe
				? (stateFlags & FNQL_STEAM_UGC_ITEM_STATE_SUBSCRIBED) != 0
					&& (stateFlags & FNQL_STEAM_UGC_ITEM_STATE_INSTALLED) != 0
				: !(stateFlags & FNQL_STEAM_UGC_ITEM_STATE_SUBSCRIBED);
		}
		if (settled) {
			const bool restartFilesystem =
				pending.action == SubscriptionAction::Subscribe;
			pending = {};
			ScheduleSnapshotRefresh(restartFilesystem, 1u);
			continue;
		}
		if (static_cast<std::uint32_t>(now - pending.startedAt)
			>= kSubscriptionTimeoutMsec) {
			Com_Printf(S_COLOR_YELLOW
				"Workshop %s state for item %llu did not settle before the timeout.\n",
				pending.action == SubscriptionAction::Subscribe
					? "subscription" : "unsubscription",
				static_cast<unsigned long long>(pending.itemId));
			pending = {};
			continue;
		}
		pending.nextAt = now + kSubscriptionSettleMsec;
	}

	if (snapshotRefresh.active && snapshotRefresh.nextAt
		&& static_cast<std::int32_t>(now - snapshotRefresh.nextAt) >= 0) {
		if (Cvar_VariableIntegerValue("cl_workshopDownloadActive")) {
			snapshotRefresh.nextAt = now + kSubscriptionSettleMsec;
		} else {
			const SnapshotRefreshResult result = RefreshSubscribedSnapshot(
				snapshotRefresh.restartFilesystem);
			if (result == SnapshotRefreshResult::Complete) {
				snapshotRefresh = {};
				ScheduleFallbackSnapshotPoll(kSnapshotPollMsec);
			} else {
				snapshotRefresh.nextAt = now
					+ (result == SnapshotRefreshResult::WaitingForInstall
						? kSubscriptionSettleMsec : kSnapshotRetryMsec);
			}
		}
	}
	if (!manualDownload.active) {
		return;
	}

	RefreshManualProgress();
	std::uint32_t stateFlags = 0;
	if (FNQL_Steam_GetItemState(manualDownload.itemId, &stateFlags)
			== FNQL_STEAM_RESULT_OK
		&& (stateFlags & FNQL_STEAM_UGC_ITEM_STATE_INSTALLED)) {
		FinishManualDownload();
		return;
	}
	if (manualDownload.failureHint) {
		Com_Printf(S_COLOR_YELLOW "Steamworks download failed for item %llu.\n",
			static_cast<unsigned long long>(manualDownload.itemId));
		ResetManualDownload(true);
		return;
	}
	if (static_cast<std::uint32_t>(now - manualDownload.startedAt)
		>= kManualDownloadTimeoutMsec) {
		Com_Printf(S_COLOR_YELLOW "Steamworks download timed out for item %llu.\n",
			static_cast<unsigned long long>(manualDownload.itemId));
		ResetManualDownload(true);
	}
}

void Com_WorkshopClaimConnectionDownloads(void) {
	if (!manualDownload.active) {
		return;
	}
	Com_Printf("Connection-required Workshop downloads are taking ownership from the explicit download of item %llu.\n",
		static_cast<unsigned long long>(manualDownload.itemId));
	ResetManualDownload(true);
}

qboolean Com_WorkshopDownloadItem(std::uint64_t itemId) {
	if (!itemId || !FNQL_Steam_Available(FNQL_STEAM_CAP_UGC)) {
		return qfalse;
	}
	if (Cvar_VariableIntegerValue("cl_workshopDownloadActive")) {
		Com_Printf("Steamworks downloads are already active, ignoring an explicit download request for %llu\n",
			static_cast<unsigned long long>(itemId));
		return qfalse;
	}
	if (manualDownload.active) {
		Com_Printf("Steamworks download for item %llu is already active.\n",
			static_cast<unsigned long long>(manualDownload.itemId));
		return qfalse;
	}

	std::uint32_t stateFlags = 0;
	if (FNQL_Steam_GetItemState(itemId, &stateFlags) == FNQL_STEAM_RESULT_OK
		&& (stateFlags & FNQL_STEAM_UGC_ITEM_STATE_INSTALLED)) {
		Com_Printf("Workshop item %llu: in cache.\n",
			static_cast<unsigned long long>(itemId));
		manualDownload.itemId = itemId;
		manualDownload.startedAt = static_cast<std::uint32_t>(Com_Milliseconds());
		manualDownload.active = true;
		SetManualProgress(itemId);
		return (FinishManualDownload() || manualDownload.active) ? qtrue : qfalse;
	}

	const fnqlSteamResult_t result = FNQL_Steam_DownloadItem(itemId, qtrue);
	if (!ResultStarted(result)) {
		return qfalse;
	}
	manualDownload.itemId = itemId;
	manualDownload.startedAt = static_cast<std::uint32_t>(Com_Milliseconds());
	manualDownload.active = true;
	SetManualProgress(itemId);
	Com_Printf("Workshop item %llu: requesting download.\n",
		static_cast<unsigned long long>(itemId));
	return qtrue;
}

qboolean Com_WorkshopSubscribeItem(std::uint64_t itemId) {
	if (!itemId || !FNQL_Steam_Available(FNQL_STEAM_CAP_UGC)) {
		return qfalse;
	}
	if (!CanTrackSubscriptionAction(itemId)) {
		Com_Printf(S_COLOR_YELLOW
			"WARNING: The Workshop subscription tracking limit (%u) is active; try again after an earlier request settles.\n",
			static_cast<unsigned int>(kRetailWorkshopLimit));
		return qfalse;
	}
	const fnqlSteamResult_t result = FNQL_Steam_SubscribeItem(itemId);
	if (!ResultStarted(result)) {
		return qfalse;
	}
	Com_Printf("Subscribing to Workshop item %llu.\n",
		static_cast<unsigned long long>(itemId));
	ScheduleSubscriptionRefresh(itemId, SubscriptionAction::Subscribe);
	return qtrue;
}

qboolean Com_WorkshopUnsubscribeItem(std::uint64_t itemId) {
	if (!itemId || !FNQL_Steam_Available(FNQL_STEAM_CAP_UGC)) {
		return qfalse;
	}
	if (!CanTrackSubscriptionAction(itemId)) {
		Com_Printf(S_COLOR_YELLOW
			"WARNING: The Workshop subscription tracking limit (%u) is active; try again after an earlier request settles.\n",
			static_cast<unsigned int>(kRetailWorkshopLimit));
		return qfalse;
	}
	const fnqlSteamResult_t result = FNQL_Steam_UnsubscribeItem(itemId);
	if (!ResultStarted(result)) {
		return qfalse;
	}
	Com_Printf("Unsubscribing from Workshop item %llu.\n",
		static_cast<unsigned long long>(itemId));
	/* Retail deliberately leaves the current mount intact until a later FS
	 * restart.  Refresh only the future subscribed snapshot after Steam settles. */
	ScheduleSubscriptionRefresh(itemId, SubscriptionAction::Unsubscribe);
	return qtrue;
}

} // extern "C"
