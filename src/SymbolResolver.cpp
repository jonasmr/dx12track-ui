#include "SymbolResolver.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

#include <windows.h> // WIN32_LEAN_AND_MEAN / NOMINMAX come from the project defines
#include <dbghelp.h> // UnDecorateSymbolName

#include "PDB.h"
#include "PDB_RawFile.h"
#include "PDB_InfoStream.h"
#include "PDB_DBIStream.h"

#include "Trace.h"

namespace dx12track {

namespace fs = std::filesystem;

namespace {

// Minimal read-only memory map of a file.
struct MappedFile {
    HANDLE file    = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    void*  base    = nullptr;
    size_t size    = 0;

    bool Open(const std::wstring& path) {
        file = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz{};
        if (!::GetFileSizeEx(file, &sz)) return false;
        size = (size_t)sz.QuadPart;
        mapping = ::CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping) return false;
        base = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        return base != nullptr;
    }
    ~MappedFile() {
        if (base)    ::UnmapViewOfFile(base);
        if (mapping) ::CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE) ::CloseHandle(file);
    }
};

// Public-symbol names are MSVC-decorated ("?Foo@bar@@..."); turn those into a
// readable scope-qualified name. Module symbols are already undecorated.
std::string Undecorate(const char* name) {
    if (!name) return {};
    if (name[0] == '?') {
        char buf[1024];
        DWORD n = ::UnDecorateSymbolName(name, buf, (DWORD)sizeof buf, UNDNAME_NAME_ONLY);
        if (n) return std::string(buf, n);
    }
    return name;
}

std::string BaseName(const std::string& path) {
    size_t s = path.find_last_of("\\/");
    return s == std::string::npos ? path : path.substr(s + 1);
}

// Serialize a PDB GUID the way the capture records it: lowercase 8-4-4-4-12,
// e.g. "442672e8-70df-442a-a793-f04c91ff85e2".
std::string GuidToString(const PDB::GUID& g) {
    char buf[37];
    std::snprintf(buf, sizeof buf,
        "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        g.Data1, g.Data2, g.Data3,
        g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
        g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return buf;
}

// Recorded GUIDs may carry surrounding braces; strip them for comparison.
std::string StripBraces(std::string s) {
    if (s.size() >= 2 && s.front() == '{' && s.back() == '}')
        s = s.substr(1, s.size() - 2);
    return s;
}

bool IEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

// Locate a PDB for the module: prefer the recorded path, else look next to the
// module image, else swap the image's extension for .pdb.
std::string FindPdb(const Module& m) {
    std::error_code ec;
    if (!m.pdb_name.empty() && fs::exists(m.pdb_name, ec) && !fs::is_directory(m.pdb_name, ec))
        return m.pdb_name;

    if (!m.name.empty()) {
        fs::path mod(m.name);
        if (!m.pdb_name.empty()) {
            fs::path cand = mod.parent_path() / fs::path(BaseName(m.pdb_name));
            if (fs::exists(cand, ec)) return cand.string();
        }
        fs::path cand2 = mod; cand2.replace_extension(".pdb");
        if (fs::exists(cand2, ec)) return cand2.string();
    }
    return {};
}

} // namespace

