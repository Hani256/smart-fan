// Tests for App/fan_control.c: the curve, the hysteresis, the slew limit and
// the modes. Each test is one small story about how the fan should behave.

#include "fan_control.h"
#include "unity.h"

static fan_ctrl_t c;
static uint8_t kick;

void setUp(void) { fan_ctrl_init(&c); } // a fresh controller for every test
void tearDown(void) {}

// One second at this temperature, sensor working.
static uint8_t step(float t) { return fan_ctrl_step(&c, 1, t, &kick); }

// Run long enough for the slew limit to reach wherever it is going.
static uint8_t settle(float t) {
  uint8_t d = 0;
  for (int i = 0; i < 20; i++)
    d = step(t);
  return d;
}

void test_starts_in_auto_with_the_fan_stopped(void) {
  TEST_ASSERT_EQUAL(MODE_AUTO, c.mode);
  TEST_ASSERT_EQUAL_UINT8(0, c.duty_now);
}

void test_cold_room_keeps_the_fan_off(void) {
  TEST_ASSERT_EQUAL_UINT8(0, settle(18.0f));
  TEST_ASSERT_EQUAL_UINT8(0, kick);
}

void test_warm_room_starts_at_the_lowest_speed_with_a_kick(void) {
  TEST_ASSERT_EQUAL_UINT8(FAN_DUTY_START, step(21.5f));
  TEST_ASSERT_EQUAL_UINT8(1, kick);
  step(21.5f);
  TEST_ASSERT_EQUAL_UINT8(0, kick); // only when starting, not every second
}

void test_hot_room_ramps_up_by_at_most_10_percent_a_second(void) {
  uint8_t expected[] = {30, 40, 50, 60, 70, 80, 90, 100, 100};
  for (unsigned i = 0; i < sizeof(expected); i++)
    TEST_ASSERT_EQUAL_UINT8(expected[i], step(30.0f));
}

void test_halfway_up_the_curve_gives_halfway_speed(void) {
  // 23 C is halfway from 21 to 25, so halfway from 30 % to 100 %.
  TEST_ASSERT_EQUAL_UINT8(65, settle(23.0f));
}

void test_hysteresis_keeps_running_between_the_two_thresholds(void) {
  settle(21.0f);                         // on at FAN_T_ON
  TEST_ASSERT_NOT_EQUAL(0, step(20.5f)); // between OFF and ON: stays on
  TEST_ASSERT_EQUAL_UINT8(0, step(20.0f)); // at FAN_T_OFF: off
  TEST_ASSERT_EQUAL_UINT8(0, step(20.5f)); // and stays off on the way back up
}

void test_temperature_on_the_threshold_does_not_make_it_chatter(void) {
  // A reading that wobbles around 21 C must not switch the fan on and off.
  float wobble[] = {21.0f, 20.9f, 21.0f, 20.8f, 21.1f, 20.9f, 20.6f, 21.0f};
  settle(21.0f);
  for (unsigned i = 0; i < sizeof(wobble) / sizeof(wobble[0]); i++)
    TEST_ASSERT_NOT_EQUAL(0, step(wobble[i]));
}

void test_stale_sensor_runs_the_fan_at_the_safe_speed(void) {
  uint8_t d = 0;
  for (int i = 0; i < 20; i++)
    d = fan_ctrl_step(&c, 0, 0.0f, &kick); // 0 = reading not fresh
  TEST_ASSERT_EQUAL_UINT8(FAN_SAFE_DUTY, d);
}

void test_off_stops_the_fan_at_once(void) {
  settle(30.0f);
  fan_ctrl_command(&c, MODE_OFF, 0);
  TEST_ASSERT_EQUAL_UINT8(0, step(30.0f)); // no ramping down
}

void test_manual_holds_its_speed_whatever_the_temperature(void) {
  fan_ctrl_command(&c, MODE_MANUAL, 70);
  TEST_ASSERT_EQUAL_UINT8(70, settle(15.0f));
  TEST_ASSERT_EQUAL_UINT8(70, settle(35.0f));
}

void test_the_button_goes_auto_manual_off_and_round_again(void) {
  fan_ctrl_command(&c, MODE_NEXT, 0);
  TEST_ASSERT_EQUAL(MODE_MANUAL, c.mode);
  fan_ctrl_command(&c, MODE_NEXT, 0);
  TEST_ASSERT_EQUAL(MODE_OFF, c.mode);
  fan_ctrl_command(&c, MODE_NEXT, 0);
  TEST_ASSERT_EQUAL(MODE_AUTO, c.mode);
}

void test_the_button_keeps_the_last_manual_speed(void) {
  fan_ctrl_command(&c, MODE_MANUAL, 80);
  fan_ctrl_command(&c, MODE_NEXT, 0); // manual to off
  fan_ctrl_command(&c, MODE_NEXT, 0); // off to auto
  fan_ctrl_command(&c, MODE_NEXT, 0); // auto to manual
  TEST_ASSERT_EQUAL_UINT8(80, settle(15.0f));
}

void test_manual_speed_below_the_start_speed_settles(void) {
  // The page only offers 30 % and up, but the protocol allows anything from
  // 0 to 100. Below FAN_DUTY_START the fan cannot turn reliably, so asking
  // for 20 % must give a steady speed, not one that jumps up and down.
  fan_ctrl_command(&c, MODE_MANUAL, 20);
  uint8_t a = settle(22.0f);
  uint8_t b = step(22.0f);
  TEST_ASSERT_EQUAL_UINT8(a, b);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_starts_in_auto_with_the_fan_stopped);
  RUN_TEST(test_cold_room_keeps_the_fan_off);
  RUN_TEST(test_warm_room_starts_at_the_lowest_speed_with_a_kick);
  RUN_TEST(test_hot_room_ramps_up_by_at_most_10_percent_a_second);
  RUN_TEST(test_halfway_up_the_curve_gives_halfway_speed);
  RUN_TEST(test_hysteresis_keeps_running_between_the_two_thresholds);
  RUN_TEST(test_temperature_on_the_threshold_does_not_make_it_chatter);
  RUN_TEST(test_stale_sensor_runs_the_fan_at_the_safe_speed);
  RUN_TEST(test_off_stops_the_fan_at_once);
  RUN_TEST(test_manual_holds_its_speed_whatever_the_temperature);
  RUN_TEST(test_the_button_goes_auto_manual_off_and_round_again);
  RUN_TEST(test_the_button_keeps_the_last_manual_speed);
  RUN_TEST(test_manual_speed_below_the_start_speed_settles);
  return UNITY_END();
}
