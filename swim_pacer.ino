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
  int numSwimmersPerLane[4] = {3,3,3,3};    // Per-lane swimmer counts (preferred)
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
  float hideAfterSeconds = 1.0;              // Hide underwater light after surface phase (seconds)
};

GlobalConfigSettings globalConfigSettings;

// ========= Swim Set Settings ==========
// This is used only to store default values
struct SwimSetSettings {
  float speedMetersPerSecond = 1.693;        // Speed in meters per second (~5.56 ft/s)
  int swimTimeSeconds = 30;                  // Swim time for laps in seconds
  int restTimeSeconds = 5;                   // Rest time between laps in seconds
  int swimSetDistance = 50;                  // Distance for pace calculation (in pool's native units)
  int swimmerIntervalSeconds = 4;            // Interval between swimmers in seconds
  int numRounds = 10;                        // Number of rounds/sets to complete
};

SwimSetSettings swimSetSettings;

// ----- Swim set queue types and storage -----
enum SwimSetType { SWIMSET_TYPE = 0, REST_TYPE = 1, LOOP_TYPE = 2, MOVE_TYPE = 3 };

struct SwimSet {
  uint8_t rounds;          // number of rounds
  uint16_t swimDistance;   // distance in pool's native units (no units attached)
  uint16_t swimSeconds;    // seconds to swim the 'length'
  uint16_t restSeconds;    // rest time between rounds
  uint16_t swimmerInterval; // interval between swimmers (seconds)
  uint8_t type;            // SwimSetType
  uint8_t repeat;          // repeat metadata for LOOP_TYPE
  uint32_t id;             // device-assigned canonical id (0 = not assigned)
  unsigned long long clientTempId; // client-supplied temporary id for reconciliation (0 = none)
};

// Simple fixed-size in-memory per-lane queues
const int SWIMSET_QUEUE_MAX = 12;
const int MAX_LANES_SUPPORTED = 4;
SwimSet swimSetQueue[MAX_LANES_SUPPORTED][SWIMSET_QUEUE_MAX];
int swimSetQueueHead[MAX_LANES_SUPPORTED] = {0,0,0,0};
int swimSetQueueTail[MAX_LANES_SUPPORTED] = {0,0,0,0};
int swimSetQueueCount[MAX_LANES_SUPPORTED] = {0,0,0,0};

// Device-assigned swimset ID counter (monotonic)
uint32_t nextSwimSetId = 0;

// Forward declarations for functions used below
float convertPoolToMeters(float distanceInPoolUnits);
void recalculateValues();
void initializeSwimmers();
void saveSwimSetSettings();
void saveGlobalConfigSettings();


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

// (applySwimSetToSettings moved below after Swimmer is defined)

// ----- Swim set queue storage (moved below globals) -----
// (previously declared earlier; moved so globals are grouped together)
// Note: queue storage is sized by SWIMSET_QUEUE_MAX and accessed via enqueue/dequeue functions

// The queue arrays are declared earlier but we define the helpers here for readability.
bool enqueueSwimSet(const SwimSet &s) {
  // For backward compatibility, if s.type encodes lane in an extended field it
  // is ignored. Require callers to supply swim-set payloads per-lane via
  // the HTTP endpoint which passes the lane separately. Here use currentLane.
  int lane = currentLane;
  if (DEBUG_ENABLED) {
    Serial.print("enqueueSwimSet(): received set for lane:");
    Serial.println(lane);
    Serial.print("  rounds=");
    Serial.println(s.rounds);
    Serial.print("  swimDistance=");
    Serial.println(s.swimDistance);
    Serial.print("  swimTime=");
    Serial.println(s.swimSeconds);
    Serial.print("  restTime=");
    Serial.println(s.restSeconds);
    Serial.print("  swimmerInterval=");
    Serial.println(s.swimmerInterval);
  }
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) return false;
  if (swimSetQueueCount[lane] >= SWIMSET_QUEUE_MAX) {
    if (DEBUG_ENABLED) Serial.println("  queue full, rejecting set");
    return false;
  }
  swimSetQueue[lane][swimSetQueueTail[lane]] = s;
  swimSetQueueTail[lane] = (swimSetQueueTail[lane] + 1) % SWIMSET_QUEUE_MAX;
  swimSetQueueCount[lane]++;
  if (DEBUG_ENABLED) {
    Serial.print("  Queue Count After Insert="); Serial.println(swimSetQueueCount[lane]);
  }
  return true;
}

bool dequeueSwimSet(SwimSet &out) {
  int lane = currentLane;
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) return false;
  if (swimSetQueueCount[lane] == 0) return false;
  out = swimSetQueue[lane][swimSetQueueHead[lane]];
  swimSetQueueHead[lane] = (swimSetQueueHead[lane] + 1) % SWIMSET_QUEUE_MAX;
  swimSetQueueCount[lane]--;
  return true;
}

// Peek at the next swim set in queue without removing it
bool peekSwimSet(SwimSet &out, int lane) {
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) return false;
  if (swimSetQueueCount[lane] == 0) return false;
  out = swimSetQueue[lane][swimSetQueueHead[lane]];
  return true;
}

// Get a swim set by index in the queue (0 = first set in queue)
bool getSwimSetByIndex(SwimSet &out, int lane, int index) {
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) return false;
  if (index < 0 || index >= swimSetQueueCount[lane]) return false;

  // Calculate the actual queue position accounting for circular buffer
  int actualIndex = (swimSetQueueHead[lane] + index) % SWIMSET_QUEUE_MAX;
  out = swimSetQueue[lane][actualIndex];
  return true;
}

// Minimal JSON extractor helpers (no ArduinoJson dependency)
float extractJsonFloat(const String &json, const char *key, float fallback) {
  String keyStr = String("\"") + key + String("\"");
  int idx = json.indexOf(keyStr);
  if (idx < 0) return fallback;
  int colon = json.indexOf(':', idx);
  if (colon < 0) return fallback;
  int start = colon + 1;
  while (start < json.length() && (json.charAt(start) == ' ' || json.charAt(start) == '"')) start++;
  int end = start;
  while (end < json.length() && (isDigit(json.charAt(end)) || json.charAt(end) == '.' || json.charAt(end) == '-')) end++;
  String numStr = json.substring(start, end);
  if (numStr.length() == 0) return fallback;
  return numStr.toFloat();
}

long extractJsonLong(const String &json, const char *key, long fallback) {
  return (long)extractJsonFloat(json, key, (float)fallback);
}

String extractJsonString(const String &json, const char *key, const char *fallback) {
  String keyStr = String("\"") + key + String("\"");
  int idx = json.indexOf(keyStr);
  if (idx < 0) return String(fallback);
  int colon = json.indexOf(':', idx);
  if (colon < 0) return String(fallback);
  int start = json.indexOf('"', colon);
  if (start < 0) return String(fallback);
  start++;
  int end = json.indexOf('"', start);
  if (end < 0) return String(fallback);
  return json.substring(start, end);
}


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
  float distanceTraveled;      // Distance traveled in current underwater phase
  unsigned long hideTimerStart;   // When hide timer started (after surface distance completed)
  bool finished;               // Has swimmer completed all rounds

  // Per-swimmer cached swim set settings (for independent progression through queue)
  int cachedNumRounds;         // Number of rounds for this swimmer's active swim set
  int cachedLapsPerRound;      // Number of laps per round (calculated from distance)
  float cachedSpeedMPS;        // Speed in meters per second
  int cachedRestSeconds;       // Rest duration in seconds
  uint32_t activeSwimSetId;    // ID of the active swim set this swimmer is executing
  int queueIndex;              // Index in the lane's queue that this swimmer is currently executing (0-based)
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

