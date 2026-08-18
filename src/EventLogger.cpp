// EventLogger.cpp

#include "EventLogger.h"
#include <stdarg.h>
#include <time.h>

// Global instance
EventLogger eventLogger;

EventLogger::EventLogger() 
    : _head(0), _count(0), _liveCallback(nullptr) {
}

void EventLogger::log(const String& message) {
    unsigned long timestamp_ = millis();
    time_t now_;
    time(&now_);  // Get current time
    
    // Store event in ring buffer
    _buffer[_head] = Event(timestamp_, now_, message);
    _head = (_head + 1) % EVENT_BUFFER_SIZE;
    
    if (_count < EVENT_BUFFER_SIZE) {
        _count++;
    }
    
    // Also output to Serial
    Serial.print(formatTimestamp(timestamp_, now_));
    Serial.print(" ");
    Serial.println(message);
    
    // Call live callback (for WebSerial)
    if (_liveCallback) {
        _liveCallback(Event(timestamp_, now_, message));
    }
}

void EventLogger::logf(const char* format, ...) {
    char buffer_[256];
    va_list args_;
    
    va_start(args_, format);
    vsnprintf(buffer_, sizeof(buffer_), format, args_);
    va_end(args_);
    
    log(String(buffer_));
}

std::vector<EventLogger::Event> EventLogger::getEvents() const {
    std::vector<Event> events_;
    events_.reserve(_count);
    
    if (_count == 0) {
        return events_;
    }
    
    // Calculate start position (oldest event)
    size_t start_ = (_count < EVENT_BUFFER_SIZE) ? 0 : _head;
    
    // Add events in chronological order
    for (size_t i_ = 0; i_ < _count; i_++) {
        size_t index_ = (start_ + i_) % EVENT_BUFFER_SIZE;
        events_.push_back(_buffer[index_]);
    }
    
    return events_;
}

void EventLogger::setLiveCallback(std::function<void(const Event&)> callback) {
    _liveCallback = callback;
}

void EventLogger::clearLiveCallback() {
    _liveCallback = nullptr;
}

void EventLogger::clear() {
    _head = 0;
    _count = 0;
}

size_t EventLogger::count() const {
    return _count;
}

String EventLogger::formatEvent(const Event& event) const {
    return formatTimestamp(event.timestamp, event.realTime) + " " + event.message;
}

bool EventLogger::isValidTime(time_t t) const {
    // Time is valid if > year 2020 (Unix timestamp > 1577836800)
    return t > 1577836800;
}

String EventLogger::formatTimestamp(unsigned long timestamp, time_t realTime) const {
    // Use real time if available
    if (isValidTime(realTime)) {
        struct tm timeinfo_;
        localtime_r(&realTime, &timeinfo_);
        
        char buffer_[20];
        strftime(buffer_, sizeof(buffer_), "%Y-%m-%d %H:%M:%S", &timeinfo_);
        return String(buffer_);
    }
    
    // Otherwise use millis() based time
    unsigned long seconds_ = timestamp / 1000;
    unsigned long minutes_ = seconds_ / 60;
    unsigned long hours_ = minutes_ / 60;
    
    seconds_ %= 60;
    minutes_ %= 60;
    hours_ %= 24;
    
    char buffer_[12];
    snprintf(buffer_, sizeof(buffer_), "[%02lu:%02lu:%02lu]", hours_, minutes_, seconds_);
    return String(buffer_);
}