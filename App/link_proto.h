#ifndef LINK_PROTO_H
#define LINK_PROTO_H

// The text protocol between the STM32 and the ESP32.
//
// Like fan_control, this is only logic: it builds and reads lines of text and
// never touches the UART. The tasks in freertos.c do the sending and
// receiving.
//
// STM32 to ESP32, once a second:
//
//   $S,234,452,40,1500,A*hh   temperature and humidity in tenths, duty in %,
//                             speed in rpm, mode (A, M or O)
//   $S,,,50,1500,A*hh         the same, when the reading is stale
//
// ESP32 to STM32, when someone changes the mode on the page:
//
//   $M,A*hh       automatic
//   $M,O*hh       off
//   $M,M,60*hh    manual, at 60 %
//
// hh is an NMEA style checksum: the XOR of every character between '$' and
// '*', in hex. A line with a wrong checksum is thrown away, so a damaged byte
// can never turn into a wrong value.

#include <stddef.h>
#include <stdint.h>

#include "fan_control.h" // fan_mode_t

// XOR of n characters.
uint8_t link_checksum(const char *p, int n);

// Write the status line, with checksum and "\r\n", into buf. Returns its
// length. temp_c and hum_pct are left out when fresh is 0.
int link_format_status(char *buf, size_t size, uint8_t fresh, float temp_c,
                       float hum_pct, uint8_t duty_pct, uint32_t rpm,
                       fan_mode_t mode);

// Read one command line, without the "\r\n". Returns 1 and fills in *mode
// and *duty_pct if it is a valid command, 0 if it is damaged or unknown.
// The syntax is exact: manual speed is 1 to 3 decimal digits from 0 to 100,
// with no sign, spaces or anything else before the '*'. On 0, *mode and
// *duty_pct are left as they were.
int link_parse_command(const char *s, fan_mode_t *mode, uint8_t *duty_pct);

#endif
