#include <platform/linux/LinuxSystemMonitor.h>
#include <fstream>
#include <sstream>
#include <string>
#include <dirent.h>
#include <unistd.h>
#include <sys/types.h>
#include <platform/mac/MacSystemMonitor.h>

// -------- CPU LOAD --------
static CpuLoad readCpuLoad()
{
    CpuLoad load{};
    std::ifstream file("/proc/stat");
    std::string cpu;
    if (file.is_open()) {
        file >> cpu >> load.user >> load.nice >> load.system >> load.idle;
    }
    return load;
}

static double cpuPercent()
{
    static bool first = true;
    static CpuLoad prev;
    CpuLoad cur = readCpuLoad();

    if (first) { prev = cur; first = false; return 0.0; }

    unsigned long long userDiff = cur.user - prev.user;
    unsigned long long niceDiff = cur.nice - prev.nice;
    unsigned long long sysDiff  = cur.system - prev.system;
    unsigned long long idleDiff = cur.idle - prev.idle;
    prev = cur;

    double total = userDiff + niceDiff + sysDiff + idleDiff;
    if (total == 0) return 0.0;
    return 100.0 * (userDiff + niceDiff + sysDiff) / total;
}

// -------- MEMORY --------
static MemStats readMemStats()
{
    MemStats ms{};
    std::ifstream file("/proc/meminfo");
    std::string key;
    unsigned long value;
    std::string unit;

    unsigned long memFree=0, buffers=0, cached=0;

    while (file >> key >> value >> unit) {
        if (key == "MemTotal:")   ms.total = value * 1024ULL;
        else if (key == "MemFree:") memFree = value * 1024ULL;
        else if (key == "Buffers:") buffers = value * 1024ULL;
        else if (key == "Cached:") cached = value * 1024ULL;
        else if (key == "SwapTotal:") ms.swapTotal = value * 1024ULL;
        else if (key == "SwapFree:") {
            unsigned long swapFree = value * 1024ULL;
            ms.swapUsed = ms.swapTotal - swapFree;
        }
    }

    ms.free = memFree + buffers + cached;
    ms.used = ms.total - ms.free;
    return ms;
}

// -------- PROCESSES / THREADS --------
static ProcessThreadTotals readProcessThreadTotals()
{
    ProcessThreadTotals totals{};

    DIR *proc = opendir("/proc");
    if (!proc) return totals;

    struct dirent *entry;
    while ((entry = readdir(proc)) != nullptr) {
        // Only numeric dirs are PIDs
        pid_t pid = atoi(entry->d_name);
        if (pid <= 0) continue;

        std::string statusPath = std::string("/proc/") + entry->d_name + "/status";
        std::ifstream statusFile(statusPath);
        if (!statusFile.is_open()) continue;

        std::string line;
        int threads = 0;
        while (std::getline(statusFile, line)) {
            if (line.rfind("Threads:", 0) == 0) {
                std::istringstream iss(line);
                std::string tmp;
                iss >> tmp >> threads;
                break;
            }
        }

        totals.processCount++;
        totals.threadCount += threads;
    }

    closedir(proc);
    return totals;
}


// -------- Implementation --------
CpuStats LinuxSystemMonitor::getCpuStats()
{
    CpuStats cpu{};
    cpu.cpuUsage = cpuPercent();

    // For clock frequency, we can read from /proc/cpuinfo (first processor)
    std::ifstream file("/proc/cpuinfo");
    std::string line;
    while (std::getline(file, line)) {
        if (line.rfind("cpu MHz", 0) == 0) {
            auto pos = line.find(":");
            if (pos != std::string::npos) {
                cpu.cpuClock = std::stod(line.substr(pos + 1));
            }
            break;
        }
    }

    cpu.cpuTemperature = -1; // Usually requires lm-sensors or /sys/class/thermal
    return cpu;
}

MemStats LinuxSystemMonitor::getMemStats()
{
    return readMemStats();
}

ProcessThreadTotals LinuxSystemMonitor::getProcessThreadCount()
{
    return readProcessThreadTotals();
}
