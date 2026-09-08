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
// Opt-in offscreen retail adapter probe; see scripts/verify_webui_requests.py.
// No engine, window, keyboard, mouse, or screenshot operations are used.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "webui_backend.hpp"
#include "webui_native_request.hpp"
#include "awesomium_backend_win32.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

static_assert( sizeof( void * ) == 4, "The retail WebUI probe must target x86" );

namespace {
using namespace fnql::webui;

// Generated locally from one RGBA pixel (48, 160, 112, 255), using the PNG
// signature, IHDR/IDAT/IEND chunks and zlib compression. No retail image data.
constexpr std::uint8_t kAvatarPng[] = {
	0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
	0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
	0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x30, 0x58, 0x50, 0xf0,
	0x1f, 0x00, 0x04, 0x84, 0x02, 0x40, 0x4d, 0xf6, 0xc1, 0xa9, 0x00, 0x00,
	0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
};

struct AvatarFixture {
	struct Entry {
		std::string path;
		bool ready = false;
		unsigned int attempts = 0;
		unsigned int materializations = 0;
		unsigned int releases = 0;
	};
	// Populate before running callbacks so releaseToken addresses stay stable.
	std::vector<Entry> entries;
	unsigned int calls = 0;
	unsigned int attempts = 0;
	unsigned int materializations = 0;
	unsigned int releases = 0;

	static bool Request( void *context, std::string_view path,
		ResourceBuffer *buffer ) noexcept {
		auto &fixture = *static_cast<AvatarFixture *>( context );
		++fixture.calls;
		*buffer = {};
		for ( auto &entry : fixture.entries ) {
			if ( path != entry.path ) continue;
			++fixture.attempts;
			++entry.attempts;
			if ( !entry.ready ) return false;
			++fixture.materializations;
			++entry.materializations;
			*buffer = { kAvatarPng, sizeof( kAvatarPng ), &entry };
			return true;
		}
		return false;
	}

	static void Release( void *context, ResourceBuffer *buffer ) noexcept {
		auto &fixture = *static_cast<AvatarFixture *>( context );
		++fixture.releases;
		if ( buffer->releaseToken ) {
			++static_cast<Entry *>( buffer->releaseToken )->releases;
		}
		*buffer = {};
	}
};

void Require( bool condition, const char *message ) {
	if ( !condition ) throw std::runtime_error( message );
}

void Check( BackendResult result ) {
	if ( !result ) throw std::runtime_error( std::string( result.detail ) );
}

std::string Utf8( const wchar_t *text ) {
	const int length = WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS,
		text, -1, nullptr, 0, nullptr, nullptr );
	Require( length > 0, "Path could not be converted to UTF-8" );
	std::string value( static_cast<std::size_t>( length ), '\0' );
	Require( WideCharToMultiByte( CP_UTF8, WC_ERR_INVALID_CHARS,
		text, -1, value.data(), length, nullptr, nullptr ) == length,
		"Path conversion failed" );
	value.pop_back();
	return value;
}

void PumpFor( BackendHost &host, DWORD milliseconds ) {
	const DWORD started = GetTickCount();
	do {
		Check( host.Pump() );
		Sleep( 10 );
	} while ( GetTickCount() - started < milliseconds );
}

void WaitForNotification( BackendHost &host, DWORD timeout = 5000 ) {
	const DWORD started = GetTickCount();
	while ( !host.Status().nativeRequestsPending ) {
		if ( GetTickCount() - started >= timeout ) {
			const auto status = host.Status();
			std::fprintf( stderr, "Notification timeout: loading=%d alive=%d crashed=%d paused=%d ready=%d error=%d\n",
				status.loading, status.viewAlive, status.crashed, status.renderingPaused,
				status.nativeRequestNotificationsReady, status.nativeErrorCode );
			std::array<char, 1024> state{};
			const auto script = host.EvaluateString( { "JSON.stringify({url:String(window.location.href),wake:window.__probeWakeCount?window.__probeWakeCount():-1,avatar:typeof window.__probeAvatarImages,ready:document.readyState})", "" }, state.data(), state.size() );
			if ( script.result ) std::fprintf( stderr, "Notification document: %s\n", state.data() );
			Require( false, "Native request notification timed out" );
		}
		Check( host.Pump() );
		Sleep( 10 );
	}
	Require( host.Status().nativeRequestNotificationsReady,
		"Received notification without completing handshake" );
}

