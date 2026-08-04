#include "webui.h"
#include "settings.h"
#include "config.h"
#include "volumio_api.h"
#include "Language.h"
#include "Translation.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

static WebServer server(80);

// Matches whichever single LANG_XX is uncommented in Language.h - drives the
// page's <html lang="..."> attribute only (all visible text already comes
// from Translation.h via the wc* constants).
#if defined(LANG_FR)
  static const char kHtmlLang[] = "fr";
#elif defined(LANG_DE)
  static const char kHtmlLang[] = "de";
#elif defined(LANG_ES)
  static const char kHtmlLang[] = "es";
#elif defined(LANG_NL)
  static const char kHtmlLang[] = "nl";
#elif defined(LANG_PT)
  static const char kHtmlLang[] = "pt";
#else
  static const char kHtmlLang[] = "en";
#endif

// ---------------------------------------------------------------------
// Settings page template - self-contained (no CDN calls). %TOKEN% /
// %CHECKED% / %IP% etc. substituted in handleRoot().
// ---------------------------------------------------------------------
static const char PAGE_TEMPLATE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="%WC_HTML_LANG%">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, viewport-fit=cover">
<title>%WC_PAGE_TITLE%</title>
<style>
  :root {
    color-scheme: dark;
    --bg: #101012;
    --card: #1c1c1e;
    --border: #2c2c2e;
    --text: #f2f2f7;
    --sub: #8e8e93;
    --accent: #0a84ff;
    --danger: #ff453a;
  }
  * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
  body {
    margin: 0 auto;
    padding: env(safe-area-inset-top) 16px calc(env(safe-area-inset-bottom) + 24px);
    background: var(--bg);
    color: var(--text);
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    max-width: 480px;
  }
  h1 { font-size: 22px; font-weight: 700; margin: 20px 0 4px; }
  .sub { color: var(--sub); font-size: 13px; margin-bottom: 20px; }
  .card {
    background: var(--card);
    border: 1px solid var(--border);
    border-radius: 14px;
    padding: 4px 16px;
    margin-bottom: 20px;
  }
  .row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 14px 0;
    border-bottom: 1px solid var(--border);
    gap: 12px;
  }
  .row:last-child { border-bottom: none; }
  .row.stack { flex-direction: column; align-items: flex-start; }
  label.title { font-size: 16px; }
  input[type=text], select {
    background: #2c2c2e;
    border: none;
    border-radius: 8px;
    color: var(--text);
    padding: 10px 12px;
    font-size: 15px;
    width: 100%;
    margin-top: 8px;
  }
  select { -webkit-appearance: none; appearance: none; }
  input[type=text]:focus, select:focus { outline: 2px solid var(--accent); }
  .switch { position: relative; width: 51px; height: 31px; flex-shrink: 0; }
  .switch input { opacity: 0; width: 0; height: 0; }
  .slider {
    position: absolute; inset: 0; background: #39393d;
    border-radius: 999px; transition: .2s; cursor: pointer;
  }
  .slider::before {
    content: ""; position: absolute; width: 27px; height: 27px;
    left: 2px; top: 2px; background: white; border-radius: 50%;
    transition: .2s;
  }
  .switch input:checked + .slider { background: #34c759; }
  .switch input:checked + .slider::before { transform: translateX(20px); }
  .btn {
    display: block; width: 100%; text-align: center;
    padding: 14px; border-radius: 12px; font-size: 16px; font-weight: 600;
    border: none; margin-bottom: 12px; cursor: pointer;
  }
  .btn-primary { background: var(--accent); color: white; }
  .btn-secondary { background: var(--card); color: var(--text); border: 1px solid var(--border); }
  .btn-danger { background: rgba(255,69,58,0.12); color: var(--danger); }
  .actions { display: flex; gap: 10px; }
  .actions .btn { margin-bottom: 0; }
  .section-title { font-size: 13px; color: var(--sub); text-transform: uppercase; letter-spacing: 0.04em; margin: 24px 4px 8px; }
  .toast {
    position: fixed; left: 16px; right: 16px; bottom: calc(env(safe-area-inset-bottom) + 16px);
    background: #34c759; color: #04210c; padding: 12px 16px; border-radius: 12px;
    font-size: 14px; font-weight: 600; text-align: center;
    box-shadow: 0 4px 16px rgba(0,0,0,0.4);
    opacity: 0; transform: translateY(8px); transition: .25s; pointer-events: none;
  }
  .toast.show { opacity: 1; transform: translateY(0); }
  footer { text-align: center; color: var(--sub); font-size: 12px; margin-top: 24px; }
</style>
</head>
<body>
  <h1>%WC_HEADING%</h1>
  <div class="sub">%WC_DEVICE_AT%%IP%</div>

  <form id="settingsForm" action="/save" method="POST">
    <div class="section-title">%WC_SEC_VHOST%</div>
    <div class="card">
      <div class="row stack">
        <label class="title" for="vhost">%WC_NAME_OR_IP%</label>
        <div style="display:flex; gap:8px; width:100%;">
          <input type="text" id="vhost" name="vhost" value="%VHOST%" placeholder="volumio.local" autocapitalize="off" autocorrect="off" spellcheck="false" oninput="this.style.color=''" style="flex:1; margin-top:8px;">
          <button type="button" id="vhostTestBtn" class="btn btn-secondary" style="width:auto; padding:10px 16px; margin:8px 0 0; white-space:nowrap;" onclick="testHost()">%WC_TEST%</button>
        </div>
      </div>
    </div>

    <div class="section-title">%WC_SEC_COVERART%</div>
    <div class="card">
      <div class="row stack">
        <label class="title" for="discogs">%WC_DISCOGS_LABEL%</label>
        <input type="text" id="discogs" name="discogs" value="%TOKEN%" placeholder="%WC_DISCOGS_PLACEHOLDER%" autocapitalize="off" autocorrect="off" spellcheck="false">
      </div>
      <div class="row">
        <div>
          <label class="title" for="itunes">iTunes</label>
        </div>
        <label class="switch">
          <input type="checkbox" id="itunes" name="itunes" %CHECKED%>
          <span class="slider"></span>
        </label>
      </div>
    </div>

    <div class="section-title">%WC_SEC_SCREENSAVER%</div>
    <div class="card">
      <div class="row" style="gap:16px;">
        <div class="row stack" style="padding:0; border:none; flex:1;">
          <label class="title" for="ss_mode">%WC_STYLE%</label>
          <select id="ss_mode" name="ss_mode">%SS_MODE_OPTIONS%</select>
        </div>
        <div class="row stack" style="padding:0; border:none; flex:1;">
          <label class="title" for="ss_min">%WC_TIME%</label>
          <select id="ss_min" name="ss_min">%SS_MIN_OPTIONS%</select>
        </div>
      </div>
    </div>

    <div class="actions">
      <button type="submit" class="btn btn-primary">%WC_UPDATE%</button>
    </div>
  </form>

  <div class="section-title">%WC_SEC_DEVICE%</div>
  <div class="card" style="padding:12px 16px;">
    <button class="btn btn-secondary" style="margin-bottom:10px;" onclick="confirmAction('/touchcal', '%WC_RECAL_CONFIRM%')">%WC_RECAL_BTN%</button>
    <button class="btn btn-danger" style="margin-bottom:0;" onclick="confirmAction('/wifireset', '%WC_RESETWIFI_CONFIRM%')">%WC_RESETWIFI_BTN%</button>
  </div>

  <footer>
    <div>%VERSION% &copy;2026 Wabbit Wanch Design</div>
  </footer>
  <div class="toast" id="toast">%WC_SETTINGS_SAVED%</div>

<script>
function confirmAction(path, message) {
  if (!confirm(message)) return;
  fetch(path, { method: 'POST' }).finally(() => showToast('%WC_REBOOTING_JS%'));
}
function testHost() {
  var inp = document.getElementById('vhost');
  var btn = document.getElementById('vhostTestBtn');
  var host = inp.value.trim();
  if (!host) return;
  var label = btn.textContent;
  btn.disabled = true;
  inp.style.color = '';
  fetch('/testhost?host=' + encodeURIComponent(host))
    .then(function (r) { return r.json(); })
    .then(function (j) { inp.style.color = j.ok ? '#34c759' : '#ff453a'; })
    .catch(function () { inp.style.color = '#ff453a'; })
    .finally(function () { btn.disabled = false; btn.textContent = label; });
}
function showToast(msg) {
  var t = document.getElementById('toast');
  t.textContent = msg;
  t.classList.add('show');
  setTimeout(function () { t.classList.remove('show'); }, 2500);
}
if (new URLSearchParams(location.search).get('saved') === '1') {
  showToast('%WC_SETTINGS_SAVED_JS%');
  history.replaceState(null, '', location.pathname);
}
</script>
</body>
</html>
)rawliteral";

