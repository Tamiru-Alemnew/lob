#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lob/types.hpp"

namespace lob {

enum class MsgType : std::uint8_t { Add = 0, Cancel = 1, Modify = 2, Market = 3 };

struct Msg {
    MsgType type;
    Side side;
    std::uint16_t pad = 0;
    Qty qty;
    OrderId id;
    Price price;
};
static_assert(sizeof(Msg) == 24);

struct WorkloadHeader {
    char magic[4] = {'L', 'O', 'B', 'W'};
    std::uint32_t version = 1;
    std::uint64_t count = 0;
    std::uint64_t seed = 0;
    Price base_price = 0;
    std::uint64_t num_levels = 0;
    std::uint64_t max_id = 0;
};

struct Workload {
    WorkloadHeader header;
    std::vector<Msg> msgs;
};

Workload read_workload(const std::string& path);
void write_workload(const std::string& path, const Workload& w);

}  // namespace lob
