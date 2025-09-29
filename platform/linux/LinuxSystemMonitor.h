#pragma once

#include <core/ISystemMonitor.h>


class LinuxSystemMonitor : public ISystemMonitor {
public:
    ~LinuxSystemMonitor() override = default;
    CpuStats getCpuStats() override;
    MemStats getMemStats() override;
    ProcessThreadTotals getProcessThreadCount() override;
};
