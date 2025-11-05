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
  4. Configure globalConfigSettings via web interface

  Required Libraries: FastLED (install via Arduino IDE Library Manager)
*/

#include <FastLED.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <new>
// ESP-IDF Bluetooth control (only needed on ESP32 builds)
#ifdef ARDUINO_ARCH_ESP32
#include "esp_bt.h"
#endif

// Build tag for runtime verification. If GIT_COMMIT is provided by build system,
// use it, otherwise use compile time.
#ifndef BUILD_TAG
#ifdef GIT_COMMIT
#define BUILD_TAG "git:" GIT_COMMIT
#else
#define BUILD_TAG __DATE__ " " __TIME__
#endif
#endif

// ========== HARDWARE CONFIGURATION ==========
#define LED_TYPE        WS2812B     // LED strip type
#define COLOR_ORDER     GRB         // Color order (may need adjustment)

// Maximum supported LEDs by FastLED for this project
#define MAX_LEDS 150

// GPIO pins for multiple LED strips (lanes)
// Lane 1: GPIO 18, Lane 2: GPIO 19, Lane 3: GPIO 21, Lane 4: GPIO 2
const int LED_PINS[4] = {18, 19, 21, 2}; // GPIO pins for lanes 1-4

// Debug control - set to true to enable detailed debug output
const bool DEBUG_ENABLED = true;
const unsigned long DEBUG_INTERVAL_MS = 2000; // Print periodic status every 2 seconds
unsigned long lastDebugPrint = 0;

// ========== WIFI CONFIGURATION ==========
const char* ssid = "SwimPacer_Config";        // WiFi network name
const char* password = "";                    // No password for easy access
IPAddress local_IP(192, 168, 4, 1);         // ESP32 IP address
IPAddress gateway(192, 168, 4, 1);          // Gateway IP
IPAddress subnet(255, 255, 255, 0);         // Subnet mask

// ========== DEFAULT SETTINGS ==========
struct GlobalConfigSettings {
  float poolLength = 25.0;                   // Pool length (in poolUnits - yards or meters)
  bool poolUnitsYards = true;                // true = yards, false = meters
  float stripLengthMeters = 23.0;            // LED strip length in meters (75 feet)
  int ledsPerMeter = 30;                     // LEDs per meter
  int numLanes = 1;                          // Number of LED strips/lanes connected
  float pulseWidthFeet = 1.0;                // Width of pulse in feet
  bool delayIndicatorsEnabled = true;        // Whether to show delay countdown indicators
  int numSwimmers = 1;                       // Number of swimmers (light pulses)
  uint8_t colorRed = 0;                      // RGB color values - default to blue
  uint8_t colorGreen = 0;
  uint8_t colorBlue = 255;
  uint8_t brightness = 196;                  // Overall brightness (0-255)
  bool isRunning = false;                    // Whether the effect is active (default: stopped)
  bool laneRunning[4] = {false, false, false, false}; // Per-lane running states
  bool sameColorMode = false;                // Whether all swimmers use the same color (true) or individual colors (false)
  bool underwatersEnabled = false;           // Whether underwater indicators are enabled
  float firstUnderwaterDistanceFeet = 5.0;   // First underwater distance in feet
  float underwaterDistanceFeet = 3.0;        // Subsequent underwater distance in feet
  float surfaceDistanceFeet = 2.0;           // Surface phase distance in feet
  float lightPulseSizeFeet = 1.0;            // Size of underwater light pulse in feet
  float hideAfterSeconds = 3.0;              // Hide underwater light after surface phase (seconds)
};

GlobalConfigSettings globalConfigSettings;

// ========= Swim Set Settings ==========
struct SwimSetSettings {
  float speedMetersPerSecond = 1.693;        // Speed in meters per second (~5.56 ft/s)
  int restTimeSeconds = 5;                   // Rest time between laps in seconds
  int swimSetDistance = 50;                  // Distance for pace calculation (in pool's native units)
  int initialDelaySeconds = 10;              // Initial delay before first swimmer starts
  int swimmerIntervalSeconds = 4;            // Interval between swimmers in seconds
  int numRounds = 10;                        // Number of rounds/sets to complete
};

SwimSetSettings swimSetSettings;

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
  bool hasStarted;  // Track if this swimmer has had their first start

  // Round and rest tracking
  int currentRound;              // Current round number (1-based)
  int currentLap;             // Current length within the round (1-based)
  int lapsPerRound;           // Number of lengths needed to complete one round
  bool isResting;                // Is swimmer currently resting at wall
  unsigned long restStartTime;   // When rest period started
  float totalDistance;           // Total distance traveled in current length (in pool's native units)
  int lapDirection;           // Direction for current length: 1 = going away, -1 = returning

  // Debug tracking - to avoid repeated messages
  bool debugRestingPrinted;      // Has the "resting" state been printed already
  bool debugSwimmingPrinted;     // Has the "swimming" state been printed already

  // Underwater tracking
  bool underwaterActive;        // Is underwater light currently active
  bool inSurfacePhase;         // Has switched to surface color
  bool isFirstUnderwater;      // Is this the first underwater for this swimmer
  float distanceTraveled;      // Distance traveled in current underwater phase
  unsigned long hideTimerStart;   // When hide timer started (after surface distance completed)
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
  pinMode(2, OUTPUT); // On many ESP32 dev boards the on-board LED is on GPIO2
  Serial.begin(115200);
  Serial.print("ESP32 Swim Pacer Starting... build=");
  Serial.println(BUILD_TAG);

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

  // Blink the LED to give an easy to see message the program is working.
  digitalWrite(2, HIGH);
  delay(250);
  digitalWrite(2, LOW);
  delay(250);

  if (globalConfigSettings.isRunning) {
    updateLEDEffect();
    printPeriodicStatus(); // Print status update every few seconds
  } else {
    // If stopped, just clear the strip
    FastLED.clear();
    FastLED.show();
  }

  // Recalculate if globalConfigSettings changed
  if (needsRecalculation) {
    recalculateValues();
    needsRecalculation = false;
  }
}

void setupLEDs() {
  // Calculate total LEDs first
  totalLEDs = (int)(globalConfigSettings.stripLengthMeters * globalConfigSettings.ledsPerMeter);
  if (totalLEDs > 150) {
    totalLEDs = 150;
  }

  // Clear any existing FastLED configurations
  FastLED.clear();

  // Initialize LED arrays for each configured lane
  for (int lane = 0; lane < globalConfigSettings.numLanes; lane++) {
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
        FastLED.addLeds<LED_TYPE, 2, COLOR_ORDER>(leds[3], totalLEDs);
        break;
    }

    // Clear this lane's LEDs
    fill_solid(leds[lane], totalLEDs, CRGB::Black);
  }

  // Initialize unused lanes to nullptr
  for (int lane = globalConfigSettings.numLanes; lane < 4; lane++) {
    if (leds[lane] != nullptr) {
      delete[] leds[lane];
      leds[lane] = nullptr;
    }
  }

  FastLED.setBrightness(globalConfigSettings.brightness);
  FastLED.show();
}

void setupWiFi() {
  Serial.println("Setting up WiFi Access Point...");
  // Try to reduce peak power draw: disable Bluetooth
#if defined(ESP_BT_CONTROLLER_INIT_CONFIG) || defined(CONFIG_BT_ENABLED)
  Serial.println("DEBUG: Disabling BT controller");
  esp_bt_controller_disable();
#endif

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ssid, password);

  Serial.println("WiFi AP Started");
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());
}

