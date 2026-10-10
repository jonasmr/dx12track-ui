#include "Trace.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace dx12track {

using json = nlohmann::json;

namespace {
// Parse a "0x..." (or bare) hex string to a 64-bit value.
uint64_t ParseHex(const std::string& s) {
    return s.empty() ? 0 : std::strtoull(s.c_str(), nullptr, 16);
}

bool EndsWithNoCase(const std::string& s, std::string_view suffix) {
    if (s.size() < suffix.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        const char a = s[s.size() - suffix.size() + i];
        if (std::tolower((unsigned char)a) != std::tolower((unsigned char)suffix[i]))
            return false;
    }
    return true;
}

bool FileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(std::filesystem::path(path), ec);
}
} // namespace

std::string EtwSidecarPathFor(const std::string& main_log) {
    if (main_log.empty()) return {};
    if (EndsWithNoCase(main_log, ".jsonl"))
        return main_log.substr(0, main_log.size() - 6) + ".etw.jsonl";
    return main_log + ".etw.jsonl";
}

bool IsEtwSidecarPath(const std::string& path) {
    return EndsWithNoCase(path, ".etw.jsonl");
}

LineTail::Status LineTail::Poll(const std::string& path,
                                const std::function<void(std::string_view)>& on_line) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return Status::Missing;
    const uint64_t size = (uint64_t)f.tellg();
    if (size < offset_) return Status::Shrunk;
    if (size == offset_) return Status::Unchanged;

    f.seekg((std::streamoff)offset_, std::ios::beg);
    std::string chunk((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    offset_ += chunk.size();

    std::string buf;
    buf.swap(partial_);
    buf += chunk;

    size_t pos = 0;
    while (true) {
        size_t nl = buf.find('\n', pos);
        if (nl == std::string::npos) {
            partial_.assign(buf, pos, buf.size() - pos);
            break;
        }
        on_line(std::string_view(buf).substr(pos, nl - pos));
        pos = nl + 1;
    }
    return Status::Read;
}

int LocBucket(std::string_view g) {
    if (g == "vram") return kLocVram;
    if (g == "sys")  return kLocSys;
    return kLocUnknown; // "unknown" / anything unrecognized
}

const char* LocBucketName(int b) {
    switch (b) {
        case kLocVram:    return "VRAM";
        case kLocSys:     return "Sys";
        case kLocUnknown: return "Unknown";
        default:          return "n/a";
    }
}

const LocEntry* Obj::LocationEntryAt(uint64_t t) const {
    const auto& v = etw.locations;
    // First entry with ts > t; the one before it is in effect at t.
    auto it = std::upper_bound(v.begin(), v.end(), t,
        [](uint64_t x, const LocEntry& e) { return x < e.ts_ns; });
    return it == v.begin() ? nullptr : &*(it - 1);
}

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

int PrioBucket(std::string_view p) {
    if (p == "Minimum") return 1;
    if (p == "Low")     return 2;
    if (p == "Normal")  return 3;
    if (p == "High")    return 4;
    if (p == "Maximum") return 5;
    if (p == "Custom")  return 6;
    return kPrioUnset; // "" / "Unset" / anything unrecognized
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

const char* PrioBucketName(int b) {
    switch (b) {
        case 1: return "Minimum";
        case 2: return "Low";
        case 3: return "Normal";
        case 4: return "High";
        case 5: return "Maximum";
        case 6: return "Custom";
        default: return "Unset";
    }
}

void Trace::ResetModelState() {
    objects_.clear();
    live_index_.clear();
    id_index_.clear();
    pending_prio_.clear();
    samples_.clear();
    modules_.clear();
    pid = 0;
    protocol = 0;
    exe.clear();
    qpc_freq = 0;
    qpc_start = 0;
    start_ns = end_ns = 0;
    child_exited = false;
    exit_code = 0;
    cur_total_ = cur_live_ = 0;
    for (auto& v : cur_by_heap_)  v = 0;
    for (auto& v : cur_by_alloc_) v = 0;
    for (auto& v : cur_by_prio_)  v = 0;
    for (auto& v : cur_by_prio_dev_) v = 0;
    peak_ts_ns_ = peak_bytes_ = 0;
    have_start_ = false;
    hello_seen_ = false;
    ++generation_;
    // A fresh capture: sidecar data from the old run must never attach to the
    // new run's objects (ids restart), so drop it and re-read the sidecar.
    ResetEtwState(/*rewind=*/true);
}

void Trace::ResetEtwState(bool rewind) {
    pending_etw_.clear();
    for (Obj& o : objects_) o.etw = EtwObjInfo{};
    etw_hello_      = EtwHello{};
    etw_stats_      = EtwStats{};
    etw_counters_.clear();
    etw_diag_count_ = 0;
    etw_hello_seen_ = false;
    etw_rejected_   = false;
    ++data_version_;
    if (rewind) {
        etw_tail_.Reset();
        has_etw_ = false;
    }
}

void Trace::Clear() {
    ResetModelState();
    main_tail_.Reset();
    error_.clear();
}

bool Trace::Load(const std::string& path) {
    path_ = path;
    // Opening a sidecar directly opens its main log (which loads the sidecar).
    // `run.etw.jsonl` -> `run.jsonl`; for a sidecar of a main log that didn't
    // end in .jsonl (`run.log.etw.jsonl`), fall back to `run.log`.
    if (IsEtwSidecarPath(path)) {
        const std::string base = path.substr(0, path.size() - 10); // strip ".etw.jsonl"
        const std::string main_log = base + ".jsonl";
        if (FileExists(main_log) || !FileExists(base)) path_ = main_log;
        else                                           path_ = base;
    }
    return Reload();
}

bool Trace::Reload() {
    Clear();
    etw_path_ = EtwSidecarPathFor(path_);
    const auto st = main_tail_.Poll(path_, [this](std::string_view l) { IngestLine(l); });
    if (st == LineTail::Status::Missing) {
        error_ = "Could not open " + path_;
        return false;
    }
    PollEtw();
    return true;
}

bool Trace::PollTail() {
    if (path_.empty()) return false;
    bool changed = false;
    switch (main_tail_.Poll(path_, [this](std::string_view l) { IngestLine(l); })) {
        case LineTail::Status::Shrunk:
            // The file shrank: the previous capture's file was truncated/replaced
            // by a new run. Re-read from scratch (drops the old capture's state,
            // sidecar included).
            return Reload();
        case LineTail::Status::Read:
            changed = true;
            break;
        default:
            break;
    }
    if (PollEtw()) changed = true;
    return changed;
}

bool Trace::PollEtw() {
    // Wait for the main log's hello: it identifies the run the sidecar must
    // belong to (and a hello mid-stream rewinds the sidecar anyway).
    if (etw_path_.empty() || !hello_seen_) return false;
    auto ingest = [this](std::string_view l) { IngestEtwLine(l); };
    // A missing sidecar is re-checked on every poll (one failed open): the
    // launcher may create it a little after the main log.
    LineTail::Status st = etw_tail_.Poll(etw_path_, ingest);
    if (st == LineTail::Status::Missing) return false;
    const bool was_loaded = has_etw_;
    has_etw_ = true;
    if (st == LineTail::Status::Shrunk) {
        // Sidecar truncated/replaced (new run): start over from offset 0.
        ResetEtwState(/*rewind=*/true);
        has_etw_ = true;
        etw_tail_.Poll(etw_path_, ingest);
        return true;
    }
    return st == LineTail::Status::Read || !was_loaded;
}

void Trace::IngestLine(std::string_view line) {
    // Trim a trailing '\r' (CRLF logs) and skip blank lines.
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.remove_suffix(1);
    if (line.empty()) return;

    json j = json::parse(line, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return;
    ++data_version_;

    const std::string ev = j.value("event", std::string());
    const uint64_t ts = j.value("ts_ns", (uint64_t)0);

    if (ev == "hello") {
        // A hello after we've already seen one means a new capture session
        // began (e.g. the process restarted while tailing) — drop the old data.
        if (have_start_) ResetModelState();
        pid       = j.value("pid", 0u);
        protocol  = j.value("protocol", 0u);
        qpc_freq  = j.value("qpc_freq", (uint64_t)0);
        qpc_start = j.value("qpc_start", (uint64_t)0); // protocol 4+
        exe       = j.value("exe", std::string());
        start_ns = ts;
        end_ns   = ts;
        have_start_ = true;
        hello_seen_ = true;
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
        o.ptr            = ParseHex(j.value("ptr", std::string())); // protocol 4+
        o.type           = j.value("type", std::string("Unknown"));
        o.alloc          = j.value("alloc", std::string("None"));
        o.heap           = j.value("heap", std::string("None"));
        o.dim            = j.value("dim", std::string("Unknown"));
        o.name           = j.value("name", std::string());
        o.format         = j.value("format", 0u);
        o.size           = j.value("size", (uint64_t)0);
        o.parent_heap_id = j.value("parent_heap_id", (uint64_t)0);
        o.parent_heap_ptr = ParseHex(j.value("parent_heap_ptr", std::string()));
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
        Obj& ref = objects_[idx];
        live_index_[ref.id] = idx;
        id_index_[ref.id]   = idx;

        // A residency_priority event may have arrived before this `created`.
        if (auto pit = pending_prio_.find(ref.id); pit != pending_prio_.end()) {
            ref.priority      = pit->second.first;
            ref.priority_name = pit->second.second;
            ref.prio_bucket   = PrioBucket(ref.priority_name);
            pending_prio_.erase(pit);
        }
        // Sidecar lines may reference the id before its `created` was read.
        if (auto eit = pending_etw_.find(ref.id); eit != pending_etw_.end()) {
            ref.etw = std::move(eit->second);
            pending_etw_.erase(eit);
        }

        ++cur_live_;
        // Only count actual backing memory (Committed/Heap); Placed resources
        // alias into a heap and Reserved resources are virtual.
        if (ref.size > 0 && AllocCountsAsMemory(ref.alloc_bucket)) {
            cur_total_ += ref.size;
            cur_by_heap_[ref.heap_bucket]   += ref.size;
            cur_by_alloc_[ref.alloc_bucket] += ref.size;
            cur_by_prio_[ref.prio_bucket]   += ref.size;
            if (!IsHostVisibleHeap(ref.heap_bucket))
                cur_by_prio_dev_[ref.prio_bucket] += ref.size;
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
            cur_by_prio_[o.prio_bucket]   -= o.size;
            if (!IsHostVisibleHeap(o.heap_bucket))
                cur_by_prio_dev_[o.prio_bucket] -= o.size;
        }
        if (cur_live_ > 0) --cur_live_;
        live_index_.erase(it);

    } else if (ev == "residency_priority") {
        uint64_t    id    = j.value("id", (uint64_t)0);
        int32_t     prio  = j.value("priority", 0);
        std::string pname = j.value("priority_name", std::string());
        if (pname.empty()) pname = "Unset";
        const int new_bucket = PrioBucket(pname);

        // id == 0 means the pageable's vtable wasn't registered -> can't attribute.
        if (id == 0) return;
        auto it = id_index_.find(id);
        if (it == id_index_.end()) {
            // Arrived before the object's `created`; apply when it shows up.
            pending_prio_[id] = {prio, pname};
            return; // no sample yet
        }
        Obj& o = objects_[it->second];
        const int old_bucket = o.prio_bucket;
        o.priority      = prio;
        o.priority_name = pname;
        o.prio_bucket   = new_bucket;
        // Shift its counted bytes between priority buckets (only while still live
        // and memory-bearing; once destroyed the bytes are already removed).
        if (old_bucket != new_bucket && o.destroyed_ns == UINT64_MAX &&
            o.size > 0 && AllocCountsAsMemory(o.alloc_bucket)) {
            cur_by_prio_[old_bucket] -= o.size;
            cur_by_prio_[new_bucket] += o.size;
            if (!IsHostVisibleHeap(o.heap_bucket)) {
                cur_by_prio_dev_[old_bucket] -= o.size;
                cur_by_prio_dev_[new_bucket] += o.size;
            }
        }
        // fall through: the priority partition changed -> record a sample.

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
    for (int i = 0; i < kPrioBuckets;  ++i) s.by_prio[i]  = cur_by_prio_[i];
    for (int i = 0; i < kPrioBuckets;  ++i) s.by_prio_dev[i] = cur_by_prio_dev_[i];
    samples_.push_back(s);
}

const Module* Trace::ModuleForAddress(uint64_t addr) const {
    for (const Module& m : modules_)
        if (m.Contains(addr)) return &m;
    return nullptr;
}

const Obj* Trace::ObjectById(uint64_t id) const {
    auto it = id_index_.find(id);
    return it == id_index_.end() ? nullptr : &objects_[it->second];
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
            if (AllocCountsAsMemory(o.alloc_bucket)) {
                m.total_bytes += o.size;
                const int loc = o.LocationAt(t);
                m.loc_bytes [loc] += o.size;
                m.loc_counts[loc] += 1;
            }
        }
    }
    return m;
}

EtwObjInfo& Trace::EtwInfoFor(uint64_t id) {
    if (auto it = id_index_.find(id); it != id_index_.end())
        return objects_[it->second].etw;
    return pending_etw_[id]; // applied when the object's `created` arrives
}

void Trace::IngestEtwLine(std::string_view line) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.remove_suffix(1);
    if (line.empty()) return;

    json j = json::parse(line, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) return;

    const std::string ev = j.value("event", std::string());
    const uint64_t ts = j.value("ts_ns", (uint64_t)0);

    if (ev == "etw_hello") {
        // A second etw_hello means the sidecar was restarted for a new session.
        if (etw_hello_seen_) ResetEtwState(/*rewind=*/false);
        etw_hello_seen_       = true;
        etw_hello_.etw_format = j.value("etw_format", 0u);
        etw_hello_.pid        = j.value("pid", 0u);
        etw_hello_.qpc_freq   = j.value("qpc_freq", (uint64_t)0);
        etw_hello_.qpc_start  = j.value("qpc_start", (uint64_t)0);
        etw_hello_.session    = j.value("session", std::string());
        etw_hello_.main_log   = j.value("main_log", std::string());
        // pid / qpc_start are copied from the main log's hello. A mismatch means
        // a stale sidecar from another run (e.g. not yet rewritten after a
        // restart): ignore it rather than attach its ids to this run's objects.
        etw_rejected_ =
            (pid && etw_hello_.pid && pid != etw_hello_.pid) ||
            (qpc_start && etw_hello_.qpc_start && qpc_start != etw_hello_.qpc_start);
        ++data_version_;
        return;
    }
    if (etw_rejected_) return;
    ++data_version_;

    if (ev == "location") {
        const uint64_t id = j.value("id", (uint64_t)0);
        if (id == 0) return;
        LocEntry e;
        e.ts_ns    = ts;
        e.loc      = LocBucket(j.value("group", std::string()));
        e.via_heap = j.value("via", std::string()) == "heap";
        // Lines aren't sorted by ts_ns: insert in order (after equal stamps, so
        // the later line wins a tie).
        auto& v = EtwInfoFor(id).locations;
        auto it = std::upper_bound(v.begin(), v.end(), ts,
            [](uint64_t x, const LocEntry& le) { return x < le.ts_ns; });
        v.insert(it, e);
    } else if (ev == "etw_bind") {
        const uint64_t id = j.value("id", (uint64_t)0);
        if (id == 0) return;
        EtwObjInfo& info = EtwInfoFor(id);
        info.bound  = true;
        info.lib_id = j.value("lib_id", (uint64_t)0);
        if (j.value("late", false)) info.late = true;
    } else if (ev == "driver_size") {
        const uint64_t id = j.value("id", (uint64_t)0);
        if (id == 0) return;
        EtwObjInfo& info = EtwInfoFor(id);
        if (info.driver_size == 0 || ts >= info.driver_size_ts) { // keep the latest
            info.driver_size    = j.value("bytes", (uint64_t)0);
            info.driver_size_ts = ts;
        }
    } else if (ev == "residency") {
        const uint64_t id = j.value("id", (uint64_t)0);
        if (id == 0) return; // unbound object
        const std::string op = j.value("op", std::string());
        EtwObjInfo& info = EtwInfoFor(id);
        if (op == "page_in")       ++info.page_ins;
        else if (op == "page_out") ++info.page_outs;
    } else if (ev == "counters") {
        EtwCounters c;
        c.ts_ns             = ts;
        c.local_budget      = j.value("local_budget",      kNoCounter);
        c.local_resident    = j.value("local_resident",    kNoCounter);
        c.local_usage       = j.value("local_usage",       kNoCounter);
        c.nonlocal_budget   = j.value("nonlocal_budget",   kNoCounter);
        c.nonlocal_resident = j.value("nonlocal_resident", kNoCounter);
        c.nonlocal_usage    = j.value("nonlocal_usage",    kNoCounter);
        c.demoted           = j.value("demoted",           kNoCounter);
        auto it = std::upper_bound(etw_counters_.begin(), etw_counters_.end(), ts,
            [](uint64_t x, const EtwCounters& ec) { return x < ec.ts_ns; });
        etw_counters_.insert(it, c);
    } else if (ev == "etw_stats") {
        EtwStats s;
        s.present = true;
        s.ts_ns   = ts;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.key() == "ts_ns" || !it.value().is_number_integer()) continue;
            const uint64_t v = it.value().get<uint64_t>();
            s.all.emplace_back(it.key(), v);
            if      (it.key() == "events")        s.events        = v;
            else if (it.key() == "events_lost")   s.events_lost   = v;
            else if (it.key() == "buffers_lost")  s.buffers_lost  = v;
            else if (it.key() == "binds")         s.binds         = v;
            else if (it.key() == "late_matches")  s.late_matches  = v;
            else if (it.key() == "unbound_alive") s.unbound_alive = v;
        }
        etw_stats_ = std::move(s);
    } else if (ev == "etw_diag") {
        ++etw_diag_count_;
    }
    // etw_object / etw_object_destroyed: only meaningful once bound, which
    // etw_bind / location already cover.
}

