// Own-process tray fixture: no database, engine, registry writes or installed app.
#include "../windows/chronox_widget.h"
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <iostream>
#include <stdexcept>
#include <string_view>

void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
int main() try {
    require(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),"COM initialization");
    struct ComLifetime { ~ComLifetime() { CoUninitialize(); } } comLifetime;
    std::exception_ptr error;
    int opens=0,exits=0,startupChanges=0;
    chronox::ChronoxWidget widget([] { return chronox::WidgetSnapshot{}; },[&] { ++exits; },
        [&] { ++opens; },[&](auto e) { error=e; },[] { return chronox::StartupState::Enabled; },
        [&] { ++startupChanges; },chronox::WidgetPage::Today,[](auto) {});
    widget.create(false);
    HWND own{};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM data)->BOOL {
        wchar_t cls[128]{}; GetClassNameW(window,cls,128);
        if(std::wstring_view(cls)==L"ChronoxV04WidgetWindow") *reinterpret_cast<HWND*>(data)=window;
        return TRUE;
    },reinterpret_cast<LPARAM>(&own));
    require(own!=nullptr,"Fixture widget exists");
    const auto notify=[&](UINT event,UINT icon=1) {
        SendMessageW(own,WM_APP+1,0,MAKELPARAM(event,icon));
        if(error) std::rethrow_exception(error);
    };
    const auto releaseGesture=[&] { SendMessageW(own,WM_TIMER,4,0); if(error) std::rethrow_exception(error); };
    notify(NIN_SELECT,2); require(opens==0,"Ignore notifications for another tray icon");
    notify(NIN_SELECT); require(opens==1,"Single click opens dashboard immediately, before any timer");
    require(!IsWindowVisible(own),"Single click does not reveal hidden widget");
    notify(WM_LBUTTONUP); notify(WM_LBUTTONDBLCLK); notify(NIN_SELECT);
    require(opens==1,"Duplicate and double-click notifications open dashboard once");
    releaseGesture(); require(!IsWindowVisible(own),"Release timer never toggles widget");
    notify(WM_LBUTTONUP); require(opens==2,"Legacy left activation opens dashboard");
    releaseGesture(); notify(NIN_KEYSELECT); require(opens==3,"Keyboard activation opens dashboard");
    require(!IsWindowVisible(own),"Keyboard activation preserves hidden widget");
    releaseGesture();
    ShowWindow(own,SW_SHOWNA);
    notify(NIN_SELECT); require(opens==4 && IsWindowVisible(own),"Activation preserves visible widget");
    releaseGesture();
    SendMessageW(own,WM_COMMAND,1001,0);
    require(!IsWindowVisible(own) && opens==4,"Widget visibility remains a separate menu command");
    SendMessageW(own,WM_COMMAND,1005,0);
    require(opens==5,"Dashboard menu command still opens dashboard");
    require(exits==0 && startupChanges==0,"Activation does not quit or change startup settings");
    if(error) std::rethrow_exception(error);
    std::cout << "Immediate tray activation, keyboard, duplicate suppression and widget visibility: PASS\n";
} catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
