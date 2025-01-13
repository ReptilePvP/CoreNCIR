// Last updated: 2025-01-13 02:21 AM EST
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
// 1/11/24 10:00 PM Working
// LED Array
CRGB leds[NUM_LEDS];

// Menu States
enum MenuState {
    MAIN_DISPLAY,
    SETTINGS_MENU,
    UNIT_SELECTION,
    BRIGHTNESS_ADJUSTMENT,
    SOUND_SETTINGS,
    EMISSIVITY_ADJUSTMENT,
    EMISSIVITY_CONFIRM,
    RESTART_CONFIRM
};

// Configuration namespace
namespace Config {
    namespace Display {
        // Modern color scheme
        const uint32_t COLOR_BACKGROUND = 0x1A1A1A;  // Dark gray background
        const uint32_t COLOR_TEXT = 0xE0E0E0;        // Light gray text
        const uint32_t COLOR_PRIMARY = 0x0099FF;     // Bright blue for primary elements
        const uint32_t COLOR_SUCCESS = 0x00E676;     // Material green
        const uint32_t COLOR_ERROR = 0xFF5252;       // Material red
        const uint32_t COLOR_WARNING = 0xFFD740;     // Material amber
        const uint32_t COLOR_ACCENT = 0x7C4DFF;      // Material deep purple
        const uint32_t COLOR_SECONDARY_BG = 0x2D2D2D; // Slightly lighter background for contrast
        const uint32_t COLOR_BORDER = 0x404040;      // Medium gray for borders
        
        // UI Constants
        const int HEADER_HEIGHT = 40;
        const int PADDING = 10;
        const int CORNER_RADIUS = 8;  // For rounded rectangles
    }
    
    namespace Emissivity {
        const float MIN = 0.65f;
        const float MAX = 1.00f;
        const float STEP = 0.01f;
    }
    
    namespace Animation {
        const int TRANSITION_MS = 150;  // Animation duration in milliseconds
    }
}

// Temperature ranges in Fahrenheit
const int16_t TEMP_COLD_F = 480;    // Below this is too cold
const int16_t TEMP_MIN_F = 580;     // Start of perfect range
const int16_t TEMP_MAX_F = 640;     // End of perfect range
const int16_t TEMP_HOT_F = 800;     // Above this is too hot

// Temperature ranges in Celsius (converted from Fahrenheit)
const int16_t TEMP_COLD_C = (TEMP_COLD_F - 32) * 5 / 9;
const int16_t TEMP_MIN_C = (TEMP_MIN_F - 32) * 5 / 9;
const int16_t TEMP_MAX_C = (TEMP_MAX_F - 32) * 5 / 9;
const int16_t TEMP_HOT_C = (TEMP_HOT_F - 32) * 5 / 9;

// Custom colors for temperature status
const uint32_t COLOR_COLD = 0x1E90FF;  // Snowy blue
const CRGB LED_COLOR_COLD = CRGB(30, 144, 255);  // Matching LED color for cold
const CRGB LED_COLOR_WARNING = CRGB(255, 191, 0);  // Amber for warning
const CRGB LED_COLOR_PERFECT = CRGB(0, 255, 0);  // Lime green for perfect
const CRGB LED_COLOR_HOT = CRGB(255, 0, 0);  // Red for too hot

// Settings structure
struct Settings {
    bool useCelsius = true;
    bool soundEnabled = true;
    uint8_t brightness = 255;  // Default to 100% (PWM value 255)
    float emissivity = 0.95f;
    
    void save() {
        Preferences prefs;
        prefs.begin("settings", false);  // false = RW mode
        prefs.putBool("useCelsius", useCelsius);
        prefs.putUChar("brightness", brightness);
        prefs.putBool("soundEnabled", soundEnabled);
        prefs.putFloat("emissivity", emissivity);
        prefs.end();
    }
    
    void load() {
        Preferences prefs;
        prefs.begin("settings", true);  // true = read-only mode
        useCelsius = prefs.getBool("useCelsius", true);  // default to Celsius
        brightness = prefs.getUChar("brightness", 255);   // default to 100%
        soundEnabled = prefs.getBool("soundEnabled", true); // default to sound on
        emissivity = prefs.getFloat("emissivity", 0.95f);  // default to 0.95
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
    float lastTemp = 0;
    unsigned long lastDisplayUpdate = 0;
    static const unsigned long DISPLAY_UPDATE_INTERVAL = 250; // Update every 250ms
    
    void updateStatus(const String& message, uint32_t color = Config::Display::COLOR_TEXT) {
        statusMessage = message;
        statusColor = color;
    }

    bool shouldUpdateDisplay() {
        unsigned long currentTime = millis();
        if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
            lastDisplayUpdate = currentTime;
            return true;
        }
        return false;
    }
} state;

