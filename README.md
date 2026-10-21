# countdown-esp

ESP32 firmware for the countdown, built with [PlatformIO](https://platformio.org/) on the Arduino framework.

It is a countdown display: the ESP32 port of `countdown-client` (`../countdown/client`). It registers
with [auth-broker](https://github.com/nvorkinn/auth-broker) as a `display`, then polls `/api/frame` for
frames that a renderer has drawn and puts them on the selected Waveshare e-paper panel.
Each poll also carries the board's logs and metrics to the broker.

## Setup

PlatformIO is installed from pip:

```sh
python3 -m venv .venv
source .venv/bin/activate
pip install platformio
```

Open `countdown-esp` as the folder in VS Code and accept the recommended PlatformIO IDE extension (listed in `.vscode/extensions.json`).

## Common commands

Run these from this directory (`countdown-esp`):

```sh
pio run -e esp-wrover-kit     # build the firmware
pio run -e esp-wrover-kit-it8951 # build for the 7.8" 1872x1404 IT8951 HAT
pio run -t upload             # build and flash the board on the default serial port
pio device monitor            # open the serial monitor (115200 baud)
pio test -e native            # run the unit tests in test/ on this machine
pio run -t erase              # wipe the flash: Wi-Fi, the device secret and the set-up screen
```

The broker URL is a build flag, `BROKER_URL` in `platformio.ini`. To point a build somewhere else:

```sh
PLATFORMIO_BUILD_FLAGS='-DBROKER_URL=\"http://192.168.1.10:8000\"' pio run -t upload
```

Each build writes `.pio/build/<environment>/firmware.bin` (application only) and
`firmware-merged.bin` (standalone flash image).

### Selecting a screen

| PlatformIO environment | Panel | Broker dimensions | Monochrome frame |
|------------------------|-------|-------------------|------------------|
| `esp-wrover-kit` (default) | Waveshare 7.5" V2 | 800x480 | 48,000 bytes |
| `esp-wrover-kit-it8951` | Waveshare 7.8" IT8951 HAT | 1872x1404 | 328,536 bytes |

Use the same `-e` option when uploading, for example `pio run -e esp-wrover-kit-it8951 -t upload`.
Both environments target the ESP32-WROVER board; the 7.8" build requires working PSRAM. Large heap
allocations explicitly use the PSRAM-enabled allocator. HTTP bodies are bounded to the panel's frame
size, including chunked responses, and memory/read failures are logged rather than displaying a partial frame.

Panel selection is compiled in: each environment chooses exactly one `EPD_PANEL_*` flag and its wiring.
Registration and frame validation get their dimensions from the same panel object. Polling, provisioning,
logging and the broker's frame format are unchanged: row-padded, MSB-first, one bit per pixel, 1 black / 0 white.
The IT8951 driver expands that format into small 8-bpp transfer chunks and uses full GC16 refreshes;
it does not allocate an expanded framebuffer or rely on firmware-specific fast-refresh modes.

To add another controller, implement `EpdPanel` in `lib/epd/`, select it in `src/main.cpp`, and add a
PlatformIO environment with the appropriate pins/transport. Dimensions alone cannot make a driver
compatible with another controller.

### Flashing without PlatformIO

Download the `firmware` artifact from a successful **Firmware** GitHub Actions run, unzip it, and select
the file matching your board/panel whose name ends in **`-merged.bin`**. The build merges the bootloader,
partition table, Arduino boot image and application using PlatformIO's actual upload offsets.

In Espressif's Flash Download Tool, select **ESP32**, choose the merged file at address **`0x0`**,
and use the serial port connected to the board (4 MB flash, DIO, 40 MHz for these builds). A compatible
browser flasher that accepts a local binary at `0x0` also works. No project checkout or PlatformIO is
needed on the flashing machine. With esptool v5:

```sh
python -m esptool --chip esp32 --port /dev/cu.usbserial-XXXX write-flash 0x0 countdown-esp32-esp-wrover-kit-it8951-main-merged.bin
```

Replace the port and filename with yours; Windows ports look like `COM3`. With esptool v4, use
`write_flash` instead of `write-flash`. If automatic bootloader entry fails, hold BOOT while starting
the flash, then release it once connected. Reset the board afterwards.

**A board built before the partition table changed needs the merged image once.** The firmware now uses a 3 MB app slot
(`partitions.csv`), and the partition table is in flash, so an application-only `firmware.bin`
flashed over an older table doesn't fit its smaller slot and won't boot. Flash the merged image at `0x0` (it also
writes the new table), as above. A board already on the 3 MB layout but from before the `setup` partition
needs it too: an application-only update keeps a table with no `setup` partition, so the board never shows its
set-up screen and its set-up network stays open.

**Treat merged images as fresh installs:** the padding covers NVS, so flashing one erases saved Wi-Fi
and the device secret (it stops short of the `setup` partition, so the set-up screen stays). The board
will open its first-boot portal and register a new device. For updates
that preserve NVS, use PlatformIO upload or flash the matching application-only `firmware.bin` at its
application offset (`0x10000` for these builds), with the existing matching bootloader/partition layout.
Do not flash the application-only image at `0x0`.

The broker URL and panel selection are baked into the binary; Wi-Fi credentials and the device secret
are provisioned on the board, not included in the download.

### Provisioning a board

Each board gets its own set-up screen, written to the `setup` partition (`partitions.csv`) by
`provision/provision.py`: a random password for the set-up network, and a frame with two QR codes, one
that joins `ePaper-Dashboard-XXXX` (named from the board's MAC) and one that opens
`http://countdown.local/wifi`. With the board on USB:

```sh
cd provision
uv run provision.py --port /dev/cu.usbmodemXXXX --panel 7in5_v2   # or --panel it8951
```

It also saves the screen as `provision/screens/ePaper-Dashboard-XXXX.png`, which git ignores: keep it, as
it's the record of that board's password. If a board's network wants a password its panel isn't showing
(the 7.5" driver can't tell when no panel is attached, so it may think it drew the screen), look it up
there, or run the script again, which writes a new password and screen. It writes only the `setup`
partition, so it can run before or after flashing the firmware, and later uploads leave it alone;
`pio run -t erase` wipes it. The partition
holds `SET1`, the password (64 bytes, NUL-padded), the frame's length (4 bytes, little-endian) and the
frame, in the broker's frame format. `uv run pytest` runs the script's tests.

### Coverage

The native tests are built with `--coverage`. With [gcovr](https://gcovr.com/) (macOS needs the `--gcov-executable` part):

```sh
pio test -e native
gcovr --root . --filter lib/ --gcov-executable "xcrun llvm-cov gcov" .pio/build/native
```

## First boot

1. With no Wi-Fi saved in NVS (`WIFI_SSID`, `WIFI_PASSWORD`), or if the saved network can't be joined
   within 5 minutes, the board opens a set-up portal: a Wi-Fi network named `ePaper-Dashboard-XXXX`. (A
   saved network gets that long because a router can take a few minutes to come back after a power cut;
   until then the panel keeps the last dashboard.) On a
   [provisioned](#provisioning-a-board) board the panel first shows the set-up screen, and the network takes
   the password on it: its first QR code joins the network, its second opens `http://countdown.local/wifi`
   in the phone's browser. On a board that wasn't provisioned, or if the panel couldn't draw the screen,
   the network is open. On an iPhone no pop-up appears (the board answers Apple's connectivity check): join
   the network, open Safari and go to `http://countdown.local/wifi` or any plain-http site such as
   `example.com`, which redirects to the set-up page. If
   Safari insists on https, browse to `http://192.168.4.1` instead. Pick the network and save: the board
   joins it, closes the portal, and Safari opens the broker by itself once the phone is back online
   (if the password was wrong, the page returns to the set-up page after a minute). On Android the
   set-up page opens in its own sheet, with no redirect afterwards. If nobody sets it up within
   5 minutes, the board restarts and tries the saved network again.
2. It makes a device secret (like Python's `secrets.token_urlsafe(24)`), saves it to NVS as
   `device_secret`, and registers with the broker, sending the selected panel's size (`width` and `height`,
   800x480 or 1872x1404). It registers again on every boot, with the same secret.
3. It polls `/api/frame` with a `POST`, waiting as long as the broker's `Retry-After` says (60 seconds
   if it is missing or invalid). The broker works the header out so the next poll lands just after its
   next render (about :03 and :33 past the minute), so the wait counts from when the answer arrived: the time
   spent drawing a new frame comes off it, and it is never less than 1 second. A failed request waits 60 seconds.
   Every HTTP response is logged in the same format to serial and
   buffered for the next poll: `Frame response: HTTP 200 (48000 bytes), Retry-After: "30" seconds`.
   The header is quoted as received, or shown as `<missing>` when absent. A 401 triggers immediate
   re-registration without sleeping, regardless of the header. Connection failures log the error and retry delay.
   Registration retries log `Sleeping for N seconds` before each wait.
   The broker answers 202 until it matches the display with a renderer. See
   [What each poll sends](#what-each-poll-sends).
4. Each new frame (a `200`) is drawn on the panel: wake it, send the frame, refresh (timing depends on the panel),
   and put it back into deep sleep, as `countdown-client` does on the Pi.
   The board remembers the frame's `ETag` and sends it as `If-None-Match` on later polls, so the broker
   answers `304` until the frame changes. The ETag is kept in memory only: after a restart the
   first poll fetches and draws the frame again. A `304` (like any 1xx or `204`) has no body, so none
   is read: only its headers (`Retry-After`, `ETag`) are used.

   **Partial refresh (7.5" panel).** The board keeps the frame it last drew. When a new frame differs from it in
   a small window (at most 25% of the screen, as the smallest byte-aligned rectangle that holds every change),
   only that window is redrawn with the panel's fast waveform, with no full-screen flash (`PartialPolicy` in
   `lib/epd/src/epd_display.h`). It draws in full instead for the first frame after a boot (after the set-up
   screen too, as the next frame differs from it by far more than 25%), a bigger change, the frame after any
   failed draw, and after every 30 partial refreshes in a row, which clears the ghosting they leave.
   The broker still just sends whole frames; nothing in the protocol changes. The 7.8" IT8951 panel always
   refreshes in full.

## What each poll sends

```json
{
  "role": "display",
  "metrics": "esp32,device_id={device_id} uptime_s=3600i,free_heap=182344i,min_free_heap=150212i,free_psram=4100000i,wifi_rssi=-61i,reset_reason=1i,logs_dropped=0i",
  "logs": [{"service": "countdown-esp32", "host": "countdown", "level": "info", "_msg": "Frame response: HTTP 200 (48000 bytes), Retry-After: \"30\" seconds", "uptime_ms": 3599012}]
}
```

- **`metrics`:** one [InfluxDB Line Protocol](https://docs.influxdata.com/influxdb/v2/reference/syntax/line-protocol/)
  line, measurement `esp32`, every field an integer. It has no timestamp, so whatever stores it stamps
  it with the time it arrives (the board has no wall clock). `{device_id}` is left for the broker to fill
  in with the device it authenticated the poll as. The fields are `uptime_s`, `free_heap`, `min_free_heap`,
  `free_psram`, `wifi_rssi`, `reset_reason` and `logs_dropped`.
- **`logs`:** every line logged since the broker last answered, oldest first, from Wi-Fi set-up on.
  They're stamped with milliseconds since boot, since the board has no wall clock. Each line uses the
  standard log fields shared by everything shipped to VictoriaLogs: `service` (`countdown-esp32`), `host`
  (`kDeviceName` in `src/device_name.h`, the same on every board), `level` (`info`, `warn` or `error`) and
  `_msg`, plus `uptime_ms`. The broker replaces `service` and `host`, setting `host` and a `device_id` field to
  the device it authenticated the poll as, which is how boards are told apart, dates each line from its
  `uptime_ms`, and sends them to VictoriaLogs (`/insert/jsonline`, with `_stream_fields=service,host`).
  An answer of 200, 202, 304 or 404 clears them. When there's no answer, a 401, or anything
  else, they're kept for the next poll.
- **Limits:** to protect the heap during a long outage, the buffer holds at most 50 lines and 8 KB of messages (lines
  are cut at 512 bytes). Past that the oldest lines go, and `logs_dropped` counts them since boot.
  Serial still gets every line in full.
- **`reset_reason`:** ESP-IDF's `esp_reset_reason_t`: 1 power-on, 3 software restart, 4 panic,
  5–7 watchdogs, 8 deep sleep, 9 brownout.

## Connecting the e-paper panel

### 7.5" V2 (default)

The panel is the Waveshare 7.5" V2 (800×480, black and white), driven through Waveshare's
**e-Paper HAT**, the small board the panel's flat ribbon cable plugs into. On the Pi the HAT sits on the
pin header. On the ESP32 it's wired up through the 9-pin connector on its side, with the cable Waveshare
includes, or any female-to-female jumper wires.

Go by the labels printed on the HAT and on the ESP32 board's edge, not by wire colour:

| HAT  | ESP32   | What it does |
|------|---------|--------------|
| VCC  | 3V3     | Power (3.3 V, not 5 V) |
| GND  | GND     | Ground |
| DIN  | GPIO 14 | Data to the panel (SPI MOSI) |
| CLK  | GPIO 13 | SPI clock |
| CS   | GPIO 15 | Chip select |
| DC   | GPIO 33 | Command or data |
| RST  | GPIO 26 | Reset |
| BUSY | GPIO 25 | The panel says it's still working |
| PWR  | GPIO 32 | Turns the HAT on and off. Leave it unconnected on a HAT without a PWR pin |

- **Where the pinout comes from:** it follows Waveshare's own e-Paper ESP32 Driver Board, except DC,
  which moves from 27 to 33 because this board's RGB LED is on 27.
- **Pins it avoids:** GPIO 16 and 17 (the WROVER module's PSRAM) and the boot-sensitive pins 0, 2 and 12.
- **Changing pins:** each pin is an `EPD_PIN_*` build flag in `platformio.ini`. For a HAT without PWR,
  set `-DEPD_PIN_PWR=-1`.
- **The HAT's switches:** if the HAT has an "Interface Config" switch, set it to 0 (4-wire SPI).
  Leave "Display Config" as it was on the Pi.

If the panel isn't connected or powered, each frame logs `The e-paper panel didn't answer` after
10 seconds, and the board carries on polling.

### 7.8" IT8951 HAT

This is a different driver board and protocol, **not** a drop-in replacement for the 7.5" HAT.
Use the [Waveshare 7.8" IT8951 HAT](https://www.waveshare.com/wiki/7.8inch_e-Paper_HAT),
not a bare panel without its controller. Connect everything with power off and set the HAT to SPI mode.

| IT8951 HAT | ESP32 | What it does |
|------------|-------|--------------|
| 5V | Regulated 5 V supply | HAT power; **not the ESP32's 3V3 output** |
| GND | GND (shared with supply) | Common ground |
| MOSI | GPIO 14 | Data to controller |
| MISO | GPIO 34 | Data from controller (required) |
| SCK | GPIO 13 | SPI clock |
| CS | GPIO 15 | Chip select |
| RST | GPIO 26 | Reset |
| HRDY | GPIO 25 | Low while controller is busy |

The signals use 3.3 V logic; never apply 5 V to an ESP32 GPIO. Supply enough current for the HAT's
refresh demand (Waveshare lists about 1.2 W typical). There is no DC or switched PWR connection in this
environment. Sleeping the controller does not disconnect its external 5 V supply.

**VCOM must match the voltage printed on your panel's ribbon/label.** The default build preserves the
controller's existing value and logs it in millivolts; it cannot determine whether that value matches
your physical panel. Check it before use. If needed, add a build flag with the positive magnitude in
millivolts: a label of `-2.31 V` means `-DIT8951_VCOM_MV=2310` (example only, not a recommended voltage).
For a one-off build:

```sh
PLATFORMIO_BUILD_FLAGS='-DIT8951_VCOM_MV=2310' pio run -e esp-wrover-kit-it8951
```

The driver checks that the HAT reports 1872x1404 before loading an image. HRDY and refresh-status waits
are bounded; a failure closes SPI and resets the controller so the next poll can try again.

### ESP32 antenna

The board's ESP32-WROVER-IE module has no antenna of its own. Plug a 2.4 GHz antenna into its u.FL
connector, or Wi-Fi only reaches a few metres.

## Layout

- `platformio.ini`: build environments: `esp-wrover-kit` (the default), `esp-wrover-kit-it8951` and `native` (unit tests)
- `lib/broker_client/`: registering and polling for frames, in plain C++ with no Arduino, so it can be
  tested on the host. What it needs from the board (HTTP, NVS, randomness, sleeping, logging, a clock,
  metrics, a display) are the interfaces in `ports.h`. `log_buffer.h` keeps log lines until the broker has them
- `lib/epd/`: the shared `EpdPanel` interface, the 7.8" IT8951 driver, and the 7.5" V2 driver, ported command for command from
  `../countdown/epd/src/countdown_epd/epd7in5_V2.py`. It drives the HAT's wires through the `EpdBus`
  interface in `epd_bus.h`. IT8951 uses the bidirectional word-oriented `It8951Bus` instead
- `src/`: the ESP32 side: `main.cpp`, the ports' implementations (`esp_ports.cpp`), the Wi-Fi set-up
  portal (`wifi_setup.cpp`) and its set-up screen from the `setup` partition (`setup_partition.cpp`), the
  HAT's wires over SPI (`esp_epd_bus.cpp`) and the broker's pinned root certificates (`root_certs.h`)
- `lib/portal/`: `captive_probe.h`, which recognises Apple's connectivity-check hosts, and `setup_screen.h`,
  which reads the `setup` partition's header (plain C++, tested on the host)
- `partitions.csv`: the flash layout: the core's `huge_app.csv` (a 3 MB app slot), with 384 KB of its SPIFFS
  given to the `setup` partition
- `provision/`: `provision.py`, which writes a board's set-up screen, and its tests
- `test/test_broker_client/`: Unity tests for `lib/broker_client`, with fakes of the ports
- `test/test_epd/`: Unity tests for `lib/epd`, against a fake HAT that records every command and wire change
- `test/test_portal/`: Unity tests for `lib/portal`
- `include/`: project headers

## CI

`.github/workflows/firmware.yml` runs the native tests (uploading coverage to Codecov under the
`countdown-esp32` flag) and builds both panel variants on pushes to `main` and pull requests.
The `firmware` artifact contains application-only and `-merged.bin` standalone images, with the
environment and Git ref in each filename. Local builds generate the same merged images via
`scripts/merge_firmware.py`. `.github/workflows/provision.yml` runs the provisioning script's tests when
`provision/` or `partitions.csv` changes.
