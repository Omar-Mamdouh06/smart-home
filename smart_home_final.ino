
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <Servo.h>

// LCD I2C: address 0x27, 16 columns, 2 rows
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo doorServo;

// ---------------- PIN CONNECTIONS ----------------

// Security system
const byte windowPin = 2;
const byte buzzerPin = 3;
const byte alarmLedPin = 4;
const byte armButtonPin = 5;

// Keypad rows: D6-D9
// Keypad columns: D10-D13 would conflict with other devices,
// so D0, D1, D10, D11 are used below.

// Door
const byte servoPin = 12;

// Climate
const byte fanPin = 13;
const byte lm35Pin = A3;

// Lighting
const byte lightLedPin = A0;
const byte lightButtonPin = A1;
const byte ldrPin = A2;

// ---------------- KEYPAD ----------------

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {6, 7, 8, 9};
byte colPins[COLS] = {10, 11, 0, 1};

Keypad keypad = Keypad(
  makeKeymap(keys), rowPins, colPins, ROWS, COLS
);

// ---------------- PASSWORDS ----------------

const String securityPassword = "1234";
const String doorPassword = "1234";

String enteredPassword = "";

// ---------------- SYSTEM STATES ----------------

bool securityArmed = true;
bool alarmActive = false;

bool manualLight = false;
bool lightButtonLast = HIGH;

unsigned long lastLightButtonTime = 0;

// Thresholds
const int darknessThreshold = 400;
const float temperatureLimit = 28.0;

// ---------------- SETUP ----------------

void setup() {
  pinMode(windowPin, INPUT_PULLUP);
  pinMode(armButtonPin, INPUT_PULLUP);

  pinMode(buzzerPin, OUTPUT);
  pinMode(alarmLedPin, OUTPUT);
  pinMode(fanPin, OUTPUT);
  pinMode(lightLedPin, OUTPUT);

  pinMode(lightButtonPin, INPUT_PULLUP);

  doorServo.attach(servoPin);
  doorServo.write(0);  // Door closed

  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Smart Home");
  lcd.setCursor(0, 1);
  lcd.print("System Ready");
  delay(1500);
  lcd.clear();
}

// ---------------- MAIN LOOP ----------------

void loop() {
  checkSecurity();
  checkKeypad();
  checkLighting();
  checkTemperature();
  updateAlarm();
}

// ---------------- SECURITY SYSTEM ----------------

void checkSecurity() {
  // Press button to arm/disarm ONLY when no alarm is active
  static bool lastButtonState = HIGH;
  bool buttonState = digitalRead(armButtonPin);

  if (lastButtonState == HIGH &&
      buttonState == LOW &&
      !alarmActive) {
    securityArmed = !securityArmed;

    lcd.clear();
    lcd.print(securityArmed ? "Security Armed" :
                              "Security Off");
    delay(700);
  }

  lastButtonState = buttonState;

  // LOW means the window switch has been activated
  if (securityArmed && digitalRead(windowPin) == LOW) {
    alarmActive = true;
  }
}

void updateAlarm() {
  if (alarmActive) {
    digitalWrite(buzzerPin, HIGH);
    digitalWrite(alarmLedPin, HIGH);
  } else {
    digitalWrite(buzzerPin, LOW);
    digitalWrite(alarmLedPin, LOW);
  }
}

// ---------------- KEYPAD ----------------

void checkKeypad() {
  char key = keypad.getKey();

  if (key == NO_KEY) {
    return;
  }

  if (key == '*') {
    enteredPassword = "";
    lcd.clear();
    lcd.print("Password cleared");
    delay(500);
    return;
  }

  if (key == '#') {
    checkPassword();
    enteredPassword = "";
    return;
  }

  // Accept numeric keys only
  if (key >= '0' && key <= '9' &&
      enteredPassword.length() < 8) {
    enteredPassword += key;

    lcd.clear();
    lcd.print("Password:");
    lcd.setCursor(0, 1);

    for (unsigned int i = 0;
         i < enteredPassword.length(); i++) {
      lcd.print('*');
    }
  }
}

void checkPassword() {
  if (alarmActive) {
    if (enteredPassword == securityPassword) {
      alarmActive = false;
      securityArmed = false;

      digitalWrite(buzzerPin, LOW);
      digitalWrite(alarmLedPin, LOW);

      lcd.clear();
      lcd.print("Alarm Stopped");
      lcd.setCursor(0, 1);
      lcd.print("Security Off");
      delay(1000);
    } else {
      lcd.clear();
      lcd.print("Wrong Password");
      delay(800);
    }
    return;
  }

  // Door password
  if (enteredPassword == doorPassword) {
    lcd.clear();
    lcd.print("Door Opening");

    doorServo.write(90);
    delay(2000);

    doorServo.write(0);

    lcd.clear();
    lcd.print("Door Closed");
    delay(500);
  } else {
    lcd.clear();
    lcd.print("Wrong Password");
    delay(800);
  }
}

// ---------------- LIGHTING ----------------

void checkLighting() {
  bool buttonState = digitalRead(lightButtonPin);

  // Manual toggle button
  if (lightButtonLast == HIGH &&
      buttonState == LOW &&
      millis() - lastLightButtonTime > 250) {
    manualLight = !manualLight;
    lastLightButtonTime = millis();
  }

  lightButtonLast = buttonState;

  int lightLevel = analogRead(ldrPin);

  // LED turns on if it is dark OR manual mode is enabled
  if (lightLevel < darknessThreshold || manualLight) {
    digitalWrite(lightLedPin, HIGH);
  } else {
    digitalWrite(lightLedPin, LOW);
  }
}

// ---------------- CLIMATE CONTROL ----------------

void checkTemperature() {
  int sensorValue = analogRead(lm35Pin);

  // LM35: 10 mV per degree Celsius, Arduino reference 5V
  float voltage = sensorValue * (5.0 / 1023.0);
  float temperature = voltage * 100.0;

  digitalWrite(fanPin,
               temperature >= temperatureLimit
               ? HIGH : LOW);
}
