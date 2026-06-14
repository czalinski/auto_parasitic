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
Adafruit_SSD1306 display0(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire,  -1);  // Wire  (SDA=21, SCL=22)
Adafruit_SSD1306 display1(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, -1);  // Wire1 (SDA=25, SCL=26)

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
// Shunt resistance in milliohms per channel; 0 = no cal file found.
// Files: /cal_W0_0x40.txt etc., written by the esp32cal firmware.
float cal_mohm0[4] = {0};
float cal_mohm1[4] = {0};

static String calPath(int bus, int i) {
  char buf[32];
  snprintf(buf, sizeof(buf), "/cal_W%d_0x%02X.txt", bus, inaAddrs[i]);
  return String(buf);
}

static void loadCalFiles() {
  for (int i = 0; i < 4; i++) {
    for (int bus = 0; bus < 2; bus++) {
      float& slot = (bus == 0) ? cal_mohm0[i] : cal_mohm1[i];
      String path = calPath(bus, i);
      if (!SD.exists(path.c_str())) continue;
      File f = SD.open(path.c_str());
      if (!f) continue;
      String s = f.readStringUntil('\n');
      f.close();
      s.trim();
      float v = s.toFloat();
      if (v > 0) {
        slot = v;
        Serial.printf("[CAL] Loaded %s = %.3f mOhm\n", path.c_str(), v);
      }
    }
  }
}

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
    f.print("time_ok,timestamp");
    for (int i = 0; i < 4; i++) f.printf(",W0_0x%02X_mA", inaAddrs[i]);
    for (int i = 0; i < 4; i++) f.printf(",W1_0x%02X_mA", inaAddrs[i]);
    f.println();
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
  // Empty field = missing or uncalibrated — Excel/pandas treat as null
  for (int i = 0; i < 4; i++) { f.print(","); if (mA0[i] >= 0) f.print(mA0[i]); }
  for (int i = 0; i < 4; i++) { f.print(","); if (mA1[i] >= 0) f.print(mA1[i]); }
  f.println();
  f.close();
}

// ---------------- OLED ----------------
static void drawToDisplay(Adafruit_SSD1306& d, int busId, const char* sdStatus, const char* btStatus, int mA0[4], int mA1[4]) {
  d.clearDisplay();
  d.setTextSize(1);
  d.setTextColor(SSD1306_WHITE);

  d.setCursor(0, 0);  d.printf("%d %s", busId, sdStatus);
  d.setCursor(60, 0); d.print(btStatus);

  for (int i = 0; i < 4; i++) {
    int y = 16 + i * 12;
    d.setCursor(0, y);
    if      (mA0[i] == -9999) d.print("----");
    else if (mA0[i] == -9998) d.print("NOCL");
    else                      d.printf("%4d", mA0[i]);
    d.setCursor(60, y);
    if      (mA1[i] == -9999) d.print("----");
    else if (mA1[i] == -9998) d.print("NOCL");
    else                      d.printf("%4d", mA1[i]);
  }
  d.display();
}

