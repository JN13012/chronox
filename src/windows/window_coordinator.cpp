#include "window_coordinator.h"

#include "dashboard_window.h"

#include <utility>

namespace chronox {

WindowCoordinator::WindowCoordinator(ErrorHandler errorHandler)
    : errorHandler_(errorHandler) {}

WindowCoordinator::~WindowCoordinator() = default;

void WindowCoordinator::openDashboard() {
    if (dashboard_ == nullptr) {
        dashboard_ = std::make_unique<DashboardWindow>(errorHandler_);
    }
    dashboard_->showOrActivate();
}

} // namespace chronox
