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
// awesomium_backend_win32.cpp -- independent retail Awesomium runtime adapter

#include "awesomium_backend_win32.hpp"
#include "webui_backend.hpp"

#if defined( _WIN32 ) && ( defined( _M_IX86 ) || defined( __i386__ ) )

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <limits>
#include <new>
#include <string>
#include <string_view>

namespace fnql::webui {
namespace {

// Retail Quake Live 1.40.6 uses Awesomium 1.7.4.2 and publishes a generated
// stdcall C ABI alongside its C++ exports. Resolve that narrow ABI at runtime:
// no proprietary headers, import library, object layout, or allocator crosses
// FnQL's browser-neutral backend boundary.
class RetailAwesomiumBackend final : public Backend {
public:
	BackendDescriptor Describe() const noexcept override {
		return {
			kBackendInterfaceVersion,
			"Retail Awesomium 1.7",
			Capability::SoftwareSurface | Capability::IntegerScriptResult
				| Capability::TransparentView | Capability::ResourceRequests,
			true
		};
	}

	BackendResult Start( const StartupParameters &parameters ) noexcept override {
		Shutdown();
		status_ = {};
		lastErrorCode_ = BackendError::None;
		lastError_[0] = '\0';
		hostServices_ = parameters.hostServices;

		try {
			if ( !SelectRuntimePaths( parameters ) ) {
				return LastFailure();
			}
			if ( !LoadRuntime() || !ResolveImports() ) {
				return LastFailure();
			}
			if ( !CreateRuntime( parameters ) ) {
				ShutdownRuntimeObjects();
				return LastFailure();
			}
		} catch ( const std::bad_alloc & ) {
			ShutdownRuntimeObjects();
			return Fail( BackendError::ResourceUnavailable,
				"out of memory while starting the retail Awesomium backend" );
		} catch ( ... ) {
			ShutdownRuntimeObjects();
			return Fail( BackendError::OperationFailed,
				"unexpected exception while starting the retail Awesomium backend" );
		}

		status_.viewAlive = true;
		status_.windowObjectBound = true;
		status_.focused = true;
		return BackendResult::Success();
	}

	void Shutdown() noexcept override {
		ShutdownRuntimeObjects();
		status_ = {};
	}

	BackendStatus Status() const noexcept override {
		BackendStatus current = status_;
		if ( !view_ || !imports_.webViewSurface ) {
			return current;
		}

		current.loading = imports_.webViewIsLoading
			? imports_.webViewIsLoading( view_ ) : false;
		current.crashed = imports_.webViewIsCrashed
			? imports_.webViewIsCrashed( view_ ) : false;
		current.nativeErrorCode = imports_.webViewLastError
			? imports_.webViewLastError( view_ ) : 0;
		current.viewAlive = !current.crashed;

		void *surface = imports_.webViewSurface( view_ );
		if ( surface ) {
			const int width = imports_.bitmapWidth( surface );
			const int height = imports_.bitmapHeight( surface );
			if ( width > 0 && height > 0 ) {
				current.surface = { width, height };
				current.surfaceDirty = imports_.bitmapIsDirty( surface );
			}
		} else {
			current.surface = {};
			current.surfaceDirty = false;
		}
		return current;
	}

	BackendResult Navigate( std::string_view url ) noexcept override {
		try {
			const std::wstring wideUrl = ToWide( url );
			if ( wideUrl.empty() ) {
				return Fail( BackendError::InvalidArgument,
					"could not convert the WebUI URL to UTF-16" );
			}
			void *urlObject = imports_.newWebUrl( wideUrl.c_str() );
			if ( !urlObject ) {
				return Fail( BackendError::ResourceUnavailable,
					"could not allocate an Awesomium WebURL" );
			}
			imports_.webViewLoadUrl( view_, urlObject );
			imports_.deleteWebUrl( urlObject );
			status_.loading = true;
			return BackendResult::Success();
		} catch ( ... ) {
			return Fail( BackendError::OperationFailed,
				"failed to navigate the retail Awesomium view" );
		}
	}

	BackendResult Pump() noexcept override {
		if ( !core_ || !view_ ) {
			return Fail( BackendError::InvalidState,
				"retail Awesomium objects are not live" );
		}
		imports_.webCoreUpdate( core_ );
		RetryPendingResources();
		const BackendStatus current = Status();
		status_ = current;
		if ( current.crashed ) {
			return Fail( BackendError::Crashed,
				"the retail Awesomium child process crashed" );
		}
		return BackendResult::Success();
	}

	BackendResult Resize( SurfaceSize size ) noexcept override {
		if ( !view_ || !size.IsValid() ) {
			return Fail( BackendError::InvalidArgument,
				"invalid retail Awesomium resize request" );
		}
		imports_.webViewResize( view_, size.width, size.height );
		status_.surface = {};
		status_.surfaceDirty = false;
		return BackendResult::Success();
	}

	BackendResult ExecuteScript( const ScriptRequest &request ) noexcept override {
		try {
			const std::wstring source = ToWide( request.source );
			const std::wstring frame = ToWide( request.frame );
			if ( source.empty() ) {
				return Fail( BackendError::InvalidArgument,
					"could not convert JavaScript to UTF-16" );
			}
			imports_.webViewExecuteJavascript( view_, source.c_str(), frame.c_str() );
			return BackendResult::Success();
		} catch ( ... ) {
			return Fail( BackendError::OperationFailed,
				"failed to execute JavaScript in the retail Awesomium view" );
		}
	}

