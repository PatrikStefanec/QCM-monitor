// Dear ImGui: standalone example application for Windows API + DirectX 11

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include <vector>
#include <chrono>
#include <deque>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <iostream>
#include <memory>
#include <algorithm>
#include <windows.h>
#include <filesystem>

#include "SerialPort.h"
#include "MonitorChannel.h"

#include "imgui.h"
#include "implot.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include "../UI/themes.h"


// Data
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

static std::vector<std::unique_ptr<MonitorChannel>> g_channels;
static int g_nextChannelId = 1;
static bool g_logging = false;
static std::ofstream g_logFile;
static std::string g_logFilePath;
static std::string g_logSavedMessage;
namespace fs = std::filesystem;

static char g_labelBuf[128] = "";

static int g_themeIndex = 0; // 0 = Dark, 1 = Light

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LONG WINAPI CrashLogHandler(EXCEPTION_POINTERS*)
{
    if (g_logFile.is_open())
    {
        g_logFile.flush();
        g_logFile.close();
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static std::string GetLogsDirectory()
{
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);

    fs::path exeDir = fs::path(exePath).parent_path();
    fs::path projectRoot = exeDir.parent_path().parent_path();
    fs::path logsDir = projectRoot / "logs";

    std::error_code ec;
    fs::create_directories(logsDir, ec);

    return logsDir.string();
}

static void ApplyTheme(int themeIndex)
{
    if (themeIndex == 1)
        ImGui::StyleColorsLight();
    // else if (themeIndex == 2) 
    //     Themes::ApplyImGuiPaperAndInkStyle();       
    else
        ImGui::StyleColorsDark();

    // Re-apply the viewport fix every time, since StyleColorsX() resets all colors
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }
}

