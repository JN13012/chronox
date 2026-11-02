#pragma once

#include <exception>
#include <functional>
#include <memory>

namespace chronox {

class DashboardWindow;

class WindowCoordinator {
public:
    using ErrorHandler = std::function<void(std::exception_ptr)>;

    explicit WindowCoordinator(ErrorHandler errorHandler);
    ~WindowCoordinator();

    WindowCoordinator(const WindowCoordinator&) = delete;
    WindowCoordinator& operator=(const WindowCoordinator&) = delete;

    void openDashboard();

private:
    ErrorHandler errorHandler_{};
    std::unique_ptr<DashboardWindow> dashboard_;
};

} // namespace chronox
