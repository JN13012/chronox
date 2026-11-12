#include "../windows/dashboard_layout.h"
#include "../windows/ui_theme.h"
#include <iostream>
#include <stdexcept>

int main() try {
    using namespace chronox::ui;
    if (DashboardLayout{1050, 700}.innerWidth() != 718 ||
        DashboardLayout{720, 520}.innerWidth() != 580)
        throw std::runtime_error("Layout extraction changed the baseline");
    if (contrast(palette::text, palette::panel) < 4.5 ||
        contrast(palette::muted, palette::panel) < 4.5)
        throw std::runtime_error("Unreadable base palette");
    std::cout << "Extracted layout and palette: PASS\n";
} catch (const std::exception& e) { std::cerr << e.what(); return 1; }
