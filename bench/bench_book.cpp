#include <benchmark/benchmark.h>

#include <vector>

#include "lob/matching_engine.hpp"
#include "lob/spsc_queue.hpp"
#include "lob/workload.hpp"
#include "reference_book.hpp"

namespace {

const lob::Workload& workload() {
    static const lob::Workload w = lob::generate({.seed = 7, .count = 1'000'000});
    return w;
}

template <class Book>
void BM_Replay(benchmark::State& state) {
    const lob::Workload& w = workload();
    std::vector<lob::Event> events;
    events.reserve(1 << 16);
    for (auto _ : state) {
        state.PauseTiming();
        Book book(lob::book_config(w.header));
        state.ResumeTiming();
        for (const lob::Msg& m : w.msgs) {
            events.clear();
            book.process(m, events);
        }
        benchmark::DoNotOptimize(events.data());
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations() * w.msgs.size()));
}
BENCHMARK(BM_Replay<lob::MatchingEngine>)->Unit(benchmark::kMillisecond)->MinTime(2);
BENCHMARK(BM_Replay<lob::ReferenceBook>)->Unit(benchmark::kMillisecond)->MinTime(2);

template <class Book>
void BM_AddCancelSameLevel(benchmark::State& state) {
    lob::BookConfig cfg{.base_price = 0, .num_levels = 1024, .max_orders = 1024, .max_id = 1 << 20};
    Book book(cfg);
    std::vector<lob::Event> events;
    events.reserve(64);
    lob::OrderId id = 1;
    for (auto _ : state) {
        events.clear();
        book.process({lob::MsgType::Add, lob::Side::Buy, 0, 10, id, 500}, events);
        book.process({lob::MsgType::Cancel, lob::Side::Buy, 0, 0, id, 0}, events);
        id = id % (1 << 20) + 1;
        benchmark::DoNotOptimize(events.data());
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_AddCancelSameLevel<lob::MatchingEngine>);
BENCHMARK(BM_AddCancelSameLevel<lob::ReferenceBook>);

void BM_SpscSameThread(benchmark::State& state) {
    lob::SpscQueue<lob::Msg, 1024> q;
    lob::Msg m{};
    for (auto _ : state) {
        q.try_push(m);
        benchmark::DoNotOptimize(q.try_pop());
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SpscSameThread);

}  // namespace

BENCHMARK_MAIN();
