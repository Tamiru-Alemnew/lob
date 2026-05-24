#pragma once

#include <vector>

#include "lob/events.hpp"
#include "lob/messages.hpp"
#include "lob/order_book.hpp"

namespace lob {

// Single-instrument price-time priority matcher. Events are appended to a
// caller-owned vector; reserve it once and the engine never allocates.
class MatchingEngine {
public:
    explicit MatchingEngine(const BookConfig& cfg) : book_(cfg) {}

    void process(const Msg& m, std::vector<Event>& out);

    void add(OrderId id, Side s, Price p, Qty q, std::vector<Event>& out);
    void market(OrderId id, Side s, Qty q, std::vector<Event>& out);
    void cancel(OrderId id, std::vector<Event>& out);
    // Size down at the same price keeps queue priority; a price change or a
    // size increase is a cancel followed by a new add at the back of the queue.
    void modify(OrderId id, Price p, Qty q, std::vector<Event>& out);

    const OrderBook& book() const { return book_; }

private:
    Qty match(OrderId taker, Side s, Price limit, bool limited, Qty q, std::vector<Event>& out);

    OrderBook book_;
};

}  // namespace lob
