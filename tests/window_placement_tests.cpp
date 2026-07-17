#include "../code/platform/window_placement.hpp"

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

} // namespace

int main() {
	TestDecorationsStayInsideUsableBounds();
	TestNegativeOriginMonitor();
	TestOversizedWindowKeepsTitleBarReachable();
	TestInvalidBoundsDoNotMoveWindow();
	return failures == 0 ? 0 : 1;
}
