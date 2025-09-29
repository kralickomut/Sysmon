#pragma once

#include <mach/arm/vm_types.h>
#include <core/ISystemMonitor.h>


class MacSystemMonitor : public ISystemMonitor {
public:
    ~MacSystemMonitor() override = default;
    CpuStats getCpuStats() override;
    MemStats getMemStats() override;
    ProcessThreadTotals getProcessThreadCount() override;
};

// ----- CPU -----
struct CpuLoad {
    natural_t user = 0, system = 0, idle = 0, nice = 0;
};
