#include <mach/arm/vm_types.h>
#include <mach/mach_host.h>
#include <mach/host_info.h>
#include <mach/mach_init.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <QProcess>
#include <QRegularExpressionMatchIterator>
#include <libproc.h>
#include <platform/mac/MacSystemMonitor.h>

// ----- CPU -----
struct CpuLoad {
    natural_t user = 0, system = 0, idle = 0, nice = 0;
};

static double cpuClock() {
    auto readUint64 = [](const char* name, uint64_t& out) -> bool {
        size_t len = sizeof(out);
        if (sysctlbyname(name, &out, &len, nullptr, 0) == 0 && len == sizeof(out) && out > 0) {
            return true;
        }
        return false;
    };

    uint64_t hz = 0;

    // Try max first (often populated)
    if (readUint64("hw.cpufrequency_max", hz)) {
        return hz / 1'000'000.0; // MHz
    }

    // Try nominal/base
    if (readUint64("hw.cpufrequency", hz)) {
        return hz / 1'000'000.0; // MHz
    }

    // Intel Macs sometimes expose the TSC frequency (Apple Silicon won't)
    if (readUint64("machdep.tsc.frequency", hz)) {
        return hz / 1'000'000.0; // MHz
    }


    // OS is not exposing
    return 0.0;
}


static CpuCoresStats readAppleSiliconCpuCores()
{
    CpuCoresStats stats;

    QProcess pm;
    pm.start("/usr/bin/powermetrics", {"--samplers", "cpu_power", "-n", "1"});
    if (!pm.waitForFinished(6000)) {
        pm.kill();
        pm.waitForFinished();
        return stats;  // return empty struct
    }

    const QString out = QString::fromUtf8(pm.readAllStandardOutput());

    // Match lines like "CPU 0 frequency: 1273 MHz"
    QRegularExpression re(R"(CPU\s+(\d+)\s+frequency:\s+(\d+)\s+MHz)",
                          QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator it = re.globalMatch(out);

    double total = 0.0;
    int count = 0;

    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        if (m.hasMatch()) {
            int coreId   = m.captured(1).toInt();
            double mhz   = m.captured(2).toDouble();

            stats.coresMap[coreId] = mhz;  // store per-core freq
            total += mhz;
            count++;
        }
    }

    stats.totalCores = count;
    if (count > 0) {
        stats.averageFreq = total / count;
    }

    return stats;
}

static CpuLoad readCpuLoad() {
    host_cpu_load_info_data_t info;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        (host_info_t)&info, &count) != KERN_SUCCESS) {
        return {};
    }
    CpuLoad l;
    l.user   = info.cpu_ticks[CPU_STATE_USER];
    l.system = info.cpu_ticks[CPU_STATE_SYSTEM];
    l.idle   = info.cpu_ticks[CPU_STATE_IDLE];
    l.nice   = info.cpu_ticks[CPU_STATE_NICE];
    return l;
}

static double cpuPercent() {
    static bool first = true;
    static CpuLoad prev;
    CpuLoad cur = readCpuLoad();
    if (first) { prev = cur; first = false; return 0.0; }

    natural_t userDiff   = cur.user - prev.user;
    natural_t sysDiff    = cur.system - prev.system;
    natural_t idleDiff   = cur.idle - prev.idle;
    natural_t niceDiff   = cur.nice - prev.nice;
    prev = cur;

    double total = userDiff + sysDiff + idleDiff + niceDiff;
    if (total == 0) return 0.0;
    return 100.0 * (userDiff + sysDiff + niceDiff) / total;
}






CpuStats MacSystemMonitor::getCpuStats() {
    CpuStats cpu;
    cpu.cpuUsage = cpuPercent();

#if defined(__APPLE__) && defined(__arm64__)
    cpu.cores = readAppleSiliconCpuCores();
    cpu.cpuClock = cpu.cores.averageFreq;  // use average as "cpuClock"
#else
    cpu.cpuClock = cpuClock();  // Intel Macs
#endif

    cpu.cpuTemperature = -1;
    return cpu;
}

MemStats MacSystemMonitor::getMemStats() {
    MemStats ms;

    // total RAM
    int mib[2] = { CTL_HW, HW_MEMSIZE };
    size_t len = sizeof(ms.total);
    sysctl(mib, 2, &ms.total, &len, nullptr, 0);

    // virtual memory stats
    vm_size_t pageSize;
    host_page_size(mach_host_self(), &pageSize);

    vm_statistics64_data_t vmStats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          (host_info64_t)&vmStats, &count) == KERN_SUCCESS) {
        quint64 freeMem  = (quint64)vmStats.free_count * pageSize;
        quint64 active   = (quint64)vmStats.active_count * pageSize;
        quint64 inactive = (quint64)vmStats.inactive_count * pageSize;
        quint64 wired    = (quint64)vmStats.wire_count * pageSize;
        ms.free = freeMem;
        ms.used = active + inactive + wired;
    }

    // swap info
    struct xsw_usage swap;
    size_t swlen = sizeof(swap);
    if (sysctlbyname("vm.swapusage", &swap, &swlen, nullptr, 0) == 0) {
        ms.swapTotal = swap.xsu_total;
        ms.swapUsed  = swap.xsu_used;
    }

    return ms;
}

ProcessThreadTotals MacSystemMonitor::getProcessThreadCount() {
    ProcessThreadTotals totals{};

    // 1) Ask how many PIDs there are, then fetch them
    int bytesNeeded = proc_listallpids(nullptr, 0);
    if (bytesNeeded <= 0) return totals;

    int cap = bytesNeeded / sizeof(pid_t) + 64;           // small headroom
    std::vector<pid_t> pids(cap);
    int bytesFilled = proc_listallpids(pids.data(), cap * (int)sizeof(pid_t));
    if (bytesFilled <= 0) return totals;

    int n = bytesFilled / (int)sizeof(pid_t);

    // 2) For each PID, fetch task info (contains thread count)
    for (int i = 0; i < n; ++i) {
        pid_t pid = pids[i];
        if (pid <= 0) continue;

        proc_taskallinfo tai{};
        int r = proc_pidinfo(pid, PROC_PIDTASKALLINFO, 0,
                             &tai, (int)sizeof(tai));
        if (r == (int)sizeof(tai)) {
            totals.processCount++;
            totals.threadCount += tai.ptinfo.pti_threadnum;  // thread count in this process
        }
        // If r <= 0, the process may have exited or be restricted; just skip.
    }

    return totals;
}






