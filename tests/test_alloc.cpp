#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <new>
#include <vector>

#include "lob/matching_engine.hpp"
#include "lob/workload.hpp"

namespace {
std::atomic<std::size_t> g_allocs{0};
}

void* operator new(std::size_t n) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

TEST(Allocation, MatchingPathNeverAllocates) {
    const lob::Workload w = lob::generate({.seed = 11, .count = 500'000});
    lob::MatchingEngine engine(lob::book_config(w.header));
    std::vector<lob::Event> events;
    events.reserve(1 << 16);
    const std::size_t before = g_allocs.load();
    ASSERT_GT(before, 0u) << "allocation hook is not active";
    for (const lob::Msg& m : w.msgs) {
        events.clear();
        engine.process(m, events);
    }
    EXPECT_EQ(g_allocs.load() - before, 0u);
}