// ---------------------------------------------------------------------
// Request handlers
// ---------------------------------------------------------------------

// Escapes just enough that a stray `"` in a pasted token can't break value="...".
static String escapeAttr(const String &s) {
  String out = s;
  out.replace("&", "&amp;");
  out.replace("\"", "&quot;");
  return out;
}

// Escapes a translated string for safe embedding inside a single-quoted JS
// string literal - applied to every wc* constant that lands inside <script>
// or an onclick="...(...)" attribute, so a translation with an apostrophe
// (e.g. French/Spanish "n'est"-style text) can never break the page's JS.
// Order matters: backslash first, then the quote, so an escaped quote's own
// backslash doesn't get re-escaped.
static String jsEscape(const String &s) {
  String out = s;
  out.replace("\\", "\\\\");
  out.replace("'", "\\'");
  return out;
}

// Builds the <option> list for the screensaver minutes dropdown, with
// `selected` on whatever's currently saved - plain server-rendered options
// rather than JS, so the page still works with JS disabled/broken.
static String buildMinuteOptions(int selected) {
  String out;
  for (int v = 0; v <= 60; v += 5) {
    out += "<option value=\"" + String(v) + "\"";
    if (v == selected) out += " selected";
    out += ">" + String(v) + "</option>";
  }
  return out;
}

