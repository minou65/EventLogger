// EventLogger.h

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
    struct Event {
        unsigned long timestamp;
        time_t realTime;  // Real-time clock (Unix timestamp)
        String message;

        Event() : timestamp(0), realTime(0), message("") {}
        Event(unsigned long ts, time_t rt, const String& msg) : timestamp(ts), realTime(rt), message(msg) {}
    };

    EventLogger();

    // Add event
    void log(const String& message);

    // Add event with printf-style formatting
    void logf(const char* format, ...);

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

    // Formatted output of an event
    String formatEvent(const Event& event) const;

    // Optional web module (ESPAsyncWebServer)
    bool beginWeb(AsyncWebServer* server, const String& basePath = "/webserial");
    void loop(); // reserved for API compatibility

    // Incoming web command callback
    void setCommandCallback(std::function<void(const String&)> callback);
    void clearCommandCallback();

    // Outgoing text stream (Web console output, not persisted to event buffer)
    void webPrint(const String& message);
    void webPrintln(const String& message);
    void webPrintln();

    // Connected stream clients
    size_t webClientCount() const;

private:
    Event _buffer[EVENT_BUFFER_SIZE];
    size_t _head;           // Position for next element
    size_t _count;          // Number of stored events
    std::function<void(const Event&)> _liveCallback;
    std::function<void(const String&)> _commandCallback;

    String formatTimestamp(unsigned long timestamp, time_t realTime) const;
    bool isValidTime(time_t t) const;

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

#endif