	IntegerScriptResult EvaluateInteger( const ScriptRequest &request ) noexcept override {
		try {
			const std::wstring source = ToWide( request.source );
			const std::wstring frame = ToWide( request.frame );
			if ( source.empty() ) {
				return { Fail( BackendError::InvalidArgument,
					"could not convert JavaScript to UTF-16" ), 0 };
			}
			void *value = imports_.webViewExecuteJavascriptWithResult(
				view_, source.c_str(), frame.c_str() );
			if ( !value ) {
				return { Fail( BackendError::OperationFailed,
					"Awesomium returned no JavaScript result" ), 0 };
			}
			const int integer = imports_.jsValueToInteger( value );
			imports_.deleteJsValue( value );
			return { BackendResult::Success(), integer };
		} catch ( ... ) {
			return { Fail( BackendError::OperationFailed,
				"failed to evaluate JavaScript in the retail Awesomium view" ), 0 };
		}
	}

	BackendResult CopySurface( const MutableSurface &destination ) noexcept override {
		void *surface = view_ ? imports_.webViewSurface( view_ ) : nullptr;
		if ( !surface ) {
			return Fail( BackendError::ResourceUnavailable,
				"the retail Awesomium surface is not ready" );
		}
		if ( destination.rowStride > static_cast<std::size_t>(
			( std::numeric_limits<int>::max )() ) ) {
			return Fail( BackendError::InvalidArgument,
				"the WebUI surface stride exceeds Awesomium's ABI" );
		}

		imports_.bitmapCopyTo(
			surface,
			destination.pixels,
			static_cast<int>( destination.rowStride ),
			4,
			true,
			false );
		imports_.bitmapSetIsDirty( surface, false );
		status_.surfaceDirty = false;
		return BackendResult::Success();
	}

	BackendResult SetZoom( int percent ) noexcept override {
		imports_.webViewSetZoom( view_, percent );
		return BackendResult::Success();
	}

	BackendResult SetRenderingPaused( bool paused ) noexcept override {
		if ( paused ) {
			imports_.webViewPauseRendering( view_ );
		} else {
			imports_.webViewResumeRendering( view_ );
		}
		status_.renderingPaused = paused;
		return BackendResult::Success();
	}

	BackendResult SetFocus( bool focused ) noexcept override {
		if ( focused ) {
			imports_.webViewFocus( view_ );
		} else {
			imports_.webViewUnfocus( view_ );
		}
		status_.focused = focused;
		return BackendResult::Success();
	}

	BackendResult InjectMouseMove( const MouseMoveEvent &event ) noexcept override {
		imports_.webViewInjectMouseMove( view_, event.x, event.y );
		return BackendResult::Success();
	}

	BackendResult InjectMouseButton( const MouseButtonEvent &event ) noexcept override {
		const int button = static_cast<int>( event.button );
		if ( event.action == ButtonAction::Press ) {
			imports_.webViewInjectMouseDown( view_, button );
		} else {
			imports_.webViewInjectMouseUp( view_, button );
		}
		return BackendResult::Success();
	}

	BackendResult InjectMouseWheel( const MouseWheelEvent &event ) noexcept override {
		constexpr int kAwesomiumWheelDelta = 120;
		imports_.webViewInjectMouseWheel(
			view_, event.horizontalTicks * kAwesomiumWheelDelta,
			event.verticalTicks * kAwesomiumWheelDelta );
		return BackendResult::Success();
	}

	BackendResult InjectKeyboard( const KeyboardEvent &event ) noexcept override {
		void *keyboard = imports_.newWebKeyboardEvent(
			static_cast<unsigned int>( event.type ),
			event.virtualKey,
			static_cast<long>( event.nativeKey ) );
		if ( !keyboard ) {
			return Fail( BackendError::ResourceUnavailable,
				"could not allocate an Awesomium keyboard event" );
		}
		imports_.webViewInjectKeyboardEvent( view_, keyboard );
		imports_.deleteWebKeyboardEvent( keyboard );
		return BackendResult::Success();
	}

	BackendResult StopLoading() noexcept override {
		imports_.webViewStop( view_ );
		return BackendResult::Success();
	}

	BackendResult ClearCache() noexcept override {
		imports_.webSessionClearCache( session_ );
		return BackendResult::Success();
	}

	BackendResult Reload( bool ignoreCache ) noexcept override {
		imports_.webViewReload( view_, ignoreCache );
		return BackendResult::Success();
	}

private:
	using NewObject = void *(__stdcall *)( void );
	using DeleteObject = void (__stdcall *)( void * );
	using SetString = void (__stdcall *)( void *, const wchar_t * );
	using SetBool = void (__stdcall *)( void *, bool );
	using RegisterStringCallback = void (__stdcall *)( const wchar_t *(__stdcall *)( const wchar_t * ) );
	using InitializeCore = void *(__stdcall *)( void * );
	using ShutdownCore = void (__stdcall *)( void );
	using UpdateCore = void (__stdcall *)( void * );
	using CreateSession = void *(__stdcall *)( void *, const wchar_t *, void * );
	using SessionVoid = void (__stdcall *)( void * );
	using AddDataSource = void (__stdcall *)( void *, const wchar_t *, void * );
	using NewDataPakSource = void *(__stdcall *)( const wchar_t * );
	using DataSourceDirectorConnect = void (__stdcall *)( void *, void * );
	using DataSourceSendResponse = void (__stdcall *)( void *, int, int,
		std::uint8_t *, const wchar_t * );
	using CreateView = void *(__stdcall *)( void *, int, int, void *, int );
	using ViewVoid = void (__stdcall *)( void * );
	using ViewSetBool = void (__stdcall *)( void *, bool );
	using ViewBool = bool (__stdcall *)( void * );
	using ViewInt = int (__stdcall *)( void * );
	using ViewObject = void *(__stdcall *)( void * );
	using ViewResize = void (__stdcall *)( void *, int, int );
	using ViewSetInt = void (__stdcall *)( void *, int );
	using ViewMouseMove = void (__stdcall *)( void *, int, int );
	using ViewMouseButton = void (__stdcall *)( void *, int );
	using NewKeyboardEvent = void *(__stdcall *)( unsigned int, unsigned int, long );
	using InjectKeyboardEvent = void (__stdcall *)( void *, void * );
	using NewWebUrl = void *(__stdcall *)( const wchar_t * );
	using LoadUrl = void (__stdcall *)( void *, void * );
	using ExecuteJavascript = void (__stdcall *)( void *, const wchar_t *, const wchar_t * );
	using ExecuteJavascriptWithResult = void *(__stdcall *)( void *, const wchar_t *, const wchar_t * );
	using JsValueToInteger = int (__stdcall *)( void * );
	using UpcastFactory = void *(__stdcall *)( void * );
	using SetSurfaceFactory = void (__stdcall *)( void *, void * );
	using BitmapInt = int (__stdcall *)( void * );
	using BitmapBool = bool (__stdcall *)( void * );
	using BitmapSetBool = void (__stdcall *)( void *, bool );
	using BitmapCopyTo = void (__stdcall *)( void *, std::uint8_t *, int, int, bool, bool );

