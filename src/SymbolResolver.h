#pragma once
//
// SymbolResolver: turns stack return addresses into function names using
// raw_pdb, lazily and on demand. PDBs are parsed once per module and the
// extracted (rva -> name) tables are cached, so repeated lookups are cheap.
//
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace dx12track {

class Trace;
struct Module;

struct ResolvedFrame {
    uint64_t    address = 0;
    std::string module;   // module base name (e.g. "ModelViewer.exe"), or ""
    std::string symbol;   // "Func+0x12", "module+0x1234", or "" if unknown
    bool        resolved = false; // true if a function symbol was found
    bool        pdb_mismatch = false; // a PDB was on disk but didn't match the capture
    std::string note;     // diagnostic for the mismatch (empty otherwise)
};

class SymbolResolver {
public:
    // Resolve each address against the trace's loaded modules.
    std::vector<ResolvedFrame> Resolve(const std::vector<uint64_t>& addrs,
                                       const Trace& trace);
    // Drop cached PDB data (e.g. after loading a different trace).
    void Clear() { cache_.clear(); }

private:
    struct Sym { uint32_t rva; std::string name; };
    struct ModuleSyms {
        bool                loaded   = false; // we attempted to load the PDB
        bool                have_pdb = false; // a usable PDB was found
        bool                mismatch = false; // PDB on disk, but GUID/age != capture
        std::string         pdb_path;         // resolved path, for diagnostics
        std::string         want_guid, disk_guid; // capture vs on-disk GUID
        uint32_t            want_age = 0, disk_age = 0;
        std::vector<Sym>    syms;             // sorted by rva
    };

    const ModuleSyms& GetOrLoad(const Module& m);

    std::unordered_map<uint64_t, ModuleSyms> cache_; // key: module base
};

} // namespace dx12track