// Apply the swim set to current swimSetSettings and start the set on current lane
void applySwimSetToSettings(const SwimSet &s) {
  // Compute speed (m/s) from swimDistance (in pool units) and swimSeconds
  // convert swimDistance to meters using current pool units
  float swimDistanceMeters = convertPoolToMeters((float)s.swimDistance);
  if (s.swimSeconds <= 0.0f) return; // avoid divide by zero
  float speedMPS = swimDistanceMeters / s.swimSeconds;

  swimSetSettings.speedMetersPerSecond = speedMPS;
  swimSetSettings.swimSetDistance = s.swimDistance;
  swimSetSettings.numRounds = s.rounds;
  swimSetSettings.restTimeSeconds = s.restSeconds;
  swimSetSettings.swimmerIntervalSeconds = s.swimmerInterval;

  needsRecalculation = true;
  recalculateValues();

  // Debug: print swim set being applied
  if (DEBUG_ENABLED) {
    Serial.println("applySwimSetToSettings(): applying set:");
    Serial.print("  currentLane="); Serial.println(currentLane);
    Serial.print("  rounds="); Serial.println(s.rounds);
    Serial.print("  swimDistance="); Serial.println(s.swimDistance);
    Serial.print("  swimSeconds="); Serial.println(s.swimSeconds);
    Serial.print("  restSeconds="); Serial.println(s.restSeconds);
    Serial.print("  swimmerIntervalSeconds="); Serial.println(s.swimmerInterval);
    Serial.print("  computed speedMPS="); Serial.println(speedMPS, 4);
  }

  // Defensive check: ensure swimmer interval is sane (non-zero). If it's zero
  // the computed target rest durations will be zero which results in immediate
  // starts for all swimmers. Reset to a safe default for runtime behavior but
  // avoid persisting an invalid zero value until we've validated all fields.
  if (swimSetSettings.swimmerIntervalSeconds <= 0) {
    if (DEBUG_ENABLED) {
      Serial.print("Warning: swimmerIntervalSeconds <= 0 (value=");
      Serial.print(swimSetSettings.swimmerIntervalSeconds);
      Serial.println(") - resetting to default 4 seconds (will be persisted)");
    }
    swimSetSettings.swimmerIntervalSeconds = 4;
  }

  // Persist swim set settings (now that swimmerIntervalSeconds is validated)
  saveSwimSetSettings();

  if (DEBUG_ENABLED) {
    Serial.print("  swimmerIntervalSeconds="); Serial.println(swimSetSettings.swimmerIntervalSeconds);
  }

  // Initialize swimmers for the current lane to start fresh
  for (int i = 0; i < 6; i++) {
    //swimmers[currentLane][i].position = 0; This should continue from previous position
    //swimmers[currentLane][i].direction = 1; This should continue from previous direction
    swimmers[currentLane][i].hasStarted = false;  // Initialize as not started
    swimmers[currentLane][i].lastUpdate = millis();

    // Initialize round and rest tracking
    swimmers[currentLane][i].currentRound = 1;
    swimmers[currentLane][i].currentLap = 1;
    swimmers[currentLane][i].lapsPerRound = ceil(swimSetSettings.swimSetDistance / globalConfigSettings.poolLength);
    swimmers[currentLane][i].isResting = true;
    swimmers[currentLane][i].finished = false;
    // Start rest timer now so swimmerInterval-based stagger will be honored
    swimmers[currentLane][i].restStartTime = millis();
    // Total distance should start at 0 for the new set
    swimmers[currentLane][i].totalDistance = 0.0;
    //swimmers[currentLane][i].lapDirection = 1;  This should continue from previous direction

    // Initialize underwater tracking
    swimmers[currentLane][i].underwaterActive = globalConfigSettings.underwatersEnabled;
    swimmers[currentLane][i].inSurfacePhase = false;
    swimmers[currentLane][i].distanceTraveled = 0.0;
    swimmers[currentLane][i].hideTimerStart = 0;

    // Initialize debug tracking
    swimmers[currentLane][i].debugRestingPrinted = false;
    swimmers[currentLane][i].debugSwimmingPrinted = false;

    // Cache swim set settings for this swimmer (enables independent progression through queue)
    swimmers[currentLane][i].cachedNumRounds = s.rounds;
    swimmers[currentLane][i].cachedLapsPerRound = ceil(s.swimDistance / globalConfigSettings.poolLength);
    swimmers[currentLane][i].cachedSpeedMPS = speedMPS;
    swimmers[currentLane][i].cachedRestSeconds = s.restSeconds;
    swimmers[currentLane][i].activeSwimSetId = s.id;
    swimmers[currentLane][i].queueIndex = 0;  // Starting with first set in queue
  }

  // Debug: dump swimmer state for current lane after initialization (first 6 swimmers)
  if (DEBUG_ENABLED) {
    Serial.println("  swimmer state after applying the swim set:");
    for (int i = 0; i < 6; i++) {
      Swimmer &sw = swimmers[currentLane][i];
      Serial.print("  swimmer="); Serial.print(i);
      Serial.print(" pos="); Serial.print(sw.position);
      Serial.print(" dir="); Serial.print(sw.direction);
      Serial.print(" hasStarted="); Serial.print(sw.hasStarted ? 1 : 0);
      Serial.print(" currentRound="); Serial.print(sw.currentRound);
      Serial.print(" currentLap="); Serial.print(sw.currentLap);
      Serial.print(" lapsPerRound="); Serial.print(sw.lapsPerRound);
      Serial.print(" isResting="); Serial.print(sw.isResting ? 1 : 0);
      Serial.print(" restStartTime="); Serial.print(sw.restStartTime);
      // Compute the target rest duration and absolute start time so it's easy to verify stagger
      unsigned long targetRestDuration;
      if (sw.currentRound == 1) {
        // First round: staggered start based on swimmer interval
        targetRestDuration = (unsigned long)((i + 1) * (unsigned long)swimSetSettings.swimmerIntervalSeconds * 1000);
      } else {
        targetRestDuration = (unsigned long)(swimSetSettings.restTimeSeconds * 1000) + (unsigned long)(i * swimSetSettings.swimmerIntervalSeconds * 1000);
      }
      unsigned long swimmerStartTime = sw.restStartTime + targetRestDuration;
      Serial.print(" targetRestSecs="); Serial.print(swimSetSettings.restTimeSeconds);
      Serial.print(" startAtMillis="); Serial.print(swimmerStartTime);
      Serial.print(" totalDistance="); Serial.print(sw.totalDistance, 3);
      Serial.print(" lapDirection="); Serial.println(sw.lapDirection);
    }
  }

  // Start the current lane pacer
  globalConfigSettings.laneRunning[currentLane] = true;
  // Update global isRunning if any lane is running
  globalConfigSettings.isRunning = false;
  for (int i = 0; i < globalConfigSettings.numLanes; i++) {
    if (globalConfigSettings.laneRunning[i]) { globalConfigSettings.isRunning = true; break; }
  }
  saveGlobalConfigSettings();
}


