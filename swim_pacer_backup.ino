/*
  ESP32 WiFi-Configurable LED Strip Swim Pacer

  This code creates a configurable pulse of light that travels down the strip
  and bounces back. Settings can be changed via WiFi web interface.

  Hardware:
  - ESP32 Development Board
  - WS2812B LED Strip
  - External 5V Power Supply for LED strip

  Wiring:
  - LED Strip +5V to External 5V Power Supply
  - LED Strip GND to ESP32 GND AND Power Supply GND
  - LED Strip Data to ESP32 Pin 18 (configurable)

  WiFi Setup:
  1. ESP32 creates WiFi network: "SwimPacer_Config"
  2. Connect to this network (no password)
  3. Open browser to 192.168.4.1
  4. Configure settings via web interface

  Required Libraries: FastLED (install via Arduino IDE Library Manager)
*/

#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// ========== HARDWARE CONFIGURATION ==========
#define LED_PIN         18          // Data pin connected to the strip (ESP32 pin)
#define LED_TYPE        WS2812B     // LED strip type
#define COLOR_ORDER     GRB         // Color order (may need adjustment)

// ========== WIFI CONFIGURATION ==========
const char* ssid = "SwimPacer_Config";        // WiFi network name
const char* password = "";                    // No password for easy access
IPAddress local_IP(192, 168, 4, 1);         // ESP32 IP address
IPAddress gateway(192, 168, 4, 1);          // Gateway IP
IPAddress subnet(255, 255, 255, 0);         // Subnet mask

// ========== DEFAULT SETTINGS ==========
struct Settings {
  int totalLEDs = 150;                       // Total number of LEDs
  int ledsPerMeter = 30;                     // LEDs per meter
  float pulseWidthFeet = 1.0;                // Width of pulse in feet
  float speedFeetPerSecond = 5.56;           // Speed in feet per second
  uint8_t colorRed = 0;                      // RGB color values
  uint8_t colorGreen = 0;
  uint8_t colorBlue = 255;
  uint8_t brightness = 100;                  // Overall brightness (0-255)
  bool isRunning = false;                    // Whether the effect is active (default: stopped)
};

Settings settings;
Preferences preferences;
WebServer server(80);

// ========== CALCULATED VALUES ==========
float ledSpacingCM;
int pulseWidthLEDs;
int delayMS;

// ========== GLOBAL VARIABLES ==========
CRGB* leds;
int currentPosition = 0;
int direction = 1;
unsigned long lastUpdate = 0;
bool needsRecalculation = true;

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Swim Pacer Starting...");

  // Initialize preferences (flash storage)
  preferences.begin("swim_pacer", false);
  loadSettings();

  // Initialize LED strip
  setupLEDs();

  // Setup WiFi Access Point
  setupWiFi();

  // Setup web server
  setupWebServer();

  // Calculate initial values
  recalculateValues();

  Serial.println("Setup complete!");
  Serial.println("Connect to WiFi: " + String(ssid));
  Serial.println("Open browser to: http://192.168.4.1");
}

void loop() {
  server.handleClient();  // Handle web requests

  if (settings.isRunning) {
    updateLEDEffect();
  } else {
    // If stopped, just clear the strip
    FastLED.clear();
    FastLED.show();
  }

  // Recalculate if settings changed
  if (needsRecalculation) {
    recalculateValues();
    needsRecalculation = false;
  }
}

void setupLEDs() {
  // Allocate LED array dynamically based on total LEDs
  if (leds != nullptr) {
    delete[] leds;
  }
  leds = new CRGB[settings.totalLEDs];

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, settings.totalLEDs);
  FastLED.setBrightness(settings.brightness);
  FastLED.clear();
  FastLED.show();
}

void setupWiFi() {
  Serial.println("Setting up WiFi Access Point...");

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ssid, password);

  Serial.println("WiFi AP Started");
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());
}

