"""What provision.py writes must be what the firmware reads: see lib/portal/src/setup_screen.h."""

import pytest
from PIL import Image

import provision


def test_network_name_is_the_last_two_mac_bytes_in_upper_case_hex():
    # As portalName() in src/wifi_setup.cpp builds it from WiFi.macAddress().
    assert provision.setup_network_name((0x24, 0x0A, 0xC4, 0x12, 0x36, 0x88)) == "ePaper-Dashboard-3688"
    assert provision.setup_network_name((0, 0, 0, 0, 0x0A, 0xBC)) == "ePaper-Dashboard-0ABC"


def test_password_suits_wpa2_and_typing_by_hand():
    password = provision.new_password()
    assert 8 <= len(password) <= 63
    assert set(password) <= set(provision.PASSWORD_ALPHABET)
    assert not set("0O1lI") & set(provision.PASSWORD_ALPHABET)


def test_passwords_differ():
    assert provision.new_password() != provision.new_password()


def test_header_is_what_the_firmware_parses():
    # kSetupHeaderBytes in lib/portal/src/setup_screen.h.
    assert provision.SETUP_HEADER.size == 72
    data = provision.setup_data("JzMqG675qsVd", bytes(48000))
    assert data[:4] == b"SET1"
    assert data[4:68] == b"JzMqG675qsVd".ljust(64, b"\0")
    assert data[68:72] == (48000).to_bytes(4, "little")
    assert len(data) == 72 + 48000


def test_frame_bits_are_one_for_black():
    # As the dashboard's panel_bytes() packs every other frame: MSB first, 1 black, 0 white.
    image = Image.new("1", (16, 2), 1)
    image.putpixel((0, 1), 0)
    assert provision.frame_bytes(image) == bytes([0x00, 0x00, 0x80, 0x00])


@pytest.mark.parametrize("panel", provision.PANELS)
def test_screen_is_a_whole_frame_for_the_panel(panel):
    width, height = provision.PANELS[panel]
    screen = provision.setup_screen(width, height, "ePaper-Dashboard-3688", "JzMqG675qsVd")
    assert screen.mode == "1"
    assert screen.size == (width, height)
    assert len(provision.frame_bytes(screen)) == width // 8 * height


@pytest.mark.parametrize("panel", provision.PANELS)
def test_setup_data_fits_the_partition(panel):
    offset, size = provision.setup_partition()
    assert (offset, size) == (0x390000, 0x60000)
    screen = provision.setup_screen(*provision.PANELS[panel], "ePaper-Dashboard-3688", "x" * 12)
    assert len(provision.setup_data("x" * 12, provision.frame_bytes(screen))) <= size


def test_qr_code_has_whole_pixel_modules_within_the_size():
    code = provision.qr_code(provision.PORTAL_URL, 240)
    assert code.width == code.height <= 240
    assert code.getpixel((0, 0)) == 0  # a finder pattern's corner, with no border round it


def test_missing_setup_partition(tmp_path, monkeypatch):
    table = tmp_path / "partitions.csv"
    table.write_text("# Name, Type, SubType, Offset, Size\nnvs, data, nvs, 0x9000, 0x5000,\n")
    monkeypatch.setattr(provision, "PARTITIONS", table)
    with pytest.raises(SystemExit):
        provision.setup_partition()
