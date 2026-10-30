#include "link_proto.h"

#include <stdio.h>
#include <string.h>

uint8_t link_checksum(const char *p, int n) {
  uint8_t x = 0;
  while (n--)
    x ^= (uint8_t)*p++;
  return x;
}

int link_format_status(char *buf, size_t size, uint8_t fresh, float temp_c,
                       float hum_pct, uint8_t duty_pct, uint32_t rpm,
                       fan_mode_t mode) {
  int n;
  if (fresh)
    n = snprintf(buf, size, "$S,%d,%d,%u,%lu,%c", (int)(temp_c * 10),
                 (int)(hum_pct * 10), duty_pct, (unsigned long)rpm,
                 "AMO"[mode]);
  else
    n = snprintf(buf, size, "$S,,,%u,%lu,%c", duty_pct, (unsigned long)rpm,
                 "AMO"[mode]);

  // checksum of everything after '$', then end the line
  n += snprintf(buf + n, size - n, "*%02X\r\n", link_checksum(buf + 1, n - 1));
  return n;
}

// Value of one hex digit, or -1 if it is not one.
static int hex_value(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

// The manual speed: one to three decimal digits and nothing else, worth at
// most 100. No sign, no spaces. Limiting the length means the value can never
// overflow while it is being added up.
static int parse_duty(const char *p, const char *end, uint8_t *duty_pct) {
  if (p >= end || end - p > 3)
    return 0;
  unsigned v = 0;
  for (; p < end; p++) {
    if (*p < '0' || *p > '9')
      return 0;
    v = v * 10 + (unsigned)(*p - '0');
  }
  if (v > 100)
    return 0;
  *duty_pct = (uint8_t)v;
  return 1;
}

int link_parse_command(const char *s, fan_mode_t *mode, uint8_t *duty_pct) {
  // The checksum must be exactly two hex digits and nothing after them.
  const char *star = strchr(s, '*');
  if (s[0] != '$' || star == NULL || strlen(star) != 3)
    return 0;
  int hi = hex_value(star[1]), lo = hex_value(star[2]);
  if (hi < 0 || lo < 0 ||
      link_checksum(s + 1, (int)(star - s - 1)) != (uint8_t)(hi * 16 + lo))
    return 0; // damaged

  // The payload, from '$' up to '*', must be one of the commands exactly.
  // Nothing is written to *mode or *duty_pct unless the whole line is valid.
  size_t len = (size_t)(star - s);
  if (len == 4 && strncmp(s, "$M,A", 4) == 0) {
    *mode = MODE_AUTO;
    return 1;
  }
  if (len == 4 && strncmp(s, "$M,O", 4) == 0) {
    *mode = MODE_OFF;
    return 1;
  }
  uint8_t duty;
  if (len > 5 && strncmp(s, "$M,M,", 5) == 0 && parse_duty(s + 5, star, &duty)) {
    *mode = MODE_MANUAL;
    *duty_pct = duty;
    return 1;
  }
  return 0; // not a command we know, or not exactly one
}
