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
// webui_backend.hpp -- browser-neutral WebUI runtime boundary

// Retail Quake Live's Windows client owns a browser core, session, view,
// software surface, input bridge, and script bridge.  This contract models
// those observable responsibilities without exposing an Awesomium header,
// object layout, decorated C++ symbol, or allocator across the engine
// boundary.  A future Windows-x86 adapter may translate these operations to
// a legitimately installed external runtime; the engine remains usable with
// the deterministic null backend on every platform.

#ifndef FNQL_CLIENT_WEBUI_BACKEND_HPP
#define FNQL_CLIENT_WEBUI_BACKEND_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string_view>

namespace fnql::webui {

inline constexpr std::uint32_t kBackendInterfaceVersion = 1;
inline constexpr std::size_t kBackendDiagnosticCapacity = 384;
inline constexpr std::string_view kTrustedNavigationPrefix = "asset://ql/";

inline bool IsTrustedNavigationUrl( std::string_view url ) noexcept {
	if ( url.size() < kTrustedNavigationPrefix.size()
		|| url.compare( 0, kTrustedNavigationPrefix.size(), kTrustedNavigationPrefix ) != 0 ) {
		return false;
	}

	for ( const unsigned char character : url ) {
		if ( character == 0 || character < 0x20 || character == 0x7f ) {
			return false;
		}
	}
	return true;
}

enum class BackendError : int {
	None = 0,
	Unavailable = 1,
	ResourceUnavailable = 2,
	InvalidArgument = 3,
	InvalidState = 4,
	OperationFailed = 5,
	SurfaceMismatch = 6,
	InterfaceMismatch = 7,
	Crashed = 8,
	Unsupported = 9
};

enum class Lifecycle : std::uint8_t {
	Dormant,
	Starting,
	Running,
	Failed
};

enum class Capability : std::uint32_t {
	None = 0,
	SoftwareSurface = 1u << 0,
	IntegerScriptResult = 1u << 1,
	TransparentView = 1u << 2,
	ResourceRequests = 1u << 3
};

constexpr Capability operator|( Capability lhs, Capability rhs ) noexcept {
	return static_cast<Capability>( static_cast<std::uint32_t>( lhs )
		| static_cast<std::uint32_t>( rhs ) );
}

constexpr bool HasCapability( Capability set, Capability value ) noexcept {
	return ( static_cast<std::uint32_t>( set ) & static_cast<std::uint32_t>( value ) )
		== static_cast<std::uint32_t>( value );
}

struct BackendResult {
	BackendError code = BackendError::None;
	std::string_view detail{};

	constexpr bool Succeeded() const noexcept {
		return code == BackendError::None;
	}

	constexpr explicit operator bool() const noexcept {
		return Succeeded();
	}

	static constexpr BackendResult Success() noexcept {
		return {};
	}

	static constexpr BackendResult Failure( BackendError error,
		std::string_view message ) noexcept {
		return { error, message };
	}
};

struct BackendDescriptor {
	std::uint32_t interfaceVersion = kBackendInterfaceVersion;
	std::string_view name = "Unavailable";
	Capability capabilities = Capability::None;
	bool available = false;
};

struct SurfaceSize {
	int width = 0;
	int height = 0;

	constexpr bool IsValid() const noexcept {
		return width > 0 && height > 0;
	}

	constexpr bool operator==( const SurfaceSize &other ) const noexcept {
		return width == other.width && height == other.height;
	}

	constexpr bool operator!=( const SurfaceSize &other ) const noexcept {
		return !( *this == other );
	}

	constexpr SurfaceSize ConstrainedTo( int maximumDimension ) const noexcept {
		if ( !IsValid() || maximumDimension <= 0
			|| ( width <= maximumDimension && height <= maximumDimension ) ) {
			return *this;
		}

		if ( width >= height ) {
			const int scaledHeight = static_cast<int>(
				static_cast<std::int64_t>( height ) * maximumDimension / width );
			return { maximumDimension, scaledHeight > 0 ? scaledHeight : 1 };
		}

		const int scaledWidth = static_cast<int>(
			static_cast<std::int64_t>( width ) * maximumDimension / height );
		return { scaledWidth > 0 ? scaledWidth : 1, maximumDimension };
	}

