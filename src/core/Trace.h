#pragma once

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
