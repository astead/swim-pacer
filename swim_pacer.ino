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

// Compile-time switch to enable debug logs
#ifndef DEBUG_SERIAL
  // set to 1 to enable Serial debug output, 0 to disable (default: disabled for performance)
  #define DEBUG_SERIAL 0
#endif

#if DEBUG_SERIAL
  #define LOG(...) Serial.print(__VA_ARGS__)
  #define LOGLN(...) Serial.println(__VA_ARGS__)
#else
  #define LOG(...) do {} while(0)
  #define LOGLN(...) do {} while(0)
#endif

// ========== HARDWARE CONFIGURATION ==========
//#define LED_TYPE        WS2812B     // LED strip type
#define LED_TYPE        WS2815
#define COLOR_ORDER     GRB         // Color order (may need adjustment)

// GPIO pins for multiple LED strips (lanes)
// Lane 1: GPIO 18, Lane 2: GPIO 19, Lane 3: GPIO 21, Lane 4: GPIO 2
#define LANE_0_PIN 18
#define LANE_1_PIN 19
#define LANE_2_PIN 21
#define LANE_3_PIN 22

// Enum for Queue insert error codes
enum QueueInsertError {
  QUEUE_INSERT_SUCCESS = 0,
  QUEUE_INSERT_DUPLICATE = 1,
  QUEUE_INSERT_FULL = 2,
  QUEUE_INVALID_LANE = 3
};

const int SWIMSET_QUEUE_MAX = 20;
const int MAX_LANES_SUPPORTED = 4;
const int MAX_SWIMMERS_SUPPORTED = 10;
// Measured JSON size per swim set with full swimmer customization
const int BYTES_PER_SWIMSET = 350;
// StaticJsonDocument capacity (stack-allocated, no heap alignment needed)
// 20 sets × 350 bytes = 7000 bytes (well within ESP32 stack limits)
const size_t DOC_SIZE = (SWIMSET_QUEUE_MAX * BYTES_PER_SWIMSET) + 1024; // +1KB buffer

// Debug control - set to true to enable detailed debug output
const bool DEBUG_ENABLED = true;
const unsigned long DEBUG_INTERVAL_MS = 2000; // Print periodic status every 2 seconds
unsigned long lastDebugPrint = 0;

// --- Render task synchronization (FreeRTOS) ---
static SemaphoreHandle_t renderLock = NULL;        // protects writes to scanoutLEDs
static SemaphoreHandle_t renderSemaphore = NULL;   // signals render task to call FastLED.show()
static volatile bool renderTaskStarted = false;

// ========== WIFI CONFIGURATION ==========
const char* ssid = "SwimPacer";             // WiFi network name
const char* password = "";                  // No password for easy access
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
  uint8_t numRounds;        // number of numRounds
  uint16_t swimDistance;    // distance in pool's native units (no units attached)
  uint16_t swimTime;        // seconds to swim the 'length'
  uint16_t restTime;        // rest time between numRounds
  uint16_t swimmerInterval; // interval between swimmers (seconds)
  uint8_t status;           // status flags (bitmask)
  uint8_t type;             // SwimSetType
  unsigned long long uniqueId; // client-supplied uniqueId for reconciliation (0 = none)
  // loop metadata (client-supplied stable identifier for loop start)
  unsigned long long loopFromUniqueId;
  // remaining repeat iterations (server authoritative)
  int repeatRemaining;

  // per-swimmer customization
  uint8_t swimmersCount; // how many swimmer entries are present
  uint16_t swimmerTimes[MAX_SWIMMERS_SUPPORTED]; // per-swimmer swimTime seconds (0 = use set swimTime)
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

// --- Simple embedded profiler (low memory) ---
struct ProfEntry {
  const char* name;
  unsigned long totalMicros;
  unsigned long count;
  unsigned long lastStart;
  bool running;
};
const int PROF_MAX = 16;
static ProfEntry profiler[PROF_MAX];

static ProfEntry* profGet(const char* name) {
  for (int i=0;i<PROF_MAX;i++) if (profiler[i].name && strcmp(profiler[i].name, name)==0) return &profiler[i];
  for (int i=0;i<PROF_MAX;i++) if (!profiler[i].name) { profiler[i].name = name; profiler[i].totalMicros=0; profiler[i].count=0; profiler[i].running=false; return &profiler[i]; }
  return NULL;
}
static void profStart(const char* name) {
  ProfEntry* e = profGet(name);
  if (!e) return;
  e->lastStart = micros();
  e->running = true;
}
static void profEnd(const char* name) {
  ProfEntry* e = profGet(name);
  if (!e || !e->running) return;
  unsigned long delta = micros() - e->lastStart;
  e->totalMicros += delta;
  e->count++;
  e->running = false;
}
static void profPrintReport() {
  //Serial.printf("=== PROFILER REPORT ===\n");
  for (int i=0;i<PROF_MAX;i++) {
    if (!profiler[i].name) continue;
    unsigned long avg = profiler[i].count ? (profiler[i].totalMicros / profiler[i].count) : 0;
    //Serial.printf("%-22s count=%6lu total=%8luus avg=%6luus\n", profiler[i].name, profiler[i].count, profiler[i].totalMicros, avg);
  }
  //Serial.printf("=======================\n");
}

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
int poolLengthLEDs;  // Total number of LEDs representing the pool length
int fullLengthLEDs[MAX_LANES_SUPPORTED];  // Total number of rendered LEDs (ignoring gaps)
int visibleLEDs[MAX_LANES_SUPPORTED];     // Total number of actual LEDs (accounting for gaps)
std::vector<int> gapLEDs[MAX_LANES_SUPPORTED];         // positions where there are no LEDs due to gaps
std::vector<std::pair<int,int>> copySegments[MAX_LANES_SUPPORTED]; // { srcStart, length } segments for spliceOutGaps
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
bool needsRecalculation = true;

// Throttling render out to LEDs
unsigned long lastFastLEDShowMs = 0;
unsigned long minFastLEDShowIntervalMs = 20; // minimum ms between FastLED.show() calls (tunable)

// Heap monitoring telemetry (small circular buffer)
#define HEAP_SAMPLE_BUFFER_SIZE 32
struct HeapSample {
  unsigned long timestamp;
  size_t freeHeap;
  size_t minFreeHeap;
  bool valid;
};
static HeapSample heapSamples[HEAP_SAMPLE_BUFFER_SIZE];
static int heapSampleHead = 0; // points to next write
static int heapSampleCount = 0;
static unsigned long lastHeapSampleMs = 0;
static const unsigned long HEAP_SAMPLE_INTERVAL_MS = 5000; // sample every 5s

// Render task: waits for a signal, takes renderLock, performs FastLED.show(), updates lastFastLEDShowMs
static void renderTask(void *pv) {
  (void)pv;
  renderTaskStarted = true;
  for (;;) {
    if (xSemaphoreTake(renderSemaphore, portMAX_DELAY) == pdTRUE) {
      // Acquire lock so nobody writes to scanoutLEDs while we are showing
      if (xSemaphoreTake(renderLock, portMAX_DELAY) == pdTRUE) {
        profStart("FastLED.show");
        FastLED.show();
        profEnd("FastLED.show");
        lastFastLEDShowMs = millis();
        xSemaphoreGive(renderLock);
      }
    }
  }
}

// call once during setup to create sync primitives and start render task
static void startRenderTaskOnce() {
  if (renderLock == NULL) renderLock = xSemaphoreCreateMutex();
  if (renderSemaphore == NULL) renderSemaphore = xSemaphoreCreateBinary();
  if (!renderTaskStarted) {
    // Pin render task to core 1 to avoid interfering with networking on core 0 (tunable)
    xTaskCreatePinnedToCore(renderTask, "RenderTask", 4096, NULL, 1, NULL, 1);
  }
}

// Helper: parse lane from POST JSON body (returns -1 when missing)
static int parseLaneFromBody(const String &body) {
  // Try JSON extraction first
  long v = extractJsonLong(body, "lane", -1);
  if (v >= 0) return (int)v;

  // Fallback: simple form-encoded parsing (lane=<n>&...)
  String needle = String("lane=");
  int idx = body.indexOf(needle);
  if (idx < 0) return -1;
  int start = idx + needle.length();
  int end = start;
  while (end < body.length() && isDigit(body.charAt(end))) end++;
  if (end == start) return -1;
  String numStr = body.substring(start, end);
  return numStr.toInt();
}

// Parse a string parameter from either JSON body ("key":"value") or form-encoded (key=value)
static String parseStringFromBody(const String &body, const char *key) {
  // Try JSON first
  String s = extractJsonString(body, key, "");
  if (s.length() > 0) return s;

  // Fallback to form-encoded parsing
  String needle = String(key) + String("=");
  int idx = body.indexOf(needle);
  if (idx < 0) return String("");
  int start = idx + needle.length();
  int end = start;
  while (end < body.length() && body.charAt(end) != '&') end++;
  String val = body.substring(start, end);
  // URL decode simple %20 -> space
  val.replace("+", " ");
  // Basic percent-decoding
  int p = val.indexOf('%');
  while (p >= 0) {
    if (p + 2 < val.length()) {
      String hex = val.substring(p+1, p+3);
      char ch = (char)strtol(hex.c_str(), NULL, 16);
      val = val.substring(0, p) + String(ch) + val.substring(p+3);
    } else break;
    p = val.indexOf('%');
  }
  return val;
}

// Forward declarations for save/load helpers (defined later)
bool saveQueueToSlot(int lane, const char *name, int *outError);
bool loadQueueFromSlot(int lane, const char *name, int *outAdded, int *outSkipped, int *outFailed);

// The queue arrays are declared earlier but we define the helpers here for readability.
int enqueueSwimSet(const SwimSet &s, int lane) {
  if (DEBUG_ENABLED) LOGLN("enqueueSwimSet(): called");
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    if (DEBUG_ENABLED) LOGLN("  invalid lane, rejecting set");
    return QUEUE_INVALID_LANE;
  }
  if (swimSetQueueCount[lane] >= SWIMSET_QUEUE_MAX) {
    if (DEBUG_ENABLED) LOGLN("  queue full, rejecting set");
    return QUEUE_INSERT_FULL;
  }
  // Check if we already have a set with the same uniqueId in the queue
  for (int i = 0; i < swimSetQueueCount[lane]; i++) {
    int index = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
    if (swimSetQueue[lane][index].uniqueId == s.uniqueId && s.uniqueId != 0ULL) {
      if (DEBUG_ENABLED) {
        LOG("  duplicate uniqueId found in queue, rejecting set: ");
        LOGLN(s.uniqueId);
      }
      return QUEUE_INSERT_DUPLICATE;
    }
  }
  if (DEBUG_ENABLED) {
    LOG("enqueueSwimSet(): received set for lane:");
    LOG(lane);
    LOG("  [");
    LOG(s.numRounds);
    LOG("x");
    LOG(s.swimDistance);
    LOG(" on ");
    LOG(s.swimTime);
    LOG(" rest:");
    LOG(s.restTime);
    LOG(" interval:");
    LOG(s.swimmerInterval);
    LOGLN("]");
  }
  swimSetQueue[lane][swimSetQueueTail[lane]] = s;
  swimSetQueueTail[lane] = (swimSetQueueTail[lane] + 1) % SWIMSET_QUEUE_MAX;
  swimSetQueueCount[lane]++;
  if (DEBUG_ENABLED) {
    LOG("  Queue Count After Insert="); LOGLN(swimSetQueueCount[lane]);
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

bool getSwimSetByUniqueId(SwimSet &out, int lane, unsigned long long uniqueId) {
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) return false;

  for (int i = 0; i < swimSetQueueCount[lane]; i++) {
    int index = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
    if (swimSetQueue[lane][index].uniqueId == uniqueId) {
      out = swimSetQueue[lane][index];
      return true;
    }
  }
  return false;
}

