/*
        RP2040 zero based 4 channel voltage quantizer

        Inspired by Hagiwo's Dual Quantizer - https://note.com/solder_state/n/nb8b9a2f212a2

        Details of this implementation:
        - a 5 octave range on each channel using a MAX5715 quad SPI DAC
          for the 4 output channels. Needs a pullup on both !CLR and !LDAC
        - a 24LC04 I2C EEPROM for storage of settings
        - op-amp buffered outputs with V/oct trimmers
        - four trigger outputs via op-amp buffers
        - quantization & SPI processes run on core 0, UI (encoder) and I2C
          processes (screen, EEPROM) on core 1

        Compile using the Waveshare RP2040 Zero core
 */
#include <Wire.h>
#include <I2C_eeprom.h>
#include <SPI.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <pio_encoder.h>
#include "MAX5715.h"

#define ENC_SW 8
#define EXT_TRG 14
#define CVBASE 26
#define TRGBASE 9

#define DEBUGX

// 4 or 5 octaves
#define OCTAVES 5

static const uint8_t numSteps = OCTAVES * 12 + 1;

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// rotary encoder setup
PioEncoder myEnc(6); // also pin 7!
int oldPosition = 0;
int newPosition = 0;
int item = 1;

uint8_t SW = 1;
uint8_t old_SW = 1;
//uint8_t CLK_in = 0;
//uint8_t old_CLK_in = 0;
uint8_t newTrig = 0;

int cvIn[4], oldCvIn[4];
int cvOut[4], oldCvOut[4] = {9999};
uint8_t trigAct[4] = {0};
unsigned long trigMicros[4]; // timers for the triggers
unsigned long screenMillis;
uint8_t screenSave = 0;

// Y positions of white and black notes
int16_t yPos[] = {25, 10, 25, 10, 25, 25, 10, 25, 10, 25, 10, 25};

// note status; 1 = enabled, 0 = disabled
uint8_t note[4][12];

// configure EEPROM
I2C_eeprom eeprom(0x50, I2C_DEVICESIZE_24LC04);

struct configStruct {
  uint16_t note_str[4];  // ch1-4 enabled notes
  uint8_t sync;          // 2 bits each 0 = TRIG, 1 = NOTE, 2 = TRIG & NOTE, 3 = TRIG | NOTE
  uint16_t oct;          // octave offsets (3 bits each)
};
configStruct config;

int8_t sync[4] = {0}; // sync with: 0, trig; 1, note changed; 2, trig AND note; 3, trig OR note
char syncType[][5] = {"TRIG", "NOTE", "T&&N ", "T||N "};
int8_t oct[4] = {0}; // octave shift
int8_t chnl = 0, mode = 0;

// CV setting
int cvQuant[4][numSteps]; // input quantization steps
uint8_t qntSteps[4];
int semitone[numSteps];
uint8_t noteChanged;

// should display be refreshed? (1 = yes, 0 = no)
uint8_t oledRefresh = 1;

// DAC commands
uint8_t codeLoad[4] = {CODEA_LOADA, CODEB_LOADB, CODEC_LOADC, CODED_LOADD};

void setup() {
  // run the quantization code on core 0
#ifdef DEBUG
  Serial.begin(115200);
  while (!Serial);
#endif
  // set up trigger out, clock in and analogue pins
  for (uint8_t i = 0; i < 4; i++) {
    pinMode(TRGBASE + i, OUTPUT);
    digitalWrite(TRGBASE + i, 0);
  }
  pinMode(EXT_TRG, INPUT_PULLDOWN); // CLK in
  attachInterrupt(digitalPinToInterrupt(EXT_TRG), trgIn, RISING);
  analogReadResolution(12);
  analogRead(CVBASE);

  // Initialise SPI
  SPI.setRX(0);
  SPI.setCS(1);
  SPI.setSCK(2);
  SPI.setTX(3);
  SPI.begin(1);

  // Set normal power mode for all DACs, internal ref. to 4.096V, all DACS to 0V
  write5715(PWR_NORM, 0x0F00);
  write5715(REFINT41, 0x0000);
  write5715(CODEALL_LOADALL, 0x0000);

  // Calculate DAC values for each semitone
  for (uint8_t i = 0; i < numSteps; i++) {
    semitone[i] = (int) (0.5 + 4095.0 / (OCTAVES * 12) * (float) i);
  }

  // Wait (using a blocking pop) until the other core has read the EEPROM
  rp2040.fifo.pop();

  // Initial quantizer setting
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t k = 0;
    for (uint8_t j = 0; j < numSteps; j++) {
      if (note[i][j % 12]) {
        cvQuant[i][k] = semitone[j];
        k++;
      }
    }
    qntSteps[i] = k;
  }
  noteChanged = 0;
}

