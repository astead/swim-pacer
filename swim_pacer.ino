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
  float poolLengthMeters = 22.86;            // Pool length in meters (25 yards)
  float stripLengthMeters = 23.0;            // LED strip length in meters
  int ledsPerMeter = 30;                     // LEDs per meter
  float pulseWidthFeet = 1.0;                // Width of pulse in feet
  float speedFeetPerSecond = 5.56;           // Speed in feet per second
  int restTimeSeconds = 5;                   // Rest time between laps in seconds
  int paceDistanceYards = 50;                // Distance for pace calculation in yards
  int swimmerIntervalSeconds = 4;            // Interval between swimmers in seconds
  int numSwimmers = 1;                       // Number of swimmers (light pulses)
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
int totalLEDs;                    // Total number of LEDs (calculated)
float ledSpacingCM;               // Spacing between LEDs in cm
int pulseWidthLEDs;               // Width of pulse in LEDs
int delayMS;                      // Delay between LED updates
float poolToStripRatio;           // Ratio of pool to strip length

// ========== GLOBAL VARIABLES ==========
CRGB* leds;
int currentPosition = 0;
int direction = 1;
unsigned long lastUpdate = 0;
bool needsRecalculation = true;

// ========== MULTI-SWIMMER VARIABLES ==========
struct Swimmer {
  int position;
  int direction;
  unsigned long lastUpdate;
  CRGB color;
};