void setup() {
  pinMode(2, OUTPUT); // On many ESP32 dev boards the on-board LED is on GPIO2
  Serial.begin(115200);
  Serial.print("ESP32 Swim Pacer Starting... build=");
  Serial.println(BUILD_TAG);

  // Initialize preferences (flash storage)
  preferences.begin("swim_pacer", false);
  loadSettings();

  // Initialize LED strip
    setupLEDs(); // Reinitialize LEDs when number of lanes changes

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

  digitalWrite(2, LOW);
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

  // handlers for the swimmer interface
  server.on("/toggle", HTTP_POST, handleToggle);
  server.on("/setBrightness", HTTP_POST, handleSetBrightness);
  server.on("/setPulseWidth", HTTP_POST, handleSetPulseWidth);
  server.on("/setStripLength", HTTP_POST, handleSetStripLength);
  server.on("/setPoolLength", HTTP_POST, handleSetPoolLength);
  server.on("/setLedsPerMeter", HTTP_POST, handleSetLedsPerMeter);
  server.on("/setNumLanes", HTTP_POST, handleSetNumLanes);
  server.on("/setSwimDistance", HTTP_POST, handleSetSwimDistance);
  server.on("/setSwimTime", HTTP_POST, handleSetSwimTime);
  server.on("/setRestTime", HTTP_POST, handleSetRestTime);
  server.on("/setSwimmerInterval", HTTP_POST, handleSetSwimmerInterval);
  server.on("/setDelayIndicators", HTTP_POST, handleSetDelayIndicators);
  server.on("/setNumSwimmers", HTTP_POST, handleSetNumSwimmers);
  server.on("/setNumRounds", HTTP_POST, handleSetNumRounds);

  // Color and swimmer configuration endpoints
  server.on("/setColorMode", HTTP_POST, handleSetColorMode);
  server.on("/setSwimmerColor", HTTP_POST, handleSetSwimmerColor);
  server.on("/setSwimmerColors", HTTP_POST, handleSetSwimmerColors);
  server.on("/setUnderwaterSettings", HTTP_POST, handleSetUnderwaterSettings);

  // Swim set queue endpoints
  server.on("/enqueueSwimSet", HTTP_POST, []() {
    Serial.println("/enqueueSwimSet ENTER");
    // Read raw body
    String body = server.arg("plain");
    SwimSet s;
    s.rounds = (uint8_t)extractJsonLong(body, "rounds", swimSetSettings.numRounds);
    s.swimDistance = (uint16_t)extractJsonLong(body, "swimDistance", swimSetSettings.swimSetDistance);
    s.swimSeconds = (uint16_t)extractJsonLong(body, "swimSeconds", swimSetSettings.swimTimeSeconds);
    s.restSeconds = (uint16_t)extractJsonLong(body, "restSeconds", swimSetSettings.restTimeSeconds);
    s.swimmerInterval = (uint16_t)extractJsonLong(body, "swimmerInterval", swimSetSettings.swimmerIntervalSeconds);
    s.type = (uint8_t)extractJsonLong(body, "type", SWIMSET_TYPE);
    s.repeat = (uint8_t)extractJsonLong(body, "repeat", 0);
    s.id = 0;
    s.clientTempId = 0ULL;

    // Optional clientTempId support: client may send a numeric clientTempId or a hex string
    String clientTempStr = extractJsonString(body, "clientTempId", "");
    if (clientTempStr.length() > 0) {
      // Try parse as hex if it starts with 0x or contains letters, else parse as decimal
      unsigned long long parsed = 0ULL;
      bool parsedOk = false;
      if (clientTempStr.startsWith("0x") || clientTempStr.indexOf('a') >= 0 || clientTempStr.indexOf('b') >= 0 || clientTempStr.indexOf('c') >= 0 || clientTempStr.indexOf('d') >= 0 || clientTempStr.indexOf('e') >= 0 || clientTempStr.indexOf('f') >= 0 || clientTempStr.indexOf('A') >= 0) {
        // Hex parsing
        parsed = (unsigned long long)strtoull(clientTempStr.c_str(), NULL, 16);
        parsedOk = true;
      } else {
        // Decimal parsing
        parsed = (unsigned long long)strtoull(clientTempStr.c_str(), NULL, 10);
        parsedOk = true;
      }
      if (parsedOk) s.clientTempId = parsed;
    }

    int lane = (int)extractJsonLong(body, "lane", currentLane);
    if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
      server.send(400, "text/plain", "Invalid lane");
      return;
    }
    int oldLane = currentLane;
    currentLane = lane;
    // Assign a device canonical id on enqueue (non-zero)
    if (s.id == 0) {
      nextSwimSetId++;
      if (nextSwimSetId == 0) nextSwimSetId = 1;
      s.id = nextSwimSetId;
    }
    bool ok = enqueueSwimSet(s);
    Serial.println(" Enqueue result: " + String(ok ? "OK" : "FAILED"));
    Serial.println("  Enqueued swim set: ");
    Serial.println("   rounds=" + String(s.rounds));
    Serial.println("   swimDistance=" + String(s.swimDistance));
    Serial.println("   swimSeconds=" + String(s.swimSeconds));
    Serial.println("   restSeconds=" + String(s.restSeconds));
    Serial.println("   swimmerInterval=" + String(s.swimmerInterval));
    Serial.println("   type=" + String(s.type));
    Serial.println("   repeat=" + String(s.repeat));
    Serial.println("   id=" + String(s.id));
    Serial.println("   clientTempId=" + String((unsigned long long)s.clientTempId));
    currentLane = oldLane;
    if (ok) {
      int qCount = swimSetQueueCount[lane];
      String json = "{";
      json += "\"ok\":true,";
      json += "\"id\":" + String(s.id) + ",";
      json += "\"clientTempId\":\"" + String((unsigned long long)s.clientTempId) + "\",";
      json += "\"queueCount\":" + String(qCount);
      json += "}";
      server.send(200, "application/json", json);
    } else {
      server.send(507, "text/plain", "Queue full");
    }
  });

  // Update an existing swim set in-place by device id or clientTempId
  // Body may include matchId (device id) or matchClientTempId (string) to locate
  // the queued entry. The fields length, swimSeconds, rounds, restSeconds, type, repeat
  // will be used to replace the entry. Optionally provide clientTempId to update the
  // stored clientTempId for future reconciliation.
  server.on("/updateSwimSet", HTTP_POST, []() {
    Serial.println("/updateSwimSet ENTER");
    String body = server.arg("plain");
    Serial.println(" Raw body:");
    Serial.println(body);

    // Parse required fields from JSON body (no form-encoded fallback)
    SwimSet s;
    s.rounds = (uint8_t)extractJsonLong(body, "rounds", swimSetSettings.numRounds);
    s.swimDistance = (uint16_t)extractJsonLong(body, "swimDistance", swimSetSettings.swimSetDistance);
    s.swimSeconds = (uint16_t)extractJsonLong(body, "swimSeconds", swimSetSettings.swimTimeSeconds);
    s.restSeconds = (uint16_t)extractJsonLong(body, "restSeconds", swimSetSettings.restTimeSeconds);
    s.swimmerInterval = (uint16_t)extractJsonLong(body, "swimmerInterval", swimSetSettings.swimmerIntervalSeconds);
    s.type = (uint8_t)extractJsonLong(body, "type", 0);
    s.repeat = (uint8_t)extractJsonLong(body, "repeat", 0);

    long matchId = extractJsonLong(body, "matchId", 0);
    String matchClientTemp = extractJsonString(body, "matchClientTempId", "");
    String newClientTemp = extractJsonString(body, "clientTempId", "");
    int lane = (int)extractJsonLong(body, "lane", currentLane);

    Serial.print("  lane="); Serial.println(lane);
    Serial.print("  matchId="); Serial.println(matchId);
    Serial.print("  matchClientTempId="); Serial.println(matchClientTemp);

    if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid lane\"}");
      return;
    }

    Serial.println("  received payload to update set: ");
    Serial.print(" rounds="); Serial.println(s.rounds);
    Serial.print(" swimDistance="); Serial.println(s.swimDistance);
    Serial.print(" swimSeconds="); Serial.println(s.swimSeconds);
    Serial.print(" restSeconds="); Serial.println(s.restSeconds);
    Serial.print(" swimmerInterval="); Serial.println(s.swimmerInterval);
    Serial.print(" type="); Serial.println(s.type);
    Serial.print(" repeat="); Serial.println(s.repeat);
    Serial.print(" matchId="); Serial.println(matchId);
    Serial.print(" matchClientTempId="); Serial.println(matchClientTemp);
    Serial.print(" newClientTempId="); Serial.println(newClientTemp);

    Serial.println(" Searching queue for matching entry...");
     // Find matching entry in the lane queue and update fields
    bool found = false;
    for (int i = 0; i < swimSetQueueCount[lane]; i++) {
      int idx = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
      SwimSet &entry = swimSetQueue[lane][idx];

      // Match by device id first
      if (matchId != 0 && entry.id == (uint32_t)matchId) {
        entry.rounds = s.rounds;
        entry.swimDistance = s.swimDistance;
        entry.swimSeconds = s.swimSeconds;
        entry.restSeconds = s.restSeconds;
        entry.swimmerInterval = s.swimmerInterval;
        entry.type = s.type;
        entry.repeat = s.repeat;
        if (newClientTemp.length() > 0) {
          unsigned long long parsedNew = (unsigned long long)strtoull(newClientTemp.c_str(), NULL, 16);
          if (parsedNew != 0) entry.clientTempId = parsedNew;
        }
        found = true;
      }
      // Otherwise try matching by clientTempId (hex/decimal)
      else if (matchClientTemp.length() > 0) {
        unsigned long long parsed = (unsigned long long)strtoull(matchClientTemp.c_str(), NULL, 16);
        if (parsed != 0 && entry.clientTempId == parsed) {
          entry.rounds = s.rounds;
          entry.swimDistance = s.swimDistance;
          entry.swimSeconds = s.swimSeconds;
          entry.restSeconds = s.restSeconds;
          entry.swimmerInterval = s.swimmerInterval;
          entry.type = s.type;
          entry.repeat = s.repeat;
          if (newClientTemp.length() > 0) {
            unsigned long long parsedNew = (unsigned long long)strtoull(newClientTemp.c_str(), NULL, 16);
            if (parsedNew != 0) entry.clientTempId = parsedNew;
          }
          found = true;
        }
      }

      if (found) {
        // Respond with updated canonical info + current lane queue for immediate reconciliation
        String json = "{";
        json += "\"ok\":true,";
        json += "\"id\":" + String(entry.id) + ",";
        json += "\"clientTempId\":\"" + String((unsigned long long)entry.clientTempId) + "\",";
        json += "\"queue\":";
        // build queue JSON for this lane
        {
          String q = "[";
          for (int qi = 0; qi < swimSetQueueCount[lane]; qi++) {
            int qidx = (swimSetQueueHead[lane] + qi) % SWIMSET_QUEUE_MAX;
            SwimSet &qs = swimSetQueue[lane][qidx];
            if (qi) q += ",";
            q += "{";
            q += String("\"id\":") + String(qs.id) + ",";
            q += String("\"clientTempId\":\"") + String((unsigned long long)qs.clientTempId) + String("\",");
            q += String("\"rounds\":") + String(qs.rounds) + ",";
            q += String("\"swimDistance\":") + String(qs.swimDistance) + ",";
            q += String("\"swimSeconds\":") + String(qs.swimSeconds) + ",";
            q += String("\"restSeconds\":") + String(qs.restSeconds) + ",";
            q += String("\"swimmerInterval\":") + String(qs.swimmerInterval) + ",";
            q += String("\"type\":") + String(qs.type) + ",";
            q += String("\"repeat\":") + String(qs.repeat);
            q += "}";
          }
          q += "]";
          json += q;
        }
        json += "}";
        server.send(200, "application/json", json);
        return;
      }
    } // end for

    Serial.println(" No matching entry found in queue");
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
  });

  // Delete a swim set by device id or clientTempId for a lane.
  // Accepts form-encoded or JSON: matchId=<n> or matchClientTempId=<hex>, and lane=<n>
  server.on("/deleteSwimSet", HTTP_POST, []() {
    Serial.println("/deleteSwimSet ENTER");
    String body = server.arg("plain");
    Serial.println(" Received raw body: " + body);

    // Expect application/json body
    if (body.length() == 0) {
      server.send(400, "application/json", "{\"error\":\"empty body\"}");
      return;
    }
    long matchId = extractJsonLong(body, "matchId", 0);
    String matchClientTemp = extractJsonString(body, "matchClientTempId", "");
    int lane = (int)extractJsonLong(body, "lane", currentLane);

    // Validate lane...
    if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
      Serial.println(" Invalid lane");
      server.send(400, "application/json", "{\"ok\":false, \"error\":\"Invalid lane\"}");
      return;
    }
    Serial.println("  lane=" + String(lane));
    Serial.print("  matchId="); Serial.println(matchId);
    Serial.print("  matchClientTempId="); Serial.println(matchClientTemp);

    // Build vector of existing entries
    std::vector<SwimSet> existing;
    for (int i = 0; i < swimSetQueueCount[lane]; i++) {
      int idx = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
      existing.push_back(swimSetQueue[lane][idx]);
    }

    // Try match by numeric id
    bool removed = false;
    if (matchId != 0) {
      for (size_t i = 0; i < existing.size(); i++) {
        if (existing[i].id == (uint32_t)matchId) {
          Serial.println(" deleteSwimSet: matched by id=" + String(existing[i].id));
          existing.erase(existing.begin() + i);
          removed = true;
          break;
        }
      }
    }

    // Try match by clientTempId (hex or decimal)
    if (!removed && matchClientTemp.length() > 0) {
      unsigned long long parsed = (unsigned long long)strtoull(matchClientTemp.c_str(), NULL, 16);
      if (parsed != 0) {
        for (size_t i = 0; i < existing.size(); i++) {
          if (existing[i].clientTempId == parsed) {
            Serial.println(" deleteSwimSet: matched by clientTempId=" + String((unsigned long long)existing[i].clientTempId));
            existing.erase(existing.begin() + i);
            removed = true;
            break;
          }
        }
      }
    }

    // Write remaining entries back into circular buffer
    swimSetQueueHead[lane] = 0;
    swimSetQueueTail[lane] = 0;
    swimSetQueueCount[lane] = 0;
    for (size_t i = 0; i < existing.size() && i < SWIMSET_QUEUE_MAX; i++) {
      swimSetQueue[lane][swimSetQueueTail[lane]] = existing[i];
      swimSetQueueTail[lane] = (swimSetQueueTail[lane] + 1) % SWIMSET_QUEUE_MAX;
      swimSetQueueCount[lane]++;
    }

    // Perform removal logic (match by id or clientTempId) and respond with JSON
    if (removed) server.send(200, "application/json", "{\"ok\":true}");
    else server.send(404, "application/json", "{\"ok\":false, \"error\":\"not found\"}");
  });


  // Reset swimmers for a given lane to starting wall (position=0, direction=1)
  server.on("/resetLane", HTTP_POST, []() {
    String body = server.arg("plain");
    int lane = (int)extractJsonLong(body, "lane", currentLane);
    if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
      server.send(400, "text/plain", "Invalid lane");
      return;
    }

    // Reset swimmers for this lane
    for (int i = 0; i < 6; i++) {
      swimmers[lane][i].position = 0;
      swimmers[lane][i].direction = 1; // point towards far wall
      swimmers[lane][i].hasStarted = false;
      swimmers[lane][i].currentRound = 1;
      swimmers[lane][i].currentLap = 1;
      swimmers[lane][i].isResting = true;
      swimmers[lane][i].restStartTime = millis();
      swimmers[lane][i].totalDistance = 0.0;
      swimmers[lane][i].underwaterActive = globalConfigSettings.underwatersEnabled;
      swimmers[lane][i].inSurfacePhase = false;
      swimmers[lane][i].distanceTraveled = 0.0;
      swimmers[lane][i].hideTimerStart = 0;
    }

    // Update lane running state: do not alter laneRunning flag here - caller controls starting/stopping
    // No 'runningSets' or 'runningSettings' arrays exist in this firmware build; nothing to clear.

    server.send(200, "text/plain", "OK");
  });

  server.on("/runSwimSetNow", HTTP_POST, []() {
    Serial.println("/runSwimSetNow ENTER");
    String body = server.arg("plain");
    SwimSet s;
    s.rounds = (uint8_t)extractJsonLong(body, "rounds", swimSetSettings.numRounds);
    s.swimDistance = (uint16_t)extractJsonLong(body, "swimDistance", swimSetSettings.swimSetDistance);
    s.swimSeconds = (uint16_t)extractJsonLong(body, "swimSeconds", swimSetSettings.swimTimeSeconds);
    s.restSeconds = (uint16_t)extractJsonLong(body, "restSeconds", swimSetSettings.restTimeSeconds);
    s.swimmerInterval = (uint16_t)extractJsonLong(body, "swimmerInterval", swimSetSettings.swimmerIntervalSeconds);
    s.type = (uint8_t)extractJsonLong(body, "type", SWIMSET_TYPE);
    s.repeat = (uint8_t)extractJsonLong(body, "repeat", 0);

    // Apply and start immediately
    Serial.println("/runSwimSetNow: calling applySwimSetToSettings()");
    applySwimSetToSettings(s);
    server.send(200, "text/plain", "Started");
  });

  server.on("/getSwimQueue", HTTP_GET, []() {
    Serial.println("/getSwimQueue ENTER");
    int lane = currentLane;
    String json = "[";
    for (int i = 0; i < swimSetQueueCount[lane]; i++) {
      int idx = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
      SwimSet &s = swimSetQueue[lane][idx];
      if (i) json += ",";
      json += "{";
      json += String("\"id\":") + String(s.id) + ",";
      json += String("\"clientTempId\":\"") + String((unsigned long long)s.clientTempId) + String("\",");
      json += String("\"rounds\":") + String(s.rounds) + ",";
      json += String("\"swimDistance\":") + String(s.swimDistance) + ",";
      json += String("\"swimSeconds\":") + String(s.swimSeconds, 2) + ",";
      json += String("\"restSeconds\":") + String(s.restSeconds, 2) + ",";
      json += String("\"swimmerInterval\":") + String(s.swimmerInterval, 2) + ",";
      json += String("\"type\":") + String(s.type) + ",";
      json += String("\"repeat\":") + String(s.repeat);
      json += "}";
    }
    json += "]";
    server.send(200, "application/json", json);
  });

  // Reorder swim queue for a lane. Expects form-encoded: lane=<n>&order=<id1,id2,...>
  // id values can be device ids (numeric) or clientTempId strings. Entries not matched
  // will be appended at the end in their existing order.
  server.on("/reorderSwimQueue", HTTP_POST, []() {
    Serial.println("/reorderSwimQueue ENTER");
    String body = server.arg("plain");
    int lane = (int)extractJsonLong(body, "lane", currentLane);
    if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
      server.send(400, "text/plain", "Invalid lane");
      return;
    }

    // Extract order parameter - attempt to find "order": or order= in form-encoded
    String orderStr = "";
    if (body.indexOf("order=") >= 0) {
      int idx = body.indexOf("order=");
      int start = idx + 6;
      orderStr = body.substring(start);
      // Trim trailing ampersand if present
      int amp = orderStr.indexOf('&');
      if (amp >= 0) orderStr = orderStr.substring(0, amp);
      // URL decode approx (replace + with space and %xx hex)
      orderStr.replace('+', ' ');
      // Very small URL decode helper for % hex
      String decoded = "";
      for (int i = 0; i < orderStr.length(); i++) {
        if (orderStr.charAt(i) == '%' && i + 2 < orderStr.length()) {
          String hex = orderStr.substring(i+1, i+3);
          char c = (char)strtol(hex.c_str(), NULL, 16);
          decoded += c;
          i += 2;
        } else {
          decoded += orderStr.charAt(i);
        }
      }
      orderStr = decoded;
    } else {
      orderStr = extractJsonString(body, "order", "");
    }

    if (orderStr.length() == 0) {
      server.send(400, "text/plain", "Missing order");
      return;
    }

    // Parse CSV tokens
    std::vector<String> tokens;
    int start = 0;
    for (int i = 0; i <= orderStr.length(); i++) {
      if (i == orderStr.length() || orderStr.charAt(i) == ',') {
        String tok = orderStr.substring(start, i);
        tok.trim();
        if (tok.length() > 0) tokens.push_back(tok);
        start = i + 1;
      }
    }

    // Build a temporary list of pointers to existing queue entries for this lane
    std::vector<SwimSet> existing;
    for (int i = 0; i < swimSetQueueCount[lane]; i++) {
      int idx = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
      existing.push_back(swimSetQueue[lane][idx]);
    }

    // New ordered list
    std::vector<SwimSet> ordered;

    // Helper to match token to an existing entry by id or clientTempId
    auto matchAndConsume = [&](const String &tok) -> bool {
      // Try numeric device id match first
      unsigned long long parsed = (unsigned long long)strtoull(tok.c_str(), NULL, 10);
      for (size_t i = 0; i < existing.size(); i++) {
        if (parsed != 0 && existing[i].id == (uint32_t)parsed) {
          ordered.push_back(existing[i]);
          existing.erase(existing.begin() + i);
          return true;
        }
      }
      // Try parsing as hex clientTempId
      unsigned long long parsedHex = (unsigned long long)strtoull(tok.c_str(), NULL, 16);
      for (size_t i = 0; i < existing.size(); i++) {
        if (parsedHex != 0 && existing[i].clientTempId == parsedHex) {
          ordered.push_back(existing[i]);
          existing.erase(existing.begin() + i);
          return true;
        }
      }
      return false;
    };

    // Match tokens in order
    for (size_t t = 0; t < tokens.size(); t++) {
      matchAndConsume(tokens[t]);
    }

    // Append any remaining entries not specified
    for (size_t i = 0; i < existing.size(); i++) ordered.push_back(existing[i]);

    // Write ordered back into circular buffer for the lane
    // Reset head/tail/count and re-enqueue in order
    swimSetQueueHead[lane] = 0;
    swimSetQueueTail[lane] = 0;
    swimSetQueueCount[lane] = 0;
    for (size_t i = 0; i < ordered.size() && i < SWIMSET_QUEUE_MAX; i++) {
      swimSetQueue[lane][swimSetQueueTail[lane]] = ordered[i];
      swimSetQueueTail[lane] = (swimSetQueueTail[lane] + 1) % SWIMSET_QUEUE_MAX;
      swimSetQueueCount[lane]++;
    }

    if (DEBUG_ENABLED) {
      Serial.print("reorderSwimQueue: lane "); Serial.print(lane); Serial.print(" newCount="); Serial.println(swimSetQueueCount[lane]);
    }

    server.send(200, "text/plain", "OK");
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
  // Build array for per-lane swimmer counts
  json += "\"numSwimmersPerLane\": [";
  for (int li = 0; li < 4; li++) {
    json += String(globalConfigSettings.numSwimmersPerLane[li]);
    if (li < 3) json += ",";
  }
  json += "],";
  json += "\"poolLength\":" + String(globalConfigSettings.poolLength, 2) + ",";
  // poolLengthUnits should be a JSON string
  json += "\"poolLengthUnits\":\"" + String(globalConfigSettings.poolUnitsYards ? "yards" : "meters") + "\",";
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
  if (server.hasArg("poolLength") && server.hasArg("poolLengthUnits")) {
    String poolLengthStr = server.arg("poolLength");
    String poolLengthUnitsStr = server.arg("poolLengthUnits");

    // Parse the pool length and units
    // Format examples: "25", "50", "25m", "50m"
    bool isMeters = poolLengthUnitsStr == "meters";
    float poolLength = poolLengthStr.toFloat(); // This will parse the number part

    globalConfigSettings.poolLength = poolLength;
    globalConfigSettings.poolUnitsYards = !isMeters; // true if yards, false if meters

    saveGlobalConfigSettings();
    needsRecalculation = true;
    // Update laps per round instead of full reinitialization
    updateSwimmersLapsPerRound();
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
    setupLEDs(); // Reinitialize LEDs when number of lanes changes
  }
  server.send(200, "text/plain", "Number of lanes updated");
}