const SymbolResolver::ModuleSyms& SymbolResolver::GetOrLoad(const Module& m) {
    auto it = cache_.find(m.base);
    if (it != cache_.end()) return it->second;

    ModuleSyms& ms = cache_[m.base];
    ms.loaded = true;

    const std::string pdb_path = FindPdb(m);
    if (pdb_path.empty()) return ms;
    ms.pdb_path = pdb_path;

    MappedFile mf;
    if (!mf.Open(fs::path(pdb_path).wstring())) return ms;

    if (PDB::ValidateFile(mf.base, mf.size) != PDB::ErrorCode::Success) return ms;
    const PDB::RawFile rawFile = PDB::CreateRawFile(mf.base);
    if (PDB::HasValidDBIStream(rawFile) != PDB::ErrorCode::Success) return ms;

    const PDB::InfoStream infoStream(rawFile);

    // Reject a PDB that doesn't match the one present at capture time. The
    // GUID+age uniquely identify a build; a same-named/same-path PDB from a
    // later build resolves addresses to the wrong functions, which is worse
    // than no symbols at all. Only enforce when the capture recorded a GUID.
    if (!m.pdb_guid.empty()) {
        const PDB::Header* hdr = infoStream.GetHeader();
        const std::string disk_guid = hdr ? GuidToString(hdr->guid) : std::string();
        const std::string want_guid = StripBraces(m.pdb_guid);
        const bool guid_ok = hdr && IEquals(disk_guid, want_guid);
        // age 0 means the capture didn't record one -> don't gate on it.
        const bool age_ok  = hdr && (m.pdb_age == 0 || hdr->age == m.pdb_age);
        if (!guid_ok || !age_ok) {
            ms.mismatch  = true;
            ms.want_guid = want_guid;
            ms.disk_guid = disk_guid;
            ms.want_age  = m.pdb_age;
            ms.disk_age  = hdr ? hdr->age : 0;
            return ms; // leave have_pdb=false -> frames fall back to module+rva
        }
    }

    if (infoStream.UsesDebugFastLink()) return ms; // /DEBUG:FASTLINK unsupported

    const PDB::DBIStream dbiStream = PDB::CreateDBIStream(rawFile);
    if (dbiStream.HasValidImageSectionStream(rawFile) != PDB::ErrorCode::Success ||
        dbiStream.HasValidSymbolRecordStream(rawFile) != PDB::ErrorCode::Success ||
        dbiStream.HasValidPublicSymbolStream(rawFile) != PDB::ErrorCode::Success)
        return ms;

    const PDB::ImageSectionStream imageSections = dbiStream.CreateImageSectionStream(rawFile);
    const PDB::ModuleInfoStream moduleInfo = dbiStream.CreateModuleInfoStream(rawFile);
    const PDB::CoalescedMSFStream symbolRecords = dbiStream.CreateSymbolRecordStream(rawFile);

    auto add = [&](const char* name, uint32_t rva) {
        if (name && rva) ms.syms.push_back({rva, Undecorate(name)});
    };

    // Function symbols from each module's symbol stream.
    const PDB::ArrayView<PDB::ModuleInfoStream::Module> mods = moduleInfo.GetModules();
    for (const PDB::ModuleInfoStream::Module& mod : mods) {
        if (!mod.HasSymbolStream()) continue;
        const PDB::ModuleSymbolStream sym = mod.CreateSymbolStream(rawFile);
        sym.ForEachSymbol([&](const PDB::CodeView::DBI::Record* rec) {
            using K = PDB::CodeView::DBI::SymbolRecordKind;
            switch (rec->header.kind) {
                case K::S_LPROC32:
                    add(rec->data.S_LPROC32.name,
                        imageSections.ConvertSectionOffsetToRVA(
                            rec->data.S_LPROC32.section, rec->data.S_LPROC32.offset));
                    break;
                case K::S_GPROC32:
                    add(rec->data.S_GPROC32.name,
                        imageSections.ConvertSectionOffsetToRVA(
                            rec->data.S_GPROC32.section, rec->data.S_GPROC32.offset));
                    break;
                case K::S_LPROC32_ID:
                    add(rec->data.S_LPROC32_ID.name,
                        imageSections.ConvertSectionOffsetToRVA(
                            rec->data.S_LPROC32_ID.section, rec->data.S_LPROC32_ID.offset));
                    break;
                case K::S_GPROC32_ID:
                    add(rec->data.S_GPROC32_ID.name,
                        imageSections.ConvertSectionOffsetToRVA(
                            rec->data.S_GPROC32_ID.section, rec->data.S_GPROC32_ID.offset));
                    break;
                default:
                    break;
            }
        });
    }

    // Public symbols cover anything the module streams missed.
    const PDB::PublicSymbolStream publicSyms = dbiStream.CreatePublicSymbolStream(rawFile);
    const PDB::ArrayView<PDB::HashRecord> hashRecords = publicSyms.GetRecords();
    for (const PDB::HashRecord& hr : hashRecords) {
        const PDB::CodeView::DBI::Record* rec = publicSyms.GetRecord(symbolRecords, hr);
        if (rec->header.kind != PDB::CodeView::DBI::SymbolRecordKind::S_PUB32) continue;
        const uint32_t rva = imageSections.ConvertSectionOffsetToRVA(
            rec->data.S_PUB32.section, rec->data.S_PUB32.offset);
        add(rec->data.S_PUB32.name, rva);
    }

    std::sort(ms.syms.begin(), ms.syms.end(),
              [](const Sym& a, const Sym& b) { return a.rva < b.rva; });
    ms.have_pdb = !ms.syms.empty();
    return ms;
}

std::vector<ResolvedFrame> SymbolResolver::Resolve(const std::vector<uint64_t>& addrs,
                                                   const Trace& trace) {
    std::vector<ResolvedFrame> out;
    out.reserve(addrs.size());

    for (uint64_t addr : addrs) {
        ResolvedFrame f;
        f.address = addr;

        const Module* m = trace.ModuleForAddress(addr);
        if (!m) { out.push_back(std::move(f)); continue; }

        f.module = BaseName(m->name);
        const uint32_t rva = (uint32_t)(addr - m->base);

        const ModuleSyms& ms = GetOrLoad(*m);
        if (ms.mismatch) {
            f.pdb_mismatch = true;
            f.note = BaseName(ms.pdb_path) + ": on-disk PDB (guid " + ms.disk_guid +
                     " age " + std::to_string(ms.disk_age) + ") doesn't match the "
                     "capture (guid " + ms.want_guid + " age " +
                     std::to_string(ms.want_age) + ")";
        }
        if (ms.have_pdb) {
            // largest symbol whose rva <= target
            auto it = std::upper_bound(ms.syms.begin(), ms.syms.end(), rva,
                [](uint32_t v, const Sym& s) { return v < s.rva; });
            if (it != ms.syms.begin()) {
                --it;
                uint32_t off = rva - it->rva;
                char buf[32];
                std::snprintf(buf, sizeof buf, "+0x%X", off);
                f.symbol = off ? it->name + buf : it->name;
                f.resolved = true;
            }
        }
        if (f.symbol.empty()) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "+0x%X", rva);
            f.symbol = f.module + buf; // no symbols: show module+rva
        }
        out.push_back(std::move(f));
    }
    return out;
}

} // namespace dx12track
