#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <BluetoothSerial.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_INA219.h>
#include <sys/time.h>
#include <time.h>

// ---------------- OLED ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, -1);

// ---------------- INA219 ----------------
uint8_t inaAddrs[4] = {0x40, 0x41, 0x44, 0x45};
Adafruit_INA219 ina0[4];   // Wire  (SDA=21, SCL=22)
Adafruit_INA219 ina1[4];   // Wire1 (SDA=25, SCL=26)
bool present0[4] = {false};
bool present1[4] = {false};

// ---------------- SD CARD ----------------
#define SD_CS 5
bool sdOK = false;
String logPath = "";
String currentHour = "";

// ---------------- BLUETOOTH / TIME ----------------
BluetoothSerial SerialBT;
bool timeSynced = false;
String btBuffer = "";

// ---------------- CALIBRATION ----------------
float cal_expected = 15.0;
float cal_measured0[4] = {15.0, 15.0, 15.0, 15.0};
float cal_measured1[4] = {15.0, 15.0, 15.0, 15.0};
float cal_factor0[4];
float cal_factor1[4];

// ---------------- HELPERS ----------------
static void fillTime(const char* fmt, char* buf, size_t len) {
  time_t now = time(nullptr);
  strftime(buf, len, fmt, localtime(&now));
}

void startNewLogFile() {
  char fname[40];
  if (timeSynced) {
    fillTime("/log_%Y%m%d_%H%M%S.csv", fname, sizeof(fname));
    char hour[16];
    fillTime("%Y%m%d_%H", hour, sizeof(hour));
    currentHour = String(hour);
  } else {
    snprintf(fname, sizeof(fname), "/NOSYNC_%lu.csv", millis());
    currentHour = "";
  }
  logPath = String(fname);

  File f = SD.open(logPath, FILE_WRITE);
  if (f) {
    f.println("time_ok,timestamp,b0c0_mA,b0c1_mA,b0c2_mA,b0c3_mA,b1c0_mA,b1c1_mA,b1c2_mA,b1c3_mA");
    f.close();
    Serial.printf("[SD] New log: %s\n", logPath.c_str());
  } else {
    Serial.printf("[SD] Failed to create: %s\n", logPath.c_str());
  }
}

void writeRow(int mA0[4], int mA1[4]) {
  if (!sdOK || logPath.isEmpty()) return;

  char ts[32];
  if (timeSynced) {
    fillTime("%Y-%m-%d %H:%M:%S", ts, sizeof(ts));
  } else {
    snprintf(ts, sizeof(ts), "boot+%lums", millis());
  }

  File f = SD.open(logPath, FILE_WRITE);
  if (!f) {
    Serial.println("[SD] Write failed — will retry init");
    sdOK = false;
    return;
  }
  f.print(timeSynced ? 1 : 0);
  f.print(",");
  f.print(ts);
  // Empty field (nothing between commas) = missing sample — Excel/pandas treat as null
  for (int i = 0; i < 4; i++) { f.print(","); if (mA0[i] != -9999) f.print(mA0[i]); }
  for (int i = 0; i < 4; i++) { f.print(","); if (mA1[i] != -9999) f.print(mA1[i]); }
  f.println();
  f.close();
}

// ---------------- OLED ----------------
void drawGrid(const char* sdStatus, const char* btStatus, int mA0[4], int mA1[4]) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print(sdStatus);
  display.setCursor(60, 0);
  display.print(btStatus);

  for (int i = 0; i < 4; i++) {
    int y = 16 + i * 12;
    display.setCursor(0, y);
    if (mA0[i] == -9999) display.print("    ");
    else                  display.printf("%4d", mA0[i]);
    display.setCursor(60, y);
    if (mA1[i] == -9999) display.print("    ");
    else                  display.printf("%4d", mA1[i]);
  }
  display.display();
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);
  Wire1.begin(25, 26);

  // I2C scan — diagnostic, remove once hardware is confirmed
  Serial.println("Scanning Wire (SDA=21, SCL=22)...");
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0)
      Serial.printf("  Found: 0x%02X\n", addr);
  }
  Serial.println("Scanning Wire1 (SDA=25, SCL=26)...");
  for (byte addr = 1; addr < 127; addr++) {
    Wire1.beginTransmission(addr);
    if (Wire1.endTransmission() == 0)
      Serial.printf("  Found: 0x%02X\n", addr);
  }

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("[OLED] Not found at 0x3C");
  } else {
    Serial.println("[OLED] OK");
    display.clearDisplay();
    display.display();
  }

  // INA219 — report which are present
  for (int i = 0; i < 4; i++) {
    ina0[i] = Adafruit_INA219(inaAddrs[i]);
    ina1[i] = Adafruit_INA219(inaAddrs[i]);
    present0[i] = ina0[i].begin(&Wire);
    present1[i] = ina1[i].begin(&Wire1);
    Serial.printf("[INA219] 0x%02X  Wire=%s  Wire1=%s\n",
      inaAddrs[i], present0[i] ? "OK" : "--", present1[i] ? "OK" : "--");
    cal_factor0[i] = cal_expected / cal_measured0[i];
    cal_factor1[i] = cal_expected / cal_measured1[i];
  }

  // SD — one attempt, non-blocking
  sdOK = SD.begin(SD_CS);
  if (sdOK) {
    Serial.println("[SD] OK");
    startNewLogFile();
  } else {
    Serial.println("[SD] Not found — will retry in loop");
  }

  // BT — advertise and return immediately
  SerialBT.begin("ESP32_Logger");
  Serial.println("[BT] Advertising as ESP32_Logger");
}

// ---------------- LOOP ----------------
void loop() {
  int mA0[4], mA1[4];

  // Read all present INA219s; -9999 = sensor not detected (sentinel)
  for (int i = 0; i < 4; i++) {
    mA0[i] = present0[i] ? (int)round(ina0[i].getCurrent_mA() * cal_factor0[i]) : -9999;
    mA1[i] = present1[i] ? (int)round(ina1[i].getCurrent_mA() * cal_factor1[i]) : -9999;
  }

  // BT time sync — non-blocking, accumulate chars as they arrive
  if (!timeSynced && SerialBT.hasClient()) {
    while (SerialBT.available()) {
      char c = (char)SerialBT.read();
      if (c == '\n') {
        btBuffer.trim();
        long ts = btBuffer.toInt();
        if (ts > 0) {
          struct timeval tv = { (time_t)ts, 0 };
          settimeofday(&tv, nullptr);
          SerialBT.println("OK");
          timeSynced = true;
          Serial.println("[BT] Time synced — starting new log file");
          startNewLogFile();
        }
        btBuffer = "";
      } else {
        btBuffer += c;
      }
    }
  }

  // SD retry
  if (!sdOK) {
    sdOK = SD.begin(SD_CS);
    if (sdOK) {
      Serial.println("[SD] Card ready — starting log file");
      startNewLogFile();
    }
  }

  // Hourly rotation (only when time is trusted)
  if (sdOK && timeSynced) {
    char nowHour[16];
    fillTime("%Y%m%d_%H", nowHour, sizeof(nowHour));
    if (String(nowHour) != currentHour) {
      startNewLogFile();
    }
  }

  writeRow(mA0, mA1);

  const char* sdStr = sdOK      ? "SD OK"   : "SD Bad";
  const char* btStr = timeSynced ? "Time OK" :
                      SerialBT.hasClient() ? "BT Conn" : "BT Wait";
  drawGrid(sdStr, btStr, mA0, mA1);

  delay(500);
}
