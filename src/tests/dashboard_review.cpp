// Presentation-only manual review / resource fixture. No database or registry access.
// Arguments: page(0..2) clientWidth clientHeight seconds(0=manual) output.json state
#include "../windows/dashboard_window.h"
#include <windows.h>
#include <commctrl.h>
#include <ctime>
#include "../windows/ui_window_geometry.h"
#include <psapi.h>
#include <objbase.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>

using namespace chronox;
using namespace std::chrono_literals;

int main(int argc, char** argv) try {
    const int page = argc > 1 ? std::clamp(std::stoi(argv[1]), 0, 2) : 0;
    const int width = argc > 2 ? std::stoi(argv[2]) : 1050;
    const int height = argc > 3 ? std::stoi(argv[3]) : 700;
    const int seconds = argc > 4 ? std::stoi(argv[4]) : 0;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    struct ComLifetime { ~ComLifetime() { CoUninitialize(); } } comLifetime;
    std::exception_ptr error;
    ActivitySettings settings;
    DashboardSnapshot snapshot;
    snapshot.capturedAt = 1'794'484'800'000'000;
    snapshot.todayRange = calendarPeriodContaining(snapshot.capturedAt, CalendarPeriod::Day);
    snapshot.todayDuration = 5h + 27min + 15s;
    snapshot.weekDuration = 28h + 16min;
    snapshot.monthDuration = 134h + 52min;
    snapshot.yearDuration = 1234h + 59min + 59s;
    snapshot.activityDurations = {3h + 12min, 18h + 45min, 80h, 500h};
    snapshot.activityMeasured.fill(true);
    snapshot.activityState = ActivityState::Inactive;
    snapshot.trackingActive = true;
    const std::string_view state = argc > 6 ? argv[6] : "standard";
    if (state == "missing") snapshot.activityMeasured.fill(false);
    if (state == "zero") snapshot.activityDurations.fill(0s);
    if (state == "uncertain") { snapshot.timeUncertain = true; snapshot.activityState = ActivityState::Uncertain; }
    if (state == "disabled") { settings.enabled = false; snapshot.activityDetectionEnabled = false; }
    snapshot.todayIntervals = {{snapshot.todayRange.start + 8 * 3'600'000'000LL,
                                snapshot.todayRange.start + 10 * 3'600'000'000LL}};
    snapshot.weekDays = {5h, 4h, 6h, 3h, 7h, 0h, 2h};
    snapshot.recentSessions = {{{snapshot.capturedAt - 3'600'000'000LL,
                                  snapshot.capturedAt}, 1h, true}};
    if (state == "reference") snapshot.activityState = ActivityState::Active;
    snapshot.daysWithSession={1,5,19,210}; snapshot.monthDailyAverage=7h+6min;
    for (int i = 1; i < 125; ++i) {
        const auto end = snapshot.capturedAt - i * 1'200'000'000LL;
        snapshot.recentSessions.push_back({{end - 3'600'000'000LL, end}, 1h, false});
    }
    const auto allSessions=snapshot.recentSessions;
    const auto weekRange=calendarPeriodContaining(snapshot.capturedAt,CalendarPeriod::Week);
    snapshot.recentSessions.clear();
    for(auto session:allSessions) {
        const auto start=std::max(session.interval.start,weekRange.start), end=std::min(session.interval.end,weekRange.end);
        if(end<=start) continue;
        session.interval={start,end}; session.duration=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::microseconds(end-start));
        snapshot.recentSessions.push_back(session);
    }
    int rangeReads=0,pageReads=0;
    std::optional<TimeRange> lastRange;
    const auto historyProvider = [&](CalendarPeriod p, Timestamp t) {
            DashboardHistorySnapshot h;
            h.period = p; h.anchor = t; h.range = calendarPeriodContaining(t, p);
            h.capturedAt = snapshot.capturedAt;
            h.duration = 1234h + 59min + 59s;
            h.activityDuration = state == "zero" ? 0s : 500h + 12min;
            h.activityMeasured = state != "missing"; h.timeUncertain = state == "uncertain";
            h.daysWithActivity = 12; h.sessions = snapshot.recentSessions;
            Timestamp day = h.range.start;
            while (day < h.range.end) {
                const auto range = calendarPeriodContaining(day,
                    p == CalendarPeriod::Year ? CalendarPeriod::Month : CalendarPeriod::Day);
                h.buckets.push_back({range, 5h + 27min});
                day = range.end;
            }
            return h;        };
    const auto pageProvider = [&](std::size_t offset,std::optional<TimeRange> range) {
        ++pageReads;
        std::vector<DashboardSession> rows;
        std::size_t skipped=0;
        for(auto session:allSessions) {
            if(range) {
                const auto start=std::max(session.interval.start,range->start), end=std::min(session.interval.end,range->end);
                if(end<=start) continue;
                session.active=session.active && end==session.interval.end;
                session.interval={start,end}; session.duration=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::microseconds(end-start));
            }
            if(skipped++<offset) continue;
            rows.push_back(session); if(rows.size()==50) break;
        }
        return rows;
    };
    const auto rangeProvider = [&](CalendarPeriod p,TimeRange range) {
        ++rangeReads; lastRange=range;
        auto h=historyProvider(p,range.start); h.range=range; h.sessions=pageProvider(0,range); h.buckets.clear(); h.duration={};
        Timestamp start=range.start;
        const auto stride=std::max(1,static_cast<int>((range.end-range.start)/86'400'000'000LL/31)+1);
        while(start<range.end) {
            const auto end=std::min(range.end,calendarPeriodContaining(shiftCalendarPeriod(start,CalendarPeriod::Day,stride),CalendarPeriod::Day).start);
            std::chrono::seconds duration{};
            for(const auto& s:allSessions) {
                const auto a=std::max(start,s.interval.start), b=std::min(end,s.interval.end);
                if(b>a) duration+=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::microseconds(b-a));
            }
            h.buckets.push_back({{start,end},duration}); h.duration+=duration; start=end;
        }
        return h;
    };
    DashboardWindow window([&](auto e) { error = e; }, [&] { return snapshot; },historyProvider,
        [&] { return settings; }, [&](auto s) { settings = s; },{pageProvider,rangeProvider});
    window.showOrActivate();
    HWND own{};
    EnumThreadWindows(GetCurrentThreadId(), [](HWND w, LPARAM p) -> BOOL {
        wchar_t cls[128]{}; GetClassNameW(w, cls, 128);
        if (std::wstring_view(cls) == L"ChronoxDashboardWindow")
            *reinterpret_cast<HWND*>(p) = w;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&own));
    if (!own) throw std::runtime_error("No fixture window");
    const auto dpi = GetDpiForWindow(own);
    RECT rect{0, 0, MulDiv(width, dpi, 96), MulDiv(height, dpi, 96)};
    ui::adjustClientRectForDpi(&rect, static_cast<DWORD>(GetWindowLongPtrW(own, GWL_STYLE)), FALSE, 0, dpi);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(MonitorFromWindow(own, MONITOR_DEFAULTTONEAREST), &monitor))
        throw std::runtime_error("Cannot position review on its monitor");
    const int outerWidth = rect.right - rect.left, outerHeight = rect.bottom - rect.top;
    const int x = monitor.rcWork.left + std::max(0L, (monitor.rcWork.right-monitor.rcWork.left-outerWidth)/2);
    const int y = monitor.rcWork.top + std::max(0L, (monitor.rcWork.bottom-monitor.rcWork.top-outerHeight)/2);
    if (!SetWindowPos(own, nullptr, x, y, outerWidth, outerHeight, SWP_NOZORDER))
        throw std::runtime_error("Cannot resize review window");
    RECT client{}; GetClientRect(own, &client);
    if (client.right != MulDiv(width, dpi, 96) || client.bottom != MulDiv(height, dpi, 96))
        throw std::runtime_error("Review client size does not match requested DIPs");
    const auto caption = L"Chronox - revue isolee / client " + std::to_wstring(client.right) +
        L" x " + std::to_wstring(client.bottom) + L" px / DPI " + std::to_wstring(dpi);
    SetWindowTextW(own, caption.c_str());
    const auto select = [&](int index) {
        // Only this fixture's own HWND. Does not generate keyboard/mouse input.
        SendMessageW(own, WM_LBUTTONUP, 0, MAKELPARAM(MulDiv(30, dpi, 96),
                     MulDiv(120 + 66 * index, dpi, 96)));
    };
    select(page);
    const auto pump = [&] {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (error) std::rethrow_exception(error);
    };
    const auto until = [&](auto end) {
        while (std::chrono::steady_clock::now() < end) { pump(); std::this_thread::sleep_for(10ms); }
    };
    if (!seconds) {
        while (IsWindowVisible(own)) { pump(); std::this_thread::sleep_for(10ms); }
    } else {
        // Test only this fixture's own native controls; no production data or settings.
        select(0); pump();
        SendMessageW(own,WM_VSCROLL,SB_BOTTOM,0); pump();
        const auto listScroll=GetDlgItem(own,3301);
        SCROLLINFO listInfo{sizeof(SCROLLINFO),SIF_ALL},mainInfo{sizeof(SCROLLINFO),SIF_POS};
        if(!listScroll || !IsWindowVisible(listScroll) || !GetScrollInfo(listScroll,SB_CTL,&listInfo) || listInfo.nMax<50)
            throw std::runtime_error("Weekly scrollbar is missing or sessions are truncated");
        GetScrollInfo(own,SB_VERT,&mainInfo); const auto mainPosition=mainInfo.nPos;
        SendMessageW(own,WM_VSCROLL,SB_BOTTOM,reinterpret_cast<LPARAM>(listScroll)); pump();
        GetScrollInfo(listScroll,SB_CTL,&listInfo); GetScrollInfo(own,SB_VERT,&mainInfo);
        if(listInfo.nPos!=listInfo.nMax-static_cast<int>(listInfo.nPage)+1 || mainInfo.nPos!=mainPosition)
            throw std::runtime_error("Weekly scrollbar must reach the final row without scrolling the dashboard");
        SendMessageW(own,WM_VSCROLL,SB_TOP,reinterpret_cast<LPARAM>(listScroll)); pump();
        GetScrollInfo(listScroll,SB_CTL,&listInfo);
        if(listInfo.nPos!=0) throw std::runtime_error("Weekly scrollbar did not return to first row");
        std::cout << "All weekly rows and independent native scrollbar: PASS\n";
        select(1); pump();
        SYSTEMTIME first{},last{};
        const auto localDate=[](Timestamp timestamp) {
            const auto seconds=static_cast<std::time_t>(timestamp/1'000'000);
            std::tm local{}; localtime_s(&local,&seconds);
            return SYSTEMTIME{static_cast<WORD>(local.tm_year+1900),static_cast<WORD>(local.tm_mon+1),0,static_cast<WORD>(local.tm_mday),0,0,0,0};
        };
        first=localDate(shiftCalendarPeriod(snapshot.capturedAt,CalendarPeriod::Day,-10)); last=localDate(snapshot.capturedAt);
        SendMessageW(GetDlgItem(own,3201),DTM_SETSYSTEMTIME,GDT_VALID,reinterpret_cast<LPARAM>(&first));
        SendMessageW(GetDlgItem(own,3202),DTM_SETSYSTEMTIME,GDT_VALID,reinterpret_cast<LPARAM>(&last));
        SendMessageW(own,WM_COMMAND,MAKEWPARAM(3203,BN_CLICKED),0); pump();
        const auto expectedStart=calendarPeriodContaining(shiftCalendarPeriod(snapshot.capturedAt,CalendarPeriod::Day,-10),CalendarPeriod::Day).start;
        if(rangeReads!=1 || !lastRange || lastRange->start!=expectedStart || lastRange->end!=snapshot.todayRange.end)
            throw std::runtime_error("Inclusive native range filter failed");
        const int readsBeforePaint=rangeReads+pageReads;
        InvalidateRect(own,nullptr,FALSE); UpdateWindow(own); pump();
        if(rangeReads+pageReads!=readsBeforePaint) throw std::runtime_error("Read provider called during paint");
        SendMessageW(own,WM_COMMAND,MAKEWPARAM(3204,BN_CLICKED),0); pump();
        std::cout << "Inclusive native dates, reset and paint-only cache: PASS\n";
        // Warm all pages before collecting comparable process-wide resource counts.
        for (int i = 0; i < 6; ++i) { select(i % 3); pump(); }
        select(page);
        until(std::chrono::steady_clock::now() + 16s);
        const auto metrics = [] {
            PROCESS_MEMORY_COUNTERS_EX m{}; m.cb = sizeof(m);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof(m));
            DWORD handles{}; GetProcessHandleCount(GetCurrentProcess(), &handles);
            FILETIME create{}, exit{}, kernel{}, user{};
            GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user);
            const auto ticks = [](FILETIME f) { return (static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
            return std::array<unsigned long long, 6>{ticks(kernel) + ticks(user), m.PrivateUsage,
                m.WorkingSetSize, handles, GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
                GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS)};
        };
        const auto before = metrics(); const auto start = std::chrono::steady_clock::now();
        until(start + std::chrono::seconds(seconds));
        const auto after = metrics();
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::ofstream out(argc > 5 ? argv[5] : "dashboard-resources.json");
        out << "{\"page\":" << page << ",\"width\":" << width << ",\"height\":" << height
            << ",\"dpi\":" << dpi << ",\"seconds\":" << elapsed << ",\"cpu_one_core_percent\":"
            << (after[0] - before[0]) / 1e7 / elapsed * 100 << ",\"before\":[";
        for (int i = 1; i < 6; ++i) out << (i == 1 ? "" : ",") << before[i];
        out << "],\"after\":[";
        for (int i = 1; i < 6; ++i) out << (i == 1 ? "" : ",") << after[i];
        out << "]}\n";
    }
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
