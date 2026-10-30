// Tests for esp32/lib/status_parse: how the ESP32 reads the STM32's status
// line. Every malformed line here carries its real checksum, so a rejection
// means the syntax or range check caught it, not the checksum.

#include <stdio.h>
#include <string.h>

#include "link_proto.h" // the STM32 side, for the round trip test
#include "status_parse.h"
#include "unity.h"

static status_msg_t out;
static const status_msg_t SENTINEL = {7, 1234, 567, 89, 4321, 'Z'};

void setUp(void) { out = SENTINEL; }
void tearDown(void) {}

static int parse(const char *s) { return status_parse(s, &out); }

static const char *signed_line(const char *payload) {
  static char line[96];
  snprintf(line, sizeof(line), "$%s*%02X", payload,
           link_checksum(payload, (int)strlen(payload)));
  return line;
}

// Rejected, and *out exactly as it was before.
static void assert_rejected(const char *line) {
  out = SENTINEL;
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, parse(line), line);
  TEST_ASSERT_EQUAL_INT_MESSAGE(SENTINEL.fresh, out.fresh, line);
  TEST_ASSERT_EQUAL_INT16_MESSAGE(SENTINEL.temp10, out.temp10, line);
  TEST_ASSERT_EQUAL_UINT16_MESSAGE(SENTINEL.hum10, out.hum10, line);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(SENTINEL.duty, out.duty, line);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(SENTINEL.rpm, out.rpm, line);
  TEST_ASSERT_EQUAL_CHAR_MESSAGE(SENTINEL.mode, out.mode, line);
}
static void assert_payload_rejected(const char *payload) {
  assert_rejected(signed_line(payload));
}

void test_reads_a_fresh_line(void) {
  TEST_ASSERT_EQUAL_INT(1, parse("$S,234,452,40,1500,A*38"));
  TEST_ASSERT_EQUAL_INT(1, out.fresh);
  TEST_ASSERT_EQUAL_INT16(234, out.temp10);
  TEST_ASSERT_EQUAL_UINT16(452, out.hum10);
  TEST_ASSERT_EQUAL_UINT8(40, out.duty);
  TEST_ASSERT_EQUAL_UINT32(1500, out.rpm);
  TEST_ASSERT_EQUAL_CHAR('A', out.mode);
}

void test_reads_a_stale_line(void) {
  TEST_ASSERT_EQUAL_INT(1, parse("$S,,,50,1500,O*31"));
  TEST_ASSERT_EQUAL_INT(0, out.fresh);
  TEST_ASSERT_EQUAL_UINT8(50, out.duty);
  TEST_ASSERT_EQUAL_UINT32(1500, out.rpm);
  TEST_ASSERT_EQUAL_CHAR('O', out.mode);
}

void test_reads_a_temperature_below_zero(void) {
  TEST_ASSERT_EQUAL_INT(1, parse("$S,-50,800,0,0,M*22"));
  TEST_ASSERT_EQUAL_INT16(-50, out.temp10);
}

void test_reads_the_edges_of_every_range(void) {
  TEST_ASSERT_EQUAL_INT(1, parse(signed_line("S,-199,0,0,0,A")));
  TEST_ASSERT_EQUAL_INT(1, parse(signed_line("S,799,1000,100,4294967295,M")));
  TEST_ASSERT_EQUAL_UINT32(4294967295u, out.rpm);
  TEST_ASSERT_EQUAL_INT(1, parse(signed_line("S,-0,500,50,1,O")));
  TEST_ASSERT_EQUAL_INT16(0, out.temp10);
}

void test_reads_whatever_the_stm32_formats(void) {
  // Lines made by the STM32's own formatter, including the one it sends at
  // startup before the sensor has answered (a zeroed sample, not fresh).
  struct {
    uint8_t fresh;
    float t, h;
    uint8_t duty;
    uint32_t rpm;
    fan_mode_t mode;
  } cases[] = {
      {0, 0.0f, 0.0f, 0, 0, MODE_AUTO},       {1, 23.4f, 45.2f, 40, 1500, MODE_AUTO},
      {1, -19.9f, 0.0f, 0, 0, MODE_OFF},      {1, 79.9f, 100.0f, 100, 3030, MODE_MANUAL},
      {0, 23.4f, 45.2f, 50, 1890, MODE_OFF},
  };
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    char buf[LINK_STATUS_MAX_LEN];
    int n = link_format_status(buf, sizeof(buf), cases[i].fresh, cases[i].t,
                               cases[i].h, cases[i].duty, cases[i].rpm,
                               cases[i].mode);
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    buf[n - 2] = '\0'; // the ESP32 strips the "\r\n" before parsing
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, parse(buf), buf);
    TEST_ASSERT_EQUAL_INT(cases[i].fresh, out.fresh);
    TEST_ASSERT_EQUAL_UINT8(cases[i].duty, out.duty);
    TEST_ASSERT_EQUAL_UINT32(cases[i].rpm, out.rpm);
    TEST_ASSERT_EQUAL_CHAR("AMO"[cases[i].mode], out.mode);
  }
}

