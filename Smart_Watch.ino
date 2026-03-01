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
#define BUTTON_PIN 1

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
LSM6DS3 myIMU(I2C_MODE, 0x6A);

// BLE Setup
BLEService watchService("1805");
BLEService batteryService("180F");
BLEUnsignedCharCharacteristic batteryLevelChar("2A19", BLERead | BLENotify);
BLEFloatCharacteristic batteryVoltageChar("2A1A", BLERead | BLENotify);
BLEStringCharacteristic timeChar("2A2B", BLERead | BLEWrite, 20);
BLEStringCharacteristic notifyChar("2A46", BLERead | BLEWrite, 40);
BLEStringCharacteristic callChar("2A47", BLERead | BLEWrite, 20);
BLEStringCharacteristic dateChar("2A27", BLERead | BLEWrite, 20);
BLEStringCharacteristic weatherChar("2A24", BLERead | BLEWrite, 40);

String currentDate = "Waiting...";
String currentTemp = "--";

//button variables
unsigned long pressStartTime = 0;
bool isPressing = false;
const int LONG_PRESS_THRESHOLD = 1000;  // 2 seconds
bool longPressTriggered = false;

// Time & System variables
int hours = 0, minutes = 0, seconds = 0;
unsigned long lastTick = 0;
bool timeSet = false;
bool bleConnected = false;
unsigned long lastUpdate = 0;
const long updateInterval = 30000;
int percentage = 0;

// --- LOGIC VARIABLES ---
enum DisplayMode { MODE_IMU,
                   MODE_ALWAYS_ON };
DisplayMode currentMode = MODE_IMU;

bool menuActive = false;
int tempSelection = 0;
unsigned long lastPressTime = 0;
const long autoSelectDelay = 2000;

// --- LOGIC VARIABLES ---
enum WatchFace { WATCH_FACE_1,
                 WATCH_FACE_2,
                 WATCH_FACE_3 };
WatchFace currentFace = WATCH_FACE_1;

