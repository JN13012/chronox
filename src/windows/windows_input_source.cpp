#include "windows_input_source.h"
#include <windows.h>
namespace chronox {
InputSample sampleWindowsInput() {
    const auto before=GetTickCount64();
    LASTINPUTINFO info{}; info.cbSize=sizeof(info);
    const bool valid=GetLastInputInfo(&info)!=FALSE;
    const auto after=GetTickCount64();
    return {after,info.dwTime,valid && after>=before && after-before<=100};
}
}
