#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <ArduinoBLE.h>
#include <LSM6DS3.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

#define MOTOR_PIN 10  // GPIO 10 connected to transistor base
#define BAT_READ_PIN PIN_VBAT
#define BAT_ENABLE_PIN PIN_VBAT_ENABLE
#define CHARGE_SPEED_PIN P0_13

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
LSM6DS3 myIMU(I2C_MODE, 0x6A);

// BLE Setup
BLEService watchService("1805");
BLEStringCharacteristic timeChar("2A2B", BLERead | BLEWrite, 20);
BLEStringCharacteristic notifyChar("2A46", BLERead | BLEWrite, 40);
BLEStringCharacteristic callChar("2A47", BLERead | BLEWrite, 20);

// Time & System variables
int hours = 0, minutes = 0, seconds = 0;
unsigned long lastTick = 0;
bool timeSet = false;
bool bleConnected = false;
unsigned long lastUpdate = 0;
const long updateInterval = 30000; 
int percentage = 0;

// Battery & Motion
float movingAvgVolts = 3.7;
bool screenOn = true;
unsigned long lastMoveTime = 0;
const long sleepTimeout = 10000;
float lastX, lastY, lastZ;

// Notification & Call
String msgQueue[3];
int msgCount = 0;
unsigned long msgStartTime = 0;
const long msgVisibleTime = 5000;
bool isShowingMsg = false;
String callerID = "";
bool isCalling = false;

// Vibration Variables (Non-blocking)
unsigned long motorEndTime = 0;
bool isVibrating = false;

// Function Prototypes
void updateBatteryData();
void wakeScreen();
void updateTimeFromString(String input);
void startVibration(int durationMs);

void setup() {
  Serial.begin(115200);
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);

  // Power on display pins
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  digitalWrite(4, HIGH);
  digitalWrite(5, HIGH);
  delay(100);

  pinMode(CHARGE_SPEED_PIN, OUTPUT);
  digitalWrite(CHARGE_SPEED_PIN, LOW);

  pinMode(BAT_ENABLE_PIN, OUTPUT);
  digitalWrite(BAT_ENABLE_PIN, LOW);

  Wire.begin();
  display.begin(0x3C, true);
  display.setTextWrap(true);

  if (myIMU.begin() != 0) Serial.println("IMU error");

  if (BLE.begin()) {
    BLE.setLocalName("XIAO-Watch-Jerit");
    BLE.setAdvertisedService(watchService);
    watchService.addCharacteristic(timeChar);
    watchService.addCharacteristic(notifyChar);
    watchService.addCharacteristic(callChar);
    BLE.addService(watchService);
    BLE.advertise();
  }
  updateBatteryData();
}

