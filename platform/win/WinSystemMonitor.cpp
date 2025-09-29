// Keep clang/Qt Creator happy on non-Windows hosts
#ifndef _WIN32
// Not building on Windows: make this translation unit empty.
static int _win_stub_TU = 0;
#else

#include <platform/win/WinSystemMonitor.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>      // GetPerformanceInfo
#include <tlhelp32.h>   // Toolhelp32Snapshot
#include <winreg.h>     // RegGetValueW
#include <cstdint>
#include <string>

// ---------- CPU % (GetSystemTimes) ----------
static inline unsigned long long ftToULL(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart;
}

static double readCpuPercent() {
    static bool first = true;
    static unsigned long long prevIdle = 0, prevKernel = 0, prevUser = 0;

    FILETIME idleFT{}, kernelFT{}, userFT{};
    if (!GetSystemTimes(&idleFT, &kernelFT, &userFT)) return 0.0;

    unsigned long long idle   = ftToULL(idleFT);
    unsigned long long kernel = ftToULL(kernelFT);
    unsigned long long user   = ftToULL(userFT);

    if (first) { first = false; prevIdle = idle; prevKernel = kernel; prevUser = user; return 0.0; }

    const unsigned long long idleDiff   = idle   - prevIdle;
    const unsigned long long kernelDiff = kernel - prevKernel;
    const unsigned long long userDiff   = user   - prevUser;

    prevIdle = idle; prevKernel = kernel; prevUser = user;

    // NOTE: kernel includes idle time, so subtract idle.
    const unsigned long long sysDiff   = (kernelDiff - idleDiff);
    const unsigned long long totalDiff = sysDiff + userDiff + idleDiff;

    if (totalDiff == 0) return 0.0;
    return 100.0 * double(sysDiff + userDiff) / double(totalDiff);
}

// ---------- Nominal CPU clock (MHz) ----------
static double nominalCpuMHz() {
    // HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0 ~MHz (DWORD)
    HKEY hKey{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS) {
        return 0.0;
    }
    DWORD mhz = 0, cb = sizeof(DWORD);
    LONG rc = RegGetValueW(hKey, nullptr, L"~MHz", RRF_RT_DWORD, nullptr, &mhz, &cb);
    RegCloseKey(hKey);
    return (rc == ERROR_SUCCESS) ? double(mhz) : 0.0;
}

// ---------- Memory (RAM + pagefile/commit) ----------
static MemStats readMem() {
    MemStats ms{};

    // RAM
    MEMORYSTATUSEX msex{};
    msex.dwLength = sizeof(msex);
    if (GlobalMemoryStatusEx(&msex)) {
        ms.total = static_cast<quint64>(msex.ullTotalPhys);
        quint64 free = static_cast<quint64>(msex.ullAvailPhys);
        ms.free = free;
        ms.used = (ms.total > free) ? (ms.total - free) : 0;
    }

    // Pagefile / Commit (treat as "swap")
    PERFORMANCE_INFORMATION pi{};
    if (GetPerformanceInfo(&pi, sizeof(pi))) {
        const quint64 pageSize   = static_cast<quint64>(pi.PageSize);
        const quint64 commitTot  = static_cast<quint64>(pi.CommitTotal) * pageSize;
        const quint64 commitLim  = static_cast<quint64>(pi.CommitLimit) * pageSize;
        // Windows doesn't expose "swap used" exactly; commit charge is the closest public metric.
        ms.swapTotal = commitLim;
        ms.swapUsed  = (commitTot > ms.total) ? (commitTot - ms.total) : 0; // heuristic
        // If you prefer, you can just report commitTot/commitLim directly:
        // ms.swapUsed = commitTot; ms.swapTotal = commitLim;
    }

    return ms;
}

// ---------- Processes & Threads (totals) ----------
static ProcessThreadTotals readProcThreadTotals() {
    ProcessThreadTotals t{};

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return t;

    // Count processes
    PROCESSENTRY32 pe{}; pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do { ++t.processCount; } while (Process32Next(snap, &pe));
    }

    // Count threads
    THREADENTRY32 te{}; te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do { ++t.threadCount; } while (Thread32Next(snap, &te));
    }

    CloseHandle(snap);
    return t;
}

// ---------- ISystemMonitor implementation ----------
CpuStats WinSystemMonitor::getCpuStats() {
    CpuStats s{};
    s.cpuUsage = readCpuPercent();
    s.cpuClock = nominalCpuMHz();   // nominal/base (live MHz would need vendor API/driver)
    s.cpuTemperature = -1;          // not available via public Win32
    // Optional: fill s.cores from PDH or vendor APIs; omitted for portability
    return s;
}

MemStats WinSystemMonitor::getMemStats() {
    return readMem();
}

ProcessThreadTotals WinSystemMonitor::getProcessThreadCount() {
    return readProcThreadTotals();
}


#endif
