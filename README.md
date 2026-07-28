# Volumio Player - CYD 2.8"

A remote display + touch transport controls for a Volumio box, running on
a "CYD" (Cheap Yellow Display) 2.8" ESP32 board - ESP32-2432S028R. ST7789 TFT Driver was used.

<img width="275" height="480" alt="VolumioRemote" src="https://github.com/user-attachments/assets/de219ee3-b152-4103-84b6-af598e80fac5" />

Arduino IDE sketch.

## Hardware

* ESP32 CYD (Cheap Yellow Display) — 2.8" 240×320 TFT with XPT2046 touchscreen
* The board used and tested is the ST7789-based CYD. Earlier boards used an ILI9341 driver. Included is the ST7789 driver in the TFT-eSPI_ST7789 folder. Copy the User_Setup.h to your TFT_eSPI folder in your Arduino libraries to use it.

<img width="480" height="287" alt="CYD_Back" src="https://github.com/user-attachments/assets/9724d696-0d97-4f9f-8807-8aac3eacd8e9" />


## Features

* Wifi signal strength
* Looks for Volumio.local (or IP address)
* Retrieves album art from iTunes API or Discogs (via token)
* Uses familiar Volumio controls for play/stop and volume
* Web Radio - display station, album art, artist and song title
* Web Radio mode - progress bar is a volume slider
* USB - displays artist, album art, song title, album info
* Supports web radio and USB navigation for selection
* Log into VolumioRemote.local (or IP) to set preferences
* Calibration built in for touch screen (first run or via Preferences)
* Uses Discogs free API to retrive album artwork (sign in to Discogs to get free API; no subscription required)
* Screen saver when idle for selected minutes (saves burn in)
* Multi-language supported: Enlish, French, German, Spanish, Dutch, Portuguese
* Captive portal for easy Wifi setup on LAN


## Select Source

Touch the lined icon opposite corner of the wifi display. Select from any web radios you have saved in Volumio's Favorite Radios or from any USB source. When playing from USB, providing the song was saved with artist/album Volumio Remote will look for the album cover. An album cover that can't be found will display a "no clue" graphic. Selecting any song in a USB list will play that list and stop, it does not repeat ad nauseam.


<img width="221" height="480" alt="Source" src="https://github.com/user-attachments/assets/8b606791-b803-46b0-8188-4b160c0fa1a8" />


## Board settings (Arduino IDE)

- Board: **ESP32 Dev Module**
- Partition Scheme: **No OTA (2MB APP/2MB SPIFFS)**


## Libraries

* TFT_eSPI is in this repository for ST7789 driver
* TFT_eSPI
* ArduinoJson
* HTTPClient
* WebServer
* ESPmDNS
* WiFiManager


## Language Selection

Open `All_Settings.h` and uncomment the language you want:

```cpp
#define LANG_EN   // English  (default)
//#define LANG_FR   // French
//#define LANG_DE   // German
//#define LANG_ES   // Spanish
//#define LANG_NL   // Dutch
//#define LANG_PT   // Portuguese
//#define LANG_TR   // Turkish
```

Note the laguage setting does NOT translate the songs or albums that Volumio gives it. It is for the VolumioRemote user interface.


## Setup

### 1. Download and rename the sketch folder

Download the zip from GitHub and extract it. GitHub will produce a folder called "VolumioRemoteCYD-master" or "VolumioRemoteCYD-main". Remove the "-master" or "-main" from the folder/directory name. This must be done before the sketch is loaded into the Arduino 2.3.x IDE.

### 2. Select the correct board and partition scheme

See board settings above.

### 3. Flash the sketch to the CYD

This must be done first so the data folder contents will then upload properly.

### 4. Upload the LittleFS data folder contents

Graphics and fonts are contained in the data folder. Use the following to assist with uploading the data folder contents.


**Arduino IDE 1.x**

Install the ESP32 LittleFS upload plugin (if needed):

* Download from: https://github.com/lorol/LITTLEFS_esp32fs-plugin/releases
* Place the `.jar` file in `<Arduino sketchbook>/tools/ESP32LittleFS/tool/`
* Restart the IDE
* Use **Tools menu → ESP32 LittleFS Data Upload**

**Arduino IDE 2.x**

Install the separate upload tool (if needed):

* Download from: https://github.com/earlephilhower/arduino-littlefs-upload/releases
* Place the `.vsix` file in the correct location per the instructions on that page
* Restart the IDE
* On macOS, press [⌘] + [Shift] + [P], Windows: [Ctrl] + [Shift] + [P]
* Type in Upload and you'll see "Upload LittleFS to PICO/ESP8266/ESP32"

> ⚠️ The serial monitor must be **closed** before uploading the data partition.


## Preferences

Log into VolumioRemote.local (or the IP) and there will be settings for your remote player.

<img width="257" height="480" alt="Preferences" src="https://github.com/user-attachments/assets/49da3a5f-2465-4cc4-bc6f-123ca09bc032" />


Navigate to Discogs and sign up. It's a no cost service and allows AlbumArt to be retrieved. Once signed in:

* Top right: View My Profile
* When that shows, a Settings icon will show on the right, click it
* Click on developers from the options on the left
* Click the Generate new token button
* Copy that token and paste into VolumioRemote and use Update button to save it

Enable the iTunes toggle. Again it uses a public API, no sign up or registration required.

Select your screen saver and timeout for it to activate.

Should the need ever arise to calibrate the touch screen, it can be directly from the Preferences. All it does is require you to touch the screen according to the corner marker it shows. Unless something changes, it's a one and done.

---
#####VolumioRemote ©2026 Wabbit Wanch Design - Open Source under the GPL-3.0 license
