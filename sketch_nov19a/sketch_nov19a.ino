#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ================= I2C =================
const byte PCF_ADDRESS = 0x38;
const byte LCD_ADDRESS = 0x27;

// ================= PINS =================
const byte windowPin = 2;
const byte buzzerPin = 3;
const byte alarmLedPin = 4;
const byte armButtonPin = 5;
const byte servoPin = 6;
const byte fanPin = 7;
const byte motionPin = 8;
const byte exteriorLightPin = 9;
const byte indoorLightPin = 10;
const byte manualButtonPin = 11;

const byte ldrPin = A0;
const byte lm35Pin = A1;
const byte potPin = A2;

// ================= SETTINGS =================
const byte WINDOW_OPEN_LEVEL = HIGH;
const byte MOTION_ACTIVE_LEVEL = HIGH;

const byte CLOSED_ANGLE = 0;
const byte OPEN_ANGLE = 90;

const int LIGHT_THRESHOLD = 400;
const float ADC_REFERENCE_VOLTS = 5.0f;

const unsigned long DOOR_TIME = 2000;
const unsigned long DEBOUNCE_TIME = 50;
const unsigned long PASSWORD_TIMEOUT = 15000;

// ================= PASSWORDS =================
const char DOOR_PASSWORD[] = "1234";
const char SECURITY_PASSWORD[] = "2580";

// ================= KEYPAD =================
const byte rowBits[4] = {7, 6, 5, 4};
const byte colBits[4] = {3, 2, 1, 0};

const char keys[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

// ================= LCD / SERVO =================
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);
Servo doorServo;

// ================= DEBOUNCING =================
struct DebouncedInput {
  byte pin;
  bool raw;
  bool stable;
  unsigned long changedAt;

  void begin(byte number, byte mode) {
    pin = number;
    pinMode(pin, mode);

    raw = digitalRead(pin);
    stable = raw;
    changedAt = millis();
  }

  bool update(unsigned long now) {
    bool reading = digitalRead(pin);

    if (reading != raw) {
      raw = reading;
      changedAt = now;
    }

    if (raw != stable &&
        now - changedAt >= DEBOUNCE_TIME) {
      stable = raw;
      return true;
    }

    return false;
  }
};

DebouncedInput armButton;
DebouncedInput manualButton;
DebouncedInput windowContact;

// ================= SYSTEM STATE =================
bool securityArmed = true;
bool alarmActive = false;

bool doorOpen = false;
bool manualLight = false;

bool motionDetected = false;
bool fanOn = false;

bool indoorOn = false;
bool exteriorOn = false;

bool keypadOK = false;
bool lcdOK = false;

bool previousWindowOpen = false;

int lightValue = 0;

float temperature = 0;
float setTemperature = 20;

unsigned long doorOpenedAt = 0;
unsigned long lastSensors = 0;
unsigned long lastDisplay = 0;
unsigned long lastDiagnostic = 0;
unsigned long lastPCFRetry = 0;

// ================= PASSWORD STATE =================
char password[5] = "";
byte passwordLength = 0;
bool passwordOverflow = false;

unsigned long lastPasswordInput = 0;

// ================= KEYPAD STATE =================
char candidate = 0;
bool keyReleased = false;

unsigned long candidateChangedAt = 0;
unsigned long lastScan = 0;

// ================= LCD STATE =================
char notice[17] = "Security ON";
unsigned long noticeStarted = 0;

char previousLines[2][17] = {{0}};

// ================= CLEAR PASSWORD =================
void clearPassword() {
  passwordLength = 0;
  password[0] = '\0';
  passwordOverflow = false;
}

// ================= LCD MESSAGE =================
void showNotice(const char *text) {
  strncpy(notice, text, 16);
  notice[16] = '\0';
  noticeStarted = millis();
}

