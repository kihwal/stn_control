/*
 * N7DDC tuner serial interface for programmed tuning.
 * Dec 7, 2022. Kihwal Lee,K9SUL
 *
 * This will disable the autotuning capability of the tuner.
 * It may be extended to include auto tuning algorithm with
 * external control in the future.
 *
 * Used a Teensy 2.0, which has a built-in USB-serial port. The control is
 * done over the serial port.
 * 
 * ATU-1000 connections
 *       L  1  2  3  4  5  6  7  C  1  2  3  4  5  6  7  N  GND  VDD FWD REF
 * uC pins 24  4 25  5 26  7  6    18 14 17 13 16 12 15 11  8,19  20   2   3
 * Teensy  20  0 21  1  4  3  2    14 10 13  9 12  8 11  7   G   VDD  19  18
 * 
 * A 7x7 (7 inductors and 7 capacitors) tuner configuration gives 2^7 = 128
 * possible values for L and C respectibly. 
 * 
 * Commands
 * 1. Set tu0101sLLLCCCN
 * LLL: 3 digit L index 0..127. prepend with 0 if necessary.
 * CCC: 3 digit C index 0..127. 
 * N: L-network configuration. 0 (hi-z ant) or 1 (lo-z ant). 
 * returns okLLLLCCCCN
 * 
 * 2. Read tu0101r
 * returns okLLLLCCCCN
 * 
 * 3. Read power tu0101p
 * returns okFFFFRRRR0
 * F: forward
 * R: reflected
 * 
 * 4. Test tu0101t
 * runs a test sequence.
 * 
 * The serial port is configured to be 9600 baud.
 */
#include <LiquidCrystal_I2C.h>
#include <stdio.h>

//#define TU_CAL_USE_INTERNAL

#define TU_CMD_SET  's'
#define TU_CMD_READ 'r'
#define TU_CMD_PWR  'p'
#define TU_CMD_TEST 't'
#define TU_CMD_MAGIC "tu0101"

// relay control pins. These are not physical pin numbers,
// but the assigned IO pin numbers for the Teensy 2.0. 
#define TU_L1 20
#define TU_L2  0
#define TU_L3 21
#define TU_L4  1
#define TU_L5  4
#define TU_L6  3
#define TU_L7  2

#define TU_C1 14
#define TU_C2 10
#define TU_C3 13
#define TU_C4  9
#define TU_C5 12
#define TU_C6  8
#define TU_C7 11
#define TU_NC  7  // L network configuration

//#define TU_PWR_FWD 18
//#define TU_PWR_REF 19

// use an external coupler
// A Telepost LPC-50x pin 3 and 5 are connected to 17 and 16.
// pin 1 is +12V, pin 7 is gnd. The max output is 10V, so they
// are fed through a voltage divider (100k+100k).
#define TU_PWR_FWD 16
#define TU_PWR_REF 17

#define TU_NC_HIZ 0
#define TU_NC_LOZ 1
#define TU_BUFF_LEN 20
#define TU_UI_PWR_UPDATE_INTERVAL_MSEC 250


uint8_t l_val = 0; // current L value 0..127
uint8_t c_val = 0; // current C value 0..127
uint8_t n_val = TU_NC_HIZ; // current network config. t/f
char buff[TU_BUFF_LEN]; // The longest command or response is 14 bytes.
char magic[] = TU_CMD_MAGIC;
uint8_t l_pins[] = {TU_L1, TU_L2, TU_L3, TU_L4, TU_L5, TU_L6, TU_L7};
uint8_t c_pins[] = {TU_C1, TU_C2, TU_C3, TU_C4, TU_C5, TU_C6, TU_C7};
boolean first = true;
int pwr, swr, pwr_max;
int raw_fwd, raw_ref;
unsigned long last_update;

LiquidCrystal_I2C lcd(0x27, 16, 2);

#ifdef TU_CAL_USE_INTERNAL
// internal coupler with 20:1 transformers
#define TU_CAL_TBL_SIZE 100

uint16_t tu_cal_adc[TU_CAL_TBL_SIZE] = {
  0,  10,  20,  28,  34,  38,  44,  49,  52,  58,
 60,  79,  92, 106, 117, 138, 157, 174, 188, 202,
211, 224, 227, 234, 237, 245, 253, 254, 257, 264,
270, 278, 282, 291, 298, 303, 309, 313, 318, 322,
332, 354, 369, 384, 399, 412, 427, 440, 454, 468,
480, 493, 500, 515, 527, 533, 544, 550, 561, 566,
577, 582, 590, 596, 601, 609, 615, 619, 628, 632,
638, 642, 657, 666, 670, 674, 678, 683, 687, 695,
700, 704, 712, 716, 719, 726, 731, 735, 740, 744,
747, 754, 758, 760, 766, 768, 775, 780, 799, 806};

