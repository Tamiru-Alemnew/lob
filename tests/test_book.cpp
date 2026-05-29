#include <gtest/gtest.h>

#include <vector>

#include "lob/matching_engine.hpp"

using namespace lob;

namespace {

class BookTest : public ::testing::Test {
protected:
    MatchingEngine engine{BookConfig{.base_price = 100, .num_levels = 256, .max_orders = 64, .max_id = 1000}};
    std::vector<Event> ev;

    std::vector<Event> add(OrderId id, Side s, Price p, Qty q) { return run({MsgType::Add, s, 0, q, id, p}); }
    std::vector<Event> market(OrderId id, Side s, Qty q) { return run({MsgType::Market, s, 0, q, id, 0}); }
    std::vector<Event> cancel(OrderId id) { return run({MsgType::Cancel, Side::Buy, 0, 0, id, 0}); }
    std::vector<Event> modify(OrderId id, Price p, Qty q) { return run({MsgType::Modify, Side::Buy, 0, q, id, p}); }

    std::vector<Event> run(const Msg& m) {
        ev.clear();
        engine.process(m, ev);
        EXPECT_TRUE(engine.book().check_invariants());
        return ev;
    }

    static std::vector<Event> trades(const std::vector<Event>& e) {
        std::vector<Event> t;
        for (const Event& x : e)
            if (x.type == EventType::Trade) t.push_back(x);
        return t;
    }

    std::vector<std::pair<OrderId, Qty>> queue(Side s, Price p) const {
        for (const LevelView& l : engine.book().levels(s))
            if (l.price == p) return l.orders;
        return {};
    }
};

Event trade(OrderId taker, OrderId maker, Price p, Qty q) { return {EventType::Trade, Reject::None, taker, maker, p, q}; }

TEST_F(BookTest, SamePriceFillsOldestFirst) {
    add(1, Side::Sell, 150, 5);
    add(2, Side::Sell, 150, 5);
    add(3, Side::Sell, 150, 5);
    EXPECT_EQ(trades(add(4, Side::Buy, 150, 7)), (std::vector{trade(4, 1, 150, 5), trade(4, 2, 150, 2)}));
    EXPECT_EQ(queue(Side::Sell, 150), (std::vector<std::pair<OrderId, Qty>>{{2, 3}, {3, 5}}));
}

TEST_F(BookTest, BetterPriceFillsFirstAtMakerPrice) {
    add(1, Side::Sell, 152, 5);
    add(2, Side::Sell, 151, 5);
    add(3, Side::Sell, 150, 5);
    EXPECT_EQ(trades(add(4, Side::Buy, 155, 12)),
              (std::vector{trade(4, 3, 150, 5), trade(4, 2, 151, 5), trade(4, 1, 152, 2)}));
    EXPECT_EQ(engine.book().best(Side::Sell), 152);
}

TEST_F(BookTest, LimitStopsAtItsPriceAndRestsTheRest) {
    add(1, Side::Sell, 150, 5);
    add(2, Side::Sell, 153, 5);
    EXPECT_EQ(trades(add(3, Side::Buy, 151, 8)), (std::vector{trade(3, 1, 150, 5)}));
    EXPECT_EQ(engine.book().best(Side::Buy), 151);
    EXPECT_EQ(queue(Side::Buy, 151), (std::vector<std::pair<OrderId, Qty>>{{3, 3}}));
    EXPECT_FALSE(engine.book().crossed());
}

TEST_F(BookTest, CancelOfPartlyFilledOrderRemovesTheRemainder) {
    add(1, Side::Buy, 140, 10);
    add(2, Side::Sell, 140, 4);
    const auto e = cancel(1);
    ASSERT_EQ(e.size(), 1u);
    EXPECT_EQ(e[0].type, EventType::Cancelled);
    EXPECT_EQ(e[0].qty, 6u);
    EXPECT_FALSE(engine.book().best(Side::Buy));
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST_F(BookTest, CancelMiddleOfQueueKeepsOthersInOrder) {
    add(1, Side::Buy, 140, 1);
    add(2, Side::Buy, 140, 2);
    add(3, Side::Buy, 140, 3);
    cancel(2);
    EXPECT_EQ(queue(Side::Buy, 140), (std::vector<std::pair<OrderId, Qty>>{{1, 1}, {3, 3}}));
}

TEST_F(BookTest, MarketOrderNeverRests) {
    add(1, Side::Sell, 150, 3);
    const auto e = market(2, Side::Buy, 10);
    EXPECT_EQ(trades(e), (std::vector{trade(2, 1, 150, 3)}));
    EXPECT_EQ(e.back().type, EventType::Cancelled);
    EXPECT_EQ(e.back().qty, 7u);
    EXPECT_FALSE(engine.book().best(Side::Buy));
    EXPECT_FALSE(engine.book().best(Side::Sell));
}

TEST_F(BookTest, ModifyDownKeepsPriority) {
    add(1, Side::Buy, 140, 10);
    add(2, Side::Buy, 140, 10);
    modify(1, 140, 4);
    EXPECT_EQ(queue(Side::Buy, 140), (std::vector<std::pair<OrderId, Qty>>{{1, 4}, {2, 10}}));
}

TEST_F(BookTest, ModifyUpOrRepriceLosesPriority) {
    add(1, Side::Buy, 140, 10);
    add(2, Side::Buy, 140, 10);
    modify(1, 140, 11);
    EXPECT_EQ(queue(Side::Buy, 140), (std::vector<std::pair<OrderId, Qty>>{{2, 10}, {1, 11}}));
    modify(2, 141, 10);
    EXPECT_EQ(engine.book().best(Side::Buy), 141);
}

TEST_F(BookTest, ModifyThatCrossesTrades) {
    add(1, Side::Sell, 150, 5);
    add(2, Side::Buy, 140, 5);
    EXPECT_EQ(trades(modify(2, 150, 5)), (std::vector{trade(2, 1, 150, 5)}));
    EXPECT_EQ(engine.book().order_count(), 0u);
}

TEST_F(BookTest, Rejects) {
    EXPECT_EQ(add(1, Side::Buy, 99, 5)[0].reason, Reject::OutOfBand);
    EXPECT_EQ(add(1, Side::Buy, 356, 5)[0].reason, Reject::OutOfBand);
    EXPECT_EQ(add(1, Side::Buy, 120, 0)[0].reason, Reject::ZeroQty);
    add(1, Side::Buy, 120, 5);
    EXPECT_EQ(add(1, Side::Buy, 121, 5)[0].reason, Reject::DuplicateId);
    EXPECT_EQ(cancel(77)[0].reason, Reject::UnknownId);
    EXPECT_EQ(modify(77, 120, 1)[0].reason, Reject::UnknownId);
}

TEST_F(BookTest, BestPriceTrackingAcrossBitmapWords) {
    for (Price p : {100, 163, 164, 227, 355}) add(static_cast<OrderId>(p), Side::Sell, p, 1);
    for (Price p : {100, 163, 164, 227, 355}) {
        EXPECT_EQ(engine.book().best(Side::Sell), p);
        cancel(static_cast<OrderId>(p));
    }
    EXPECT_FALSE(engine.book().best(Side::Sell));
    for (Price p : {355, 292, 291, 101, 100}) add(static_cast<OrderId>(p), Side::Buy, p, 1);
    for (Price p : {355, 292, 291, 101, 100}) {
        EXPECT_EQ(engine.book().best(Side::Buy), p);
        cancel(static_cast<OrderId>(p));
    }
    EXPECT_FALSE(engine.book().best(Side::Buy));
}

}  // namespace
