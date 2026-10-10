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

One JSON object per line. `hello.protocol` is `1` (no callstacks), `2` (modules + stacks), `3` (residency priority) or `4` (ETW join keys). `dx12track/FORMAT.md` is the full spec; every version is additive for JSONL readers, so nothing is rejected by protocol number.

- `hello` — `pid`, `protocol`, `qpc_freq`, `exe`, `qpc_start` *(proto 4: QPC at `ts_ns` 0)*
- `module_loaded` *(proto 2)* — `base`(hex str), `size`, `timestamp`, `pdb_age`, `pdb_guid`, `name`, `pdb_name`
- `module_unloaded` *(proto 2)* — `base`
- `created` — `id`, `ptr` *(proto 4: app-visible interface pointer, hex str)*, `type`, `alloc`, `heap`, `dim`, `format`, `size`, `parent_heap_id`, `parent_heap_ptr` *(proto 3, Placed only)*, `name`, `stack`(hex-string array, **optional**)
- `residency_priority` *(proto 3)* — `id`, `object_ptr`, `priority`, `priority_name`
- `renamed` — `id`, `name`
- `destroyed` — `id`
- `goodbye` — `exit_code`

**ETW sidecar** (`dx12track.exe --etw`): a second file next to the main log, `run.jsonl` → `run.etw.jsonl` (a path not ending in `.jsonl` gets `.etw.jsonl` appended). Same `ts_ns` timeline as the main log but lines are **not** sorted by `ts_ns`; it refers to main-log objects only by `id`. Events: `etw_hello` (first; `pid`/`qpc_start` copied from the main hello), `etw_bind` (`id`, `lib_id`, optional `late`), `location` (`id`, `group` = vram|sys|unknown, `via` = self|heap; on first bind and every change), `driver_size` (`id`, `bytes`), `residency` (`id`, `op` = page_in|page_out), `counters` (1/s, process video-memory counters in bytes), `etw_object`, `etw_object_destroyed`, `etw_diag`, `etw_stats` (last).

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
- **ETW sidecar** (all UI below appears only when one is loaded; otherwise the UI is unchanged apart from an "etw : no ETW sidecar" status line):
  - Loading: `Trace::Load/Reload` derive the sidecar path and load it if present; opening a `*.etw.jsonl` directly opens its main log. Both files are tailed by `PollTail()` through the shared `LineTail` reader; a missing sidecar is re-checked every poll (the launcher may create it late). The sidecar is only read after the main `hello`; an `etw_hello` whose `pid`/`qpc_start` don't match it is a stale file from another run and is ignored (warning in the status panel). Any fresh main capture (reload, shrink, `hello` mid-stream) drops all ETW state and re-reads the sidecar from offset 0; a sidecar shrink or a second `etw_hello` resets ETW state too. Sidecar lines for ids whose `created` hasn't been read yet are buffered (`pending_etw_`) and applied when it arrives.
  - Model: `Obj::etw` holds the location history (sorted by ts on insert), latest `driver_size`, page-in/out counts, bind info. `Obj::LocationAt(t)` → `kLocVram` / `kLocSys` / `kLocUnknown` (ETW said unknown) / `kLocNA` (nothing known at t: unbound, or before the first `location`).
  - UI: `location` column (value at the tab's ref time; tooltip shows via, history, paging, driver size) + `Location` filter (also applies to Save...); VRAM/Sys/Unknown/n/a breakdown in the Memory summary; **By location** timeline mode (stacked, counted memory only, splittable); two timeline overlays, each with a toolbar checkbox (default on): the `local_usage` / `local_budget` counters in every mode (budget is left off-scale if it's far above the data), and VRAM/Sys/Unknown location lines in every mode except By location; neither is drawn in the host-visible split panel; sidecar path, ETW stats and an events/buffers-lost warning in the dx12track panel.
  - The by-location series (`Trace::LocationSeries()`) is built separately from `samples_` by a time-sorted sweep over create/destroy + location changes, because location lines arrive out of order. It is cached on `Trace::data_version()` and only rebuilt when either file changed and the mode is shown.
  - Save... writes `qpc_start` / `ptr` (v4) and, with ETW data, a `"location"` (value at the snapshot time) next to `"category"`.

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
- ETW: there is no real sidecar sample in the repo. To exercise the ETW UI, write a `<name>.etw.jsonl` next to a copy of a log following `dx12track/FORMAT.md` (`etw_hello` with the log's `pid`, then `etw_bind` + `location` lines for some Committed ids); the location column, summary breakdown and **By location** mode should appear.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