const std::vector<LocSample>& Trace::LocationSeries() const {
    if (loc_series_version_ == data_version_) return loc_series_;
    loc_series_version_ = data_version_;
    loc_series_.clear();

    // One delta per create / location change / destroy of a counted object.
    // from/to = -1 means "not counted" (before creation / after destruction).
    struct Delta { uint64_t ts; int from; int to; uint64_t size; bool dev; };
    std::vector<Delta> deltas;
    deltas.reserve(objects_.size() * 2);
    for (const Obj& o : objects_) {
        if (o.size == 0 || !AllocCountsAsMemory(o.alloc_bucket)) continue;
        const bool dev = !IsHostVisibleHeap(o.heap_bucket);
        int cur = o.LocationAt(o.created_ns);
        deltas.push_back({o.created_ns, -1, cur, o.size, dev});
        for (const LocEntry& e : o.etw.locations) {
            if (e.ts_ns <= o.created_ns) continue; // folded into `cur`
            if (e.ts_ns >= o.destroyed_ns) break;
            if (e.loc == cur) continue;
            deltas.push_back({e.ts_ns, cur, e.loc, o.size, dev});
            cur = e.loc;
        }
        if (o.destroyed_ns != UINT64_MAX)
            deltas.push_back({o.destroyed_ns, cur, -1, o.size, dev});
    }
    std::stable_sort(deltas.begin(), deltas.end(),
                     [](const Delta& a, const Delta& b) { return a.ts < b.ts; });

    // Sweep in time order; one sample per distinct timestamp.
    LocSample cur;
    for (size_t i = 0; i < deltas.size();) {
        const uint64_t ts = deltas[i].ts;
        for (; i < deltas.size() && deltas[i].ts == ts; ++i) {
            const Delta& d = deltas[i];
            if (d.from >= 0) {
                cur.by_loc[d.from] -= d.size;
                if (d.dev) cur.by_loc_dev[d.from] -= d.size;
            }
            if (d.to >= 0) {
                cur.by_loc[d.to] += d.size;
                if (d.dev) cur.by_loc_dev[d.to] += d.size;
            }
        }
        cur.ts_ns = ts;
        loc_series_.push_back(cur);
    }
    return loc_series_;
}

} // namespace dx12track
