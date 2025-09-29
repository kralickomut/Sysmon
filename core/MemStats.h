#pragma once

#include <QtCore/qtypes.h>


// ----- RAM -----
struct MemStats {
    quint64 total = 0;
    quint64 used = 0;
    quint64 free = 0;
    quint64 swapUsed = 0;
    quint64 swapTotal = 0;
};
