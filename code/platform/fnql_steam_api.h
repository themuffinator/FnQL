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

#ifndef FNQL_STEAM_API_H
#define FNQL_STEAM_API_H

/*
 * Public ABI between FnQL and an independently distributed Steam provider.
 * Keep this header C-compatible: providers may use any implementation language
 * and must never expose C++ objects, compiler-specific bools, or ownership.
 */

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
# define FNQL_STEAM_CALL __cdecl
# if defined(FNQL_STEAM_PROVIDER_EXPORTS)
#  define FNQL_STEAM_EXPORT __declspec(dllexport)
# else
#  define FNQL_STEAM_EXPORT
# endif
#else
# define FNQL_STEAM_CALL
# if defined(FNQL_STEAM_PROVIDER_EXPORTS)
#  define FNQL_STEAM_EXPORT __attribute__((visibility("default")))
# else
#  define FNQL_STEAM_EXPORT
# endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define FNQL_STEAM_ABI_VERSION 0x00010000u
#define FNQL_STEAM_GET_PROVIDER_SYMBOL "FnQLSteam_GetProvider"
#define FNQL_STEAM_TEXT_CAPACITY 512u
#define FNQL_STEAM_NAME_CAPACITY 128u
#define FNQL_STEAM_PATH_CAPACITY 1024u
#define FNQL_STEAM_AUTH_TICKET_CAPACITY 2048u
#define FNQL_STEAM_UGC_TITLE_CAPACITY 129u
#define FNQL_STEAM_UGC_DESCRIPTION_CAPACITY 8001u
#define FNQL_STEAM_UGC_PREVIEW_URL_CAPACITY 1024u

typedef uint32_t fnqlSteamBool_t;

typedef enum fnqlSteamResult_e {
	FNQL_STEAM_RESULT_OK = 0,
	FNQL_STEAM_RESULT_PENDING = 1,
	FNQL_STEAM_RESULT_UNAVAILABLE = -1,
	FNQL_STEAM_RESULT_INVALID_ARGUMENT = -2,
	FNQL_STEAM_RESULT_NOT_INITIALIZED = -3,
	FNQL_STEAM_RESULT_UNSUPPORTED = -4,
	FNQL_STEAM_RESULT_FAILED = -5,
	FNQL_STEAM_RESULT_BUFFER_TOO_SMALL = -6,
	FNQL_STEAM_RESULT_ABI_MISMATCH = -7
} fnqlSteamResult_t;

typedef enum fnqlSteamRole_e {
	FNQL_STEAM_ROLE_CLIENT = 1u << 0,
	FNQL_STEAM_ROLE_GAME_SERVER = 1u << 1
} fnqlSteamRole_t;

/*
 * ISteamMatchmakingServers request kinds.  These are deliberately distinct
 * from the retail WebUI's filter values: the WebUI uses 2 for Friends, while
 * Steam's request selector uses 5.  Keep the provider ABI explicit so an
 * engine caller cannot silently turn a Friends refresh into Internet.
 */
typedef enum fnqlSteamServerBrowserRequestMode_e {
	FNQL_STEAM_SERVER_BROWSER_INTERNET = 0u,
	FNQL_STEAM_SERVER_BROWSER_LAN = 1u,
	FNQL_STEAM_SERVER_BROWSER_FAVORITES = 3u,
	FNQL_STEAM_SERVER_BROWSER_HISTORY = 4u,
	FNQL_STEAM_SERVER_BROWSER_FRIENDS = 5u
} fnqlSteamServerBrowserRequestMode_t;

