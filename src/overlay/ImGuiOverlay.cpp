// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "overlay/ImGuiOverlay.h"

#include <Windows.h>

#define D3D12_FEATURE_DATA_D3D12_OPTIONS D3D12_FEATURE_DATA_D3D12_OPTIONS_LEGACY
#define D3D12_FEATURE_DATA_ARCHITECTURE D3D12_FEATURE_DATA_ARCHITECTURE_LEGACY
#define D3D12_RAYTRACING_GEOMETRY_DESC D3D12_RAYTRACING_GEOMETRY_DESC_LEGACY
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#undef D3D12_FEATURE_DATA_D3D12_OPTIONS
#undef D3D12_FEATURE_DATA_ARCHITECTURE
#undef D3D12_RAYTRACING_GEOMETRY_DESC

#include <MinHook.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

#include <array>
#include <atomic>
#include <cfloat>
#include <filesystem>
#include <mutex>


#include "input/MenuInputGuard.h"
#include "ll/api/mod/NativeMod.h"
#include "plugin/LHolo.h"
#include "structure/StructureLoader.h"
#include "ui/FluentTheme.h"


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM,
                                                             LPARAM);

namespace lholo::overlay {
namespace {

using PresentFn = HRESULT(__stdcall *)(IDXGISwapChain *, UINT, UINT);
using Present1Fn = HRESULT(__stdcall *)(IDXGISwapChain1 *, UINT, UINT,
                                        DXGI_PRESENT_PARAMETERS const *);
using ResizeBuffersFn = HRESULT(__stdcall *)(IDXGISwapChain *, UINT, UINT, UINT,
                                             DXGI_FORMAT, UINT);
using ResizeBuffers1Fn = HRESULT(__stdcall *)(IDXGISwapChain3 *, UINT, UINT,
                                              UINT, DXGI_FORMAT, UINT,
                                              UINT const *, IUnknown *const *);
using ExecuteCommandListsFn = void(__stdcall *)(ID3D12CommandQueue *, UINT,
                                                ID3D12CommandList *const *);

PresentFn gOriginalPresent{};
Present1Fn gOriginalPresent1{};
ResizeBuffersFn gOriginalResizeBuffers{};
ResizeBuffers1Fn gOriginalResizeBuffers1{};
ExecuteCommandListsFn gOriginalExecuteCommandLists{};

void *gPresentTarget{};
void *gPresent1Target{};
void *gResizeTarget{};
void *gResize1Target{};
void *gExecuteTarget{};

ID3D11Device *gDevice{};
ID3D11DeviceContext *gDeviceContext{};
ID3D11On12Device *gDevice11On12{};
ID3D12CommandQueue *gGameQueue{};
// Weak identity of the swap chain currently owned by LHolo. Access is guarded
// by gResourceMutex; retaining it avoids extending the game's COM lifetime.
IDXGISwapChain *gActiveSwapChain{};

HWND gWindow{};
WNDPROC gOriginalWndProc{};
std::atomic_bool gInstalled{false};
std::atomic_bool gShuttingDown{false};
std::atomic_bool gRendering{false};
std::atomic_ullong gGraphicsResumeAt{};
std::mutex gResourceMutex;
bool gImGuiInitialized{};
bool gGraphicsInitialized{};
bool gGuiVisibleLastFrame{};
std::atomic_bool gMouseHandoffActive{};
// ImGui's Win32 backend sets the native cursor handle to null while drawing
// its software cursor. LHolo can skip NewFrame entirely after the menu closes,
// so restore the arrow handle on the window thread without touching the
// ShowCursor display counter owned by Minecraft.
constexpr UINT kMsgRestoreNativeCursor = WM_APP + 0x101;
std::array<bool, 256> gGameKeysDown{};
std::array<bool, 5> gGameMouseButtonsDown{};
std::atomic_bool gConsumeEscapeRelease{false};

constexpr ULONGLONG kFullscreenGraphicsResumeDelayMs = 750;
constexpr ULONGLONG kResizeGraphicsResumeDelayMs = 100;
constexpr size_t kPresentVtableIndex = 8;
constexpr size_t kResizeBuffersVtableIndex = 13;
constexpr size_t kPresent1VtableIndex = 22;
constexpr size_t kResizeBuffers1VtableIndex = 39;
constexpr size_t kExecuteCommandListsVtableIndex = 10;

auto &logger() { return LHolo::getInstance().getSelf().getLogger(); }

void logGraphicsFailure(IDXGISwapChain *swapChain, char const *operation,
                        HRESULT result) {
  HRESULT removedReason = S_OK;
  ID3D12Device *device12{};
  if (swapChain &&
      SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D12Device),
                                     reinterpret_cast<void **>(&device12)))) {
    removedReason = device12->GetDeviceRemovedReason();
    device12->Release();
  }
  logger().error("ImGui graphics call {} failed: HRESULT=0x{:08X}, "
                 "deviceRemovedReason=0x{:08X}",
                 operation, static_cast<unsigned int>(result),
                 static_cast<unsigned int>(removedReason));
}