	bool MinimumRowBytes( std::size_t *bytes ) const noexcept {
		if ( !bytes || !IsValid() ) {
			return false;
		}

		const auto pixelWidth = static_cast<std::size_t>( width );
		if ( pixelWidth > ( std::numeric_limits<std::size_t>::max )() / 4u ) {
			return false;
		}

		*bytes = pixelWidth * 4u;
		return true;
	}
};

// Backends copy pixels in the renderer's established 32-bit RGBA order.  The
// alpha channel remains premultiplied, matching software Chromium surfaces.
enum class SurfaceFormat : std::uint8_t {
	Rgba8Premultiplied
};

struct MutableSurface {
	std::uint8_t *pixels = nullptr;
	std::size_t capacity = 0;
	std::size_t rowStride = 0;
	SurfaceSize size{};
	SurfaceFormat format = SurfaceFormat::Rgba8Premultiplied;

	bool IsValid() const noexcept {
		std::size_t minimumRowBytes;
		if ( !pixels || !size.MinimumRowBytes( &minimumRowBytes )
			|| rowStride < minimumRowBytes ) {
			return false;
		}

		const auto rows = static_cast<std::size_t>( size.height );
		return rows <= ( std::numeric_limits<std::size_t>::max )() / rowStride
			&& capacity >= rowStride * rows;
	}
};

struct ResourceBuffer {
	const std::uint8_t *bytes = nullptr;
	std::size_t size = 0;
	void *releaseToken = nullptr;

	constexpr explicit operator bool() const noexcept {
		return bytes != nullptr;
	}
};

using RequestResourceFn = bool (*)( void *context, std::string_view virtualPath,
	ResourceBuffer *resource ) noexcept;
using ReleaseResourceFn = void (*)( void *context,
	ResourceBuffer *resource ) noexcept;

struct HostServices {
	void *context = nullptr;
	RequestResourceFn requestResource = nullptr;
	ReleaseResourceFn releaseResource = nullptr;

	constexpr bool CanRequestResources() const noexcept {
		return requestResource != nullptr && releaseResource != nullptr;
	}
};

struct StartupParameters {
	// All text is borrowed for the duration of Start().  A backend that needs
	// it after the call must make an owned copy.
	std::string_view runtimePath{};
	std::string_view basePath{};
	std::string_view retailPath{};
	std::string_view playerName{};
	std::uint32_t appId = 0;
	std::uint32_t identityLow = 0;
	std::uint32_t identityHigh = 0;
	SurfaceSize initialSurface{};
	// Function pointers remain valid for the installed backend's lifetime.
	// Every successful request must be paired with exactly one release.
	HostServices hostServices{};
	std::string_view initialConfigJson{};
	std::string_view initialMapJson{};
	std::string_view initialFactoryJson{};
	// Executed by capable runtimes before document scripts and reinjected by
	// the client after navigation. This lets retail pages observe qz_instance
	// during their original bootstrap order without exposing runtime objects.
	std::string_view startupScript{};
};

struct BackendStatus {
	bool viewAlive = false;
	bool windowObjectBound = false;
	bool loading = false;
	bool crashed = false;
	bool surfaceDirty = false;
	bool renderingPaused = false;
	bool focused = false;
	SurfaceSize surface{};
	SurfaceFormat surfaceFormat = SurfaceFormat::Rgba8Premultiplied;
	int nativeErrorCode = 0;

	constexpr bool HasSurface() const noexcept {
		return viewAlive && !crashed && surface.IsValid();
	}
};

struct ScriptRequest {
	std::string_view source{};
	std::string_view frame{};
};

struct IntegerScriptResult {
	BackendResult result = BackendResult::Failure(
		BackendError::OperationFailed, "script result was not produced" );
	int value = 0;
};

enum class MouseButton : std::uint8_t {
	Left,
	Middle,
	Right
};

enum class ButtonAction : std::uint8_t {
	Press,
	Release
};

struct MouseMoveEvent {
	int x = 0;
	int y = 0;
};

struct MouseButtonEvent {
	MouseButton button = MouseButton::Left;
	ButtonAction action = ButtonAction::Press;
};

// Wheel values are logical ticks.  Runtime-specific units (Awesomium uses a
// larger integer delta) belong in the adapter rather than in engine input.
struct MouseWheelEvent {
	int horizontalTicks = 0;
	int verticalTicks = 0;
};

enum class KeyboardEventType : std::uint8_t {
	KeyDown,
	KeyUp,
	Character
};

struct KeyboardEvent {
	KeyboardEventType type = KeyboardEventType::KeyDown;
	std::uint32_t virtualKey = 0;
	std::intptr_t nativeKey = 0;
};

class Backend {
public:
	virtual ~Backend() = default;

