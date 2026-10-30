#include "link_proto.h"

#include <ctype.h>
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

int link_parse_command(const char *s, fan_mode_t *mode, uint8_t *duty_pct) {
  const char *star = strchr(s, '*');
  // The checksum must be exactly two hex digits and nothing after them.
  // sscanf alone would also take " 6", because %x skips spaces.
  if (s[0] != '$' || star == NULL || strlen(star) != 3 ||
      !isxdigit((unsigned char)star[1]) || !isxdigit((unsigned char)star[2]))
    return 0;
  unsigned sent;
  if (sscanf(star + 1, "%2x", &sent) != 1 ||
      link_checksum(s + 1, (int)(star - s - 1)) != sent)
    return 0; // damaged

  unsigned duty;
  if (strncmp(s, "$M,A*", 5) == 0) {
    *mode = MODE_AUTO;
  } else if (strncmp(s, "$M,O*", 5) == 0) {
    *mode = MODE_OFF;
  } else if (sscanf(s, "$M,M,%u*", &duty) == 1 && duty <= 100) {
    *mode = MODE_MANUAL;
    *duty_pct = (uint8_t)duty;
  } else {
    return 0; // not a command we know
  }
  return 1;
}
