#pragma once
#include <Arduino.h>

// ---- Volumio REST API ----
struct VolumioState {
  bool   valid = false;
  String status;        // "play" | "pause" | "stop"
  String title;
  String artist;
  String album;
  String albumArtUrl;   // fully-qualified URL, ready to fetch
  String service;       // "mpd", "webradio", "airplay_emulation", etc.
  int    volume = -1;
  bool   mute = false;
  int    seek = 0;       // ms
  int    duration = 0;   // s
  bool disableUiControls = false;  // hide transport buttons (AirPlay)
};

bool volumioGetState(VolumioState &out);       // fetch current state, true on success
bool volumioCommand(const String &cmd);        // "play", "pause", "toggle", "next", "prev", ...
bool volumioSetVolume(int vol);                // absolute volume, 0-100
bool volumioTestHost(const String &host);  // web page Test button

// ---- Station, USB and playlist lists ----
static const int MAX_STATIONS = 200;  // list size for favorites, USB, playlists

struct WebRadioStation {
  String name;
  String uri;
  bool isFolder = false;  // USB: true = folder
};

int volumioGetFavouriteWebRadios(WebRadioStation out[], int maxCount);
bool volumioPlayWebRadio(const WebRadioStation &station);  // switch station

// ---- USB music ----
bool volumioGetUsbRootUri(String &uriOut);  // true + root uri if a drive is mounted
int volumioBrowseUsb(const String &uri, WebRadioStation out[], int maxCount);  // folders and songs at one uri
bool volumioPlayUsbSong(WebRadioStation songs[], int songCount, int startIndex);  // play one song, queue the rest
bool volumioPlayUsbFolder(const WebRadioStation &folder);  // plays this folder and its subfolders, then stops

// ---- Playlists ----
int volumioGetPlaylists(WebRadioStation out[], int maxCount);  // saved playlist names
bool volumioPlayPlaylist(const String &name);  // play a whole playlist
int volumioBrowsePlaylist(const String &name, WebRadioStation out[], int maxCount);  // songs in one playlist
