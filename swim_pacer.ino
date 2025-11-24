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
#include <ArduinoJson.h>
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
//#define LED_TYPE        WS2812B     // LED strip type
#define LED_TYPE        WS2815
#define COLOR_ORDER     GRB         // Color order (may need adjustment)

// Maximum supported LEDs by FastLED for this project
#define MAX_LEDS 150

// GPIO pins for multiple LED strips (lanes)
// Lane 1: GPIO 18, Lane 2: GPIO 19, Lane 3: GPIO 21, Lane 4: GPIO 2
#define LANE_0_PIN 18
#define LANE_1_PIN 19
#define LANE_2_PIN 21
#define LANE_3_PIN 2

// Enum for Queue insert error codes
enum QueueInsertError {
  QUEUE_INSERT_SUCCESS = 0,
  QUEUE_INSERT_DUPLICATE = 1,
  QUEUE_INSERT_FULL = 2,
  QUEUE_INVALID_LANE = 3
};

const int SWIMSET_QUEUE_MAX = 12;
const int MAX_LANES_SUPPORTED = 4;
const int MAX_SWIMMERS_SUPPORTED = 10;

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
const int DEFAULT_NUM_SWIMMERS = 3;
struct GlobalConfigSettings {
  float poolLength = 25.0;                   // Pool length (in poolUnits - yards or meters)
  bool poolUnitsYards = true;                // true = yards, false = meters
  float stripLengthMeters = 23.0;            // LED strip length in meters (75 feet)
  int ledsPerMeter = 30;                     // LEDs per meter
  int numLedStrips[MAX_LANES_SUPPORTED] = {1, 1, 1, 1}; // Number of LED strips per lane
  int gapBetweenStrips = 23;                 // gap inbetween LED strips that don't contain LEDs (centimeters)
  int numLanes = 1;                          // Number of LED strips/lanes connected
  float pulseWidthFeet = 1.0;                // Width of pulse in feet
  bool delayIndicatorsEnabled = true;        // Whether to show delay countdown indicators
  int numSwimmersPerLane[MAX_LANES_SUPPORTED] = {              // Per-lane swimmer counts (preferred)
    DEFAULT_NUM_SWIMMERS,
    DEFAULT_NUM_SWIMMERS,
    DEFAULT_NUM_SWIMMERS,
    DEFAULT_NUM_SWIMMERS
  };
  uint8_t colorRed = 0;                      // RGB color values - default to blue
  uint8_t colorGreen = 0;
  uint8_t colorBlue = 255;
  uint8_t brightness = 100;                  // Overall brightness (0-255)
  bool isRunning = false;                    // Whether the effect is active (default: stopped)
  bool laneRunning[MAX_LANES_SUPPORTED] = {false, false, false, false}; // Per-lane running states
  bool sameColorMode = false;                // Whether all swimmers use the same color (true) or individual colors (false)
  bool underwatersEnabled = false;           // Whether underwater indicators are enabled
  float firstUnderwaterDistanceFeet = 5.0;   // First underwater distance in feet
  float underwaterDistanceFeet = 3.0;        // Subsequent underwater distance in feet
  float underwatersSizeFeet = 1.0;           // Size of underwater light pulse in feet
  float hideAfterSeconds = 1.0;              // Hide underwater light after surface phase (seconds)
  uint8_t underwaterColorRed = 0;            // RGB color values for underwater indicators when underwater
  uint8_t underwaterColorGreen = 0;
  uint8_t underwaterColorBlue = 255;
  uint8_t underwaterSurfaceColorRed = 0;            // RGB color values for underwater indicators when surfacing
  uint8_t underwaterSurfaceColorGreen = 255;
  uint8_t underwaterSurfaceColorBlue = 0;
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
  int numRounds = 10;                        // Number of numRounds/sets to complete
};

SwimSetSettings swimSetSettings;

// ----- Swim set queue types and storage -----
enum SwimSetType { SWIMSET_TYPE = 0, REST_TYPE = 1, LOOP_TYPE = 2, MOVE_TYPE = 3 };

// ----- Swim set status bit mask -----
enum SwimSetStatus {
  SWIMSET_STATUS_PENDING    = 0x0 << 0,
  SWIMSET_STATUS_SYNCHED    = 0x1 << 0,
  SWIMSET_STATUS_ACTIVE     = 0x1 << 1,
  SWIMSET_STATUS_COMPLETED  = 0x1 << 2,
  SWIMSET_STATUS_STOPPED    = 0x1 << 3,
};

struct SwimSet {
  uint8_t numRounds;           // number of numRounds
  uint16_t swimDistance;    // distance in pool's native units (no units attached)
  uint16_t swimTime;     // seconds to swim the 'length'
  uint16_t restTime;     // rest time between numRounds
  uint16_t swimmerInterval; // interval between swimmers (seconds)
  uint8_t status;           // status flags (bitmask)
  uint8_t type;             // SwimSetType
  uint8_t repeat;           // repeat metadata for LOOP_TYPE
  uint32_t id;              // device-assigned canonical id (0 = not assigned)
  unsigned long long uniqueId; // client-supplied unique id for reconciliation (0 = none)
};

// Simple fixed-size in-memory per-lane queues
SwimSet swimSetQueue[MAX_LANES_SUPPORTED][SWIMSET_QUEUE_MAX];
int swimSetQueueHead[MAX_LANES_SUPPORTED] = {0,0,0,0};
int swimSetQueueTail[MAX_LANES_SUPPORTED] = {0,0,0,0};
int swimSetQueueCount[MAX_LANES_SUPPORTED] = {0,0,0,0};

// Device-assigned swimset ID counter (monotonic)
uint32_t nextSwimSetId = 0;

// track the queue index (0 = head) of the set currently started for each lane
int laneActiveQueueIndex[MAX_LANES_SUPPORTED] = {-1, -1, -1, -1};

// Forward declarations for functions used below
float convertPoolToMeters(float distanceInPoolUnits);
void loadSettings();
void updateLEDEffect();
void printPeriodicStatus();
void recalculateValues(int lane);
void initializeSwimmers();
void updateSwimmersLapsPerRound();
void saveSwimSetSettings();
void saveGlobalConfigSettings();

// Web handler prototypes (defined later)
void handleSetGapBetweenStrips();
void handleSetNumLanes();
void handleSetSwimDistance();
void handleSetSwimTime();
void handleSetRestTime();
void handleSetSwimmerInterval();
void handleSetDelayIndicators();
void handleSetNumSwimmers();
void handleSetNumRounds();
void handleSetColorMode();
void handleSetSwimmerColor();
void handleSetSwimmerColors();
void handleSetUnderwaterSettings();
void handleStopSwimSet();

Preferences preferences;
WebServer server(80);

// ========== CALCULATED VALUES ==========
int fullLengthLEDs[MAX_LANES_SUPPORTED];  // Total number of rendered LEDs (ignoring gaps)
int visibleLEDs[MAX_LANES_SUPPORTED];     // Total number of actual LEDs (accounting for gaps)
std::vector<int> gapLEDs[MAX_LANES_SUPPORTED];         // positions where there are no LEDs due to gaps
float ledSpacingCM;               // Spacing between LEDs in cm
int pulseWidthLEDs;               // Width of pulse in LEDs
int underwatersWidthLEDs;         // Width of underwaters pulse in LEDs
int delayMS;                      // Delay between LED updates
float poolToStripRatio;           // Ratio of pool to strip length
CRGB underwaterColor;            // Color for underwater indicators
CRGB underwaterSurfaceColor;     // Color for underwater surface indicators

// ========== GLOBAL VARIABLES ==========
CRGB* renderedLEDs[MAX_LANES_SUPPORTED] = {nullptr, nullptr, nullptr, nullptr}; // Array of LED strip pointers for each lane
CRGB* scanoutLEDs[MAX_LANES_SUPPORTED] = {nullptr, nullptr, nullptr, nullptr}; // Array of LED strip pointers for each lane
int direction = 1;
unsigned long lastUpdate = 0;
bool needsRecalculation = true;

// Helper: parse lane from POST JSON body (returns -1 when missing)
static int parseLaneFromBody(const String &body) {
  // extractJsonLong is already in your codebase; fallback to -1 if missing
  long v = extractJsonLong(body.c_str(), "lane", -1);
  return (int)v;
}

// The queue arrays are declared earlier but we define the helpers here for readability.
int enqueueSwimSet(const SwimSet &s, int lane) {
  if (DEBUG_ENABLED) Serial.println("enqueueSwimSet(): called");
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    if (DEBUG_ENABLED) Serial.println("  invalid lane, rejecting set");
    return QUEUE_INVALID_LANE;
  }
  if (swimSetQueueCount[lane] >= SWIMSET_QUEUE_MAX) {
    if (DEBUG_ENABLED) Serial.println("  queue full, rejecting set");
    return QUEUE_INSERT_FULL;
  }
  // Check if we already have a set with the same uniqueId in the queue
  for (int i = 0; i < swimSetQueueCount[lane]; i++) {
    int index = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
    if (swimSetQueue[lane][index].uniqueId == s.uniqueId && s.uniqueId != 0ULL) {
      if (DEBUG_ENABLED) {
        Serial.print("  duplicate uniqueId found in queue, rejecting set: ");
        Serial.println(s.uniqueId);
      }
      return QUEUE_INSERT_DUPLICATE;
    }
  }
  if (DEBUG_ENABLED) {
    Serial.print("enqueueSwimSet(): received set for lane:");
    Serial.print(lane);
    Serial.print("  [");
    Serial.print(s.numRounds);
    Serial.print("x");
    Serial.print(s.swimDistance);
    Serial.print(" on ");
    Serial.print(s.swimTime);
    Serial.print(" rest:");
    Serial.print(s.restTime);
    Serial.print(" interval:");
    Serial.print(s.swimmerInterval);
    Serial.println("]");
  }
  swimSetQueue[lane][swimSetQueueTail[lane]] = s;
  swimSetQueueTail[lane] = (swimSetQueueTail[lane] + 1) % SWIMSET_QUEUE_MAX;
  swimSetQueueCount[lane]++;
  if (DEBUG_ENABLED) {
    Serial.print("  Queue Count After Insert="); Serial.println(swimSetQueueCount[lane]);
  }
  return QUEUE_INSERT_SUCCESS;
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
  unsigned long expectedStartTime; // When the swimmer is expected to start swimming (after rest)
  unsigned long expectedRestDuration; // Expected rest duration (ms) for current rest period

  // Debug tracking - to avoid repeated messages
  bool debugRestingPrinted;      // Has the "resting" state been printed already
  bool debugSwimmingPrinted;     // Has the "swimming" state been printed already

  // Underwater tracking
  bool underwaterActive;        // Is underwater light currently active
  bool inSurfacePhase;         // Has switched to surface color
  float distanceTraveled;      // Distance traveled in current underwater phase
  unsigned long hideTimerStart;   // When hide timer started (after surface distance completed)
  bool finished;               // Has swimmer completed all numRounds

  // Per-swimmer cached swim set settings (for independent progression through queue)
  int cachedNumRounds;         // Number of numRounds for this swimmer's active swim set
  int cachedLapsPerRound;      // Number of laps per round (calculated from distance)
  float cachedSpeedMPS;        // Speed in meters per second
  int cachedRestSeconds;       // Rest duration in seconds
  uint32_t activeSwimSetId;    // ID of the active swim set this swimmer is executing
  int queueIndex;              // Index in the lane's queue that this swimmer is currently executing (0-based)
};

Swimmer swimmers[MAX_LANES_SUPPORTED][MAX_SWIMMERS_SUPPORTED];
CRGB swimmerColors[] = {
  CRGB::Red,
  CRGB::Green,
  CRGB::Blue,
  CRGB::Yellow,
  CRGB::Purple,
  CRGB::Cyan
};

