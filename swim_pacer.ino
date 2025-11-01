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
#define LED_TYPE        WS2812B     // LED strip type
#define COLOR_ORDER     GRB         // Color order (may need adjustment)

// GPIO pins for multiple LED strips (lanes)
// Lane 1: GPIO 18, Lane 2: GPIO 19, Lane 3: GPIO 21, Lane 4: GPIO 22
const int LED_PINS[4] = {18, 19, 21, 22}; // GPIO pins for lanes 1-4

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
  int numLanes = 2;                          // Number of LED strips/lanes connected
  float pulseWidthFeet = 1.0;                // Width of pulse in feet
  float speedFeetPerSecond = 5.56;           // Speed in feet per second
  int restTimeSeconds = 5;                   // Rest time between laps in seconds
  int paceDistanceYards = 50;                // Distance for pace calculation in yards
  int initialDelaySeconds = 10;              // Initial delay before first swimmer starts
  int swimmerIntervalSeconds = 4;            // Interval between swimmers in seconds
  bool delayIndicatorsEnabled = true;        // Whether to show delay countdown indicators
  int numSwimmers = 3;                       // Number of swimmers (light pulses)
  int numRounds = 10;                        // Number of rounds/sets to complete
  uint8_t colorRed = 0;                      // RGB color values
  uint8_t colorGreen = 0;
  uint8_t colorBlue = 255;
  uint8_t brightness = 196;                  // Overall brightness (0-255)
  bool isRunning = false;                    // Whether the effect is active (default: stopped)
  bool laneRunning[4] = {false, false, false, false}; // Per-lane running states
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
CRGB* leds[4] = {nullptr, nullptr, nullptr, nullptr}; // Array of LED strip pointers for up to 4 lanes
int currentLane = 0;              // Currently selected lane for configuration
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

