#pragma once

// The check vocabulary. Two jobs a bare `if (bad) return 1;` does
// badly: it stops at the FIRST failure, and it does not say what the
// values were. CHECK keeps going and prints both sides; REQUIRE is for
// what the rest of the test cannot proceed without.
//
//   int main() {
//       CHECK_EQ(f.Update(v), true);
//       REQUIRE(app);                    // only inside main
//       return check::summary("field");
//   }

#include <cstdio>
#include <string>
#include <type_traits>

namespace check {

inline int failures = 0;
inline int total = 0;

template <typename T> std::string show(const T &v) {
    if constexpr (std::is_same_v<T, bool>)
        return v ? "true" : "false";
    else if constexpr (std::is_convertible_v<T, std::string>)
        return std::string(v);
    else
        return std::to_string(v);
}

inline void fail(const char *file, int line, const char *what,
                 const std::string &detail) {
    ++failures;
    std::printf("FAIL %s:%d  %s%s%s\n", file, line, what,
                detail.empty() ? "" : "  ", detail.c_str());
}

// 0 when everything held, 1 otherwise — a main can `return` it.
inline int summary(const char *name) {
    if (failures)
        std::printf("FAILED: %s (%d of %d checks)\n", name, failures, total);
    else
        std::printf("PASS: %s (%d checks)\n", name, total);
    return failures ? 1 : 0;
}

// A named non-result: the machine cannot host this test. Returns
// ctest's SKIP_RETURN_CODE, so the summary says Skipped rather than
// Passed — a skip and a pass must not read alike in the log EITHER.
inline constexpr int skip_code = 77;

inline int skip(const char *name, const char *why) {
    std::printf("SKIP: %s (%s)\n", name, why);
    return skip_code;
}

} // namespace check

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++check::total;                                                        \
        if (!(cond))                                                           \
            check::fail(__FILE__, __LINE__, #cond, "");                        \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        ++check::total;                                                        \
        const auto va_ = (a);                                                  \
        const auto vb_ = (b);                                                  \
        if (!(va_ == vb_))                                                     \
            check::fail(__FILE__, __LINE__, #a " == " #b,                      \
                        check::show(va_) + " vs " + check::show(vb_));         \
    } while (0)

#define CHECK_LT(a, b)                                                         \
    do {                                                                       \
        ++check::total;                                                        \
        const auto va_ = (a);                                                  \
        const auto vb_ = (b);                                                  \
        if (!(va_ < vb_))                                                      \
            check::fail(__FILE__, __LINE__, #a " < " #b,                       \
                        check::show(va_) + " vs " + check::show(vb_));         \
    } while (0)

#define CHECK_GT(a, b)                                                         \
    do {                                                                       \
        ++check::total;                                                        \
        const auto va_ = (a);                                                  \
        const auto vb_ = (b);                                                  \
        if (!(va_ > vb_))                                                      \
            check::fail(__FILE__, __LINE__, #a " > " #b,                       \
                        check::show(va_) + " vs " + check::show(vb_));         \
    } while (0)

// What the rest of the test depends on: report and leave. Only
// inside main, because it returns.
#define REQUIRE(cond)                                                          \
    do {                                                                       \
        ++check::total;                                                        \
        if (!(cond)) {                                                         \
            check::fail(__FILE__, __LINE__, "required: " #cond, "");           \
            return 1;                                                          \
        }                                                                      \
    } while (0)