	// FnQL drives backends from the client main thread.  Shutdown() must be
	// idempotent and Status() must remain safe after shutdown so partial-start
	// cleanup and failed-state diagnostics cannot depend on SDK object liveness.
	virtual BackendDescriptor Describe() const noexcept = 0;
	virtual BackendResult Start( const StartupParameters &parameters ) noexcept = 0;
	virtual void Shutdown() noexcept = 0;
	virtual BackendStatus Status() const noexcept = 0;

	virtual BackendResult Navigate( std::string_view ) noexcept {
		return Unsupported( "URL navigation is unsupported" );
	}

	virtual BackendResult Pump() noexcept {
		return Unsupported( "runtime pumping is unsupported" );
	}

	virtual BackendResult Resize( SurfaceSize ) noexcept {
		return Unsupported( "surface resize is unsupported" );
	}

	virtual BackendResult ExecuteScript( const ScriptRequest & ) noexcept {
		return Unsupported( "script execution is unsupported" );
	}

	virtual IntegerScriptResult EvaluateInteger( const ScriptRequest & ) noexcept {
		return { Unsupported( "integer script results are unsupported" ), 0 };
	}

	virtual BackendResult CopySurface( const MutableSurface & ) noexcept {
		return Unsupported( "software surface copies are unsupported" );
	}

	virtual BackendResult SetZoom( int ) noexcept {
		return Unsupported( "zoom is unsupported" );
	}

	virtual BackendResult SetRenderingPaused( bool ) noexcept {
		return Unsupported( "rendering pause is unsupported" );
	}

	virtual BackendResult SetFocus( bool ) noexcept {
		return Unsupported( "focus changes are unsupported" );
	}

	virtual BackendResult InjectMouseMove( const MouseMoveEvent & ) noexcept {
		return Unsupported( "mouse movement is unsupported" );
	}

	virtual BackendResult InjectMouseButton( const MouseButtonEvent & ) noexcept {
		return Unsupported( "mouse buttons are unsupported" );
	}

	virtual BackendResult InjectMouseWheel( const MouseWheelEvent & ) noexcept {
		return Unsupported( "mouse wheel input is unsupported" );
	}

	virtual BackendResult InjectKeyboard( const KeyboardEvent & ) noexcept {
		return Unsupported( "keyboard input is unsupported" );
	}

	virtual BackendResult StopLoading() noexcept {
		return Unsupported( "stopping navigation is unsupported" );
	}

	virtual BackendResult ClearCache() noexcept {
		return Unsupported( "cache clearing is unsupported" );
	}

	virtual BackendResult Reload( bool ) noexcept {
		return Unsupported( "reload is unsupported" );
	}

protected:
	static constexpr BackendResult Unsupported( std::string_view detail ) noexcept {
		return BackendResult::Failure( BackendError::Unsupported, detail );
	}
};

class NullBackend final : public Backend {
public:
	BackendDescriptor Describe() const noexcept override {
		return { kBackendInterfaceVersion, "Unavailable", Capability::None, false };
	}

	BackendResult Start( const StartupParameters & ) noexcept override {
		return BackendResult::Failure( BackendError::Unavailable,
			"Awesomium runtime backend is unavailable in this build." );
	}

	void Shutdown() noexcept override {
	}

	BackendStatus Status() const noexcept override {
		return {};
	}
};

class BackendHost final {
public:
	BackendHost() noexcept : backend_( &nullBackend_ ) {
		CopyProviderName( backend_->Describe().name );
	}

	BackendHost( const BackendHost & ) = delete;
	BackendHost &operator=( const BackendHost & ) = delete;

