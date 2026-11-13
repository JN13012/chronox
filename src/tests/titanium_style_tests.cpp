#include <windows.h>
#include "../prototypes/titanium_style.h"
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
using namespace chronox::ui;
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
int main() try {
    ComPtr<IDWriteFactory> factory;
    require(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf()))),"DirectWrite factory");
    ComPtr<IDWriteTypography> tabular;
    require(SUCCEEDED(titanium::createTabularTypography(factory.Get(),&tabular)),"Tabular feature");
    require(titanium::body.size>=12 && titanium::label.size>=12,"Essential text minimum");
    for(auto surface : {titanium::surfaceTop,titanium::surfaceBottom,titanium::selected,titanium::hover}) {
        require(contrast(titanium::silver,surface)>=4.5,"Primary text contrast");
        require(contrast(titanium::secondary,surface)>=4.5,"Secondary text contrast");
        require(contrast(titanium::cyan,surface)>=4.5,"Selected information contrast");
    }
    ComPtr<IDWriteTextFormat> format;
    require(SUCCEEDED(createTextFormat(factory.Get(),titanium::body,&format)),"Body format");
    const auto measure=[&](const std::wstring& text,float width=1000.f) {
        ComPtr<IDWriteTextLayout> layout;
        require(SUCCEEDED(factory->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),format.Get(),width,100,&layout)),"Text layout");
        require(SUCCEEDED(layout->SetTypography(tabular.Get(),{0,static_cast<UINT32>(text.size())})),"Tabular application");
        DWRITE_TEXT_METRICS metrics{}; require(SUCCEEDED(layout->GetMetrics(&metrics)),"Metrics"); return metrics;
    };
    const auto digitWidth=measure(L"000000").width;
    for(wchar_t digit=L'1';digit<=L'9';++digit)
        require(std::abs(measure(std::wstring(6,digit)).width-digitWidth)<.01f,"Digits must have equal advances");
    // Same seven equal slots as the renderer, including the narrow desktop chart.
    for(float slot : {49.f,52.f,80.f}) for(auto text : {L"0 h",L"5 h 00",L"23 h 59",L"25 h 00"}) {
        const auto m=measure(text,slot);
        require(m.width<=slot+.1f && m.height<=20,"Weekly label must fit at 12 DIP without wrapping");
    }
    for(auto text : {L"12/11/2026",L"12:00 – En cours",L"1 h 00 min 0 s"}) {
        const auto m=measure(text,120);
        require(m.width<=120.1f && m.height<=19,"Session text must fit its 12 DIP row");
    }
    std::cout<<"Titanium text contrast, 12 DIP labels, tabular digits and chart/table fit: PASS\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