// Same server-rendered-options pattern as buildMinuteOptions() above - keep
// the value order in sync with the SS_MODE_* constants in Volumio_Player.ino.
static String buildModeOptions(int selected) {
  static const char *kLabels[] = { "Bounce", "Fireworks", "Tetris", "Starfield", "Matrix" };
  static const int kCount = sizeof(kLabels) / sizeof(kLabels[0]);
  String out;
  for (int v = 0; v < kCount; v++) {
    out += "<option value=\"" + String(v) + "\"";
    if (v == selected) out += " selected";
    out += ">" + String(kLabels[v]) + "</option>";
  }
  return out;
}

static void handleRoot() {
  String html = FPSTR(PAGE_TEMPLATE);
  html.replace("%VHOST%", escapeAttr(settingsGetVolumioHost()));
  html.replace("%TOKEN%", escapeAttr(settingsGetDiscogsToken()));
  html.replace("%CHECKED%", settingsGetUseItunes() ? "checked" : "");
  html.replace("%IP%", WiFi.localIP().toString());
  html.replace("%VERSION%", VERSION);

  html.replace("%SS_MIN_OPTIONS%", buildMinuteOptions(settingsGetScreensaverTimeoutMin()));
  html.replace("%SS_MODE_OPTIONS%", buildModeOptions(settingsGetScreensaverMode()));

  // Plain HTML text/attribute content - escapeAttr() is enough here.
  html.replace("%WC_HTML_LANG%", kHtmlLang);
  html.replace("%WC_PAGE_TITLE%", wcPageTitle);
  html.replace("%WC_HEADING%", wcHeading);
  html.replace("%WC_DEVICE_AT%", wcDeviceAt);
  html.replace("%WC_SEC_VHOST%", wcSecVolumioHost);
  html.replace("%WC_NAME_OR_IP%", wcNameOrIp);
  html.replace("%WC_TEST%", wcTest);
  html.replace("%WC_SEC_COVERART%", wcSecCoverArt);
  html.replace("%WC_DISCOGS_LABEL%", wcDiscogsToken);
  html.replace("%WC_DISCOGS_PLACEHOLDER%", escapeAttr(wcDiscogsPlaceholder));
  html.replace("%WC_SEC_SCREENSAVER%", wcSecScreensaver);
  html.replace("%WC_STYLE%", wcStyle);
  html.replace("%WC_TIME%", wcTime);
  html.replace("%WC_UPDATE%", wcUpdate);
  html.replace("%WC_SEC_DEVICE%", wcSecDevice);
  html.replace("%WC_RECAL_BTN%", wcRecalTouch);
  html.replace("%WC_RESETWIFI_BTN%", wcResetWifi);
  html.replace("%WC_SETTINGS_SAVED%", wcSettingsSaved);

  // These land inside single-quoted JS (either a <script> literal or a
  // JS-string argument sitting inside an onclick="..." attribute) - jsEscape()
  // first so an apostrophe in a translation can't break out, then escapeAttr()
  // for the two that also sit inside a double-quoted HTML attribute.
  html.replace("%WC_RECAL_CONFIRM%", escapeAttr(jsEscape(wcRecalConfirm)));
  html.replace("%WC_RESETWIFI_CONFIRM%", escapeAttr(jsEscape(wcResetWifiConfirm)));
  html.replace("%WC_REBOOTING_JS%", jsEscape(wcRebooting));
  html.replace("%WC_SETTINGS_SAVED_JS%", jsEscape(wcSettingsSaved));

  server.send(200, "text/html", html);
}

