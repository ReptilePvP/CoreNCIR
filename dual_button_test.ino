#include <M5CoreS3.h>

// Pin definitions for Dual Button Unit
#define BUTTON1_PIN 17  // First button
#define BUTTON2_PIN 18  // Second button

// Button states
int lastValue1 = 1;  // Using 1 as default (not pressed) due to INPUT_PULLUP
int lastValue2 = 1;
int currentValue1 = 1;
int currentValue2 = 1;

// Colors
#define COLOR_BACKGROUND BLACK
#define COLOR_TEXT WHITE
#define COLOR_HIGHLIGHT YELLOW
#define COLOR_BUTTON_PRESSED GREEN
#define COLOR_BUTTON_RELEASED RED

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\nDual Button Test Starting...");
    
    // Initialize M5CoreS3
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    
    // Initialize button pins with internal pull-up resistors
    pinMode(BUTTON1_PIN, INPUT_PULLUP);
    pinMode(BUTTON2_PIN, INPUT_PULLUP);
    Serial.println("Buttons initialized with INPUT_PULLUP");
    
    // Setup display
    CoreS3.Display.setRotation(1);  // Landscape mode
    CoreS3.Display.fillScreen(COLOR_BACKGROUND);
    CoreS3.Display.setTextSize(2);
    
    // Draw title
    CoreS3.Display.setTextColor(COLOR_HIGHLIGHT);
    CoreS3.Display.drawString("Dual Button Test", CoreS3.Display.width()/2, 20, 2);
    
    // Draw button labels
    CoreS3.Display.setTextColor(COLOR_TEXT);
    CoreS3.Display.drawString("Button 1 (G17)", CoreS3.Display.width()/4, 60, 2);
    CoreS3.Display.drawString("Button 2 (G18)", (CoreS3.Display.width()*3)/4, 60, 2);
    
    // Draw static labels
    CoreS3.Display.drawString("State:", 50, 100, 2);
    CoreS3.Display.drawString("State:", CoreS3.Display.width()/2 + 50, 100, 2);
    
    Serial.println("Display initialized");
}

void loop() {
    // Read button states
    currentValue1 = digitalRead(BUTTON1_PIN);
    currentValue2 = digitalRead(BUTTON2_PIN);
    
    // Debug output
    static unsigned long lastDebugTime = 0;
    if (millis() - lastDebugTime >= 1000) {
        Serial.printf("Button States - B1: %d, B2: %d\n", currentValue1, currentValue2);
        lastDebugTime = millis();
    }
    
    // Handle Button 1
    if (currentValue1 != lastValue1) {
        Serial.printf("Button 1 changed to: %s\n", currentValue1 ? "RELEASED" : "PRESSED");
        
        // Clear previous state
        CoreS3.Display.fillRect(50, 120, 120, 30, COLOR_BACKGROUND);
        
        // Update display
        CoreS3.Display.setTextColor(currentValue1 ? COLOR_BUTTON_RELEASED : COLOR_BUTTON_PRESSED);
        CoreS3.Display.drawString(currentValue1 ? "Released" : "Pressed", 50, 120, 2);
        
        // Play sound feedback
        if (currentValue1 == 0) {  // Button pressed
            CoreS3.Speaker.tone(2000, 50);
        }
        
        lastValue1 = currentValue1;
    }
    
    // Handle Button 2
    if (currentValue2 != lastValue2) {
        Serial.printf("Button 2 changed to: %s\n", currentValue2 ? "RELEASED" : "PRESSED");
        
        // Clear previous state
        CoreS3.Display.fillRect(CoreS3.Display.width()/2 + 50, 120, 120, 30, COLOR_BACKGROUND);
        
        // Update display
        CoreS3.Display.setTextColor(currentValue2 ? COLOR_BUTTON_RELEASED : COLOR_BUTTON_PRESSED);
        CoreS3.Display.drawString(currentValue2 ? "Released" : "Pressed", CoreS3.Display.width()/2 + 50, 120, 2);
        
        // Play sound feedback
        if (currentValue2 == 0) {  // Button pressed
            CoreS3.Speaker.tone(1500, 50);
        }
        
        lastValue2 = currentValue2;
    }
    
    delay(50);  // Debounce delay
}