bool WatchFace_menuActive = false;
int WatchFace_tempSelection = 0;
unsigned long WatchFace_lastPressTime = 0;
const long WatchFace_autoSelectDelay = 2000;

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
  pinMode(BUTTON_PIN, INPUT_PULLUP);
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
    BLE.setLocalName("XIAO-Watch");
    BLE.setAdvertisedService(watchService);
    watchService.addCharacteristic(timeChar);
    watchService.addCharacteristic(dateChar);
    watchService.addCharacteristic(weatherChar);
    watchService.addCharacteristic(notifyChar);
    watchService.addCharacteristic(callChar);
    BLE.addService(watchService);

    // Setup Battery Service
    batteryService.addCharacteristic(batteryLevelChar);
    batteryService.addCharacteristic(batteryVoltageChar);  // New characteristic
    BLE.addService(batteryService);

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

  // --- 1. BUTTON & MENU LOGIC ---
  int buttonState = digitalRead(BUTTON_PIN);

  // 1. Detection of Press (Logic LOW)
  if (buttonState == LOW && !isPressing) {
    pressStartTime = millis();
    isPressing = true;
    longPressTriggered = false;
  }

  // 2. Detection of LONG PRESS WHILE HOLDING (1 Second)
  if (buttonState == LOW && isPressing && !longPressTriggered) {
    if (millis() - pressStartTime >= 1000) {  // Trigger at 1 second hold
      if (!WatchFace_menuActive && !menuActive) {
        WatchFace_menuActive = true;
        WatchFace_tempSelection = (currentFace == WATCH_FACE_1) ? 0 : 1;
        if (!screenOn) wakeScreen();
        startVibration(100);
      }
      longPressTriggered = true;  // Prevents this from firing again until released
      WatchFace_lastPressTime = millis();
    }
  }

  // 3. Detection of Release (For Short Press and Cleanup)
  if (buttonState == HIGH && isPressing) {
    unsigned long duration = millis() - pressStartTime;
    isPressing = false;

    // Only handle short press if the long press hasn't already fired
    if (!longPressTriggered) {
      if (duration > 50) {  // Normal Short Press
        if (!menuActive && !WatchFace_menuActive) {
          menuActive = true;
          tempSelection = (currentMode == MODE_IMU) ? 0 : 1;
          if (!screenOn) wakeScreen();
          startVibration(100);
        } else if (menuActive) {
          tempSelection = (tempSelection + 1) % 2;
          startVibration(50);
        } else if (WatchFace_menuActive) {
          WatchFace_tempSelection = (WatchFace_tempSelection + 1) % 3;
          startVibration(50);
        }
        lastPressTime = millis();
      }
    }
  }

  // --- 2. AUTO-SELECTION TIMERS ---
  if (menuActive && (millis() - lastPressTime >= autoSelectDelay)) {
    currentMode = (tempSelection == 0) ? MODE_IMU : MODE_ALWAYS_ON;
    menuActive = false;
    lastMoveTime = millis();
  }

  if (WatchFace_menuActive && (millis() - WatchFace_lastPressTime >= WatchFace_autoSelectDelay)) {
    if (WatchFace_tempSelection == 0) currentFace = WATCH_FACE_1;
    else if (WatchFace_tempSelection == 1) currentFace = WATCH_FACE_2;
    else currentFace = WATCH_FACE_3;  // Added third case

    WatchFace_menuActive = false;
    lastMoveTime = millis();
  }

  // --- 2. PERIODIC BATTERY UPDATE ---
  if (millis() - lastUpdate >= updateInterval) {
    lastUpdate = millis();
    updateBatteryData();
  }

  // --- 3. MOTION & DISPLAY POWER LOGIC ---
  if (!menuActive && !WatchFace_menuActive) {
    if (currentMode == MODE_ALWAYS_ON) {
      if (!screenOn) wakeScreen();
    } else {
      float x = myIMU.readFloatAccelX();
      float y = myIMU.readFloatAccelY();
      float z = myIMU.readFloatAccelZ();

      if (abs(x - lastX) > 0.15 || abs(y - lastY) > 0.15 || abs(z - lastZ) > 0.15) {
        if (!screenOn) wakeScreen();
        lastMoveTime = millis();
      }
      lastX = x;
      lastY = y;
      lastZ = z;

      if (screenOn && (millis() - lastMoveTime > sleepTimeout) && !isCalling && !isShowingMsg) {
        screenOn = false;
        display.oled_command(SH110X_DISPLAYOFF);
      }
    }
  }

  // --- 4. BLE HANDLING ---
  BLEDevice central = BLE.central();
  if (central) {
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
        startVibration(1000);
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
        startVibration(3000);
        wakeScreen();
      }
    }  //--------------------------
    // 2. Date Update
    if (dateChar.written()) {
      currentDate = dateChar.value();
    }

    // 3. Weather Update (Parses "25C,Clear Sky")
    if (weatherChar.written()) {
      String val = weatherChar.value();
      int commaIndex = val.indexOf(',');
      String tempRaw = (commaIndex > 0) ? val.substring(0, commaIndex) : val;

      // Clean up the string: remove 'C', 'c', and any extra spaces
      tempRaw.replace("C", "");
      tempRaw.replace("c", "");
      tempRaw.trim();
      currentTemp = tempRaw;
    }  //--------------------------
  } else {
    bleConnected = false;
    isCalling = false;
    callerID = "";
  }

  // --- 5. CLOCK LOGIC ---
  if (timeSet && millis() - lastTick >= 1000) {
    lastTick += 1000;
    seconds++;
    if (seconds >= 60) {
      seconds = 0;
      minutes++;
    }
    if (minutes >= 60) {
      minutes = 0;
      hours++;
    }
    if (hours >= 24) { hours = 0; }
  }

  // --- 6. MESSAGE QUEUE LOGIC ---
  if (msgCount > 0 && !isShowingMsg) {
    isShowingMsg = true;
    msgStartTime = millis();
  }
  if (isShowingMsg && (millis() - msgStartTime >= msgVisibleTime)) {
    isShowingMsg = false;
    for (int i = 0; i < msgCount - 1; i++) msgQueue[i] = msgQueue[i + 1];
    msgCount--;
  }


  // --- 7. DISPLAY LOGIC ---
  if (screenOn) {
    display.clearDisplay();
    display.setTextColor(SH110X_WHITE);

    if (WatchFace_menuActive) {
      drawWatchFaceMenu();
    } else if (menuActive) {
      drawMenu();
    } else if (isCalling && callerID != "") {
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
      // Show the selected Watch Face
      if (currentFace == WATCH_FACE_1) face1();
      else if (currentFace == WATCH_FACE_2) face2();
      else face3();  // Call the new face here
    }
    display.display();  // Now properly refreshes for all cases
  }
}