HWND findProcessWindow() {
  struct Search {
    DWORD pid;
    HWND result;
  } search{GetCurrentProcessId(), nullptr};
  EnumWindows(
      [](HWND window, LPARAM parameter) -> BOOL {
        auto &state = *reinterpret_cast<Search *>(parameter);
        DWORD pid{};
        GetWindowThreadProcessId(window, &pid);
        if (pid == state.pid && IsWindowVisible(window) &&
            GetWindow(window, GW_OWNER) == nullptr) {
          state.result = window;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  return search.result;
}

void releaseGraphicsBackend() {
  if (gGraphicsInitialized) {
    ImGui_ImplDX11_Shutdown();
    gGraphicsInitialized = false;
  }
  if (gDeviceContext) {
    ID3D11RenderTargetView *empty{};
    gDeviceContext->OMSetRenderTargets(1, &empty, nullptr);
    gDeviceContext->ClearState();
    gDeviceContext->Flush();
  }
  if (gDevice11On12)
    gDevice11On12->Release();
  if (gDeviceContext)
    gDeviceContext->Release();
  if (gDevice)
    gDevice->Release();
  gDevice11On12 = nullptr;
  gDeviceContext = nullptr;
  gDevice = nullptr;
}

bool canUseSwapChainLocked(IDXGISwapChain *swapChain) {
  if (!swapChain)
    return false;
  if (!gActiveSwapChain || gActiveSwapChain == swapChain)
    return true;
  if (gGraphicsInitialized || !gWindow)
    return false;

  // A fullscreen/device transition may replace the swap-chain object while
  // retaining the game window. Permit that explicit handoff, but never let
  // a composition/off-screen chain from another mod claim the overlay.
  DXGI_SWAP_CHAIN_DESC description{};
  return SUCCEEDED(swapChain->GetDesc(&description)) &&
         description.OutputWindow == gWindow;
}

// Callers must hold gResourceMutex across both helpers and the original DXGI
// resize call so Present cannot rebuild against a swap chain mid-transition.
void prepareForSwapChainResizeLocked() {
  if (gGraphicsInitialized)
    releaseGraphicsBackend();
}

void deferGraphicsResumeAfterSwapChainResizeLocked() {
  // Window-edge dragging can issue a burst of resizes. Debounce backend
  // recreation and let the first stable Present rebuild it lazily. Never
  // shorten the longer suspension already scheduled by an F11 transition.
  auto const resizeResumeAt = GetTickCount64() + kResizeGraphicsResumeDelayMs;
  auto const currentResumeAt =
      gGraphicsResumeAt.load(std::memory_order_acquire);
  if (currentResumeAt < resizeResumeAt) {
    gGraphicsResumeAt.store(resizeResumeAt, std::memory_order_release);
  }
}

void loadFonts() {
  auto &io = ImGui::GetIO();
  ImFontConfig config{};
  config.OversampleH = 2;
  config.OversampleV = 2;
  auto const chineseFont = "C:\\Windows\\Fonts\\msyh.ttc";
  if (std::filesystem::exists(chineseFont)) {
    // Build the atlas at 2x the logical base size. A 4K/default 2x UI can
    // then render at native font resolution instead of magnifying an 18px
    // atlas, which made text and navigation edges look pixelated.
    io.Fonts->AddFontFromFileTTF(chineseFont, 36.0f, &config,
                                 io.Fonts->GetGlyphRangesChineseFull());
  } else {
    io.Fonts->AddFontDefault();
  }

  // Chinese glyph ranges do not include the warning sign used by the
  // experimental-feature notice. Merge that single glyph from Windows'
  // symbol font so the original label is rendered instead of as '?'.
  auto const symbolFont = "C:\\Windows\\Fonts\\seguisym.ttf";
  if (std::filesystem::exists(symbolFont)) {
    static constexpr ImWchar warningGlyphRange[]{0x26A0, 0x26A0, 0};
    ImFontConfig symbolConfig{};
    symbolConfig.MergeMode = true;
    symbolConfig.PixelSnapH = true;
    symbolConfig.OversampleH = 2;
    symbolConfig.OversampleV = 2;
    io.Fonts->AddFontFromFileTTF(symbolFont, 36.0f, &symbolConfig,
                                 warningGlyphRange);
  }
}

LRESULT forwardToGame(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
      wParam < gGameKeysDown.size()) {
    gGameKeysDown[static_cast<std::size_t>(wParam)] = true;
  } else if ((message == WM_KEYUP || message == WM_SYSKEYUP) &&
             wParam < gGameKeysDown.size()) {
    gGameKeysDown[static_cast<std::size_t>(wParam)] = false;
  }
  switch (message) {
  case WM_LBUTTONDOWN:
    gGameMouseButtonsDown[0] = true;
    break;
  case WM_LBUTTONUP:
    gGameMouseButtonsDown[0] = false;
    break;
  case WM_RBUTTONDOWN:
    gGameMouseButtonsDown[1] = true;
    break;
  case WM_RBUTTONUP:
    gGameMouseButtonsDown[1] = false;
    break;
  case WM_MBUTTONDOWN:
    gGameMouseButtonsDown[2] = true;
    break;
  case WM_MBUTTONUP:
    gGameMouseButtonsDown[2] = false;
    break;
  case WM_XBUTTONDOWN:
    gGameMouseButtonsDown[GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 3 : 4] =
        true;
    break;
  case WM_XBUTTONUP:
    gGameMouseButtonsDown[GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? 3 : 4] =
        false;
    break;
  default:
    break;
  }
  return gOriginalWndProc ? CallWindowProcW(gOriginalWndProc, window, message,
                                            wParam, lParam)
                          : DefWindowProcW(window, message, wParam, lParam);
}

void releaseGameInput(HWND window) {
  // Minecraft has already seen these down events. Send matching releases
  // before the menu starts swallowing input, otherwise movement/use remains
  // latched after the physical key is released while ImGui is open.
  input::MenuInputHandoffScope inputHandoff;
  for (std::size_t key = 0; key < gGameKeysDown.size(); ++key) {
    if (!gGameKeysDown[key])
      continue;
    auto const virtualKey = static_cast<UINT>(key);
    auto scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
    auto const extended = virtualKey == VK_LEFT || virtualKey == VK_UP ||
                          virtualKey == VK_RIGHT || virtualKey == VK_DOWN ||
                          virtualKey == VK_PRIOR || virtualKey == VK_NEXT ||
                          virtualKey == VK_END || virtualKey == VK_HOME ||
                          virtualKey == VK_INSERT || virtualKey == VK_DELETE ||
                          virtualKey == VK_DIVIDE || virtualKey == VK_NUMLOCK;
    LPARAM keyUp =
        1 | (static_cast<LPARAM>(scanCode) << 16) | (1LL << 30) | (1LL << 31);
    if (extended)
      keyUp |= 1LL << 24;
    auto const systemKey = virtualKey == VK_MENU || virtualKey == VK_LMENU ||
                           virtualKey == VK_RMENU;
    forwardToGame(window, systemKey ? WM_SYSKEYUP : WM_KEYUP, virtualKey,
                  keyUp);
  }

  POINT cursor{};
  GetCursorPos(&cursor);
  ScreenToClient(window, &cursor);
  auto const mousePosition = MAKELPARAM(cursor.x, cursor.y);
  constexpr std::array<UINT, 5> upMessages{
      WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP, WM_XBUTTONUP, WM_XBUTTONUP};
  for (std::size_t button = 0; button < gGameMouseButtonsDown.size();
       ++button) {
    if (!gGameMouseButtonsDown[button])
      continue;
    WPARAM buttonParam{};
    if (button == 3)
      buttonParam = MAKEWPARAM(0, XBUTTON1);
    if (button == 4)
      buttonParam = MAKEWPARAM(0, XBUTTON2);
    forwardToGame(window, upMessages[button], buttonParam, mousePosition);
  }
}

bool confineMouseToClientCenter(HWND window) {
  RECT clientRect{};
  if (!window || !GetClientRect(window, &clientRect))
    return false;
  POINT topLeft{clientRect.left, clientRect.top};
  POINT bottomRight{clientRect.right, clientRect.bottom};
  if (!ClientToScreen(window, &topLeft) ||
      !ClientToScreen(window, &bottomRight))
    return false;
  RECT screenRect{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
  if (!ClipCursor(&screenRect))
    return false;
  return SetCursorPos((screenRect.left + screenRect.right) / 2,
                      (screenRect.top + screenRect.bottom) / 2) != FALSE;
}

void prepareMouseHandoff(HWND window) {
  if (!window)
    return;

  // ImGui keeps button/position state independently from Win32. Clear it
  // before Minecraft resumes relative mouse input so the closing click can
  // never survive into the first gameplay frame.
  if (gImGuiInitialized && ImGui::GetCurrentContext()) {
    auto &io = ImGui::GetIO();
    for (bool &down : io.MouseDown)
      down = false;
    io.MouseWheel = 0.0f;
    io.MouseWheelH = 0.0f;
    io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
  }
  gGameMouseButtonsDown.fill(false);

  // A full-screen menu can leave the absolute OS cursor anywhere. Bedrock
  // converts that absolute position back to relative-look input when it
  // captures the mouse again; centering first prevents a one-frame camera
  // jump proportional to the distance from the menu button to the center.
  if (confineMouseToClientCenter(window)) {
    gMouseHandoffActive.store(true, std::memory_order_release);
  }
}

void maintainMouseHandoff(HWND window) {
  if (!gMouseHandoffActive.load(std::memory_order_acquire) || !window)
    return;
  if (!structure::isInputTransitionBlocked()) {
    // Keep the client-area clip installed after the transition. Minecraft
    // replaces it itself when opening one of its own UI screens, while
    // releasing it here left the cursor free to reach the title-bar close
    // button before gameplay input had recaptured it.
    gMouseHandoffActive.store(false, std::memory_order_release);
    return;
  }
  confineMouseToClientCenter(window);
}

bool isMenuInputMessage(UINT message) {
  switch (message) {
  case WM_INPUT:
  case WM_INPUT_DEVICE_CHANGE:
  case WM_MOUSEMOVE:
  case WM_LBUTTONDOWN:
  case WM_LBUTTONUP:
  case WM_RBUTTONDOWN:
  case WM_RBUTTONUP:
  case WM_MBUTTONDOWN:
  case WM_MBUTTONUP:
  case WM_XBUTTONDOWN:
  case WM_XBUTTONUP:
  case WM_MOUSEWHEEL:
  case WM_MOUSEHWHEEL:
  case WM_KEYDOWN:
  case WM_KEYUP:
  case WM_SYSKEYDOWN:
  case WM_SYSKEYUP:
  case WM_CHAR:
    return true;
  default:
    return false;
  }
}

bool isFullscreenKeyMessage(UINT message, WPARAM wParam) {
  return wParam == VK_F11 &&
         (message == WM_KEYDOWN || message == WM_KEYUP ||
          message == WM_SYSKEYDOWN || message == WM_SYSKEYUP);
}

LRESULT consumeMenuInputMessage(HWND window, UINT message, WPARAM wParam,
                                LPARAM lParam) {
  if (message == WM_INPUT) {
    // Foreground RIM_INPUT must reach DefWindowProc for User32 cleanup,
    // but must not be forwarded to Minecraft's original WndProc.
    return DefWindowProcW(window, message, wParam, lParam);
  }
  return 1;
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam,
                            LPARAM lParam) {
  if (message == kMsgRestoreNativeCursor) {
    ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
    return 0;
  }
  if (message == WM_KILLFOCUS ||
      (message == WM_ACTIVATEAPP && wParam == FALSE)) {
    structure::resetHotkeyState();
    gMouseHandoffActive.store(false, std::memory_order_release);
    ClipCursor(nullptr);
  }
  if (!gShuttingDown.load(std::memory_order_acquire) && message == WM_KEYDOWN &&
      wParam == VK_F11 && (lParam & (1LL << 30)) == 0) {
    // Fullscreen transition may replace or resize the swap-chain buffers.
    // Tear down the whole D3D11On12 side before Minecraft handles F11; a
    // ResizeBuffers hook alone is too late for renderer paths that start
    // their transition directly from the window message.
    {
      std::lock_guard lock(gResourceMutex);
      releaseGraphicsBackend();
      gGraphicsResumeAt.store(GetTickCount64() +
                                  kFullscreenGraphicsResumeDelayMs,
                              std::memory_order_release);
    }
    logger().info("ImGui graphics backend suspended for fullscreen transition");
  }
  auto const guiWasVisible = structure::isGuiVisible();
  if (!gShuttingDown.load(std::memory_order_acquire) && gImGuiInitialized &&
      (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
      structure::handleGuiHotkeyKeyDown(static_cast<unsigned int>(wParam))) {
    if (!guiWasVisible && structure::isGuiVisible())
      releaseGameInput(window);
    if (guiWasVisible && !structure::isGuiVisible())
      confineMouseToClientCenter(window);
    return 1;
  }
  if (!gShuttingDown.load(std::memory_order_acquire) && gImGuiInitialized &&
      (message == WM_KEYUP || message == WM_SYSKEYUP) &&
      structure::handleGuiHotkeyKeyUp(static_cast<unsigned int>(wParam))) {
    return 1;
  }
  if (!gShuttingDown.load(std::memory_order_acquire) && gImGuiInitialized &&
      (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) &&
      structure::handleMouseWheelDelta(
          static_cast<int>(GET_WHEEL_DELTA_WPARAM(wParam)))) {
    return 1;
  }
  if ((message == WM_KEYUP || message == WM_SYSKEYUP) && wParam == VK_ESCAPE &&
      gConsumeEscapeRelease.exchange(false, std::memory_order_acq_rel)) {
    return 1;
  }
  // Mouse middle/side buttons can be bound as hotkeys too. They arrive as their
  // own window messages, so translate them to virtual-key codes and route them
  // through the same capture/trigger path as the keyboard.
  if (!gShuttingDown.load(std::memory_order_acquire) && gImGuiInitialized) {
    unsigned int mouseKey = 0;
    switch (message) {
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
      mouseKey = VK_MBUTTON;
      break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
      mouseKey =
          GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
      break;
    default:
      break;
    }
    if (mouseKey != 0) {
      if (message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN) {
        if (structure::handleGuiHotkeyKeyDown(mouseKey)) {
          if (!guiWasVisible && structure::isGuiVisible())
            releaseGameInput(window);
          if (guiWasVisible && !structure::isGuiVisible())
            confineMouseToClientCenter(window);
          return 1;
        }
      } else if (structure::handleGuiHotkeyKeyUp(mouseKey)) {
        return 1;
      }
    }
  }
  if (!gShuttingDown.load(std::memory_order_acquire) && gImGuiInitialized &&
      structure::isGuiVisible()) {
    gMouseHandoffActive.store(false, std::memory_order_release);
    ClipCursor(nullptr);
    ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
    if (isFullscreenKeyMessage(message, wParam)) {
      return gOriginalWndProc ? CallWindowProcW(gOriginalWndProc, window,
                                                message, wParam, lParam)
                              : DefWindowProcW(window, message, wParam, lParam);
    }
    if (message == WM_KEYDOWN && wParam == VK_ESCAPE) {
      gConsumeEscapeRelease.store(true, std::memory_order_release);
      structure::requestOpenGui();
      confineMouseToClientCenter(window);
      return 1;
    }
    if (isMenuInputMessage(message)) {
      return consumeMenuInputMessage(window, message, wParam, lParam);
    }
  }
  if (structure::isMenuInputCaptured() && isMenuInputMessage(message) &&
      !isFullscreenKeyMessage(message, wParam)) {
    return consumeMenuInputMessage(window, message, wParam, lParam);
  }
  return forwardToGame(window, message, wParam, lParam);
}

void executeCommandListsHook(ID3D12CommandQueue *queue, UINT count,
                             ID3D12CommandList *const *lists) {
  if (!gGameQueue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
    std::lock_guard lock(gResourceMutex);
    if (!gGameQueue) {
      gGameQueue = queue;
      gGameQueue->AddRef();
    }
  }
  gOriginalExecuteCommandLists(queue, count, lists);
}

bool initializeImGui(IDXGISwapChain *swapChain) {
  if (gImGuiInitialized && gGraphicsInitialized)
    return true;
  if (GetTickCount64() < gGraphicsResumeAt.load(std::memory_order_acquire))
    return false;

  // Every failed attempt starts the next Present from a known empty graphics
  // state. This also cleans partial COM objects left by a failed API call.
  releaseGraphicsBackend();
  if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D11Device),
                                     reinterpret_cast<void **>(&gDevice)))) {
    gDevice->GetImmediateContext(&gDeviceContext);
  } else {
    if (!gGameQueue)
      return false;
    ID3D12Device *device12{};
    if (FAILED(swapChain->GetDevice(__uuidof(ID3D12Device),
                                    reinterpret_cast<void **>(&device12))))
      return false;
    auto const result = D3D11On12CreateDevice(
        device12, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
        reinterpret_cast<IUnknown **>(&gGameQueue), 1, 0, &gDevice,
        &gDeviceContext, nullptr);
    device12->Release();
    if (FAILED(result) || !gDevice) {
      releaseGraphicsBackend();
      return false;
    }
    if (FAILED(gDevice->QueryInterface(
            __uuidof(ID3D11On12Device),
            reinterpret_cast<void **>(&gDevice11On12)))) {
      releaseGraphicsBackend();
      return false;
    }
  }

  DXGI_SWAP_CHAIN_DESC description{};
  if (FAILED(swapChain->GetDesc(&description))) {
    releaseGraphicsBackend();
    return false;
  }
  auto const window =
      description.OutputWindow ? description.OutputWindow : findProcessWindow();
  if (!window || (gWindow && window != gWindow)) {
    releaseGraphicsBackend();
    return false;
  }

  if (!gImGuiInitialized) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    loadFonts();
    if (!ImGui_ImplWin32_Init(window)) {
      ui::resetFluentTheme();
      ImGui::DestroyContext();
      releaseGraphicsBackend();
      return false;
    }
    gWindow = window;
    gOriginalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        gWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(windowProc)));
    gImGuiInitialized = true;
    logger().info("Injected Dear ImGui overlay initialized");
  }
  if (!ImGui_ImplDX11_Init(gDevice, gDeviceContext)) {
    releaseGraphicsBackend();
    return false;
  }
  gGraphicsInitialized = true;
  gActiveSwapChain = swapChain;
  logger().info("ImGui graphics backend initialized");
  return true;
}

