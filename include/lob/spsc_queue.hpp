#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <new>
#include <optional>
#include <type_traits>

namespace lob {

inline constexpr std::size_t kCacheLine = 64;

// Lock-free single-producer single-consumer ring buffer.
//
// The producer owns tail_, the consumer owns head_. Each side publishes its
// index with a release store and reads the other's with an acquire load, so
// a slot's contents are visible before the index that hands it over. Each
// side also keeps a cached copy of the other's index and only reloads it when
// the queue looks full (or empty), which keeps the shared cache line quiet.
template <class T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>);

public:
    bool try_push(const T& v) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail - head_cache_ == Capacity) {
            head_cache_ = head_.load(std::memory_order_acquire);
            if (tail - head_cache_ == Capacity) return false;
        }
        slots_[tail & kMask] = v;
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    std::optional<T> try_pop() {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_cache_) {
            tail_cache_ = tail_.load(std::memory_order_acquire);
            if (head == tail_cache_) return std::nullopt;
        }
        T v = slots_[head & kMask];
        head_.store(head + 1, std::memory_order_release);
        return v;
    }

    std::size_t size_approx() const {
        return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    alignas(kCacheLine) std::atomic<std::size_t> head_{0};
    alignas(kCacheLine) std::size_t tail_cache_ = 0;  // consumer's copy of tail_
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
    alignas(kCacheLine) std::size_t head_cache_ = 0;  // producer's copy of head_
    alignas(kCacheLine) std::array<T, Capacity> slots_{};
};

}  // namespace lob
