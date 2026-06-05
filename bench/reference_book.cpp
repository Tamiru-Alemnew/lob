#include "reference_book.hpp"

#include <algorithm>

namespace lob {

void ReferenceBook::process(const Msg& m, std::vector<Event>& out) {
    switch (m.type) {
        case MsgType::Add: add(m.id, m.side, m.price, m.qty, out); return;
        case MsgType::Cancel: cancel(m.id, out); return;
        case MsgType::Market:
            if (m.qty == 0) {
                out.push_back({EventType::Rejected, Reject::ZeroQty, m.id});
                return;
            }
            out.push_back({EventType::Ack, Reject::None, m.id, 0, 0, m.qty});
            if (Qty rest = match(m.id, m.side, 0, false, m.qty, out))
                out.push_back({EventType::Cancelled, Reject::None, m.id, 0, 0, rest});
            return;
        case MsgType::Modify: {
            auto it = where_.find(m.id);
            if (it == where_.end()) {
                out.push_back({EventType::Rejected, Reject::UnknownId, m.id});
                return;
            }
            if (m.qty == 0) return cancel(m.id, out);
            if (!in_band(m.price)) {
                out.push_back({EventType::Rejected, Reject::OutOfBand, m.id});
                return;
            }
            Loc loc = it->second;
            if (m.price == loc.price && m.qty <= loc.it->qty) {
                loc.it->qty = m.qty;
                out.push_back({EventType::Ack, Reject::None, m.id, 0, m.price, m.qty});
                return;
            }
            cancel(m.id, out);
            add(m.id, loc.side, m.price, m.qty, out);
            return;
        }
    }
}

template <class Book>
Qty ReferenceBook::sweep(Book& book, OrderId taker, Side s, Price limit, bool limited, Qty q, std::vector<Event>& out) {
    while (q > 0 && !book.empty()) {
        auto lvl = book.begin();
        if (limited && (s == Side::Buy ? lvl->first > limit : lvl->first < limit)) break;
        Queue& queue = lvl->second;
        while (q > 0 && !queue.empty()) {
            RefOrder& maker = queue.front();
            const Qty fill = std::min(q, maker.qty);
            out.push_back({EventType::Trade, Reject::None, taker, maker.id, lvl->first, fill});
            q -= fill;
            maker.qty -= fill;
            if (maker.qty == 0) {
                where_.erase(maker.id);
                queue.pop_front();
            }
        }
        if (queue.empty()) book.erase(lvl);
    }
    return q;
}

Qty ReferenceBook::match(OrderId taker, Side s, Price limit, bool limited, Qty q, std::vector<Event>& out) {
    return s == Side::Buy ? sweep(asks_, taker, s, limit, limited, q, out)
                          : sweep(bids_, taker, s, limit, limited, q, out);
}

void ReferenceBook::add(OrderId id, Side s, Price p, Qty q, std::vector<Event>& out) {
    if (q == 0) return out.push_back({EventType::Rejected, Reject::ZeroQty, id});
    if (!in_band(p)) return out.push_back({EventType::Rejected, Reject::OutOfBand, id});
    if (where_.count(id)) return out.push_back({EventType::Rejected, Reject::DuplicateId, id});
    out.push_back({EventType::Ack, Reject::None, id, 0, p, q});
    const Qty rest = match(id, s, p, true, q, out);
    if (rest == 0) return;
    Queue& queue = s == Side::Buy ? bids_[p] : asks_[p];
    queue.push_back({id, rest});
    where_[id] = {s, p, std::prev(queue.end())};
}

void ReferenceBook::cancel(OrderId id, std::vector<Event>& out) {
    auto it = where_.find(id);
    if (it == where_.end()) return out.push_back({EventType::Rejected, Reject::UnknownId, id});
    const Loc loc = it->second;
    out.push_back({EventType::Cancelled, Reject::None, id, 0, loc.price, loc.it->qty});
    if (loc.side == Side::Buy) {
        auto lvl = bids_.find(loc.price);
        lvl->second.erase(loc.it);
        if (lvl->second.empty()) bids_.erase(lvl);
    } else {
        auto lvl = asks_.find(loc.price);
        lvl->second.erase(loc.it);
        if (lvl->second.empty()) asks_.erase(lvl);
    }
    where_.erase(it);
}

std::vector<LevelView> ReferenceBook::levels(Side s) const {
    std::vector<LevelView> out;
    auto dump = [&out](const auto& book) {
        for (const auto& [price, queue] : book) {
            LevelView v{price, {}};
            for (const RefOrder& o : queue) v.orders.emplace_back(o.id, o.qty);
            out.push_back(std::move(v));
        }
    };
    if (s == Side::Buy)
        dump(bids_);
    else
        dump(asks_);
    return out;
}

}  // namespace lob
