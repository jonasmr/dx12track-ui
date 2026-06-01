#include "Trace.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

namespace dx12track {

using json = nlohmann::json;

namespace {
// Parse a "0x..." (or bare) hex string to a 64-bit value.
uint64_t ParseHex(const std::string& s) {
    return s.empty() ? 0 : std::strtoull(s.c_str(), nullptr, 16);
}
} // namespace

int HeapBucket(std::string_view h) {
    if (h == "Default")   return 1;
    if (h == "Upload")    return 2;
    if (h == "Readback")  return 3;
    if (h == "Custom")    return 4;
    if (h == "GpuUpload") return 5;
    return 0;
}

int AllocBucket(std::string_view a) {
    if (a == "Committed") return 1;
    if (a == "Placed")    return 2;
    if (a == "Reserved")  return 3;
    if (a == "Heap")      return 4;
    return 0;
}

const char* HeapBucketName(int b) {
    switch (b) {
        case 1: return "Default";
        case 2: return "Upload";
        case 3: return "Readback";
        case 4: return "Custom";
        case 5: return "GpuUpload";
        default: return "(none)";
    }
}

const char* AllocBucketName(int b) {
    switch (b) {
        case 1: return "Committed";
        case 2: return "Placed";
        case 3: return "Reserved";
        case 4: return "Heap";
        default: return "None";
    }
}

void Trace::ResetModelState() {
    objects_.clear();
    live_index_.clear();
    samples_.clear();
    modules_.clear();
    pid = 0;
    protocol = 0;
    exe.clear();
    qpc_freq = 0;
    start_ns = end_ns = 0;
    child_exited = false;
    exit_code = 0;
    cur_total_ = cur_live_ = 0;
    for (auto& v : cur_by_heap_)  v = 0;
    for (auto& v : cur_by_alloc_) v = 0;
    peak_ts_ns_ = peak_bytes_ = 0;
    have_start_ = false;
    ++generation_;
}

void Trace::Clear() {
    ResetModelState();
    read_offset_ = 0;
    partial_.clear();
    error_.clear();
}

bool Trace::Load(const std::string& path) {
    path_ = path;
    return Reload();
}

bool Trace::Reload() {
    Clear();
    std::ifstream f(path_, std::ios::binary);
    if (!f) {
        error_ = "Could not open " + path_;
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    read_offset_ = content.size();

    size_t pos = 0;
    while (pos < content.size()) {
        size_t nl = content.find('\n', pos);
        if (nl == std::string::npos) {
            // Trailing partial line (file not newline-terminated yet).
            partial_.assign(content, pos, content.size() - pos);
            read_offset_ -= partial_.size();
            break;
        }
        IngestLine(std::string_view(content).substr(pos, nl - pos));
        pos = nl + 1;
    }
    return true;
}

bool Trace::PollTail() {
    if (path_.empty()) return false;
    std::ifstream f(path_, std::ios::binary | std::ios::ate);
    if (!f) return false;
    uint64_t size = (uint64_t)f.tellg();
    if (size < read_offset_) {
        // The file shrank: the previous capture's file was truncated/replaced
        // by a new run. Re-read from scratch (drops the old capture's state).
        f.close();
        return Reload();
    }
    if (size == read_offset_) return false; // nothing new

    f.seekg((std::streamoff)read_offset_, std::ios::beg);
    std::string chunk((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    read_offset_ += chunk.size();

    std::string buf;
    buf.swap(partial_);
    buf += chunk;

    size_t pos = 0;
    bool changed = false;
    while (true) {
        size_t nl = buf.find('\n', pos);
        if (nl == std::string::npos) {
            partial_.assign(buf, pos, buf.size() - pos);
            break;
        }
        IngestLine(std::string_view(buf).substr(pos, nl - pos));
        changed = true;
        pos = nl + 1;
    }
    return changed;
}

void Trace::IngestLine(std::string_view line) {
    // Trim a trailing '\r' (CRLF logs) and skip blank lines.
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.remove_suffix(1);
    if (line.empty()) return;

    json j = json::parse(line, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return;

    const std::string ev = j.value("event", std::string());
    const uint64_t ts = j.value("ts_ns", (uint64_t)0);

    if (ev == "hello") {
        // A hello after we've already seen one means a new capture session
        // began (e.g. the process restarted while tailing) — drop the old data.
        if (have_start_) ResetModelState();
        pid      = j.value("pid", 0u);
        protocol = j.value("protocol", 0u);
        qpc_freq = j.value("qpc_freq", (uint64_t)0);
        exe      = j.value("exe", std::string());
        start_ns = ts;
        end_ns   = ts;
        have_start_ = true;
        return;
    }
    if (ev == "goodbye") {
        child_exited = true;
        exit_code    = j.value("exit_code", 0u);
        end_ns       = std::max(end_ns, ts);
        return;
    }
    if (ev == "module_loaded") {       // protocol 2+: for symbol resolution
        Module m;
        m.base         = ParseHex(j.value("base", std::string()));
        m.size         = j.value("size", (uint64_t)0);
        m.pe_timestamp = j.value("timestamp", 0u);
        m.pdb_age      = j.value("pdb_age", 0u);
        m.pdb_guid     = j.value("pdb_guid", std::string());
        m.name         = j.value("name", std::string());
        m.pdb_name     = j.value("pdb_name", std::string());
        modules_.push_back(std::move(m));
        return;
    }
    if (ev == "module_unloaded") {
        // Keep the record for offline resolution; base reuse is rare in a
        // single trace and we want to resolve addresses captured earlier.
        return;
    }

    if (!have_start_) { start_ns = ts; have_start_ = true; }
    end_ns = std::max(end_ns, ts);

    if (ev == "created") {
        Obj o;
        o.id             = j.value("id", (uint64_t)0);
        o.type           = j.value("type", std::string("Unknown"));
        o.alloc          = j.value("alloc", std::string("None"));
        o.heap           = j.value("heap", std::string("None"));
        o.dim            = j.value("dim", std::string("Unknown"));
        o.name           = j.value("name", std::string());
        o.format         = j.value("format", 0u);
        o.size           = j.value("size", (uint64_t)0);
        o.parent_heap_id = j.value("parent_heap_id", (uint64_t)0);
        o.created_ns     = ts;
        o.heap_bucket    = HeapBucket(o.heap);
        o.alloc_bucket   = AllocBucket(o.alloc);

        if (auto it = j.find("stack"); it != j.end() && it->is_array()) {
            o.stack.reserve(it->size());
            for (const auto& frame : *it)
                if (frame.is_string()) o.stack.push_back(ParseHex(frame.get<std::string>()));
        }

        size_t idx = objects_.size();
        objects_.push_back(std::move(o));
        const Obj& ref = objects_[idx];
        live_index_[ref.id] = idx;

        ++cur_live_;
        // Only count actual backing memory (Committed/Heap); Placed resources
        // alias into a heap and Reserved resources are virtual.
        if (ref.size > 0 && AllocCountsAsMemory(ref.alloc_bucket)) {
            cur_total_ += ref.size;
            cur_by_heap_[ref.heap_bucket]   += ref.size;
            cur_by_alloc_[ref.alloc_bucket] += ref.size;
        }
        if (cur_total_ > peak_bytes_) { peak_bytes_ = cur_total_; peak_ts_ns_ = ts; }

    } else if (ev == "destroyed") {
        uint64_t id = j.value("id", (uint64_t)0);
        auto it = live_index_.find(id);
        if (it == live_index_.end()) return; // unknown / double free
        Obj& o = objects_[it->second];
        o.destroyed_ns = ts;
        if (o.size > 0 && AllocCountsAsMemory(o.alloc_bucket)) {
            cur_total_ -= o.size;
            cur_by_heap_[o.heap_bucket]   -= o.size;
            cur_by_alloc_[o.alloc_bucket] -= o.size;
        }
        if (cur_live_ > 0) --cur_live_;
        live_index_.erase(it);

    } else if (ev == "renamed") {
        uint64_t id = j.value("id", (uint64_t)0);
        auto it = live_index_.find(id);
        if (it != live_index_.end())
            objects_[it->second].name = j.value("name", std::string());
        return; // no memory change -> no sample
    } else {
        return; // unknown event
    }

    // Record a step in the time series for create/destroy events.
    Sample s;
    s.ts_ns       = ts;
    s.total_bytes = cur_total_;
    s.live_count  = cur_live_;
    for (int i = 0; i < kHeapBuckets;  ++i) s.by_heap[i]  = cur_by_heap_[i];
    for (int i = 0; i < kAllocBuckets; ++i) s.by_alloc[i] = cur_by_alloc_[i];
    samples_.push_back(s);
}

const Module* Trace::ModuleForAddress(uint64_t addr) const {
    for (const Module& m : modules_)
        if (m.Contains(addr)) return &m;
    return nullptr;
}

MemorySummary Trace::SummaryAt(uint64_t t) const {
    MemorySummary m;
    for (const Obj& o : objects_) {
        if (!o.LiveAt(t)) continue;
        ++m.live_count;
        m.per_type[o.type]++;
        if (o.size > 0) {
            // Keep the per-cell breakdown for all kinds (incl. Placed/Reserved),
            // but only count actual backing memory in the totals.
            m.bytes [o.heap_bucket][o.alloc_bucket] += o.size;
            m.counts[o.heap_bucket][o.alloc_bucket] += 1;
            if (AllocCountsAsMemory(o.alloc_bucket)) m.total_bytes += o.size;
        }
    }
    return m;
}

} // namespace dx12track