void setup1() {
  // Run the I2C (screen, EEPROM) & rotary encoder code on core 1

  Wire.begin();

  // Set up the encoder
  myEnc.begin();
  pinMode(ENC_SW, INPUT_PULLUP);

  // Initialize OLED
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();

  // Read stored data
  eeprom.begin();
  if (eeprom.isConnected()) {
    eeprom.readBlock(0, (uint8_t *) &config, sizeof(config));
  } else {
    // No EEPROM so set defaults
    for (uint8_t i = 0; i < 4; i++) {
      config.note_str[i] = 0;
    }
    config.sync = 0;
    // all octave shifts set to 0
    config.oct = 0b010010010010;
  }
  for (uint8_t i = 0; i < 4; i++) {
    // active / inactive notes
    for (uint8_t j = 0; j < 12; j++) {
      note[i][j] = (config.note_str[i] >> j) & 1U;
    }
    // sync mode
    sync[i] = (config.sync >> (i * 2)) & 0x03;
    // octave offset
    oct[i] = (config.oct >> (i * 3)) & 0x07;
  }
  // Tell the other core that EEPROM data has been read
  rp2040.fifo.push_nb(1);

  screenMillis = millis();
}

void loop() {
  // Quantize input CVs and handle triggers according to the config settings
  //old_CLK_in = CLK_in;

  // Turn off any expired triggers
  for (uint8_t i = 0; i < 4; i++) {
    if (trigAct[i] && (micros() - trigMicros[i]) > 5000) {
      digitalWrite(TRGBASE + i, 0);
      trigAct[i] = 0;
    }
  }

  // Read input CVs and quantize
  for (uint8_t i = 0; i < 4; i++) {
    cvIn[i] = analogRead(CVBASE + i);
    if (qntSteps[i]) {
      // At least one quantisation note selected
      cvIn[i] &= 0xFFFC; // clear the lowest 2 bits to de-jitter
      // Allow settling time for the next ADC reading
      if (i < 3)
        analogRead(CVBASE + i + 1);
      else
        analogRead(CVBASE);
      // ADC anti-jitter - ignore small changes
      if (abs(oldCvIn[i] - cvIn[i]) >= 20) {
        for (uint8_t j = 0; j < qntSteps[i] - 1; j++) {
          // Quantize each channel
          if (cvIn[i] < cvQuant[i][j + 1]) {
            // The input CV is in the current quantization interval, so find the nearer note
            if ((cvIn[i] - cvQuant[i][j]) <= (cvQuant[i][j+1] - cvIn[i])) {
              // Closer to the lower note
              cvOut[i] = cvQuant[i][j] + (oct[i] - 2) * 1000;
            } else {
              // Closer to the upper note
              cvOut[i] = cvQuant[i][j+1] + (oct[i] - 2) * 1000;
            }
            break;
          }
        }
      }
    } else {
      // No quantisation specified, so output unchanged apart from octave shift
      cvOut[i] = cvIn[i] + (oct[i] - 2) * 1000;
    }
    while (cvOut[i] < 0)    cvOut[i] += 1000;
    while (cvOut[i] > 4095) cvOut[i] -= 1000;
  }

  // Output quantized CVs and triggers if required
  //CLK_in = digitalRead(EXT_TRG);
  //if (CLK_in == 1 && old_CLK_in == 0) newTrig = 1;

  for (uint8_t i = 0; i < 4; i++) {
    // Output CV amd trigger if criteria are met
    if ((3 == sync[i] && (newTrig || cvOut[i] != oldCvOut[i])) ||  // T|N
        (2 == sync[i] && newTrig && cvOut[i] != oldCvOut[i]) ||    // T&N
        (1 == sync[i] && cvOut[i] != oldCvOut[i]) ||               // NOTE
        (0 == sync[i] && newTrig)) {                               // TRIG
      // CV
      write5715(CODEA_LOADA + i, cvOut[i]);
      oldCvIn[i] = cvIn[i];
      oldCvOut[i] = cvOut[i];
      // Trigger only for quantised output or active trigger
      if (newTrig || qntSteps[i]) {
        digitalWrite(TRGBASE + i, 1);
        trigMicros[i] = micros();
        trigAct[i] = 1;
      }
    }
  }
  newTrig = 0;
}

