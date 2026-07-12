extern "C" {
#include "q_shared.h"
}

#include <cstddef>

static_assert( sizeof( usercmd_t ) == 28 );
static_assert( offsetof( usercmd_t, weapon ) == 20 );
static_assert( offsetof( usercmd_t, weaponPrimary ) == 21 );
static_assert( offsetof( usercmd_t, fov ) == 22 );
static_assert( offsetof( usercmd_t, forwardmove ) == 23 );
static_assert( offsetof( usercmd_t, rightmove ) == 24 );
static_assert( offsetof( usercmd_t, upmove ) == 25 );

static_assert( sizeof( trajectory_t ) == 40 );
static_assert( offsetof( trajectory_t, gravity ) == 36 );

static_assert( sizeof( entityState_t ) == 236 );
static_assert( offsetof( entityState_t, health ) == 200 );
static_assert( offsetof( entityState_t, armor ) == 204 );
static_assert( offsetof( entityState_t, location ) == 212 );
static_assert( offsetof( entityState_t, generic1 ) == 224 );
static_assert( offsetof( entityState_t, doubleJumped ) == 232 );

// This is the reconstructed source-known prefix. Retail modules reserve a
// larger player-state region; fields beyond this prefix remain module-owned.
static_assert( sizeof( playerState_t ) == 0x1f8 );
static_assert( offsetof( playerState_t, weaponPrimary ) == 148 );
static_assert( offsetof( playerState_t, fov ) == 156 );
static_assert( offsetof( playerState_t, stats ) == 192 );
static_assert( offsetof( playerState_t, forwardmove ) == 476 );
static_assert( offsetof( playerState_t, ping ) == 480 );
static_assert( offsetof( playerState_t, armorTier ) == 500 );

int main() {
	return 0;
}
