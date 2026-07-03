# ESP32 Wii Remote Emulator

Emulates a Nintendo Wii Remote (`RVL-CNT-01`) on an **ESP32 NodeMCU-32S** using Bluetooth Classic (BR/EDR).

## Phase 1 — Bluetooth Pairing & Persistent Connection

This phase implements only the Bluetooth layer:

- ✅ Discoverable as `Nintendo RVL-CNT-01`
- ✅ Correct Class of Device (`0x002504`)
- ✅ Full SDP records (HID + PnP) with authentic Wiimote values
- ✅ Correct HID descriptor (raw bytes from a real Wii Remote)
- ✅ Wiimote PIN pairing protocol (reversed BDA)
- ✅ Link key persistence in NVS (bond survives reboot)
- ✅ Auto-reconnect to Wii after power cycle
- ✅ Status report (0x20) response to keep the Wii happy

---

## Why ESP-IDF, not Arduino

The Arduino-ESP32 framework does not expose:

- `esp_bt_l2cap_bt_api.h` — raw L2CAP PSM registration
- `esp_sdp_api.h` — SDP record creation with raw attribute lists
- `esp_bt_gap_register_callback` with `PIN_REQ_EVT` and `LINK_KEY_*` events

These are all required to correctly emulate a Wiimote. ESP-IDF is mandatory.

---

## Hardware

- ESP32 NodeMCU-32S (any ESP32 with Bluetooth Classic)
- **No BLE module** — uses Bluetooth Classic (BR/EDR) only

---

## Bluetooth Protocol Details

All values sourced from:
- [WiiBrew Wiimote](https://wiibrew.org/wiki/Wiimote)
- [xwiimote PROTOCOL doc](https://github.com/dvdhrm/xwiimote/blob/master/doc/PROTOCOL)
- `hcidump` captures of real `RVL-CNT-01` hardware

| Parameter | Value |
|-----------|-------|
| Device Name | `Nintendo RVL-CNT-01` |
| Class of Device | `0x002504` |
| Vendor ID | `0x057E` (Nintendo) |
| Product ID | `0x0306` |
| HID Control PSM | `0x11` |
| HID Interrupt PSM | `0x13` |
| Pairing PIN | Reversed own BDA (6 bytes, binary) |

### PIN Pairing

The Wii Remote's pairing PIN is the **reverse of its own Bluetooth address** as raw bytes — not ASCII.

Example: BDA `AA:BB:CC:DD:EE:FF` → PIN = `{0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA}`

Source: [WiiBrew Wiimote#Bluetooth_Pairing](https://wiibrew.org/wiki/Wiimote#Bluetooth_Pairing)

### Auto-Reconnect

After successful pairing:
1. The Wii stores the ESP32's BDA
2. The ESP32 stores the Wii's BDA + link key in NVS
3. On next boot, the ESP32 initiates outbound L2CAP connections to the Wii
4. The Wii authenticates using the stored link key
5. No SYNC button press required

### Status Report (0x20)

After the interrupt channel opens, the Wii immediately sends report `0x15` (status request). The Wiimote must respond with report `0x20` within ~1 second or the Wii disconnects.

Our response: LED1 lit, battery = full (0x80), no extension, no speaker, no IR.

---

## Project Structure

```
esp32-wiimote/
├── CMakeLists.txt                     # Top-level ESP-IDF build
├── platformio.ini                     # PlatformIO configuration
├── sdkconfig.defaults                 # Bluetooth Classic config
├── partitions_wiimote.csv             # Flash partition table
├── main/
│   ├── CMakeLists.txt
│   └── main.c                         # Application entry point
└── components/
    └── wiimote_bt/
        ├── CMakeLists.txt
        ├── include/
        │   ├── wiimote_bt.h           # Public API + constants
        │   ├── wiimote_sdp.h          # SDP/HID descriptor
        │   ├── wiimote_linkkey.h      # NVS link key store
        │   └── wiimote_l2cap.h        # L2CAP channel management
        ├── wiimote_bt.c               # GAP + auth + pairing
        ├── wiimote_sdp.c              # SDP record registration
        ├── wiimote_linkkey.c          # NVS persistence
        └── wiimote_l2cap.c            # L2CAP PSM 0x11 + 0x13
```

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
4. After pairing the Wii assigns the remote to Player 1

## Subsequent Connections

1. Power on the ESP32
2. Power on the Wii
3. The ESP32 automatically reconnects — no SYNC button needed

---

## Future Phases

- Phase 2: Core buttons (A, B, 1, 2, +, -, Home, D-Pad)
- Phase 3: Accelerometer (3-axis)
- Phase 4: LEDs + Rumble
- Phase 5: IR camera (4-point tracking)
- Phase 6: Extension port (Nunchuk, Classic Controller)

---

## References

- [WiiBrew — Wiimote](https://wiibrew.org/wiki/Wiimote)
- [xwiimote PROTOCOL](https://github.com/dvdhrm/xwiimote/blob/master/doc/PROTOCOL)
- [Linux hid-wiimote driver](https://github.com/torvalds/linux/tree/master/drivers/hid)
- [Bluetooth Specs dump (yts.rdy.jp)](http://www.yts.rdy.jp/pic/GB002/Bluetooth_Specs.htm)
- [jloehr HID-Wiimote thesis](https://www.julianloehr.de/educational-work/hid-wiimote/)
