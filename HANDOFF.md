# dx12track-ui — Handoff

A Dear ImGui + ImPlot desktop viewer for `dx12track.jsonl` capture logs (DX12
resource allocation traces). Windows + Direct3D 11. This doc is the catch-up
notes for picking the project up on another machine / in a fresh session.

Repo: `git@github.com:jonasmr/dx12track-ui.git` (branch `main`).

---

## Getting set up

```sh
git clone --recurse-submodules git@github.com:jonasmr/dx12track-ui.git
# or, after a plain clone:
git submodule update --init --recursive
```

Submodules:
- `dx12track/` — the capture tool this UI reads (read-only **reference**; do not modify). Its `src/common/EventTypes.h` is the source of truth for the wire/JSON format.
- `src/third_party/raw_pdb/` — MolecularMatters raw_pdb, used for PDB symbol resolution.

**Build** (VS2022, v143, x64; bundled vcpkg restores deps on first build):
```
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" dx12track-ui.sln /p:Configuration=Debug /p:Platform=x64
```
vcpkg manifest deps: `imgui[dx11-binding,win32-binding,docking-experimental]`, `implot`, `nlohmann-json`. Link libs added in the vcxproj: `d3d11 dxgi d3dcompiler dbghelp comdlg32 shell32`.

**Run:** `build\x64\Debug\dx12track-ui.exe [path.jsonl]`. Symbol resolution needs the captured modules' PDBs present on disk (e.g. the ModelViewer build's `.pdb`).

`old-dx12track.jsonl` (protocol-1 sample) and `dx12track.jsonl` (protocol-2 sample) are at the repo root for testing. `old-dx12track.jsonl` may be untracked — copy it if missing.

---

## Source layout

| File | Responsibility |
|------|----------------|
| `src/main.cpp` | Win32 + DX11 backend, the dockspace + `BuildDefaultLayout`, drag-drop, ImGui setup |
| `src/Trace.{h,cpp}` | Parse JSONL → object model + memory time-series; modules; live-tail; restart detection |
| `src/App.{h,cpp}` | All ImGui/ImPlot windows; filters, tabs, range selection, callstack display, file open/last-file |
| `src/SymbolResolver.{h,cpp}` | raw_pdb-based address→symbol resolution (on demand, cached per module) |
| `src/Format.h` | Byte/time string helpers |

Four docked panels: **Memory over time** (top-left), **Active allocations** (bottom-left), **dx12track** status/callstack (top-right), **Memory summary** (bottom-right).

---

## Data format (from `dx12track/src/common/EventTypes.h`)

One JSON object per line. `hello.protocol` is `1` (no callstacks) or `2`.

- `hello` — `pid`, `protocol`, `qpc_freq`, `exe`
- `module_loaded` *(proto 2)* — `base`(hex str), `size`, `timestamp`, `pdb_age`, `pdb_guid`, `name`, `pdb_name`
- `module_unloaded` *(proto 2)* — `base`
- `created` — `id`, `type`, `alloc`, `heap`, `dim`, `format`, `size`, `parent_heap_id`, `name`, `stack`(hex-string array, **optional**)
- `renamed` — `id`, `name`
- `destroyed` — `id`
- `goodbye` — `exit_code`

Value domains (mirrored as hardcoded lists in `App.cpp` to avoid pulling in the DirectX/Agility headers):
- type: Unknown, Device, Resource, Heap, DescriptorHeap, CommandQueue, CommandAllocator, CommandList, PipelineState, RootSignature, Fence, QueryHeap, CommandSignature
- alloc: None, Committed, Placed, Reserved, Heap
- heap: None, Default, Upload, Readback, Custom, GpuUpload
- dim: Unknown, Buffer, Tex1D, Tex2D, Tex3D

The UI parses the string fields directly — it needs **no** DirectX headers.

---

## Features & the decisions behind them

