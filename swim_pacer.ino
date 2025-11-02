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
#include <SPIFFS.h>

// ========== HARDWARE CONFIGURATION ==========
#define LED_TYPE        WS2812B     // LED strip type
#define COLOR_ORDER     GRB         // Color order (may need adjustment)

// GPIO pins for multiple LED strips (lanes)
// Lane 1: GPIO 18, Lane 2: GPIO 19, Lane 3: GPIO 21, Lane 4: GPIO 2
const int LED_PINS[4] = {18, 19, 21, 2}; // GPIO pins for lanes 1-4

// ========== WIFI CONFIGURATION ==========
const char* ssid = "SwimPacer_Config";        // WiFi network name
const char* password = "";                    // No password for easy access
IPAddress local_IP(192, 168, 4, 1);         // ESP32 IP address
IPAddress gateway(192, 168, 4, 1);          // Gateway IP
IPAddress subnet(255, 255, 255, 0);         // Subnet mask

// ========== DEFAULT SETTINGS ==========
struct Settings {
  float poolLengthMeters = 22.86;            // Pool length in meters (25 yards)
  float stripLengthMeters = 23.0;            // LED strip length in meters (75 feet)
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
  uint8_t colorRed = 255;                    // RGB color values - default to red
  uint8_t colorGreen = 0;
  uint8_t colorBlue = 0;
  uint8_t brightness = 196;                  // Overall brightness (0-255)
  bool isRunning = false;                    // Whether the effect is active (default: stopped)
  bool laneRunning[4] = {false, false, false, false}; // Per-lane running states
  bool underwatersEnabled = false;           // Whether underwater indicators are enabled
  float firstUnderwaterDistanceFeet = 5.0;   // First underwater distance in feet
  float underwaterDistanceFeet = 3.0;        // Subsequent underwater distance in feet
  float surfaceDistanceFeet = 2.0;           // Surface phase distance in feet
  float lightPulseSizeFeet = 1.0;            // Size of underwater light pulse in feet
  float hideAfterSeconds = 3.0;              // Hide underwater light after surface phase (seconds)
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
  bool hasStarted;  // Track if this swimmer has had their first start