void Execute( BackendHost &host, std::string_view script ) {
	Check( host.ExecuteScript( { script, "" } ) );
}

int Integer( BackendHost &host, std::string_view script ) {
	const auto result = host.EvaluateInteger( { script, "" } );
	Check( result.result );
	return result.value;
}

NativeRequestResult ReadRequest( BackendHost &host, char *buffer, std::size_t capacity ) {
	std::array<char, kNativeRequestTransportCapacity> encoded{};
	const auto result = host.EvaluateString( {
		kPopNativeRequestScript, "" }, encoded.data(), encoded.size() );
	Check( result.result );
	return DecodeNativeRequest( std::string_view( encoded.data(), result.length ),
		buffer, capacity );
}

std::string Pop( BackendHost &host ) {
	std::array<char, kNativeRequestBytes> buffer{};
	const auto result = ReadRequest( host, buffer.data(), buffer.size() );
	Check( result.result );
	return buffer.data();
}

void Quiet( BackendHost &host ) {
	PumpFor( host, 250 );
	Check( host.AcknowledgeNativeRequests() );
	const int before = Integer( host, "window.__probeWakeCount()" );
	PumpFor( host, 1200 );
	Require( Integer( host, "window.__probeWakeCount()" ) == before,
		"An empty queue continued notifying while idle" );
	Require( !host.Status().nativeRequestsPending, "Idle queue signaled more requests" );
}

void PumpAvatars( BackendHost &host, AvatarFixture &fixture ) {
	const auto before = fixture.attempts;
	Check( host.Pump() );
	Require( fixture.attempts - before <= 4,
		"More than four avatar paths were materialized in one browser pump" );
}

void PumpAvatarsFor( BackendHost &host, AvatarFixture &fixture, DWORD milliseconds ) {
	const DWORD started = GetTickCount();
	do {
		PumpAvatars( host, fixture );
		Sleep( 10 );
	} while ( GetTickCount() - started < milliseconds );
}

template <typename Predicate>
void WaitForAvatar( BackendHost &host, AvatarFixture &fixture,
	Predicate ready, const char *failure, DWORD timeout = 3000 ) {
	const DWORD started = GetTickCount();
	while ( !ready() ) {
		Require( GetTickCount() - started < timeout, failure );
		PumpAvatars( host, fixture );
		Sleep( 10 );
	}
}

void AvatarJavascript( BackendHost &host ) {
	Execute( host,
		"window.__probeAvatarImages=[];window.__probeAvatarLoads=0;window.__probeAvatarFetches=0;window.__probeAvatarErrors=0;"
		"window.__probeAvatarImage=function(path){var i=new Image();i.onload=function(){if(i.naturalWidth===1&&i.naturalHeight===1){window.__probeAvatarLoads++;}else{window.__probeAvatarErrors++;}};"
		"i.onerror=function(){window.__probeAvatarErrors++;};window.__probeAvatarImages.push(i);i.src=path;};"
		"window.__probeAvatarFetch=function(path){var r=new XMLHttpRequest();r.open('GET',path,true);r.onload=function(){if(r.responseText.length){window.__probeAvatarFetches++;}else{window.__probeAvatarErrors++;}};"
		"r.onerror=function(){window.__probeAvatarErrors++;};r.send(null);};" );
}

