#include "lob/messages.hpp"

#include <cstring>
#include <fstream>
#include <stdexcept>

namespace lob {

Workload read_workload(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    Workload w;
    in.read(reinterpret_cast<char*>(&w.header), sizeof w.header);
    if (!in || std::memcmp(w.header.magic, "LOBW", 4) != 0 || w.header.version != 1)
        throw std::runtime_error(path + ": not a LOBW v1 workload");
    w.msgs.resize(w.header.count);
    in.read(reinterpret_cast<char*>(w.msgs.data()), static_cast<std::streamsize>(w.msgs.size() * sizeof(Msg)));
    if (!in) throw std::runtime_error(path + ": truncated");
    return w;
}

void write_workload(const std::string& path, const Workload& w) {
    std::ofstream out(path, std::ios::binary);
    WorkloadHeader h = w.header;
    h.count = w.msgs.size();
    out.write(reinterpret_cast<const char*>(&h), sizeof h);
    out.write(reinterpret_cast<const char*>(w.msgs.data()), static_cast<std::streamsize>(w.msgs.size() * sizeof(Msg)));
    if (!out) throw std::runtime_error("cannot write " + path);
}

}  // namespace lob
