# Robohand

A simple Arduino sketch to control a robotic hand (servo-based) — provided as `Robohand.ino`.

## Summary

This project contains an Arduino sketch that reads inputs (sensors/buttons/potentiometers) and drives servos to move a robotic hand. Use this README to set up the hardware, upload the sketch, and customize pin mappings or servo ranges.

## Files

- `Robohand.ino` — main Arduino sketch (open in Arduino IDE).

## Required Hardware

- Arduino board (Uno, Nano, Mega, etc.)
- 3–5 x hobby servos (depending on hand design)
- Power supply for servos (separate 5–6V recommended)
- Jumper wires, breadboard
- Optional: potentiometers, push buttons, flex sensors for control

## Wiring (example)

- Connect servo signal wires to Arduino digital pins (example):
  - Thumb: D3
  - Index: D5
  - Middle: D6
  - Ring: D9
  - Pinky: D10
- Connect servo power (V+) and ground to the servo power supply. Connect servo ground to Arduino GND.
- Connect control inputs (potentiometers or buttons) to analog pins A0–A2 as needed.

Adjust pin numbers in `Robohand.ino` to match your wiring.

## Software & Upload

1. Install the Arduino IDE from https://arduino.cc.
2. Open `Robohand.ino` in the Arduino IDE.
3. Select the correct board and port under `Tools` → `Board` / `Port`.
4. Connect the Arduino via USB and click Upload.

If using external servo power, ensure the external supply is connected and common-grounded with the Arduino before powering servos.

## Usage

- Once uploaded, control the hand using the configured inputs (potentiometers/buttons). The sketch maps inputs to servo positions; modify the mapping in the code if needed.

## Customization

- Open `Robohand.ino` and edit pin definitions and servo limits to match your hardware.
- Calibrate servo min/max values to prevent mechanical binding.

## Troubleshooting

- Servos jittering: provide a stable external power supply and common ground.
- Servo not moving: check signal pin, correct power, and that the sketch references the correct pin.

## License

MIT License — feel free to reuse and modify. Include attribution if you share.

## Contact

If you want help customizing the sketch or wiring, open an issue or reply here.

## ESP32-S3 (connection & run)

This sketch can run on an ESP32-S3-based board with a few small changes. Follow these steps to connect and upload:

1. Install ESP32 board support in the Arduino IDE:
  - Open `Tools` → `Board` → `Boards Manager` and install the "esp32" package by Espressif.
2. Power and wiring:
  - Servos typically need 5V. Use a separate 5–6V servo power supply and connect all grounds together (ESP32 GND ↔ servo power GND).
  - Connect each servo signal wire to a free ESP32-S3 GPIO. Example (change pins in the sketch to match):
    - Thumb: GPIO 18
    - Index: GPIO 19
    - Middle: GPIO 21
    - Ring: GPIO 22
    - Pinky: GPIO 23
  - Avoid pins reserved for flash/SPI (commonly GPIO6–GPIO11) and consult your board's pinout for S3-specific restrictions.
3. Library and code changes:
  - If `Robohand.ino` uses the AVR `Servo.h`, switch to an ESP32-compatible servo library such as `ESP32Servo` (install via Library Manager) or use a PWM/i2c driver (PCA9685) for many servos.
  - Update the pin definitions at the top of `Robohand.ino` to the GPIOs you wired.
  - Ensure servo pulse limits are appropriate for your servos (calibrate `min`/`max` values to avoid binding).
4. Uploading the sketch:
  - In Arduino IDE select `Tools` → `Board` → `ESP32S3 Dev Module` (or the specific board name) and the correct `Port`.
  - Click Upload. If required, press the boot/enable buttons as prompted by the IDE.
5. Troubleshooting & tips:
  - If servos twitch or reset the board, power them from a dedicated supply and keep a common ground.
  - For many servos, using a dedicated PWM driver (PCA9685) reduces load on the ESP32 and simplifies timing.
  - Use the Serial Monitor to view debug messages; set the baud rate in the sketch to match (commonly 115200).

These steps should get `Robohand.ino` running on an ESP32-S3 board. If you want, I can update `Robohand.ino` to include `ESP32Servo` examples and default ESP32 pin definitions.

## Control via Phone (Web Interface)

To control the robot hand from your phone using a web browser on the local network:

### Setup

1. **Connect ESP32-S3 to WiFi:**
   - Add WiFi credentials to `Robohand.ino`:
     ```cpp
     const char* ssid = "YOUR_WIFI_SSID";
     const char* password = "YOUR_WIFI_PASSWORD";
     ```
   - Initialize WiFi in `setup()`:
     ```cpp
     WiFi.begin(ssid, password);
     while (WiFi.status() != WL_CONNECTED) {
       delay(500);
     }
     Serial.println(WiFi.localIP());
     ```
   - Note the IP address printed to the Serial Monitor (e.g., `192.168.1.100`).

2. **Create a simple web server:**
   - Add these includes at the top of `Robohand.ino`:
     ```cpp
     #include <WiFi.h>
     #include <WebServer.h>
     ```
   - Create a WebServer instance:
     ```cpp
     WebServer server(80);  // Listen on port 80 (HTTP)
     ```
   - In `setup()`, define a handler that serves an HTML control panel:
     ```cpp
     server.on("/", HTTP_GET, []() {
       String html = "<html><body style='font-family:Arial'>"
         "<h1>Robohand Control</h1>"
         "<button onclick=\"fetch('/servo?pin=18&pos=90')\">Thumb Open</button>"
         "<button onclick=\"fetch('/servo?pin=18&pos=0')\">Thumb Close</button><br>"
         "<button onclick=\"fetch('/servo?pin=19&pos=90')\">Index Open</button>"
         "<button onclick=\"fetch('/servo?pin=19&pos=0')\">Index Close</button><br>"
         "</body></html>";
       server.send(200, "text/html", html);
     });
     ```
   - Add a servo control endpoint:
     ```cpp
     server.on("/servo", HTTP_GET, []() {
       int pin = server.arg("pin").toInt();
       int pos = server.arg("pos").toInt();
       // Attach servo and move to position
       // (details depend on your Servo library)
       server.send(200, "text/plain", "OK");
     });
     ```
   - Start the server in `setup()`:
     ```cpp
     server.begin();
     ```
   - In `loop()`, handle incoming requests:
     ```cpp
     server.handleClient();
     ```

3. **Access from your phone:**
   - Connect your device to the **`RoboHand-S3`** WiFi network (password: `12345678`).
   - Open a web browser and navigate to: **`http://192.168.4.1`**
   - No port number needed — it runs on port 80 (standard HTTP), so the browser uses it automatically.
   - You should see the Robohand control panel with buttons to open/close fingers.

4. **Customize the web interface:**
   - Add sliders for smooth servo control:
     ```html
     <input type="range" min="0" max="180" value="90" 
       onchange="fetch('/servo?pin=18&pos=' + this.value)">
     ```
   - Add visual feedback (color change, status display) using JavaScript.

### Troubleshooting

- **Phone can't find the ESP32:** Ensure both phone and ESP32-S3 are on the same WiFi network.
- **Web page doesn't load:** Check the IP address from the Serial Monitor (e.g., 192.168.1.), and make sure no firewall is blocking port 80.
- **Servos don't respond:** Verify the `/servo` endpoint is correctly wired to your servo control code.


