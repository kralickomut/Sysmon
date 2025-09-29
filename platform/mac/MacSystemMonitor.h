#pragma once
#include <core/ISystemMonitor.h>

#ifdef __APPLE__
#include <mach/arm/vm_types.h>
#endif

class MacSystemMonitor : public ISystemMonitor {
public:
    ~MacSystemMonitor() override = default;
    CpuStats getCpuStats() override;
    MemStats getMemStats() override;
    ProcessThreadTotals getProcessThreadCount() override;
};