// Apply the swim set to current swimSetSettings and start the set on current lane
void applySwimSetToSettings(const SwimSet &s, int lane) {
  long currentMillis = millis();

  // Compute speed (m/s) from swimDistance (in pool units) and swimTime
  // convert swimDistance to meters using current pool units
  float swimDistanceMeters = convertPoolToMeters((float)s.swimDistance);
  if (s.swimTime <= 0.0f) return; // avoid divide by zero
  float speedMPS = swimDistanceMeters / s.swimTime;

  swimSetSettings.speedMetersPerSecond = speedMPS;
  swimSetSettings.swimSetDistance = s.swimDistance;
  swimSetSettings.numRounds = s.numRounds;
  swimSetSettings.restTimeSeconds = s.restTime;
  swimSetSettings.swimmerIntervalSeconds = s.swimmerInterval;

  // Debug: print swim set being applied
  if (DEBUG_ENABLED) {
    Serial.println("applySwimSetToSettings(): applying set:");
    Serial.print("  lane="); Serial.println(lane);
    Serial.print("  numRounds="); Serial.println(s.numRounds);
    Serial.print("  swimDistance="); Serial.println(s.swimDistance);
    Serial.print("  swimTime="); Serial.println(s.swimTime);
    Serial.print("  restTime="); Serial.println(s.restTime);
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
  for (int i = 0; i < MAX_SWIMMERS_SUPPORTED; i++) {
    //swimmers[lane][i].position = 0; This should continue from previous position
    //swimmers[lane][i].direction = 1; This should continue from previous direction
    swimmers[lane][i].hasStarted = false;  // Initialize as not started
    swimmers[lane][i].lastUpdate = currentMillis;

    // Initialize round and rest tracking
    swimmers[lane][i].currentRound = 1;
    swimmers[lane][i].currentLap = 1;
    swimmers[lane][i].lapsPerRound = ceil(swimSetSettings.swimSetDistance / globalConfigSettings.poolLength);
    swimmers[lane][i].isResting = true;
    swimmers[lane][i].finished = false;
    // Start rest timer now so swimmerInterval-based stagger will be honored
    swimmers[lane][i].restStartTime = currentMillis;
    // Total distance should start at 0 for the new set
    swimmers[lane][i].totalDistance = 0.0;
    //swimmers[lane][i].lapDirection = 1;  This should continue from previous direction
    // Compute the target rest duration and absolute start time so it's easy to verify stagger
    unsigned long initilRestDuration = (i + 1) * swimSetSettings.swimmerIntervalSeconds * 1000UL;
    swimmers[lane][i].expectedStartTime = currentMillis + initilRestDuration;
    swimmers[lane][i].expectedRestDuration = initilRestDuration;

    // Initialize underwater tracking
    swimmers[lane][i].underwaterActive = globalConfigSettings.underwatersEnabled;
    swimmers[lane][i].inSurfacePhase = false;
    swimmers[lane][i].distanceTraveled = 0.0;
    swimmers[lane][i].hideTimerStart = 0;

    // Initialize debug tracking
    swimmers[lane][i].debugRestingPrinted = false;
    swimmers[lane][i].debugSwimmingPrinted = false;

    // Cache swim set settings for this swimmer (enables independent progression through queue)
    swimmers[lane][i].cachedNumRounds = s.numRounds;
    swimmers[lane][i].cachedLapsPerRound = ceil(s.swimDistance / globalConfigSettings.poolLength);
    swimmers[lane][i].cachedSpeedMPS = speedMPS;
    swimmers[lane][i].cachedRestSeconds = s.restTime;
    swimmers[lane][i].activeSwimSetId = s.id;

    // set queue index: use laneActiveQueueIndex if set, otherwise 0
    int qidx = -1;
    if (lane >= 0 && lane < MAX_LANES_SUPPORTED) qidx = laneActiveQueueIndex[lane];
    if (qidx < 0) qidx = 0;
    swimmers[lane][i].queueIndex = qidx;
  }

  // Debug: dump swimmer state for current lane after initialization (first 3 swimmers)
  if (DEBUG_ENABLED) {
    Serial.println("  swimmer state after applying the swim set:");
    for (int i = 0; i < MAX_SWIMMERS_SUPPORTED; i++) {
      Swimmer &sw = swimmers[lane][i];
      Serial.print("  swimmer="); Serial.print(i);
      Serial.print(" pos="); Serial.print(sw.position);
      Serial.print(" dir="); Serial.print(sw.direction);
      Serial.print(" hasStarted="); Serial.print(sw.hasStarted ? 1 : 0);
      Serial.print(" currentRound="); Serial.print(sw.currentRound);
      Serial.print(" currentLap="); Serial.print(sw.currentLap);
      Serial.print(" lapsPerRound="); Serial.print(sw.lapsPerRound);
      Serial.print(" isResting="); Serial.print(sw.isResting ? 1 : 0);
      Serial.print(" restStartTime="); Serial.print(sw.restStartTime);
      Serial.print(" expectedRestDuration="); Serial.print(sw.expectedRestDuration / 1000.0);
      Serial.print(" startAtMillis="); Serial.print(sw.expectedStartTime);
      Serial.print(" totalDistance="); Serial.print(sw.totalDistance, 3);
      Serial.print(" lapDirection="); Serial.println(sw.lapDirection);
    }
  }

  // s is a const reference (may be a temporary or payload); don't modify it.
  // If this swim set came from the lane queue, mark the corresponding queue entry ACTIVE.
  if (lane >= 0 && lane < MAX_LANES_SUPPORTED) {
    int qidx = laneActiveQueueIndex[lane];
    if (qidx >= 0 && qidx < swimSetQueueCount[lane]) {
      int actualIdx = (swimSetQueueHead[lane] + qidx) % SWIMSET_QUEUE_MAX;
      swimSetQueue[lane][actualIdx].status |= SWIMSET_STATUS_ACTIVE;
      swimSetQueue[lane][actualIdx].status &= ~SWIMSET_STATUS_STOPPED;
    }
  }

  // Start the current lane pacer
  globalConfigSettings.laneRunning[lane] = true;
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


  // Setup WiFi Access Point
  setupWiFi();

  // Setup web server
  setupWebServer();

  // Calculate initial values
  FastLED.clear();
  for (int li = 0; li < MAX_LANES_SUPPORTED; li++) {
    recalculateValues(li);
  }
  FastLED.setBrightness(globalConfigSettings.brightness);
  FastLED.show();

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
    //printPeriodicStatus(); // Print status update every few seconds
  }

  // Recalculate if globalConfigSettings changed
  if (needsRecalculation) {
    FastLED.clear();
    for (int li = 0; li < MAX_LANES_SUPPORTED; li++) {
      recalculateValues(li);
    }
    FastLED.setBrightness(globalConfigSettings.brightness);
    FastLED.show();
    needsRecalculation = false;
  }

  digitalWrite(2, LOW);
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

  // Handle getting current globalConfigSettings
  server.on("/globalConfigSettings", HTTP_GET, handleGetSettings);

  // Handlers for updating globalConfigSettings
  server.on("/setBrightness", HTTP_POST, handleSetBrightness);
  server.on("/setPulseWidth", HTTP_POST, handleSetPulseWidth);
  server.on("/setStripLength", HTTP_POST, handleSetStripLength);
  server.on("/setPoolLength", HTTP_POST, handleSetPoolLength);
  server.on("/setLedsPerMeter", HTTP_POST, handleSetLedsPerMeter);
  server.on("/setNumLedStrips", HTTP_POST, handleSetNumLedStrips);
  server.on("/setGapBetweenStrips", HTTP_POST, handleSetGapBetweenStrips);
  server.on("/setNumLanes", HTTP_POST, handleSetNumLanes);
  server.on("/setSwimDistance", HTTP_POST, handleSetSwimDistance);
  server.on("/setSwimTime", HTTP_POST, handleSetSwimTime);
  server.on("/setRestTime", HTTP_POST, handleSetRestTime);
  server.on("/setSwimmerInterval", HTTP_POST, handleSetSwimmerInterval);
  server.on("/setDelayIndicators", HTTP_POST, handleSetDelayIndicators);
  server.on("/setNumSwimmers", HTTP_POST, handleSetNumSwimmers);
  server.on("/setNumRounds", HTTP_POST, handleSetNumRounds);
  server.on("/setColorMode", HTTP_POST, handleSetColorMode);
  server.on("/setSwimmerColor", HTTP_POST, handleSetSwimmerColor);
  server.on("/setSwimmerColors", HTTP_POST, handleSetSwimmerColors);
  server.on("/setUnderwaterSettings", HTTP_POST, handleSetUnderwaterSettings);

  // Swim set queue management endpoints
  server.on("/enqueueSwimSet", HTTP_POST, handleEnqueueSwimSet);
  server.on("/updateSwimSet", HTTP_POST, handleUpdateSwimSet);
  server.on("/deleteSwimSet", HTTP_POST, handleDeleteSwimSet);
  server.on("/startSwimSet", HTTP_POST, handleStartSwimSet);
  server.on("/stopSwimSet", HTTP_POST, handleStopSwimSet);
  server.on("/getSwimQueue", HTTP_POST, handleGetSwimQueue);
  server.on("/reorderSwimQueue", HTTP_POST, handleReorderSwimQueue);

  // Miscellaneous handlers
  server.on("/resetLane", HTTP_POST, handleResetLane);

  server.begin();
  Serial.println("Web server started");
}

// Web Handlers ******************************************

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
  json += "\"stripLengthMeters\":" + String(globalConfigSettings.stripLengthMeters, 2) + ",";
  json += "\"ledsPerMeter\":" + String(globalConfigSettings.ledsPerMeter) + ",";
  json += "\"numLedStrips\": [";
  for (int li = 0; li < MAX_LANES_SUPPORTED; li++) {
    json += String(globalConfigSettings.numLedStrips[li]);
    if (li < MAX_LANES_SUPPORTED - 1) json += ",";
  }
  json += "],";
  json += "\"gapBetweenStrips\":" + String(globalConfigSettings.gapBetweenStrips, 2) + ",";
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
  Serial.print(" handleGetSettings: numSwimmersPerLane:");
  for (int li = 0; li < MAX_LANES_SUPPORTED; li++) {
    json += String(globalConfigSettings.numSwimmersPerLane[li]);
    if (li < MAX_LANES_SUPPORTED - 1) json += ",";
    Serial.print(" " + String(globalConfigSettings.numSwimmersPerLane[li]));
  }
  Serial.println();
  json += "],";
  json += "\"poolLength\":" + String(globalConfigSettings.poolLength, 2) + ",";
  // poolLengthUnits should be a JSON string
  json += "\"poolLengthUnits\":\"" + String(globalConfigSettings.poolUnitsYards ? "yards" : "meters") + "\",";
  json += "\"underwatersEnabled\":" + String(globalConfigSettings.underwatersEnabled ? "true" : "false") + ",";
  json += "\"delayIndicatorsEnabled\":" + String(globalConfigSettings.delayIndicatorsEnabled ? "true" : "false") + ",";

  // Include underwater colors stored in Preferences as hex strings (fallbacks match drawUnderwaterZone())
  String underwaterHex;
  RGBtoHex(globalConfigSettings.underwaterColorRed, globalConfigSettings.underwaterColorGreen, globalConfigSettings.underwaterColorBlue, underwaterHex);
  String surfaceHex;
  RGBtoHex(globalConfigSettings.underwaterSurfaceColorRed, globalConfigSettings.underwaterSurfaceColorGreen, globalConfigSettings.underwaterSurfaceColorBlue, surfaceHex);
  json += "\"underwaterColor\":\"" + underwaterHex + "\",";
  json += "\"surfaceColor\":\"" + surfaceHex + "\"}";

  server.send(200, "application/json", json);
}

void handleSetBrightness() {
  if (server.hasArg("brightness")) {
    globalConfigSettings.brightness = server.arg("brightness").toInt();
    if (DEBUG_ENABLED) {
      Serial.print("handleSetBrightness: updated brightness=");
      Serial.println(globalConfigSettings.brightness);
    }
    FastLED.setBrightness(globalConfigSettings.brightness);
    saveGlobalConfigSettings();
    server.send(200, "text/plain", "Brightness updated");
  } else {
    server.send(400, "text/plain", "Missing brightness parameter");
  }
}

void handleSetPulseWidth() {
  if (server.hasArg("pulseWidth")) {
    globalConfigSettings.pulseWidthFeet = server.arg("pulseWidth").toFloat();
    if (DEBUG_ENABLED) {
      Serial.print("handleSetPulseWidth: updated pulseWidthFeet=");
      Serial.println(globalConfigSettings.pulseWidthFeet);
    }
    saveGlobalConfigSettings();
    server.send(200, "text/plain", "Pulse width updated");
  } else {
    server.send(400, "text/plain", "Missing pulseWidth parameter");
  }
}

void handleSetStripLength() {
  if (server.hasArg("stripLengthMeters")) {
    globalConfigSettings.stripLengthMeters = server.arg("stripLengthMeters").toFloat();
    if (DEBUG_ENABLED) {
      Serial.print("handleSetStripLength: updated stripLengthMeters=");
      Serial.println(globalConfigSettings.stripLengthMeters);
    }
    saveGlobalConfigSettings();
    needsRecalculation = true;
    server.send(200, "text/plain", "Strip length updated");
  } else {
    server.send(400, "text/plain", "Missing stripLengthMeters parameter");
  }
}

