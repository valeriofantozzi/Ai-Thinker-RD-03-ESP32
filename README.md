## Ai-Thinker RD-03D on ESP32 🚀

A small project to use the Ai-Thinker RD-03D millimeter-wave radar with ESP32 and a simple browser-based GUI.

- **Platform**: ESP32 (HardwareSerial on `Serial2`)
- **Library**: `RadarSensor` adapted for multi-target parsing and ESP32
- **Viewer**: p5.js Web Serial GUI (`GUI/p5js-rd03`)

### Project layout 📁
```text
ESP32_RD03D/            # Example sketch using the adapted RadarSensor
  ├─ ESP32_RD03D.ino
  ├─ RadarSensor.cpp    # ESP32-capable parser + multi-target
  └─ RadarSensor.h
docs/
  └─ RD03D/             # Original, single-target SoftwareSerial version for reference
      ├─ RadarSensor.cpp
      └─ RadarSensor.h
GUI/
  └─ p5js-rd03/         # Web Serial viewer (Chrome)
      ├─ index.html
      ├─ sketch.js
      └─ serial.js
```

## ESP32 adaptation – high level 🛠️

The original library (`docs/RD03D/`) was designed for a single target over `SoftwareSerial` and a fixed 24-byte payload. The adapted version in `ESP32_RD03D/` focuses on ESP32 and robustness:

- **ESP32 serial backend**: Uses `Serial2` (HardwareSerial) with configurable RX/TX pins instead of `SoftwareSerial`.
- **Parser robustness**: State-machine waits for header `AA FF 03 00` and consumes bytes until the trailer `55 CC` is found.
  - Accepts variable payload length (multiples of 8 bytes), not just a fixed 26 total.
  - Safer buffering with bounds checks.
- **Multi-target support (up to 3)**: Stores an array of `RadarTarget` instead of only one.
  - Optional short-term target hold to smooth brief dropouts.
  - Ignores obviously empty slots (very small X/Y or `y == 0`).
- **Distance/angle**:
  - Recomputes distance from `(x,y)` in millimeters.
  - Angle computed as `atan2(y, x)` in degrees (no extra rotation or sign flip).
- **Signed decoding**: Decodes 15-bit magnitude with MSB as sign bit per module’s format, applied consistently to X/Y/speed.
- **Mode control**: On ESP32 startup, sends UART commands to force the sensor into multi-target mode.
- **Debugging**: Optional raw payload dump (`RAW24:`) and per-slot raw values to help troubleshooting.

> Note: The original `docs/RD03D` version parsed only the first 8 bytes (one target) from a fixed 24-byte payload and applied an additional −90° rotation and inversion to the angle. The ESP32 version switches to native `atan2(y,x)` degrees and multi-target parsing.

## Hardware and wiring 🔌

- Sensor UART -> ESP32 `Serial2`
  - ESP32 RX2: GPIO 16 (default in example)
  - ESP32 TX2: GPIO 17 (default in example)
  - GND ↔ GND, VCC per sensor specs (3.3V logic)
- USB cable for serial monitor and flashing

Baud rates used:
- USB Serial monitor (PC ↔ ESP32): `115200`
- Sensor UART (ESP32 ↔ RD-03D on Serial2): `256000`

## Build and flash ▶️

1. Open `ESP32_RD03D/ESP32_RD03D.ino` in Arduino IDE (or PlatformIO).
2. Select your ESP32 board and the correct COM/tty port.
3. Flash the sketch.
4. Open the Serial Monitor at `115200` baud to see human-readable output:
   - Multi-target blocks like:
     - `Targets: N` then for each `[i]` the `X (mm)`, `Y (mm)`, `Distance (mm)`, `Angle (degrees)`, `Speed (cm/s)`.

## Web GUI (p5.js) 🖥️🎨

The GUI reads the same human-readable lines (or JSON lines if you emit them) via Web Serial.

How to use (serve over localhost; Web Serial requires a secure context):
1. Start a static server, then open the page in Chrome:
   - From project root:
     - `python3 -m http.server 8000`
     - Open `http://localhost:8000/GUI/p5js-rd03/index.html`
   - Or from the GUI folder:
     - `cd GUI/p5js-rd03 && python3 -m http.server 8000`
     - Open `http://localhost:8000/index.html`
2. Click "Connect" and choose your ESP32’s USB serial port.
3. Set baud to `115200` to match the Serial Monitor output.
4. Toggle options:
   - Half FOV 180° (semi-circle top view)
   - Scale (mm/px)
   - Trail visualization and raw log
5. You’ll see tracked points plotted in mm, with live info on the left and a message log at the bottom.

Parsing behavior (GUI):
- Prefers JSON lines: `{ "t": [ {"x":int,"y":int,"d":float,"a":float,"s":float}, ... ] }`
- Falls back to the human-readable format printed by `ESP32_RD03D.ino`.

## Differences vs original library (summary) 🆚

- **Serial backend**: `SoftwareSerial` → ESP32 `Serial2` with pin configuration.
- **Targets**: Single target → up to 3 targets, with short hold across dropouts.
- **Parsing**: Fixed-length 26-byte frame → header+trailer framing and variable payload (8 bytes per target).
- **Filtering**: Ignore near-zero X/Y and slots where `y == 0` to reduce noise.
- **Math**: Angle now `atan2(y,x)` in degrees; distance recomputed from `(x,y)`.
- **Signed values**: Consistent MSB-sign decoding for X, Y, speed.
- **Mode control**: Sends multi-target command on boot when running on ESP32.
- **Debug**: Raw dumps and per-slot values for troubleshooting.

## Troubleshooting 🔍

- No points in GUI:
  - Ensure baud in GUI is `115200` (matches USB Serial output from the sketch).
  - Make sure the correct USB serial port is selected in Chrome.
  - Check wiring to `Serial2` (GPIO 16/17 by default) and common ground.
  - Leave "Raw log" enabled to inspect incoming lines.
- Unstable targets:
  - Reduce `RD03_ZERO_THRESH_MM` or adjust hold frames in `RadarSensor.h`.
  - Verify the module is in multi-target mode (the sketch sends the command on boot).

## Credits 🙌

- Ai-Thinker RD-03D module and documentation (see `docs/documentations`).
- Original single-target reference under `docs/RD03D/`.
- p5.js for the browser renderer.