// Function declarations
void handleButtons();
void drawMainDisplay(float temperature);
void drawSettingsMenu();
void drawUnitSelection();
void drawBrightnessAdjustment();
void drawSoundSettings();
void drawEmissivityAdjustment();
void drawEmissivityConfirm();
void drawRestartConfirm();
void updateDisplay();
void handleKeyAndLED();
float readTemperature();
float celsiusToFahrenheit(float celsius);
bool isValidTemperature(float temp);
void drawStatusBox();
void playSuccessSound();
void playErrorSound();

const char* menuItems[] = {"Temperature Unit", "Brightness", "Sound", "Emissivity", "Exit"};

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\nStarting initialization...");

    // Initialize Core S3 with all features enabled
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    delay(500);  // Give hardware time to initialize
    
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
    FastLED.setBrightness(255);
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
    delay(500);
    Serial.println("I2C initialized");
    
    // Initial display update
    state.updateStatus("Ready", Config::Display::COLOR_SUCCESS);
    updateDisplay();
    Serial.println("Initialization complete");
}

void loop() {
    CoreS3.update();  // Update button states
    
    // Add debug output for state tracking
    static MenuState lastDebugState = MAIN_DISPLAY;
    if (lastDebugState != state.menuState) {
        Serial.printf("State changed from %d to %d\n", lastDebugState, state.menuState);
        lastDebugState = state.menuState;
    }

    handleButtons();
    handleKeyAndLED();
    
    static unsigned long lastTempUpdate = 0;
    static unsigned long lastDebugTime = 0;
    const unsigned long TEMP_UPDATE_INTERVAL = 100;  // Keep at 100ms for live readings
    const unsigned long DEBUG_INTERVAL = 1000;  // Debug output every second
    
    unsigned long currentTime = millis();
    
    // Debug output every second
    if (currentTime - lastDebugTime >= DEBUG_INTERVAL) {
        float currentTemp = readTemperature();
        Serial.print("Menu State: ");
        Serial.print(state.menuState);
        Serial.print(" IsMonitoring: ");
        Serial.print(state.isMonitoring);
        Serial.print(" Temp: ");
        Serial.print(currentTemp);
        Serial.print("°C (");
        Serial.print(celsiusToFahrenheit(currentTemp));
        Serial.println("°F)");
        lastDebugTime = millis();
    }
    
    // Temperature reading and display update
    if (currentTime - lastTempUpdate >= TEMP_UPDATE_INTERVAL) {
        float temp = readTemperature();
        bool tempChanged = abs(temp - state.lastTemp) > 0.5;
        
        if (state.menuState == MAIN_DISPLAY) {
            state.lastTemp = temp;  // Always update the last temperature
            if (tempChanged || state.shouldUpdateDisplay()) {
                updateDisplay();
            }
        }
        lastTempUpdate = currentTime;
    }
    
    delay(100);  // Small delay to prevent overwhelming the processor
}

