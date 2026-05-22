#pragma once

#include <cstdint>

#include "lob/order.hpp"

namespace lob {

// FIFO of resting orders at one price: an intrusive doubly linked list, so
// removing any order is O(1) once its node is known.
struct PriceLevel {
    Order* head = nullptr;
    Order* tail = nullptr;
    std::uint64_t total = 0;
    std::uint32_t count = 0;

    bool empty() const { return head == nullptr; }

    void push_back(Order* o) {
        o->prev = tail;
        o->next = nullptr;
        if (tail)
            tail->next = o;
        else
            head = o;
        tail = o;
        total += o->qty;
        ++count;
    }

    void erase(Order* o) {
        if (o->prev)
            o->prev->next = o->next;
        else
            head = o->next;
        if (o->next)
            o->next->prev = o->prev;
        else
            tail = o->prev;
        total -= o->qty;
        --count;
    }
};

}  // namespace lob
