# Order book design notes

## Data layout

```
OrderBook
 ├─ sides_[Buy], sides_[Sell]
 │    ├─ levels: PriceLevel[num_levels]      index = price - base_price
 │    ├─ bits:   uint64[num_levels / 64]     bit i set <=> level i non-empty
 │    └─ best:   index of best level or -1
 ├─ index_: Order*[max_id + 1]               id -> resting node (nullptr if none)
 └─ pool_:  Order[max_orders] + free list    every node, allocated once
PriceLevel { head, tail, total, count }      FIFO, intrusive prev/next in Order
```

`Order` is 40 bytes: id, price, quantity, side and the two list pointers. Nothing else lives on the hot
path.

## Operations and their cost

| operation | work |
|---|---|
| add (passive) | bounds check, index lookup for duplicates, pool pop, list append, set bit, maybe update best: O(1) |
| add (aggressive) | as above plus one loop iteration per maker touched; each emptied level clears a bit and finds the next best with a word scan |
| market | match loop only; never rests |
| cancel | index lookup, unlink, clear bit if the level empties, rescan best if it was the best: O(1) plus a word scan |
| modify down | in place, keeps priority: O(1) |
| modify up / reprice | cancel + add |

Finding the next best level after the best one empties is `countl_zero` / `countr_zero` over the bitmap,
starting at the old best. With prices clustered around the mid this is almost always the same word, and
even an empty 65,536-level side is 1,024 words to scan.

## Why an array of levels

One instrument trades in a band. A flat array gives the level for a price with one subtraction and no
pointer chasing; `std::map` costs a tree descent with a likely cache miss per node, and allocates a node
per new level.

What breaks: the band is fixed at construction. Prices outside it are rejected (`OutOfBand`). A
production book would re-centre (shift the window when the mid nears an edge, which is O(levels) but
rare) or fall back to a tree for the far tails. A fast-moving instrument with a fine tick needs a wide
band, and a wide band with sparse levels makes the bitmap scans longer and the array colder in cache.

## Ids

The engine assumes it assigns order ids itself, as an exchange does, so ids are dense and the id index is
a flat vector. A client-id front end would map client ids to these with an open-addressing hash map such
as `ankerl::unordered_dense`; `std::unordered_map` allocates a node per entry and chases a pointer per
lookup, which is the slow choice.

## Memory

All order nodes come from `OrderPool`, sized at construction. Levels, bitmap and id index are also sized
up front. The event vector is reserved by the caller. `tests/test_alloc.cpp` replaces global
`operator new` with a counter and checks that replaying 500,000 messages allocates nothing.

## The SPSC ring

Head (consumer) and tail (producer) are `std::atomic<size_t>` on separate 64-byte cache lines. The
producer writes the slot, then publishes `tail + 1` with a release store; the consumer reads `tail` with
an acquire load before reading the slot. Release/acquire is what guarantees the slot's contents are
visible before the index that hands it over; with relaxed ordering the consumer could see the new tail
and still read a stale slot. Each side keeps a cached copy of the other side's index and reloads it only
when the queue looks full (producer) or empty (consumer), so in steady state neither core touches the
other's cache line.

Capacity is a power of two so the slot index is a mask, and indices are free-running counters so full
and empty are distinguishable without a spare slot.

## Measuring

`bench/latency_harness.cpp` pins itself to one core, replays the workload once to warm caches and
branch predictors, then replays it N more times. Each timed replay reads the cycle counter before and
after every message and records the difference in a per-type HdrHistogram; total replay time over
message count gives throughput. Throughput and latency therefore come from the same replay, and the
throughput includes the cost of the two timer reads per message. An untimed replay is also run so the
overhead is visible.

On x86 the counter is `rdtsc`, converted with a ratio calibrated against `steady_clock` over 200 ms.
`rdtsc` is not serialising, so a single reading can be off by a few cycles; across millions of samples the
percentiles are stable.
