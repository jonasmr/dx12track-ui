#include "App.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "implot.h"

#include "Format.h"

namespace dx12track {

namespace {

int ByteAxisFormatter(double value, char* buff, int size, void* /*user*/) {
    uint64_t v = value <= 0.0 ? 0 : (uint64_t)value;
    std::string s = FormatBytes(v);
    return std::snprintf(buff, (size_t)size, "%s", s.c_str());
}

std::string ToLower(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

} // namespace

App::App(std::string jsonl_path) {
    loaded_ = trace_.Load(jsonl_path);
}

double App::SelectedSeconds() const {
    return NsToSeconds(selected_ts_, trace_.start_ns);
}

void App::SetSelected(uint64_t ts) {
    ts = std::clamp(ts, trace_.start_ns, trace_.end_ns ? trace_.end_ns : trace_.start_ns);
    selected_ts_ = ts;
}

void App::Draw() {
    if (first_frame_) {
        // Default the cursor to peak memory — the sample log is a clean
        // shutdown (everything freed at the end), so the end is uninteresting.
        SetSelected(trace_.peak_ts_ns() ? trace_.peak_ts_ns() : trace_.end_ns);
        first_frame_ = false;
    }

    DrawMenuBar();
    DrawTimeline();
    DrawSummary();
    DrawAllocations();
}

void App::DrawMenuBar() {
    ImGui::Begin("dx12track");

    if (!loaded_) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Load failed: %s",
                           trace_.error().c_str());
    }

    ImGui::Text("exe : %s", trace_.exe.empty() ? "(unknown)" : trace_.exe.c_str());
    ImGui::Text("pid : %u", trace_.pid);
    ImGui::Text("uptime : %s", FormatNs(trace_.end_ns - trace_.start_ns).c_str());
    ImGui::Text("events : %zu objects, %zu samples",
                trace_.objects().size(), trace_.samples().size());
    ImGui::Text("peak : %s @ %s", FormatBytes(trace_.peak_bytes()).c_str(),
                FormatNs(trace_.peak_ts_ns() - trace_.start_ns).c_str());

    if (trace_.child_exited)
        ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "child exited (code %u)",
                           trace_.exit_code);

    ImGui::Separator();
    ImGui::TextWrapped("file: %s", trace_.path().c_str());
    if (ImGui::Button("Reload")) {
        loaded_ = trace_.Reload();
        first_frame_ = true; // re-snap cursor to peak
    }
    ImGui::SameLine();
    ImGui::Checkbox("Live tail", &live_tail_);

    ImGui::End();
}

void App::DrawTimeline() {
    ImGui::Begin("Memory over time");

    ImGui::RadioButton("Total", (int*)&mode_, (int)PlotMode::Total);
    ImGui::SameLine();
    ImGui::RadioButton("By heap", (int*)&mode_, (int)PlotMode::ByHeap);
    ImGui::SameLine();
    ImGui::RadioButton("By alloc kind", (int*)&mode_, (int)PlotMode::ByAlloc);
    ImGui::SameLine();
    if (ImGui::Button("Jump to peak")) SetSelected(trace_.peak_ts_ns());
    ImGui::SameLine();
    if (ImGui::Button("Jump to end")) SetSelected(trace_.end_ns);

    const auto& samples = trace_.samples();
    if (samples.empty()) {
        ImGui::TextUnformatted("No memory events in this trace.");
        ImGui::End();
        return;
    }

    const size_t N = samples.size();
    xs_.resize(N);
    for (size_t i = 0; i < N; ++i)
        xs_[i] = NsToSeconds(samples[i].ts_ns, trace_.start_ns);

    if (ImPlot::BeginPlot("##mem", ImVec2(-1, -1))) {
        ImPlot::SetupAxes("time (s)", "memory",
                          ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxisFormat(ImAxis_Y1, ByteAxisFormatter);
        ImPlot::SetupLegend(ImPlotLocation_NorthWest);

        if (mode_ == PlotMode::Total) {
            ys_.resize(N);
            for (size_t i = 0; i < N; ++i) ys_[i] = (double)samples[i].total_bytes;
            ImPlotSpec fill;
            fill.FillAlpha = 0.25f;
            ImPlot::PlotShaded("Total", xs_.data(), ys_.data(), (int)N, 0.0, fill);
            ImPlot::PlotStairs("Total", xs_.data(), ys_.data(), (int)N);
        } else {
            std::vector<double> baseline(N, 0.0);
            const int first = 1;
            const int last  = (mode_ == PlotMode::ByHeap) ? kHeapBuckets : kAllocBuckets;
            ImPlotSpec fill;
            fill.FillAlpha = 0.65f;
            for (int b = first; b < last; ++b) {
                lo_.resize(N);
                hi_.resize(N);
                bool any = false;
                for (size_t i = 0; i < N; ++i) {
                    uint64_t v = (mode_ == PlotMode::ByHeap) ? samples[i].by_heap[b]
                                                             : samples[i].by_alloc[b];
                    lo_[i] = baseline[i];
                    hi_[i] = baseline[i] + (double)v;
                    baseline[i] = hi_[i];
                    if (v) any = true;
                }
                if (!any) continue; // don't clutter the legend with empty bands
                const char* name = (mode_ == PlotMode::ByHeap) ? HeapBucketName(b)
                                                               : AllocBucketName(b);
                ImPlot::PlotShaded(name, xs_.data(), lo_.data(), hi_.data(), (int)N, fill);
            }
        }

        // Selected-time cursor: draggable, and click-to-place anywhere.
        double sx = SelectedSeconds();
        if (ImPlot::DragLineX(1, &sx, ImVec4(1, 1, 0, 1), 2.0f))
            SetSelected(trace_.start_ns + (uint64_t)(sx < 0 ? 0 : sx * 1e9));
        if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            double mx = ImPlot::GetPlotMousePos().x;
            SetSelected(trace_.start_ns + (uint64_t)(mx < 0 ? 0 : mx * 1e9));
        }
        ImPlot::EndPlot();
    }

    ImGui::End();
}

