#include "webui_resource_queue.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using fnql::webui::ResourceRetryQueue;
using Batch = ResourceRetryQueue::Batch;
using TimePoint = ResourceRetryQueue::TimePoint;

#define CHECK( expression ) \
	do { \
		if ( !( expression ) ) { \
			std::cerr << __func__ << ':' << __LINE__ \
				<< ": check failed: " #expression "\n"; \
			return false; \
		} \
	} while ( false )

TimePoint At( int milliseconds ) {
	return TimePoint{} + std::chrono::milliseconds( milliseconds );
}

// The production queue owns 64 URL buffers; allocate test fixtures away from
// the comparatively small Windows x86 stack used by the standalone runner.
struct Fixture {
	std::unique_ptr<ResourceRetryQueue> owned = std::make_unique<ResourceRetryQueue>();
	ResourceRetryQueue &queue = *owned;
	Batch batch;
};

bool Contains( const Batch &batch, int id ) {
	return std::find( batch.requestIds.begin(), batch.requestIds.begin() + batch.count, id )
		!= batch.requestIds.begin() + batch.count;
}

bool IdleDoesNoWork() {
	Fixture f;
	CHECK( f.queue.Empty() && f.queue.Size() == 0 );
	for ( int time = 0; time <= 10000; ++time ) {
		CHECK( !f.queue.NextDue( At( time ), f.batch ) );
		CHECK( f.batch.count == 0 && f.batch.Path().empty() );
	}
	f.queue.Wake( {}, At( 10001 ) );
	CHECK( !f.queue.NextDue( At( 10001 ), f.batch ) );
	return true;
}

std::vector<int> AttemptTimes( int frameMilliseconds ) {
	Fixture f;
	std::vector<int> times;
	f.queue.Enqueue( 1, "asset://steam/avatar/1/large", At( 0 ) );
	for ( int now = 0; now <= 10000; now += frameMilliseconds ) {
		if ( f.queue.NextDue( At( now ), f.batch ) ) {
			times.push_back( now );
		}
	}
	return times;
}

bool RetriesUseElapsedTimeAndRemainBounded() {
	const std::vector<int> expected{ 0, 250, 750, 1750, 3750, 5750, 7750, 9750 };
	CHECK( AttemptTimes( 2 ) == expected ); // 500 FPS
	CHECK( AttemptTimes( 1 ) == expected ); // 1000 FPS
	Fixture f;
	CHECK( f.queue.Enqueue( 7, "avatar/7", At( 0 ) ) );
	for ( const int time : { 0, 250, 750, 1750, 3750 } ) {
		CHECK( f.queue.NextDue( At( time ), f.batch ) );
	}
	// A long suspension produces one retry, without replaying missed attempts.
	CHECK( f.queue.NextDue( At( 600000 ), f.batch ) );
	CHECK( !f.queue.NextDue( At( 600000 ), f.batch ) );
	CHECK( !f.queue.NextDue( At( 601999 ), f.batch ) );
	CHECK( f.queue.NextDue( At( 602000 ), f.batch ) );
	for ( int time = 604000; time <= 800000; time += 2000 ) {
		CHECK( f.queue.NextDue( At( time ), f.batch ) );
		CHECK( f.batch.count == 1 && f.batch.requestIds[0] == 7 );
	}
	CHECK( f.queue.Size() == 1 ); // No permanent-failure classification exists.
	return true;
}

bool CoalescesOwnedPathsWithoutResettingBackoff() {
	Fixture f;
	std::string source = "asset://steam/avatar/42/large?version=1";
	CHECK( f.queue.Enqueue( 1, source, At( 0 ) ) );
	source.assign( "changed after enqueue" );
	CHECK( f.queue.NextDue( At( 0 ), f.batch ) );
	CHECK( f.batch.Path() == "asset://steam/avatar/42/large?version=1" );
	CHECK( f.queue.Enqueue( 2, "ASSET://STEAM/AVATAR/42/LARGE#other", At( 100 ) ) );
	CHECK( !f.queue.NextDue( At( 249 ), f.batch ) );
	CHECK( f.queue.NextDue( At( 250 ), f.batch ) );
	CHECK( f.batch.count == 2 && Contains( f.batch, 1 ) && Contains( f.batch, 2 ) );
	CHECK( f.queue.Enqueue( 3, "asset://steam/avatar/42/large?another#fragment", At( 300 ) ) );
	// Replacing an existing ID also retains the common path's retry deadline.
	CHECK( f.queue.Enqueue( 2, "asset://steam/avatar/42/large", At( 400 ) ) );
	CHECK( !f.queue.NextDue( At( 749 ), f.batch ) );
	CHECK( f.queue.NextDue( At( 750 ), f.batch ) );
	CHECK( f.batch.count == 3 );
	f.queue.Complete( f.batch );
	CHECK( f.batch.count == 3 && f.queue.Empty() );
	CHECK( !f.queue.NextDue( At( 100000 ), f.batch ) );
	return true;
}

