#pragma once

#include <functional>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

#include "lob/events.hpp"
#include "lob/messages.hpp"
#include "lob/order_book.hpp"

namespace lob {

// The naive book an interviewer would write first: std::map of price to
// std::list of orders, plus a hash map from id to list iterator. Same rules
// and same events as MatchingEngine; used as the benchmark baseline and as
// the oracle in the differential fuzz test.
class ReferenceBook {
   public:
    explicit ReferenceBook(const BookConfig& cfg) : base_(cfg.base_price), levels_(cfg.num_levels) {}

    void process(const Msg& m, std::vector<Event>& out);
    std::vector<LevelView> levels(Side s) const;

   private:
    struct RefOrder {
        OrderId id;
        Qty qty;
    };
    using Queue = std::list<RefOrder>;
    struct Loc {
        Side side;
        Price price;
        Queue::iterator it;
    };

    bool in_band(Price p) const { return p >= base_ && static_cast<std::size_t>(p - base_) < levels_; }
    void add(OrderId id, Side s, Price p, Qty q, std::vector<Event>& out);
    void cancel(OrderId id, std::vector<Event>& out);
    template <class Book>
    Qty sweep(Book& book, OrderId taker, Side s, Price limit, bool limited, Qty q, std::vector<Event>& out);
    Qty match(OrderId taker, Side s, Price limit, bool limited, Qty q, std::vector<Event>& out);

    Price base_;
    std::size_t levels_;
    std::map<Price, Queue, std::greater<>> bids_;
    std::map<Price, Queue> asks_;
    std::unordered_map<OrderId, Loc> where_;
};

}  // namespace lob
