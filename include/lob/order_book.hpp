#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "lob/order.hpp"
#include "lob/pool.hpp"
#include "lob/price_level.hpp"

namespace lob {

struct BookConfig {
    Price base_price = 0;  // lowest tradable price, in ticks
    std::size_t num_levels = 1 << 16;
    std::size_t max_orders = 1 << 20;  // resting orders, sizes the pool
    OrderId max_id = 1 << 20;          // ids are dense and engine-assigned
};

struct LevelView {
    Price price;
    std::vector<std::pair<OrderId, Qty>> orders;
    friend bool operator==(const LevelView&, const LevelView&) = default;
};

// Price levels live in a flat array indexed by tick offset from base_price,
// with a bitmap of non-empty levels so the next best price is found with a
// count-trailing/leading-zeros scan instead of a walk over empty levels.
class OrderBook {
   public:
    explicit OrderBook(const BookConfig& cfg);

    bool in_band(Price p) const { return p >= base_ && static_cast<std::uint64_t>(p - base_) < num_levels_; }

    std::optional<Price> best(Side s) const {
        const SideBook& b = sides_[idx(s)];
        if (b.best < 0) return std::nullopt;
        return base_ + b.best;
    }

    PriceLevel& level(Side s, Price p) { return sides_[idx(s)].levels[p - base_]; }
    const PriceLevel& level(Side s, Price p) const { return sides_[idx(s)].levels[p - base_]; }

    Order* find(OrderId id) const { return id < index_.size() ? index_[id] : nullptr; }

    Order* insert(OrderId id, Side s, Price p, Qty q);
    void remove(Order* o);
    void reduce(Order* o, Qty by);

    std::size_t order_count() const { return pool_.used(); }
    std::uint64_t resting_qty(Side s) const;
    std::vector<LevelView> levels(Side s) const;
    bool crossed() const;
    bool check_invariants() const;

   private:
    struct SideBook {
        std::vector<PriceLevel> levels;
        std::vector<std::uint64_t> bits;
        std::int64_t best = -1;
    };

    static constexpr std::size_t idx(Side s) { return static_cast<std::size_t>(s); }
    void mark(SideBook& b, std::int64_t i);
    void unmark(Side s, SideBook& b, std::int64_t i);
    std::int64_t next_lower(const SideBook& b, std::int64_t from) const;
    std::int64_t next_higher(const SideBook& b, std::int64_t from) const;

    Price base_;
    std::size_t num_levels_;
    SideBook sides_[2];
    std::vector<Order*> index_;
    OrderPool pool_;
};

}  // namespace lob
