# ESP32 Wii Remote Emulator

Emulates a Nintendo Wii Remote (`RVL-CNT-01`) on an **ESP32 NodeMCU-32S** using Bluetooth Classic (BR/EDR).

## Phase 2 — Bluetooth + D-Pad + A-Button

- ✅ Discoverable as `Nintendo RVL-CNT-01`
- ✅ Correct Class of Device (`0x002504`)
- ✅ Full SDP records (HID + PnP) with authentic Wiimote values
- ✅ Correct HID descriptor (raw bytes from a real Wii Remote)
- ✅ Wiimote PIN pairing protocol (reversed BDA)
- ✅ Link key persistence in NVS (bond survives reboot)
- ✅ Auto-reconnect to Wii after power cycle
- ✅ Status report (0x20) + ACK for data mode (0x12)
- ✅ D-Pad: Up / Down / Left / Right
- ✅ A-Button (confirm / select)

---

## Hardware

- ESP32 NodeMCU-32S (any ESP32 with Bluetooth Classic)
- 5× momentary push buttons

### Wiring

Each button connects the GPIO pin to **GND**.  
Internal pull-ups are enabled — **no external resistors needed**.

| Button      | GPIO | Board label (NodeMCU-32S) |
|-------------|------|---------------------------|
| D-Pad Up    | 18   | D18                       |
| D-Pad Down  | 19   | D19                       |
| D-Pad Left  | 21   | D21                       |
| D-Pad Right | 22   | D22                       |
| A Button    | 23   | D23                       |

To use different pins, edit the `#define GPIO_*` lines at the top of `main/main.c`.

**Avoid these pins** (reserved / boot-sensitive on ESP32):
- GPIO 0, 2, 5, 12, 15 — boot-strapping
- GPIO 6–11 — connected to internal flash
- GPIO 34–39 — input-only (no pull-up, usable if you add external resistors)

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

1. Flash the ESP32
2. Press the red **SYNC** button inside the Wii battery cover
3. The Wii discovers `Nintendo RVL-CNT-01` and pairs
4. Buttons work immediately after pairing

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