void FetchAvatar( BackendHost &host, const AvatarFixture::Entry &entry, bool image = false ) {
	Execute( host, std::string( image ? "window.__probeAvatarImage('" : "window.__probeAvatarFetch('" )
		+ entry.path + "');" );
}

void AvatarRetries( BackendHost &host, AvatarFixture &fixture ) {
	Check( host.SetRenderingPaused( true ) );
	AvatarJavascript( host );
	const auto callsBeforeMalformed = fixture.calls;
	Execute( host,
		"window.__probeInvalidAvatarDone=0;window.__probeInvalidAvatarBody=-1;window.__probeInvalidAvatarError=0;"
		"var invalidAvatar=new XMLHttpRequest();invalidAvatar.open('GET','asset://steam/not-an-avatar',true);"
		"invalidAvatar.onload=function(){window.__probeInvalidAvatarBody=invalidAvatar.responseText.length;window.__probeInvalidAvatarDone++;};"
		"invalidAvatar.onerror=function(){window.__probeInvalidAvatarError=1;window.__probeInvalidAvatarBody=invalidAvatar.responseText.length;window.__probeInvalidAvatarDone++;};invalidAvatar.send(null);" );
	WaitForAvatar( host, fixture, [&] {
		return Integer( host, "window.__probeInvalidAvatarDone" ) == 1;
	}, "Rejected Steam resource path remained outstanding" );
	Require( Integer( host, "window.__probeInvalidAvatarBody" ) == 0
		&& fixture.calls == callsBeforeMalformed,
		"Rejected Steam path returned data or reached native host resources" );
	std::puts( "PASS: invalid Steam resource routes complete empty without native resource requests" );
	auto &large = fixture.entries[0];
	auto &small = fixture.entries[1];
	auto &medium = fixture.entries[2];
	FetchAvatar( host, large, true );
	for ( int i = 0; i < 3; ++i ) FetchAvatar( host, small );
	FetchAvatar( host, medium );
	WaitForAvatar( host, fixture, [&] {
		return large.attempts && small.attempts && medium.attempts;
	}, "Initial avatar requests did not reach host services" );
	PumpAvatarsFor( host, fixture, 60 );
	Require( large.attempts == 1 && small.attempts == 1 && medium.attempts == 1,
		"Pending duplicates or idle pumps retried an avatar before its backoff" );
	large.ready = true;
	host.NotifyResourceAvailable( large.path );
	PumpAvatars( host, fixture );
	Require( large.materializations == 1 && small.materializations == 0
		&& medium.materializations == 0,
		"Avatar-ready wake was delayed or changed a different size variant" );
	small.ready = true;
	host.NotifyResourceAvailable( small.path );
	PumpAvatars( host, fixture );
	Require( small.materializations == 1,
		"Duplicate avatar requests were materialized more than once" );
	medium.ready = true;
	host.NotifyResourceAvailable( medium.path );
	PumpAvatars( host, fixture );
	WaitForAvatar( host, fixture, [&] {
		return Integer( host, "window.__probeAvatarLoads" ) == 1
			&& Integer( host, "window.__probeAvatarFetches" ) == 4;
	}, "Not every paused image/duplicate request received its ready resource" );
	Require( Integer( host, "window.__probeAvatarErrors" ) == 0,
		"Ready avatar responses failed to load" );
	Require( large.releases == 1 && small.releases == 1 && medium.releases == 1,
		"Avatar materializations did not receive exactly one release" );
	const auto completedAttempts = fixture.attempts;
	PumpAvatarsFor( host, fixture, 2100 );
	Require( fixture.attempts == completedAttempts,
		"Completed avatar requests continued retrying" );
	std::puts( "PASS: paused avatar image wakes immediately, duplicate requests share one buffer, size variants stay separate, completed requests stay idle" );

	auto &fallback = fixture.entries[3];
	FetchAvatar( host, fallback );
	WaitForAvatar( host, fixture, [&] { return fallback.attempts > 0; },
		"Fallback avatar never received its initial attempt" );
	PumpAvatarsFor( host, fixture, 700 );
	Require( fallback.attempts >= 2 && fallback.attempts <= 3,
		"Pending avatar fallback retry rate was not bounded by elapsed time" );
	fallback.ready = true;
	WaitForAvatar( host, fixture, [&] {
		return Integer( host, "window.__probeAvatarFetches" ) == 5;
	}, "A missed avatar-ready event stranded the fallback request", 2500 );
	Require( fallback.materializations == 1 && fallback.releases == 1,
		"Fallback completion duplicated or leaked its materialization" );
	std::puts( "PASS: delayed avatars use bounded fallback retries and still complete without a readiness event" );

	std::string burst;
	for ( std::size_t index = 6; index < fixture.entries.size(); ++index ) {
		fixture.entries[index].ready = true;
		burst += "window.__probeAvatarFetch('" + fixture.entries[index].path + "');";
	}
	Execute( host, burst );
	WaitForAvatar( host, fixture, [&] {
		return Integer( host, "window.__probeAvatarFetches" ) == 13;
	}, "The bounded avatar burst did not drain" );
	for ( std::size_t index = 6; index < fixture.entries.size(); ++index ) {
		Require( fixture.entries[index].materializations == 1
			&& fixture.entries[index].releases == 1,
			"Avatar burst did not materialize and release each path once" );
	}
	std::puts( "PASS: simultaneous avatar paths drain with at most four host fetches per browser pump" );

	for ( const bool reload : { true, false } ) {
		auto &entry = fixture.entries[reload ? 4 : 5];
		FetchAvatar( host, entry );
		WaitForAvatar( host, fixture, [&] { return entry.attempts > 0; },
			"Lifecycle avatar did not become pending" );
		const auto before = entry.attempts;
		if ( reload ) Check( host.Reload( false ) );
		else Check( host.Navigate( "asset://ql/index.html" ) );
		Require( !host.Status().nativeRequestNotificationsReady,
			"Avatar lifecycle transition retained the request handshake" );
		entry.ready = true;
		host.NotifyResourceAvailable( entry.path );
		PumpAvatarsFor( host, fixture, 900 );
		Require( entry.attempts == before && entry.materializations == 0,
			"An old document's avatar request survived navigation or reload" );
		WaitForNotification( host );
		Require( Integer( host, "String(window.location.href)==='asset://ql/index.html'?1:0" ) == 1,
			"Avatar lifecycle probe navigated to an error document" );
		Check( host.AcknowledgeNativeRequests() );
		AvatarJavascript( host );
	}
	Require( fixture.materializations == fixture.releases,
		"The avatar probe leaked a successful resource buffer" );
	std::puts( "PASS: reload and navigation discard stale avatar retries and late ready notifications" );
}