int findSwimSetIndexByUniqueId(int lane, unsigned long long uniqueId) {
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) return -1;

  for (int i = 0; i < swimSetQueueCount[lane]; i++) {
    int index = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
    if (swimSetQueue[lane][index].uniqueId == uniqueId) {
      return index;
    }
  }
  return -1;
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
  unsigned long lastPositionUpdate; // time of last position integration/update (ms)
  unsigned long swimStartTime;      // absolute millis() when swimmer is allowed to start (0 = already started)
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
  float cachedSpeedPoolUnitsPerMs; // speed in pool-units per millisecond (cheap multiply)
  unsigned long cachedRestMs;      // rest duration in milliseconds (cached)
  int cachedRestSeconds;       // Rest duration in seconds
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
    LOGLN("applySwimSetToSettings(): applying set:");
    LOG("  lane="); LOGLN(lane);
    LOG("  numRounds="); LOGLN(s.numRounds);
    LOG("  swimDistance="); LOGLN(s.swimDistance);
    LOG("  swimTime="); LOGLN(s.swimTime);
    LOG("  restTime="); LOGLN(s.restTime);
    LOG("  swimmerIntervalSeconds="); LOGLN(s.swimmerInterval);
    LOG("  computed speedMPS="); LOGLN(speedMPS, 4);
  }

  // Defensive check: ensure swimmer interval is sane (non-zero). If it's zero
  // the computed target rest durations will be zero which results in immediate
  // starts for all swimmers. Reset to a safe default for runtime behavior but
  // avoid persisting an invalid zero value until we've validated all fields.
  if (swimSetSettings.swimmerIntervalSeconds <= 0) {
    if (DEBUG_ENABLED) {
      LOG("Warning: swimmerIntervalSeconds <= 0 (value=");
      LOG(swimSetSettings.swimmerIntervalSeconds);
      LOGLN(") - resetting to default 4 seconds (will be persisted)");
    }
    swimSetSettings.swimmerIntervalSeconds = 4;
  }

  // Persist swim set settings (now that swimmerIntervalSeconds is validated)
  saveSwimSetSettings();

  if (DEBUG_ENABLED) {
    LOG("  swimmerIntervalSeconds="); LOGLN(swimSetSettings.swimmerIntervalSeconds);
  }

  // Initialize swimmers for the current lane to start fresh
  for (int i = 0; i < MAX_SWIMMERS_SUPPORTED; i++) {
    //swimmers[lane][i].position = 0; This should continue from previous position
    swimmers[lane][i].hasStarted = false;  // Initialize as not started
    swimmers[lane][i].lastPositionUpdate = currentMillis;
    swimmers[lane][i].swimStartTime = 0; // will be set when rest completes (or 0 means start immediately)

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
    //swimmers[lane][i].lapDirection = 1;  This should continue from previous lapDirection
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

    // Determine per-swimmer swimTime (use per-swimmer override if present else set swimTime)
    float swimmerSpeedMPS = speedMPS;
    float restForThisSwimmer = (float)s.restTime;
    if (s.swimmersCount > 0 && i < s.swimmersCount && s.swimmerTimes[i] > 0) {
      uint16_t swimmerTime = s.swimmerTimes[i];
      float swimmerDistanceMeters = convertPoolToMeters((float)s.swimDistance);
      swimmerSpeedMPS = (swimmerTime > 0) ? (swimmerDistanceMeters / (float)swimmerTime) : 0.0f;
      // Keep total (swim + rest) equal to canonical set total (s.swimTime + s.restTime)
      int totalPair = (int)s.swimTime + (int)s.restTime;
      restForThisSwimmer = (float)(totalPair - (int)swimmerTime);
      if (restForThisSwimmer < 0.0f) restForThisSwimmer = 0.0f; // clamp
    }
    swimmers[lane][i].cachedSpeedMPS = swimmerSpeedMPS;
    swimmers[lane][i].cachedRestSeconds = restForThisSwimmer;
    // Precompute pool-unit speed-per-ms to avoid divide ops in inner loop.
    // poolUnits: yards if poolUnitsYards==true, meters otherwise.
    float poolUnitFactor = globalConfigSettings.poolUnitsYards ? 1.0f / 0.9144f : 1.0f; // multiply meters->poolUnits
    float speedInPoolUnitsPerSec = swimmerSpeedMPS * poolUnitFactor;
    swimmers[lane][i].cachedSpeedPoolUnitsPerMs = speedInPoolUnitsPerSec / 1000.0f; // per-ms
    swimmers[lane][i].cachedRestMs = (unsigned long)(restForThisSwimmer * 1000.0f);
    // set queue index: use laneActiveQueueIndex if set, otherwise 0
    int qidx = -1;
    if (lane >= 0 && lane < MAX_LANES_SUPPORTED) qidx = laneActiveQueueIndex[lane];
    if (qidx < 0) qidx = 0;
    swimmers[lane][i].queueIndex = qidx;
  }

  // Debug: dump swimmer state for current lane after initialization (first 3 swimmers)
  if (DEBUG_ENABLED) {
    LOGLN("  swimmer state after applying the swim set:");
    for (int i = 0; i < MAX_SWIMMERS_SUPPORTED; i++) {
      Swimmer &sw = swimmers[lane][i];
      LOG("  swimmer="); LOG(i);
      LOG(" pos="); LOG(sw.position);
      LOG(" hasStarted="); LOG(sw.hasStarted ? 1 : 0);
      LOG(" currentRound="); LOG(sw.currentRound);
      LOG(" currentLap="); LOG(sw.currentLap);
      LOG(" lapsPerRound="); LOG(sw.lapsPerRound);
      LOG(" isResting="); LOG(sw.isResting ? 1 : 0);
      LOG(" restStartTime="); LOG(sw.restStartTime);
      LOG(" expectedRestDuration="); LOG(sw.expectedRestDuration / 1000.0);
      LOG(" startAtMillis="); LOG(sw.expectedStartTime);
      LOG(" totalDistance="); LOG(sw.totalDistance, 3);
      LOG(" lapDirection="); LOGLN(sw.lapDirection);
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
  LOGLN("Setting pinMode for on-board LED (GPIO2)...");
  pinMode(2, OUTPUT); // On many ESP32 dev boards the on-board LED is on GPIO2

  Serial.begin(115200);
  LOG("ESP32 Swim Pacer Starting... build=");
  LOGLN(BUILD_TAG);

  // Create sync primitives but DON'T start the render task yet
  LOGLN("Initializing render synchronization...");
  if (renderLock == NULL) renderLock = xSemaphoreCreateMutex();
  if (renderSemaphore == NULL) renderSemaphore = xSemaphoreCreateBinary();

  // Initialize preferences (flash storage)
  LOGLN("Initializing preferences...");
  preferences.begin("swim_pacer", false);
  loadSettings();

  // Setup WiFi Access Point
  LOGLN("Setting up WiFi Access Point...");
  setupWiFi();

  // Setup web server
  LOGLN("Setting up web server...");
  setupWebServer();

  // Calculate initial values
  LOGLN("Clearing FastLED...");
  FastLED.clear();
  LOGLN("Recalculating values for all lanes...");
  for (int li = 0; li < MAX_LANES_SUPPORTED; li++) {
    recalculateValues(li);
  }

  LOGLN("Finished recalculating values, setting brightness");
  FastLED.setBrightness(globalConfigSettings.brightness);

  // NOW start the render task after FastLED controllers are configured
  LOGLN("Starting render task...");
  if (!renderTaskStarted) {
    xTaskCreatePinnedToCore(renderTask, "RenderTask", 4096, NULL, 1, NULL, 1);
    renderTaskStarted = true;
  }

  // Signal initial show
  LOGLN("Signaling initial render...");
  if (renderSemaphore != NULL) xSemaphoreGive(renderSemaphore);

  LOGLN("Calling initialize Swimmers");
  // Initialize swimmers
  initializeSwimmers();

  LOGLN("Setup complete!");
  LOGLN("Connect to WiFi: " + String(ssid));
  LOGLN("Open browser to: http://192.168.4.1");
}

// call profPrintReport periodically (e.g., every 30s) in loop() or expose an HTTP handler
unsigned long lastProfPrint = 0;

void loop() {
  server.handleClient();  // Handle web requests
  unsigned long now = millis();

  // Blink the LED to give an easy to see message the program is working.
  digitalWrite(2, HIGH);

  if (globalConfigSettings.isRunning) {
    updateLEDEffect();
    //printPeriodicStatus(); // Print status update every few seconds
  }

  // periodic profiler print (adjust interval as needed)
  if (now - lastProfPrint > 30000) {
    lastProfPrint = now;
    profPrintReport();
  }

  // Periodic heap sampling for memory leak detection
  if (now - lastHeapSampleMs >= HEAP_SAMPLE_INTERVAL_MS) {
    lastHeapSampleMs = now;
    size_t freeHeap = ESP.getFreeHeap();
    size_t minFree = ESP.getMinFreeHeap();
    heapSamples[heapSampleHead].timestamp = now;
    heapSamples[heapSampleHead].freeHeap = freeHeap;
    heapSamples[heapSampleHead].minFreeHeap = minFree;
    heapSamples[heapSampleHead].valid = true;
    heapSampleHead = (heapSampleHead + 1) % HEAP_SAMPLE_BUFFER_SIZE;
    if (heapSampleCount < HEAP_SAMPLE_BUFFER_SIZE) heapSampleCount++;
    // Print to serial when DEBUG_SERIAL enabled
    //Serial.printf("[HEAP] ts=%lu free=%lu min=%lu\n", now, (unsigned long)freeHeap, (unsigned long)minFree);
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
  // Try to reduce peak power draw: disable Bluetooth
#if defined(ESP_BT_CONTROLLER_INIT_CONFIG) || defined(CONFIG_BT_ENABLED)
  LOGLN("DEBUG: Disabling BT controller");
  esp_bt_controller_disable();
#endif

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ssid, password);

  LOGLN("WiFi AP Started");
  LOG("IP address: ");
  LOGLN(WiFi.softAPIP());
}

void setupWebServer() {
  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    LOGLN("SPIFFS Mount Failed");
    return;
  }
  LOGLN("SPIFFS mounted successfully");

  // List SPIFFS contents for debugging
  LOGLN("SPIFFS Directory listing:");
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file) {
    LOG("  ");
    LOG(file.name());
    LOG(" (");
    LOG(file.size());
    LOGLN(" bytes)");
    file = root.openNextFile();
  }
  LOGLN("End of SPIFFS listing");

  // Serve the main configuration page
  server.on("/", handleRoot);

  // Serve CSS file
  server.on("/style.css", []() {
    File file = SPIFFS.open("/style.css", "r");
    if (!file) {
      LOGLN("ERROR: Could not open /style.css from SPIFFS");
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
      LOGLN("ERROR: Could not open /script.js from SPIFFS");
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

  // Save/load queue endpoints (accept lane and name)
  server.on("/saveQueue", HTTP_POST, [](){
    String body = server.arg("plain");
    // Expect JSON body: { "lane": <n>, "name": "..." }
    int lane = (int)extractJsonLong(body, "lane", -1);
    String name = extractJsonString(body, "name", "");
    Serial.printf("[HTTP] /saveQueue body=%s\n", body.c_str());
    Serial.printf("[HTTP] /saveQueue parsed lane=%d name=%s\n", lane, name.c_str());
    String json;
    if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
      json = "{\"ok\":false,\"error\":\"invalid lane\"}";
      server.send(200, "application/json", json);
      return;
    }
    if (name.length() == 0) {
      json = "{\"ok\":false,\"error\":\"missing name\"}";
      server.send(200, "application/json", json);
      return;
    }
    int err = 0;
    bool ok = saveQueueToSlot(lane, name.c_str(), &err);
    if (ok) {
      server.send(200, "application/json", "{\"ok\":true}");
    } else {
      String resp = "{\"ok\":false,\"error\":\"";
      if (err == 1) resp += "no_space";
      else if (err == 2) resp += "open_failed";
      else if (err == 3) resp += "write_failed";
      else if (err == 4) resp += "rename_failed";
      else resp += "unknown";
      resp += "\"}";
      server.send(200, "application/json", resp);
    }
  });

  server.on("/loadQueue", HTTP_POST, [](){
    String body = server.arg("plain");
    // Expect JSON body: { "lane": <n>, "name": "..." }
    int lane = (int)extractJsonLong(body, "lane", -1);
    String name = extractJsonString(body, "name", "");
    Serial.printf("[HTTP] /loadQueue body=%s\n", body.c_str());
    Serial.printf("[HTTP] /loadQueue parsed lane=%d name=%s\n", lane, name.c_str());
    String json;
    if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
      json = "{\"ok\":false,\"error\":\"invalid lane\"}";
      server.send(200, "application/json", json);
      return;
    }
    if (name.length() == 0) {
      json = "{\"ok\":false,\"error\":\"missing name\"}";
      server.send(200, "application/json", json);
      return;
    }
      int added = 0;
      int skipped = 0;
      int failed = 0;
      bool ok = loadQueueFromSlot(lane, name.c_str(), &added, &skipped, &failed);
      String resp = "{";
      resp += "\"ok\":"; resp += (ok ? "true" : "false"); resp += ",";
      resp += "\"added\":"; resp += String(added); resp += ",";
      resp += "\"skipped\":"; resp += String(skipped); resp += ",";
      resp += "\"failed\":"; resp += String(failed);
      resp += "}";
      server.send(200, "application/json", resp);
  });

    // Static variables shared between upload handler and POST callback
    static String uploadError = "";
    static String uploadPath = "";

    // Upload saved queue: POST /uploadQueue with multipart file
    server.on("/uploadQueue", HTTP_POST, [](){
      // This is the final callback after upload completes
      if (uploadError.length() > 0) {
        Serial.printf("[HTTP] /uploadQueue failed: %s\n", uploadError.c_str());
        server.send(200, "application/json", "{\"ok\":false,\"error\":\"" + uploadError + "\"}");
        uploadError = ""; // Clear for next upload
        uploadPath = "";
        return;
      }

      if (uploadPath.length() == 0) {
        server.send(400, "application/json", "{\"ok\":false,\"error\":\"No file uploaded\"}");
        return;
      }

      // Validate the uploaded file is valid JSON and matches our swim set format
      if (!SPIFFS.begin(true)) {
        server.send(200, "application/json", "{\"ok\":false,\"error\":\"SPIFFS mount failed\"}");
        uploadPath = "";
        return;
      }

      File f = SPIFFS.open(uploadPath, FILE_READ);
      if (!f) {
        server.send(200, "application/json", "{\"ok\":false,\"error\":\"Failed to read uploaded file\"}");
        uploadPath = "";
        return;
      }

      size_t sz = f.size();
      DynamicJsonDocument doc(sz + 1024);
      DeserializationError err = deserializeJson(doc, f);
      f.close();

      if (err) {
        SPIFFS.remove(uploadPath);
        String errMsg = "Invalid JSON: ";
        errMsg += err.c_str();
        server.send(200, "application/json", "{\"ok\":false,\"error\":\"" + errMsg + "\"}");
        Serial.printf("[HTTP] /uploadQueue: JSON parse error: %s\n", err.c_str());
        uploadPath = "";
        return;
      }

      // Validate it's an array
      JsonArray arr = doc.as<JsonArray>();
      if (!arr) {
        SPIFFS.remove(uploadPath);
        server.send(200, "application/json", "{\"ok\":false,\"error\":\"Not a valid swim set file\"}");
        Serial.println("[HTTP] /uploadQueue: Not a JSON array");
        uploadPath = "";
        return;
      }

      // Validate array items have swim set structure
      for (JsonObject obj : arr) {
        if (!obj.containsKey("type") || !obj.containsKey("uniqueId") || !obj.containsKey("swimmers")) {
          SPIFFS.remove(uploadPath);
          server.send(200, "application/json", "{\"ok\":false,\"error\":\"Not a valid swim set file format\"}");
          Serial.println("[HTTP] /uploadQueue: Missing required swim set fields");
          uploadPath = "";
          return;
        }
      }

      Serial.printf("[HTTP] /uploadQueue completed successfully: %s\n", uploadPath.c_str());
      server.send(200, "application/json", "{\"ok\":true}");
      uploadPath = ""; // Clear for next upload
    }, [](){
      // This is the upload handler callback

      HTTPUpload& upload = server.upload();
      static File uploadFile;

      if (upload.status == UPLOAD_FILE_START) {
        // Clear any previous error/path
        uploadError = "";
        uploadPath = "";

        String filename = upload.filename;
        if (!filename.endsWith(".json")) {
          filename += ".json";
        }
        // Sanitize the filename
        String sanitized = sanitizeName(filename.substring(0, filename.length() - 5));
        uploadPath = "/saves/" + sanitized + ".json";

        Serial.printf("[HTTP] /uploadQueue start: %s -> %s\n", filename.c_str(), uploadPath.c_str());

        if (!SPIFFS.begin(true)) {
          Serial.println("[HTTP] /uploadQueue: SPIFFS mount failed");
          uploadError = "SPIFFS mount failed";
          uploadPath = "";
          return;
        }

        // Check available space
        size_t total = SPIFFS.totalBytes();
        size_t used = SPIFFS.usedBytes();
        size_t available = total - used;

        Serial.printf("[HTTP] /uploadQueue: SPIFFS space: %u total, %u used, %u available\n",
                      (unsigned)total, (unsigned)used, (unsigned)available);

        // Reject if less than 10KB available
        if (available < 10240) {
          Serial.printf("[HTTP] /uploadQueue: Insufficient space (%u bytes available)\n", (unsigned)available);
          uploadError = "Out of storage space on device";
          uploadPath = "";
          return;
        }

        // Ensure saves directory exists
        if (!SPIFFS.exists("/saves")) {
          SPIFFS.mkdir("/saves");
        }

        uploadFile = SPIFFS.open(uploadPath, FILE_WRITE);
        if (!uploadFile) {
          Serial.printf("[HTTP] /uploadQueue: failed to create file %s\n", uploadPath.c_str());
          uploadError = "Failed to create file";
          uploadPath = "";
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) {
          size_t written = uploadFile.write(upload.buf, upload.currentSize);
          if (written != upload.currentSize) {
            Serial.printf("[HTTP] /uploadQueue write error: expected %u, wrote %u\n",
                          upload.currentSize, written);
            uploadFile.close();
            SPIFFS.remove(uploadPath);
            uploadError = "Write failed - out of storage space";
            uploadPath = "";
            return;
          }
          Serial.printf("[HTTP] /uploadQueue write: %d bytes\n", upload.currentSize);
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) {
          uploadFile.close();
          Serial.printf("[HTTP] /uploadQueue complete: %d bytes total\n", upload.totalSize);
          // uploadPath is already set and will be used by final handler
        } else {
          uploadError = "Upload file handle was null";
          uploadPath = "";
        }
      }
    });

    // Download saved queue: GET /downloadQueue?name=...
    server.on("/downloadQueue", HTTP_GET, [](){
      String name = server.arg("name");
      Serial.printf("[HTTP] /downloadQueue name=%s\n", name.c_str());

      if (name.length() == 0) {
        server.send(400, "text/plain", "Missing name parameter");
        return;
      }

      if (!SPIFFS.begin(true)) {
        server.send(500, "text/plain", "SPIFFS mount failed");
        return;
      }

      String path = "/saves/" + name + ".json";
      if (SPIFFS.exists(path)) {
        File file = SPIFFS.open(path, "r");
        if (file) {
          server.streamFile(file, "application/json");
          file.close();
          Serial.printf("[HTTP] /downloadQueue: Successfully served %s\n", path.c_str());
        } else {
          server.send(500, "text/plain", "Failed to open file");
        }
      } else {
        server.send(404, "text/plain", "File not found");
        Serial.printf("[HTTP] /downloadQueue: File not found %s\n", path.c_str());
      }
    });

    // Delete saved queue: POST /deleteQueue { "name": "..." }
    server.on("/deleteQueue", HTTP_POST, [](){
      String body = server.arg("plain");
      String name = extractJsonString(body, "name", "");
      Serial.printf("[HTTP] /deleteQueue body=%s\n", body.c_str());
      Serial.printf("[HTTP] /deleteQueue parsed name=%s\n", name.c_str());

      if (name.length() == 0) {
        server.send(200, "application/json", "{\"ok\":false,\"error\":\"missing name\"}");
        return;
      }

      if (!SPIFFS.begin(true)) {
        server.send(200, "application/json", "{\"ok\":false,\"error\":\"SPIFFS mount failed\"}");
        return;
      }

      String path = "/saves/" + name + ".json";
      if (SPIFFS.exists(path)) {
        bool removed = SPIFFS.remove(path);
        if (removed) {
          Serial.printf("[HTTP] /deleteQueue: Successfully deleted %s\n", path.c_str());
          server.send(200, "application/json", "{\"ok\":true}");
        } else {
          Serial.printf("[HTTP] /deleteQueue: Failed to delete %s\n", path.c_str());
          server.send(200, "application/json", "{\"ok\":false,\"error\":\"delete failed\"}");
        }
      } else {
        Serial.printf("[HTTP] /deleteQueue: File not found %s\n", path.c_str());
        server.send(200, "application/json", "{\"ok\":false,\"error\":\"file not found\"}");
      }
    });

    // List saved slots: POST /listSaves { "lane": <n> } (lane ignored)
    server.on("/listSaves", HTTP_POST, [](){
      // We accept a lane parameter for compatibility, but saves are lane-agnostic.
      String body = server.arg("plain");
      int lane = (int)extractJsonLong(body, "lane", -1);
      if (!SPIFFS.begin(true)) {
        server.send(200, "application/json", "{\"ok\":false,\"error\":\"SPIFFS mount failed\"}");
        return;
      }

      Serial.printf("[HTTP] /listSaves called (lane param=%d)\n", lane);

      File root = SPIFFS.open("/saves");
      if (!root) {
        Serial.println("[HTTP] /listSaves: /saves directory not found or empty");
        server.send(200, "application/json", "{\"ok\":true,\"saves\":[]}");
        return;
      }

      String out = "{\"ok\":true,\"saves\":[";
      File f = root.openNextFile();
      bool first = true;
      while (f) {
        String fname = f.name();
        Serial.printf("[HTTP] /listSaves found file: %s\n", fname.c_str());
        // Expecting names like /saves/<name>.json
        if (fname.endsWith(".json")) {
          // strip directory and extension
          int slash = fname.lastIndexOf('/');
          String display = (slash >= 0) ? fname.substring(slash + 1, fname.length() - 5) : fname.substring(0, fname.length() - 5);
          if (!first) out += ",";
          out += "{\"file\":\"" + fname + "\",\"name\":\"" + display + "\"}";
          first = false;
        }
        f = root.openNextFile();
      }
      out += "]}";
      server.send(200, "application/json", out);
    });

  // Miscellaneous handlers
  server.on("/resetLane", HTTP_POST, handleResetLane);

  server.begin();
  LOGLN("Web server started");
}

