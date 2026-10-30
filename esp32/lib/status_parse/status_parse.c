#include "status_parse.h"

#include <string.h>

static int hex_value(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

// Digits from *p up to the next ',' or '*', worth at most max. Moves *p to
// that separator. At least one digit, nothing else. The check against max
// happens on every digit, so the value can never overflow.
static int parse_uint(const char **p, uint32_t max, uint32_t *out) {
  const char *s = *p;
  uint64_t v = 0;
  if (*s < '0' || *s > '9')
    return 0;
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (uint64_t)(*s - '0');
    if (v > max)
      return 0;
    s++;
  }
  *p = s;
  *out = (uint32_t)v;
  return 1;
}

// Expect exactly the character c at *p, and step over it.
static int expect(const char **p, char c) {
  if (**p != c)
    return 0;
  (*p)++;
  return 1;
}

int status_parse(const char *line, status_msg_t *out) {
  // Checksum: one '*', followed by exactly two hex digits and nothing more.
  const char *star = strchr(line, '*');
  if (line[0] != '$' || star == NULL || strlen(star) != 3)
    return 0;
  int hi = hex_value(star[1]), lo = hex_value(star[2]);
  if (hi < 0 || lo < 0)
    return 0;
  uint8_t x = 0;
  for (const char *c = line + 1; c < star; c++)
    x ^= (uint8_t)*c;
  if (x != (uint8_t)(hi * 16 + lo))
    return 0;

  // Fill a local copy, and only hand it over once everything has passed.
  status_msg_t m = {0};
  const char *p = line + 1;
  uint32_t v;
  if (!expect(&p, 'S') || !expect(&p, ','))
    return 0;

  if (*p == ',') { // stale: temperature and humidity both empty
    m.fresh = 0;
    p++;
    if (!expect(&p, ','))
      return 0;
  } else {
    m.fresh = 1;
    int negative = (*p == '-');
    if (negative)
      p++;
    if (!parse_uint(&p, negative ? 199u : 799u, &v))
      return 0;
    m.temp10 = (int16_t)(negative ? -(int32_t)v : (int32_t)v);
    if (!expect(&p, ','))
      return 0;
    if (!parse_uint(&p, 1000u, &v))
      return 0;
    m.hum10 = (uint16_t)v;
    if (!expect(&p, ','))
      return 0;
  }

  if (!parse_uint(&p, 100u, &v))
    return 0;
  m.duty = (uint8_t)v;
  if (!expect(&p, ','))
    return 0;
  if (!parse_uint(&p, 4294967295u, &v))
    return 0;
  m.rpm = v;
  if (!expect(&p, ','))
    return 0;
  if (*p != 'A' && *p != 'M' && *p != 'O')
    return 0;
  m.mode = *p++;
  if (p != star) // anything between the mode and the '*' is an error
    return 0;

  *out = m;
  return 1;
}