void App::DrawSummary() {
    ImGui::Begin("Memory summary");

    MemorySummary m = trace_.SummaryAt(selected_ts_);

    ImGui::Text("At t = %s", FormatNs(selected_ts_ - trace_.start_ns).c_str());
    ImGui::Text("live objects: %llu     memory: %s",
                (unsigned long long)m.live_count, FormatBytes(m.total_bytes).c_str());
    ImGui::Checkbox("Show counts instead of bytes", &show_counts_);
    ImGui::Spacing();

    // --- heap type x allocation kind grid ---
    // Columns are the memory-bearing alloc kinds: Committed, Placed, Reserved, Heap.
    static const int kAllocCols[] = {1, 2, 3, 4};
    const ImGuiTableFlags tflags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_SizingStretchProp;

    ImGui::SeparatorText("By heap type / allocation kind");
    if (ImGui::BeginTable("grid", 6, tflags)) {
        ImGui::TableSetupColumn("Heap \\ Kind");
        for (int a : kAllocCols) ImGui::TableSetupColumn(AllocBucketName(a));
        ImGui::TableSetupColumn("Total");
        ImGui::TableHeadersRow();

        uint64_t col_total[5] = {};   // indexed by alloc bucket
        uint64_t col_total_c[5] = {};
        uint64_t grand = 0, grand_c = 0;

        for (int h = 1; h < kHeapBuckets; ++h) {
            uint64_t row_bytes = 0, row_count = 0;
            for (int a : kAllocCols) { row_bytes += m.bytes[h][a]; row_count += m.counts[h][a]; }
            if (row_bytes == 0) continue; // hide empty heap types

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(HeapBucketName(h));
            for (int a : kAllocCols) {
                ImGui::TableNextColumn();
                if (show_counts_)
                    ImGui::Text("%llu", (unsigned long long)m.counts[h][a]);
                else
                    ImGui::TextUnformatted(FormatBytes(m.bytes[h][a]).c_str());
                col_total[a]   += m.bytes[h][a];
                col_total_c[a] += m.counts[h][a];
            }
            ImGui::TableNextColumn();
            if (show_counts_) ImGui::Text("%llu", (unsigned long long)row_count);
            else              ImGui::TextUnformatted(FormatBytes(row_bytes).c_str());
            grand += row_bytes; grand_c += row_count;
        }

        // total row
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("TOTAL");
        for (int a : kAllocCols) {
            ImGui::TableNextColumn();
            if (show_counts_) ImGui::Text("%llu", (unsigned long long)col_total_c[a]);
            else              ImGui::TextUnformatted(FormatBytes(col_total[a]).c_str());
        }
        ImGui::TableNextColumn();
        if (show_counts_) ImGui::Text("%llu", (unsigned long long)grand_c);
        else              ImGui::TextUnformatted(FormatBytes(grand).c_str());

        ImGui::EndTable();
    }

    // --- live objects by type ---
    ImGui::SeparatorText("Live objects by type");
    if (ImGui::BeginTable("types", 2, tflags)) {
        ImGui::TableSetupColumn("Type");
        ImGui::TableSetupColumn("Live");
        ImGui::TableHeadersRow();
        for (const auto& [type, count] : m.per_type) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(type.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%llu", (unsigned long long)count);
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

void App::DrawAllocations() {
    ImGui::Begin("Active allocations");

    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##filter", "filter by name / type", filter_, sizeof filter_);
    ImGui::SameLine();

    // Collect indices of objects live at the selected time, honoring the filter.
    std::string flt = ToLower(filter_);
    std::vector<size_t> rows;
    rows.reserve(trace_.objects().size());
    const auto& objs = trace_.objects();
    for (size_t i = 0; i < objs.size(); ++i) {
        if (!objs[i].LiveAt(selected_ts_)) continue;
        if (!flt.empty()) {
            std::string hay = ToLower(objs[i].name) + " " + ToLower(objs[i].type);
            if (hay.find(flt) == std::string::npos) continue;
        }
        rows.push_back(i);
    }
    ImGui::Text("%zu live", rows.size());

    const ImGuiTableFlags tflags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SortTristate;

    enum Col { C_Id, C_Type, C_Alloc, C_Heap, C_Dim, C_Format, C_Size, C_Age, C_Name };

    if (ImGui::BeginTable("allocs", 9, tflags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("id",     ImGuiTableColumnFlags_WidthFixed |
                                          ImGuiTableColumnFlags_DefaultSort, 0, C_Id);
        ImGui::TableSetupColumn("type",   ImGuiTableColumnFlags_WidthFixed, 0, C_Type);
        ImGui::TableSetupColumn("alloc",  ImGuiTableColumnFlags_WidthFixed, 0, C_Alloc);
        ImGui::TableSetupColumn("heap",   ImGuiTableColumnFlags_WidthFixed, 0, C_Heap);
        ImGui::TableSetupColumn("dim",    ImGuiTableColumnFlags_WidthFixed, 0, C_Dim);
        ImGui::TableSetupColumn("format", ImGuiTableColumnFlags_WidthFixed, 0, C_Format);
        ImGui::TableSetupColumn("size",   ImGuiTableColumnFlags_WidthFixed, 0, C_Size);
        ImGui::TableSetupColumn("age",    ImGuiTableColumnFlags_WidthFixed, 0, C_Age);
        ImGui::TableSetupColumn("name",   ImGuiTableColumnFlags_WidthStretch, 0, C_Name);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs();
            ss && ss->SpecsCount > 0) {
            const ImGuiTableColumnSortSpecs& s = ss->Specs[0];
            const bool asc = s.SortDirection == ImGuiSortDirection_Ascending;
            std::sort(rows.begin(), rows.end(), [&](size_t a, size_t b) {
                const Obj& x = objs[a];
                const Obj& y = objs[b];
                int c = 0;
                switch (s.ColumnUserID) {
                    case C_Id:     c = (x.id < y.id) ? -1 : (x.id > y.id); break;
                    case C_Type:   c = x.type.compare(y.type); break;
                    case C_Alloc:  c = x.alloc.compare(y.alloc); break;
                    case C_Heap:   c = x.heap.compare(y.heap); break;
                    case C_Dim:    c = x.dim.compare(y.dim); break;
                    case C_Format: c = (int)x.format - (int)y.format; break;
                    case C_Size:   c = (x.size < y.size) ? -1 : (x.size > y.size); break;
                    case C_Age:    c = (x.created_ns > y.created_ns) ? -1
                                       : (x.created_ns < y.created_ns); break;
                    case C_Name:   c = x.name.compare(y.name); break;
                }
                if (c == 0) c = (x.id < y.id) ? -1 : (x.id > y.id);
                return asc ? c < 0 : c > 0;
            });
        }

        ImGuiListClipper clipper;
        clipper.Begin((int)rows.size());
        while (clipper.Step()) {
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                const Obj& o = objs[rows[r]];
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)o.id);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(o.type.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(o.alloc.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(o.heap.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(o.dim.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%u", o.format);
                ImGui::TableNextColumn();
                if (o.size) ImGui::TextUnformatted(FormatBytes(o.size).c_str());
                else        ImGui::TextUnformatted("-");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(FormatNs(selected_ts_ - o.created_ns).c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(o.name.empty() ? "(unnamed)" : o.name.c_str());
            }
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace dx12track