	struct Imports {
		RegisterStringCallback registerStringCallback = nullptr;
		NewObject newWebConfig = nullptr;
		DeleteObject deleteWebConfig = nullptr;
		SetString configAssetProtocol = nullptr;
		SetString configChildProcessPath = nullptr;
		ViewSetInt configLogLevel = nullptr;
		SetString configLogPath = nullptr;
		SetString configPackagePath = nullptr;
		SetString configUserAgent = nullptr;
		SetString configUserScript = nullptr;
		NewObject newWebPreferences = nullptr;
		DeleteObject deleteWebPreferences = nullptr;
		SetBool preferencesEnablePlugins = nullptr;
		SetBool preferencesEnableWebSecurity = nullptr;
		SetBool preferencesEnableGpuAcceleration = nullptr;
		InitializeCore webCoreInitialize = nullptr;
		ShutdownCore webCoreShutdown = nullptr;
		UpdateCore webCoreUpdate = nullptr;
		CreateSession webCoreCreateSession = nullptr;
		NewObject newBitmapSurfaceFactory = nullptr;
		UpcastFactory bitmapSurfaceFactoryUpcast = nullptr;
		SetSurfaceFactory webCoreSetSurfaceFactory = nullptr;
		NewDataPakSource newDataPakSource = nullptr;
		NewObject newDataSource = nullptr;
		DataSourceDirectorConnect dataSourceDirectorConnect = nullptr;
		DataSourceSendResponse dataSourceSendResponse = nullptr;
		AddDataSource webSessionAddDataSource = nullptr;
		SessionVoid webSessionRelease = nullptr;
		SessionVoid webSessionClearCache = nullptr;
		CreateView webCoreCreateView = nullptr;
		ViewVoid webViewDestroy = nullptr;
		ViewSetBool webViewSetTransparent = nullptr;
		ViewVoid webViewResumeRendering = nullptr;
		ViewVoid webViewPauseRendering = nullptr;
		ViewVoid webViewFocus = nullptr;
		ViewVoid webViewUnfocus = nullptr;
		ViewBool webViewIsLoading = nullptr;
		ViewBool webViewIsCrashed = nullptr;
		ViewInt webViewLastError = nullptr;
		ViewObject webViewSurface = nullptr;
		ViewResize webViewResize = nullptr;
		ViewSetInt webViewSetZoom = nullptr;
		ViewMouseMove webViewInjectMouseMove = nullptr;
		ViewMouseButton webViewInjectMouseDown = nullptr;
		ViewMouseButton webViewInjectMouseUp = nullptr;
		ViewMouseMove webViewInjectMouseWheel = nullptr;
		NewKeyboardEvent newWebKeyboardEvent = nullptr;
		DeleteObject deleteWebKeyboardEvent = nullptr;
		InjectKeyboardEvent webViewInjectKeyboardEvent = nullptr;
		NewWebUrl newWebUrl = nullptr;
		DeleteObject deleteWebUrl = nullptr;
		LoadUrl webViewLoadUrl = nullptr;
		ExecuteJavascript webViewExecuteJavascript = nullptr;
		ExecuteJavascriptWithResult webViewExecuteJavascriptWithResult = nullptr;
		JsValueToInteger jsValueToInteger = nullptr;
		DeleteObject deleteJsValue = nullptr;
		ViewVoid webViewStop = nullptr;
		ViewSetBool webViewReload = nullptr;
		BitmapInt bitmapWidth = nullptr;
		BitmapInt bitmapHeight = nullptr;
		BitmapBool bitmapIsDirty = nullptr;
		BitmapSetBool bitmapSetIsDirty = nullptr;
		BitmapCopyTo bitmapCopyTo = nullptr;
	};

	template <typename Function>
	bool Resolve( Function &function, const char *name ) noexcept {
		function = reinterpret_cast<Function>( GetProcAddress( module_, name ) );
		if ( function ) {
			return true;
		}
		Fail( BackendError::InterfaceMismatch,
			"retail awesomium.dll is missing required export %s", name );
		return false;
	}