// Blue botton = button 2
// Red botton = button 1
void handleButtons() {
    static bool lastButton1State = true;
    static bool lastButton2State = true;
    static unsigned long lastButtonPress = 0;
    const unsigned long debounceDelay = 250;
    
    bool button1State = digitalRead(BUTTON1_PIN);
    bool button2State = digitalRead(BUTTON2_PIN);
    
    // Add debounce check
    unsigned long currentTime = millis();
    if (currentTime - lastButtonPress < debounceDelay) {
        return;  // Exit if not enough time has passed since last button press
    }
    
    // Button 1 press (Menu/Select)
    if (!button1State && lastButton1State) {
        lastButtonPress = currentTime;
        playSuccessSound();
        Serial.print("Button 1 pressed - ");
        
        switch (state.menuState) {
            case MAIN_DISPLAY:
                state.menuState = SETTINGS_MENU;
                state.menuSelection = 0;
                Serial.println("Entering Settings Menu");
                break;
                
            case SETTINGS_MENU:
                Serial.printf("Selecting menu item %d: ", state.menuSelection);
                if (state.menuSelection == 4) { // Exit option
                    // First, change the state
                    state.menuState = MAIN_DISPLAY;
                    state.menuSelection = 0;  // Reset menu selection
                    settings.save();  // Save settings before exiting
                    
                    // Clear screen and force redraw
                    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
                    
                    // Debug output
                    Serial.println("Exiting to Main Display");
                    
                    // Force immediate redraw of main display
                    drawMainDisplay(readTemperature());
                    
                    // Reset button states and add delay
                    delay(250);  // Debounce delay
                    lastButton1State = digitalRead(BUTTON1_PIN);
                    lastButton2State = digitalRead(BUTTON2_PIN);
                    
                    // Force another update to ensure display is correct
                    updateDisplay();
                    
                    return;  // Exit immediately to prevent further processing
                }
                // Handle other menu options
                switch (state.menuSelection) {
                    case 0: 
                        state.menuState = UNIT_SELECTION; 
                        Serial.println("Temperature Unit");
                        break;
                    case 1: 
                        state.menuState = BRIGHTNESS_ADJUSTMENT; 
                        Serial.println("Brightness");
                        break;
                    case 2: 
                        state.menuState = SOUND_SETTINGS; 
                        Serial.println("Sound");
                        break;
                    case 3: 
                        state.menuState = EMISSIVITY_ADJUSTMENT; 
                        Serial.println("Emissivity");
                        break;
                }
                break;
                
            case UNIT_SELECTION:
            case BRIGHTNESS_ADJUSTMENT:
            case SOUND_SETTINGS:
                state.menuState = SETTINGS_MENU;  // Return to settings menu
                settings.save();
                Serial.println("Returning to Settings Menu");
                break;
                
            case EMISSIVITY_ADJUSTMENT:
                if (!button2State && lastButton2State) {  // Red button - decrease
                    if (settings.emissivity > Config::Emissivity::MIN) {
                        settings.emissivity -= Config::Emissivity::STEP;
                        settings.save();
                        Serial.printf("Emissivity decreased to: %.2f\n", settings.emissivity);
                        updateDisplay();
                    }
                }
                if (!button1State && lastButton1State) {  // Blue button - increase
                    if (settings.emissivity < Config::Emissivity::MAX) {
                        settings.emissivity += Config::Emissivity::STEP;
                        settings.save();
                        Serial.printf("Emissivity increased to: %.2f\n", settings.emissivity);
                        updateDisplay();
                    }
                }
                break;
                
            case EMISSIVITY_CONFIRM:
                if (!button1State && lastButton1State) {  // Blue button - Restart now
                    settings.save();
                    ESP.restart();
                }
                if (!button2State && lastButton2State) {  // Red button - Cancel
                    settings.load();  // Reload previous settings
                    state.menuState = SETTINGS_MENU;
                    updateDisplay();
                }
                break;
                
            case RESTART_CONFIRM:
                ESP.restart();
                break;
        }
        updateDisplay();
        delay(50);  // Additional small debounce delay
    }
    
    // Button 2 press (Navigate/Adjust)
    if (!button2State && lastButton2State) {
        lastButtonPress = currentTime;
        playSuccessSound();
        Serial.print("Button 2 pressed - ");
        
        switch (state.menuState) {
            case SETTINGS_MENU:
                state.menuSelection = (state.menuSelection + 1) % 5;  // Cycle through 0-4
                Serial.printf("Menu Selection changed to: %d\n", state.menuSelection);
                break;
                
            case UNIT_SELECTION:
                settings.useCelsius = !settings.useCelsius;
                settings.save();
                Serial.printf("Temperature unit changed to: %s\n", 
                    settings.useCelsius ? "Celsius" : "Fahrenheit");
                break;
                
            case BRIGHTNESS_ADJUSTMENT:
                settings.brightness = (settings.brightness == 255) ? 64 :
                                    (settings.brightness == 64) ? 128 :
                                    (settings.brightness == 128) ? 192 : 255;
                CoreS3.Display.setBrightness(settings.brightness);
                settings.save();
                Serial.printf("Brightness changed to: %d\n", settings.brightness);
                break;
                
            case SOUND_SETTINGS:
                settings.soundEnabled = !settings.soundEnabled;
                settings.save();
                Serial.printf("Sound %s\n", settings.soundEnabled ? "enabled" : "disabled");
                break;
                
            case EMISSIVITY_ADJUSTMENT:
                if (button2State) {  // Increase emissivity
                    if (settings.emissivity < Config::Emissivity::MAX) {
                        settings.emissivity += Config::Emissivity::STEP;
                        settings.save();
                        Serial.printf("Emissivity increased to: %.2f\n", settings.emissivity);
                    }
                } else {  // Decrease emissivity
                    if (settings.emissivity > Config::Emissivity::MIN) {
                        settings.emissivity -= Config::Emissivity::STEP;
                        settings.save();
                        Serial.printf("Emissivity decreased to: %.2f\n", settings.emissivity);
                    }
                }
                break;
                
            case RESTART_CONFIRM:
                state.menuState = SETTINGS_MENU;
                Serial.println("Restart cancelled");
                break;
        }
        updateDisplay();
        delay(50);  // Additional small debounce delay
    }
    
    lastButton1State = button1State;
    lastButton2State = button2State;
}