void setupWebServer() {
  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  Serial.println("SPIFFS mounted successfully");

  // List SPIFFS contents for debugging
  Serial.println("SPIFFS Directory listing:");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    Serial.print("  ");
    Serial.print(file.name());
    Serial.print(" (");
    Serial.print(file.size());
    Serial.println(" bytes)");
    file = root.openNextFile();
  }
  Serial.println("End of SPIFFS listing");

  // Serve the main configuration page
  server.on("/", handleRoot);

  // Serve CSS file
  server.on("/style.css", []() {
    File file = SPIFFS.open("/style.css", "r");
    if (!file) {
      Serial.println("ERROR: Could not open /style.css from SPIFFS");
      server.send(404, "text/plain", "CSS file not found");
      return;
    }
    server.streamFile(file, "text/css");
    file.close();
  });

  // Serve JavaScript file
  server.on("/script.js", []() {
    File file = SPIFFS.open("/script.js", "r");
    if (!file) {
      Serial.println("ERROR: Could not open /script.js from SPIFFS");
      server.send(404, "text/plain", "JavaScript file not found");
      return;
    }
    server.streamFile(file, "application/javascript");
    file.close();
  });

  // Handle getting current globalConfigSettings (for dynamic updates)
  server.on("/globalConfigSettings", HTTP_GET, handleGetSettings);
  server.on("/currentLane", HTTP_GET, handleGetCurrentLane);

  // API Units and persistence notes:
  // - Speed: stored and returned as meters per second (m/s). Client should POST /setSpeed with m/s.
  //   Preference key: "speedMPS" (float)
  // - Brightness: stored as 0-255 (byte) on the device, but the UI displays 0-100%.
  //   Preference key: "brightness" (uchar)
  // - Strip length: meters (float) - preference key: "stripLengthM"
  // - LEDs per meter: integer - preference key: "ledsPerMeter"
  // - Colors: swimmer default color stored as three bytes colorRed/colorGreen/colorBlue; client uses hex (#rrggbb)


  // Expose API info & units for client-side consumption
  server.on("/apiInfo", HTTP_GET, []() {
    String info = "{";
    info += "\"speedUnits\":\"m/s\","; // client-facing units for setSpeed endpoint
    info += "\"colorFormat\":\"#rrggbb\",";
    info += "\"brightnessClient\":\"percent\","; // client should show 0-100%
    info += "\"stripLengthUnits\":\"meters\"";
    info += "}";
    server.send(200, "application/json", info);
  });

  // Handle start/stop commands
  server.on("/control", HTTP_POST, handleControl);

  // New simplified handlers for the updated interface
  server.on("/toggle", HTTP_POST, handleToggle);
  server.on("/setSpeed", HTTP_POST, handleSetSpeed);
  server.on("/setColor", HTTP_POST, handleSetColor);
  server.on("/setBrightness", HTTP_POST, handleSetBrightness);
  server.on("/setPulseWidth", HTTP_POST, handleSetPulseWidth);
  server.on("/setStripLength", HTTP_POST, handleSetStripLength);
  server.on("/setPoolLength", HTTP_POST, handleSetPoolLength);
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

  // Color and swimmer configuration endpoints
  server.on("/setColorMode", HTTP_POST, handleSetColorMode);
  server.on("/setSwimmerColor", HTTP_POST, handleSetSwimmerColor);
  server.on("/setSwimmerColors", HTTP_POST, handleSetSwimmerColors);
  server.on("/setUnderwaterSettings", HTTP_POST, handleSetUnderwaterSettings);

  // Temporary debug endpoint to reset color preferences
  server.on("/resetColors", HTTP_POST, []() {
    preferences.remove("colorRed");
    preferences.remove("colorGreen");
    preferences.remove("colorBlue");
    loadSettings(); // Reload with new defaults
    initializeSwimmers(); // Apply to swimmers
    server.send(200, "text/plain", "Color preferences reset to red default");
  });

  server.begin();
  Serial.println("Web server started");
}

void handleRoot() {
  Serial.println("handleRoot() called - serving main page");

  // Enhanced SPIFFS debugging
  if (SPIFFS.begin()) {
    // Try to open the main file
    File file = SPIFFS.open("/swim-pacer.html", "r");
    if (file && file.size() > 0) {
      server.streamFile(file, "text/html");
      file.close();
      return;
    }
    if (file) file.close();
  } else {
    Serial.println("ERROR: SPIFFS.begin() failed!");
  }

  // Fallback if SPIFFS fails
  Serial.println("SPIFFS file not found, serving fallback HTML");
  String html = "<!DOCTYPE html><html><head><title>ESP32 Swim Pacer</title></head>";
  html += "<body><h1>ESP32 Swim Pacer</h1>";
  html += "<p>SPIFFS files need to be uploaded.</p>";
  html += "<p><strong>Error:</strong> swim-pacer.html not found in SPIFFS</p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleGetSettings() {
  // Provide an expanded JSON blob the client can consume to reflect device state
  String json = "{";
  json += "\"totalLEDs\":" + String(totalLEDs) + ",";
  json += "\"stripLengthMeters\":" + String(globalConfigSettings.stripLengthMeters, 2) + ",";
  json += "\"ledsPerMeter\":" + String(globalConfigSettings.ledsPerMeter) + ",";
  json += "\"pulseWidthFeet\":" + String(globalConfigSettings.pulseWidthFeet, 2) + ",";
  // Return swim speed in feet/sec (legacy) but include a named field for clarity
  // Return speed in meters per second for consistent units with the client
  json += "\"speedMetersPerSecond\":" + String(swimSetSettings.speedMetersPerSecond, 3) + ",";
  json += "\"colorRed\":" + String(globalConfigSettings.colorRed) + ",";
  json += "\"colorGreen\":" + String(globalConfigSettings.colorGreen) + ",";
  json += "\"colorBlue\":" + String(globalConfigSettings.colorBlue) + ",";
  json += "\"brightness\":" + String(globalConfigSettings.brightness) + ","; // 0-255
  json += "\"numLanes\":" + String(globalConfigSettings.numLanes) + ",";
  json += "\"numSwimmers\":" + String(globalConfigSettings.numSwimmers) + ",";
  json += "\"poolLength\":" + String(globalConfigSettings.poolLength, 2) + ",";
  json += "\"poolUnitsYards\":" + String(globalConfigSettings.poolUnitsYards ? "true" : "false") + ",";
  json += "\"underwatersEnabled\":" + String(globalConfigSettings.underwatersEnabled ? "true" : "false") + ",";
  json += "\"delayIndicatorsEnabled\":" + String(globalConfigSettings.delayIndicatorsEnabled ? "true" : "false") + ",";
  json += "\"isRunning\":" + String(globalConfigSettings.laneRunning[currentLane] ? "true" : "false") + ",";
  json += "\"currentLane\":" + String(currentLane) + ",";

  // Include underwater colors stored in Preferences as hex strings (fallbacks match drawUnderwaterZone())
  String underwaterHex = preferences.getString("underwaterColor", "#0066CC");
  String surfaceHex = preferences.getString("surfaceColor", "#66CCFF");
  json += "\"underwaterColor\":\"" + underwaterHex + "\",";
  json += "\"surfaceColor\":\"" + surfaceHex + "\"}";

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
      globalConfigSettings.isRunning = !globalConfigSettings.isRunning;
      saveGlobalConfigSettings();
    }
  }

  server.send(200, "text/plain", "Control updated");
}

