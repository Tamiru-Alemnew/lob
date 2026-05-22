#pragma once

#include <cstddef>
#include <memory>

#include "lob/order.hpp"

namespace lob {

// Fixed-capacity order pool. All nodes are allocated up front; free nodes
// are chained through Order::next, so acquire and release are O(1) and the
// matching path never touches the heap.
class OrderPool {
public:
    explicit OrderPool(std::size_t capacity)
        : nodes_(std::make_unique<Order[]>(capacity)), capacity_(capacity) {
        for (std::size_t i = 0; i + 1 < capacity; ++i) nodes_[i].next = &nodes_[i + 1];
        free_ = capacity ? &nodes_[0] : nullptr;
    }

    Order* acquire() {
        Order* o = free_;
        if (o) {
            free_ = o->next;
            ++used_;
        }
        return o;
    }

    void release(Order* o) {
        o->next = free_;
        o->prev = nullptr;
        free_ = o;
        --used_;
    }

    std::size_t used() const { return used_; }
    std::size_t capacity() const { return capacity_; }

private:
    std::unique_ptr<Order[]> nodes_;
    Order* free_ = nullptr;
    std::size_t capacity_;
    std::size_t used_ = 0;
};

}  // namespace lob
