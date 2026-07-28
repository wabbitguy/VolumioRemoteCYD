// Function declarations (touchCydInit/GetPoint/LoadCalibration/
// ClearCalibration/Calibrate) live in config.h, not a dedicated header -
// see the "Touch" section there.
#include "config.h"
#include <SPI.h>
#include <TFT_eSPI.h>
#include <Preferences.h>
#include <math.h>  // fabsf(), lroundf()

// ---------------------------------------------------------------------
// Raw XPT2046 SPI access - own bus (HSPI), own CS, manually driven rather
// than handed to SPIClass as an auto-CS slave, since we need tight control
// over the command/response framing.
// ---------------------------------------------------------------------
static SPIClass touchSPI(HSPI);
static SPISettings touchSPISettings(1000000, MSBFIRST, SPI_MODE0);  // XPT2046 tops out well under 2.5MHz reliably; 1MHz is comfortably safe over a jumper/ribbon

static const uint8_t CMD_READ_X  = 0xD0;
static const uint8_t CMD_READ_Y  = 0x90;
static const uint8_t CMD_READ_Z1 = 0xB0;
static const uint8_t CMD_READ_Z2 = 0xC0;

// Below this, treat it as noise/hover rather than a real press. Z1+4095-Z2
// grows *larger* with a firmer press (see touchCydRawRead()) - this needs
// retuning if your panel feels chronically over/under-sensitive.
static const int PRESSURE_THRESHOLD = 400;

static uint16_t xptReadCmd(uint8_t cmd) {
  digitalWrite(TOUCH_CS, LOW);
  touchSPI.transfer(cmd);
  uint16_t hi = touchSPI.transfer(0x00);
  uint16_t lo = touchSPI.transfer(0x00);
  digitalWrite(TOUCH_CS, HIGH);
  return ((hi << 8) | lo) >> 3;  // 12-bit result, right-justified out of the 16 clocked bits
}

// Raw (uncalibrated) touch read. Returns false (rx/ry/pressure untouched
// except pressure, which is always set) if the panel isn't currently
// pressed hard enough to trust.
static bool touchCydRawRead(int &rx, int &ry, int &pressure) {
  if (digitalRead(TOUCH_IRQ) == HIGH) return false;  // IRQ is active-low - idle high means definitely not touched, skip the SPI round-trip entirely

  touchSPI.beginTransaction(touchSPISettings);

  int z1 = xptReadCmd(CMD_READ_Z1);
  int z2 = xptReadCmd(CMD_READ_Z2);
  pressure = z1 + 4095 - z2;

  if (pressure < PRESSURE_THRESHOLD) {
    touchSPI.endTransaction();
    return false;
  }

  // Two samples per axis, averaged - cheap noise rejection for a resistive
  // panel without the complexity of a full median filter.
  int x1 = xptReadCmd(CMD_READ_X);
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
  touchSPI.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI, -1);  // -1: no auto-CS pin, we drive TOUCH_CS ourselves above
}

// ---------------------------------------------------------------------
// Calibration - 3-point affine fit, Preferences-backed.
// ---------------------------------------------------------------------
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

// Blocks until a firm, settled press is detected, then returns its raw
// coordinates. Used only during calibration, where blocking is fine (and
// wanted - no other UI competes for the screen while this runs).
static void waitForRawTap(int &rx, int &ry) {
  // Wait for release first, so a finger still down from the previous
  // target (or from tapping "start calibration" on the settings page)
  // can't immediately register as this target's tap too.
  while (digitalRead(TOUCH_IRQ) == LOW) delay(10);
  delay(150);

  int pressure;
  while (true) {
    if (touchCydRawRead(rx, ry, pressure)) {
      delay(30);  // let the reading settle rather than trusting the very first contact-bounce sample
      int rx2, ry2, p2;
      if (touchCydRawRead(rx2, ry2, p2)) {
        rx = (rx + rx2) / 2;
        ry = (ry + ry2) / 2;
      }
      break;
    }
    delay(10);
  }

  while (digitalRead(TOUCH_IRQ) == LOW) delay(10);  // wait for this tap to release before returning
  delay(150);
}

void touchCydCalibrate(TFT_eSPI &tft) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextFont(2);
  tft.drawString("Tap each crosshair", SCREEN_W / 2, 8);

  // Three non-collinear screen points (a diagonal pair alone can't tell a
  // swapped X/Y axis from a non-swapped one - both screen axes move
  // together along a diagonal either way). Kept well inside the panel
  // edges - resistive touch is often least accurate right at the border.
  const int margin = 24;
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

  // Classic 3-point affine solve (Cramer's rule) for
  //   screenX = ax*rawX + bx*rawY + cx
  //   screenY = ay*rawX + by*rawY + cy
  // using the three (raw -> screen) correspondences just captured.
  float rx1 = rxPts[0], ry1 = ryPts[0], sx1 = sxPts[0], sy1 = syPts[0];
  float rx2 = rxPts[1], ry2 = ryPts[1], sx2 = sxPts[1], sy2 = syPts[1];
  float rx3 = rxPts[2], ry3 = ryPts[2], sx3 = sxPts[2], sy3 = syPts[2];

  float delta = (rx1 - rx3) * (ry2 - ry3) - (rx2 - rx3) * (ry1 - ry3);
  if (fabsf(delta) < 1e-6) {
    // Degenerate tap pattern (e.g. same spot tapped three times) - bail out
    // without saving bad coefficients rather than dividing by ~zero. Runs
    // again next boot since nothing was written to flash.
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("Calibration failed - retrying", SCREEN_W / 2, SCREEN_H / 2);
    delay(1500);
    touchCydCalibrate(tft);
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