// New simplified handlers for the updated interface
void handleToggle() {
  // Toggle the current lane's running state
  globalConfigSettings.laneRunning[currentLane] = !globalConfigSettings.laneRunning[currentLane];

  // Update global running state (true if any lane is running)
  globalConfigSettings.isRunning = false;
  for (int i = 0; i < globalConfigSettings.numLanes; i++) {
    if (globalConfigSettings.laneRunning[i]) {
      globalConfigSettings.isRunning = true;
      break;
    }
  }

  saveGlobalConfigSettings();

  String status = globalConfigSettings.laneRunning[currentLane] ? "Lane Pacer Started" : "Lane Pacer Stopped";
  server.send(200, "text/plain", status);
}

void handleSetSpeed() {
  if (server.hasArg("speed")) {
    float speed = server.arg("speed").toFloat();
    // speed comes in meters per second (client and server now use m/s)
    swimSetSettings.speedMetersPerSecond = speed;
    saveSwimSetSettings();
    needsRecalculation = true;
  }
  server.send(200, "text/plain", "Speed updated");
}

void handleSetColor() {
  if (server.hasArg("color")) {
    String color = server.arg("color");

    // Check if it's a hex color (starts with #) or named color
    if (color.startsWith("#")) {
      // Use hex-to-RGB conversion for hex colors from browser
      uint8_t r, g, b;
      hexToRGB(color, r, g, b);

      globalConfigSettings.colorRed = r;
      globalConfigSettings.colorGreen = g;
      globalConfigSettings.colorBlue = b;
    } else {
      // Handle named colors for backward compatibility
      if (color == "red") {
        globalConfigSettings.colorRed = 255; globalConfigSettings.colorGreen = 0; globalConfigSettings.colorBlue = 0;
      } else if (color == "green") {
        globalConfigSettings.colorRed = 0; globalConfigSettings.colorGreen = 255; globalConfigSettings.colorBlue = 0;
      } else if (color == "blue") {
        globalConfigSettings.colorRed = 0; globalConfigSettings.colorGreen = 0; globalConfigSettings.colorBlue = 255;
      } else if (color == "yellow") {
        globalConfigSettings.colorRed = 255; globalConfigSettings.colorGreen = 255; globalConfigSettings.colorBlue = 0;
      } else if (color == "purple") {
        globalConfigSettings.colorRed = 128; globalConfigSettings.colorGreen = 0; globalConfigSettings.colorBlue = 128;
      } else if (color == "cyan") {
        globalConfigSettings.colorRed = 0; globalConfigSettings.colorGreen = 255; globalConfigSettings.colorBlue = 255;
      } else if (color == "white") {
        globalConfigSettings.colorRed = 255; globalConfigSettings.colorGreen = 255; globalConfigSettings.colorBlue = 255;
      }
    }

    saveGlobalConfigSettings();
    // Reinitialize swimmers to apply the new color
    initializeSwimmers();
  }
  server.send(200, "text/plain", "Color updated");
}

void handleSetBrightness() {
  if (server.hasArg("brightness")) {
    int brightness = server.arg("brightness").toInt();
    globalConfigSettings.brightness = brightness;
    FastLED.setBrightness(brightness);
    saveGlobalConfigSettings();
  }
  server.send(200, "text/plain", "Brightness updated");
}

void handleSetPulseWidth() {
  if (server.hasArg("pulseWidth")) {
    float pulseWidth = server.arg("pulseWidth").toFloat();
    globalConfigSettings.pulseWidthFeet = pulseWidth;
    saveGlobalConfigSettings();
    needsRecalculation = true;
  }
  server.send(200, "text/plain", "Pulse width updated");
}

void handleSetStripLength() {
  if (server.hasArg("stripLength")) {
    float stripLength = server.arg("stripLength").toFloat();
    globalConfigSettings.stripLengthMeters = stripLength;
    saveGlobalConfigSettings();
    needsRecalculation = true;
    setupLEDs();  // Reinitialize LED array with new length
  }
  server.send(200, "text/plain", "Strip length updated");
}

void handleSetPoolLength() {
  if (server.hasArg("poolLength")) {
    String poolLengthStr = server.arg("poolLength");

    // Parse the pool length and units
    // Format examples: "25", "50", "25m", "50m"
    bool isMeters = poolLengthStr.endsWith("m");
    float poolLength = poolLengthStr.toFloat(); // This will parse the number part

    globalConfigSettings.poolLength = poolLength;
    globalConfigSettings.poolUnitsYards = !isMeters; // true if yards, false if meters

    saveGlobalConfigSettings();
    needsRecalculation = true;
    initializeSwimmers(); // Reinitialize with new pool length
  }
  server.send(200, "text/plain", "Pool length updated");
}

void handleSetLedsPerMeter() {
  if (server.hasArg("ledsPerMeter")) {
    int ledsPerMeter = server.arg("ledsPerMeter").toInt();
    globalConfigSettings.ledsPerMeter = ledsPerMeter;
    saveGlobalConfigSettings();
    needsRecalculation = true;
    setupLEDs();  // Reinitialize LED array with new density
  }
  server.send(200, "text/plain", "LEDs per meter updated");
}

void handleSetNumLanes() {
  if (server.hasArg("numLanes")) {
    int numLanes = server.arg("numLanes").toInt();
    globalConfigSettings.numLanes = numLanes;
    saveGlobalConfigSettings();
    // Reinitialize LEDs when number of lanes changes
    setupLEDs();
  }
  server.send(200, "text/plain", "Number of lanes updated");
}

void handleSetCurrentLane() {
  if (server.hasArg("currentLane")) {
    int lane = server.arg("currentLane").toInt();
    if (lane >= 0 && lane < globalConfigSettings.numLanes) {
      currentLane = lane;
      // Reinitialize swimmers for the new lane if it's running
      if (globalConfigSettings.laneRunning[currentLane]) {
        initializeSwimmers();
      }
    }
  }
  server.send(200, "text/plain", "Current lane updated");
}

void handleSetRestTime() {
  if (server.hasArg("restTime")) {
    int restTime = server.arg("restTime").toInt();
    swimSetSettings.restTimeSeconds = restTime;
    saveSwimSetSettings();
  }
  server.send(200, "text/plain", "Rest time updated");
}

void handleSetPaceDistance() {
  if (server.hasArg("paceDistance")) {
    int paceDistance = server.arg("paceDistance").toInt();
    swimSetSettings.swimSetDistance = paceDistance;
    saveSwimSetSettings();
  }
  server.send(200, "text/plain", "Pace distance updated");
}

void handleSetInitialDelay() {
  if (server.hasArg("initialDelay")) {
    int initialDelay = server.arg("initialDelay").toInt();
    swimSetSettings.initialDelaySeconds = initialDelay;
    saveSwimSetSettings();
    initializeSwimmers(); // Update swimmer start times with new delay
  }
  server.send(200, "text/plain", "Initial delay updated");
}

void handleSetSwimmerInterval() {
  if (server.hasArg("swimmerInterval")) {
    int swimmerInterval = server.arg("swimmerInterval").toInt();
    swimSetSettings.swimmerIntervalSeconds = swimmerInterval;
    saveSwimSetSettings();
    initializeSwimmers(); // Update swimmer start times with new interval
  }
  server.send(200, "text/plain", "Swimmer interval updated");
}

void handleSetDelayIndicators() {
  if (server.hasArg("enabled")) {
    bool enabled = server.arg("enabled") == "true";
    globalConfigSettings.delayIndicatorsEnabled = enabled;
    saveGlobalConfigSettings();
  }
  server.send(200, "text/plain", "Delay indicators updated");
}