void handleKeyAndLED() {
    bool currentKeyState = digitalRead(KEY_PIN);
    
    // Key press detection (with debounce)
    static unsigned long lastKeyPressTime = 0;
    const unsigned long debounceDelay = 250;
    unsigned long currentTime = millis();
    
    if (!currentKeyState && !state.keyPressed && (currentTime - lastKeyPressTime > debounceDelay)) {
        state.keyPressed = true;
        lastKeyPressTime = currentTime;
        
        // Handle key press based on current menu state
        switch (state.menuState) {
            case MAIN_DISPLAY:
                state.isMonitoring = !state.isMonitoring;
                if (state.isMonitoring) {
                    state.updateStatus("Monitoring On", Config::Display::COLOR_SUCCESS);
                } else {
                    state.updateStatus("Monitoring Off", Config::Display::COLOR_ERROR);
                }
                break;
                
            case EMISSIVITY_ADJUSTMENT:
                state.menuState = EMISSIVITY_CONFIRM;
                updateDisplay();
                break;
        }
        
        // Toggle LED state
        state.ledActive = !state.ledActive;
        leds[0] = state.ledActive ? CRGB::Green : CRGB::Black;
        FastLED.show();
        
        // Play sound if enabled
        if (settings.soundEnabled) {
            playSuccessSound();
        }
    } else if (currentKeyState && state.keyPressed) {
        state.keyPressed = false;
    }
}

void updateDisplay() {
    // Clear any previous status message when entering settings menu
    if (state.menuState == SETTINGS_MENU) {
        state.statusMessage = "";
    }
    
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
        case EMISSIVITY_ADJUSTMENT:
            drawEmissivityAdjustment();
            break;
        case EMISSIVITY_CONFIRM:
            drawEmissivityConfirm();
            break;
        case RESTART_CONFIRM:
            drawRestartConfirm();
            break;
    }
    
    // Only draw status box if not in settings menu
    if (state.menuState != SETTINGS_MENU) {
        drawStatusBox();
    }
}

