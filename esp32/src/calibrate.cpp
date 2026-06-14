#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_INA219.h>

// ---- Hardware matches main.cpp ----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SD_CS 5

Adafruit_SSD1306 display0(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire,  -1);
Adafruit_SSD1306 display1(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire1, -1);

uint8_t inaAddrs[4] = {0x40, 0x41, 0x44, 0x45};
Adafruit_INA219 ina0[4];
Adafruit_INA219 ina1[4];
bool present0[4] = {false};
bool present1[4] = {false};

bool sdOK = false;

// ---- Calibration state ----
// Channels: [0..3] = Wire0, [4..7] = Wire1
enum CalState { CAL_IDLE, CAL_SETTLING, CAL_MEASURING, CAL_DONE };

struct ChannelCal {
    CalState state = CAL_IDLE;
    unsigned long stateStart = 0;
    double sum = 0;
    int count = 0;
    float result_mohm = 0;
};

ChannelCal chans[8];

// Minimum shunt voltage to treat as "1A applied".
// At 1A through 5mΩ (smallest spec'd shunt) → 5mV.  3mV gives margin vs noise.
static const float DETECT_MV   = 3.0f;
static const unsigned long SETTLE_MS  = 3000;
static const unsigned long MEASURE_MS = 30000;

// ---- Helpers ----
static inline int idx(int bus, int i) { return bus * 4 + i; }

static String calPath(int bus, int i) {
    char buf[32];
    snprintf(buf, sizeof(buf), "/cal_W%d_0x%02X.txt", bus, inaAddrs[i]);
    return String(buf);
}

static float readShuntMV(int bus, int i) {
    float v = (bus == 0) ? (present0[i] ? ina0[i].getShuntVoltage_mV() : 0.0f)
                         : (present1[i] ? ina1[i].getShuntVoltage_mV() : 0.0f);
    return v < 0 ? -v : v;  // accept either polarity
}

// ---- SD: load existing calibration files at startup ----
static void loadCalFiles() {
    for (int bus = 0; bus < 2; bus++) {
        for (int i = 0; i < 4; i++) {
            String path = calPath(bus, i);
            if (!SD.exists(path.c_str())) continue;
            File f = SD.open(path.c_str());
            if (!f) continue;
            String s = f.readStringUntil('\n');
            f.close();
            s.trim();
            float v = s.toFloat();
            if (v > 0) {
                int c = idx(bus, i);
                chans[c].result_mohm = v;
                chans[c].state = CAL_DONE;
                Serial.printf("[CAL] Loaded %s = %.3f mOhm\n", path.c_str(), v);
            }
        }
    }
}

static void writeCalFile(int bus, int i, float mohm) {
    String path = calPath(bus, i);
    File f = SD.open(path.c_str(), FILE_WRITE);
    if (f) {
        f.printf("%.3f\n", mohm);
        f.close();
        Serial.printf("[CAL] Wrote %s = %.3f mOhm\n", path.c_str(), mohm);
    } else {
        Serial.printf("[CAL] Failed to write %s\n", path.c_str());
    }
}

// ---- Per-channel state machine (called every loop) ----
static void processChannel(int bus, int i) {
    int c = idx(bus, i);
    if (chans[c].state == CAL_DONE) return;

    // Skip sensors not on the bus
    if (bus == 0 && !present0[i]) return;
    if (bus == 1 && !present1[i]) return;

    float mv = readShuntMV(bus, i);
    unsigned long now = millis();

    switch (chans[c].state) {
        case CAL_IDLE:
            if (mv >= DETECT_MV) {
                chans[c].state = CAL_SETTLING;
                chans[c].stateStart = now;
                Serial.printf("[CAL] W%d 0x%02X detected %.2f mV — settling\n",
                              bus, inaAddrs[i], mv);
            }
            break;

        case CAL_SETTLING:
            if (mv < DETECT_MV) {
                chans[c].state = CAL_IDLE;
                Serial.printf("[CAL] W%d 0x%02X lost current during settle\n", bus, inaAddrs[i]);
            } else if (now - chans[c].stateStart >= SETTLE_MS) {
                chans[c].state = CAL_MEASURING;
                chans[c].stateStart = now;
                chans[c].sum = 0;
                chans[c].count = 0;
                Serial.printf("[CAL] W%d 0x%02X measuring...\n", bus, inaAddrs[i]);
            }
            break;

        case CAL_MEASURING:
            if (mv < DETECT_MV) {
                // Current removed before measurement window closed; restart
                chans[c].state = CAL_IDLE;
                chans[c].sum = 0;
                chans[c].count = 0;
                Serial.printf("[CAL] W%d 0x%02X lost current — reset\n", bus, inaAddrs[i]);
            } else {
                chans[c].sum += mv;
                chans[c].count++;
                if (now - chans[c].stateStart >= MEASURE_MS) {
                    float mohm = (float)(chans[c].sum / chans[c].count);
                    // At 1A: R(mΩ) = V(mV) / I(A) = V(mV) / 1
                    chans[c].result_mohm = mohm;
                    chans[c].state = CAL_DONE;
                    if (sdOK) writeCalFile(bus, i, mohm);
                    Serial.printf("[CAL] W%d 0x%02X DONE: %.3f mOhm (%d samples)\n",
                                  bus, inaAddrs[i], mohm, chans[c].count);
                }
            }
            break;

        case CAL_DONE:
            break;
    }
}

