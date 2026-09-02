#pragma once

#include <simview/Event.h>

#include <algorithm>
#include <vector>

namespace sv {
namespace impl {

struct Cb {
    void (*fn)(void *);
    void *user;
};

struct Ecb {
    void (*fn)(const Event &, void *);
    void *user;
};

// Fired in registration order by walking a COPIED reverse: a
// callback that registers another does not see it this frame.
template <typename L, typename F> void in_order(const L &l, F f) {
    std::vector<typename L::value_type> v(l.begin(), l.end());
    std::for_each(v.rbegin(), v.rend(), f);
}

} // namespace impl
} // namespace sv