	bool ResolveImports() noexcept {
		if ( importsResolved_ ) {
			return true;
		}

#define FNQL_AWE_IMPORT( field, symbol ) \
		if ( !Resolve( imports_.field, symbol ) ) { return false; }
		FNQL_AWE_IMPORT( registerStringCallback, "_RegisterWStringCallback_awesomium@4" );
		FNQL_AWE_IMPORT( newWebConfig, "_Awe_new_WebConfig@0" );
		FNQL_AWE_IMPORT( deleteWebConfig, "_Awe_delete_WebConfig@4" );
		FNQL_AWE_IMPORT( configAssetProtocol, "_Awe_WebConfig_asset_protocol_set@8" );
		FNQL_AWE_IMPORT( configChildProcessPath, "_Awe_WebConfig_child_process_path_set@8" );
		FNQL_AWE_IMPORT( configLogLevel, "_Awe_WebConfig_log_level_set@8" );
		FNQL_AWE_IMPORT( configLogPath, "_Awe_WebConfig_log_path_set@8" );
		FNQL_AWE_IMPORT( configPackagePath, "_Awe_WebConfig_package_path_set@8" );
		FNQL_AWE_IMPORT( configUserAgent, "_Awe_WebConfig_user_agent_set@8" );
		FNQL_AWE_IMPORT( configUserScript, "_Awe_WebConfig_user_script_set@8" );
		FNQL_AWE_IMPORT( newWebPreferences, "_Awe_new_WebPreferences@0" );
		FNQL_AWE_IMPORT( deleteWebPreferences, "_Awe_delete_WebPreferences@4" );
		FNQL_AWE_IMPORT( preferencesEnablePlugins, "_Awe_WebPreferences_enable_plugins_set@8" );
		FNQL_AWE_IMPORT( preferencesEnableWebSecurity, "_Awe_WebPreferences_enable_web_security_set@8" );
		FNQL_AWE_IMPORT( preferencesEnableGpuAcceleration, "_Awe_WebPreferences_enable_gpu_acceleration_set@8" );
		FNQL_AWE_IMPORT( webCoreInitialize, "_Awe_WebCore_Initialize@4" );
		FNQL_AWE_IMPORT( webCoreShutdown, "_Awe_WebCore_Shutdown@0" );
		FNQL_AWE_IMPORT( webCoreUpdate, "_Awe_WebCore_Update@4" );
		FNQL_AWE_IMPORT( webCoreCreateSession, "_Awe_WebCore_CreateWebSession@12" );
		FNQL_AWE_IMPORT( newBitmapSurfaceFactory, "_Awe_new_BitmapSurfaceFactory@0" );
		FNQL_AWE_IMPORT( bitmapSurfaceFactoryUpcast, "_Awe_BitmapSurfaceFactory_Upcast@4" );
		FNQL_AWE_IMPORT( webCoreSetSurfaceFactory, "_Awe_WebCore_set_surface_factory@8" );
		FNQL_AWE_IMPORT( newDataPakSource, "_Awe_new_DataPakSource@4" );
		FNQL_AWE_IMPORT( newDataSource, "_Awe_new_DataSource@0" );
		FNQL_AWE_IMPORT( dataSourceDirectorConnect, "_Awe_DataSource_director_connect@8" );
		FNQL_AWE_IMPORT( dataSourceSendResponse, "_Awe_DataSource_SendResponse@20" );
		FNQL_AWE_IMPORT( webSessionAddDataSource, "_Awe_WebSession_AddDataSource@12" );
		FNQL_AWE_IMPORT( webSessionRelease, "_Awe_WebSession_Release@4" );
		FNQL_AWE_IMPORT( webSessionClearCache, "_Awe_WebSession_ClearCache@4" );
		FNQL_AWE_IMPORT( webCoreCreateView, "_Awe_WebCore_CreateWebView_0@20" );
		FNQL_AWE_IMPORT( webViewDestroy, "_Awe_WebView_Destroy@4" );
		FNQL_AWE_IMPORT( webViewSetTransparent, "_Awe_WebView_SetTransparent@8" );
		FNQL_AWE_IMPORT( webViewResumeRendering, "_Awe_WebView_ResumeRendering@4" );
		FNQL_AWE_IMPORT( webViewPauseRendering, "_Awe_WebView_PauseRendering@4" );
		FNQL_AWE_IMPORT( webViewFocus, "_Awe_WebView_Focus@4" );
		FNQL_AWE_IMPORT( webViewUnfocus, "_Awe_WebView_Unfocus@4" );
		FNQL_AWE_IMPORT( webViewIsLoading, "_Awe_WebView_IsLoading@4" );
		FNQL_AWE_IMPORT( webViewIsCrashed, "_Awe_WebView_IsCrashed@4" );
		FNQL_AWE_IMPORT( webViewLastError, "_Awe_WebView_last_error@4" );
		FNQL_AWE_IMPORT( webViewSurface, "_Awe_WebView_surface@4" );
		FNQL_AWE_IMPORT( webViewResize, "_Awe_WebView_Resize@12" );
		FNQL_AWE_IMPORT( webViewSetZoom, "_Awe_WebView_SetZoom@8" );
		FNQL_AWE_IMPORT( webViewInjectMouseMove, "_Awe_WebView_InjectMouseMove@12" );
		FNQL_AWE_IMPORT( webViewInjectMouseDown, "_Awe_WebView_InjectMouseDown@8" );
		FNQL_AWE_IMPORT( webViewInjectMouseUp, "_Awe_WebView_InjectMouseUp@8" );
		FNQL_AWE_IMPORT( webViewInjectMouseWheel, "_Awe_WebView_InjectMouseWheel@12" );
		FNQL_AWE_IMPORT( newWebKeyboardEvent, "_Awe_new_WebKeyboardEvent_1@12" );
		FNQL_AWE_IMPORT( deleteWebKeyboardEvent, "_Awe_delete_WebKeyboardEvent@4" );
		FNQL_AWE_IMPORT( webViewInjectKeyboardEvent, "_Awe_WebView_InjectKeyboardEvent@8" );
		FNQL_AWE_IMPORT( newWebUrl, "_Awe_new_WebURL_1@4" );
		FNQL_AWE_IMPORT( deleteWebUrl, "_Awe_delete_WebURL@4" );
		FNQL_AWE_IMPORT( webViewLoadUrl, "_Awe_WebView_LoadURL@8" );
		FNQL_AWE_IMPORT( webViewExecuteJavascript, "_Awe_WebView_ExecuteJavascript@12" );
		FNQL_AWE_IMPORT( webViewExecuteJavascriptWithResult, "_Awe_WebView_ExecuteJavascriptWithResult@12" );
		FNQL_AWE_IMPORT( jsValueToInteger, "_Awe_JSValue_ToInteger@4" );
		FNQL_AWE_IMPORT( deleteJsValue, "_Awe_delete_JSValue@4" );
		FNQL_AWE_IMPORT( webViewStop, "_Awe_WebView_Stop@4" );
		FNQL_AWE_IMPORT( webViewReload, "_Awe_WebView_Reload@8" );
		FNQL_AWE_IMPORT( bitmapWidth, "_Awe_BitmapSurface_width@4" );
		FNQL_AWE_IMPORT( bitmapHeight, "_Awe_BitmapSurface_height@4" );
		FNQL_AWE_IMPORT( bitmapIsDirty, "_Awe_BitmapSurface_is_dirty@4" );
		FNQL_AWE_IMPORT( bitmapSetIsDirty, "_Awe_BitmapSurface_set_is_dirty@8" );
		FNQL_AWE_IMPORT( bitmapCopyTo, "_Awe_BitmapSurface_CopyTo@24" );
#undef FNQL_AWE_IMPORT

		imports_.registerStringCallback( CopyReturnedString );
		importsResolved_ = true;
		return true;
	}

