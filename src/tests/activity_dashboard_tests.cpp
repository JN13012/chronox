// Own-process presentation fixture: no application data or registry access.
#include "../windows/dashboard_window.h"
#include <windows.h>
#include <objbase.h>
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace chronox;
void check(bool x) { if(!x) throw std::runtime_error("Activity dashboard invariant failed"); }
int main() try {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    check(SUCCEEDED(com));
    {
        std::exception_ptr error;
        ActivitySettings settings;
        int changes{};
        DashboardSnapshot snapshot;
        snapshot.capturedAt=1'794'225'600'000'000;
        snapshot.todayRange=calendarPeriodContaining(snapshot.capturedAt,CalendarPeriod::Day);
        snapshot.activityMeasured.fill(true); snapshot.activityDurations.fill(std::chrono::seconds(20));
        DashboardWindow window([&](auto e){error=e;},[&]{return snapshot;},
            [&](CalendarPeriod p,Timestamp t) { DashboardHistorySnapshot h; h.period=p; h.anchor=t; h.range=calendarPeriodContaining(t,p); h.capturedAt=t; return h; },
            [&]{return settings;},[&](ActivitySettings s){settings=s; ++changes;});
        window.showOrActivate();
        HWND own{};
        EnumThreadWindows(GetCurrentThreadId(),[](HWND w,LPARAM p)->BOOL {
            wchar_t name[128]{}; GetClassNameW(w,name,128);
            if(std::wstring_view(name)==L"ChronoxDashboardWindow") *reinterpret_cast<HWND*>(p)=w;
            return TRUE;
        },reinterpret_cast<LPARAM>(&own));
        check(own!=nullptr);
        const auto clickPage=[&](int index) {
            const int dpi=static_cast<int>(GetDpiForWindow(own));
            SendMessageW(own,WM_LBUTTONUP,0,MAKELPARAM(MulDiv(30,dpi,96),MulDiv(120+56*index,dpi,96)));
            if(error) std::rethrow_exception(error);
        };
        clickPage(2);
        const auto enabled=GetDlgItem(own,3101), delay=GetDlgItem(own,3102);
        check(enabled && delay && IsWindowVisible(enabled));
        check(SendMessageW(enabled,BM_GETCHECK,0,0)==BST_CHECKED);
        check(SendMessageW(delay,CB_GETCURSEL,0,0)==1);
        SendMessageW(enabled,BM_CLICK,0,0); check(!settings.enabled && !IsWindowEnabled(delay));
        SendMessageW(enabled,BM_CLICK,0,0); check(settings.enabled && IsWindowEnabled(delay));
        for(int i=0;i<5;++i) {
            SendMessageW(delay,CB_SETCURSEL,i,0);
            SendMessageW(own,WM_COMMAND,MAKEWPARAM(3102,CBN_SELCHANGE),reinterpret_cast<LPARAM>(delay));
            constexpr int minutes[]{1,5,10,15,30}; check(settings.minutes==minutes[i]);
        }
        check(changes==7);
        SendMessageW(own,WM_KEYDOWN,VK_TAB,0); check(GetFocus()==enabled);
        SendMessageW(enabled,WM_KEYDOWN,VK_TAB,0); check(GetFocus()==delay);
        clickPage(0); check(!IsWindowVisible(enabled));
        for(auto state:{ActivityState::Active,ActivityState::Inactive,ActivityState::Locked,ActivityState::Sleeping,ActivityState::Uncertain}) {
            snapshot.activityState=state; window.refresh();
        }
        clickPage(1); clickPage(2); check(GetDlgItem(own,3101)==enabled);
        DWORD beforeHandles{}; GetProcessHandleCount(GetCurrentProcess(),&beforeHandles);
        const auto beforeGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS), beforeUser=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
        for(int i=0;i<20;++i) { SendMessageW(own,WM_CLOSE,0,0); window.showOrActivate(); }
        check(GetDlgItem(own,3102)==delay);
        DWORD afterHandles{}; GetProcessHandleCount(GetCurrentProcess(),&afterHandles);
        const auto afterGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS), afterUser=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
        check(beforeHandles==afterHandles && beforeGdi==afterGdi && beforeUser==afterUser);
        std::cout<<"Dashboard 20 hide/show cycles: handles="<<afterHandles<<", GDI="<<afterGdi<<", USER="<<afterUser<<", all stable\n";
        if(error) std::rethrow_exception(error);
    }
    CoUninitialize();
    std::cout<<"Own-process settings controls, thresholds, states and navigation: PASS\n";
}
catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
