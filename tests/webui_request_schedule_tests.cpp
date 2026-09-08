#include "webui_request_schedule.hpp"

#include <cstdint>
#include <iostream>

using fnql::webui::RequestSchedule;

#define CHECK(condition) do { if (!(condition)) { \
	std::cerr << "line " << __LINE__ << ": " #condition "\n"; return 1; \
} } while (false)

int main() {
	// Idle gameplay/menu frames must not become synchronous browser calls at
	// any game frame rate once the asynchronous transport has been observed.
	for (const int fps : {60, 125, 250, 1000}) {
		RequestSchedule schedule;
		for (int frame = 0; frame < fps * 30; ++frame) {
			CHECK(!schedule.ShouldPoll(std::int64_t(frame) * 1000000 / fps, true, false));
		}
	}

	// A hidden lobby command or preload quit wakes the very next frame, even
	// immediately after an empty read. More than one frame's budget is drained
	// without requiring another browser notification.
	RequestSchedule schedule;
	CHECK(schedule.ShouldPoll(1000, true, true));
	schedule.DidPoll(1000, true);
	CHECK(schedule.ShouldPoll(1001, true, false));
	schedule.DidPoll(1001, true);
	CHECK(schedule.ShouldPoll(1002, true, false));
	schedule.DidPoll(1002, false);
	CHECK(!schedule.ShouldPoll(1003, true, false));
	CHECK(schedule.ShouldPoll(1003, true, true));

	// Missing notification support/handshake retains polling independently of
	// frame count. Check exact boundaries and recovery from a reset clock.
	schedule.Reset();
	CHECK(schedule.ShouldPoll(0, false, false));
	schedule.DidPoll(0, false);
	for (std::int64_t time = 1; time < 60000; ++time) {
		CHECK(!schedule.ShouldPoll(time, false, false));
	}
	CHECK(schedule.ShouldPoll(60000, false, false));
	schedule.DidPoll(60000, false);
	CHECK(schedule.ShouldPoll(100, false, false));
	CHECK(!schedule.ShouldPoll(120000, true, false));
	schedule.Reset();
	CHECK(schedule.ShouldPoll(120000, false, false));
	CHECK(!schedule.ShouldPoll(120000, true, false));
	std::cout << "WebUI request scheduling checks passed\n";
	return 0;
}