void handleSetNumSwimmers() {
  if (server.hasArg("numSwimmers")) {
    int numSwimmers = server.arg("numSwimmers").toInt();
    globalConfigSettings.numSwimmers = numSwimmers;
    saveGlobalConfigSettings();
  }
  server.send(200, "text/plain", "Number of swimmers updated");
}

void handleSetNumRounds() {
  if (server.hasArg("numRounds")) {
    int numRounds = server.arg("numRounds").toInt();
    swimSetSettings.numRounds = numRounds;
    saveSwimSetSettings();
  }
  server.send(200, "text/plain", "Number of rounds updated");
}

// Convert hex color string to RGB values
void hexToRGB(String hexColor, uint8_t &r, uint8_t &g, uint8_t &b) {
  // Remove # if present
  if (hexColor.startsWith("#")) {
    hexColor = hexColor.substring(1);
  }

  // Convert hex to RGB
  long number = strtol(hexColor.c_str(), NULL, 16);
  r = (number >> 16) & 0xFF;
  g = (number >> 8) & 0xFF;
  b = number & 0xFF;

  // Test what CRGB actually produces
  CRGB testColor = CRGB(r, g, b);
}

// Alternative color creation for GRB strips if FastLED auto-conversion fails
CRGB createGRBColor(uint8_t r, uint8_t g, uint8_t b) {
  // For GRB strips, we may need to manually swap R and G
  // This function allows us to test different color orders
  return CRGB(r, g, b);  // Normal RGB - FastLED should handle conversion
  // If that doesn't work, try: return CRGB(g, r, b);  // Manual GRB
}

void handleSetColorMode() {
  if (server.hasArg("colorMode")) {
    String colorMode = server.arg("colorMode");

    // Update globalConfigSettings based on color mode
    globalConfigSettings.sameColorMode = (colorMode == "same");
    saveGlobalConfigSettings();

    // Update ALL swimmer colors based on new mode (without resetting position/timing)
    for (int lane = 0; lane < 4; lane++) {
      for (int i = 0; i < 6; i++) {
        if (globalConfigSettings.sameColorMode) {
          // Same mode: all swimmers get the default color
          swimmers[lane][i].color = CRGB(globalConfigSettings.colorRed, globalConfigSettings.colorGreen, globalConfigSettings.colorBlue);
        } else {
          // Individual mode: each swimmer gets their predefined color
          swimmers[lane][i].color = swimmerColors[i];
        }
      }
    }
  }
  server.send(200, "text/plain", "Color mode updated");
}

void handleSetSwimmerColor() {
  if (server.hasArg("color")) {
    String hexColor = server.arg("color");
    uint8_t r, g, b;
    hexToRGB(hexColor, r, g, b);

    // Update default color globalConfigSettings
    globalConfigSettings.colorRed = r;
    globalConfigSettings.colorGreen = g;
    globalConfigSettings.colorBlue = b;
    saveGlobalConfigSettings();

    // If in "same color" mode, update ALL swimmers to use this color
    if (globalConfigSettings.sameColorMode) {
      CRGB newColor = CRGB(r, g, b);
      for (int lane = 0; lane < 4; lane++) {
        for (int i = 0; i < 6; i++) {
          swimmers[lane][i].color = newColor;
        }
      }
    }
    // If in "individual" mode, don't update swimmers - they keep their individual colors
  }
  server.send(200, "text/plain", "Swimmer color updated");
}

void handleSetSwimmerColors() {
  if (server.hasArg("colors")) {
    String colorsString = server.arg("colors");

    // Parse comma-separated hex colors
    int colorIndex = 0;
    int startIndex = 0;

    for (int i = 0; i <= colorsString.length() && colorIndex < 6; i++) {
      if (i == colorsString.length() || colorsString.charAt(i) == ',') {
        String hexColor = colorsString.substring(startIndex, i);
        hexColor.trim();

        if (hexColor.length() > 0) {
          uint8_t r, g, b;
          hexToRGB(hexColor, r, g, b);

          // Update this specific swimmer's color for all lanes (just the color, not position/timing)
          CRGB newColor = CRGB(r, g, b);
          for (int lane = 0; lane < 4; lane++) {
            if (colorIndex < 6) {
              swimmers[lane][colorIndex].color = newColor;
            }
          }

          colorIndex++;
        }
        startIndex = i + 1;
      }
    }
    // Note: This does NOT update default globalConfigSettings - it's for individual customization only
  }
  server.send(200, "text/plain", "Individual swimmer colors updated");
}

void handleSetUnderwaterSettings() {
  if (server.hasArg("enabled")) {
    bool enabled = server.arg("enabled") == "true";
    globalConfigSettings.underwatersEnabled = enabled;

    if (enabled) {
      // Update underwater distances
      if (server.hasArg("firstUnderwaterDistance")) {
        globalConfigSettings.firstUnderwaterDistanceFeet = server.arg("firstUnderwaterDistance").toFloat();
      }
      if (server.hasArg("underwaterDistance")) {
        globalConfigSettings.underwaterDistanceFeet = server.arg("underwaterDistance").toFloat();
      }
      if (server.hasArg("hideAfter")) {
        globalConfigSettings.hideAfterSeconds = server.arg("hideAfter").toFloat();
      }
      if (server.hasArg("lightSize")) {
        globalConfigSettings.lightPulseSizeFeet = server.arg("lightSize").toFloat();
      }

      // Update colors
      if (server.hasArg("underwaterColor") && server.hasArg("surfaceColor")) {
        String underwaterHex = server.arg("underwaterColor");
        String surfaceHex = server.arg("surfaceColor");

        // Store underwater colors
        preferences.putString("underwaterColor", underwaterHex);
        preferences.putString("surfaceColor", surfaceHex);
      }
    }

    saveGlobalConfigSettings();
  }
  server.send(200, "text/plain", "Underwater globalConfigSettings updated");
}

void saveGlobalConfigSettings() {
  preferences.putFloat("poolLength", globalConfigSettings.poolLength);
  preferences.putBool("poolUnitsYards", globalConfigSettings.poolUnitsYards);
  preferences.putFloat("stripLengthM", globalConfigSettings.stripLengthMeters);
  preferences.putInt("ledsPerMeter", globalConfigSettings.ledsPerMeter);
  preferences.putFloat("pulseWidthFeet", globalConfigSettings.pulseWidthFeet);
  preferences.putInt("numSwimmers", globalConfigSettings.numSwimmers);
  preferences.putUChar("colorRed", globalConfigSettings.colorRed);
  preferences.putUChar("colorGreen", globalConfigSettings.colorGreen);
  preferences.putUChar("colorBlue", globalConfigSettings.colorBlue);
  preferences.putUChar("brightness", globalConfigSettings.brightness);
  preferences.putBool("isRunning", globalConfigSettings.isRunning);
  preferences.putBool("sameColorMode", globalConfigSettings.sameColorMode);
  preferences.putBool("underwatersEnabled", globalConfigSettings.underwatersEnabled);
}

void saveSwimSetSettings() {
  // Persist speed in meters per second
  preferences.putFloat("speedMPS", swimSetSettings.speedMetersPerSecond);
  preferences.putInt("restTimeSeconds", swimSetSettings.restTimeSeconds);
  preferences.putInt("swimSetDistance", swimSetSettings.swimSetDistance);
  preferences.putInt("initialDelay", swimSetSettings.initialDelaySeconds);
  preferences.putInt("swimmerInterval", swimSetSettings.swimmerIntervalSeconds);
  preferences.putInt("numRounds", swimSetSettings.numRounds);
}

