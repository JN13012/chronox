#include "../windows/window_coordinator.h"

#include <windows.h>

#include <exception>
#include <stdexcept>
#include <string>

namespace {

void showError(const std::string& message) {
    const int length = MultiByteToWideChar(
        CP_UTF8, 0, message.c_str(), static_cast<int>(message.size()), nullptr, 0);
    std::wstring wideMessage;
    if (length > 0) {
        wideMessage.resize(static_cast<std::size_t>(length));
        MultiByteToWideChar(CP_UTF8, 0, message.c_str(),
                            static_cast<int>(message.size()), &wideMessage[0],
                            length);
    } else {
        wideMessage = L"An unexpected dashboard error occurred.";
    }
    MessageBoxW(nullptr, wideMessage.c_str(), L"Chronox dashboard smoke test",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

void reportError(std::exception_ptr error) {
    try {
        if (error != nullptr) {
            std::rethrow_exception(error);
        }
    } catch (const std::exception& exception) {
        showError(exception.what());
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        if (!SetProcessDpiAwarenessContext(
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) &&
            GetLastError() != ERROR_ACCESS_DENIED) {
            throw std::runtime_error(
                "Could not enable per-monitor DPI awareness for the smoke test");
        }
        chronox::WindowCoordinator windows(reportError);
        windows.openDashboard();
        windows.openDashboard();

        MSG message{};
        int result = 0;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return result == -1 ? 1 : static_cast<int>(message.wParam);
    } catch (const std::exception& error) {
        showError(error.what());
        return 1;
    }
}
