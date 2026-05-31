#pragma once

#include <cstdint>

#include "lob/messages.hpp"
#include "lob/order_book.hpp"

namespace lob {

struct WorkloadParams {
    std::uint64_t seed = 1;
    std::uint64_t count = 10'000'000;
    double add = 0.60, cancel = 0.35;  // the rest are market orders
    double marketable = 0.05;          // share of adds priced through the opposite best
    std::uint64_t num_levels = 1 << 16;
};

// Deterministic message stream around a random-walk mid price. The generator
// runs the matching engine itself so cancels target orders that are live.
Workload generate(const WorkloadParams& p);

BookConfig book_config(const WorkloadHeader& h);

}  // namespace lob