Swimmer swimmers[4][6]; // Support up to 6 swimmers per lane, for up to 4 lanes
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

  // Clear any existing FastLED configurations
  FastLED.clear();

  // Initialize LED arrays for each configured lane
  for (int lane = 0; lane < settings.numLanes; lane++) {
    // Allocate LED array dynamically for this lane
    if (leds[lane] != nullptr) {
      delete[] leds[lane];
    }
    leds[lane] = new CRGB[totalLEDs];

    // Add LED strip for this lane with appropriate GPIO pin
    switch (lane) {
      case 0:
        FastLED.addLeds<LED_TYPE, 18, COLOR_ORDER>(leds[0], totalLEDs);
        break;
      case 1:
        FastLED.addLeds<LED_TYPE, 19, COLOR_ORDER>(leds[1], totalLEDs);
        break;
      case 2:
        FastLED.addLeds<LED_TYPE, 21, COLOR_ORDER>(leds[2], totalLEDs);
        break;
      case 3:
        FastLED.addLeds<LED_TYPE, 22, COLOR_ORDER>(leds[3], totalLEDs);
        break;
    }

    // Clear this lane's LEDs
    fill_solid(leds[lane], totalLEDs, CRGB::Black);
  }

  // Initialize unused lanes to nullptr
  for (int lane = settings.numLanes; lane < 4; lane++) {
    if (leds[lane] != nullptr) {
      delete[] leds[lane];
      leds[lane] = nullptr;
    }
  }

  FastLED.setBrightness(settings.brightness);
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
  server.on("/currentLane", HTTP_GET, handleGetCurrentLane);

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
  server.on("/setNumLanes", HTTP_POST, handleSetNumLanes);
  server.on("/setCurrentLane", HTTP_POST, handleSetCurrentLane);
  server.on("/setRestTime", HTTP_POST, handleSetRestTime);
  server.on("/setPaceDistance", HTTP_POST, handleSetPaceDistance);
  server.on("/setInitialDelay", HTTP_POST, handleSetInitialDelay);
  server.on("/setSwimmerInterval", HTTP_POST, handleSetSwimmerInterval);
  server.on("/setDelayIndicators", HTTP_POST, handleSetDelayIndicators);
  server.on("/setNumSwimmers", HTTP_POST, handleSetNumSwimmers);
  server.on("/setNumRounds", HTTP_POST, handleSetNumRounds);

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
        .big-button { font-size: 18px; padding: 15px; margin: 0; min-width: 140px; }
        .button-group { display: flex; gap: 10px; margin: 10px 0; }
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

        /* Toggle Switch Styles */
        .toggle-container {
            display: flex;
            align-items: center;
            gap: 15px;
        }

        .toggle-switch {
            position: relative;
            display: inline-block;
            width: 60px;
            height: 30px;
        }

        .toggle-switch input {
            opacity: 0;
            width: 0;
            height: 0;
        }

        .toggle-slider {
            position: absolute;
            cursor: pointer;
            top: 0;
            left: 0;
            right: 0;
            bottom: 0;
            background-color: #ccc;
            transition: .4s;
            border-radius: 30px;
        }

        .toggle-slider:before {
            position: absolute;
            content: "";
            height: 24px;
            width: 24px;
            left: 3px;
            bottom: 3px;
            background-color: white;
            transition: .4s;
            border-radius: 50%;
        }

        input:checked + .toggle-slider {
            background-color: #2196F3;
        }

        input:checked + .toggle-slider:before {
            transform: translateX(30px);
        }

        .toggle-labels {
            display: flex;
            align-items: center;
            gap: 8px;
            font-size: 14px;
            color: #666;
        }

        .toggle-label {
            transition: color 0.3s;
        }

        .toggle-label.active {
            color: #2196F3;
            font-weight: bold;
        }

        /* Underwater Controls Styling */
        .underwater-controls {
            background: linear-gradient(135deg, #e3f2fd 0%, #f0f8ff 100%);
            border: 2px solid #81d4fa;
            border-radius: 12px;
            padding: 20px;
            margin: 15px 0;
            box-shadow: 0 2px 8px rgba(33, 150, 243, 0.1);
        }

        .underwater-controls .control {
            margin: 15px 0;
        }

        .underwater-controls .control:first-child {
            margin-top: 0;
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
            <!-- Lane Selector (shown when multiple lanes configured) -->
            <div id="laneSelector" style="display: none; margin: 15px 0;">
                <div class="control">
                    <select id="currentLane" onchange="updateCurrentLane()">
                        <!-- Options populated by JavaScript -->
                    </select>
                </div>
            </div>

            <div class="status stopped" id="status">Pacer Stopped</div>

            <!-- Detailed Status (shown when running) -->
            <!-- Work Set Queue Display -->
            <div id="queueDisplay" style="background: #f8f9fa; padding: 15px; border-radius: 8px; margin: 10px 0; border-left: 4px solid #28a745;">
                <h4 style="margin: 0 0 10px 0; color: #28a745;">Work Set Queue</h4>
                <div id="queueList" style="font-size: 14px;">
                    <div style="color: #666; font-style: italic;">No sets queued</div>
                </div>
            </div>

            <!-- Active Set Status - now integrated into queue display -->
            <div id="detailedStatus" style="display: none;"></div>

            <div class="control">
                <div class="button-group" id="pacerButtons" style="display: none;">
                    <button class="big-button" onclick="startQueue()" id="startBtn">Start Queue</button>
                    <button class="big-button" onclick="stopQueue()" id="stopBtn" style="display: none; background: #dc3545;">Stop</button>
                </div>
                <div class="button-group" id="configButtons">
                    <button class="big-button" onclick="createWorkSet()" id="createSetBtn">Create Set</button>
                </div>
                <div class="button-group" id="queueButtons" style="display: none;">
                    <button class="big-button" onclick="queueWorkSet()" id="queueBtn">Queue Set</button>
                    <button class="big-button" onclick="cancelWorkSet()" id="cancelBtn" style="background: #dc3545;">Cancel</button>
                </div>
                <div class="button-group" id="editButtons" style="display: none;">
                    <button class="big-button" onclick="saveWorkSet()" id="saveBtn">Save Changes</button>
                    <button class="big-button" onclick="cancelEdit()" id="cancelEditBtn" style="background: #dc3545;">Cancel</button>
                </div>
            </div>

            <!-- Configuration Controls -->
            <div id="configControls">
                <div class="control">
                    <label for="numRounds">Number of Rounds:</label>
                    <input type="number" id="numRounds" min="1" max="20" step="1" value="10" oninput="updateNumRounds()">
                </div>

                <div class="control">
                    <label for="paceDistance">Distance:</label>
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
                    <label for="pacePer50">Pace (seconds per <span id="paceDistanceLabel">50 yards</span>):</label>
                    <input type="number" id="pacePer50" value="30" min="20" max="300" step="0.5" oninput="updateFromPace()">
                </div>

                <div class="control">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 5px;">
                        <label for="restTime">Rest Time:</label>
                        <span id="restTimeValue">5 seconds</span>
                    </div>
                    <input type="range" id="restTime" min="0" max="30" step="1" value="5" oninput="updateRestTime()">
                </div>

                <div class="control">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 5px;">
                        <label for="numSwimmers">Number of Swimmers:</label>
                        <span id="numSwimmersValue">3</span>
                    </div>
                    <input type="range" id="numSwimmers" min="1" max="6" step="1" value="3" oninput="updateNumSwimmers()">
                </div>
            </div>

            <!-- Swimmer Set Display -->
            <div id="swimmerSet" style="display: none;">
                <h3>Swim Set</h3>
                <div id="setDetails" style="font-size: 16px; font-weight: bold; margin: 10px 0; padding: 10px; background: #f0f8ff; border-radius: 5px;"></div>
                <div id="swimmerList"></div>
            </div>
        </div>

        <!-- Coach Config Page -->
        <div id="coach" class="page">
            <h3>Swim Settings</h3>
            <div class="control">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 5px;">
                    <label for="initialDelay">Initial Delay:</label>
                    <span id="initialDelayValue">10 seconds</span>
                </div>
                <input type="range" id="initialDelay" min="0" max="30" step="1" value="10" oninput="updateInitialDelay()">
            </div>
            <div class="control">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 5px;">
                    <label for="swimmerInterval">Delay between swimmers:</label>
                    <span id="swimmerIntervalValue">4 seconds</span>
                </div>
                <input type="range" id="swimmerInterval" min="1" max="20" step="1" value="4" oninput="updateSwimmerInterval()">
            </div>

            <div class="toggle-container">
                <h3 style="margin: 0;">Delay Indicators</h3>
                <div class="toggle-labels">
                    <span class="toggle-label" id="delayIndicatorOff">OFF</span>
                    <label class="toggle-switch">
                        <input type="checkbox" id="delayIndicatorsEnabled" checked onchange="updateDelayIndicatorsEnabled()">
                        <span class="toggle-slider"></span>
                    </label>
                    <span class="toggle-label active" id="delayIndicatorOn">ON</span>
                    <small style="color: #666; margin-left: 10px; font-size: 12px;">max 5 seconds</small>
                </div>
            </div>

            <div class="toggle-container" style="margin-top: 20px;">
                <h3 style="margin: 0;">Underwaters</h3>
                <div class="toggle-labels">
                    <span class="toggle-label active" id="toggleOff">OFF</span>
                    <label class="toggle-switch">
                        <input type="checkbox" id="underwatersEnabled" onchange="updateUnderwatersEnabled()">
                        <span class="toggle-slider"></span>
                    </label>
                    <span class="toggle-label" id="toggleOn">ON</span>
                </div>
            </div>

            <div id="underwatersControls" class="underwater-controls" style="display: none;">
                <div class="control">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 5px;">
                        <label for="lightSize">Light pulse size:</label>
                        <span id="lightSizeValue">1.0 foot</span>
                    </div>
                    <input type="range" id="lightSize" min="0.5" max="5" step="0.5" value="1.0" oninput="updateLightSize()">
                </div>

                <div class="control">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 5px;">
                        <label for="firstUnderwaterDistance">First underwater distance:</label>
                        <span id="firstUnderwaterDistanceValue">20 feet</span>
                    </div>
                    <input type="range" id="firstUnderwaterDistance" min="1" max="50" step="1" value="20" oninput="updateFirstUnderwaterDistance()">
                </div>

                <div class="control">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 5px;">
                        <label for="underwaterDistance">Underwater distance:</label>
                        <span id="underwaterDistanceValue">20 feet</span>
                    </div>
                    <input type="range" id="underwaterDistance" min="1" max="50" step="1" value="20" oninput="updateUnderwaterDistance()">
                </div>

                <div class="control">
                    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 5px;">
                        <label for="hideAfter">Hide after:</label>
                        <span id="hideAfterValue">3 seconds</span>
                    </div>
                    <input type="range" id="hideAfter" min="1" max="10" step="1" value="3" oninput="updateHideAfter()">
                </div>

                <div class="control">
                    <div class="color-selection-row" style="display: flex; align-items: center; gap: 10px;">
                        <div class="color-indicator" id="underwaterColorIndicator"
                             style="width: 30px; height: 30px; border-radius: 50%; background-color: #0000ff; border: 2px solid #ccc; cursor: pointer;"
                             onclick="openColorPickerForUnderwater()"></div>
                        <label>Underwater Color</label>
                    </div>
                </div>

                <div class="control">
                    <div class="color-selection-row" style="display: flex; align-items: center; gap: 10px;">
                        <div class="color-indicator" id="surfaceColorIndicator"
                             style="width: 30px; height: 30px; border-radius: 50%; background-color: #00ff00; border: 2px solid #ccc; cursor: pointer;"
                             onclick="openColorPickerForSurface()"></div>
                        <label>Surface Color</label>
                    </div>
                </div>
            </div>

            <h3>Light Settings</h3>
            <div class="control">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 5px;">
                    <label for="brightness">Brightness:</label>
                    <span id="brightnessValue">75%</span>
                </div>
                <input type="range" id="brightness" min="0" max="100" value="75" oninput="updateBrightness()">
            </div>

            <div class="control">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 5px;">
                    <label for="pulseWidth">Light pulse size:</label>
                    <span id="pulseWidthValue">1.0 foot</span>
                </div>
                <input type="range" id="pulseWidth" min="0.5" max="5" step="0.5" value="1.0" oninput="updatePulseWidth()">
            </div>

            <div class="control">
                <label>Swimmer Colors:</label>
                <div style="margin-top: 10px;">
                    <div id="individualColorsRow" style="display: flex; align-items: center; margin-bottom: 8px; cursor: pointer; padding: 8px; border-radius: 5px;" onclick="selectIndividualColors()">
                        <div id="individualColorsIcon" style="width: 30px; height: 30px; border: 2px solid #333; border-radius: 50%; position: relative; flex-shrink: 0; min-width: 30px; min-height: 30px; overflow: hidden;">
                            <div style="position: absolute; top: 0; left: 0; width: 50%; height: 50%; background-color: #ff0000;"></div>
                            <div style="position: absolute; top: 0; right: 0; width: 50%; height: 50%; background-color: #00ff00;"></div>
                            <div style="position: absolute; bottom: 0; left: 0; width: 50%; height: 50%; background-color: #0000ff;"></div>
                            <div style="position: absolute; bottom: 0; right: 0; width: 50%; height: 50%; background-color: #ffff00;"></div>
                        </div>
                        <input type="radio" id="individualColors" name="colorMode" value="individual" checked onchange="updateColorMode()" style="display: none;">
                        <label for="individualColors" style="margin: 0; margin-left: 8px; cursor: pointer;">Individual colors</label>
                    </div>
                    <div id="sameColorRow" style="display: flex; align-items: center; margin-bottom: 8px; cursor: pointer; padding: 8px; border-radius: 5px;">
                        <div id="colorIndicator" class="swimmer-color" style="background-color: #0000ff; cursor: pointer; flex-shrink: 0; min-width: 30px; min-height: 30px;" onclick="selectSameColorAndOpenPicker(event)"></div>
                        <input type="radio" id="sameColor" name="colorMode" value="same" onchange="updateColorMode()" style="display: none;">
                        <label for="sameColor" style="margin: 0; margin-left: 8px; cursor: pointer;" onclick="selectSameColor()">Same color</label>
                    </div>
                    <div id="colorPickerSection" style="display: none; margin-top: 10px; margin-left: 24px;">
                        <input type="color" id="swimmerColorPicker" value="#0000ff" onchange="updateSwimmerColor()" style="opacity: 0; pointer-events: none;">
                    </div>
                </div>
            </div>
        </div>

        <!-- Advanced Page -->
        <div id="advanced" class="page">
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

            <div class="control">
                <label for="numLanes">Number of lanes:</label>
                <input type="number" id="numLanes" value="2" min="1" max="4" onchange="updateNumLanes()">
            </div>

            <!-- Lane Names Management -->
            <div id="laneNamesSection" style="margin-top: 20px;">
                <div style="margin: 10px 0;">
                    <h4 style="margin: 0; color: #333; display: inline-block;">Lane Names:</h4>
                    <span id="identifyLanesBtn" onclick="toggleLaneIdentification()" style="display: inline-block; margin-left: 10px; padding: 2px 6px; font-size: 12px; border: 1px solid #007bff; border-radius: 3px; background: #007bff; color: white; cursor: pointer; font-weight: 500; white-space: nowrap; user-select: none;">Identify Lanes</span>
                </div>
                <div id="laneNamesList">
                    <!-- Lane name inputs populated by JavaScript -->
                </div>
            </div>
        </div>
    </div>

    <!-- Custom Color Picker Modal (Global) -->
    <div id="customColorPicker" style="display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.5); z-index: 1000;" onclick="closeColorPicker()">
        <div style="position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 20px rgba(0,0,0,0.3);" onclick="event.stopPropagation()">
            <h4 style="margin: 0 0 15px 0; text-align: center;">Choose Color</h4>
            <div id="colorGrid" style="display: grid; grid-template-columns: repeat(6, 40px); gap: 8px; margin-bottom: 15px;">
                <!-- Colors will be populated by JavaScript -->
            </div>
            <button onclick="closeColorPicker()" style="width: 100%; padding: 8px; background: #007bff; color: white; border: none; border-radius: 5px; cursor: pointer;">Close</button>
        </div>
    </div>

    <script>
        let currentSettings = {
            speed: 5.0,
            color: 'red',
            brightness: 196,
            pulseWidth: 1.0,
            restTime: 5,
            paceDistance: 50,
            initialDelay: 10,
            swimmerInterval: 4,
            delayIndicatorsEnabled: true,
            numSwimmers: 3,
            numRounds: 10,
            colorMode: 'individual',
            swimmerColor: '#0000ff',
            poolLength: '25',
            stripLength: 23,
            ledsPerMeter: 30,
            numLanes: 2,
            currentLane: 0,
            laneNames: ['Lane 1', 'Lane 2', 'Lane 3', 'Lane 4'],
            isRunning: false,
            underwatersEnabled: false,
            lightSize: 1.0,
            firstUnderwaterDistance: 20,
            underwaterDistance: 20,
            hideAfter: 3,
            underwaterColor: '#0000ff',
            surfaceColor: '#00ff00'
        };

        // Swimmer set configuration - now lane-specific
        let swimmerSets = [[], [], [], []]; // Array of sets for each lane (up to 4 lanes)
        let laneRunning = [false, false, false, false]; // Running state for each lane
        let runningSets = [null, null, null, null]; // Immutable copies of sets when pacer starts
        let runningSettings = [null, null, null, null]; // Settings snapshot when pacer starts
        let currentSwimmerIndex = -1; // Track which swimmer is being edited

        // Work Set Queue Management - now lane-specific
        let workSetQueues = [[], [], [], []]; // Array of queued work sets for each lane
        let activeWorkSets = [null, null, null, null]; // Currently running work set for each lane
        let editingWorkSetIndexes = [-1, -1, -1, -1]; // Index of work set being edited for each lane (-1 if creating new)
        let createdWorkSets = [null, null, null, null]; // Temporarily holds created set before queuing for each lane
        const swimmerColors = ['red', 'green', 'blue', 'yellow', 'purple', 'cyan'];
        const colorHex = {
            'red': '#ff0000',
            'green': '#00ff00',
            'blue': '#0000ff',
            'yellow': '#ffff00',
            'purple': '#800080',
            'cyan': '#00ffff',
            'custom': '#0000ff' // Default custom color
        };

        // Lane identification system
        let laneIdentificationMode = false;
        const laneIdentificationColors = ['#ff0000', '#00ff00', '#0000ff', '#ffff00']; // Red, Green, Blue, Yellow

        // Convert hex color to color name for swimmer assignment
        function hexToColorName(hexColor) {
            // Check if it matches any existing colors
            for (const [colorName, colorValue] of Object.entries(colorHex)) {
                if (colorValue.toLowerCase() === hexColor.toLowerCase()) {
                    return colorName;
                }
            }
            // If no match found, update custom color and return 'custom'
            colorHex.custom = hexColor;
            return 'custom';
        }

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

            // Update current settings
            currentSettings.poolLength = poolLength;
            currentSettings.stripLength = stripLength;
            currentSettings.ledsPerMeter = ledsPerMeter;

            // Apply changes immediately
            updateSettings();
        }

        function updateNumLanes() {
            const numLanes = parseInt(document.getElementById('numLanes').value);
            currentSettings.numLanes = numLanes;
            updateLaneSelector();
            updateLaneNamesSection();
            updateSettings();
        }

        function updateLaneSelector() {
            const laneSelector = document.getElementById('laneSelector');
            const currentLaneSelect = document.getElementById('currentLane');

            // Show/hide lane selector based on number of lanes
            if (currentSettings.numLanes > 1) {
                laneSelector.style.display = 'block';

                // Populate lane options
                currentLaneSelect.innerHTML = '';
                for (let i = 0; i < currentSettings.numLanes; i++) {
                    const option = document.createElement('option');
                    option.value = i;
                    option.textContent = currentSettings.laneNames[i];
                    currentLaneSelect.appendChild(option);
                }

                // Set current lane
                currentLaneSelect.value = currentSettings.currentLane;
            } else {
                laneSelector.style.display = 'none';
                currentSettings.currentLane = 0; // Default to lane 0 for single lane
            }
        }

        function updateLaneNamesSection() {
            const laneNamesList = document.getElementById('laneNamesList');
            laneNamesList.innerHTML = '';

            for (let i = 0; i < currentSettings.numLanes; i++) {
                const laneDiv = document.createElement('div');
                laneDiv.style.cssText = 'margin: 8px 0; display: flex; align-items: center; gap: 10px;';

                // Color indicator (shown only in identification mode)
                const colorIndicator = document.createElement('div');
                colorIndicator.style.cssText = `width: 20px; height: 20px; border-radius: 50%; border: 2px solid #ddd; display: ${laneIdentificationMode ? 'block' : 'none'};`;
                if (laneIdentificationMode) {
                    colorIndicator.style.backgroundColor = laneIdentificationColors[i];
                }

                const input = document.createElement('input');
                input.type = 'text';
                input.value = currentSettings.laneNames[i];
                input.placeholder = `Lane ${i + 1}`;
                input.style.cssText = 'flex: 1; padding: 8px 12px; border: 1px solid #ddd; border-radius: 4px; font-size: 14px;';
                input.onchange = function() {
                    currentSettings.laneNames[i] = this.value;
                    updateLaneSelector(); // Refresh the dropdown on main page
                    updateSettings(); // Save the changes
                };

                laneDiv.appendChild(colorIndicator);
                laneDiv.appendChild(input);
                laneNamesList.appendChild(laneDiv);
            }
        }

        function toggleLaneIdentification() {
            const button = document.getElementById('identifyLanesBtn');

            if (!laneIdentificationMode) {
                // Start identification mode
                laneIdentificationMode = true;
                button.textContent = 'Stop';
                button.style.backgroundColor = '#dc3545';
                button.style.color = 'white';

                // Update the lane names section to show color indicators
                updateLaneNamesSection();

                // Send command to ESP32 to light up lanes with identification colors
                for (let i = 0; i < currentSettings.numLanes; i++) {
                    const color = laneIdentificationColors[i];
                    sendLaneIdentificationCommand(i, color);
                }
            } else {
                // Stop identification mode
                laneIdentificationMode = false;
                button.textContent = 'Identify Lanes';
                button.style.backgroundColor = '#007bff';
                button.style.color = 'white';
                button.style.borderColor = '#007bff';

                // Update the lane names section to hide color indicators
                updateLaneNamesSection();

                // Send command to ESP32 to stop identification and resume normal operation
                stopLaneIdentification();
            }
        }

        function sendLaneIdentificationCommand(laneIndex, colorHex) {
            // Convert hex color to RGB
            const r = parseInt(colorHex.substr(1, 2), 16);
            const g = parseInt(colorHex.substr(3, 2), 16);
            const b = parseInt(colorHex.substr(5, 2), 16);

            fetch('/identifyLane', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `lane=${laneIndex}&r=${r}&g=${g}&b=${b}`
            }).catch(error => {
                console.log('Lane identification command failed (standalone mode)');
            });
        }

        function stopLaneIdentification() {
            fetch('/stopIdentification', {
                method: 'POST'
            }).catch(error => {
                console.log('Stop identification command failed (standalone mode)');
            });
        }

        function updateCurrentLane() {
            const newLane = parseInt(document.getElementById('currentLane').value);
            currentSettings.currentLane = newLane;

            // Update isRunning to reflect the new lane's state
            currentSettings.isRunning = laneRunning[newLane];

            // Get elements for swimmer set management
            const swimmerSetDiv = document.getElementById('swimmerSet');
            const configControls = document.getElementById('configControls');
            const createSetBtn = document.getElementById('createSetBtn');
            const currentSet = getCurrentSwimmerSet();

            // Update button text based on whether the new lane has a set
            if (currentSet.length > 0) {
                createSetBtn.textContent = 'Modify Set';
            } else {
                createSetBtn.textContent = 'Create Set';
            }

            // If swimmer set is currently displayed, update it for the new lane
            if (swimmerSetDiv && swimmerSetDiv.style.display === 'block') {
                if (currentSet.length > 0) {
                    // New lane has a set, display it
                    displaySwimmerSet();
                } else {
                    // New lane has no set, hide set display and show config controls
                    configControls.style.display = 'block';
                    swimmerSetDiv.style.display = 'none';
                    createSetBtn.textContent = 'Create Set';
                }
            }

            // Update the status display for the new lane
            updateStatus();

            // Update queue display for the new lane
            updateQueueDisplay();
            updatePacerButtons();

            // If the new lane is running, start status updates
            if (currentSettings.isRunning) {
                initializePacerStatus();
                startStatusUpdates();
            } else {
                stopStatusUpdates();
            }
        }

        function getCurrentSwimmerSet() {
            return swimmerSets[currentSettings.currentLane] || [];
        }

        function setCurrentSwimmerSet(newSet) {
            swimmerSets[currentSettings.currentLane] = newSet;
        }

        // Helper functions for running set data (immutable copies)
        function getRunningSwimmerSet() {
            const currentLane = currentSettings.currentLane;
            return runningSets[currentLane] || getCurrentSwimmerSet();
        }

        function getRunningSettings() {
            const currentLane = currentSettings.currentLane;
            return runningSettings[currentLane] || currentSettings;
        }

        function updateBrightness() {
            const brightnessPercent = document.getElementById('brightness').value;

            // Convert percentage (0-100) to internal value (20-255)
            // Formula: internal = 20 + (percent / 100) * (255 - 20)
            const internalBrightness = Math.round(20 + (brightnessPercent / 100) * (255 - 20));

            currentSettings.brightness = internalBrightness;
            document.getElementById('brightnessValue').textContent = brightnessPercent + '%';
            updateSettings();
        }

        function initializeBrightnessDisplay() {
            // Convert internal brightness (20-255) back to percentage (0-100)
            // Formula: percent = (internal - 20) * 100 / (255 - 20)
            const brightnessPercent = Math.round((currentSettings.brightness - 20) * 100 / (255 - 20));

            document.getElementById('brightness').value = brightnessPercent;
            document.getElementById('brightnessValue').textContent = brightnessPercent + '%';
        }

        function updatePulseWidth() {
            const pulseWidth = document.getElementById('pulseWidth').value;
            currentSettings.pulseWidth = parseFloat(pulseWidth);
            const unit = parseFloat(pulseWidth) === 1.0 ? ' foot' : ' feet';
            document.getElementById('pulseWidthValue').textContent = pulseWidth + unit;
            updateSettings();
        }

        function updateRestTime() {
            const restTime = document.getElementById('restTime').value;
            currentSettings.restTime = parseInt(restTime);
            const unit = parseInt(restTime) === 1 ? ' second' : ' seconds';
            document.getElementById('restTimeValue').textContent = restTime + unit;
            updateSettings();
        }

        function updateInitialDelay() {
            const initialDelay = document.getElementById('initialDelay').value;
            currentSettings.initialDelay = parseInt(initialDelay);
            const unit = parseInt(initialDelay) === 1 ? ' second' : ' seconds';
            document.getElementById('initialDelayValue').textContent = initialDelay + unit;
            updateSettings();
        }

        function updateSwimmerInterval() {
            const swimmerInterval = document.getElementById('swimmerInterval').value;
            currentSettings.swimmerInterval = parseInt(swimmerInterval);
            const unit = parseInt(swimmerInterval) === 1 ? ' second' : ' seconds';
            document.getElementById('swimmerIntervalValue').textContent = swimmerInterval + unit;
            updateSettings();
        }

        function updateDelayIndicatorsEnabled() {
            const enabled = document.getElementById('delayIndicatorsEnabled').checked;
            currentSettings.delayIndicatorsEnabled = enabled;

            // Update toggle labels
            const toggleOff = document.getElementById('delayIndicatorOff');
            const toggleOn = document.getElementById('delayIndicatorOn');

            if (enabled) {
                toggleOff.classList.remove('active');
                toggleOn.classList.add('active');
            } else {
                toggleOff.classList.add('active');
                toggleOn.classList.remove('active');
            }

            updateSettings();
        }

        function updateUnderwatersEnabled() {
            const enabled = document.getElementById('underwatersEnabled').checked;
            currentSettings.underwatersEnabled = enabled;

            // Show/hide underwaters controls
            const controls = document.getElementById('underwatersControls');
            controls.style.display = enabled ? 'block' : 'none';

            // Update toggle labels
            const toggleOff = document.getElementById('toggleOff');
            const toggleOn = document.getElementById('toggleOn');

            if (enabled) {
                toggleOff.classList.remove('active');
                toggleOn.classList.add('active');
            } else {
                toggleOff.classList.add('active');
                toggleOn.classList.remove('active');
            }

            updateSettings();
        }

        function updateLightSize() {
            const lightSize = document.getElementById('lightSize').value;
            currentSettings.lightSize = parseFloat(lightSize);
            const unit = parseFloat(lightSize) === 1.0 ? ' foot' : ' feet';
            document.getElementById('lightSizeValue').textContent = lightSize + unit;
            updateSettings();
        }

        function updateFirstUnderwaterDistance() {
            const distance = document.getElementById('firstUnderwaterDistance').value;
            currentSettings.firstUnderwaterDistance = parseInt(distance);
            const unit = parseInt(distance) === 1 ? ' foot' : ' feet';
            document.getElementById('firstUnderwaterDistanceValue').textContent = distance + unit;
            updateSettings();
        }

        function updateUnderwaterDistance() {
            const distance = document.getElementById('underwaterDistance').value;
            currentSettings.underwaterDistance = parseInt(distance);
            const unit = parseInt(distance) === 1 ? ' foot' : ' feet';
            document.getElementById('underwaterDistanceValue').textContent = distance + unit;
            updateSettings();
        }

        function updateHideAfter() {
            const hideAfter = document.getElementById('hideAfter').value;
            currentSettings.hideAfter = parseInt(hideAfter);
            const unit = parseInt(hideAfter) === 1 ? ' second' : ' seconds';
            document.getElementById('hideAfterValue').textContent = hideAfter + unit;
            updateSettings();
        }

        function openColorPickerForUnderwater() {
            currentColorContext = 'underwater';
            openColorPicker();
        }

        function openColorPickerForSurface() {
            currentColorContext = 'surface';
            openColorPicker();
        }

        function updateNumSwimmers() {
            const numSwimmers = document.getElementById('numSwimmers').value;
            currentSettings.numSwimmers = parseInt(numSwimmers);
            document.getElementById('numSwimmersValue').textContent = numSwimmers;
            updateSettings();
        }

        function updateNumRounds() {
            const numRounds = document.getElementById('numRounds').value;
            currentSettings.numRounds = parseInt(numRounds);
            updateSettings();
        }

        function updateColorMode() {
            const colorMode = document.querySelector('input[name="colorMode"]:checked').value;
            currentSettings.colorMode = colorMode;
            updateVisualSelection();
            updateSettings();
        }

        function selectIndividualColors() {
            document.getElementById('individualColors').checked = true;
            updateColorMode();
        }

        function selectSameColor() {
            document.getElementById('sameColor').checked = true;
            updateColorMode();
        }

        function selectSameColorAndOpenPicker(event) {
            // Prevent the row click from triggering
            event.stopPropagation();

            // Select same color mode
            document.getElementById('sameColor').checked = true;
            updateColorMode();

            // Open color picker
            openColorPicker();
        }

        function updateVisualSelection() {
            const isIndividual = document.getElementById('individualColors').checked;
            const individualRow = document.getElementById('individualColorsRow');
            const sameColorRow = document.getElementById('sameColorRow');

            // Update visual feedback by highlighting the selected row
            if (isIndividual) {
                individualRow.style.backgroundColor = '#e3f2fd';
                individualRow.style.border = '2px solid #007bff';
                sameColorRow.style.backgroundColor = 'transparent';
                sameColorRow.style.border = '2px solid transparent';
            } else {
                individualRow.style.backgroundColor = 'transparent';
                individualRow.style.border = '2px solid transparent';
                sameColorRow.style.backgroundColor = '#e3f2fd';
                sameColorRow.style.border = '2px solid #007bff';
            }
        }

        function openColorPicker() {
            // Reset swimmer index (this is called from coach config)
            currentSwimmerIndex = -1;

            // Populate color grid if not already done
            populateColorGrid();

            // Show the custom color picker modal
            document.getElementById('customColorPicker').style.display = 'block';
        }

        function closeColorPicker() {
            document.getElementById('customColorPicker').style.display = 'none';
            currentSwimmerIndex = -1; // Reset swimmer index when closing
        }

        function populateColorGrid() {
            const colorGrid = document.getElementById('colorGrid');
            if (colorGrid.children.length > 0) return; // Already populated

            // Define a palette of common colors
            const colors = [
                '#ff0000', '#00ff00', '#0000ff', '#ffff00', '#ff00ff', '#00ffff',
                '#800000', '#008000', '#000080', '#808000', '#800080', '#008080',
                '#ff8000', '#80ff00', '#8000ff', '#ff0080', '#0080ff', '#ff8080',
                '#ffa500', '#90ee90', '#add8e6', '#f0e68c', '#dda0dd', '#afeeee',
                '#ffffff', '#c0c0c0', '#808080', '#404040', '#202020', '#000000'
            ];

            colors.forEach(color => {
                const colorDiv = document.createElement('div');
                colorDiv.style.cssText = `
                    width: 40px;
                    height: 40px;
                    background-color: ${color};
                    border: 2px solid #333;
                    border-radius: 50%;
                    cursor: pointer;
                    transition: transform 0.1s;
                `;
                colorDiv.onmouseover = () => colorDiv.style.transform = 'scale(1.1)';
                colorDiv.onmouseout = () => colorDiv.style.transform = 'scale(1)';
                colorDiv.onclick = () => selectColor(color);
                colorGrid.appendChild(colorDiv);
            });
        }

        function selectColor(color) {
            if (currentSwimmerIndex >= 0) {
                // Updating individual swimmer color
                const currentSet = getCurrentSwimmerSet();
                // Defensive programming: ensure we don't accidentally modify all swimmers
                if (currentSwimmerIndex < currentSet.length) {
                    // Store the hex color directly to avoid shared reference issues
                    currentSet[currentSwimmerIndex].color = color;
                    console.log(`Updated swimmer ${currentSwimmerIndex} in ${currentSettings.laneNames[currentSettings.currentLane]} color to ${color}`);
                    displaySwimmerSet();
                }
                currentSwimmerIndex = -1; // Reset
            } else if (currentColorContext === 'underwater') {
                // Updating underwater color
                currentSettings.underwaterColor = color;
                document.getElementById('underwaterColorIndicator').style.backgroundColor = color;
                updateSettings();
            } else if (currentColorContext === 'surface') {
                // Updating surface color
                currentSettings.surfaceColor = color;
                document.getElementById('surfaceColorIndicator').style.backgroundColor = color;
                updateSettings();
            } else {
                // Updating coach config same color setting
                currentSettings.swimmerColor = color;
                document.getElementById('colorIndicator').style.backgroundColor = color;
                updateSettings();
            }

            currentColorContext = null; // Reset context
            closeColorPicker();
        }

        function updateSwimmerColor() {
            const swimmerColor = document.getElementById('swimmerColorPicker').value;
            currentSettings.swimmerColor = swimmerColor;

            // Update the color indicator
            document.getElementById('colorIndicator').style.backgroundColor = swimmerColor;

            updateSettings();
        }

        // Pacer status tracking variables - now lane-specific
        let pacerStartTimes = [0, 0, 0, 0]; // Start time for each lane
        let currentRounds = [1, 1, 1, 1]; // Current round for each lane
        let statusUpdateInterval = null;
        let currentColorContext = null; // Track which color picker context we're in

        function updateStatus() {
            const status = currentSettings.isRunning ? "Pacer Started" : "Pacer Stopped";
            const statusElement = document.getElementById('status');
            const toggleBtnElement = document.getElementById('toggleBtn');
            
            if (statusElement) {
                statusElement.textContent = status;
                statusElement.className = 'status ' + (currentSettings.isRunning ? 'running' : 'stopped');
            }
            
            // toggleBtn doesn't exist in queue-based interface, so check first
            if (toggleBtnElement) {
                toggleBtnElement.textContent = currentSettings.isRunning ? 'Stop Pacer' : 'Start Pacer';
            }
        }

        function togglePacer() {
            // Toggle the running state for the current lane
            const currentLane = currentSettings.currentLane;
            laneRunning[currentLane] = !laneRunning[currentLane];
            currentSettings.isRunning = laneRunning[currentLane];

            // Update UI immediately for better responsiveness
            updateStatus();

            // Handle detailed status display
            const detailedStatus = document.getElementById('detailedStatus');
            if (currentSettings.isRunning) {
                // Starting pacer for current lane - create immutable copies
                pacerStartTimes[currentLane] = Date.now();
                currentRounds[currentLane] = 1;

                // Create immutable copies of current set and settings
                runningSets[currentLane] = JSON.parse(JSON.stringify(getCurrentSwimmerSet()));
                runningSettings[currentLane] = {
                    paceDistance: currentSettings.paceDistance,
                    pacePer50: parseFloat(document.getElementById('pacePer50').value),
                    restTime: currentSettings.restTime,
                    numRounds: currentSettings.numRounds,
                    initialDelay: currentSettings.initialDelay,
                    numSwimmers: currentSettings.numSwimmers,
                    laneName: currentSettings.laneNames[currentLane]
                };

                initializePacerStatus();
                startStatusUpdates();
            } else {
                // Stopping pacer for current lane - clear running copies
                runningSets[currentLane] = null;
                runningSettings[currentLane] = null;
                stopStatusUpdates();
            }

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

        function initializePacerStatus() {
            const currentLane = currentSettings.currentLane;
            const runningData = getRunningSettings();

            document.getElementById('currentRound').textContent = currentRounds[currentLane];
            document.getElementById('totalRounds').textContent = runningData.numRounds;

            // Initialize round timing display using running settings
            const paceSeconds = runningData.pacePer50;
            const restSeconds = runningData.restTime;
            const totalRoundTime = paceSeconds + restSeconds;
            const totalRoundMinutes = Math.floor(totalRoundTime / 60);
            const totalRoundSecondsOnly = Math.floor(totalRoundTime % 60);
            const totalTimeStr = `${totalRoundMinutes}:${totalRoundSecondsOnly.toString().padStart(2, '0')}`;
            document.getElementById('roundTiming').textContent = `0:00 / ${totalTimeStr}`;

            document.getElementById('activeSwimmers').textContent = '0';

            // Update set basics display using running settings
            const paceDistance = runningData.paceDistance;
            const numRounds = runningData.numRounds;
            document.getElementById('setBasics').textContent = `- ${numRounds} x ${paceDistance}'s`;

            // Show initial delay countdown using running settings
            if (runningData.initialDelay > 0) {
                document.getElementById('nextEvent').textContent = `Starting in ${runningData.initialDelay}s`;
                document.getElementById('currentPhase').textContent = 'Initial Delay';
            } else {
                document.getElementById('nextEvent').textContent = 'Starting now';
                document.getElementById('currentPhase').textContent = 'Swimming';
            }

            document.getElementById('elapsedTime').textContent = '00:00';
        }

        function startStatusUpdates() {
            statusUpdateInterval = setInterval(updatePacerStatus, 1000); // Update every second
        }

        function stopStatusUpdates() {
            if (statusUpdateInterval) {
                clearInterval(statusUpdateInterval);
                statusUpdateInterval = null;
            }
        }

        function updatePacerStatus() {
            if (!currentSettings.isRunning) return;

            const currentLane = currentSettings.currentLane;
            const runningData = getRunningSettings();
            const elapsedSeconds = Math.floor((Date.now() - pacerStartTimes[currentLane]) / 1000);
            const minutes = Math.floor(elapsedSeconds / 60);
            const seconds = elapsedSeconds % 60;
            document.getElementById('elapsedTime').textContent =
                `${minutes.toString().padStart(2, '0')}:${seconds.toString().padStart(2, '0')}`;

            // Account for initial delay using running settings
            const initialDelaySeconds = runningData.initialDelay;
            const timeAfterInitialDelay = elapsedSeconds - initialDelaySeconds;

            // Check if we're still in the initial delay period
            if (timeAfterInitialDelay < 0) {
                // Still in initial delay phase
                document.getElementById('currentPhase').textContent = 'Initial Delay';
                document.getElementById('activeSwimmers').textContent = '0';
                document.getElementById('nextEvent').textContent = `Starting in ${Math.ceil(-timeAfterInitialDelay)}s`;

                // Show initial round timing during delay using running settings
                const paceSeconds = runningData.pacePer50;
                const restSeconds = runningData.restTime;
                const totalRoundTime = paceSeconds + restSeconds;
                const totalRoundMinutes = Math.floor(totalRoundTime / 60);
                const totalRoundSecondsOnly = Math.floor(totalRoundTime % 60);
                const totalTimeStr = `${totalRoundMinutes}:${totalRoundSecondsOnly.toString().padStart(2, '0')}`;
                document.getElementById('roundTiming').textContent = `0:00 / ${totalTimeStr}`;

                document.getElementById('currentRound').textContent = '1';
                return;
            }

            // Calculate current phase and progress (after initial delay) using running settings
            const paceSeconds = runningData.pacePer50;
            const restSeconds = runningData.restTime;
            const totalRoundTime = paceSeconds + restSeconds;

            const timeInCurrentRound = timeAfterInitialDelay % totalRoundTime;

            // Format round timing display
            const currentRoundMinutes = Math.floor(timeInCurrentRound / 60);
            const currentRoundSeconds = Math.floor(timeInCurrentRound % 60);
            const totalRoundMinutes = Math.floor(totalRoundTime / 60);
            const totalRoundSecondsOnly = Math.floor(totalRoundTime % 60);

            const currentTimeStr = `${currentRoundMinutes}:${currentRoundSeconds.toString().padStart(2, '0')}`;
            const totalTimeStr = `${totalRoundMinutes}:${totalRoundSecondsOnly.toString().padStart(2, '0')}`;

            document.getElementById('roundTiming').textContent = `${currentTimeStr} / ${totalTimeStr}`;

            // Determine current phase
            if (timeInCurrentRound < paceSeconds) {
                document.getElementById('currentPhase').textContent = 'Swimming';
                document.getElementById('activeSwimmers').textContent = runningData.numSwimmers;
                const remainingSwimTime = paceSeconds - timeInCurrentRound;
                document.getElementById('nextEvent').textContent = `Rest in ${Math.ceil(remainingSwimTime)}s`;
            } else {
                document.getElementById('currentPhase').textContent = 'Rest Period';
                document.getElementById('activeSwimmers').textContent = '0';
                const remainingRestTime = totalRoundTime - timeInCurrentRound;
                document.getElementById('nextEvent').textContent = `Next round in ${Math.ceil(remainingRestTime)}s`;
            }

            // Update current round (after initial delay)
            const calculatedRound = Math.floor(timeAfterInitialDelay / totalRoundTime) + 1;
            if (calculatedRound !== currentRounds[currentLane] && calculatedRound <= runningData.numRounds) {
                currentRounds[currentLane] = calculatedRound;
                document.getElementById('currentRound').textContent = currentRounds[currentLane];
            }

            // Check if set is complete using running settings
            if (calculatedRound > runningData.numRounds) {
                document.getElementById('currentPhase').textContent = 'Set Complete!';
                document.getElementById('nextEvent').textContent = 'Finished';
                document.getElementById('activeSwimmers').textContent = '0';
                
                // Handle queue completion
                if (activeWorkSet) {
                    // Set is complete, remove from queue and handle next set
                    setTimeout(() => {
                        handleSetCompletion();
                    }, 2000); // Give user 2 seconds to see completion message
                }
            }
        }

        function createWorkSet() {
            // Validate configuration first
            if (currentSettings.numSwimmers < 1) {
                alert('Please set at least 1 swimmer');
                return;
            }
            
            if (currentSettings.numRounds < 1) {
                alert('Please set at least 1 round');
                return;
            }

            const configControls = document.getElementById('configControls');
            const swimmerSetDiv = document.getElementById('swimmerSet');

            // Clear existing set for current lane
            setCurrentSwimmerSet([]);

            // Get current pace from the main settings
            const currentPace = parseFloat(document.getElementById('pacePer50').value);

            // Create swimmer configurations for current lane
            const newSet = [];
            for (let i = 0; i < currentSettings.numSwimmers; i++) {
                // Determine color based on color mode
                let swimmerColor;
                if (currentSettings.colorMode === 'same') {
                    // Store the actual hex color directly to avoid shared "custom" reference
                    swimmerColor = currentSettings.swimmerColor;
                } else {
                    // Use predefined colors for individual mode
                    swimmerColor = colorHex[swimmerColors[i]];
                }

                // Create individual swimmer object (no shared references)
                const newSwimmer = {
                    id: i + 1,
                    color: swimmerColor, // Store hex color directly
                    pace: currentPace,
                    interval: i === 0 ? currentSettings.initialDelay : currentSettings.initialDelay + (i * currentSettings.swimmerInterval), // First swimmer uses initial delay, others add swimmer intervals
                    lane: currentSettings.currentLane // Track which lane this swimmer belongs to
                };

                newSet.push(newSwimmer);
                console.log(`Created swimmer ${i + 1} for ${currentSettings.laneNames[currentSettings.currentLane]} with color: ${swimmerColor}`);
            }

            // Store the set for current lane
            setCurrentSwimmerSet(newSet);

            // Create work set object with metadata
            createdWorkSets[currentSettings.currentLane] = {
                id: Date.now(), // Simple unique ID
                lane: currentSettings.currentLane,
                laneName: currentSettings.laneNames[currentSettings.currentLane],
                swimmers: newSet,
                settings: { 
                    ...currentSettings,
                    pacePer50: currentPace // Explicitly include the pace value
                }, // Deep copy of current settings with pace
                summary: generateSetSummary(newSet, currentSettings)
            };

            // Display the set
            displaySwimmerSet();

            // Hide config controls and show swimmer set
            configControls.style.display = 'none';
            swimmerSetDiv.style.display = 'block';
            
            // Switch to queue buttons
            document.getElementById('configButtons').style.display = 'none';
            document.getElementById('queueButtons').style.display = 'block';
        }

        function generateSetSummary(swimmers, settings) {
            const paceDistance = settings.paceDistance;
            const avgPace = swimmers.length > 0 ? swimmers[0].pace : 30;
            const restTime = settings.restTime;
            const numRounds = settings.numRounds;
            
            return `${numRounds} x ${paceDistance}'s on the ${avgPace} with ${restTime} sec rest`;
        }

        function queueWorkSet() {
            const currentLane = currentSettings.currentLane;
            
            if (!createdWorkSets[currentLane]) {
                alert('No set to queue');
                return;
            }

            // Add to current lane's queue
            workSetQueues[currentLane].push(createdWorkSets[currentLane]);
            
            // Clear created set for current lane
            createdWorkSets[currentLane] = null;
            
            // Return to configuration mode
            returnToConfigMode();
            
            // Update queue display
            updateQueueDisplay();
            
            // Show start button if this is the first set
            updatePacerButtons();
        }

        function cancelWorkSet() {
            const currentLane = currentSettings.currentLane;
            createdWorkSets[currentLane] = null;
            returnToConfigMode();
        }

        function returnToConfigMode() {
            // Show config controls and hide swimmer set
            document.getElementById('configControls').style.display = 'block';
            document.getElementById('swimmerSet').style.display = 'none';
            
            // Switch back to config buttons
            document.getElementById('configButtons').style.display = 'block';
            document.getElementById('queueButtons').style.display = 'none';
            document.getElementById('editButtons').style.display = 'none';
        }

        function updateQueueDisplay() {
            const currentLane = currentSettings.currentLane;
            const queueList = document.getElementById('queueList');
            
            if (!queueList) {
                console.error('Queue list element not found');
                return;
            }
            
            if (workSetQueues[currentLane].length === 0) {
                queueList.innerHTML = '<div style="color: #666; font-style: italic;">No sets queued for Lane ' + (currentLane + 1) + '</div>';
                return;
            }
            
            let html = '';
            workSetQueues[currentLane].forEach((workSet, index) => {
                const isActive = activeWorkSets[currentLane] && activeWorkSets[currentLane].id === workSet.id;
                const statusClass = isActive ? 'style="background: #e7f3ff; border: 1px solid #2196F3;"' : '';
                
                html += `
                    <div ${statusClass} style="padding: 8px; margin: 5px 0; border-radius: 4px; border: 1px solid #ddd;">
                        <div style="display: flex; justify-content: space-between; align-items: center;">
                            <div>
                                <div style="font-weight: bold; color: ${isActive ? '#1976D2' : '#333'};">
                                    ${workSet.summary}
                                </div>
                            </div>
                            <div style="display: flex; gap: 5px;">
                                ${!isActive ? `<button onclick="editWorkSet(${index})" style="padding: 2px 6px; font-size: 12px; background: #007bff; color: white; border: none; border-radius: 3px; cursor: pointer;">Edit</button>` : ''}
                                ${!isActive ? `<button onclick="deleteWorkSet(${index})" style="padding: 2px 6px; font-size: 12px; background: #dc3545; color: white; border: none; border-radius: 3px; cursor: pointer;">Delete</button>` : ''}
                                ${isActive ? `<div style="font-weight: bold; color: #1976D2;">Total: <span id="elapsedTime">00:00</span></div>` : ''}
                            </div>
                        </div>
                        ${isActive ? `
                            <div style="margin-top: 8px; display: grid; grid-template-columns: 1fr 1fr; gap: 10px; font-size: 12px; color: #555;">
                                <div><strong>Round:</strong> <span id="currentRound">1</span> of <span id="totalRounds">10</span></div>
                                <div><strong>Round:</strong> <span id="roundTiming">0:00 / 0:00</span></div>
                                <div><strong>Active Swimmers:</strong> <span id="activeSwimmers">0</span></div>
                                <div><strong>Next Event:</strong> <span id="nextEvent">Starting...</span></div>
                            </div>
                            <div style="margin-top: 6px; background: #fff; border-radius: 3px; padding: 6px; font-size: 11px;">
                                <strong>Current Phase:</strong> <span id="currentPhase">Preparing to start</span>
                            </div>
                        ` : ''}
                    </div>
                `;
            });
            
            queueList.innerHTML = html;
        }

        function updatePacerButtons() {
            const currentLane = currentSettings.currentLane;
            const pacerButtons = document.getElementById('pacerButtons');
            const startBtn = document.getElementById('startBtn');
            const stopBtn = document.getElementById('stopBtn');
            
            if (!pacerButtons || !startBtn || !stopBtn) {
                console.error('Pacer button elements not found');
                return;
            }
            
            if (workSetQueues[currentLane].length > 0) {
                pacerButtons.style.display = 'block';
                if (activeWorkSets[currentLane]) {
                    startBtn.style.display = 'none';
                    stopBtn.style.display = 'block';
                } else {
                    startBtn.style.display = 'block';
                    stopBtn.style.display = 'none';
                }
            } else {
                pacerButtons.style.display = 'none';
            }
        }

        function editWorkSet(index) {
            const currentLane = currentSettings.currentLane;
            
            if (index < 0 || index >= workSetQueues[currentLane].length) return;
            
            const workSet = workSetQueues[currentLane][index];
            editingWorkSetIndexes[currentLane] = index;
            
            // Load work set data into configuration
            loadWorkSetIntoConfig(workSet);
            
            // Show swimmer set for editing
            displaySwimmerSet();
            document.getElementById('configControls').style.display = 'none';
            document.getElementById('swimmerSet').style.display = 'block';
            
            // Switch to edit buttons
            document.getElementById('configButtons').style.display = 'none';
            document.getElementById('editButtons').style.display = 'block';
        }

        function loadWorkSetIntoConfig(workSet) {
            // Restore settings
            Object.assign(currentSettings, workSet.settings);
            
            // Set current lane
            currentSettings.currentLane = workSet.lane;
            
            // Restore swimmer set
            setCurrentSwimmerSet(workSet.swimmers);
            
            // Update all UI elements to reflect loaded settings
            updateAllUIFromSettings();
        }

        function saveWorkSet() {
            const currentLane = currentSettings.currentLane;
            
            if (editingWorkSetIndexes[currentLane] === -1) return;
            
            // Update the work set in the current lane's queue
            const currentSet = getCurrentSwimmerSet();
            workSetQueues[currentLane][editingWorkSetIndexes[currentLane]] = {
                ...workSetQueues[currentLane][editingWorkSetIndexes[currentLane]],
                swimmers: currentSet,
                settings: { ...currentSettings },
                summary: generateSetSummary(currentSet, currentSettings)
            };
            
            editingWorkSetIndexes[currentLane] = -1;
            returnToConfigMode();
            updateQueueDisplay();
        }

        function cancelEdit() {
            const currentLane = currentSettings.currentLane;
            editingWorkSetIndexes[currentLane] = -1;
            returnToConfigMode();
        }

        function deleteWorkSet(index) {
            const currentLane = currentSettings.currentLane;
            
            if (index < 0 || index >= workSetQueues[currentLane].length) return;
            
            if (confirm('Delete this work set from Lane ' + (currentLane + 1) + '?')) {
                workSetQueues[currentLane].splice(index, 1);
                updateQueueDisplay();
                updatePacerButtons();
            }
        }

        function startQueue() {
            const currentLane = currentSettings.currentLane;
            
            if (workSetQueues[currentLane].length === 0) return;
            
            // Start the first work set in current lane's queue
            activeWorkSets[currentLane] = workSetQueues[currentLane][0];
            
            // Load the active set into the pacer system
            loadWorkSetForExecution(activeWorkSets[currentLane]);
            
            // Start the pacer
            startPacerExecution();
            
            // Update displays
            updateQueueDisplay();
            updatePacerButtons();
        }

        function stopQueue() {
            const currentLane = currentSettings.currentLane;
            
            // Stop current execution
            stopPacerExecution();
            
            // Clear active set for current lane
            activeWorkSets[currentLane] = null;
            
            // Update displays
            updateQueueDisplay();
            updatePacerButtons();
        }

        function loadWorkSetForExecution(workSet) {
            // Set the current lane
            currentSettings.currentLane = workSet.lane;
            
            // Load settings
            Object.assign(currentSettings, workSet.settings);
            
            // Update the DOM input field with the work set's pace
            if (workSet.settings.pacePer50) {
                document.getElementById('pacePer50').value = workSet.settings.pacePer50;
            }
            
            // Load swimmer set
            setCurrentSwimmerSet(workSet.swimmers);
            
            // Update lane selector to reflect current lane
            updateLaneSelector();
        }

        function startPacerExecution() {
            // Use existing pacer start logic
            const currentLane = currentSettings.currentLane;
            const currentSet = getCurrentSwimmerSet();
            
            if (currentSet.length === 0) {
                alert('No swimmers configured for this lane');
                return;
            }

            // Create immutable copies for running state
            runningSets[currentLane] = JSON.parse(JSON.stringify(currentSet));
            runningSettings[currentLane] = JSON.parse(JSON.stringify(currentSettings));

            // Start the pacer for this lane
            laneRunning[currentLane] = true;
            currentSettings.isRunning = true;

            // Initialize timing
            if (!pacerStartTimes[currentLane]) {
                pacerStartTimes[currentLane] = Date.now();
            }

            // Send start command to ESP32
            sendStartCommand();

            // Start status updates (timer display)
            startStatusUpdates();

            // Update status display
            updateStatus();
        }

        function stopPacerExecution() {
            const currentLane = currentSettings.currentLane;
            
            // Stop the pacer for this lane
            laneRunning[currentLane] = false;
            currentSettings.isRunning = false;

            // Stop status updates (timer display)
            stopStatusUpdates();

            // Clear running state
            runningSets[currentLane] = null;
            runningSettings[currentLane] = null;

            // Send stop command to ESP32
            sendStopCommand();
        }

        // Functions to communicate with ESP32
        function sendStartCommand() {
            // First update ESP32 with current work set settings
            updateSettings();
            
            // Small delay to ensure settings are applied before starting
            setTimeout(() => {
                fetch('/toggle', { method: 'POST' })
                .then(response => response.text())
                .then(result => {
                    console.log('Start command sent:', result);
                })
                .catch(error => {
                    console.log('Running in standalone mode - start command not sent');
                });
            }, 100); // 100ms delay
        }

        function sendStopCommand() {
            fetch('/toggle', { method: 'POST' })
            .then(response => response.text())
            .then(result => {
                console.log('Stop command sent:', result);
            })
            .catch(error => {
                console.log('Running in standalone mode - stop command not sent');
            });
        }

        // Update the original createSet function to call the new workflow
        function createSet() {
            // Redirect to new workflow
            createWorkSet();
        }

        function displaySwimmerSet() {
            const currentSet = getCurrentSwimmerSet();

            // Update set details in swim practice nomenclature
            const setDetails = document.getElementById('setDetails');
            const paceDistance = currentSettings.paceDistance;
            const avgPace = currentSet.length > 0 ? currentSet[0].pace : parseFloat(document.getElementById('pacePer50').value);
            const restTime = currentSettings.restTime;
            const numRounds = currentSettings.numRounds;
            const laneName = currentSettings.laneNames[currentSettings.currentLane];

            setDetails.innerHTML = `${laneName}: ${numRounds} x ${paceDistance}'s on the ${avgPace} with ${restTime} sec rest`;

            const swimmerList = document.getElementById('swimmerList');
            swimmerList.innerHTML = '';

            currentSet.forEach((swimmer, index) => {
                const row = document.createElement('div');
                row.className = 'swimmer-row';

                // Calculate delay from previous swimmer
                let delayFromPrevious;
                if (index === 0) {
                    delayFromPrevious = currentSettings.initialDelay; // First swimmer shows initial delay
                } else {
                    delayFromPrevious = currentSettings.swimmerInterval; // Others show interval between swimmers
                }

                // Determine the actual color value for display
                let displayColor;
                if (swimmer.color.startsWith('#')) {
                    // It's already a hex color
                    displayColor = swimmer.color;
                } else {
                    // It's a color name, look it up
                    displayColor = colorHex[swimmer.color] || swimmer.color;
                }

                row.innerHTML = `
                    <div class="swimmer-color" style="background-color: ${displayColor}"
                         onclick="cycleSwimmerColor(${index})" title="Click to change color"></div>
                    <div class="swimmer-info">Swimmer ${swimmer.id}</div>
                    <div>Delay: ${delayFromPrevious}s</div>
                    <div>Pace: <input type="number" class="swimmer-pace-input" value="${swimmer.pace}"
                         min="20" max="300" step="0.5" onchange="updateSwimmerPace(${index}, this.value)"> sec</div>
                `;

                swimmerList.appendChild(row);
            });
        }

        function cycleSwimmerColor(swimmerIndex) {
            const currentSet = getCurrentSwimmerSet();
            // Defensive programming: validate swimmer index
            if (swimmerIndex < 0 || swimmerIndex >= currentSet.length) {
                console.error(`Invalid swimmer index: ${swimmerIndex}`);
                return;
            }

            // Set the current swimmer being edited and open color picker
            currentSwimmerIndex = swimmerIndex;
            console.log(`Opening color picker for swimmer ${swimmerIndex} in ${currentSettings.laneNames[currentSettings.currentLane]}`);
            populateColorGrid();
            document.getElementById('customColorPicker').style.display = 'block';
        }

        function updateSwimmerPace(swimmerIndex, newPace) {
            const currentSet = getCurrentSwimmerSet();
            if (currentSet[swimmerIndex]) {
                currentSet[swimmerIndex].pace = parseFloat(newPace);
            }
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

            fetch('/setNumLanes', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `numLanes=${currentSettings.numLanes}`
            }).catch(error => {
                console.log('Number of lanes update - server not available (standalone mode)');
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

            fetch('/setDelayIndicators', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `enabled=${currentSettings.delayIndicatorsEnabled}`
            }).catch(error => {
                console.log('Delay indicators update - server not available (standalone mode)');
            });

            fetch('/setNumSwimmers', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `numSwimmers=${currentSettings.numSwimmers}`
            }).catch(error => {
                console.log('Number of swimmers update - server not available (standalone mode)');
            });

            fetch('/setNumRounds', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `numRounds=${currentSettings.numRounds}`
            }).catch(error => {
                console.log('Number of rounds update - server not available (standalone mode)');
            });

            fetch('/setCurrentLane', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: `currentLane=${currentSettings.currentLane}`
            }).catch(error => {
                console.log('Current lane update - server not available (standalone mode)');
            });
        }

        // Helper function to update all UI elements from settings
        function updateAllUIFromSettings() {
            // Update input fields
            document.getElementById('numRounds').value = currentSettings.numRounds;
            document.getElementById('restTime').value = currentSettings.restTime;
            document.getElementById('numSwimmers').value = currentSettings.numSwimmers;
            document.getElementById('initialDelay').value = currentSettings.initialDelay;
            document.getElementById('swimmerInterval').value = currentSettings.swimmerInterval;
            document.getElementById('pacePer50').value = currentSettings.speed;
            document.getElementById('paceDistance').value = currentSettings.paceDistance;
            document.getElementById('brightness').value = Math.round((currentSettings.brightness - 20) * 100 / (255 - 20));
            
            // Update display values
            updateNumRounds();
            updateRestTime();
            updateNumSwimmers();
            updateInitialDelay();
            updateSwimmerInterval();
            updateSpeed();
            updatePaceDistance();
            updateBrightness();
            
            // Update lane selector
            updateLaneSelector();
        }

        // Initialize queue display on page load
        function initializeQueueSystem() {
            // Ensure queue display is updated on initialization
            if (document.getElementById('queueList')) {
                updateQueueDisplay();
                updatePacerButtons();
            } else {
                // Retry after a short delay if elements aren't ready
                setTimeout(initializeQueueSystem, 100);
            }
        }

        function handleSetCompletion() {
            const currentLane = currentSettings.currentLane;
            
            if (!activeWorkSets[currentLane]) return;
            
            // Remove completed set from current lane's queue
            const completedSetIndex = workSetQueues[currentLane].findIndex(set => set.id === activeWorkSets[currentLane].id);
            if (completedSetIndex !== -1) {
                workSetQueues[currentLane].splice(completedSetIndex, 1);
            }
            
            // Clear active set for current lane
            activeWorkSets[currentLane] = null;
            
            // Stop current execution
            stopPacerExecution();
            
            // Check if there are more sets in current lane's queue
            if (workSetQueues[currentLane].length > 0) {
                // Auto-advance to next set after a brief pause
                setTimeout(() => {
                    if (confirm('Start next set in Lane ' + (currentLane + 1) + ' queue?')) {
                        startQueue();
                    } else {
                        // User declined, update displays to show queue
                        updateQueueDisplay();
                        updatePacerButtons();
                    }
                }, 1000);
            } else {
                // No more sets in current lane, return to idle state
                updateQueueDisplay();
                updatePacerButtons();
                alert('All sets completed for Lane ' + (currentLane + 1) + '!');
            }
        }

        // Add function to refresh swimmer set display
        function updateSwimmerSetDisplay() {
            const swimmerSetDiv = document.getElementById('swimmerSet');
            if (swimmerSetDiv && swimmerSetDiv.style.display === 'block') {
                displaySwimmerSet();
            }
        }

        // Initialize
        updateCalculations();
        updateVisualSelection();
        initializeBrightnessDisplay();
        updateLaneSelector();
        updateLaneNamesSection();
        updateStatus();
        
        // Initialize queue system after DOM is ready
        document.addEventListener('DOMContentLoaded', function() {
            initializeQueueSystem();
        });
        
        // Also initialize immediately in case DOM is already loaded
        if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', initializeQueueSystem);
        } else {
            initializeQueueSystem();
        }
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
  json += "\"isRunning\":" + String(settings.laneRunning[currentLane] ? "true" : "false") + ",";
  json += "\"currentLane\":" + String(currentLane);
  json += "}";

  server.send(200, "application/json", json);
}

