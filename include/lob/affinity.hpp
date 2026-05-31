#pragma once

#include <thread>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace lob {

// Pins the calling thread to one CPU. Returns false where unsupported (macOS).
inline bool pin_to_cpu(int cpu) {
#if defined(__linux__)
    if (cpu < 0) return false;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof set, &set) == 0;
#else
    (void)cpu;
    return false;
#endif
}

}  // namespace lob
