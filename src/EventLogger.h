#ifndef _EVENTLOGGER_h
#define _EVENTLOGGER_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif

#include <vector>
#include <functional>
#include <time.h>

// Configurable buffer size - can be overridden by user
#ifndef EVENT_BUFFER_SIZE
#define EVENT_BUFFER_SIZE 100  // Default: Number of events in ring buffer
#endif

// Async Web support detection (optional dependency)
#ifndef EVENTLOGGER_HAS_ASYNC_WEB
#if defined(ESP32) && defined(__has_include)
#if __has_include(<ESPAsyncWebServer.h>)
#define EVENTLOGGER_HAS_ASYNC_WEB 1
#else
#define EVENTLOGGER_HAS_ASYNC_WEB 0
#endif
#else
#define EVENTLOGGER_HAS_ASYNC_WEB 0
#endif
#endif

#if EVENTLOGGER_HAS_ASYNC_WEB
#include <ESPAsyncWebServer.h>
#else
class AsyncWebServer;
class AsyncWebServerRequest;
class AsyncEventSource;
#endif

class EventLogger {
public:
    enum class Status : uint8_t {
        OK,
        WARN,
        INFO,
        DEBUG,
        ERROR,
        VERBOSE
    };

    // Maximum supported indentation level for hierarchical logs
    static const uint8_t MAX_INDENT_LEVEL = 4;

    struct Event {
        unsigned long timestamp;
        time_t realTime;  // Real-time clock (Unix timestamp)
        Status status;
        uint8_t indent;   // Indentation level (0-4) for hierarchical logs
        char message[96]; // Feste Gr��e statt String -> keine Heap-Allokation mehr

        Event() : timestamp(0), realTime(0), status(Status::INFO), indent(0) {
            message[0] = '\0';
        }
        Event(unsigned long ts, time_t rt, const String& msg, Status st = Status::INFO, uint8_t ind = 0)
            : timestamp(ts), realTime(rt), status(st), indent(ind) {
            strncpy(message, msg.c_str(), sizeof(message) - 1);
            message[sizeof(message) - 1] = '\0';
        }
    };

    EventLogger();

    // Add event (backward compatible: defaults to INFO)
    void log(const String& message);
    void log(const String& message, Status status);
    void log(const String& message, Status status, uint8_t indent);

    // Enable or disable a status for buffered and live output
    bool setStatusEnabled(Status status, bool enabled);

    // Add event with printf-style formatting
    void logf(const char* format, ...);
    void logf(Status status, const char* format, ...);
    void logf(Status status, uint8_t indent, const char* format, ...);

    // Get all events from buffer (oldest first)
    std::vector<Event> getEvents() const;

    // Register callback for new events
    void setLiveCallback(std::function<void(const Event&)> callback);

    // Remove callback
    void clearLiveCallback();

    // Clear buffer
    void clear();

    // Number of events in buffer
    size_t count() const;

    // Number of events overwritten since construction or the last clear
    uint32_t getLostEventCount() const;

    // Formatted output of an event
    String formatEvent(const Event& event) const;

    // Optional web module (ESPAsyncWebServer)
    bool beginWeb(AsyncWebServer* server, const String& basePath = "/webserial");
    void loop(); // reserved for API compatibility

    // Incoming web command callback
    void setCommandCallback(std::function<void(const String&)> callback);
    void clearCommandCallback();

    // Outgoing text stream (Web console output, not persisted to event buffer)
    void webPrint(const String& message, Status status = Status::INFO, uint8_t indent = 0);
    void webPrintln(const String& message, Status status = Status::INFO, uint8_t indent = 0);
    void webPrintln();
    void webPrintf(const char* format, ...);
    void webPrintf(Status status, const char* format, ...);
    void webPrintf(Status status, uint8_t indent, const char* format, ...);

    // Connected stream clients
    size_t webClientCount() const;

private:
    Event _buffer[EVENT_BUFFER_SIZE];
    size_t _head;
    size_t _count;
    uint32_t _lostEventCount;
    uint8_t _enabledStatusMask;
    mutable portMUX_TYPE _bufferMux = portMUX_INITIALIZER_UNLOCKED;

    std::function<void(const Event&)> _liveCallback;
    std::function<void(const String&)> _commandCallback;

    String formatTimestamp(unsigned long timestamp, time_t realTime) const;
    bool isValidTime(time_t t) const;
    String formatStatusTag(Status status) const;
    static const char* statusToText(Status status);
    static uint8_t statusBit(Status status);
    bool isStatusEnabled(Status status) const;
    static String indentToSpaces(uint8_t indent);

    bool processWebCommand(const String& command);

#if EVENTLOGGER_HAS_ASYNC_WEB
    AsyncWebServer* _webServer;
    AsyncEventSource* _eventSource;
    bool _webInitialized;

    String _webBasePath;
    String _webHistoryPath;
    String _webStreamPath;
    String _webCommandPath;

    void setupWebRoutes();
    void broadcastWebLine(const String& line);
    String buildHistoryJson() const;
    static String escapeJson(const String& value);
#endif
};

// Global instance
extern EventLogger eventLogger;

