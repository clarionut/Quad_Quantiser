# Quad_Quantiser
The original inspiration for this project came from the Dual Quantizer by Hagiwo (https://note.com/solder_state/n/nb8b9a2f212a2), in particular the use of an RP2040 microcontroller and the general style of the OLED display / rotary encoder user interface. There the resemblance pretty much ends.

This project uses a WaveShare RP2040 Zero board as the processor, a MAX5715 12-bit 4-channel SPI DAC buffered and amplified to give a 5-octave output range on each channel, plus a 24LC04 I2C EEPROM to allow the configuration to be saved. Both cores of the RP2040 are used with core 0 handling the analogue inputs, quantisation and output to the MAX5715, while core 1 handles the rotary encoder / I2C screen user interface and also the EEPROM.

There is a trigger input (common to all channels) plus CV and trigger outputs for each individual channel. The trigger input is monitored by an interrupt rather than by polling, so fast triggers should be acceptable. The channels can be independently configured to change the CV output and generate a trigger when:
- the quantisation of an input changes (NOTE)
- a trigger is received even if the quantisation is unchanged (TRIG)
- a trigger is received _and_ the quantisation has changed (T&N)
- a trigger is received _or_ the quantisation has changed (T|N)

The code is inteded for use in the Arduino IDE (v 2.x) using the Waveshare RP2040 Zero core. RP2040_Quantizer6.ino and MAX5715.h should both be in the sketch folder.

The Kicad schematic and board layout 

At present the quantisation is only to the conventional western 12 semitone scale. One obvious extension is to include less convention scales - e.g. quarter tone or Bohlen-Pierce.
