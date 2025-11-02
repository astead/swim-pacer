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