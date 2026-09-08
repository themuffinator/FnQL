#include "webui_backend.hpp"
#include "webui_native_request.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using fnql::webui::Backend;
using fnql::webui::BackendDescriptor;
using fnql::webui::BackendError;
using fnql::webui::BackendHost;
using fnql::webui::BackendResult;
using fnql::webui::BackendStatus;
using fnql::webui::ButtonAction;
using fnql::webui::Capability;
using fnql::webui::IntegerScriptResult;
using fnql::webui::KeyboardEvent;
using fnql::webui::Lifecycle;
using fnql::webui::MouseButtonEvent;
using fnql::webui::MouseMoveEvent;
using fnql::webui::MouseWheelEvent;
using fnql::webui::MutableSurface;
using fnql::webui::ResourceBuffer;
using fnql::webui::ScriptRequest;
using fnql::webui::StringScriptResult;
using fnql::webui::StartupParameters;
using fnql::webui::SurfaceSize;

#define CHECK( expression ) \
	do { \
		if ( !( expression ) ) { \
			std::cerr << __func__ << ':' << __LINE__ \
				<< ": check failed: " #expression "\n"; \
			return false; \
		} \
	} while ( false )

struct ResourceProbe {
	std::array<std::uint8_t, 3> bytes{ 0x51, 0x4c, 0x21 };
	std::string requestedPath;
	int requestCount = 0;
	int releaseCount = 0;
};

bool RequestProbeResource( void *context, std::string_view path,
	ResourceBuffer *resource ) noexcept {
	auto *probe = static_cast<ResourceProbe *>( context );
	if ( !probe || !resource ) {
		return false;
	}
	probe->requestedPath.assign( path.data(), path.size() );
	++probe->requestCount;
	resource->bytes = probe->bytes.data();
	resource->size = probe->bytes.size();
	resource->releaseToken = probe;
	return true;
}

void ReleaseProbeResource( void *context, ResourceBuffer *resource ) noexcept {
	auto *probe = static_cast<ResourceProbe *>( context );
	if ( !probe || !resource || resource->releaseToken != probe ) {
		return;
	}
	++probe->releaseCount;
	*resource = {};
}

class FakeBackend final : public Backend {
public:
	BackendDescriptor descriptor{
		fnql::webui::kBackendInterfaceVersion,
		"Deterministic fake browser",
		Capability::SoftwareSurface | Capability::IntegerScriptResult
			| Capability::TransparentView,
		true
	};
	BackendStatus status{};
	bool failStart = false;
	bool clearFailureDetailOnShutdown = false;
	bool requestResourceOnStart = false;
	bool startupResourceRead = false;
	std::uint8_t startupResourceFirstByte = 0;
	std::string startFailureDetail = "fake startup failure";
	int startCount = 0;
	int shutdownCount = 0;
	int pumpCount = 0;
	int resizeCount = 0;
	int copyCount = 0;
	int mouseMoveCount = 0;
	int mouseButtonCount = 0;
	int mouseWheelCount = 0;
	int keyboardCount = 0;
	int stopCount = 0;
	int clearCacheCount = 0;
	int reloadCount = 0;
	int zoom = 100;
	bool ignoreCache = false;
	std::string lastUrl;
	std::string lastScript;
	std::string lastFrame;
	std::string stringValue = "result";
	bool failString = false;
	bool invalidStringLength = false;
	int stringCount = 0;
	StartupParameters lastStartup{};
	MouseMoveEvent lastMouseMove{};
	MouseButtonEvent lastMouseButton{};
	MouseWheelEvent lastMouseWheel{};
	KeyboardEvent lastKeyboard{};

	BackendDescriptor Describe() const noexcept override {
		return descriptor;
	}

	BackendResult Start( const StartupParameters &parameters ) noexcept override {
		++startCount;
		lastStartup = parameters;
		if ( failStart ) {
			return BackendResult::Failure( BackendError::OperationFailed,
				startFailureDetail );
		}
		if ( requestResourceOnStart ) {
			ResourceBuffer resource;
			startupResourceRead = parameters.hostServices.CanRequestResources()
				&& parameters.hostServices.requestResource(
					parameters.hostServices.context,
					"asset://ql/index.html",
					&resource );
			if ( startupResourceRead ) {
				startupResourceFirstByte = resource.size > 0 ? resource.bytes[0] : 0;
				parameters.hostServices.releaseResource(
					parameters.hostServices.context, &resource );
			}
		}
		status.viewAlive = true;
		status.windowObjectBound = true;
		status.focused = true;
		status.surfaceDirty = true;
		status.surface = parameters.initialSurface;
		return BackendResult::Success();
	}

