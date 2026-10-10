# Smart desk fan

A desk fan that picks its own speed from the room temperature, on an STM32F411
with FreeRTOS. I am building it to actually use in summer, and to get properly
comfortable with an RTOS while doing it rather than by reading about one.

A 12 V PC fan is driven from a PWM pin through a transistor. A temperature and
humidity sensor and a small OLED sit on the same I2C bus. The fan's tachometer
wire goes back into a timer input, so the firmware knows how fast the fan is
really turning and not just what it asked for.

## What works today

- Two application tasks plus the FreeRTOS default task, each with its own job
  and its own rate
- AHT20 read over I2C: temperature and humidity, once per second
- SSD1306 OLED showing temperature, humidity, fan speed and commanded duty
- 25 kHz PWM on the fan, which is above hearing, so the fan does not whine
- Tachometer counted through a timer input capture interrupt and turned into RPM
- A PID that sets the duty from the temperature

## The state of the control, honestly

The PID is the part I am least happy with, for two reasons.

The gains are guesses. They were picked by trying numbers until the behaviour
looked reasonable, which is not the same as knowing they are right.

More importantly, a PID on room temperature is the wrong shape for this device.
A desk fan does not cool a room, it blows air at the person sitting in front of
it. So there is no loop to close: the error never goes to zero however hard the
fan works, and the integral term just keeps climbing into its limit. What this
device actually wants is a map from temperature to fan speed, warmer means
faster, with a floor, a ceiling and some hysteresis. That replacement is the
next real piece of work.

Writing this down because the code currently says PID and the comment at the
top of it says PID, and anyone reading it should know that is a leftover from
the first version rather than a decision.

## Hardware

| | |
|---|---|
| MCU | STM32F411CEU6, the "Black Pill" board |
| Clock | 96 MHz from a 25 MHz crystal |
| Fan PWM | TIM1 CH1 on PA8, 25 kHz, 3840 counts of resolution |
| Tachometer | TIM2 CH1 on PA5, input capture on rising edges, 1 us ticks |
| Sensor and display | AHT20 and SSD1306 on I2C1, PB6 and PB7, 400 kHz |
| RTOS | FreeRTOS through CMSIS-RTOS v2 |

The fan is switched low side by a logic level MOSFET, with the tach line pulled
up to 3.3 V. The fan's own 12 V supply and the board's ground are common.

## Building

The project is a STM32CubeMX project with a CMake build. You need the ARM GNU
toolchain, CMake and Ninja on your path.

```
cmake --preset Debug
cmake --build build/Debug
```

That produces `smart_fan.elf` and, through a post-build step, the
`smart_fan.bin` and `smart_fan.hex` that a flashing tool wants, all in
`build/Debug`.

Flashing with an ST-Link:

```
st-flash write build/Debug/smart_fan.bin 0x8000000
```

The `.elf` is an ARM binary. Running it on your own machine will only get you
an "exec format error", which is exactly what should happen.

If CMake complains about a path that no longer exists, the build directory is
holding a cached path from wherever the project used to live. Delete `build`
and configure again.

The peripheral configuration lives in `smart_fan.ioc` and can be reopened in
STM32CubeMX. Everything I have written by hand is inside the `USER CODE`
markers, so regenerating does not wipe it.

## What is next

In the order I plan to do it:

1. Fixed period control loop, so the timestep the integral uses is the timestep
   that actually elapsed
2. Validity and age on the sensor reading, and a defined safe state when the
   reading is stale, instead of running forever on the last value
3. Replace the PID with the temperature to speed map described above
4. Kick-start and hysteresis, because a 12 V fan will not start from standstill
   at a low duty even though it will keep turning there
5. A mutex on the I2C bus, before anything else starts using it
6. Replace the shared `volatile` globals between tasks with a queue or a task
   notification, which is the part I most want to learn properly
7. A button and a few modes: automatic, fixed speed, off
8. An ESP32 over a serial link, and a small dashboard to see and set it from a
   phone

## Licence

MIT, see LICENSE.