void loop1() {
    // Start screensaver after 60 seconds
  if (!screenSave && (millis() - screenMillis > 60000)) {
    screenSave = 1;
    display.clearDisplay();
    display.display();
  }
  // Process rotary encoder input
  old_SW = SW;
  newPosition = myEnc.getCount();
  // Encoder has 4 steps per detent
  if (((newPosition - 3) / 4  > oldPosition / 4) || ((newPosition + 3) / 4  < oldPosition / 4)) {
    int8_t inc = 0;
    if ((newPosition - 3) / 4  > oldPosition / 4)
      inc = -1;
    else if ((newPosition + 3) / 4  < oldPosition / 4)
      inc = +1;
    oledRefresh = 1;
    oldPosition = newPosition;
    if (screenSave) {
      screenSave = 0;
    } else if (0 == mode) {
     item += inc;
      if (-1 == item) item = 15;
      item %= 16;
    } else {
      switch (item) {
       case 0:
        chnl += inc;
        if (chnl < 0) chnl = 3;
        chnl %= 4;
        break;
       case 13:
        sync[chnl] += inc;
        if (sync[chnl] < 0) sync[chnl] = 3;
        sync[chnl] %= 4;
        break;
       case 14:
        oct[chnl] += inc;
        if (oct[chnl] < 0) oct[chnl] = 4;
        oct[chnl] %= 5;
        break;
      }
    }
    screenMillis = millis();
  }
  item = constrain(item, 0, 15);

  // Process push switch input
  SW = digitalRead(ENC_SW);
  if (SW && !old_SW) {
    oledRefresh = 1;
    if (screenSave) {
      screenSave = 0;
    } else if (0 == mode) {
      if (0 == item) {
        mode = 1;
      } else if (item <= 12 && item >= 1) {
        // note selection
        note[chnl][item - 1] = !note[chnl][item - 1];
        noteChanged = 1;

      } else if ((13 == item) || (14 == item)) {
        // sync or oct setting
        mode = 1;

      } else if (15 == item) {
        save();
      }

      // Update the active note selection
      if (noteChanged) {
        for (uint8_t i = 0; i < 4; i++) {
          uint8_t k = 0;
          for (uint8_t j = 0; j < numSteps; j++) {
            if (note[i][j % 12] == 1) {
              cvQuant[i][k] = semitone[j];
              k++;
            }
          }
          qntSteps[i] = k;
        }
        noteChanged = 0;
      }

    } else {
      mode = 0;
    }
    screenMillis = millis();
  }
  // Update display if necessary
  if (oledRefresh == 1) {
    OLED_display();
    oledRefresh = 0;
  }
}

