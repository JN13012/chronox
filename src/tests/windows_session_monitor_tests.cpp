#include "windows_session_monitor.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace chronox {

struct WindowsSessionMonitorTestAccess {
    static void dispatch(WindowsSessionMonitor& monitor, UINT message, WPARAM value) {
        monitor.handleMessage(nullptr,message,value,0);
    }
    static HWND createWindow(WindowsSessionMonitor& monitor) {
        return monitor.createWindow();
    }

    static void destroyWindow(WindowsSessionMonitor& monitor) {
        monitor.destroyWindow();
    }
};

} // namespace chronox

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void createsAndDestroysHiddenWindowWithoutSessionRegistration() {
    chronox::WindowsSessionMonitor monitor(
        [](chronox::SessionEvent) {},
        [] {},
        [] {});

    const HWND window = chronox::WindowsSessionMonitorTestAccess::createWindow(monitor);
    require(window != nullptr, "hidden monitor window is created");
    require(IsWindow(window) != FALSE, "created monitor HWND is valid");

    chronox::WindowsSessionMonitorTestAccess::destroyWindow(monitor);
    require(IsWindow(window) == FALSE, "monitor window is destroyed");

    chronox::WindowsSessionMonitorTestAccess::destroyWindow(monitor);
    require(IsWindow(window) == FALSE, "repeated cleanup does not reuse an invalid HWND");

    HWND destructorOwnedWindow{};
    {
        chronox::WindowsSessionMonitor destructorOwnedMonitor(
            [](chronox::SessionEvent) {},
            [] {},
            [] {});
        destructorOwnedWindow =
            chronox::WindowsSessionMonitorTestAccess::createWindow(destructorOwnedMonitor);
        require(destructorOwnedWindow != nullptr && IsWindow(destructorOwnedWindow) != FALSE,
                "second hidden monitor window is created");
    }
    require(IsWindow(destructorOwnedWindow) == FALSE,
            "monitor destructor destroys its still-valid HWND");
}

} // namespace

int main() {
    createsAndDestroysHiddenWindowWithoutSessionRegistration();
    int changes=0, suspends=0, resumes=0;
    chronox::WindowsSessionMonitor monitor(
        [&](chronox::SessionEvent e){if(e==chronox::SessionEvent::Suspended) ++suspends;
            if(e==chronox::SessionEvent::Resumed) ++resumes;}, []{}, []{}, [&]{++changes;});
    chronox::WindowsSessionMonitorTestAccess::dispatch(monitor,WM_TIMECHANGE,0);
    chronox::WindowsSessionMonitorTestAccess::dispatch(monitor,WM_POWERBROADCAST,PBT_APMSUSPEND);
    chronox::WindowsSessionMonitorTestAccess::dispatch(monitor,WM_POWERBROADCAST,PBT_APMRESUMEAUTOMATIC);
    chronox::WindowsSessionMonitorTestAccess::dispatch(monitor,WM_POWERBROADCAST,PBT_APMRESUMESUSPEND);
    require(changes==1 && suspends==1 && resumes==2,"synthetic power/time notifications delivered");
    std::cout << "All Windows session monitor tests passed.\n";
}
