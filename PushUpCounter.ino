#include <Wire.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <SD.h>

// OLED setup with U8g2 (page buffer mode)
U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// HC-SR04 Pins
#define trigPin 6
#define echoPin 9

// Button Pins
#define modePin 3
#define incrmnt 4
#define dcrmnt 5
#define PUSHUP_THRESHOLD 12.0  // Fixed threshold in cm

// SD Card
const int chipSelect = 10;
bool sdAvailable = false;

enum Mode { PUSHUP, CUSTOM, MODE_SELECT };
Mode currentMode = MODE_SELECT;

int setRepCount = 3, setSetCount = 3;
int setCount = 0, totalSets = 0, counter = 0;
unsigned long plankTime = 0;

bool timing = false;
float plankDistance = 20.0;  // Default value
const float marginPercent = 0.15;

float lowerThreshold = 14.0;
float upperThreshold = 25.0;
float maxUpperLimit = 50.0;
float minLowerLimit = 10.0;

enum State { WAITING, GOING_DOWN, GOING_UP };
State exerciseState = WAITING;

float getDistance() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  return pulseIn(echoPin, HIGH, 30000) * 0.034 / 2.0;
}

float getStableDistance() {
  float sum = 0;
  int validSamples = 0;

  for (byte i = 0; i < 10; i++) {
    float d = getDistance();
    if (d >= 2 && d <= 100) {
      sum += d;
      validSamples++;
    }
    delay(10);
  }

  if (validSamples >= 5) {
    return sum / validSamples;
  } else {
    return -1;
  }
}

void displayText(const char* line1, const char* line2, const char* line3) {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 12, line1);
    u8g2.drawStr(0, 30, line2);
    u8g2.drawStr(0, 48, line3);
  } while (u8g2.nextPage());
}

void waitForSelection(const char* title, int& variable) {
  bool selected = false;
  while (!selected) {
    char valueStr[10];
    itoa(variable, valueStr, 10);
    displayText(title, valueStr, "");
    delay(200);
    if (!digitalRead(incrmnt)) {
      variable++;
      while (digitalRead(incrmnt)) delay(10);
      delay(300);
    }
    if (!digitalRead(dcrmnt) && variable > 0) {
      variable--;
      while (digitalRead(dcrmnt)) delay(10);
      delay(300);
    }
    if (!digitalRead(modePin)) {
      while (digitalRead(modePin)) delay(10);
      selected = true;
    }
  }
}

void showCountdown(int seconds, const char* message) {
  displayText(message, "Rest", "");
  delay(seconds * 1000);
}

void trackPushUp() {
  while (setCount < setSetCount) {
    if (setCount > 0) showCountdown(10, "Rest");
    counter = 0;
    exerciseState = WAITING;

    while (counter < setRepCount) {
      float d = getStableDistance();

      switch (exerciseState) {
        case WAITING:
          if (d > upperThreshold) exerciseState = GOING_DOWN;
          break;
        case GOING_DOWN:
          if (d < lowerThreshold) exerciseState = GOING_UP;
          break;
        case GOING_UP:
          if (d > upperThreshold) {
            counter++;
            char countStr[12];
            sprintf(countStr, "Push:%d", counter);
            char setStr[10];
            sprintf(setStr, "S:%d", setCount + 1);
            displayText(countStr, setStr, "");
            exerciseState = WAITING;
            delay(800);
          }
          break;
      }

      delay(50);
    }

    setCount++;
    totalSets++;
    char setStr[10];
    sprintf(setStr, "S:%d", setCount);
    displayText(setStr, "done", "");
    delay(1000);
  }
}

void trackCustom() {
  setCount = 0;
  unsigned long setStart, setEnd;
  char logEntry[64];

  while (setCount < setSetCount) {
    if (setCount > 0) showCountdown(10, "Rest");

    char setStr[20];
    sprintf(setStr, "Set %d Start", setCount + 1);
    displayText("Custom", setStr, "");
    delay(1000);

    setStart = millis();
    while (digitalRead(modePin) && digitalRead(incrmnt) && digitalRead(dcrmnt)) {
      unsigned long now = millis();
      unsigned long seconds = (now - setStart) / 1000;
      char timeStr[16];
      sprintf(timeStr, "Time: %lus", seconds);
      displayText("Custom", setStr, timeStr);
      delay(200);
    }
    setEnd = millis();

    unsigned long duration = (setEnd - setStart) / 1000;
    if (sdAvailable) {
      File dataFile = SD.open("w.txt", FILE_WRITE);
      if (dataFile) {
        sprintf(logEntry, "[CUSTOM] [Set:%d] [Time:%lus]", setCount + 1, duration);
        dataFile.println(logEntry);
        dataFile.close();
      }
    }

    char doneStr[20];
    sprintf(doneStr, "Set %d Done", setCount + 1);
    displayText("Custom", doneStr, "");
    delay(1000);

    setCount++;
  }

  displayText("Custom", "Workout Done", "");
  delay(2000);
}

void selectMode() {
  byte modeIndex = 0;
  const char* modes[] = {"Push", "Custom"};
  bool modeSelected = false;

  while (!modeSelected) {
    displayText("Mode", modes[modeIndex], "");
    delay(200);
    if (!digitalRead(modePin)) {
      modeIndex = (modeIndex + 1) % 2;
      while (digitalRead(modePin)) delay(10);
      delay(300);
    }
    if (!digitalRead(incrmnt)) {
      while (digitalRead(incrmnt)) delay(10);
      modeSelected = true;
    }
  }

  currentMode = (Mode)modeIndex;

  waitForSelection("Sets", setSetCount);
  waitForSelection("Reps", setRepCount);
}

void reset() {
  setCount = totalSets = counter = plankTime = 0;
  timing = false;
  currentMode = MODE_SELECT;
}

void setup() {
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(modePin, INPUT_PULLUP);
  pinMode(incrmnt, INPUT_PULLUP);
  pinMode(dcrmnt, INPUT_PULLUP);

  u8g2.begin();
  displayText("OLED", "Ready", "");
  delay(1000);

  if (!SD.begin(chipSelect)) {
    displayText("SD", "Init fail", "");
    delay(2000);
    sdAvailable = false;
  } else {
    displayText("SD", "OK", "");
    delay(1000);
    sdAvailable = true;
  }
}

void loop() {
  if (currentMode == MODE_SELECT) {
    selectMode();
  } else {
    if (currentMode == PUSHUP) {
      trackPushUp();
    } else if (currentMode == CUSTOM) {
      trackCustom();
    }
    displayText("Done", "Workout", "");
    delay(2000);
    reset();
  }
}