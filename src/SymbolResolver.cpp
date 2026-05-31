#include "SymbolResolver.h"

#include <algorithm>
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