// Web Handlers ******************************************

void handleRoot() {
  LOGLN("handleRoot() called - serving main page");

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
    LOGLN("ERROR: SPIFFS.begin() failed!");
  }

  // Fallback if SPIFFS fails
  LOGLN("SPIFFS file not found, serving fallback HTML");
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
  json += "\"gapBetweenStrips\":" + String(globalConfigSettings.gapBetweenStrips) + ",";
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
  LOG(" handleGetSettings: numSwimmersPerLane:");
  for (int li = 0; li < MAX_LANES_SUPPORTED; li++) {
    json += String(globalConfigSettings.numSwimmersPerLane[li]);
    if (li < MAX_LANES_SUPPORTED - 1) json += ",";
    LOG(" " + String(globalConfigSettings.numSwimmersPerLane[li]));
  }
  LOGLN();
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
  json += "\"surfaceColor\":\"" + surfaceHex + "\",";
  json += "\"firstUnderwaterDistanceFeet\":" + String(globalConfigSettings.firstUnderwaterDistanceFeet, 2) + ",";
  json += "\"underwaterDistanceFeet\":" + String(globalConfigSettings.underwaterDistanceFeet, 2) + ",";
  json += "\"underwatersSizeFeet\":" + String(globalConfigSettings.underwatersSizeFeet, 2) + ",";
  json += "\"hideAfterSeconds\":" + String(globalConfigSettings.hideAfterSeconds, 2);
  json += "}";

  server.send(200, "application/json", json);
}

void handleSetBrightness() {
  if (server.hasArg("brightness")) {
    globalConfigSettings.brightness = server.arg("brightness").toInt();
    if (DEBUG_ENABLED) {
      LOG("handleSetBrightness: updated brightness=");
      LOGLN(globalConfigSettings.brightness);
    }
    FastLED.setBrightness(globalConfigSettings.brightness);
    saveGlobalConfigSettings();
    server.send(200, "text/plain", "Brightness updated");
  } else {
    server.send(200, "text/plain", "Missing brightness parameter");
  }
}

void handleSetPulseWidth() {
  if (server.hasArg("pulseWidth")) {
    globalConfigSettings.pulseWidthFeet = server.arg("pulseWidth").toFloat();
    if (DEBUG_ENABLED) {
      LOG("handleSetPulseWidth: updated pulseWidthFeet=");
      LOGLN(globalConfigSettings.pulseWidthFeet);
    }
    saveGlobalConfigSettings();
    server.send(200, "text/plain", "Pulse width updated");
  } else {
    server.send(200, "text/plain", "Missing pulseWidth parameter");
  }
}

void handleSetStripLength() {
  if (server.hasArg("stripLengthMeters")) {
    globalConfigSettings.stripLengthMeters = server.arg("stripLengthMeters").toFloat();
    if (DEBUG_ENABLED) {
      LOG("handleSetStripLength: updated stripLengthMeters=");
      LOGLN(globalConfigSettings.stripLengthMeters);
    }
    saveGlobalConfigSettings();
    needsRecalculation = true;
    server.send(200, "text/plain", "Strip length updated");
  } else {
    server.send(200, "text/plain", "Missing stripLengthMeters parameter");
  }
}

void handleSetNumLedStrips() {
  if (server.hasArg("lane") && server.hasArg("numLedStrips")) {
    int lane = server.arg("lane").toInt();
    if (lane >= 0 && lane < MAX_LANES_SUPPORTED) {
      globalConfigSettings.numLedStrips[lane] = server.arg("numLedStrips").toInt();
      if (DEBUG_ENABLED) {
        LOG("handleSetNumLedStrips: updated numLedStrips=");
        LOGLN(globalConfigSettings.numLedStrips[lane]);
      }
      saveGlobalConfigSettings();
      needsRecalculation = true;
      server.send(200, "text/plain", "Number of LED strips updated");
    } else {
      server.send(200, "text/plain", "Invalid lane parameter");
    }
  } else {
    server.send(200, "text/plain", "Missing lane or numLedStrips parameter");
  }
}

void handleSetGapBetweenStrips() {
  if (server.hasArg("gapBetweenStrips")) {
    globalConfigSettings.gapBetweenStrips = server.arg("gapBetweenStrips").toInt();
    if (DEBUG_ENABLED) {
      LOG("handleSetGapBetweenStrips: updated gapBetweenStrips=");
      LOGLN(globalConfigSettings.gapBetweenStrips);
    }
    saveGlobalConfigSettings();
    needsRecalculation = true;
    server.send(200, "text/plain", "Gap between strips updated");
  } else {
    server.send(200, "text/plain", "Missing gapBetweenStrips parameter");
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
      LOG("handleSetPoolLength: updated poolLength=");
      LOG(globalConfigSettings.poolLength);
      LOGLN(globalConfigSettings.poolUnitsYards ? " yards" : " meters");
    }

    saveGlobalConfigSettings();
    needsRecalculation = true;
    // Update laps per round instead of full reinitialization
    updateSwimmersLapsPerRound();
    server.send(200, "text/plain", "Pool length updated");
  } else {
    server.send(200, "text/plain", "Missing poolLength or poolLengthUnits parameter");
  }
}

