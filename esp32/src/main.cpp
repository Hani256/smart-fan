#include "page.h"         // PAGE, the web page
#include "secrets.h"      // WIFI_SSID and WIFI_PASS, not in git
#include "status_parse.h" // checks a status line, in lib/status_parse
#include <Arduino.h>
#include <ArduinoOTA.h>   // upload new firmware over WiFi
#include <ESPmDNS.h>      // so the phone can find it as smartfan.local
#include <PubSubClient.h> // MQTT client
#include <WebServer.h>    // serve the page
#include <WiFi.h>         // connect to the home network

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
  char mode = 'A'; // A, M or O, as the STM32 reports it
};
static Status st;

static WebServer server(80);

// ---- MQTT ------------------------------------------------------------------
// The ESP32 also publishes everything to an MQTT broker, and takes mode
// changes from it. Topics:
//   smartfan/state      out, retained: the latest values as JSON
//   smartfan/status     out, retained: "online", or "offline" from the Last
//   Will smartfan/mode/set   in: "A", "O" or "M,<0-100>"
static WiFiClient mqtt_net; // the TCP connection under MQTT
static PubSubClient mqtt(mqtt_net);
static uint32_t mqtt_try_ms = 0; // when we last tried to reach the broker

// Adds the checksum and sends one command line to the STM32.
static void send_to_stm32(const char *cmd) {
  uint8_t x = 0;
  for (const char *p = cmd + 1; *p; p++) // everything after the '$'
    x ^= (uint8_t)*p;
  Serial2.printf("%s*%02X\r\n", cmd, x);
}

// The latest values, as JSON on smartfan/state. Retained, so anyone who
// subscribes later gets the last state straight away.
static void mqtt_publish_state() {
  if (!mqtt.connected())
    return;
  char json[128];
  if (st.fresh)
    snprintf(json, sizeof(json),
             "{\"fresh\":true,\"t\":%.1f,\"h\":%.1f,\"duty\":%u,\"rpm\":%lu,"
             "\"mode\":\"%c\"}",
             st.temp_c, st.hum_pct, st.duty, st.rpm, st.mode);
  else
    snprintf(json, sizeof(json),
             "{\"fresh\":false,\"t\":null,\"h\":null,\"duty\":%u,\"rpm\":%lu,"
             "\"mode\":\"%c\"}",
             st.duty, st.rpm, st.mode);
  mqtt.publish("smartfan/state", json, true);
}

// A message on smartfan/mode/set. Anything that is not exactly "A", "O" or
// "M,<0-100>" is ignored, the same as a damaged line from the STM32.
static void on_mqtt(char *topic, uint8_t *payload, unsigned int n) {
  (void)topic; // we only subscribe to one topic
  char msg[8];
  if (n == 0 || n >= sizeof(msg))
    return;
  memcpy(msg, payload, n);
  msg[n] = '\0';

  char cmd[16];
  if (strcmp(msg, "A") == 0) {
    strcpy(cmd, "$M,A");
  } else if (strcmp(msg, "O") == 0) {
    strcpy(cmd, "$M,O");
  } else if (n >= 3 && strncmp(msg, "M,", 2) == 0 &&
             strspn(msg + 2, "0123456789") == n - 2 && atoi(msg + 2) <= 100) {
    snprintf(cmd, sizeof(cmd), "$M,M,%d", atoi(msg + 2));
  } else {
    Serial.printf("mqtt: ignored \"%s\"\n", msg);
    return;
  }
  send_to_stm32(cmd);
}

// Keeps the MQTT connection up. Called from loop() once WiFi is online.
static void mqtt_poll() {
  if (mqtt.connected()) {
    mqtt.loop(); // receive messages, answer the broker's keep-alive
    return;
  }
  if (millis() - mqtt_try_ms < 10000)
    return; // the broker is not there: try again every 10 s, not all the time
  mqtt_try_ms = millis();

  // Last Will: if the ESP32 vanishes without saying goodbye (switched off),
  // the broker itself publishes "offline" on smartfan/status.
  if (mqtt.connect("smartfan", MQTT_USER, MQTT_PASS, "smartfan/status", 1, true,
                   "offline")) {
    mqtt.publish("smartfan/status", "online", true);
    mqtt.subscribe("smartfan/mode/set");
    Serial.println("mqtt: connected");
  } else {
    Serial.printf("mqtt: broker not reachable (state %d)\n", mqtt.state());
  }
}
// ----------------------------------------------------------------------------

// One line from the STM32. status_parse checks the checksum, the exact layout
// and every range. Only a line that passes all of it changes what the page
// shows and counts as a sign of life. Anything else counts once as bad and
// changes nothing else.
static void handle_line(const char *s) {
  status_msg_t m;
  if (!status_parse(s, &m)) {
    bad++;
    Serial.printf("bad line: %s\n", s);
    return;
  }
  good++;
  last_line_ms = millis();

  st.fresh = m.fresh; // stale: the STM32 says its reading is old
  if (m.fresh) {
    st.temp_c = m.temp10 / 10.0f;
    st.hum_pct = m.hum10 / 10.0f;
  }
  st.duty = m.duty;
  st.rpm = m.rpm;
  st.mode = m.mode;
  mqtt_publish_state();
}

static void send_page() { server.send(200, "text/html", PAGE); }

static void send_status() {
  char json[192];
  snprintf(json, sizeof(json),
           "{\"fresh\":%s,\"t\":%.1f,\"h\":%.1f,\"duty\":%u,\"rpm\":%lu,"
           "\"mode\":\"%c\",\"age_ms\":%lu,\"ok\":%lu,\"bad\":%lu,\"mqtt\":%d}",
           st.fresh ? "true" : "false", st.temp_c, st.hum_pct, st.duty, st.rpm,
           st.mode, (unsigned long)(millis() - last_line_ms),
           (unsigned long)good, (unsigned long)bad, mqtt.state());
  server.send(200, "application/json", json);
}

// The page asks for a new mode with POST /mode?m=A, ?m=O or ?m=M&duty=60.
// Turn that into a command line for the STM32, with the same checksum.
static void set_mode() {
  String m = server.arg("m");
  char cmd[16];
  if (m == "A") {
    snprintf(cmd, sizeof(cmd), "$M,A");
  } else if (m == "O") {
    snprintf(cmd, sizeof(cmd), "$M,O");
  } else if (m == "M") {
    long duty = server.arg("duty").toInt();
    if (duty < 0 || duty > 100) {
      server.send(400, "text/plain", "duty must be 0 to 100");
      return;
    }
    snprintf(cmd, sizeof(cmd), "$M,M,%ld", duty);
  } else {
    server.send(400, "text/plain", "m must be A, M or O");
    return;
  }

  send_to_stm32(cmd);
  server.send(200, "text/plain", "ok");
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
      server.on("/mode", HTTP_POST, set_mode);
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
    mqtt.setServer(MQTT_HOST, 1883);
    mqtt.setCallback(on_mqtt);
    mqtt.setSocketTimeout(2); // do not hang for long if the broker is gone
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
    mqtt_poll();           // keep the broker connection up
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