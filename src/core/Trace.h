#pragma once

// Internal to src/ — the tracing seam. A build with SIMVIEW_TRACE=ON
// links Tracy's client and these macros become its zones; every other
// build compiles them to nothing, so a release carries no trace of
// the profiler (tests/installed_surface never sees a symbol). The GPU
// half — sections on the graphics queue, gpud's batches on the
// compute queue — lives in platform/Timing.cpp, which is the one file
// that speaks Tracy's wire protocol.

#if SIMVIEW_TRACE
#include <tracy/Tracy.hpp>
#define SV_ZONE(name) ZoneScopedN(name)
#define SV_FRAME_MARK() FrameMark
inline constexpr bool kTraceBuild = true;
#else
#define SV_ZONE(name)
#define SV_FRAME_MARK()
inline constexpr bool kTraceBuild = false;
#endif
