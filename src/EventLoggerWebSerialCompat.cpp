#include "EventLoggerWebSerialCompat.h"
#include <stdarg.h>
#include <vector>

EventLoggerWebSerialCompat EventWebSerial;

bool EventLoggerWebSerialCompat::begin(AsyncWebServer* server, const String& path) {
    return eventLogger.beginWeb(server, path);
}

void EventLoggerWebSerialCompat::onMessage(MessageCallback callback) {
    _messageCallback = callback;

    if (_messageCallback) {
        eventLogger.setCommandCallback([this](const String& command_) {
            this->forwardCommand(command_);
            });
    }
    else {
        eventLogger.clearCommandCallback();
    }
}

void EventLoggerWebSerialCompat::print(const String& message) {
    eventLogger.webPrint(message);
}

void EventLoggerWebSerialCompat::println(const String& message) {
    eventLogger.webPrintln(message);
}

void EventLoggerWebSerialCompat::println() {
    eventLogger.webPrintln();
}

void EventLoggerWebSerialCompat::printf(const char* format, ...) {
    char buffer_[256];
    va_list args_;

    va_start(args_, format);
    vsnprintf(buffer_, sizeof(buffer_), format, args_);
    va_end(args_);

    eventLogger.webPrint(String(buffer_));
}

int EventLoggerWebSerialCompat::getConnectionCount() const {
    return static_cast<int>(eventLogger.webClientCount());
}

void EventLoggerWebSerialCompat::loop() {
    eventLogger.loop();
}

void EventLoggerWebSerialCompat::forwardCommand(const String& command) {
    if (!_messageCallback) {
        return;
    }

    std::vector<uint8_t> data_;
    data_.reserve(command.length());

    for (size_t i_ = 0; i_ < command.length(); i_++) {
        data_.push_back(static_cast<uint8_t>(command.charAt(i_)));
    }

    if (data_.empty()) {
        _messageCallback(nullptr, 0);
    }
    else {
        _messageCallback(data_.data(), data_.size());
    }
}