	~BackendHost() {
		Shutdown();
	}

	// The host does not own an installed backend.  Its lifetime must extend
	// through RemoveBackend(), which makes allocator ownership explicit for a
	// future external-runtime adapter.
	BackendResult InstallBackend( Backend &backend ) noexcept {
		if ( lifecycle_ != Lifecycle::Dormant ) {
			return Remember( BackendResult::Failure( BackendError::InvalidState,
				"cannot replace a running WebUI backend" ) );
		}

		const BackendDescriptor descriptor = backend.Describe();
		if ( descriptor.interfaceVersion != kBackendInterfaceVersion ) {
			return Remember( BackendResult::Failure( BackendError::InterfaceMismatch,
				"WebUI backend interface version does not match the engine" ) );
		}
		if ( !descriptor.available ) {
			return Remember( BackendResult::Failure( BackendError::Unavailable,
				"WebUI backend reported that its runtime is unavailable" ) );
		}

		backend_ = &backend;
		CopyProviderName( descriptor.name );
		return Remember( BackendResult::Success() );
	}

	void RemoveBackend() noexcept {
		Shutdown();
		backend_ = &nullBackend_;
		CopyProviderName( backend_->Describe().name );
		Remember( BackendResult::Success() );
	}

	bool IsAvailable() const noexcept {
		const BackendDescriptor descriptor = backend_->Describe();
		return descriptor.interfaceVersion == kBackendInterfaceVersion
			&& descriptor.available;
	}

	bool IsRunning() const noexcept {
		return lifecycle_ == Lifecycle::Running;
	}

	Lifecycle GetLifecycle() const noexcept {
		return lifecycle_;
	}

	const char *ProviderName() const noexcept {
		return providerName_.data();
	}

	BackendDescriptor Descriptor() const noexcept {
		return backend_->Describe();
	}

	BackendStatus Status() const noexcept {
		if ( lifecycle_ != Lifecycle::Running && lifecycle_ != Lifecycle::Failed ) {
			return {};
		}
		return backend_->Status();
	}

	BackendResult LastResult() const noexcept {
		return { lastError_, std::string_view( diagnostic_.data() ) };
	}

	BackendResult Start( const StartupParameters &parameters ) noexcept {
		if ( lifecycle_ == Lifecycle::Running ) {
			const BackendStatus status = backend_->Status();
			if ( status.viewAlive && !status.crashed ) {
				if ( parameters.initialSurface.IsValid()
					&& status.surface != parameters.initialSurface ) {
					return Resize( parameters.initialSurface );
				}
				return Remember( BackendResult::Success() );
			}
			lifecycle_ = Lifecycle::Failed;
		}

		if ( lifecycle_ != Lifecycle::Dormant && lifecycle_ != Lifecycle::Failed ) {
			return Remember( BackendResult::Failure( BackendError::InvalidState,
				"WebUI backend cannot start in its current lifecycle state" ) );
		}
		if ( !IsAvailable() ) {
			return Remember( BackendResult::Failure( BackendError::Unavailable,
				"Awesomium runtime backend is unavailable in this build." ) );
		}
		if ( !parameters.initialSurface.IsValid() ) {
			return Remember( BackendResult::Failure( BackendError::InvalidArgument,
				"WebUI startup surface dimensions are invalid" ) );
		}

		if ( lifecycle_ == Lifecycle::Failed ) {
			backend_->Shutdown();
		}
		lifecycle_ = Lifecycle::Starting;
		const BackendResult result = backend_->Start( parameters );
		if ( !result ) {
			const BackendResult remembered = Remember( result );
			backend_->Shutdown();
			lifecycle_ = Lifecycle::Failed;
			return remembered;
		}

		const BackendStatus status = backend_->Status();
		if ( !status.viewAlive || status.crashed ) {
			backend_->Shutdown();
			lifecycle_ = Lifecycle::Failed;
			return Remember( BackendResult::Failure(
				status.crashed ? BackendError::Crashed : BackendError::OperationFailed,
				"WebUI backend started without a live browser view" ) );
		}

		lifecycle_ = Lifecycle::Running;
		return Remember( BackendResult::Success() );
	}