	bool CreateRuntime( const StartupParameters &parameters ) noexcept {
		config_ = imports_.newWebConfig();
		preferences_ = imports_.newWebPreferences();
		if ( !config_ || !preferences_ ) {
			Fail( BackendError::ResourceUnavailable,
				"could not allocate Awesomium configuration objects" );
			return false;
		}

		const std::wstring logPath = JoinPath( profileRoot_, L"awesomium.log" );
		const std::wstring userScript = ToWide( parameters.startupScript );
		imports_.configAssetProtocol( config_, L"asset" );
		// Retail leaves child_process_path at its default. SelectRuntimePaths has
		// already validated the companion helper without changing that behavior.
		imports_.configLogPath( config_, logPath.c_str() );
		wchar_t verboseLogging[8]{};
		const DWORD verboseLoggingLength = GetEnvironmentVariableW(
			L"FNQL_WEBUI_VERBOSE_LOG", verboseLogging,
			static_cast<DWORD>( std::size( verboseLogging ) ) );
		if ( verboseLoggingLength > 0
			&& verboseLoggingLength < std::size( verboseLogging )
			&& verboseLogging[0] != L'0' ) {
			imports_.configLogLevel( config_, 2 );
		}
		imports_.configPackagePath( config_, assetRoot_.c_str() );
		imports_.configUserAgent( config_,
			L"Mozilla/5.0 (Windows NT 10.0; Win32; x86) Awesomium/1.7.4.2 FnQL/1.0" );
		if ( !userScript.empty() ) {
			imports_.configUserScript( config_, userScript.c_str() );
		}
		imports_.preferencesEnablePlugins( preferences_, true );
		imports_.preferencesEnableWebSecurity( preferences_, false );
		imports_.preferencesEnableGpuAcceleration( preferences_, false );

		core_ = imports_.webCoreInitialize( config_ );
		if ( !core_ ) {
			Fail( BackendError::OperationFailed,
				"Awesomium WebCore initialization failed" );
			return false;
		}

		bitmapFactory_ = imports_.newBitmapSurfaceFactory();
		void *surfaceFactory = bitmapFactory_
			? imports_.bitmapSurfaceFactoryUpcast( bitmapFactory_ ) : nullptr;
		if ( !surfaceFactory ) {
			Fail( BackendError::ResourceUnavailable,
				"could not create the Awesomium bitmap surface factory" );
			return false;
		}
		imports_.webCoreSetSurfaceFactory( core_, surfaceFactory );

		session_ = imports_.webCoreCreateSession(
			core_, profileRoot_.c_str(), preferences_ );
		if ( !session_ ) {
			Fail( BackendError::OperationFailed,
				"could not create the Awesomium WebSession" );
			return false;
		}

		// Route QL asset requests through the engine whenever HostServices are
		// available. That resolver applies fnql-web.pak first and the external
		// retail web.pak second. The native Awesomium DataPak source remains the
		// compatibility fallback for hosts that do not expose resource callbacks.
		if ( hostServices_.CanRequestResources() ) {
			qlDataSource_ = imports_.newDataSource();
			if ( !qlDataSource_ ) {
				Fail( BackendError::ResourceUnavailable,
					"could not create the FnQL WebUI overlay data source" );
				return false;
			}
			activeBackend_ = this;
			imports_.dataSourceDirectorConnect( qlDataSource_,
				reinterpret_cast<void *>( &OnQlResourceRequest ) );
			imports_.webSessionAddDataSource( session_, L"QL", qlDataSource_ );
		} else {
			dataPakSource_ = imports_.newDataPakSource( webPakPath_.c_str() );
			if ( !dataPakSource_ ) {
				Fail( BackendError::ResourceUnavailable,
					"could not create the retail web.pak data source" );
				return false;
			}
			imports_.webSessionAddDataSource( session_, L"QL", dataPakSource_ );
		}

		// Retail installs a second, lower-case source host for
		// asset://steam/avatar/<steamid>. Keep the Awesomium object and callback
		// boundary here while the engine-owned HostServices callback supplies the
		// external Steam pixels as a bounded PNG resource.
		if ( hostServices_.CanRequestResources() ) {
			steamDataSource_ = imports_.newDataSource();
			if ( !steamDataSource_ ) {
				Fail( BackendError::ResourceUnavailable,
					"could not create the retail Steam avatar data source" );
				return false;
			}
			activeBackend_ = this;
			imports_.dataSourceDirectorConnect( steamDataSource_,
				reinterpret_cast<void *>( &OnSteamResourceRequest ) );
			imports_.webSessionAddDataSource( session_, L"steam", steamDataSource_ );
		}

		view_ = imports_.webCoreCreateView(
			core_, parameters.initialSurface.width, parameters.initialSurface.height,
			session_, 0 );
		if ( !view_ ) {
			Fail( BackendError::OperationFailed,
				"could not create the offscreen Awesomium WebView" );
			return false;
		}
		imports_.webViewSetTransparent( view_, true );
		imports_.webViewResumeRendering( view_ );
		imports_.webViewFocus( view_ );
		return true;
	}

