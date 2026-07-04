# ESP32 Wii Remote Emulator

Emulates a Nintendo Wii Remote (`RVL-CNT-01`) on an **ESP32 NodeMCU-32S** using Bluetooth Classic (BR/EDR).

## Phase 2 — Bluetooth + D-Pad Input

- ✅ Discoverable as `Nintendo RVL-CNT-01`
- ✅ Correct Class of Device (`0x002504`)
- ✅ Full SDP records (HID + PnP) with authentic Wiimote values
- ✅ Correct HID descriptor (raw bytes from a real Wii Remote)
- ✅ Wiimote PIN pairing protocol (reversed BDA)
- ✅ Link key persistence in NVS (bond survives reboot)
- ✅ Auto-reconnect to Wii after power cycle
- ✅ Status report (0x20) response
- ✅ ACK for data mode request (0x12)
- ✅ D-Pad input (Up / Down / Left / Right) via GPIO

---

## Hardware

- ESP32 NodeMCU-32S (any ESP32 with Bluetooth Classic)
- 4× momentary push buttons for D-Pad (connect GPIO to GND)

### D-Pad GPIO Wiring

| Direction | GPIO | Wire     |
|-----------|------|----------|
| Up        | 32   | GPIO→GND |
| Down      | 33   | GPIO→GND |
| Left      | 25   | GPIO→GND |
| Right     | 26   | GPIO→GND |

Internal pull-ups are enabled — no external resistors needed.
Change the pin numbers in `main/main.c` if your wiring differs.

---

## Build & Flash

```bash
# With PlatformIO
pio run --target upload

# With ESP-IDF directly
idf.py build flash monitor
```

---

## First-Time Pairing

1. Flash the ESP32
2. Open the battery cover of your Wii and press the red **SYNC** button
3. The Wii will discover `Nintendo RVL-CNT-01` and pair
4. D-Pad should work immediately after pairing

## Subsequent Connections

1. Power on the ESP32
2. Power on the Wii
3. The ESP32 automatically reconnects — no SYNC button needed

---

## Bluetooth Protocol Details

| Parameter | Value |
|-----------|-------|
| Device Name | `Nintendo RVL-CNT-01` |
| Class of Device | `0x002504` |
| Vendor ID | `0x057E` (Nintendo) |
| Product ID | `0x0306` |
| HID Control PSM | `0x11` |
| HID Interrupt PSM | `0x13` |
| Pairing PIN | Reversed own BDA (6 bytes, binary) |

### Core Button Report (0x30)

Byte 0 bitmask:
- `0x01` D-Pad Left
- `0x02` D-Pad Right
- `0x04` D-Pad Down
- `0x08` D-Pad Up
- `0x10` Plus (+)

Byte 1 bitmask:
- `0x01` 2
- `0x02` 1
- `0x04` B
- `0x08` A
- `0x10` Minus (−)
- `0x80` Home

---

## Project Structure

```
esp32-wiimote/
├── CMakeLists.txt
├── platformio.ini
├── sdkconfig.defaults
├── partitions_wiimote.csv
├── main/
│   ├── CMakeLists.txt
│   └── main.c                  # App entry + D-Pad GPIO task
└── components/
    └── wiimote_bt/
        ├── CMakeLists.txt
        ├── include/
        │   ├── wiimote_bt.h    # Public API + button bitmasks
        │   ├── wiimote_sdp.h   # SDP + HID descriptor
        │   ├── wiimote_linkkey.h
        │   └── wiimote_l2cap.h
        ├── wiimote_bt.c        # GAP + auth + button reports
        ├── wiimote_sdp.c       # SDP record registration
        ├── wiimote_linkkey.c   # NVS persistence
        └── wiimote_l2cap.c     # L2CAP PSM 0x11 + 0x13
```

---

## Future Phases

- Phase 3: Accelerometer (3-axis)
- Phase 4: LEDs + Rumble
- Phase 5: IR camera (4-point tracking)
- Phase 6: Extension port (Nunchuk, Classic Controller)

---

## References

- [WiiBrew — Wiimote](https://wiibrew.org/wiki/Wiimote)
- [xwiimote PROTOCOL](https://github.com/dvdhrm/xwiimote/blob/master/doc/PROTOCOL)
- [Linux hid-wiimote driver](https://github.com/torvalds/linux/tree/master/drivers/hid)
