#include <FastLED.h>
#include <M5CoreS3.h>

// Pin Definitions
#define KEY_PIN 8     // Try G9 first
#define LED_PIN 9     // Try G8 first
#define NUM_LEDS 1

CRGB leds[NUM_LEDS];

void setup() {
    Serial.begin(115200);
    delay(1000);  // Give time for serial to initialize
    
    Serial.println("\nKey Unit Test Starting...");
    
    // Initialize M5CoreS3
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    
    // Initialize KEY pin
    pinMode(KEY_PIN, INPUT_PULLUP);
    Serial.printf("KEY_PIN (G%d) initialized as INPUT_PULLUP\n", KEY_PIN);
    
    // Initialize FastLED
    Serial.printf("Initializing LED on pin G%d...\n", LED_PIN);
    FastLED.addLeds<SK6812, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(128);
    
    // Test different colors
    Serial.println("Testing RED...");
    leds[0] = CRGB::Red;
    FastLED.show();
    delay(1000);
    
    Serial.println("Testing GREEN...");
    leds[0] = CRGB::Green;
    FastLED.show();
    delay(1000);
    
    Serial.println("Testing BLUE...");
    leds[0] = CRGB::Blue;
    FastLED.show();
    delay(1000);
    
    leds[0] = CRGB::Black;
    FastLED.show();
    
    Serial.println("Setup complete. Starting main loop...");
}

void loop() {
    // Read and report key state
    static bool lastKeyState = true;
    bool currentKeyState = digitalRead(KEY_PIN);
    
    // Report key state changes
    if (currentKeyState != lastKeyState) {
        Serial.printf("Key state changed to: %s\n", currentKeyState ? "RELEASED" : "PRESSED");
        
        // Change LED color based on key state
        if (!currentKeyState) {  // Key is pressed
            leds[0] = CRGB::Green;
            Serial.println("LED set to GREEN");
        } else {  // Key is released
            leds[0] = CRGB::Black;
            Serial.println("LED turned OFF");
        }
        FastLED.show();
        
        lastKeyState = currentKeyState;
    }
    
    delay(50);  // Small delay to prevent serial spam
}