	void Shutdown() noexcept {
		if ( lifecycle_ != Lifecycle::Dormant ) {
			backend_->Shutdown();
		}
		lifecycle_ = Lifecycle::Dormant;
	}

	BackendResult Pump() noexcept {
		BackendResult ready = RequireRunning();
		if ( !ready ) {
			return ready;
		}

		const BackendResult result = backend_->Pump();
		if ( !result ) {
			return Remember( result );
		}
		const BackendStatus status = backend_->Status();
		if ( status.crashed || !status.viewAlive ) {
			lifecycle_ = Lifecycle::Failed;
			return Remember( BackendResult::Failure(
				status.crashed ? BackendError::Crashed : BackendError::OperationFailed,
				"WebUI browser view was lost while pumping the backend" ) );
		}
		return Remember( BackendResult::Success() );
	}

	BackendResult Navigate( std::string_view url ) noexcept {
		BackendResult ready = RequireRunning();
		if ( !ready ) {
			return ready;
		}
		if ( !IsTrustedNavigationUrl( url ) ) {
			return Remember( BackendResult::Failure( BackendError::InvalidArgument,
				"WebUI navigation is restricted to asset://ql/" ) );
		}
		return Remember( backend_->Navigate( url ) );
	}

	BackendResult Resize( SurfaceSize size ) noexcept {
		BackendResult ready = RequireRunning();
		if ( !ready ) {
			return ready;
		}
		if ( !size.IsValid() ) {
			return Remember( BackendResult::Failure( BackendError::InvalidArgument,
				"WebUI surface dimensions are invalid" ) );
		}
		return Remember( backend_->Resize( size ) );
	}

	BackendResult ExecuteScript( ScriptRequest request ) noexcept {
		BackendResult ready = RequireRunning();
		if ( !ready ) {
			return ready;
		}
		if ( request.source.empty() ) {
			return Remember( BackendResult::Failure( BackendError::InvalidArgument,
				"WebUI script source is empty" ) );
		}
		return Remember( backend_->ExecuteScript( request ) );
	}

	IntegerScriptResult EvaluateInteger( ScriptRequest request ) noexcept {
		BackendResult ready = RequireRunning();
		if ( !ready ) {
			return { ready, 0 };
		}
		if ( request.source.empty() ) {
			return { Remember( BackendResult::Failure( BackendError::InvalidArgument,
				"WebUI script source is empty" ) ), 0 };
		}
		if ( !HasCapability( backend_->Describe().capabilities,
			Capability::IntegerScriptResult ) ) {
			return { Remember( BackendResult::Failure( BackendError::Unsupported,
				"WebUI backend does not support integer script results" ) ), 0 };
		}

		IntegerScriptResult result = backend_->EvaluateInteger( request );
		result.result = Remember( result.result );
		if ( !result.result ) {
			result.value = 0;
		}
		return result;
	}

	BackendResult CopySurface( const MutableSurface &surface ) noexcept {
		BackendResult ready = RequireRunning();
		if ( !ready ) {
			return ready;
		}
		if ( !HasCapability( backend_->Describe().capabilities,
			Capability::SoftwareSurface ) ) {
			return Remember( BackendResult::Failure( BackendError::Unsupported,
				"WebUI backend does not expose a software surface" ) );
		}
		if ( !surface.IsValid() ) {
			return Remember( BackendResult::Failure( BackendError::InvalidArgument,
				"WebUI destination surface is invalid" ) );
		}

		const BackendStatus status = backend_->Status();
		if ( !status.HasSurface() || surface.size != status.surface
			|| surface.format != status.surfaceFormat ) {
			return Remember( BackendResult::Failure( BackendError::SurfaceMismatch,
				"WebUI destination does not match the live browser surface" ) );
		}
		return Remember( backend_->CopySurface( surface ) );
	}

	BackendResult SetZoom( int percent ) noexcept {
		BackendResult ready = RequireRunning();
		if ( !ready ) {
			return ready;
		}
		if ( percent <= 0 || percent > 1000 ) {
			return Remember( BackendResult::Failure( BackendError::InvalidArgument,
				"WebUI zoom percentage is out of range" ) );
		}
		return Remember( backend_->SetZoom( percent ) );
	}

