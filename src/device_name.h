#pragma once

// How this board shows up: its Wi-Fi hostname, the set-up portal's mDNS name (http://countdown.local/wifi,
// which the set-up screen from provision/provision.py points at), and the host and service on every log
// line sent to VictoriaLogs. The same on every board: the broker tells boards apart by their device_id.
constexpr char kDeviceName[] = "countdown";
constexpr char kLogService[] = "countdown-esp32";
