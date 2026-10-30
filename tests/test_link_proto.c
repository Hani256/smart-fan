// Tests for App/link_proto.c: the lines that go between the STM32 and the
// ESP32, and above all that a damaged line is never believed.

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

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_checksum_matches_a_value_worked_out_by_hand);
  RUN_TEST(test_status_line_with_a_fresh_reading);
  RUN_TEST(test_status_line_leaves_a_stale_reading_out);
  RUN_TEST(test_status_line_handles_temperatures_below_zero);
  RUN_TEST(test_reads_auto);
  RUN_TEST(test_reads_off);
  RUN_TEST(test_reads_manual_with_its_speed);
  RUN_TEST(test_rejects_a_wrong_checksum);
  RUN_TEST(test_rejects_a_speed_above_100);
  RUN_TEST(test_rejects_commands_it_does_not_know);
  RUN_TEST(test_rejects_lines_that_are_not_commands_at_all);
  RUN_TEST(test_any_single_damaged_character_is_caught);
  return UNITY_END();
}