void setupWebServer() {
  // Serve the main configuration page
  server.on("/", handleRoot);

  // Handle settings updates
  server.on("/update", HTTP_POST, handleUpdate);

  // Handle getting current settings (for dynamic updates)
  server.on("/settings", HTTP_GET, handleGetSettings);

  // Handle start/stop commands
  server.on("/control", HTTP_POST, handleControl);

  server.begin();
  Serial.println("Web server started");
}

void handleRoot() {
  String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Swim Pacer Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 20px; background: #f0f0f0; }
        .container { background: white; padding: 20px; border-radius: 10px; max-width: 500px; margin: 0 auto; }
        .control { margin: 15px 0; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        input, button { padding: 8px; border-radius: 5px; border: 1px solid #ccc; width: 100%; box-sizing: border-box; }
        button { background: #007bff; color: white; cursor: pointer; margin: 5px 0; }
        button:hover { background: #0056b3; }
        .status { padding: 10px; border-radius: 5px; margin: 10px 0; }
        .running { background: #d4edda; color: #155724; }
        .stopped { background: #f8d7da; color: #721c24; }
        .color-preview { width: 50px; height: 30px; border: 1px solid #ccc; display: inline-block; margin-left: 10px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🏊‍♂️ Swim Pacer Configuration</h1>

        <div class="status" id="status">Loading...</div>

        <div class="control">
            <button onclick="togglePacer()" id="toggleBtn">Toggle</button>
        </div>

        <form onsubmit="updateSettings(event)">
            <div class="control">
                <label>Total LEDs:</label>
                <input type="number" id="totalLEDs" min="1" max="1000">
            </div>

            <div class="control">
                <label>LEDs per Meter:</label>
                <input type="number" id="ledsPerMeter" min="1" max="200">
            </div>

            <div class="control">
                <label>Pulse Width (feet):</label>
                <input type="number" id="pulseWidthFeet" step="0.1" min="0.1" max="10">
            </div>

            <div class="control">
                <label>Speed (feet per second):</label>
                <input type="number" id="speedFeetPerSecond" step="0.1" min="0.1" max="20">
            </div>

            <div class="control">
                <label>Color - Red (0-255):</label>
                <input type="number" id="colorRed" min="0" max="255">
            </div>

            <div class="control">
                <label>Color - Green (0-255):</label>
                <input type="number" id="colorGreen" min="0" max="255">
            </div>

            <div class="control">
                <label>Color - Blue (0-255):</label>
                <input type="number" id="colorBlue" min="0" max="255">
                <div class="color-preview" id="colorPreview"></div>
            </div>

            <div class="control">
                <label>Brightness (0-255):</label>
                <input type="number" id="brightness" min="0" max="255">
            </div>

            <button type="submit">Update Settings</button>
        </form>
    </div>

    <script>
        function loadSettings() {
            fetch('/settings')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('totalLEDs').value = data.totalLEDs;
                    document.getElementById('ledsPerMeter').value = data.ledsPerMeter;
                    document.getElementById('pulseWidthFeet').value = data.pulseWidthFeet;
                    document.getElementById('speedFeetPerSecond').value = data.speedFeetPerSecond;
                    document.getElementById('colorRed').value = data.colorRed;
                    document.getElementById('colorGreen').value = data.colorGreen;
                    document.getElementById('colorBlue').value = data.colorBlue;
                    document.getElementById('brightness').value = data.brightness;

                    updateStatus(data.isRunning);
                    updateColorPreview();
                });
        }

        function updateSettings(event) {
            event.preventDefault();

            const formData = new FormData();
            formData.append('totalLEDs', document.getElementById('totalLEDs').value);
            formData.append('ledsPerMeter', document.getElementById('ledsPerMeter').value);
            formData.append('pulseWidthFeet', document.getElementById('pulseWidthFeet').value);
            formData.append('speedFeetPerSecond', document.getElementById('speedFeetPerSecond').value);
            formData.append('colorRed', document.getElementById('colorRed').value);
            formData.append('colorGreen', document.getElementById('colorGreen').value);
            formData.append('colorBlue', document.getElementById('colorBlue').value);
            formData.append('brightness', document.getElementById('brightness').value);

            fetch('/update', {
                method: 'POST',
                body: formData
            }).then(() => {
                alert('Settings updated!');
                loadSettings();
            });
        }

        function togglePacer() {
            const formData = new FormData();
            formData.append('action', 'toggle');

            fetch('/control', {
                method: 'POST',
                body: formData
            }).then(() => {
                loadSettings();
            });
        }

        function updateStatus(isRunning) {
            const status = document.getElementById('status');
            const toggleBtn = document.getElementById('toggleBtn');

            if (isRunning) {
                status.textContent = 'Status: RUNNING';
                status.className = 'status running';
                toggleBtn.textContent = 'Stop Pacer';
            } else {
                status.textContent = 'Status: STOPPED';
                status.className = 'status stopped';
                toggleBtn.textContent = 'Start Pacer';
            }
        }

        function updateColorPreview() {
            const r = document.getElementById('colorRed').value;
            const g = document.getElementById('colorGreen').value;
            const b = document.getElementById('colorBlue').value;

            document.getElementById('colorPreview').style.backgroundColor =
                `rgb(${r}, ${g}, ${b})`;
        }

        // Update color preview when values change
        document.getElementById('colorRed').addEventListener('input', updateColorPreview);
        document.getElementById('colorGreen').addEventListener('input', updateColorPreview);
        document.getElementById('colorBlue').addEventListener('input', updateColorPreview);

        // Load initial settings
        loadSettings();
    </script>
</body>
</html>
)";

  server.send(200, "text/html", html);
}

void handleUpdate() {
  // Update settings from web form
  if (server.hasArg("totalLEDs")) {
    settings.totalLEDs = server.arg("totalLEDs").toInt();
    setupLEDs(); // Reinitialize LED array
  }
  if (server.hasArg("ledsPerMeter")) settings.ledsPerMeter = server.arg("ledsPerMeter").toInt();
  if (server.hasArg("pulseWidthFeet")) settings.pulseWidthFeet = server.arg("pulseWidthFeet").toFloat();
  if (server.hasArg("speedFeetPerSecond")) settings.speedFeetPerSecond = server.arg("speedFeetPerSecond").toFloat();
  if (server.hasArg("colorRed")) settings.colorRed = server.arg("colorRed").toInt();
  if (server.hasArg("colorGreen")) settings.colorGreen = server.arg("colorGreen").toInt();
  if (server.hasArg("colorBlue")) settings.colorBlue = server.arg("colorBlue").toInt();
  if (server.hasArg("brightness")) {
    settings.brightness = server.arg("brightness").toInt();
    FastLED.setBrightness(settings.brightness);
  }

  saveSettings();
  needsRecalculation = true;

  server.send(200, "text/plain", "Settings updated");
}

void handleGetSettings() {
  String json = "{";
  json += "\"totalLEDs\":" + String(settings.totalLEDs) + ",";
  json += "\"ledsPerMeter\":" + String(settings.ledsPerMeter) + ",";
  json += "\"pulseWidthFeet\":" + String(settings.pulseWidthFeet) + ",";
  json += "\"speedFeetPerSecond\":" + String(settings.speedFeetPerSecond) + ",";
  json += "\"colorRed\":" + String(settings.colorRed) + ",";
  json += "\"colorGreen\":" + String(settings.colorGreen) + ",";
  json += "\"colorBlue\":" + String(settings.colorBlue) + ",";
  json += "\"brightness\":" + String(settings.brightness) + ",";
  json += "\"isRunning\":" + String(settings.isRunning ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

void handleControl() {
  if (server.hasArg("action")) {
    String action = server.arg("action");
    if (action == "toggle") {
      settings.isRunning = !settings.isRunning;
      saveSettings();
    }
  }

  server.send(200, "text/plain", "Control updated");
}

void saveSettings() {
  preferences.putInt("totalLEDs", settings.totalLEDs);
  preferences.putInt("ledsPerMeter", settings.ledsPerMeter);
  preferences.putFloat("pulseWidthFeet", settings.pulseWidthFeet);
  preferences.putFloat("speedFPS", settings.speedFeetPerSecond);
  preferences.putUChar("colorRed", settings.colorRed);
  preferences.putUChar("colorGreen", settings.colorGreen);
  preferences.putUChar("colorBlue", settings.colorBlue);
  preferences.putUChar("brightness", settings.brightness);
  preferences.putBool("isRunning", settings.isRunning);

  Serial.println("Settings saved to flash memory");
}

void loadSettings() {
  settings.totalLEDs = preferences.getInt("totalLEDs", 150);
  settings.ledsPerMeter = preferences.getInt("ledsPerMeter", 30);
  settings.pulseWidthFeet = preferences.getFloat("pulseWidthFeet", 1.0);
  settings.speedFeetPerSecond = preferences.getFloat("speedFPS", 5.56);
  settings.colorRed = preferences.getUChar("colorRed", 0);
  settings.colorGreen = preferences.getUChar("colorGreen", 0);
  settings.colorBlue = preferences.getUChar("colorBlue", 255);
  settings.brightness = preferences.getUChar("brightness", 100);
  settings.isRunning = preferences.getBool("isRunning", false);  // Default: stopped

  Serial.println("Settings loaded from flash memory");
}

void recalculateValues() {
  ledSpacingCM = 100.0 / settings.ledsPerMeter;
  float pulseWidthCM = settings.pulseWidthFeet * 30.48;
  pulseWidthLEDs = (int)(pulseWidthCM / ledSpacingCM);
  float speedCMPerSecond = settings.speedFeetPerSecond * 30.48;
  delayMS = (int)(ledSpacingCM / speedCMPerSecond * 1000);

  // Reset position if it's out of bounds
  if (currentPosition >= settings.totalLEDs) {
    currentPosition = settings.totalLEDs - 1;
  }

  Serial.println("Values recalculated:");
  Serial.println("  LED spacing: " + String(ledSpacingCM) + " cm");
  Serial.println("  Pulse width: " + String(pulseWidthLEDs) + " LEDs");
  Serial.println("  Update delay: " + String(delayMS) + " ms");
}

void updateLEDEffect() {
  unsigned long currentTime = millis();

  if (currentTime - lastUpdate >= delayMS) {
    lastUpdate = currentTime;

    // Clear all LEDs
    FastLED.clear();

    // Draw the current pulse
    drawPulse(currentPosition);

    // Update FastLED
    FastLED.show();

    // Move to next position
    currentPosition += direction;

    // Check for bouncing at ends
    if (currentPosition >= settings.totalLEDs - 1) {
      direction = -1;
      currentPosition = settings.totalLEDs - 1;
    } else if (currentPosition <= 0) {
      direction = 1;
      currentPosition = 0;
    }
  }
}

void drawPulse(int centerPos) {
  int halfWidth = pulseWidthLEDs / 2;
  CRGB pulseColor = CRGB(settings.colorRed, settings.colorGreen, settings.colorBlue);

  for (int i = 0; i < pulseWidthLEDs; i++) {
    int ledIndex = centerPos - halfWidth + i;

    if (ledIndex >= 0 && ledIndex < settings.totalLEDs) {
      int distanceFromCenter = abs(i - halfWidth);
      float brightnessFactor = 1.0 - (float)distanceFromCenter / (float)halfWidth;
      brightnessFactor = max(0.0f, brightnessFactor);

      CRGB color = pulseColor;
      color.nscale8((uint8_t)(brightnessFactor * 255));

      leds[ledIndex] = color;
    }
  }
}