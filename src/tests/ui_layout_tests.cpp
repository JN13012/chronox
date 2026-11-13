#include "../windows/dashboard_layout.h"
#include "../windows/ui_theme.h"
#include "../windows/ui_scrollbar.h"
#include <string_view>
#include "../windows/dashboard_messages.h"
#include <iostream>
#include <stdexcept>
#include <limits>
#ifdef _WIN32
#include "../windows/ui_text.h"
#include "../windows/dashboard_format.h"
#include "../windows/dashboard_text_selection.h"
#include <objbase.h>
#endif

int main() try {
    using namespace chronox::ui;
    // Literal source text must compile identically to explicit Unicode escapes.
    if(std::wstring_view(L"Dernières sessions · Début – Fin — Durée") !=
       L"Derni\u00e8res sessions \u00b7 D\u00e9but \u2013 Fin \u2014 Dur\u00e9e" ||
       std::string_view("é") != "\xc3\xa9")
        throw std::runtime_error("UI source and execution character sets must be UTF-8");
    const auto scroll=sessionScrollGeometry(120,125,4,0);
    const auto end=sessionScrollGeometry(120,125,4,121);
    if(scroll.thumbHeight<24 || scroll.thumbTop!=scroll.trackTop ||
       std::abs(end.thumbTop+end.thumbHeight-end.trackBottom)>.01f ||
       scroll.positionForThumb(-100)!=0 || scroll.positionForThumb(1000)!=121 ||
       sessionScrollGeometry(120,2,4,0).maximum!=0)
        throw std::runtime_error("Rounded scroll thumb must preserve its size and reach both endpoints");
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
    {
        const auto require=[](bool ok,const char* message) { if(!ok) throw std::runtime_error(message); };
        ComPtr<IDWriteTextFormat> format;
        require(SUCCEEDED(createTextFormat(factory.Get(),type::label,&format)),"Selection format");
        ComPtr<IDWriteTextLayout> sessionHeading;
        require(SUCCEEDED(factory->CreateTextLayout(L"Dernières sessions",18,format.Get(),300,26,&sessionHeading)),"Session heading layout");
        DWRITE_TEXT_METRICS headingMetrics{};
        require(SUCCEEDED(sessionHeading->GetMetrics(&headingMetrics)) && headingMetrics.height<=26,"Session heading glyphs fit without bottom clipping");
        DashboardTextSelection selection;
        const D2D1_RECT_F clip{0,0,500,200};
        selection.beginFrame();
        auto& run=selection.append(factory.Get(),L"Bonjour Chronox",format.Get(),220,32,
                                    DWRITE_TEXT_ALIGNMENT_LEADING,10,20,clip);
        auto* cached=run.layout.Get();
        selection.endFrame();
        require(selection.beginSelection(11,28),"Mouse must hit readable text");
        require(selection.copyText()==L"Bonjour Chronox","Click selects complete block");
        require(!selection.highlights().empty(),"Selection must have glyph highlight geometry");
        FLOAT x{},y{}; DWRITE_HIT_TEST_METRICS hit{};
        require(SUCCEEDED(cached->HitTestTextPosition(8,FALSE,&x,&y,&hit)),"Character position");
        require(selection.drag(10+x+.01f,20+y+hit.height/2),"Drag selects a range");
        require(selection.copyText()==L"Bonjour ","Drag must follow DirectWrite character boundaries");
        selection.selectAll(); require(selection.copyText()==L"Bonjour Chronox","Select all block");
        selection.beginFrame();
        auto& reused=selection.append(factory.Get(),L"Bonjour Chronox",format.Get(),220,32,
                                      DWRITE_TEXT_ALIGNMENT_LEADING,10,20,clip);
        require(reused.layout.Get()==cached,"Repaint must reuse layout");
        selection.endFrame(); require(selection.hasSelection(),"Unchanged repaint preserves selection");
        selection.beginFrame();
        selection.append(factory.Get(),L"Valeur actualis\u00e9e",format.Get(),220,32,
                         DWRITE_TEXT_ALIGNMENT_LEADING,10,20,clip);
        selection.endFrame(); require(!selection.hasSelection(),"Live value update must invalidate stale selection");
        selection.beginFrame();
        selection.append(factory.Get(),L"Texte hors viewport",format.Get(),220,32,
                         DWRITE_TEXT_ALIGNMENT_LEADING,10,-40,clip);
        selection.endFrame(); require(!selection.beginSelection(11,-32),"Clipped text cannot be selected");
        selection.beginFrame();
        selection.append(factory.Get(),L"Texte visible",format.Get(),220,32,
                         DWRITE_TEXT_ALIGNMENT_LEADING,10,0,{30,0,200,32});
        selection.endFrame(); require(!selection.beginSelection(11,8),"Partial viewport clip must reject hidden glyphs");
        selection.beginFrame();
        auto& wrapped=selection.append(factory.Get(),L"Une longue valeur sur plusieurs lignes",format.Get(),85,100,
                         DWRITE_TEXT_ALIGNMENT_LEADING,10,20,clip);
        wrapped.layout->HitTestTextPosition(0,FALSE,&x,&y,&hit);
        selection.endFrame(); require(selection.beginSelection(10+x+.1f,20+y+hit.height/2),"Wrapped text hit");
        require(selection.highlights().size()>1,"Wrapped selection must highlight every line");
        selection.beginFrame(); selection.endFrame();
        require(selection.copyText().empty(),"Hidden page cannot retain selected text");
        selection.beginFrame();
        selection.append(factory.Get(),L"Premier bloc",format.Get(),220,32,
                         DWRITE_TEXT_ALIGNMENT_LEADING,10,20,clip);
        auto& second=selection.append(factory.Get(),L"Ligne suivante",format.Get(),220,32,
                         DWRITE_TEXT_ALIGNMENT_LEADING,10,60,clip);
        auto* secondLayout=second.layout.Get();
        selection.append(factory.Get(),L"Bas de page",format.Get(),220,32,
                         DWRITE_TEXT_ALIGNMENT_LEADING,10,500,clip);
        selection.endFrame();
        require(selection.beginSelection(11,28),"First block hit");
        secondLayout->HitTestTextPosition(5,FALSE,&x,&y,&hit);
        require(selection.drag(10+x+.01f,60+y+hit.height/2),"Drag must reach another block");
        require(selection.copyText()==L"Premier bloc\r\nLigne","Cross-block copy must follow reading order");
        selection.selectAll();
        const std::wstring page=L"Premier bloc\r\nLigne suivante\r\nBas de page";
        require(selection.copyText()==page,"Select all must include page text below viewport");
        selection.beginFrame();
        selection.append(factory.Get(),L"Premier bloc",format.Get(),220,32,
                         DWRITE_TEXT_ALIGNMENT_LEADING,10,0,clip);
        selection.append(factory.Get(),L"Ligne suivante",format.Get(),220,32,
                         DWRITE_TEXT_ALIGNMENT_LEADING,10,40,clip);
        selection.append(factory.Get(),L"Bas de page",format.Get(),220,32,
                         DWRITE_TEXT_ALIGNMENT_LEADING,10,480,clip);
        selection.endFrame();
        require(selection.copyText()==page,"Scrolling must preserve page selection");
        selection.beginFrame();
        selection.append(factory.Get(),L"Premier bloc",format.Get(),220,32,
                         DWRITE_TEXT_ALIGNMENT_LEADING,10,0,clip);
        selection.append(factory.Get(),L"Valeur actualis\u00e9e",format.Get(),220,32,
                         DWRITE_TEXT_ALIGNMENT_LEADING,10,40,clip);
        require(selection.endFrame() && !selection.hasSelection(),"Changed page selection must clear and request repaint");
        ComPtr<IDWriteTextFormat> barFormat;
        require(SUCCEEDED(createTextFormat(factory.Get(),type::compact,&barFormat)),"Bar label format");
        for(float slot : {30.f,45.f,100.f}) for(const auto* value : {L"0h00",L"2h15",L"11h06"}) {
            selection.beginFrame();
            auto& label=selection.append(factory.Get(),value,barFormat.Get(),slot,16,
                DWRITE_TEXT_ALIGNMENT_CENTER,100-slot/2,30,clip);
            DWRITE_TEXT_METRICS m{}; label.layout->GetMetrics(&m);
            require(std::abs(label.x+m.left+m.width/2-100)<.1f && m.height<=16,
                    "Duration must be centered over its bar without clipping");
            selection.endFrame();
        }
        std::cout<<"Click/drag, exact text copy, clipping, repaint cache and centered bar labels: PASS\n";
    }
    if (compactDuration(std::chrono::seconds(0))!=L"0:00:00" ||
        compactDuration(std::chrono::seconds(85*3600+34*60+34))!=L"85:34:34")
        throw std::runtime_error("Compact format must retain cumulative hours and seconds");
    for (const float width : {640.f, 720.f, 759.f, 760.f, 819.f, 820.f, 867.f, 868.f, 900.f, 1050.f, 1370.f, 1440.f, 1600.f}) {
        const auto layout = DashboardLayout{width, 520};
        // History keeps full unit names and two columns; overview uses compact totals.
        const float card = (layout.innerWidth() - DashboardLayout::gap) / 2;
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
        checkAnnotation(trackingStatus(false,L"Mesure incertaine",false),type::label,layout.statusWidth(),34);
        checkAnnotation(L"Session : lundi au dimanche",type::label,(layout.innerWidth()-DashboardLayout::gap)/2-20,17);
        checkAnnotation(L"2562047788015215 h 30 min 7 s",type::label,(layout.innerWidth()-24)*.36f+2,34);
        checkAnnotation(L"Clavier/souris : 2562047788015215 h 30 min 7 s",type::activity,card-24,38);
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
        const std::wstring usual[]{L"2 h 53 min 44 s",L"27 h 19 min 46 s",L"75 h 50 min 15 s",L"81 h 17 min 3 s"};
        const std::wstring activity[]{L"Clavier/souris : 2 h 08 min 48 s",L"Clavier/souris : non mesur\u00e9e"};
        MetricCardLayout compact;
        if (FAILED(measureMetricCards(factory.Get(),usual,activity,card-24,compact)) ||
            compact.height()>=154 || compact.activityTop()<compact.valueTop+compact.valueHeight)
            throw std::runtime_error("Ordinary cards must be compact without overlap");
        const std::wstring huge[]{L"2562047788015215 h 30 min 7 s"};
        const std::wstring hugeActivity[]{L"Clavier/souris : 2562047788015215 h 30 min 7 s"};
        MetricCardLayout expanded;
        if (FAILED(measureMetricCards(factory.Get(),huge,hugeActivity,card-24,expanded)))
            throw std::runtime_error("Long totals must retain adaptive space");
        ComPtr<IDWriteTextLayout> expandedText;
        if (FAILED(createMetricLayout(factory.Get(),huge[0],card-24,expanded.valueHeight,&expandedText)))
            throw std::runtime_error("Expanded total cannot fit");
        checkAnnotation(hugeActivity[0],type::activity,card-24,expanded.activityHeight);
        const std::wstring totals[]{L"31:37:17",L"80:07:46",L"85:34:34"};
        const std::wstring inputs[]{L"5 jours travaillés",L"Moy./jour : 7:06:00",L"210 jours travaillés"};
        MetricCardLayout responsive;
        if (FAILED(measureResponsiveMetricCards(factory.Get(),totals,inputs,layout.innerWidth(),responsive)))
            throw std::runtime_error("Responsive overview cannot fit ordinary totals");
        if ((width==1050 && responsive.columns!=3) || (width==720 && responsive.columns!=3))
            throw std::runtime_error("Expected three period summaries without a duplicate today card");
        if (width==1050 && DashboardLayout::overviewHeight(layout.innerWidth(),responsive.height(),700,responsive.columns)>522.1f)
            throw std::runtime_error("Ordinary overview should fit 1050x700 without vertical overflow");
        // The daily total now shares its panel with the timeline. Even the
        // largest supported duration must fit above the track at 100 DIPs.
        const float timelineWidth = DashboardLayout::stackedCharts(layout.innerWidth())
            ? layout.innerWidth() : (layout.innerWidth()-DashboardLayout::gap)/2;
        for (auto duration : {std::chrono::seconds(0), std::chrono::seconds(24*3600), std::chrono::seconds::max()}) {
            ComPtr<IDWriteTextLayout> daily;
            if (FAILED(createMetricLayout(factory.Get(),compactDuration(duration),timelineWidth-32,64,&daily)))
                throw std::runtime_error("Daily total overlaps the merged timeline");
        }
        const std::wstring maxTotal[]{compactDuration(std::chrono::seconds::max())};
        const std::wstring maxInput[]{L"Clavier/souris : "+maxTotal[0]};
        MetricCardLayout exceptional;
        if (FAILED(measureResponsiveMetricCards(factory.Get(),maxTotal,maxInput,layout.innerWidth(),exceptional)))
            throw std::runtime_error("Extreme totals require safe responsive fallback");
    }
#endif
    std::cout << "Monotone layout 640..1600, selected contrast="
              << contrast(palette::text, palette::selection)
              << ", 24..28 DIP full values and focus ring: PASS\n";
} catch (const std::exception& e) { std::cerr << e.what(); return 1; }
