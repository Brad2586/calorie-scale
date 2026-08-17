# Calorie Scale

A smart kitchen scale: an ESP32-CAM weighs food with a load cell (via HX711),
photographs it, and a web app identifies the food with Gemini and computes
total calories = calorie density x weight.

## Hardware

- AI-Thinker ESP32-CAM + OV2640 (on the ESP32-CAM-MB programmer board)
- Load cell + HX711 amplifier
- Wiring used by the firmware (change at the top of the sketch if yours differs):
  - HX711 `DT` -> GPIO 13
  - HX711 `SCK` -> GPIO 12
  - HX711 `VCC` -> 5V, `GND` -> GND

## 1. Flash the firmware

1. Install [Arduino IDE](https://www.arduino.cc/en/software).
2. In **File > Preferences > Additional boards manager URLs** add:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   then install **esp32** in the Boards Manager.
3. In the Library Manager install **HX711** by *Bogdan Necula (bogde)*.
4. In `firmware/CalorieScaleCam/`, copy `secrets.example.h` to `secrets.h` and
   fill in your 2.4 GHz Wi-Fi name and password (the ESP32 cannot join 5 GHz
   networks). Then open `CalorieScaleCam.ino` and check
   `HX711_DT_PIN` / `HX711_SCK_PIN` match your wiring.
5. Seat the ESP32-CAM on the MB board, plug it into the laptop, select board
   **AI Thinker ESP32-CAM** and the right COM port, then Upload.
6. Open the Serial Monitor at 115200 baud and press the RST button. Note the
   IP address it prints (e.g. `192.168.1.42`).

## 2. Calibrate the scale

The sketch ships with a placeholder `CALIBRATION_FACTOR = 420.0`. To find yours:

1. With nothing on the scale, visit `http://<ESP32-IP>/tare` in a browser.
2. Place an object of known weight (e.g. a 500 g water bottle) on the scale.
3. Visit `http://<ESP32-IP>/weight` and note the reported grams.
4. New factor = current factor x (reported grams / true grams).
   Example: factor 420, it reports 618 g for a true 500 g ->
   new factor = 420 x 618 / 500 = 519.1.
5. Update `CALIBRATION_FACTOR` in the sketch, re-upload, and verify.

## 3. Use the web app

1. Get a free Gemini API key at [aistudio.google.com/apikey](https://aistudio.google.com/apikey).
2. Open `web/index.html` in a browser (double-click it).
3. Click **Settings**, enter the ESP32 IP address and your API key, and Save.
4. Put food on the scale and click **Identify food & count calories**.
5. Click **Add to session log** to keep a running total for a meal.

The API key stays in your browser (localStorage) and is sent only to Google.

## Firmware HTTP API

| Endpoint | Returns |
|----------|---------|
| `/weight` | `{"grams": 123.4}` |
| `/photo` | JPEG capture (`?flash=1` fires the LED) |
| `/tare` | zeroes the scale |

## Troubleshooting

- **Weight reads `--` / offline**: check the ESP32 IP in Settings, and that the
  laptop and ESP32 are on the same Wi-Fi network.
- **`HX711 not responding`**: DT/SCK pins in the sketch don't match the wiring,
  or the HX711 isn't powered.
- **Camera init failed**: reseat the camera ribbon cable, then press RST.
- **Random reboots when taking photos**: power the board with a 5V supply that
  can deliver 1-2 A (a phone charger), not a weak USB hub port.
- **Wrong food identified**: improve lighting (try the flash option in
  Settings), move the camera 20-40 cm above the food, plain background helps.