// --- HELPERS ---

void startVibration(int durationMs) {
  digitalWrite(MOTOR_PIN, HIGH);
  motorEndTime = millis() + durationMs;
  isVibrating = true;
}

void updateBatteryData() {
  // 1. Tell the MCU to use high-resolution (12-bit)
  analogReadResolution(12);

  // 2. Read the "shrunk" voltage from the pin (0 to 4095)
  int rawADC = analogRead(BAT_READ_PIN);

  // 3. Multiply it back!
  // (rawADC * 3.3 / 4095.0) calculates the voltage at the PIN (max 1.4V)
  // Multiplying by 3.0 stretches it back to the BATTERY voltage (max 4.2V)
  float instantVolts = (rawADC * 3.3 / 4095.0) * 3.0;

  // 4. Smooth out the readings so the percentage doesn't jump around
  movingAvgVolts = (movingAvgVolts * 0.8) + (instantVolts * 0.2);

  // 5. Calculate Percentage
  // 3.2V is usually "dead" for a LiPo, 4.2V is "100%"
  percentage = map(constrain(movingAvgVolts * 100, 320, 420), 320, 420, 0, 100);

  // --- SEND TO PHONE ---
  if (BLE.connected()) {
    batteryLevelChar.writeValue((byte)percentage);
    batteryVoltageChar.writeValue(movingAvgVolts);
  }

  Serial.print("Real Battery Voltage: ");
  Serial.print(movingAvgVolts);
  Serial.println("V");
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

void drawWatchFaceMenu() {
  display.setTextSize(1);
  display.setCursor(30, 5);
  display.println("WATCH FACE");
  display.drawFastHLine(0, 15, 128, SH110X_WHITE);

  display.setCursor(15, 25);
  if (WatchFace_tempSelection == 0) display.print("> Face 1");
  else display.print("  Face 1");

  display.setCursor(15, 37);
  if (WatchFace_tempSelection == 1) display.print("> Face 2");
  else display.print("  Face 2");

  display.setCursor(15, 49);
  if (WatchFace_tempSelection == 2) display.print("> Face 3");
  else display.print("  Face 3");

  int barWidth = map(millis() - WatchFace_lastPressTime, 0, WatchFace_autoSelectDelay, 128, 0);
  display.fillRect(0, 60, barWidth, 4, SH110X_WHITE);
}

void face1() {
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
  display.println(timeSet ? "SYSTEM ACTIVE" : "WAITING...");

  display.setTextSize(1);
  display.setCursor(105, 55);
  display.print(percentage);
  display.print("%");

  // Battery Bar
  int barW = 18;
  int barH = 7;
  int barX = (128 - barW) / 2;
  display.drawRect(barX, 1, barW, barH, SH110X_WHITE);
  display.drawRect(barX + barW, 3, 1, 3, SH110X_WHITE);
  int fillWidth = map(percentage, 0, 100, 0, barW - 4);
  if (fillWidth > 0) display.fillRect(barX + 2, 3, fillWidth, barH - 4, SH110X_WHITE);
}

void face2() {
  // Full Face: Time + Weather
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);

  // --- DATE ---
  display.setTextSize(1);
  display.setCursor(5, 42);
  display.print(currentDate);

  // --- WEATHER (Format: "25.C") ---
  display.setCursor(5, 54);
  display.print("Temp: ");
  display.print(currentTemp);
  // Draw small degree circle manually
  int x = display.getCursorX();
  int y = display.getCursorY();
  display.fillRect(x, y, 2, 2, SH110X_WHITE);
  display.setCursor(x + 5, 54);
  display.print("C");  // Using a simple dot as the degree separator

  int displayHours = hours % 12;
  if (displayHours == 0) displayHours = 12;
  String ampm = (hours >= 12) ? "PM" : "AM";

  display.setTextSize(2);
  display.setCursor(10, 14);
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
}