// ---- Display ----
static void drawDisplay(Adafruit_SSD1306& d, int busId, const char* sdStr) {
    d.clearDisplay();
    d.setTextSize(1);
    d.setTextColor(SSD1306_WHITE);

    // Header: bus label, SD status
    d.setCursor(0, 0);
    d.printf("%d CAL %s", busId, sdStr);

    // Column headers
    d.setCursor(0,  8);  d.print("  Bus0");
    d.setCursor(64, 8);  d.print("  Bus1");

    // 4 rows: left = Wire0 channels, right = Wire1 channels
    for (int i = 0; i < 4; i++) {
        int y = 20 + i * 11;

        // Left column: Wire0
        d.setCursor(0, y);
        int c0 = idx(0, i);
        if (!present0[i]) {
            d.print(" --");
        } else {
            switch (chans[c0].state) {
                case CAL_DONE:    d.printf("%.1f", chans[c0].result_mohm); break;
                case CAL_IDLE:    d.print("----"); break;
                default:          d.print("####"); break;
            }
        }

        // Right column: Wire1
        d.setCursor(64, y);
        int c1 = idx(1, i);
        if (!present1[i]) {
            d.print(" --");
        } else {
            switch (chans[c1].state) {
                case CAL_DONE:    d.printf("%.1f", chans[c1].result_mohm); break;
                case CAL_IDLE:    d.print("----"); break;
                default:          d.print("####"); break;
            }
        }
    }

    d.display();
}

// ---- Setup / Loop ----
void setup() {
    Serial.begin(115200);
    Serial.println("[CAL] Calibration mode starting");

    Wire.begin(21, 22);
    Wire1.begin(25, 26);

    if (!display0.begin(SSD1306_SWITCHCAPVCC, 0x3C))
        Serial.println("[OLED0] Not found");
    else { display0.clearDisplay(); display0.display(); }

    if (!display1.begin(SSD1306_SWITCHCAPVCC, 0x3C))
        Serial.println("[OLED1] Not found");
    else { display1.clearDisplay(); display1.display(); }

    for (int i = 0; i < 4; i++) {
        ina0[i] = Adafruit_INA219(inaAddrs[i]);
        ina1[i] = Adafruit_INA219(inaAddrs[i]);
        present0[i] = ina0[i].begin(&Wire);
        present1[i] = ina1[i].begin(&Wire1);
        Serial.printf("[INA219] 0x%02X  Wire=%s  Wire1=%s\n",
                      inaAddrs[i], present0[i] ? "OK" : "--", present1[i] ? "OK" : "--");
    }

    sdOK = SD.begin(SD_CS);
    Serial.printf("[SD] %s\n", sdOK ? "OK" : "Not found");
    if (sdOK) loadCalFiles();
}

void loop() {
    // SD retry
    if (!sdOK) {
        static unsigned long lastRetry = 0;
        if (millis() - lastRetry >= 3000) {
            lastRetry = millis();
            sdOK = SD.begin(SD_CS);
            if (sdOK) {
                Serial.println("[SD] Card ready");
                loadCalFiles();
            }
        }
    }

    for (int bus = 0; bus < 2; bus++)
        for (int i = 0; i < 4; i++)
            processChannel(bus, i);

    const char* sdStr = sdOK ? "OK" : "NOSD";
    drawDisplay(display0, 0, sdStr);
    drawDisplay(display1, 1, sdStr);

    delay(100);  // 10 samples/sec
}
