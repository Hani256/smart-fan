#include <Arduino.h>

// Wiring: ESP32 GPIO16 (RX) <- STM32 PA2 (TX)
//         ESP32 GPIO4  (TX) -> STM32 PA3 (RX)
static const int LINK_RX_PIN = 16;
static const int LINK_TX_PIN = 4;

static char line[64];  // the line being received
static size_t len = 0; // how many characters it has so far

static uint32_t good = 0, bad = 0; // how many lines passed and failed
static uint32_t last_line_ms = 0;  // when the last good line arrived

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
    Serial.printf(
        "T %.1f C  H %.1f %%  fan %u %%  %lu rpm  (ok %lu, bad %lu)\n",
        t10 / 10.0, h10 / 10.0, duty, rpm, (unsigned long)good,
        (unsigned long)bad);
  } else if (sscanf(s, "$S,,,%u,%lu", &duty, &rpm) == 2) {
    Serial.printf(
        "T --.- C  H --.- %%  fan %u %%  %lu rpm  (ok %lu, bad %lu)\n", duty,
        rpm, (unsigned long)good, (unsigned long)bad);
  } else {
    Serial.printf("did not understand: %s\n", s);
  }
}

void setup() {
  Serial.begin(115200); // USB, to the Mac
  Serial2.begin(115200, SERIAL_8N1, LINK_RX_PIN, LINK_TX_PIN); // to the STM32
  Serial.println("listening for the STM32");
  last_line_ms = millis();
}

void loop() {
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
  if (millis() - last_line_ms > 3000) {
    Serial.println("nothing from the STM32 for 3 s, check the wiring");
    last_line_ms = millis();
  }
}