void render(IDXGISwapChain *swapChain) {
  if (gShuttingDown.load(std::memory_order_acquire))
    return;
  if (gRendering.exchange(true, std::memory_order_acq_rel))
    return;
  struct Reset {
    ~Reset() { gRendering.store(false, std::memory_order_release); }
  } reset;
  std::lock_guard lock(gResourceMutex);
  if (!canUseSwapChainLocked(swapChain))
    return;

  // Initialize the backend and install the WndProc on the first usable
  // Present even while the menu is hidden. This matches ChiyanMap's proven
  // lifecycle: input must be ready before a hotkey can make the UI visible.
  // D3D12 startup may not have exposed its command queue yet, so a failed
  // attempt is intentionally retried on the next Present.
  if (!initializeImGui(swapChain))
    return;
  structure::processPendingActions();
  auto const showGui = structure::isGuiVisible();
  auto const showHud = !showGui && structure::hasHudInfo();
  if (gGuiVisibleLastFrame != showGui) {
    if (!showGui) {
      prepareMouseHandoff(gWindow);
      PostMessageW(gWindow, kMsgRestoreNativeCursor, 0, 0);
    }
  }
  gGuiVisibleLastFrame = showGui;
  if (!showGui)
    maintainMouseHandoff(gWindow);
  ImGui::GetIO().MouseDrawCursor = showGui;
  auto const showHint = structure::actionHintActive();
  if (!showGui && !showHud && !showHint)
    return;

  if (showGui)
    ClipCursor(nullptr);

  auto draw = [](ID3D11RenderTargetView *target) {
    gDeviceContext->OMSetRenderTargets(1, &target, nullptr);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    if (structure::isGuiVisible()) {
      structure::renderGui();
      // The close button changes visibility during this render call,
      // after the frame-level transition check above.
      if (!structure::isGuiVisible()) {
        prepareMouseHandoff(gWindow);
        PostMessageW(gWindow, kMsgRestoreNativeCursor, 0, 0);
        gGuiVisibleLastFrame = false;
      }
    } else {
      structure::renderHud();
      structure::renderMaterialHud();
    }
    structure::renderActionHint();
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ID3D11RenderTargetView *empty{};
    gDeviceContext->OMSetRenderTargets(1, &empty, nullptr);
  };

  if (gDevice11On12) {
    UINT index{};
    IDXGISwapChain3 *swapChain3{};
    if (SUCCEEDED(swapChain->QueryInterface(
            __uuidof(IDXGISwapChain3),
            reinterpret_cast<void **>(&swapChain3)))) {
      index = swapChain3->GetCurrentBackBufferIndex();
      swapChain3->Release();
    }

    ID3D12Resource *backBuffer{};
    auto const getBufferResult =
        swapChain->GetBuffer(index, __uuidof(ID3D12Resource),
                             reinterpret_cast<void **>(&backBuffer));
    if (FAILED(getBufferResult)) {
      logGraphicsFailure(swapChain, "GetBuffer(D3D12)", getBufferResult);
      return;
    }
    ID3D11Resource *wrappedBuffer{};
    D3D11_RESOURCE_FLAGS resourceFlags{D3D11_BIND_RENDER_TARGET};
    auto const wrappedResult = gDevice11On12->CreateWrappedResource(
        backBuffer, &resourceFlags, D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_PRESENT, __uuidof(ID3D11Resource),
        reinterpret_cast<void **>(&wrappedBuffer));
    backBuffer->Release();
    if (FAILED(wrappedResult) || !wrappedBuffer) {
      logGraphicsFailure(swapChain, "CreateWrappedResource", wrappedResult);
      return;
    }

    ID3D11RenderTargetView *target{};
    auto const targetResult =
        gDevice->CreateRenderTargetView(wrappedBuffer, nullptr, &target);
    if (SUCCEEDED(targetResult) && target) {
      gDevice11On12->AcquireWrappedResources(&wrappedBuffer, 1);
      draw(target);
      gDevice11On12->ReleaseWrappedResources(&wrappedBuffer, 1);
      gDeviceContext->Flush();
    } else {
      logGraphicsFailure(swapChain, "CreateRenderTargetView(D3D11On12)",
                         targetResult);
    }
    if (target)
      target->Release();
    wrappedBuffer->Release();
  } else {
    ID3D11Texture2D *backBuffer{};
    auto const getBufferResult = swapChain->GetBuffer(
        0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&backBuffer));
    if (FAILED(getBufferResult)) {
      logGraphicsFailure(swapChain, "GetBuffer(D3D11)", getBufferResult);
      return;
    }
    ID3D11RenderTargetView *target{};
    auto const result =
        gDevice->CreateRenderTargetView(backBuffer, nullptr, &target);
    backBuffer->Release();
    if (SUCCEEDED(result)) {
      draw(target);
      target->Release();
    } else {
      logGraphicsFailure(swapChain, "CreateRenderTargetView(D3D11)", result);
    }
  }
}