void handleGetCurrentLane() {
  String json = "{";
  json += "\"currentLane\":" + String(currentLane);
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
  // Toggle the current lane's running state
  settings.laneRunning[currentLane] = !settings.laneRunning[currentLane];

  // Update global running state (true if any lane is running)
  settings.isRunning = false;
  for (int i = 0; i < settings.numLanes; i++) {
    if (settings.laneRunning[i]) {
      settings.isRunning = true;
      break;
    }
  }

  saveSettings();

  String status = settings.laneRunning[currentLane] ? "Lane Pacer Started" : "Lane Pacer Stopped";
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

void handleSetNumLanes() {
  if (server.hasArg("numLanes")) {
    int numLanes = server.arg("numLanes").toInt();
    settings.numLanes = numLanes;
    saveSettings();
    // Reinitialize LEDs when number of lanes changes
    setupLEDs();
  }
  server.send(200, "text/plain", "Number of lanes updated");
}

void handleSetCurrentLane() {
  if (server.hasArg("currentLane")) {
    int lane = server.arg("currentLane").toInt();
    if (lane >= 0 && lane < settings.numLanes) {
      currentLane = lane;
      // Reinitialize swimmers for the new lane if it's running
      if (settings.laneRunning[currentLane]) {
        initializeSwimmers();
      }
    }
  }
  server.send(200, "text/plain", "Current lane updated");
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

void handleSetInitialDelay() {
  if (server.hasArg("initialDelay")) {
    int initialDelay = server.arg("initialDelay").toInt();
    settings.initialDelaySeconds = initialDelay;
    saveSettings();
  }
  server.send(200, "text/plain", "Initial delay updated");
}

void handleSetSwimmerInterval() {
  if (server.hasArg("swimmerInterval")) {
    int swimmerInterval = server.arg("swimmerInterval").toInt();
    settings.swimmerIntervalSeconds = swimmerInterval;
    saveSettings();
  }
  server.send(200, "text/plain", "Swimmer interval updated");
}

void handleSetDelayIndicators() {
  if (server.hasArg("enabled")) {
    bool enabled = server.arg("enabled") == "true";
    settings.delayIndicatorsEnabled = enabled;
    saveSettings();
  }
  server.send(200, "text/plain", "Delay indicators updated");
}

void handleSetNumSwimmers() {
  if (server.hasArg("numSwimmers")) {
    int numSwimmers = server.arg("numSwimmers").toInt();
    settings.numSwimmers = numSwimmers;
    saveSettings();
  }
  server.send(200, "text/plain", "Number of swimmers updated");
}

void handleSetNumRounds() {
  if (server.hasArg("numRounds")) {
    int numRounds = server.arg("numRounds").toInt();
    settings.numRounds = numRounds;
    saveSettings();
  }
  server.send(200, "text/plain", "Number of rounds updated");
}

void saveSettings() {
  preferences.putFloat("poolLengthM", settings.poolLengthMeters);
  preferences.putFloat("stripLengthM", settings.stripLengthMeters);
  preferences.putInt("ledsPerMeter", settings.ledsPerMeter);
  preferences.putFloat("pulseWidthFeet", settings.pulseWidthFeet);
  preferences.putFloat("speedFPS", settings.speedFeetPerSecond);
  preferences.putInt("restTimeSeconds", settings.restTimeSeconds);
  preferences.putInt("paceDistanceYards", settings.paceDistanceYards);
  preferences.putInt("initialDelay", settings.initialDelaySeconds);
  preferences.putInt("swimmerInterval", settings.swimmerIntervalSeconds);
  preferences.putInt("numSwimmers", settings.numSwimmers);
  preferences.putInt("numRounds", settings.numRounds);
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
  settings.initialDelaySeconds = preferences.getInt("initialDelay", 10);
  settings.swimmerIntervalSeconds = preferences.getInt("swimmerInterval", 4);
  settings.numSwimmers = preferences.getInt("numSwimmers", 3);
  settings.numRounds = preferences.getInt("numRounds", 10);
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

  // Update LEDs for all active lanes
  for (int lane = 0; lane < settings.numLanes; lane++) {
    if (leds[lane] != nullptr) {
      // Clear this lane's LEDs
      fill_solid(leds[lane], totalLEDs, CRGB::Black);

      // Only animate if this lane is running
      if (settings.laneRunning[lane]) {
        // Draw delay indicators if enabled for this lane
        if (settings.delayIndicatorsEnabled) {
          drawDelayIndicators(currentTime, lane);
        }

        // Update and draw each active swimmer for this lane
        for (int i = 0; i < settings.numSwimmers; i++) {
          updateSwimmer(i, currentTime, lane);
          drawSwimmerPulse(i, lane);
        }
      }
    }
  }

  // Update FastLED
  FastLED.show();
}

void drawDelayIndicators(unsigned long currentTime, int laneIndex) {
  // Convert feet to meters for calculations
  const float feetToMeters = 0.3048;
  const float maxDelayDistanceFeet = 5.0;
  const float maxDelayDistanceMeters = maxDelayDistanceFeet * feetToMeters;

  // Calculate LEDs for maximum delay distance (5 feet)
  int maxDelayLEDs = (int)(maxDelayDistanceMeters * settings.ledsPerMeter);

  // Check each swimmer (including swimmer 0 for initial delay)
  for (int i = 0; i < settings.numSwimmers; i++) {
    unsigned long swimmerStartTime = swimmers[laneIndex][i].lastUpdate;
    unsigned long delayStartTime;
    int delaySeconds;

    if (i == 0) {
      // First swimmer uses initial delay
      delaySeconds = settings.initialDelaySeconds;
      delayStartTime = swimmerStartTime - (settings.initialDelaySeconds * 1000);
    } else {
      // Subsequent swimmers use swimmer interval delay
      delaySeconds = settings.swimmerIntervalSeconds;
      delayStartTime = swimmerStartTime - (settings.swimmerIntervalSeconds * 1000);
    }

    // Check if we're in the delay period for this swimmer
    if (currentTime >= delayStartTime && currentTime < swimmerStartTime) {
      // Calculate remaining delay time in seconds
      float remainingDelaySeconds = (float)(swimmerStartTime - currentTime) / 1000.0;

      // Determine the delay distance based on remaining time
      float delayDistanceFeet = 0;

      if (delaySeconds <= 5) {
        // For delays 5 seconds or less, show full countdown at 1 foot per second
        delayDistanceFeet = remainingDelaySeconds;
      } else {
        // For delays more than 5 seconds, only show indicator during final 5 seconds
        if (remainingDelaySeconds <= 5.0) {
          delayDistanceFeet = remainingDelaySeconds;
        }
        // If more than 5 seconds remain, delayDistanceFeet stays 0 (no indicator)
      }

      // Only proceed if we should show an indicator
      if (delayDistanceFeet > 0) {
        // Ensure minimum of 0 feet
        if (delayDistanceFeet < 0) delayDistanceFeet = 0;

      // Convert to LEDs
      float delayDistanceMeters = delayDistanceFeet * feetToMeters;
      int delayLEDs = (int)(delayDistanceMeters * settings.ledsPerMeter);

      // Limit to available space and ensure minimum visibility
      if (delayLEDs > maxDelayLEDs) delayLEDs = maxDelayLEDs;
      if (delayLEDs < 1 && delayDistanceFeet > 0) delayLEDs = 1; // Ensure at least 1 LED if there's time left

      // Check for conflicts with active swimmers and draw delay indicator
      CRGB swimmerColor = swimmers[laneIndex][i].color;
      for (int ledIndex = 0; ledIndex < delayLEDs && ledIndex < totalLEDs; ledIndex++) {
        // Check if this LED position conflicts with any active swimmer
        bool hasConflict = false;

        for (int j = 0; j < settings.numSwimmers; j++) {
          // Skip if this swimmer hasn't started yet
          if (currentTime < swimmers[laneIndex][j].lastUpdate) continue;

          // Check if this LED is within the swimmer's pulse range
          int swimmerCenter = swimmers[laneIndex][j].position;
          int halfWidth = pulseWidthLEDs / 2;
          int swimmerStart = swimmerCenter - halfWidth;
          int swimmerEnd = swimmerCenter + halfWidth;

          if (ledIndex >= swimmerStart && ledIndex <= swimmerEnd) {
            hasConflict = true;
            break;
          }
        }

        // Only draw delay indicator if there's no conflict with active swimmers
        if (!hasConflict) {
          // Create a dimmed version of the swimmer's color for the delay indicator
          CRGB delayColor = swimmerColor;
          delayColor.nscale8(128); // 50% brightness for delay indicator
          leds[laneIndex][ledIndex] = delayColor;
        }
      }
      } // Close the "if (delayDistanceFeet > 0)" condition
    }
  }
}

void initializeSwimmers() {
  for (int lane = 0; lane < 4; lane++) {
    for (int i = 0; i < 6; i++) {
      swimmers[lane][i].position = 0;
      swimmers[lane][i].direction = 1;
      swimmers[lane][i].lastUpdate = millis() + (settings.initialDelaySeconds * 1000) + (i * settings.swimmerIntervalSeconds * 1000); // Initial delay + staggered start times
      swimmers[lane][i].color = swimmerColors[i];
    }
  }
}

void updateSwimmer(int swimmerIndex, unsigned long currentTime, int laneIndex) {
  if (currentTime - swimmers[laneIndex][swimmerIndex].lastUpdate >= delayMS) {
    swimmers[laneIndex][swimmerIndex].lastUpdate = currentTime;

    // Move to next position
    swimmers[laneIndex][swimmerIndex].position += swimmers[laneIndex][swimmerIndex].direction;

    // Check for bouncing at ends
    if (swimmers[laneIndex][swimmerIndex].position >= totalLEDs - 1) {
      swimmers[laneIndex][swimmerIndex].direction = -1;
      swimmers[laneIndex][swimmerIndex].position = totalLEDs - 1;
    } else if (swimmers[laneIndex][swimmerIndex].position <= 0) {
      swimmers[laneIndex][swimmerIndex].direction = 1;
      swimmers[laneIndex][swimmerIndex].position = 0;
    }
  }
}

void drawSwimmerPulse(int swimmerIndex, int laneIndex) {
  int centerPos = swimmers[laneIndex][swimmerIndex].position;
  int halfWidth = pulseWidthLEDs / 2;
  CRGB pulseColor = swimmers[laneIndex][swimmerIndex].color;

  for (int i = 0; i < pulseWidthLEDs; i++) {
    int ledIndex = centerPos - halfWidth + i;

    if (ledIndex >= 0 && ledIndex < totalLEDs) {
      int distanceFromCenter = abs(i - halfWidth);
      float brightnessFactor = 1.0 - (float)distanceFromCenter / (float)halfWidth;
      brightnessFactor = max(0.0f, brightnessFactor);

      CRGB color = pulseColor;
      color.nscale8((uint8_t)(brightnessFactor * 255));

      // Add this swimmer's color to existing LED (allows overlapping)
      leds[laneIndex][ledIndex] += color;
    }
  }
}

void drawPulse(int centerPos, int laneIndex = 0) {
  int halfWidth = pulseWidthLEDs / 2;
  CRGB pulseColor = CRGB(settings.colorRed, settings.colorGreen, settings.colorBlue);

  for (int i = 0; i < pulseWidthLEDs; i++) {
    int ledIndex = centerPos - halfWidth + i;

    if (ledIndex >= 0 && ledIndex < totalLEDs) {
      int distanceFromCenter = abs(i - halfWidth);
      float brightnessFactor = 1.0 - (float)distanceFromCenter / (float)halfWidth;
      brightnessFactor = max(0.0f, brightnessFactor);

      CRGB color = pulseColor;
      color.nscale8((uint8_t)(brightnessFactor * 255));

      leds[laneIndex][ledIndex] = color;
    }
  }
}