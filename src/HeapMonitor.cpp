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

// A threshold must be exceeded continuously for this long before a warning
// is emitted, so short allocation spikes (e.g. a web request) are ignored.
#ifndef HEAPMONITOR_WARNING_SUSTAIN_MS
#define HEAPMONITOR_WARNING_SUSTAIN_MS (30UL * 1000UL)
#endif

// Interval between heap samples taken by checkHeapWarnings().
#ifndef HEAPMONITOR_SAMPLE_INTERVAL_MS
#define HEAPMONITOR_SAMPLE_INTERVAL_MS 1000UL
#endif

static uint32_t _freeHeapWarnThreshold = HEAPMONITOR_DEFAULT_FREE_HEAP_WARN_BYTES;
static uint8_t _fragmentationWarnThreshold = HEAPMONITOR_DEFAULT_FRAGMENTATION_WARN_PERCENT;

struct HeapWarningState {
    bool active;
    unsigned long activeSinceMillis;
    unsigned long lastStreamWarnMillis;
    unsigned long lastCacheWarnMillis;
    uint32_t worstValue;
};

static HeapWarningState _freeHeapWarning = {};
static HeapWarningState _fragmentationWarning = {};
static unsigned long _lastSampleMillis = 0;

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

    // ESP.getFreeHeap() counts MALLOC_CAP_INTERNAL memory, so use the free size
    // of the same capability as largestBlock_ to get a consistent ratio.
    uint32_t free8Bit_ = heap_caps_get_free_size(MALLOC_CAP_8BIT);

    uint8_t fragmentation_ = 0;
    if (free8Bit_ > 0 && largestBlock_ < free8Bit_) {
        fragmentation_ = (uint8_t)(100 - (uint32_t)((uint64_t)largestBlock_ * 100 / free8Bit_));
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

// Tracks how long a condition has been exceeded and its worst value since the
// last warning. Returns true when a warning is due on the stream.
static bool updateHeapWarning(HeapWarningState& state_, bool exceeded_, uint32_t value_, bool higherIsWorse_, unsigned long now_) {
    if (!exceeded_) {
        state_.active = false;
        return false;
    }

    if (!state_.active) {
        state_.active = true;
        state_.activeSinceMillis = now_;
        state_.worstValue = value_;
    }
    else if (higherIsWorse_ ? (value_ > state_.worstValue) : (value_ < state_.worstValue)) {
        state_.worstValue = value_;
    }

    if (now_ - state_.activeSinceMillis < HEAPMONITOR_WARNING_SUSTAIN_MS) {
        return false;
    }

    return (state_.lastStreamWarnMillis == 0) ||
        (now_ - state_.lastStreamWarnMillis) >= HEAPMONITOR_WARNING_STREAM_INTERVAL_MS;
}

// Writes the warning to the stream, persists it to the event cache when the
// cache interval has elapsed, and restarts worst-value tracking.
static void emitHeapWarning(HeapWarningState& state_, const char* message_, uint32_t currentValue_, unsigned long now_) {
    state_.lastStreamWarnMillis = now_;

    bool cacheDue_ = (state_.lastCacheWarnMillis == 0) ||
        (now_ - state_.lastCacheWarnMillis) >= HEAPMONITOR_WARNING_CACHE_INTERVAL_MS;

    if (cacheDue_) {
        state_.lastCacheWarnMillis = now_;
        eventLogger.log(message_, EventLogger::Status::WARN);
    }
    else {
        eventLogger.webPrintln(message_, EventLogger::Status::WARN);
    }

    state_.worstValue = currentValue_;
}

// Checks the free heap and fragmentation against the configured thresholds.
// The heap is sampled every HEAPMONITOR_SAMPLE_INTERVAL_MS and a warning is
// only raised once a threshold has been exceeded continuously for
// HEAPMONITOR_WARNING_SUSTAIN_MS, so short allocation spikes are ignored.
// Warnings report the current value, how long the condition has lasted and
// the worst value seen since the previous warning. They are written to the
// stream (webPrintln) at most once per HEAPMONITOR_WARNING_STREAM_INTERVAL_MS
// and persisted to the event cache at most once per
// HEAPMONITOR_WARNING_CACHE_INTERVAL_MS. Warnings are suppressed during the
// startup grace period, since boot-time allocation churn (WiFi, TLS, NTP,
// web server init) can temporarily look like high fragmentation.
void checkHeapWarnings() {
    unsigned long now_ = millis();

    if (now_ - _bootMillis < HEAPMONITOR_WARNING_STARTUP_GRACE_MS) {
        return;
    }

    if (_lastSampleMillis != 0 && (now_ - _lastSampleMillis) < HEAPMONITOR_SAMPLE_INTERVAL_MS) {
        return;
    }
    _lastSampleMillis = now_;

    uint32_t freeHeap_, minFreeHeap_, largestBlock_;
    int32_t deltaSinceBoot_;
    uint8_t fragmentation_ = computeHeapMetrics(freeHeap_, minFreeHeap_, largestBlock_, deltaSinceBoot_);

    if (updateHeapWarning(_freeHeapWarning, freeHeap_ < _freeHeapWarnThreshold, freeHeap_, false, now_)) {
        char message_[80];
        snprintf(message_, sizeof(message_), "Free heap below threshold: %lu bytes for %lus (min %lu)",
            (unsigned long)freeHeap_,
            (now_ - _freeHeapWarning.activeSinceMillis) / 1000UL,
            (unsigned long)_freeHeapWarning.worstValue);
        emitHeapWarning(_freeHeapWarning, message_, freeHeap_, now_);
    }

    if (updateHeapWarning(_fragmentationWarning, fragmentation_ > _fragmentationWarnThreshold, fragmentation_, true, now_)) {
        char message_[96];
        snprintf(message_, sizeof(message_), "Heap fragmentation %u%% for %lus (peak %lu%%, largest block %lu bytes)",
            fragmentation_,
            (now_ - _fragmentationWarning.activeSinceMillis) / 1000UL,
            (unsigned long)_fragmentationWarning.worstValue,
            (unsigned long)largestBlock_);
        emitHeapWarning(_fragmentationWarning, message_, fragmentation_, now_);
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