bool EventsWakeOnlyMatchingPathsAndCanWakeAll() {
	Fixture f;
	CHECK( f.queue.Enqueue( 1, "avatar/42/large?v=1", At( 0 ) ) );
	CHECK( f.queue.Enqueue( 2, "avatar/420/large", At( 0 ) ) );
	CHECK( f.queue.NextDue( At( 0 ), f.batch ) );
	CHECK( f.queue.NextDue( At( 0 ), f.batch ) );
	f.queue.Wake( "AVATAR/42/LARGE#fresh", At( 100 ) );
	CHECK( f.queue.NextDue( At( 100 ), f.batch ) );
	CHECK( f.batch.count == 1 && f.batch.requestIds[0] == 1 );
	CHECK( !f.queue.NextDue( At( 100 ), f.batch ) );
	f.queue.Wake( "avatar/4", At( 120 ) );
	CHECK( !f.queue.NextDue( At( 120 ), f.batch ) );
	CHECK( f.queue.NextDue( At( 250 ), f.batch ) );
	CHECK( f.batch.requestIds[0] == 2 );
	// The event restarted path 1 at its initial 250ms delay.
	CHECK( !f.queue.NextDue( At( 349 ), f.batch ) );
	CHECK( f.queue.NextDue( At( 350 ), f.batch ) );
	CHECK( f.batch.requestIds[0] == 1 );
	f.queue.Wake( {}, At( 400 ) );
	CHECK( f.queue.NextDue( At( 400 ), f.batch ) );
	const int first = f.batch.requestIds[0];
	CHECK( f.queue.NextDue( At( 400 ), f.batch ) );
	CHECK( f.batch.requestIds[0] != first );
	CHECK( !f.queue.NextDue( At( 400 ), f.batch ) );
	return true;
}

bool ProviderCallbacksCannotOverwriteWakeOrCompleteReusedIds() {
	Fixture f;
	CHECK( f.queue.Enqueue( 1, "avatar/1", At( 0 ) ) );
	CHECK( f.queue.Enqueue( 2, "avatar/1", At( 0 ) ) );
	CHECK( f.queue.NextDue( At( 0 ), f.batch ) );
	Batch fetched = f.batch;
	// Simulate a ready event during a failed provider attempt. NextDue has
	// already recorded its delay, so the event remains immediately actionable.
	f.queue.Wake( "avatar/1", At( 1 ) );
	CHECK( f.queue.NextDue( At( 1 ), f.batch ) );
	CHECK( f.queue.Enqueue( 1, "AVATAR/1?new", At( 2 ) ) );
	f.queue.Complete( fetched );
	CHECK( fetched.count == 1 && fetched.requestIds[0] == 2 );
	CHECK( f.queue.Size() == 1 );
	// Completing the old snapshot again cannot complete any other request.
	f.queue.Complete( fetched );
	CHECK( fetched.count == 0 );
	CHECK( f.queue.Enqueue( 1, "avatar/another", At( 3 ) ) );
	f.queue.Complete( f.batch );
	CHECK( f.batch.count == 0 && f.queue.Size() == 1 );
	CHECK( f.queue.NextDue( At( 3 ), f.batch ) );
	CHECK( f.batch.Path() == "avatar/another" );
	f.queue.Complete( f.batch );
	CHECK( f.batch.count == 1 && f.queue.Empty() );
	// Completion removes ownership before a SendResponse callback can enqueue.
	CHECK( f.queue.Enqueue( 1, "avatar/another", At( 3 ) ) );
	CHECK( f.queue.NextDue( At( 3 ), f.batch ) );
	return true;
}

bool RoundRobinPreservesFairnessWithinFourBatchBudget() {
	Fixture f;
	for ( int id = 0; id < static_cast<int>( ResourceRetryQueue::kCapacity ); ++id ) {
		CHECK( f.queue.Enqueue( id, "avatar/" + std::to_string( id ), At( 0 ) ) );
	}
	std::array<int, ResourceRetryQueue::kCapacity> visits{};
	for ( int frame = 0; frame < 16; ++frame ) {
		// Even when callbacks wake every request, cursor rotation prevents the
		// first four slots from starving the other sixty requests.
		for ( int budget = 0; budget < 4; ++budget ) {
			CHECK( f.queue.NextDue( At( frame ), f.batch ) );
			CHECK( f.batch.count == 1 );
			const int id = f.batch.requestIds[0];
			CHECK( id >= 0 && id < static_cast<int>( visits.size() ) );
			++visits[static_cast<std::size_t>( id )];
			f.queue.Wake( {}, At( frame ) );
		}
	}
	for ( const int count : visits ) {
		CHECK( count == 1 );
	}
	return true;
}