void loop() {
  // --- 0. MOTOR OVERSEER (Non-blocking) ---
  if (isVibrating && millis() >= motorEndTime) {
    digitalWrite(MOTOR_PIN, LOW);
    isVibrating = false;
  }

  // --- 1. PERIODIC BATTERY UPDATE ---
  if (millis() - lastUpdate >= updateInterval) {
    lastUpdate = millis();
    updateBatteryData();
  }

  // --- 2. MOTION DETECTION (Wrist Wake) ---
  float x = myIMU.readFloatAccelX();
  float y = myIMU.readFloatAccelY();
  float z = myIMU.readFloatAccelZ();

  if (abs(x - lastX) > 0.15 || abs(y - lastY) > 0.15 || abs(z - lastZ) > 0.15) {
    if (!screenOn) wakeScreen();
    lastMoveTime = millis();
  }
  lastX = x; lastY = y; lastZ = z;

  if (screenOn && (millis() - lastMoveTime > sleepTimeout) && !isCalling && !isShowingMsg) {
    screenOn = false;
    display.oled_command(SH110X_DISPLAYOFF);
  }

  // --- 3. BLE HANDLING ---
  BLEDevice central = BLE.central();
  if (central) {
    // Vibrate 0.5s on initial connection
    if (!bleConnected) {
      bleConnected = true;
      startVibration(500); 
    }

    if (timeChar.written()) updateTimeFromString(timeChar.value());

    if (notifyChar.written()) {
      String val = notifyChar.value();
      val.trim();
      if (val != "CLR" && val != "" && msgCount < 3) {
        msgQueue[msgCount] = val;
        msgCount++;
        startVibration(1000); // 1s Vibrate for notification
        wakeScreen();
      }
    }

    if (callChar.written()) {
      String val = callChar.value();
      val.trim();
      if (val == "CLR" || val == "") {
        isCalling = false;
        callerID = "";
      } else {
        callerID = val;
        isCalling = true;
        startVibration(3000); // 3s Vibrate for call
        wakeScreen();
      }
    }
  } else {
    bleConnected = false;
    isCalling = false;
    callerID = "";
  }

  // --- 4. CLOCK LOGIC ---
  if (timeSet && millis() - lastTick >= 1000) {
    lastTick += 1000;
    seconds++;
    if (seconds >= 60) { seconds = 0; minutes++; }
    if (minutes >= 60) { minutes = 0; hours++; }
    if (hours >= 24) { hours = 0; }
  }

  // --- 5. MESSAGE QUEUE LOGIC ---
  if (msgCount > 0 && !isShowingMsg) {
    isShowingMsg = true;
    msgStartTime = millis();
  }
  if (isShowingMsg && (millis() - msgStartTime >= msgVisibleTime)) {
    isShowingMsg = false;
    for (int i = 0; i < msgCount - 1; i++) msgQueue[i] = msgQueue[i + 1];
    msgCount--;
  }

  // --- 6. DISPLAY LOGIC ---
  if (screenOn) {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);

    if (isCalling && callerID != "") {
      display.setTextSize(1);
      display.setCursor(30, 5);
      display.println("INCOMING CALL");
      display.println("---------------------");
      display.setCursor(0, 30);
      display.setTextSize(2);
      display.println(callerID);
    } else if (isShowingMsg) {
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("NOTIFICATION");
      display.println("---------------------");
      display.setCursor(0, 22);
      display.println(msgQueue[0]);
    } else {
      // Main UI
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print(movingAvgVolts, 2);
      display.print("V");
      display.setCursor(85, 0);
      display.print(bleConnected ? "BLE:ON" : "BLE:OFF");

      int displayHours = hours % 12;
      if (displayHours == 0) displayHours = 12;
      String ampm = (hours >= 12) ? "PM" : "AM";

      display.setTextSize(2);
      display.setCursor(5, 25);
      if (displayHours < 10) display.print("0");
      display.print(displayHours);
      display.print(":");
      if (minutes < 10) display.print("0");
      display.print(minutes);
      display.print(":");
      if (seconds < 10) display.print("0");
      display.print(seconds);
      display.setTextSize(1);
      display.print(" ");
      display.print(ampm);

      display.setCursor(0, 55);
      display.println(timeSet ? "SYSTEM ACTIVE" : "WAITING FOR SYNC...");

      // Battery Bar
      int barW = 18; int barH = 7;
      int barX = (128 - barW) / 2;
      display.drawRect(barX, 1, barW, barH, SH110X_WHITE);
      display.drawRect(barX + barW, 3, 1, 3, SH110X_WHITE);
      int fillWidth = map(percentage, 0, 100, 0, barW - 4);
      if (fillWidth > 0) display.fillRect(barX + 2, 3, fillWidth, barH - 4, SH110X_WHITE);
    }
    display.display();
  }
}

// --- HELPERS ---

void startVibration(int durationMs) {
  digitalWrite(MOTOR_PIN, HIGH);
  motorEndTime = millis() + durationMs;
  isVibrating = true;
}

void updateBatteryData() {
  int rawADC = analogRead(BAT_READ_PIN);
  float instantVolts = (rawADC * 3.0 / 1024.0) * (1510.0 / 510.0);
  movingAvgVolts = (movingAvgVolts * 0.8) + (instantVolts * 0.2);
  percentage = map(constrain(movingAvgVolts * 100, 320, 420), 320, 420, 0, 100);
}

void wakeScreen() {
  screenOn = true;
  display.oled_command(SH110X_DISPLAYON);
  lastMoveTime = millis();
}

void updateTimeFromString(String input) {
  input.trim();
  if (input.length() >= 8) {
    hours = input.substring(0, 2).toInt();
    minutes = input.substring(3, 5).toInt();
    seconds = input.substring(6, 8).toInt();
    timeSet = true;
    lastTick = millis();
  }
}