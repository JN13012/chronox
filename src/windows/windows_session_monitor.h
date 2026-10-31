#pragma once

#include "../core/session_tracker.h"

#include <windows.h>

#include <functional>

namespace chronox {

class WindowsSessionMonitor {
public:
    using EventHandler = std::function<void(SessionEvent)>;
    using StopHandler = std::function<void()>;
    using TickHandler = std::function<void()>;

    WindowsSessionMonitor(EventHandler onEvent, StopHandler onStop, TickHandler onTick);
    ~WindowsSessionMonitor();

    WindowsSessionMonitor(const WindowsSessionMonitor&) = delete;
    WindowsSessionMonitor& operator=(const WindowsSessionMonitor&) = delete;

    static bool queryInitiallyLocked();
    void start();
    void run();

private:
    friend struct WindowsSessionMonitorTestAccess;

    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    HWND createWindow();
    void destroyWindow();
    void releaseWindowResources(HWND window);

    HWND window_{};
    EventHandler onEvent_;
    StopHandler onStop_;
    TickHandler onTick_;
    bool registeredForSessionNotifications_{};
    bool timerSet_{};
};

} // namespace chronox
