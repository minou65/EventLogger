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
    
    // Register callback for new events (for WebSerial live updates)
    void setLiveCallback(std::function<void(const Event&)> callback);
    
    // Remove callback
    void clearLiveCallback();
    
    // Clear buffer
    void clear();
    
    // Number of events in buffer
    size_t count() const;
    
    // Formatted output of an event
    String formatEvent(const Event& event) const;
    
private:
    Event _buffer[EVENT_BUFFER_SIZE];
    size_t _head;           // Position for next element
    size_t _count;          // Number of stored events
    std::function<void(const Event&)> _liveCallback;
    
    String formatTimestamp(unsigned long timestamp, time_t realTime) const;
    bool isValidTime(time_t t) const;
};

// Global instance
extern EventLogger eventLogger;

#endif