#ifndef FAN_CONTROL_H
#define FAN_CONTROL_H

// Deciding how fast the fan should turn.
//
// Only logic lives here: no HAL, no FreeRTOS, no hardware. Numbers go in,
// a number comes out. That is what lets the same code run on the STM32 and
// in the unit tests on a laptop.

#include <stdint.h>

// Temperature to fan speed.
//
// A desk fan does not cool the room, it blows air at whoever is sitting in
// front of it, so there is no loop to close and a PID has nothing to converge
// to. What this needs is a comfort curve: this warm means this fast. Set these
// numbers to what feels right to you, that is the correct way to tune them.
#define FAN_T_ON            21.0f // start blowing at or above this
#define FAN_T_OFF           20.0f // and stop again only once it has dropped to here
#define FAN_T_FULL          25.0f // full speed at or above this
#define FAN_DUTY_START      30    // the slowest duty at which the fan actually turns
#define FAN_DUTY_MAX        100
#define FAN_SLEW_PER_S      10    // the most the duty may change in one second
#define FAN_SAFE_DUTY       50    // what the fan runs at when it has nothing to go on
#define MANUAL_DUTY_DEFAULT 50    // manual speed until someone picks another

// What the fan should do. Changed by the button and from the phone.
typedef enum {
  MODE_AUTO = 0, // follow the temperature curve
  MODE_MANUAL,   // fixed speed
  MODE_OFF,      // stopped
  MODE_NEXT,     // only in commands: "go to the next mode" (the button)
} fan_mode_t;

// Everything the controller remembers from one second to the next.
typedef struct {
  fan_mode_t mode;
  uint8_t manual_duty; // the speed for MODE_MANUAL
  uint8_t fan_on;      // hysteresis: is the curve currently blowing?
  uint8_t duty_now;    // what we command, after slew limiting
} fan_ctrl_t;

// Start in automatic, fan stopped.
void fan_ctrl_init(fan_ctrl_t *c);

// Apply one command from the button or the phone. duty_pct is only used
// with MODE_MANUAL.
void fan_ctrl_command(fan_ctrl_t *c, fan_mode_t mode, uint8_t duty_pct);

// One second has passed. Given the temperature (and whether it can be
// trusted), return the duty to command now, 0 to 100. *kick is set to 1 when
// the fan is starting from standstill and should get a short full power kick.
uint8_t fan_ctrl_step(fan_ctrl_t *c, uint8_t temp_fresh, float temp_c,
                      uint8_t *kick);

#endif
