#ifndef _EVENTLOGGER_WEBSERIAL_COMPAT_h
#define _EVENTLOGGER_WEBSERIAL_COMPAT_h

#include "EventLogger.h"
#include <functional>

class EventLoggerWebSerialCompat {
public:
    typedef std::function<void(uint8_t*, size_t)> MessageCallback;

    bool begin(AsyncWebServer* server, const String& path = "/webserial");
    void onMessage(MessageCallback callback);

    void print(const String& message);
    void println(const String& message);
    void println();
    void printf(const char* format, ...);

    int getConnectionCount() const;
    void loop();

private:
    MessageCallback _messageCallback;

    void forwardCommand(const String& command);
};

// Globale kompatible Instanz
extern EventLoggerWebSerialCompat EventWebSerial;

// Optional: Alias für bestehenden Code
// Vor Include dieses Headers definieren:
// #define EVENTLOGGER_DEFINE_WEBSERIAL_ALIAS
#ifdef EVENTLOGGER_DEFINE_WEBSERIAL_ALIAS
#define WebSerial EventWebSerial
#endif

#endif