void saveSettings() {
  saveGlobalConfigSettings();
  saveSwimSetSettings();
}

void loadGlobalConfigSettings() {
  globalConfigSettings.poolLength = preferences.getFloat("poolLength", 25.0);  // 25 yards default
  globalConfigSettings.poolUnitsYards = preferences.getBool("poolUnitsYards", true);  // Default to yards
  globalConfigSettings.stripLengthMeters = preferences.getFloat("stripLengthM", 23.0);
  globalConfigSettings.ledsPerMeter = preferences.getInt("ledsPerMeter", 30);
  globalConfigSettings.pulseWidthFeet = preferences.getFloat("pulseWidthFeet", 1.0);
  // Keep preference fallbacks consistent with struct defaults
  globalConfigSettings.numSwimmers = preferences.getInt("numSwimmers", 1);
  globalConfigSettings.colorRed = preferences.getUChar("colorRed", 0);      // Default to blue
  globalConfigSettings.colorGreen = preferences.getUChar("colorGreen", 0);
  globalConfigSettings.colorBlue = preferences.getUChar("colorBlue", 255);
  globalConfigSettings.brightness = preferences.getUChar("brightness", 196);
  globalConfigSettings.isRunning = preferences.getBool("isRunning", false);  // Default: stopped
  globalConfigSettings.sameColorMode = preferences.getBool("sameColorMode", false); // Default: individual colors
  globalConfigSettings.underwatersEnabled = preferences.getBool("underwatersEnabled", false);
}

void loadSwimSetSettings() {
  // Load speed (meters per second) with a sensible default ~1.693 m/s (5.56 ft/s)
  swimSetSettings.speedMetersPerSecond = preferences.getFloat("speedMPS", 1.693);
  swimSetSettings.restTimeSeconds = preferences.getInt("restTimeSeconds", 5);
  swimSetSettings.swimSetDistance = preferences.getInt("swimSetDistance", 50);  // Default in yards (matches pool default)
  swimSetSettings.initialDelaySeconds = preferences.getInt("initialDelay", 10);
  swimSetSettings.swimmerIntervalSeconds = preferences.getInt("swimmerInterval", 4);
  swimSetSettings.numRounds = preferences.getInt("numRounds", 10);
}

void loadSettings() {
  loadGlobalConfigSettings();
  loadSwimSetSettings();
}

void recalculateValues() {
  // Calculate total LEDs from strip length and density
  totalLEDs = (int)(globalConfigSettings.stripLengthMeters * globalConfigSettings.ledsPerMeter);
  // Enforce a maximum to match allocation limits and hardware/FastLED constraints
  if (totalLEDs > MAX_LEDS) {
    totalLEDs = MAX_LEDS;
  }

  // Calculate LED spacing
  ledSpacingCM = 100.0 / globalConfigSettings.ledsPerMeter;

  // Calculate pulse width in LEDs
  float pulseWidthCM = globalConfigSettings.pulseWidthFeet * 30.48;
  pulseWidthLEDs = (int)(pulseWidthCM / ledSpacingCM);

  // Calculate pool to strip ratio for timing adjustments
  float poolLengthInMeters = convertPoolToStripUnits(globalConfigSettings.poolLength);
  poolToStripRatio = poolLengthInMeters / globalConfigSettings.stripLengthMeters;

  // Calculate delayMS based on swim pace and distance between LEDs
  // start with distance per LED in meters
  float ledSpacingMeters = ledSpacingCM / 100.0;
  // Now calculate time to swim 1 meter at given speed
  // swimSetSettings.speedMetersPerSecond is already in meters/second
  float timeToSwim1Meter = 1.0 / (swimSetSettings.speedMetersPerSecond);
  // Now calculate time to swim distance between 1 LED
  float timeToSwim1LED = timeToSwim1Meter / globalConfigSettings.ledsPerMeter;
  delayMS = (int)(timeToSwim1LED * 1000);

  // Ensure reasonable bounds
  delayMS = max(10, min(delayMS, 1000));

  // Reset position if it's out of bounds
  //if (currentPosition >= totalLEDs) {
  //  currentPosition = totalLEDs - 1;
  //}

  // Reinitialize swimmers when values change
  // TODO: it would be better to de-couple this
  //initializeSwimmers();
}

// Helper function to convert pool units (yards or meters) to LED strip units (meters)
float convertPoolToStripUnits(float distanceInPoolUnits) {
  if (globalConfigSettings.poolUnitsYards) {
    // Convert yards to meters for LED strip
    return distanceInPoolUnits * 0.9144;
  } else {
    // Already in meters
    return distanceInPoolUnits;
  }
}

void updateLEDEffect() {
  // Update LEDs for all active lanes
  for (int lane = 0; lane < globalConfigSettings.numLanes; lane++) {
    if (leds[lane] != nullptr) {
      // Clear this lane's LEDs
      fill_solid(leds[lane], totalLEDs, CRGB::Black);

      // Only animate if this lane is running
      if (globalConfigSettings.laneRunning[lane]) {
        // Update and draw each active swimmer for this lane FIRST (higher priority)
        for (int i = 0; i < globalConfigSettings.numSwimmers; i++) {
          updateSwimmer(i, lane);
          drawSwimmerPulse(i, lane);

          // Draw underwater zone for this swimmer if enabled
          if (globalConfigSettings.underwatersEnabled) {
            drawUnderwaterZone(i, lane);
          }
        }

        // Draw delay indicators if enabled for this lane (lower priority)
        if (globalConfigSettings.delayIndicatorsEnabled) {
          drawDelayIndicators(lane);
        }
      }
    }
  }

  // Update FastLED
  FastLED.show();
}