void handleSetLedsPerMeter() {
  if (server.hasArg("ledsPerMeter")) {
    globalConfigSettings.ledsPerMeter = server.arg("ledsPerMeter").toInt();
    if (DEBUG_ENABLED) {
      LOG("handleSetLedsPerMeter: updated ledsPerMeter=");
      LOGLN(globalConfigSettings.ledsPerMeter);
    }
    saveGlobalConfigSettings();
    needsRecalculation = true;
    server.send(200, "text/plain", "LEDs per meter updated");
  } else {
    server.send(200, "text/plain", "Missing ledsPerMeter parameter");
  }
}

void handleSetNumLanes() {
  if (server.hasArg("numLanes")) {;
    globalConfigSettings.numLanes = server.arg("numLanes").toInt();
    if (DEBUG_ENABLED) {
      LOG("handleSetNumLanes: updated numLanes=");
      LOGLN(globalConfigSettings.numLanes);
    }
    saveGlobalConfigSettings();
    server.send(200, "text/plain", "Number of lanes updated");
  } else {
    server.send(200, "text/plain", "Missing numLanes parameter");
  }
}

void handleSetSwimTime() {
  if (server.hasArg("swimTime")) {
    swimSetSettings.swimTimeSeconds = server.arg("swimTime").toInt();
    if (DEBUG_ENABLED) {
      LOG("handleSetSwimTime: updated swimTime=");
      LOGLN(swimSetSettings.swimTimeSeconds);
    }
    saveSwimSetSettings();
    server.send(200, "text/plain", "Swim time updated");
  } else {
    server.send(200, "text/plain", "Missing swimTime parameter");
  }
}

void handleSetRestTime() {
  if (server.hasArg("restTime")) {
    swimSetSettings.restTimeSeconds = server.arg("restTime").toInt();
    if (DEBUG_ENABLED) {
      LOG("handleSetRestTime: updated restTime=");
      LOGLN(swimSetSettings.restTimeSeconds);
    }
    saveSwimSetSettings();
    server.send(200, "text/plain", "Rest time updated");
  } else {
    server.send(200, "text/plain", "Missing restTime parameter");
  }
}

void handleSetSwimDistance() {
  if (server.hasArg("swimDistance")) {
    swimSetSettings.swimSetDistance = server.arg("swimDistance").toInt();
    if (DEBUG_ENABLED) {
      LOG("handleSetSwimDistance: updated swimDistance=");
      LOGLN(swimSetSettings.swimSetDistance);
    }
    saveSwimSetSettings();
    server.send(200, "text/plain", "Swim distance updated");
  } else {
    server.send(200, "text/plain", "Missing swimDistance parameter");
  }
}

void handleSetSwimmerInterval() {
  if (server.hasArg("swimmerInterval")) {
    swimSetSettings.swimmerIntervalSeconds = server.arg("swimmerInterval").toInt();
    if (DEBUG_ENABLED) {
      LOG("handleSetSwimmerInterval: updated swimmerInterval=");
      LOGLN(swimSetSettings.swimmerIntervalSeconds);
    }
    saveSwimSetSettings();
    server.send(200, "text/plain", "Swimmer interval updated");
  } else {
    server.send(200, "text/plain", "Missing swimmerInterval parameter");
  }
}

void handleSetDelayIndicators() {
  if (server.hasArg("enabled")) {
    globalConfigSettings.delayIndicatorsEnabled = (server.arg("enabled") == "true");
    if (DEBUG_ENABLED) {
      LOG("handleSetDelayIndicators: updated delayIndicatorsEnabled=");
      LOGLN(globalConfigSettings.delayIndicatorsEnabled);
    }
    saveGlobalConfigSettings();
    server.send(200, "text/plain", "Delay indicators updated");
  } else {
    server.send(200, "text/plain", "Missing enabled parameter");
  }
}

void handleSetNumSwimmers() {
  if (server.hasArg("numSwimmers")) {
    int lane = server.arg("lane").toInt();
    if (lane >= 0 && lane < MAX_LANES_SUPPORTED) {
      globalConfigSettings.numSwimmersPerLane[lane] = server.arg("numSwimmers").toInt();
      if (DEBUG_ENABLED) {
        LOG("handleSetNumSwimmers: updated numSwimmers for lane "); LOG(lane);
        LOG(" to "); LOGLN(globalConfigSettings.numSwimmersPerLane[lane]);
      }
      saveGlobalConfigSettings();
      server.send(200, "text/plain", "Number of swimmers updated");
    } else {
      server.send(200, "text/plain", "Invalid lane parameter");
    }
  } else {
    server.send(200, "text/plain", "Missing numSwimmers parameter");
  }
}

void handleSetNumRounds() {
  if (server.hasArg("numRounds")) {
    swimSetSettings.numRounds = server.arg("numRounds").toInt();
    if (DEBUG_ENABLED) {
      LOG("handleSetNumRounds: updated numRounds=");
      LOGLN(swimSetSettings.numRounds);
    }
    saveSwimSetSettings();
    server.send(200, "text/plain", "Number of numRounds updated");
  } else {
    server.send(200, "text/plain", "Missing numRounds parameter");
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
      LOG("handleSetColorMode: updated sameColorMode=");
      LOGLN(globalConfigSettings.sameColorMode);
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
    server.send(200, "text/plain", "Missing colorMode parameter");
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
      LOG("handleSetSwimmerColor: updated color=[");
      LOG(r); LOG(",");
      LOG(g); LOG(",");
      LOG(b); LOGLN("]");
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
    server.send(200, "text/plain", "Missing color parameter");
  }
}

void handleSetSwimmerColors() {
  if (server.hasArg("lane") && server.hasArg("colors")) {
    int lane = server.arg("lane").toInt();
    if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
      server.send(200, "text/plain", "Invalid lane parameter");
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
    server.send(200, "text/plain", "Missing colors parameter");
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
    LOGLN(false);
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
  LOGLN("/enqueueSwimSet ENTER");
  // Read raw body
  String body = server.arg("plain");
  int lane = parseLaneFromBody(body);
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
    return;
  }
  SwimSet s;
  s.numRounds = (uint8_t)extractJsonLong(body, "numRounds", swimSetSettings.numRounds);
  s.swimDistance = (uint16_t)extractJsonLong(body, "swimDistance", swimSetSettings.swimSetDistance);
  s.swimTime = (uint16_t)extractJsonLong(body, "swimTime", swimSetSettings.swimTimeSeconds);
  s.restTime = (uint16_t)extractJsonLong(body, "restTime", swimSetSettings.restTimeSeconds);
  s.swimmerInterval = (uint16_t)extractJsonLong(body, "swimmerInterval", swimSetSettings.swimmerIntervalSeconds);
  s.type = (uint8_t)extractJsonLong(body, "type", SWIMSET_TYPE);
  s.status = SWIMSET_STATUS_PENDING;
  s.repeatRemaining = (uint8_t)extractJsonLong(body, "repeatRemaining", 0);

  s.uniqueId = 0ULL;
  String uniqueIdStr = extractJsonString(body, "uniqueId", "");
  if (uniqueIdStr.length() > 0) {
    unsigned long long parsed = parseUniqueIdHex(uniqueIdStr);
    if (parsed != 0ULL) {
      s.uniqueId = parsed;
    } else {
      LOGLN(" Warning: invalid uniqueId format: " + uniqueIdStr);
      server.send(200, "application/json", "{\"ok\":false,\"error\":\"invalid uniqueId format\"}");
      return;
    }
  } else {
    LOGLN(" Warning: missing uniqueId");
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"missing uniqueId\"}");
    return;
  }

  s.loopFromUniqueId = 0ULL;
  String loopFromUniqueIdStr = extractJsonString(body, "loopFromUniqueId", "");
  if (loopFromUniqueIdStr.length() > 0 && loopFromUniqueIdStr != "0") {
    unsigned long long parsed = parseUniqueIdHex(loopFromUniqueIdStr);
    if (parsed != 0ULL) {
      s.loopFromUniqueId = parsed;
    } else {
      LOGLN(" Warning: invalid loopFromUniqueId format: " + loopFromUniqueIdStr);
      server.send(200, "application/json", "{\"ok\":false,\"error\":\"invalid loopFromUniqueId format\"}");
      return;
    }
  }

  // parse swimmers array if provided (use ArduinoJson for robust parsing)
  {
    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, body);
    s.swimmersCount = 0;
    if (!err && doc.containsKey("swimmers") && doc["swimmers"].is<JsonArray>()) {
      JsonArray arr = doc["swimmers"].as<JsonArray>();
      int i = 0;
      for (JsonVariant v : arr) {
        if (i >= MAX_SWIMMERS_SUPPORTED) break;
        // accept object { swimTime: N }
        if (v.is<JsonObject>() && v.containsKey("swimTime")) {
          s.swimmerTimes[i] = (uint16_t) v["swimTime"].as<int>();
          if (s.swimmerTimes[i] == s.swimTime) {
            s.swimmerTimes[i] = 0; // optimize by not doing anything special if same as set swimTime
          }
        } else {
          s.swimmerTimes[i] = 0; // default to 0 meaning use set swimTime
        }
        i++;
      }
      s.swimmersCount = (uint8_t)i;
    } else {
      // default: no per-swimmer custom times
      s.swimmersCount = 0;
    }
  }

  int result = enqueueSwimSet(s, lane);
  LOGLN(" Enqueue result: " + String((result == QUEUE_INSERT_SUCCESS) ? "OK" : "FAILED"));
  LOGLN("  Enqueued swim set: ");
  LOGLN("   lane=" + String(lane));
  LOGLN("   numRounds=" + String(s.numRounds));
  LOGLN("   swimDistance=" + String(s.swimDistance));
  LOGLN("   swimTime=" + String(s.swimTime));
  LOGLN("   restTime=" + String(s.restTime));
  LOGLN("   swimmerInterval=" + String(s.swimmerInterval));
  LOGLN("   type=" + String(s.type));
  LOGLN("   loopFromUniqueId=" + uniqueIdToHex(s.loopFromUniqueId));
  LOGLN("   repeatRemaining=" + String(s.repeatRemaining));
  LOGLN("   uniqueId=" + uniqueIdToHex(s.uniqueId));

  if (result == QUEUE_INSERT_SUCCESS) {
    Serial.printf(" ADD Set at index: %d, UID=%s, type=%d, Rounds=%d\n",
      (swimSetQueueHead[lane] + swimSetQueueCount[lane] - 1) % SWIMSET_QUEUE_MAX,
      uniqueIdToHex(s.uniqueId).c_str(),
      s.type,
      s.numRounds);
    int qCount = swimSetQueueCount[lane];
    String json = "{";
    json += "\"ok\":true,";
    json += "\"uniqueId\":\"" + uniqueIdToHex(s.uniqueId) + "\"";
    json += "}";
    server.send(200, "application/json", json);
  } else if (result == QUEUE_INSERT_DUPLICATE) {
    server.send(200, "application/json", "{\"ok\":false,\"duplicate\":true,\"errorCode\":" + String(result) + "}");
  } else {
    server.send(200, "application/json", "{\"ok\":false,\"errorCode\":" + String(result) + "}");
  }
}

// Update an existing swim set in-place by uniqueId
// Body should include matchUniqueId (string) to locate
// the queued entry. The fields length, swimTime, numRounds, restTime, type, repeat
// will be used to replace the entry. Optionally provide uniqueId to update the
// stored uniqueId for future reconciliation.
void handleUpdateSwimSet() {
  LOGLN("/updateSwimSet ENTER");
  String body = server.arg("plain");
  int lane = parseLaneFromBody(body);
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
    return;
  }
  LOGLN(" Raw body:");
  LOGLN(body);

  // Parse required fields from JSON body (no form-encoded fallback)
  SwimSet s;
  s.numRounds = (uint8_t)extractJsonLong(body, "numRounds", swimSetSettings.numRounds);
  s.swimDistance = (uint16_t)extractJsonLong(body, "swimDistance", swimSetSettings.swimSetDistance);
  s.swimTime = (uint16_t)extractJsonLong(body, "swimTime", swimSetSettings.swimTimeSeconds);
  s.restTime = (uint16_t)extractJsonLong(body, "restTime", swimSetSettings.restTimeSeconds);
  s.swimmerInterval = (uint16_t)extractJsonLong(body, "swimmerInterval", swimSetSettings.swimmerIntervalSeconds);
  s.type = (uint8_t)extractJsonLong(body, "type", 0);
  s.repeatRemaining = (uint8_t)extractJsonLong(body, "repeatRemaining", 0);

  String uniqueIdStr = extractJsonString(body, "uniqueId", "");
  unsigned long long uniqueId = 0ULL;
  if (uniqueIdStr.length() > 0) {
    uniqueId = parseUniqueIdHex(uniqueIdStr);
  } else {
    LOGLN(" Warning: missing uniqueId");
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"missing uniqueId\"}");
    return;
  }

  String loopFromUniqueIdStr = extractJsonString(body, "loopFromUniqueId", "");
  s.loopFromUniqueId = 0ULL;
  if (loopFromUniqueIdStr.length() > 0) {
    s.loopFromUniqueId = parseUniqueIdHex(loopFromUniqueIdStr);
  }

  // parse swimmers array if provided
  {
    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, body);
    s.swimmersCount = 0;
    if (!err && doc.containsKey("swimmers") && doc["swimmers"].is<JsonArray>()) {
      JsonArray arr = doc["swimmers"].as<JsonArray>();
      int i = 0;
      for (JsonVariant v : arr) {
        if (i >= MAX_SWIMMERS_SUPPORTED) break;
        if (v.is<JsonObject>() && v.containsKey("swimTime")) {
          s.swimmerTimes[i] = (uint16_t) v["swimTime"].as<int>();
          if (s.swimmerTimes[i] == s.swimTime) {
            s.swimmerTimes[i] = 0; // optimize by not doing anything special if same as set swimTime
          }
        } else {
          s.swimmerTimes[i] = 0; // default to 0 meaning use set swimTime
        }
        i++;
      }
      s.swimmersCount = (uint8_t)i;
    } else {
      s.swimmersCount = 0;
    }
  }

  LOGLN("  received payload to update set: ");
  LOG(" numRounds="); LOGLN(s.numRounds);
  LOG(" swimDistance="); LOGLN(s.swimDistance);
  LOG(" swimTime="); LOGLN(s.swimTime);
  LOG(" restTime="); LOGLN(s.restTime);
  LOG(" swimmerInterval="); LOGLN(s.swimmerInterval);
  LOG(" type="); LOGLN(s.type);
  LOG(" repeatRemaining="); LOGLN(s.repeatRemaining);
  LOG(" loopFromUniqueId="); LOGLN(uniqueIdToHex(s.loopFromUniqueId));
  LOG(" uniqueId="); LOGLN(uniqueIdToHex(uniqueId));

  LOGLN(" Searching queue for matching entry...");
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
      entry.repeatRemaining = s.repeatRemaining;
      entry.loopFromUniqueId = s.loopFromUniqueId;
      found = true;
      entry.swimmersCount = s.swimmersCount;
      for (int si=0; si<entry.swimmersCount && si<MAX_SWIMMERS_SUPPORTED; ++si) {
        entry.swimmerTimes[si] = s.swimmerTimes[si];
      }
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

  LOGLN(" No matching entry found in queue");
  server.send(200, "application/json", "{\"ok\":false,\"error\":\"not found\"}");
}

