#pragma once

#include <cstdint>

namespace fnql::client {

struct WindowResizeRequest {
	int width = 0;
	int height = 0;
	bool preserveWindow = true;
};

// Coalesces the event burst produced by an interactive resize into one
// renderer refresh. Unsigned arithmetic keeps the deadline comparison valid
// when the platform millisecond counter wraps.
class WindowResizeScheduler {
public:
	// Short enough that snap and drag completion feel immediate, while still
	// coalescing the dense event stream produced during interactive resizing.
	static constexpr std::uint32_t kDebounceMilliseconds = 100;

	bool Notify( std::uint32_t now, int width, int height, bool preserveWindow ) {
		if ( width < 4 || height < 4 ) {
			return false;
		}

		request_.width = width;
		request_.height = height;
		request_.preserveWindow = pending_
			? request_.preserveWindow && preserveWindow
			: preserveWindow;
		deadline_ = now + kDebounceMilliseconds;
		pending_ = true;
		return true;
	}

	bool ConsumeIfReady( std::uint32_t now, WindowResizeRequest *request ) {
		if ( !pending_ || !DeadlineReached( now, deadline_ ) || !request ) {
			return false;
		}

		*request = request_;
		Reset();
		return true;
	}

	void Complete( std::uint32_t now ) {
		if ( pending_ ) {
			deadline_ = now;
		}
	}

	void Reset() {
		request_ = {};
		deadline_ = 0;
		pending_ = false;
	}

	bool Pending() const {
		return pending_;
	}

private:
	static constexpr bool DeadlineReached( std::uint32_t now,
		std::uint32_t deadline ) {
		return now - deadline < 0x80000000u;
	}

	WindowResizeRequest request_{};
	std::uint32_t deadline_ = 0;
	bool pending_ = false;
};

} // namespace fnql::client
