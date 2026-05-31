#pragma once

#include <chrono>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#endif

namespace lob {

// Cycle counter on x86 (rdtsc), steady_clock elsewhere. Convert with ns_per_tick().
inline std::uint64_t ticks() {
#if defined(__x86_64__) || defined(_M_X64)
    return __rdtsc();
#else
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

inline double ns_per_tick() {
#if defined(__x86_64__) || defined(_M_X64)
    const auto t0 = std::chrono::steady_clock::now();
    const std::uint64_t c0 = ticks();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(200)) {
    }
    const std::uint64_t c1 = ticks();
    const auto ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
    return ns / static_cast<double>(c1 - c0);
#else
    return 1e9 * std::chrono::steady_clock::period::num / std::chrono::steady_clock::period::den;
#endif
}

}  // namespace lob
