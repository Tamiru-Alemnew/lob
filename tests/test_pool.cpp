#include <gtest/gtest.h>

#include <vector>

#include "lob/matching_engine.hpp"

using namespace lob;

TEST(Pool, ReusesNodesAndReportsExhaustion) {
    OrderPool pool(2);
    Order* a = pool.acquire();
    Order* b = pool.acquire();
    EXPECT_NE(a, nullptr);
    EXPECT_NE(b, nullptr);
    EXPECT_EQ(pool.acquire(), nullptr);
    pool.release(a);
    EXPECT_EQ(pool.acquire(), a);
}

TEST(Pool, EngineCancelsRestWhenPoolIsFull) {
    MatchingEngine engine({.base_price = 0, .num_levels = 16, .max_orders = 1, .max_id = 8});
    std::vector<Event> ev;
    engine.process({MsgType::Add, Side::Buy, 0, 5, 1, 3}, ev);
    ev.clear();
    engine.process({MsgType::Add, Side::Buy, 0, 5, 2, 4}, ev);
    ASSERT_EQ(ev.size(), 2u);
    EXPECT_EQ(ev[1].type, EventType::Cancelled);
    EXPECT_EQ(ev[1].reason, Reject::PoolFull);
    EXPECT_TRUE(engine.book().check_invariants());
}