HRESULT __stdcall presentHook(IDXGISwapChain *swapChain, UINT interval,
                              UINT flags) {
  render(swapChain);
  return gOriginalPresent(swapChain, interval, flags);
}

HRESULT __stdcall present1Hook(IDXGISwapChain1 *swapChain, UINT interval,
                               UINT flags,
                               DXGI_PRESENT_PARAMETERS const *parameters) {
  render(swapChain);
  return gOriginalPresent1(swapChain, interval, flags, parameters);
}

HRESULT __stdcall resizeHook(IDXGISwapChain *swapChain, UINT count, UINT width,
                             UINT height, DXGI_FORMAT format, UINT flags) {
  std::unique_lock lock(gResourceMutex);
  if (gActiveSwapChain != swapChain) {
    lock.unlock();
    return gOriginalResizeBuffers(swapChain, count, width, height, format,
                                  flags);
  }

  // ResizeBuffers requires every reference to the old buffers to be gone.
  // Although LHolo creates its wrapped back buffer and RTV per frame, the
  // D3D11On12 context may still retain state from the last submission. Use
  // the same full graphics-backend teardown as the proven F11 path, while
  // preserving the ImGui context, Win32 backend and menu state.
  prepareForSwapChainResizeLocked();

  auto const result =
      gOriginalResizeBuffers(swapChain, count, width, height, format, flags);
  deferGraphicsResumeAfterSwapChainResizeLocked();
  if (FAILED(result)) {
    logGraphicsFailure(swapChain, "ResizeBuffers", result);
  }
  return result;
}

