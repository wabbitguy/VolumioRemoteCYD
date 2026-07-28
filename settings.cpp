#include "settings.h"
#include "config.h"  // VOLUMIO_HOST - default until the settings page overrides it
#include <Preferences.h>

static Preferences prefs;

// Cached in RAM so the hot path (artworkRender) never touches NVS - only
// the settings page's Save action writes back to flash.
static String gDiscogsToken;
static bool gUseItunes = true;  // default on, matches pre-Discogs behavior
static uint32_t gScreensaverTimeoutMin = 10;  // default: 10 minutes idle
static uint8_t gScreensaverMode = 0;           // default: 0 = bounce
static String gVolumioHost;

void settingsInit() {
  prefs.begin("settings", true);  // read-only
  gDiscogsToken = prefs.getString("discogs", "");
  gUseItunes = prefs.getBool("itunes", true);
  gScreensaverTimeoutMin = prefs.getUInt("ssTimeout", 10);
  gScreensaverMode = (uint8_t)prefs.getUInt("ssMode", 0);
  gVolumioHost = prefs.getString("vhost", VOLUMIO_HOST);
  prefs.end();
}

String settingsGetDiscogsToken() {
  return gDiscogsToken;
}

void settingsSetDiscogsToken(const String &token) {
  gDiscogsToken = token;
  prefs.begin("settings", false);
  prefs.putString("discogs", token);
  prefs.end();
}

bool settingsGetUseItunes() {
  return gUseItunes;
}

void settingsSetUseItunes(bool enabled) {
  gUseItunes = enabled;
  prefs.begin("settings", false);
  prefs.putBool("itunes", enabled);
  prefs.end();
}

uint32_t settingsGetScreensaverTimeoutMin() {
  return gScreensaverTimeoutMin;
}

void settingsSetScreensaverTimeoutMin(uint32_t minutes) {
  gScreensaverTimeoutMin = minutes;
  prefs.begin("settings", false);
  prefs.putUInt("ssTimeout", minutes);
  prefs.end();
}

uint8_t settingsGetScreensaverMode() {
  return gScreensaverMode;
}

void settingsSetScreensaverMode(uint8_t mode) {
  gScreensaverMode = mode;
  prefs.begin("settings", false);
  prefs.putUInt("ssMode", mode);
  prefs.end();
}

String settingsGetVolumioHost() {
  return gVolumioHost;
}

void settingsSetVolumioHost(const String &host) {
  String trimmed = host;
  trimmed.trim();
  gVolumioHost = trimmed.length() ? trimmed : String(VOLUMIO_HOST);  // blank field falls back to the compiled-in default rather than breaking every request

  prefs.begin("settings", false);
  prefs.putString("vhost", gVolumioHost);
  prefs.end();
}