  // Underwater tracking
  bool underwaterActive;        // Is underwater light currently active
  unsigned long underwaterStartTime;  // When underwater phase started
  bool inSurfacePhase;         // Has switched to surface color
  bool isFirstUnderwater;      // Is this the first underwater for this swimmer
  float distanceTraveled;      // Distance traveled in current underwater phase
  float surfaceDistanceTraveled; // Distance traveled in surface phase
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
        FastLED.addLeds<LED_TYPE, 2, COLOR_ORDER>(leds[3], totalLEDs);
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
    Serial.println("CSS request received - serving /style.css");
    File file = SPIFFS.open("/style.css", "r");
    if (!file) {
      Serial.println("ERROR: Could not open /style.css from SPIFFS");
      server.send(404, "text/plain", "CSS file not found");
      return;
    }
    Serial.println("Successfully opened /style.css, streaming to client");
    server.streamFile(file, "text/css");
    file.close();
  });

  // Serve JavaScript file
  server.on("/script.js", []() {
    Serial.println("JavaScript request received - serving /script.js");
    File file = SPIFFS.open("/script.js", "r");
    if (!file) {
      Serial.println("ERROR: Could not open /script.js from SPIFFS");
      server.send(404, "text/plain", "JavaScript file not found");
      return;
    }
    Serial.println("Successfully opened /script.js, streaming to client");
    server.streamFile(file, "application/javascript");
    file.close();
  });

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
    Serial.println("=== SPIFFS DEBUG INFO ===");
    Serial.printf("SPIFFS total bytes: %u\n", SPIFFS.totalBytes());
    Serial.printf("SPIFFS used bytes: %u\n", SPIFFS.usedBytes());

    // List all files with full details
    File root = SPIFFS.open("/");
    if (root) {
      File file = root.openNextFile();
      int count = 0;
      while (file) {
        count++;
        Serial.printf("File %d: '%s' - %u bytes\n", count, file.name(), file.size());

        // Try to read first few bytes
        if (file.size() > 0) {
          file.seek(0);
          char buffer[50];
          int bytesRead = file.readBytes(buffer, 49);
          buffer[bytesRead] = '\0';
          Serial.printf("  First %d bytes: %s\n", bytesRead, buffer);
        }

        file = root.openNextFile();
      }
      root.close();
      Serial.printf("Total files found: %d\n", count);
    }
    Serial.println("=== END SPIFFS DEBUG ===");

    // Try to open the main file
    File file = SPIFFS.open("/swim-pacer.html", "r");
    if (file && file.size() > 0) {
      Serial.print("SUCCESS: Serving from SPIFFS - file size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
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

void handleUpdate() {
  // Update settings from web form
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
  json += "\"totalLEDs\":" + String(totalLEDs) + ",";
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

    // Check if it's a hex color (starts with #) or named color
    if (color.startsWith("#")) {
      // Use hex-to-RGB conversion for hex colors from browser
      uint8_t r, g, b;
      hexToRGB(color, r, g, b);

      settings.colorRed = r;
      settings.colorGreen = g;
      settings.colorBlue = b;
    } else {
      // Handle named colors for backward compatibility
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
    }

    saveSettings();
    // Reinitialize swimmers to apply the new color
    initializeSwimmers();
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
    initializeSwimmers(); // Update swimmer start times with new delay
  }
  server.send(200, "text/plain", "Initial delay updated");
}

void handleSetSwimmerInterval() {
  if (server.hasArg("swimmerInterval")) {
    int swimmerInterval = server.arg("swimmerInterval").toInt();
    settings.swimmerIntervalSeconds = swimmerInterval;
    saveSettings();
    initializeSwimmers(); // Update swimmer start times with new interval
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
    // Store color mode for future use
    preferences.putString("colorMode", colorMode);
    Serial.println("Color mode updated to: " + colorMode);
  }
  server.send(200, "text/plain", "Color mode updated");
}

void handleSetSwimmerColor() {
  if (server.hasArg("color")) {
    String hexColor = server.arg("color");
    uint8_t r, g, b;
    hexToRGB(hexColor, r, g, b);

    // Update default color settings for "same color" mode
    settings.colorRed = r;
    settings.colorGreen = g;
    settings.colorBlue = b;

    saveSettings();
    initializeSwimmers(); // Apply new color
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

          // Update swimmer color for current lane
          for (int lane = 0; lane < 4; lane++) {
            if (colorIndex < 6) {
              swimmers[lane][colorIndex].color = CRGB(r, g, b);
            }
          }

          // If this is the first swimmer, also update default settings
          if (colorIndex == 0) {
            settings.colorRed = r;
            settings.colorGreen = g;
            settings.colorBlue = b;
          }

          Serial.println("Swimmer " + String(colorIndex + 1) + " color updated to: " + hexColor);
          colorIndex++;
        }
        startIndex = i + 1;
      }
    }

    saveSettings();
  }
  server.send(200, "text/plain", "Individual swimmer colors updated");
}