// Delete a swim set by uniqueId for a lane.
// Accepts form-encoded or JSON: matchUniqueId=<hex>, and lane=<n>
void handleDeleteSwimSet() {
  LOGLN("/deleteSwimSet ENTER");
  String body = server.arg("plain");
  int lane = parseLaneFromBody(body);
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
    return;
  }
  LOGLN(" Received raw body: " + body);

  // Expect application/json body
  if (body.length() == 0) {
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
    return;
  }

  unsigned long long matchUniqueId = 0ULL;
  String matchUniqueIdStr = extractJsonString(body, "matchUniqueId", "");
  if (matchUniqueIdStr.length() > 0) {
    matchUniqueId = parseUniqueIdHex(matchUniqueIdStr);
  } else {
    LOGLN(" Warning: missing matchUniqueId");
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"missing matchUniqueId\"}");
    return;
  }

  // Validate lane...
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    LOGLN(" Invalid lane");
    server.send(200, "application/json", "{\"ok\":false, \"error\":\"Invalid lane\"}");
    return;
  }
  LOGLN("  lane=" + String(lane));
  LOG("  matchUniqueId="); LOGLN(matchUniqueIdStr);

  // Build vector of existing entries
  std::vector<SwimSet> existing;
  for (int i = 0; i < swimSetQueueCount[lane]; i++) {
    int idx = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
    existing.push_back(swimSetQueue[lane][idx]);
  }

  // Try match by uniqueId
  bool found = false;
  // Try match by uniqueId (hex or decimal)
  if (!found && matchUniqueId != 0ULL) {
    for (size_t i = 0; i < existing.size(); i++) {
      if (existing[i].uniqueId == matchUniqueId) {
        LOGLN(" deleteSwimSet: matched by uniqueId=" + String((unsigned long long)existing[i].uniqueId));
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

  // Perform removal logic (match by uniqueId) and respond with JSON
  if (found) {
    LOG(" deleteSwimSet: deleted entry, new queue count=");
    LOGLN(swimSetQueueCount[lane]);
    server.send(200, "application/json", "{\"ok\":true, \"message\":\"deleted\"}");
  } else {
    LOG(" deleteSwimSet: no matching entry found, nothing to delete, queue count=");
    LOGLN(String(swimSetQueueCount[lane]));
    server.send(200, "application/json", "{\"ok\":true, \"message\":\"not found\"}");
  }
}

void handleStartSwimSet() {
  LOGLN("/startSwimSet ENTER");
  String body = server.arg("plain");
  int lane = parseLaneFromBody(body);
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
    return;
  }

  // If client specified matchUniqueId, try to find that entry in the lane queue.
  String matchUniqueIdStr = extractJsonString(body, "matchUniqueId", "");
  unsigned long long matchUniqueId = 0ULL;
  if (matchUniqueIdStr.length() > 0) {
    matchUniqueId = parseUniqueIdHex(matchUniqueIdStr);
  } else {
    LOGLN(" Warning: missing matchUniqueId");
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"missing matchUniqueId\"}");
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
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"no swim set available\"}");
    return;
  }

  // record which queue index we started (will be used by applySwimSetToSettings)
  laneActiveQueueIndex[lane] = matchedQueueIndex;
  LOGLN("/startSwimSet: calling applySwimSetToSettings()");
  applySwimSetToSettings(s, lane);

  // Respond with uniqueId so client can reconcile
  String resp = "{";
  resp += "\"ok\":true,";
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
      server.send(200, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
      return;
  }
  LOG("handleStopSwimSet: for lane: ");
  LOGLN(lane);
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
  LOGLN("filling lane with all black LEDs");
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
  //LOGLN("/getSwimQueue ENTER");
  String body = server.arg("plain");
  int lane = parseLaneFromBody(body);
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
    return;
  }

  // Use ArduinoJson to build the response to avoid any string-concatenation bugs.
  // Adjust the StaticJsonDocument size if you have very large queues.
  StaticJsonDocument<DOC_SIZE> doc; // Stack-allocated, auto-freed on return

  // Build queue array for the current lane
  JsonArray q = doc.createNestedArray("queue");
  // Return ALL queued sets (not just active)
  for (int i = 0; i < swimSetQueueCount[lane]; i++) {
    int idx = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
    SwimSet &s = swimSetQueue[lane][idx];
    JsonObject item = q.createNestedObject();
    item["uniqueId"] = uniqueIdToHex(s.uniqueId);
    item["numRounds"] = s.numRounds;
    item["swimDistance"] = s.swimDistance;
    item["swimTime"] = s.swimTime;
    item["restTime"] = s.restTime;
    item["swimmerInterval"] = s.swimmerInterval;
    item["type"] = s.type;
    item["loopFromUniqueId"] = uniqueIdToHex(s.loopFromUniqueId);
    item["repeatRemaining"] = s.repeatRemaining;
    // numeric status bitmask
    item["status"] = (int)s.status;
    // include swimmers array (swimTime per swimmer) if present
    if (s.swimmersCount > 0) {
      JsonArray sw = item.createNestedArray("swimmers");
      for (int si = 0; si < s.swimmersCount && si < MAX_SWIMMERS_SUPPORTED; ++si) {
        JsonObject sv = sw.createNestedObject();
        if (s.swimmerTimes[si] == 0) {
          sv["swimTime"] = s.swimTime; // use set swimTime
        } else {
          sv["swimTime"] = s.swimmerTimes[si];
        }
      }
    }

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
  out.reserve(8192);
  serializeJson(doc, out);

  if (DEBUG_ENABLED) {
    //LOGLN(" getSwimQueue -> payload:");
    //LOGLN(out);
  }

  // Return combined payload { queue: [...], status: {...} }
  server.send(200, "application/json", out);
}

// Reorder swim queue for a lane. Expects form-encoded:
// lane=<n>&order=<uniqueId1,uniqueId2,...>
// uniqueId values should be uniqueId strings. Entries not matched
// will be appended at the end in their existing order.
void handleReorderSwimQueue() {
  LOGLN("/reorderSwimQueue ENTER");
  String body = server.arg("plain");
  int lane = parseLaneFromBody(body);
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
    return;
  }

  // Extract order parameter - attempt to find "order": or order= in form-encoded
  String orderStr = "";
  if (body.indexOf("order=") >= 0) {
    int idx = body.indexOf("order=");
    int start = idx + sizeof("order=");
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
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"missing/invalid order\"}");
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

  // Helper to match token to an existing entry by uniqueId
  auto matchAndConsume = [&](const String &tok) -> bool {
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
    LOG("reorderSwimQueue: lane "); LOG(lane); LOG(" newCount="); LOGLN(swimSetQueueCount[lane]);
  }

  String json = "{";
    json += "\"ok\":true";
    json += "}";
  server.send(200, "application/json", json);
}

// Reset swimmers for a given lane to starting wall (position=0, lapDirection=1)
void handleResetLane() {
  String body = server.arg("plain");
  int lane = parseLaneFromBody(body);
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) {
    server.send(200, "application/json", "{\"ok\":false,\"error\":\"missing/invalid lane\"}");
    return;
  }

  // Reset swimmers for this lane
  for (int i = 0; i < MAX_SWIMMERS_SUPPORTED; i++) {
    swimmers[lane][i].position = 0;
    swimmers[lane][i].lapDirection = 1;
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
  preferences.putInt("gapBetweenStrips", globalConfigSettings.gapBetweenStrips);
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
    LOGLN("saveSwimSetSettings: invalid swimmerIntervalSeconds <= 0, resetting to 4");
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
  globalConfigSettings.stripLengthMeters = preferences.getFloat("stripLengthM", 5.0);
  for (int li = 0; li < MAX_LANES_SUPPORTED; li++) {
    String key = String("numLedStrips") + String(li);
    globalConfigSettings.numLedStrips[li] = preferences.getInt(key.c_str(), 1);
  }
  globalConfigSettings.gapBetweenStrips = preferences.getInt("gapBetweenStrips", 23);
  globalConfigSettings.ledsPerMeter = preferences.getInt("ledsPerMeter", 30);
  globalConfigSettings.pulseWidthFeet = preferences.getFloat("pulseWidthFeet", 1.0);
  // Keep preference fallbacks consistent with struct defaults
  // Load per-lane counts if present (key changed to "swimLaneX" to fit 15-char NVS limit)
  LOG(" loadGlobalConfigSettings: numSwimmersPerLane:");
  for (int li = 0; li < MAX_LANES_SUPPORTED; li++) {
    String key = String("swimLane") + String(li);
    globalConfigSettings.numSwimmersPerLane[li] = preferences.getInt(key.c_str(), DEFAULT_NUM_SWIMMERS);
    LOG(" " + String(globalConfigSettings.numSwimmersPerLane[li]));
  }
  LOGLN();
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

// Sanitize a user-provided name to a safe filename portion (replace non-alnum with '_')
static String sanitizeName(const String &name) {
  String out;
  out.reserve(name.length());
  for (size_t i = 0; i < name.length(); i++) {
    char c = name.charAt(i);
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-') {
      out += c;
    } else {
      out += '_';
    }
  }
  return out;
}

// Save the current lane's queue to SPIFFS slot file: /saves/<name>.json
// The saved queue is lane-agnostic and can be loaded into any lane.
bool saveQueueToSlot(int lane, const char *name, int *outError) {
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) return false;
  String sname = sanitizeName(String(name));
  String path = String("/saves/") + sname + String(".json");

  Serial.printf("[saveQueueToSlot] lane=%d name=%s path=%s\n", lane, name, path.c_str());

  if (!SPIFFS.begin(true)) return false;

  // Build JSON array of queued swim sets for this lane
  StaticJsonDocument<DOC_SIZE> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < swimSetQueueCount[lane]; i++) {
    int actualIdx = (swimSetQueueHead[lane] + i) % SWIMSET_QUEUE_MAX;
    SwimSet &ss = swimSetQueue[lane][actualIdx];
    JsonObject o = arr.createNestedObject();
    o["type"] = ss.type;
    o["uniqueId"] = uniqueIdToHex(ss.uniqueId);
    // lane is implicit from the save filename; do not store per-set lane here
    o["numRounds"] = ss.numRounds;
    o["swimDistance"] = ss.swimDistance;
    o["swimTime"] = ss.swimTime;
    o["restTime"] = ss.restTime;
    o["repeatRemaining"] = ss.repeatRemaining;
    o["loopFromUniqueId"] = uniqueIdToHex(ss.loopFromUniqueId);
    JsonArray sw = o.createNestedArray("swimmers");
    for (int j = 0; j < ss.swimmersCount && j < MAX_SWIMMERS_SUPPORTED; j++) {
      JsonObject swp = sw.createNestedObject();
      swp["swimTimeMs"] = ss.swimmerTimes[j];
    }
  }

  // Ensure saves directory exists
  if (!SPIFFS.exists("/saves")) {
    SPIFFS.mkdir("/saves");
  }

  // Check available storage space before writing using SPIFFS API
  // Estimate JSON size using ArduinoJson utility
  size_t estSize = measureJson(doc) + 1024; // add margin for formatting
  size_t total = SPIFFS.totalBytes();
  size_t used = SPIFFS.usedBytes();
  if (total > 0 && (total - used) < estSize) {
    // Not enough space
    if (outError) *outError = 1; // no space
    Serial.printf("[saveQueueToSlot] not enough space: total=%u used=%u est=%u\n", (unsigned)total, (unsigned)used, (unsigned)estSize);
    return false;
  }

  // Atomic write
  String tmpPath = path + String(".tmp");
  File tmp = SPIFFS.open(tmpPath, FILE_WRITE);
  if (!tmp) {
    if (outError) *outError = 2; // open failed
    Serial.printf("[saveQueueToSlot] failed to open tmp file: %s\n", tmpPath.c_str());
    return false;
  }
  if (serializeJson(doc, tmp) == 0) {
    tmp.close();
    SPIFFS.remove(tmpPath);
    if (outError) *outError = 3; // write failed
    Serial.printf("[saveQueueToSlot] serializeJson returned 0 for path: %s\n", tmpPath.c_str());
    return false;
  }
  tmp.close();
  SPIFFS.remove(path);
  bool renamed = SPIFFS.rename(tmpPath, path);
  if (!renamed && outError) *outError = 4; // rename failed
  if (renamed) {
    Serial.printf("[saveQueueToSlot] renamed %s -> %s\n", tmpPath.c_str(), path.c_str());
  } else {
    Serial.printf("[saveQueueToSlot] rename failed %s -> %s\n", tmpPath.c_str(), path.c_str());
  }
  return renamed;
}