	void Shutdown() noexcept override {
		++shutdownCount;
		status = {};
		if ( clearFailureDetailOnShutdown ) {
			startFailureDetail.clear();
		}
	}

	BackendStatus Status() const noexcept override {
		return status;
	}

	BackendResult Navigate( std::string_view url ) noexcept override {
		lastUrl.assign( url.data(), url.size() );
		status.loading = true;
		return BackendResult::Success();
	}

	BackendResult Pump() noexcept override {
		++pumpCount;
		status.loading = false;
		return BackendResult::Success();
	}

	BackendResult Resize( SurfaceSize size ) noexcept override {
		++resizeCount;
		status.surface = size;
		status.surfaceDirty = true;
		return BackendResult::Success();
	}

	BackendResult ExecuteScript( const ScriptRequest &request ) noexcept override {
		lastScript.assign( request.source.data(), request.source.size() );
		lastFrame.assign( request.frame.data(), request.frame.size() );
		return BackendResult::Success();
	}

	IntegerScriptResult EvaluateInteger( const ScriptRequest &request ) noexcept override {
		lastScript.assign( request.source.data(), request.source.size() );
		lastFrame.assign( request.frame.data(), request.frame.size() );
		return { BackendResult::Success(), 42 };
	}

	StringScriptResult EvaluateString( const ScriptRequest &request,
		char *buffer, std::size_t capacity ) noexcept override {
		++stringCount;
		lastScript.assign( request.source );
		lastFrame.assign( request.frame );
		const std::size_t bytes = ( std::min )( stringValue.size(), capacity - 1 );
		std::memcpy( buffer, stringValue.data(), bytes );
		buffer[bytes] = '\0';
		return { failString ? BackendResult::Failure( BackendError::OperationFailed,
			"partial fake string result" ) : BackendResult::Success(),
			invalidStringLength ? capacity : bytes };
	}

	BackendResult CopySurface( const MutableSurface &surface ) noexcept override {
		++copyCount;
		for ( int row = 0; row < surface.size.height; ++row ) {
			std::uint8_t *destination = surface.pixels
				+ static_cast<std::size_t>( row ) * surface.rowStride;
			std::fill( destination, destination + surface.size.width * 4,
				static_cast<std::uint8_t>( 0x30 + row ) );
		}
		status.surfaceDirty = false;
		return BackendResult::Success();
	}

	BackendResult SetZoom( int value ) noexcept override {
		zoom = value;
		return BackendResult::Success();
	}

	BackendResult SetRenderingPaused( bool paused ) noexcept override {
		status.renderingPaused = paused;
		return BackendResult::Success();
	}

	BackendResult SetFocus( bool focused ) noexcept override {
		status.focused = focused;
		return BackendResult::Success();
	}

	BackendResult InjectMouseMove( const MouseMoveEvent &event ) noexcept override {
		++mouseMoveCount;
		lastMouseMove = event;
		return BackendResult::Success();
	}

	BackendResult InjectMouseButton( const MouseButtonEvent &event ) noexcept override {
		++mouseButtonCount;
		lastMouseButton = event;
		return BackendResult::Success();
	}

	BackendResult InjectMouseWheel( const MouseWheelEvent &event ) noexcept override {
		++mouseWheelCount;
		lastMouseWheel = event;
		return BackendResult::Success();
	}

	BackendResult InjectKeyboard( const KeyboardEvent &event ) noexcept override {
		++keyboardCount;
		lastKeyboard = event;
		return BackendResult::Success();
	}

	BackendResult StopLoading() noexcept override {
		++stopCount;
		status.loading = false;
		return BackendResult::Success();
	}

	BackendResult ClearCache() noexcept override {
		++clearCacheCount;
		return BackendResult::Success();
	}

	BackendResult Reload( bool discardCache ) noexcept override {
		++reloadCount;
		ignoreCache = discardCache;
		status.loading = true;
		return BackendResult::Success();
	}
};

StartupParameters MakeStartup( SurfaceSize size = { 2, 2 } ) {
	StartupParameters parameters;
	parameters.runtimePath = "C:/retail/runtime";
	parameters.basePath = "C:/retail";
	parameters.playerName = "Ranger";
	parameters.appId = 282440;
	parameters.identityLow = 7;
	parameters.identityHigh = 8;
	parameters.initialSurface = size;
	parameters.initialConfigJson = "{\"config\":true}";
	parameters.initialMapJson = "[]";
	parameters.initialFactoryJson = "[]";
	return parameters;
}

