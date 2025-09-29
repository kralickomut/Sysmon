#pragma once

#include <stdlib.h>
#include "CpuStats.h"
#include "MemStats.h"
#include "ProcessStats.h"


class ISystemMonitor {
public:
    virtual ~ISystemMonitor() = default;
    virtual CpuStats getCpuStats() = 0;
    virtual MemStats getMemStats() = 0;
    virtual ProcessThreadTotals getProcessThreadCount() = 0;
};