void face3() {
  display.clearDisplay();
  // ===== WEATHER (Top Left) =====
  int weatherType = 0;  // 0=Sun, 1=Cloud, 2=Rain

  if (weatherType == 0)
    drawSun(16, 12);
  else if (weatherType == 1)
    drawCloud(4, 10);
  else
    drawRain(4, 10);


  // ===== BATTERY (Left Middle) =====
  drawBattery(2, 26, percentage);
  // ===== TIME (Perfect Center) =====
  display.setTextSize(4);
  int displayHours = hours % 12;
  if (displayHours == 0) displayHours = 12;
  // Centered X for 2 digits at size 4
  int timeX = 40;
  // Hours (Top Half)
  display.setCursor(timeX, 0);
  if (displayHours < 10) display.print("0");
  display.print(displayHours);

  // Minutes (Bottom Half)
  display.setCursor(timeX, 32);
  if (minutes < 10) display.print("0");
  display.print(minutes);


  // ===== AM / PM (Right Middle) =====
  bool isPM = (hours >= 12);
  display.setTextSize(1);
  int ampmX = 102;

  // AM
  if (!isPM) {
    display.fillRect(ampmX - 2, 18, 22, 12, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  } else {
    display.setTextColor(SH110X_WHITE);
  }
  display.setCursor(ampmX, 20);
  display.print("AM");

  // PM
  if (isPM) {
    display.fillRect(ampmX - 2, 34, 22, 12, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
  } else {
    display.setTextColor(SH110X_WHITE);
  }
  display.setCursor(ampmX, 36);
  display.print("PM");
  display.setTextColor(SH110X_WHITE);
  display.display();
}

// ===== Weather Functions =====
void drawSun(int x, int y) {
  // Sun body
  display.fillCircle(x, y, 5, SH110X_WHITE);
  // Rays
  display.drawLine(x, y - 9, x, y - 6, SH110X_WHITE);
  display.drawLine(x, y + 6, x, y + 9, SH110X_WHITE);
  display.drawLine(x - 9, y, x - 6, y, SH110X_WHITE);
  display.drawLine(x + 6, y, x + 9, y, SH110X_WHITE);
  display.drawLine(x - 6, y - 6, x - 3, y - 3, SH110X_WHITE);
  display.drawLine(x + 6, y - 6, x + 3, y - 3, SH110X_WHITE);
  display.drawLine(x - 6, y + 6, x - 3, y + 3, SH110X_WHITE);
  display.drawLine(x + 6, y + 6, x + 3, y + 3, SH110X_WHITE);
}
void drawCloud(int x, int y) {
  display.fillCircle(x, y, 4, SH110X_WHITE);
  display.fillCircle(x + 6, y - 2, 5, SH110X_WHITE);
  display.fillCircle(x + 12, y, 4, SH110X_WHITE);
  display.fillRect(x, y, 16, 6, SH110X_WHITE);
}

void drawRain(int x, int y) {
  drawCloud(x, y);
  display.drawLine(x + 3, y + 8, x + 3, y + 12, SH110X_WHITE);
  display.drawLine(x + 8, y + 8, x + 8, y + 12, SH110X_WHITE);
  display.drawLine(x + 13, y + 8, x + 13, y + 12, SH110X_WHITE);
}

void drawBattery(int x, int y, int percentage) {
  // Outer border
  display.drawRect(x, y, 26, 14, SH110X_WHITE);
  // Tip
  display.fillRect(x + 26, y + 4, 3, 6, SH110X_WHITE);
  // Clear inside
  display.fillRect(x + 1, y + 1, 24, 12, SH110X_BLACK);
  // Battery level bar (bottom part)
  int barWidth = map(percentage, 0, 100, 0, 22);
  display.fillRect(x + 2, y + 9, barWidth, 3, SH110X_WHITE);
  // Percentage text
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(x + 5, y + 2);
  display.print(percentage);
  display.print("%");
}

void drawMenu() {
  display.setTextSize(1);
  display.setCursor(35, 5);
  display.println("SETTINGS");
  display.drawFastHLine(0, 15, 128, SH110X_WHITE);
  display.setCursor(15, 30);
  if (tempSelection == 0) display.print("> ");
  else display.print("  ");
  display.print("IMU Wake");
  display.setCursor(15, 45);
  if (tempSelection == 1) display.print("> ");
  else display.print("  ");
  display.print("Always On");
  // Timer Bar
  int barWidth = map(millis() - lastPressTime, 0, autoSelectDelay, 128, 0);
  display.fillRect(0, 60, barWidth, 4, SH110X_WHITE);
}