typedef enum fnqlSteamCapability_e {
	FNQL_STEAM_CAP_CLIENT = 1ull << 0,
	FNQL_STEAM_CAP_IDENTITY = 1ull << 1,
	FNQL_STEAM_CAP_OVERLAY = 1ull << 2,
	FNQL_STEAM_CAP_RICH_PRESENCE = 1ull << 3,
	FNQL_STEAM_CAP_LOBBIES = 1ull << 4,
	FNQL_STEAM_CAP_SERVER_BROWSER = 1ull << 5,
	/* Subscription, install-state, and download operations through the UGC
	 * interface owned by the active role.  A CLIENT role uses client-owned UGC.
	 * For a GAME_SERVER-only startup this bit must remain clear until
	 * start_game_server has acquired GameServer-owned UGC, and must be cleared
	 * before stop_game_server returns; a provider must not initialize CLIENT
	 * merely to satisfy an unattended server.  get_status.capabilities is the
	 * runtime-authoritative mask for these ownership transitions. */
	FNQL_STEAM_CAP_UGC = 1ull << 6,
	FNQL_STEAM_CAP_STATS = 1ull << 7,
	FNQL_STEAM_CAP_AUTH = 1ull << 8,
	FNQL_STEAM_CAP_GAME_SERVER = 1ull << 9,
	FNQL_STEAM_CAP_GAME_SERVER_STATS = 1ull << 10,
	/* Converts the retail module's private Json::Value ABI to bounded UTF-8.
	 * Keeping this in an optional provider avoids teaching the portable engine
	 * about one compiler/STL-specific object layout. */
	FNQL_STEAM_CAP_RETAIL_JSON = 1ull << 11,
	/* Bounded Steam avatar acquisition through caller-owned RGBA buffers. */
	FNQL_STEAM_CAP_AVATARS = 1ull << 12,
	FNQL_STEAM_CAP_FRIENDS = 1ull << 13,
	/* Asynchronous all-UGC call-result support is separate from the download
	 * lane.  A GAME_SERVER-only provider must leave this clear unless it also
	 * implements GameServer-owned call-result registration and pumping. */
	FNQL_STEAM_CAP_UGC_QUERY = 1ull << 14,
	/* Receives Steam purchase authorization decisions; transaction creation
	 * remains an external service responsibility. */
	FNQL_STEAM_CAP_MICROTRANSACTION_CALLBACK = 1ull << 15,
	FNQL_STEAM_CAP_CLIENT_P2P = 1ull << 16,
	FNQL_STEAM_CAP_GAME_SERVER_P2P = 1ull << 17,
	FNQL_STEAM_CAP_VOICE = 1ull << 18,
	/* Per-user client SteamUserStats readback plus local achievement mutation.
	 * This is distinct from the authoritative GameServer stats capability. */
	FNQL_STEAM_CAP_CLIENT_STATS = 1ull << 19,
	FNQL_STEAM_CAP_LOBBY_SNAPSHOT = 1ull << 20,
	FNQL_STEAM_CAP_GAME_SERVER_PACKET_IO = 1ull << 21,
	FNQL_STEAM_CAP_GAME_SERVER_METADATA = 1ull << 22,
	/* Account-level favorite mutation through Steam matchmaking. */
	FNQL_STEAM_CAP_FAVORITES = 1ull << 23,
	FNQL_STEAM_CAP_SERVER_DETAILS = 1ull << 24
} fnqlSteamCapability_t;

typedef enum fnqlSteamAvatarSize_e {
	FNQL_STEAM_AVATAR_SMALL = 0,
	FNQL_STEAM_AVATAR_MEDIUM = 1,
	FNQL_STEAM_AVATAR_LARGE = 2
} fnqlSteamAvatarSize_t;

typedef enum fnqlSteamLogLevel_e {
	FNQL_STEAM_LOG_DEBUG = 0,
	FNQL_STEAM_LOG_INFO = 1,
	FNQL_STEAM_LOG_WARNING = 2,
	FNQL_STEAM_LOG_ERROR = 3
} fnqlSteamLogLevel_t;

