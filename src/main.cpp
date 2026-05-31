// dx12track-ui: Dear ImGui + ImPlot viewer for dx12track.jsonl.
// Win32 + Direct3D11 backend, adapted from the official Dear ImGui
// example_win32_directx11 skeleton.

#include <d3d11.h>
#include <tchar.h>
#include <shellapi.h> // drag-and-drop
#include <cstdlib>    // __argc / __argv
#include <string>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_internal.h" // DockBuilder*
#include "implot.h"

#include "App.h"

// Forward-declared message handler from the Win32 backend.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {

ID3D11Device*           g_device      = nullptr;
ID3D11DeviceContext*    g_context     = nullptr;
IDXGISwapChain*         g_swapchain   = nullptr;
ID3D11RenderTargetView* g_rtv         = nullptr;
dx12track::App*         g_app         = nullptr; // for the WM_DROPFILES handler

void CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swapchain->GetBuffer(0, IID_PPV_ARGS(&back));
    if (back) {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

void CleanupRenderTarget() {
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
    D3D_FEATURE_LEVEL fl;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                      levels, 2, D3D11_SDK_VERSION, &sd, &g_swapchain,
                                      &g_device, &fl, &g_context) != S_OK)
        return false;
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_swapchain) { g_swapchain->Release(); g_swapchain = nullptr; }
    if (g_context)   { g_context->Release();   g_context = nullptr; }
    if (g_device)    { g_device->Release();    g_device = nullptr; }
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return true;
    switch (msg) {
        case WM_SIZE:
            if (g_device && wp != SIZE_MINIMIZED) {
                CleanupRenderTarget();
                g_swapchain->ResizeBuffers(0, (UINT)LOWORD(lp), (UINT)HIWORD(lp),
                                           DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0; // disable ALT app menu
            break;
        case WM_DROPFILES: {
            HDROP drop = (HDROP)wp;
            wchar_t path[1024];
            if (g_app && ::DragQueryFileW(drop, 0, path, 1024)) {
                int n = ::WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
                if (n > 0) {
                    std::string s((size_t)(n - 1), '\0');
                    ::WideCharToMultiByte(CP_UTF8, 0, path, -1, s.data(), n, nullptr, nullptr);
                    g_app->LoadFile(s);
                }
            }
            ::DragFinish(drop);
            return 0;
        }
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProc(hwnd, msg, wp, lp);
}

// Build a sensible default 4-pane layout the first time the dockspace exists.
void BuildDefaultLayout(ImGuiID root, const ImVec2& size) {
    ImGui::DockBuilderRemoveNode(root);
    ImGui::DockBuilderAddNode(root, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(root, size);

    ImGuiID right;
    ImGuiID left = ImGui::DockBuilderSplitNode(root, ImGuiDir_Left, 0.62f, nullptr, &right);
    ImGuiID left_bottom;
    ImGuiID left_top = ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.55f, nullptr, &left_bottom);
    ImGuiID right_bottom;
    // dx12track (top of the right column) gets the larger share so the resolved
    // callstack has room for ~15+ frames.
    ImGuiID right_top = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.58f, nullptr, &right_bottom);

    // Mark the left area as the central node: on window resize (e.g. maximize)
    // the right column keeps its width (from SizeRef) and the graph/allocations
    // area absorbs the extra space. The splitter stays user-draggable.
    if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(left_bottom))
        n->SetLocalFlags(n->LocalFlags | ImGuiDockNodeFlags_CentralNode);

    ImGui::DockBuilderDockWindow("Memory over time",   left_top);
    ImGui::DockBuilderDockWindow("Active allocations", left_bottom);
    ImGui::DockBuilderDockWindow("dx12track",          right_top);
    ImGui::DockBuilderDockWindow("Memory summary",     right_bottom);
    ImGui::DockBuilderFinish(root);
}

} // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    // No arg: let App pick the last opened file, then the default name.
    std::string path = (__argc > 1) ? __argv[1] : std::string();

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0, 0, inst, nullptr, nullptr,
                       nullptr, nullptr, L"dx12track_ui", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"dx12track UI",
                                WS_OVERLAPPEDWINDOW, 100, 100, 1480, 900,
                                nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);
    ::DragAcceptFiles(hwnd, TRUE); // accept .jsonl files dropped onto the window

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // We rebuild the dock layout programmatically every run, so don't persist
    // it: a saved layout (after the user rearranges panels) can reload with
    // two central nodes and trip an ImGui assert on startup.
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    dx12track::App app(path);
    g_app = &app;

    bool layout_built = false;
    bool running = true;
    while (running) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        if (app.live_tail())
            app.trace().PollTail();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Fullscreen dockspace host.
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGuiWindowFlags host_flags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoDocking;
        ImGui::Begin("##dockhost", nullptr, host_flags);
        ImGui::PopStyleVar(3);
        ImGuiID dock_id = ImGui::GetID("dockspace");
        ImGui::DockSpace(dock_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
        if (!layout_built) {
            BuildDefaultLayout(dock_id, vp->WorkSize);
            layout_built = true;
        }
        ImGui::End();

        app.Draw();

        ImGui::Render();
        const float clear[4] = { 0.10f, 0.10f, 0.12f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapchain->Present(1, 0); // vsync
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}
