#pragma once
//
// Small formatting helpers shared across the UI. Mirrors the byte/time
// formatting style used by the launcher console renderer
// (dx12track/src/launcher/Renderer.cpp:17-35) but returns std::string so it
// drops straight into ImGui text calls.
//
#include <cstdint>
#include <cstdio>
#include <string>

namespace dx12track {

// "  1.25 MB", "65.00 KB", "512 B" ...
inline std::string FormatBytes(uint64_t bytes) {
    char buf[32];
    if (bytes >= (uint64_t)1 << 30)
        std::snprintf(buf, sizeof buf, "%.2f GB", (double)bytes / (double)((uint64_t)1 << 30));
    else if (bytes >= (uint64_t)1 << 20)
        std::snprintf(buf, sizeof buf, "%.2f MB", (double)bytes / (double)((uint64_t)1 << 20));
    else if (bytes >= (uint64_t)1 << 10)
        std::snprintf(buf, sizeof buf, "%.2f KB", (double)bytes / (double)((uint64_t)1 << 10));
    else
        std::snprintf(buf, sizeof buf, "%llu B", (unsigned long long)bytes);
    return buf;
}

// "1:02:03.456" elapsed wall clock from a nanosecond count.
inline std::string FormatNs(uint64_t ns) {
    uint64_t total_ms = ns / 1'000'000ull;
    uint64_t ms = total_ms % 1000;
    uint64_t total_s = total_ms / 1000;
    uint64_t h = total_s / 3600;
    uint64_t m = (total_s % 3600) / 60;
    uint64_t s = total_s % 60;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%llu:%02llu:%02llu.%03llu",
                  (unsigned long long)h, (unsigned long long)m,
                  (unsigned long long)s, (unsigned long long)ms);
    return buf;
}

// Seconds (float) from process start — used as the ImPlot X axis unit.
inline double NsToSeconds(uint64_t ns, uint64_t start_ns) {
    return (double)((ns >= start_ns) ? (ns - start_ns) : 0) / 1e9;
}

} // namespace dx12track