int main(int, char**)
{
    SetUnhandledExceptionFilter(CrashLogHandler);

    // Make process DPI aware and obtain main monitor scale
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"QCM thickness monitor", WS_OVERLAPPEDWINDOW, 100, 100, (int)(1280 * main_scale), (int)(800 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoAutoMerge = true;   // popped-out windows never merge back into the main window
    io.ConfigViewportsNoTaskBarIcon = false; // (default) give each popped-out window its own taskbar icon
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ApplyTheme(g_themeIndex);
    ImGuiStyle& style = ImGui::GetStyle();
    //ImGui::StyleColorsLight();

    // Setup scaling
    // ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Load Fonts
    // - If fonts are not explicitly loaded, Dear ImGui will select an embedded font: either AddFontDefaultVector() or AddFontDefaultBitmap().
    //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small threshold.
    // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code (e.g. use an assertion, display an error and quit).
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //style.FontSizeBase = 20.0f;
    //io.Fonts->AddFontDefaultVector();
    //io.Fonts->AddFontDefaultBitmap();
    //io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    //IM_ASSERT(font != nullptr);

    // Our state
    bool show_demo_window = false;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Handle window being minimized or screen locked
        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        {
            {
                ImGuiViewport* mainViewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(mainViewport->Pos, ImGuiCond_Always);
                ImGui::SetNextWindowSize(mainViewport->Size, ImGuiCond_Always);
                ImGui::SetNextWindowViewport(mainViewport->ID);
                ImGui::Begin("QCM Thickness Monitoring - Multi Channel", nullptr,
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                            ImGuiWindowFlags_NoBringToFrontOnFocus);

                if (ImGui::Button("Add channel"))
                g_channels.push_back(std::make_unique<MonitorChannel>(g_nextChannelId++));
                ImGui::SameLine();
                ImGui::Text("Channels: %d", (int)g_channels.size());

                ImGui::SameLine();
                if (ImGui::Button(g_logging ? "Stop and save logging" : "Start logging"))
                {
                if (!g_logging)
                {
                    auto now = std::chrono::system_clock::now();
                    std::time_t t = std::chrono::system_clock::to_time_t(now);
                    std::tm tmStruct;
                    localtime_s(&tmStruct, &t);

                    std::ostringstream oss;
                    oss << "tm13_alllog_" << std::put_time(&tmStruct, "%Y%m%d_%H%M%S") << ".csv";
                    g_logFilePath = GetLogsDirectory() + "\\" + oss.str();

                    g_logFile.open(g_logFilePath, std::ios::out);
                    if (g_logFile.is_open())
                    {
                        g_logFile << std::fixed << std::setprecision(4);
                        g_logFile << "Channel,Time_s,Frequency_Hz,Rate5pt_Hz_s,Rate30pt_Hz_s,Rate60pt_Hz_s,OscillatorStatus,SampleNumber,DurationMs,DeviceId,Label\n";
                        g_logging = true;
                        g_logSavedMessage.clear();
                        std::cout << "[TM13Monitor] Logging started: " << g_logFilePath << std::endl;
                    }
                }
                else
                {
                    g_logFile.flush();
                    g_logFile.close();
                    g_logging = false;
                    g_logSavedMessage = "Log \"" + g_logFilePath + "\" was saved successfully.";
                    std::cout << "[TM13Monitor] Logging stopped, saved to: " << g_logFilePath << std::endl;
                }
                }
                ImGui::SameLine();
                {
                    ImVec2 boxSize(20, 20);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImU32 boxColor = g_logging ? IM_COL32(0, 200, 0, 255) : IM_COL32(100, 100, 100, 255);
                    dl->AddRectFilled(p, ImVec2(p.x + boxSize.x, p.y + boxSize.y), boxColor, 3.0f);
                    dl->AddRect(p, ImVec2(p.x + boxSize.x, p.y + boxSize.y), IM_COL32(0, 0, 0, 255), 3.0f);
                    ImGui::Dummy(boxSize);
                }

                if (g_logging)
                    ImGui::TextColored(ImVec4(0.6f, 1, 0.6f, 1), "Logging to: %s", g_logFilePath.c_str());
                else if (!g_logSavedMessage.empty())
                    ImGui::TextColored(ImVec4(0.6f, 1, 0.6f, 1), "%s", g_logSavedMessage.c_str());
                
                ImGui::SameLine();
                ImGui::SetNextItemWidth(250);
                ImGui::InputTextWithHint("##Label", "Label", g_labelBuf, sizeof(g_labelBuf));
                
                ImGui::SameLine();
                const char* themeNames[] = { "Dark", "Light"};
                ImGui::SetNextItemWidth(100);
                if (ImGui::Combo("Theme", &g_themeIndex, themeNames, IM_ARRAYSIZE(themeNames)))
                    ApplyTheme(g_themeIndex);

                ImGui::Separator();

                for (auto& ch : g_channels) ch->Update(&g_logFile, g_logging, g_labelBuf);

                bool anyDockedRenderedYet = false;
                for (size_t i = 0; i < g_channels.size(); ++i)
                {
                    if (g_channels[i]->IsPoppedOut())
                    {
                        g_channels[i]->Render(); // separate OS window, doesn't touch main window layout
                        continue;
                    }

                    if (anyDockedRenderedYet)
                        ImGui::SameLine();

                    g_channels[i]->Render();
                    anyDockedRenderedYet = true;
                }
                g_channels.erase(
                    std::remove_if(g_channels.begin(), g_channels.end(),
                        [](const std::unique_ptr<MonitorChannel>& ch) { return ch->WantsRemoval(); }),
                    g_channels.end());

                ImGui::End();
            }
        }

        // 3. Show another simple window.
        if (show_another_window)
        {
            ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                show_another_window = false;
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        // Update and render additional platform windows (popped-out viewports)
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        // Present
        HRESULT hr = g_pSwapChain->Present(1, 0);   // Present with vsync
        //HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    if (g_logFile.is_open())
    {
        g_logFile.flush();
        g_logFile.close();
    }
    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    // This is a basic setup. Optimally could use e.g. DXGI_SWAP_EFFECT_FLIP_DISCARD and handle fullscreen mode differently. See #8979 for suggestions.
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
