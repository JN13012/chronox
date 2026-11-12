#pragma once

#include "ui_theme.h"

namespace chronox::ui {
struct MetricCardLayout {
    float valueHeight = 40, activityHeight = 20;
    static constexpr float valueTop = 44;
    constexpr float activityTop() const { return valueTop + valueHeight + 2; }
    constexpr float height() const { return activityTop() + activityHeight + 8; }
};
struct DashboardLayout {
    static constexpr float initialWidth = 1050, initialHeight = 700;
    static constexpr float expandedSidebar = 232, compactSidebar = 72;
    float width, height;
    constexpr float sidebar() const {
        // Grow by less than one DIP per added client DIP: content never shrinks.
        return std::clamp(compactSidebar + (width - 720) * 160 / 330,
                          compactSidebar, expandedSidebar);
    }
    constexpr float padding() const { return space::xl; }
    constexpr float left() const { return sidebar() + padding(); }
    constexpr float right() const { return width - padding(); }
    constexpr float innerWidth() const { return right() - left() - 28; }
    static constexpr int metricColumns(float available) { return available >= 1000 ? 4 : 2; }
    static constexpr float metricHeight = MetricCardLayout{}.height(), gap = space::md;
    static constexpr float overviewStatusHeight = 40;
    static constexpr float metricsHeight(float available, float cardHeight = metricHeight) {
        return cardHeight * (4 / metricColumns(available)) +
               gap * (4 / metricColumns(available) - 1);
    }
    static constexpr float overviewHeight(float available, float cardHeight = metricHeight) {
        return overviewStatusHeight + metricsHeight(available,cardHeight) + gap + 180 + gap + 300;
    }
    static constexpr float historyHeight = 770;
};

enum class SettingsFocus { Navigation, Enabled, Delay };
constexpr SettingsFocus nextSettingsFocus(SettingsFocus from, bool backwards, bool delayEnabled) {
    if (from == SettingsFocus::Enabled)
        return backwards || !delayEnabled ? SettingsFocus::Navigation : SettingsFocus::Delay;
    if (from == SettingsFocus::Delay)
        return backwards ? SettingsFocus::Enabled : SettingsFocus::Navigation;
    return backwards && delayEnabled ? SettingsFocus::Delay : SettingsFocus::Enabled;
}
} // namespace chronox::ui
