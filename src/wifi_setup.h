#pragma once

#include <string>

#include "ports.h"

// Joins the Wi-Fi network saved in `store` (WIFI_SSID, WIFI_PASSWORD). If none is saved, or it can't
// be joined, opens a WiFiManager set-up portal (an access point named ePaper-Dashboard-XXXX, with the
// page at http://countdown.local/wifi) and saves what's entered there; the "Saving" page then sends the
// browser to `<afterSetupUrl>/pair?t=...` once the phone is back online. On a board provisioned with
// provision/provision.py, `display` shows the set-up screen from the `setup` partition first, and the
// access point takes the password on it; otherwise the access point is open. Returns once connected;
// restarts the board if the portal times out.
void connectWifi(countdown::Store& store, countdown::Random& random, countdown::Log& log,
				 const std::string& afterSetupUrl, countdown::Display& display);