typedef enum fnqlSteamEventType_e {
	FNQL_STEAM_EVENT_NONE = 0,
	FNQL_STEAM_EVENT_PROVIDER_READY,
	FNQL_STEAM_EVENT_PROVIDER_STOPPED,
	FNQL_STEAM_EVENT_WARNING,
	FNQL_STEAM_EVENT_OVERLAY_STATE,
	FNQL_STEAM_EVENT_RICH_PRESENCE_JOIN_REQUESTED,
	FNQL_STEAM_EVENT_GAME_SERVER_CHANGE_REQUESTED,
	FNQL_STEAM_EVENT_SERVER_LIST_START,
	FNQL_STEAM_EVENT_SERVER_RESPONSE,
	FNQL_STEAM_EVENT_SERVER_LIST_COMPLETE,
	FNQL_STEAM_EVENT_LOBBY_CREATED,
	FNQL_STEAM_EVENT_LOBBY_ENTERED,
	FNQL_STEAM_EVENT_LOBBY_LEFT,
	FNQL_STEAM_EVENT_LOBBY_CHAT_MESSAGE,
	/* The completed result snapshot is already readable through
	 * get_ugc_query_results.  Before this synchronous event, the provider detaches
	 * the pending call-result and clears its shared in-flight ownership so the
	 * handler may re-enter request_ugc_query safely.  On non-I/O completion the
	 * captured native query remains live during delivery and is released exactly
	 * once afterward using that local handle, never re-entered shared state.  An
	 * I/O failure instead releases the captured handle before emitting failure. */
	FNQL_STEAM_EVENT_UGC_QUERY_COMPLETE,
	FNQL_STEAM_EVENT_UGC_DOWNLOAD_COMPLETE,
	FNQL_STEAM_EVENT_USER_STATS_RECEIVED,
	/* Final asynchronous validation for an accepted begin_auth_session request.
	 * subject_id is the ticket SteamID; only RESULT_OK authenticates it. */
	FNQL_STEAM_EVENT_AUTH_RESULT,
	FNQL_STEAM_EVENT_GAME_SERVER_CONNECTED,
	FNQL_STEAM_EVENT_GAME_SERVER_DISCONNECTED,
	/* Completion for an asynchronous store_user_stats request. subject_id is
	 * the player SteamID and result is the final store outcome. */
	FNQL_STEAM_EVENT_USER_STATS_STORED,
	/* A matching-AppID Workshop item is installed and safe to inspect.
	 * subject_id is the item ID, value is size on disk, flags is timestamp,
	 * and text is the bounded absolute install folder when available. */
	FNQL_STEAM_EVENT_UGC_ITEM_INSTALLED,
	FNQL_STEAM_EVENT_LOBBY_CHAT_UPDATE,
	FNQL_STEAM_EVENT_LOBBY_DATA_UPDATED,
	FNQL_STEAM_EVENT_LOBBY_GAME_CREATED,
	FNQL_STEAM_EVENT_LOBBY_KICKED,
	FNQL_STEAM_EVENT_LOBBY_JOIN_REQUESTED,
	/* AvatarImageLoaded_t: subject_id is the SteamID, value is Steam's image
	 * handle, and flags packs width in the low 16 bits and height in the high. */
	FNQL_STEAM_EVENT_AVATAR_IMAGE_LOADED,
	/* subject_id is the changed SteamID; flags is EPersonaChange. */
	FNQL_STEAM_EVENT_PERSONA_STATE_CHANGED,
	/* subject_id is the friend SteamID; flags is the originating AppID. */
	FNQL_STEAM_EVENT_FRIEND_RICH_PRESENCE_UPDATED,
	/* MicroTxnAuthorizationResponse_t: flags is the AppID, subject_id is the
	 * complete order ID, and value is normalized to 0 or 1. */
	FNQL_STEAM_EVENT_MICROTRANSACTION_AUTHORIZATION,
	/* flags is FNQL_STEAM_ROLE_CLIENT or FNQL_STEAM_ROLE_GAME_SERVER and
	 * subject_id is the requesting remote SteamID. Admission is an engine
	 * decision; the provider never accepts a request automatically. */
	FNQL_STEAM_EVENT_P2P_SESSION_REQUEST,
	/* Same role/identity fields; value is EP2PSessionError. */
	FNQL_STEAM_EVENT_P2P_SESSION_FAILURE,
	/* Server-detail events preserve the three independent retail response
	 * channels. Player: text/name, value/signed score bits, detail/time text.
	 * Rule: text/key, detail/value. Complete: flags/success-channel mask and
	 * value/failure-channel mask (ping=1, players=2, rules=4). */
	FNQL_STEAM_EVENT_SERVER_DETAIL_RESPONSE,
	FNQL_STEAM_EVENT_SERVER_DETAIL_PLAYER,
	FNQL_STEAM_EVENT_SERVER_DETAIL_RULE,
	FNQL_STEAM_EVENT_SERVER_DETAIL_COMPLETE
} fnqlSteamEventType_t;