bool NullBackendIsDeterministicAndDefaultOff() {
	BackendHost host;
	CHECK( !host.IsAvailable() );
	CHECK( !host.IsRunning() );
	CHECK( host.GetLifecycle() == Lifecycle::Dormant );
	CHECK( std::string( host.ProviderName() ) == "Unavailable" );

	const BackendResult start = host.Start( MakeStartup() );
	CHECK( start.code == BackendError::Unavailable );
	CHECK( start.detail == "Awesomium runtime backend is unavailable in this build." );
	CHECK( host.LastResult().detail == start.detail );
	CHECK( host.GetLifecycle() == Lifecycle::Dormant );
	CHECK( host.Status().viewAlive == false );
	CHECK( host.Navigate( "asset://ql/index.html" ).code == BackendError::InvalidState );
	return true;
}

bool RejectsUnavailableAndVersionMismatchedAdapters() {
	BackendHost host;
	FakeBackend fake;
	fake.descriptor.available = false;
	CHECK( host.InstallBackend( fake ).code == BackendError::Unavailable );
	CHECK( !host.IsAvailable() );

	fake.descriptor.available = true;
	fake.descriptor.interfaceVersion = fnql::webui::kBackendInterfaceVersion + 1;
	CHECK( host.InstallBackend( fake ).code == BackendError::InterfaceMismatch );
	CHECK( !host.IsAvailable() );
	return true;
}

bool EnforcesLifecycleAndSupportsExplicitRecovery() {
	FakeBackend fake;
	FakeBackend replacement;
	BackendHost host;
	CHECK( host.InstallBackend( fake ) );
	CHECK( host.IsAvailable() );
	CHECK( std::string( host.ProviderName() ) == "Deterministic fake browser" );

	StartupParameters invalid = MakeStartup( { 0, 720 } );
	CHECK( host.Start( invalid ).code == BackendError::InvalidArgument );
	CHECK( fake.startCount == 0 );

	CHECK( host.Start( MakeStartup() ) );
	CHECK( host.GetLifecycle() == Lifecycle::Running );
	CHECK( fake.startCount == 1 );
	CHECK( host.Status().viewAlive );
	CHECK( host.InstallBackend( replacement ).code == BackendError::InvalidState );

	CHECK( host.Start( MakeStartup( { 4, 3 } ) ) );
	CHECK( fake.startCount == 1 );
	CHECK( fake.resizeCount == 1 );
	CHECK( host.Status().surface == ( SurfaceSize{ 4, 3 } ) );
	CHECK( host.Pump() );
	CHECK( fake.pumpCount == 1 );

	fake.status.crashed = true;
	CHECK( host.Pump().code == BackendError::Crashed );
	CHECK( host.GetLifecycle() == Lifecycle::Failed );
	fake.status.crashed = false;
	CHECK( host.Start( MakeStartup() ) );
	CHECK( host.GetLifecycle() == Lifecycle::Running );
	CHECK( fake.startCount == 2 );

	host.Shutdown();
	CHECK( host.GetLifecycle() == Lifecycle::Dormant );
	const int shutdownCount = fake.shutdownCount;
	host.Shutdown();
	CHECK( fake.shutdownCount == shutdownCount );
	host.RemoveBackend();
	CHECK( !host.IsAvailable() );
	return true;
}

bool CleansUpPartialStartupFailures() {
	FakeBackend fake;
	BackendHost host;
	CHECK( host.InstallBackend( fake ) );
	fake.failStart = true;
	fake.startFailureDetail.assign( 1000, 'x' );
	fake.clearFailureDetailOnShutdown = true;
	const BackendResult failed = host.Start( MakeStartup() );
	CHECK( failed.code == BackendError::OperationFailed );
	CHECK( failed.detail.size() == fnql::webui::kBackendDiagnosticCapacity - 1 );
	CHECK( std::all_of( failed.detail.begin(), failed.detail.end(),
		[]( char value ) { return value == 'x'; } ) );
	CHECK( fake.startFailureDetail.empty() );
	CHECK( host.GetLifecycle() == Lifecycle::Failed );
	CHECK( fake.shutdownCount == 1 );

	fake.failStart = false;
	fake.clearFailureDetailOnShutdown = false;
	CHECK( host.Start( MakeStartup() ) );
	CHECK( fake.startCount == 2 );
	CHECK( fake.shutdownCount == 2 );
	return true;
}

