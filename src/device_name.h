#pragma once

// How this board shows up: its Wi-Fi hostname, and the set-up portal's mDNS name (http://countdown.local/wifi,
// which the set-up screen from provision/provision.py points at). The same on every board: in its logs the broker
// names a board by its device_id, which tells boards apart.
constexpr char kDeviceName[] = "countdown";