void drawDelayIndicators(int laneIndex) {
  unsigned long currentTime = millis();

  // Max delay distance in pool's native units
  // Convert 5 feet to pool's native units
  float maxDelayDistanceFeet = 5.0;
  float maxDelayDistance;
  if (globalConfigSettings.poolUnitsYards) {
    maxDelayDistance = maxDelayDistanceFeet / 3.0; // feet to yards
  } else {
    maxDelayDistance = maxDelayDistanceFeet * 0.3048; // feet to meters
  }

  // Convert to strip units and calculate LEDs
  float maxDelayDistanceInStripUnits = convertPoolToStripUnits(maxDelayDistance);
  int maxDelayLEDs = (int)(maxDelayDistanceInStripUnits * globalConfigSettings.ledsPerMeter);

  // Find the next swimmer who is resting and has the shortest time until they start
  int nextSwimmerIndex = -1;
  float shortestRemainingDelay = 999999.0;

  for (int i = 0; i < globalConfigSettings.numSwimmers; i++) {
    Swimmer* swimmer = &swimmers[laneIndex][i];

    // Only consider swimmers who are resting
    if (!swimmer->isResting) {
      continue;
    }

    // Calculate when this swimmer should start
    unsigned long targetRestDuration;
    if (swimmer->currentRound == 1) {
      // First round: use initial delay + swimmer interval
      targetRestDuration =
        swimSetSettings.initialDelaySeconds * 1000 +
        (i * swimSetSettings.swimmerIntervalSeconds * 1000);
    } else {
      // Subsequent rounds: use rest time + swimmer interval
      targetRestDuration =
        swimSetSettings.restTimeSeconds * 1000 +
        (i * swimSetSettings.swimmerIntervalSeconds * 1000);
    }

    unsigned long swimmerStartTime = swimmer->restStartTime + targetRestDuration;

    // Check if this swimmer hasn't started yet
    if (currentTime < swimmerStartTime) {
      float remainingDelaySeconds = (float)(swimmerStartTime - currentTime) / 1000.0;

      // Track the swimmer with the shortest remaining delay
      if (remainingDelaySeconds < shortestRemainingDelay) {
        shortestRemainingDelay = remainingDelaySeconds;
        nextSwimmerIndex = i;
      }
    }
  }

  // Only draw delay indicator for the next swimmer (if any)
  if (nextSwimmerIndex >= 0 && shortestRemainingDelay <= 5.0) {
    Swimmer* nextSwimmer = &swimmers[laneIndex][nextSwimmerIndex];

    // Delay distance at 1 foot per second
    float delayDistanceFeet = shortestRemainingDelay;

    // Convert feet to pool's native units
    float delayDistanceInPoolUnits;
    if (globalConfigSettings.poolUnitsYards) {
      delayDistanceInPoolUnits = delayDistanceFeet / 3.0; // feet to yards
    } else {
      delayDistanceInPoolUnits = delayDistanceFeet * 0.3048; // feet to meters
    }

    // Convert to strip units and then to LEDs
    float delayDistanceInStripUnits = convertPoolToStripUnits(delayDistanceInPoolUnits);
    int delayLEDs = (int)(delayDistanceInStripUnits * globalConfigSettings.ledsPerMeter);

    // Ensure at least 1 LED if there's time remaining
    if (delayLEDs < 1) delayLEDs = 1;
    if (delayLEDs > maxDelayLEDs) delayLEDs = maxDelayLEDs;

    // Draw delay indicator
    CRGB swimmerColor = nextSwimmer->color;
    for (int ledIndex = 0; ledIndex < delayLEDs && ledIndex < totalLEDs; ledIndex++) {
      // Only draw delay indicator if LED is currently black (swimmers get priority)
      if (leds[laneIndex][ledIndex] == CRGB::Black) {
        // Create a dimmed version of the swimmer's color for the delay indicator
        CRGB delayColor = swimmerColor;
        delayColor.nscale8(128); // 50% brightness for delay indicator
        leds[laneIndex][ledIndex] = delayColor;
      }
    }
  }
}

void initializeSwimmers() {
  // Calculate how many pool lengths equal one round
  // Both swimSetDistance and poolLength are in the same units (pool's native units)
  int lapsPerRound = (int)ceil((float)swimSetSettings.swimSetDistance / globalConfigSettings.poolLength);

  if (DEBUG_ENABLED) {
    Serial.println("\n========== INITIALIZING SWIMMERS ==========");
    Serial.print("Pool length: ");
    Serial.print(globalConfigSettings.poolLength);
    Serial.println(globalConfigSettings.poolUnitsYards ? " yards" : " meters");
    Serial.print("Swim set distance: ");
    Serial.print(swimSetSettings.swimSetDistance);
    Serial.println(globalConfigSettings.poolUnitsYards ? " yards" : " meters");
    Serial.print("Calculated laps per round: ");
    Serial.println(lapsPerRound);
    Serial.print("Delay indicators: ");
    Serial.println(globalConfigSettings.delayIndicatorsEnabled ? "ENABLED" : "DISABLED");
    Serial.print("Underwaters: ");
    Serial.println(globalConfigSettings.underwatersEnabled ? "ENABLED" : "DISABLED");
    Serial.println("===========================================\n");
  }

  unsigned long currentTime = millis();

  for (int lane = 0; lane < 4; lane++) {
    for (int i = 0; i < 6; i++) {
      swimmers[lane][i].position = 0;
      swimmers[lane][i].direction = 1;
      swimmers[lane][i].hasStarted = false;  // Initialize as not started
      swimmers[lane][i].lastUpdate = currentTime;

      // Initialize round and rest tracking
      swimmers[lane][i].currentRound = 1;
      swimmers[lane][i].currentLap = 1;
      swimmers[lane][i].lapsPerRound = lapsPerRound;
      swimmers[lane][i].isResting = true;
      swimmers[lane][i].restStartTime = currentTime;
      swimmers[lane][i].totalDistance = 0.0;
      swimmers[lane][i].lapDirection = 1;  // Start going away from start wall

      // Initialize debug tracking
      swimmers[lane][i].debugRestingPrinted = false;
      swimmers[lane][i].debugSwimmingPrinted = false;

      // Initialize underwater tracking
      swimmers[lane][i].underwaterActive = false;
      swimmers[lane][i].inSurfacePhase = false;
      swimmers[lane][i].isFirstUnderwater = true;  // First underwater will use first distance
      swimmers[lane][i].distanceTraveled = 0.0;
      swimmers[lane][i].hideTimerStart = 0;

      // Set colors based on mode
      updateSwimmerColors();
    }
  }
}

void updateSwimmerColors() {
  // Update swimmer colors without resetting position/timing
  for (int lane = 0; lane < 4; lane++) {
    for (int i = 0; i < 6; i++) {
      // Use web interface color for all swimmers in same color mode, predefined colors for individual mode
      if (globalConfigSettings.sameColorMode) {
        swimmers[lane][i].color = CRGB(globalConfigSettings.colorRed, globalConfigSettings.colorGreen, globalConfigSettings.colorBlue);
      } else {
        swimmers[lane][i].color = swimmerColors[i];
      }
    }
  }
}

