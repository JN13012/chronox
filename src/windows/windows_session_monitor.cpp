#include "windows_session_monitor.h"

#include <wtsapi32.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace chronox {
namespace {

constexpr wchar_t WindowClassName[] = L"ChronoxSessionMonitorWindow";
constexpr UINT_PTR TimerId = 1;

std::runtime_error windowsError(const char* operation, DWORD error) {
    return std::runtime_error(std::string(operation) + " failed with Windows error " +
                              std::to_string(error));
}

} // namespace

WindowsSessionMonitor::WindowsSessionMonitor(EventHandler onEvent, StopHandler onStop,
                                             TickHandler onTick)
    : onEvent_(std::move(onEvent)), onStop_(std::move(onStop)),
      onTick_(std::move(onTick)) {}

WindowsSessionMonitor::~WindowsSessionMonitor() {
    if (window_ != nullptr && IsWindow(window_)) {
        releaseWindowResources(window_);
        DestroyWindow(window_);
    }
    window_ = nullptr;
}

bool WindowsSessionMonitor::queryInitiallyLocked() {
    LPWSTR buffer = nullptr;
    DWORD bytesReturned = 0;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION,
                                     WTSSessionInfoEx, &buffer, &bytesReturned)) {
        throw windowsError("WTSQuerySessionInformationW", GetLastError());
    }

    bool locked = false;
    if (bytesReturned < sizeof(WTSINFOEXW)) {
        WTSFreeMemory(buffer);
        throw windowsError("WTSQuerySessionInformationW returned incomplete session state",
                           ERROR_INVALID_DATA);
    }

    const auto* info = reinterpret_cast<const WTSINFOEXW*>(buffer);
    if (info->Level != 1) {
        WTSFreeMemory(buffer);
        throw windowsError("WTSQuerySessionInformationW returned an unsupported session state",
                           ERROR_INVALID_DATA);
    }

    switch (info->Data.WTSInfoExLevel1.SessionFlags) {
    case WTS_SESSIONSTATE_LOCK:
        locked = true;
        break;
    case WTS_SESSIONSTATE_UNLOCK:
        locked = false;
        break;
    default:
        WTSFreeMemory(buffer);
        throw windowsError("WTSQuerySessionInformationW returned an unknown lock state",
                           ERROR_INVALID_DATA);
    }

    WTSFreeMemory(buffer);
    return locked;
}

void WindowsSessionMonitor::start() {
    if (window_ != nullptr) {
        throw windowsError("WindowsSessionMonitor::start", ERROR_ALREADY_INITIALIZED);
    }

    const HWND window = createWindow();
    if (window == nullptr) {
        throw windowsError("CreateWindowExW", GetLastError());
    }

    if (!WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION)) {
        const DWORD error = GetLastError();
        destroyWindow();
        throw windowsError("WTSRegisterSessionNotification", error);
    }
    registeredForSessionNotifications_ = true;

    if (SetTimer(window, TimerId, 60'000, nullptr) == 0) {
        const DWORD error = GetLastError();
        releaseWindowResources(window_);
        destroyWindow();
        throw windowsError("SetTimer", error);
    }
    timerSet_ = true;

    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
}

HWND WindowsSessionMonitor::createWindow() {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &WindowsSessionMonitor::windowProcedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = WindowClassName;

    if (RegisterClassExW(&windowClass) == 0) {
        const DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            throw windowsError("RegisterClassExW", error);
        }
    }

    const HWND window =
        CreateWindowExW(0, WindowClassName, L"", 0, 0, 0, 0, 0, nullptr,
                        nullptr, windowClass.hInstance, this);
    if (window != nullptr) {
        window_ = window;
    }
    return window;
}

void WindowsSessionMonitor::destroyWindow() {
    const HWND window = window_;
    if (window == nullptr || !IsWindow(window)) {
        window_ = nullptr;
        registeredForSessionNotifications_ = false;
        timerSet_ = false;
        return;
    }

    releaseWindowResources(window);
    if (!DestroyWindow(window)) {
        throw windowsError("DestroyWindow", GetLastError());
    }
}

void WindowsSessionMonitor::releaseWindowResources(HWND window) {
    if (window == nullptr || !IsWindow(window)) {
        registeredForSessionNotifications_ = false;
        timerSet_ = false;
        return;
    }

    if (registeredForSessionNotifications_) {
        WTSUnRegisterSessionNotification(window);
        registeredForSessionNotifications_ = false;
    }
    if (timerSet_) {
        KillTimer(window, TimerId);
        timerSet_ = false;
    }
}

void WindowsSessionMonitor::run() {
    MSG message{};
    while (true) {
        const int result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            throw windowsError("GetMessageW", GetLastError());
        }
        if (result == 0) {
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

LRESULT CALLBACK WindowsSessionMonitor::windowProcedure(HWND window, UINT message,
                                                         WPARAM wParam, LPARAM lParam) {
    auto* monitor = reinterpret_cast<WindowsSessionMonitor*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* createInfo = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        if (createInfo == nullptr || createInfo->lpCreateParams == nullptr) {
            return FALSE;
        }
        monitor = static_cast<WindowsSessionMonitor*>(createInfo->lpCreateParams);
        SetLastError(ERROR_SUCCESS);
        if (SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(monitor)) == 0 &&
            GetLastError() != ERROR_SUCCESS) {
            return FALSE;
        }
    }
    if (monitor != nullptr) {
        return monitor->handleMessage(window, message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT WindowsSessionMonitor::handleMessage(HWND window, UINT message,
                                             WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_LOCK) {
            onEvent_(SessionEvent::Locked);
        } else if (wParam == WTS_SESSION_UNLOCK) {
            onEvent_(SessionEvent::Unlocked);
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMSUSPEND) {
            onEvent_(SessionEvent::Suspended);
        } else if (wParam == PBT_APMRESUMECRITICAL ||
                   wParam == PBT_APMRESUMEAUTOMATIC ||
                   wParam == PBT_APMRESUMESUSPEND) {
            onEvent_(SessionEvent::Resumed);
        }
        return TRUE;
    case WM_TIMER:
        if (wParam == TimerId) {
            onTick_();
        }
        return 0;
    case WM_ENDSESSION:
        if (wParam != 0) {
            onStop_();
            PostQuitMessage(0);
        }
        return 0;
    case WM_CLOSE:
        onStop_();
        if (window != nullptr && IsWindow(window)) {
            DestroyWindow(window);
        }
        return 0;
    case WM_DESTROY:
        releaseWindowResources(window);
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY: {
        releaseWindowResources(window);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        if (window_ == window) {
            window_ = nullptr;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

} // namespace chronox