void test_rejects_bad_checksums(void) {
  assert_rejected("$S,234,452,40,1500,A*39"); // wrong value
  assert_rejected("$S,234,452,40,1500,A* 8"); // space as a digit
  assert_rejected("$S,234,452,40,1500,A*3");  // one digit
  assert_rejected("$S,234,452,40,1500,A*388"); // three
  assert_rejected("$S,234,452,40,1500,A*38x"); // trailing text
  assert_rejected("$S,234,452,40,1500,A");     // none
  assert_rejected("S,234,452,40,1500,A*38");   // no '$'
  assert_rejected("");
}

void test_rejects_wrong_structure(void) {
  assert_payload_rejected("S,234,452,40,1500");     // mode missing
  assert_payload_rejected("S,234,452,40,1500,A,1"); // extra field
  assert_payload_rejected("S,234,452,40,1500,AX");  // mode too long
  assert_payload_rejected("S,,452,40,1500,A");      // half stale
  assert_payload_rejected("S,234,,40,1500,A");      // the other half
  assert_payload_rejected("S,234,452,,1500,A");     // empty duty
  assert_payload_rejected("S,234,452,40,,A");       // empty rpm
  assert_payload_rejected("S,234,452,40,1500,");    // empty mode
  assert_payload_rejected("M,234,452,40,1500,A");   // not a status line
  assert_payload_rejected("S234,452,40,1500,A");    // missing comma
}

void test_rejects_anything_but_digits_in_numbers(void) {
  assert_payload_rejected("S,+234,452,40,1500,A");
  assert_payload_rejected("S, 234,452,40,1500,A");
  assert_payload_rejected("S,234 ,452,40,1500,A");
  assert_payload_rejected("S,2.4,452,40,1500,A");
  assert_payload_rejected("S,234,-452,40,1500,A");
  assert_payload_rejected("S,234,452,40,15x0,A");
  assert_payload_rejected("S,-,452,40,1500,A");
  assert_payload_rejected("S,--5,452,40,1500,A");
}

void test_rejects_values_out_of_range(void) {
  assert_payload_rejected("S,800,452,40,1500,A");  // 80.0 C, the STM32 drops that
  assert_payload_rejected("S,-200,452,40,1500,A"); // -20.0 C, the same
  assert_payload_rejected("S,234,1001,40,1500,A"); // over 100 %
  assert_payload_rejected("S,234,452,101,1500,A"); // duty over 100
  assert_payload_rejected("S,234,452,40,1500,X");  // unknown mode
  assert_payload_rejected("S,234,452,40,1500,a");  // modes are upper case
}

void test_rejects_numbers_that_would_overflow(void) {
  assert_payload_rejected("S,234,452,40,4294967296,A"); // one past 32 bits
  assert_payload_rejected("S,234,452,40,99999999999999999999,A");
  assert_payload_rejected("S,4294967530,452,40,1500,A"); // wraps to 234
  assert_payload_rejected("S,234,452,296,1500,A");       // wraps to 40 in 8 bits
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_reads_a_fresh_line);
  RUN_TEST(test_reads_a_stale_line);
  RUN_TEST(test_reads_a_temperature_below_zero);
  RUN_TEST(test_reads_the_edges_of_every_range);
  RUN_TEST(test_reads_whatever_the_stm32_formats);
  RUN_TEST(test_rejects_bad_checksums);
  RUN_TEST(test_rejects_wrong_structure);
  RUN_TEST(test_rejects_anything_but_digits_in_numbers);
  RUN_TEST(test_rejects_values_out_of_range);
  RUN_TEST(test_rejects_numbers_that_would_overflow);
  return UNITY_END();
}
