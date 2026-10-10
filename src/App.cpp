#include "App.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <windows.h>
#include <commdlg.h> // GetOpenFileNameW / GetSaveFileNameW

#include <nlohmann/json.hpp>

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

// DXGI_FORMAT enum name with the "DXGI_FORMAT_" prefix stripped, or nullptr if
// unknown. Hardcoded so the UI needn't pull in dxgiformat.h (mirrors how the
// type/alloc/heap/dim enum names are handled). Values from the official enum.
const char* DxgiFormatToken(uint32_t f) {
    switch (f) {
        case 0:   return "UNKNOWN";
        case 1:   return "R32G32B32A32_TYPELESS";
        case 2:   return "R32G32B32A32_FLOAT";
        case 3:   return "R32G32B32A32_UINT";
        case 4:   return "R32G32B32A32_SINT";
        case 5:   return "R32G32B32_TYPELESS";
        case 6:   return "R32G32B32_FLOAT";
        case 7:   return "R32G32B32_UINT";
        case 8:   return "R32G32B32_SINT";
        case 9:   return "R16G16B16A16_TYPELESS";
        case 10:  return "R16G16B16A16_FLOAT";
        case 11:  return "R16G16B16A16_UNORM";
        case 12:  return "R16G16B16A16_UINT";
        case 13:  return "R16G16B16A16_SNORM";
        case 14:  return "R16G16B16A16_SINT";
        case 15:  return "R32G32_TYPELESS";
        case 16:  return "R32G32_FLOAT";
        case 17:  return "R32G32_UINT";
        case 18:  return "R32G32_SINT";
        case 19:  return "R32G8X24_TYPELESS";
        case 20:  return "D32_FLOAT_S8X24_UINT";
        case 21:  return "R32_FLOAT_X8X24_TYPELESS";
        case 22:  return "X32_TYPELESS_G8X24_UINT";
        case 23:  return "R10G10B10A2_TYPELESS";
        case 24:  return "R10G10B10A2_UNORM";
        case 25:  return "R10G10B10A2_UINT";
        case 26:  return "R11G11B10_FLOAT";
        case 27:  return "R8G8B8A8_TYPELESS";
        case 28:  return "R8G8B8A8_UNORM";
        case 29:  return "R8G8B8A8_UNORM_SRGB";
        case 30:  return "R8G8B8A8_UINT";
        case 31:  return "R8G8B8A8_SNORM";
        case 32:  return "R8G8B8A8_SINT";
        case 33:  return "R16G16_TYPELESS";
        case 34:  return "R16G16_FLOAT";
        case 35:  return "R16G16_UNORM";
        case 36:  return "R16G16_UINT";
        case 37:  return "R16G16_SNORM";
        case 38:  return "R16G16_SINT";
        case 39:  return "R32_TYPELESS";
        case 40:  return "D32_FLOAT";
        case 41:  return "R32_FLOAT";
        case 42:  return "R32_UINT";
        case 43:  return "R32_SINT";
        case 44:  return "R24G8_TYPELESS";
        case 45:  return "D24_UNORM_S8_UINT";
        case 46:  return "R24_UNORM_X8_TYPELESS";
        case 47:  return "X24_TYPELESS_G8_UINT";
        case 48:  return "R8G8_TYPELESS";
        case 49:  return "R8G8_UNORM";
        case 50:  return "R8G8_UINT";
        case 51:  return "R8G8_SNORM";
        case 52:  return "R8G8_SINT";
        case 53:  return "R16_TYPELESS";
        case 54:  return "R16_FLOAT";
        case 55:  return "D16_UNORM";
        case 56:  return "R16_UNORM";
        case 57:  return "R16_UINT";
        case 58:  return "R16_SNORM";
        case 59:  return "R16_SINT";
        case 60:  return "R8_TYPELESS";
        case 61:  return "R8_UNORM";
        case 62:  return "R8_UINT";
        case 63:  return "R8_SNORM";
        case 64:  return "R8_SINT";
        case 65:  return "A8_UNORM";
        case 66:  return "R1_UNORM";
        case 67:  return "R9G9B9E5_SHAREDEXP";
        case 68:  return "R8G8_B8G8_UNORM";
        case 69:  return "G8R8_G8B8_UNORM";
        case 70:  return "BC1_TYPELESS";
        case 71:  return "BC1_UNORM";
        case 72:  return "BC1_UNORM_SRGB";
        case 73:  return "BC2_TYPELESS";
        case 74:  return "BC2_UNORM";
        case 75:  return "BC2_UNORM_SRGB";
        case 76:  return "BC3_TYPELESS";
        case 77:  return "BC3_UNORM";
        case 78:  return "BC3_UNORM_SRGB";
        case 79:  return "BC4_TYPELESS";
        case 80:  return "BC4_UNORM";
        case 81:  return "BC4_SNORM";
        case 82:  return "BC5_TYPELESS";
        case 83:  return "BC5_UNORM";
        case 84:  return "BC5_SNORM";
        case 85:  return "B5G6R5_UNORM";
        case 86:  return "B5G5R5A1_UNORM";
        case 87:  return "B8G8R8A8_UNORM";
        case 88:  return "B8G8R8X8_UNORM";
        case 89:  return "R10G10B10_XR_BIAS_A2_UNORM";
        case 90:  return "B8G8R8A8_TYPELESS";
        case 91:  return "B8G8R8A8_UNORM_SRGB";
        case 92:  return "B8G8R8X8_TYPELESS";
        case 93:  return "B8G8R8X8_UNORM_SRGB";
        case 94:  return "BC6H_TYPELESS";
        case 95:  return "BC6H_UF16";
        case 96:  return "BC6H_SF16";
        case 97:  return "BC7_TYPELESS";
        case 98:  return "BC7_UNORM";
        case 99:  return "BC7_UNORM_SRGB";
        case 100: return "AYUV";
        case 101: return "Y410";
        case 102: return "Y416";
        case 103: return "NV12";
        case 104: return "P010";
        case 105: return "P016";
        case 106: return "420_OPAQUE";
        case 107: return "YUY2";
        case 108: return "Y210";
        case 109: return "Y216";
        case 110: return "NV11";
        case 111: return "AI44";
        case 112: return "IA44";
        case 113: return "P8";
        case 114: return "A8P8";
        case 115: return "B4G4R4A4_UNORM";
        case 130: return "P208";
        case 131: return "V208";
        case 132: return "V408";
        case 189: return "SAMPLER_FEEDBACK_MIN_MIP_OPAQUE";
        case 190: return "SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE";
        default:  return nullptr;
    }
}

// Display form of a DXGI_FORMAT: prefix stripped, underscores -> spaces (e.g.
// "BC7 UNORM SRGB"). Unknown values fall back to the raw integer.
std::string FormatName(uint32_t f) {
    const char* tok = DxgiFormatToken(f);
    if (!tok) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%u", f);
        return buf;
    }
    std::string s(tok);
    for (char& c : s) if (c == '_') c = ' ';
    return s;
}

// Color for a residency-priority tier (bucket index from PrioBucket).
ImVec4 PriorityColor(int bucket) {
    switch (bucket) {
        case 1: return ImVec4(0.55f, 0.65f, 0.85f, 1.0f); // Minimum - blue-gray
        case 2: return ImVec4(0.30f, 0.80f, 0.85f, 1.0f); // Low      - cyan
        case 3: return ImVec4(0.40f, 0.80f, 0.40f, 1.0f); // Normal   - green
        case 4: return ImVec4(0.95f, 0.65f, 0.25f, 1.0f); // High     - orange
        case 5: return ImVec4(0.90f, 0.35f, 0.35f, 1.0f); // Maximum  - red
        case 6: return ImVec4(0.85f, 0.45f, 0.90f, 1.0f); // Custom   - magenta
        default: return ImVec4(0.55f, 0.55f, 0.55f, 1.0f); // Unset   - dim gray
    }
}

// Color for a memory-location bucket (from LocBucket); shared by the table
// column and the By-location graph.
ImVec4 LocationColor(int bucket) {
    switch (bucket) {
        case kLocVram:    return ImVec4(0.35f, 0.75f, 0.40f, 1.0f); // green
        case kLocSys:     return ImVec4(0.95f, 0.55f, 0.20f, 1.0f); // orange
        case kLocUnknown: return ImVec4(0.65f, 0.55f, 0.85f, 1.0f); // lavender
        default:          return ImVec4(0.50f, 0.50f, 0.50f, 1.0f); // n/a - gray
    }
}

// Display/sort order of the location buckets: VRAM, Sys, Unknown, n/a.
constexpr int kLocOrder[kLocBuckets] = {kLocVram, kLocSys, kLocUnknown, kLocNA};
int LocSortKey(int bucket) {
    for (int i = 0; i < kLocBuckets; ++i)
        if (kLocOrder[i] == bucket) return i;
    return kLocBuckets;
}

