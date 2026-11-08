#include "HeapMonitor.h"
#include <EventLogger.h>
#include <time.h>

static int _lastLoggedDay = -1;

void logFreeHeapOnBoot() {
    eventLogger.logf(
        EventLogger::Status::INFO,
        "Boot: freeHeap=%u bytes largestFreeBlock=%u bytes",
        ESP.getFreeHeap(),
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void checkFreeHeapAtMidnight() {
    time_t now_;
    time(&now_);

    struct tm timeinfo_;
    localtime_r(&now_, &timeinfo_);

    // Nur gültige Zeit berücksichtigen (NTP synchronisiert)
    if (now_ < 1577836800) {
        return;
    }

    if (timeinfo_.tm_hour == 0 && timeinfo_.tm_mday != _lastLoggedDay) {
        _lastLoggedDay = timeinfo_.tm_mday;

        eventLogger.logf(
            EventLogger::Status::INFO,
            "Midnight: freeHeap=%u bytes largestFreeBlock=%u bytes",
            ESP.getFreeHeap(),
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
}

void loopHeapMonitor() {
    checkFreeHeapAtMidnight();
}

void beginHeapMonitor() {
    logFreeHeapOnBoot();
}
