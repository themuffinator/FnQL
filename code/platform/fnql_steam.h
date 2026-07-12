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

#ifndef FNQL_STEAM_H
#define FNQL_STEAM_H

#include "../qcommon/q_shared.h"
#include "fnql_steam_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*fnqlSteamEngineEventFn)(const fnqlSteamEvent_t *event,
	void *context);

void FNQL_Steam_Init(uint32_t roles);
void FNQL_Steam_Reconfigure(uint32_t roles);
void FNQL_Steam_Shutdown(void);
void FNQL_Steam_Pump(void);
void FNQL_Steam_SetEventSink(fnqlSteamEngineEventFn sink, void *context);
qboolean FNQL_Steam_AddEventSink(fnqlSteamEngineEventFn sink, void *context);
void FNQL_Steam_RemoveEventSink(fnqlSteamEngineEventFn sink, void *context);

qboolean FNQL_Steam_Available(uint64_t capability);
uint64_t FNQL_Steam_Capabilities(void);
qboolean FNQL_Steam_GetStatus(fnqlSteamStatus_t *status);
const char *FNQL_Steam_StatusDetail(void);

qboolean FNQL_Steam_IsSubscribedApp(uint32_t appId);
fnqlSteamResult_t FNQL_Steam_OpenOverlayUrl(const char *url);
fnqlSteamResult_t FNQL_Steam_OpenOverlayUser(const char *dialog, uint64_t steamId);
fnqlSteamResult_t FNQL_Steam_SetRichPresence(const char *key, const char *value);
fnqlSteamResult_t FNQL_Steam_RequestServers(uint32_t requestMode, uint32_t appId);
fnqlSteamResult_t FNQL_Steam_RefreshServers(void);
void FNQL_Steam_CancelServers(void);
fnqlSteamResult_t FNQL_Steam_CreateLobby(uint32_t lobbyType, uint32_t maxMembers);
fnqlSteamResult_t FNQL_Steam_JoinLobby(uint64_t lobbyId);
fnqlSteamResult_t FNQL_Steam_LeaveLobby(uint64_t lobbyId);
fnqlSteamResult_t FNQL_Steam_SetLobbyServer(uint64_t lobbyId, uint32_t ip,
	uint16_t port, uint64_t serverId);
fnqlSteamResult_t FNQL_Steam_InviteToLobby(uint64_t lobbyId, uint64_t steamId);
fnqlSteamResult_t FNQL_Steam_SendLobbyChat(uint64_t lobbyId, const void *bytes,
	uint32_t byteCount);
fnqlSteamResult_t FNQL_Steam_GetSubscribedItems(uint64_t *itemIds,
	uint32_t capacity, uint32_t *count);
fnqlSteamResult_t FNQL_Steam_GetItemInstallInfo(uint64_t itemId, char *folder,
	uint32_t folderCapacity, uint64_t *sizeOnDisk, uint32_t *timestamp);
fnqlSteamResult_t FNQL_Steam_GetItemDownloadInfo(uint64_t itemId,
	uint64_t *downloaded, uint64_t *total);
fnqlSteamResult_t FNQL_Steam_GetItemState(uint64_t itemId,
	uint32_t *stateFlags);
fnqlSteamResult_t FNQL_Steam_GetAvatarRGBA(uint64_t steamId,
	uint32_t avatarSize, void *rgba, uint32_t rgbaCapacity,
	uint32_t *width, uint32_t *height, uint32_t *requiredSize);
fnqlSteamResult_t FNQL_Steam_GetFriends(uint32_t friendFlags,
	fnqlSteamFriend_t *friends, uint32_t capacity, uint32_t *count);
fnqlSteamResult_t FNQL_Steam_GetFriend(uint64_t steamId,
	fnqlSteamFriend_t *friendInfo);
fnqlSteamResult_t FNQL_Steam_RequestUgcQuery(uint32_t rawFilter);
fnqlSteamResult_t FNQL_Steam_GetUgcQueryResults(fnqlSteamUgcItem_t *items,
	uint32_t capacity, uint32_t *count, uint32_t *totalMatching);
fnqlSteamResult_t FNQL_Steam_SendP2PPacket(uint32_t role, uint64_t remoteId,
	const void *data, uint32_t dataSize, uint32_t sendType, int32_t channel);
fnqlSteamResult_t FNQL_Steam_PeekP2PPacket(uint32_t role, int32_t channel,
	uint32_t *packetSize);
fnqlSteamResult_t FNQL_Steam_ReadP2PPacket(uint32_t role, int32_t channel,
	void *data, uint32_t capacity, uint32_t *dataSize, uint64_t *remoteId);
fnqlSteamResult_t FNQL_Steam_AcceptP2PSession(uint32_t role,
	uint64_t remoteId);
fnqlSteamResult_t FNQL_Steam_CloseP2PSession(uint32_t role,
	uint64_t remoteId);
fnqlSteamResult_t FNQL_Steam_StartVoiceRecording(void);
void FNQL_Steam_StopVoiceRecording(void);
fnqlSteamResult_t FNQL_Steam_GetCompressedVoice(void *data,
	uint32_t capacity, uint32_t *dataSize);
fnqlSteamResult_t FNQL_Steam_DecompressVoice(const void *compressed,
	uint32_t compressedSize, void *pcm, uint32_t pcmCapacity,
	uint32_t *pcmSize, uint32_t sampleRate);