// Path of the tiny settings file that remembers the last opened trace.
std::string LastFileRecordPath() {
    std::error_code ec;
    if (const char* base = std::getenv("LOCALAPPDATA"); base && *base) {
        std::filesystem::path dir = std::filesystem::path(base) / "dx12track-ui";
        std::filesystem::create_directories(dir, ec);
        return (dir / "last_file.txt").string();
    }
    return "dx12track-ui_last.txt"; // fallback: current directory
}

std::string ReadLastFile() {
    std::ifstream f(LastFileRecordPath());
    std::string line;
    if (f) std::getline(f, line);
    return line;
}

void WriteLastFile(const std::string& path) {
    std::ofstream f(LastFileRecordPath(), std::ios::trunc);
    if (f) f << path;
}

// Path of the persisted category-filter config.
std::string MemoryConfigPath() {
    std::error_code ec;
    if (const char* base = std::getenv("LOCALAPPDATA"); base && *base) {
        std::filesystem::path dir = std::filesystem::path(base) / "dx12track-ui";
        std::filesystem::create_directories(dir, ec);
        return (dir / "memory_config.txt").string();
    }
    return "dx12track-ui_memory_config.txt"; // fallback: current directory
}

std::string ReadMemoryConfig() {
    std::ifstream f(MemoryConfigPath(), std::ios::binary);
    if (!f) return {};
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

void WriteMemoryConfig(const char* text) {
    std::ofstream f(MemoryConfigPath(), std::ios::trunc | std::ios::binary);
    if (f) f << text;
}

std::string Utf8(const wchar_t* w) {
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s((size_t)(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// Native open-file dialog filtered to .jsonl. Returns false if cancelled.
bool BrowseForFile(std::string& out) {
    wchar_t buf[1024] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ofn.hwndOwner   = vp ? (HWND)vp->PlatformHandleRaw : nullptr;
    ofn.lpstrFilter = L"JSONL traces\0*.jsonl\0All files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = (DWORD)std::size(buf);
    ofn.lpstrTitle  = L"Open dx12track .jsonl";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetOpenFileNameW(&ofn)) return false;
    out = Utf8(buf);
    return true;
}

// Native save-file dialog for .jsonl exports (prompts before overwriting).
// `default_name` seeds the filename box. Returns false if cancelled.
bool BrowseForSaveFile(const wchar_t* default_name, std::string& out) {
    wchar_t buf[1024] = {};
    ::lstrcpynW(buf, default_name, (int)std::size(buf));
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ofn.hwndOwner   = vp ? (HWND)vp->PlatformHandleRaw : nullptr;
    ofn.lpstrFilter = L"JSONL traces\0*.jsonl\0All files\0*.*\0";
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = (DWORD)std::size(buf);
    ofn.lpstrTitle  = L"Save allocations as .jsonl";
    ofn.lpstrDefExt = L"jsonl";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!::GetSaveFileNameW(&ofn)) return false;
    out = Utf8(buf);
    return true;
}

} // namespace

App::App(std::string jsonl_path) {
    // Startup file resolution: an explicit command-line path wins; otherwise
    // try the last opened file, then fall back to the default name.
    if (!jsonl_path.empty()) {
        loaded_ = trace_.Load(jsonl_path);
    } else {
        std::string last = ReadLastFile();
        loaded_ = !last.empty() && trace_.Load(last);
        if (!loaded_) loaded_ = trace_.Load("dx12track.jsonl");
    }
    need_prompt_ = !loaded_; // nothing loaded -> ask for a file on first frame
    if (loaded_) WriteLastFile(trace_.path());

    // Seed the column filters with the full set of values dx12track can emit,
    // so the dropdowns list every option even if the trace doesn't use them.
    // Mirrors the *Name() tables in dx12track/src/common/EventTypes.h.
    for (const char* v : {"Unknown", "Device", "Resource", "Heap", "DescriptorHeap",
                          "CommandQueue", "CommandAllocator", "CommandList",
                          "PipelineState", "RootSignature", "Fence", "QueryHeap",
                          "CommandSignature"})
        type_show_[v] = true;
    for (const char* v : {"None", "Committed", "Placed", "Reserved", "Heap"})
        alloc_show_[v] = true;
    for (const char* v : {"None", "Default", "Upload", "Readback", "Custom", "GpuUpload"})
        heap_show_[v] = true;
    for (const char* v : {"Unknown", "Buffer", "Tex1D", "Tex2D", "Tex3D"})
        dim_show_[v] = true;
    for (const char* v : {"Unset", "Minimum", "Low", "Normal", "High", "Maximum", "Custom"})
        prio_show_[v] = true;
    for (int b = 0; b < kLocBuckets; ++b)
        loc_show_[LocBucketName(b)] = true;

    // Load the persisted category-filter config and build the tree.
    std::string cfg = ReadMemoryConfig();
    std::snprintf(config_buf_, sizeof config_buf_, "%s", cfg.c_str());
    ApplyFilterConfig();
}

double App::SelectedSeconds() const {
    return NsToSeconds(selected_ts_, trace_.start_ns);
}

void App::SetSelected(uint64_t ts) {
    // Clamp to the render-time end (main log and ETW sidecar), so the region
    // where only sidecar data exists is selectable too.
    const uint64_t end = trace_.view_end_ns();
    ts = std::clamp(ts, trace_.start_ns, std::max(end, trace_.start_ns));
    selected_ts_ = ts;
}

void App::ResetAfterLoad() {
    resolver_.Clear();
    picked_id_ = 0;
    picked_frames_.clear();
    picked_has_stack_ = false;
    range_valid_ = false;
    dragging_range_ = false;
    xlink_valid_ = false;
    obj_cat_.clear();          // recategorize the new trace's objects
    categorized_count_ = 0;
    first_frame_ = true; // re-snap cursor to the new trace's peak
}

void App::LoadFile(const std::string& path) {
    loaded_ = trace_.Load(path); // bumps trace generation -> Draw resets the view
    if (loaded_) WriteLastFile(trace_.path()); // remember for next launch
}

namespace {
std::string TrimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}
} // namespace

void App::ApplyFilterConfig() {
    defs_.clear();
    std::string text = config_buf_;
    int cur = -1; // index into defs_ of the current definition
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos
                                                                     : nl - pos);
        pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string t = TrimStr(line);
        if (t.empty()) continue;

        size_t colon = t.find(':');
        if (colon != std::string::npos) {
            Definition d;
            std::string pathstr = TrimStr(t.substr(0, colon));
            for (size_t p = 0; p <= pathstr.size();) {
                size_t s = pathstr.find('/', p);
                std::string seg = TrimStr(
                    pathstr.substr(p, s == std::string::npos ? std::string::npos : s - p));
                if (!seg.empty()) d.path.push_back(seg);
                if (s == std::string::npos) break;
                p = s + 1;
            }
            std::string pat = TrimStr(t.substr(colon + 1));
            if (!pat.empty()) d.patterns.push_back(ToLower(pat));
            defs_.push_back(std::move(d));
            cur = (int)defs_.size() - 1;
        } else if (cur >= 0) {
            defs_[cur].patterns.push_back(ToLower(t)); // continuation pattern
        }
    }
    // Drop definitions with no path (e.g. a stray ": foo").
    defs_.erase(std::remove_if(defs_.begin(), defs_.end(),
                               [](const Definition& d) { return d.path.empty(); }),
                defs_.end());

    BuildCategoryTree();
    obj_cat_.clear();          // force a full recategorize against the new rules
    categorized_count_ = 0;
}

void App::BuildCategoryTree() {
    tree_.clear();
    def_leaf_.assign(defs_.size(), -1);
    tree_.push_back(CatNode{}); // node 0 = root

    auto child = [&](int parent, const std::string& name) -> int {
        for (int c : tree_[parent].children)
            if (tree_[c].name == name) return c;
        CatNode n;
        n.name = name;
        n.parent = parent;
        tree_.push_back(std::move(n));
        int idx = (int)tree_.size() - 1;
        tree_[parent].children.push_back(idx);
        return idx;
    };

    for (size_t di = 0; di < defs_.size(); ++di) {
        int node = 0;
        for (const std::string& seg : defs_[di].path) node = child(node, seg);
        tree_[node].leaf_def = (int)di;
        def_leaf_[di] = node;
    }
    uncat_node_ = child(0, "(uncategorized)");
}

int App::Categorize(const Obj& o) const {
    std::string lname = ToLower(o.name);
    for (size_t di = 0; di < defs_.size(); ++di) {
        for (const std::string& p : defs_[di].patterns) {
            if (p == "*" || (!p.empty() && lname.find(p) != std::string::npos))
                return (int)di;
        }
    }
    return -1;
}

void App::EnsureCategorized() {
    const auto& objs = trace_.objects();
    if (categorized_count_ > objs.size()) { // trace shrank/reset
        obj_cat_.clear();
        categorized_count_ = 0;
    }
    if (obj_cat_.size() < objs.size()) obj_cat_.resize(objs.size(), -1);
    for (size_t i = categorized_count_; i < objs.size(); ++i)
        obj_cat_[i] = Categorize(objs[i]);
    categorized_count_ = objs.size();
}