void updateSwimmer(int swimmerIndex, int laneIndex) {
  // If the lane is not running just exit
  if (!globalConfigSettings.laneRunning[laneIndex]) {
    return;
  }
  Swimmer* swimmer = &swimmers[laneIndex][swimmerIndex];
  unsigned long currentTime = millis();

  // Check if swimmer is resting
  if (swimmer->isResting) {
    // How much time have they been resting
    unsigned long restElapsed = currentTime - swimmer->restStartTime;

    // What is our target rest duration (in milliseconds)?
    unsigned long targetRestDuration =
      (swimmer->currentRound == 1) ?
        // This is the first round, so we use the initial delay plus swimmer interval
        swimSetSettings.initialDelaySeconds * 1000 +
        ((swimmerIndex) * swimSetSettings.swimmerIntervalSeconds * 1000) :
        // This is not the first round, so we use the rest period
        swimSetSettings.restTimeSeconds * 1000;

    // Print debug info only once when entering rest state
    if (DEBUG_ENABLED && !swimmer->debugRestingPrinted) {
      Serial.println("================================================");
      Serial.print("Lane ");
      Serial.print(laneIndex);
      Serial.print(", Swimmer ");
      Serial.print(swimmerIndex);
      Serial.print(" - Round ");
      Serial.print(swimmer->currentRound);
      Serial.println(" - RESTING at wall");
      Serial.print("  Target rest duration: ");
      Serial.print(targetRestDuration / 1000.0);
      Serial.println(" seconds");
      swimmer->debugRestingPrinted = true;
      swimmer->debugSwimmingPrinted = false; // Reset for next swim phase
    }

    if (restElapsed >= targetRestDuration) {
      // Rest period complete, resume swimming
      if (DEBUG_ENABLED) {
        Serial.println("------------------------------------------------");
        Serial.print("Lane ");
        Serial.print(laneIndex);
        Serial.print(", Swimmer ");
        Serial.print(swimmerIndex);
        Serial.println(" - REST COMPLETE, starting to swim");
        Serial.print("  Rest duration: ");
        Serial.print(restElapsed / 1000.0);
        Serial.print(" sec (target: ");
        Serial.print(targetRestDuration / 1000.0);
        Serial.println(" sec)");
      }

      swimmer->isResting = false;
      swimmer->restStartTime = 0;
      // Set last updated time to account for any overshoot
      swimmer->lastUpdate = currentTime - (restElapsed - targetRestDuration);

      // Start underwater phase when leaving the wall
      if (globalConfigSettings.underwatersEnabled) {
        swimmer->underwaterActive = true;
        swimmer->inSurfacePhase = false;
        swimmer->distanceTraveled = 0.0;
        swimmer->hideTimerStart = 0;
      }
    } else {
      // Still resting, don't move
      return;
    }
  }

  // Swimmer is actively swimming
  if (currentTime - swimmer->lastUpdate >= delayMS) {
    // Print debug info only once when entering swimming state
    if (DEBUG_ENABLED && !swimmer->debugSwimmingPrinted) {
      Serial.println("------------------------------------------------");
      Serial.print("Lane ");
      Serial.print(laneIndex);
      Serial.print(", Swimmer ");
      Serial.print(swimmerIndex);
      Serial.print(" - SWIMMING - Round ");
      Serial.print(swimmer->currentRound);
      Serial.print(", Lap ");
      Serial.print(swimmer->currentLap);
      Serial.print("/");
      Serial.print(swimmer->lapsPerRound);
      Serial.print(", Distance: 0.0");
      Serial.println(globalConfigSettings.poolUnitsYards ? " yards" : " meters");
      swimmer->debugSwimmingPrinted = true;
      swimmer->debugRestingPrinted = false; // Reset for next rest phase
    }

    // Calculate time elapsed since last position update
    float deltaSeconds = (currentTime - swimmer->lastUpdate) / 1000.0;
    swimmer->lastUpdate = currentTime;

    // Calculate distance traveled this frame in pool's native units
    // Convert speed (meters/sec) to pool's native units
    float speedInPoolUnits;
    if (globalConfigSettings.poolUnitsYards) {
      // Convert meters/sec to yards/sec (1 yard = 0.9144 m)
      speedInPoolUnits = swimSetSettings.speedMetersPerSecond / 0.9144;
    } else {
      // Already meters/sec
      speedInPoolUnits = swimSetSettings.speedMetersPerSecond;
    }
    float distanceTraveled = speedInPoolUnits * deltaSeconds;

    // Update total distance
    swimmer->totalDistance += distanceTraveled;

    // Check if swimmer has completed one pool length based on actual distance
    // Calculate 1-based lap number (lap 1 = 0 to poolLength, lap 2 = poolLength to 2*poolLength, etc.)
    int calculatedLap = (int)floor(swimmer->totalDistance / globalConfigSettings.poolLength) + 1;

    // distance for current length/lap
    float distanceForCurrentLength = swimmer->totalDistance - ((calculatedLap - 1) * globalConfigSettings.poolLength);

    if (calculatedLap > swimmer->currentLap) {
      float overshoot = swimmer->totalDistance - (calculatedLap * globalConfigSettings.poolLength);

      if (DEBUG_ENABLED) {
        Serial.println("  *** WALL TURN ***");
        Serial.print("    Completed lap ");
        Serial.print(swimmer->currentLap);
        Serial.print(", overshoot: ");
        Serial.print(overshoot);
        Serial.println(globalConfigSettings.poolUnitsYards ? " yards" : " meters");
      }

      // Change direction
      swimmer->lapDirection *= -1;

      // Increment length counter
      swimmer->currentLap++;

      // Check if round is complete (all lengths done)
      if (swimmer->currentLap > swimmer->lapsPerRound) {
        if (DEBUG_ENABLED) {
          Serial.println("  *** ROUND COMPLETE ***");
          Serial.print("    Completed round ");
          Serial.println(swimmer->currentRound);
        }

        // Round complete - reset length counter and check if we should rest
        swimmer->currentLap = 1;
        swimmer->totalDistance = 0.0;

        // Only rest if we haven't completed all rounds yet
        if (swimmer->currentRound < swimSetSettings.numRounds) {
          swimmer->currentRound++;
          swimmer->isResting = true;
          swimmer->restStartTime = currentTime;
        } else {
          if (DEBUG_ENABLED) {
            Serial.println("  *** ALL ROUNDS COMPLETE ***");
            Serial.print("    Swimmer ");
            Serial.print(swimmerIndex);
            Serial.println(" finished!");
          }
        }

        // Place LED at the wall based on direction
        if (swimmer->lapDirection == 1) {
          swimmer->position = 0;
        } else {
          // Convert pool length to LED position
          float poolLengthInStripUnits = convertPoolToStripUnits(globalConfigSettings.poolLength);
          float ledPosition = floor((poolLengthInStripUnits * globalConfigSettings.ledsPerMeter)-1);
          swimmer->position = ledPosition;
        }
      } else {
        // Still in the same round
        // Start underwater phase at wall
        if (globalConfigSettings.underwatersEnabled) {
          swimmer->underwaterActive = true;
          swimmer->inSurfacePhase = false;
          swimmer->distanceTraveled = 0.0;
          swimmer->hideTimerStart = 0;
        }

        // Place LED position where we overshot the wall
        float overshootInStripUnits = convertPoolToStripUnits(overshoot);
        int overshootLEDs = (int)(overshootInStripUnits * globalConfigSettings.ledsPerMeter);
        if (swimmer->lapDirection == 1) {
          swimmer->position = overshootLEDs;
        } else {
          float poolLengthInStripUnits = convertPoolToStripUnits(globalConfigSettings.poolLength);
          float ledPosition = floor((poolLengthInStripUnits * globalConfigSettings.ledsPerMeter)-1) - overshootLEDs;
          swimmer->position = ledPosition;
        }
      }
    } else {
      // Normal movement within the pool length
      // distanceForCurrentLength is the distance traveled in the current lap
      // Convert to LED strip units
      float distanceInStripUnits = convertPoolToStripUnits(distanceForCurrentLength);
      float poolLengthInStripUnits = convertPoolToStripUnits(globalConfigSettings.poolLength);

      // Calculate LED position based on direction
      if (swimmer->lapDirection == 1) {
        // Swimming away from start: position = 0 + distance
        swimmer->position = (int)(distanceInStripUnits * globalConfigSettings.ledsPerMeter);
      } else {
        // Swimming back to start: position = poolLength - distance
        float positionInStripUnits = poolLengthInStripUnits - distanceInStripUnits;
        swimmer->position = (int)(positionInStripUnits * globalConfigSettings.ledsPerMeter);
      }
    }

    // Mark swimmer as started once they begin moving
    if (!swimmer->hasStarted) {
      swimmer->hasStarted = true;
      if (DEBUG_ENABLED) {
        Serial.print("  Swimmer ");
        Serial.print(swimmerIndex);
        Serial.println(" has STARTED");
      }
    }

    // Track distance for underwater calculations
    if (swimmer->underwaterActive) {
      // Which underwater values to use based on if it's the first lap or not
      // First lap of each round uses firstUnderwaterDistance (off the wall)
      // Second lap uses regular underwaterDistance
      float targetUnderwaterDistanceFeet = (swimmer->currentLap == 1) ?
        globalConfigSettings.firstUnderwaterDistanceFeet :
        globalConfigSettings.underwaterDistanceFeet;

      // Convert feet to pool's native units
      float targetUnderwaterDistance;
      if (globalConfigSettings.poolUnitsYards) {
        targetUnderwaterDistance = targetUnderwaterDistanceFeet / 3.0; // feet to yards
      } else {
        targetUnderwaterDistance = targetUnderwaterDistanceFeet * 0.3048; // feet to meters
      }

      // Are we within underwater distance from a wall?
      if (distanceForCurrentLength < targetUnderwaterDistance) {
        swimmer->underwaterActive = true;
        swimmer->inSurfacePhase = false;
        swimmer->hideTimerStart = 0; // Reset hide timer
      } else {
        // Is this the first time we're exceeding the underwater distance?
        if (!swimmer->inSurfacePhase) {
          swimmer->inSurfacePhase = true;
          swimmer->hideTimerStart = currentTime;
        } else {
          // Check if hide timer has elapsed to turn off underwater light
          float hideAfterSeconds = globalConfigSettings.hideAfterSeconds;
          if ((currentTime - swimmer->hideTimerStart) >= (unsigned long)(hideAfterSeconds * 1000)) {
            swimmer->underwaterActive = false;
            swimmer->inSurfacePhase = false;
            swimmer->hideTimerStart = 0;
          }
        }
      }
    }
  }
}

