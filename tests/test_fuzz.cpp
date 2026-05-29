#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "lob/matching_engine.hpp"
#include "lob/workload.hpp"
#include "reference_book.hpp"

using namespace lob;

namespace {

// Adversarial stream: a narrow band so prices cross constantly and hit the
// band edges, ids that are sometimes reused or unknown, zero quantities, and
// modifies of every kind.
std::vector<Msg> random_stream(std::uint64_t seed, std::size_t n, Price levels) {
    std::mt19937_64 rng(seed);
    std::vector<Msg> out;
    OrderId next = 1;
    for (std::size_t i = 0; i < n; ++i) {
        const Side side = rng() & 1 ? Side::Buy : Side::Sell;
        const Price px = static_cast<Price>(rng() % (levels + 4)) - 2;
        const Qty qty = rng() % 50 == 0 ? 0 : static_cast<Qty>(1 + rng() % 20);
        const OrderId known = next > 1 ? 1 + rng() % (next - 1) : 1;
        switch (rng() % 10) {
            case 0: case 1: case 2: case 3: out.push_back({MsgType::Add, side, 0, qty, next++, px}); break;
            case 4: out.push_back({MsgType::Add, side, 0, qty, rng() % 4 == 0 ? known : next++, px}); break;
            case 5: case 6: out.push_back({MsgType::Cancel, side, 0, 0, rng() % 8 == 0 ? next + 5 : known, 0}); break;
            case 7: case 8: out.push_back({MsgType::Modify, side, 0, qty, known, px}); break;
            default: out.push_back({MsgType::Market, side, 0, static_cast<Qty>(rng() % 40), next++, 0}); break;
        }
    }
    return out;
}

void expect_same(const std::vector<Msg>& msgs, const BookConfig& cfg, std::size_t check_every = 1) {
    MatchingEngine engine(cfg);
    ReferenceBook ref(cfg);
    std::vector<Event> a, b;
    for (std::size_t i = 0; i < msgs.size(); ++i) {
        a.clear();
        b.clear();
        engine.process(msgs[i], a);
        ref.process(msgs[i], b);
        ASSERT_EQ(a, b) << "events differ at message " << i;
        if (i % check_every == 0) {
            ASSERT_TRUE(engine.book().check_invariants()) << "invariant broken at message " << i;
        }
    }
    EXPECT_EQ(engine.book().levels(Side::Buy), ref.levels(Side::Buy));
    EXPECT_EQ(engine.book().levels(Side::Sell), ref.levels(Side::Sell));
}

TEST(DifferentialFuzz, AdversarialStreamsMatchReference) {
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        const BookConfig cfg{.base_price = 0, .num_levels = 40, .max_orders = 1 << 16, .max_id = 1 << 16};
        expect_same(random_stream(seed, 20'000, 40), cfg);
    }
}

TEST(DifferentialFuzz, WideBandAcrossBitmapWords) {
    const BookConfig cfg{.base_price = 1000, .num_levels = 300, .max_orders = 1 << 16, .max_id = 1 << 16};
    std::vector<Msg> msgs = random_stream(99, 30'000, 300);
    for (Msg& m : msgs) m.price += 1000;
    expect_same(msgs, cfg);
}

TEST(DifferentialFuzz, BenchmarkWorkloadMatchesReference) {
    const Workload w = generate({.seed = 3, .count = 300'000});
    expect_same(w.msgs, book_config(w.header), 1000);
}

TEST(Invariants, RestingQuantityEqualsAddsMinusFillsMinusCancels) {
    const Workload w = generate({.seed = 5, .count = 20'000});
    MatchingEngine engine(book_config(w.header));
    std::vector<Event> ev;
    std::int64_t added = 0, filled = 0, cancelled = 0;
    for (const Msg& m : w.msgs) {
        ev.clear();
        engine.process(m, ev);
        for (const Event& e : ev) {
            if (e.type == EventType::Ack && m.type == MsgType::Add) added += e.qty;
            if (e.type == EventType::Trade && m.type == MsgType::Add) filled += 2 * e.qty;
            if (e.type == EventType::Trade && m.type == MsgType::Market) filled += e.qty;
            if (e.type == EventType::Cancelled && m.type == MsgType::Cancel) cancelled += e.qty;
        }
        const auto resting = static_cast<std::int64_t>(engine.book().resting_qty(Side::Buy) +
                                                       engine.book().resting_qty(Side::Sell));
        ASSERT_EQ(resting, added - filled - cancelled);
    }
}

}  // namespace
