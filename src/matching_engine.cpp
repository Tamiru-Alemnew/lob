#include "lob/matching_engine.hpp"

#include <algorithm>

namespace lob {

namespace {
Event reject(OrderId id, Reject why) { return {EventType::Rejected, why, id}; }
}  // namespace

void MatchingEngine::process(const Msg& m, std::vector<Event>& out) {
    switch (m.type) {
        case MsgType::Add: add(m.id, m.side, m.price, m.qty, out); break;
        case MsgType::Cancel: cancel(m.id, out); break;
        case MsgType::Modify: modify(m.id, m.price, m.qty, out); break;
        case MsgType::Market: market(m.id, m.side, m.qty, out); break;
    }
}

Qty MatchingEngine::match(OrderId taker, Side s, Price limit, bool limited, Qty q, std::vector<Event>& out) {
    const Side maker_side = opposite(s);
    while (q > 0) {
        const auto best = book_.best(maker_side);
        if (!best) break;
        if (limited && (s == Side::Buy ? *best > limit : *best < limit)) break;
        PriceLevel& lvl = book_.level(maker_side, *best);
        while (q > 0 && !lvl.empty()) {
            Order* maker = lvl.head;
            const Qty fill = std::min(q, maker->qty);
            out.push_back({EventType::Trade, Reject::None, taker, maker->id, maker->price, fill});
            q -= fill;
            if (fill == maker->qty)
                book_.remove(maker);
            else
                book_.reduce(maker, fill);
        }
    }
    return q;
}

void MatchingEngine::add(OrderId id, Side s, Price p, Qty q, std::vector<Event>& out) {
    if (q == 0) return out.push_back(reject(id, Reject::ZeroQty));
    if (!book_.in_band(p)) return out.push_back(reject(id, Reject::OutOfBand));
    if (book_.find(id)) return out.push_back(reject(id, Reject::DuplicateId));
    out.push_back({EventType::Ack, Reject::None, id, 0, p, q});
    const Qty rest = match(id, s, p, true, q, out);
    if (rest > 0 && !book_.insert(id, s, p, rest)) {
        out.push_back({EventType::Cancelled, Reject::PoolFull, id, 0, p, rest});
    }
}

void MatchingEngine::market(OrderId id, Side s, Qty q, std::vector<Event>& out) {
    if (q == 0) return out.push_back(reject(id, Reject::ZeroQty));
    out.push_back({EventType::Ack, Reject::None, id, 0, 0, q});
    const Qty rest = match(id, s, 0, false, q, out);
    if (rest > 0) out.push_back({EventType::Cancelled, Reject::None, id, 0, 0, rest});
}

void MatchingEngine::cancel(OrderId id, std::vector<Event>& out) {
    Order* o = book_.find(id);
    if (!o) return out.push_back(reject(id, Reject::UnknownId));
    out.push_back({EventType::Cancelled, Reject::None, id, 0, o->price, o->qty});
    book_.remove(o);
}

void MatchingEngine::modify(OrderId id, Price p, Qty q, std::vector<Event>& out) {
    Order* o = book_.find(id);
    if (!o) return out.push_back(reject(id, Reject::UnknownId));
    if (q == 0) return cancel(id, out);
    if (!book_.in_band(p)) return out.push_back(reject(id, Reject::OutOfBand));
    if (p == o->price && q <= o->qty) {
        book_.reduce(o, o->qty - q);
        out.push_back({EventType::Ack, Reject::None, id, 0, p, q});
        return;
    }
    const Side s = o->side;
    cancel(id, out);
    add(id, s, p, q, out);
}

}  // namespace lob
