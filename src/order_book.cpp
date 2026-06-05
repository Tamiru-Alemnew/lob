#include "lob/order_book.hpp"

#include <bit>

namespace lob {

OrderBook::OrderBook(const BookConfig& cfg)
    : base_(cfg.base_price), num_levels_(cfg.num_levels), index_(cfg.max_id + 1, nullptr), pool_(cfg.max_orders) {
    for (SideBook& b : sides_) {
        b.levels.resize(num_levels_);
        b.bits.resize((num_levels_ + 63) / 64);
    }
}

Order* OrderBook::insert(OrderId id, Side s, Price p, Qty q) {
    Order* o = pool_.acquire();
    if (!o) return nullptr;
    if (id >= index_.size()) index_.resize(2 * id + 1, nullptr);
    o->id = id;
    o->side = s;
    o->price = p;
    o->qty = q;
    SideBook& b = sides_[idx(s)];
    const std::int64_t i = p - base_;
    b.levels[i].push_back(o);
    mark(b, i);
    if (b.best < 0 || (s == Side::Buy ? i > b.best : i < b.best)) b.best = i;
    index_[id] = o;
    return o;
}

void OrderBook::remove(Order* o) {
    SideBook& b = sides_[idx(o->side)];
    const std::int64_t i = o->price - base_;
    PriceLevel& lvl = b.levels[i];
    lvl.erase(o);
    if (lvl.empty()) unmark(o->side, b, i);
    index_[o->id] = nullptr;
    pool_.release(o);
}

void OrderBook::reduce(Order* o, Qty by) {
    o->qty -= by;
    sides_[idx(o->side)].levels[o->price - base_].total -= by;
}

void OrderBook::mark(SideBook& b, std::int64_t i) { b.bits[i >> 6] |= std::uint64_t{1} << (i & 63); }

void OrderBook::unmark(Side s, SideBook& b, std::int64_t i) {
    b.bits[i >> 6] &= ~(std::uint64_t{1} << (i & 63));
    if (i == b.best) b.best = s == Side::Buy ? next_lower(b, i) : next_higher(b, i);
}

std::int64_t OrderBook::next_lower(const SideBook& b, std::int64_t from) const {
    std::int64_t w = from >> 6;
    std::uint64_t word = b.bits[w] & ((std::uint64_t{1} << (from & 63)) - 1);
    while (true) {
        if (word) return (w << 6) + 63 - std::countl_zero(word);
        if (--w < 0) return -1;
        word = b.bits[w];
    }
}

std::int64_t OrderBook::next_higher(const SideBook& b, std::int64_t from) const {
    const auto words = static_cast<std::int64_t>(b.bits.size());
    std::int64_t w = from >> 6;
    std::uint64_t word = (from & 63) == 63 ? 0 : b.bits[w] & (~std::uint64_t{0} << ((from & 63) + 1));
    while (true) {
        if (word) return (w << 6) + std::countr_zero(word);
        if (++w >= words) return -1;
        word = b.bits[w];
    }
}

std::uint64_t OrderBook::resting_qty(Side s) const {
    std::uint64_t total = 0;
    for (const PriceLevel& l : sides_[idx(s)].levels) total += l.total;
    return total;
}

std::vector<LevelView> OrderBook::levels(Side s) const {
    std::vector<LevelView> out;
    const SideBook& b = sides_[idx(s)];
    for (std::size_t k = 0; k < num_levels_; ++k) {
        const std::size_t i = s == Side::Buy ? num_levels_ - 1 - k : k;
        if (b.levels[i].empty()) continue;
        LevelView v{base_ + static_cast<Price>(i), {}};
        for (const Order* o = b.levels[i].head; o; o = o->next) v.orders.emplace_back(o->id, o->qty);
        out.push_back(std::move(v));
    }
    return out;
}

bool OrderBook::crossed() const {
    const auto bid = best(Side::Buy), ask = best(Side::Sell);
    return bid && ask && *bid >= *ask;
}

bool OrderBook::check_invariants() const {
    if (crossed()) return false;
    std::size_t orders = 0;
    for (Side s : {Side::Buy, Side::Sell}) {
        const SideBook& b = sides_[idx(s)];
        std::int64_t expect_best = -1;
        for (std::size_t i = 0; i < num_levels_; ++i) {
            const PriceLevel& l = b.levels[i];
            const bool bit = (b.bits[i >> 6] >> (i & 63)) & 1;
            if (bit == l.empty()) return false;
            std::uint64_t total = 0;
            std::uint32_t count = 0;
            for (const Order* o = l.head; o; o = o->next) {
                if (o->qty == 0 || o->side != s || o->price != base_ + static_cast<Price>(i)) return false;
                if (find(o->id) != o) return false;
                total += o->qty;
                ++count;
            }
            if (total != l.total || count != l.count) return false;
            orders += count;
            if (!l.empty() && (s == Side::Buy || expect_best < 0)) expect_best = static_cast<std::int64_t>(i);
        }
        if (expect_best != b.best) return false;
    }
    return orders == pool_.used();
}

}  // namespace lob