uint16_t tu_cal_watts[TU_CAL_TBL_SIZE] = {
   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
  10,  15,  20,  25,  30,  40,  50,  60,  70,  80,
  90,  98, 101, 107, 111, 117, 124, 126, 130, 136,
 143, 150, 155, 165, 172, 180, 185, 190, 194, 200,
 211, 240, 262, 283, 302, 323, 346, 367, 386, 410,
 433, 456, 469, 488, 511, 522, 544, 557, 580, 591,
 614, 625, 647, 657, 668, 690, 698, 708, 733, 739,
 751, 760, 798, 820, 831, 841, 852, 863, 874, 895,
 905, 917, 940, 950, 960, 979, 990,1000,1018,1027,
1037,1056,1065,1071,1087,1095,1112,1122,1184,1207};

#else
// external coupler, Telepost LPC-502
#define TU_CAL_TBL_SIZE 90

uint16_t tu_cal_adc[TU_CAL_TBL_SIZE] = {
   0,  10,  15,  19,  23,  26,  29,  32,  34,  36,
  39,  48,  56,  64,  73,  89, 100, 105, 111, 117,
 124, 129, 135, 138, 144, 148, 151, 155, 160, 165,
 170, 175, 180, 185, 193, 198, 202, 206, 211, 213,
 225, 232, 245, 254, 263, 270, 279, 287, 295, 301,
 306, 310, 318, 320, 329, 336, 338, 346, 352, 355,
 362, 364, 367, 374, 376, 382, 385, 387, 393, 395,
 398, 402, 405, 408, 410, 415, 417, 419, 422, 424,
 427, 431, 434, 435, 437, 440, 443, 447, 450, 453
};

uint16_t tu_cal_watts[TU_CAL_TBL_SIZE] = {
   0,   1,   2,   3,   4,   5,   6,   7,   8,   9,
  10,  15,  20,  25,  30,  45,  55,  62,  70,  77,
  86,  93, 101, 107, 115, 121, 127, 133, 141, 151,
 160, 171, 181, 190, 205, 213, 223, 232, 243, 250,
 276, 292, 323, 351, 375, 391, 418, 442, 467, 488,
 502, 515, 540, 553, 579, 604, 615, 645, 668, 679,
 705, 716, 728, 753, 763, 786, 800, 811, 832, 843,
 855, 876, 887, 898, 909, 930, 940, 951, 962, 973,
 984,1003,1017,1025,1034,1045,1059,1072,1086,1099
};
#endif


int get_watts(uint16_t raw) {
  int exact = 0, idx, min, max;
  int power, diff, rate;

  // beyond the table coverage
  if (raw > tu_cal_adc[TU_CAL_TBL_SIZE - 1]) {
    diff = (raw - tu_cal_adc[TU_CAL_TBL_SIZE - 1]);
#ifdef TU_CAL_USE_INTERNAL    
    return (16*tu_cal_adc[TU_CAL_TBL_SIZE - 1] + diff*53)/16;
#else
    return (16*tu_cal_adc[TU_CAL_TBL_SIZE - 1] + diff*72)/16;
#endif
  }

  // search for the equal or the closest lower value.
  min = 0;
  max = TU_CAL_TBL_SIZE;
  while(1) {
    idx = (min + max)/2;
    if (tu_cal_adc[idx] == raw) {
      exact = 1;
      break;
    } else if (tu_cal_adc[max] == raw) {
      exact = 1;
      idx = max;
      break;
    } else if (tu_cal_adc[min] == raw) {
      exact = 1;
      idx = min;
      break;
    } else if (tu_cal_adc[idx] < raw) {
      if (tu_cal_adc[idx+1] > raw) {
        // found the target
        break;
      } else {
        min = idx;
      }
    } else if (tu_cal_adc[idx] > raw) {
      max = idx;
    }
  }

  if (exact) {
    return tu_cal_watts[idx];
  }

  // interpolate scale up by 16 and down at the end.
  rate = (16*(tu_cal_watts[idx+1] - tu_cal_watts[idx]))/(tu_cal_adc[idx+1] - tu_cal_adc[idx]);
  power = (16*tu_cal_watts[idx] + (raw - tu_cal_adc[idx])*rate)/16;
  
  return power;
}

void update_pwr_swr() {
  int f = 0, r = 0;
  
  for (int i = 0; i < 64; i++) {
    f += analogRead(TU_PWR_FWD);
    r += analogRead(TU_PWR_REF);
  }
  raw_fwd = f/64;
  raw_ref = r/64;

  pwr = get_watts(raw_fwd);
  if (pwr == 0) {
    swr = 100;
    return;
  } else if (raw_fwd < raw_ref) {
    swr = 999;
    return;
  }
  int ref = get_watts(raw_ref);

  // calculate swr scaling factor 128
  double vf = sqrt(pwr*50.0f);
  double vr = sqrt(ref*50.0f);
  swr = (int)(100*(vf+vr)/(vf-vr));
  if (swr < 100)
    swr = 100;
  // save the net power
  pwr = pwr - ref;
  if (pwr_max < pwr)
    pwr_max = pwr; // record the peak power. it's reset after displaying.
}

