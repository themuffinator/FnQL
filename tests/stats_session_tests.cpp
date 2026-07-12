#include "../code/server/stats_session.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace stats = fnql::server::stats;

namespace {

int failures = 0;

void Check( bool condition, const char *message ) {
	if ( !condition ) {
		std::fprintf( stderr, "FAIL: %s\n", message );
		++failures;
	}
}

void TestFields() {
	stats::Session session;
	Check( !session.AddField( 0, 1 ), "inactive session rejects updates" );
	session.Begin( 42 );
	Check( session.AddField( 0, 7 ), "valid field update" );
	Check( session.Field( 0 ) == 7 && session.FieldDirty( 0 ), "field retained and dirty" );
	Check( !session.AddField( -1, 1 ), "negative field rejected" );
	Check( !session.AddField( static_cast<int>( stats::FieldCount ), 1 ),
		"high field rejected" );
	Check( session.AddField( 1, ( std::numeric_limits<std::int32_t>::max )() ),
		"max field update" );
	Check( session.AddField( 1, 1 ), "overflowing update accepted with saturation" );
	Check( session.Field( 1 ) == ( std::numeric_limits<std::int32_t>::max )(),
		"positive field saturation" );
	Check( session.AddField( 2, ( std::numeric_limits<std::int32_t>::min )() ),
		"min field update" );
	Check( session.AddField( 2, -1 ), "underflowing update accepted with saturation" );
	Check( session.Field( 2 ) == ( std::numeric_limits<std::int32_t>::min )(),
		"negative field saturation" );
	Check( session.AddField( 3, 5 ), "pending delta before provider load" );
	Check( session.LoadField( 3, 100 ), "provider base loaded" );
	Check( session.Field( 3 ) == 105 && session.FieldLoaded( 3 ),
		"provider base and pending delta merge" );
	session.MarkFieldStored( 3 );
	Check( !session.FieldDirty( 3 ), "stored field becomes clean" );
}

void TestAchievementsAndIdentityReset() {
	stats::Session session;
	session.Begin( 10 );
	Check( session.UnlockAchievement( 58 ), "last achievement unlocks" );
	Check( session.HasAchievement( 58 ) && session.AchievementDirty( 58 ),
		"achievement retained and dirty" );
	Check( !session.UnlockAchievement( 59 ), "invalid achievement rejected" );
	Check( session.LoadAchievement( 57, true ) && session.HasAchievement( 57 ),
		"provider achievement load" );
	Check( session.AchievementLoaded( 57 ), "provider achievement marked loaded" );
	session.MarkAchievementStored( 58 );
	Check( !session.AchievementDirty( 58 ), "stored achievement becomes clean" );
	session.MarkRequestIssued();
	session.Begin( 10 );
	Check( session.HasAchievement( 58 ) && session.RequestIssued(),
		"same identity preserves session" );
	session.Begin( 11 );
	Check( !session.HasAchievement( 58 ) && !session.RequestIssued() &&
		session.Identity() == 11, "identity change clears state" );
	session.Reset();
	Check( !session.Active() && session.Identity() == 0, "explicit reset clears identity" );
}

void TestAsyncStoreCompletion() {
	stats::Session session;
	session.Begin( 77 );
	Check( session.AddField( 4, 2 ), "async field update" );
	Check( session.UnlockAchievement( 5 ), "async achievement update" );
	std::array<bool, stats::FieldCount> fields{};
	std::array<bool, stats::AchievementCount> achievements{};
	fields[4] = true;
	achievements[5] = true;
	session.BeginStorePending( fields, achievements );
	Check( session.StorePending(), "pending store retained" );
	Check( session.AddField( 4, 1 ), "field may change while store is pending" );
	session.CompletePendingStore( true );
	Check( !session.StorePending(), "successful completion clears pending state" );
	Check( session.FieldDirty( 4 ), "newer field generation remains dirty" );
	Check( !session.AchievementDirty( 5 ), "unchanged achievement generation becomes clean" );

	fields.fill( false );
	achievements.fill( false );
	fields[4] = true;
	session.BeginStorePending( fields, achievements );
	session.CompletePendingStore( false );
	Check( session.FieldDirty( 4 ) && !session.StorePending(),
		"failed completion retains dirty field for retry" );
	session.BeginStorePending( fields, achievements );
	session.CompletePendingStore( true );
	Check( !session.FieldDirty( 4 ), "successful retry cleans matching generation" );
}

} // namespace

int main() {
	TestFields();
	TestAchievementsAndIdentityReset();
	TestAsyncStoreCompletion();
	if ( failures ) {
		std::fprintf( stderr, "%d test(s) failed\n", failures );
		return 1;
	}
	std::puts( "stats session tests passed" );
	return 0;
}