void Benchmark( BackendHost &host ) {
	std::puts( "BENCHMARK: local offscreen synchronous IPC samples; these are not gameplay frame timings" );
	for ( const int units : { 32, 256, 1023 } ) {
		Integer( host, "window.__qlr_native_read=new Array("
			+ std::to_string( units + 1 ) + ").join('x');1;" );
		const auto before = std::chrono::steady_clock::now();
		for ( int index = 0; index < units; ++index ) {
			Require( Integer( host, "window.__qlr_native_read.charCodeAt("
				+ std::to_string( index ) + ")" ) == 'x', "Legacy character probe lost a code unit" );
		}
		const auto after = std::chrono::steady_clock::now();
		Integer( host, "window.__qlr_native_requests.push(window.__qlr_native_read);1;" );
		const auto start = std::chrono::steady_clock::now();
		const std::string request = Pop( host );
		const auto end = std::chrono::steady_clock::now();
		Require( request == std::string( static_cast<std::size_t>( units ), 'x' ),
			"Bulk benchmark changed request text" );
		std::printf( "BENCHMARK: units=%d per_character_ms=%.3f bulk_ms=%.3f\n", units,
			std::chrono::duration<double, std::milli>( after - before ).count(),
			std::chrono::duration<double, std::milli>( end - start ).count() );
	}
}