void handleSetNumLedStrips() {
  if (server.hasArg("lane") && server.hasArg("numLedStrips")) {
    int lane = server.arg("lane").toInt();
    if (lane >= 0 && lane < MAX_LANES_SUPPORTED) {
      globalConfigSettings.numLedStrips[lane] = server.arg("numLedStrips").toInt();
      if (DEBUG_ENABLED) {
        Serial.print("handleSetNumLedStrips: updated numLedStrips=");
        Serial.println(globalConfigSettings.numLedStrips[lane]);
      }
      saveGlobalConfigSettings();
      needsRecalculation = true;
      server.send(200, "text/plain", "Number of LED strips updated");
    } else {
      server.send(400, "text/plain", "Invalid lane parameter");
    }
  } else {
    server.send(400, "text/plain", "Missing lane or numLedStrips parameter");
  }
}

void handleSetGapBetweenStrips() {
  if (server.hasArg("gapBetweenStrips")) {
    globalConfigSettings.gapBetweenStrips = server.arg("gapBetweenStrips").toInt();
    if (DEBUG_ENABLED) {
      Serial.print("handleSetGapBetweenStrips: updated gapBetweenStrips=");
      Serial.println(globalConfigSettings.gapBetweenStrips);
    }
    saveGlobalConfigSettings();
    needsRecalculation = true;
    server.send(200, "text/plain", "Gap between strips updated");
  } else {
    server.send(400, "text/plain", "Missing gapBetweenStrips parameter");
  }
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
    if (DEBUG_ENABLED) {
      Serial.print("handleSetPoolLength: updated poolLength=");
      Serial.print(globalConfigSettings.poolLength);
      Serial.println(globalConfigSettings.poolUnitsYards ? " yards" : " meters");
    }

    saveGlobalConfigSettings();
    needsRecalculation = true;
    // Update laps per round instead of full reinitialization
    updateSwimmersLapsPerRound();
    server.send(200, "text/plain", "Pool length updated");
  } else {
    server.send(400, "text/plain", "Missing poolLength or poolLengthUnits parameter");
  }
}

void handleSetLedsPerMeter() {
  if (server.hasArg("ledsPerMeter")) {
    globalConfigSettings.ledsPerMeter = server.arg("ledsPerMeter").toInt();
    if (DEBUG_ENABLED) {
      Serial.print("handleSetLedsPerMeter: updated ledsPerMeter=");
      Serial.println(globalConfigSettings.ledsPerMeter);
    }
    saveGlobalConfigSettings();
    needsRecalculation = true;
    server.send(200, "text/plain", "LEDs per meter updated");
  } else {
    server.send(400, "text/plain", "Missing ledsPerMeter parameter");
  }
}

void handleSetNumLanes() {
  if (server.hasArg("numLanes")) {;
    globalConfigSettings.numLanes = server.arg("numLanes").toInt();
    if (DEBUG_ENABLED) {
      Serial.print("handleSetNumLanes: updated numLanes=");
      Serial.println(globalConfigSettings.numLanes);
    }
    saveGlobalConfigSettings();
    server.send(200, "text/plain", "Number of lanes updated");
  } else {
    server.send(400, "text/plain", "Missing numLanes parameter");
  }
}

void handleSetSwimTime() {
  if (server.hasArg("swimTime")) {
    swimSetSettings.swimTimeSeconds = server.arg("swimTime").toInt();
    if (DEBUG_ENABLED) {
      Serial.print("handleSetSwimTime: updated swimTime=");
      Serial.println(swimSetSettings.swimTimeSeconds);
    }
    saveSwimSetSettings();
    server.send(200, "text/plain", "Swim time updated");
  } else {
    server.send(400, "text/plain", "Missing swimTime parameter");
  }
}

void handleSetRestTime() {
  if (server.hasArg("restTime")) {
    swimSetSettings.restTimeSeconds = server.arg("restTime").toInt();
    if (DEBUG_ENABLED) {
      Serial.print("handleSetRestTime: updated restTime=");
      Serial.println(swimSetSettings.restTimeSeconds);
    }
    saveSwimSetSettings();
    server.send(200, "text/plain", "Rest time updated");
  } else {
    server.send(400, "text/plain", "Missing restTime parameter");
  }
}

void handleSetSwimDistance() {
  if (server.hasArg("swimDistance")) {
    swimSetSettings.swimSetDistance = server.arg("swimDistance").toInt();
    if (DEBUG_ENABLED) {
      Serial.print("handleSetSwimDistance: updated swimDistance=");
      Serial.println(swimSetSettings.swimSetDistance);
    }
    saveSwimSetSettings();
    server.send(200, "text/plain", "Swim distance updated");
  } else {
    server.send(400, "text/plain", "Missing swimDistance parameter");
  }
}

void handleSetSwimmerInterval() {
  if (server.hasArg("swimmerInterval")) {
    swimSetSettings.swimmerIntervalSeconds = server.arg("swimmerInterval").toInt();
    if (DEBUG_ENABLED) {
      Serial.print("handleSetSwimmerInterval: updated swimmerInterval=");
      Serial.println(swimSetSettings.swimmerIntervalSeconds);
    }
    saveSwimSetSettings();
    server.send(200, "text/plain", "Swimmer interval updated");
  } else {
    server.send(400, "text/plain", "Missing swimmerInterval parameter");
  }
}

void handleSetDelayIndicators() {
  if (server.hasArg("enabled")) {
    globalConfigSettings.delayIndicatorsEnabled = (server.arg("enabled") == "true");
    if (DEBUG_ENABLED) {
      Serial.print("handleSetDelayIndicators: updated delayIndicatorsEnabled=");
      Serial.println(globalConfigSettings.delayIndicatorsEnabled);
    }
    saveGlobalConfigSettings();
    server.send(200, "text/plain", "Delay indicators updated");
  } else {
    server.send(400, "text/plain", "Missing enabled parameter");
  }
}

void handleSetNumSwimmers() {
  if (server.hasArg("numSwimmers")) {
    int lane = server.arg("lane").toInt();
    if (lane >= 0 && lane < MAX_LANES_SUPPORTED) {
      globalConfigSettings.numSwimmersPerLane[lane] = server.arg("numSwimmers").toInt();
      if (DEBUG_ENABLED) {
        Serial.print("handleSetNumSwimmers: updated numSwimmers for lane "); Serial.print(lane);
        Serial.print(" to "); Serial.println(globalConfigSettings.numSwimmersPerLane[lane]);
      }
      saveGlobalConfigSettings();
      server.send(200, "text/plain", "Number of swimmers updated");
    } else {
      server.send(400, "text/plain", "Invalid lane parameter");
    }
  } else {
    server.send(400, "text/plain", "Missing numSwimmers parameter");
  }
}

void handleSetNumRounds() {
  if (server.hasArg("numRounds")) {
    swimSetSettings.numRounds = server.arg("numRounds").toInt();
    if (DEBUG_ENABLED) {
      Serial.print("handleSetNumRounds: updated numRounds=");
      Serial.println(swimSetSettings.numRounds);
    }
    saveSwimSetSettings();
    server.send(200, "text/plain", "Number of numRounds updated");
  } else {
    server.send(400, "text/plain", "Missing numRounds parameter");
  }
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

void RGBtoHex(uint8_t r, uint8_t g, uint8_t b, String& hexColor) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
  hexColor = String(buf);
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
    // Update globalConfigSettings based on color mode
    globalConfigSettings.sameColorMode = (server.arg("colorMode") == "same");
    if (DEBUG_ENABLED) {
      Serial.print("handleSetColorMode: updated sameColorMode=");
      Serial.println(globalConfigSettings.sameColorMode);
    }
    saveGlobalConfigSettings();

    // Update ALL swimmer colors based on new mode (without resetting position/timing)
    for (int lane = 0; lane < MAX_LANES_SUPPORTED; lane++) {
      for (int i = 0; i < MAX_SWIMMERS_SUPPORTED; i++) {
        if (globalConfigSettings.sameColorMode) {
          // Same mode: all swimmers get the default color
          swimmers[lane][i].color =
            CRGB(globalConfigSettings.colorRed,
              globalConfigSettings.colorGreen,
              globalConfigSettings.colorBlue);
        } else {
          // Individual mode: each swimmer gets their predefined color
          swimmers[lane][i].color = swimmerColors[i];
        }
      }
    }
    server.send(200, "text/plain", "Color mode updated");
  } else {
    server.send(400, "text/plain", "Missing colorMode parameter");
  }
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
    if (DEBUG_ENABLED) {
      Serial.print("handleSetSwimmerColor: updated color=[");
      Serial.print(r); Serial.print(",");
      Serial.print(g); Serial.print(",");
      Serial.print(b); Serial.println("]");
    }
    saveGlobalConfigSettings();

    // If in "same color" mode, update ALL swimmers to use this color
    if (globalConfigSettings.sameColorMode) {
      CRGB newColor = CRGB(r, g, b);
      for (int lane = 0; lane < MAX_LANES_SUPPORTED; lane++) {
        for (int i = 0; i < MAX_SWIMMERS_SUPPORTED; i++) {
          swimmers[lane][i].color = newColor;
        }
      }
    }
    // If in "individual" mode, don't update swimmers - they keep their individual colors
    server.send(200, "text/plain", "Swimmer color updated");
  } else {
    server.send(400, "text/plain", "Missing color parameter");
  }
}

void handleSetSwimmerColors() {
  if (server.hasArg("lane") && server.hasArg("colors")) {
    int lane = server.arg("lane").toInt();
    if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
      server.send(400, "text/plain", "Invalid lane parameter");
      return;
    }
    String colorsString = server.arg("colors");

    // Parse comma-separated hex colors
    int colorIndex = 0;
    int startIndex = 0;

    for (int i = 0; i <= colorsString.length() &&  colorIndex < MAX_SWIMMERS_SUPPORTED; i++) {
      if (i == colorsString.length() || colorsString.charAt(i) == ',') {
        String hexColor = colorsString.substring(startIndex, i);
        hexColor.trim();

        if (hexColor.length() > 0) {
          uint8_t r, g, b;
          hexToRGB(hexColor, r, g, b);

          // Update this specific swimmer's color for the current lane
          CRGB newColor = CRGB(r, g, b);
          if (colorIndex < MAX_SWIMMERS_SUPPORTED) {
            swimmers[lane][colorIndex].color = newColor;
          }

          colorIndex++;
        }
        startIndex = i + 1;
      }
    }
    // Note: This does NOT update default globalConfigSettings - it's for individual customization only
    server.send(200, "text/plain", "Individual swimmer colors updated");
  } else {
    server.send(400, "text/plain", "Missing colors parameter");
  }
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
        globalConfigSettings.underwatersSizeFeet = server.arg("lightSize").toFloat();
        calculateUnderwatersSize();
      }

      // Update colors
      if (server.hasArg("underwaterColor") && server.hasArg("surfaceColor")) {
        String underwaterHex = server.arg("underwaterColor");
        String surfaceHex = server.arg("surfaceColor");

        // store global RGB values
        uint8_t r, g, b;
        hexToRGB(underwaterHex, r, g, b);
        globalConfigSettings.underwaterColorRed = r;
        globalConfigSettings.underwaterColorGreen = g;
        globalConfigSettings.underwaterColorBlue = b;

        hexToRGB(surfaceHex, r, g, b);
        globalConfigSettings.underwaterSurfaceColorRed = r;
        globalConfigSettings.underwaterSurfaceColorGreen = g;
        globalConfigSettings.underwaterSurfaceColorBlue = b;

        calculateUnderwatersColors();
      }
    }

    saveGlobalConfigSettings();
  } else {
    Serial.println(false);
  }
  server.send(200, "text/plain", "Underwater globalConfigSettings updated");
}

// Helper: parse a client-supplied UniqueId string as hex (expects lowercase hex or mixed case, no "0x" required)
// Returns 0 when parse fails (0 reserved meaning "no uniqueId")
static unsigned long long parseUniqueIdHex(const String &s) {
  if (s.length() == 0) return 0ULL;
  // strtoull with base 16 will parse both lower/upper-case hex digits; ignore any leading 0x if present
  const char *c = s.c_str();
  // skip optional "0x" or "0X"
  if (s.startsWith("0x") || s.startsWith("0X")) c += 2;
  unsigned long long v = (unsigned long long)strtoull(c, NULL, 16);
  return v;
}

// Helper: format unsigned long long as lowercase hex string (no 0x)
static String uniqueIdToHex(unsigned long long v) {
  if (v == 0ULL) return String("");
  char buf[32];
  // %llx prints lowercase hex
  sprintf(buf, "%llx", (unsigned long long)v);
  return String(buf);
}