HRESULT __stdcall resize1Hook(IDXGISwapChain3 *swapChain, UINT count,
                              UINT width, UINT height, DXGI_FORMAT format,
                              UINT flags, UINT const *creationNodeMask,
                              IUnknown *const *presentQueue) {
  std::unique_lock lock(gResourceMutex);
  if (gActiveSwapChain != static_cast<IDXGISwapChain *>(swapChain)) {
    lock.unlock();
    return gOriginalResizeBuffers1(swapChain, count, width, height, format,
                                   flags, creationNodeMask, presentQueue);
  }
  prepareForSwapChainResizeLocked();
  auto const result =
      gOriginalResizeBuffers1(swapChain, count, width, height, format, flags,
                              creationNodeMask, presentQueue);
  deferGraphicsResumeAfterSwapChainResizeLocked();
  if (FAILED(result)) {
    logGraphicsFailure(swapChain, "ResizeBuffers1", result);
  }
  return result;
}

bool installHook(void *target, void *detour, void **original) {
  return target && MH_CreateHook(target, detour, original) == MH_OK &&
         MH_EnableHook(target) == MH_OK;
}

void removeHook(void *&target) {
  if (!target)
    return;
  MH_DisableHook(target);
  MH_RemoveHook(target);
  target = nullptr;
}

} // namespace

