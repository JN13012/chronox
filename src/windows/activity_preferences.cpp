#include "activity_preferences.h"
#include <windows.h>
#include <stdexcept>
#include <utility>
namespace chronox {
ActivityPreferences::ActivityPreferences(std::wstring key) : key_(std::move(key)) {
    if(key_.empty()) throw std::invalid_argument("Empty activity preference key");
}
ActivitySettings ActivityPreferences::load() const {
    DWORD value{}, size=sizeof(value);
    const auto error=RegGetValueW(HKEY_CURRENT_USER,key_.c_str(),L"ActivityDetection",RRF_RT_REG_DWORD,nullptr,&value,&size);
    if(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND) return {};
    if(error!=ERROR_SUCCESS || (value & ~0x100ffUL)!=0) throw std::runtime_error("Invalid activity preferences");
    ActivitySettings s{(value&0x10000)!=0,static_cast<int>(value&0xff)};
    if(!validActivitySettings(s)) throw std::runtime_error("Invalid inactivity threshold preference");
    return s;
}
void ActivityPreferences::save(ActivitySettings s) const {
    if(!validActivitySettings(s)) throw std::invalid_argument("Invalid inactivity threshold");
    HKEY key{};
    if(RegCreateKeyExW(HKEY_CURRENT_USER,key_.c_str(),0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)
        throw std::runtime_error("Cannot open activity preferences");
    // One DWORD keeps the enabled/threshold pair atomic.
    const DWORD value=(s.enabled?0x10000:0)|static_cast<DWORD>(s.minutes);
    const auto result=RegSetValueExW(key,L"ActivityDetection",0,REG_DWORD,reinterpret_cast<const BYTE*>(&value),sizeof(value));
    RegCloseKey(key);
    if(result!=ERROR_SUCCESS) throw std::runtime_error("Cannot save activity preferences");
}
}