void handleEnqueueSwimSet() {
  Serial.println("/enqueueSwimSet ENTER");
  // Read raw body
  String body = server.arg("plain");
  int lane = parseLaneFromBody(body);
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
    return;
  }
  SwimSet s;
  s.numRounds = (uint8_t)extractJsonLong(body, "numRounds", swimSetSettings.numRounds);
  s.swimDistance = (uint16_t)extractJsonLong(body, "swimDistance", swimSetSettings.swimSetDistance);
  s.swimTime = (uint16_t)extractJsonLong(body, "swimTime", swimSetSettings.swimTimeSeconds);
  s.restTime = (uint16_t)extractJsonLong(body, "restTime", swimSetSettings.restTimeSeconds);
  s.swimmerInterval = (uint16_t)extractJsonLong(body, "swimmerInterval", swimSetSettings.swimmerIntervalSeconds);
  s.type = (uint8_t)extractJsonLong(body, "type", SWIMSET_TYPE);
  s.repeat = (uint8_t)extractJsonLong(body, "repeat", 0);
  s.uniqueId = 0ULL;
  s.status = SWIMSET_STATUS_PENDING;

  String uniqueIdStr = extractJsonString(body, "uniqueId", "");
  if (uniqueIdStr.length() > 0) {
    unsigned long long parsed = parseUniqueIdHex(uniqueIdStr);
    if (parsed != 0ULL) {
      s.uniqueId = parsed;
    } else {
      Serial.println(" Warning: invalid uniqueId format: " + uniqueIdStr);
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid uniqueId format\"}");
      return;
    }
  } else {
    Serial.println(" Warning: missing uniqueId");
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing uniqueId\"}");
    return;
  }

  int result = enqueueSwimSet(s, lane);
  Serial.println(" Enqueue result: " + String((result == QUEUE_INSERT_SUCCESS) ? "OK" : "FAILED"));
  Serial.println("  Enqueued swim set: ");
  Serial.println("   lane=" + String(lane));
  Serial.println("   numRounds=" + String(s.numRounds));
  Serial.println("   swimDistance=" + String(s.swimDistance));
  Serial.println("   swimTime=" + String(s.swimTime));
  Serial.println("   restTime=" + String(s.restTime));
  Serial.println("   swimmerInterval=" + String(s.swimmerInterval));
  Serial.println("   type=" + String(s.type));
  Serial.println("   repeat=" + String(s.repeat));
  Serial.println("   uniqueId=" + uniqueIdToHex(s.uniqueId));

  if (result == QUEUE_INSERT_SUCCESS) {
    int qCount = swimSetQueueCount[lane];
    String json = "{";
    json += "\"ok\":true,";
    json += "\"uniqueId\":\"" + uniqueIdToHex(s.uniqueId) + "\"";
    json += "}";
    server.send(200, "application/json", json);
  } else {
    server.send(507, "application/json", "{\"ok\":false,\"errorCode\":" + String(result) + "}");
  }
}

// Update an existing swim set in-place by device id or uniqueId
// Body may include matchId (device id) or matchUniqueId (string) to locate
// the queued entry. The fields length, swimTime, numRounds, restTime, type, repeat
// will be used to replace the entry. Optionally provide uniqueId to update the
// stored uniqueId for future reconciliation.
void handleUpdateSwimSet() {
  Serial.println("/updateSwimSet ENTER");
  String body = server.arg("plain");
  int lane = parseLaneFromBody(body);
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
    return;
  }
  Serial.println(" Raw body:");
  Serial.println(body);

  // Parse required fields from JSON body (no form-encoded fallback)
  SwimSet s;
  s.numRounds = (uint8_t)extractJsonLong(body, "numRounds", swimSetSettings.numRounds);
  s.swimDistance = (uint16_t)extractJsonLong(body, "swimDistance", swimSetSettings.swimSetDistance);
  s.swimTime = (uint16_t)extractJsonLong(body, "swimTime", swimSetSettings.swimTimeSeconds);
  s.restTime = (uint16_t)extractJsonLong(body, "restTime", swimSetSettings.restTimeSeconds);
  s.swimmerInterval = (uint16_t)extractJsonLong(body, "swimmerInterval", swimSetSettings.swimmerIntervalSeconds);
  s.type = (uint8_t)extractJsonLong(body, "type", 0);
  s.repeat = (uint8_t)extractJsonLong(body, "repeat", 0);

  String uniqueIdStr = extractJsonString(body, "uniqueId", "");
  unsigned long long uniqueId = 0ULL;
  if (uniqueIdStr.length() > 0) {
    uniqueId = parseUniqueIdHex(uniqueIdStr);
  } else {
    Serial.println(" Warning: missing uniqueId");
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing uniqueId\"}");
    return;
  }


  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid lane\"}");
    return;
  }

  Serial.println("  received payload to update set: ");
  Serial.print(" numRounds="); Serial.println(s.numRounds);
  Serial.print(" swimDistance="); Serial.println(s.swimDistance);
  Serial.print(" swimTime="); Serial.println(s.swimTime);
  Serial.print(" restTime="); Serial.println(s.restTime);
  Serial.print(" swimmerInterval="); Serial.println(s.swimmerInterval);
  Serial.print(" type="); Serial.println(s.type);
  Serial.print(" repeat="); Serial.println(s.repeat);
  Serial.print(" uniqueId="); Serial.println(uniqueIdToHex(uniqueId));

  Serial.println(" Searching queue for matching entry...");
    // Find matching entry in the lane queue and update fields
  bool found = false;
  for (int i = 0; i < swimSetQueueCount[lane]; i++) {
    int idx = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
    SwimSet &entry = swimSetQueue[lane][idx];

    if (uniqueId != 0 && entry.uniqueId == uniqueId) {
      entry.numRounds = s.numRounds;
      entry.swimDistance = s.swimDistance;
      entry.swimTime = s.swimTime;
      entry.restTime = s.restTime;
      entry.swimmerInterval = s.swimmerInterval;
      entry.type = s.type;
      entry.repeat = s.repeat;
      found = true;
    }

    if (found) {
      // Respond with updated canonical info + current lane queue for immediate reconciliation
      String json = "{";
      json += "\"ok\":true";
      json += "}";
      server.send(200, "application/json", json);
      return;
    }
  } // end for

  Serial.println(" No matching entry found in queue");
  server.send(404, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
}

// Delete a swim set by device id or uniqueId for a lane.
// Accepts form-encoded or JSON: matchId=<n> or matchUniqueId=<hex>, and lane=<n>
void handleDeleteSwimSet() {
  Serial.println("/deleteSwimSet ENTER");
  String body = server.arg("plain");
  int lane = parseLaneFromBody(body);
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
    return;
  }
  Serial.println(" Received raw body: " + body);

  // Expect application/json body
  if (body.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
    return;
  }

  unsigned long long matchUniqueId = 0ULL;
  String matchUniqueIdStr = extractJsonString(body, "matchUniqueId", "");
  if (matchUniqueIdStr.length() > 0) {
    matchUniqueId = parseUniqueIdHex(matchUniqueIdStr);
  } else {
    Serial.println(" Warning: missing matchUniqueId");
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing matchUniqueId\"}");
    return;
  }

  // Validate lane...
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    Serial.println(" Invalid lane");
    server.send(400, "application/json", "{\"ok\":false, \"error\":\"Invalid lane\"}");
    return;
  }
  Serial.println("  lane=" + String(lane));
  Serial.print("  matchUniqueId="); Serial.println(matchUniqueIdStr);

  // Build vector of existing entries
  std::vector<SwimSet> existing;
  for (int i = 0; i < swimSetQueueCount[lane]; i++) {
    int idx = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
    existing.push_back(swimSetQueue[lane][idx]);
  }

  // Try match by numeric id
  bool found = false;
  // Try match by uniqueId (hex or decimal)
  if (!found && matchUniqueId != 0ULL) {
    for (size_t i = 0; i < existing.size(); i++) {
      if (existing[i].uniqueId == matchUniqueId) {
        Serial.println(" deleteSwimSet: matched by uniqueId=" + String((unsigned long long)existing[i].uniqueId));
        existing.erase(existing.begin() + i);
        found = true;
        break;
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

  // Perform removal logic (match by id or uniqueId) and respond with JSON
  if (found) {
    Serial.print(" deleteSwimSet: deleted entry, new queue count=");
    Serial.println(swimSetQueueCount[lane]);
    server.send(200, "application/json", "{\"ok\":true, \"message\":\"deleted\"}");
  } else {
    Serial.print(" deleteSwimSet: no matching entry found, nothing to delete, queue count=");
    Serial.println(String(swimSetQueueCount[lane]));
    server.send(200, "application/json", "{\"ok\":true, \"message\":\"not found\"}");
  }
}

void handleStartSwimSet() {
  Serial.println("/startSwimSet ENTER");
  String body = server.arg("plain");
  int lane = parseLaneFromBody(body);
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
    return;
  }

  // If client specified matchUniqueId, try to find that entry in the lane queue.
  String matchUniqueIdStr = extractJsonString(body, "matchUniqueId", "");
  unsigned long long matchUniqueId = 0ULL;
  if (matchUniqueIdStr.length() > 0) {
    matchUniqueId = parseUniqueIdHex(matchUniqueIdStr);
  } else {
    Serial.println(" Warning: missing matchUniqueId");
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing matchUniqueId\"}");
    return;
  }

  SwimSet s;
  bool haveSet = false;
  int matchedQueueIndex = -1; // relative to head (0 = head)

  // Search queue for matching entry
  for (int i = 0; i < swimSetQueueCount[lane]; i++) {
    int idx = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
    SwimSet &entry = swimSetQueue[lane][idx];
    if (entry.uniqueId == matchUniqueId) {
      s = entry;
      haveSet = true;
      matchedQueueIndex = i;
      break;
    }
  }

  // If still no set, fall back to the head-of-queue for this lane (if any)
  if (!haveSet) {
    if (swimSetQueueCount[lane] > 0) {
      int idx = swimSetQueueHead[lane] % SWIMSET_QUEUE_MAX;
      s = swimSetQueue[lane][idx];
      haveSet = true;
      matchedQueueIndex = 0;
    }
  }

  if (!haveSet) {
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"no swim set available\"}");
    return;
  }

  // record which queue index we started (will be used by applySwimSetToSettings)
  laneActiveQueueIndex[lane] = matchedQueueIndex;
  Serial.println("/startSwimSet: calling applySwimSetToSettings()");
  applySwimSetToSettings(s, lane);

  // Respond with canonical id (if any) so client can reconcile
  String resp = "{";
  resp += "\"ok\":true,";
  resp += "\"startedId\":" + String(s.id) + ",";
  resp += "\"uniqueId\":\"" + uniqueIdToHex(s.uniqueId) + "\"";
  resp += "}";
  server.send(200, "application/json", resp);
}

void handleStopSwimSet() {// determine lane from query/form or JSON body
  int lane = -1;
  if (server.hasArg("lane")) {
      // query param or form-encoded POST
      lane = server.arg("lane").toInt();
  } else {
      // attempt to parse JSON body: {"lane":N}
      String body = server.arg("plain");
      if (body.length() > 0) {
          lane = (int)extractJsonLong(body, "lane", -1);
      }
  }

  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
      return;
  }
  Serial.print("handleStopSwimSet: for lane: ");
  Serial.println(lane);
  // Toggle the specified lane's running state
  globalConfigSettings.laneRunning[lane] = false;

  // Set any active swim sets to stopped
  int qidx = laneActiveQueueIndex[lane];
  if (qidx >= 0 && qidx < swimSetQueueCount[lane]) {
    int actualIdx = (swimSetQueueHead[lane] + qidx) % SWIMSET_QUEUE_MAX;
    swimSetQueue[lane][actualIdx].status &= ~SWIMSET_STATUS_ACTIVE;
    swimSetQueue[lane][actualIdx].status |= SWIMSET_STATUS_STOPPED;
  }

  // Clear lights on this lane to black
  Serial.println("filling lane with all black LEDs");
  if (scanoutLEDs[lane] != nullptr && visibleLEDs[lane] > 0) {
    fill_solid(scanoutLEDs[lane], visibleLEDs[lane], CRGB::Black);
  }
  if (renderedLEDs[lane] != nullptr && fullLengthLEDs[lane] > 0) {
    fill_solid(renderedLEDs[lane], fullLengthLEDs[lane], CRGB::Black);
  }
  // Immediately push the black frame to the strips so the lane clears even if no other lanes are running
  FastLED.show();

  // Update global running state (true if any lane is running)
  globalConfigSettings.isRunning = false;
  for (int i = 0; i < globalConfigSettings.numLanes; i++) {
    if (globalConfigSettings.laneRunning[i]) {
      globalConfigSettings.isRunning = true;
      break;
    }
  }

  saveGlobalConfigSettings();

  String status = "Lane " + String(lane) + " Stopped";
  server.send(200, "text/plain", status);
}