void setup() {
  // Relay control pins
  // L1 .. L7
  for (int i = 0; i < 7; i++) {
    pinMode(l_pins[i], OUTPUT);
    pinMode(c_pins[i], OUTPUT);
  }

  // network config
  pinMode(TU_NC,  OUTPUT);

  // Analog I/O pins do not need special setup.
  // simply use analogRead().

  Serial.begin(9600);
  Serial.setTimeout(200);

  lcd.init();
  lcd.backlight();

  lcd.setCursor(2, 0);
  lcd.print("Remote Tuner");
  lcd.setCursor(4, 1);
  lcd.print("by K9SUL");
  pwr = 0;
  swr = 100;
  pwr_max = 0;
  last_update = millis();
}

void loop() {
  update_pwr_swr();
  update_lcd();
  
  if (!Serial.available())
    return;
    
  uint8_t len = Serial.readBytes(buff, TU_BUFF_LEN - 1);
  // remove trailing newline chars
  for (; buff[len - 1] == '\n'; len--);
  buff[len] = '\0'; // terminate with null
  if (len < strlen(TU_CMD_MAGIC) + 1) {
    // less than the minimum length of a command
    print_error();
  } else {
    parse_command();
  }
}

void print_error() {
  Serial.println("er000000000");
}

void parse_command() {
  // check for the magic sequence
  for (uint8_t i = 0; i < strlen(TU_CMD_MAGIC); i++) {
    if (magic[i] != buff[i]) {
      print_error();
      return;
    }
  }
  // skip the magic header
  char *c_str = buff + strlen(TU_CMD_MAGIC);

  // get the command
  if (c_str[0] == TU_CMD_SET) {
    if (strlen(c_str) != 8) {
      print_error();
      return;
    }

    // param 1: L value
    char tmp[4];
    int param;
    for (int i = 0; i < 3; i++) {
      tmp[i] = c_str[i + 1];
    }
    tmp[3] = '\0';
    sscanf(tmp, "%d", &param);
    l_val = (uint8_t)param;

    // param 2: C value
    for (int i = 0; i < 3; i++) {
      tmp[i] = c_str[i + 4];
    }
    tmp[3] = '\0';
    sscanf(tmp, "%d", &param);
    c_val = (uint8_t)param;
    
    n_val = (c_str[7] == '0') ? TU_NC_HIZ : TU_NC_LOZ;
    update_lc();
  } else if (c_str[0] == TU_CMD_READ) {
    // do the default response
  } else if (c_str[0] == TU_CMD_PWR) {
    // read power signals.
    sprintf(buff, "ok%04d%04d0", raw_fwd, raw_ref);
    Serial.println(buff);
    return;
  } else if (c_str[0] == TU_CMD_TEST) {
    test_relays();
  } else {
      // unknown command.
      Serial.println("er000000000");
      return;
  }
  // default response
  sprintf(buff, "ok%04d%04d%d", l_val, c_val, n_val == 1 ? 1 : 0);
  Serial.println(buff);
  return;
}

void test_relays() {
  for (uint8_t i = 0; i <128; i++) {
    update_lc(i, 0, true);
    delay(200);
  }

  for (uint8_t i = 0; i <128; i++) {
    update_lc(0, i, false);
    delay(200);
  }
  update_lc();
}

void update_lc() {
  update_lc(l_val, c_val, n_val);
}

void update_lc(uint8_t lval, uint8_t cval, uint8_t nc) {
  update_lcd(lval, cval, nc);

  // L-network config
  digitalWrite(TU_NC, nc);

  // L and C
  for (uint8_t i = 0; i < 7; i++) {
    digitalWrite(l_pins[i], (lval & 0x01));
    digitalWrite(c_pins[i], (cval & 0x01));
    lval >>= 1;
    cval >>= 1;
  }
}

void update_lcd() {
  update_lcd(l_val, c_val, n_val);
}

void update_lcd(uint8_t lval, uint8_t cval, uint8_t nc) {
  if (first) {
    lcd.clear();
    first = false;
  }
  lcd.setCursor(0, 0);
  sprintf(buff, "L=%03d C=%03d %s", lval, cval, (nc == TU_NC_HIZ) ? "HiZ" : "LoZ");
  lcd.print(buff);
  if (last_update + TU_UI_PWR_UPDATE_INTERVAL_MSEC > millis()) {
    // do not update the power, swr yet.
    return;
  }
  lcd.setCursor(0, 1);
  sprintf(buff, "P=%4dW SWR=%1d.%02d", pwr, swr/100, swr%100);
  lcd.print(buff);
  last_update = millis();
  pwr_max = 0;
}