bool ProvidesBalancedHostResourceServices() {
	FakeBackend fake;
	BackendHost host;
	ResourceProbe resources;
	StartupParameters startup = MakeStartup();
	startup.hostServices.context = &resources;
	startup.hostServices.requestResource = RequestProbeResource;
	startup.hostServices.releaseResource = ReleaseProbeResource;
	fake.requestResourceOnStart = true;
	fake.descriptor.capabilities = fake.descriptor.capabilities
		| Capability::ResourceRequests;

	CHECK( host.InstallBackend( fake ) );
	CHECK( host.Start( startup ) );
	CHECK( fake.startupResourceRead );
	CHECK( fake.startupResourceFirstByte == 0x51 );
	CHECK( resources.requestedPath == "asset://ql/index.html" );
	CHECK( resources.requestCount == 1 );
	CHECK( resources.releaseCount == 1 );
	return true;
}

bool ValidatesAndCopiesSoftwareSurfaces() {
	FakeBackend fake;
	BackendHost host;
	CHECK( host.InstallBackend( fake ) );
	CHECK( host.Start( MakeStartup() ) );

	std::vector<std::uint8_t> pixels( 24, 0xcc );
	MutableSurface invalidStride{ pixels.data(), pixels.size(), 7, { 2, 2 } };
	CHECK( host.CopySurface( invalidStride ).code == BackendError::InvalidArgument );
	CHECK( fake.copyCount == 0 );

	MutableSurface wrongSize{ pixels.data(), pixels.size(), 12, { 3, 2 } };
	CHECK( host.CopySurface( wrongSize ).code == BackendError::SurfaceMismatch );
	CHECK( fake.copyCount == 0 );

	MutableSurface valid{ pixels.data(), pixels.size(), 12, { 2, 2 } };
	CHECK( host.CopySurface( valid ) );
	CHECK( fake.copyCount == 1 );
	CHECK( pixels[0] == 0x30 );
	CHECK( pixels[7] == 0x30 );
	CHECK( pixels[8] == 0xcc );
	CHECK( pixels[12] == 0x31 );
	CHECK( pixels[19] == 0x31 );
	CHECK( pixels[20] == 0xcc );
	CHECK( !host.Status().surfaceDirty );
	return true;
}

bool ConstrainsBrowserSurfacesWithoutChangingAspectRatio() {
	CHECK( ( SurfaceSize{ 1280, 720 }.ConstrainedTo( 2048 )
		== SurfaceSize{ 1280, 720 } ) );
	CHECK( ( SurfaceSize{ 2560, 1440 }.ConstrainedTo( 2048 )
		== SurfaceSize{ 2048, 1152 } ) );
	CHECK( ( SurfaceSize{ 1440, 2560 }.ConstrainedTo( 2048 )
		== SurfaceSize{ 1152, 2048 } ) );
	CHECK( ( SurfaceSize{ 1, 4096 }.ConstrainedTo( 2048 )
		== SurfaceSize{ 1, 2048 } ) );
	CHECK( ( SurfaceSize{ 2560, 1440 }.ConstrainedTo( 0 )
		== SurfaceSize{ 2560, 1440 } ) );
	return true;
}

