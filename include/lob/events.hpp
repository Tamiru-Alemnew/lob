#pragma once

#include <cstdint>

#include "lob/types.hpp"

namespace lob {

enum class EventType : std::uint8_t { Ack, Trade, Cancelled, Rejected };

enum class Reject : std::uint8_t { None, UnknownId, DuplicateId, OutOfBand, ZeroQty, PoolFull };

// Trade: id = aggressor, maker = resting order, price = maker's price.
// Cancelled: qty = quantity removed (also the unfilled rest of a market order).
struct Event {
    EventType type;
    Reject reason = Reject::None;
    OrderId id = 0;
    OrderId maker = 0;
    Price price = 0;
    Qty qty = 0;

    friend bool operator==(const Event&, const Event&) = default;
};

}  // namespace lob