- **Timeline graph** (ImPlot): Total / by-heap / by-alloc-kind stacked areas; cursor defaults to **peak** memory because a clean capture frees everything by the end (0 live at `goodbye`). 3% Y headroom so the peak isn't flush against the top edge.
- **Split graph** (default ON): a second graph splits host-visible heaps (Upload/Readback) from device-local — useful for dedicated GPUs. Implemented for Total/by-heap; disabled for by-alloc (not heap-separable).
- **Shift-drag range selection** on the timeline (normal drag still pans/zooms). Highlighted band; **Escape** clears it; the click-cursor line is hidden while a range is active. With a range selected, three extra tabs appear next to **Active Allocations**:
  - *Allocations* — created in range, still alive at its end (leak suspects)
  - *Frees* — alive at range start, freed before its end
  - *Alloc&Free* — created and freed within the range (transient churn)
- **Active allocations table**: sortable; text filter on **name** only; multi-select dropdowns for Type/Alloc/Heap/Dim (seeded with the full enum sets). Summary line shows count + total size after filtering. Whole-row selectable.
- **Callstacks** (proto 2): clicking a row resolves that allocation's stack **on demand** via `SymbolResolver` and shows it in the dx12track panel. PDBs are located by recorded path / next to the module / extension-swap, parsed once with raw_pdb (module `S_*PROC32` + public `S_PUB32`), public names undecorated via `UnDecorateSymbolName`, cached per module.
- **File loading**: `Open...` button, drag-drop a `.jsonl` onto the window, default live-tail ON, prompt if the startup file is missing. Startup file order: command-line arg → last-opened (persisted in `%LOCALAPPDATA%\dx12track-ui\last_file.txt`) → `dx12track.jsonl` → prompt.
- **Live-tail restart detection**: if the file shrinks (truncated/replaced) → full reload; a second `hello` mid-stream → reset model. A `Trace::generation()` counter bumps on every fresh capture; `App` watches it to clear per-trace UI state and re-snap to the new peak.

---

## Gotchas / constraints (read before changing things)

- **ImPlot is v1.0** (vcpkg baseline), a *rewritten* API — not the classic 0.x most examples use. Plot calls take a trailing `ImPlotSpec`; per-item style (e.g. fill alpha) is set on that spec, not via `PushStyleVar`. Grep `vcpkg_installed/.../include/implot.h` when unsure of a signature. See also the `implot-v1-api` memory note.
- **`imgui.ini` is disabled** (`io.IniFilename = nullptr` in `main.cpp`). The dock layout is rebuilt programmatically every run by `BuildDefaultLayout`. Persisting it caused a two-central-node assert after the user rearranged panels. Consequence: manual panel/splitter arrangements don't persist across runs.
- **Fixed-width right column on maximize**: the left area is marked the dockspace **central node** (`ImGuiDockNodeFlags_CentralNode` on `left_bottom`). ImGui's resize logic then keeps the non-central (right) column at its `SizeRef` width while the central side absorbs extra space. The splitter is still user-draggable.
- ImGui 1.92: child-border flag is `ImGuiChildFlags_Borders` (plural); table borders likewise.
- raw_pdb sources are compiled directly into the project (see the vcxproj `ItemGroup`); they include `PDB_PCH.h` as a normal header, so no PCH config is needed. Doesn't support `/DEBUG:FASTLINK` PDBs.
- System-module frames (ntdll, kernel32, …) usually have no local PDB → shown as `module+0xRVA`.

---

## Verifying changes

Build, then run against `dx12track.jsonl` (proto 2) and `old-dx12track.jsonl` (proto 1). Quick checks:
- Graph rises then falls to ~0; cursor sits at the peak (~473 MB for the sample).
- Click the `Texture` allocation → callstack resolves to `Texture::Create2D → Graphics::Initialize → … → wWinMainCRTStartup`.
- Shift-drag the startup ramp → the *Allocations*/*Frees*/*Alloc&Free* tabs populate with distinct sets.
- Live-tail: copy the sample to a temp file, tail it, then truncate+rewrite a new `hello` → the UI resets instead of stacking.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
