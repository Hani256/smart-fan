// Tests for App/link_proto.c: the lines that go between the STM32 and the
// ESP32, and above all that a damaged line is never believed.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "link_proto.h"
#include "unity.h"

static fan_mode_t mode;
static uint8_t duty;

void setUp(void) {
  mode = (fan_mode_t)99; // values no command would set, so we notice
  duty = 255;            // if a rejected line changed them anyway
}
void tearDown(void) {}

static int parse(const char *s) { return link_parse_command(s, &mode, &duty); }

// Build "$<payload>*hh" with the checksum the payload really has, so a test
// that expects rejection is testing the syntax check, not the checksum.
static const char *signed_line(const char *payload) {
  static char line[64];
  snprintf(line, sizeof(line), "$%s*%02X", payload,
           link_checksum(payload, (int)strlen(payload)));
  return line;
}

// A rejected line must leave both outputs exactly as they were.
static void assert_rejected(const char *payload) {
  mode = (fan_mode_t)99;
  duty = 255;
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, parse(signed_line(payload)), payload);
  TEST_ASSERT_EQUAL_INT_MESSAGE(99, mode, payload);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(255, duty, payload);
}

void test_checksum_matches_a_value_worked_out_by_hand(void) {
  const char *body = "S,234,452,40,1500";
  TEST_ASSERT_EQUAL_HEX8(0x55, link_checksum(body, (int)strlen(body)));
}

void test_status_line_with_a_fresh_reading(void) {
  char buf[48];
  int n = link_format_status(buf, sizeof(buf), 1, 23.4f, 45.2f, 40, 1500,
                             MODE_AUTO);
  TEST_ASSERT_EQUAL_STRING("$S,234,452,40,1500,A*38\r\n", buf);
  TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);
}

void test_status_line_leaves_a_stale_reading_out(void) {
  char buf[48];
  link_format_status(buf, sizeof(buf), 0, 23.4f, 45.2f, 50, 1500, MODE_OFF);
  TEST_ASSERT_EQUAL_STRING("$S,,,50,1500,O*31\r\n", buf);
}

void test_status_line_handles_temperatures_below_zero(void) {
  char buf[48];
  link_format_status(buf, sizeof(buf), 1, -5.0f, 80.0f, 0, 0, MODE_MANUAL);
  TEST_ASSERT_EQUAL_STRING("$S,-50,800,0,0,M*22\r\n", buf);
}

// The fresh line used in the size tests: 25 characters, plus '\0' = 26.
static const char *FULL = "$S,234,452,40,1500,A*38\r\n";
static int format_into(char *buf, size_t size) {
  return link_format_status(buf, size, 1, 23.4f, 45.2f, 40, 1500, MODE_AUTO);
}

void test_status_line_fits_exactly_with_room_for_the_terminator(void) {
  // A heap buffer of exactly this size, so AddressSanitizer catches a write
  // even one byte past the end.
  char *buf = malloc(26);
  TEST_ASSERT_EQUAL_INT(25, format_into(buf, 26));
  TEST_ASSERT_EQUAL_STRING(FULL, buf);
  free(buf);
}

void test_status_line_one_byte_short_fails_cleanly(void) {
  char *buf = malloc(25); // no room for the '\0'
  TEST_ASSERT_EQUAL_INT(-1, format_into(buf, 25));
  TEST_ASSERT_EQUAL_STRING("", buf);
  free(buf);
}

void test_status_line_in_tiny_buffers_fails_cleanly(void) {
  for (size_t size = 1; size < 25; size++) {
    char *buf = malloc(size);
    TEST_ASSERT_EQUAL_INT(-1, format_into(buf, size));
    TEST_ASSERT_EQUAL_STRING("", buf);
    free(buf);
  }
}

void test_status_line_with_no_buffer_at_all(void) {
  char sentinel = 'x';
  TEST_ASSERT_EQUAL_INT(-1, format_into(&sentinel, 0));
  TEST_ASSERT_EQUAL_CHAR('x', sentinel); // size 0: nothing written
  TEST_ASSERT_EQUAL_INT(-1, format_into(NULL, 0));
}

void test_the_longest_possible_line_fits_the_documented_size(void) {
  char buf[LINK_STATUS_MAX_LEN];
  int n = link_format_status(buf, sizeof(buf), 1, -214748364.0f, -214748364.0f,
                             100, 4294967295u, MODE_MANUAL);
  TEST_ASSERT_GREATER_THAN_INT(0, n);
  TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);
}

void test_reads_auto(void) {
  TEST_ASSERT_EQUAL_INT(1, parse("$M,A*20"));
  TEST_ASSERT_EQUAL(MODE_AUTO, mode);
}

void test_reads_off(void) {
  TEST_ASSERT_EQUAL_INT(1, parse("$M,O*2E"));
  TEST_ASSERT_EQUAL(MODE_OFF, mode);
}

