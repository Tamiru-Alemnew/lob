#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "lob/affinity.hpp"
#include "lob/matching_engine.hpp"
#include "lob/spsc_queue.hpp"
#include "lob/workload.hpp"
#include "reference_book.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Counts {
    std::uint64_t trades = 0, cancels = 0, rejects = 0, events = 0;
    void add(const lob::Event& e) {
        ++events;
        trades += e.type == lob::EventType::Trade;
        cancels += e.type == lob::EventType::Cancelled;
        rejects += e.type == lob::EventType::Rejected;
    }
};

template <class Book>
Counts run_inline(Book& book, const lob::Workload& w, double& seconds) {
    std::vector<lob::Event> events;
    events.reserve(1 << 16);
    Counts c;
    const auto t0 = Clock::now();
    for (const lob::Msg& m : w.msgs) {
        events.clear();
        book.process(m, events);
#ifdef LOB_CHECK_INVARIANTS
        if constexpr (std::is_same_v<Book, lob::MatchingEngine>) {
            if (!book.book().check_invariants()) throw std::logic_error("book invariant violated");
        }
#endif
        for (const lob::Event& e : events) c.add(e);
    }
    seconds = std::chrono::duration<double>(Clock::now() - t0).count();
    return c;
}

// Reader thread -> SPSC ring -> matcher thread (pinned) -> SPSC ring -> consumer thread.
Counts run_pipeline(const lob::Workload& w, int cpu, double& seconds) {
    static lob::SpscQueue<lob::Msg, 1 << 16> in;
    static lob::SpscQueue<lob::Event, 1 << 16> out;
    std::atomic<bool> matcher_done{false};
    Counts c;
    lob::MatchingEngine engine(lob::book_config(w.header));

    const auto t0 = Clock::now();
    std::thread reader([&] {
        for (const lob::Msg& m : w.msgs)
            while (!in.try_push(m)) {
            }
    });
    std::thread matcher([&] {
        lob::pin_to_cpu(cpu);
        std::vector<lob::Event> events;
        events.reserve(1 << 16);
        for (std::size_t n = 0; n < w.msgs.size();) {
            auto m = in.try_pop();
            if (!m) continue;
            ++n;
            events.clear();
            engine.process(*m, events);
            for (const lob::Event& e : events)
                while (!out.try_push(e)) {
                }
        }
        matcher_done.store(true, std::memory_order_release);
    });
    while (true) {
        if (auto e = out.try_pop()) {
            c.add(*e);
        } else if (matcher_done.load(std::memory_order_acquire) && out.size_approx() == 0) {
            break;
        }
    }
    reader.join();
    matcher.join();
    seconds = std::chrono::duration<double>(Clock::now() - t0).count();
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    std::string path, engine = "lob";
    bool pipeline = false;
    int cpu = -1, loops = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--reference")
            engine = "reference";
        else if (a == "--pipeline")
            pipeline = true;
        else if (a == "--cpu" && i + 1 < argc)
            cpu = std::stoi(argv[++i]);
        else if (a == "--loops" && i + 1 < argc)
            loops = std::stoi(argv[++i]);
        else if (path.empty())
            path = a;
        else {
            std::cerr << "usage: replay FILE [--reference] [--pipeline] [--cpu N] [--loops N]\n";
            return 2;
        }
    }
    if (path.empty()) {
        std::cerr << "usage: replay FILE [--reference] [--pipeline] [--cpu N] [--loops N]\n";
        return 2;
    }
    const lob::Workload w = lob::read_workload(path);
    double seconds = 0;
    Counts c;
    if (pipeline) {
        c = run_pipeline(w, cpu, seconds);
    } else {
        lob::pin_to_cpu(cpu);
        if (engine == "reference") {
            lob::ReferenceBook book(lob::book_config(w.header));
            c = run_inline(book, w, seconds);
        } else {
            lob::MatchingEngine book(lob::book_config(w.header));
            c = run_inline(book, w, seconds);
            for (int i = 1; i < loops; ++i) {
                lob::MatchingEngine again(lob::book_config(w.header));
                double s = 0;
                run_inline(again, w, s);
            }
            const auto bids = book.book().levels(lob::Side::Buy), asks = book.book().levels(lob::Side::Sell);
            std::cout << "final book: " << book.book().order_count() << " resting orders on " << bids.size()
                      << " bid and " << asks.size() << " ask levels\n";
        }
    }
    std::cout << (pipeline ? "pipeline" : engine) << ": " << w.msgs.size() << " msgs in " << seconds
              << " s = " << static_cast<double>(w.msgs.size()) / seconds / 1e6 << " M msgs/s; " << c.trades
              << " trades, " << c.cancels << " cancels, " << c.rejects << " rejects, " << c.events << " events\n";
}