bool EnforcesCapacityAndPathBoundsWithoutDiscardingRequests() {
	Fixture f;
	CHECK( !f.queue.Enqueue( 1, {}, At( 0 ) ) );
	CHECK( !f.queue.Enqueue( 1, "?query", At( 0 ) ) );
	CHECK( !f.queue.Enqueue( 1, "#fragment", At( 0 ) ) );
	CHECK( !f.queue.Enqueue( 1, std::string( "path\0suffix", 11 ), At( 0 ) ) );
	CHECK( !f.queue.Enqueue( 1, "path\n", At( 0 ) ) );
	CHECK( !f.queue.Enqueue( 1, std::string( ResourceRetryQueue::kPathCapacity, 'x' ), At( 0 ) ) );
	CHECK( f.queue.Empty() );
	const std::string maximum( ResourceRetryQueue::kPathCapacity - 1, 'x' );
	CHECK( f.queue.Enqueue( 1, maximum, At( 0 ) ) );
	CHECK( f.queue.NextDue( At( 0 ), f.batch ) );
	CHECK( f.batch.Path() == maximum && f.batch.path.back() == '\0' );
	f.queue.Clear();
	for ( int id = 0; id < static_cast<int>( ResourceRetryQueue::kCapacity ); ++id ) {
		CHECK( f.queue.Enqueue( id, "same/resource", At( 0 ) ) );
	}
	CHECK( !f.queue.Enqueue( 64, "same/resource", At( 0 ) ) );
	CHECK( f.queue.Size() == ResourceRetryQueue::kCapacity );
	CHECK( f.queue.Enqueue( 4, "SAME/RESOURCE?new", At( 0 ) ) );
	CHECK( !f.queue.Enqueue( 4, {}, At( 0 ) ) );
	CHECK( f.queue.NextDue( At( 0 ), f.batch ) );
	CHECK( f.batch.count == ResourceRetryQueue::kCapacity );
	f.queue.Complete( f.batch );
	CHECK( f.queue.Empty() && f.batch.count == ResourceRetryQueue::kCapacity );
	return true;
}

bool ClearAndTakeAllResetDeadlinesWithoutReusingOldGenerations() {
	Fixture f;
	CHECK( f.queue.Enqueue( 3, "resource", At( 0 ) ) );
	CHECK( f.queue.NextDue( At( 0 ), f.batch ) );
	Batch stale = f.batch;
	f.queue.Clear();
	CHECK( f.queue.Empty() );
	CHECK( f.queue.Enqueue( 3, "resource", At( 1 ) ) );
	f.queue.Complete( stale );
	CHECK( stale.count == 0 && f.queue.Size() == 1 );
	CHECK( f.queue.NextDue( At( 1 ), f.batch ) );
	CHECK( !f.queue.NextDue( At( 250 ), stale ) );
	CHECK( f.queue.NextDue( At( 251 ), stale ) );
	CHECK( f.queue.Enqueue( 9, "other", At( 252 ) ) );
	std::array<int, ResourceRetryQueue::kCapacity> ids{};
	CHECK( f.queue.TakeAll( ids ) == 2 );
	CHECK( ids[0] == 3 && ids[1] == 9 && f.queue.Empty() );
	CHECK( f.queue.TakeAll( ids ) == 0 );
	CHECK( f.queue.Enqueue( 3, "resource", At( 253 ) ) );
	f.queue.Complete( f.batch );
	CHECK( f.batch.count == 0 && f.queue.Size() == 1 );
	CHECK( f.queue.NextDue( At( 253 ), f.batch ) );
	CHECK( f.batch.requestIds[0] == 3 );
	return true;
}

} // namespace

int main() {
	const bool passed = IdleDoesNoWork()
		&& RetriesUseElapsedTimeAndRemainBounded()
		&& CoalescesOwnedPathsWithoutResettingBackoff()
		&& EventsWakeOnlyMatchingPathsAndCanWakeAll()
		&& ProviderCallbacksCannotOverwriteWakeOrCompleteReusedIds()
		&& RoundRobinPreservesFairnessWithinFourBatchBudget()
		&& EnforcesCapacityAndPathBoundsWithoutDiscardingRequests()
		&& ClearAndTakeAllResetDeadlinesWithoutReusingOldGenerations();
	return passed ? 0 : 1;
}