	void ShutdownRuntimeObjects() noexcept {
		if ( activeBackend_ == this ) {
			activeBackend_ = nullptr;
		}
		pendingResources_.fill( {} );
		if ( view_ && imports_.webViewDestroy ) {
			imports_.webViewDestroy( view_ );
		}
		view_ = nullptr;
		if ( session_ && imports_.webSessionRelease ) {
			imports_.webSessionRelease( session_ );
		}
		session_ = nullptr;
		dataPakSource_ = nullptr; // owned by the released WebSession
		qlDataSource_ = nullptr; // owned by the released WebSession
		steamDataSource_ = nullptr; // owned by the released WebSession
		if ( core_ && imports_.webCoreShutdown ) {
			imports_.webCoreShutdown();
		}
		core_ = nullptr;
		bitmapFactory_ = nullptr; // owned by WebCore
		if ( preferences_ && imports_.deleteWebPreferences ) {
			imports_.deleteWebPreferences( preferences_ );
		}
		preferences_ = nullptr;
		if ( config_ && imports_.deleteWebConfig ) {
			imports_.deleteWebConfig( config_ );
		}
		config_ = nullptr;
		hostServices_ = {};
	}

	struct PendingResource {
		int requestId = 0;
		unsigned int attempts = 0;
		bool active = false;
		std::array<char, 4096> path{};
	};

	static void __stdcall OnSteamResourceRequest( int requestId, void *,
		const wchar_t *path ) noexcept {
		if ( activeBackend_ ) {
			activeBackend_->HandleSteamResourceRequest( requestId, path );
		}
	}

	static void __stdcall OnQlResourceRequest( int requestId, void *,
		const wchar_t *path ) noexcept {
		if ( activeBackend_ ) {
			activeBackend_->HandleQlResourceRequest( requestId, path );
		}
	}

	static const wchar_t *ResourceMimeType( const char *path ) noexcept {
		const char *extension = path ? std::strrchr( path, '.' ) : nullptr;
		if ( !extension ) {
			return L"application/octet-stream";
		}
		if ( _stricmp( extension, ".html" ) == 0 ) return L"text/html";
		if ( _stricmp( extension, ".css" ) == 0 ) return L"text/css";
		if ( _stricmp( extension, ".js" ) == 0 ) return L"text/javascript";
		if ( _stricmp( extension, ".json" ) == 0 ) return L"application/json";
		if ( _stricmp( extension, ".png" ) == 0 ) return L"image/png";
		if ( _stricmp( extension, ".jpg" ) == 0 || _stricmp( extension, ".jpeg" ) == 0 ) return L"image/jpeg";
		if ( _stricmp( extension, ".gif" ) == 0 ) return L"image/gif";
		if ( _stricmp( extension, ".svg" ) == 0 ) return L"image/svg+xml";
		if ( _stricmp( extension, ".woff" ) == 0 ) return L"application/font-woff";
		if ( _stricmp( extension, ".ttf" ) == 0 ) return L"application/x-font-ttf";
		return L"application/octet-stream";
	}

	void HandleQlResourceRequest( int requestId, const wchar_t *widePath ) noexcept {
		std::array<char, 4096> utf8{};
		if ( requestId <= 0 || !widePath || !widePath[0] ) {
			return;
		}

		const int converted = WideCharToMultiByte( CP_UTF8, 0, widePath, -1,
			utf8.data(), static_cast<int>( utf8.size() ), nullptr, nullptr );
		if ( converted <= 0 ) {
			return;
		}

		const char *path = utf8.data();
		while ( *path == '/' || *path == '\\' ) {
			++path;
		}
		if ( _strnicmp( path, "asset://ql/", 11 ) == 0 ) {
			path += 11;
		} else if ( _strnicmp( path, "ql://", 5 ) == 0 ) {
			path += 5;
		}
		if ( !path[0] ) {
			path = "index.html";
		}

		std::array<char, 4096> virtualPath{};
		const int written = std::snprintf( virtualPath.data(), virtualPath.size(),
			"asset://ql/%s", path );
		if ( written <= 0 || static_cast<std::size_t>( written ) >= virtualPath.size() ) {
			return;
		}
		TrySendResource( qlDataSource_, requestId, virtualPath.data(), ResourceMimeType( path ) );
	}

