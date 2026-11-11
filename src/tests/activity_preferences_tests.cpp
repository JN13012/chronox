#include "../windows/activity_preferences.h"
#include <windows.h>
#include <stdexcept>
#include <iostream>
using namespace chronox;
void check(bool v) { if(!v) throw std::runtime_error("Activity preference invariant"); }
int main() {
    const auto key=L"Software\\ChronoxTests\\Activity-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    ActivityPreferences p(key);
    check(p.load()==ActivitySettings{});
    for(int m:{1,5,10,15,30}) for(bool enabled:{false,true}) { p.save({enabled,m}); check(p.load()==ActivitySettings{enabled,m}); }
    bool rejected=false; try { p.save({true,2}); } catch(const std::invalid_argument&) {rejected=true;} check(rejected);
    check(p.load()==ActivitySettings{true,30});
    HKEY h{}; check(RegOpenKeyExW(HKEY_CURRENT_USER,key.c_str(),0,KEY_SET_VALUE,&h)==ERROR_SUCCESS);
    DWORD invalid=0x10002; check(RegSetValueExW(h,L"ActivityDetection",0,REG_DWORD,reinterpret_cast<const BYTE*>(&invalid),sizeof(invalid))==ERROR_SUCCESS); RegCloseKey(h);
    rejected=false; try {p.load();} catch(const std::exception&) {rejected=true;} check(rejected);
    check(RegDeleteTreeW(HKEY_CURRENT_USER,key.c_str())==ERROR_SUCCESS);
    std::cout << "Default, all thresholds, disabled mode and invalid preferences: PASS\n";
}
