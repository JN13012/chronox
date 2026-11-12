#pragma once

#include "ui_theme.h"

namespace chronox::ui {
struct MetricCardLayout {
    float valueHeight = 40, activityHeight = 20;
    int columns = 2;
    static constexpr float valueTop = 30;
    constexpr float activityTop() const { return valueTop + valueHeight + 2; }
    constexpr float height() const { return activityTop() + activityHeight + 8; }
};
struct DashboardLayout {
    static constexpr float initialWidth = 1050, initialHeight = 700;
    static constexpr float expandedSidebar = 196, compactSidebar = 72;
    float width, height;
    constexpr float sidebar() const {
        // Grow by less than one DIP per added client DIP: content never shrinks.
        return std::clamp(compactSidebar + (width - 720) * 124 / 330,
                          compactSidebar, expandedSidebar);
    }
    constexpr float padding() const { return space::lg; }
    constexpr float left() const { return sidebar() + padding(); }
    constexpr float right() const { return width - padding(); }
    constexpr float innerWidth() const { return right() - left() - 28; }
    constexpr float statusLeft() const { return left() + 180; }
    constexpr float statusWidth() const { return right() - statusLeft(); }
    static constexpr int metricColumns(float available) { return available >= 680 ? 4 : 2; }
    static constexpr float metricHeight = MetricCardLayout{}.height(), gap = 10;
    static constexpr float overviewIntroHeight = 24;
    static constexpr bool stackedCharts(float available) { return available < 600; }
    static constexpr float chartHeight(float viewportHeight) { return std::clamp(viewportHeight*.2f,120.f,160.f); }
    static constexpr float recentHeight(float viewportHeight) { return std::clamp(viewportHeight*.34f,160.f,300.f); }
    static constexpr float metricsHeight(float available, float cardHeight = metricHeight, int columns = 0) {
        if (!columns) columns=metricColumns(available);
        return cardHeight * (4 / columns) + gap * (4 / columns - 1);
    }
    static constexpr float overviewHeight(float available, float cardHeight = metricHeight,
                                          float viewportHeight = initialHeight, int columns = 0) {
        return overviewIntroHeight + metricsHeight(available,cardHeight,columns) + gap +
            (stackedCharts(available)?84+gap:0) + chartHeight(viewportHeight) + gap + recentHeight(viewportHeight);
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
