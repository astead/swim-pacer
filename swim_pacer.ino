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

  // New simplified handlers for the updated interface
  server.on("/toggle", HTTP_POST, handleToggle);
  server.on("/setSpeed", HTTP_POST, handleSetSpeed);
  server.on("/setColor", HTTP_POST, handleSetColor);
  server.on("/setBrightness", HTTP_POST, handleSetBrightness);
  server.on("/setPulseWidth", HTTP_POST, handleSetPulseWidth);

  server.begin();
  Serial.println("Web server started");
}

void handleRoot() {
  String html = R"(
<!-- ========== SYNC MARKER: START ESP32 HTML ========== -->
<!-- This HTML content is synchronized with swim_pacer.ino handleRoot() function -->
<!-- When editing, ensure both files stay identical between sync markers -->
<!DOCTYPE html>
<html>
<head>
    <title>Swim Pacer</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 20px; background: #f0f0f0; }
        .container { background: white; padding: 20px; border-radius: 10px; max-width: 500px; margin: 0 auto; }
        .control { margin: 15px 0; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        input, button, select { padding: 8px; border-radius: 5px; border: 1px solid #ccc; width: 100%; box-sizing: border-box; }
        button { background: #007bff; color: white; cursor: pointer; margin: 5px 0; }
        button:hover { background: #0056b3; }
        button.secondary { background: #6c757d; }
        button.secondary:hover { background: #545b62; }
        .status { padding: 10px; border-radius: 5px; margin: 10px 0; }
        .running { background: #d4edda; color: #155724; }
        .stopped { background: #f8d7da; color: #721c24; }
        .color-preview { width: 50px; height: 30px; border: 1px solid #ccc; display: inline-block; margin-left: 10px; }

        /* Navigation */
        .nav-tabs { display: flex; margin-bottom: 20px; border-bottom: 2px solid #dee2e6; }
        .nav-tab { flex: 1; padding: 10px; text-align: center; cursor: pointer; background: #f8f9fa; border: none; border-bottom: 3px solid transparent; color: #495057; }
        .nav-tab.active { background: white; border-bottom-color: #007bff; color: #007bff; font-weight: bold; }
        .nav-tab:hover { background: #e9ecef; color: #495057; }

        /* Page content */
        .page { display: none; }
        .page.active { display: block; }

        /* Main page specific */
        .big-button { font-size: 18px; padding: 15px; margin: 10px 0; }
        .color-wheel { display: flex; gap: 10px; margin: 10px 0; flex-wrap: wrap; }
        .color-option { width: 40px; height: 40px; border-radius: 50%; border: 3px solid transparent; cursor: pointer; }
        .color-option.selected { border-color: #333; }

        .calculated-info {
            background: #e7f3ff;
            padding: 10px;
            border-radius: 5px;
            margin: 10px 0;
            font-size: 14px;
        }
    </style>
</head>
<body>
    <div class="container">
        <!-- Navigation -->
        <div class="nav-tabs">
            <button class="nav-tab active" onclick="showPage('main')">Swim Pacer</button>
            <button class="nav-tab" onclick="showPage('coach')">Coach Config</button>
            <button class="nav-tab" onclick="showPage('advanced')">Advanced</button>
        </div>

        <!-- Main Pacer Page -->
        <div id="main" class="page active">
            <div class="status stopped" id="status">Pacer Stopped</div>

            <div class="control">
                <button class="big-button" onclick="togglePacer()" id="toggleBtn">Start Pacer</button>
            </div>

            <div class="control">
                <label for="pacePer50">Target Pace (seconds per 50 yards):</label>
                <input type="number" id="pacePer50" value="30" min="20" max="60" step="0.5" oninput="updateFromPace()">
            </div>

            <div class="control">
                <label>LED Color:</label>
                <div class="color-wheel">
                    <div class="color-option selected" style="background: red;" onclick="selectColor('red')" data-color="red"></div>
                    <div class="color-option" style="background: green;" onclick="selectColor('green')" data-color="green"></div>
                    <div class="color-option" style="background: blue;" onclick="selectColor('blue')" data-color="blue"></div>
                    <div class="color-option" style="background: yellow;" onclick="selectColor('yellow')" data-color="yellow"></div>
                    <div class="color-option" style="background: purple;" onclick="selectColor('purple')" data-color="purple"></div>
                    <div class="color-option" style="background: cyan;" onclick="selectColor('cyan')" data-color="cyan"></div>
                    <div class="color-option" style="background: white; border: 1px solid #ccc;" onclick="selectColor('white')" data-color="white"></div>
                </div>
            </div>
        </div>
        
        <!-- Coach Config Page -->
        <div id="coach" class="page">
            <h2>Coach Configuration</h2>
            <p>Adjust display settings and apply changes. Use the Main Pacer page to set pace.</p>

            <div class="control">
                <label for="brightness">Brightness:</label>
                <input type="range" id="brightness" min="20" max="255" value="150" oninput="updateBrightness()">
                <span id="brightnessValue">150</span>
            </div>

            <div class="control">
                <label for="pulseWidth">Pulse Width (feet):</label>
                <input type="range" id="pulseWidth" min="0.5" max="5" step="0.5" value="1.0" oninput="updatePulseWidth()">
                <span id="pulseWidthValue">1.0</span>
            </div>

            <div class="control">
                <button onclick="applyPaceSettings()">Apply Settings</button>
            </div>
        </div>

        <!-- Advanced Page -->
        <div id="advanced" class="page">
            <h2>Advanced Settings</h2>

            <div class="control">
                <label for="poolLength">Pool Length (feet):</label>
                <select id="poolLength" onchange="updateCalculations()">
                    <option value="150">50 yards (150 feet)</option>
                    <option value="75">25 yards (75 feet)</option>
                    <option value="82">25 meters (82 feet)</option>
                    <option value="164">50 meters (164 feet)</option>
                </select>
            </div>

            <div class="control">
                <label for="numLeds">Number of LEDs:</label>
                <input type="number" id="numLeds" value="150" min="10" max="300" onchange="updateSettings()">
            </div>

            <div class="calculated-info">
                <div><strong>Current Pace:</strong> <span id="currentPace">30.0 sec/50yd</span></div>
                <div><strong>LEDs per foot:</strong> <span id="ledsPerFoot">1.0</span></div>
                <div><strong>Time per LED:</strong> <span id="timePerLed">0.20 seconds</span></div>
            </div>

            <div class="control">
                <button onclick="saveSettings()">Save Settings</button>
            </div>
        </div>
    </div>

    <script>
        let currentSettings = {
            speed: 5.0,
            color: 'red',
            brightness: 150,
            pulseWidth: 1.0,
            poolLength: 150,
            numLeds: 150,
            isRunning: false
        };

        // Page navigation
        function showPage(pageId) {
            // Hide all pages
            document.querySelectorAll('.page').forEach(page => page.classList.remove('active'));
            document.querySelectorAll('.nav-tab').forEach(tab => tab.classList.remove('active'));

            // Show selected page
            document.getElementById(pageId).classList.add('active');
            event.target.classList.add('active');
        }

        // Conversion functions for swimming
        function paceToSpeed(paceSeconds, poolYards = 50) {
            const poolFeet = poolYards * 3; // Convert yards to feet
            return poolFeet / paceSeconds;
        }

        function speedToPace(speedFps, poolYards = 50) {
            const poolFeet = poolYards * 3; // Convert yards to feet
            return poolFeet / speedFps;
        }

        function updateFromPace() {
            const pace = parseFloat(document.getElementById('pacePer50').value);
            const speed = paceToSpeed(pace);
            currentSettings.speed = speed;

            updateCalculations();
        }

        function updateCalculations() {
            const speed = currentSettings.speed;
            const poolLength = parseInt(document.getElementById('poolLength').value);
            const numLeds = parseInt(document.getElementById('numLeds').value);

            // Update pace display
            const paceSeconds = speedToPace(speed);
            document.getElementById('currentPace').textContent = paceSeconds.toFixed(1) + ' sec/50yd';

            // Update LED metrics
            const ledsPerFoot = numLeds / poolLength;
            const timePerLed = 1 / (speed * ledsPerFoot);
            document.getElementById('ledsPerFoot').textContent = ledsPerFoot.toFixed(1);
            document.getElementById('timePerLed').textContent = (timePerLed * 1000).toFixed(0) + ' ms';
        }

        function selectColor(color) {
            currentSettings.color = color;
            document.querySelectorAll('.color-option').forEach(opt => opt.classList.remove('selected'));
            document.querySelector(`[data-color="${color}"]`).classList.add('selected');
            updateSettings();
        }

        function updateBrightness() {
            const brightness = document.getElementById('brightness').value;
            currentSettings.brightness = parseInt(brightness);
            document.getElementById('brightnessValue').textContent = brightness;
            updateSettings();
        }

        function updatePulseWidth() {
            const pulseWidth = document.getElementById('pulseWidth').value;
            currentSettings.pulseWidth = parseFloat(pulseWidth);
            document.getElementById('pulseWidthValue').textContent = pulseWidth;
            updateSettings();
        }

        function togglePacer() {
            currentSettings.isRunning = !currentSettings.isRunning;
            
            // Update UI immediately for better responsiveness
            const status = currentSettings.isRunning ? "Pacer Started" : "Pacer Stopped";
            document.getElementById('status').textContent = status;
            document.getElementById('status').className = 'status ' + (currentSettings.isRunning ? 'running' : 'stopped');
            document.getElementById('toggleBtn').textContent = currentSettings.isRunning ? 'Stop Pacer' : 'Start Pacer';

            // Try to notify server (will fail gracefully in standalone mode)
            fetch('/toggle', { method: 'POST' })
            .then(response => response.text())
            .then(result => {
                // Server responded, update status with server message
                document.getElementById('status').textContent = result;
            })
            .catch(error => {
                // Server not available (standalone mode), keep local status
                console.log('Running in standalone mode - server not available');
            });
        }

        function applyPaceSettings() {
            updateSettings();
            showPage('main');
            // Switch to main tab
            document.querySelectorAll('.nav-tab').forEach(tab => tab.classList.remove('active'));
            document.querySelector('.nav-tab').classList.add('active');
        }

        function updateSettings() {
            fetch('/setSpeed', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `speed=${currentSettings.speed}`
            }).catch(error => {
                console.log('Speed update - server not available (standalone mode)');
            });

            fetch('/setPulseWidth', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `pulseWidth=${currentSettings.pulseWidth}`
            }).catch(error => {
                console.log('Pulse width update - server not available (standalone mode)');
            });
        }

        function saveSettings() {
            updateSettings();

            fetch('/setColor', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `color=${currentSettings.color}`
            }).catch(error => {
                console.log('Color update - server not available (standalone mode)');
            });

            fetch('/setBrightness', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `brightness=${currentSettings.brightness}`
            }).catch(error => {
                console.log('Brightness update - server not available (standalone mode)');
            });

            alert('Settings saved!');
        }

        // Initialize
        updateCalculations();
    </script>
</body>
</html>
<!-- ========== SYNC MARKER: END ESP32 HTML ========== -->
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

// New simplified handlers for the updated interface
void handleToggle() {
  settings.isRunning = !settings.isRunning;
  saveSettings();

  String status = settings.isRunning ? "Pacer Started" : "Pacer Stopped";
  server.send(200, "text/plain", status);
}

void handleSetSpeed() {
  if (server.hasArg("speed")) {
    float speed = server.arg("speed").toFloat();
    settings.speedFeetPerSecond = speed;
    saveSettings();
    needsRecalculation = true;
  }
  server.send(200, "text/plain", "Speed updated");
}

void handleSetColor() {
  if (server.hasArg("color")) {
    String color = server.arg("color");
    // Convert color name to RGB values
    if (color == "red") {
      settings.colorRed = 255; settings.colorGreen = 0; settings.colorBlue = 0;
    } else if (color == "green") {
      settings.colorRed = 0; settings.colorGreen = 255; settings.colorBlue = 0;
    } else if (color == "blue") {
      settings.colorRed = 0; settings.colorGreen = 0; settings.colorBlue = 255;
    } else if (color == "yellow") {
      settings.colorRed = 255; settings.colorGreen = 255; settings.colorBlue = 0;
    } else if (color == "purple") {
      settings.colorRed = 128; settings.colorGreen = 0; settings.colorBlue = 128;
    } else if (color == "cyan") {
      settings.colorRed = 0; settings.colorGreen = 255; settings.colorBlue = 255;
    } else if (color == "white") {
      settings.colorRed = 255; settings.colorGreen = 255; settings.colorBlue = 255;
    }
    saveSettings();
  }
  server.send(200, "text/plain", "Color updated");
}

void handleSetBrightness() {
  if (server.hasArg("brightness")) {
    int brightness = server.arg("brightness").toInt();
    settings.brightness = brightness;
    FastLED.setBrightness(brightness);
    saveSettings();
  }
  server.send(200, "text/plain", "Brightness updated");
}

void handleSetPulseWidth() {
  if (server.hasArg("pulseWidth")) {
    float pulseWidth = server.arg("pulseWidth").toFloat();
    settings.pulseWidthFeet = pulseWidth;
    saveSettings();
    needsRecalculation = true;
  }
  server.send(200, "text/plain", "Pulse width updated");
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