bool ForwardsTypedNavigationScriptAndInput() {
	FakeBackend fake;
	BackendHost host;
	CHECK( host.InstallBackend( fake ) );
	CHECK( host.Start( MakeStartup() ) );

	CHECK( host.Navigate( {} ).code == BackendError::InvalidArgument );
	CHECK( host.Navigate( "https://example.invalid/" ).code == BackendError::InvalidArgument );
	CHECK( host.Navigate( "asset://ql/index.html#home" ) );
	CHECK( fake.lastUrl == "asset://ql/index.html#home" );
	CHECK( host.ExecuteScript( { {}, {} } ).code == BackendError::InvalidArgument );
	CHECK( host.ExecuteScript( { "window.main_hook_v2()", "main" } ) );
	CHECK( fake.lastScript == "window.main_hook_v2()" );
	CHECK( fake.lastFrame == "main" );

	const IntegerScriptResult integer = host.EvaluateInteger( { "6*7", "" } );
	CHECK( integer.result );
	CHECK( integer.value == 42 );
	CHECK( host.SetZoom( 0 ).code == BackendError::InvalidArgument );
	CHECK( host.SetZoom( 125 ) );
	CHECK( fake.zoom == 125 );
	CHECK( host.SetRenderingPaused( true ) );
	CHECK( host.Status().renderingPaused );
	CHECK( host.SetFocus( false ) );
	CHECK( !host.Status().focused );

	CHECK( host.InjectMouseMove( { 17, 29 } ) );
	CHECK( fake.mouseMoveCount == 1 );
	CHECK( fake.lastMouseMove.x == 17 && fake.lastMouseMove.y == 29 );
	CHECK( host.InjectMouseButton( { fnql::webui::MouseButton::Right,
		ButtonAction::Release } ) );
	CHECK( fake.mouseButtonCount == 1 );
	CHECK( fake.lastMouseButton.button == fnql::webui::MouseButton::Right );
	CHECK( fake.lastMouseButton.action == ButtonAction::Release );
	CHECK( host.InjectMouseWheel( {} ).code == BackendError::InvalidArgument );
	CHECK( host.InjectMouseWheel( { 0, -2 } ) );
	CHECK( fake.mouseWheelCount == 1 );
	CHECK( fake.lastMouseWheel.verticalTicks == -2 );
	CHECK( host.InjectKeyboard( { fnql::webui::KeyboardEventType::Character,
		'A', 0x1e0001 } ) );
	CHECK( fake.keyboardCount == 1 );
	CHECK( fake.lastKeyboard.virtualKey == 'A' );
	CHECK( host.InjectKeyboard( { fnql::webui::KeyboardEventType::KeyUp,
		0x08, 0 } ) );
	CHECK( fake.keyboardCount == 2 );
	CHECK( fake.lastKeyboard.type == fnql::webui::KeyboardEventType::KeyUp );
	CHECK( fake.lastKeyboard.virtualKey == 0x08 );
	CHECK( fake.lastKeyboard.nativeKey == 0 );
	CHECK( host.InjectKeyboard( {} ).code == BackendError::InvalidArgument );
	CHECK( fake.keyboardCount == 2 );

	CHECK( host.StopLoading() );
	CHECK( fake.stopCount == 1 );
	CHECK( host.ClearCache() );
	CHECK( fake.clearCacheCount == 1 );
	CHECK( host.Reload( true ) );
	CHECK( fake.reloadCount == 1 && fake.ignoreCache );
	return true;
}

bool BoundsStringScriptResultsAndClearsFailures() {
	FakeBackend fake;
	BackendHost host;
	std::array<char, 32> buffer{};
	buffer[0] = 'x';
	CHECK( !host.EvaluateString( { "1", "" }, buffer.data(), buffer.size() ).result );
	CHECK( buffer[0] == '\0' );
	CHECK( host.InstallBackend( fake ) );
	CHECK( host.Start( MakeStartup() ) );
	CHECK( host.EvaluateString( { "1", "" }, buffer.data(), buffer.size() ).result.code
		== BackendError::Unsupported );
	CHECK( fake.stringCount == 0 );
	fake.descriptor.capabilities = fake.descriptor.capabilities | Capability::StringScriptResult;
	CHECK( host.EvaluateString( { "", "" }, buffer.data(), buffer.size() ).result.code
		== BackendError::InvalidArgument );
	CHECK( !host.EvaluateString( { "1", "" }, nullptr, 32 ).result );
	CHECK( !host.EvaluateString( { "1", "" }, buffer.data(), 0 ).result );
	CHECK( fake.stringCount == 0 );
	const auto result = host.EvaluateString( { "'result'", "main" }, buffer.data(), buffer.size() );
	CHECK( result.result && result.length == 6 );
	CHECK( std::string_view( buffer.data(), result.length ) == "result" );
	CHECK( fake.stringCount == 1 && fake.lastScript == "'result'" && fake.lastFrame == "main" );
	fake.failString = true;
	const auto failure = host.EvaluateString( { "1", "" }, buffer.data(), buffer.size() );
	CHECK( !failure.result && failure.length == 0 && buffer[0] == '\0' );
	fake.failString = false;
	fake.invalidStringLength = true;
	CHECK( !host.EvaluateString( { "1", "" }, buffer.data(), buffer.size() ).result );
	CHECK( buffer[0] == '\0' );
	fake.invalidStringLength = false;
	fake.stringValue.assign( "part\0suffix", 11 );
	CHECK( !host.EvaluateString( { "1", "" }, buffer.data(), buffer.size() ).result );
	CHECK( buffer[0] == '\0' );
	return true;
}

