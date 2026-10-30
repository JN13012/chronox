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
    if (registeredForSessionNotifications_ && window_ != nullptr) {
        WTSUnRegisterSessionNotification(window_);
    }
    if (window_ != nullptr && IsWindow(window_)) {
        KillTimer(window_, TimerId);
        DestroyWindow(window_);
    }
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
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &WindowsSessionMonitor::windowProcedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = WindowClassName;

    if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw windowsError("RegisterClassExW", GetLastError());
    }

    window_ = CreateWindowExW(0, WindowClassName, L"", 0, 0, 0, 0, 0,
                              nullptr, nullptr, windowClass.hInstance, this);
    if (window_ == nullptr) {
        throw windowsError("CreateWindowExW", GetLastError());
    }

    if (!WTSRegisterSessionNotification(window_, NOTIFY_FOR_THIS_SESSION)) {
        const DWORD error = GetLastError();
        DestroyWindow(window_);
        window_ = nullptr;
        throw windowsError("WTSRegisterSessionNotification", error);
    }
    registeredForSessionNotifications_ = true;

    if (SetTimer(window_, TimerId, 60'000, nullptr) == 0) {
        const DWORD error = GetLastError();
        WTSUnRegisterSessionNotification(window_);
        registeredForSessionNotifications_ = false;
        DestroyWindow(window_);
        window_ = nullptr;
        throw windowsError("SetTimer", error);
    }

    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
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
        monitor = static_cast<WindowsSessionMonitor*>(createInfo->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(monitor));
    }
    if (monitor != nullptr) {
        return monitor->handleMessage(message, wParam, lParam);
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT WindowsSessionMonitor::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
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
        } else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
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
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window_, message, wParam, lParam);
    }
}

} // namespace chronox
