// EventLogger.cpp

#include "EventLogger.h"
#include <stdarg.h>
#include <time.h>

// Global instance
EventLogger eventLogger;

EventLogger::EventLogger()
    : _head(0)
    , _count(0)
    , _liveCallback(nullptr)
    , _commandCallback(nullptr)
#if EVENTLOGGER_HAS_ASYNC_WEB
    , _webServer(nullptr)
    , _eventSource(nullptr)
    , _webInitialized(false)
    , _webBasePath("")
    , _webHistoryPath("")
    , _webStreamPath("")
    , _webCommandPath("")
#endif
{
}

void EventLogger::log(const String& message) {
    unsigned long timestamp_ = millis();
    time_t now_;
    time(&now_);

    Event event_(timestamp_, now_, message);

    // First store in buffer
    _buffer[_head] = event_;
    _head = (_head + 1) % EVENT_BUFFER_SIZE;

    if (_count < EVENT_BUFFER_SIZE) {
        _count++;
    }

    // Optional serial output
    if (Serial) {
        Serial.print(formatTimestamp(timestamp_, now_));
        Serial.print(" ");
        Serial.println(message);
    }

    if (_liveCallback) {
        _liveCallback(event_);
    }

#if EVENTLOGGER_HAS_ASYNC_WEB
    if (_webInitialized) {
        broadcastWebLine(formatEvent(event_));
    }
#endif
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

        char timeBuffer_[20];
        strftime(timeBuffer_, sizeof(timeBuffer_), "%Y-%m-%d %H:%M:%S", &timeinfo_);

        char buffer_[24];
        snprintf(buffer_, sizeof(buffer_), "[%s]", timeBuffer_);
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

void EventLogger::setCommandCallback(std::function<void(const String&)> callback) {
    _commandCallback = callback;
}

void EventLogger::clearCommandCallback() {
    _commandCallback = nullptr;
}

bool EventLogger::processWebCommand(const String& command) {
    String cmd_ = command;
    cmd_.trim();

    if (cmd_.length() == 0) {
        return false;
    }

    logf("WebCommand: %s", cmd_.c_str());

    if (_commandCallback) {
        _commandCallback(cmd_);
        return true;
    }

    return false;
}

void EventLogger::webPrint(const String& message) {
#if EVENTLOGGER_HAS_ASYNC_WEB
    if (_webInitialized) {
        broadcastWebLine(message);
    }
#else
    (void)message;
#endif
}

void EventLogger::webPrintln(const String& message) {
    webPrint(message);
}

void EventLogger::webPrintln() {
    webPrint("");
}

size_t EventLogger::webClientCount() const {
#if EVENTLOGGER_HAS_ASYNC_WEB
    if (_eventSource != nullptr) {
        return _eventSource->count();
    }
#endif
    return 0;
}

void EventLogger::loop() {
    // Reserved for compatibility / future use
}

bool EventLogger::beginWeb(AsyncWebServer* server, const String& basePath) {
    if (server == nullptr) {
        return false;
    }

#if EVENTLOGGER_HAS_ASYNC_WEB
    if (_webInitialized) {
        return true;
    }

    _webServer = server;
    _webBasePath = (basePath.length() == 0) ? "/webserial" : basePath;
    if (!_webBasePath.startsWith("/")) {
        _webBasePath = "/" + _webBasePath;
    }

    _webHistoryPath = _webBasePath + "/history";
    _webStreamPath = _webBasePath + "/stream";
    _webCommandPath = _webBasePath + "/command";

    _eventSource = new AsyncEventSource(_webStreamPath.c_str());
    if (_eventSource == nullptr) {
        return false;
    }

    _eventSource->onConnect([this](AsyncEventSourceClient* client_) {
        (void)client_;
    });

    _webServer->addHandler(_eventSource);
    setupWebRoutes();

    _webInitialized = true;
    log("EventLogger Web initialized on " + _webBasePath);
    return true;
#else
    (void)server;
    (void)basePath;
    return false;
#endif
}

#if EVENTLOGGER_HAS_ASYNC_WEB
void EventLogger::setupWebRoutes() {
    // 1) Spezifische Endpunkte zuerst
    _webServer->on(_webHistoryPath.c_str(), HTTP_GET, [this](AsyncWebServerRequest* request) {
        AsyncResponseStream* response_ = request->beginResponseStream("application/json; charset=UTF-8");
        response_->print("[");

        auto events_ = getEvents();
        for (size_t i_ = 0; i_ < events_.size(); i_++) {
            if (i_ > 0) {
                response_->print(",");
            }

            response_->print("\"");
            response_->print(escapeJson(formatEvent(events_[i_])));
            response_->print("\"");
        }

        response_->print("]");
        request->send(response_);
    });

    _webServer->on(
        _webCommandPath.c_str(),
        HTTP_POST,
        [this](AsyncWebServerRequest* request) {
            if (request->contentLength() == 0) {
                String command_;
                if (request->hasParam("cmd", true)) {
                    command_ = request->getParam("cmd", true)->value();
                } else if (request->hasParam("cmd")) {
                    command_ = request->getParam("cmd")->value();
                }

                if (processWebCommand(command_)) {
                    request->send(200, "text/plain", "OK");
                } else {
                    request->send(400, "text/plain", "Missing command or no callback");
                }
            }
        },
        nullptr,
        [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
            String* body_ = reinterpret_cast<String*>(request->_tempObject);

            if (index == 0) {
                body_ = new String();
                request->_tempObject = body_;
            }

            if (body_ != nullptr) {
                for (size_t i_ = 0; i_ < len; i_++) {
                    *body_ += static_cast<char>(data[i_]);
                }
            }

            if ((index + len) == total) {
                bool ok_ = false;
                if (body_ != nullptr) {
                    ok_ = processWebCommand(*body_);
                    delete body_;
                    request->_tempObject = nullptr;
                }

                request->send(ok_ ? 200 : 400, "text/plain", ok_ ? "OK" : "Missing command or no callback");
            }
        }
    );

    // 2) UI-Root als letztes (wichtig)
    _webServer->on(_webBasePath.c_str(), HTTP_GET, [this](AsyncWebServerRequest* request) {
        static const char page_[] PROGMEM = R"HTML(
<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>EventLogger Console</title>
<style>
body {
  font-family: monospace;
  margin: 0;
  padding: 0;
  background: #f5f6f8;
  color: #1f2328;
}

#top {
  position: sticky;
  top: 0;
  z-index: 1000;
  padding: 10px;
  background: #ffffff;
  border-bottom: 1px solid #d0d7de;
  display: flex;
  gap: 8px;
}

#cmd {
  flex: 1;
  background: #ffffff;
  color: #1f2328;
  border: 1px solid #8c959f;
  padding: 8px;
}

button {
  background: #0969da;
  color: #ffffff;
  border: 0;
  padding: 8px 12px;
  cursor: pointer;
}

button:hover {
  background: #0550ae;
}

#log {
  white-space: pre-wrap;
  padding: 10px;
  margin: 0;
  line-height: 1.35;
}
</style>
</head>
<body>
<div id="top">
  <input id="cmd" type="text" placeholder="Befehl eingeben (z.B. help)">
  <button onclick="sendCmd()">Senden</button>
  <button onclick="clearLog()">Clear View</button>
</div>
<pre id="log"></pre>
<script>
const basePath = window.location.pathname.replace(/\/$/, '');
const log = document.getElementById('log');
const cmd = document.getElementById('cmd');
let streamWarned = false;
let autoFollow = true;

function isNearBottom() {
  const threshold = 24;
  return (window.innerHeight + window.scrollY) >= (document.body.scrollHeight - threshold);
}

window.addEventListener('scroll', () => {
  autoFollow = isNearBottom();
});

function addLine(line) {
  const wasNearBottom = isNearBottom();
  log.textContent += line + "\n";

  if (autoFollow || wasNearBottom) {
    window.scrollTo(0, document.body.scrollHeight);
    autoFollow = true;
  }
}

function clearLog() {
  log.textContent = "";
  autoFollow = true;
}

async function loadHistory() {
  const historyUrl = basePath + '/history';
  const r = await fetch(historyUrl, { cache: 'no-store' });

  if (!r.ok) {
    const t = await r.text();
    throw new Error('HTTP ' + r.status + ' @ ' + historyUrl + ' :: ' + t.substring(0, 120));
  }

  const text = await r.text();
  const arr = JSON.parse(text);

  for (const line of arr) {
    addLine(line);
  }
  addLine('=== Live Events ===');
}

async function sendCmd() {
  const value = cmd.value.trim();
  if (!value) return;

  const cmdUrl = basePath + '/command';
  const r = await fetch(cmdUrl, {
    method: 'POST',
    headers: { 'Content-Type': 'text/plain' },
    body: value
  });

  if (!r.ok) {
    const t = await r.text();
    addLine('[command failed] HTTP ' + r.status + ' :: ' + t.substring(0, 120));
  }

  cmd.value = '';
}

cmd.addEventListener('keydown', (e) => {
  if (e.key === 'Enter') {
    sendCmd();
  }
});

const streamUrl = basePath + '/stream';
const es = new EventSource(streamUrl);

es.onopen = () => {
  streamWarned = false;
};

es.addEventListener('log', (e) => addLine(e.data));

es.onerror = () => {
  if (!streamWarned) {
    addLine('[stream disconnected] ' + streamUrl);
    streamWarned = true;
  }
};

loadHistory().catch((e) => addLine('[history failed] ' + e.message));
</script>
</body>
</html>
)HTML";
        request->send(200, "text/html; charset=UTF-8", page_);
    });
}

void EventLogger::broadcastWebLine(const String& line) {
    if (_eventSource != nullptr) {
        _eventSource->send(line.c_str(), "log", millis());
    }
}

String EventLogger::buildHistoryJson() const {
    auto events_ = getEvents();
    String json_ = "[";
    for (size_t i_ = 0; i_ < events_.size(); i_++) {
        if (i_ > 0) {
            json_ += ",";
        }
        json_ += "\"";
        json_ += escapeJson(formatEvent(events_[i_]));
        json_ += "\"";
    }
    json_ += "]";
    return json_;
}

String EventLogger::escapeJson(const String& value) {
    String out_;
    out_.reserve(value.length() + 8);

    for (size_t i_ = 0; i_ < value.length(); i_++) {
        char c_ = value.charAt(i_);
        switch (c_) {
            case '\"': out_ += "\\\""; break;
            case '\\': out_ += "\\\\"; break;
            case '\b': out_ += "\\b"; break;
            case '\f': out_ += "\\f"; break;
            case '\n': out_ += "\\n"; break;
            case '\r': out_ += "\\r"; break;
            case '\t': out_ += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c_) < 0x20) {
                    out_ += ' ';
                } else {
                    out_ += c_;
                }
                break;
        }
    }

    return out_;
}
#endif