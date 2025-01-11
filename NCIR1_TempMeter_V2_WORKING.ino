#include <FastLED.h>
#include <M5CoreS3.h>
#include <M5GFX.h>
#include <M5Unified.h>
#include <Wire.h>
#include <Preferences.h>

// Pin Definitions
#define KEY_PIN 8      // Key unit input pin (G8)
#define LED_PIN 9      // Key unit LED pin (G9)
#define BUTTON1_PIN 17 // Dual Button unit - Button 1 (G17)
#define BUTTON2_PIN 18 // Dual Button unit - Button 2 (G18)
#define NUM_LEDS 1

// LED Array
CRGB leds[NUM_LEDS];

// Menu States
enum MenuState {
    MAIN_DISPLAY,
    SETTINGS_MENU,
    UNIT_SELECTION,
    BRIGHTNESS_ADJUSTMENT,
    SOUND_SETTINGS
};

// Configuration namespace
namespace Config {
    namespace Display {
        const uint32_t COLOR_BACKGROUND = TFT_BLACK;
        const uint32_t COLOR_TEXT = TFT_WHITE;
        const uint32_t COLOR_SUCCESS = TFT_GREEN;
        const uint32_t COLOR_ERROR = TFT_RED;
        const uint32_t COLOR_HIGHLIGHT = TFT_YELLOW;
    }
}

// Settings structure
struct Settings {
    bool useCelsius = true;
    bool soundEnabled = true;
    uint8_t brightness = 128;
    
    void load() {
        Preferences prefs;
        prefs.begin("tempMeter", false);
        useCelsius = prefs.getBool("celsius", true);
        soundEnabled = prefs.getBool("sound", true);
        brightness = prefs.getUChar("bright", 128);
        prefs.end();
    }
    
    void save() {
        Preferences prefs;
        prefs.begin("tempMeter", false);
        prefs.putBool("celsius", useCelsius);
        prefs.putBool("sound", soundEnabled);
        prefs.putUChar("bright", brightness);
        prefs.end();
    }
} settings;

// State structure
struct State {
    bool isMonitoring = false;
    bool keyPressed = false;
    bool ledActive = false;
    MenuState menuState = MAIN_DISPLAY;
    String statusMessage = "";
    uint32_t statusColor = Config::Display::COLOR_TEXT;
    int menuSelection = 0;
    
    void updateStatus(const String& message, uint32_t color = Config::Display::COLOR_TEXT) {
        statusMessage = message;
        statusColor = color;
    }
} state;

// Function declarations
void handleButtons();
void drawMainDisplay(float temperature);
void drawSettingsMenu();
void drawUnitSelection();
void drawBrightnessAdjustment();
void drawSoundSettings();
void updateDisplay();
void handleKeyAndLED();
float readTemperature();
float celsiusToFahrenheit(float celsius);
bool isValidTemperature(float temp);
void drawStatusBox();
void playSuccessSound();
void playErrorSound();

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\nStarting initialization...");

    // Initialize Core S3 with all features enabled
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    delay(100);  // Give hardware time to initialize
    
    // Initialize power management
    CoreS3.Power.begin();
    CoreS3.Power.setChargeCurrent(900);
    CoreS3.Power.setExtOutput(true);
    delay(100);
    
    // Initialize pins
    pinMode(KEY_PIN, INPUT_PULLUP);
    pinMode(BUTTON1_PIN, INPUT_PULLUP);
    pinMode(BUTTON2_PIN, INPUT_PULLUP);
    Serial.println("Pins initialized");
    
    // Initialize FastLED
    FastLED.addLeds<SK6812, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(128);
    leds[0] = CRGB::Black;
    FastLED.show();
    Serial.println("FastLED initialized");
    
    // Initialize display
    CoreS3.Display.setRotation(1);
    CoreS3.Display.setBrightness(settings.brightness);
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    Serial.println("Display initialized");
    
    // Load settings
    settings.load();
    Serial.println("Settings loaded");
    
    // Initialize I2C for NCIR1
    Wire.begin(2, 1);
    delay(50);
    Serial.println("I2C initialized");
    
    // Initial display update
    state.updateStatus("Ready", Config::Display::COLOR_SUCCESS);
    updateDisplay();
    Serial.println("Initialization complete");
}

void loop() {
    CoreS3.update();  // Update button states
    handleButtons();
    handleKeyAndLED();
    
    static unsigned long lastTempUpdate = 0;
    if (millis() - lastTempUpdate >= 100) {  // Update every 100ms
        float temp = readTemperature();
        if (state.isMonitoring && state.menuState == MAIN_DISPLAY) {
            updateDisplay();
        }
        lastTempUpdate = millis();
    }
    
    delay(10);  // Small delay to prevent overwhelming the processor
}