typedef enum fnqlSteamFriendFlag_e {
	FNQL_STEAM_FRIEND_HAS_NICKNAME = 1u << 0,
	FNQL_STEAM_FRIEND_HAS_GAME = 1u << 1,
	FNQL_STEAM_FRIEND_PLAYING_QUAKE_LIVE = 1u << 2
} fnqlSteamFriendFlag_t;

/* Stable ISteamUGC EItemState bits used by retail Quake Live.  Keeping the
 * complete public bit set here prevents individual engine consumers from
 * growing incompatible magic constants. */
typedef enum fnqlSteamUgcItemState_e {
	FNQL_STEAM_UGC_ITEM_STATE_NONE = 0,
	FNQL_STEAM_UGC_ITEM_STATE_SUBSCRIBED = 1u << 0,
	FNQL_STEAM_UGC_ITEM_STATE_LEGACY = 1u << 1,
	FNQL_STEAM_UGC_ITEM_STATE_INSTALLED = 1u << 2,
	FNQL_STEAM_UGC_ITEM_STATE_NEEDS_UPDATE = 1u << 3,
	FNQL_STEAM_UGC_ITEM_STATE_DOWNLOADING = 1u << 4,
	FNQL_STEAM_UGC_ITEM_STATE_DOWNLOAD_PENDING = 1u << 5
} fnqlSteamUgcItemState_t;

typedef struct fnqlSteamServer_s {
	uint32_t size;
	uint32_t ip;
	uint16_t connection_port;
	uint16_t query_port;
	int32_t ping;
	uint32_t app_id;
	int32_t players;
	int32_t max_players;
	int32_t bot_players;
	fnqlSteamBool_t password_protected;
	fnqlSteamBool_t secure;
	uint32_t last_played;
	int32_t server_version;
	uint64_t steam_id;
	char game_directory[32];
	char map[32];
	char game_description[64];
	char server_name[64];
	char game_tags[128];
} fnqlSteamServer_t;

typedef struct fnqlSteamEvent_s {
	uint32_t size;
	uint32_t type;
	int32_t result;
	uint32_t flags;
	uint64_t subject_id;
	uint64_t value;
	char text[FNQL_STEAM_TEXT_CAPACITY];
	char detail[FNQL_STEAM_TEXT_CAPACITY];
	fnqlSteamServer_t server;
} fnqlSteamEvent_t;

typedef void (FNQL_STEAM_CALL *fnqlSteamLogFn)(void *context, uint32_t level,
	const char *message);
typedef void (FNQL_STEAM_CALL *fnqlSteamEventFn)(void *context,
	const fnqlSteamEvent_t *event);
typedef uint64_t (FNQL_STEAM_CALL *fnqlSteamMillisecondsFn)(void *context);

typedef struct fnqlSteamHost_s {
	uint32_t size;
	uint32_t abi_version;
	void *context;
	fnqlSteamLogFn log;
	fnqlSteamEventFn emit_event;
	fnqlSteamMillisecondsFn milliseconds;
} fnqlSteamHost_t;

typedef struct fnqlSteamStartup_s {
	uint32_t size;
	uint32_t abi_version;
	uint32_t app_id;
	uint32_t roles;
	char retail_install_path[FNQL_STEAM_PATH_CAPACITY];
	char steam_api_path[FNQL_STEAM_PATH_CAPACITY];
	char product_name[FNQL_STEAM_NAME_CAPACITY];
	char product_version[FNQL_STEAM_NAME_CAPACITY];
	char game_directory[FNQL_STEAM_NAME_CAPACITY];
} fnqlSteamStartup_t;

