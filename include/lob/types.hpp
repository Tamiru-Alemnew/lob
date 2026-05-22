#pragma once

#include <cstdint>

namespace lob {

using OrderId = std::uint64_t;
using Price = std::int64_t;  // integer ticks; never a floating-point price
using Qty = std::uint32_t;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };

constexpr Side opposite(Side s) { return s == Side::Buy ? Side::Sell : Side::Buy; }

}  // namespace lob
