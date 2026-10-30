# Board guide

How the board is built, what goes where, and how everything off the board is
wired to it. The parts are listed in [BOM.md](BOM.md).

## Making the board

1. Print `board.stl` in PLA, tracks facing up. No supports.
2. Cover the whole top with copper tape and press it down firmly onto the
   ridges, a fingernail or the back of a knife along every edge.
3. Cut along both edges of every ridge with a sharp craft knife and peel the
   copper off the flat areas in between, so only the ridges keep it.
4. Open the holes through the tape with a needle.
5. Before soldering anything, check with a multimeter that no two tracks
   touch. Copper tape that is cut short of an edge is the usual culprit.

Solder the low parts first (resistors, diodes), then the transistor, button,
capacitors and connectors, then the regulator and the module headers. Keep
the iron short on each joint, the PLA underneath softens.

## Getting the polarity right

| Part | Which way round |
|---|---|
| D1, D2 | band (cathode) towards the module, the anode side comes from the regulator |
| C1, C2 | the minus stripe on the GND track |
| Q1 | E to GND, B to R1 and R2, C to the fan's PWM wire |
| U3 | Vin from 12 V, GND in the middle, Vout to the diodes |
| J3 | the 12 V wire on the pin connected to C1's plus |

## Module orientation

- **Black Pill:** USB connector pointing towards the ESP32
- **ESP32:** USB connector pointing towards the Black Pill

The two USB connectors face each other, so there is no room for a cable with
both modules in place. The STM32 is flashed through the SWD pins at its far
end, and the ESP32 is updated over WiFi after the first upload.

Check the pin labels printed on each module against the table below before
plugging it in. The first board had the Black Pill footprint flipped.

## Pins in use

| Signal | STM32 | ESP32 | Board connector |
|---|---|---|---|
| Fan PWM (through Q1) | PA8, TIM1 CH1 | | J4 pin 4 |
| Fan tach | PA5, TIM2 CH1 | | J4 pin 3 |
| I2C SCL | PB6 | | J1, J2 |
| I2C SDA | PB7 | | J1, J2 |
| Mode button | PA0 | | SW1 |
| STM32 TX to ESP32 | PA2 | GPIO16 (RX) | on the board |
| ESP32 TX to STM32 | PA3 | GPIO4 (TX) | on the board |
| 5 V | 5V pin, through D1 | 5V pin, through D2 | |
| GND | G | GND | |

## Wiring off the board

### Power

```
12 V supply ── DC jack ── rocker switch ── J3 (+12 V)
                  └──────────────────────── J3 (GND)
```

On the HEBL 21 jack the **middle lug is GND**. +12 V is the side lug that
connects to the centre pin. The third lug stays free.

### Fan (J4)

| J4 pin | Fan wire |
|---|---|
| 1 | GND |
| 2 | +12 V |
| 3 | Tach |
| 4 | PWM |

This is the standard 4 pin fan order, so the fan's own plug fits directly.
A bare pin header has no key, so check that the plug's GND wire sits on the
pin joined to the GND track. The wrong way round, 12 V lands on the tach pin.

### OLED and sensor (J1, J2)

Both sit on the same I2C bus, so it does not matter which goes on which
connector. Their pin orders differ, so wire each one by the labels on the
module, not by position:

| Module | Pin order on the module |
|---|---|
| SSD1306 OLED | GND, VCC, SCL, SDA |
| AHT20 + BMP280 | VDD, SDA, GND, SCL |

Both run from 3.3 V. Addresses: OLED 0x3C, AHT20 0x38, BMP280 0x77.

## First power-up

1. Without the modules plugged in, connect 12 V and switch on. The 5 V pins
   of both module sockets should read about 4.6 to 4.8 V (5 V minus the
   diode).
2. Switch off, plug in the Black Pill, the OLED and the sensor, switch on.
   The LED on the Black Pill blinks and the OLED shows the temperature.
3. Plug in the ESP32 and the fan. Within about 30 s the web page shows
   `Live`.
