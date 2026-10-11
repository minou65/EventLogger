# EventLogger

A lightweight event logger for Arduino projects. `EventLogger` stores recent events in a fixed-size ring buffer, writes them to `Serial`, and can notify a callback whenever a new event is logged.

## Features

- Fixed-size ring buffer with configurable capacity
- Events returned in chronological order, oldest first
- Millisecond timestamps based on `millis()`
- Optional real-time timestamps from the system clock
- `printf`-style formatted logging
- Optional live callback for WebSerial or other integrations
- Optional ESP32 web console with event history, live updates, and incoming commands
- Optional WebSerial-compatible wrapper for existing sketches
- Global `eventLogger` instance included by the library

## Installation

### Arduino IDE

1. Download or clone this repository into your Arduino libraries directory.
2. Restart the Arduino IDE.
3. Include the library in your sketch:

```cpp
#include <EventLogger.h>
```

### PlatformIO

Copy the repository into `lib/EventLogger` or add it as a library dependency in `platformio.ini`.

## Quick start

```cpp
#include <Arduino.h>
#include <EventLogger.h>

void setup() {
    Serial.begin(115200);

    eventLogger.log("Device started");
    eventLogger.logf("Sensor value: %d", 42);

    for (const EventLogger::Event& event : eventLogger.getEvents()) {
        Serial.println(eventLogger.formatEvent(event));
    }
}

void loop() {
}
```

Every call to `log()` or `logf()` is written to `Serial` and added to the buffer. When the buffer is full, the oldest event is overwritten.

## ESP32 web console

The optional web console is available on ESP32 when the `ESPAsyncWebServer` library is installed and its header is available. On other platforms, or when the dependency is unavailable, `beginWeb()` returns `false` and web output is disabled. The library detects the dependency automatically; no feature macro is normally needed.

Register the routes on your async web server before starting it:

```cpp
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <EventLogger.h>

AsyncWebServer server(80);

void handleCommand(const String& command) {
    if (command == "help") {
        eventLogger.webPrintln("Available command: help");
    }
}

void setup() {
    Serial.begin(115200);
    // Connect to Wi-Fi before starting the server.

    eventLogger.setCommandCallback(handleCommand);
    if (!eventLogger.beginWeb(&server)) {
        Serial.println("EventLogger web console is unavailable");
    }
    server.begin();

    eventLogger.log("Device started");
}

void loop() {
    eventLogger.loop(); // Reserved for compatibility; currently does nothing.
}
```

By default, the console is served at `/webserial`. The `basePath` argument can change this path; for example, `beginWeb(&server, "/events")` serves the console at `/events`.

| Endpoint | Method | Behavior |
| --- | --- | --- |
| `/webserial` | GET | Browser console showing buffered history and live events |
| `/webserial/history` | GET | Buffered events as a JSON array of formatted strings, oldest first |
| `/webserial/stream` | GET | Server-sent event stream for new log entries and web output |
| `/webserial/command` | POST | Sends the request body (or a `cmd` parameter) to the command callback |

The command callback runs synchronously. Incoming commands are trimmed and also recorded as `WebCommand: ...`. A command request returns HTTP 200 when a callback is registered and invoked, otherwise HTTP 400. `webPrint()` and `webPrintln()` each send one text entry to connected browser clients; neither adds it to the event buffer. `webClientCount()` reports the number of connected stream clients.

## API

### Logging

```cpp
void log(const String& message);
void logf(const char* format, ...);
```

`logf()` uses the same formatting rules as `printf` and stores a maximum of 255 formatted characters per call.

### Reading and clearing events

```cpp
std::vector<EventLogger::Event> getEvents() const;
size_t count() const;
void clear();
```

`getEvents()` returns a copy of the stored events ordered from oldest to newest.

Each event contains:

```cpp
struct Event {
    unsigned long timestamp; // milliseconds since boot
    time_t realTime;         // Unix timestamp, when available
    String message;
};
```

### Live updates

```cpp
void setLiveCallback(std::function<void(const EventLogger::Event&)> callback);
void clearLiveCallback();
```

Example:

```cpp
void onEvent(const EventLogger::Event& event) {
    // Forward the event to WebSerial, MQTT, or another transport.
    Serial.println(eventLogger.formatEvent(event));
}

void setup() {
    Serial.begin(115200);
    eventLogger.setLiveCallback(onEvent);
}
```

The callback runs synchronously from `log()`, so keep it short and avoid blocking operations where possible.

### Web console and commands

```cpp
bool beginWeb(AsyncWebServer* server, const String& basePath = "/webserial");
void setCommandCallback(std::function<void(const String&)> callback);
void clearCommandCallback();
void webPrint(const String& message);
void webPrintln(const String& message);
void webPrintln();
size_t webClientCount() const;
void loop();
```

`beginWeb()` returns `false` if the server pointer is null or async web support is unavailable. `loop()` is currently a no-op, retained for API compatibility.

For sketches using the familiar WebSerial-style interface, include the compatibility header:

```cpp
#include <EventLoggerWebSerialCompat.h>

EventWebSerial.begin(&server);
EventWebSerial.onMessage([](uint8_t* data, size_t length) {
    String command;
    for (size_t i = 0; i < length; ++i) {
        command += static_cast<char>(data[i]);
    }
    EventWebSerial.println("Received: " + command);
});

EventWebSerial.println("Ready for commands");
EventWebSerial.printf("Connected clients: %d", EventWebSerial.getConnectionCount());
```

The compatibility wrapper forwards to the global `eventLogger` instance. Its API includes `begin()`, `onMessage()`, `print()`, `println()`, `printf()`, `getConnectionCount()`, and `loop()`. To use the name `WebSerial` instead of `EventWebSerial`, define `EVENTLOGGER_DEFINE_WEBSERIAL_ALIAS` before including this header. This alias can conflict with another library that defines `WebSerial`.

### Formatting

```cpp
String formatEvent(const EventLogger::Event& event) const;
```

If the system clock contains a valid time after 2020, the output uses local time in the format `YYYY-MM-DD HH:MM:SS`. Otherwise it uses elapsed time in the format `[HH:MM:SS]`.

## Configuration

Define `EVENT_BUFFER_SIZE` before including the header to change the default buffer capacity of 100 events:

```cpp
#define EVENT_BUFFER_SIZE 250
#include <EventLogger.h>
```

The buffer is statically allocated, so increasing its size increases RAM usage. The library also uses `std::vector` and `std::function`; make sure your target platform and toolchain provide the required C++ standard library support.

## Timestamp requirements

The logger always records `millis()`. It also calls `time()` and stores the returned Unix timestamp. To receive calendar timestamps, initialize the system clock through the facilities provided by your board or networking/time library. If the clock is not valid, formatted output automatically falls back to elapsed time since boot.

## Repository structure

```text
src/
  EventLogger.h
  EventLogger.cpp
    EventLoggerWebSerialCompat.h
    EventLoggerWebSerialCompat.cpp
library.properties
```

## Contributing

Please read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes. The project uses C++14 and keeps code comments and documentation in English.

## License

No license is currently specified for this repository.
