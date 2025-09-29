#pragma once

#include <QString>
#include <map>

struct CpuCoresStats {
    qint8 totalCores = 0;
    double averageFreq = 0;
    std::map<int, double> coresMap = {};
};

struct CpuStats {
    double cpuUsage = 0;
    double cpuClock = 0;
    CpuCoresStats cores = {};
    double cpuTemperature = 0;
};

