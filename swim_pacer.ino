/*
  WS2812B LED Strip Chase/Bounce Effect

  This code creates a configurable pulse of light that travels down the strip
  and bounces back, with adjustable speed, color, and pulse width.

  Wiring:
  - LED Strip +5V to External 5V Power Supply
  - LED Strip GND to Arduino GND AND Power Supply GND
  - LED Strip Data to Arduino Pin 6

  Required Library: FastLED (install via Arduino IDE Library Manager)
*/

#include <FastLED.h>

// ========== HARDWARE CONFIGURATION ==========
#define LED_PIN         6           // Data pin connected to the strip
#define LED_TYPE        WS2812B     // LED strip type
#define COLOR_ORDER     GRB         // Color order (may need adjustment)

// ========== LED STRIP SPECIFICATIONS ==========
#define TOTAL_LEDS      150         // Total number of LEDs in your strip
#define LEDS_PER_METER  30          // LEDs per meter (30 for your strip)
#define STRIP_LENGTH_M  5.0         // Total strip length in meters (16.4ft = ~5m)

// ========== CONFIGURABLE PARAMETERS ==========

// LED Spacing
const float LED_SPACING_CM = 100.0 / LEDS_PER_METER;  // Distance between LEDs in centimeters (3.33cm)
const float LED_SPACING_INCHES = LED_SPACING_CM / 2.54; // Distance between LEDs in inches (1.31")

// Pulse Width Configuration
const float PULSE_WIDTH_FEET = 1.0;                    // Width of light pulse in feet
const float PULSE_WIDTH_CM = PULSE_WIDTH_FEET * 30.48; // Convert to centimeters
const int PULSE_WIDTH_LEDS = (int)(PULSE_WIDTH_CM / LED_SPACING_CM); // Number of LEDs in pulse (~9 LEDs)

// Speed Configuration
const float SPEED_FEET_PER_SECOND = 5.56;              // Speed of pulse in feet per second
const float SPEED_CM_PER_SECOND = SPEED_FEET_PER_SECOND * 30.48; // Convert to cm/s
const int DELAY_MS = (int)(LED_SPACING_CM / SPEED_CM_PER_SECOND * 1000); // Delay between LED updates in milliseconds

// Color Configuration
const CRGB PULSE_COLOR = CRGB::Blue;                   // Color of the pulse (change as desired)
const uint8_t BRIGHTNESS = 100;                       // Overall brightness (0-255)

// ========== GLOBAL VARIABLES ==========
CRGB leds[TOTAL_LEDS];
int currentPosition = 0;    // Current center position of the pulse
int direction = 1;          // Direction of movement (1 = forward, -1 = backward)
unsigned long lastUpdate = 0;

void setup() {
  Serial.begin(9600);

  // Print configuration info
  Serial.println("=== LED Strip Chase/Bounce Configuration ===");
  Serial.print("Total LEDs: "); Serial.println(TOTAL_LEDS);
  Serial.print("Strip Length: "); Serial.print(STRIP_LENGTH_M); Serial.println(" meters");
  Serial.print("LED Spacing: "); Serial.print(LED_SPACING_CM, 2); Serial.print(" cm (");
  Serial.print(LED_SPACING_INCHES, 2); Serial.println(" inches)");
  Serial.print("Pulse Width: "); Serial.print(PULSE_WIDTH_FEET); Serial.print(" feet (");
  Serial.print(PULSE_WIDTH_LEDS); Serial.println(" LEDs)");
  Serial.print("Speed: "); Serial.print(SPEED_FEET_PER_SECOND); Serial.println(" feet/second");
  Serial.print("Update Delay: "); Serial.print(DELAY_MS); Serial.println(" ms");
  Serial.println("==========================================");

  // Initialize FastLED
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, TOTAL_LEDS);
  FastLED.setBrightness(BRIGHTNESS);

  // Clear all LEDs
  FastLED.clear();
  FastLED.show();

  Serial.println("Starting chase effect...");
}

void loop() {
  unsigned long currentTime = millis();

  // Check if it's time to update
  if (currentTime - lastUpdate >= DELAY_MS) {
    lastUpdate = currentTime;

    // Clear all LEDs (no trail effect)
    FastLED.clear();

    // Draw the current pulse
    drawPulse(currentPosition);

    // Update FastLED
    FastLED.show();

    // Move to next position
    currentPosition += direction;

    // Check for bouncing at ends
    if (currentPosition >= TOTAL_LEDS - 1) {
      direction = -1;  // Reverse direction
      currentPosition = TOTAL_LEDS - 1;
      Serial.println("Bouncing off end");
    } else if (currentPosition <= 0) {
      direction = 1;   // Reverse direction
      currentPosition = 0;
      Serial.println("Bouncing off start");
    }
  }
}

// Function to draw the pulse centered at the given position
void drawPulse(int centerPos) {
  int halfWidth = PULSE_WIDTH_LEDS / 2;

  // Draw pulse with gradient effect (brightest at center)
  for (int i = 0; i < PULSE_WIDTH_LEDS; i++) {
    int ledIndex = centerPos - halfWidth + i;

    // Check if LED index is valid
    if (ledIndex >= 0 && ledIndex < TOTAL_LEDS) {
      // Calculate brightness based on distance from center
      int distanceFromCenter = abs(i - halfWidth);
      float brightnessFactor = 1.0 - (float)distanceFromCenter / (float)halfWidth;
      brightnessFactor = max(0.0f, brightnessFactor); // Ensure non-negative

      // Apply color with calculated brightness
      CRGB color = PULSE_COLOR;
      color.nscale8((uint8_t)(brightnessFactor * 255));

      // Add to existing color (for overlapping effects)
      leds[ledIndex] += color;
    }
  }
}

// ========== UTILITY FUNCTIONS ==========

// Function to change pulse color during runtime
void setPulseColor(CRGB newColor) {
  // You can call this function to change color
  // Example: setPulseColor(CRGB::Red);
}

// Function to change speed during runtime
void setSpeed(float newSpeedFeetPerSecond) {
  // Recalculate delay based on new speed
  float newSpeedCmPerSecond = newSpeedFeetPerSecond * 30.48;
  int newDelay = (int)(LED_SPACING_CM / newSpeedCmPerSecond * 1000);

  Serial.print("Speed changed to: "); Serial.print(newSpeedFeetPerSecond);
  Serial.print(" ft/s (delay: "); Serial.print(newDelay); Serial.println(" ms)");
}

// Function to change pulse width during runtime
void setPulseWidth(float newWidthFeet) {
  // You would need to modify the global variable and recalculate
  // This is more complex due to const declarations above
  Serial.print("To change pulse width, modify PULSE_WIDTH_FEET and recompile");
}