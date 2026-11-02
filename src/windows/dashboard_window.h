#pragma once

#include <exception>
#include <functional>
#include <memory>

namespace chronox {

class DashboardWindow {
public:
    using ErrorHandler = std::function<void(std::exception_ptr)>;

    explicit DashboardWindow(ErrorHandler errorHandler);
    ~DashboardWindow();

    DashboardWindow(const DashboardWindow&) = delete;
    DashboardWindow& operator=(const DashboardWindow&) = delete;

    void showOrActivate();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chronox
