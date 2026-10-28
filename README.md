# Smart desk fan

A desk fan that picks its own speed from the room temperature, on an STM32F411
with FreeRTOS. I am building it to actually use in summer, and to get properly
comfortable with an RTOS while doing it rather than by reading about one.

A 4 pin 12 V PC fan gets its speed from a PWM pin through a transistor. A
temperature and humidity sensor and a small OLED sit on the same I2C bus. The fan's tachometer
wire goes back into a timer input, so the firmware knows how fast the fan is
really turning and not just what it asked for.

## What works today

- Two application tasks plus the FreeRTOS default task, each with its own job
  and its own rate
- AHT20 read over I2C: temperature and humidity, once per second
- SSD1306 OLED showing temperature, humidity, fan speed and commanded duty
- 25 kHz PWM on the fan, which is above hearing, so the fan does not whine
- Tachometer counted through a timer input capture interrupt and turned into RPM
- A comfort curve from temperature to fan speed, with hysteresis, a slew limit
  and a short full power kick when the fan starts from standstill
- A safe state: if the temperature reading is older than 5 s, the fan runs at
  a fixed 50 % and the OLED shows dashes instead of an old number
- The tasks share data through one item queues (mailboxes) instead of shared
  `volatile` globals. The only one left is the tach pulse counter between the
  interrupt and the fan task, which is read and cleared inside a critical
  section
- If the sensor is unplugged and plugged back in, the I2C peripheral is
  rebuilt after three failed rounds and the sensor is calibrated again

## Why a curve and not a PID

The first version used a PID on room temperature, and that was the wrong
shape for this device. A desk fan does not cool a room, it blows air at the
person sitting in front of it. So there is no loop to close: the error never
goes to zero however hard the fan works, and the integral term just keeps
climbing into its limit.

What it uses now is a map from temperature to fan speed. Below 21 C it is off,
from there it rises in a straight line to full speed at 25 C, and it only
switches off again once the room has dropped to 20 C, so a temperature sitting
on the threshold cannot make it chatter. Those numbers are meant to be tuned
by how it feels, and they are all at the top of `freertos.c`.

## Hardware

| | |
|---|---|
| MCU | STM32F411CEU6, the "Black Pill" board |
| Clock | 96 MHz from a 25 MHz crystal |
| Fan PWM | TIM1 CH1 on PA8, 25 kHz, 3840 counts of resolution |
| Tachometer | TIM2 CH1 on PA5, input capture on rising edges, 1 us ticks |
| Sensor and display | AHT20 and SSD1306 on I2C1, PB6 and PB7, 400 kHz |
| Mode button | PA0, falling edge interrupt, internal pull-up (not used by the firmware yet) |
| ESP32 link | USART2 on PA2 (TX) and PA3 (RX), with DMA (not used by the firmware yet) |
| RTOS | FreeRTOS through CMSIS-RTOS v2 |

The fan is an ARCTIC P12 PWM, a 4 pin fan that runs from 12 V all the time and
takes its speed from the PWM wire. A PN2222A transistor pulls that wire low,
driven from PA8 through a base resistor. The transistor inverts the signal, so
the timer channel is set to active low and the duty in the code still means
what it says. The tach wire is open collector and is pulled up to 3.3 V by the
internal pull-up on PA5.

Everything sits on a single sided board printed in PLA, with copper tape on
raised ridges as the tracks. A 12 V jack feeds the fan directly and a 5 V
regulator for the Black Pill and the ESP32. The fan's 12 V supply and the
board's ground are common.

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

Already done, in this order: a fixed period control loop, validity and age on
the sensor reading with a safe state, the PID replaced by the curve, kick-start
and hysteresis, and mailboxes instead of shared globals.

Still to do:

1. A button and a few modes: automatic, fixed speed, off
2. A mutex on the I2C bus, before anything else starts using it
3. An ESP32 over a serial link, and a small dashboard to see and set it from a
   phone

## Licence

MIT, see LICENSE.
