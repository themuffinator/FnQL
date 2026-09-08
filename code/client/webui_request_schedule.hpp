/*
Copyright (C) 2026 FnQL contributors

This file is part of FnQL, licensed under the GNU General Public License,
version 2 or (at your option) any later version. See LICENSE for details.
*/
#ifndef FNQL_CLIENT_WEBUI_REQUEST_SCHEDULE_HPP
#define FNQL_CLIENT_WEBUI_REQUEST_SCHEDULE_HPP

#include <cstdint>

namespace fnql::webui {

// Notification-capable views incur no synchronous empty-queue polling after
// their first wake proves the transport works. Keep a wall-clock fallback for
// other backends and for documents whose notification transport has not started.
class RequestSchedule {
public:
	bool ShouldPoll( std::int64_t now, bool notificationsReady,
		bool notified ) const noexcept {
		if ( notified || remaining_ ) {
			return true;
		}
		if ( notificationsReady ) {
			return false;
		}
		return !polled_ || now < lastPoll_ || now - lastPoll_ >= kIdleMicroseconds;
	}

	void DidPoll( std::int64_t now, bool budgetExhausted ) noexcept {
		lastPoll_ = now;
		polled_ = true;
		remaining_ = budgetExhausted;
	}

	void Reset() noexcept {
		*this = {};
	}

private:
	static constexpr std::int64_t kIdleMicroseconds = 60000;
	std::int64_t lastPoll_ = 0;
	bool polled_ = false;
	bool remaining_ = false;
};

} // namespace fnql::webui

#endif
