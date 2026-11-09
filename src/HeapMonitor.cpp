#include <EventLogger.h>
#include <time.h>

static int _lastLoggedDay = -1;
static uint32_t _bootFreeHeap = 0;
static unsigned long _bootMillis = 0;

// Grace period after boot during which heap warnings are suppressed.
// WiFi connection, TLS handshakes, NTP sync and web server initialization
// cause short-lived allocation churn that can temporarily look like high
// fragmentation, even though the heap settles down shortly after.
#ifndef HEAPMONITOR_WARNING_STARTUP_GRACE_MS
#define HEAPMONITOR_WARNING_STARTUP_GRACE_MS (2UL * 60UL * 1000UL)
#endif

// Default warning thresholds (can be overridden via setHeapWarningThresholds()).
#ifndef HEAPMONITOR_DEFAULT_FREE_HEAP_WARN_BYTES
#define HEAPMONITOR_DEFAULT_FREE_HEAP_WARN_BYTES 40000UL
#endif
#ifndef HEAPMONITOR_DEFAULT_FRAGMENTATION_WARN_PERCENT
#define HEAPMONITOR_DEFAULT_FRAGMENTATION_WARN_PERCENT 50
#endif

// Minimum time between repeated warnings of the same kind on the stream
// (webPrintln), to avoid flooding the client while a condition persists.
#ifndef HEAPMONITOR_WARNING_STREAM_INTERVAL_MS
#define HEAPMONITOR_WARNING_STREAM_INTERVAL_MS (60UL * 1000UL)
#endif

// Minimum time between repeated warnings of the same kind persisted to the
// event ring buffer (cache). Kept deliberately longer than the stream
// interval so the cache is not flooded while a condition persists.
#ifndef HEAPMONITOR_WARNING_CACHE_INTERVAL_MS
#define HEAPMONITOR_WARNING_CACHE_INTERVAL_MS (60UL * 60UL * 1000UL)
#endif

static uint32_t _freeHeapWarnThreshold = HEAPMONITOR_DEFAULT_FREE_HEAP_WARN_BYTES;
static uint8_t _fragmentationWarnThreshold = HEAPMONITOR_DEFAULT_FRAGMENTATION_WARN_PERCENT;

static unsigned long _lastFreeHeapStreamWarnMillis = 0;
static unsigned long _lastFreeHeapCacheWarnMillis = 0;
static unsigned long _lastFragmentationStreamWarnMillis = 0;
static unsigned long _lastFragmentationCacheWarnMillis = 0;

void setHeapWarningThresholds(uint32_t freeHeapBytes_, uint8_t fragmentationPercent_) {
    _freeHeapWarnThreshold = freeHeapBytes_;
    _fragmentationWarnThreshold = fragmentationPercent_;
}