bool DecodesCompleteNativeRequestsWithoutTruncation() {
	using fnql::webui::DecodeNativeRequest;
	std::array<char, fnql::webui::kNativeRequestBytes> buffer{};
	auto decode = [&buffer]( std::string_view value ) {
		return DecodeNativeRequest( value, buffer.data(), buffer.size() );
	};
	auto result = decode( "e" );
	CHECK( result.result && !result.hasRequest && buffer[0] == '\0' );
	result = decode( "r!" );
	CHECK( result.result && result.hasRequest && buffer[0] == '\0' );
	result = decode( "r006100e96c34d83dde00!" );
	CHECK( result.result && result.hasRequest );
	CHECK( std::string( buffer.data() ) == "a\xc3\xa9\xe6\xb0\xb4\xf0\x9f\x98\x80" );
	result = decode( "r007f008007ff0800ffffd800dc00dbffdfff!" );
	CHECK( result.result && result.hasRequest );
	CHECK( std::string( buffer.data() ) == "\x7f\xc2\x80\xdf\xbf\xe0\xa0\x80"
		"\xef\xbf\xbf\xf0\x90\x80\x80\xf4\x8f\xbf\xbf" );
	for ( const auto malformed : { "o", "", "r", "r0061", "r0061!x", "r061!",
		"r006100000062!", "r0061d800!", "rdc00!", "rd8000061!", "rzzzz!" } ) {
		buffer[0] = 'x';
		result = decode( malformed );
		CHECK( !result.result && !result.hasRequest && buffer[0] == '\0' );
	}
	std::string maximum = "r";
	for ( std::size_t i = 0; i < buffer.size() - 1; ++i ) {
		maximum += "0061";
	}
	maximum += '!';
	result = decode( maximum );
	CHECK( result.result && result.hasRequest && std::strlen( buffer.data() ) == buffer.size() - 1 );
	CHECK( buffer.back() == '\0' );
	maximum.insert( 1, "0061" );
	CHECK( !decode( maximum ).result && buffer[0] == '\0' );
	std::array<char, 5> exact{};
	CHECK( DecodeNativeRequest( "rd83dde00!", exact.data(), exact.size() ).result );
	CHECK( std::strlen( exact.data() ) == 4 );
	CHECK( !DecodeNativeRequest( "rd83dde00!", exact.data(), 4 ).result );
	CHECK( exact[0] == '\0' );
	CHECK( !DecodeNativeRequest( "r0061!", nullptr, 8 ).result );
	CHECK( !DecodeNativeRequest( "r0061!", exact.data(), 0 ).result );
	return true;
}

bool RestrictsPrivilegedNavigationToRetailAssetOrigin() {
	CHECK( fnql::webui::IsTrustedNavigationUrl( "asset://ql/index.html" ) );
	CHECK( fnql::webui::IsTrustedNavigationUrl( "asset://ql/index.html#home" ) );
	CHECK( !fnql::webui::IsTrustedNavigationUrl( "https://example.invalid/" ) );
	CHECK( !fnql::webui::IsTrustedNavigationUrl( "asset://steam/avatar" ) );
	CHECK( !fnql::webui::IsTrustedNavigationUrl( "asset://ql-malicious/index.html" ) );

	constexpr char embeddedNull[] = "asset://ql/index.html\0suffix";
	CHECK( !fnql::webui::IsTrustedNavigationUrl(
		std::string_view( embeddedNull, sizeof( embeddedNull ) - 1 ) ) );
	return true;
}

} // namespace

int main() {
	const bool passed = NullBackendIsDeterministicAndDefaultOff()
		&& RejectsUnavailableAndVersionMismatchedAdapters()
		&& EnforcesLifecycleAndSupportsExplicitRecovery()
		&& CleansUpPartialStartupFailures()
		&& ProvidesBalancedHostResourceServices()
		&& ValidatesAndCopiesSoftwareSurfaces()
		&& ConstrainsBrowserSurfacesWithoutChangingAspectRatio()
		&& ForwardsTypedNavigationScriptAndInput()
		&& BoundsStringScriptResultsAndClearsFailures()
		&& DecodesCompleteNativeRequestsWithoutTruncation()
		&& RestrictsPrivilegedNavigationToRetailAssetOrigin();
	return passed ? 0 : 1;
}
