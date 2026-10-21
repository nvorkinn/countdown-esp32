#pragma once

// How this board shows up: its Wi-Fi hostname, the set-up portal's mDNS name (http://countdown.local/wifi,
// which the set-up screen from provision/provision.py points at), and the `host` on its log lines. The same
// on every board: the broker replaces the logs' `host` with the board's device_id, which tells boards apart.
constexpr char kDeviceName[] = "countdown";
constexpr char kLogService[] = "countdown-esp32";
