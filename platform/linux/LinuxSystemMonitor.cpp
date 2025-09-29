#include <platform/linux/LinuxSystemMonitor.h>
#include <fstream>
#include <sstream>
#include <string>
#include <dirent.h>
#include <unistd.h>
#include <filesystem>
#include <thread>
#include <regex>
#include <sys/types.h>

namespace fs = std::filesystem;

// ----- CPU -----
struct CpuLoad {
    unsigned long long user=0, nice=0, system=0, idle=0;
};

static double readCpuTemperature() {
    namespace fs = std::filesystem;

    for (const auto& entry : fs::directory_iterator("/sys/class/hwmon")) {
        std::ifstream nameFile(entry.path() / "name");
        std::string chipName;
        if (nameFile && std::getline(nameFile, chipName)) {
            // Filter only CPU-related sensors
            if (chipName.find("coretemp") != std::string::npos ||
                chipName.find("k10temp") != std::string::npos ||
                chipName.find("cpu") != std::string::npos)
            {
                // Many hwmon devices have multiple temp*_input
                for (int i = 1; i < 10; ++i) {
                    std::ifstream tempFile(entry.path() / ("temp" + std::to_string(i) + "_input"));
                    if (tempFile) {
                        long milliC;
                        tempFile >> milliC;
                        return milliC / 1000.0; // convert to Celsius
                    }
                }
            }
        }
    }
    return -1.0; // unavailable
}

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

static CpuCoresStats readLinuxCpuCores()
{
    CpuCoresStats stats;
    double sumMhz = 0.0;
    int count = 0;

    // ---- 1) sysfs: /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq ----
    const fs::path sysCpu = "/sys/devices/system/cpu";
    std::regex cpuDirRe(R"(cpu(\d+))");
    bool gotSysfs = false;

    if (fs::exists(sysCpu) && fs::is_directory(sysCpu)) {
        for (const auto& entry : fs::directory_iterator(sysCpu)) {
            if (!entry.is_directory()) continue;
            const std::string name = entry.path().filename().string();
            std::smatch m;
            if (!std::regex_match(name, m, cpuDirRe)) continue;

            int coreId = std::stoi(m[1].str());
            fs::path freqFile = entry.path() / "cpufreq" / "scaling_cur_freq";
            if (!fs::exists(freqFile)) continue;

            std::ifstream f(freqFile);
            long long khz = 0;
            if (f && (f >> khz) && khz > 0) {
                double mhz = khz / 1000.0;
                stats.coresMap[coreId] = mhz;
                sumMhz += mhz;
                ++count;
                gotSysfs = true;
            }
        }
    }

    // ---- 2) fallback: /proc/cpuinfo -> “cpu MHz” lines (one per CPU) ----
    if (!gotSysfs) {
        std::ifstream ci("/proc/cpuinfo");
        std::string line;
        int coreId = 0;
        while (std::getline(ci, line)) {
            if (line.rfind("cpu MHz", 0) == 0) {
                auto pos = line.find(':');
                if (pos != std::string::npos) {
                    double mhz = std::stod(line.substr(pos + 1));
                    stats.coresMap[coreId++] = mhz;
                    sumMhz += mhz;
                    ++count;
                }
            }
        }
    }

    // ---- 3) finalize / last-resort cores count ----
    stats.totalCores = count;
    stats.averageFreq = (count > 0) ? (sumMhz / count) : 0.0;

    if (stats.totalCores == 0) {
        // at least report core count even if freq unavailable
        stats.totalCores = int(std::thread::hardware_concurrency());
    }

    return stats;
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
    cpu.cores = readLinuxCpuCores();
    cpu.cpuClock = cpu.cores.averageFreq;

    /*
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
    */
    cpu.cpuTemperature = readCpuTemperature(); // Usually requires lm-sensors or /sys/class/thermal
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
