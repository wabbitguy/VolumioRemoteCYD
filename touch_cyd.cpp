#include "Settings.h"  // touch function declarations are here
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <math.h>  // fabsf(), lroundf()

// ---- XPT2046 on its own SPI bus ----
static SPIClass touchSPI(HSPI);
static SPISettings touchSPISettings(1000000, MSBFIRST, SPI_MODE0);  // 1 MHz

static const uint8_t CMD_READ_X  = 0xD0;
static const uint8_t CMD_READ_Y  = 0x90;
static const uint8_t CMD_READ_Z1 = 0xB0;
static const uint8_t CMD_READ_Z2 = 0xC0;

static const int PRESSURE_THRESHOLD = 400;  // press needed; lower = lighter touch

static uint16_t xptReadCmd(uint8_t cmd) {
  digitalWrite(TOUCH_CS, LOW);
  touchSPI.transfer(cmd);
  uint16_t hi = touchSPI.transfer(0x00);
  uint16_t lo = touchSPI.transfer(0x00);
  digitalWrite(TOUCH_CS, HIGH);
  return ((hi << 8) | lo) >> 3;  // 12-bit result
}

static bool touchCydRawRead(int &rx, int &ry, int &pressure) {
  if (digitalRead(TOUCH_IRQ) == HIGH) return false;  // IRQ is low while touched

  touchSPI.beginTransaction(touchSPISettings);

  int z1 = xptReadCmd(CMD_READ_Z1);
  int z2 = xptReadCmd(CMD_READ_Z2);
  pressure = z1 + 4095 - z2;

  if (pressure < PRESSURE_THRESHOLD) {
    touchSPI.endTransaction();
    return false;
  }

  int x1 = xptReadCmd(CMD_READ_X);  // two samples per axis, averaged
  int x2 = xptReadCmd(CMD_READ_X);
  int y1 = xptReadCmd(CMD_READ_Y);
  int y2 = xptReadCmd(CMD_READ_Y);

  touchSPI.endTransaction();

  rx = (x1 + x2) / 2;
  ry = (y1 + y2) / 2;
  return true;
}

void touchCydInit() {
  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  pinMode(TOUCH_IRQ, INPUT);
  touchSPI.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, -1);  // -1: we drive CS ourselves
}

// ---- Calibration (3 points, saved in Preferences) ----
struct CalCoef {
  float ax, bx, cx;  // screenX = ax*rawX + bx*rawY + cx
  float ay, by, cy;  // screenY = ay*rawX + by*rawY + cy
};

static CalCoef gCal;
static bool gCalLoaded = false;
static Preferences calPrefs;

bool touchCydLoadCalibration() {
  calPrefs.begin("touchcal", true);
  size_t len = calPrefs.isKey("cal") ? calPrefs.getBytes("cal", &gCal, sizeof(gCal)) : 0;
  calPrefs.end();
  gCalLoaded = (len == sizeof(gCal));
  return gCalLoaded;
}

static void saveCalibration(const CalCoef &c) {
  calPrefs.begin("touchcal", false);
  calPrefs.putBytes("cal", &c, sizeof(c));
  calPrefs.end();
  gCal = c;
  gCalLoaded = true;
}

void touchCydClearCalibration() {
  calPrefs.begin("touchcal", false);
  calPrefs.remove("cal");
  calPrefs.end();
  gCalLoaded = false;
}

bool touchCydGetPoint(uint16_t &sx, uint16_t &sy) {
  if (!gCalLoaded) return false;

  int rx, ry, pressure;
  if (!touchCydRawRead(rx, ry, pressure)) return false;

  float fx = gCal.ax * rx + gCal.bx * ry + gCal.cx;
  float fy = gCal.ay * rx + gCal.by * ry + gCal.cy;

  sx = (uint16_t)constrain((int)lroundf(fx), 0, SCREEN_W - 1);
  sy = (uint16_t)constrain((int)lroundf(fy), 0, SCREEN_H - 1);
  return true;
}

static void waitForRawTap(int &rx, int &ry) {
  while (digitalRead(TOUCH_IRQ) == LOW) delay(10);  // wait for release first
  delay(150);

  int pressure;
  while (true) {
    if (touchCydRawRead(rx, ry, pressure)) {
      delay(30);  // let the reading settle
      int rx2, ry2, p2;
      if (touchCydRawRead(rx2, ry2, p2)) {
        rx = (rx + rx2) / 2;
        ry = (ry + ry2) / 2;
      }
      break;
    }
    delay(10);
  }

  while (digitalRead(TOUCH_IRQ) == LOW) delay(10);  // wait for release
  delay(150);
}

void touchCydCalibrate(TFT_eSPI &tft, const char *prompt, const char *failedMsg) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.loadFont(AA_FONT_SMALL, LittleFS);  // Font 2 has no accents
  tft.drawString(prompt, SCREEN_W / 2, 8);
  tft.unloadFont();

  const int margin = 24;  // crosshairs kept away from the edges
  const int sxPts[3] = { margin, SCREEN_W - margin, margin };
  const int syPts[3] = { margin, margin, SCREEN_H - margin };
  int rxPts[3], ryPts[3];

  for (int i = 0; i < 3; i++) {
    tft.drawLine(sxPts[i] - 8, syPts[i], sxPts[i] + 8, syPts[i], TFT_MAGENTA);
    tft.drawLine(sxPts[i], syPts[i] - 8, sxPts[i], syPts[i] + 8, TFT_MAGENTA);
    tft.drawCircle(sxPts[i], syPts[i], 6, TFT_MAGENTA);

    waitForRawTap(rxPts[i], ryPts[i]);

    tft.fillCircle(sxPts[i], syPts[i], 10, TFT_BLACK);
    tft.drawLine(sxPts[i] - 8, syPts[i], sxPts[i] + 8, syPts[i], TFT_BLACK);
    tft.drawLine(sxPts[i], syPts[i] - 8, sxPts[i], syPts[i] + 8, TFT_BLACK);
  }

  float rx1 = rxPts[0], ry1 = ryPts[0], sx1 = sxPts[0], sy1 = syPts[0];  // 3-point affine solve (Cramer's rule)
  float rx2 = rxPts[1], ry2 = ryPts[1], sx2 = sxPts[1], sy2 = syPts[1];
  float rx3 = rxPts[2], ry3 = ryPts[2], sx3 = sxPts[2], sy3 = syPts[2];

  float delta = (rx1 - rx3) * (ry2 - ry3) - (rx2 - rx3) * (ry1 - ry3);
  if (fabsf(delta) < 1e-6) {  // taps in a line, try again
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.loadFont(AA_FONT_SMALL, LittleFS);
    tft.drawString(failedMsg, SCREEN_W / 2, SCREEN_H / 2);
    tft.unloadFont();
    delay(1500);
    touchCydCalibrate(tft, prompt, failedMsg);
    return;
  }

  CalCoef c;
  c.ax = ((sx1 - sx3) * (ry2 - ry3) - (sx2 - sx3) * (ry1 - ry3)) / delta;
  c.bx = ((rx1 - rx3) * (sx2 - sx3) - (rx2 - rx3) * (sx1 - sx3)) / delta;
  c.cx = sx1 - c.ax * rx1 - c.bx * ry1;

  c.ay = ((sy1 - sy3) * (ry2 - ry3) - (sy2 - sy3) * (ry1 - ry3)) / delta;
  c.by = ((rx1 - rx3) * (sy2 - sy3) - (rx2 - rx3) * (sy1 - sy3)) / delta;
  c.cy = sy1 - c.ay * rx1 - c.by * ry1;

  saveCalibration(c);
}