void logFreeHeapOnBoot() {
    _bootFreeHeap = ESP.getFreeHeap();

    eventLogger.logf(
        EventLogger::Status::INFO,
        "Boot: freeHeap=%u bytes largestFreeBlock=%u bytes",
        _bootFreeHeap,
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

// Builds a human readable uptime string, e.g. "3d 14h"
static String formatUptime() {
    unsigned long uptimeSeconds_ = millis() / 1000UL;

    unsigned long days_ = uptimeSeconds_ / 86400UL;
    unsigned long hours_ = (uptimeSeconds_ % 86400UL) / 3600UL;
    unsigned long minutes_ = (uptimeSeconds_ % 3600UL) / 60UL;

    char buffer_[32];
    if (days_ > 0) {
        snprintf(buffer_, sizeof(buffer_), "%lud %luh", days_, hours_);
    }
    else if (hours_ > 0) {
        snprintf(buffer_, sizeof(buffer_), "%luh %lum", hours_, minutes_);
    }
    else {
        snprintf(buffer_, sizeof(buffer_), "%lum", minutes_);
    }

    return String(buffer_);
}

// Computes current heap metrics and returns the fragmentation percentage.
// Also fills in the provided out-parameters.
static uint8_t computeHeapMetrics(uint32_t& freeHeap_, uint32_t& minFreeHeap_, uint32_t& largestBlock_, int32_t& deltaSinceBoot_) {
    freeHeap_ = ESP.getFreeHeap();
    minFreeHeap_ = ESP.getMinFreeHeap();
    largestBlock_ = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    deltaSinceBoot_ = (int32_t)freeHeap_ - (int32_t)_bootFreeHeap;

    uint8_t fragmentation_ = 0;
    if (freeHeap_ > 0) {
        fragmentation_ = (uint8_t)(100 - (uint32_t)((uint64_t)largestBlock_ * 100 / freeHeap_));
    }
    return fragmentation_;
}

void printHeapReport() {
    uint32_t freeHeap_, minFreeHeap_, largestBlock_;
    int32_t deltaSinceBoot_;
    uint8_t fragmentation_ = computeHeapMetrics(freeHeap_, minFreeHeap_, largestBlock_, deltaSinceBoot_);

    eventLogger.webPrintln("Heap Report");
    eventLogger.webPrintf(EventLogger::Status::INFO, 1, "Uptime:             %s", formatUptime().c_str());
    eventLogger.webPrintf(EventLogger::Status::INFO, 1, "Free Heap:          %u bytes", freeHeap_);
    eventLogger.webPrintf(EventLogger::Status::INFO, 1, "Min Free Heap:      %u bytes", minFreeHeap_);
    eventLogger.webPrintf(EventLogger::Status::INFO, 1, "Largest Block:      %u bytes", largestBlock_);
    eventLogger.webPrintf(EventLogger::Status::INFO, 1, "Fragmentation:      %u %%", fragmentation_);
    eventLogger.webPrintf(EventLogger::Status::INFO, 1, "Change Since Boot:  %ld bytes", deltaSinceBoot_);

    // Also persist a compact summary in the event ring buffer so the report
    // remains traceable even without an active web client (webPrintln/webPrintf
    // is not persisted).
    eventLogger.logf(
        EventLogger::Status::INFO,
        "Heap report: uptime=%s free=%u min=%u largest=%u frag=%u%% delta=%ld",
        formatUptime().c_str(),
        freeHeap_,
        minFreeHeap_,
        largestBlock_,
        fragmentation_,
        deltaSinceBoot_);
}

void checkFreeHeapAtMidnight() {
    time_t now_;
    time(&now_);

    struct tm timeinfo_;
    localtime_r(&now_, &timeinfo_);

    // Only consider valid time (NTP synchronized)
    if (now_ < 1577836800) {
        return;
    }

    if (timeinfo_.tm_hour == 0 && timeinfo_.tm_mday != _lastLoggedDay) {
        _lastLoggedDay = timeinfo_.tm_mday;
        printHeapReport();
    }
}

// Checks the free heap and fragmentation against the configured thresholds.
// Warnings are written to the stream (webPrintln) at most once per
// HEAPMONITOR_WARNING_STREAM_INTERVAL_MS, and persisted to the event cache
// at most once per HEAPMONITOR_WARNING_CACHE_INTERVAL_MS, to avoid flooding
// either output while a condition persists. Warnings are suppressed during
// the startup grace period, since boot-time allocation churn (WiFi, TLS,
// NTP, web server init) can temporarily look like high fragmentation.
void checkHeapWarnings() {
    if (millis() - _bootMillis < HEAPMONITOR_WARNING_STARTUP_GRACE_MS) {
        return;
    }

    uint32_t freeHeap_, minFreeHeap_, largestBlock_;
    int32_t deltaSinceBoot_;
    uint8_t fragmentation_ = computeHeapMetrics(freeHeap_, minFreeHeap_, largestBlock_, deltaSinceBoot_);

    unsigned long now_ = millis();

    if (freeHeap_ < _freeHeapWarnThreshold) {
        bool streamDue_ = (_lastFreeHeapStreamWarnMillis == 0) ||
            (now_ - _lastFreeHeapStreamWarnMillis) >= HEAPMONITOR_WARNING_STREAM_INTERVAL_MS;
        bool cacheDue_ = (_lastFreeHeapCacheWarnMillis == 0) ||
            (now_ - _lastFreeHeapCacheWarnMillis) >= HEAPMONITOR_WARNING_CACHE_INTERVAL_MS;

        if (streamDue_) {
            _lastFreeHeapStreamWarnMillis = now_;

            char message_[64];
            snprintf(message_, sizeof(message_), "Free heap below threshold: %u bytes", freeHeap_);

            if (cacheDue_) {
                _lastFreeHeapCacheWarnMillis = now_;
                eventLogger.log(message_, EventLogger::Status::WARN);
            }
            else {
                eventLogger.webPrintln(message_, EventLogger::Status::WARN);
            }
        }
    }

    if (fragmentation_ > _fragmentationWarnThreshold) {
        bool streamDue_ = (_lastFragmentationStreamWarnMillis == 0) ||
            (now_ - _lastFragmentationStreamWarnMillis) >= HEAPMONITOR_WARNING_STREAM_INTERVAL_MS;
        bool cacheDue_ = (_lastFragmentationCacheWarnMillis == 0) ||
            (now_ - _lastFragmentationCacheWarnMillis) >= HEAPMONITOR_WARNING_CACHE_INTERVAL_MS;

        if (streamDue_) {
            _lastFragmentationStreamWarnMillis = now_;

            char message_[64];
            snprintf(message_, sizeof(message_), "Heap fragmentation reached %u%%", fragmentation_);

            if (cacheDue_) {
                _lastFragmentationCacheWarnMillis = now_;
                eventLogger.log(message_, EventLogger::Status::WARN);
            }
            else {
                eventLogger.webPrintln(message_, EventLogger::Status::WARN);
            }
        }
    }
}

void loopHeapMonitor() {
    checkFreeHeapAtMidnight();
    checkHeapWarnings();
}

void beginHeapMonitor() {
    _bootMillis = millis();
    logFreeHeapOnBoot();
}