void handleSetSwimTime() {
  if (server.hasArg("swimTime")) {
    int swimTime = server.arg("swimTime").toInt();
    swimSetSettings.swimTimeSeconds = swimTime;
    saveSwimSetSettings();
  }
  server.send(200, "text/plain", "Swim time updated");
}

void handleSetRestTime() {
  if (server.hasArg("restTime")) {
    int restTime = server.arg("restTime").toInt();
    swimSetSettings.restTimeSeconds = restTime;
    saveSwimSetSettings();
  }
  server.send(200, "text/plain", "Rest time updated");
}

void handleSetSwimDistance() {
  if (server.hasArg("swimDistance")) {
    int swimDistance = server.arg("swimDistance").toInt();
    swimSetSettings.swimSetDistance = swimDistance;
    saveSwimSetSettings();
  }
  server.send(200, "text/plain", "Swim distance updated");
}


void handleSetSwimmerInterval() {
  // Accept both form-encoded posts (swimmerInterval=<n>) and JSON bodies
  String body = server.arg("plain");
  int swimmerInterval = swimSetSettings.swimmerIntervalSeconds; // fallback to current

  if (server.hasArg("swimmerInterval")) {
    swimmerInterval = server.arg("swimmerInterval").toInt();
  } else if (body.length() > 0) {
    // Try to extract from JSON payload as a best-effort fallback
    int parsed = (int)extractJsonLong(body, "swimmerInterval", swimmerInterval);
    swimmerInterval = parsed;
  }

  // Validate value: ignore non-positive values which often result from
  // malformed client payloads (e.g., Number(undefined) -> NaN -> "NaN" -> toInt() == 0)
  if (swimmerInterval <= 0) {
    if (DEBUG_ENABLED) {
      Serial.print("handleSetSwimmerInterval: rejected invalid swimmerInterval="); Serial.println(swimmerInterval);
    }
    server.send(400, "text/plain", "Invalid swimmerInterval; must be >= 1");
    return;
  }

  // Apply and persist valid value
  swimSetSettings.swimmerIntervalSeconds = swimmerInterval;
  saveSwimSetSettings();

  if (DEBUG_ENABLED) {
    Serial.print("handleSetSwimmerInterval: updated swimmerInterval="); Serial.println(swimmerInterval);
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
    int lane = server.arg("lane").toInt();

    globalConfigSettings.numSwimmersPerLane[lane] = numSwimmers;

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
  } else {
    Serial.println(false);
  }
  server.send(200, "text/plain", "Underwater globalConfigSettings updated");
}

