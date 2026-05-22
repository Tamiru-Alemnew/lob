#pragma once

#include "lob/types.hpp"

namespace lob {

struct Order {
    OrderId id = 0;
    Price price = 0;
    Qty qty = 0;
    Side side = Side::Buy;
    Order* prev = nullptr;
    Order* next = nullptr;
};

}  // namespace lob