	void HandleSteamResourceRequest( int requestId, const wchar_t *widePath ) noexcept {
		std::array<char, 4096> utf8{};
		if ( requestId <= 0 || !widePath || !widePath[0] ) {
			return;
		}

		const int converted = WideCharToMultiByte( CP_UTF8, 0, widePath, -1,
			utf8.data(), static_cast<int>( utf8.size() ), nullptr, nullptr );
		if ( converted <= 0 ) {
			return;
		}

		const char *path = utf8.data();
		while ( *path == '/' || *path == '\\' ) {
			++path;
		}
		if ( _strnicmp( path, "asset://steam/", 14 ) == 0 ) {
			path += 14;
		} else if ( _strnicmp( path, "steam://", 8 ) == 0 ) {
			path += 8;
		}
		if ( _strnicmp( path, "avatar/", 7 ) != 0 ) {
			return;
		}

		std::array<char, 4096> virtualPath{};
		const int written = std::snprintf( virtualPath.data(), virtualPath.size(),
			"asset://steam/%s", path );
		if ( written <= 0 || static_cast<std::size_t>( written ) >= virtualPath.size() ) {
			return;
		}
		if ( !TrySendSteamResource( requestId, virtualPath.data() ) ) {
			QueuePendingResource( requestId, virtualPath.data() );
		}
	}

	bool TrySendResource( void *dataSource, int requestId, const char *path,
		const wchar_t *mimeType ) noexcept {
		ResourceBuffer resource{};
		if ( !dataSource || !mimeType || !hostServices_.CanRequestResources()
			|| !hostServices_.requestResource( hostServices_.context, path, &resource ) ) {
			return false;
		}

		const bool valid = resource.bytes && resource.size > 0
			&& resource.size <= static_cast<std::size_t>(
				( std::numeric_limits<int>::max )() );
		if ( valid ) {
			imports_.dataSourceSendResponse( dataSource, requestId,
				static_cast<int>( resource.size ),
				const_cast<std::uint8_t *>( resource.bytes ), mimeType );
		}
		hostServices_.releaseResource( hostServices_.context, &resource );
		return valid;
	}

	bool TrySendSteamResource( int requestId, const char *path ) noexcept {
		return TrySendResource( steamDataSource_, requestId, path, L"image/png" );
	}

	void QueuePendingResource( int requestId, const char *path ) noexcept {
		PendingResource *freeSlot = nullptr;
		for ( PendingResource &pending : pendingResources_ ) {
			if ( pending.active && pending.requestId == requestId ) {
				freeSlot = &pending;
				break;
			}
			if ( !pending.active && !freeSlot ) {
				freeSlot = &pending;
			}
		}
		if ( !freeSlot ) {
			return;
		}
		freeSlot->requestId = requestId;
		freeSlot->attempts = 0;
		freeSlot->active = true;
		std::snprintf( freeSlot->path.data(), freeSlot->path.size(), "%s", path );
	}

	void RetryPendingResources() noexcept {
		for ( PendingResource &pending : pendingResources_ ) {
			if ( !pending.active ) {
				continue;
			}
			++pending.attempts;
			if ( pending.attempts % 5u != 0u ) {
				continue;
			}
			if ( TrySendSteamResource( pending.requestId, pending.path.data() ) ) {
				pending = {};
			}
		}
	}

	bool SelectRuntimePaths( const StartupParameters &parameters ) noexcept {
		const std::wstring home = ToWide( parameters.runtimePath );
		const std::wstring base = ToWide( parameters.basePath );
		const std::wstring retail = ToWide( parameters.retailPath );
		std::array<std::wstring, 4> roots{
			base, retail, home, ExecutableDirectory()
		};

		assetRoot_.clear();
		profileRoot_.clear();
		webPakPath_.clear();
		if ( module_ ) {
			// The generated runtime keeps process-global state. Keep the first
			// successfully loaded DLL pinned, but revalidate its external helper.
			childProcessPath_ = JoinPath(
				runtimeRoot_, L"awesomium_process.exe" );
			if ( !FileExists( childProcessPath_ ) ) {
				childProcessPath_.clear();
			}
		} else {
			runtimeRoot_.clear();
			dllPath_.clear();
			childProcessPath_.clear();
			for ( const std::wstring &root : roots ) {
				if ( root.empty() ) {
					continue;
				}
				const std::wstring dll = JoinPath( root, L"awesomium.dll" );
				const std::wstring helper = JoinPath(
					root, L"awesomium_process.exe" );
				if ( FileExists( dll ) && FileExists( helper ) ) {
					runtimeRoot_ = root;
					dllPath_ = dll;
					childProcessPath_ = helper;
					break;
				}
			}
		}
		for ( const std::wstring &root : roots ) {
			if ( root.empty() ) {
				continue;
			}
			const std::wstring pak = JoinPath( root, L"web.pak" );
			if ( FileExists( pak ) ) {
				assetRoot_ = root;
				webPakPath_ = pak;
				break;
			}
		}
		profileRoot_ = home.empty() ? assetRoot_ : home;

		if ( runtimeRoot_.empty() || dllPath_.empty()
			|| childProcessPath_.empty() ) {
			Fail( BackendError::Unavailable,
				"retail awesomium.dll and awesomium_process.exe were not found together" );
			return false;
		}
		if ( assetRoot_.empty() || webPakPath_.empty() ) {
			Fail( BackendError::ResourceUnavailable,
				"retail web.pak was not found under the configured QL paths" );
			return false;
		}
		if ( profileRoot_.empty() ) {
			Fail( BackendError::InvalidArgument,
				"the Awesomium profile path is empty" );
			return false;
		}
		return true;
	}

