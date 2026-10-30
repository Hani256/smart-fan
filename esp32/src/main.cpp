#include "page.h"    // PAGE, the web page
#include "secrets.h" // WIFI_SSID and WIFI_PASS, not in git
#include <Arduino.h>
#include <ArduinoOTA.h> // upload new firmware over WiFi
#include <ESPmDNS.h>    // so the phone can find it as smartfan.local
#include <WebServer.h>  // serve the page
#include <WiFi.h>       // connect to the home network

// Wiring: ESP32 GPIO16 (RX) <- STM32 PA2 (TX)
//         ESP32 GPIO4  (TX) -> STM32 PA3 (RX)
static const int LINK_RX_PIN = 16;
static const int LINK_TX_PIN = 4;

static char line[64];  // the line being received
static size_t len = 0; // how many characters it has so far

static uint32_t good = 0, bad = 0; // how many lines passed and failed
static uint32_t last_line_ms = 0;  // when the last good line arrived
static uint32_t last_warn_ms = 0;  // when we last complained about silence

// The newest values from the STM32, for the web page.
struct Status {
  bool fresh = false; // temperature and humidity are valid
  float temp_c = 0, hum_pct = 0;
  unsigned duty = 0;
  unsigned long rpm = 0;
};
static Status st;

static WebServer server(80);

// Same checksum as the STM32: XOR of everything between '$' and '*'.
static bool checksum_ok(const char *s) {
  if (s[0] != '$')
    return false;
  const char *star = strchr(s, '*');
  if (star == nullptr || strlen(star) < 3)
    return false;

  uint8_t x = 0;
  for (const char *p = s + 1; p < star; p++)
    x ^= (uint8_t)*p;

  unsigned sent;
  if (sscanf(star + 1, "%2X", &sent) != 1)
    return false;
  return sent == x;
}

static void handle_line(const char *s) {
  if (!checksum_ok(s)) {
    bad++;
    Serial.printf("bad line: %s\n", s);
    return;
  }
  good++;
  last_line_ms = millis();

  int t10, h10;
  unsigned duty;
  unsigned long rpm;

  if (sscanf(s, "$S,%d,%d,%u,%lu", &t10, &h10, &duty, &rpm) == 4) {
    st.fresh = true;
    st.temp_c = t10 / 10.0f;
    st.hum_pct = h10 / 10.0f;
    st.duty = duty;
    st.rpm = rpm;
  } else if (sscanf(s, "$S,,,%u,%lu", &duty, &rpm) == 2) {
    st.fresh = false; // the STM32 says its reading is stale
    st.duty = duty;
    st.rpm = rpm;
  } else {
    Serial.printf("did not understand: %s\n", s);
  }
}

static void send_page() { server.send(200, "text/html", PAGE); }

static void send_status() {
  char json[160];
  snprintf(json, sizeof(json),
           "{\"fresh\":%s,\"t\":%.1f,\"h\":%.1f,\"duty\":%u,\"rpm\":%lu,"
           "\"age_ms\":%lu,\"ok\":%lu,\"bad\":%lu}",
           st.fresh ? "true" : "false", st.temp_c, st.hum_pct, st.duty, st.rpm,
           (unsigned long)(millis() - last_line_ms), (unsigned long)good,
           (unsigned long)bad);
  server.send(200, "application/json", json);
}

static bool online = false;      // web server and OTA are running
static uint32_t last_try_ms = 0; // when we last asked the router

// Ask the router to let us in. Drops any half-finished attempt first.
static void wifi_begin() {
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  last_try_ms = millis();
}

// Called from loop(). Once connected, starts the page and OTA (only once).
// Until then, tries again every 10 s, because a router may refuse the first
// attempt right after the ESP32 was reset.
static void wifi_poll() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!online) {
      server.on("/", send_page);
      server.on("/status.json", send_status);
      server.begin();
      ArduinoOTA.setHostname("smartfan"); // also makes smartfan.local work
      ArduinoOTA.setPassword(OTA_PASS);
      ArduinoOTA.begin();
      Serial.printf("connected: http://%s/  (OTA ready)\n",
                    WiFi.localIP().toString().c_str());
      online = true;
    }
    return;
  }
  if (millis() - last_try_ms > 10000) {
    Serial.println("no WiFi yet, trying again");
    wifi_begin();
  }
}

void setup() {
  Serial.begin(115200); // USB, to the Mac
  Serial2.begin(115200, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN); // to the STM32
  Serial.println("listening for the STM32");
  last_line_ms = millis();
  WiFi.setHostname("smartfan");
  WiFi.mode(WIFI_STA);
  wifi_begin(); // does not wait; wifi_poll() in loop() takes it from here
}

void loop() {
  wifi_poll();
  if (online) {
    ArduinoOTA.handle();   // a new firmware arriving over WiFi?
    server.handleClient(); // answer the phone, if it asked for something
  }

  while (Serial2.available()) {
    char c = (char)Serial2.read();
    if (c == '\n') { // end of a line
      if (len > 0 && line[len - 1] == '\r')
        len--;          // the STM32 ends lines with \r\n, drop the \r
      line[len] = '\0'; // close the string
      if (len > 0)
        handle_line(line);
      len = 0; // start the next line
    } else if (len < sizeof(line) - 1) {
      line[len++] = c; // one more character
    } else {
      len = 0; // far too long to be ours, throw it away
    }
  }

  if (millis() - last_line_ms > 3000 && millis() - last_warn_ms > 3000) {
    Serial.println("nothing from the STM32 for 3 s, check the wiring");
    last_warn_ms = millis();
  }
}