// -----------------------------------------------------------------------
// Logging convenience macros
// -----------------------------------------------------------------------
// Provide short, consistent macros for the most common log levels.
// Internally these forward to the existing EventLogger::logf() overloads.
//
// Example:
//   LOG_OK("Initialization complete");
//   LOG_INFO("WiFi connected");
//   LOG_WARN("Low RSSI: %d", rssi);
//   LOG_ERROR("Connection lost");
//   LOG_DEBUG("Packet received: %u bytes", len);
#ifndef LOG_OK
#define LOG_OK(format, ...)    eventLogger.logf(EventLogger::Status::OK,    format, ##__VA_ARGS__)
#endif

#ifndef LOG_INFO
#define LOG_INFO(format, ...)  eventLogger.logf(EventLogger::Status::INFO,  format, ##__VA_ARGS__)
#endif

#ifndef LOG_WARN
#define LOG_WARN(format, ...)  eventLogger.logf(EventLogger::Status::WARN,  format, ##__VA_ARGS__)
#endif

#ifndef LOG_ERROR
#define LOG_ERROR(format, ...) eventLogger.logf(EventLogger::Status::ERROR, format, ##__VA_ARGS__)
#endif

#ifndef LOG_DEBUG
#define LOG_DEBUG(format, ...) eventLogger.logf(EventLogger::Status::DEBUG, format, ##__VA_ARGS__)
#endif

// -----------------------------------------------------------------------
// Logging convenience macros with explicit indent level
// -----------------------------------------------------------------------
// Indentation levels range from 0 to EventLogger::MAX_INDENT_LEVEL.
#ifndef LOG_OK_AT
#define LOG_OK_AT(indent, format, ...)    eventLogger.logf(EventLogger::Status::OK,    indent, format, ##__VA_ARGS__)
#endif

#ifndef LOG_INFO_AT
#define LOG_INFO_AT(indent, format, ...)  eventLogger.logf(EventLogger::Status::INFO,  indent, format, ##__VA_ARGS__)
#endif

#ifndef LOG_WARN_AT
#define LOG_WARN_AT(indent, format, ...)  eventLogger.logf(EventLogger::Status::WARN,  indent, format, ##__VA_ARGS__)
#endif

#ifndef LOG_ERROR_AT
#define LOG_ERROR_AT(indent, format, ...) eventLogger.logf(EventLogger::Status::ERROR, indent, format, ##__VA_ARGS__)
#endif

#ifndef LOG_DEBUG_AT
#define LOG_DEBUG_AT(indent, format, ...) eventLogger.logf(EventLogger::Status::DEBUG, indent, format, ##__VA_ARGS__)
#endif

// -----------------------------------------------------------------------
// Web print convenience macros
// -----------------------------------------------------------------------
// Same idea as LOG_* macros, but for the outgoing web stream (webPrintf),
// which is not persisted to the event buffer.
//
// Example:
//   WEB_LOG_OK("Initialization complete");
//   WEB_LOG_INFO("WiFi connected");
//   WEB_LOG_WARN("Low RSSI: %d", rssi);
//   WEB_LOG_ERROR("Connection lost");
//   WEB_LOG_DEBUG("Packet received: %u bytes", len);
#ifndef WEB_LOG_OK
#define WEB_LOG_OK(format, ...)    eventLogger.webPrintf(EventLogger::Status::OK,    format, ##__VA_ARGS__)
#endif

#ifndef WEB_LOG_INFO
#define WEB_LOG_INFO(format, ...)  eventLogger.webPrintf(EventLogger::Status::INFO,  format, ##__VA_ARGS__)
#endif

#ifndef WEB_LOG_WARN
#define WEB_LOG_WARN(format, ...)  eventLogger.webPrintf(EventLogger::Status::WARN,  format, ##__VA_ARGS__)
#endif

#ifndef WEB_LOG_ERROR
#define WEB_LOG_ERROR(format, ...) eventLogger.webPrintf(EventLogger::Status::ERROR, format, ##__VA_ARGS__)
#endif

#ifndef WEB_LOG_DEBUG
#define WEB_LOG_DEBUG(format, ...) eventLogger.webPrintf(EventLogger::Status::DEBUG, format, ##__VA_ARGS__)
#endif

// -----------------------------------------------------------------------
// Web print convenience macros with explicit indent level
// -----------------------------------------------------------------------
// Indentation levels range from 0 to EventLogger::MAX_INDENT_LEVEL.
#ifndef WEB_LOG_OK_AT
#define WEB_LOG_OK_AT(indent, format, ...)    eventLogger.webPrintf(EventLogger::Status::OK,    indent, format, ##__VA_ARGS__)
#endif

#ifndef WEB_LOG_INFO_AT
#define WEB_LOG_INFO_AT(indent, format, ...)  eventLogger.webPrintf(EventLogger::Status::INFO,  indent, format, ##__VA_ARGS__)
#endif

#ifndef WEB_LOG_WARN_AT
#define WEB_LOG_WARN_AT(indent, format, ...)  eventLogger.webPrintf(EventLogger::Status::WARN,  indent, format, ##__VA_ARGS__)
#endif

#ifndef WEB_LOG_ERROR_AT
#define WEB_LOG_ERROR_AT(indent, format, ...) eventLogger.webPrintf(EventLogger::Status::ERROR, indent, format, ##__VA_ARGS__)
#endif

#ifndef WEB_LOG_DEBUG_AT
#define WEB_LOG_DEBUG_AT(indent, format, ...) eventLogger.webPrintf(EventLogger::Status::DEBUG, indent, format, ##__VA_ARGS__)
#endif

#endif