void drawMainDisplay(float temperature) {
    static float lastDisplayedTemp = -999;
    static MenuState lastMenuState = MAIN_DISPLAY;
    static bool wasInTarget = false;
    
    // Force a full redraw if we're coming from a different menu state
    bool needsFullRedraw = (lastMenuState != state.menuState);
    lastMenuState = state.menuState;
    
    // Ensure a full redraw when transitioning to the main display
    if (state.menuState == MAIN_DISPLAY && needsFullRedraw) {
        CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
        lastDisplayedTemp = -999;
        
        // Draw header
        CoreS3.Display.fillRoundRect(Config::Display::PADDING, 
                                   Config::Display::PADDING, 
                                   CoreS3.Display.width() - (Config::Display::PADDING * 2),
                                   Config::Display::HEADER_HEIGHT,
                                   Config::Display::CORNER_RADIUS,
                                   Config::Display::COLOR_SECONDARY_BG);
                                   
        // Draw header text
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextColor(Config::Display::COLOR_PRIMARY);
        CoreS3.Display.drawString("Temperature Monitor", 
                                CoreS3.Display.width() / 2,
                                Config::Display::PADDING + (Config::Display::HEADER_HEIGHT / 2));
    }
    
    if (isValidTemperature(temperature)) {
        float displayTemp = settings.useCelsius ? temperature : celsiusToFahrenheit(temperature);
        
        // Determine temperature status, display color, and LED color
        uint32_t tempColor;
        CRGB ledColor;
        String statusMsg;
        bool inTargetRange = false;
        
        if (settings.useCelsius) {
            if (displayTemp < TEMP_COLD_C) {
                tempColor = COLOR_COLD;
                ledColor = LED_COLOR_COLD;
                statusMsg = "Too Cold!";
            } else if (displayTemp < TEMP_MIN_C) {
                tempColor = Config::Display::COLOR_WARNING;
                ledColor = LED_COLOR_WARNING;
                statusMsg = "Warming Up";
            } else if (displayTemp <= TEMP_MAX_C) {
                tempColor = Config::Display::COLOR_SUCCESS;
                ledColor = LED_COLOR_PERFECT;
                statusMsg = "Perfect Temperature";
                inTargetRange = true;
            } else if (displayTemp > TEMP_HOT_C) {
                tempColor = Config::Display::COLOR_ERROR;
                ledColor = LED_COLOR_HOT;
                statusMsg = "Too Hot!";
            } else {
                tempColor = Config::Display::COLOR_WARNING;
                ledColor = LED_COLOR_WARNING;
                statusMsg = "Cooling Down";
            }
        } else {
            if (displayTemp < TEMP_COLD_F) {
                tempColor = COLOR_COLD;
                ledColor = LED_COLOR_COLD;
                statusMsg = "Too Cold!";
            } else if (displayTemp < TEMP_MIN_F) {
                tempColor = Config::Display::COLOR_WARNING;
                ledColor = LED_COLOR_WARNING;
                statusMsg = "Warming Up";
            } else if (displayTemp <= TEMP_MAX_F) {
                tempColor = Config::Display::COLOR_SUCCESS;
                ledColor = LED_COLOR_PERFECT;
                statusMsg = "Perfect Temperature";
                inTargetRange = true;
            } else if (displayTemp > TEMP_HOT_F) {
                tempColor = Config::Display::COLOR_ERROR;
                ledColor = LED_COLOR_HOT;
                statusMsg = "Too Hot!";
            } else {
                tempColor = Config::Display::COLOR_WARNING;
                ledColor = LED_COLOR_WARNING;
                statusMsg = "Cooling Down";
            }
        }
        
        // Update status message and color
        state.updateStatus(statusMsg, tempColor);
        
        // Update LED color if monitoring is active
        if (state.isMonitoring) {
            leds[0] = ledColor;
            FastLED.show();
        }
        
        // Play sound when entering target range
        if (inTargetRange && !wasInTarget && settings.soundEnabled) {
            CoreS3.Speaker.tone(1000, 100);
        }
        wasInTarget = inTargetRange;
        
        // Update if temperature changed significantly or needs full redraw
        if (abs(displayTemp - lastDisplayedTemp) >= 0.5 || needsFullRedraw) {
            char tempStr[10];
            char unitStr[2] = {settings.useCelsius ? 'C' : 'F', '\0'};
            sprintf(tempStr, "%d", (int)round(displayTemp));
            
            // Calculate all positions first
            int centerX = CoreS3.Display.width() / 2;
            int centerY = CoreS3.Display.height() / 2;
            
            // Calculate dimensions for temperature display
            CoreS3.Display.setTextSize(6);  // Larger temperature
            int tempWidth = CoreS3.Display.textWidth(tempStr);
            int tempHeight = CoreS3.Display.fontHeight();
            
            // Calculate dimensions for unit display
            CoreS3.Display.setTextSize(3);  // Smaller unit
            int unitWidth = CoreS3.Display.textWidth(unitStr);
            
            // Calculate the total width needed
            int totalWidth = tempWidth + unitWidth + Config::Display::PADDING;
            
            // Clear previous temperature area
            int clearWidth = totalWidth + (Config::Display::PADDING * 4);
            int clearHeight = tempHeight + (Config::Display::PADDING * 4);
            CoreS3.Display.fillRoundRect(centerX - (clearWidth/2),
                                       centerY - (clearHeight/2),
                                       clearWidth,
                                       clearHeight,
                                       Config::Display::CORNER_RADIUS,
                                       Config::Display::COLOR_SECONDARY_BG);
            
            // Draw temperature - always centered
            CoreS3.Display.setTextSize(6);
            CoreS3.Display.setTextColor(tempColor);  // Use status-based color
            CoreS3.Display.setTextDatum(middle_center);
            CoreS3.Display.drawString(tempStr, 
                                    centerX - (unitWidth/2), 
                                    centerY);
            
            // Draw unit
            CoreS3.Display.setTextSize(3);
            CoreS3.Display.drawString(unitStr,
                                    centerX + (tempWidth/2) + Config::Display::PADDING,
                                    centerY - (tempHeight/4));  // Align with top of temperature
            
            // Draw monitoring indicator if active
            if (state.isMonitoring) {
                int indicatorY = centerY + clearHeight/2 + Config::Display::PADDING * 2;
                CoreS3.Display.fillCircle(centerX, indicatorY, 5, Config::Display::COLOR_SUCCESS);
            }
            
            lastDisplayedTemp = displayTemp;
        }
    } else if (needsFullRedraw) {
        state.updateStatus("Invalid Temperature", Config::Display::COLOR_ERROR);
    }
}

