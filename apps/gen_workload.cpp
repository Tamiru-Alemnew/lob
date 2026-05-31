#include <cstdlib>
#include <iostream>
#include <string>

#include "lob/workload.hpp"

int main(int argc, char** argv) {
    lob::WorkloadParams p;
    std::string out = "workload.bin";
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--seed") p.seed = std::stoull(v);
        else if (k == "--count") p.count = std::stoull(v);
        else if (k == "--out") out = v;
        else {
            std::cerr << "usage: gen_workload [--seed N] [--count N] [--out FILE]\n";
            return 2;
        }
    }
    const lob::Workload w = lob::generate(p);
    lob::write_workload(out, w);
    std::size_t counts[4] = {};
    for (const lob::Msg& m : w.msgs) ++counts[static_cast<int>(m.type)];
    std::cout << out << ": " << w.msgs.size() << " messages (seed " << p.seed << "): " << counts[0] << " adds, "
              << counts[1] << " cancels, " << counts[3] << " market\n";
}
