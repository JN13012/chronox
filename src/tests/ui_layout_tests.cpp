#include "../windows/dashboard_layout.h"
#include "../windows/ui_theme.h"
#include "../windows/dashboard_messages.h"
#include <iostream>
#include <stdexcept>
#include <limits>
#ifdef _WIN32
#include "../windows/ui_text.h"
#include <objbase.h>
#endif

int main() try {
    using namespace chronox::ui;
    float previous = 0;
    for (int width = 640; width <= 1600; ++width) {
        const auto layout = DashboardLayout{static_cast<float>(width), 520};
        if (layout.innerWidth() < previous)
            throw std::runtime_error("Content shrinks at a layout breakpoint");
        previous = layout.innerWidth();
    }
    if (contrast(palette::text, palette::panel) < 4.5 ||
        contrast(palette::muted, palette::panel) < 4.5 ||
        contrast(palette::text, palette::steel) < 4.5 ||
        contrast(palette::muted, palette::steel) < 4.5 ||
        contrast(palette::text, palette::selection) < 4.5)
        throw std::runtime_error("Unreadable base palette");
    if (nextSettingsFocus(SettingsFocus::Enabled, true, true) != SettingsFocus::Navigation ||
        nextSettingsFocus(SettingsFocus::Delay, false, true) != SettingsFocus::Navigation ||
        nextSettingsFocus(SettingsFocus::Delay, true, true) != SettingsFocus::Enabled ||
        nextSettingsFocus(SettingsFocus::Enabled, false, false) != SettingsFocus::Navigation)
        throw std::runtime_error("Settings focus escape is broken");
#ifdef _WIN32
    using Microsoft::WRL::ComPtr;
    ComPtr<IDWriteFactory> factory;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(factory.GetAddressOf())))) return 1;
    for (const float width : {640.f, 720.f, 759.f, 760.f, 819.f, 820.f, 1050.f, 1370.f, 1440.f, 1600.f}) {
        const auto layout = DashboardLayout{width, 520};
        const int columns = DashboardLayout::metricColumns(layout.innerWidth());
        const float card = (layout.innerWidth() - DashboardLayout::gap * (columns - 1)) / columns;
        const auto checkAnnotation = [&](const std::wstring& text, TextStyle style, float w, float h) {
            ComPtr<IDWriteTextFormat> f;
            if (FAILED(createTextFormat(factory.Get(), style, &f))) throw std::runtime_error("Format creation failed");
            ComPtr<IDWriteTextLayout> l;
            if (FAILED(factory->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),f.Get(),w,h,&l)))
                throw std::runtime_error("Text measurement failed");
            DWRITE_TEXT_METRICS m{}; l->GetMetrics(&m);
            if (m.width > w+.1f || m.height > h+.1f)
                throw std::runtime_error("An availability/state annotation would be truncated");
        };
        checkAnnotation(coverageMessage(true),type::label,layout.innerWidth(),36);
        checkAnnotation(coverageMessage(false),type::label,layout.innerWidth(),36);
        checkAnnotation(uncertaintyMessage,type::label,layout.innerWidth(),36);
        checkAnnotation(trackingStatus(false,L"Mesure incertaine",false),type::label,layout.innerWidth()-38,33);
        checkAnnotation(L"Session : lundi au dimanche",type::label,(layout.innerWidth()-DashboardLayout::gap)/2-20,17);
        checkAnnotation(L"2562047788015215 h 30 min 7 s",type::label,(layout.innerWidth()-24)*.36f+2,34);
        checkAnnotation(L"Activit\u00e9 : 2562047788015215 h 30 min 7 s",type::activity,card-24,38);
        for (const auto* value : {L"0 h 00 min 0 s", L"1234 h 59 min 59 s",
                L"999999 h 59 min 59 s", L"2562047788015215 h 30 min 7 s", L"Non mesur\u00e9e"}) {
            ComPtr<IDWriteTextLayout> text;
            float size{};
            if (FAILED(createMetricLayout(factory.Get(), value, card - 24, 64, &text, &size)) || size < 24)
            { std::wcerr << L"width=" << width << L" card=" << card << L" value=" << value << L"\n";
                throw std::runtime_error("A principal value would be truncated"); }
            DWRITE_TEXT_METRICS metrics{}; text->GetMetrics(&metrics);
            if (metrics.height > 64.1f || metrics.width > card - 23.9f)
                throw std::runtime_error("DirectWrite metrics exceed the card");
        }
    }
#endif
    std::cout << "Monotone layout 640..1600, selected contrast="
              << contrast(palette::text, palette::selection)
              << ", 24..28 DIP full values and focus ring: PASS\n";
} catch (const std::exception& e) { std::cerr << e.what(); return 1; }