typedef struct fnqlSteamProviderInfo_s {
	uint32_t size;
	uint32_t abi_version;
	/* Sampled by the engine after startup; describe operations usable at that
	 * point, not merely the provider's compiled maximum.  Later role-dependent
	 * availability is reported by fnqlSteamStatus_t.capabilities. */
	uint64_t capabilities;
	char name[FNQL_STEAM_NAME_CAPACITY];
	char version[FNQL_STEAM_NAME_CAPACITY];
} fnqlSteamProviderInfo_t;

typedef struct fnqlSteamStatus_s {
	uint32_t size;
	uint32_t initialized_roles;
	/* Current runtime availability.  FnQL resamples and function-table-validates
	 * this mask after role start/stop and callback-driven transitions. */
	uint64_t capabilities;
	uint64_t local_steam_id;
	fnqlSteamBool_t client_logged_on;
	fnqlSteamBool_t overlay_enabled;
	fnqlSteamBool_t game_server_logged_on;
	char persona_name[FNQL_STEAM_NAME_CAPACITY];
	char country[16];
	char detail[FNQL_STEAM_TEXT_CAPACITY];
} fnqlSteamStatus_t;

typedef struct fnqlSteamFriend_s {
	uint32_t size;
	uint32_t flags;
	uint64_t steam_id;
	uint64_t game_id;
	uint64_t lobby_id;
	int32_t persona_state;
	int32_t relationship;
	uint32_t app_id;
	uint32_t server_ip;
	uint16_t server_port;
	uint16_t query_port;
	char persona_name[FNQL_STEAM_NAME_CAPACITY];
	char nickname[FNQL_STEAM_NAME_CAPACITY];
	char status[FNQL_STEAM_TEXT_CAPACITY];
	char lan_ip[64];
	char connect[FNQL_STEAM_TEXT_CAPACITY];
} fnqlSteamFriend_t;

typedef struct fnqlSteamUgcItem_s {
	uint32_t size;
	int32_t result;
	uint64_t published_file_id;
	uint32_t creator_app_id;
	uint32_t consumer_app_id;
	char title[FNQL_STEAM_UGC_TITLE_CAPACITY];
	char description[FNQL_STEAM_UGC_DESCRIPTION_CAPACITY];
	char preview_url[FNQL_STEAM_UGC_PREVIEW_URL_CAPACITY];
} fnqlSteamUgcItem_t;

typedef struct fnqlSteamLobbyMember_s {
	uint32_t size;
	uint64_t steam_id;
	char persona_name[FNQL_STEAM_NAME_CAPACITY];
} fnqlSteamLobbyMember_t;

typedef struct fnqlSteamLobbyData_s {
	uint32_t size;
	char key[FNQL_STEAM_NAME_CAPACITY];
	char value[FNQL_STEAM_TEXT_CAPACITY];
} fnqlSteamLobbyData_t;

typedef struct fnqlSteamGameServerConfig_s {
	uint32_t size;
	uint32_t bind_ip;
	uint16_t steam_port;
	uint16_t game_port;
	uint16_t query_port;
	uint16_t server_mode;
	fnqlSteamBool_t dedicated;
	fnqlSteamBool_t password_protected;
	uint32_t max_players;
	char version[FNQL_STEAM_NAME_CAPACITY];
	char product[FNQL_STEAM_NAME_CAPACITY];
	char game_description[FNQL_STEAM_NAME_CAPACITY];
	char mod_directory[FNQL_STEAM_NAME_CAPACITY];
	char server_name[FNQL_STEAM_NAME_CAPACITY];
	char map_name[FNQL_STEAM_NAME_CAPACITY];
	char game_tags[FNQL_STEAM_TEXT_CAPACITY];
} fnqlSteamGameServerConfig_t;

