// Own-process presentation fixture: no application data or registry access.
#include "../windows/dashboard_window.h"
#include <windows.h>
#include "../windows/ui_window_geometry.h"
#include "../windows/dashboard_layout.h"
#include <objbase.h>
#include <iostream>
#include <algorithm>
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
        SendMessageW(delay,WM_KEYDOWN,VK_TAB,0); check(GetFocus()==own);
        SendMessageW(own,WM_KEYDOWN,VK_TAB,0); check(!IsWindowVisible(enabled));
        clickPage(2);
        // Keyboard state is thread-local; these messages target only our fixture.
        BYTE savedKeys[256]{}; check(GetKeyboardState(savedKeys));
        BYTE shiftKeys[256]{}; std::copy(std::begin(savedKeys),std::end(savedKeys),std::begin(shiftKeys));
        shiftKeys[VK_SHIFT]=0x80; check(SetKeyboardState(shiftKeys));
        SendMessageW(own,WM_KEYDOWN,VK_TAB,0); check(GetFocus()==delay);
        SendMessageW(delay,WM_KEYDOWN,VK_TAB,0); check(GetFocus()==enabled);
        SendMessageW(enabled,WM_KEYDOWN,VK_TAB,0); check(GetFocus()==own);
        SendMessageW(own,WM_KEYDOWN,VK_TAB,0); check(!IsWindowVisible(enabled));
        check(SetKeyboardState(savedKeys));
        clickPage(2);
        SendMessageW(enabled,BM_CLICK,0,0); check(!IsWindowEnabled(delay));
        SendMessageW(own,WM_KEYDOWN,VK_TAB,0); check(GetFocus()==enabled);
        SendMessageW(enabled,WM_KEYDOWN,VK_TAB,0); check(GetFocus()==own);
        SendMessageW(own,WM_KEYDOWN,VK_TAB,0); check(!IsWindowVisible(enabled));
        clickPage(2); SendMessageW(enabled,BM_CLICK,0,0);
        clickPage(0); check(!IsWindowVisible(enabled));
        for(auto state:{ActivityState::Active,ActivityState::Inactive,ActivityState::Locked,ActivityState::Sleeping,ActivityState::Uncertain}) {
            snapshot.activityState=state; window.refresh();
        }
        snapshot.activityMeasured.fill(false); window.refresh();
        snapshot.activityMeasured.fill(true); snapshot.activityDurations.fill(std::chrono::seconds(0)); window.refresh();
        snapshot.timeUncertain=true; window.refresh(); snapshot.timeUncertain=false;
        clickPage(1); clickPage(2); check(GetDlgItem(own,3101)==enabled);
        DWORD beforeHandles{}; GetProcessHandleCount(GetCurrentProcess(),&beforeHandles);
        const auto beforeGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS), beforeUser=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
        for(int i=0;i<100;++i) { SendMessageW(own,WM_CLOSE,0,0); window.showOrActivate(); }
        check(GetDlgItem(own,3102)==delay);
        DWORD afterHandles{}; GetProcessHandleCount(GetCurrentProcess(),&afterHandles);
        const auto afterGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS), afterUser=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
        check(beforeHandles==afterHandles && beforeGdi==afterGdi && beforeUser==afterUser);
        for (const auto size : {SIZE{1050,700},SIZE{720,520},SIZE{819,520},SIZE{820,520},SIZE{1440,1000}}) {
            const int dpi=static_cast<int>(GetDpiForWindow(own));
            RECT outer{0,0,MulDiv(size.cx,dpi,96),MulDiv(size.cy,dpi,96)};
            check(ui::adjustClientRectForDpi(&outer,static_cast<DWORD>(GetWindowLongPtrW(own,GWL_STYLE)),FALSE,0,dpi));
            SetWindowPos(own,nullptr,0,0,outer.right-outer.left,outer.bottom-outer.top,SWP_NOMOVE|SWP_NOZORDER);
            RECT actual{}; check(GetClientRect(own,&actual));
            check(actual.right==MulDiv(size.cx,dpi,96) && actual.bottom==MulDiv(size.cy,dpi,96));
            for(int page=0;page<3;++page) {
                clickPage(page);
                const int titleX=static_cast<int>(ui::DashboardLayout{static_cast<float>(size.cx),static_cast<float>(size.cy)}.left()+8);
                const auto titlePoint=MAKELPARAM(MulDiv(titleX,dpi,96),MulDiv(70,dpi,96));
                SendMessageW(own,WM_LBUTTONDOWN,MK_LBUTTON,titlePoint);
                check(GetCapture()==own);
                SendMessageW(own,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(MulDiv(titleX+35,dpi,96),MulDiv(70,dpi,96)));
                SendMessageW(own,WM_LBUTTONUP,0,MAKELPARAM(MulDiv(titleX+35,dpi,96),MulDiv(70,dpi,96)));
                check(GetCapture()!=own);
                SendMessageW(own,WM_KEYDOWN,VK_ESCAPE,0);
                check(IsWindowVisible(own)); // first Escape clears text, not the dashboard
                BYTE pageKeys[256]{},controlKeys[256]{}; check(GetKeyboardState(pageKeys));
                std::copy(std::begin(pageKeys),std::end(pageKeys),std::begin(controlKeys));
                controlKeys[VK_CONTROL]=0x80; check(SetKeyboardState(controlKeys));
                SendMessageW(own,WM_KEYDOWN,'A',0); // select whole page without a pre-existing selection
                check(SetKeyboardState(pageKeys));
                SendMessageW(own,WM_VSCROLL,SB_BOTTOM,0);
                SCROLLINFO info{sizeof(info),SIF_ALL}; check(GetScrollInfo(own,SB_VERT,&info));
                check(info.nPos==std::max(0,info.nMax-static_cast<int>(info.nPage)+1));
                SendMessageW(own,WM_VSCROLL,SB_TOP,0);
                check(GetScrollInfo(own,SB_VERT,&info)); check(info.nPos==0);
                SendMessageW(own,WM_KEYDOWN,VK_ESCAPE,0);
                check(IsWindowVisible(own)); // scrolling preserved the page selection
                if(error) std::rethrow_exception(error);
            }
        }
        std::cout<<"Dashboard 100 hide/show cycles: handles="<<afterHandles<<", GDI="<<afterGdi<<", USER="<<afterUser<<", all stable\n";
        if(error) std::rethrow_exception(error);
    }
    CoUninitialize();
    std::cout<<"Own-process settings controls, thresholds, states and navigation: PASS\n";
}
catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
