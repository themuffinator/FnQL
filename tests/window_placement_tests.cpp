#include "../code/platform/window_placement.hpp"
#include "../code/client/canvas_geometry.hpp"
#include "../code/client/window_resize.hpp"

#include <cstdio>

namespace {

int failures;

void Check( bool condition, const char *message ) {
	if ( !condition ) {
		std::fprintf( stderr, "FAIL: %s\n", message );
		++failures;
	}
}

void TestDecorationsStayInsideUsableBounds() {
	using namespace fnql::window;
	constexpr Bounds workArea{ 0, 0, 1920, 1040 };
	constexpr Insets frame{ 8, 31, 8, 8 };

	constexpr Position topLeft = ConstrainClientOrigin(
		{ -200, -100 }, 1280, 720, workArea, frame );
	Check( topLeft.x == 8 && topLeft.y == 31,
		"client origin accounts for the left frame and title bar" );

	constexpr Position bottomRight = ConstrainClientOrigin(
		{ 1800, 900 }, 1280, 720, workArea, frame );
	Check( bottomRight.x == 632 && bottomRight.y == 312,
		"complete decorated window is constrained at right and bottom" );
}

void TestNegativeOriginMonitor() {
	using namespace fnql::window;
	constexpr Bounds leftMonitor{ -2560, -200, 2560, 1440 };
	constexpr Insets frame{ 6, 28, 6, 6 };
	constexpr Position placed = ConstrainClientOrigin(
		{ -4000, -500 }, 1600, 900, leftMonitor, frame );

	Check( placed.x == -2554 && placed.y == -172,
		"negative multi-monitor coordinates remain valid" );
}

void TestOversizedWindowKeepsTitleBarReachable() {
	using namespace fnql::window;
	constexpr Bounds workArea{ 100, 50, 800, 600 };
	constexpr Insets frame{ 5, 30, 5, 5 };
	constexpr Position placed = ConstrainClientOrigin(
		{ 500, 500 }, 1200, 900, workArea, frame );

	Check( placed.x == 105 && placed.y == 80,
		"oversized window pins its leading frame and title bar on screen" );
}

void TestInvalidBoundsDoNotMoveWindow() {
	using namespace fnql::window;
	constexpr Position desired{ -90, 45 };
	constexpr Position placed = ConstrainClientOrigin(
		desired, 640, 480, { 0, 0, 0, 0 } );
	Check( placed.x == desired.x && placed.y == desired.y,
		"failed display queries leave placement unchanged" );
}

bool Near( float actual, float expected ) {
	const float difference = actual - expected;
	return difference > -0.001f && difference < 0.001f;
}

void TestResizeSchedulerCoalescesBursts() {
	using fnql::client::WindowResizeRequest;
	using fnql::client::WindowResizeScheduler;

	WindowResizeScheduler scheduler;
	WindowResizeRequest request;
	Check( scheduler.Notify( 1000, 800, 600, true ),
		"valid resize starts a refresh deadline" );
	Check( scheduler.Notify( 1100, 1280, 720, true ),
		"later resize replaces dimensions and extends the deadline" );
	constexpr std::uint32_t finalDeadline =
		1100 + WindowResizeScheduler::kDebounceMilliseconds;
	Check( !scheduler.ConsumeIfReady( finalDeadline - 1, &request ),
		"resize burst is not consumed before its final deadline" );
	Check( scheduler.ConsumeIfReady( finalDeadline, &request ),
		"resize burst is consumed at its final deadline" );
	Check( request.width == 1280 && request.height == 720 && request.preserveWindow,
		"resize refresh uses the final dimensions and fastest safe path" );
	Check( !scheduler.Pending(), "consuming a resize clears pending state" );
}

void TestResizeSchedulerUsesSafestBackendPath() {
	using fnql::client::WindowResizeRequest;
	using fnql::client::WindowResizeScheduler;

	WindowResizeScheduler scheduler;
	WindowResizeRequest request;
	scheduler.Notify( 20, 1024, 768, true );
	scheduler.Notify( 30, 1024, 768, false );
	Check( scheduler.ConsumeIfReady(
		30 + WindowResizeScheduler::kDebounceMilliseconds, &request ) &&
		!request.preserveWindow,
		"one non-retainable event downgrades the complete resize burst" );
	Check( !scheduler.Notify( 300, 0, 0, true ) && !scheduler.Pending(),
		"minimized or invalid sizes never replace a valid renderer mode" );
}

void TestResizeSchedulerHandlesClockWrap() {
	using fnql::client::WindowResizeRequest;
	using fnql::client::WindowResizeScheduler;

	WindowResizeScheduler scheduler;
	WindowResizeRequest request;
	constexpr std::uint32_t start = 0xfffffff0u;
	constexpr std::uint32_t deadline =
		start + WindowResizeScheduler::kDebounceMilliseconds;
	scheduler.Notify( start, 1600, 900, true );
	Check( !scheduler.ConsumeIfReady( deadline - 1, &request ),
		"wrapped clock remains before the resize deadline" );
	Check( scheduler.ConsumeIfReady( deadline, &request ),
		"wrapped clock reaches the resize deadline exactly" );
}

void TestResizeSchedulerCanCompleteInteractiveResize() {
	using fnql::client::WindowResizeRequest;
	using fnql::client::WindowResizeScheduler;

	WindowResizeScheduler scheduler;
	WindowResizeRequest request;
	scheduler.Notify( 500, 1366, 768, true );
	scheduler.Complete( 510 );
	Check( scheduler.ConsumeIfReady( 510, &request ),
		"a platform resize-end event refreshes without an extra debounce delay" );
	Check( request.width == 1366 && request.height == 768,
		"completed interactive resize retains its final dimensions" );
}

void TestCanvasGeometryTracksDrawableAspect() {
	using fnql::client::CalculateCanvasGeometry;

	const auto native = CalculateCanvasGeometry( 640, 480 );
	Check( Near( native.scale, 1.0f ) && Near( native.biasX, 0.0f ) && Near( native.biasY, 0.0f ),
		"native canvas dimensions have no alignment bias" );

	const auto wide = CalculateCanvasGeometry( 1920, 1080 );
	Check( Near( wide.scale, 2.25f ) && Near( wide.biasX, 240.0f ) && Near( wide.biasY, 0.0f ),
		"wide drawable centres the 4:3 virtual canvas horizontally" );

	const auto tall = CalculateCanvasGeometry( 800, 1000 );
	Check( Near( tall.scale, 1.25f ) && Near( tall.biasX, 0.0f ) && Near( tall.biasY, 200.0f ),
		"tall drawable centres the 4:3 virtual canvas vertically" );

	const auto invalid = CalculateCanvasGeometry( 0, 0 );
	Check( Near( invalid.scale, 1.0f ) && Near( invalid.biasX, 0.0f ) && Near( invalid.biasY, 0.0f ),
		"invalid transient drawable dimensions retain safe canvas defaults" );
}

void TestLegacyMenuAspectMigratesOnlyOnce() {
	using fnql::client::ShouldMigrateMenuAspectToRetail;
	using fnql::client::kRetailMenuAspectPolicyVersion;

	Check( ShouldMigrateMenuAspectToRetail( true, 0, 0 ),
		"an archived stretch value predating retail policy is migrated" );
	Check( !ShouldMigrateMenuAspectToRetail( false, 0, 0 ),
		"a clean command-line opt-out is not mistaken for an archived default" );
	Check( !ShouldMigrateMenuAspectToRetail( true, 1, 0 ),
		"an already-correct retail value remains unchanged" );
	Check( !ShouldMigrateMenuAspectToRetail(
		true, 0, kRetailMenuAspectPolicyVersion ),
		"an explicit opt-out remains valid after the migration marker is current" );
}

} // namespace

int main() {
	TestDecorationsStayInsideUsableBounds();
	TestNegativeOriginMonitor();
	TestOversizedWindowKeepsTitleBarReachable();
	TestInvalidBoundsDoNotMoveWindow();
	TestResizeSchedulerCoalescesBursts();
	TestResizeSchedulerUsesSafestBackendPath();
	TestResizeSchedulerHandlesClockWrap();
	TestResizeSchedulerCanCompleteInteractiveResize();
	TestCanvasGeometryTracksDrawableAspect();
	TestLegacyMenuAspectMigratesOnlyOnce();
	return failures == 0 ? 0 : 1;
}