Swimmer swimmers[6]; // Support up to 6 swimmers
CRGB swimmerColors[] = {
  CRGB::Red,
  CRGB::Green, 
  CRGB::Blue,
  CRGB::Yellow,
  CRGB::Purple,
  CRGB::Cyan
};

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

  // Initialize swimmers
  initializeSwimmers();

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
  // Calculate total LEDs first
  totalLEDs = (int)(settings.stripLengthMeters * settings.ledsPerMeter);

  // Allocate LED array dynamically based on calculated total LEDs
  if (leds != nullptr) {
    delete[] leds;
  }
  leds = new CRGB[totalLEDs];

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, totalLEDs);
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
  server.on("/setStripLength", HTTP_POST, handleSetStripLength);
  server.on("/setLedsPerMeter", HTTP_POST, handleSetLedsPerMeter);
  server.on("/setRestTime", HTTP_POST, handleSetRestTime);
  server.on("/setPaceDistance", HTTP_POST, handleSetPaceDistance);
  server.on("/setSwimmerInterval", HTTP_POST, handleSetSwimmerInterval);
  server.on("/setNumSwimmers", HTTP_POST, handleSetNumSwimmers);

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

        /* Swimmer set styles */
        .swimmer-row { 
            display: flex; 
            align-items: center; 
            padding: 10px; 
            margin: 5px 0; 
            background: #f8f9fa; 
            border-radius: 5px; 
            gap: 15px;
        }
        .swimmer-color { 
            width: 30px; 
            height: 30px; 
            border-radius: 50%; 
            border: 2px solid #333; 
            cursor: pointer;
        }
        .swimmer-info { 
            flex: 1; 
            font-weight: bold;
        }
        .swimmer-pace-input { 
            width: 80px; 
            padding: 5px; 
            border: 1px solid #ccc; 
            border-radius: 3px;
        }

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
                <button class="big-button" onclick="createSet()" id="createSetBtn" style="margin-left: 10px;">Create Set</button>
            </div>

            <div class="control">
                <label for="paceDistance">Pace Distance:</label>
                <select id="paceDistance" onchange="updatePaceDistance()">
                    <option value="25">25 yards</option>
                    <option value="50" selected>50 yards</option>
                    <option value="75">75 yards</option>
                    <option value="100">100 yards</option>
                    <option value="200">200 yards</option>
                    <option value="500">500 yards</option>
                </select>
            </div>

            <div class="control">
                <label for="pacePer50">Target Pace (seconds per <span id="paceDistanceLabel">50 yards</span>):</label>
                <input type="number" id="pacePer50" value="30" min="20" max="300" step="0.5" oninput="updateFromPace()">
            </div>

            <div class="control">
                <label for="restTime">Rest Time (seconds):</label>
                <input type="range" id="restTime" min="0" max="30" step="1" value="5" oninput="updateRestTime()">
                <span id="restTimeValue">5</span>
            </div>

            <div class="control">
                <label for="numSwimmers">Number of Swimmers:</label>
                <input type="range" id="numSwimmers" min="1" max="6" step="1" value="1" oninput="updateNumSwimmers()">
                <span id="numSwimmersValue">1</span>
            </div>

            <!-- Swimmer Set Display -->
            <div id="swimmerSet" style="display: none;">
                <h3>Swimmer Set Configuration</h3>
                <div id="swimmerList"></div>
            </div>
        </div>

        <!-- Coach Config Page -->
        <div id="coach" class="page">
            <h3>Swim Settings</h3>
            <div class="control">
                <label for="swimmerInterval">Swimmer Interval (seconds):</label>
                <input type="range" id="swimmerInterval" min="1" max="20" step="1" value="4" oninput="updateSwimmerInterval()">
                <span id="swimmerIntervalValue">4</span>
            </div>

            <h3>Light Settings</h3>
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
            <p>Configure LED strip and pool dimensions. The pulse timing accounts for pool vs. strip length differences.</p>

            <div class="control">
                <label for="poolLength">Pool Length:</label>
                <select id="poolLength" onchange="updateCalculations()">
                    <option value="25">25 yards</option>
                    <option value="50">50 yards</option>
                    <option value="25m">25 meters</option>
                    <option value="50m">50 meters</option>
                </select>
            </div>

            <div class="control">
                <label for="stripLength">LED Strip Length (meters):</label>
                <input type="number" id="stripLength" value="23" min="1" max="50" step="0.5" onchange="updateCalculations()">
            </div>

            <div class="control">
                <label for="ledsPerMeter">LEDs per Meter:</label>
                <input type="number" id="ledsPerMeter" value="30" min="10" max="144" onchange="updateCalculations()">
            </div>

            <div class="calculated-info">
                <div><strong>Total LEDs:</strong> <span id="totalLeds">690</span></div>
                <div><strong>Pool Length:</strong> <span id="poolLengthDisplay">25 yards (22.9m)</span></div>
                <div><strong>Strip covers:</strong> <span id="stripCoverage">100.4% of pool</span></div>
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
            restTime: 5,
            paceDistance: 50,
            swimmerInterval: 4,
            numSwimmers: 1,
            poolLength: '25',
            stripLength: 23,
            ledsPerMeter: 30,
            isRunning: false
        };

        // Swimmer set configuration
        let swimmerSet = [];
        const swimmerColors = ['red', 'green', 'blue', 'yellow', 'purple', 'cyan'];
        const colorHex = {
            'red': '#ff0000',
            'green': '#00ff00', 
            'blue': '#0000ff',
            'yellow': '#ffff00',
            'purple': '#800080',
            'cyan': '#00ffff'
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
            const paceDistance = currentSettings.paceDistance;
            // Convert pace to speed (feet per second) based on selected distance
            const distanceFeet = paceDistance * 3; // yards to feet
            const speed = distanceFeet / pace;
            currentSettings.speed = speed;

            updateCalculations();
        }

        function updatePaceDistance() {
            const paceDistance = parseInt(document.getElementById('paceDistance').value);
            currentSettings.paceDistance = paceDistance;
            
            // Update the pace label
            document.getElementById('paceDistanceLabel').textContent = paceDistance + ' yards';
            
            // Recalculate speed based on current pace input and new distance
            updateFromPace();
            updateSettings();
        }

        function updateCalculations() {
            const poolLength = document.getElementById('poolLength').value;
            const stripLength = parseFloat(document.getElementById('stripLength').value);
            const ledsPerMeter = parseInt(document.getElementById('ledsPerMeter').value);

            // Convert pool length to meters
            let poolLengthMeters;
            let poolDisplay;
            if (poolLength.endsWith('m')) {
                poolLengthMeters = parseFloat(poolLength);
                poolDisplay = poolLength + ` (${(poolLengthMeters * 1.094).toFixed(1)} yards)`;
            } else {
                // Yards to meters
                poolLengthMeters = parseInt(poolLength) * 0.9144;
                poolDisplay = poolLength + ` yards (${poolLengthMeters.toFixed(1)}m)`;
            }

            // Calculate total LEDs
            const totalLeds = Math.round(stripLength * ledsPerMeter);

            // Calculate coverage
            const coverage = ((stripLength / poolLengthMeters) * 100).toFixed(1);

            // Update displays
            document.getElementById('totalLeds').textContent = totalLeds;
            document.getElementById('poolLengthDisplay').textContent = poolDisplay;
            document.getElementById('stripCoverage').textContent = coverage + '% of pool';

            // Update current settings
            currentSettings.poolLength = poolLength;
            currentSettings.stripLength = stripLength;
            currentSettings.ledsPerMeter = ledsPerMeter;
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

        function updateRestTime() {
            const restTime = document.getElementById('restTime').value;
            currentSettings.restTime = parseInt(restTime);
            document.getElementById('restTimeValue').textContent = restTime;
            updateSettings();
        }

        function updateSwimmerInterval() {
            const swimmerInterval = document.getElementById('swimmerInterval').value;
            currentSettings.swimmerInterval = parseInt(swimmerInterval);
            document.getElementById('swimmerIntervalValue').textContent = swimmerInterval;
            updateSettings();
        }

        function updateNumSwimmers() {
            const numSwimmers = document.getElementById('numSwimmers').value;
            currentSettings.numSwimmers = parseInt(numSwimmers);
            document.getElementById('numSwimmersValue').textContent = numSwimmers;
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

        function createSet() {
            // Clear existing set
            swimmerSet = [];
            
            // Get current pace from the main settings
            const currentPace = parseFloat(document.getElementById('pacePer50').value);
            
            // Create swimmer configurations
            for (let i = 0; i < currentSettings.numSwimmers; i++) {
                swimmerSet.push({
                    id: i + 1,
                    color: swimmerColors[i],
                    pace: currentPace,
                    interval: i * currentSettings.swimmerInterval // Start time offset
                });
            }
            
            // Display the set
            displaySwimmerSet();
            
            // Show the swimmer set section
            document.getElementById('swimmerSet').style.display = 'block';
        }

        function displaySwimmerSet() {
            const swimmerList = document.getElementById('swimmerList');
            swimmerList.innerHTML = '';
            
            swimmerSet.forEach((swimmer, index) => {
                const row = document.createElement('div');
                row.className = 'swimmer-row';
                
                row.innerHTML = `
                    <div class="swimmer-color" style="background-color: ${colorHex[swimmer.color]}" 
                         onclick="cycleSwimmerColor(${index})" title="Click to change color"></div>
                    <div class="swimmer-info">Swimmer ${swimmer.id}</div>
                    <div>Start: ${swimmer.interval}s</div>
                    <div>Pace: <input type="number" class="swimmer-pace-input" value="${swimmer.pace}" 
                         min="20" max="300" step="0.5" onchange="updateSwimmerPace(${index}, this.value)"> sec</div>
                `;
                
                swimmerList.appendChild(row);
            });
        }

        function cycleSwimmerColor(swimmerIndex) {
            const currentColorIndex = swimmerColors.indexOf(swimmerSet[swimmerIndex].color);
            const nextColorIndex = (currentColorIndex + 1) % swimmerColors.length;
            swimmerSet[swimmerIndex].color = swimmerColors[nextColorIndex];
            displaySwimmerSet();
        }

        function updateSwimmerPace(swimmerIndex, newPace) {
            swimmerSet[swimmerIndex].pace = parseFloat(newPace);
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

            fetch('/setStripLength', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `stripLength=${currentSettings.stripLength}`
            }).catch(error => {
                console.log('Strip length update - server not available (standalone mode)');
            });

            fetch('/setLedsPerMeter', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `ledsPerMeter=${currentSettings.ledsPerMeter}`
            }).catch(error => {
                console.log('LEDs per meter update - server not available (standalone mode)');
            });

            fetch('/setRestTime', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `restTime=${currentSettings.restTime}`
            }).catch(error => {
                console.log('Rest time update - server not available (standalone mode)');
            });

            fetch('/setPaceDistance', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `paceDistance=${currentSettings.paceDistance}`
            }).catch(error => {
                console.log('Pace distance update - server not available (standalone mode)');
            });

            fetch('/setSwimmerInterval', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `swimmerInterval=${currentSettings.swimmerInterval}`
            }).catch(error => {
                console.log('Swimmer interval update - server not available (standalone mode)');
            });

            fetch('/setNumSwimmers', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `numSwimmers=${currentSettings.numSwimmers}`
            }).catch(error => {
                console.log('Number of swimmers update - server not available (standalone mode)');
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

void handleSetStripLength() {
  if (server.hasArg("stripLength")) {
    float stripLength = server.arg("stripLength").toFloat();
    settings.stripLengthMeters = stripLength;
    saveSettings();
    needsRecalculation = true;
    setupLEDs();  // Reinitialize LED array with new length
  }
  server.send(200, "text/plain", "Strip length updated");
}

void handleSetLedsPerMeter() {
  if (server.hasArg("ledsPerMeter")) {
    int ledsPerMeter = server.arg("ledsPerMeter").toInt();
    settings.ledsPerMeter = ledsPerMeter;
    saveSettings();
    needsRecalculation = true;
    setupLEDs();  // Reinitialize LED array with new density
  }
  server.send(200, "text/plain", "LEDs per meter updated");
}

void handleSetRestTime() {
  if (server.hasArg("restTime")) {
    int restTime = server.arg("restTime").toInt();
    settings.restTimeSeconds = restTime;
    saveSettings();
    needsRecalculation = true;
  }
  server.send(200, "text/plain", "Rest time updated");
}

void handleSetPaceDistance() {
  if (server.hasArg("paceDistance")) {
    int paceDistance = server.arg("paceDistance").toInt();
    settings.paceDistanceYards = paceDistance;
    saveSettings();
    needsRecalculation = true;
  }
  server.send(200, "text/plain", "Pace distance updated");
}

void handleSetSwimmerInterval() {
  if (server.hasArg("swimmerInterval")) {
    int swimmerInterval = server.arg("swimmerInterval").toInt();
    settings.swimmerIntervalSeconds = swimmerInterval;
    saveSettings();
  }
  server.send(200, "text/plain", "Swimmer interval updated");
}

void handleSetNumSwimmers() {
  if (server.hasArg("numSwimmers")) {
    int numSwimmers = server.arg("numSwimmers").toInt();
    settings.numSwimmers = numSwimmers;
    saveSettings();
  }
  server.send(200, "text/plain", "Number of swimmers updated");
}

void saveSettings() {
  preferences.putFloat("poolLengthM", settings.poolLengthMeters);
  preferences.putFloat("stripLengthM", settings.stripLengthMeters);
  preferences.putInt("ledsPerMeter", settings.ledsPerMeter);
  preferences.putFloat("pulseWidthFeet", settings.pulseWidthFeet);
  preferences.putFloat("speedFPS", settings.speedFeetPerSecond);
  preferences.putInt("restTimeSeconds", settings.restTimeSeconds);
  preferences.putInt("paceDistanceYards", settings.paceDistanceYards);
  preferences.putInt("swimmerInterval", settings.swimmerIntervalSeconds);
  preferences.putInt("numSwimmers", settings.numSwimmers);
  preferences.putUChar("colorRed", settings.colorRed);
  preferences.putUChar("colorGreen", settings.colorGreen);
  preferences.putUChar("colorBlue", settings.colorBlue);
  preferences.putUChar("brightness", settings.brightness);
  preferences.putBool("isRunning", settings.isRunning);

  Serial.println("Settings saved to flash memory");
}

void loadSettings() {
  settings.poolLengthMeters = preferences.getFloat("poolLengthM", 22.86);  // 25 yards
  settings.stripLengthMeters = preferences.getFloat("stripLengthM", 23.0);
  settings.ledsPerMeter = preferences.getInt("ledsPerMeter", 30);
  settings.pulseWidthFeet = preferences.getFloat("pulseWidthFeet", 1.0);
  settings.speedFeetPerSecond = preferences.getFloat("speedFPS", 5.56);
  settings.restTimeSeconds = preferences.getInt("restTimeSeconds", 5);
  settings.paceDistanceYards = preferences.getInt("paceDistanceYards", 50);
  settings.swimmerIntervalSeconds = preferences.getInt("swimmerInterval", 4);
  settings.numSwimmers = preferences.getInt("numSwimmers", 1);
  settings.colorRed = preferences.getUChar("colorRed", 0);
  settings.colorGreen = preferences.getUChar("colorGreen", 0);
  settings.colorBlue = preferences.getUChar("colorBlue", 255);
  settings.brightness = preferences.getUChar("brightness", 100);
  settings.isRunning = preferences.getBool("isRunning", false);  // Default: stopped

  Serial.println("Settings loaded from flash memory");
}

void recalculateValues() {
  // Calculate total LEDs from strip length and density
  totalLEDs = (int)(settings.stripLengthMeters * settings.ledsPerMeter);

  // Calculate LED spacing
  ledSpacingCM = 100.0 / settings.ledsPerMeter;

  // Calculate pulse width in LEDs
  float pulseWidthCM = settings.pulseWidthFeet * 30.48;
  pulseWidthLEDs = (int)(pulseWidthCM / ledSpacingCM);

  // Calculate pool to strip ratio for timing adjustments
  poolToStripRatio = settings.poolLengthMeters / settings.stripLengthMeters;

  // Calculate speed and timing based on pool length (50-yard swim = 2x pool length)
  float swimDistanceMeters = settings.poolLengthMeters * 2.0;  // Down and back
  float swimTimeSeconds = swimDistanceMeters / (settings.speedFeetPerSecond * 0.3048);

  // LED animation should complete in the same time as the swim
  // If strip is shorter than pool, animation runs faster when "in view"
  // If strip is longer than pool, animation pauses when "out of pool"
  float effectiveStripLength = min(settings.stripLengthMeters, settings.poolLengthMeters);
  float animationDistance = effectiveStripLength * 2.0;  // Down and back on visible portion

  // Calculate delay per LED movement
  int totalAnimationSteps = (int)(animationDistance / ledSpacingCM * 100);
  delayMS = (int)((swimTimeSeconds * 1000) / totalAnimationSteps);

  // Ensure reasonable bounds
  delayMS = max(10, min(delayMS, 2000));

  // Reset position if it's out of bounds
  if (currentPosition >= totalLEDs) {
    currentPosition = totalLEDs - 1;
  }

  Serial.println("Values recalculated:");
  Serial.println("  Total LEDs: " + String(totalLEDs));
  Serial.println("  LED spacing: " + String(ledSpacingCM) + " cm");
  Serial.println("  Pulse width: " + String(pulseWidthLEDs) + " LEDs");
  Serial.println("  Pool/Strip ratio: " + String(poolToStripRatio));
  Serial.println("  Update delay: " + String(delayMS) + " ms");
  
  // Reinitialize swimmers when values change
  initializeSwimmers();
}

void updateLEDEffect() {
  unsigned long currentTime = millis();

  // Clear all LEDs
  FastLED.clear();

  // Update and draw each active swimmer
  for (int i = 0; i < settings.numSwimmers; i++) {
    updateSwimmer(i, currentTime);
    drawSwimmerPulse(i);
  }

  // Update FastLED
  FastLED.show();
}

void initializeSwimmers() {
  for (int i = 0; i < 6; i++) {
    swimmers[i].position = 0;
    swimmers[i].direction = 1;
    swimmers[i].lastUpdate = millis() + (i * settings.swimmerIntervalSeconds * 1000); // Stagger start times
    swimmers[i].color = swimmerColors[i];
  }
}

void updateSwimmer(int swimmerIndex, unsigned long currentTime) {
  if (currentTime - swimmers[swimmerIndex].lastUpdate >= delayMS) {
    swimmers[swimmerIndex].lastUpdate = currentTime;

    // Move to next position
    swimmers[swimmerIndex].position += swimmers[swimmerIndex].direction;

    // Check for bouncing at ends
    if (swimmers[swimmerIndex].position >= totalLEDs - 1) {
      swimmers[swimmerIndex].direction = -1;
      swimmers[swimmerIndex].position = totalLEDs - 1;
    } else if (swimmers[swimmerIndex].position <= 0) {
      swimmers[swimmerIndex].direction = 1;
      swimmers[swimmerIndex].position = 0;
    }
  }
}

void drawSwimmerPulse(int swimmerIndex) {
  int centerPos = swimmers[swimmerIndex].position;
  int halfWidth = pulseWidthLEDs / 2;
  CRGB pulseColor = swimmers[swimmerIndex].color;

  for (int i = 0; i < pulseWidthLEDs; i++) {
    int ledIndex = centerPos - halfWidth + i;

    if (ledIndex >= 0 && ledIndex < totalLEDs) {
      int distanceFromCenter = abs(i - halfWidth);
      float brightnessFactor = 1.0 - (float)distanceFromCenter / (float)halfWidth;
      brightnessFactor = max(0.0f, brightnessFactor);

      CRGB color = pulseColor;
      color.nscale8((uint8_t)(brightnessFactor * 255));

      // Add this swimmer's color to existing LED (allows overlapping)
      leds[ledIndex] += color;
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