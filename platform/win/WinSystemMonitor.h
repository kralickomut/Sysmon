#pragma once

#include <core/ISystemMonitor.h>


class WinSystemMonitor : public ISystemMonitor {
public:
    ~WinSystemMonitor() override = default;
    CpuStats getCpuStats() override;
    MemStats getMemStats() override;
    ProcessThreadTotals getProcessThreadCount() override;
};
