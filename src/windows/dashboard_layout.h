#pragma once

namespace chronox::ui {
struct DashboardLayout {
    static constexpr float initialWidth = 1050, initialHeight = 700;
    static constexpr float expandedSidebar = 232, compactSidebar = 72;
    static constexpr float compactThreshold = 820;
    float width, height;
    constexpr float sidebar() const {
        return width < compactThreshold ? compactSidebar : expandedSidebar;
    }
    constexpr float padding() const { return width < 760 ? 20.f : 36.f; }
    constexpr float left() const { return sidebar() + padding(); }
    constexpr float right() const { return width - padding(); }
    constexpr float innerWidth() const { return right() - left() - 28; }
};
} // namespace chronox::ui
