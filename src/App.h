#pragma once
//
// App: owns the loaded Trace and all ImGui/ImPlot windows. Everything is keyed
// off a single selected timestamp so the graph cursor, the memory summary, and
// the active-allocations list always describe the same instant.
//
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "Trace.h"
#include "SymbolResolver.h"

namespace dx12track {

class App {
public:
    explicit App(std::string jsonl_path);

    // Called once per frame from the main loop (after any tail polling).
    void Draw();

    // Whether live-tail polling is enabled (read by the main loop each frame).
    bool live_tail() const { return live_tail_; }

    Trace& trace() { return trace_; }

private:
    void DrawMenuBar();
    void DrawTimeline();      // ImPlot memory-over-time graph(s) + cursor
    void DrawSummary();       // grouped memory totals at the selected time
    void DrawAllocations();   // tabbed object tables (active / range deltas)

    // One filtered, sortable object table inside a tab. `include` selects which
    // objects to list; `ref_t` is the time used for the age column; `noun` is
    // the word in the "N <noun>" summary line.
    void DrawAllocTable(const char* id, const char* noun, uint64_t ref_t,
                        const std::function<bool(const Obj&)>& include);

    // Draw one memory-over-time plot. heap_filter: 0 = all heaps, 1 = device-
    // local only (excludes Upload/Readback), 2 = host-visible only (Upload +
    // Readback). height < 0 fills the available space.
    void DrawTimelinePanel(const char* plot_id, float height, int heap_filter);

    void SetSelected(uint64_t ts);
    double SelectedSeconds() const;

    // Multi-select dropdown over the distinct values in `sel` (value -> shown).
    void FilterCombo(const char* label, std::map<std::string, bool>& sel);

    enum class PlotMode { Total, ByHeap, ByAlloc };

    Trace        trace_;
    bool         loaded_      = false;
    uint64_t     selected_ts_ = 0;
    PlotMode     mode_        = PlotMode::Total;
    bool         live_tail_   = false;
    bool         show_counts_ = false;
    bool         split_host_  = true; // Upload/Readback in a second graph

    // Shared X-axis range so the split graphs pan/zoom together.
    double       xlink_min_   = 0.0;
    double       xlink_max_   = 0.0;
    bool         xlink_valid_ = false;

    // On-demand callstack resolution for the clicked allocation.
    SymbolResolver            resolver_;
    uint64_t                  picked_id_   = 0;     // 0 = none picked
    std::string               picked_label_;        // "id 12  Texture"
    bool                      picked_has_stack_ = false;
    std::vector<ResolvedFrame> picked_frames_;

    // Shift-drag time-range selection on the timeline (for leak hunting).
    bool         range_valid_     = false;
    uint64_t     range_start_     = 0;
    uint64_t     range_end_       = 0;
    bool         dragging_range_  = false;
    double       drag_anchor_sec_ = 0.0;

    char         filter_[128] = {};
    bool         first_frame_ = true;

    // Per-column value filters for the allocations table (value -> shown).
    // Seeded with the full set of values dx12track can emit (see EventTypes.h),
    // and topped up from the data; everything defaults to shown.
    std::map<std::string, bool> type_show_, alloc_show_, heap_show_, dim_show_;

    // Scratch buffers reused across frames for ImPlot (avoids per-frame alloc).
    std::vector<double> xs_, ys_, lo_, hi_;
};

} // namespace dx12track