void test_reads_manual_with_its_speed(void) {
  TEST_ASSERT_EQUAL_INT(1, parse("$M,M,60*06"));
  TEST_ASSERT_EQUAL(MODE_MANUAL, mode);
  TEST_ASSERT_EQUAL_UINT8(60, duty);
}

void test_rejects_a_wrong_checksum(void) {
  TEST_ASSERT_EQUAL_INT(0, parse("$M,A*21"));
  TEST_ASSERT_EQUAL(99, mode); // and changes nothing
}

void test_rejects_a_speed_above_100(void) {
  TEST_ASSERT_EQUAL_INT(0, parse("$M,M,101*30"));
}

void test_rejects_commands_it_does_not_know(void) {
  TEST_ASSERT_EQUAL_INT(0, parse("$M,X*39"));
}

void test_rejects_lines_that_are_not_commands_at_all(void) {
  TEST_ASSERT_EQUAL_INT(0, parse(""));
  TEST_ASSERT_EQUAL_INT(0, parse("$M,A"));    // no checksum
  TEST_ASSERT_EQUAL_INT(0, parse("M,A*20"));  // no '$'
  TEST_ASSERT_EQUAL_INT(0, parse("$M,A*2"));  // checksum cut short
  TEST_ASSERT_EQUAL_INT(0, parse("garbage")); // noise on the line
}

void test_any_single_damaged_character_is_caught(void) {
  // Change each character of a valid command, one at a time, to every other
  // printable character. Not one of those damaged lines may be accepted.
  const char *good = "$M,M,60*06";
  char line[16];
  for (size_t i = 0; i < strlen(good); i++) {
    for (char ch = ' '; ch <= '~'; ch++) {
      if (ch == good[i])
        continue;
      strcpy(line, good);
      line[i] = ch;
      TEST_ASSERT_EQUAL_INT_MESSAGE(0, parse(line), line);
    }
  }
}

void test_rejects_manual_speeds_with_anything_but_digits(void) {
  assert_rejected("M,M,60junk"); // sscanf used to stop at the junk and accept
  assert_rejected("M,M,+60");
  assert_rejected("M,M,-1");
  assert_rejected("M,M, 60");
  assert_rejected("M,M,60 ");
  assert_rejected("M,M,6 0");
  assert_rejected("M,M,0x10");
}

void test_rejects_missing_and_extra_fields(void) {
  assert_rejected("M,M,");
  assert_rejected("M,M");
  assert_rejected("M,M,60,1");
  assert_rejected("M,A,1");
  assert_rejected("M,A ");
  assert_rejected("M,AX");
  assert_rejected("M,O,");
  assert_rejected("M,");
  assert_rejected("M");
}

void test_rejects_manual_speeds_out_of_range_or_too_long(void) {
  assert_rejected("M,M,101");
  assert_rejected("M,M,999");
  assert_rejected("M,M,1000");
  assert_rejected("M,M,0100");       // four digits, even though it is 100
  assert_rejected("M,M,4294967356"); // wraps to 60 in a 32 bit unsigned
}

void test_accepts_the_whole_manual_range(void) {
  TEST_ASSERT_EQUAL_INT(1, parse(signed_line("M,M,0")));
  TEST_ASSERT_EQUAL_UINT8(0, duty);
  TEST_ASSERT_EQUAL_INT(1, parse(signed_line("M,M,100")));
  TEST_ASSERT_EQUAL_UINT8(100, duty);
  TEST_ASSERT_EQUAL_INT(1, parse(signed_line("M,M,7")));
  TEST_ASSERT_EQUAL_UINT8(7, duty);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_checksum_matches_a_value_worked_out_by_hand);
  RUN_TEST(test_status_line_with_a_fresh_reading);
  RUN_TEST(test_status_line_leaves_a_stale_reading_out);
  RUN_TEST(test_status_line_handles_temperatures_below_zero);
  RUN_TEST(test_status_line_fits_exactly_with_room_for_the_terminator);
  RUN_TEST(test_status_line_one_byte_short_fails_cleanly);
  RUN_TEST(test_status_line_in_tiny_buffers_fails_cleanly);
  RUN_TEST(test_status_line_with_no_buffer_at_all);
  RUN_TEST(test_the_longest_possible_line_fits_the_documented_size);
  RUN_TEST(test_reads_auto);
  RUN_TEST(test_reads_off);
  RUN_TEST(test_reads_manual_with_its_speed);
  RUN_TEST(test_rejects_a_wrong_checksum);
  RUN_TEST(test_rejects_a_speed_above_100);
  RUN_TEST(test_rejects_commands_it_does_not_know);
  RUN_TEST(test_rejects_lines_that_are_not_commands_at_all);
  RUN_TEST(test_any_single_damaged_character_is_caught);
  RUN_TEST(test_rejects_manual_speeds_with_anything_but_digits);
  RUN_TEST(test_rejects_missing_and_extra_fields);
  RUN_TEST(test_rejects_manual_speeds_out_of_range_or_too_long);
  RUN_TEST(test_accepts_the_whole_manual_range);
  return UNITY_END();
}
