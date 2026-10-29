#pragma once
#include <Arduino.h>
#include "volumio_api.h"

// ---- Album art ----
void artworkInit();  // set up the JPEG decoder

bool artworkRender(const VolumioState &state, int x, int y, int size);  // true if something was drawn

// ---- Button icons ----
bool artworkDrawIcon(const char *path, int x, int y, int srcSize, int dstSize);

bool artworkDrawIconPressed(const char *path, int x, int y, int srcSize, int dstSize);  // inverted, for touch feedback
