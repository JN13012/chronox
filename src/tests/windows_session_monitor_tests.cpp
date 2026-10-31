#include "windows_session_monitor.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace chronox {

struct WindowsSessionMonitorTestAccess {
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
    std::cout << "All Windows session monitor tests passed.\n";
}
