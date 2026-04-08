/*
 * OpenA2DP - Bluetooth A2DP control tool
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * renderer.cpp - D3D11 device/swapchain + imgui backend integration
 *
 * This is the only C++ file in the app module.  It uses the C++ imgui
 * API and backends directly, exposing a plain-C interface (renderer.h)
 * so that the rest of the application stays pure C.
 */

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <d3d11.h>
#include <windows.h>

#include "renderer.h"

/* ── D3D11 state ────────────────────────────────────────────────────── */

static ID3D11Device            *g_device    = nullptr;
static ID3D11DeviceContext     *g_context   = nullptr;
static IDXGISwapChain          *g_swapchain = nullptr;
static ID3D11RenderTargetView  *g_rtv       = nullptr;
static bool                     g_occluded  = false;
static UINT                     g_resize_w  = 0;
static UINT                     g_resize_h  = 0;

static const float g_clear[4] = { 0.10f, 0.10f, 0.12f, 1.00f };

/* ── helpers ────────────────────────────────────────────────────────── */

static void create_render_target()
{
    ID3D11Texture2D *buf = nullptr;
    g_swapchain->GetBuffer(0, IID_PPV_ARGS(&buf));
    g_device->CreateRenderTargetView(buf, nullptr, &g_rtv);
    buf->Release();
}

static void cleanup_render_target()
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

static bool create_d3d(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, 2, D3D11_SDK_VERSION,
        &sd, &g_swapchain, &g_device, &got, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            levels, 2, D3D11_SDK_VERSION,
            &sd, &g_swapchain, &g_device, &got, &g_context);
    if (FAILED(hr))
        return false;

    IDXGIFactory *factory = nullptr;
    if (SUCCEEDED(g_swapchain->GetParent(IID_PPV_ARGS(&factory)))) {
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        factory->Release();
    }

    create_render_target();
    return true;
}

static void cleanup_d3d()
{
    cleanup_render_target();
    if (g_swapchain) { g_swapchain->Release(); g_swapchain = nullptr; }
    if (g_context)   { g_context->Release();   g_context   = nullptr; }
    if (g_device)    { g_device->Release();     g_device    = nullptr; }
}

/* ── public C API ───────────────────────────────────────────────────── */

extern "C" int oa2dp_renderer_init(void *hwnd)
{
    if (!create_d3d((HWND)hwnd))
        return -1;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    return 0;
}

extern "C" void oa2dp_renderer_shutdown(void)
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanup_d3d();
}

extern "C" int oa2dp_renderer_begin_frame(void)
{
    if (g_resize_w != 0 && g_resize_h != 0) {
        cleanup_render_target();
        g_swapchain->ResizeBuffers(0, g_resize_w, g_resize_h,
                                   DXGI_FORMAT_UNKNOWN, 0);
        g_resize_w = g_resize_h = 0;
        create_render_target();
    }

    if (g_occluded) {
        if (g_swapchain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            return 0;
        }
        g_occluded = false;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    return 1;
}

extern "C" void oa2dp_renderer_end_frame(void)
{
    ImGui::Render();

    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, g_clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO &io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    HRESULT hr = g_swapchain->Present(1, 0);
    g_occluded = (hr == DXGI_STATUS_OCCLUDED);
}

extern "C" void oa2dp_renderer_resize(unsigned int w, unsigned int h)
{
    g_resize_w = w;
    g_resize_h = h;
}

/* Forward declare from imgui_impl_win32.cpp */
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

extern "C" int oa2dp_renderer_wndproc(void *hwnd, unsigned int msg,
                                      uintptr_t wparam, intptr_t lparam)
{
    LRESULT r = ImGui_ImplWin32_WndProcHandler(
        (HWND)hwnd, msg, (WPARAM)wparam, (LPARAM)lparam);
    return r ? 1 : 0;
}
