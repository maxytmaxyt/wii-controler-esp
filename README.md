# ESP32 Wii Remote Emulator

Emulates a Nintendo Wii Remote (`RVL-CNT-01`) on an **ESP32 NodeMCU-32S** using Bluetooth Classic (BR/EDR).

## Browser controls — Bluetooth + virtual buttons

- ✅ Discoverable as `Nintendo RVL-CNT-01`
- ✅ Correct Class of Device (`0x002504`)
- ✅ Classic HID device profile with the Wiimote report descriptor and PnP identity
- ✅ Correct HID descriptor (raw bytes from a real Wii Remote)
- ✅ Wiimote PIN pairing protocol (reversed BDA)
- ✅ Bluetooth bond persistence through Bluedroid NVS storage
- ✅ Auto-reconnect to Wii after power cycle
- ✅ Status report (0x20) + ACK for data mode (0x12)
- ✅ Browser controller with D-Pad, A/B/1/2, Plus, Minus and Home
- ✅ Touch, mouse and keyboard controls with simultaneous button presses

---

## Hardware

- ESP32 NodeMCU-32S (any ESP32 with Bluetooth Classic)
- Phone, tablet or computer with Wi-Fi and a modern browser

### Open the web controller

No external buttons or wiring are required. The ESP32 starts its own Wi-Fi
access point and hosts the controller page:

1. Connect your device to the Wi-Fi network **WiiRemote-ESP32**.
2. Enter the password **wiiremote**.
3. Open **http://192.168.4.1/** in a browser.
4. Click **Verbinden / Pairing starten** in the page.
5. To pair for the first time, press the **SYNC** button on the Wii console
   (behind the front cover), then hold buttons on the web page to control it.

The access point has no internet connection. If your device asks, stay
connected to the network.

---

## Build & Flash

```bash
# PlatformIO
pio run --target upload

# ESP-IDF
idf.py build flash monitor
```

---

## First-Time Pairing

1. Flash the ESP32 and connect to its web page.
2. Click **Verbinden / Pairing starten** to make the ESP32 discoverable.
3. Press the **SYNC** button on the Wii console (behind the front cover).
   Do not press SYNC on a Wii Remote; the ESP32 is emulating that remote.
4. The Wii discovers `Nintendo RVL-CNT-01` and pairs. Buttons work after
   the connection is established.

## Subsequent Connections

1. Power on the ESP32
2. Power on the Wii
3. Reconnects automatically — no SYNC needed

---

## Core Button Report (0x30)

### Byte 0 (D-Pad + Plus)

| Bit | Mask | Button      |
|-----|------|-------------|
| 0   | 0x01 | D-Pad Left  |
| 1   | 0x02 | D-Pad Right |
| 2   | 0x04 | D-Pad Down  |
| 3   | 0x08 | D-Pad Up    |
| 4   | 0x10 | Plus (+)    |

### Byte 1 (Face buttons)

| Bit | Mask | Button |
|-----|------|--------|
| 0   | 0x01 | 2      |
| 1   | 0x02 | 1      |
| 2   | 0x04 | B      |
| 3   | 0x08 | **A**  |
| 4   | 0x10 | Minus  |
| 7   | 0x80 | Home   |

---

## Future Phases

- Phase 3: Accelerometer (3-axis)
- Phase 4: LEDs + Rumble
- Phase 5: IR camera
- Phase 6: Nunchuk / Classic Controller extension

---

## References

- [WiiBrew — Wiimote](https://wiibrew.org/wiki/Wiimote)
- [xwiimote PROTOCOL](https://github.com/dvdhrm/xwiimote/blob/master/doc/PROTOCOL)
- [Linux hid-wiimote driver](https://github.com/torvalds/linux/tree/master/drivers/hid)