	BackendResult SetRenderingPaused( bool paused ) noexcept {
		BackendResult ready = RequireRunning();
		return ready ? Remember( backend_->SetRenderingPaused( paused ) ) : ready;
	}

	BackendResult SetFocus( bool focused ) noexcept {
		BackendResult ready = RequireRunning();
		return ready ? Remember( backend_->SetFocus( focused ) ) : ready;
	}

	BackendResult InjectMouseMove( MouseMoveEvent event ) noexcept {
		BackendResult ready = RequireRunning();
		return ready ? Remember( backend_->InjectMouseMove( event ) ) : ready;
	}

	BackendResult InjectMouseButton( MouseButtonEvent event ) noexcept {
		BackendResult ready = RequireRunning();
		return ready ? Remember( backend_->InjectMouseButton( event ) ) : ready;
	}

	BackendResult InjectMouseWheel( MouseWheelEvent event ) noexcept {
		BackendResult ready = RequireRunning();
		if ( !ready ) {
			return ready;
		}
		if ( event.horizontalTicks == 0 && event.verticalTicks == 0 ) {
			return Remember( BackendResult::Failure( BackendError::InvalidArgument,
				"WebUI mouse wheel event has no movement" ) );
		}
		return Remember( backend_->InjectMouseWheel( event ) );
	}

	BackendResult InjectKeyboard( KeyboardEvent event ) noexcept {
		BackendResult ready = RequireRunning();
		return ready ? Remember( backend_->InjectKeyboard( event ) ) : ready;
	}

	BackendResult StopLoading() noexcept {
		BackendResult ready = RequireRunning();
		return ready ? Remember( backend_->StopLoading() ) : ready;
	}

	BackendResult ClearCache() noexcept {
		BackendResult ready = RequireRunning();
		return ready ? Remember( backend_->ClearCache() ) : ready;
	}

	BackendResult Reload( bool ignoreCache ) noexcept {
		BackendResult ready = RequireRunning();
		return ready ? Remember( backend_->Reload( ignoreCache ) ) : ready;
	}

private:
	BackendResult RequireRunning() noexcept {
		if ( lifecycle_ != Lifecycle::Running ) {
			return Remember( BackendResult::Failure( BackendError::InvalidState,
				"WebUI backend does not have a running browser view" ) );
		}

		const BackendStatus status = backend_->Status();
		if ( status.crashed || !status.viewAlive ) {
			lifecycle_ = Lifecycle::Failed;
			return Remember( BackendResult::Failure(
				status.crashed ? BackendError::Crashed : BackendError::OperationFailed,
				"WebUI browser view is no longer available" ) );
		}
		return BackendResult::Success();
	}

	BackendResult Remember( BackendResult result ) noexcept {
		lastError_ = result.code;
		diagnostic_.fill( '\0' );
		const std::size_t copyLength = result.detail.size() < diagnostic_.size() - 1
			? result.detail.size()
			: diagnostic_.size() - 1;
		if ( copyLength > 0 ) {
			std::memcpy( diagnostic_.data(), result.detail.data(), copyLength );
		}
		return { lastError_, std::string_view( diagnostic_.data(), copyLength ) };
	}

	void CopyProviderName( std::string_view name ) noexcept {
		providerName_.fill( '\0' );
		if ( name.empty() ) {
			name = "Unnamed WebUI backend";
		}
		const std::size_t copyLength = name.size() < providerName_.size() - 1
			? name.size()
			: providerName_.size() - 1;
		std::memcpy( providerName_.data(), name.data(), copyLength );
	}

	NullBackend nullBackend_{};
	Backend *backend_;
	Lifecycle lifecycle_ = Lifecycle::Dormant;
	BackendError lastError_ = BackendError::None;
	std::array<char, kBackendDiagnosticCapacity> diagnostic_{};
	std::array<char, 96> providerName_{};
};

// Process-wide client host.  External adapters opt in by explicitly calling
// InstallBackend(); FnQL never searches for or loads an SDK DLL implicitly.
BackendHost &ClientBackendHost() noexcept;

} // namespace fnql::webui

#endif // FNQL_CLIENT_WEBUI_BACKEND_HPP