void Run( BackendHost &host, const std::string &retail, const std::string &profile,
	const std::string &bridge, bool benchmark ) {
	AvatarFixture avatars;
	for ( const char *size : { "large/", "small/", "medium/" } ) {
		avatars.entries.push_back( { "asset://steam/avatar/" + std::string( size )
			+ "76561198000000001" } );
	}
	for ( unsigned int id = 2; id <= 12; ++id ) {
		avatars.entries.push_back( { "asset://steam/avatar/large/765611980000000"
			+ ( id < 10 ? std::string( "0" ) : std::string() ) + std::to_string( id ) } );
	}
	StartupParameters parameters;
	parameters.runtimePath = profile;
	parameters.basePath = retail;
	parameters.retailPath = retail;
	parameters.initialSurface = { 64, 64 };
	parameters.startupScript = bridge;
	parameters.hostServices = { &avatars, &AvatarFixture::Request, &AvatarFixture::Release };
	Check( host.Start( parameters ) );
	Check( host.Navigate( "asset://ql/index.html" ) );
	Require( !host.Status().nativeRequestNotificationsReady,
		"Navigation retained the old document's handshake" );
	WaitForNotification( host, 10000 );
	Check( host.AcknowledgeNativeRequests() );
	Require( !host.Status().nativeRequestsPending
		&& host.Status().nativeRequestNotificationsReady,
		"Acknowledgement did not preserve handshake and clear pending work" );
	Quiet( host );
	Require( Integer( host, "String(window.location.href)==='asset://ql/index.html'?1:0" ) == 1,
		"Retail request fixture did not load the expected document" );
	std::puts( "PASS: production startup handshake, acknowledgement, idle silence" );

	Check( host.SetRenderingPaused( true ) );
	Execute( host, "for(var i=0;i<20;i++){window.__probeQueue('probe',String(i));}" );
	WaitForNotification( host );
	Check( host.AcknowledgeNativeRequests() );
	for ( int frame = 0, consumed = 0; frame < 3; ++frame ) {
		for ( int i = 0; i < 8 && consumed < 20; ++i, ++consumed ) {
			Require( Pop( host ) == "probe\n" + std::to_string( consumed ),
				"Paused queue backlog was reordered or lost" );
		}
		if ( frame == 0 ) {
			Require( Integer( host, "window.__qlr_native_requests.length" ) == 12,
				"A full frame did not retain its remaining requests" );
		}
		PumpFor( host, 10 );
	}
	Require( Pop( host ).empty(), "Backlog did not drain completely" );
	Quiet( host );
	std::puts( "PASS: paused browser delivers requests and preserves backlog beyond eight requests" );

	Execute( host, "window.__probeQueue('probe','na\\u00efve \\ud83d\\ude80');" );
	WaitForNotification( host );
	Check( host.AcknowledgeNativeRequests() );
	Require( Pop( host ) == "probe\nna\xc3\xafve \xf0\x9f\x9a\x80",
		"Bulk script result changed Unicode text" );
	Quiet( host );
	std::puts( "PASS: request text survives one bounded UTF-8 string result" );

	for ( const auto malformed : { "'nul\\u0000suffix'", "'\\ud800'", "new Array(1025).join('x')" } ) {
		Execute( host, "window.__probeQueue('probe'," + std::string( malformed )
			+ ");window.__probeQueue('probe','valid-tail');" );
		WaitForNotification( host );
		Check( host.AcknowledgeNativeRequests() );
		std::array<char, kNativeRequestBytes> rejected{};
		const auto invalid = ReadRequest( host, rejected.data(), rejected.size() );
		Require( !invalid.result && !invalid.hasRequest && rejected[0] == '\0',
			"Malformed native request was accepted or returned partial text" );
		WaitForNotification( host );
		Check( host.AcknowledgeNativeRequests() );
		Require( Pop( host ) == "probe\nvalid-tail", "Rejected queue head stranded its valid tail" );
		Quiet( host );
	}
	Execute( host, "window.__probeQueue('probe',new Array(1018).join('x'));" );
	WaitForNotification( host );
	Check( host.AcknowledgeNativeRequests() );
	Require( Pop( host ) == "probe\n" + std::string( 1017, 'x' ),
		"Maximum-sized native request was truncated" );
	Quiet( host );
	std::puts( "PASS: embedded NUL, malformed UTF-16 and oversized heads reject without losing valid tails; maximum request fits" );

	Execute( host, "window.__probeStallNext=true;window.__probeQueue('probe','watchdog');" );
	WaitForNotification( host, 3000 );
	Require( Integer( host, "window.__probeAborts" ) == 1,
		"Production watchdog did not abort its stalled request exactly once" );
	Check( host.AcknowledgeNativeRequests() );
	Require( Pop( host ) == "probe\nwatchdog", "Watchdog retry lost queued work" );
	Quiet( host );
	std::puts( "PASS: production watchdog retries a stalled notification while rendering is paused" );

	Execute( host,
		"window.__probeRejected=0;['pending/1-2cmd','pending/1-2?cmd=quit','pending/1-2/quit','pending/1--2'].forEach(function(path){"
		"var r=new XMLHttpRequest();r.open('GET','asset://fnqlbridge/'+path,true);r.onload=function(){window.__probeRejected++;};r.send(null);});" );
	PumpFor( host, 500 );
	Require( Integer( host, "window.__probeRejected" ) == 4,
		"Invalid notification requests were left outstanding" );
	Require( !host.Status().nativeRequestsPending, "Malformed resource path signaled native work" );
	std::puts( "PASS: malformed notification paths are completed without signaling commands" );

	if ( benchmark ) {
		Benchmark( host );
	}
	AvatarRetries( host, avatars );

	Check( host.Reload( false ) );
	Require( !host.Status().nativeRequestsPending
		&& !host.Status().nativeRequestNotificationsReady,
		"Reload did not reset the document handshake" );
	WaitForNotification( host );
	Check( host.AcknowledgeNativeRequests() );
	Check( host.Navigate( "asset://ql/index.html" ) );
	Require( !host.Status().nativeRequestsPending
		&& !host.Status().nativeRequestNotificationsReady,
		"Navigate did not reset the document handshake" );
	WaitForNotification( host );
	host.Shutdown();
	Require( !host.Status().nativeRequestsPending
		&& !host.Status().nativeRequestNotificationsReady,
		"Shutdown retained a stale notification" );
	std::puts( "PASS: reload, navigation, and shutdown reset notification state" );
}
} // namespace

int wmain( int argc, wchar_t **argv ) {
	if ( argc != 4 && ( argc != 5 || std::wstring_view( argv[4] ) != L"--benchmark" ) ) {
		std::fputs( "Usage: webui_retail_request_probe <retail-path> <profile-path> <bridge-js> [--benchmark]\n", stderr );
		return 2;
	}
	BackendHost host;
	InstallRetailAwesomiumBackend( host );
	try {
		std::ifstream stream( argv[3], std::ios::binary );
		Require( static_cast<bool>( stream ), "Could not read production request bridge fixture" );
		const std::string bridge( ( std::istreambuf_iterator<char>( stream ) ),
			std::istreambuf_iterator<char>() );
		Require( !bridge.empty(), "Production request bridge fixture was empty" );
		Run( host, Utf8( argv[1] ), Utf8( argv[2] ), bridge, argc == 5 );
		return 0;
	} catch ( const std::exception &error ) {
		std::fprintf( stderr, "FAIL: %s\n", error.what() );
		host.Shutdown();
		return 1;
	}
}