void saveGlobalConfigSettings() {
  preferences.putFloat("poolLength", globalConfigSettings.poolLength);
  preferences.putBool("poolUnitsYards", globalConfigSettings.poolUnitsYards);
  preferences.putFloat("stripLengthM", globalConfigSettings.stripLengthMeters);
  preferences.putInt("ledsPerMeter", globalConfigSettings.ledsPerMeter);
  preferences.putFloat("pulseWidthFeet", globalConfigSettings.pulseWidthFeet);
  for (int li = 0; li < 4; li++) {
    String key = String("swimLane") + String(li);
    preferences.putInt(key.c_str(), globalConfigSettings.numSwimmersPerLane[li]);
  }
  preferences.putUChar("colorRed", globalConfigSettings.colorRed);
  preferences.putUChar("colorGreen", globalConfigSettings.colorGreen);
  preferences.putUChar("colorBlue", globalConfigSettings.colorBlue);
  preferences.putUChar("brightness", globalConfigSettings.brightness);
  preferences.putBool("isRunning", globalConfigSettings.isRunning);
  preferences.putBool("sameColorMode", globalConfigSettings.sameColorMode);
  preferences.putBool("underwaterEn", globalConfigSettings.underwatersEnabled);
}

void saveSwimSetSettings() {
  // Persist speed in meters per second
  preferences.putFloat("speedMPS", swimSetSettings.speedMetersPerSecond);
  preferences.putInt("swimTimeSeconds", swimSetSettings.swimTimeSeconds);
  preferences.putInt("restTimeSeconds", swimSetSettings.restTimeSeconds);
  preferences.putInt("swimSetDistance", swimSetSettings.swimSetDistance);
  // Sanitize swimmer interval before persisting to avoid storing invalid (<=0) values
  int si = swimSetSettings.swimmerIntervalSeconds;
  if (si <= 0) si = 4; // enforce safe default
  preferences.putInt("swimmerInterval", si);
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
  // Load per-lane counts if present (key changed to "swimLaneX" to fit 15-char NVS limit)
  for (int li = 0; li < 4; li++) {
    String key = String("swimLane") + String(li);
    globalConfigSettings.numSwimmersPerLane[li] = preferences.getInt(key.c_str(), 3);
  }
  globalConfigSettings.colorRed = preferences.getUChar("colorRed", 0);      // Default to blue
  globalConfigSettings.colorGreen = preferences.getUChar("colorGreen", 0);
  globalConfigSettings.colorBlue = preferences.getUChar("colorBlue", 255);
  globalConfigSettings.brightness = preferences.getUChar("brightness", 196);
  globalConfigSettings.isRunning = preferences.getBool("isRunning", false);  // Default: stopped
  globalConfigSettings.sameColorMode = preferences.getBool("sameColorMode", false); // Default: individual colors

  // Load underwatersEnabled (key changed to "underwaterEn" to fit 15-char NVS limit)
  globalConfigSettings.underwatersEnabled = preferences.getBool("underwaterEn", false);
}

