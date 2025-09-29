// main.cpp
#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <memory>

// Core API (your domain types + interface)
#include <core/ISystemMonitor.h>
#include <core/CpuStats.h>
#include <core/MemStats.h>
#include <core/ProcessStats.h>

// Pick the concrete implementation for this platform
#if defined(Q_OS_MAC)
#include <platform/mac/MacSystemMonitor.h>
using MonitorImpl = MacSystemMonitor;
#elif defined(Q_OS_LINUX)
#include <platform/linux/LinuxSystemMonitor.h>
using MonitorImpl = LinuxSystemMonitor;
#elif defined(Q_OS_WIN)
#include <platform/win/WinSystemMonitor.h>
using MonitorImpl = WindowsSystemMonitor;
#else
#error "Unsupported platform"
#endif

static std::unique_ptr<ISystemMonitor> g_monitor;

static void printOnce()
{
    const CpuStats cpu = g_monitor->getCpuStats();
    const MemStats mem = g_monitor->getMemStats();
    const ProcessThreadTotals pt = g_monitor->getProcessThreadCount();

    // CPU headline
    qDebug().noquote()
        << "CPU Usage:" << QString::number(cpu.cpuUsage, 'f', 1) + "%"
        << "| Avg Freq:" << QString::number(cpu.cores.averageFreq, 'f', 0) + " MHz";

    // Per-core (if available)
    if (!cpu.cores.coresMap.empty()) {
        QStringList lines;
        for (const auto& [coreId, mhz] : cpu.cores.coresMap) {
            lines << QString("Core %1: %2 MHz").arg(coreId).arg(mhz, 0, 'f', 0);
        }
        qDebug().noquote() << "Per-core:" << lines.join(", ");
    }

    // Temperature (if your struct uses -1 for N/A)
    if (cpu.cpuTemperature >= 0.0)
        qDebug().noquote() << "Temp:" << QString::number(cpu.cpuTemperature, 'f', 1) + " °C";
    else
        qDebug().noquote() << "Temp: N/A";

    // Memory
    qDebug().noquote() << "RAM used:"
                       << (mem.used / (1024*1024)) << "MB /"
                       << (mem.total / (1024*1024)) << "MB";
    qDebug().noquote() << "Swap used:"
                       << (mem.swapUsed / (1024*1024)) << "MB /"
                       << (mem.swapTotal / (1024*1024)) << "MB";

    // Processes / threads
    qDebug().noquote() << "Processes:" << pt.processCount
                       << "| Threads:" << pt.threadCount;

    qDebug().noquote() << "Core count:" << (cpu.cores.totalCores) << "Cores";

    qDebug() << "-----------------------------";
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    g_monitor = std::make_unique<MonitorImpl>();

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &printOnce);
    timer.start(3000); // every 3 seconds

    // print immediately once at start
    QTimer::singleShot(0, &printOnce);

    return app.exec();
}