void drawSettingsMenu() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    
    // Draw title
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextDatum(top_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_PRIMARY);
    CoreS3.Display.drawString("Settings", CoreS3.Display.width()/2, 20);
    
    // Calculate positions for menu items
    const int startY = 70;
    const int itemSpacing = 35;
    const int totalItems = 5;  // Including Exit
    
    // Draw all menu items
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextDatum(middle_left);
    
    for (int i = 0; i < totalItems; i++) {
        int y = startY + (i * itemSpacing);
        if (i == state.menuSelection) {
            // Selected item
            CoreS3.Display.fillRoundRect(10, y - 15, CoreS3.Display.width() - 20, 30,
                                       Config::Display::CORNER_RADIUS,
                                       Config::Display::COLOR_PRIMARY);
            CoreS3.Display.setTextColor(Config::Display::COLOR_BACKGROUND);
        } else {
            // Unselected items
            CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
        }
        CoreS3.Display.drawString(menuItems[i], 20, y);
    }
    
    // Draw navigation hint
    CoreS3.Display.setTextSize(1);
    CoreS3.Display.setTextDatum(bottom_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString("Red: Select  |  Blue: Next", CoreS3.Display.width()/2, CoreS3.Display.height() - 10);
}

void drawUnitSelection() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    
    // Draw title
    CoreS3.Display.drawString("Temperature Unit", CoreS3.Display.width()/2, 30);
    
    // Draw current selection
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.drawString(settings.useCelsius ? "Celsius" : "Fahrenheit", CoreS3.Display.width()/2, CoreS3.Display.height()/2);
    
    // Clear the bottom area first
    CoreS3.Display.fillRect(0, CoreS3.Display.height() - 60, CoreS3.Display.width(), 60, Config::Display::COLOR_BACKGROUND);
    
    // Draw instructions
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Press Red to save", CoreS3.Display.width()/2, CoreS3.Display.height() - 80);
    CoreS3.Display.drawString("Press Blue to change", CoreS3.Display.width()/2, CoreS3.Display.height() - 40);
}

void drawBrightnessAdjustment() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    
    // Draw title
    CoreS3.Display.drawString("Brightness", CoreS3.Display.width()/2, 30);
    
    // Draw current value
    CoreS3.Display.setTextSize(3);
    String brightnessText;
    switch(settings.brightness) {
        case 64:  brightnessText = "25%"; break;
        case 128: brightnessText = "50%"; break;
        case 192: brightnessText = "75%"; break;
        case 255: brightnessText = "100%"; break;
        default:  brightnessText = "100%"; break;  // Default to 100% for any other value
    }
    CoreS3.Display.drawString(brightnessText, CoreS3.Display.width()/2, CoreS3.Display.height()/2);
    
    // Clear the bottom area first
    CoreS3.Display.fillRect(0, CoreS3.Display.height() - 60, CoreS3.Display.width(), 60, Config::Display::COLOR_BACKGROUND);
    
    // Draw instructions
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Press Red to save", CoreS3.Display.width()/2, CoreS3.Display.height() - 80);
    CoreS3.Display.drawString("Press Blue to change", CoreS3.Display.width()/2, CoreS3.Display.height() - 40);
}