// Backs the settings page's "Test" button - checks whatever host/IP is
// currently typed into the field, not the saved setting, so the user can
// try a value before committing to it with Update.
static void handleTestHost() {
  String host = server.hasArg("host") ? server.arg("host") : "";
  bool ok = volumioTestHost(host);
  server.send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void handleSave() {
  if (server.hasArg("vhost")) {
    settingsSetVolumioHost(server.arg("vhost"));
  }

  if (server.hasArg("discogs")) {
    settingsSetDiscogsToken(server.arg("discogs"));
  }
  // Unchecked checkboxes simply aren't sent by the browser, so presence
  // of the arg (not its value) is what matters here.
  settingsSetUseItunes(server.hasArg("itunes"));

  uint32_t ssMin = server.hasArg("ss_min") ? (uint32_t)server.arg("ss_min").toInt() : 0;
  settingsSetScreensaverTimeoutMin(ssMin);

  if (server.hasArg("ss_mode")) {
    settingsSetScreensaverMode((uint8_t)server.arg("ss_mode").toInt());
  }

  server.sendHeader("Location", "/?saved=1");
  server.send(303);
}

static void handleWifiReset() {
  server.send(200, "text/plain", "Resetting WiFi settings, device is rebooting...");
  delay(300);  // let the response flush before the reboot tears down the connection
  requestWifiReset();
}

static void handleTouchCal() {
  server.send(200, "text/plain", "Clearing touch calibration, device is rebooting...");
  delay(300);
  requestTouchRecalibration();
}

static void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ---------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------
void webuiInit() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/testhost", HTTP_GET, handleTestHost);
  server.on("/wifireset", HTTP_POST, handleWifiReset);
  server.on("/touchcal", HTTP_POST, handleTouchCal);
  server.onNotFound(handleNotFound);
  server.begin();

  // Advertises as http://<WIFI_PORTAL_AP_NAME>.local/.
  MDNS.begin(WIFI_PORTAL_AP_NAME);
  MDNS.addService("http", "tcp", 80);
}

void webuiHandleClient() {
  server.handleClient();
}
