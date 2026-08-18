# EventLogger

A lightweight event logger for Arduino projects. `EventLogger` stores recent events in a fixed-size ring buffer, writes them to `Serial`, and can notify a callback whenever a new event is logged.

## Features

- Fixed-size ring buffer with configurable capacity
- Events returned in chronological order, oldest first
- Millisecond timestamps based on `millis()`
- Optional real-time timestamps from the system clock
- `printf`-style formatted logging
- Optional live callback for WebSerial or other integrations
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

For example, to store 250 events:

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
library.properties
```

## Contributing

Please read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes. The project uses C++14 and keeps code comments and documentation in English.

## License

No license is currently specified for this repository.
