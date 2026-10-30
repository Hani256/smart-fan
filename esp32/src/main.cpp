#include "secrets.h" // WIFI_SSID and WIFI_PASS, not in git
#include <Arduino.h>
#include <ESPmDNS.h>   // so the phone can find it as smartfan.local
#include <WebServer.h> // serve the page
#include <WiFi.h>      // connect to the home network

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

// The page itself. It asks the ESP32 for /status.json once a second and
// puts the numbers on the screen.
static const char PAGE[] = R"html(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart fan</title>
<style>
body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:24px}
.l{color:#888;margin-top:20px}.v{font-size:44px;font-weight:600}
#link{margin-top:28px;color:#888}
</style></head><body>
<div class="l">Temperature</div><div class="v" id="t">--</div>
<div class="l">Humidity</div><div class="v" id="h">--</div>
<div class="l">Fan</div><div class="v" id="f">--</div>
<div id="link">waiting...</div>
<script>
const $ = id => document.getElementById(id);
async function tick() {
  try {
    const s = await (await fetch('/status.json')).json();
    $('t').textContent = s.fresh ? s.t.toFixed(1) + ' °C' : '--';
    $('h').textContent = s.fresh ? s.h.toFixed(1) + ' %' : '--';
    $('f').textContent = s.duty + ' %  ' + s.rpm + ' rpm';
    $('link').textContent = (s.ok > 0 && s.age_ms < 3000)
        ? 'STM32 link ok' : 'no data from the STM32';
  } catch (e) {
    $('link').textContent = 'ESP32 not reachable';
  }
}
setInterval(tick, 1000);
tick();
</script></body></html>)html";

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

// Join the home network, then start the web server. Gives up after about
// 10 s so the serial link keeps working even without WiFi.
static void wifi_start() {
  WiFi.setHostname("smartfan");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("connecting to WiFi");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(250);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nno WiFi. Check secrets.h, then press EN to try again");
    return;
  }
  Serial.printf("\nconnected: http://%s/\n", WiFi.localIP().toString().c_str());

  if (MDNS.begin("smartfan"))
    Serial.println("also at: http://smartfan.local/");

  server.on("/", send_page);
  server.on("/status.json", send_status);
  server.begin();
}

void setup() {
  Serial.begin(115200); // USB, to the Mac
  Serial2.begin(115200, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN); // to the STM32
  Serial.println("listening for the STM32");
  last_line_ms = millis();
  wifi_start();
}

void loop() {
  server.handleClient(); // answer the phone, if it asked for something

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