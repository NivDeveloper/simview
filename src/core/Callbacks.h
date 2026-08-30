#pragma once

// Internal to src/ — the two callback shapes. Here rather than in
// platform/ or ui/ because both layers keep a list of them: the frame
// callbacks are platform's, the panel callbacks are ui's, and a type
// two layers share lives below both.

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

// forward_list push_front reverses registration; fire in registration
// order by walking a copied reverse. The COPY matters: a callback that
// registers another does not see its own registration this frame.
// Lists are tiny; per frame is fine.
template <typename L, typename F> void in_order(const L &l, F f) {
    std::vector<typename L::value_type> v(l.begin(), l.end());
    std::for_each(v.rbegin(), v.rend(), f);
}

} // namespace impl
} // namespace sv