uint32_t FNQL_Steam_GetVoiceSampleRate(void);
fnqlSteamResult_t FNQL_Steam_GetGameServerSteamId(uint64_t *steamId);
fnqlSteamResult_t FNQL_Steam_RequestClientUserStats(uint64_t steamId);
fnqlSteamResult_t FNQL_Steam_GetClientUserStatI32(uint64_t steamId,
	const char *name, int32_t *value);
fnqlSteamResult_t FNQL_Steam_GetClientUserStatF32(uint64_t steamId,
	const char *name, float *value);
fnqlSteamResult_t FNQL_Steam_GetClientUserAchievement(uint64_t steamId,
	const char *name, qboolean *unlocked, uint32_t *unlockTime);
fnqlSteamResult_t FNQL_Steam_GetAchievementDisplayAttribute(const char *name,
	const char *key, char *value, uint32_t valueCapacity);
fnqlSteamResult_t FNQL_Steam_SetClientAchievement(const char *name);
fnqlSteamResult_t FNQL_Steam_StoreClientStats(void);
fnqlSteamResult_t FNQL_Steam_ResetClientStats(qboolean includeAchievements);
fnqlSteamResult_t FNQL_Steam_GetLobbyMembers(uint64_t lobbyId,
	fnqlSteamLobbyMember_t *members, uint32_t capacity, uint32_t *count,
	uint64_t *ownerId, uint32_t *memberLimit);
fnqlSteamResult_t FNQL_Steam_GetLobbyData(uint64_t lobbyId,
	fnqlSteamLobbyData_t *items, uint32_t capacity, uint32_t *count);
fnqlSteamResult_t FNQL_Steam_SetLobbyData(uint64_t lobbyId, const char *key,
	const char *value);
fnqlSteamResult_t FNQL_Steam_InviteToGame(uint64_t steamId,
	const char *connectString);
fnqlSteamResult_t FNQL_Steam_HandleGameServerPacket(const void *data,
	uint32_t dataSize, uint32_t sourceIp, uint16_t sourcePort);
fnqlSteamResult_t FNQL_Steam_GetGameServerPacket(void *data,
	uint32_t capacity, uint32_t *dataSize, uint32_t *destinationIp,
	uint16_t *destinationPort);
fnqlSteamResult_t FNQL_Steam_SetGameServerKeyValue(const char *key,
	const char *value);
fnqlSteamResult_t FNQL_Steam_UpdateGameServerUser(uint64_t steamId,
	const char *playerName, uint32_t score);
fnqlSteamResult_t FNQL_Steam_GetGameServerPublicIp(uint32_t *publicIp);
fnqlSteamResult_t FNQL_Steam_CreateUnauthenticatedUser(uint64_t *steamId);
fnqlSteamResult_t FNQL_Steam_SetFavoriteServer(uint32_t appId, uint32_t ip,
	uint16_t connectionPort, uint16_t queryPort, qboolean add);
fnqlSteamResult_t FNQL_Steam_RequestServerDetails(uint32_t ip,
	uint16_t queryPort);
void FNQL_Steam_CancelServerDetails(void);
fnqlSteamResult_t FNQL_Steam_SetGameServerBotCount(uint32_t botCount);
fnqlSteamResult_t FNQL_Steam_SetLocalVoiceSpeaking(qboolean speaking);
fnqlSteamResult_t FNQL_Steam_DownloadItem(uint64_t itemId, qboolean highPriority);
fnqlSteamResult_t FNQL_Steam_SubscribeItem(uint64_t itemId);
fnqlSteamResult_t FNQL_Steam_UnsubscribeItem(uint64_t itemId);
fnqlSteamResult_t FNQL_Steam_RequestUserStats(uint64_t steamId);
fnqlSteamResult_t FNQL_Steam_GetUserStatI32(uint64_t steamId,
	const char *name, int32_t *value);
fnqlSteamResult_t FNQL_Steam_SetUserStatI32(uint64_t steamId,
	const char *name, int32_t value);
fnqlSteamResult_t FNQL_Steam_GetUserAchievement(uint64_t steamId,
	const char *name, qboolean *unlocked);
fnqlSteamResult_t FNQL_Steam_SetUserAchievement(uint64_t steamId,
	const char *name);
fnqlSteamResult_t FNQL_Steam_StoreUserStats(uint64_t steamId);
fnqlSteamResult_t FNQL_Steam_SerializeRetailJson(const void *retailValue,
	char *json, uint32_t jsonCapacity, uint32_t *jsonSize);
fnqlSteamResult_t FNQL_Steam_GetAuthTicket(void *ticket, uint32_t ticketCapacity,
	uint32_t *ticketSize, uint32_t *ticketHandle);
void FNQL_Steam_CancelAuthTicket(uint32_t ticketHandle);
fnqlSteamResult_t FNQL_Steam_BeginAuthSession(const void *ticket,
	uint32_t ticketSize, uint64_t steamId);
void FNQL_Steam_EndAuthSession(uint64_t steamId);
fnqlSteamResult_t FNQL_Steam_StartGameServer(
	const fnqlSteamGameServerConfig_t *config);
void FNQL_Steam_StopGameServer(void);
fnqlSteamResult_t FNQL_Steam_UpdateGameServer(
	const fnqlSteamGameServerConfig_t *config);

#ifdef __cplusplus
}
#endif

#endif