// Load queue JSON from a slot file and replace the lane's queue with its contents
// Load queue JSON from a slot file and append entries to the lane's queue.
// Returns true on overall success; additionally reports counts via out parameters.
bool loadQueueFromSlot(int lane, const char *name, int *outAdded, int *outSkipped, int *outFailed) {
  if (lane < 0 || lane >= MAX_LANES_SUPPORTED) return false;
  String sname = sanitizeName(String(name));
  String path = String("/saves/") + sname + String(".json");
  Serial.printf("[loadQueueFromSlot] lane=%d name=%s path=%s\n", lane, name, path.c_str());

  if (!SPIFFS.begin(true)) return false;
  if (!SPIFFS.exists(path)) return false;
  if (!SPIFFS.exists(path)) {
    Serial.printf("[loadQueueFromSlot] file does not exist: %s\n", path.c_str());
    return false;
  }

  File f = SPIFFS.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[loadQueueFromSlot] failed to open file: %s\n", path.c_str());
    return false;
  }

  size_t sz = f.size();
  DynamicJsonDocument doc(sz + 1024);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  JsonArray arr = doc.as<JsonArray>();
  if (!arr) return false;

  int added = 0;
  int skipped = 0;
  int failed = 0;

  for (JsonObject o : arr) {
    SwimSet s;
    s.type = o["type"] | SWIMSET_TYPE;
    String uid = o["uniqueId"].as<String>();
    s.uniqueId = parseUniqueIdHex(uid);
    s.numRounds = o["numRounds"] | 1;
    s.swimDistance = o["swimDistance"] | 25;
    s.swimTime = o["swimTime"] | swimSetSettings.swimTimeSeconds;
    s.restTime = o["restTime"] | swimSetSettings.restTimeSeconds;
    s.repeatRemaining = o["repeatRemaining"] | 0;
    String loopFrom = o["loopFromUniqueId"].as<String>();
    s.loopFromUniqueId = parseUniqueIdHex(loopFrom);
    s.swimmersCount = 0;
    JsonArray sw = o["swimmers"];
    for (JsonObject swp : sw) {
      if (s.swimmersCount < MAX_SWIMMERS_SUPPORTED) {
        s.swimmerTimes[s.swimmersCount] = swp["swimTimeMs"] | 0;
        s.swimmersCount++;
      }
    }
    // Attempt enqueue; respect duplicate and full errors
    int result = enqueueSwimSet(s, lane);
    if (result == QUEUE_INSERT_SUCCESS) {
      added++;
      Serial.printf("[loadQueueFromSlot] enqueued set uid=%s lane=%d\n", uid.c_str(), lane);
    } else if (result == QUEUE_INSERT_DUPLICATE) {
      skipped++;
      Serial.printf("[loadQueueFromSlot] skipped duplicate uid=%s lane=%d\n", uid.c_str(), lane);
    } else {
      failed++;
      Serial.printf("[loadQueueFromSlot] failed to enqueue uid=%s lane=%d ret=%d\n", uid.c_str(), lane, result);
    }
  }

  if (outAdded) *outAdded = added;
  if (outSkipped) *outSkipped = skipped;
  if (outFailed) *outFailed = failed;

  return true;
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
  LOG("recalculateValues: lane="); LOGLN(lane);
  // Only acquire lock if render task is running (avoid deadlock during setup)
  bool needLock = renderTaskStarted;
  if (needLock && renderLock) xSemaphoreTake(renderLock, portMAX_DELAY);

  if (lane >= globalConfigSettings.numLanes) {
    if (scanoutLEDs[lane] != nullptr) {
      delete[] scanoutLEDs[lane];
      scanoutLEDs[lane] = nullptr;
    }
    if (needLock && renderLock) xSemaphoreGive(renderLock);
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
    LOG(" visibleLEDs changed from "); LOG(visibleLEDs[lane]);
    LOG(" to "); LOGLN(fullLengthLEDs[lane] - totalMissingLEDs);
    // TODO: Should we reset FastLED?
  }
  visibleLEDs[lane] = fullLengthLEDs[lane] - totalMissingLEDs;

  // Get the individual index values of the LEDs that are in a gap
  gapLEDs[lane].clear();
  gapLEDs[lane].reserve(64);
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

  poolLengthLEDs = (int)(poolLengthInMeters * globalConfigSettings.ledsPerMeter);

  // Build copySegments (contiguous segments in renderedLEDs -> scanoutLEDs)
  copySegments[lane].clear();
  copySegments[lane].reserve(8);
  if (fullLengthLEDs[lane] > 0) {
    int prev = 0;
    for (size_t gi = 0; gi < gapLEDs[lane].size(); ++gi) {
      int g = gapLEDs[lane][gi];
      if (g > prev) {
        copySegments[lane].push_back(std::make_pair(prev, g - prev));
      }
      prev = g + 1;
    }
    // final tail segment
    if (prev < fullLengthLEDs[lane]) {
      copySegments[lane].push_back(std::make_pair(prev, fullLengthLEDs[lane] - prev));
    }
  }
  // Sanity: visibleLEDs should equal sum of segment lengths
  // (optional check during debug)
  #if DEBUG_SERIAL
  {
    int tot = 0;
    for (auto &p : copySegments[lane]) tot += p.second;
    if (tot != visibleLEDs[lane]) {
      LOGLN("Warning: copySegments total != visibleLEDs");
    }
  }
  #endif

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
  LOGLN("################################");
  LOG("render delayMS="); LOGLN(delayMS);
  LOG("visibleLEDs="); LOGLN(visibleLEDs[lane]);
  LOG("fullLengthLEDs="); LOGLN(fullLengthLEDs[lane]);
  LOG("ledsPerStrip="); LOGLN(ledsPerStrip);
  LOG("ledsPerGap="); LOGLN(ledsPerGap);
  LOG("totalMissingLEDs="); LOGLN(totalMissingLEDs);
  LOG("gapLEDs count="); LOGLN(gapLEDs[lane].size());
  LOGLN("################################");

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

  // Release lock when done so renderTask can show safely
  if (needLock && renderLock) xSemaphoreGive(renderLock);
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
  profStart("updateLEDEffect");
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

  // Throttle FastLED.show() so it runs at most once per minFastLEDShowIntervalMs.
  // Use delayMS (time per-LED derived from pace) as a sensible lower bound.
  unsigned long now = millis();
  unsigned long targetMin = (unsigned long)max((int)minFastLEDShowIntervalMs, delayMS);
  if (now - lastFastLEDShowMs >= targetMin) {
    // Signal render task to perform show; renderSemaphore is a binary semaphore.
    // Multiple gives are safe (binary semaphore will stay available), so no extra guard needed.
    if (renderSemaphore != NULL) xSemaphoreGive(renderSemaphore);
  }

  profEnd("updateLEDEffect");
}

void spliceOutGaps(int lane) {
  // Try to acquire renderLock non-blocking so we don't contend with render task.
  // If renderLock is taken (render in progress) we skip copying this frame; render task will show
  // previous scanout buffer and next frame will update scanout again.
  bool locked = false;
  if (renderLock != NULL) {
    if (xSemaphoreTake(renderLock, (TickType_t)0) == pdTRUE) {
      locked = true;
    } else {
      // couldn't acquire lock -> skip splice this frame to avoid concurrent mutation
      return;
    }
  }

  // Fast path: if no gaps, single memcpy
  if (copySegments[lane].empty()) {
    memcpy(scanoutLEDs[lane], renderedLEDs[lane], visibleLEDs[lane] * sizeof(CRGB));
    if (locked) xSemaphoreGive(renderLock);
    return;
  }

  // Use precomputed segments to perform few memcpy calls instead of per-LED copying
  int dst = 0;
  for (const auto &seg : copySegments[lane]) {
    int srcStart = seg.first;
    int len = seg.second;
    memcpy(&scanoutLEDs[lane][dst], &renderedLEDs[lane][srcStart], len * sizeof(CRGB));
    dst += len;
  }

  // Defensive: if we didn't fill the scanout buffer, clear the remainder
  if (dst < visibleLEDs[lane]) {
    int remaining = visibleLEDs[lane] - dst;
    if (remaining > 0) {
      // FastLED helper - efficient fill of CRGB array
      fill_solid(&scanoutLEDs[lane][dst], remaining, CRGB::Black);
      // Fallback alternative (if you prefer raw memset and know CRGB == 0 is black):
      // memset(&scanoutLEDs[lane][dst], 0, remaining * sizeof(CRGB));
    }
  }

  if (locked) xSemaphoreGive(renderLock);
}

bool drawDelayIndicators(int laneIndex, int swimmerIndex) {
  profStart("drawDelayIndicators");
  Swimmer* swimmer = &swimmers[laneIndex][swimmerIndex];

  // Only consider swimmers who are resting
  if (!swimmer->isResting || swimmer->finished) {
    profEnd("drawDelayIndicators");
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
    if (swimmer->lapDirection > 0) {
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
      for (int ledIndex = poolLengthLEDs - 1; ledIndex >= poolLengthLEDs - remainingDelayLEDs && ledIndex >= 0; ledIndex--) {
        // Are we within rendered number of LEDs?
        if (ledIndex < fullLengthLEDs[laneIndex]) {
          // Only draw delay indicator if LED is currently black (swimmers get priority)
          if (renderedLEDs[laneIndex][ledIndex] == CRGB::Black) {
            // Create a dimmed version of the swimmer's color for the delay indicator
            CRGB delayColor = swimmer->color;
            delayColor.nscale8(128); // 50% brightness for delay indicator
            renderedLEDs[laneIndex][ledIndex] = delayColor;
          }
        }
      }
    }

    profEnd("drawDelayIndicators");
    return true;
  } else {
    // No delay to draw for this swimmer
    profEnd("drawDelayIndicators");
    return false;
  }
}

