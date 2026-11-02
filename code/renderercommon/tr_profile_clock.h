/*
===========================================================================
Copyright (C) 2026 FnQL contributors

Wall clock for renderer profiling deltas.

ri.Milliseconds is the client's per-frame virtual clock (cls.realtime). It
drives 2D shader time and motion blur so cl_avidemo, timescale, and sv_lockstep
captures animate deterministically, and it must stay that way. It does not
advance within a frame, though, so a start/end pair measured with it is always
zero: com_speeds reported rf and bk as 0 and charged all renderer time to cl
(issue #7). Profiling (com_speeds, r_speeds, warm-up timings) reads the
engine's real clock through ri.Microseconds instead and never feeds rendering.
===========================================================================
*/

#ifndef TR_PROFILE_CLOCK_H
#define TR_PROFILE_CLOCK_H

static ID_INLINE int R_ProfileMilliseconds( void )
{
	// A host without the microsecond import keeps the old, frame-quantised
	// reading rather than failing.
	return ri.Microseconds ? (int)( ri.Microseconds() / 1000 ) : ri.Milliseconds();
}

#endif // TR_PROFILE_CLOCK_H