bool ensureInstalled() {
  if (gInstalled.load(std::memory_order_acquire))
    return true;
  gShuttingDown.store(false, std::memory_order_release);
  auto window = findProcessWindow();
  if (!window)
    return false;
  auto const status = MH_Initialize();
  if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
    return false;

  D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
  DXGI_SWAP_CHAIN_DESC description{};
  description.BufferCount = 1;
  description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  description.OutputWindow = window;
  description.SampleDesc.Count = 1;
  description.Windowed = TRUE;
  description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
  ID3D11Device *dummyDevice{};
  ID3D11DeviceContext *dummyContext{};
  IDXGISwapChain *dummySwapChain{};
  if (FAILED(D3D11CreateDeviceAndSwapChain(
          nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &featureLevel, 1,
          D3D11_SDK_VERSION, &description, &dummySwapChain, &dummyDevice,
          nullptr, &dummyContext)))
    return false;

  auto **swapVtable = *reinterpret_cast<void ***>(dummySwapChain);
  gPresentTarget = swapVtable[kPresentVtableIndex];
  gResizeTarget = swapVtable[kResizeBuffersVtableIndex];
  bool ok = installHook(gPresentTarget, reinterpret_cast<void *>(presentHook),
                        reinterpret_cast<void **>(&gOriginalPresent)) &&
            installHook(gResizeTarget, reinterpret_cast<void *>(resizeHook),
                        reinterpret_cast<void **>(&gOriginalResizeBuffers));
  IDXGISwapChain1 *swapChain1{};
  if (SUCCEEDED(dummySwapChain->QueryInterface(
          __uuidof(IDXGISwapChain1), reinterpret_cast<void **>(&swapChain1)))) {
    gPresent1Target =
        (*reinterpret_cast<void ***>(swapChain1))[kPresent1VtableIndex];
    ok = installHook(gPresent1Target, reinterpret_cast<void *>(present1Hook),
                     reinterpret_cast<void **>(&gOriginalPresent1)) &&
         ok;
    swapChain1->Release();
  }
  IDXGISwapChain3 *swapChain3{};
  if (SUCCEEDED(dummySwapChain->QueryInterface(
          __uuidof(IDXGISwapChain3), reinterpret_cast<void **>(&swapChain3)))) {
    gResize1Target =
        (*reinterpret_cast<void ***>(swapChain3))[kResizeBuffers1VtableIndex];
    ok = installHook(gResize1Target, reinterpret_cast<void *>(resize1Hook),
                     reinterpret_cast<void **>(&gOriginalResizeBuffers1)) &&
         ok;
    swapChain3->Release();
  } else {
    ok = false;
  }
  dummySwapChain->Release();
  dummyContext->Release();
  dummyDevice->Release();

  ID3D12Device *dummyDevice12{};
  if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                  __uuidof(ID3D12Device),
                                  reinterpret_cast<void **>(&dummyDevice12)))) {
    D3D12_COMMAND_QUEUE_DESC queueDescription{};
    queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue *dummyQueue{};
    if (SUCCEEDED(dummyDevice12->CreateCommandQueue(
            &queueDescription, __uuidof(ID3D12CommandQueue),
            reinterpret_cast<void **>(&dummyQueue)))) {
      gExecuteTarget = (*reinterpret_cast<void ***>(
          dummyQueue))[kExecuteCommandListsVtableIndex];
      ok =
          installHook(
              gExecuteTarget, reinterpret_cast<void *>(executeCommandListsHook),
              reinterpret_cast<void **>(&gOriginalExecuteCommandLists)) &&
          ok;
      dummyQueue->Release();
    }
    dummyDevice12->Release();
  }

  if (!ok) {
    shutdown();
    return false;
  }
  gInstalled.store(true, std::memory_order_release);
  logger().info("Injected ImGui DXGI hooks installed");
  return true;
}

void shutdown() {
  gShuttingDown.store(true, std::memory_order_release);
  gMouseHandoffActive.store(false, std::memory_order_release);
  ClipCursor(nullptr);
  removeHook(gExecuteTarget);
  removeHook(gResize1Target);
  removeHook(gPresent1Target);
  removeHook(gResizeTarget);
  removeHook(gPresentTarget);
  if (gOriginalWndProc && gWindow && IsWindow(gWindow)) {
    SetWindowLongPtrW(gWindow, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(gOriginalWndProc));
  }
  gOriginalWndProc = nullptr;

  std::lock_guard lock(gResourceMutex);
  releaseGraphicsBackend();
  if (gImGuiInitialized) {
    ImGui_ImplWin32_Shutdown();
    ui::resetFluentTheme();
    ImGui::DestroyContext();
    gImGuiInitialized = false;
  }
  if (gGameQueue)
    gGameQueue->Release();
  gGameQueue = nullptr;
  gActiveSwapChain = nullptr;
  gWindow = nullptr;
  gInstalled.store(false, std::memory_order_release);
}

} // namespace lholo::overlay
