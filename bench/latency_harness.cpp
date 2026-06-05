// Replays a workload file through the matcher on one pinned thread and
// measures, in the same replay, throughput and per-message latency (split by
// message type) with HdrHistogram. One warm-up replay, then N timed repeats.
#include <hdr/hdr_histogram.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "lob/affinity.hpp"
#include "lob/clock.hpp"
#include "lob/matching_engine.hpp"
#include "lob/workload.hpp"
#include "reference_book.hpp"

namespace {

struct Histogram {
    hdr_histogram* h = nullptr;
    Histogram() { hdr_init(1, 100'000'000, 3, &h); }  // 1 ns .. 100 ms, 3 significant digits
    ~Histogram() { hdr_close(h); }
    Histogram(const Histogram&) = delete;
    Histogram& operator=(const Histogram&) = delete;
    void record(std::int64_t ns) { hdr_record_value(h, std::max<std::int64_t>(ns, 1)); }
    double p(double q) const { return static_cast<double>(hdr_value_at_percentile(h, q)); }
};

struct RunResult {
    double seconds = 0;
    double msgs_per_s = 0;
    std::uint64_t timed[4] = {};
    std::unique_ptr<Histogram> hist[4];
};

const char* kNames[4] = {"add", "cancel", "modify", "market"};

template <class Book>
RunResult replay(const lob::Workload& w, double ns_per_tick, bool timed) {
    Book book(lob::book_config(w.header));
    std::vector<lob::Event> events;
    events.reserve(1 << 16);
    RunResult r;
    for (auto& h : r.hist) h = std::make_unique<Histogram>();
    std::vector<std::uint32_t> lat;
    if (timed) lat.resize(w.msgs.size());

    const auto t0 = std::chrono::steady_clock::now();
    if (timed) {
        for (std::size_t i = 0; i < w.msgs.size(); ++i) {
            const std::uint64_t a = lob::ticks();
            events.clear();
            book.process(w.msgs[i], events);
            const std::uint64_t b = lob::ticks();
            lat[i] = static_cast<std::uint32_t>(std::min<std::uint64_t>(b - a, UINT32_MAX));
        }
    } else {
        for (const lob::Msg& m : w.msgs) {
            events.clear();
            book.process(m, events);
        }
    }
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    r.msgs_per_s = static_cast<double>(w.msgs.size()) / r.seconds;
    if (timed) {
        for (std::size_t i = 0; i < w.msgs.size(); ++i) {
            const int t = static_cast<int>(w.msgs[i].type);
            r.hist[t]->record(static_cast<std::int64_t>(lat[i] * ns_per_tick + 0.5));
            ++r.timed[t];
        }
    }
    return r;
}

std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

template <class Book>
int run(const lob::Workload& w, const std::string& engine, int repeats, int cpu, const std::string& out,
        const std::string& workload_path) {
    const bool pinned = lob::pin_to_cpu(cpu);
    const double npt = lob::ns_per_tick();
    replay<Book>(w, npt, true);  // warm-up

    std::vector<RunResult> runs;
    std::vector<double> untimed;
    for (int i = 0; i < repeats; ++i) {
        runs.push_back(replay<Book>(w, npt, true));
        untimed.push_back(replay<Book>(w, npt, false).msgs_per_s);
    }
    std::vector<std::size_t> order(runs.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return runs[a].msgs_per_s < runs[b].msgs_per_s; });
    const RunResult& med = runs[order[order.size() / 2]];
    std::sort(untimed.begin(), untimed.end());

    std::ostringstream js;
    js << "{\n  \"engine\": \"" << engine << "\",\n  \"workload\": \"" << json_escape(workload_path)
       << "\",\n  \"messages\": " << w.msgs.size() << ",\n  \"seed\": " << w.header.seed
       << ",\n  \"repeats\": " << repeats << ",\n  \"pinned_cpu\": " << (pinned ? cpu : -1)
       << ",\n  \"ns_per_tick\": " << npt << ",\n  \"throughput_msgs_per_s\": {\"median\": " << med.msgs_per_s
       << ", \"min\": " << runs[order.front()].msgs_per_s << ", \"max\": " << runs[order.back()].msgs_per_s
       << ", \"all\": [";
    for (std::size_t i = 0; i < runs.size(); ++i) js << (i ? ", " : "") << runs[i].msgs_per_s;
    js << "]},\n  \"untimed_throughput_msgs_per_s\": {\"median\": " << untimed[untimed.size() / 2]
       << ", \"min\": " << untimed.front() << ", \"max\": " << untimed.back() << "},\n  \"latency_ns_median_run\": {";
    bool first = true;
    for (int t = 0; t < 4; ++t) {
        if (!med.timed[t]) continue;
        const Histogram& h = *med.hist[t];
        js << (first ? "" : ",") << "\n    \"" << kNames[t] << "\": {\"count\": " << med.timed[t]
           << ", \"p50\": " << h.p(50) << ", \"p99\": " << h.p(99) << ", \"p99.9\": " << h.p(99.9)
           << ", \"max\": " << hdr_max(h.h) << "}";
        first = false;
    }
    js << "\n  },\n  \"latency_p99_ns_per_run\": {";
    first = true;
    for (int t = 0; t < 4; ++t) {
        if (!med.timed[t]) continue;
        js << (first ? "" : ", ") << "\"" << kNames[t] << "\": [";
        for (std::size_t i = 0; i < runs.size(); ++i) js << (i ? ", " : "") << runs[i].hist[t]->p(99);
        js << "]";
        first = false;
    }
    js << "}\n}\n";

    std::cout << js.str();
    if (!out.empty()) {
        std::ofstream f(out);
        f << js.str();
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string path, out, engine = "lob";
    int repeats = 5, cpu = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--reference")
            engine = "reference";
        else if (a == "--repeats" && i + 1 < argc)
            repeats = std::stoi(argv[++i]);
        else if (a == "--cpu" && i + 1 < argc)
            cpu = std::stoi(argv[++i]);
        else if (a == "--out" && i + 1 < argc)
            out = argv[++i];
        else if (path.empty())
            path = a;
        else {
            std::cerr << "usage: latency_harness FILE [--reference] [--repeats N] [--cpu N] [--out FILE]\n";
            return 2;
        }
    }
    if (path.empty() || repeats < 1) {
        std::cerr << "usage: latency_harness FILE [--reference] [--repeats N] [--cpu N] [--out FILE]\n";
        return 2;
    }
    const lob::Workload w = lob::read_workload(path);
    if (engine == "reference") return run<lob::ReferenceBook>(w, engine, repeats, cpu, out, path);
    return run<lob::MatchingEngine>(w, engine, repeats, cpu, out, path);
}