void drawGrid(const char* sdStatus, const char* btStatus, int mA0[4], int mA1[4]) {
  drawToDisplay(display0, 0, sdStatus, btStatus, mA0, mA1);
  drawToDisplay(display1, 1, sdStatus, btStatus, mA0, mA1);
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
  if (!display0.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    Serial.println("[OLED0] Not found at 0x3C on Wire");
  else {
    Serial.println("[OLED0] OK");
    display0.clearDisplay(); display0.display();
  }
  if (!display1.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    Serial.println("[OLED1] Not found at 0x3C on Wire1");
  else {
    Serial.println("[OLED1] OK");
    display1.clearDisplay(); display1.display();
  }

  // INA219 — report which are present
  for (int i = 0; i < 4; i++) {
    ina0[i] = Adafruit_INA219(inaAddrs[i]);
    ina1[i] = Adafruit_INA219(inaAddrs[i]);
    present0[i] = ina0[i].begin(&Wire);
    present1[i] = ina1[i].begin(&Wire1);
    Serial.printf("[INA219] 0x%02X  Wire=%s  Wire1=%s\n",
      inaAddrs[i], present0[i] ? "OK" : "--", present1[i] ? "OK" : "--");
  }

  // SD — one attempt, non-blocking
  sdOK = SD.begin(SD_CS);
  if (sdOK) {
    Serial.println("[SD] OK");
    loadCalFiles();
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

  // Read all present INA219s.
  // Sentinels: -9999 = sensor absent, -9998 = present but no cal file.
  // I(mA) = V_shunt(mV) * 1000 / R_shunt(mΩ)
  for (int i = 0; i < 4; i++) {
    if (!present0[i])       mA0[i] = -9999;
    else if (!cal_mohm0[i]) mA0[i] = -9998;
    else mA0[i] = (int)round(fabsf(ina0[i].getShuntVoltage_mV()) * 1000.0f / cal_mohm0[i]);

    if (!present1[i])       mA1[i] = -9999;
    else if (!cal_mohm1[i]) mA1[i] = -9998;
    else mA1[i] = (int)round(fabsf(ina1[i].getShuntVoltage_mV()) * 1000.0f / cal_mohm1[i]);
  }

  // BT time sync — hasClient() must always be called so the library state
  // machine can detect disconnects and accept new connections.
  if (SerialBT.hasClient()) {
    while (SerialBT.available()) {
      char c = (char)SerialBT.read();
      if (c == '\n') {
        btBuffer.trim();
        long ts = btBuffer.toInt();
        if (ts > 0) {
          struct timeval tv = { (time_t)ts, 0 };
          settimeofday(&tv, nullptr);
          SerialBT.println("OK");
          // Send CSV header so Pi knows column layout for this session
          SerialBT.print("time_ok,timestamp");
          for (int i = 0; i < 4; i++) SerialBT.printf(",W0_0x%02X_mA", inaAddrs[i]);
          for (int i = 0; i < 4; i++) SerialBT.printf(",W1_0x%02X_mA", inaAddrs[i]);
          SerialBT.println();
          if (!timeSynced) {
            timeSynced = true;
            Serial.println("[BT] Time synced — starting new log file");
            startNewLogFile();
          }
        }
        btBuffer = "";
      } else {
        btBuffer += c;
      }
    }

    // Stream current sample to Pi every 10s — fire-and-forget, SD card is authoritative
    static unsigned long lastBTSend = 0;
    if (timeSynced && millis() - lastBTSend >= 10000) {
      lastBTSend = millis();
      char btTs[32];
      fillTime("%Y-%m-%d %H:%M:%S", btTs, sizeof(btTs));
      SerialBT.printf("1,%s", btTs);
      for (int i = 0; i < 4; i++) { SerialBT.print(","); if (mA0[i] >= 0) SerialBT.print(mA0[i]); }
      for (int i = 0; i < 4; i++) { SerialBT.print(","); if (mA1[i] >= 0) SerialBT.print(mA1[i]); }
      SerialBT.println();
    }
  }

  // Every 5s: re-scan both I2C buses and re-probe any missing INA219s
  static unsigned long lastI2CScan = 0;
  if (millis() - lastI2CScan >= 5000) {
    lastI2CScan = millis();

    // Full bus scan
    for (int bus = 0; bus < 2; bus++) {
      TwoWire& w = bus == 0 ? Wire : Wire1;
      bool anyFound = false;
      for (byte addr = 1; addr < 127; addr++) {
        w.beginTransmission(addr);
        if (w.endTransmission() == 0) {
          Serial.printf("[I2C] Wire%d 0x%02X\n", bus, addr);
          anyFound = true;
        }
      }
      if (!anyFound) Serial.printf("[I2C] Wire%d: nothing found\n", bus);
    }

    // Re-probe missing INA219s
    for (int i = 0; i < 4; i++) {
      if (!present0[i]) {
        present0[i] = ina0[i].begin(&Wire);
        Serial.printf("[INA] Wire  0x%02X: %s\n", inaAddrs[i], present0[i] ? "now found" : "absent");
      }
      if (!present1[i]) {
        present1[i] = ina1[i].begin(&Wire1);
        Serial.printf("[INA] Wire1 0x%02X: %s\n", inaAddrs[i], present1[i] ? "now found" : "absent");
      }
    }
  }

  // SD retry — throttled to every 3s
  if (!sdOK) {
    static unsigned long lastSDRetry = 0;
    if (millis() - lastSDRetry >= 3000) {
      lastSDRetry = millis();
      sdOK = SD.begin(SD_CS);
      Serial.printf("[SD] begin() returned %s\n", sdOK ? "true" : "false");
      if (sdOK) {
        Serial.println("[SD] Card ready — starting log file");
        loadCalFiles();
        startNewLogFile();
      }
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
