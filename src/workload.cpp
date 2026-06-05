#include "lob/workload.hpp"

#include <algorithm>
#include <random>
#include <unordered_map>
#include <vector>

#include "lob/matching_engine.hpp"

namespace lob {

namespace {

class LiveSet {
   public:
    void insert(OrderId id) {
        pos_[id] = ids_.size();
        ids_.push_back(id);
    }
    void erase(OrderId id) {
        auto it = pos_.find(id);
        if (it == pos_.end()) return;
        ids_[it->second] = ids_.back();
        pos_[ids_.back()] = it->second;
        ids_.pop_back();
        pos_.erase(it);
    }
    bool empty() const { return ids_.empty(); }
    OrderId pick(std::mt19937_64& rng) const { return ids_[rng() % ids_.size()]; }

   private:
    std::vector<OrderId> ids_;
    std::unordered_map<OrderId, std::size_t> pos_;
};

}  // namespace

BookConfig book_config(const WorkloadHeader& h) {
    BookConfig c;
    c.base_price = h.base_price;
    c.num_levels = h.num_levels;
    c.max_id = h.max_id;
    c.max_orders = std::min<std::uint64_t>(h.max_id + 1, 1u << 22);
    return c;
}

Workload generate(const WorkloadParams& p) {
    Workload w;
    w.header.seed = p.seed;
    w.header.base_price = 0;
    w.header.num_levels = p.num_levels;
    w.header.max_id = p.count;
    w.msgs.reserve(p.count);

    MatchingEngine engine(book_config(w.header));
    std::vector<Event> events;
    events.reserve(1 << 16);
    LiveSet live;
    std::mt19937_64 rng(p.seed);
    std::uniform_real_distribution<double> u(0, 1);
    std::geometric_distribution<int> depth(0.05);
    std::uniform_int_distribution<Qty> size(1, 100);

    const auto lo = static_cast<Price>(p.num_levels / 8), hi = static_cast<Price>(p.num_levels - p.num_levels / 8);
    Price mid = static_cast<Price>(p.num_levels / 2);
    OrderId next_id = 1;

    while (w.msgs.size() < p.count) {
        if (u(rng) < 0.05) mid = std::clamp<Price>(mid + (rng() & 1 ? 1 : -1), lo, hi);
        const double r = u(rng);
        const Side side = rng() & 1 ? Side::Buy : Side::Sell;
        Msg m{};
        if (r < p.add || (r < p.add + p.cancel && live.empty())) {
            const int off = depth(rng);
            const Price px = u(rng) < p.marketable ? (side == Side::Buy ? mid + 1 + off % 3 : mid - 1 - off % 3)
                                                   : (side == Side::Buy ? mid - off : mid + 1 + off);
            m = {MsgType::Add, side, 0, size(rng), next_id++, px};
        } else if (r < p.add + p.cancel) {
            m = {MsgType::Cancel, Side::Buy, 0, 0, live.pick(rng), 0};
        } else {
            m = {MsgType::Market, side, 0, size(rng), next_id++, 0};
        }
        w.msgs.push_back(m);

        events.clear();
        engine.process(m, events);
        for (const Event& e : events) {
            if (e.type == EventType::Trade && !engine.book().find(e.maker)) live.erase(e.maker);
            if (e.type == EventType::Cancelled) live.erase(e.id);
        }
        if (m.type == MsgType::Add && engine.book().find(m.id)) live.insert(m.id);
    }
    w.header.count = w.msgs.size();
    w.header.max_id = next_id;
    return w;
}

}  // namespace lob