void initializeSwimmers() {
  if (DEBUG_ENABLED) {
    LOGLN("\n========== INITIALIZING SWIMMERS ==========");
  }

  for (int lane = 0; lane < MAX_LANES_SUPPORTED; lane++) {
  for (int i = 0; i < MAX_SWIMMERS_SUPPORTED; i++) {
      swimmers[lane][i].position = 0;
      swimmers[lane][i].hasStarted = false;  // Initialize as not started
      swimmers[lane][i].lastPositionUpdate = 0;
      swimmers[lane][i].swimStartTime = 0; // will be set when rest completes (or 0 means start immediately)

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
      swimmers[lane][i].cachedSpeedPoolUnitsPerMs = 0.0;
      swimmers[lane][i].cachedRestMs = 0;
      swimmers[lane][i].cachedRestSeconds = 0;
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
  profStart("updateSwimmer");
  // If the lane is not running just exit
  if (!globalConfigSettings.laneRunning[laneIndex]) {
    profEnd("updateSwimmer");
    return;
  }
  Swimmer* swimmer = &swimmers[laneIndex][swimmerIndex];
  // If swimmer has already completed all numRounds, do nothing
  if (swimmer->finished) {
    profEnd("updateSwimmer");
    return;
  }
  unsigned long currentTime = millis();

  int laneSwimmerCount = globalConfigSettings.numSwimmersPerLane[laneIndex];
  // TODO: we shouldn't have to do this, it should be done once per lane elsewhere
  if (laneSwimmerCount < 1) laneSwimmerCount = 1;
  if (laneSwimmerCount > MAX_SWIMMERS_SUPPORTED) laneSwimmerCount = MAX_SWIMMERS_SUPPORTED;

  profStart("updateSwimmer - isResting");
  // Check if swimmer is resting
  // ------------------------------ SWIMMER RESTING LOGIC ------------------------------
  if (swimmer->isResting) {
    // How much time have they been resting
    unsigned long restElapsed = currentTime - swimmer->restStartTime;

    // What is our target rest duration (in milliseconds)?
    if (swimmer->expectedRestDuration == 0) {
      // TODO: We should never hit this, if we were resting we should have already set expectedRestDuration
      LOGLN("WARNING: swimmer expectedRestDuration was 0 during rest phase, calculating now");
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
        LOGLN("------------------------------------------------");
        LOG("Lane ");
        LOG(laneIndex);
        LOG(", Swimmer ");
        LOG(swimmerIndex);
        LOGLN(" - REST COMPLETE, starting to swim");
        LOG("  Rest duration: ");
        LOG(restElapsed / 1000.0);
        LOG(" sec (target: ");
        LOG(swimmer->expectedRestDuration / 1000.0);
        LOGLN(" sec)");
      }

      // Reset rest tracking
      // TODO: seems redundant to track rest duration and expected start time separately
      swimmer->isResting = false;
      swimmer->hasStarted = true;
      swimmer->restStartTime = 0;
      swimmer->expectedRestDuration = 0;
      swimmer->expectedStartTime = 0;

      long actualStartTime = swimmer->restStartTime + swimmer->expectedRestDuration;
      // Set swimStartTime to the logical start instant (accounting for overshoot)
      swimmer->swimStartTime = currentTime - actualStartTime;
      // Reset the position-update clock so the next delta is measured from now
      swimmer->lastPositionUpdate = currentTime;
      // Set total distance to 0.0
      // We don't account for distance traveled since rest period ended
      // because we will handle that in the swimming logic below
      // That is why we adjust swimStartTime above to when the rest ended
      // TODO: The down side is we may not handle this position update till
      // the next frame, causing a small jump in position
      swimmer->totalDistance = 0.0;

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
        LOGLN("================================================");
        LOG("Lane ");
        LOG(laneIndex);
        LOG(", Swimmer ");
        LOG(swimmerIndex);
        LOG(" - Round ");
        LOG(swimmer->currentRound);
        LOGLN(" - RESTING at wall");
        LOG("  Target rest duration: ");
        LOG(swimmer->expectedRestDuration / 1000.0);
        LOGLN(" seconds");
        LOG("  Elapsed rest time: ");
        LOG(restElapsed / 1000.0);
        LOGLN(" seconds");
        swimmer->debugRestingPrinted = true;
        swimmer->debugSwimmingPrinted = false; // Reset for next swim phase
      }
      profEnd("updateSwimmer");
      return;
    }
  }
  profEnd("updateSwimmer - isResting");

  // We don't use an else here because the swimmer may have just finished resting
  // ------------------------------ SWIMMER SWIMMING LOGIC ------------------------------
  // Swimmer is actively swimming
  if (currentTime - swimmer->lastPositionUpdate >= delayMS) {
    profStart("updateSwimmer - isSwimming calcs");
    // Print debug info only once when entering swimming state
    if (DEBUG_ENABLED && !swimmer->debugSwimmingPrinted && swimmerIndex < laneSwimmerCount) {
      LOGLN("------------------------------------------------");
      LOG("Lane ");
      LOG(laneIndex);
      LOG(", Swimmer ");
      LOG(swimmerIndex);
      LOG(" - SWIMMING - ");
      LOG("QI: ");
      LOG(swimmer->queueIndex);
      LOG(", R: ");
      LOG(swimmer->currentRound);
      LOG(", L: ");
      LOG(swimmer->currentLap);
      LOG("/");
      LOG(swimmer->lapsPerRound);
      LOG(", dir: ");
      LOG(swimmer->lapDirection == 1 ? "away from start" : "towards start");
      LOG(", pos: ");
      LOG(swimmer->position);
      LOG(", Dist: ");
      LOG(swimmer->totalDistance);
      LOGLN(globalConfigSettings.poolUnitsYards ? " yards" : " meters");
      swimmer->debugSwimmingPrinted = true;
      swimmer->debugRestingPrinted = false; // Reset for next rest phase
    }

    // Cheap per-frame integration using cached per-ms speeds
    unsigned long deltaMs = currentTime - swimmer->lastPositionUpdate;
    swimmer->lastPositionUpdate = currentTime;
    // distance advanced in pool units (yards or meters depending on config)
    float distanceTraveled = swimmer->cachedSpeedPoolUnitsPerMs * (float)deltaMs;
    swimmer->totalDistance += distanceTraveled;

    // Check if swimmer has completed one pool length based on actual distance
    // Calculate 1-based lap number (lap 1 = 0 to poolLength, lap 2 = poolLength to 2*poolLength, etc.)
    int calculatedLap = (int)ceil(swimmer->totalDistance / globalConfigSettings.poolLength);

    // distance for current length/lap
    float distanceForCurrentLength = swimmer->totalDistance - ((calculatedLap - 1) * globalConfigSettings.poolLength);
    profEnd("updateSwimmer - isSwimming calcs");
    // ---------------------------------- HANDLE WALL TURN ----------------------------------
    if (calculatedLap > swimmer->currentLap) {
      float overshootInDistance = swimmer->totalDistance - (swimmer->currentLap * globalConfigSettings.poolLength);
      long overshootInMilliseconds = (long)(overshootInDistance / swimmer->cachedSpeedMPS * 1000.0);

      if (DEBUG_ENABLED && swimmerIndex < laneSwimmerCount) {
        LOGLN("  *** WALL TURN ***");
        LOG("    calculatedLap based on distance: ");
        LOG(calculatedLap);
        LOG(", Current lap: ");
        LOG(swimmer->currentLap);
        LOG(", Total distance: ");
        LOG(swimmer->totalDistance);
        LOG(", Distance for current length: ");
        LOG(distanceForCurrentLength);
        LOG(", overshootInDistance: ");
        LOG(overshootInDistance);
        LOG(globalConfigSettings.poolUnitsYards ? " yards" : " meters");
        LOG(", overshootInMilliseconds: ");
        LOGLN(overshootInMilliseconds);
      }

      // Change lapDirection
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
          LOGLN("  *** ROUND COMPLETE ***");
          LOG("    Completed round ");
          LOGLN(swimmer->currentRound);
          LOG("    Current lap reset to ");
          LOGLN(swimmer->currentLap);
          LOG("    Total distance reset to ");
          LOGLN(swimmer->totalDistance);
        }

        // -------------------------------- STILL MORE ROUNDS TO GO --------------------------------
        // Only rest if we haven't completed all numRounds yet
        if (swimmer->currentRound < swimmer->cachedNumRounds) {
          profStart("updateSwimmer - more rounds");
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
            LOGLN("  *** MORE ROUNDS TO GO IN SET ***");
            LOG("    Now on round ");
            LOGLN(swimmer->currentRound);
            LOGLN("    isResting set to true");
            LOG("    restStartTime to ");
            LOG(swimmer->restStartTime);
            LOG(" (accounting for overshootInDistance of ");
            LOGLN((overshootInDistance / swimmer->cachedSpeedMPS * 1000.0));
          }
          profEnd("updateSwimmer - more rounds");
        } else {
          profStart("updateSwimmer - done rounds");
          // -------------------------------- ALL ROUNDS COMPLETE --------------------------------
          // All numRounds complete for this swim set
          if (DEBUG_ENABLED && swimmerIndex < laneSwimmerCount) {
            LOGLN("  *** ALL ROUNDS COMPLETE FOR CURRENT SET ***");
            LOG("    Swimmer ");
            LOG(swimmerIndex);
            LOG(" finished swim set (queue index ");
            LOG(swimmer->queueIndex);
            LOGLN(")");
          }

          // ---------------------------- ALL SWIMMERS COMPLETE -----------------------------
          // If this was the last swimmer for the lane,
          // we can mark this swim set as complete for the lane.
          // However, we still need to check if there are more sets in the queue.
          if (swimmerIndex >= laneSwimmerCount - 1) {
            if (DEBUG_ENABLED && swimmerIndex < laneSwimmerCount) {
              LOG("    Swimmer ");
              LOG(swimmerIndex);
              LOG(" is the last swimmer in the lane. ");
              LOGLN("Marking swim set as complete for the lane.");
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
          profEnd("updateSwimmer - done rounds");
          profStart("updateSwimmer - load next round");
          // ---------------------------- LOAD NEXT SWIM SET IF AVAILABLE ----------------------------
          // Try to get the next swim set in queue (queueIndex + 1)
          // If we are the first swimmer move to the next set
          // If we are not the first swimmer move to the same set as the first
          // swimmer, as they handled processing any kind of loops
          // TODO: the danger is if the previous swimmer has already moved
          // beyond the next set.  Another option is to have each swimmer track
          // their own next set to go to, but that may complicate things.
          bool didLoopJump = false;
          int nextIndex = (swimmerIndex == 0) ?
            (swimmer->queueIndex + 1) :
            swimmers[laneIndex][swimmerIndex-1].queueIndex;
          SwimSet nextSet;
          bool hasNextSet = getSwimSetByIndex(nextSet, laneIndex, nextIndex);
          if (hasNextSet && swimmerIndex == 0) {
            Serial.printf("{1} NEXT SET [%d]: UID=%s, Type=%d, Rounds=%d, Status=%d, remaining=%d\n",
              nextIndex,
              uniqueIdToHex(nextSet.uniqueId).c_str(),
              nextSet.type,
              nextSet.numRounds,
              nextSet.status,
              nextSet.repeatRemaining);
          }
          // Check if this is a repeat Swim Set
          // TODO: To avoid possible double-increment issue and have a clearer control flow,
          //       separate the LOOP jump from normal advancement
          // Only to the loop processing for the first swimmer, the other swimmers
          if (hasNextSet && nextSet.type == LOOP_TYPE && swimmerIndex == 0) {
            if (nextSet.loopFromUniqueId == 0) {
              // This is a loop set, but the loopFromUniqueId is not set, so we cannot loop
              LOG("Error: LOOP_TYPE swim set with UniqueId ");
              LOG(nextSet.uniqueId);
              LOGLN(" has loopFromUniqueId of 0, cannot loop.");
              Serial.printf("  Skipping LOOP set because loopFromUniqueId is 0, moving to next set after this one.\n");
              // Let's just skip to the next set after this one
              nextIndex++; // Skip the LOOP set
              hasNextSet = getSwimSetByIndex(nextSet, laneIndex, nextIndex);
            } else if (nextSet.repeatRemaining > 0) {
              // We still have repeats remaining, so we need to reset everything and loop back
              // Find the swim set we should go to next based on the loopFrom UniqueId
              SwimSet loopFromSet;
              if (!getSwimSetByUniqueId(loopFromSet, laneIndex, nextSet.loopFromUniqueId)) {
                // Could not find the loopFromUniqueId in the queue, log error and skip to next set
                LOG("Error: LOOP target uniqueId ");
                LOG(nextSet.loopFromUniqueId);
                LOGLN(" not found in queue");
                Serial.printf("  Skipping LOOP set because loopFromUniqueId not found, moving to next set after this one.\n");
                nextIndex++; // Skip the LOOP set
                hasNextSet = getSwimSetByIndex(nextSet, laneIndex, nextIndex);
                if (hasNextSet) {
                  Serial.printf("{2} BAD ID: NEXT SET [%d]: UID=%s, Type=%d, Rounds=%d, Status=%d\n",
                    nextIndex,
                    uniqueIdToHex(nextSet.uniqueId).c_str(),
                    nextSet.type,
                    nextSet.numRounds,
                    nextSet.status);
                }
              } else {
                // Reset all sets in the loop range
                int loopStartIdx = findSwimSetIndexByUniqueId(laneIndex, nextSet.loopFromUniqueId);
                if (loopStartIdx == -1) {
                  Serial.printf("  ERROR: findSwimSetIndexByUniqueId returned -1\n");
                  LOGLN("Error: findSwimSetIndexByUniqueId returned -1");
                } else {
                  Serial.printf("  Resetting swim sets from index %d to %d for looping\n",
                    loopStartIdx,
                    swimmer->queueIndex);
                  int currentIdx = loopStartIdx;
                  while (true) {
                    int actualIdx = (swimSetQueueHead[laneIndex] + currentIdx) % SWIMSET_QUEUE_MAX;

                    // Clear COMPLETED flag so sets re-execute during loop
                    swimSetQueue[laneIndex][actualIdx].status &= ~SWIMSET_STATUS_COMPLETED;
                    swimSetQueue[laneIndex][actualIdx].status &= ~SWIMSET_STATUS_ACTIVE;

                    // If any are loop sets, we should re-set those repeatRemaining counters
                    if (swimSetQueue[laneIndex][actualIdx].type == LOOP_TYPE &&
                        swimSetQueue[laneIndex][actualIdx].repeatRemaining == 0) {
                        swimSetQueue[laneIndex][actualIdx].repeatRemaining =
                          swimSetQueue[laneIndex][actualIdx].numRounds;
                    }
                    if (swimmerIndex == 0) {
                      Serial.printf("  Resetting swim set at index %d: UID=%s, Type=%d, Rounds=%d, Status=%d\n",
                        currentIdx,
                        uniqueIdToHex(swimSetQueue[laneIndex][actualIdx].uniqueId).c_str(),
                        swimSetQueue[laneIndex][actualIdx].type,
                        swimSetQueue[laneIndex][actualIdx].numRounds,
                        swimSetQueue[laneIndex][actualIdx].status);
                    }
                    if (currentIdx == swimmer->queueIndex) break;
                    currentIdx++;
                    if (currentIdx >= swimSetQueueCount[laneIndex]) break;
                  }
                }

                // Decrement the LOOP set's repeatRemaining in the actual queue
                int loopSetIdx = (swimSetQueueHead[laneIndex] + swimmer->queueIndex + 1) % SWIMSET_QUEUE_MAX;
                swimSetQueue[laneIndex][loopSetIdx].repeatRemaining--;

                // Set the LOOP set status to active
                swimSetQueue[laneIndex][loopSetIdx].status |= SWIMSET_STATUS_ACTIVE;

                // We need to set our next set to be the loopFromSet now
                // Update swimmer's queueIndex to the loop start BEFORE loading nextSet
                // Jump back to loop start WITHOUT incrementing
                nextIndex = loopStartIdx;
                didLoopJump = true; // Mark that we did a loop jump
                nextSet = loopFromSet;
                swimmer->queueIndex = nextIndex;
                hasNextSet = true;
                if (hasNextSet && swimmerIndex == 0) {
                  Serial.printf("{3} LOOP SETTING NEXT SET TO [%d]: UID=%s, Type=%d, Rounds=%d, Status=%d\n",
                    swimmer->queueIndex,
                    uniqueIdToHex(nextSet.uniqueId).c_str(),
                    nextSet.type,
                    nextSet.numRounds,
                    nextSet.status);
                }
              }
            } else {
              // Set the LOOP set as completed since we are done with it
              swimSetQueue[laneIndex][nextIndex].status |= SWIMSET_STATUS_COMPLETED;
              // TODO: Technically the last swimmer should set this to inactive
              // but we're letting the first swimmer handle all the loop logic.
              swimSetQueue[laneIndex][nextIndex].status &= ~SWIMSET_STATUS_ACTIVE;

              // No repeats remaining, just continue to next set after this one
              nextIndex++; // Skip the LOOP set
              hasNextSet = getSwimSetByIndex(nextSet, laneIndex, nextIndex);

              if (hasNextSet && swimmerIndex == 0) {
                Serial.printf("{4} NEXT SET [%d]: UID=%s, Type=%d, Rounds=%d, Status=%d\n",
                  nextIndex,
                  uniqueIdToHex(nextSet.uniqueId).c_str(),
                  nextSet.type,
                  nextSet.numRounds,
                  nextSet.status);
              }
            }
          }
          if (hasNextSet) {
            if (DEBUG_ENABLED && swimmerIndex < laneSwimmerCount) {
              LOGLN("  *** AUTO-ADVANCING TO NEXT SWIM SET ***");
              LOG("queue index: ");
              LOG(nextIndex);
              LOG(", numRounds: ");
              LOG(nextSet.numRounds);
              LOG(", swimDistance: ");
              LOG(nextSet.swimDistance);
              LOG(", swim seconds: ");
              LOG(nextSet.swimTime);
              LOG(", rest seconds: ");
              LOG(nextSet.restTime);
              LOG(", status: ");
              LOGLN(nextSet.status);
            }

            // Calculate new settings for the next set
            float lengthMeters = convertPoolToMeters((float)nextSet.swimDistance);
            float speedMPS = (nextSet.swimTime > 0) ? (lengthMeters / nextSet.swimTime) : 0.0;
            int lapsPerRound = ceil(nextSet.swimDistance / globalConfigSettings.poolLength);

            // Cache the new swim set settings in this swimmer
            int currentSetRestSeconds = swimmer->cachedRestSeconds;
            swimmer->cachedNumRounds = nextSet.numRounds;
            swimmer->cachedLapsPerRound = lapsPerRound;

            // Determine per-swimmer swimTime (use per-swimmer override if present else set swimTime)
            float swimmerSpeedMPS = speedMPS;
            float restForThisSwimmer = nextSet.restTime;
            if (nextSet.swimmersCount > 0 && swimmerIndex < nextSet.swimmersCount && nextSet.swimmerTimes[swimmerIndex] > 0) {
              uint16_t swimmerTime = nextSet.swimmerTimes[swimmerIndex];
              float swimmerDistanceMeters = convertPoolToMeters((float)nextSet.swimDistance);
              swimmerSpeedMPS = (swimmerTime > 0) ? (swimmerDistanceMeters / (float)swimmerTime) : 0.0f;
              // Keep total (swim + rest) equal to canonical set total (s.swimTime + s.restTime)
              int totalPair = (int)nextSet.swimTime + (int)nextSet.restTime;
              restForThisSwimmer = (float)(totalPair - (int)swimmerTime);
              if (restForThisSwimmer < 0.0f) restForThisSwimmer = 0.0f; // clamp
            }
            swimmer->cachedSpeedMPS = swimmerSpeedMPS;
            swimmer->cachedRestSeconds = restForThisSwimmer;

            //swimmer->queueIndex = nextIndex;  // Move to next index in queue
            // Update queueIndex ONLY if we didn't already jump via LOOP
            if (!didLoopJump || swimmerIndex > 0) {
              swimmer->queueIndex = nextIndex;
            }

            // --- KEEP laneActiveQueueIndex IN SYNC WITH FIRST SWIMMER ADVANCE ---
            // When any swimmer advances to the next queue index, make the lane's active
            // queue index reflect that new value so client status (activeIndex) stays accurate.
            if (swimmerIndex == 0) {
              laneActiveQueueIndex[laneIndex] = swimmer->queueIndex;
            }
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
              LOG("    Swimmer ");
              LOG(swimmerIndex);
              LOGLN(" starting rest before next set");
            }

            // ---------------------------- SETUP REMAINING INACTIVE SWIMMERS FOR NEW SET ----------------------------
            // We are loading the next swim set, and
            // If this was the last swimmer for the lane,
            // Setup any remaining non-active swimmers so they are in the right place.
            if (swimmerIndex >= laneSwimmerCount - 1) {
              for (int si = swimmerIndex + 1; si < laneSwimmerCount; si++) {
                swimmers[laneIndex][si].cachedNumRounds = nextSet.numRounds;
                swimmers[laneIndex][si].cachedLapsPerRound = lapsPerRound;
                // Since these swimmers are inactive, they all get the base speed/rest
                swimmers[laneIndex][si].cachedSpeedMPS = speedMPS;
                swimmers[laneIndex][si].cachedRestSeconds = nextSet.restTime;
                swimmers[laneIndex][si].queueIndex = swimmer->queueIndex;  // Move to the index of the last active swimmer
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
              LOGLN("  *** NO MORE SETS IN QUEUE ***");
              LOG("    Swimmer ");
              LOG(swimmerIndex);
              LOG(" completed all ");
              LOG(nextIndex);
              LOGLN(" sets!");
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
          profEnd("updateSwimmer - done rounds");
        }

        // We finished a round, so we need to position the swimmer at the wall
        // Place LED at the wall based on lapDirection
        if (swimmer->lapDirection == 1) {
          swimmer->position = 0;
        } else {
          // Convert pool length to LED position
          float poolLengthInMeters = convertPoolToMeters(globalConfigSettings.poolLength);
          int ledPosition = (int)roundf(poolLengthInMeters * globalConfigSettings.ledsPerMeter) - 1;
          swimmer->position = max(0, ledPosition);
        }
        // Move any remaining inactive swimmers to same position
        if (swimmerIndex >= laneSwimmerCount - 1) {
          for (int si = swimmerIndex + 1; si < MAX_SWIMMERS_SUPPORTED; si++) {
            swimmers[laneIndex][si].position = swimmer->position;
          }
        }

      } else {
        profStart("updateSwimmer - regular lap turn");
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
        int overshootLEDs = (int)roundf(overshootInMeters * globalConfigSettings.ledsPerMeter);
        if (swimmer->lapDirection == 1) {
          swimmer->position = overshootLEDs;
        } else {
          float poolLengthInMeters = convertPoolToMeters(globalConfigSettings.poolLength);
          int ledEnd = (int)roundf(poolLengthInMeters * globalConfigSettings.ledsPerMeter) - 1;
          swimmer->position = max(0, ledEnd - overshootLEDs);
        }
        profEnd("updateSwimmer - regular lap turn");
      }
    } else {
      profStart("updateSwimmer - normal swimming");
      // -------------------------------- NORMAL MOVEMENT WITHIN POOL LENGTH --------------------------------
      // Normal movement within the same pool length
      // distanceForCurrentLength is the distance traveled in the current lap
      // Convert to LED strip units
      float distanceInMeters = convertPoolToMeters(distanceForCurrentLength);
      float poolLengthInMeters = convertPoolToMeters(globalConfigSettings.poolLength);

      // Calculate LED position based on lapDirection
      if (swimmer->lapDirection == 1) {
        // Swimming away from start: position = 0 + distance (use round)
        swimmer->position = (int)roundf(distanceInMeters * globalConfigSettings.ledsPerMeter);
      } else {
        // Swimming back to start: position = poolLength - distance (use round)
        float positionInMeters = poolLengthInMeters - distanceInMeters;
        swimmer->position = (int)roundf(positionInMeters * globalConfigSettings.ledsPerMeter);
      }
      profEnd("updateSwimmer - normal swimming");
    }

    // TODO: maybe this should be in the sections above, although then it would be
    // repeated in multiple places
    // ------------------------------ UNDERWATER LIGHT LOGIC ------------------------------
    // Track distance for underwater calculations
    if (swimmer->underwaterActive) {
      profStart("updateSwimmer - underwaters");
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
      profEnd("updateSwimmer - underwaters");
    }
  }
  profEnd("updateSwimmer");
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
    LOGLN("\n========== STATUS UPDATE ==========");
    for (int lane = 0; lane < globalConfigSettings.numLanes; lane++) {
      if (globalConfigSettings.laneRunning[lane]) {
        LOG("Lane ");
        LOG(lane);
        LOGLN(":");
        int laneSwimmerCount = globalConfigSettings.numSwimmersPerLane[lane];
        if (laneSwimmerCount < 1) laneSwimmerCount = 1;
        if (laneSwimmerCount > MAX_SWIMMERS_SUPPORTED) laneSwimmerCount = MAX_SWIMMERS_SUPPORTED;
        for (int i = 0; i < laneSwimmerCount; i++) {
          Swimmer* s = &swimmers[lane][i];
          LOG("  Swimmer ");
          LOG(i);
          LOG(": ");
          if (s->isResting) {
            LOG("RESTING (");
            LOG((currentTime - s->restStartTime) / 1000.0);
            LOG("s)");
            LOG(" targetRest:");
            LOG(s->expectedRestDuration / 1000.0);
            LOG(" QI:");
            LOG(s->queueIndex);
            LOG(" hasStarted=");
            LOG(s->hasStarted);
          } else {
            LOG("Swimming - ");

            LOG(" QI:");
            LOG(s->queueIndex);
            LOG(" R");
            LOG(s->currentRound);
            LOG("/");
            LOG(swimSetSettings.numRounds);
            LOG(" L");
            LOG(s->currentLap);
            LOG("/");
            LOG(s->lapsPerRound);

            // Calculate distance in current lap (1-based lap calculation)
            int currentLapNum = (int)ceil(s->totalDistance / globalConfigSettings.poolLength);
            float distanceInCurrentLap = s->totalDistance - ((currentLapNum - 1) * globalConfigSettings.poolLength);

            LOG(" Dist:");
            LOG(distanceInCurrentLap, 1);
            LOG(globalConfigSettings.poolUnitsYards ? "yd" : "m");
            LOG(" Pos:");
            LOG(s->position);
          }
          LOGLN();
        }
      }
    }
    LOGLN("===================================\n");
  }
}

