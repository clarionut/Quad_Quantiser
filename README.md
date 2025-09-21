# Quad_Quantiser
<img src='https://github.com/clarionut/Quad_Quantiser/blob/main/pictures/Quad_Quantizer.jpg' width='50px'>

The original inspiration for this project came from the [Dual Quantizer by Hagiwo](https://note.com/solder_state/n/nb8b9a2f212a2), in particular the use of an RP2040 microcontroller and the general style of the OLED display / rotary encoder user interface. There the resemblance pretty much ends.

This project uses a WaveShare RP2040 Zero board as the processor, a MAX5715 12-bit 4-channel SPI DAC buffered and amplified to give a 5-octave output range on each of the four channels, plus a 24LC04 I2C EEPROM to allow the configuration to be saved. Both cores of the RP2040 are used with core 0 handling the analogue inputs, quantisation and output to the MAX5715, while core 1 handles the rotary encoder / 0.96" I2C OLED screen user interface and also the EEPROM.

There is a trigger input (common to all channels) plus CV and trigger outputs for each individual channel. The trigger input is monitored by an interrupt rather than by polling, so fast triggers should be acceptable. The channels can be independently configured to change the CV output and generate a trigger when:
- a trigger is received even if the quantisation is unchanged (TRIG)
- the quantisation of an input changes (NOTE)
- a trigger is received _and_ the quantisation has changed (T&&N)
- a trigger is received _or_ the quantisation changes (T||N)

The code is intended for use in the Arduino IDE (v 2.x) using the Waveshare RP2040 Zero core. RP2040_Quantizer6.ino and MAX5715.h should both be in the sketch folder.

The Kicad schematic includes custom symbols and footprints for the RP2040 Zero and for the MAX5715. The PCB layout is for a board to be mounted on brackets perpendicular to the front panel, and also includes custom footprints for the RP2040, MAX5715 and the power connector. I've also included a STEP file model of the RP2040 board for use with the Kicad 3D viewer. Note that the WaveShare Rp2040 Zero board does not have a reset connection broken out - I soldered a very thin wire from the RESET pad on the main PCB to one of the pads on the RP2040 Zero's Reset switch

At present the quantisation is limited to the conventional western 12-semitone scale. One obvious extension is to include less conventional scales - e.g. quarter tone or Bohlen-Pierce.

This design and its associated software are published under the Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA) 4.0 Deed.