void OLED_display() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  // Channel number
  display.setCursor(34, 0);
  display.print("CHANNEL:");
  display.setCursor(91, 0);
  display.print(chnl + 1);
  // Notes
  int xPos = 10;
  for (uint8_t i = 0; i < 12; i++) {
    drawNote(xPos, yPos[i], 12, 13, 3, WHITE, note[chnl][i]);
    // Skip non-existent black note between E & F
    xPos += (4 == i) ? 16 : 8;
  }
  // Config settings
  display.setCursor(5, 46);
  display.print("SYNC: ");
  display.setCursor(45, 46);
  display.print(syncType[sync[chnl]]);
  display.setCursor(64, 46);
  display.print("  OCT: ");
  display.setCursor(111, 46);
  display.print(oct[chnl] - 2);
  display.setCursor(54, 57);
  display.print("SAVE");
  // Draw pointer for current item
  if (0 == item) {
    // Channel
    if (1 == mode)
      // Change channel
      display.drawCircle(85, 3, 2, WHITE);
    else
      drawPointer(82, 0, 82, 6, 88, 3, WHITE, 0);
  } else if (item <= 5) {
    // First five notes
    drawPointer(7 + item * 8, 38, 4 + item * 8, 43, 10 + item * 8, 43, WHITE, 1);
  } else if (item >= 6 && item <= 12) {
    // Remainder of notes
    drawPointer(15 + item * 8, 38, 12 + item * 8, 43, 18 + item * 8, 43, WHITE, 1);
  } else if (13 == item) {
    // Sync
    if (1 == mode) {
      // Change sync mode
      display.drawCircle(38, 49, 2, WHITE);
    } else {
      drawPointer(35, 46, 35, 52, 41, 49, WHITE, 0);
    }
  } else if (14 == item) {
    // Octave
    if (1 == mode) {
      // Change octave offset
      display.drawCircle(104, 49, 2, WHITE);
    } else {
      drawPointer(101, 46, 101, 52, 107, 49, WHITE, 0);
    }
  } else if (15 == item) {
    // Save
    drawPointer(44, 57, 44, 63, 50, 60, WHITE, 1);
  }
  display.display();
}

void save() {
  // Save settings data to EEPROM
  display.clearDisplay();
  display.setTextColor(BLACK, WHITE);
  if (eeprom.isConnected()) {
    config.sync = 0;
    config.oct = 0;
    for (uint8_t i = 0; i < 4; i++) {
      // Note on/off settings
      for (uint8_t j = 0; j < 12; j++) {
        if (note[i][j])
          config.note_str[i] |= (1U << j);
        else
          config.note_str[i] &= ~(1U << j);
      }
      // Trigger sync setting
      config.sync <<= 2;
      config.sync |= sync[3 - i];
      // Octave offset
      config.oct <<= 3;
      config.oct |= oct[3 - i];
    }
    eeprom.writeBlock(0, (uint8_t *) &config, sizeof(config));
    display.setCursor(38, 57);
    display.print(" SAVED ");
  } else {
    display.setCursor(24, 57);
    display.print(" NO EEPROM ");
  }
  // Show message on completion
  display.display();
  delay(1000);
}

void write5715(uint8_t cmd, uint16_t data) {
  // Send values to the MAX5715
  // N.B. The data values written to the MAX5715 must match the description in the data sheet
  // Do the required bit shift here
  data <<= 4;
  uint8_t tmp[3];
  tmp[0] = cmd;
  tmp[1] = (data & 0xFF00) >> 8;
  tmp[2] = (data & 0x000F);

  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE1));
  SPI.transfer(&tmp, nullptr, 3);
  SPI.endTransaction();

  delayMicroseconds(1);
}

void drawNote(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t c, uint8_t f) {
  // Draw note rectangle; fill type specified by parameter f
  if (f)
    display.fillRoundRect(x, y, w, h, r, c);
  else
    display.drawRoundRect(x, y, w, h, r, c);
}

void drawPointer(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t c, uint8_t f) {
  // Draw pointer, fill type specified by parameter f
  // Filled pointers => click for immediate action
  // Empty pointers  => click to enable rotary encoder for selection, click again to finish
  if (f)
    display.fillTriangle(x0, y0, x1, y1, x2, y2, c);
  else
    display.drawTriangle(x0, y0, x1, y1, x2, y2, c);
}

void trgIn() {
  newTrig = 1;
}