void drawSwimmerPulse(int swimmerIndex, int laneIndex) {
  profStart("drawSwimmerPulse");
  Swimmer* swimmer = &swimmers[laneIndex][swimmerIndex];

  // Don't draw if swimmer is resting
  if (swimmer->isResting) {
    profEnd("drawSwimmerPulse");
    return;
  }

  // Only draw if swimmer should be active (start time has passed). swimStartTime==0 means already allowed.
  if (swimmer->swimStartTime > 0 && millis() < swimmer->swimStartTime) {
    profEnd("drawSwimmerPulse");
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
      //if (renderedLEDs[laneIndex][ledIndex] == CRGB::Black) {
      //  renderedLEDs[laneIndex][ledIndex] = color;
      //}
      // Write the pulse pixel for this frame (overwrite). Layers/priority are
      // still controlled by draw order; forcing the write avoids 1-pixel flicker.
      renderedLEDs[laneIndex][ledIndex] = color;
    }
  }
  profEnd("drawSwimmerPulse");
}

void drawUnderwaterZone(int laneIndex, int swimmerIndex) {
  profStart("drawUnderwaterZone");
  Swimmer* swimmer = &swimmers[laneIndex][swimmerIndex];
  unsigned long currentTime = millis();

  // Only draw if swimmer should be active (start time has passed)
  if (swimmer->isResting) {
    profEnd("drawUnderwaterZone");
    return; // Swimmer hasn't started yet
  }

  // Check if underwater is active for this swimmer
  if (!swimmer->underwaterActive) {
    profEnd("drawUnderwaterZone");
    return; // No underwater light needed
  }

  // Check if we should hide underwater light (after hide timer expires)
  if (swimmer->hideTimerStart > 0) {
    float hideAfterSeconds = globalConfigSettings.hideAfterSeconds;
    unsigned long hideTimer = currentTime - swimmer->hideTimerStart;

    if (hideTimer >= (hideAfterSeconds * 1000)) {
      // Hide underwater light
      swimmer->underwaterActive = false;
      profEnd("drawUnderwaterZone");
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
  profEnd("drawUnderwaterZone");
}