void handleButtons() {
    static bool lastButton1State = true;
    static bool lastButton2State = true;
    
    bool button1State = digitalRead(BUTTON1_PIN);
    bool button2State = digitalRead(BUTTON2_PIN);
    
    // Button 1 press (Menu/Select)
    if (!button1State && lastButton1State) {
        playSuccessSound();
        
        switch (state.menuState) {
            case MAIN_DISPLAY:
                state.menuState = SETTINGS_MENU;
                state.menuSelection = 0;
                break;
                
            case SETTINGS_MENU:
                switch (state.menuSelection) {
                    case 0: state.menuState = UNIT_SELECTION; break;
                    case 1: state.menuState = BRIGHTNESS_ADJUSTMENT; break;
                    case 2: state.menuState = SOUND_SETTINGS; break;
                    case 3: // Exit
                        state.menuState = MAIN_DISPLAY;
                        settings.save();
                        break;
                }
                break;
                
            case UNIT_SELECTION:
            case BRIGHTNESS_ADJUSTMENT:
            case SOUND_SETTINGS:
                state.menuState = SETTINGS_MENU;
                settings.save();
                break;
        }
        updateDisplay();
        delay(50);  // Debounce
    }
    
    // Button 2 press (Navigate/Adjust)
    if (!button2State && lastButton2State) {
        playSuccessSound();
        
        switch (state.menuState) {
            case SETTINGS_MENU:
                state.menuSelection = (state.menuSelection + 1) % 4;
                Serial.printf("Menu Selection: %d\n", state.menuSelection);  // Debug print
                break;
                
            case UNIT_SELECTION:
                settings.useCelsius = !settings.useCelsius;
                break;
                
            case BRIGHTNESS_ADJUSTMENT:
                settings.brightness = (settings.brightness + 32) % 256;
                CoreS3.Display.setBrightness(settings.brightness);
                break;
                
            case SOUND_SETTINGS:
                settings.soundEnabled = !settings.soundEnabled;
                break;
        }
        updateDisplay();
        delay(50);  // Debounce
    }
    
    lastButton1State = button1State;
    lastButton2State = button2State;
}

void handleKeyAndLED() {
    static bool lastKeyState = true;
    bool currentKeyState = digitalRead(KEY_PIN);
    
    if (!currentKeyState && lastKeyState) {  // Key pressed
        if (state.menuState == MAIN_DISPLAY) {
            state.isMonitoring = !state.isMonitoring;
            
            if (state.isMonitoring) {
                leds[0] = CRGB::Green;
                state.ledActive = true;
                state.updateStatus("Monitoring...", Config::Display::COLOR_SUCCESS);
            } else {
                leds[0] = CRGB::Black;
                state.ledActive = false;
                state.updateStatus("Stopped", Config::Display::COLOR_TEXT);
            }
            FastLED.show();
            
            if (settings.soundEnabled) {
                CoreS3.Speaker.tone(2000, 50);
            }
            updateDisplay();
        }
        delay(50);  // Debounce
    }
    lastKeyState = currentKeyState;
}

void updateDisplay() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    
    switch (state.menuState) {
        case MAIN_DISPLAY:
            drawMainDisplay(readTemperature());
            break;
        case SETTINGS_MENU:
            drawSettingsMenu();
            break;
        case UNIT_SELECTION:
            drawUnitSelection();
            break;
        case BRIGHTNESS_ADJUSTMENT:
            drawBrightnessAdjustment();
            break;
        case SOUND_SETTINGS:
            drawSoundSettings();
            break;
    }
    
    drawStatusBox();
}

void drawMainDisplay(float temperature) {
    if (isValidTemperature(temperature)) {
        char tempStr[10];
        float displayTemp = settings.useCelsius ? temperature : celsiusToFahrenheit(temperature);
        sprintf(tempStr, "%d%c", (int)round(displayTemp), settings.useCelsius ? 'C' : 'F');
        
        // Draw temperature in a box
        const int boxPadding = 20;
        CoreS3.Display.setTextSize(4);
        
        // Calculate approximate text width and height
        int charWidth = CoreS3.Display.textWidth("0") * 4;  // Approximate width per character
        int charHeight = CoreS3.Display.fontHeight() * 4;   // Approximate height
        int textWidth = strlen(tempStr) * charWidth;
        
        // Calculate box dimensions and position
        int boxWidth = textWidth + (boxPadding * 2);
        int boxHeight = charHeight + (boxPadding * 2);
        int boxX = (CoreS3.Display.width() - boxWidth) / 2;
        int boxY = (CoreS3.Display.height() - boxHeight) / 2 - 20;
        
        // Draw box and text
        CoreS3.Display.drawRect(boxX, boxY, boxWidth, boxHeight, Config::Display::COLOR_TEXT);
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
        CoreS3.Display.drawString(tempStr, CoreS3.Display.width()/2, boxY + boxHeight/2);
    } else {
        state.updateStatus("Invalid Temperature", Config::Display::COLOR_ERROR);
    }
}

