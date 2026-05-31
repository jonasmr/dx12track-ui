#pragma once
//
// App: owns the loaded Trace and all ImGui/ImPlot windows. Everything is keyed
// off a single selected timestamp so the graph cursor, the memory summary, and
// the active-allocations list always describe the same instant.
//
#include <cstdint>
#include <string>
#include <vector>

#include "Trace.h"

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
    void DrawTimeline();      // ImPlot memory-over-time graph + cursor
    void DrawSummary();       // grouped memory totals at the selected time
    void DrawAllocations();   // table of all objects live at the selected time

    void SetSelected(uint64_t ts);
    double SelectedSeconds() const;

    enum class PlotMode { Total, ByHeap, ByAlloc };

    Trace        trace_;
    bool         loaded_      = false;
    uint64_t     selected_ts_ = 0;
    PlotMode     mode_        = PlotMode::Total;
    bool         live_tail_   = false;
    bool         show_counts_ = false;

    char         filter_[128] = {};
    bool         first_frame_ = true;

    // Scratch buffers reused across frames for ImPlot (avoids per-frame alloc).
    std::vector<double> xs_, ys_, lo_, hi_;
};

} // namespace dx12track