// ================= I2C =================
bool acknowledgeI2C(byte address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool writePCF(byte value) {
  Wire.beginTransmission(PCF_ADDRESS);
  Wire.write(value);

  return Wire.endTransmission() == 0;
}

// ================= KEYPAD SCAN =================
char scanKeypad() {
  char result = 0;
  byte count = 0;

  for (byte row = 0; row < 4; row++) {
    byte pattern =
      (byte)(0xFF & ~(1 << rowBits[row]));

    if (!writePCF(pattern)) {
      return '?';
    }

    delayMicroseconds(100);

    if (Wire.requestFrom(PCF_ADDRESS, (byte)1) != 1) {
      writePCF(0xFF);
      return '?';
    }

    byte value = Wire.read();

    for (byte col = 0; col < 4; col++) {
      if ((value & (1 << colBits[col])) == 0) {
        result = keys[row][col];
        count++;
      }
    }
  }

  if (!writePCF(0xFF)) {
    return '?';
  }

  if (count > 1) {
    return '!';
  }

  return result;
}

// ================= PASSWORD CHECK =================
void submitPassword() {
  if (passwordLength != 4 || passwordOverflow) {
    showNotice("Need 4 digits");
  }

  else if (strcmp(password, SECURITY_PASSWORD) == 0) {
    securityArmed = false;
    alarmActive = false;

    digitalWrite(buzzerPin, LOW);
    digitalWrite(alarmLedPin, LOW);

    showNotice("Alarm reset/OFF");

    Serial.println(F("Alarm reset; security OFF"));
  }

  else if (strcmp(password, DOOR_PASSWORD) == 0) {
    doorServo.write(OPEN_ANGLE);

    doorOpenedAt = millis();
    doorOpen = true;

    showNotice("Door opened");

    Serial.println(F("Door OPEN"));
  }

  else {
    showNotice("Wrong password");

    Serial.println(F("Wrong password"));
  }

  clearPassword();
}

// ================= HANDLE KEY =================
void handleKey(char key) {
  if (key == '*') {
    clearPassword();
    showNotice("Input cleared");
  }

  else if (key == '#') {
    submitPassword();
  }

  else if (key >= '0' && key <= '9') {
    lastPasswordInput = millis();
    notice[0] = '\0';

    if (passwordLength < 4) {
      password[passwordLength] = key;
      passwordLength++;

      password[passwordLength] = '\0';
    }

    else {
      passwordOverflow = true;
      showNotice("Too long: use *");
    }
  }
}

// ================= KEYPAD UPDATE =================
void updateKeypad(unsigned long now) {
  if (now - lastScan < 15) {
    return;
  }

  lastScan = now;

  if (!keypadOK) {
    if (now - lastPCFRetry < 1000) {
      return;
    }

    lastPCFRetry = now;

    if (!writePCF(0xFF)) {
      return;
    }

    keypadOK = true;
    candidate = '?';
    keyReleased = false;
    candidateChangedAt = now;

    Serial.println(F("Keypad connected"));
  }

  char current = scanKeypad();

  if (current == '?') {
    keypadOK = false;
    lastPCFRetry = now;
    keyReleased = false;

    clearPassword();

    Serial.println(F("Keypad I2C error"));
    return;
  }

  if (current == '!') {
    keyReleased = false;
  }

  if (current != candidate) {
    candidate = current;
    candidateChangedAt = now;
  }

  if (now - candidateChangedAt < 40) {
    return;
  }

  if (current == 0) {
    keyReleased = true;
  }

  else if (current != '!' && keyReleased) {
    keyReleased = false;
    handleKey(current);
  }
}

// ================= SECURITY / BUTTONS =================
void updateSecurityAndButtons(unsigned long now) {
  windowContact.update(now);

  if (armButton.update(now) &&
      armButton.stable == LOW) {

    securityArmed = !securityArmed;

    clearPassword();

    previousWindowOpen =
      (windowContact.stable == WINDOW_OPEN_LEVEL);

    showNotice(
      securityArmed ? "Security ON" : "Security OFF"
    );

    Serial.println(
      securityArmed ? F("Security ON") : F("Security OFF")
    );
  }

  if (manualButton.update(now) &&
      manualButton.stable == LOW) {

    manualLight = !manualLight;

    showNotice(
      manualLight ? "Indoor forced ON" : "Indoor AUTO"
    );
  }

  bool windowOpenNow =
    (windowContact.stable == WINDOW_OPEN_LEVEL);

  // Alarm only on a closed-to-open transition while armed.
  if (securityArmed &&
      windowOpenNow &&
      !previousWindowOpen &&
      !alarmActive) {

    alarmActive = true;

    clearPassword();
    showNotice("Window ALARM!");

    Serial.println(F("Window alarm latched"));
  }

  previousWindowOpen = windowOpenNow;

  // IR sensor: HIGH means an object is detected.
  motionDetected =
    (digitalRead(motionPin) == MOTION_ACTIVE_LEVEL);
}

// ================= SENSORS =================
void updateSensors(unsigned long now) {
  if (now - lastSensors < 250) {
    return;
  }

  lastSensors = now;

  // LM35
  analogRead(lm35Pin);

  uint16_t sum = 0;

  for (byte i = 0; i < 8; i++) {
    sum += analogRead(lm35Pin);
  }

  float average = sum / 8.0f;

  temperature =
    average * (ADC_REFERENCE_VOLTS / 1024.0f) * 100.0f;

  // Potentiometer
  analogRead(potPin);

  int potValue = analogRead(potPin);

  setTemperature =
    20.0f + potValue * (15.0f / 1023.0f);

  fanOn = temperature > setTemperature;

  // LDR
  analogRead(ldrPin);
  lightValue = analogRead(ldrPin);
}

// ================= OUTPUTS =================
void updateOutputs(unsigned long now) {
  if (doorOpen &&
      now - doorOpenedAt >= DOOR_TIME) {

    doorServo.write(CLOSED_ANGLE);
    doorOpen = false;

    Serial.println(F("Door CLOSED"));
  }

  indoorOn =
    (lightValue < LIGHT_THRESHOLD) || manualLight;

  exteriorOn =
    securityArmed && motionDetected;

  digitalWrite(buzzerPin, alarmActive ? HIGH : LOW);
  digitalWrite(alarmLedPin, alarmActive ? HIGH : LOW);

  digitalWrite(indoorLightPin, indoorOn ? HIGH : LOW);
  digitalWrite(exteriorLightPin, exteriorOn ? HIGH : LOW);

  digitalWrite(fanPin, fanOn ? HIGH : LOW);
}

// ================= LCD LINE =================
void writeLCDLine(byte row, const char *text) {
  char line[17];

  memset(line, ' ', 16);
  line[16] = '\0';

  for (byte i = 0; i < 16 && text[i]; i++) {
    line[i] = text[i];
  }

  if (strcmp(line, previousLines[row]) != 0) {
    lcd.setCursor(0, row);
    lcd.print(line);

    memcpy(previousLines[row], line, 17);
  }
}

// ================= LCD UPDATE =================
void updateLCD(unsigned long now) {
  if (!lcdOK || now - lastDisplay < 250) {
    return;
  }

  lastDisplay = now;

  if (!acknowledgeI2C(LCD_ADDRESS)) {
    lcdOK = false;
    Serial.println(F("LCD disconnected"));
    return;
  }

  char top[17];
  char bottom[17];

  if (alarmActive) {
    snprintf(
      top,
      sizeof(top),
      "ALARM D:%c S:%s",
      doorOpen ? 'O' : 'C',
      securityArmed ? "ON" : "OFF"
    );
  }

  else {
    snprintf(
      top,
      sizeof(top),
      "D:%s S:%s",
      doorOpen ? "OPEN" : "CLOSED",
      securityArmed ? "ON" : "OFF"
    );
  }

  if (notice[0] &&
      now - noticeStarted < 1500) {

    strcpy(bottom, notice);
  }

  else if (passwordLength > 0) {
    char stars[5] = "";

    memset(stars, '*', passwordLength);
    stars[passwordLength] = '\0';

    snprintf(
      bottom,
      sizeof(bottom),
      "Code:%s #=OK",
      stars
    );
  }

  else if (!keypadOK) {
    strcpy(bottom, "Keypad I2C error");
  }

  else {
    char temperatureText[8];
    char setpointText[8];

    dtostrf(temperature, 4, 1, temperatureText);
    dtostrf(setTemperature, 4, 1, setpointText);

    snprintf(
      bottom,
      sizeof(bottom),
      "T:%.5s S:%.4s",
      temperatureText,
      setpointText
    );
  }

  writeLCDLine(0, top);
  writeLCDLine(1, bottom);
}

// ================= SERIAL DIAGNOSTICS =================
void printDiagnostics(unsigned long now) {
  if (now - lastDiagnostic < 1000) {
    return;
  }

  lastDiagnostic = now;

  Serial.print(F("SEC="));
  Serial.print(securityArmed);

  Serial.print(F(" ALARM="));
  Serial.print(alarmActive);

  Serial.print(F(" WINDOW="));
  Serial.print(windowContact.stable);

  Serial.print(F(" MOTION="));
  Serial.print(digitalRead(motionPin));

  Serial.print(F(" LDR="));
  Serial.print(lightValue);

  Serial.print(F(" TEMP="));
  Serial.print(temperature, 1);

  Serial.print(F(" SET="));
  Serial.print(setTemperature, 1);

  Serial.print(F(" FAN="));
  Serial.println(fanOn);
}

// ================= SETUP =================
void setup() {
  Serial.begin(9600);

  Wire.begin();
  Wire.setClock(100000);
  Wire.setWireTimeout(3000, true);

  pinMode(buzzerPin, OUTPUT);
  pinMode(alarmLedPin, OUTPUT);
  pinMode(fanPin, OUTPUT);
  pinMode(exteriorLightPin, OUTPUT);
  pinMode(indoorLightPin, OUTPUT);

  digitalWrite(buzzerPin, LOW);
  digitalWrite(alarmLedPin, LOW);
  digitalWrite(fanPin, LOW);
  digitalWrite(exteriorLightPin, LOW);
  digitalWrite(indoorLightPin, LOW);

  armButton.begin(armButtonPin, INPUT);
  manualButton.begin(manualButtonPin, INPUT_PULLUP);
  windowContact.begin(windowPin, INPUT_PULLUP);

  pinMode(motionPin, INPUT);

  doorServo.write(CLOSED_ANGLE);
  doorServo.attach(servoPin);

  keypadOK = writePCF(0xFF);
  lcdOK = acknowledgeI2C(LCD_ADDRESS);

  if (lcdOK) {
    lcd.init();
    lcd.backlight();
  }

  armButton.begin(armButtonPin, INPUT);
  manualButton.begin(manualButtonPin, INPUT_PULLUP);
  windowContact.begin(windowPin, INPUT_PULLUP);

  previousWindowOpen =
    (windowContact.stable == WINDOW_OPEN_LEVEL);

  lastSensors = millis() - 250UL;
  updateSensors(millis());

  noticeStarted = millis();
  lastDisplay = millis() - 250UL;

  updateOutputs(millis());
  updateLCD(millis());

  Serial.println(F("Smart Home System Started"));
  Serial.println(F("Door password: 1234#"));
  Serial.println(F("Alarm reset password: 2580#"));

  Serial.println(
    keypadOK ? F("PCF 0x38 OK") : F("PCF NOT responding")
  );

  Serial.println(
    lcdOK ? F("LCD 0x27 OK") : F("LCD NOT responding")
  );
}

// ================= MAIN LOOP =================
void loop() {
  unsigned long now = millis();

  updateSecurityAndButtons(now);

  updateKeypad(millis());

  now = millis();

  if ((passwordLength || passwordOverflow) &&
      now - lastPasswordInput >= PASSWORD_TIMEOUT) {

    clearPassword();
    showNotice("Input timed out");
  }

  updateSensors(now);

  updateOutputs(millis());

  updateLCD(millis());

  printDiagnostics(millis());
}