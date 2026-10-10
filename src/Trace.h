#pragma once
//
// Trace: parses a dx12track.jsonl log into an in-memory object model plus a
// derived memory-over-time series, and answers "what was live at instant T?"
// queries for the UI. Supports a full load and incremental live-tailing of a
// file that is still being written.
//
// If an ETW sidecar (`<log>.etw.jsonl`, written by `dx12track.exe --etw`, see
// dx12track/FORMAT.md) sits next to the main log, it is loaded and tailed too:
// it adds per-object memory location (VRAM / system memory) history, driver
// sizes, page-in/out counts, process video-memory counters and session stats.
//
// Bucket layout mirrors dx12track/src/launcher/Model.h (MemoryTotals) so the
// summary table matches the console renderer.
//
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace dx12track {

// Heap-type buckets: 0 = none/unknown, then the D3D12_HEAP_TYPE order.
constexpr int kHeapBuckets  = 6; // none, Default, Upload, Readback, Custom, GpuUpload
// Allocation-kind buckets matching AllocationKind ordinals.
constexpr int kAllocBuckets   = 5; // None, Committed, Placed, Reserved, Heap
constexpr int kAllocNone      = 0;
constexpr int kAllocCommitted = 1;
constexpr int kAllocPlaced    = 2;
constexpr int kAllocReserved  = 3;
constexpr int kAllocHeap      = 4;

// Residency-priority buckets. 0 = Unset (the app never called
// SetResidencyPriority on the object), then the D3D12_RESIDENCY_PRIORITY tiers.
constexpr int kPrioBuckets = 7; // Unset, Minimum, Low, Normal, High, Maximum, Custom
constexpr int kPrioUnset   = 0;

// Allocation kinds that represent actual backing memory. Committed resources
// and Heap objects own real memory; Placed resources alias into a heap (already
// counted via that heap) and Reserved resources are virtual, so both are
// excluded from memory totals/graphs.
inline bool AllocCountsAsMemory(int alloc_bucket) {
    return alloc_bucket == kAllocCommitted || alloc_bucket == kAllocHeap;
}

// Upload/Readback (heap buckets 2 and 3) are the CPU-visible heaps; everything
// else that counts as memory is device-local. Residency priority only matters
// for device-local memory, so the by-priority graph separates the two.
inline bool IsHostVisibleHeap(int heap_bucket) {
    return heap_bucket == 2 || heap_bucket == 3;
}

int HeapBucket(std::string_view heap);
int AllocBucket(std::string_view alloc);
int PrioBucket(std::string_view priority_name);
const char* HeapBucketName(int bucket);
const char* AllocBucketName(int bucket);
const char* PrioBucketName(int bucket);

// Memory-location buckets, from the ETW sidecar's `location` events. 0 = n/a:
// no location known at that instant (object never bound to an ETW object, no
// sidecar, or before its first `location` line).
constexpr int kLocBuckets = 4; // n/a, VRAM, Sys, Unknown
constexpr int kLocNA      = 0;
constexpr int kLocVram    = 1; // "vram": DXGK local segment group
constexpr int kLocSys     = 2; // "sys": non-local (system memory)
constexpr int kLocUnknown = 3; // "unknown": ETW said it doesn't know

int LocBucket(std::string_view group); // "vram" / "sys" / anything else -> Unknown
const char* LocBucketName(int bucket);  // "n/a", "VRAM", "Sys", "Unknown"

// Sidecar path for a main log: `run.jsonl` -> `run.etw.jsonl`; a path not ending
// in `.jsonl` (case-insensitive) gets `.etw.jsonl` appended. Mirrors
// EtwSidecarPath() in dx12track/src/launcher/Main.cpp.
std::string EtwSidecarPathFor(const std::string& main_log);
// True if `path` names an ETW sidecar (ends in `.etw.jsonl`, case-insensitive).
bool IsEtwSidecarPath(const std::string& path);

// Incremental reader for a line-oriented file that may still be growing. Each
// Poll() reads whatever was appended since the previous one and hands every
// complete line to `on_line`; an unterminated trailing line is held back until
// its '\n' arrives. Used for both the main log and the ETW sidecar.
class LineTail {
public:
    enum class Status {
        Missing,   // file could not be opened
        Unchanged, // nothing new
        Shrunk,    // file is smaller than what was consumed (replaced/truncated);
                   // nothing was read -- the caller should Reset() and re-read
        Read,      // new bytes were consumed
    };
    Status Poll(const std::string& path,
                const std::function<void(std::string_view)>& on_line);
    void Reset() { offset_ = 0; partial_.clear(); }

private:
    uint64_t    offset_ = 0; // bytes consumed from the file so far
    std::string partial_;    // trailing line not yet terminated by '\n'
};

// A module loaded in the traced process (protocol 2+), used to map a stack
// return address back to a binary + PDB for symbol resolution.
struct Module {
    uint64_t    base         = 0;
    uint64_t    size         = 0;
    uint32_t    pe_timestamp = 0;
    uint32_t    pdb_age      = 0;
    std::string pdb_guid;   // e.g. "8c3d35b8-e4f6-4d6e-8f7a-fd384f00bdbb"
    std::string name;       // module path (the .exe/.dll)
    std::string pdb_name;   // PDB path or bare filename
    bool Contains(uint64_t addr) const { return addr >= base && addr < base + size; }
};

// One `location` sidecar line for an object: from ts_ns on, the object's memory
// is in bucket `loc` (kLocVram / kLocSys / kLocUnknown).
struct LocEntry {
    uint64_t ts_ns    = 0;
    int      loc      = kLocUnknown;
    bool     via_heap = false; // placed resource following its parent heap
};

// What the ETW sidecar says about one main-log object.
struct EtwObjInfo {
    std::vector<LocEntry> locations;  // kept sorted by ts_ns (lines arrive unsorted)
    uint64_t driver_size    = 0;      // latest `driver_size` bytes (0 = none)
    uint64_t driver_size_ts = 0;
    uint32_t page_ins       = 0;      // `residency` op counts
    uint32_t page_outs      = 0;
    uint64_t lib_id         = 0;      // ETW library object id (from etw_bind)
    bool     bound          = false;  // an etw_bind referenced this object
    bool     late           = false;  // ...bound after the object was destroyed
};

struct Obj {
    uint64_t    id             = 0;
    uint64_t    ptr            = 0;          // app-visible interface pointer (protocol 4+)
    std::string type;          // "Resource", "PipelineState", ...
    std::string alloc;         // "Committed", "Placed", "None", ...
    std::string heap;          // "Default", "Upload", "None", ...
    std::string dim;           // "Buffer", "Tex2D", "Unknown"
    std::string name;
    uint32_t    format         = 0;
    uint64_t    size           = 0;
    uint64_t    parent_heap_id = 0;
    uint64_t    parent_heap_ptr = 0;        // raw IUnknown* of the parent heap (Placed)
    uint64_t    created_ns     = 0;
    uint64_t    destroyed_ns   = UINT64_MAX; // UINT64_MAX while still live
    int         heap_bucket    = 0;
    int         alloc_bucket   = 0;
    int32_t     priority       = 0;          // raw D3D12_RESIDENCY_PRIORITY value
    std::string priority_name  = "Unset";    // tier name, or "Unset" if never set
    int         prio_bucket    = kPrioUnset; // PrioBucket(priority_name)
    std::vector<uint64_t> stack; // capture-time return addresses (optional)
    EtwObjInfo  etw;             // from the ETW sidecar (empty without one)

    bool LiveAt(uint64_t t) const { return created_ns <= t && t < destroyed_ns; }
    // Latest location entry at or before t, or nullptr (-> n/a).
    const LocEntry* LocationEntryAt(uint64_t t) const;
    // Location bucket at instant t (kLocNA if nothing is known yet).
    int LocationAt(uint64_t t) const {
        const LocEntry* e = LocationEntryAt(t);
        return e ? e->loc : kLocNA;
    }
};

// One step in the memory-over-time series (one per create/destroy event).
struct Sample {
    uint64_t ts_ns        = 0;
    uint64_t total_bytes  = 0;
    uint64_t by_heap [kHeapBuckets]  = {};
    uint64_t by_alloc[kAllocBuckets] = {};
    uint64_t by_prio [kPrioBuckets]  = {}; // all counted memory, by priority
    uint64_t by_prio_dev[kPrioBuckets] = {}; // device-local memory only, by priority
    uint32_t live_count   = 0;
};

// One step in the by-location series (counted memory only). Values hold from
// ts_ns until the next sample's ts_ns. Built from the object model rather than
// at ingest time because location changes arrive out of order.
struct LocSample {
    uint64_t ts_ns = 0;
    uint64_t by_loc    [kLocBuckets] = {}; // all counted memory
    uint64_t by_loc_dev[kLocBuckets] = {}; // device-local heaps only
};

// Session/join statistics from the sidecar's `etw_stats` line (last line).
struct EtwStats {
    bool     present       = false;
    uint64_t ts_ns         = 0;
    uint64_t events        = 0;
    uint64_t events_lost   = 0;
    uint64_t buffers_lost  = 0;
    uint64_t binds         = 0;
    uint64_t late_matches  = 0;
    uint64_t unbound_alive = 0;
    // Every integer field of the line in file order (incl. the above and all
    // unbound_<type> counts), for display.
    std::vector<std::pair<std::string, uint64_t>> all;
};

// One `counters` line: the process's video-memory counters (bytes). A value
// of kNoCounter means the kernel hadn't reported that counter yet.
constexpr uint64_t kNoCounter = UINT64_MAX;
struct EtwCounters {
    uint64_t ts_ns             = 0;
    uint64_t local_budget      = kNoCounter;
    uint64_t local_resident    = kNoCounter;
    uint64_t local_usage       = kNoCounter;
    uint64_t nonlocal_budget   = kNoCounter;
    uint64_t nonlocal_resident = kNoCounter;
    uint64_t nonlocal_usage    = kNoCounter;
    uint64_t demoted           = kNoCounter;
};

// Header of the sidecar (`etw_hello`).
struct EtwHello {
    uint32_t    etw_format = 0;
    uint32_t    pid        = 0;
    uint64_t    qpc_freq   = 0;
    uint64_t    qpc_start  = 0;
    std::string session;
    std::string main_log;
};

// Aggregated state at a chosen instant, for the summary view.
struct MemorySummary {
    uint64_t bytes [kHeapBuckets][kAllocBuckets] = {};
    uint64_t counts[kHeapBuckets][kAllocBuckets] = {};
    uint64_t total_bytes = 0;
    uint64_t live_count  = 0;
    std::map<std::string, uint64_t> per_type; // live object count by ObjectType name
    // Counted memory (AllocCountsAsMemory) by location bucket at t.
    uint64_t loc_bytes [kLocBuckets] = {};
    uint64_t loc_counts[kLocBuckets] = {};
};

class Trace {
public:
    // Full (re)load from disk. Returns false if the file could not be opened.
    // Passing an ETW sidecar (`*.etw.jsonl`) opens its main log instead. The
    // sidecar next to the main log, if any, is loaded alongside.
    bool Load(const std::string& path);
    // Re-read the current file (and its sidecar) from scratch (keeps the path).
    bool Reload();
    // Ingest any bytes appended to the main log or the sidecar since the last
    // read (and pick up a sidecar that appears late). Returns true if changed.
    bool PollTail();

    // --- header info (from hello / goodbye) ---
    // protocol: 1 = no callstacks, 2 = modules + stacks, 3 = residency priority
    // + parent_heap_ptr, 4 = created.ptr + hello.qpc_start (the ETW join keys).
    uint32_t    pid          = 0;
    uint32_t    protocol     = 0;
    std::string exe;
    uint64_t    qpc_freq     = 0;
    uint64_t    qpc_start    = 0;   // QPC at ts_ns 0 (protocol 4+, else 0)
    uint64_t    start_ns     = 0;   // first timestamp seen (hello is 0)
    uint64_t    end_ns       = 0;   // last timestamp seen
    bool        child_exited = false;
    uint32_t    exit_code    = 0;

    // --- derived data ---
    const std::vector<Obj>&    objects() const { return objects_; }
    const std::vector<Sample>& samples() const { return samples_; }
    const std::vector<Module>& modules() const { return modules_; }
    // Module whose address range contains `addr`, or nullptr.
    const Module* ModuleForAddress(uint64_t addr) const;
    // Object with this tracker id (live or destroyed), or nullptr.
    const Obj* ObjectById(uint64_t id) const;
    uint64_t    peak_ts_ns()  const { return peak_ts_ns_; }
    uint64_t    peak_bytes()  const { return peak_bytes_; }
    const std::string& path() const { return path_; }
    const std::string& error() const { return error_; }

    // Bumped every time a fresh capture starts (initial load, reload, or a
    // live-tail restart). The UI watches this to reset its per-trace state.
    uint32_t generation() const { return generation_; }

    // State of the world at instant t (single pass over all objects).
    MemorySummary SummaryAt(uint64_t t) const;

    // --- ETW sidecar ---
    // True once a sidecar file was found for this log (and its etw_hello, if
    // any, matches the main log's run).
    bool has_etw() const { return has_etw_ && !etw_rejected_; }
    // The sidecar's etw_hello names a different run (pid / qpc_start mismatch);
    // its lines are ignored until a matching etw_hello shows up.
    bool etw_rejected() const { return etw_rejected_; }
    const std::string& etw_path() const { return etw_path_; }
    const EtwHello&    etw_hello() const { return etw_hello_; }
    const EtwStats&    etw_stats() const { return etw_stats_; }
    const std::vector<EtwCounters>& etw_counters() const { return etw_counters_; } // by ts
    uint32_t           etw_diag_count() const { return etw_diag_count_; }
    // Counted memory by location over time. Rebuilt lazily (only when the
    // main log or the sidecar changed since the last call).
    const std::vector<LocSample>& LocationSeries() const;

    // Bumped whenever ingested data changes (either file); for caches.
    uint64_t data_version() const { return data_version_; }

private:
    void Clear();
    void ResetModelState(); // drop parsed model/header, keep file-read position
    void IngestLine(std::string_view line);
    // Drop everything learned from the sidecar. rewind: also re-read the
    // sidecar from offset 0 on the next poll.
    void ResetEtwState(bool rewind);
    bool PollEtw();         // tail the sidecar; true if anything was ingested
    void IngestEtwLine(std::string_view line);
    // Sidecar info for a main-log id: the object's, or a pending entry if its
    // `created` hasn't arrived yet.
    EtwObjInfo& EtwInfoFor(uint64_t id);

    std::string         path_;
    std::string         error_;
    std::vector<Obj>    objects_;
    std::unordered_map<uint64_t, size_t> live_index_; // id -> index while live
    std::unordered_map<uint64_t, size_t> id_index_;   // id -> index, persistent
    // residency_priority events that arrived before their object's `created`
    // (loose cross-thread ordering); applied when the object shows up.
    std::unordered_map<uint64_t, std::pair<int32_t, std::string>> pending_prio_;
    std::vector<Sample> samples_;
    std::vector<Module> modules_;

    // running aggregates while ingesting, used to build samples_
    uint64_t cur_total_ = 0;
    uint64_t cur_by_heap_[kHeapBuckets]  = {};
    uint64_t cur_by_alloc_[kAllocBuckets] = {};
    uint64_t cur_by_prio_[kPrioBuckets]  = {};
    uint64_t cur_by_prio_dev_[kPrioBuckets] = {};
    uint32_t cur_live_  = 0;
    uint64_t peak_ts_ns_ = 0;
    uint64_t peak_bytes_ = 0;
    bool     have_start_ = false;

    uint32_t    generation_  = 0; // incremented on each fresh capture
    bool        hello_seen_  = false; // main-log hello parsed (gates the sidecar)
    uint64_t    data_version_ = 0;

    // live-tail state
    LineTail    main_tail_;

    // --- ETW sidecar state ---
    std::string etw_path_;
    LineTail    etw_tail_;
    bool        has_etw_        = false; // sidecar file opened
    bool        etw_rejected_   = false; // etw_hello is from another run
    bool        etw_hello_seen_ = false;
    EtwHello    etw_hello_;
    EtwStats    etw_stats_;
    std::vector<EtwCounters> etw_counters_;
    uint32_t    etw_diag_count_ = 0;
    // Sidecar info for ids whose `created` hasn't been read yet (the two files
    // are tailed independently); moved onto the object when it shows up.
    std::unordered_map<uint64_t, EtwObjInfo> pending_etw_;

    // LocationSeries() cache
    mutable std::vector<LocSample> loc_series_;
    mutable uint64_t loc_series_version_ = UINT64_MAX;
};

} // namespace dx12track
