#pragma once
//
// Trace: parses a dx12track.jsonl log into an in-memory object model plus a
// derived memory-over-time series, and answers "what was live at instant T?"
// queries for the UI. Supports a full load and incremental live-tailing of a
// file that is still being written.
//
// Bucket layout mirrors dx12track/src/launcher/Model.h (MemoryTotals) so the
// summary table matches the console renderer.
//
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dx12track {

// Heap-type buckets: 0 = none/unknown, then the D3D12_HEAP_TYPE order.
constexpr int kHeapBuckets  = 6; // none, Default, Upload, Readback, Custom, GpuUpload
// Allocation-kind buckets matching AllocationKind ordinals.
constexpr int kAllocBuckets = 5; // None, Committed, Placed, Reserved, Heap

int HeapBucket(std::string_view heap);
int AllocBucket(std::string_view alloc);
const char* HeapBucketName(int bucket);
const char* AllocBucketName(int bucket);

struct Obj {
    uint64_t    id             = 0;
    std::string type;          // "Resource", "PipelineState", ...
    std::string alloc;         // "Committed", "Placed", "None", ...
    std::string heap;          // "Default", "Upload", "None", ...
    std::string dim;           // "Buffer", "Tex2D", "Unknown"
    std::string name;
    uint32_t    format         = 0;
    uint64_t    size           = 0;
    uint64_t    parent_heap_id = 0;
    uint64_t    created_ns     = 0;
    uint64_t    destroyed_ns   = UINT64_MAX; // UINT64_MAX while still live
    int         heap_bucket    = 0;
    int         alloc_bucket   = 0;

    bool LiveAt(uint64_t t) const { return created_ns <= t && t < destroyed_ns; }
};

// One step in the memory-over-time series (one per create/destroy event).
struct Sample {
    uint64_t ts_ns        = 0;
    uint64_t total_bytes  = 0;
    uint64_t by_heap [kHeapBuckets]  = {};
    uint64_t by_alloc[kAllocBuckets] = {};
    uint32_t live_count   = 0;
};

// Aggregated state at a chosen instant, for the summary view.
struct MemorySummary {
    uint64_t bytes [kHeapBuckets][kAllocBuckets] = {};
    uint64_t counts[kHeapBuckets][kAllocBuckets] = {};
    uint64_t total_bytes = 0;
    uint64_t live_count  = 0;
    std::map<std::string, uint64_t> per_type; // live object count by ObjectType name
};

class Trace {
public:
    // Full (re)load from disk. Returns false if the file could not be opened.
    bool Load(const std::string& path);
    // Re-read the current file from scratch (keeps the path).
    bool Reload();
    // Ingest any bytes appended since the last read. Returns true if changed.
    bool PollTail();

    // --- header info (from hello / goodbye) ---
    uint32_t    pid          = 0;
    std::string exe;
    uint64_t    qpc_freq     = 0;
    uint64_t    start_ns     = 0;   // first timestamp seen (hello is 0)
    uint64_t    end_ns       = 0;   // last timestamp seen
    bool        child_exited = false;
    uint32_t    exit_code    = 0;

    // --- derived data ---
    const std::vector<Obj>&    objects() const { return objects_; }
    const std::vector<Sample>& samples() const { return samples_; }
    uint64_t    peak_ts_ns()  const { return peak_ts_ns_; }
    uint64_t    peak_bytes()  const { return peak_bytes_; }
    const std::string& path() const { return path_; }
    const std::string& error() const { return error_; }

    // State of the world at instant t (single pass over all objects).
    MemorySummary SummaryAt(uint64_t t) const;

private:
    void Clear();
    void IngestLine(std::string_view line);

    std::string         path_;
    std::string         error_;
    std::vector<Obj>    objects_;
    std::unordered_map<uint64_t, size_t> live_index_; // id -> index while live
    std::vector<Sample> samples_;

    // running aggregates while ingesting, used to build samples_
    uint64_t cur_total_ = 0;
    uint64_t cur_by_heap_[kHeapBuckets]  = {};
    uint64_t cur_by_alloc_[kAllocBuckets] = {};
    uint32_t cur_live_  = 0;
    uint64_t peak_ts_ns_ = 0;
    uint64_t peak_bytes_ = 0;
    bool     have_start_ = false;

    // live-tail state
    uint64_t    read_offset_ = 0; // bytes consumed from the file so far
    std::string partial_;         // trailing line not yet terminated by '\n'
};

} // namespace dx12track