void handleGetSwimQueue() {
  //Serial.println("/getSwimQueue ENTER");
  String body = server.arg("plain");
  int lane = parseLaneFromBody(body);
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
    return;
  }

  // Use ArduinoJson to build the response to avoid any string-concatenation bugs.
  // Adjust the DynamicJsonDocument size if you have very large queues.
  const size_t DOC_SIZE = 16 * 1024;
  DynamicJsonDocument doc(DOC_SIZE);

  // Build queue array for the current lane
  JsonArray q = doc.createNestedArray("queue");
  for (int i = 0; i < swimSetQueueCount[lane]; i++) {
    int idx = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
    SwimSet &s = swimSetQueue[lane][idx];
    JsonObject item = q.createNestedObject();
    item["id"] = (unsigned long)s.id;
    item["uniqueId"] = uniqueIdToHex(s.uniqueId);
    item["numRounds"] = s.numRounds;
    item["swimDistance"] = s.swimDistance;
    item["swimTime"] = s.swimTime;
    item["restTime"] = s.restTime;
    item["swimmerInterval"] = s.swimmerInterval;
    item["type"] = s.type;
    item["repeat"] = s.repeat;
    // numeric status bitmask
    item["status"] = (int)s.status;

    // representative currentRound: prefer swimmer whose queueIndex == i
    int repCurrentRound = 0;
    int laneSwimmerCount = globalConfigSettings.numSwimmersPerLane[lane];
    if (laneSwimmerCount < 1) laneSwimmerCount = 1;
    if (laneSwimmerCount > MAX_SWIMMERS_SUPPORTED) laneSwimmerCount = MAX_SWIMMERS_SUPPORTED;
    for (int si = 0; si < laneSwimmerCount; si++) {
      if (swimmers[lane][si].queueIndex == i && swimmers[lane][si].hasStarted) {
        repCurrentRound = swimmers[lane][si].currentRound;
        break;
      }
    }
    item["currentRound"] = repCurrentRound;
  }

  // Build status object
  JsonObject status = doc.createNestedObject("status");
  status["isRunning"] = globalConfigSettings.isRunning ? true : false;

  // laneRunning array
  JsonArray laneRunningArr = status.createNestedArray("laneRunning");
  for (int li = 0; li < MAX_LANES_SUPPORTED; li++) {
    laneRunningArr.add(globalConfigSettings.laneRunning[li] ? true : false);
  }

  // Serialize and send
  String out;
  serializeJson(doc, out);

  if (DEBUG_ENABLED) {
    //Serial.println(" getSwimQueue -> payload:");
    //Serial.println(out);
  }

  // Return combined payload { queue: [...], status: {...} }
  server.send(200, "application/json", out);
}

