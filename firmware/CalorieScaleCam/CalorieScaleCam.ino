// Calorie Scale - ESP32-CAM firmware
//
// One AI-Thinker ESP32-CAM board does everything:
//   - reads weight from a load cell through an HX711
//   - captures photos with the OV2640 camera
//   - serves both over Wi-Fi as a tiny HTTP API for the web app
//
// HTTP API (all responses allow cross-origin requests):
//   GET /         -> status page (quick sanity check in a browser)
//   GET /weight   -> {"grams": 123.4}
//   GET /photo    -> JPEG image  (add ?flash=1 to fire the onboard LED)
//   GET /tare     -> zeroes the scale, returns {"ok":true}
//
// Required Arduino libraries (Library Manager):
//   - "HX711" by Bogdan Necula (bogde)
// Board: "AI Thinker ESP32-CAM"

#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "HX711.h"

// ======================= USER SETTINGS =======================

// Wi-Fi credentials live in secrets.h (not committed to git).
// Copy secrets.example.h to secrets.h and fill in your network.
#include "secrets.h"

// HX711 pins - CHANGE THESE to match your wiring.
// Safe choices on the ESP32-CAM (when no SD card is used): 13, 14, 15, 2.
// Avoid GPIO 0, 1, 3, 4 (LED) and the camera pins.
// Note: GPIO 12 is a boot strapping pin. It works for HX711 SCK, but if the
// board ever fails to boot with the HX711 attached, move SCK to GPIO 14 or 15.
#define HX711_DT_PIN  13   // HX711 DT / DOUT
#define HX711_SCK_PIN 12   // HX711 SCK

// Calibration factor: raw HX711 units per gram.
// See README "Calibrating the scale" - you will replace this number.
float CALIBRATION_FACTOR = 420.0f;

// =============================================================

// AI-Thinker ESP32-CAM camera pins
#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM   0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM     5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22

#define FLASH_LED_PIN   4

WebServer server(80);
HX711 scale;

void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleOptions() {
  addCorsHeaders();
  server.send(204);
}

void handleRoot() {
  addCorsHeaders();
  server.send(200, "text/html",
    "<h1>Calorie Scale is running</h1>"
    "<p><a href='/weight'>/weight</a> | <a href='/photo'>/photo</a> | <a href='/tare'>/tare</a></p>");
}

void handleWeight() {
  addCorsHeaders();
  if (!scale.wait_ready_timeout(1000)) {
    server.send(503, "application/json", "{\"error\":\"HX711 not responding - check wiring\"}");
    return;
  }
  float grams = scale.get_units(3);  // average of 3 readings
  server.send(200, "application/json", "{\"grams\":" + String(grams, 1) + "}");
}

void handleTare() {
  addCorsHeaders();
  if (!scale.wait_ready_timeout(1000)) {
    server.send(503, "application/json", "{\"error\":\"HX711 not responding - check wiring\"}");
    return;
  }
  scale.tare(10);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handlePhoto() {
  bool useFlash = server.hasArg("flash") && server.arg("flash") == "1";
  if (useFlash) {
    digitalWrite(FLASH_LED_PIN, HIGH);
    delay(120);  // let exposure adjust
  }

  // Discard one frame so we return a fresh capture, not a stale buffer.
  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) {
    esp_camera_fb_return(fb);
  }
  fb = esp_camera_fb_get();

  if (useFlash) {
    digitalWrite(FLASH_LED_PIN, LOW);
  }

  if (!fb) {
    addCorsHeaders();
    server.send(503, "application/json", "{\"error\":\"camera capture failed\"}");
    return;
  }

  addCorsHeaders();
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");
  server.client().write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk    = XCLK_GPIO_NUM;
  config.pin_pclk    = PCLK_GPIO_NUM;
  config.pin_vsync   = VSYNC_GPIO_NUM;
  config.pin_href    = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn    = PWDN_GPIO_NUM;
  config.pin_reset   = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_SVGA;  // 800x600 - plenty for food recognition
    config.jpeg_quality = 12;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size   = FRAMESIZE_VGA;
    config.jpeg_quality = 15;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_DRAM;
  }

  return esp_camera_init(&config) == ESP_OK;
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Calorie Scale starting...");

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  if (!initCamera()) {
    Serial.println("ERROR: camera init failed. Check the ribbon cable, then press RST.");
  } else {
    Serial.println("Camera OK");
  }

  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);
  if (scale.wait_ready_timeout(2000)) {
    scale.set_scale(CALIBRATION_FACTOR);
    scale.tare(10);
    Serial.println("HX711 OK (tared)");
  } else {
    Serial.println("ERROR: HX711 not found. Check DT/SCK pins and 5V/GND wiring.");
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! Open the web app and enter this IP: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("caloriescale")) {
    Serial.println("Also reachable at http://caloriescale.local");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/weight", HTTP_GET, handleWeight);
  server.on("/photo", HTTP_GET, handlePhoto);
  server.on("/tare", HTTP_GET, handleTare);
  server.on("/tare", HTTP_POST, handleTare);
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      handleOptions();
    } else {
      addCorsHeaders();
      server.send(404, "application/json", "{\"error\":\"not found\"}");
    }
  });
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}
