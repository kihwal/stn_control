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

#define TU_PWR_FWD 19
#define TU_PWR_REF 18

#define TU_NC_HIZ 0
#define TU_NC_LOZ 1
#define TU_BUFF_LEN 16

uint8_t l_val = 0; // current L value 0..127
uint8_t c_val = 0; // current C value 0..127
uint8_t n_val = TU_NC_HIZ; // current network config. t/f
char buff[TU_BUFF_LEN]; // The longest command or response is 14 bytes.
char magic[] = TU_CMD_MAGIC;
uint8_t l_pins[] = {TU_L1, TU_L2, TU_L3, TU_L4, TU_L5, TU_L6, TU_L7};
uint8_t c_pins[] = {TU_C1, TU_C2, TU_C3, TU_C4, TU_C5, TU_C6, TU_C7};
boolean first = true;
LiquidCrystal_I2C lcd(0x27, 16, 2);

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
}

void loop() {
  unsigned long start_time = millis();
  while(Serial.available() == 0) {
    // wait for data. Backlight times out in 10 min.
    if (millis() - start_time > 180000) {
      lcd.noBacklight();
    }
  }
  // wake up if necessary.
  start_time = millis();
  lcd.backlight();
  
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
    update_lc(l_val, c_val, n_val);
  } else if (c_str[0] == TU_CMD_READ) {
    // do the default response
  } else if (c_str[0] == TU_CMD_PWR) {
    // read power signals.
    // 10-bit resolution gives 0..1024.
    int f_pwr = analogRead(TU_PWR_FWD);
    int r_pwr = analogRead(TU_PWR_REF);
    sprintf(buff, "ok%04d%04d0", f_pwr, r_pwr);
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

void update_lcd(uint8_t lval, uint8_t cval, uint8_t nc) {
  if (first) {
    lcd.clear();
    first = false;
  }
  lcd.setCursor(1, 0);
  sprintf(buff, "L=%03d C=%03d ", lval, cval);
  lcd.print(buff);
  lcd.setCursor(1, 1);
  lcd.print((nc == TU_NC_HIZ) ? "HI-Z" : "Lo-Z");

}