// Reorder swim queue for a lane. Expects form-encoded: lane=<n>&order=<id1,id2,...>
// id values can be device ids (numeric) or uniqueId strings. Entries not matched
// will be appended at the end in their existing order.
void handleReorderSwimQueue() {
  Serial.println("/reorderSwimQueue ENTER");
  String body = server.arg("plain");
  int lane = parseLaneFromBody(body);
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
    return;
  }

  // Extract order parameter - attempt to find "order": or order= in form-encoded
  String orderStr = "";
  if (body.indexOf("order=") >= 0) {
    int idx = body.indexOf("order=");
    int start = idx + 6; // TODO: What does this 6 represent? No MAGIC NUMBERS
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
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing/invalid order\"}");
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

  // Helper to match token to an existing entry by id or uniqueId
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
    // Try parsing as hex uniqueId
    unsigned long long parsedHex = (unsigned long long)strtoull(tok.c_str(), NULL, 16);
    for (size_t i = 0; i < existing.size(); i++) {
      if (parsedHex != 0 && existing[i].uniqueId == parsedHex) {
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

  String json = "{";
    json += "\"ok\":true";
    json += "}";
  server.send(200, "application/json", json);
}

// Reset swimmers for a given lane to starting wall (position=0, direction=1)
void handleResetLane() {
  String body = server.arg("plain");
  int lane = parseLaneFromBody(body);
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
    return;
  }

  // Reset swimmers for this lane
  for (int i = 0; i < MAX_SWIMMERS_SUPPORTED; i++) {
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
}

// End of Web Handlers ******************************************

void saveGlobalConfigSettings() {
  preferences.putFloat("numLanes", globalConfigSettings.numLanes);
  preferences.putFloat("poolLength", globalConfigSettings.poolLength);
  preferences.putBool("poolUnitsYards", globalConfigSettings.poolUnitsYards);
  preferences.putFloat("stripLengthM", globalConfigSettings.stripLengthMeters);
  preferences.putInt("ledsPerMeter", globalConfigSettings.ledsPerMeter);
  for (int li = 0; li < MAX_LANES_SUPPORTED; li++) {
    String key = String("numLedStrips") + String(li);
    preferences.putInt(key.c_str(), globalConfigSettings.numLedStrips[li]);
  }
  preferences.putFloat("gapBetweenStrips", globalConfigSettings.gapBetweenStrips);
  preferences.putFloat("pulseWidthFeet", globalConfigSettings.pulseWidthFeet);
  for (int li = 0; li < MAX_LANES_SUPPORTED; li++) {
    String key = String("swimLane") + String(li);
    preferences.putInt(key.c_str(), globalConfigSettings.numSwimmersPerLane[li]);
  }
  preferences.putUChar("colorRed", globalConfigSettings.colorRed);
  preferences.putUChar("colorGreen", globalConfigSettings.colorGreen);
  preferences.putUChar("colorBlue", globalConfigSettings.colorBlue);
  preferences.putUChar("brightness", globalConfigSettings.brightness);
  preferences.putBool("isRunning", globalConfigSettings.isRunning);
  preferences.putBool("sameColorMode", globalConfigSettings.sameColorMode);

  preferences.putBool("uwEn", globalConfigSettings.underwatersEnabled);
  preferences.putFloat("uwSizeFeet", globalConfigSettings.underwatersSizeFeet);
  preferences.putFloat("uwFirDistFeet", globalConfigSettings.firstUnderwaterDistanceFeet);
  preferences.putFloat("uwDistFeet", globalConfigSettings.underwaterDistanceFeet);
  preferences.putFloat("uwHideSecs", globalConfigSettings.hideAfterSeconds);
  preferences.putUChar("uwColorR", globalConfigSettings.underwaterColorRed);
  preferences.putUChar("uwColorG", globalConfigSettings.underwaterColorGreen);
  preferences.putUChar("uwColorB", globalConfigSettings.underwaterColorBlue);
  preferences.putUChar("uwSurfColR", globalConfigSettings.underwaterSurfaceColorRed);
  preferences.putUChar("uwSurfColG", globalConfigSettings.underwaterSurfaceColorGreen);
  preferences.putUChar("uwSurfColB", globalConfigSettings.underwaterSurfaceColorBlue);
}

void saveSwimSetSettings() {
  // Persist speed in meters per second
  preferences.putFloat("speedMPS", swimSetSettings.speedMetersPerSecond);
  preferences.putInt("swimTimeSeconds", swimSetSettings.swimTimeSeconds);
  preferences.putInt("restTimeSeconds", swimSetSettings.restTimeSeconds);
  preferences.putInt("swimSetDistance", swimSetSettings.swimSetDistance);
  // Sanitize swimmer interval before persisting to avoid storing invalid (<=0) values
  int si = swimSetSettings.swimmerIntervalSeconds;
  if (si <= 0) {
    Serial.println("saveSwimSetSettings: invalid swimmerIntervalSeconds <= 0, resetting to 4");
    si = 4; // enforce safe default
  }
  preferences.putInt("swimmerInterval", si);
  preferences.putInt("numRounds", swimSetSettings.numRounds);
}

void saveSettings() {
  saveGlobalConfigSettings();
  saveSwimSetSettings();
}

void loadGlobalConfigSettings() {
  globalConfigSettings.numLanes = preferences.getFloat("numLanes", 1);  // 1 lane default
  globalConfigSettings.poolLength = preferences.getFloat("poolLength", 25.0);  // 25 yards default
  globalConfigSettings.poolUnitsYards = preferences.getBool("poolUnitsYards", true);  // Default to yards
  globalConfigSettings.stripLengthMeters = preferences.getFloat("stripLengthM", 23.0);
  for (int li = 0; li < MAX_LANES_SUPPORTED; li++) {
    String key = String("numLedStrips") + String(li);
    globalConfigSettings.numLedStrips[li] = preferences.getInt(key.c_str(), 1);
  }
  globalConfigSettings.gapBetweenStrips = preferences.getFloat("gapBetweenStrips", 0.0);
  globalConfigSettings.ledsPerMeter = preferences.getInt("ledsPerMeter", 30);
  globalConfigSettings.pulseWidthFeet = preferences.getFloat("pulseWidthFeet", 1.0);
  // Keep preference fallbacks consistent with struct defaults
  // Load per-lane counts if present (key changed to "swimLaneX" to fit 15-char NVS limit)
  Serial.print(" loadGlobalConfigSettings: numSwimmersPerLane:");
  for (int li = 0; li < MAX_LANES_SUPPORTED; li++) {
    String key = String("swimLane") + String(li);
    globalConfigSettings.numSwimmersPerLane[li] = preferences.getInt(key.c_str(), DEFAULT_NUM_SWIMMERS);
    Serial.print(" " + String(globalConfigSettings.numSwimmersPerLane[li]));
  }
  Serial.println();
  globalConfigSettings.colorRed = preferences.getUChar("colorRed", 0);      // Default to blue
  globalConfigSettings.colorGreen = preferences.getUChar("colorGreen", 0);
  globalConfigSettings.colorBlue = preferences.getUChar("colorBlue", 255);
  globalConfigSettings.brightness = preferences.getUChar("brightness", 196);
  globalConfigSettings.isRunning = preferences.getBool("isRunning", false);  // Default: stopped
  globalConfigSettings.sameColorMode = preferences.getBool("sameColorMode", false); // Default: individual colors feet

  // Load underwatersEnabled (key changed to "underwaterEn" to fit 15-char NVS limit)
  globalConfigSettings.underwatersEnabled = preferences.getBool("uwEn", false);
  globalConfigSettings.underwatersSizeFeet = preferences.getFloat("uwSizeFeet", 1.0);  // Default size in feet
  globalConfigSettings.firstUnderwaterDistanceFeet = preferences.getFloat("uwFirDistFeet", 5.0);
  globalConfigSettings.underwaterDistanceFeet = preferences.getFloat("uwDistFeet", 3.0);
  globalConfigSettings.hideAfterSeconds = preferences.getFloat("uwHideSecs", 1.0);
  globalConfigSettings.underwaterColorRed = preferences.getUChar("uwColorR", 0);
  globalConfigSettings.underwaterColorGreen = preferences.getUChar("uwColorG", 0);
  globalConfigSettings.underwaterColorBlue = preferences.getUChar("uwColorB", 255);
  globalConfigSettings.underwaterSurfaceColorRed = preferences.getUChar("uwSurfColR", 0);
  globalConfigSettings.underwaterSurfaceColorGreen = preferences.getUChar("uwSurfColG", 0);
  globalConfigSettings.underwaterSurfaceColorBlue = preferences.getUChar("uwSurfColB", 255);

  calculateUnderwatersSize();
  calculateUnderwatersColors();
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

void calculateUnderwatersSize() {
  // Calculate underwater light size in LEDs
  underwatersWidthLEDs = (int)(globalConfigSettings.underwatersSizeFeet * 30.48 * globalConfigSettings.ledsPerMeter / 100.0);
}

void calculateUnderwatersColors() {
  // Update CRGB color objects for underwater indicators
  underwaterColor = CRGB(globalConfigSettings.underwaterColorRed, globalConfigSettings.underwaterColorGreen, globalConfigSettings.underwaterColorBlue);
  underwaterSurfaceColor = CRGB(globalConfigSettings.underwaterSurfaceColorRed, globalConfigSettings.underwaterSurfaceColorGreen, globalConfigSettings.underwaterSurfaceColorBlue);
}

void recalculateValues(int lane) {
  Serial.print("recalculateValues: lane="); Serial.println(lane);

  if (lane >= globalConfigSettings.numLanes) {
    if (scanoutLEDs[lane] != nullptr) {
      delete[] scanoutLEDs[lane];
      scanoutLEDs[lane] = nullptr;
    }
    return;
  }

  // Calculate total LEDs first
  int ledsPerStrip = (int)(globalConfigSettings.stripLengthMeters * globalConfigSettings.ledsPerMeter);

  // Total number of rendered LEDs, including gaps
  // TODO: Does the official 5m length include the gaps or not?
  fullLengthLEDs[lane] = ledsPerStrip * globalConfigSettings.numLedStrips[lane];

  // Total number of visible LEDs, factoring out the gaps
  // First get number of LEDs per gap
  int ledsPerGap = (int)(globalConfigSettings.gapBetweenStrips * globalConfigSettings.ledsPerMeter / 100);
  // Then figure out total number of LEDs in all the gaps
  int totalMissingLEDs = ledsPerGap * (globalConfigSettings.numLedStrips[lane] - 1);
  // Subtract total missing LEDs from full length total
  if (visibleLEDs[lane] != fullLengthLEDs[lane] - totalMissingLEDs) {
    Serial.print(" visibleLEDs changed from "); Serial.print(visibleLEDs[lane]);
    Serial.print(" to "); Serial.println(fullLengthLEDs[lane] - totalMissingLEDs);
    // TODO: Should we reset FastLED?
  }
  visibleLEDs[lane] = fullLengthLEDs[lane] - totalMissingLEDs;

  // Get the individual index values of the LEDs that are in a gap
  gapLEDs[lane].clear();
  for (int i=1; i<globalConfigSettings.numLedStrips[lane]; i++) {
    int gapStartIndex = i * ledsPerStrip + (i - 1) * ledsPerGap;
    for (int j=0; j<ledsPerGap; j++) {
      gapLEDs[lane].push_back(gapStartIndex + j);
    }
  }

  // Calculate LED spacing
  ledSpacingCM = 100.0 / globalConfigSettings.ledsPerMeter;

  // Calculate pulse width in LEDs
  float pulseWidthCM = globalConfigSettings.pulseWidthFeet * 30.48;
  pulseWidthLEDs = (int)(pulseWidthCM / ledSpacingCM);



  // Calculate pool to strip ratio for timing adjustments
  float poolLengthInMeters = convertPoolToMeters(globalConfigSettings.poolLength);
  poolToStripRatio = poolLengthInMeters / globalConfigSettings.stripLengthMeters;

  // TODO: this should just be a constant, or be set based on the swim speed
  //       it doesn't need to be here in the LED setup
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
  Serial.println("################################");
  Serial.print("render delayMS="); Serial.println(delayMS);
  Serial.print("visibleLEDs="); Serial.println(visibleLEDs[lane]);
  Serial.print("fullLengthLEDs="); Serial.println(fullLengthLEDs[lane]);
  Serial.print("ledsPerStrip="); Serial.println(ledsPerStrip);
  Serial.print("ledsPerGap="); Serial.println(ledsPerGap);
  Serial.print("totalMissingLEDs="); Serial.println(totalMissingLEDs);
  Serial.print("gapLEDs count="); Serial.println(gapLEDs[lane].size());
  Serial.println("################################");

  // Allocate LED array dynamically for this lane
  if (scanoutLEDs[lane] != nullptr) {
    delete[] scanoutLEDs[lane];
  }
  scanoutLEDs[lane] = new CRGB[visibleLEDs[lane]];

  if (renderedLEDs[lane] != nullptr) {
    delete[] renderedLEDs[lane];
  }
  renderedLEDs[lane] = new CRGB[fullLengthLEDs[lane]];

  // Clear this lane's LEDs
  fill_solid(scanoutLEDs[lane], visibleLEDs[lane], CRGB::Black);

  // FastLED requires the pin number as a compile-time template parameter.
  // Use a switch so each call uses a literal pin constant (one per lane).
  switch (lane) {
    case 0:
      FastLED.addLeds<LED_TYPE, LANE_0_PIN, COLOR_ORDER>(scanoutLEDs[lane], visibleLEDs[lane]);
      break;
    case 1:
      FastLED.addLeds<LED_TYPE, LANE_1_PIN, COLOR_ORDER>(scanoutLEDs[lane], visibleLEDs[lane]);
      break;
    case 2:
      FastLED.addLeds<LED_TYPE, LANE_2_PIN, COLOR_ORDER>(scanoutLEDs[lane], visibleLEDs[lane]);
      break;
    case 3:
      FastLED.addLeds<LED_TYPE, LANE_3_PIN, COLOR_ORDER>(scanoutLEDs[lane], visibleLEDs[lane]);
      break;
    default:
      // fallback: do nothing for out-of-range lanes
      break;
  }
}

// ----- Targeted swimmer update helpers -----
// Update lapsPerRound for all swimmers when pool length or swimSet distance changes
// TODO BUG FIX: Who is calling this? This should be lane and swim set specific
void updateSwimmersLapsPerRound() {
  int lapsPerRound = (int)ceil((float)swimSetSettings.swimSetDistance / globalConfigSettings.poolLength);
  for (int lane = 0; lane < MAX_LANES_SUPPORTED; lane++) {
    for (int i = 0; i < MAX_SWIMMERS_SUPPORTED; i++) {
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

// TODO: Seems like the name is misleading since we are also updating swimmer positions here
void updateLEDEffect() {
  // Update LEDs for all active lanes
  for (int lane = 0; lane < globalConfigSettings.numLanes; lane++) {
    if (scanoutLEDs[lane] != nullptr && renderedLEDs[lane] != nullptr) {
      // Clear this lane's LEDs
      fill_solid(renderedLEDs[lane], fullLengthLEDs[lane], CRGB::Black);

      // Only animate if this lane is running
      if (globalConfigSettings.laneRunning[lane]) {
        // Use per-lane swimmer count (clamped to max swimmers)
        int laneSwimmerCount = globalConfigSettings.numSwimmersPerLane[lane];
        if (laneSwimmerCount < 1) laneSwimmerCount = 1;
        if (laneSwimmerCount > MAX_SWIMMERS_SUPPORTED) laneSwimmerCount = MAX_SWIMMERS_SUPPORTED;

        // Update all possible swimmers and draw each active swimmer for this lane
        for (int swimmerIndex = 0; swimmerIndex < MAX_SWIMMERS_SUPPORTED; swimmerIndex++) {
          // Update the position for all possible swimmers
          updateSwimmer(swimmerIndex, lane);

          // Only draw if this swimmer is active in this lane
          if (swimmerIndex < laneSwimmerCount) {
            drawSwimmerPulse(swimmerIndex, lane);

            // Draw underwater zone for this swimmer if enabled
            if (globalConfigSettings.underwatersEnabled) {
              drawUnderwaterZone(lane, swimmerIndex);
            }

            // Draw delay indicators for the first swimmer waiting if enabled for this lane/swimmer
            bool alreadyDrawnDelay = false;
            if (globalConfigSettings.delayIndicatorsEnabled && !alreadyDrawnDelay) {
              if (drawDelayIndicators(lane, swimmerIndex)) {
                alreadyDrawnDelay = true;
              }
            }
          }
        }

        // Splice out gap LEDs for this lane
        spliceOutGaps(lane);
      }
    }
  }

  // Update FastLED
  FastLED.show();
}

void spliceOutGaps(int lane) {
  //Serial.print("spliceOutGaps: lane="); Serial.println(lane);
  // If no gaps, copy exactly visibleLEDs[lane] entries (safe)
  if (gapLEDs[lane].size() == 0) {
    //Serial.println("No gaps, copying visible LEDs directly");
    memcpy(scanoutLEDs[lane], renderedLEDs[lane], visibleLEDs[lane] * sizeof(CRGB));
    return;
  } else {
    //Serial.print("Gaps present, count="); Serial.println(gapLEDs[lane].size());
  }

  // gapLEDs[lane] must be sorted ascending for this to work. Ensure it is.
  std::sort(gapLEDs[lane].begin(), gapLEDs[lane].end());

  int scanoutIndex = 0;
  size_t gapIndex = 0;
  const size_t gapCount = gapLEDs[lane].size();
  int nextGap = (gapCount > 0) ? gapLEDs[lane][0] : -1;

  for (int renderIndex = 0; renderIndex < fullLengthLEDs[lane] && scanoutIndex < visibleLEDs[lane]; ++renderIndex) {
    if (gapIndex < gapCount && renderIndex == nextGap) {
      // skip gap LED and advance to next gap
      ++gapIndex;
      nextGap = (gapIndex < gapCount) ? gapLEDs[lane][gapIndex] : -1;
      continue;
    }
    // copy valid LED to scanout
    scanoutLEDs[lane][scanoutIndex++] = renderedLEDs[lane][renderIndex];
  }

  // Defensive: if we didn't fill the scanout buffer, clear the remainder
  for (int i = scanoutIndex; i < visibleLEDs[lane]; ++i) {
    scanoutLEDs[lane][i] = CRGB::Black;
  }
}

bool drawDelayIndicators(int laneIndex, int swimmerIndex) {
  Swimmer* swimmer = &swimmers[laneIndex][swimmerIndex];

  // Only consider swimmers who are resting
  if (!swimmer->isResting || swimmer->finished) {
    return false;
  }

  unsigned long currentTime = millis();

  // Max delay distance in pool's native units
  // Convert 5 feet to pool's native units
  // TODO: This should be a global MACRO or constant (no magic numbers)
  long maxDelayInMilliseconds = (long)(5.0 * 1000.0);                   // Max delay of 5 seconds
  float maxDelayDistanceFeet = 1.0 * (maxDelayInMilliseconds / 1000.0); // delay indicator of 1 foot per second
  float maxDelayDistanceMeters = maxDelayDistanceFeet * 0.3048;
  int maxDelayLEDs = (int)(maxDelayDistanceMeters * globalConfigSettings.ledsPerMeter);

  // Check if this swimmer's delay is within the delay window
  float timeTillStartInMilliseconds = swimmer->expectedStartTime - currentTime;
  if (timeTillStartInMilliseconds > 0 && timeTillStartInMilliseconds < maxDelayInMilliseconds) {
    // We should draw this swimmer's delay indicator

    // Calculate remaining delay in seconds
    float remainingDelaySeconds = timeTillStartInMilliseconds / 1000.0;
    // Calculate remaining delay in # of LEDs
    // First calculate remaining delay in meters
    float remainingDelayDistanceMeters = (float)remainingDelaySeconds * 0.3048; // 1 foot per second
    // Now calculate remaining delay in LEDs based on ledsPerMeter
    int remainingDelayLEDs = (int)(remainingDelayDistanceMeters * globalConfigSettings.ledsPerMeter);

    // Which side of the pool are we on?
    if (swimmer->direction > 0) {
      // Going away from start wall
      // Draw delay indicator at the start of the strip
      for (int ledIndex = 0; ledIndex < remainingDelayLEDs && ledIndex < fullLengthLEDs[laneIndex]; ledIndex++) {
        // Only draw delay indicator if LED is currently black (swimmers get priority)
        if (renderedLEDs[laneIndex][ledIndex] == CRGB::Black) {
          // Create a dimmed version of the swimmer's color for the delay indicator
          CRGB delayColor = swimmer->color;
          delayColor.nscale8(128); // 50% brightness for delay indicator
          renderedLEDs[laneIndex][ledIndex] = delayColor;
        }
      }
    } else {
      // Coming back to start wall
      // Draw delay indicator at the end of the strip
      for (int ledIndex = fullLengthLEDs[laneIndex] - 1; ledIndex >= fullLengthLEDs[laneIndex] - remainingDelayLEDs && ledIndex >= 0; ledIndex--) {
        // Only draw delay indicator if LED is currently black (swimmers get priority)
        if (renderedLEDs[laneIndex][ledIndex] == CRGB::Black) {
          // Create a dimmed version of the swimmer's color for the delay indicator
          CRGB delayColor = swimmer->color;
          delayColor.nscale8(128); // 50% brightness for delay indicator
          renderedLEDs[laneIndex][ledIndex] = delayColor;
        }
      }
    }

    return true;
  } else {
    // No delay to draw for this swimmer
    return false;
  }
}

void initializeSwimmers() {
  if (DEBUG_ENABLED) {
    Serial.println("\n========== INITIALIZING SWIMMERS ==========");
  }

  for (int lane = 0; lane < MAX_LANES_SUPPORTED; lane++) {
  for (int i = 0; i < MAX_SWIMMERS_SUPPORTED; i++) {
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
  for (int lane = 0; lane < MAX_LANES_SUPPORTED; lane++) {
    for (int i = 0; i < MAX_SWIMMERS_SUPPORTED; i++) {
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
  // If swimmer has already completed all numRounds, do nothing
  if (swimmer->finished) return;
  unsigned long currentTime = millis();

  int laneSwimmerCount = globalConfigSettings.numSwimmersPerLane[laneIndex];
  // TODO: we shouldn't have to do this, it should be done once per lane elsewhere
  if (laneSwimmerCount < 1) laneSwimmerCount = 1;
  if (laneSwimmerCount > MAX_SWIMMERS_SUPPORTED) laneSwimmerCount = MAX_SWIMMERS_SUPPORTED;

  // Check if swimmer is resting
  // ------------------------------ SWIMMER RESTING LOGIC ------------------------------
  if (swimmer->isResting) {
    // How much time have they been resting
    unsigned long restElapsed = currentTime - swimmer->restStartTime;

    // What is our target rest duration (in milliseconds)?
    if (swimmer->expectedRestDuration == 0) {
      // TODO: We should never hit this, if we were resting we should have already set expectedRestDuration
      Serial.println("WARNING: swimmer expectedRestDuration was 0 during rest phase, calculating now");
      if (swimmer->currentRound == 1) {
        // First round: staggered start using swimmerInterval as the base delay.
        // First swimmer starts after swimmerInterval, second after 2*swimmerInterval, etc.
        swimmer->expectedRestDuration = (unsigned long)((swimmerIndex + 1) * (unsigned long)swimSetSettings.swimmerIntervalSeconds * 1000);
      } else {
        // Subsequent numRounds: include rest period plus swimmer interval offset
        swimmer->expectedRestDuration = (unsigned long)(swimmer->cachedRestSeconds * 1000) + (unsigned long)(swimmerIndex * swimSetSettings.swimmerIntervalSeconds * 1000);
      }
    }

    if (restElapsed >= swimmer->expectedRestDuration) {
      // Rest period complete, resume swimming
      if (DEBUG_ENABLED && swimmerIndex < laneSwimmerCount) {
        Serial.println("------------------------------------------------");
        Serial.print("Lane ");
        Serial.print(laneIndex);
        Serial.print(", Swimmer ");
        Serial.print(swimmerIndex);
        Serial.println(" - REST COMPLETE, starting to swim");
        Serial.print("  Rest duration: ");
        Serial.print(restElapsed / 1000.0);
        Serial.print(" sec (target: ");
        Serial.print(swimmer->expectedRestDuration / 1000.0);
        Serial.println(" sec)");
      }

      // Reset rest tracking
      // TODO: seems redundant to track rest duration and expected start time separately
      swimmer->isResting = false;
      swimmer->hasStarted = true;
      swimmer->restStartTime = 0;
      swimmer->expectedRestDuration = 0;
      swimmer->expectedStartTime = 0;

      long actualStartTime = swimmer->restStartTime + swimmer->expectedRestDuration;
      // Set total distance to 0.0
      // We don't account for distance traveled since rest period ended
      // because we will handle that in the swimming logic below
      // That is why we adjust last update time below to when the rest ended
      // TODO: The down side is we may not handle this position update till
      // the next frame, causing a small jump in position
      swimmer->totalDistance = 0.0;
      // Set last updated time to account for any overshootInDistance
      // TODO: I don't like how we are co-opting lastUpdate for this purpose
      swimmer->lastUpdate = currentTime - actualStartTime;

      // Start underwater phase when leaving the wall
      if (globalConfigSettings.underwatersEnabled) {
        swimmer->underwaterActive = true;
        swimmer->inSurfacePhase = false;
        swimmer->distanceTraveled = 0.0;
        swimmer->hideTimerStart = 0;
      }
    } else {
      // Still resting, don't move

      // Print debug info only once when entering rest state
      if (DEBUG_ENABLED && !swimmer->debugRestingPrinted && swimmerIndex < laneSwimmerCount) {
        Serial.println("================================================");
        Serial.print("Lane ");
        Serial.print(laneIndex);
        Serial.print(", Swimmer ");
        Serial.print(swimmerIndex);
        Serial.print(" - Round ");
        Serial.print(swimmer->currentRound);
        Serial.println(" - RESTING at wall");
        Serial.print("  Target rest duration: ");
        Serial.print(swimmer->expectedRestDuration / 1000.0);
        Serial.println(" seconds");
        Serial.print("  Elapsed rest time: ");
        Serial.print(restElapsed / 1000.0);
        Serial.println(" seconds");
        swimmer->debugRestingPrinted = true;
        swimmer->debugSwimmingPrinted = false; // Reset for next swim phase
      }
      return;
    }
  }

  // We don't use an else here because the swimmer may have just finished resting
  // ------------------------------ SWIMMER SWIMMING LOGIC ------------------------------
  // Swimmer is actively swimming
  if (currentTime - swimmer->lastUpdate >= delayMS) {
    // Print debug info only once when entering swimming state
    if (DEBUG_ENABLED && !swimmer->debugSwimmingPrinted && swimmerIndex < laneSwimmerCount) {
      Serial.println("------------------------------------------------");
      Serial.print("Lane ");
      Serial.print(laneIndex);
      Serial.print(", Swimmer ");
      Serial.print(swimmerIndex);
      Serial.print(" - SWIMMING - ");
      Serial.print("QI: ");
      Serial.print(swimmer->queueIndex);
      Serial.print(", R: ");
      Serial.print(swimmer->currentRound);
      Serial.print(", L: ");
      Serial.print(swimmer->currentLap);
      Serial.print("/");
      Serial.print(swimmer->lapsPerRound);
      Serial.print(", dir: ");
      Serial.print(swimmer->lapDirection == 1 ? "away from start" : "towards start");
      Serial.print(", pos: ");
      Serial.print(swimmer->position);
      Serial.print(", Dist: ");
      Serial.print(swimmer->totalDistance);
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

    // ---------------------------------- HANDLE WALL TURN ----------------------------------
    if (calculatedLap > swimmer->currentLap) {
      float overshootInDistance = swimmer->totalDistance - (swimmer->currentLap * globalConfigSettings.poolLength);
      long overshootInMilliseconds = (long)(overshootInDistance / swimmer->cachedSpeedMPS * 1000.0);

      if (DEBUG_ENABLED && swimmerIndex < laneSwimmerCount) {
        Serial.println("  *** WALL TURN ***");
        Serial.print("    calculatedLap based on distance: ");
        Serial.print(calculatedLap);
        Serial.print(", Current lap: ");
        Serial.print(swimmer->currentLap);
        Serial.print(", Total distance: ");
        Serial.print(swimmer->totalDistance);
        Serial.print(", Distance for current length: ");
        Serial.print(distanceForCurrentLength);
        Serial.print(", overshootInDistance: ");
        Serial.print(overshootInDistance);
        Serial.print(globalConfigSettings.poolUnitsYards ? " yards" : " meters");
        Serial.print(", overshootInMilliseconds: ");
        Serial.println(overshootInMilliseconds);
      }

      // Change direction
      swimmer->lapDirection *= -1;

      // Increment length counter
      swimmer->currentLap++;

      // -------------------------------- CHECK ROUND COMPLETION --------------------------------
      // Check if round is complete (all lengths done)
      if (swimmer->currentLap > swimmer->lapsPerRound) {
        // Round complete - reset length counter and check if we should rest
        swimmer->currentLap = 1;
        swimmer->totalDistance = 0.0;

        if (DEBUG_ENABLED && swimmerIndex < laneSwimmerCount) {
          Serial.println("  *** ROUND COMPLETE ***");
          Serial.print("    Completed round ");
          Serial.println(swimmer->currentRound);
          Serial.print("    Current lap reset to ");
          Serial.println(swimmer->currentLap);
          Serial.print("    Total distance reset to ");
          Serial.println(swimmer->totalDistance);
        }

        // -------------------------------- STILL MORE ROUNDS TO GO --------------------------------
        // Only rest if we haven't completed all numRounds yet
        if (swimmer->currentRound < swimmer->cachedNumRounds) {
          swimmer->currentRound++;
          swimmer->isResting = true;
          // Rest start time should be calculated from when they were
          // supposed to hit the wall, not current time (to account for overshootInDistance)
          swimmer->restStartTime = currentTime - overshootInMilliseconds;
          swimmer->expectedRestDuration = (unsigned long)(swimmer->cachedRestSeconds * 1000);
          swimmer->expectedStartTime = swimmer->restStartTime + (unsigned long)(swimmer->cachedRestSeconds * 1000);

          // we may want to ensure we are not less than the interval, so would want to
          // compare with previous swimmer's expected start time plus interval
          // Do we have a previous swimmer to stagger from?
          if (swimmerIndex > 0) {
            // If so, ensure we are at least swimmerIntervalSeconds after their expected start time
            Swimmer* previousSwimmer = &swimmers[laneIndex][swimmerIndex - 1];
            unsigned long staggeredStartTime = previousSwimmer->expectedStartTime + (unsigned long)(swimSetSettings.swimmerIntervalSeconds * 1000);
            if (swimmer->expectedStartTime < staggeredStartTime) {
              swimmer->expectedStartTime = staggeredStartTime;
              swimmer->restStartTime = swimmer->expectedStartTime - (unsigned long)(swimmer->cachedRestSeconds * 1000);
            }
          }

          // TODO: we may want to verify we are not already past expectedStartTime due to overshoot

          if (DEBUG_ENABLED && swimmerIndex < laneSwimmerCount) {
            Serial.println("  *** MORE ROUNDS TO GO IN SET ***");
            Serial.print("    Now on round ");
            Serial.println(swimmer->currentRound);
            Serial.println("    isResting set to true");
            Serial.print("    restStartTime to ");
            Serial.print(swimmer->restStartTime);
            Serial.print(" (accounting for overshootInDistance of ");
            Serial.println((overshootInDistance / swimmer->cachedSpeedMPS * 1000.0));
          }
        } else {
          // -------------------------------- ALL ROUNDS COMPLETE --------------------------------
          // All numRounds complete for this swim set
          if (DEBUG_ENABLED && swimmerIndex < laneSwimmerCount) {
            Serial.println("  *** ALL ROUNDS COMPLETE FOR CURRENT SET ***");
            Serial.print("    Swimmer ");
            Serial.print(swimmerIndex);
            Serial.print(" finished swim set ID ");
            Serial.print(swimmer->activeSwimSetId);
            Serial.print(" (queue index ");
            Serial.print(swimmer->queueIndex);
            Serial.println(")");
          }

          // ---------------------------- ALL SWIMMERS COMPLETE -----------------------------
          // If this was the last swimmer for the lane,
          // we can mark this swim set as complete for the lane.
          // However, we still need to check if there are more sets in the queue.
          if (swimmerIndex >= laneSwimmerCount - 1) {
            if (DEBUG_ENABLED && swimmerIndex < laneSwimmerCount) {
              Serial.print("    Swimmer ");
              Serial.print(swimmerIndex);
              Serial.println(" is the last swimmer in the lane");
              Serial.print("    Marking swim set ID ");
              Serial.print(swimmer->activeSwimSetId);
              Serial.println(" as complete for the lane");
            }
            // Mark the queue entry (by this swimmer's queueIndex) as completed.
            int qidx = swimmer->queueIndex;
            if (qidx >= 0 && qidx < swimSetQueueCount[laneIndex]) {
              int actualIdx = (swimSetQueueHead[laneIndex] + qidx) % SWIMSET_QUEUE_MAX;
              // Set completed flag
              swimSetQueue[laneIndex][actualIdx].status |= SWIMSET_STATUS_COMPLETED;
              // turn off active flag
              swimSetQueue[laneIndex][actualIdx].status &= ~SWIMSET_STATUS_ACTIVE;
              swimSetQueue[laneIndex][actualIdx].status &= ~SWIMSET_STATUS_STOPPED;

              // Because other swimmers may have been turned off before finishing
              // by lowering the per lane swimmer count, we should force set
              // all previous swim sets in the queue as completed as well.
              for (int qi = 0; qi < qidx; qi++) {
                int aIdx = (swimSetQueueHead[laneIndex] + qi) % SWIMSET_QUEUE_MAX;
                swimSetQueue[laneIndex][aIdx].status |= SWIMSET_STATUS_COMPLETED;
                swimSetQueue[laneIndex][aIdx].status &= ~SWIMSET_STATUS_ACTIVE;
                swimSetQueue[laneIndex][aIdx].status &= ~SWIMSET_STATUS_STOPPED;
              }
            }
            // Move all the remaining non-active swimmers to the same position
            // as this last swimmer.
            for (int si = swimmerIndex + 1; si < laneSwimmerCount; si++) {
              swimmers[laneIndex][si].lapDirection *= -1;
              swimmers[laneIndex][si].currentLap = 1;
              swimmers[laneIndex][si].totalDistance = 0.0;
            }
          }

          // ---------------------------- LOAD NEXT SWIM SET IF AVAILABLE ----------------------------
          // Try to get the next swim set in queue (queueIndex + 1)
          SwimSet nextSet;
          if (getSwimSetByIndex(nextSet, laneIndex, swimmer->queueIndex + 1)) {
            if (DEBUG_ENABLED && swimmerIndex < laneSwimmerCount) {
              Serial.println("  *** AUTO-ADVANCING TO NEXT SWIM SET ***");
              Serial.print("    Next set ID: ");
              Serial.print(nextSet.id);
              Serial.print(", queue index: ");
              Serial.print(swimmer->queueIndex + 1);
              Serial.print(", numRounds: ");
              Serial.print(nextSet.numRounds);
              Serial.print(", swimDistance: ");
              Serial.print(nextSet.swimDistance);
              Serial.print(", swim seconds: ");
              Serial.print(nextSet.swimTime);
              Serial.print(", rest seconds: ");
              Serial.print(nextSet.restTime);
              Serial.print(", status: ");
              Serial.println(nextSet.status);
            }

            // Calculate new settings for the next set
            float lengthMeters = convertPoolToMeters((float)nextSet.swimDistance);
            float speedMPS = (nextSet.swimTime > 0) ? (lengthMeters / nextSet.swimTime) : 0.0;
            int lapsPerRound = ceil(nextSet.swimDistance / globalConfigSettings.poolLength);

            // Cache the new swim set settings in this swimmer
            int currentSetRestSeconds = swimmer->cachedRestSeconds;
            swimmer->cachedNumRounds = nextSet.numRounds;
            swimmer->cachedLapsPerRound = lapsPerRound;
            swimmer->cachedSpeedMPS = speedMPS;
            swimmer->cachedRestSeconds = nextSet.restTime;
            swimmer->activeSwimSetId = nextSet.id;
            swimmer->queueIndex++;  // Move to next index in queue

            // --- KEEP laneActiveQueueIndex IN SYNC WITH FIRST SWIMMER ADVANCE ---
            // When any swimmer advances to the next queue index, make the lane's active
            // queue index reflect that new value so client status (activeIndex) stays accurate.
            laneActiveQueueIndex[laneIndex] = swimmer->queueIndex;
            // Optionally persist running flag / global state consistency
            // (do not force-stop lane here; clearing is handled below when all swimmers finish)

            // Set the swim set status to active
            int qidx = swimmer->queueIndex;
            if (qidx >= 0 && qidx < swimSetQueueCount[laneIndex]) {
              int actualIdx = (swimSetQueueHead[laneIndex] + qidx) % SWIMSET_QUEUE_MAX;
              swimSetQueue[laneIndex][actualIdx].status |= SWIMSET_STATUS_ACTIVE;
              swimSetQueue[laneIndex][actualIdx].status &= ~SWIMSET_STATUS_STOPPED;
            }

            // Reset swimmer state for new set
            swimmer->currentRound = 1;
            swimmer->currentLap = 1;
            swimmer->lapsPerRound = lapsPerRound;
            swimmer->isResting = true;
            swimmer->restStartTime = currentTime - overshootInMilliseconds;
            swimmer->expectedRestDuration = currentSetRestSeconds * 1000UL;
            swimmer->expectedStartTime = swimmer->restStartTime + swimmer->expectedRestDuration;
            swimmer->totalDistance = 0.0;
            swimmer->finished = false;

            // we may want to ensure we are not less than the interval, so would want to
            // compare with previous swimmer's expected start time plus interval
            // Do we have a previous swimmer to stagger from?
            if (swimmerIndex > 0) {
              // If so, ensure we are at least swimmerIntervalSeconds after their expected start time
              Swimmer* previousSwimmer = &swimmers[laneIndex][swimmerIndex - 1];
              unsigned long staggeredStartTime = previousSwimmer->expectedStartTime + (unsigned long)(swimSetSettings.swimmerIntervalSeconds * 1000);
              if (swimmer->expectedStartTime < staggeredStartTime) {
                swimmer->expectedStartTime = staggeredStartTime;
                swimmer->restStartTime = swimmer->expectedStartTime - (unsigned long)(swimmer->cachedRestSeconds * 1000);
              }
            }

            if (DEBUG_ENABLED && swimmerIndex < laneSwimmerCount) {
              Serial.print("    Swimmer ");
              Serial.print(swimmerIndex);
              Serial.println(" starting rest before next set");
            }

            // ---------------------------- SETUP REMAINING INACTIVE SWIMMERS FOR NEW SET ----------------------------
            // We are loading the next swim set, and
            // If this was the last swimmer for the lane,
            // Setup any remaining non-active swimmers so they are in the right place.
            if (swimmerIndex >= laneSwimmerCount - 1) {
              for (int si = swimmerIndex + 1; si < laneSwimmerCount; si++) {
                swimmers[laneIndex][si].cachedNumRounds = nextSet.numRounds;
                swimmers[laneIndex][si].cachedLapsPerRound = lapsPerRound;
                swimmers[laneIndex][si].cachedSpeedMPS = speedMPS;
                swimmers[laneIndex][si].cachedRestSeconds = nextSet.restTime;
                swimmers[laneIndex][si].activeSwimSetId = nextSet.id;
                swimmers[laneIndex][si].queueIndex++;  // Move to next index in queue
                swimmers[laneIndex][si].currentRound = 1;
                swimmers[laneIndex][si].currentLap = 1;
                swimmers[laneIndex][si].lapsPerRound = lapsPerRound;
                swimmers[laneIndex][si].isResting = true;
                swimmers[laneIndex][si].restStartTime = currentTime - overshootInMilliseconds;
                swimmers[laneIndex][si].expectedStartTime = swimmer->expectedStartTime + (si - swimmerIndex) * swimSetSettings.swimmerIntervalSeconds * 1000UL;
                swimmers[laneIndex][si].expectedRestDuration = swimmers[laneIndex][si].expectedStartTime - swimmers[laneIndex][si].restStartTime;
                swimmers[laneIndex][si].totalDistance = 0.0;
                swimmers[laneIndex][si].finished = false;
              }
            }
          } else {
            // -------------------------------- NO MORE SWIM SETS IN QUEUE --------------------------------
            // No more sets in queue - swimmer is done
            // Mark swimmer as finished so we stop further updates
            swimmer->finished = true;
            swimmer->isResting = true;
            swimmer->hasStarted = false;
            swimmer->restStartTime = currentTime - overshootInMilliseconds;
            swimmer->expectedRestDuration = 0;
            swimmer->expectedStartTime = 0;
            if (DEBUG_ENABLED && swimmerIndex < laneSwimmerCount) {
              Serial.println("  *** NO MORE SETS IN QUEUE ***");
              Serial.print("    Swimmer ");
              Serial.print(swimmerIndex);
              Serial.print(" completed all ");
              Serial.print(swimmer->queueIndex + 1);
              Serial.println(" sets!");
            }

            // ---------------------------- ALL SWIMMERS COMPLETE FULL QUEUE -----------------------------
            // If this was the last swimmer for the lane (all swimmers finished),
            // clear the laneActiveQueueIndex and mark lane not running.
            bool allFinished = true;
            for (int si = 0; si < laneSwimmerCount; si++) {
              if (!swimmers[laneIndex][si].finished) { allFinished = false; break; }
            }
            if (allFinished) {
              laneActiveQueueIndex[laneIndex] = -1;
              globalConfigSettings.laneRunning[laneIndex] = false;
              // Update global isRunning if any lane remains running
              globalConfigSettings.isRunning = false;
              for (int li = 0; li < globalConfigSettings.numLanes; li++) {
                if (globalConfigSettings.laneRunning[li]) { globalConfigSettings.isRunning = true; break; }
              }
              // Move all the remaining non-active swimmers to the same position
              // as this last swimmer.
              for (int si = swimmerIndex + 1; si < laneSwimmerCount; si++) {
                swimmers[laneIndex][si].finished = true;
                swimmers[laneIndex][si].isResting = true;
                swimmers[laneIndex][si].hasStarted = false;
                swimmers[laneIndex][si].restStartTime = swimmer->restStartTime;
                swimmers[laneIndex][si].expectedRestDuration = 0;
                swimmers[laneIndex][si].expectedStartTime = 0;
              }
              saveGlobalConfigSettings();
            }
          }
        }

        // We finished a round, so we need to position the swimmer at the wall
        // Place LED at the wall based on direction
        if (swimmer->lapDirection == 1) {
          swimmer->position = 0;
        } else {
          // Convert pool length to LED position
          float poolLengthInMeters = convertPoolToMeters(globalConfigSettings.poolLength);
          float ledPosition = floor((poolLengthInMeters * globalConfigSettings.ledsPerMeter)-1);
          swimmer->position = ledPosition;
        }
        // Move any remaining inactive swimmers to same position
        if (swimmerIndex >= laneSwimmerCount - 1) {
          for (int si = swimmerIndex + 1; si < MAX_SWIMMERS_SUPPORTED; si++) {
            swimmers[laneIndex][si].position = swimmer->position;
          }
        }

      } else {
        // -------------------------------- LAP TURN STILL IN THE SAME ROUND --------------------------------
        // Lap turn - Still in the same round
        // Start underwater phase at wall
        if (globalConfigSettings.underwatersEnabled) {
          // TODO: What if we already overshot the underwater distance?
          swimmer->underwaterActive = true;
          swimmer->inSurfacePhase = false;
          swimmer->distanceTraveled = overshootInDistance;
          swimmer->hideTimerStart = 0;
        }

        // Place LED position where we overshot the wall
        // TODO: is this redundant, is overshootDistance already in meters?
        float overshootInMeters = convertPoolToMeters(overshootInDistance);
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
      // -------------------------------- NORMAL MOVEMENT WITHIN POOL LENGTH --------------------------------
      // Normal movement within the same pool length
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

    // TODO: maybe this should be in the sections above, although then it would be
    // repeated in multiple places
    // ------------------------------ UNDERWATER LIGHT LOGIC ------------------------------
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
        if (laneSwimmerCount > MAX_SWIMMERS_SUPPORTED) laneSwimmerCount = MAX_SWIMMERS_SUPPORTED;
        for (int i = 0; i < laneSwimmerCount; i++) {
          Swimmer* s = &swimmers[lane][i];
          Serial.print("  Swimmer ");
          Serial.print(i);
          Serial.print(": ");
          if (s->isResting) {
            Serial.print("RESTING (");
            Serial.print((currentTime - s->restStartTime) / 1000.0);
            Serial.print("s)");
            Serial.print(" targetRest:");
            Serial.print(s->expectedRestDuration / 1000.0);
            Serial.print(" QI:");
            Serial.print(s->queueIndex);
            Serial.print(" hasStarted=");
            Serial.print(s->hasStarted);
          } else {
            Serial.print("Swimming - ");

            Serial.print(" QI:");
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

  //int centerPos = swimmer->position;
  //int halfWidth = pulseWidthLEDs / 2;
  CRGB pulseColor = swimmer->color;

  for (int i = 0; i < pulseWidthLEDs; i++) {
    //int ledIndex = centerPos - halfWidth + i;
    int ledIndex = swimmer->position + i;


    if (ledIndex >= 0 && ledIndex < fullLengthLEDs[laneIndex]) {
      CRGB color = pulseColor;
      color.nscale8((uint8_t)(globalConfigSettings.brightness));

      // Only set color if LED is currently black (first wins priority)
      if (renderedLEDs[laneIndex][ledIndex] == CRGB::Black) {
        renderedLEDs[laneIndex][ledIndex] = color;
      }
    }
  }
}

void drawUnderwaterZone(int laneIndex, int swimmerIndex) {
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

  // Determine color based on phase
  CRGB currentUnderwaterColor = swimmer->inSurfacePhase ?
    underwaterSurfaceColor : underwaterColor;

  // Position underwater light exactly at the swimmer's position (moves with swimmer)
  int swimmerPos = swimmer->position + pulseWidthLEDs;

  // Draw the underwater light pulse centered on swimmer
  for (int i = swimmerPos; i < swimmerPos + underwatersWidthLEDs; i++) {
    if (i >= 0 && i < fullLengthLEDs[laneIndex]) {
      // Only set color if LED is currently black (first wins priority)
      if (renderedLEDs[laneIndex][i] == CRGB::Black) {
        renderedLEDs[laneIndex][i] = currentUnderwaterColor;
      }
    }
  }
}