// Print periodic status for all swimmers (called from loop)
void printPeriodicStatus() {
  if (!DEBUG_ENABLED) return;

  unsigned long currentTime = millis();
  if (currentTime - lastDebugPrint < DEBUG_INTERVAL_MS) return;

  lastDebugPrint = currentTime;

  Serial.println("\n========== STATUS UPDATE ==========");
  for (int lane = 0; lane < globalConfigSettings.numLanes; lane++) {
    if (globalConfigSettings.laneRunning[lane]) {
      Serial.print("Lane ");
      Serial.print(lane);
      Serial.println(":");
      for (int i = 0; i < globalConfigSettings.numSwimmers; i++) {
        Swimmer* s = &swimmers[lane][i];
        Serial.print("  Swimmer ");
        Serial.print(i);
        Serial.print(": ");
        if (s->isResting) {
          Serial.print("RESTING (");
          Serial.print((currentTime - s->restStartTime) / 1000.0);
          Serial.print("s)");
        } else {
          Serial.print("Swimming - R");
          Serial.print(s->currentRound);
          Serial.print("/");
          Serial.print(swimSetSettings.numRounds);
          Serial.print(" L");
          Serial.print(s->currentLap);
          Serial.print("/");
          Serial.print(s->lapsPerRound);

          // Calculate distance in current lap (1-based lap calculation)
          int currentLapNum = (int)floor(s->totalDistance / globalConfigSettings.poolLength) + 1;
          float distanceInCurrentLap = s->totalDistance - ((currentLapNum - 1) * globalConfigSettings.poolLength);

          Serial.print(" Dist:");
          Serial.print(distanceInCurrentLap, 1);
          Serial.print(globalConfigSettings.poolUnitsYards ? "yd" : "m");
          Serial.print(" Pos:");
          Serial.print(s->position);
        }
        Serial.println();
      }
    }
  }
  Serial.println("===================================\n");
}

void drawSwimmerPulse(int swimmerIndex, int laneIndex) {
  Swimmer* swimmer = &swimmers[laneIndex][swimmerIndex];

  // Don't draw if swimmer is resting
  if (swimmer->isResting) {
    return;
  }

  // Only draw if swimmer should be active (start time has passed)
  if (millis() < swimmer->lastUpdate) {
    return; // Swimmer hasn't started yet
  }

  int centerPos = swimmer->position;
  int halfWidth = pulseWidthLEDs / 2;
  CRGB pulseColor = swimmer->color;

  for (int i = 0; i < pulseWidthLEDs; i++) {
    int ledIndex = centerPos - halfWidth + i;

    if (ledIndex >= 0 && ledIndex < totalLEDs) {
      int distanceFromCenter = abs(i - halfWidth);
      float brightnessFactor = 1.0 - (float)distanceFromCenter / (float)halfWidth;
      brightnessFactor = max(0.0f, brightnessFactor);

      CRGB color = pulseColor;
      color.nscale8((uint8_t)(brightnessFactor * 255));

      // Only set color if LED is currently black (first wins priority)
      if (leds[laneIndex][ledIndex] == CRGB::Black) {
        leds[laneIndex][ledIndex] = color;
      }
    }
  }
}

void drawUnderwaterZone(int swimmerIndex, int laneIndex) {
  Swimmer* swimmer = &swimmers[laneIndex][swimmerIndex];
  unsigned long currentTime = millis();

  // Only draw if swimmer should be active (start time has passed)
  if (swimmer->isResting) {
    return; // Swimmer hasn't started yet
  }

  // Check if underwater is active for this swimmer
  if (!swimmer->underwaterActive) {
    return; // No underwater light needed
  }

  // Check if we should hide underwater light (after hide timer expires)
  if (swimmer->hideTimerStart > 0) {
    float hideAfterSeconds = globalConfigSettings.hideAfterSeconds;
    unsigned long hideTimer = currentTime - swimmer->hideTimerStart;

    if (hideTimer >= (hideAfterSeconds * 1000)) {
      // Hide underwater light
      swimmer->underwaterActive = false;
      return;
    }
  }

  // Get underwater globalConfigSettings from preferences
  String underwaterHex = preferences.getString("underwaterColor", "#0066CC"); // Default blue
  String surfaceHex = preferences.getString("surfaceColor", "#66CCFF");       // Default light blue

  // Convert hex colors to RGB
  uint8_t underwaterR, underwaterG, underwaterB;
  uint8_t surfaceR, surfaceG, surfaceB;
  hexToRGB(underwaterHex, underwaterR, underwaterG, underwaterB);
  hexToRGB(surfaceHex, surfaceR, surfaceG, surfaceB);

  CRGB underwaterColor = CRGB(underwaterR, underwaterG, underwaterB);
  CRGB surfaceColor = CRGB(surfaceR, surfaceG, surfaceB);

  // Determine color based on phase
  CRGB currentUnderwaterColor = swimmer->inSurfacePhase ?
    surfaceColor : underwaterColor;

  // Calculate underwater light size
  float lightPulseSizeFeet = globalConfigSettings.lightPulseSizeFeet;

  // Convert feet to pool's native units
  float lightSizeInPoolUnits;
  if (globalConfigSettings.poolUnitsYards) {
    lightSizeInPoolUnits = lightPulseSizeFeet / 3.0; // feet to yards
  } else {
    lightSizeInPoolUnits = lightPulseSizeFeet * 0.3048; // feet to meters
  }

  // Convert to strip units and then to LEDs
  float lightSizeInStripUnits = convertPoolToStripUnits(lightSizeInPoolUnits);
  int lightSizeLEDs = (int)(lightSizeInStripUnits * globalConfigSettings.ledsPerMeter);
  if (lightSizeLEDs < 1) lightSizeLEDs = 1; // Minimum 1 LED

  // Position underwater light exactly at the swimmer's position (moves with swimmer)
  int swimmerPos = swimmer->position;

  // Draw the underwater light pulse centered on swimmer
  for (int i = swimmerPos; i < swimmerPos + lightSizeLEDs; i++) {
    if (i >= 0 && i < totalLEDs) {
      // Only set color if LED is currently black (first wins priority)
      if (leds[laneIndex][i] == CRGB::Black) {
        leds[laneIndex][i] = currentUnderwaterColor;
      }
    }
  }
}