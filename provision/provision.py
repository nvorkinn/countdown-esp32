"""Write the Wi-Fi set-up screen and portal password into a panel's `setup` partition."""

import argparse
import csv
import secrets
import struct
from pathlib import Path

import qrcode
from esptool.cmds import attach_flash, detect_chip, reset_chip, run_stub, write_flash
from PIL import Image, ImageDraw, ImageFont

# The firmware's partition table: the `setup` partition's offset and size come from it.
PARTITIONS = Path(__file__).parent.parent / "partitions.csv"

# What goes in the `setup` partition: this header, then the frame. The firmware reads it back with
# parseSetupHeader() in lib/portal/src/setup_screen.h.
SETUP_MAGIC = b"SET1"
SETUP_HEADER = struct.Struct("<4s64sI")  # the magic, the portal's password (NUL-padded), the frame's length

# Must match kDeviceName in src/device_name.h.
PORTAL_URL = "http://countdown.local/wifi"

# Every board's set-up screen, one PNG per network name: the record of which password went where. Git
# ignores it, as it holds the passwords.
SCREENS = Path(__file__).parent / "screens"

# The dashboard's system screens' font, so this screen looks like the pairing screen that follows it.
FONT = Path(__file__).parent / "fonts" / "Ubuntu-Medium.ttf"

# Must match each panel's kWidth and kHeight in lib/epd/src.
PANELS = {"7in5_v2": (800, 480), "it8951": (1872, 1404)}

# Letters and digits that can't be mistaken for each other, for anyone typing the password by hand.
PASSWORD_ALPHABET = "abcdefghjkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789"


def setup_network_name(mac: tuple[int, ...]) -> str:
    # Must match portalName() in src/wifi_setup.cpp: the last two bytes of the MAC, in upper-case hex.
    return f"ePaper-Dashboard-{mac[4]:02X}{mac[5]:02X}"


def new_password(length: int = 12) -> str:
    return "".join(secrets.choice(PASSWORD_ALPHABET) for _ in range(length))


def qr_code(data: str, size: int) -> Image.Image:
    qr = qrcode.QRCode(border=0)
    qr.add_data(data)
    matrix = qr.get_matrix()
    modules = len(matrix)
    code = Image.new("L", (modules, modules), 255)
    for y, row in enumerate(matrix):
        for x, dark in enumerate(row):
            if dark:
                code.putpixel((x, y), 0)
    box = size // modules
    return code.resize((modules * box, modules * box), Image.Resampling.NEAREST)


def setup_screen(width: int, height: int, network: str, password: str) -> Image.Image:
    # Laid out for 800x480, scaled to fit the panel both ways (the 7.8" is 4:3, not 5:3) and centred
    # top to bottom.
    scale = min(width / 800, height / 480)
    margin = round((height - 480 * scale) / 2)

    def font(size: int) -> ImageFont.FreeTypeFont:
        return ImageFont.truetype(FONT, round(size * scale))

    screen = Image.new("L", (width, height), 255)
    draw = ImageDraw.Draw(screen)
    draw.text((width // 2, margin + round(20 * scale)), "Set up this display", font=font(44), fill=0, anchor="mt")
    steps = [
        (f"WIFI:T:WPA;S:{network};P:{password};;", "1. Join the Wi-Fi", [network, f"Password: {password}"]),
        (PORTAL_URL, "2. Open the set-up page", [PORTAL_URL.removeprefix("http://")]),
    ]
    qr_size = round(240 * scale)
    top = margin + round(84 * scale)
    for column, (data, title, details) in enumerate(steps):
        centre = width * (2 * column + 1) // 4
        code = qr_code(data, qr_size)
        screen.paste(code, (centre - code.width // 2, top + (qr_size - code.height) // 2))
        y = top + qr_size + round(16 * scale)
        draw.text((centre, y), title, font=font(28), fill=0, anchor="mt")
        for line in details:
            y += round(36 * scale)
            draw.text((centre, y), line, font=font(24), fill=0, anchor="mt")
    return screen.convert("1", dither=Image.Dither.NONE)


def frame_bytes(screen: Image.Image) -> bytes:
    """The frame the panel takes, as the dashboard's panel_bytes() makes it: 1-bit rows, inverted (1 is black)."""
    return bytes(byte ^ 0xFF for byte in screen.tobytes())


def setup_data(password: str, frame: bytes) -> bytes:
    # The header's 64s field would silently cut a longer password, leaving the screen's QR code and the
    # board disagreeing; parseSetupHeader() rejects anything but 8 to 63 printable ASCII characters.
    if not (8 <= len(password) <= 63 and all(" " <= c <= "~" for c in password)):
        raise ValueError("The portal password must be 8 to 63 printable ASCII characters")
    return SETUP_HEADER.pack(SETUP_MAGIC, password.encode(), len(frame)) + frame


def setup_partition() -> tuple[int, int]:
    """The `setup` partition's offset and size, from partitions.csv."""
    with PARTITIONS.open() as table:
        for row in csv.reader(line for line in table if not line.startswith("#")):
            fields = [field.strip() for field in row]
            if fields and fields[0] == "setup":
                return int(fields[3], 0), int(fields[4], 0)
    raise SystemExit(f"No `setup` partition in {PARTITIONS}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="the panel's serial port, e.g. /dev/cu.usbmodem5C920451261")
    parser.add_argument("--panel", required=True, choices=PANELS, help="which e-paper panel the board drives")
    args = parser.parse_args()

    offset, size = setup_partition()
    with detect_chip(args.port) as esp:
        network = setup_network_name(esp.read_mac())
        password = new_password()
        screen = setup_screen(*PANELS[args.panel], network, password)
        SCREENS.mkdir(exist_ok=True)
        record = SCREENS / f"{network}.png"
        screen.save(record)
        data = setup_data(password, frame_bytes(screen))
        if len(data) > size:
            raise SystemExit(f"The set-up data is {len(data)} bytes; the `setup` partition holds {size}")
        esp = run_stub(esp)
        attach_flash(esp)
        write_flash(esp, [(offset, data)])
        reset_chip(esp, "hard-reset")

    print(f"{network}: wrote the set-up screen and password, and saved the screen as {record}")


if __name__ == "__main__":
    main()