void handleSetUnderwaterSettings() {
  if (server.hasArg("enabled")) {
    bool enabled = server.arg("enabled") == "true";
    settings.underwatersEnabled = enabled;

    if (enabled) {
      // Update underwater distances
      if (server.hasArg("firstUnderwaterDistance")) {
        settings.firstUnderwaterDistanceFeet = server.arg("firstUnderwaterDistance").toFloat();
      }
      if (server.hasArg("underwaterDistance")) {
        settings.underwaterDistanceFeet = server.arg("underwaterDistance").toFloat();
      }
      if (server.hasArg("surfaceDistance")) {
        settings.surfaceDistanceFeet = server.arg("surfaceDistance").toFloat();
      }
      if (server.hasArg("hideAfter")) {
        settings.hideAfterSeconds = server.arg("hideAfter").toFloat();
      }
      if (server.hasArg("lightSize")) {
        settings.lightPulseSizeFeet = server.arg("lightSize").toFloat();
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

    saveSettings();
  }
  server.send(200, "text/plain", "Underwater settings updated");
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
  preferences.putBool("underwatersEnabled", settings.underwatersEnabled);
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
  settings.colorRed = preferences.getUChar("colorRed", 255);    // Default to red
  settings.colorGreen = preferences.getUChar("colorGreen", 0);
  settings.colorBlue = preferences.getUChar("colorBlue", 0);
  settings.brightness = preferences.getUChar("brightness", 100);
  settings.isRunning = preferences.getBool("isRunning", false);  // Default: stopped
  settings.underwatersEnabled = preferences.getBool("underwatersEnabled", false);
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
        // Update and draw each active swimmer for this lane FIRST (higher priority)
        for (int i = 0; i < settings.numSwimmers; i++) {
          updateSwimmer(i, currentTime, lane);
          drawSwimmerPulse(i, lane);

          // Draw underwater zone for this swimmer if enabled
          if (settings.underwatersEnabled) {
            drawUnderwaterZone(i, currentTime, lane);
          }
        }

        // Draw delay indicators if enabled for this lane (lower priority)
        if (settings.delayIndicatorsEnabled) {
          drawDelayIndicators(currentTime, lane);
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
        // Only draw delay indicator if LED is currently black (swimmers get priority)
        if (leds[laneIndex][ledIndex] == CRGB::Black) {
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
      swimmers[lane][i].hasStarted = false;  // Initialize as not started
      swimmers[lane][i].lastUpdate = millis() + (settings.initialDelaySeconds * 1000) + (i * settings.swimmerIntervalSeconds * 1000); // Initial delay + staggered start times

      // Initialize underwater tracking
      swimmers[lane][i].underwaterActive = false;
      swimmers[lane][i].underwaterStartTime = 0;
      swimmers[lane][i].inSurfacePhase = false;
      swimmers[lane][i].isFirstUnderwater = true;  // First underwater will use first distance
      swimmers[lane][i].distanceTraveled = 0.0;
      swimmers[lane][i].surfaceDistanceTraveled = 0.0;
      swimmers[lane][i].hideTimerStart = 0;

      // Use web interface color for first swimmer, predefined colors for others
      if (i == 0) {
        swimmers[lane][i].color = CRGB(settings.colorRed, settings.colorGreen, settings.colorBlue);
      } else {
        swimmers[lane][i].color = swimmerColors[i];
      }
    }
  }
}

void updateSwimmer(int swimmerIndex, unsigned long currentTime, int laneIndex) {
  // Check if swimmer should be active (start time has passed)
  if (currentTime >= swimmers[laneIndex][swimmerIndex].lastUpdate) {
    // For movement timing, check if enough time has passed since last movement
    static unsigned long lastMovement[4][6] = {0}; // Track last movement time separately

    if (currentTime - lastMovement[laneIndex][swimmerIndex] >= delayMS) {
      lastMovement[laneIndex][swimmerIndex] = currentTime;

      // Mark swimmer as started once they begin moving
      if (!swimmers[laneIndex][swimmerIndex].hasStarted) {
        swimmers[laneIndex][swimmerIndex].hasStarted = true;
      }

      // Move to next position
      swimmers[laneIndex][swimmerIndex].position += swimmers[laneIndex][swimmerIndex].direction;

      // Track distance for underwater calculations
      if (swimmers[laneIndex][swimmerIndex].underwaterActive) {
        // Calculate distance moved (in feet)
        const float feetToMeters = 0.3048;
        float ledSpacing = 1.0 / settings.ledsPerMeter; // meters per LED
        float distanceMovedFeet = ledSpacing / feetToMeters; // convert to feet

        if (swimmers[laneIndex][swimmerIndex].inSurfacePhase) {
          // In surface phase - track surface distance
          swimmers[laneIndex][swimmerIndex].surfaceDistanceTraveled += distanceMovedFeet;

          // Check if surface distance is complete to start hide timer
          if (swimmers[laneIndex][swimmerIndex].hideTimerStart == 0 &&
              swimmers[laneIndex][swimmerIndex].surfaceDistanceTraveled >= settings.surfaceDistanceFeet) {
            swimmers[laneIndex][swimmerIndex].hideTimerStart = currentTime;
          }
        } else {
          // In underwater phase - track underwater distance
          swimmers[laneIndex][swimmerIndex].distanceTraveled += distanceMovedFeet;

          // Check if underwater distance is complete to switch to surface phase
          float targetDistance = swimmers[laneIndex][swimmerIndex].isFirstUnderwater ?
                                  settings.firstUnderwaterDistanceFeet : settings.underwaterDistanceFeet;

          if (swimmers[laneIndex][swimmerIndex].distanceTraveled >= targetDistance) {
            swimmers[laneIndex][swimmerIndex].inSurfacePhase = true;
            swimmers[laneIndex][swimmerIndex].surfaceDistanceTraveled = 0.0;
          }
        }
      }

      // Check for bouncing at ends
      if (swimmers[laneIndex][swimmerIndex].position >= totalLEDs - 1) {
        swimmers[laneIndex][swimmerIndex].direction = -1;
        swimmers[laneIndex][swimmerIndex].position = totalLEDs - 1;

        // Start underwater phase at wall
        if (settings.underwatersEnabled) {
          swimmers[laneIndex][swimmerIndex].underwaterActive = true;
          swimmers[laneIndex][swimmerIndex].underwaterStartTime = currentTime;
          swimmers[laneIndex][swimmerIndex].inSurfacePhase = false;
          swimmers[laneIndex][swimmerIndex].distanceTraveled = 0.0;
          swimmers[laneIndex][swimmerIndex].surfaceDistanceTraveled = 0.0;
          swimmers[laneIndex][swimmerIndex].hideTimerStart = 0;
          // Note: isFirstUnderwater stays true for the first underwater, then gets set to false
        }
      } else if (swimmers[laneIndex][swimmerIndex].position <= 0) {
        swimmers[laneIndex][swimmerIndex].direction = 1;
        swimmers[laneIndex][swimmerIndex].position = 0;

        // Start underwater phase at wall (determine if first or subsequent)
        if (settings.underwatersEnabled) {
          swimmers[laneIndex][swimmerIndex].underwaterActive = true;
          swimmers[laneIndex][swimmerIndex].underwaterStartTime = currentTime;
          swimmers[laneIndex][swimmerIndex].inSurfacePhase = false;
          swimmers[laneIndex][swimmerIndex].distanceTraveled = 0.0;
          swimmers[laneIndex][swimmerIndex].surfaceDistanceTraveled = 0.0;
          swimmers[laneIndex][swimmerIndex].hideTimerStart = 0;
          swimmers[laneIndex][swimmerIndex].isFirstUnderwater = false; // After first wall hit, subsequent underwaters use regular distance
        }
      }
    }
  }
}

void drawSwimmerPulse(int swimmerIndex, int laneIndex) {
  // Only draw if swimmer should be active (start time has passed)
  if (millis() < swimmers[laneIndex][swimmerIndex].lastUpdate) {
    return; // Swimmer hasn't started yet
  }

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

      // Only set color if LED is currently black (first wins priority)
      if (leds[laneIndex][ledIndex] == CRGB::Black) {
        leds[laneIndex][ledIndex] = color;
      }
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

void drawUnderwaterZone(int swimmerIndex, unsigned long currentTime, int laneIndex) {
  // Only draw if swimmer should be active (start time has passed)
  if (currentTime < swimmers[laneIndex][swimmerIndex].lastUpdate) {
    return; // Swimmer hasn't started yet
  }

  // Check if underwater is active for this swimmer
  if (!swimmers[laneIndex][swimmerIndex].underwaterActive) {
    return; // No underwater light needed
  }

  // Check if we should hide underwater light (after hide timer expires)
  if (swimmers[laneIndex][swimmerIndex].hideTimerStart > 0) {
    float hideAfterSeconds = settings.hideAfterSeconds;
    unsigned long hideTimer = currentTime - swimmers[laneIndex][swimmerIndex].hideTimerStart;

    if (hideTimer >= (hideAfterSeconds * 1000)) {
      // Hide underwater light
      swimmers[laneIndex][swimmerIndex].underwaterActive = false;
      return;
    }
  }

  // Get underwater settings from preferences
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
  CRGB currentUnderwaterColor = swimmers[laneIndex][swimmerIndex].inSurfacePhase ?
    surfaceColor : underwaterColor;

  // Calculate underwater light size
  float lightPulseSizeFeet = settings.lightPulseSizeFeet;
  const float feetToMeters = 0.3048;
  int lightSizeLEDs = (int)(lightPulseSizeFeet * feetToMeters * settings.ledsPerMeter);
  if (lightSizeLEDs < 1) lightSizeLEDs = 1; // Minimum 1 LED

  // Position underwater light exactly at the swimmer's position (moves with swimmer)
  int swimmerPos = swimmers[laneIndex][swimmerIndex].position;

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