	bool LoadRuntime() noexcept {
		if ( module_ ) {
			return true;
		}

		constexpr DWORD secureFlags = LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
			| LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
		module_ = LoadLibraryExW( dllPath_.c_str(), nullptr, secureFlags );
		if ( !module_ && GetLastError() == ERROR_INVALID_PARAMETER ) {
			// Windows 7 without KB2533623 lacks the secure search flags. The
			// absolute DLL path plus altered search path keeps dependencies rooted
			// beside the legitimate retail runtime.
			module_ = LoadLibraryExW(
				dllPath_.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH );
		}
		if ( !module_ ) {
			Fail( BackendError::Unavailable,
				"could not load retail awesomium.dll (Win32 error %lu)",
				static_cast<unsigned long>( GetLastError() ) );
			return false;
		}
		return true;
	}

	static bool FileExists( const std::wstring &path ) noexcept {
		const DWORD attributes = GetFileAttributesW( path.c_str() );
		return attributes != INVALID_FILE_ATTRIBUTES
			&& ( attributes & FILE_ATTRIBUTE_DIRECTORY ) == 0;
	}

	static std::wstring JoinPath( std::wstring_view root, std::wstring_view leaf ) {
		std::wstring path( root );
		if ( !path.empty() && path.back() != L'\\' && path.back() != L'/' ) {
			path.push_back( L'\\' );
		}
		path.append( leaf );
		return path;
	}

	static std::wstring ExecutableDirectory() {
		std::array<wchar_t, 32768> path{};
		const DWORD length = GetModuleFileNameW(
			nullptr, path.data(), static_cast<DWORD>( path.size() ) );
		if ( length == 0 || length >= path.size() ) {
			return {};
		}
		std::wstring directory( path.data(), length );
		const std::size_t separator = directory.find_last_of( L"\\/" );
		return separator == std::wstring::npos
			? std::wstring{} : directory.substr( 0, separator );
	}

	static std::wstring ToWide( std::string_view text ) {
		if ( text.empty() ) {
			return {};
		}
		if ( text.size() > static_cast<std::size_t>(
			( std::numeric_limits<int>::max )() ) ) {
			return {};
		}

		const int inputLength = static_cast<int>( text.size() );
		UINT codePage = CP_UTF8;
		DWORD flags = MB_ERR_INVALID_CHARS;
		int count = MultiByteToWideChar(
			codePage, flags, text.data(), inputLength, nullptr, 0 );
		if ( count <= 0 ) {
			codePage = CP_ACP;
			flags = 0;
			count = MultiByteToWideChar(
				codePage, flags, text.data(), inputLength, nullptr, 0 );
		}
		if ( count <= 0 ) {
			return {};
		}

		std::wstring result( static_cast<std::size_t>( count ), L'\0' );
		if ( MultiByteToWideChar( codePage, flags, text.data(), inputLength,
			result.data(), count ) != count ) {
			return {};
		}
		return result;
	}

	static const wchar_t *__stdcall CopyReturnedString( const wchar_t *value ) noexcept {
		constexpr std::size_t kBufferCount = 8;
		constexpr std::size_t kBufferCharacters = 4096;
		thread_local std::array<std::array<wchar_t, kBufferCharacters>, kBufferCount> buffers{};
		thread_local std::size_t nextBuffer = 0;
		auto &buffer = buffers[nextBuffer++ % buffers.size()];
		if ( !value ) {
			buffer[0] = L'\0';
			return buffer.data();
		}
		std::size_t index = 0;
		while ( value[index] && index + 1 < buffer.size() ) {
			buffer[index] = value[index];
			++index;
		}
		buffer[index] = L'\0';
		return buffer.data();
	}

	BackendResult LastFailure() const noexcept {
		return { lastErrorCode_, std::string_view( lastError_.data() ) };
	}

	BackendResult Fail( BackendError code, const char *format, ... ) noexcept {
		va_list arguments;
		va_start( arguments, format );
		std::vsnprintf( lastError_.data(), lastError_.size(), format, arguments );
		va_end( arguments );
		lastError_.back() = '\0';
		lastErrorCode_ = code;
		return LastFailure();
	}

	Imports imports_{};
	HMODULE module_ = nullptr;
	bool importsResolved_ = false;
	void *config_ = nullptr;
	void *preferences_ = nullptr;
	void *core_ = nullptr;
	void *bitmapFactory_ = nullptr;
	void *session_ = nullptr;
	void *dataPakSource_ = nullptr;
	void *qlDataSource_ = nullptr;
	void *steamDataSource_ = nullptr;
	void *view_ = nullptr;
	HostServices hostServices_{};
	std::array<PendingResource, 64> pendingResources_{};
	BackendStatus status_{};
	BackendError lastErrorCode_ = BackendError::None;
	std::array<char, kBackendDiagnosticCapacity> lastError_{};
	std::wstring runtimeRoot_{};
	std::wstring assetRoot_{};
	std::wstring profileRoot_{};
	std::wstring dllPath_{};
	std::wstring childProcessPath_{};
	std::wstring webPakPath_{};
	static RetailAwesomiumBackend *activeBackend_;
};

RetailAwesomiumBackend *RetailAwesomiumBackend::activeBackend_ = nullptr;

RetailAwesomiumBackend &PlatformBackend() noexcept {
	static RetailAwesomiumBackend backend;
	return backend;
}

} // namespace

void InstallRetailAwesomiumBackend( BackendHost &host ) noexcept {
	(void)host.InstallBackend( PlatformBackend() );
}

} // namespace fnql::webui

#else

namespace fnql::webui {

void InstallRetailAwesomiumBackend( BackendHost & ) noexcept {
}

} // namespace fnql::webui

#endif