void drawSettingsMenu() {
    const char* menuItems[] = {"Temperature Unit", "Brightness", "Sound", "Exit"};
    const int itemHeight = 40;
    const int startY = 60;
    
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString("Settings", CoreS3.Display.width()/2, 30);
    
    for (int i = 0; i < 4; i++) {
        int y = startY + (i * itemHeight);
        if (i == state.menuSelection) {
            // Draw selection box
            int textWidth = CoreS3.Display.textWidth(menuItems[i]) * 2;  // *2 for text size
            int boxWidth = textWidth + 20;  // Add padding
            int boxHeight = 30;
            int boxX = (CoreS3.Display.width() - boxWidth) / 2;
            int boxY = y - boxHeight/2;
            
            CoreS3.Display.drawRect(boxX, boxY, boxWidth, boxHeight, Config::Display::COLOR_HIGHLIGHT);
            CoreS3.Display.setTextColor(Config::Display::COLOR_HIGHLIGHT);
        } else {
            CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
        }
        CoreS3.Display.drawString(menuItems[i], CoreS3.Display.width()/2, y);
    }
}

void drawUnitSelection() {
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    
    CoreS3.Display.drawString("Temperature Unit", CoreS3.Display.width()/2, 30);
    CoreS3.Display.drawString(settings.useCelsius ? "Celsius" : "Fahrenheit", CoreS3.Display.width()/2, CoreS3.Display.height()/2);
    CoreS3.Display.drawString("Press Button 2 to change", CoreS3.Display.width()/2, CoreS3.Display.height() - 40);
}

void drawBrightnessAdjustment() {
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    
    CoreS3.Display.drawString("Brightness", CoreS3.Display.width()/2, 30);
    char brightnessStr[8];
    sprintf(brightnessStr, "%d%%", (settings.brightness * 100) / 255);
    CoreS3.Display.drawString(brightnessStr, CoreS3.Display.width()/2, CoreS3.Display.height()/2);
    CoreS3.Display.drawString("Press Button 2 to adjust", CoreS3.Display.width()/2, CoreS3.Display.height() - 40);
}

void drawSoundSettings() {
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    
    CoreS3.Display.drawString("Sound", CoreS3.Display.width()/2, 30);
    CoreS3.Display.drawString(settings.soundEnabled ? "Enabled" : "Disabled", CoreS3.Display.width()/2, CoreS3.Display.height()/2);
    CoreS3.Display.drawString("Press Button 2 to toggle", CoreS3.Display.width()/2, CoreS3.Display.height() - 40);
}

void drawStatusBox() {
    const int boxHeight = 30;
    const int boxMargin = 5;
    int boxY = CoreS3.Display.height() - boxHeight - boxMargin;
    
    CoreS3.Display.drawRect(boxMargin, boxY, CoreS3.Display.width() - (boxMargin * 2), boxHeight, Config::Display::COLOR_TEXT);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(state.statusColor);
    CoreS3.Display.drawString(state.statusMessage, CoreS3.Display.width()/2, boxY + boxHeight/2);
}

float readTemperature() {
    Wire.beginTransmission(0x5A);
    Wire.write(0x07);
    Wire.endTransmission(false);
    Wire.requestFrom(0x5A, 3);
    
    uint16_t tempData;
    tempData = Wire.read();
    tempData |= Wire.read() << 8;
    Wire.read();  // PEC
    
    return tempData * 0.02 - 273.15;
}

float celsiusToFahrenheit(float celsius) {
    return (celsius * 9.0 / 5.0) + 32.0;
}

bool isValidTemperature(float temp) {
    return temp >= -70 && temp <= 380;
}

void playSuccessSound() {
    if (settings.soundEnabled) {
        CoreS3.Speaker.tone(2000, 50);
    }
}

void playErrorSound() {
    if (settings.soundEnabled) {
        CoreS3.Speaker.tone(1000, 100);
    }
}
