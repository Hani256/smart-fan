#include "fan_control.h"

void fan_ctrl_init(fan_ctrl_t *c) {
  c->mode = MODE_AUTO;
  c->manual_duty = MANUAL_DUTY_DEFAULT;
  c->fan_on = 0;
  c->duty_now = 0;
}

void fan_ctrl_command(fan_ctrl_t *c, fan_mode_t mode, uint8_t duty_pct) {
  if (mode == MODE_NEXT) {
    c->mode = (c->mode == MODE_OFF) ? MODE_AUTO : (fan_mode_t)(c->mode + 1);
  } else {
    c->mode = mode;
    if (mode == MODE_MANUAL)
      c->manual_duty = duty_pct;
  }
}

// What the mode and the temperature ask for, before slew limiting.
static uint8_t wanted_duty(fan_ctrl_t *c, uint8_t temp_fresh, float t) {
  if (c->mode == MODE_OFF) {
    c->fan_on = 0; // back in auto, the curve starts fresh
    return 0;
  }
  if (c->mode == MODE_MANUAL) {
    c->fan_on = 0;
    return c->manual_duty;
  }
  if (!temp_fresh) {
    c->fan_on = 0;        // a returning sensor starts the curve fresh
    return FAN_SAFE_DUTY; // nothing to go on, so stop pretending to control
  }

  // Hysteresis. It takes FAN_T_ON to start but only FAN_T_OFF to stop, so a
  // temperature sitting exactly on one threshold cannot make the fan chatter.
  if (!c->fan_on && t >= FAN_T_ON)
    c->fan_on = 1;
  if (c->fan_on && t <= FAN_T_OFF)
    c->fan_on = 0;

  if (!c->fan_on)
    return 0;
  if (t >= FAN_T_FULL)
    return FAN_DUTY_MAX;

  // Straight line between the two temperatures, starting at the lowest duty
  // that actually turns the fan rather than at zero.
  float frac = (t - FAN_T_ON) / (FAN_T_FULL - FAN_T_ON);
  return FAN_DUTY_START + (uint8_t)(frac * (FAN_DUTY_MAX - FAN_DUTY_START));
}

uint8_t fan_ctrl_step(fan_ctrl_t *c, uint8_t temp_fresh, float temp_c,
                      uint8_t *kick) {
  uint8_t want = wanted_duty(c, temp_fresh, temp_c);

  // Below FAN_DUTY_START the fan cannot turn reliably, and the slew limit
  // would bounce between that and the request every second. So any speed
  // between 0 and FAN_DUTY_START means FAN_DUTY_START.
  if (want > 0 && want < FAN_DUTY_START)
    want = FAN_DUTY_START;

  *kick = (c->duty_now == 0) && (want > 0);

  // Slew limiting. The curve is allowed to jump, the fan is not: a sudden
  // change in airflow is the thing a person in front of it actually notices.
  if (want == 0) {
    c->duty_now = 0; // stopping may be immediate
  } else if (c->duty_now < FAN_DUTY_START) {
    c->duty_now = FAN_DUTY_START; // from standstill, jump to where it turns.
  } // Ramping through 10 and 20 only makes it buzz.
  else if (want > c->duty_now) {
    c->duty_now += (want - c->duty_now > FAN_SLEW_PER_S) ? FAN_SLEW_PER_S
                                                         : (want - c->duty_now);
  } else {
    c->duty_now -= (c->duty_now - want > FAN_SLEW_PER_S) ? FAN_SLEW_PER_S
                                                         : (c->duty_now - want);
  }
  return c->duty_now;
}
