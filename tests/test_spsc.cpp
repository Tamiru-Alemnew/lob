#include <gtest/gtest.h>

#include <cstdint>
#include <thread>

#include "lob/spsc_queue.hpp"

TEST(Spsc, FullAndEmpty) {
    lob::SpscQueue<int, 4> q;
    EXPECT_FALSE(q.try_pop());
    for (int i = 0; i < 4; ++i) EXPECT_TRUE(q.try_push(i));
    EXPECT_FALSE(q.try_push(9));
    for (int i = 0; i < 4; ++i) EXPECT_EQ(q.try_pop(), i);
    EXPECT_FALSE(q.try_pop());
}

TEST(Spsc, TwoThreadsDeliverEverythingInOrder) {
    static lob::SpscQueue<std::uint64_t, 1024> q;
    constexpr std::uint64_t n = 2'000'000;
    std::thread producer([] {
        for (std::uint64_t i = 0; i < n; ++i)
            while (!q.try_push(i)) {
            }
    });
    std::uint64_t expect = 0;
    while (expect < n) {
        if (auto v = q.try_pop()) {
            ASSERT_EQ(*v, expect);
            ++expect;
        }
    }
    producer.join();
    EXPECT_FALSE(q.try_pop());
}