void loadSwimSetSettings() {
  // Load speed (meters per second) with a sensible default ~1.693 m/s (5.56 ft/s)
  swimSetSettings.speedMetersPerSecond = preferences.getFloat("speedMPS", 1.693);
  swimSetSettings.swimTimeSeconds = preferences.getInt("swimTimeSeconds", 30);
  swimSetSettings.restTimeSeconds = preferences.getInt("restTimeSeconds", 5);
  swimSetSettings.swimSetDistance = preferences.getInt("swimSetDistance", 50);  // Default in yards (matches pool default)
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
  float poolLengthInMeters = convertPoolToMeters(globalConfigSettings.poolLength);
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

// ----- Targeted swimmer update helpers -----
// Update lapsPerRound for all swimmers when pool length or swimSet distance changes
void updateSwimmersLapsPerRound() {
  int lapsPerRound = (int)ceil((float)swimSetSettings.swimSetDistance / globalConfigSettings.poolLength);
  for (int lane = 0; lane < 4; lane++) {
    for (int i = 0; i < 6; i++) {
      swimmers[lane][i].lapsPerRound = lapsPerRound;
      // Ensure currentLap is valid
      // TODO: Rather than just reset this to 1, we should probably
      // look at what the previous value and pool length was and set the
      // new value accordingly, although this shouldn't change during a set.
      if (swimmers[lane][i].currentLap > swimmers[lane][i].lapsPerRound) {
        swimmers[lane][i].currentLap = 1;
      }
    }
  }
}
// ----- end targeted helpers -----

// Helper function to convert pool units (yards or meters) to LED strip units (meters)
float convertPoolToMeters(float distanceInPoolUnits) {
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
        // Use per-lane swimmer count (clamped to 6)
        int laneSwimmerCount = globalConfigSettings.numSwimmersPerLane[lane];
        if (laneSwimmerCount < 1) laneSwimmerCount = 1;
        if (laneSwimmerCount > 6) laneSwimmerCount = 6;
        for (int i = 0; i < laneSwimmerCount; i++) {
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
  float maxDelayDistanceInMeters = convertPoolToMeters(maxDelayDistance);
  int maxDelayLEDs = (int)(maxDelayDistanceInMeters * globalConfigSettings.ledsPerMeter);

  // Find the next swimmer who is resting and has the shortest time until they start
  int nextSwimmerIndex = -1;
  float shortestRemainingDelay = 999999.0;

  int laneSwimmerCount = globalConfigSettings.numSwimmersPerLane[laneIndex];
  if (laneSwimmerCount < 1) laneSwimmerCount = 1;
  if (laneSwimmerCount > 6) laneSwimmerCount = 6;
  for (int i = 0; i < laneSwimmerCount; i++) {
    Swimmer* swimmer = &swimmers[laneIndex][i];

    // Only consider swimmers who are resting
    if (!swimmer->isResting || swimmer->finished) {
      continue;
    }

    // Calculate when this swimmer should start
    unsigned long targetRestDuration;
    if (swimmer->currentRound == 1) {
      // First round: staggered start based on swimmer interval.
      // Use swimmerInterval as the base so first swimmer starts after swimmerInterval,
      // second after 2*swimmerInterval, etc.
      targetRestDuration = (unsigned long)((i + 1) * (unsigned long)swimSetSettings.swimmerIntervalSeconds * 1000);
    } else {
      // Subsequent rounds: use rest time
      targetRestDuration = (unsigned long)(swimSetSettings.restTimeSeconds * 1000);
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
    float delayDistanceInMeters = convertPoolToMeters(delayDistanceInPoolUnits);
    int delayLEDs = (int)(delayDistanceInMeters * globalConfigSettings.ledsPerMeter);

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
  if (DEBUG_ENABLED) {
    Serial.println("\n========== INITIALIZING SWIMMERS ==========");
  }

  for (int lane = 0; lane < 4; lane++) {
  for (int i = 0; i < 6; i++) {
      swimmers[lane][i].position = 0;
      swimmers[lane][i].direction = 1;
      swimmers[lane][i].hasStarted = false;  // Initialize as not started
      swimmers[lane][i].lastUpdate = 0;

      // Initialize round and rest tracking
      swimmers[lane][i].currentRound = 0;
      swimmers[lane][i].currentLap = 0;
      swimmers[lane][i].lapsPerRound = 0;
      swimmers[lane][i].isResting = true;
      swimmers[lane][i].restStartTime = 0;
      swimmers[lane][i].totalDistance = 0.0;
      swimmers[lane][i].lapDirection = 1;  // Start going away from start wall

      // Initialize underwater tracking
      swimmers[lane][i].underwaterActive = false;
      swimmers[lane][i].inSurfacePhase = false;
      swimmers[lane][i].distanceTraveled = 0.0;
      swimmers[lane][i].hideTimerStart = 0;

      // Initialize debug tracking
      swimmers[lane][i].debugRestingPrinted = false;
      swimmers[lane][i].debugSwimmingPrinted = false;

      // Initialize finished flag
      swimmers[lane][i].finished = false;

      // Initialize cached swim set settings (will be populated when swim set starts)
      swimmers[lane][i].cachedNumRounds = 0;
      swimmers[lane][i].cachedLapsPerRound = 0;
      swimmers[lane][i].cachedSpeedMPS = 0.0;
      swimmers[lane][i].cachedRestSeconds = 0;
      swimmers[lane][i].activeSwimSetId = 0;
      swimmers[lane][i].queueIndex = 0;  // Start at first set in queue

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
  // If swimmer has already completed all rounds, do nothing
  if (swimmer->finished) return;
  unsigned long currentTime = millis();

  // Check if swimmer is resting
  if (swimmer->isResting) {
    // How much time have they been resting
    unsigned long restElapsed = currentTime - swimmer->restStartTime;

    // What is our target rest duration (in milliseconds)?
    unsigned long targetRestDuration;
    if (swimmer->currentRound == 1) {
      // First round: staggered start using swimmerInterval as the base delay.
      // First swimmer starts after swimmerInterval, second after 2*swimmerInterval, etc.
      targetRestDuration = (unsigned long)((swimmerIndex + 1) * (unsigned long)swimSetSettings.swimmerIntervalSeconds * 1000);
    } else {
      // Subsequent rounds: include rest period plus swimmer interval offset
      targetRestDuration = (unsigned long)(swimmer->cachedRestSeconds * 1000) + (unsigned long)(swimmerIndex * swimSetSettings.swimmerIntervalSeconds * 1000);
    }

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
      Serial.print("  Elapsed rest time: ");
      Serial.print(restElapsed / 1000.0);
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
      Serial.print(" - SWIMMING - ");
      Serial.print("Queue Index: ");
      Serial.print(swimmer->queueIndex);
      Serial.print(", Round ");
      Serial.print(swimmer->currentRound);
      Serial.print(", Lap ");
      Serial.print(swimmer->currentLap);
      Serial.print("/");
      Serial.print(swimmer->lapsPerRound);
      Serial.print(", direction: ");
      Serial.print(swimmer->lapDirection == 1 ? "away from start" : "towards start");
      Serial.print(", position: ");
      Serial.print(swimmer->position);
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
      speedInPoolUnits = swimmer->cachedSpeedMPS / 0.9144;
    } else {
      // Already meters/sec
      speedInPoolUnits = swimmer->cachedSpeedMPS;
    }
    float distanceTraveled = speedInPoolUnits * deltaSeconds;

    // Update total distance
    swimmer->totalDistance += distanceTraveled;

    // Check if swimmer has completed one pool length based on actual distance
    // Calculate 1-based lap number (lap 1 = 0 to poolLength, lap 2 = poolLength to 2*poolLength, etc.)
    int calculatedLap = (int)ceil(swimmer->totalDistance / globalConfigSettings.poolLength);

    // distance for current length/lap
    float distanceForCurrentLength = swimmer->totalDistance - ((calculatedLap - 1) * globalConfigSettings.poolLength);

    if (calculatedLap > swimmer->currentLap) {
      float overshoot = swimmer->totalDistance - (swimmer->currentLap * globalConfigSettings.poolLength);

      if (DEBUG_ENABLED) {
        Serial.println("  *** WALL TURN ***");
        Serial.print("    calculatedLap based on distance: ");
        Serial.print(calculatedLap);
        Serial.print(", Current lap: ");
        Serial.print(swimmer->currentLap);
        Serial.print(", Total distance: ");
        Serial.print(swimmer->totalDistance);
        Serial.print(", Distance for current length: ");
        Serial.print(distanceForCurrentLength);
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
        // Round complete - reset length counter and check if we should rest
        swimmer->currentLap = 1;
        swimmer->totalDistance = 0.0;

        if (DEBUG_ENABLED) {
          Serial.println("  *** ROUND COMPLETE ***");
          Serial.print("    Completed round ");
          Serial.println(swimmer->currentRound);
          Serial.print("    Current lap reset to ");
          Serial.println(swimmer->currentLap);
          Serial.print("    Total distance reset to ");
          Serial.println(swimmer->totalDistance);
        }

        // Only rest if we haven't completed all rounds yet
        if (swimmer->currentRound < swimmer->cachedNumRounds) {
          swimmer->currentRound++;
          swimmer->isResting = true;
          // Rest start time should be calculated from when they were
          // supposed to hit the wall, not current time (to account for overshoot)
          swimmer->restStartTime = currentTime - (overshoot / swimmer->cachedSpeedMPS * 1000.0);

          if (DEBUG_ENABLED) {
            Serial.println("  *** MORE ROUNDS TO GO IN SET ***");
            Serial.print("    Now on round ");
            Serial.println(swimmer->currentRound);
            Serial.println("    isResting set to true");
            Serial.print("    restStartTime to ");
            Serial.print(swimmer->restStartTime);
            Serial.print(" (accounting for overshoot of ");
            Serial.println((overshoot / swimmer->cachedSpeedMPS * 1000.0));
          }
        } else {
          // All rounds complete for this swim set
          if (DEBUG_ENABLED) {
            Serial.println("  *** ALL ROUNDS COMPLETE FOR CURRENT SET ***");
            Serial.print("    Swimmer ");
            Serial.print(swimmerIndex);
            Serial.print(" finished swim set ID ");
            Serial.print(swimmer->activeSwimSetId);
            Serial.print(" (queue index ");
            Serial.print(swimmer->queueIndex);
            Serial.println(")");
          }

          // Try to get the next swim set in queue (queueIndex + 1)
          SwimSet nextSet;
          if (getSwimSetByIndex(nextSet, laneIndex, swimmer->queueIndex + 1)) {
            if (DEBUG_ENABLED) {
              Serial.println("  *** AUTO-ADVANCING TO NEXT SWIM SET ***");
              Serial.print("    Next set ID: ");
              Serial.print(nextSet.id);
              Serial.print(", queue index: ");
              Serial.print(swimmer->queueIndex + 1);
              Serial.print(", rounds: ");
              Serial.print(nextSet.rounds);
              Serial.print(", swimDistance: ");
              Serial.print(nextSet.swimDistance);
              Serial.print(", swim seconds: ");
              Serial.print(nextSet.swimSeconds);
              Serial.print(", rest seconds: ");
              Serial.println(nextSet.restSeconds);
            }

            // Calculate new settings for the next set
            float lengthMeters = convertPoolToMeters((float)nextSet.swimDistance);
            float speedMPS = (nextSet.swimSeconds > 0) ? (lengthMeters / nextSet.swimSeconds) : 0.0;
            int lapsPerRound = ceil(nextSet.swimDistance / globalConfigSettings.poolLength);

            // Cache the new swim set settings in this swimmer
            swimmer->cachedNumRounds = nextSet.rounds;
            swimmer->cachedLapsPerRound = lapsPerRound;
            swimmer->cachedSpeedMPS = speedMPS;
            swimmer->cachedRestSeconds = nextSet.restSeconds;
            swimmer->activeSwimSetId = nextSet.id;
            swimmer->queueIndex++;  // Move to next index in queue

            // Reset swimmer state for new set
            swimmer->currentRound = 1;
            swimmer->currentLap = 1;
            swimmer->lapsPerRound = lapsPerRound;
            swimmer->isResting = true;
            swimmer->restStartTime = currentTime;
            swimmer->totalDistance = 0.0;
            swimmer->finished = false;

            if (DEBUG_ENABLED) {
              Serial.print("    Swimmer ");
              Serial.print(swimmerIndex);
              Serial.println(" starting rest before next set");
            }
          } else {
            // No more sets in queue - swimmer is done
            // Mark swimmer as finished so we stop further updates
            swimmer->finished = true;
            swimmer->isResting = true;
            swimmer->hasStarted = false;
            swimmer->restStartTime = currentTime;
            if (DEBUG_ENABLED) {
              Serial.println("  *** NO MORE SETS IN QUEUE ***");
              Serial.print("    Swimmer ");
              Serial.print(swimmerIndex);
              Serial.print(" completed all ");
              Serial.print(swimmer->queueIndex + 1);
              Serial.println(" sets!");
            }
          }
        }

        // Place LED at the wall based on direction
        if (swimmer->lapDirection == 1) {
          swimmer->position = 0;
        } else {
          // Convert pool length to LED position
          float poolLengthInMeters = convertPoolToMeters(globalConfigSettings.poolLength);
          float ledPosition = floor((poolLengthInMeters * globalConfigSettings.ledsPerMeter)-1);
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
        float overshootInMeters = convertPoolToMeters(overshoot);
        int overshootLEDs = (int)(overshootInMeters * globalConfigSettings.ledsPerMeter);
        if (swimmer->lapDirection == 1) {
          swimmer->position = overshootLEDs;
        } else {
          float poolLengthInMeters = convertPoolToMeters(globalConfigSettings.poolLength);
          float ledPosition = floor((poolLengthInMeters * globalConfigSettings.ledsPerMeter)-1) - overshootLEDs;
          swimmer->position = ledPosition;
        }
      }
    } else {
      // Normal movement within the pool length
      // distanceForCurrentLength is the distance traveled in the current lap
      // Convert to LED strip units
      float distanceInMeters = convertPoolToMeters(distanceForCurrentLength);
      float poolLengthInMeters = convertPoolToMeters(globalConfigSettings.poolLength);

      // Calculate LED position based on direction
      if (swimmer->lapDirection == 1) {
        // Swimming away from start: position = 0 + distance
        swimmer->position = (int)(distanceInMeters * globalConfigSettings.ledsPerMeter);
      } else {
        // Swimming back to start: position = poolLength - distance
        float positionInMeters = poolLengthInMeters - distanceInMeters;
        swimmer->position = (int)(positionInMeters * globalConfigSettings.ledsPerMeter);
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

  bool isSomethingRunning = false;
  for (int lane = 0; lane < globalConfigSettings.numLanes; lane++) {
    if (globalConfigSettings.laneRunning[lane]) {
      isSomethingRunning = true;
      break;
    }
  }
  if (isSomethingRunning) {
    Serial.println("\n========== STATUS UPDATE ==========");
    for (int lane = 0; lane < globalConfigSettings.numLanes; lane++) {
      if (globalConfigSettings.laneRunning[lane]) {
        Serial.print("Lane ");
        Serial.print(lane);
        Serial.println(":");
        int laneSwimmerCount = globalConfigSettings.numSwimmersPerLane[lane];
        if (laneSwimmerCount < 1) laneSwimmerCount = 1;
        if (laneSwimmerCount > 6) laneSwimmerCount = 6;
        for (int i = 0; i < laneSwimmerCount; i++) {
          Swimmer* s = &swimmers[lane][i];
          Serial.print("  Swimmer ");
          Serial.print(i);
          Serial.print(": ");
          if (s->isResting) {
            Serial.print("RESTING (");
            Serial.print((currentTime - s->restStartTime) / 1000.0);
            Serial.print("s)");
            Serial.print(" Q:");
            Serial.print(s->queueIndex);
            Serial.print(" hasStarted=");
            Serial.print(s->hasStarted);
          } else {
            Serial.print("Swimming - ");

            Serial.print(" Q:");
            Serial.print(s->queueIndex);
            Serial.print(" R");
            Serial.print(s->currentRound);
            Serial.print("/");
            Serial.print(swimSetSettings.numRounds);
            Serial.print(" L");
            Serial.print(s->currentLap);
            Serial.print("/");
            Serial.print(s->lapsPerRound);

            // Calculate distance in current lap (1-based lap calculation)
            int currentLapNum = (int)ceil(s->totalDistance / globalConfigSettings.poolLength);
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
  float lightSizeInMeters = convertPoolToMeters(lightSizeInPoolUnits);
  int lightSizeLEDs = (int)(lightSizeInMeters * globalConfigSettings.ledsPerMeter);
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