void drawSoundSettings() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    
    // Draw title
    CoreS3.Display.drawString("Sound", CoreS3.Display.width()/2, 30);
    
    // Draw current state
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.drawString(settings.soundEnabled ? "ON" : "OFF", CoreS3.Display.width()/2, CoreS3.Display.height()/2);
    
    // Clear the bottom area first
    CoreS3.Display.fillRect(0, CoreS3.Display.height() - 60, CoreS3.Display.width(), 60, Config::Display::COLOR_BACKGROUND);
    
    // Draw instructions
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Press Red to save", CoreS3.Display.width()/2, CoreS3.Display.height() - 80);
    CoreS3.Display.drawString("Press Blue to change", CoreS3.Display.width()/2, CoreS3.Display.height() - 40);
}

void drawEmissivityAdjustment() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    
    // Draw title
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextDatum(top_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_PRIMARY);
    CoreS3.Display.drawString("Emissivity Setting", CoreS3.Display.width()/2, 10);
    
    // Draw current value
    char valueStr[10];
    sprintf(valueStr, "%.2f", settings.emissivity);
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString(valueStr, CoreS3.Display.width()/2, 100);
    
    // Draw buttons with modern style
    const int btnWidth = 80;
    const int btnHeight = 40;
    
    // Up button
    if (settings.emissivity < Config::Emissivity::MAX) {
        CoreS3.Display.fillRoundRect(220, 60, btnWidth, btnHeight, 
                                   Config::Display::CORNER_RADIUS,
                                   Config::Display::COLOR_PRIMARY);
        CoreS3.Display.setTextColor(Config::Display::COLOR_BACKGROUND);
        CoreS3.Display.drawString("+", 260, 80);
    }
    
    // Down button
    if (settings.emissivity > Config::Emissivity::MIN) {
        CoreS3.Display.fillRoundRect(220, 140, btnWidth, btnHeight,
                                   Config::Display::CORNER_RADIUS,
                                   Config::Display::COLOR_PRIMARY);
        CoreS3.Display.setTextColor(Config::Display::COLOR_BACKGROUND);
        CoreS3.Display.drawString("-", 260, 160);
    }
    
    // Back button
    CoreS3.Display.fillRoundRect(10, 180, 100, 50,
                                Config::Display::CORNER_RADIUS,
                                Config::Display::COLOR_ACCENT);
    CoreS3.Display.setTextColor(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.drawString("Back", 60, 205);
    
    // Draw min/max values
    CoreS3.Display.setTextSize(1);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    char minStr[15], maxStr[15];
    sprintf(minStr, "Min: %.2f", Config::Emissivity::MIN);
    sprintf(maxStr, "Max: %.2f", Config::Emissivity::MAX);
    CoreS3.Display.drawString(minStr, 20, 140);
    CoreS3.Display.drawString(maxStr, 20, 60);
}

