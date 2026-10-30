# Bill of materials

Everything that goes into one fan. Reichelt order numbers where the part came
from Reichelt, the rest came from Amazon or from my parts drawer.

## On the board

| Ref | Qty | Part | Notes |
|---|---|---|---|
| U1 | 1 | STM32F411CEU6 "Black Pill" board | plugs into two 1x20 female headers |
| U2 | 1 | ESP32 DevKit, 38 pin, CP2102, USB-C | plugs into two 1x19 female headers |
| U3 | 1 | 5 V DC/DC converter, 1 A, Recom R-78E5.0-1.0 | Reichelt `R-78E50-10` |
| C1 | 1 | 10 uF 50 V electrolytic | 12 V input of U3. Reichelt `GA-A 10U 50` |
| C2 | 1 | 100 uF 25 V electrolytic | 5 V output of U3. Reichelt `NHG-A 100U 25` |
| D1, D2 | 2 | 1N5819 Schottky diode, 1 A 40 V | 5 V to the Black Pill and to the ESP32. Reichelt `1N 5819 TSC` |
| Q1 | 1 | PN2222A NPN transistor, TO-92 | pulls the fan's PWM wire low |
| R1 | 1 | 1 kΩ | base resistor of Q1 |
| R2 | 1 | 10 kΩ | holds Q1 off while the STM32 boots |
| R4, R5 | 2 | 5.1 kΩ | I2C pull-ups on SDA and SCL |
| SW1 | 1 | Tactile switch 6x6 mm, 4.3 mm high | mode button. Reichelt `TASTER 3301` |
| J1, J2 | 2 | JST XH 4 pin header, straight | OLED and sensor. Reichelt `JST XH4P ST` |
| J3 | 1 | Screw terminal, 2 pin, 5.08 mm | 12 V in. Reichelt `LAKL 1,5 2 5,08` |
| J4 | 1 | Pin header 1x4, 2.54 mm | fan. Cut from Reichelt `SL 1X50G 2,54` |
| | 4 | Female header 1x20, 2.54 mm | two for U1, two cut to 19 for U2. Reichelt `FIS BL1 20 Z` |

## Off the board

| Qty | Part | Notes |
|---|---|---|
| 1 | ARCTIC P12 Pro, 120 mm 4 pin PWM fan | |
| 1 | Fan grill 120 mm | Reichelt `DELOCK 67308` |
| 1 | AHT20 + BMP280 sensor module, I2C | |
| 1 | SSD1306 OLED 0.96", 128x64, I2C | |
| 1 | 12 V 1 A plug-in power supply, 5.5/2.1 mm plug | Reichelt `HNP 12-120V2` |
| 1 | DC panel jack 5.5/2.1 mm | Reichelt `HEBL 21` |
| 1 | Rocker switch, on/off | Reichelt `WIPPE 1801.1146` |
| | Jumper wires, female/female | sensor, OLED and power wiring |
| 4 | Self-adhesive rubber feet | Reichelt `DELOCK 18309` |
| | M3 screws and nuts | board and enclosure |

## For the board itself

| Part | Notes |
|---|---|
| PLA filament | board and enclosure |
| Copper foil tape with conductive adhesive | the tracks |
| ST-Link V2 | to flash the STM32 the first time |