void App::Draw() {
    // If the default file was missing, prompt for one on the first frame (the
    // window/viewport exists by now, so the dialog has a valid owner).
    if (need_prompt_) {
        need_prompt_ = false;
        std::string path;
        if (BrowseForFile(path)) LoadFile(path);
    }

    // A fresh capture (initial load, reload, or a live-tail restart) bumps the
    // trace generation; reset per-trace UI state and re-snap to the new peak.
    if (seen_generation_ != trace_.generation()) {
        seen_generation_ = trace_.generation();
        ResetAfterLoad();
    }

    if (first_frame_) {
        // Default the cursor to peak memory — the sample log is a clean
        // shutdown (everything freed at the end), so the end is uninteresting.
        SetSelected(trace_.peak_ts_ns() ? trace_.peak_ts_ns() : trace_.view_end_ns());
        xlink_valid_ = false; // recompute the shared X range for the new data
        first_frame_ = false;
    }

    EnsureCategorized();

    DrawMenuBar();
    DrawTimeline();
    DrawSummary();
    DrawFiltering();
    DrawPlacedInfo();
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
    ImGui::Text("uptime : %s", FormatNs(trace_.view_end_ns() - trace_.start_ns).c_str());
    ImGui::Text("events : %zu objects, %zu samples",
                trace_.objects().size(), trace_.samples().size());
    ImGui::Text("peak : %s @ %s", FormatBytes(trace_.peak_bytes()).c_str(),
                FormatNs(trace_.peak_ts_ns() - trace_.start_ns).c_str());

    if (trace_.child_exited)
        ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "child exited (code %u)",
                           trace_.exit_code);

    ImGui::Text("modules : %zu", trace_.modules().size());

    // --- ETW sidecar (dx12track --etw) ---
    if (trace_.etw_rejected()) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1),
                           "ETW sidecar ignored: from another run (pid %u)",
                           trace_.etw_hello().pid);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\nIts etw_hello pid/qpc_start don't match this log's hello.",
                              trace_.etw_path().c_str());
    } else if (trace_.has_etw()) {
        const EtwStats& st = trace_.etw_stats();
        ImGui::TextWrapped("etw : %s", trace_.etw_path().c_str());
        if (st.events_lost > 0 || st.buffers_lost > 0)
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
                "ETW data incomplete: %llu events / %llu buffers lost",
                (unsigned long long)st.events_lost, (unsigned long long)st.buffers_lost);
        if (ImGui::TreeNode("ETW details")) {
            const EtwHello& h = trace_.etw_hello();
            ImGui::Text("session %s  (format %u)", h.session.empty() ? "?" : h.session.c_str(),
                        h.etw_format);
            ImGui::Text("diagnostics : %u", trace_.etw_diag_count());
            const auto& ctr = trace_.etw_counters();
            if (!ctr.empty()) {
                const EtwCounters& c = ctr.back();
                auto show = [](const char* k, uint64_t v) {
                    if (v != kNoCounter) ImGui::Text("%s : %s", k, FormatBytes(v).c_str());
                };
                ImGui::SeparatorText("Latest counters");
                show("local budget",   c.local_budget);
                show("local usage",    c.local_usage);
                show("local resident", c.local_resident);
                show("non-local usage",  c.nonlocal_usage);
                show("demoted",        c.demoted);
            }
            if (st.present) {
                ImGui::SeparatorText("etw_stats");
                for (const auto& [k, v] : st.all)
                    if (v) ImGui::Text("%s : %llu", k.c_str(), (unsigned long long)v);
            } else {
                ImGui::TextDisabled("(no etw_stats yet: session running or killed)");
            }
            ImGui::TreePop();
        }
    } else {
        ImGui::TextDisabled("etw : no ETW sidecar");
        if (ImGui::IsItemHovered() && !trace_.etw_path().empty())
            ImGui::SetTooltip("looked for %s", trace_.etw_path().c_str());
    }

    ImGui::Separator();
    ImGui::TextWrapped("file: %s", trace_.path().c_str());
    if (ImGui::Button("Reload")) {
        loaded_ = trace_.Reload(); // generation bump -> Draw resets the view
    }
    ImGui::SameLine();
    if (ImGui::Button("Open...")) {
        std::string path;
        if (BrowseForFile(path)) LoadFile(path);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Live tail", &live_tail_);

    // --- callstack of the clicked allocation (resolved on demand) ---
    ImGui::SeparatorText("Callstack");
    if (picked_id_ == 0) {
        ImGui::TextDisabled("Click an allocation to resolve its callstack.");
    } else {
        ImGui::TextUnformatted(picked_label_.c_str());
        if (!picked_has_stack_) {
            ImGui::TextDisabled("(no callstack captured for this allocation)");
        } else if (picked_frames_.empty()) {
            ImGui::TextDisabled("(empty)");
        } else {
            // Warn once if any module's on-disk PDB was rejected as stale: those
            // frames show module+offset rather than (wrong) symbols.
            std::string mismatch_detail;
            for (const ResolvedFrame& f : picked_frames_) {
                if (f.pdb_mismatch && mismatch_detail.find(f.note) == std::string::npos) {
                    if (!mismatch_detail.empty()) mismatch_detail += "\n";
                    mismatch_detail += f.note;
                }
            }
            if (!mismatch_detail.empty()) {
                ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1),
                    "PDB mismatch: showing module+offset, not symbols.");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", mismatch_detail.c_str());
            }
            ImGui::BeginChild("stack", ImVec2(0, 0), ImGuiChildFlags_Borders);
            for (size_t i = 0; i < picked_frames_.size(); ++i) {
                const ResolvedFrame& f = picked_frames_[i];
                ImGui::TextDisabled("%2zu", i);
                ImGui::SameLine();
                if (f.resolved)
                    ImGui::TextUnformatted(f.symbol.c_str());
                else
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "%s", f.symbol.c_str());
                if (!f.module.empty() && ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s  (0x%llx)", f.module.c_str(),
                                      (unsigned long long)f.address);
            }
            ImGui::EndChild();
        }
    }

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
    ImGui::RadioButton("By priority", (int*)&mode_, (int)PlotMode::ByPriority);
    ImGui::SameLine();
    // Location comes from the ETW sidecar; without one the mode doesn't exist.
    if (trace_.has_etw()) {
        ImGui::RadioButton("By location", (int*)&mode_, (int)PlotMode::ByLocation);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Counted memory by ETW-reported location (VRAM / system memory)");
        ImGui::SameLine();
    } else if (mode_ == PlotMode::ByLocation) {
        mode_ = PlotMode::Total;
    }
    if (ImGui::Button("Jump to peak")) SetSelected(trace_.peak_ts_ns());
    ImGui::SameLine();
    if (ImGui::Button("Jump to end")) SetSelected(trace_.view_end_ns());
    ImGui::SameLine();

    // Splitting is a heap-type partition. It applies to the heap-based views
    // (Total / By heap) and to By priority (device-local shows the priority
    // breakdown; host-visible falls back to heap type, since priority is
    // irrelevant there). It does not apply to the allocation-kind breakdown.
    const bool can_split = (mode_ != PlotMode::ByAlloc);
    ImGui::BeginDisabled(!can_split);
    ImGui::Checkbox("Separate Upload/Readback graph", &split_host_);
    ImGui::EndDisabled();
    if (!can_split && ImGui::IsItemHovered())
        ImGui::SetTooltip("Allocation-kind series can't be split by heap type");
    ImGui::SameLine();
    ImGui::Checkbox("Follow tail", &graph_follow_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show the last 100s and pin the cursor to the latest sample");
    // ETW overlays; like By location, they only exist with a sidecar.
    if (trace_.has_etw()) {
        ImGui::SameLine();
        ImGui::Checkbox("Local usage/budget", &show_etw_counters_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Overlay the process's ETW local video memory usage and budget");
        ImGui::SameLine();
        ImGui::Checkbox("Location lines", &show_loc_lines_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Overlay counted memory per ETW location (VRAM / Sys / Unknown)");
    }

    // Escape clears the selected range.
    if ((range_valid_ || dragging_range_) && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        range_valid_ = false;
        dragging_range_ = false;
    }

    const auto& samples = trace_.samples();
    if (samples.empty()) {
        ImGui::TextUnformatted("No memory events in this trace.");
        ImGui::End();
        return;
    }

    // All graphs end at the latest timestamp across the main log and the ETW
    // sidecar (render-time only: the model isn't extended, since delayed
    // events can still arrive). The last value of each series holds until then.
    const uint64_t view_end = trace_.view_end_ns();
    if (mode_ == PlotMode::ByLocation) {
        // The location series is a step function (value holds until the next
        // sample); duplicate each X so the shaded bands are drawn as stairs,
        // and extend the last value to the end of the trace.
        const auto& ls = trace_.LocationSeries();
        xs_.clear();
        for (size_t i = 0; i < ls.size(); ++i) {
            const double x = NsToSeconds(ls[i].ts_ns, trace_.start_ns);
            if (i > 0) xs_.push_back(x); // end of the previous step
            xs_.push_back(x);
        }
        if (!ls.empty() && view_end > ls.back().ts_ns)
            xs_.push_back(NsToSeconds(view_end, trace_.start_ns));
    } else {
        // One point per sample, plus a final point at the view end holding the
        // last sample's values (DrawTimelinePanel maps it back to that sample).
        const size_t N = samples.size();
        xs_.resize(N);
        for (size_t i = 0; i < N; ++i)
            xs_[i] = NsToSeconds(samples[i].ts_ns, trace_.start_ns);
        if (view_end > samples.back().ts_ns)
            xs_.push_back(NsToSeconds(view_end, trace_.start_ns));
    }

    const double latest = NsToSeconds(view_end, trace_.start_ns);
    // Default X view: the whole capture, but at least 0..100s.
    if (!xlink_valid_) {
        xlink_min_ = 0.0;
        xlink_max_ = std::max(100.0, latest);
        xlink_valid_ = true;
    }
    // Follow tail: show the last 100s (or 0..100 if shorter) and pin the cursor
    // to the latest timestamp.
    if (graph_follow_) {
        if (latest > 100.0) { xlink_min_ = latest - 100.0; xlink_max_ = latest; }
        else                { xlink_min_ = 0.0;            xlink_max_ = 100.0; }
        SetSelected(view_end);
    }

    if (split_host_ && can_split) {
        if (ImPlot::BeginAlignedPlots("mem_aligned")) {
            const float h = (ImGui::GetContentRegionAvail().y - 4.0f) * 0.5f;
            DrawTimelinePanel("Device-local (Default)##dev", h, 1);
            DrawTimelinePanel("Host-visible (Upload/Readback)##host", h, 2);
            ImPlot::EndAlignedPlots();
        }
    } else {
        DrawTimelinePanel("##mem", -1.0f, 0);
    }

    ImGui::End();
}

void App::DrawTimelinePanel(const char* plot_id, float height, int heap_filter) {
    const auto& samples = trace_.samples();
    const size_t N = samples.size();
    // Points drawn by the samples-based modes: xs_ may carry one extra point
    // at the view end (see DrawTimeline), which repeats the last sample.
    const size_t NP = std::min(xs_.size(), N + 1);

    // While Shift is held, free up the left mouse button (normally pan) so we
    // can use it for range selection; restore the mapping after EndPlot.
    const bool shift = ImGui::GetIO().KeyShift;
    ImPlotInputMap& imap = ImPlot::GetInputMap();
    const int saved_pan = imap.Pan;
    if (shift) imap.Pan = ImGuiMouseButton_Middle;

    if (!ImPlot::BeginPlot(plot_id, ImVec2(-1, height))) {
        imap.Pan = saved_pan;
        return;
    }

    // Upload (bucket 2) and Readback (bucket 3) are the host-visible heaps.
    auto in_filter = [heap_filter](int b) {
        const bool host = (b == 2 || b == 3);
        if (heap_filter == 1) return !host; // device-local
        if (heap_filter == 2) return host;  // host-visible
        return true;                        // all
    };

    // Per-panel "effective mode". In By-priority the host-visible panel
    // (heap_filter==2) renders a heap-type breakdown instead, since residency
    // priority is only meaningful for device-local memory.
    const bool prio_as_heap   = (mode_ == PlotMode::ByPriority && heap_filter == 2);
    const bool stacking_heap  = (mode_ == PlotMode::ByHeap) || prio_as_heap;
    const bool stacking_prio  = (mode_ == PlotMode::ByPriority) && !prio_as_heap;
    const bool stacking_alloc = (mode_ == PlotMode::ByAlloc);
    // The device-local priority panel uses the device-only series; the unsplit
    // priority view uses the all-memory series.
    auto prio_src = [heap_filter](const Sample& s) -> const uint64_t* {
        return (heap_filter == 1) ? s.by_prio_dev : s.by_prio;
    };

    // By location: stacked VRAM / Sys / Unknown / n/a from the (separately
    // built, time-sorted) location series. xs_ holds its stair-step expansion;
    // loc_pt maps each plotted point to its source sample.
    const bool by_loc = (mode_ == PlotMode::ByLocation);
    // In the other modes, with ETW data, the same series is overlaid as
    // unfilled VRAM / Sys / Unknown lines (see below). Not in the host-visible
    // panel: Upload/Readback memory is always system memory.
    const bool loc_lines = show_loc_lines_ && !by_loc && heap_filter != 2 && trace_.has_etw();
    static const std::vector<LocSample> kNoLoc;
    const std::vector<LocSample>& lser =
        (by_loc || loc_lines) ? trace_.LocationSeries() : kNoLoc;
    std::vector<uint32_t> loc_pt;
    if (by_loc) {
        for (size_t i = 0; i < lser.size(); ++i) {
            if (i > 0) loc_pt.push_back((uint32_t)(i - 1));
            loc_pt.push_back((uint32_t)i);
        }
        if (!lser.empty() && xs_.size() > loc_pt.size())
            loc_pt.push_back((uint32_t)(lser.size() - 1)); // extended to trace end
    }
    auto loc_val = [heap_filter](const LocSample& s, int b) -> uint64_t {
        if (heap_filter == 1) return s.by_loc_dev[b];
        if (heap_filter == 2) return s.by_loc[b] - s.by_loc_dev[b];
        return s.by_loc[b];
    };
    // The process video-memory counters (ETW) describe device-local memory, so
    // they're overlaid in every mode except in the host-visible panel.
    const bool show_ctr = show_etw_counters_ && heap_filter != 2 && trace_.has_etw() &&
                          !trace_.etw_counters().empty();

    // Peak of whatever this panel will display, so we can leave headroom above
    // it (AutoFit pins the max flush against the top edge, hiding the peak).
    double ymax = 0.0;
    if (by_loc) {
        for (const LocSample& s : lser) {
            uint64_t sum = 0;
            for (int b = 0; b < kLocBuckets; ++b) sum += loc_val(s, b);
            ymax = std::max(ymax, (double)sum);
        }
    }
    for (size_t i = 0; i < N && !by_loc; ++i) {
        uint64_t s = 0;
        if (stacking_alloc) {
            for (int b = 1; b < kAllocBuckets; ++b) s += samples[i].by_alloc[b];
        } else if (stacking_prio) {
            const uint64_t* src = prio_src(samples[i]);
            for (int b = 0; b < kPrioBuckets; ++b) s += src[b];
        } else { // stacking_heap or Total: heap subset
            for (int b = 0; b < kHeapBuckets; ++b)
                if (in_filter(b)) s += samples[i].by_heap[b];
        }
        if ((double)s > ymax) ymax = (double)s;
    }
    // Location overlay lines: per-bucket peaks (a zero peak hides the line).
    // n/a is never drawn as a line.
    constexpr int kLineBuckets[] = {kLocVram, kLocSys, kLocUnknown};
    double line_max[kLocBuckets] = {};
    if (loc_lines) {
        for (const LocSample& s : lser)
            for (int b : kLineBuckets)
                line_max[b] = std::max(line_max[b], (double)loc_val(s, b));
        for (int b : kLineBuckets) ymax = std::max(ymax, line_max[b]);
    }
    // Counter overlay: local usage always counts toward the scale; local
    // budget only if it's within 2x of everything else.
    double budget_max = 0.0;
    if (show_ctr) {
        for (const EtwCounters& c : trace_.etw_counters()) {
            if (c.local_usage != kNoCounter)
                ymax = std::max(ymax, (double)c.local_usage);
            if (c.local_budget != kNoCounter)
                budget_max = std::max(budget_max, (double)c.local_budget);
        }
        if (budget_max > 0.0 && budget_max <= ymax * 2.0) ymax = std::max(ymax, budget_max);
    }

    ImPlot::SetupAxes("time (s)", "memory", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
    ImPlot::SetupAxisFormat(ImAxis_Y1, ByteAxisFormatter);
    ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, ymax > 0.0 ? ymax * 1.03 : 1.0,
                            ImPlotCond_Always);
    // Drive the X range from the shared link (default 0..max(100,total), and
    // follow-tail/zoom); also keeps the two split graphs in sync.
    ImPlot::SetupAxisLinks(ImAxis_X1, &xlink_min_, &xlink_max_);
    ImPlot::SetupLegend(ImPlotLocation_NorthWest);

    if (by_loc) {
        const size_t P = std::min(loc_pt.size(), xs_.size());
        std::vector<double> baseline(P, 0.0);
        ImPlotSpec fill;
        fill.FillAlpha = 1.0f; // opaque, see the stacked-band note below
        for (int b : kLocOrder) {
            lo_.resize(P);
            hi_.resize(P);
            bool any = false;
            for (size_t k = 0; k < P; ++k) {
                const uint64_t v = loc_val(lser[loc_pt[k]], b);
                lo_[k] = baseline[k];
                hi_[k] = baseline[k] + (double)v;
                baseline[k] = hi_[k];
                if (v) any = true;
            }
            if (!any) continue;
            fill.FillColor = LocationColor(b);
            ImPlot::PlotShaded(LocBucketName(b), xs_.data(), lo_.data(), hi_.data(),
                               (int)P, fill);
        }
    } else if (mode_ != PlotMode::Total) {
        // Priority shows the Unset band (bucket 0) too; heap/alloc skip bucket 0.
        const int first = stacking_prio ? 0 : 1;
        const int last  = stacking_heap ? kHeapBuckets
                        : stacking_prio ? kPrioBuckets : kAllocBuckets;
        std::vector<double> baseline(NP, 0.0);
        ImPlotSpec fill;
        // Opaque fill: stacked bands don't overlap each other, and translucent
        // fills accumulate alpha where samples are dense (or share timestamps),
        // which painted bright vertical stripes over busy regions.
        fill.FillAlpha = 1.0f;
        for (int b = first; b < last; ++b) {
            if (stacking_heap && !in_filter(b)) continue;
            lo_.resize(NP);
            hi_.resize(NP);
            bool any = false;
            for (size_t i = 0; i < NP; ++i) {
                const Sample& smp = samples[std::min(i, N - 1)];
                uint64_t v = stacking_heap ? smp.by_heap[b]
                           : stacking_prio ? prio_src(smp)[b]
                                           : smp.by_alloc[b];
                lo_[i] = baseline[i];
                hi_[i] = baseline[i] + (double)v;
                baseline[i] = hi_[i];
                if (v) any = true;
            }
            if (!any) continue; // don't clutter the legend with empty bands
            const char* name = stacking_heap ? HeapBucketName(b)
                             : stacking_prio ? PrioBucketName(b) : AllocBucketName(b);
            ImPlot::PlotShaded(name, xs_.data(), lo_.data(), hi_.data(), (int)NP, fill);
        }
    } else { // Total of the selected heap subset
        ys_.resize(NP);
        for (size_t i = 0; i < NP; ++i) {
            const Sample& smp = samples[std::min(i, N - 1)];
            uint64_t s = 0;
            for (int b = 0; b < kHeapBuckets; ++b)
                if (in_filter(b)) s += smp.by_heap[b];
            ys_[i] = (double)s;
        }
        const char* lbl = (heap_filter == 1) ? "Device-local"
                        : (heap_filter == 2) ? "Upload+Readback"
                                             : "Total";
        ImPlotSpec fill;
        fill.FillAlpha = 0.25f;
        ImPlot::PlotShaded(lbl, xs_.data(), ys_.data(), (int)NP, 0.0, fill);
        ImPlot::PlotStairs(lbl, xs_.data(), ys_.data(), (int)NP);
    }

    // Location overlay: counted memory per ETW location as unfilled stairs on
    // top of the current mode's graph. The series has its own timestamps, so
    // it gets its own X buffer; the last value is extended to the trace end.
    if (loc_lines && !lser.empty()) {
        lxs_.clear();
        for (const LocSample& s : lser) lxs_.push_back(NsToSeconds(s.ts_ns, trace_.start_ns));
        const uint64_t view_end = trace_.view_end_ns();
        const bool extend = view_end > lser.back().ts_ns;
        if (extend) lxs_.push_back(NsToSeconds(view_end, trace_.start_ns));
        ImPlotSpec ls;
        ls.LineWeight = 2.0f;
        for (int b : kLineBuckets) {
            if (line_max[b] <= 0.0) continue;
            lys_.clear();
            for (const LocSample& s : lser) lys_.push_back((double)loc_val(s, b));
            if (extend) lys_.push_back(lys_.back());
            ls.LineColor = LocationColor(b);
            // Distinct ID from the By-location bands; visible label unchanged.
            const std::string label = std::string(LocBucketName(b)) + "###loc_line_" +
                                      std::to_string(b);
            ImPlot::PlotStairs(label.c_str(), lxs_.data(), lys_.data(), (int)lxs_.size(), ls);
        }
    }

    // Counter overlay: the process's ETW local (device) memory usage and
    // budget, on top of whatever the current mode drew.
    if (show_ctr) {
        const auto& ctr = trace_.etw_counters();
        auto plot_ctr = [&](const char* label, uint64_t EtwCounters::*field,
                            const ImVec4& col) {
            cxs_.clear();
            cys_.clear();
            for (const EtwCounters& c : ctr)
                if (c.*field != kNoCounter) {
                    cxs_.push_back(NsToSeconds(c.ts_ns, trace_.start_ns));
                    cys_.push_back((double)(c.*field));
                }
            if (cxs_.empty()) return;
            ImPlotSpec ls;
            ls.LineColor  = col;
            ls.LineWeight = 2.0f;
            ImPlot::PlotLine(label, cxs_.data(), cys_.data(), (int)cxs_.size(), ls);
        };
        plot_ctr("local usage (ETW)", &EtwCounters::local_usage, ImVec4(1, 1, 1, 0.9f));
        // Budget is often far above the app's usage; rather than squash the
        // graph, leave it off-scale and say so in the legend.
        std::string blabel = "local budget (ETW)";
        if (budget_max > ymax * 1.03)
            blabel += " " + FormatBytes((uint64_t)budget_max) + ", off scale";
        blabel += "###budget";
        plot_ctr(blabel.c_str(), &EtwCounters::local_budget, ImVec4(1, 0.3f, 0.3f, 0.9f));
    }

    const bool hovered = ImPlot::IsPlotHovered();
    const double mouse_x = ImPlot::GetPlotMousePos().x;
    auto sec_to_ns = [&](double s) {
        return trace_.start_ns + (uint64_t)(s < 0 ? 0 : s * 1e9);
    };

    // Shift-drag selects a time range (used by the leak-hunting tabs).
    if (shift && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        dragging_range_  = true;
        drag_anchor_sec_ = mouse_x;
    }
    if (dragging_range_) {
        double a = drag_anchor_sec_, b = mouse_x;
        if (a > b) std::swap(a, b);
        range_start_ = sec_to_ns(a);
        range_end_   = sec_to_ns(b);
        range_valid_ = true;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            dragging_range_ = false;
            // A shift-click (no real drag) clears the selection instead.
            if (range_end_ <= range_start_ + 1000) range_valid_ = false;
        }
    }

    // Highlight the selected range.
    if (range_valid_) {
        const ImPlotRect lim = ImPlot::GetPlotLimits();
        const ImVec2 p0 = ImPlot::PlotToPixels(
            NsToSeconds(range_start_, trace_.start_ns), lim.Y.Max);
        const ImVec2 p1 = ImPlot::PlotToPixels(
            NsToSeconds(range_end_, trace_.start_ns), lim.Y.Min);
        ImDrawList* dl = ImPlot::GetPlotDrawList();
        ImPlot::PushPlotClipRect();
        dl->AddRectFilled(p0, p1, IM_COL32(255, 215, 90, 38));
        dl->AddRect(p0, p1, IM_COL32(255, 215, 90, 160));
        ImPlot::PopPlotClipRect();
    }

    // Selected-time cursor: draggable / click-to-place, but not while Shift is
    // held (Shift+left is reserved for range selection). Hidden entirely while
    // a range is selected, since the views then key off the range instead.
    if (!range_valid_) {
        double sx = SelectedSeconds();
        bool moved = ImPlot::DragLineX(1, &sx, ImVec4(1, 1, 0, 1), 2.0f);
        if (!shift) {
            if (moved) SetSelected(sec_to_ns(sx));
            if (hovered && !dragging_range_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                SetSelected(sec_to_ns(mouse_x));
        }
    }
    ImPlot::EndPlot();
    imap.Pan = saved_pan;
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
            // row_any: any bytes at all (controls row visibility, keeps showing
            // Placed/Reserved). row_bytes/row_count: only counted memory, used
            // for the row Total and grand total.
            uint64_t row_any = 0, row_bytes = 0, row_count = 0;
            for (int a : kAllocCols) {
                row_any += m.bytes[h][a];
                if (AllocCountsAsMemory(a)) {
                    row_bytes  += m.bytes[h][a];
                    row_count  += m.counts[h][a];
                }
            }
            if (row_any == 0) continue; // hide heap types with no allocations

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

    // --- counted memory by ETW-reported location ---
    if (trace_.has_etw()) {
        ImGui::SeparatorText("By location (ETW)");
        if (ImGui::BeginTable("loc", 3, tflags)) {
            ImGui::TableSetupColumn("Location");
            ImGui::TableSetupColumn(show_counts_ ? "Count" : "Memory");
            ImGui::TableSetupColumn("Share");
            ImGui::TableHeadersRow();
            for (int b : kLocOrder) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(LocationColor(b), "%s", LocBucketName(b));
                if (b == kLocNA && ImGui::IsItemHovered())
                    ImGui::SetTooltip("No location known: not bound to an ETW object, "
                                      "or before its first location report");
                ImGui::TableNextColumn();
                if (show_counts_) ImGui::Text("%llu", (unsigned long long)m.loc_counts[b]);
                else              ImGui::TextUnformatted(FormatBytes(m.loc_bytes[b]).c_str());
                ImGui::TableNextColumn();
                if (m.total_bytes)
                    ImGui::Text("%.1f%%", 100.0 * (double)m.loc_bytes[b] / (double)m.total_bytes);
                else
                    ImGui::TextUnformatted("-");
            }
            ImGui::EndTable();
        }
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

void App::DrawFiltering() {
    ImGui::Begin("Filtering");

    ImGui::TextWrapped("Group allocations into a tree. One rule per block:");
    ImGui::TextDisabled("path/to/node: pattern   (more patterns on following lines)");
    ImGui::TextDisabled("'*' matches everything (put it last).");

    if (ImGui::Button("Apply")) {
        ApplyFilterConfig();
        WriteMemoryConfig(config_buf_);
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        std::string cfg = ReadMemoryConfig();
        std::snprintf(config_buf_, sizeof config_buf_, "%s", cfg.c_str());
        ApplyFilterConfig();
    }
    ImGui::SameLine();
    size_t npat = 0;
    for (const Definition& d : defs_) npat += d.patterns.size();
    ImGui::TextDisabled("%zu categories, %zu patterns", defs_.size(), npat);

    ImGui::InputTextMultiline("##cfg", config_buf_, sizeof config_buf_,
                              ImVec2(-1, -1));

    ImGui::End();
}

// Placed-resource <-> heap cross-reference, keyed off the selected object.
// Selecting a Placed resource shows the heap it aliases into; selecting a Heap
// lists the placed resources living inside it. Both directions are clickable so
// you can flip back and forth.
void App::DrawPlacedInfo() {
    ImGui::Begin("Placed Resource Info");

    const Obj* sel = picked_id_ ? trace_.ObjectById(picked_id_) : nullptr;
    if (!sel) {
        ImGui::TextDisabled("Select a Placed resource or a Heap to see placement info.");
        ImGui::TextDisabled("(click a row in the allocations table)");
        ImGui::End();
        return;
    }

    const uint64_t active_t = range_valid_ ? range_end_ : selected_ts_;
    const uint64_t s0 = trace_.start_ns;

    auto obj_line = [&](const char* prefix, const Obj& o) {
        ImGui::Text("%s id %llu  %s", prefix, (unsigned long long)o.id,
                    o.name.empty() ? "(unnamed)" : o.name.c_str());
        ImGui::Text("    type %s  alloc %s  heap %s  dim %s  size %s",
                    o.type.c_str(), o.alloc.c_str(), o.heap.c_str(), o.dim.c_str(),
                    o.size ? FormatBytes(o.size).c_str() : "-");
    };

    if (sel->alloc_bucket == kAllocPlaced) {
        // --- a Placed resource is selected: describe its parent heap ---
        ImGui::SeparatorText("Selected placed resource");
        obj_line("", *sel);

        ImGui::SeparatorText("Backed by heap");
        const Obj* heap = trace_.ObjectById(sel->parent_heap_id);
        if (heap) {
            obj_line("", *heap);
            if (sel->parent_heap_ptr)
                ImGui::Text("    heap ptr 0x%llx",
                            (unsigned long long)sel->parent_heap_ptr);

            // How much of the heap is occupied by placed resources right now.
            uint64_t placed_bytes = 0, placed_count = 0;
            for (const Obj& o : trace_.objects())
                if (o.alloc_bucket == kAllocPlaced &&
                    o.parent_heap_id == heap->id && o.LiveAt(active_t)) {
                    placed_bytes += o.size;
                    ++placed_count;
                }
            ImGui::Text("    placed inside (at t=%s): %llu resources, %s",
                        FormatNs(active_t - s0).c_str(),
                        (unsigned long long)placed_count,
                        FormatBytes(placed_bytes).c_str());

            if (ImGui::Button("Go to heap")) SelectObject(*heap);
        } else {
            ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1),
                "parent heap id %llu not found in this trace",
                (unsigned long long)sel->parent_heap_id);
            if (sel->parent_heap_ptr)
                ImGui::Text("heap ptr 0x%llx", (unsigned long long)sel->parent_heap_ptr);
        }
    } else if (sel->type == "Heap") {
        // --- a Heap is selected: list the placed resources living inside it ---
        ImGui::SeparatorText("Selected heap");
        obj_line("", *sel);

        std::vector<size_t> kids;
        uint64_t kids_bytes = 0;
        const auto& objs = trace_.objects();
        for (size_t i = 0; i < objs.size(); ++i)
            if (objs[i].alloc_bucket == kAllocPlaced &&
                objs[i].parent_heap_id == sel->id && objs[i].LiveAt(active_t)) {
                kids.push_back(i);
                kids_bytes += objs[i].size;
            }

        ImGui::SeparatorText("Placed resources inside (live at selected time)");
        ImGui::Text("at t=%s : %zu resources, %s", FormatNs(active_t - s0).c_str(),
                    kids.size(), FormatBytes(kids_bytes).c_str());

        const ImGuiTableFlags tflags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_ScrollY;
        if (ImGui::BeginTable("placed_kids", 5, tflags)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("id");
            ImGui::TableSetupColumn("dim");
            ImGui::TableSetupColumn("format");
            ImGui::TableSetupColumn("size");
            ImGui::TableSetupColumn("name");
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin((int)kids.size());
            while (clipper.Step())
                for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                    const Obj& o = objs[kids[r]];
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    char idbuf[32];
                    std::snprintf(idbuf, sizeof idbuf, "%llu", (unsigned long long)o.id);
                    if (ImGui::Selectable(idbuf, picked_id_ == o.id,
                                          ImGuiSelectableFlags_SpanAllColumns))
                        SelectObject(o);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(o.dim.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(FormatName(o.format).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(o.size ? FormatBytes(o.size).c_str() : "-");
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(o.name.empty() ? "(unnamed)" : o.name.c_str());
                }
            ImGui::EndTable();
        }
    } else {
        ImGui::SeparatorText("Selected object");
        obj_line("", *sel);
        ImGui::Spacing();
        ImGui::TextDisabled("Not a Placed resource or a Heap — select one of those");
        ImGui::TextDisabled("to cross-reference placed resources with their heaps.");
    }

    ImGui::End();
}

void App::FilterCombo(const char* label, std::map<std::string, bool>& sel) {
    int on = 0;
    for (auto& [k, v] : sel) if (v) ++on;
    const int total = (int)sel.size();

    char preview[24];
    if (on == total)   std::snprintf(preview, sizeof preview, "All");
    else if (on == 0)  std::snprintf(preview, sizeof preview, "None");
    else               std::snprintf(preview, sizeof preview, "%d/%d", on, total);

    ImGui::SetNextItemWidth(96);
    if (ImGui::BeginCombo(label, preview)) {
        ImGui::PushID(label);
        // "##" suffixes keep the visible text but give these buttons IDs
        // distinct from any value checkbox (e.g. a "None" alloc/heap value).
        if (ImGui::SmallButton("All##selectAll"))   for (auto& [k, v] : sel) v = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("None##selectNone")) for (auto& [k, v] : sel) v = false;
        ImGui::Separator();
        for (auto& [k, v] : sel) {
            bool b = v;
            if (ImGui::Checkbox(k.empty() ? "(empty)" : k.c_str(), &b)) v = b;
        }
        ImGui::PopID();
        ImGui::EndCombo();
    }
}

void App::DrawAllocations() {
    ImGui::Begin("Active allocations");

    const auto& objs = trace_.objects();

    // Top up the seeded filters with any extra values seen in the data.
    for (const Obj& o : objs) {
        type_show_.try_emplace(o.type, true);
        alloc_show_.try_emplace(o.alloc, true);
        heap_show_.try_emplace(o.heap, true);
        dim_show_.try_emplace(o.dim, true);
        prio_show_.try_emplace(o.priority_name, true);
    }

    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("##filter", "filter by name", filter_, sizeof filter_);
    ImGui::SameLine(); FilterCombo("Type", type_show_);
    ImGui::SameLine(); FilterCombo("Alloc", alloc_show_);
    ImGui::SameLine(); FilterCombo("Heap", heap_show_);
    ImGui::SameLine(); FilterCombo("Dim", dim_show_);
    ImGui::SameLine(); FilterCombo("Priority", prio_show_);
    if (trace_.has_etw()) { ImGui::SameLine(); FilterCombo("Location", loc_show_); }
    ImGui::SameLine(); ImGui::Checkbox("Tree view", &show_tree_);

    const uint64_t s0 = trace_.start_ns;
    if (range_valid_) {
        ImGui::TextDisabled("range  %s ... %s   (%s)",
            FormatNs(range_start_ - s0).c_str(),
            FormatNs(range_end_ - s0).c_str(),
            FormatNs(range_end_ - range_start_).c_str());
    } else {
        ImGui::TextDisabled("Shift-drag the graph to select a time range");
    }

    const uint64_t rs = range_start_, re = range_end_;
    const uint64_t active_t = range_valid_ ? re : selected_ts_;

    if (ImGui::BeginTabBar("alloc_tabs")) {
        // Always present: objects live at the cursor (or at the end of a range).
        if (ImGui::BeginTabItem("Active Allocations")) {
            DrawAllocTable("active", "live", active_t,
                [active_t](const Obj& o) { return o.LiveAt(active_t); });
            ImGui::EndTabItem();
        }
        if (range_valid_) {
            // Created during the range and still alive at its end (leak suspects).
            if (ImGui::BeginTabItem("Allocations")) {
                DrawAllocTable("allocs", "allocated", re, [rs, re](const Obj& o) {
                    return o.created_ns >= rs && o.created_ns <= re && o.destroyed_ns > re;
                });
                ImGui::EndTabItem();
            }
            // Alive at the start of the range, freed before its end.
            if (ImGui::BeginTabItem("Frees")) {
                DrawAllocTable("frees", "freed", re, [rs, re](const Obj& o) {
                    return o.created_ns <= rs && o.destroyed_ns > rs && o.destroyed_ns <= re;
                });
                ImGui::EndTabItem();
            }
            // Both created and freed within the range (transient churn).
            if (ImGui::BeginTabItem("Alloc&Free")) {
                DrawAllocTable("allocfree", "alloc+freed", re, [rs, re](const Obj& o) {
                    return o.created_ns >= rs && o.created_ns <= re &&
                           o.destroyed_ns >= rs && o.destroyed_ns <= re;
                });
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

// Select an object: resolves its callstack and drives the placed-info panel.
void App::SelectObject(const Obj& o) {
    char idbuf[32];
    std::snprintf(idbuf, sizeof idbuf, "%llu", (unsigned long long)o.id);
    picked_id_ = o.id;
    picked_label_ = std::string(idbuf) + "  " + (o.name.empty() ? o.type : o.name);
    picked_has_stack_ = !o.stack.empty();
    picked_frames_ = resolver_.Resolve(o.stack, trace_);
}

// Render one allocation as a table row (shared by flat + tree views): 10
// columns, plus `location` after `priority` when the trace has ETW data.
void App::DrawAllocRow(const Obj& o, uint64_t ref_t) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    // Whole-row selectable: clicking resolves this object's stack.
    char idbuf[32];
    std::snprintf(idbuf, sizeof idbuf, "%llu", (unsigned long long)o.id);
    if (ImGui::Selectable(idbuf, picked_id_ == o.id,
                          ImGuiSelectableFlags_SpanAllColumns))
        SelectObject(o);
    ImGui::TableNextColumn(); ImGui::TextUnformatted(o.type.c_str());
    ImGui::TableNextColumn(); ImGui::TextUnformatted(o.alloc.c_str());
    ImGui::TableNextColumn(); ImGui::TextUnformatted(o.heap.c_str());
    ImGui::TableNextColumn(); ImGui::TextUnformatted(o.dim.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(FormatName(o.format).c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("DXGI_FORMAT = %u", o.format);
    ImGui::TableNextColumn();
    if (o.size) ImGui::TextUnformatted(FormatBytes(o.size).c_str());
    else        ImGui::TextUnformatted("-");
    ImGui::TableNextColumn();
    ImGui::TextColored(PriorityColor(o.prio_bucket), "%s", o.priority_name.c_str());
    if (o.prio_bucket == 6 /*Custom*/ && ImGui::IsItemHovered())
        ImGui::SetTooltip("raw priority = %d (0x%X)", o.priority, (unsigned)o.priority);
    if (trace_.has_etw()) {
        // Location at the tab's reference time, with the ETW details on hover.
        ImGui::TableNextColumn();
        const LocEntry* le = o.LocationEntryAt(ref_t);
        const int loc = le ? le->loc : kLocNA;
        ImGui::TextColored(LocationColor(loc), "%s", LocBucketName(loc));
        if (ImGui::IsItemHovered()) {
            const EtwObjInfo& e = o.etw;
            std::string tip;
            char buf[160];
            if (!e.bound) {
                tip = "not bound to an ETW object";
            } else {
                std::snprintf(buf, sizeof buf, "ETW lib_id %llu%s",
                              (unsigned long long)e.lib_id,
                              e.late ? " (late bind: destroyed before ETW saw it)" : "");
                tip = buf;
            }
            if (le) {
                std::snprintf(buf, sizeof buf, "\nvia %s", le->via_heap ? "heap (follows its parent heap)"
                                                                       : "self");
                tip += buf;
            }
            std::snprintf(buf, sizeof buf, "\nlocation reports: %zu", e.locations.size());
            tip += buf;
            for (const LocEntry& h : e.locations) {
                std::snprintf(buf, sizeof buf, "\n  %s  %s%s",
                              FormatNs(h.ts_ns >= trace_.start_ns ? h.ts_ns - trace_.start_ns : 0).c_str(),
                              LocBucketName(h.loc), h.via_heap ? " (via heap)" : "");
                tip += buf;
                if (&h - e.locations.data() >= 15 && e.locations.size() > 17) {
                    std::snprintf(buf, sizeof buf, "\n  ... %zu more", e.locations.size() - 16);
                    tip += buf;
                    break;
                }
            }
            std::snprintf(buf, sizeof buf, "\npage-ins %u  page-outs %u", e.page_ins, e.page_outs);
            tip += buf;
            if (e.driver_size)
                tip += "\ndriver size " + FormatBytes(e.driver_size);
            ImGui::SetTooltip("%s", tip.c_str());
        }
    }
    ImGui::TableNextColumn();
    uint64_t age = ref_t >= o.created_ns ? ref_t - o.created_ns : 0;
    ImGui::TextUnformatted(FormatNs(age).c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(o.name.empty() ? "(unnamed)" : o.name.c_str());
}

void App::DrawCategoryNode(int node, uint64_t ref_t,
                           const std::function<bool(size_t, size_t)>& less) {
    CatNode& n = tree_[node];
    if (n.count == 0) return; // hide categories with nothing in them

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    // Stable ID from the node index; size/count go in the (formatted) label.
    bool open = ImGui::TreeNodeEx(
        (void*)(intptr_t)node,
        ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen,
        "%s   %s  (%u)", n.name.c_str(), FormatBytes(n.size).c_str(), n.count);
    if (!open) return;

    // Children first (largest subtree first), then this node's own allocations.
    std::vector<int> kids = n.children;
    std::sort(kids.begin(), kids.end(),
              [this](int a, int b) { return tree_[a].size > tree_[b].size; });
    for (int c : kids) DrawCategoryNode(c, ref_t, less);

    if (!n.objs.empty()) {
        std::vector<size_t> mine = n.objs;
        if (less) std::sort(mine.begin(), mine.end(), less);
        const auto& objs = trace_.objects();
        for (size_t idx : mine) DrawAllocRow(objs[idx], ref_t);
    }
    ImGui::TreePop();
}

void App::DrawAllocTable(const char* id, const char* noun, uint64_t ref_t,
                         const std::function<bool(const Obj&)>& include) {
    const auto& objs = trace_.objects();

    // Collect objects matching this tab's predicate and the shared filters,
    // and total their size for the summary line.
    std::string flt = ToLower(filter_);
    std::vector<size_t> rows;
    rows.reserve(objs.size());
    uint64_t total_size = 0;
    // The location filter only exists (and only applies) with ETW data.
    const bool etw = trace_.has_etw();
    bool loc_ok[kLocBuckets];
    for (int b = 0; b < kLocBuckets; ++b) loc_ok[b] = loc_show_[LocBucketName(b)];
    for (size_t i = 0; i < objs.size(); ++i) {
        const Obj& o = objs[i];
        if (!include(o)) continue;
        if (!type_show_[o.type] || !alloc_show_[o.alloc] ||
            !heap_show_[o.heap] || !dim_show_[o.dim] ||
            !prio_show_[o.priority_name]) continue;
        if (etw && !loc_ok[o.LocationAt(ref_t)]) continue;
        if (!flt.empty() && ToLower(o.name).find(flt) == std::string::npos) continue;
        rows.push_back(i);
        // Placed resources alias into a heap and Reserved are virtual, so they
        // don't add to actual allocated memory.
        if (AllocCountsAsMemory(o.alloc_bucket)) total_size += o.size;
    }
    ImGui::Text("%zu %s   %s", rows.size(), noun, FormatBytes(total_size).c_str());

    // Export exactly this tab's filtered rows (independent of tree expansion or
    // scroll position) as a .jsonl mini-trace.
    ImGui::SameLine();
    if (ImGui::SmallButton((std::string("Save...##") + id).c_str())) {
        std::string path;
        if (BrowseForSaveFile(L"allocations.jsonl", path)) {
            std::string err;
            if (SaveAllocations(path, rows, ref_t, err)) {
                char buf[64];
                std::snprintf(buf, sizeof buf, "saved %zu to ", rows.size());
                save_status_ = buf + std::filesystem::u8path(path).filename().u8string();
            } else {
                save_status_ = "save failed: " + err;
            }
        }
    }
    if (!save_status_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", save_status_.c_str());
    }

    const ImGuiTableFlags tflags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Sortable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SortTristate;

    enum Col { C_Id, C_Type, C_Alloc, C_Heap, C_Dim, C_Format, C_Size, C_Prio,
               C_Loc, C_Age, C_Name };

    // The location column only exists with ETW data (see DrawAllocRow).
    if (!ImGui::BeginTable(id, etw ? 11 : 10, tflags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("id",     ImGuiTableColumnFlags_WidthFixed |
                                      ImGuiTableColumnFlags_DefaultSort, 0, C_Id);
    ImGui::TableSetupColumn("type",   ImGuiTableColumnFlags_WidthFixed, 0, C_Type);
    ImGui::TableSetupColumn("alloc",  ImGuiTableColumnFlags_WidthFixed, 0, C_Alloc);
    ImGui::TableSetupColumn("heap",   ImGuiTableColumnFlags_WidthFixed, 0, C_Heap);
    ImGui::TableSetupColumn("dim",    ImGuiTableColumnFlags_WidthFixed, 0, C_Dim);
    ImGui::TableSetupColumn("format", ImGuiTableColumnFlags_WidthFixed, 0, C_Format);
    ImGui::TableSetupColumn("size",   ImGuiTableColumnFlags_WidthFixed, 0, C_Size);
    ImGui::TableSetupColumn("priority", ImGuiTableColumnFlags_WidthFixed, 0, C_Prio);
    if (etw)
        ImGui::TableSetupColumn("location", ImGuiTableColumnFlags_WidthFixed, 0, C_Loc);
    ImGui::TableSetupColumn("age",   ImGuiTableColumnFlags_WidthFixed, 0, C_Age);
    ImGui::TableSetupColumn("name",   ImGuiTableColumnFlags_WidthStretch, 0, C_Name);
    ImGui::TableHeadersRow();

    // Build a sort comparator (by object index) from the table's sort specs.
    std::function<bool(size_t, size_t)> less;
    if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs();
        ss && ss->SpecsCount > 0) {
        const int  col = ss->Specs[0].ColumnUserID;
        const bool asc = ss->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
        less = [this, col, asc, ref_t](size_t a, size_t b) {
            const auto& os = trace_.objects();
            const Obj& x = os[a];
            const Obj& y = os[b];
            int c = 0;
            switch (col) {
                case C_Id:     c = (x.id < y.id) ? -1 : (x.id > y.id); break;
                case C_Type:   c = x.type.compare(y.type); break;
                case C_Alloc:  c = x.alloc.compare(y.alloc); break;
                case C_Heap:   c = x.heap.compare(y.heap); break;
                case C_Dim:    c = x.dim.compare(y.dim); break;
                case C_Format: c = (int)x.format - (int)y.format; break;
                case C_Size:   c = (x.size < y.size) ? -1 : (x.size > y.size); break;
                case C_Prio:   c = x.prio_bucket - y.prio_bucket; break;
                case C_Loc:    c = LocSortKey(x.LocationAt(ref_t)) -
                                   LocSortKey(y.LocationAt(ref_t)); break;
                case C_Age:    c = (x.created_ns > y.created_ns) ? -1
                                   : (x.created_ns < y.created_ns); break;
                case C_Name:   c = x.name.compare(y.name); break;
            }
            if (c == 0) c = (x.id < y.id) ? -1 : (x.id > y.id);
            return asc ? c < 0 : c > 0;
        };
    }

    if (show_tree_) {
        // Bucket the visible rows into their category leaves, then roll sizes up.
        for (CatNode& n : tree_) { n.size = 0; n.count = 0; n.objs.clear(); }
        for (size_t idx : rows) {
            int cat = (idx < obj_cat_.size()) ? obj_cat_[idx] : -1;
            int leaf = (cat >= 0 && cat < (int)def_leaf_.size()) ? def_leaf_[cat]
                                                                 : uncat_node_;
            if (leaf < 0 || leaf >= (int)tree_.size()) continue;
            CatNode& n = tree_[leaf];
            n.objs.push_back(idx);
            n.count += 1;
            if (AllocCountsAsMemory(objs[idx].alloc_bucket)) n.size += objs[idx].size;
        }
        // Child node indices are always greater than their parent's, so a single
        // high-to-low pass propagates each subtree's totals into its parent.
        for (int i = (int)tree_.size() - 1; i >= 1; --i) {
            const CatNode& n = tree_[i];
            if (n.parent >= 0) {
                tree_[n.parent].size  += n.size;
                tree_[n.parent].count += n.count;
            }
        }
        for (int c : tree_[0].children) DrawCategoryNode(c, ref_t, less);
    } else {
        if (less) std::sort(rows.begin(), rows.end(), less);
        ImGuiListClipper clipper;
        clipper.Begin((int)rows.size());
        while (clipper.Step())
            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r)
                DrawAllocRow(objs[rows[r]], ref_t);
    }
    ImGui::EndTable();
}

bool App::SaveAllocations(const std::string& path, const std::vector<size_t>& rows,
                          uint64_t ref_t, std::string& err) {
    using ojson = nlohmann::ordered_json; // keeps "event" first, like JsonLog.cpp
    const auto& objs = trace_.objects();

    std::ofstream f(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) {
        err = "could not open " + path;
        return false;
    }

    auto hex = [](uint64_t v) {
        char buf[24];
        std::snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)v);
        return std::string(buf);
    };
    // Replace (rather than throw on) invalid UTF-8, e.g. an ANSI argv path.
    auto emit = [&f](const ojson& j) {
        f << j.dump(-1, ' ', false, ojson::error_handler_t::replace) << '\n';
    };
    // Category-tree path of an object ("foo/bar/hest"), bucketed exactly like
    // the tree view; unmatched objects land in "(uncategorized)".
    auto category = [this](size_t idx) {
        int cat = (idx < obj_cat_.size()) ? obj_cat_[idx] : -1;
        int node = (cat >= 0 && cat < (int)def_leaf_.size()) ? def_leaf_[cat]
                                                             : uncat_node_;
        std::vector<const std::string*> segs;
        while (node > 0 && node < (int)tree_.size()) {
            segs.push_back(&tree_[node].name);
            node = tree_[node].parent;
        }
        std::string s;
        for (auto it = segs.rbegin(); it != segs.rend(); ++it) {
            if (!s.empty()) s += '/';
            s += **it;
        }
        return s;
    };

    // Header: enough for Trace::Load to accept the file, plus provenance.
    ojson hello;
    hello["event"]          = "hello";
    hello["ts_ns"]          = trace_.start_ns;
    hello["pid"]            = trace_.pid;
    hello["protocol"]       = trace_.protocol;
    hello["qpc_freq"]       = trace_.qpc_freq;
    if (trace_.protocol >= 4) hello["qpc_start"] = trace_.qpc_start;
    hello["exe"]            = trace_.exe;
    hello["source"]         = trace_.path();
    hello["snapshot_ts_ns"] = ref_t;
    emit(hello);

    // Modules, so the exported callstacks still resolve.
    for (const Module& m : trace_.modules()) {
        ojson j;
        j["event"]     = "module_loaded";
        j["ts_ns"]     = trace_.start_ns;
        j["base"]      = hex(m.base);
        j["size"]      = m.size;
        j["timestamp"] = m.pe_timestamp;
        j["pdb_age"]   = m.pdb_age;
        j["pdb_guid"]  = m.pdb_guid;
        j["name"]      = m.name;
        j["pdb_name"]  = m.pdb_name;
        emit(j);
    }

    // Objects in event order (creation time, then id).
    std::vector<size_t> order;
    order.reserve(rows.size());
    for (size_t idx : rows)
        if (idx < objs.size()) order.push_back(idx);
    std::sort(order.begin(), order.end(), [&objs](size_t a, size_t b) {
        const Obj& x = objs[a];
        const Obj& y = objs[b];
        if (x.created_ns != y.created_ns) return x.created_ns < y.created_ns;
        return x.id < y.id;
    });

    for (size_t idx : order) {
        const Obj& o = objs[idx];
        ojson j; // key order mirrors JsonLog.cpp's `created` line
        j["event"]          = "created";
        j["ts_ns"]          = o.created_ns;
        j["id"]             = o.id;
        if (o.ptr) j["ptr"] = hex(o.ptr);
        j["type"]           = o.type;
        j["alloc"]          = o.alloc;
        j["heap"]           = o.heap;
        j["dim"]            = o.dim;
        j["format"]         = o.format;
        j["size"]           = o.size;
        j["parent_heap_id"] = o.parent_heap_id;
        if (o.parent_heap_ptr) j["parent_heap_ptr"] = hex(o.parent_heap_ptr);
        j["name"]           = o.name; // current (post-rename) name
        j["category"]       = category(idx);
        // ETW-reported location at the snapshot time (VRAM/Sys/Unknown/n/a).
        if (trace_.has_etw()) j["location"] = LocBucketName(o.LocationAt(ref_t));
        if (!o.stack.empty()) {
            ojson stack = ojson::array();
            for (uint64_t a : o.stack) stack.push_back(hex(a));
            j["stack"] = std::move(stack);
        }
        emit(j);

        if (o.prio_bucket != kPrioUnset) {
            ojson p;
            p["event"]         = "residency_priority";
            p["ts_ns"]         = o.created_ns;
            p["id"]            = o.id;
            p["priority"]      = o.priority; // raw value, as Trace parses it
            p["priority_name"] = o.priority_name;
            emit(p);
        }
    }

    f.flush();
    if (!f) {
        err = "write error on " + path;
        return false;
    }
    return true;
}

} // namespace dx12track
