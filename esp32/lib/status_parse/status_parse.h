#ifndef STATUS_PARSE_H
#define STATUS_PARSE_H

// Reading the STM32's status line on the ESP32.
//
// Plain C with no Arduino dependency, so the same file is built into the
// ESP32 firmware and into the unit tests on a laptop.
//
// A valid line is exactly one of
//
//   $S,<temp>,<hum>,<duty>,<rpm>,<mode>*hh   fresh reading
//   $S,,,<duty>,<rpm>,<mode>*hh              stale reading
//
// temp    tenths of a degree, -199 to 799, an optional '-' then digits. The
//         STM32 only keeps readings above -20 C and below 80 C.
// hum     tenths of a percent, 0 to 1000
// duty    percent, 0 to 100
// rpm     0 to 4294967295
// mode    A, M or O
// hh      exactly two hex digits: the XOR of everything between '$' and '*'
//
// Numbers are decimal digits only: no '+', no spaces, no empty fields
// except the two of a stale line, nothing after the checksum.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int fresh;      // 1: temp10 and hum10 are valid
  int16_t temp10; // tenths of a degree C
  uint16_t hum10; // tenths of a percent
  uint8_t duty;   // percent
  uint32_t rpm;
  char mode; // 'A', 'M' or 'O'
} status_msg_t;

// Returns 1 and fills in *out if the whole line is valid. Returns 0 and
// leaves *out untouched otherwise.
int status_parse(const char *line, status_msg_t *out);

#ifdef __cplusplus
}
#endif

#endif