typedef struct fnqlSteamProvider_s {
	uint32_t size;
	uint32_t abi_version;
	fnqlSteamProviderInfo_t info;

	fnqlSteamResult_t (FNQL_STEAM_CALL *startup)(const fnqlSteamHost_t *host,
		const fnqlSteamStartup_t *startup);
	void (FNQL_STEAM_CALL *shutdown)(void);
	void (FNQL_STEAM_CALL *run_callbacks)(void);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_status)(fnqlSteamStatus_t *status);

	fnqlSteamBool_t (FNQL_STEAM_CALL *is_subscribed_app)(uint32_t app_id);
	fnqlSteamResult_t (FNQL_STEAM_CALL *open_overlay_url)(const char *url);
	fnqlSteamResult_t (FNQL_STEAM_CALL *open_overlay_user)(const char *dialog,
		uint64_t steam_id);
	fnqlSteamResult_t (FNQL_STEAM_CALL *set_rich_presence)(const char *key,
		const char *value);
	fnqlSteamResult_t (FNQL_STEAM_CALL *request_servers)(uint32_t request_mode,
		uint32_t app_id);
	fnqlSteamResult_t (FNQL_STEAM_CALL *refresh_servers)(void);
	void (FNQL_STEAM_CALL *cancel_servers)(void);

	fnqlSteamResult_t (FNQL_STEAM_CALL *create_lobby)(uint32_t lobby_type,
		uint32_t max_members);
	fnqlSteamResult_t (FNQL_STEAM_CALL *join_lobby)(uint64_t lobby_id);
	fnqlSteamResult_t (FNQL_STEAM_CALL *leave_lobby)(uint64_t lobby_id);
	fnqlSteamResult_t (FNQL_STEAM_CALL *set_lobby_server)(uint64_t lobby_id,
		uint32_t ip, uint16_t port, uint64_t server_id);
	fnqlSteamResult_t (FNQL_STEAM_CALL *invite_to_lobby)(uint64_t lobby_id,
		uint64_t steam_id);
	fnqlSteamResult_t (FNQL_STEAM_CALL *send_lobby_chat)(uint64_t lobby_id,
		const void *bytes, uint32_t byte_count);

	fnqlSteamResult_t (FNQL_STEAM_CALL *get_subscribed_items)(uint64_t *item_ids,
		uint32_t capacity, uint32_t *count);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_item_install_info)(uint64_t item_id,
		char *folder, uint32_t folder_capacity, uint64_t *size_on_disk,
		uint32_t *timestamp);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_item_download_info)(uint64_t item_id,
		uint64_t *downloaded, uint64_t *total);
	fnqlSteamResult_t (FNQL_STEAM_CALL *download_item)(uint64_t item_id,
		fnqlSteamBool_t high_priority);
	fnqlSteamResult_t (FNQL_STEAM_CALL *subscribe_item)(uint64_t item_id);
	fnqlSteamResult_t (FNQL_STEAM_CALL *unsubscribe_item)(uint64_t item_id);

	fnqlSteamResult_t (FNQL_STEAM_CALL *request_user_stats)(uint64_t steam_id);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_auth_ticket)(void *ticket,
		uint32_t ticket_capacity, uint32_t *ticket_size, uint32_t *ticket_handle);
	void (FNQL_STEAM_CALL *cancel_auth_ticket)(uint32_t ticket_handle);
	fnqlSteamResult_t (FNQL_STEAM_CALL *begin_auth_session)(const void *ticket,
		uint32_t ticket_size, uint64_t steam_id);
	void (FNQL_STEAM_CALL *end_auth_session)(uint64_t steam_id);

	fnqlSteamResult_t (FNQL_STEAM_CALL *start_game_server)(
		const fnqlSteamGameServerConfig_t *config);
	void (FNQL_STEAM_CALL *stop_game_server)(void);
	fnqlSteamResult_t (FNQL_STEAM_CALL *update_game_server)(
		const fnqlSteamGameServerConfig_t *config);

	/* Optional trailing game-server stats extension. Older providers may end
	 * at update_game_server; engine capability validation checks size first. */
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_user_stat_i32)(uint64_t steam_id,
		const char *name, int32_t *value);
	fnqlSteamResult_t (FNQL_STEAM_CALL *set_user_stat_i32)(uint64_t steam_id,
		const char *name, int32_t value);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_user_achievement)(uint64_t steam_id,
		const char *name, fnqlSteamBool_t *unlocked);
	fnqlSteamResult_t (FNQL_STEAM_CALL *set_user_achievement)(uint64_t steam_id,
		const char *name);
	fnqlSteamResult_t (FNQL_STEAM_CALL *store_user_stats)(uint64_t steam_id);

	/* Optional trailing retail-module JSON adapter. The provider writes a NUL-
	 * terminated document and reports its byte count excluding that terminator. */
	fnqlSteamResult_t (FNQL_STEAM_CALL *serialize_retail_json)(
		const void *retail_value, char *json, uint32_t json_capacity,
		uint32_t *json_size);

	/* Optional trailing Workshop state extension. */
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_item_state)(uint64_t item_id,
		uint32_t *state_flags);

	/* Optional trailing avatar extension. A NULL/short buffer reports the exact
	 * required RGBA byte count without transferring ownership across the ABI. */
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_avatar_rgba)(uint64_t steam_id,
		uint32_t avatar_size, void *rgba, uint32_t rgba_capacity,
		uint32_t *width, uint32_t *height, uint32_t *required_size);

	/* Optional trailing social snapshot extension. friend_flags is the native
	 * EFriendFlags mask and must be identical for count and indexed retrieval. */
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_friends)(uint32_t friend_flags,
		fnqlSteamFriend_t *friends, uint32_t capacity, uint32_t *count);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_friend)(uint64_t steam_id,
		fnqlSteamFriend_t *friend_info);

	/* Optional trailing asynchronous all-UGC query extension. An accepted
	 * request returns PENDING.  A replacement first unregisters and releases the
	 * old request without emitting its completion.  A terminal callback captures
	 * the native query, detaches its call-result, and clears shared ownership
	 * before branching solely on I/O failure.  Without I/O failure it copies the
	 * bounded rows, emits FNQL_STEAM_EVENT_UGC_QUERY_COMPLETE synchronously while
	 * the captured query remains live, then releases that exact local handle.
	 * A NULL payload without I/O failure is a successful empty completion; a
	 * non-OK native result without I/O failure still takes this ordering.  With
	 * I/O failure it releases the captured handle first and then emits failed
	 * completion.  The copied snapshot remains available for the engine's
	 * count-then-fetch calls
	 * until the next accepted request or shutdown. */
	fnqlSteamResult_t (FNQL_STEAM_CALL *request_ugc_query)(uint32_t raw_filter);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_ugc_query_results)(
		fnqlSteamUgcItem_t *items, uint32_t capacity, uint32_t *count,
		uint32_t *total_matching);
	/* Legacy retail P2P and voice transport. role must name exactly one side. */
	fnqlSteamResult_t (FNQL_STEAM_CALL *send_p2p_packet)(uint32_t role,
		uint64_t remote_id, const void *data, uint32_t data_size,
		uint32_t send_type, int32_t channel);
	fnqlSteamResult_t (FNQL_STEAM_CALL *peek_p2p_packet)(uint32_t role,
		int32_t channel, uint32_t *packet_size);
	fnqlSteamResult_t (FNQL_STEAM_CALL *read_p2p_packet)(uint32_t role,
		int32_t channel, void *data, uint32_t capacity, uint32_t *data_size,
		uint64_t *remote_id);
	fnqlSteamResult_t (FNQL_STEAM_CALL *accept_p2p_session)(uint32_t role,
		uint64_t remote_id);
	fnqlSteamResult_t (FNQL_STEAM_CALL *close_p2p_session)(uint32_t role,
		uint64_t remote_id);
	fnqlSteamResult_t (FNQL_STEAM_CALL *start_voice_recording)(void);
	void (FNQL_STEAM_CALL *stop_voice_recording)(void);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_compressed_voice)(void *data,
		uint32_t capacity, uint32_t *data_size);
	fnqlSteamResult_t (FNQL_STEAM_CALL *decompress_voice)(
		const void *compressed, uint32_t compressed_size, void *pcm,
		uint32_t pcm_capacity, uint32_t *pcm_size, uint32_t sample_rate);
	uint32_t (FNQL_STEAM_CALL *get_voice_sample_rate)(void);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_game_server_steam_id)(
		uint64_t *steam_id);

	/* Optional trailing client SteamUserStats extension. All strings cross the
	 * ABI through caller-owned bounded buffers; no Steam or C++ object escapes. */
	fnqlSteamResult_t (FNQL_STEAM_CALL *request_client_user_stats)(
		uint64_t steam_id);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_client_user_stat_i32)(
		uint64_t steam_id, const char *name, int32_t *value);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_client_user_stat_f32)(
		uint64_t steam_id, const char *name, float *value);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_client_user_achievement)(
		uint64_t steam_id, const char *name, fnqlSteamBool_t *unlocked,
		uint32_t *unlock_time);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_achievement_display_attribute)(
		const char *name, const char *key, char *value,
		uint32_t value_capacity);
	fnqlSteamResult_t (FNQL_STEAM_CALL *set_client_achievement)(
		const char *name);
	fnqlSteamResult_t (FNQL_STEAM_CALL *store_client_stats)(void);
	fnqlSteamResult_t (FNQL_STEAM_CALL *reset_client_stats)(
		fnqlSteamBool_t include_achievements);

	/* Optional trailing lobby projection used by retail browser events. */
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_lobby_members)(uint64_t lobby_id,
		fnqlSteamLobbyMember_t *members, uint32_t capacity, uint32_t *count,
		uint64_t *owner_id, uint32_t *member_limit);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_lobby_data)(uint64_t lobby_id,
		fnqlSteamLobbyData_t *items, uint32_t capacity, uint32_t *count);
	fnqlSteamResult_t (FNQL_STEAM_CALL *set_lobby_data)(uint64_t lobby_id,
		const char *key, const char *value);
	fnqlSteamResult_t (FNQL_STEAM_CALL *invite_to_game)(uint64_t steam_id,
		const char *connect_string);

	/* Optional legacy Steam GameServer UDP bridge. IP and port use the same
	 * packed/network-order convention as the retail GameServer interface. */
	fnqlSteamResult_t (FNQL_STEAM_CALL *handle_game_server_packet)(
		const void *data, uint32_t data_size, uint32_t source_ip,
		uint16_t source_port);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_game_server_packet)(void *data,
		uint32_t capacity, uint32_t *data_size, uint32_t *destination_ip,
		uint16_t *destination_port);
	fnqlSteamResult_t (FNQL_STEAM_CALL *set_game_server_key_value)(
		const char *key, const char *value);
	fnqlSteamResult_t (FNQL_STEAM_CALL *update_game_server_user)(
		uint64_t steam_id, const char *player_name, uint32_t score);
	fnqlSteamResult_t (FNQL_STEAM_CALL *get_game_server_public_ip)(
		uint32_t *public_ip);
	fnqlSteamResult_t (FNQL_STEAM_CALL *create_unauthenticated_user)(
		uint64_t *steam_id);

	/* Optional trailing account-level favorite mutation. IP is the packed
	 * IPv4 value used by the legacy matchmaking interface. */
	fnqlSteamResult_t (FNQL_STEAM_CALL *set_favorite_server)(uint32_t app_id,
		uint32_t ip, uint16_t connection_port, uint16_t query_port,
		fnqlSteamBool_t add);

	/* At most one bounded detail request is active per provider. Starting a new
	 * request cancels the previous one. */
	fnqlSteamResult_t (FNQL_STEAM_CALL *request_server_details)(uint32_t ip,
		uint16_t query_port);
	void (FNQL_STEAM_CALL *cancel_server_details)(void);

	/* Optional trailing live GameServer bot count publication. */
	fnqlSteamResult_t (FNQL_STEAM_CALL *set_game_server_bot_count)(
		uint32_t bot_count);

	/* Optional trailing local voice-speaking state for Steam friends UI. */
	fnqlSteamResult_t (FNQL_STEAM_CALL *set_local_voice_speaking)(
		fnqlSteamBool_t speaking);
} fnqlSteamProvider_t;

typedef const fnqlSteamProvider_t *(FNQL_STEAM_CALL *fnqlSteamGetProviderFn)(
	uint32_t requested_abi_version);

FNQL_STEAM_EXPORT const fnqlSteamProvider_t *FNQL_STEAM_CALL
FnQLSteam_GetProvider(uint32_t requested_abi_version);

#ifdef __cplusplus
}
#endif

#endif