void drawEmissivityConfirm() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    
    // Draw title and message
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextDatum(top_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_WARNING);
    CoreS3.Display.drawString("Restart Required", CoreS3.Display.width()/2, 20);
    
    CoreS3.Display.setTextSize(1);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString("Device must be restarted for", CoreS3.Display.width()/2, 60);
    CoreS3.Display.drawString("emissivity changes to take effect", CoreS3.Display.width()/2, 80);
    
    // Draw buttons
    const int btnWidth = 120;
    const int btnHeight = 40;
    const int btnSpacing = 20;
    const int startY = 140;
    
    // Restart button (Blue - Button 1)
    CoreS3.Display.fillRoundRect(CoreS3.Display.width()/2 - btnWidth - btnSpacing/2, startY,
                               btnWidth, btnHeight, Config::Display::CORNER_RADIUS,
                               Config::Display::COLOR_ERROR);
    CoreS3.Display.setTextColor(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.drawString("Cancel", CoreS3.Display.width()/2 - btnWidth/2 - btnSpacing/2, startY + btnHeight/2);
    
    // Cancel button (Red - Button 2)
    CoreS3.Display.fillRoundRect(CoreS3.Display.width()/2 + btnSpacing/2, startY,
                               btnWidth, btnHeight, Config::Display::CORNER_RADIUS,
                               Config::Display::COLOR_PRIMARY);
    CoreS3.Display.setTextColor(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.drawString("Restart Now ", CoreS3.Display.width()/2 + btnWidth/2 + btnSpacing/2, startY + btnHeight/2);
}

void drawRestartConfirm() {
    static bool lastButton1State = true;
    static bool lastButton2State = true;
    bool button1State = digitalRead(BUTTON1_PIN);
    bool button2State = digitalRead(BUTTON2_PIN);
    
    // Button 1 (Restart)
    if (!button2State && lastButton2State) {
        if (settings.soundEnabled) {
            playSuccessSound();
        }
        delay(500);
        ESP.restart();
    }
    
    // Button 2 (Cancel)
    if (!button1State && lastButton1State) {
        state.menuState = SETTINGS_MENU;
        if (settings.soundEnabled) {
            playSuccessSound();
        }
        delay(50);
    }
    
    lastButton1State = button1State;
    lastButton2State = button2State;
}

void drawStatusBox() {
    static String lastStatus = "";
    static uint32_t lastColor = 0;
    
    // Only redraw if the status or color has changed
    if (lastStatus != state.statusMessage || lastColor != state.statusColor) {
        const int boxHeight = 50;
        const int boxMargin = Config::Display::PADDING;
        const int boxWidth = CoreS3.Display.width() - (boxMargin * 2);
        const int boxY = CoreS3.Display.height() - boxHeight - boxMargin;
        
        // Draw status box with rounded corners
        CoreS3.Display.fillRoundRect(boxMargin, 
                                   boxY, 
                                   boxWidth, 
                                   boxHeight, 
                                   Config::Display::CORNER_RADIUS,
                                   Config::Display::COLOR_SECONDARY_BG);
        
        // Add subtle border
        CoreS3.Display.drawRoundRect(boxMargin,
                                   boxY,
                                   boxWidth,
                                   boxHeight,
                                   Config::Display::CORNER_RADIUS,
                                   Config::Display::COLOR_BORDER);
        
        // Only draw status text if there is a message
        if (state.statusMessage.length() > 0) {
            CoreS3.Display.setTextSize(2);
            CoreS3.Display.setTextDatum(middle_center);
            CoreS3.Display.setTextColor(state.statusColor);
            
            // Draw text with a slight shadow effect
            CoreS3.Display.drawString(state.statusMessage.c_str(), 
                                    CoreS3.Display.width()/2 + 1,
                                    boxY + boxHeight/2 + 1,
                                    Config::Display::COLOR_BACKGROUND);
            CoreS3.Display.drawString(state.statusMessage.c_str(),
                                    CoreS3.Display.width()/2,
                                    boxY + boxHeight/2,
                                    state.statusColor);
        }
        
        lastStatus = state.statusMessage;
        lastColor = state.statusColor;
    }
}

float readTemperature() {
    Wire.beginTransmission(0x5A);          // Start I2C communication with MLX90614
    Wire.write(0x07);                      // Command for ambient temperature
    if (Wire.endTransmission(false) != 0) {  // Send restart condition
        Serial.println("I2C communication error");
        return -999.0;
    }
    
    Wire.requestFrom(0x5A, 3);             // Request 3 bytes from MLX90614
    if (Wire.available() == 3) {
        uint8_t lsb = Wire.read();
        uint8_t msb = Wire.read();
        uint8_t pec = Wire.read();
        
        // Set emissivity
        Wire.beginTransmission(0x5A);
        Wire.write(0x24);                   // Emissivity register address
        uint16_t emissivityData = settings.emissivity * 65535;
        Wire.write(emissivityData & 0xFF);  // LSB
        Wire.write(emissivityData >> 8);    // MSB
        Wire.endTransmission();
        
        // Calculate temperature
        float tempK = ((msb << 8) | lsb) * 0.02;
        float tempC = tempK - 273.15;
        
        if (isValidTemperature(tempC)) {
            return tempC;
        }
    }
    
    return -999.0;  // Error value
}

float celsiusToFahrenheit(float celsius) {
    return (celsius * 9.0 / 5.0) + 32.0;
}

bool isValidTemperature(float temp) {
    return temp >= -70 && temp <= 380;
}

void playSuccessSound() {
    if (settings.soundEnabled) {
        CoreS3.Speaker.tone(1047, 100);  // 1047Hz for 100ms
        delay(100);
        CoreS3.Speaker.tone(1319, 100);  // 1319Hz for 100ms
        delay(100);
    }
}

void playErrorSound() {
    if (settings.soundEnabled) {
        CoreS3.Speaker.tone(880, 200);  // 880Hz for 200ms
        delay(250);
        CoreS3.Speaker.tone(660, 200);  // 660Hz for 200ms
        delay(200);
    }
}
