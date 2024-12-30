// 1. Include statements
#include <Wire.h>
#include <M5GFX.h>
#include <M5CoreS3.h>
#include <M5Unified.h>
#include <M5UNIT_NCIR2.h>
#include <time.h>
#include <sys/time.h>
#include <Preferences.h>

// 2. Object initialization
M5UNIT_NCIR2 ncir2;
Preferences preferences;

// Temperature and timing configuration
namespace TempConfig {
    static const int16_t MIN_C = 304;     // 580°F
    static const int16_t MAX_C = 371;     // 700°F
    static const int16_t TARGET_C = 338;  // 640°F
    static const int16_t TOLERANCE_C = 5;
}

// Timing configuration (ms)
namespace TimeConfig {
    static const unsigned long TEMP_UPDATE_INTERVAL = 250;     // Temperature update interval
    static const unsigned long BATTERY_UPDATE_INTERVAL = 60000; // Battery check interval
    static const unsigned long STATUS_UPDATE_INTERVAL = 5000;   // Status display update
    static const unsigned long TOUCH_DEBOUNCE = 100;           // Touch input debounce
    static const unsigned long BEEP_INTERVAL = 2000;           // Alert beep interval
    static const unsigned long DEBUG_UPDATE_INTERVAL = 10000;   // Debug info update
    static const unsigned long BATTERY_CHECK = 60000;           // Battery level check interval
}

// Display configuration
namespace DisplayConfig {
    // Retro-modern color scheme
    const uint32_t COLOR_BACKGROUND = 0x000B1E;  // Deep navy blue
    const uint32_t COLOR_TEXT = 0x00FF9C;        // Bright cyan/mint
    const uint32_t COLOR_BUTTON = 0x1B3358;      // Muted blue
    const uint32_t COLOR_BUTTON_ACTIVE = 0x3CD070; // Bright green
    const uint32_t COLOR_COLD = 0x00A6FB;        // Bright blue
    const uint32_t COLOR_GOOD = 0x3CD070;        // Bright green
    const uint32_t COLOR_HOT = 0xFF0F7B;         // Neon pink
    const uint32_t COLOR_WARNING = 0xFFB627;     // Warm amber
    const uint32_t COLOR_DISABLED = 0x424242;    // Dark gray
    const uint32_t COLOR_BATTERY_LOW = 0xFF0000;   // Red
    const uint32_t COLOR_BATTERY_MED = 0xFFA500;   // Orange
    const uint32_t COLOR_BATTERY_GOOD = 0x00FF00;  // Green
    const uint32_t COLOR_CHARGING = 0xFFFF00;      // Yellow
    const uint32_t COLOR_ICE_BLUE = 0x87CEFA;    // Light ice blue
    const uint32_t COLOR_NEON_GREEN = 0x39FF14;  // Bright neon green
    const uint32_t COLOR_LAVA = 0xFF4500;        // Red-orange lava color
    const uint32_t COLOR_HIGHLIGHT = 0x6B6B6B;   // Button highlight color
    const uint32_t COLOR_SELECTED = 0x3CD070;    // Selected item color (bright green)
    const uint32_t COLOR_HEADER = 0x1B3358;      // Header color (muted blue)
    const uint32_t COLOR_PROGRESS = 0x3CD070;    // Progress bar color (bright green)
}

// Touch handling
class TouchHandler {
private:
    static const int TOUCH_SAMPLES = 3;
    static const int TOUCH_THRESHOLD = 10;
    static const unsigned long DOUBLE_TAP_TIME = 300;
    
    int32_t lastX = 0;
    int32_t lastY = 0;
    unsigned long lastTouchTime = 0;
    unsigned long lastTapTime = 0;
    bool isDragging = false;
    
public:
    struct TouchEvent {
        enum class Type {
            NONE,
            TAP,
            DOUBLE_TAP,
            DRAG_START,
            DRAGGING,
            DRAG_END
        };
        
        Type type;
        int32_t x;
        int32_t y;
        int32_t deltaX;
        int32_t deltaY;
    };
    
    TouchEvent update() {
        TouchEvent event = {TouchEvent::Type::NONE, 0, 0, 0, 0};
        
        if (CoreS3.Touch.getCount()) {
            auto touchData = CoreS3.Touch.getDetail();
            int32_t x = touchData.x;
            int32_t y = touchData.y;
            unsigned long currentTime = millis();
            
            if (!isDragging) {
                if (abs(x - lastX) > TOUCH_THRESHOLD || abs(y - lastY) > TOUCH_THRESHOLD) {
                    isDragging = true;
                    event.type = TouchEvent::Type::DRAG_START;
                } else if (currentTime - lastTouchTime > TimeConfig::TOUCH_DEBOUNCE) {
                    if (currentTime - lastTapTime < DOUBLE_TAP_TIME) {
                        event.type = TouchEvent::Type::DOUBLE_TAP;
                        lastTapTime = 0;  // Reset to prevent triple tap
                    } else {
                        event.type = TouchEvent::Type::TAP;
                        lastTapTime = currentTime;
                    }
                }
            } else {
                event.type = TouchEvent::Type::DRAGGING;
            }
            
            event.x = x;
            event.y = y;
            event.deltaX = x - lastX;
            event.deltaY = y - lastY;
            
            lastX = x;
            lastY = y;
            lastTouchTime = currentTime;
        } else if (isDragging) {
            event.type = TouchEvent::Type::DRAG_END;
            event.x = lastX;
            event.y = lastY;
            isDragging = false;
        }
        
        return event;
    }
    
    void reset() {
        lastX = 0;
        lastY = 0;
        lastTouchTime = 0;
        lastTapTime = 0;
        isDragging = false;
    }
};

TouchHandler touchHandler;

// Device settings
struct Settings {
    bool useCelsius = true;
    bool soundEnabled = true;
    bool autoSleep = true;
    int brightness = 64;
    int sleepTimeout = 5;
    float emissivity = 0.87;  // Adjusted for quartz glass
    
    void load(Preferences& prefs) {
        useCelsius = prefs.getBool("useCelsius", true);
        soundEnabled = prefs.getBool("soundEnabled", true);
        autoSleep = prefs.getBool("autoSleep", true);
        brightness = prefs.getInt("brightness", 64);
        sleepTimeout = prefs.getInt("sleepTimeout", 5);
        emissivity = prefs.getFloat("emissivity", 0.87);
    }
    
    void save(Preferences& prefs) {
        prefs.putBool("useCelsius", useCelsius);
        prefs.putBool("soundEnabled", soundEnabled);
        prefs.putBool("autoSleep", autoSleep);
        prefs.putInt("brightness", brightness);
        prefs.putInt("sleepTimeout", sleepTimeout);
        prefs.putFloat("emissivity", emissivity);
    }
};

// Device state management
struct DeviceState {
    int16_t currentTemp = 0;
    bool isMonitoring = false;
    int16_t lastDisplayTemp = -999;
    String lastStatus = "";
    int lastBatLevel = -1;
    bool lastChargeState = false;
    unsigned long lastBeepTime = 0;
    unsigned long lastTempUpdate = 0;
    unsigned long lastBatteryUpdate = 0;
    unsigned long lastActivityTime = 0;
    unsigned long lastDebugUpdate = 0;
    bool lowBatteryWarningShown = false;
    float tempHistory[5] = {0};  // Keep last 5 temperature readings
    int historyIndex = 0;
    bool inSettingsMenu = false;
    int selectedMenuItem = -1;
    int settingsScrollPosition = 0;
    float tempSum = 0;  // Sum for moving average
    int tempCount = 0;  // Count for moving average
};

// Update your Button struct definition
struct Button {
    int16_t x;
    int16_t y;
    uint16_t w;
    uint16_t h;
    const char* label;
    bool highlighted;
    bool enabled;
};

// Error handling
namespace ErrorHandling {
    enum class ErrorCode {
        NONE = 0,
        SENSOR_INIT_FAILED,
        MEMORY_ALLOCATION_FAILED,
        TEMPERATURE_READ_ERROR,
        BATTERY_READ_ERROR,
        DISPLAY_ERROR
    };
    
    static ErrorCode lastError = ErrorCode::NONE;
    static char errorMessage[64] = {0};
    
    void setError(ErrorCode code, const char* msg) {
        lastError = code;
        strncpy(errorMessage, msg, sizeof(errorMessage) - 1);
        errorMessage[sizeof(errorMessage) - 1] = '\0';
    }
    
    void clearError() {
        lastError = ErrorCode::NONE;
        errorMessage[0] = '\0';
    }
    
    bool hasError() {
        return lastError != ErrorCode::NONE;
    }
}

// Improved temperature filtering
class TemperatureFilter {
private:
    static const int HISTORY_SIZE = 5;
    float history[HISTORY_SIZE];
    int index = 0;
    bool isFull = false;
    
public:
    void reset() {
        index = 0;
        isFull = false;
        for (int i = 0; i < HISTORY_SIZE; i++) {
            history[i] = 0;
        }
    }
    
    float addReading(float temp) {
        history[index] = temp;
        index = (index + 1) % HISTORY_SIZE;
        if (index == 0) isFull = true;
        
        // Calculate median of last readings
        float sortedTemp[HISTORY_SIZE];
        int validCount = isFull ? HISTORY_SIZE : index;
        
        // Copy valid readings
        for (int i = 0; i < validCount; i++) {
            sortedTemp[i] = history[i];
        }
        
        // Sort array
        for (int i = 0; i < validCount - 1; i++) {
            for (int j = 0; j < validCount - i - 1; j++) {
                if (sortedTemp[j] > sortedTemp[j + 1]) {
                    float temp = sortedTemp[j];
                    sortedTemp[j] = sortedTemp[j + 1];
                    sortedTemp[j + 1] = temp;
                }
            }
        }
        
        // Return median
        if (validCount == 0) return 0;
        if (validCount % 2 == 0) {
            return (sortedTemp[validCount/2 - 1] + sortedTemp[validCount/2]) / 2.0f;
        } else {
            return sortedTemp[validCount/2];
        }
    }
};

TemperatureFilter tempFilter;

// Function declarations
void drawButton(Button &btn, uint32_t color);
void drawInterface();
void updateDisplay();
void handleTemperatureAlerts();
void drawBatteryStatus();
bool selectTemperatureUnit();
bool touchInButton(Button &btn, int32_t touch_x, int32_t touch_y);
int16_t celsiusToFahrenheit(int16_t celsius);
int16_t fahrenheitToCelsius(int16_t fahrenheit);
void updateTemperatureDisplay(int16_t temp);
void adjustEmissivity();
void updateStatusDisplay(const char* status, uint32_t color);
void showLowBatteryWarning(int batteryLevel);
void handleTouchInput();
void toggleDisplayMode();
void toggleMonitoring();
void handleAutoSleep(unsigned long currentMillis, unsigned long lastActivityTime);
void updateTemperatureTrend();
void checkBatteryWarning(unsigned long currentMillis);
void drawSettingsMenu();
void handleSettingsMenu();
void saveSettings();
void loadSettings();
void enterSleepMode();
void enterEmissivityMode();
void updateStatusMessage();
void checkForErrors();
void wakeUp();
void drawToggleSwitch(int x, int y, bool state);
void showRestartConfirmation(float oldEmissivity, float newEmissivity);
void exitSettingsMenu();
void playTouchSound(bool success = true);
void drawBrightnessButtons(int x, int y, int currentBrightness);

// Button declarations
Button monitorBtn = {0, 0, 0, 0, "Monitor", false, true};
Button settingsBtn = {0, 0, 0, 0, "Settings", false, true};  // Changed from emissivityBtn

// Sprite management
class SpriteManager {
private:
    static LGFX_Sprite* tempSprite;
    
public:
    static bool initialize() {
        if (tempSprite == nullptr) {
            tempSprite = new LGFX_Sprite(&CoreS3.Display);
            if (tempSprite == nullptr) return false;
            if (!tempSprite->createSprite(CoreS3.Display.width(), TEMP_BOX_HEIGHT)) {
                delete tempSprite;
                tempSprite = nullptr;
                return false;
            }
        }
        return true;
    }
    
    static void cleanup() {
        if (tempSprite != nullptr) {
            tempSprite->deleteSprite();
            delete tempSprite;
            tempSprite = nullptr;
        }
    }
    
    static LGFX_Sprite* getSprite() {
        return tempSprite;
    }
};

LGFX_Sprite* SpriteManager::tempSprite = nullptr;

Settings settings;
DeviceState state;

// Touch and feedback constants
const int TOUCH_THRESHOLD = 10;      // Minimum movement to register as a touch

// Speaker constants
const int BEEP_FREQUENCY = 1000;  // 1kHz tone
const int BEEP_DURATION = 100;    // 100ms beep

// Layout constants - adjusted TEMP_BOX_HEIGHT
const int HEADER_HEIGHT = 30;
const int TEMP_BOX_HEIGHT = 70;  // Reduced from 90 to 70
const int STATUS_BOX_HEIGHT = 45;
const int MARGIN = 10;
const int BUTTON_HEIGHT = 50;
const int BUTTON_SPACING = 10;

// Emissivity constants and variables
const float EMISSIVITY_MIN = 0.1;
const float EMISSIVITY_MAX = 1.0;
const float EMISSIVITY_STEP = 0.05;

// Menu configuration
namespace MenuConfig {
    const int MENU_ITEMS = 6;  // Total number of menu items
    const char* MENU_LABELS[MENU_ITEMS] = {
        "Temperature Unit",
        "Sound",
        "Brightness",
        "Auto Sleep",
        "Emissivity",
        "Exit"
    };
}

void drawButton(Button &btn, uint32_t color) {
    // Draw button with retro-modern style
    CoreS3.Display.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 8, color);
    
    // Add highlight effect for 3D appearance
    CoreS3.Display.drawFastHLine(btn.x + 4, btn.y + 2, btn.w - 8, color + 0x303030);
    CoreS3.Display.drawFastVLine(btn.x + 2, btn.y + 4, btn.h - 8, color + 0x303030);
    
    // Add shadow effect
    CoreS3.Display.drawFastHLine(btn.x + 4, btn.y + btn.h - 2, btn.w - 8, color - 0x303030);
    CoreS3.Display.drawFastVLine(btn.x + btn.w - 2, btn.y + 4, btn.h - 8, color - 0x303030);
    
    // Draw text with retro glow effect
    if (btn.label) {
        // Draw glow
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextColor(color + 0x404040);
        for (int i = -1; i <= 1; i += 2) {
            for (int j = -1; j <= 1; j += 2) {
                CoreS3.Display.drawString(btn.label, btn.x + btn.w/2 + i, btn.y + btn.h/2 + j);
            }
        }
        // Draw main text
        CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
        CoreS3.Display.drawString(btn.label, btn.x + btn.w/2, btn.y + btn.h/2);
    }
    
    // Add disabled state visual
    if (!btn.enabled) {
        CoreS3.Display.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 8, 
            (DisplayConfig::COLOR_DISABLED & 0xFCFCFC) | 0x808080);
    }
}

void drawBrightnessButtons(int x, int y, int currentBrightness) {
    const int btnWidth = 60;
    const int btnHeight = 40;
    const int spacing = 10;
    const int levels[4] = {64, 128, 192, 255};  // 25%, 50%, 75%, 100%
    
    for (int i = 0; i < 4; i++) {
        int btnX = x + (btnWidth + spacing) * i;
        bool isSelected = (currentBrightness >= levels[i] - 32 && currentBrightness <= levels[i] + 32);
        
        // Draw button
        CoreS3.Display.fillRoundRect(btnX, y, btnWidth, btnHeight, 5, 
            isSelected ? DisplayConfig::COLOR_BUTTON_ACTIVE : DisplayConfig::COLOR_BUTTON);
        
        // Draw percentage text
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
        char label[5];
        sprintf(label, "%d%%", (levels[i] * 100) / 255);
        CoreS3.Display.drawString(label, btnX + btnWidth/2, y + btnHeight/2);
    }
}

void wakeUp() {
    // Restore display
    CoreS3.Display.wakeup();
    CoreS3.Display.setBrightness((settings.brightness * 255) / 100);
    
    // Reset activity timer
    state.lastActivityTime = millis();
    
    // Redraw interface
    drawInterface();
    updateTemperatureDisplay(ncir2.getTempValue());
    
    // Restore monitoring state and status
    if (state.isMonitoring) {
        handleTemperatureAlerts();
    } else {
        updateStatusDisplay("Ready", DisplayConfig::COLOR_TEXT);
    }
    
    // Update battery status
    drawBatteryStatus();
}

void printDebugInfo() {
    Serial.println("=== Debug Info ===");
    Serial.print("Temperature: "); Serial.println(state.currentTemp / 100.0);
    Serial.print("Battery Level: "); Serial.print(CoreS3.Power.getBatteryLevel()); Serial.println("%");
    Serial.print("Charging: "); Serial.println(CoreS3.Power.isCharging() ? "Yes" : "No");
    Serial.print("Monitoring: "); Serial.println(state.isMonitoring ? "Yes" : "No");
    Serial.print("Emissivity: "); Serial.println(settings.emissivity);
    Serial.println("================");
}

void checkForErrors() {
    
    // Check sprite allocation
    if (!SpriteManager::getSprite()) {
        ErrorHandling::setError(ErrorHandling::ErrorCode::MEMORY_ALLOCATION_FAILED, "Failed to allocate sprite memory");
        return;
    }
    
    // Check battery voltage is reasonable
    float batteryLevel = CoreS3.Power.getBatteryLevel();
    if (batteryLevel < 0 || batteryLevel > 100) {
        ErrorHandling::setError(ErrorHandling::ErrorCode::BATTERY_READ_ERROR, "Invalid battery reading");
        return;
    }
    
    ErrorHandling::clearError();
}

void updateStatusMessage() {
    // This is already handled by updateStatusDisplay()
    handleTemperatureAlerts();
}

void enterEmissivityMode() {
    // This function already exists as adjustEmissivity()
    adjustEmissivity();
}

void enterSleepMode() {
    // Save current state before sleeping
    ncir2.setLEDColor(0);  // Turn off LED
    CoreS3.Display.setBrightness(0);  // Turn off display
    CoreS3.Display.sleep();  // Put display to sleep
    
    // Wait for touch to wake up
    while (true) {
        CoreS3.update();  // Keep updating the core
        auto touch = CoreS3.Touch.getDetail();
        
        if (touch.wasPressed()) {
            wakeUp();
            break;
        }
        delay(100);  // Small delay to prevent tight polling
    }
}

void loadSettings() {
    settings.load(preferences);
    // Apply loaded settings
    CoreS3.Display.setBrightness(settings.brightness);
}

void saveSettings() {
    settings.save(preferences);
}

void handleSettingsMenu();

void updateTemperatureDisplay(int16_t temp) {
    static int16_t lastTemp = -999;
    static uint32_t lastUpdate = 0;
    
    // Update only if temperature changed or it's been more than 500ms
    if (temp != lastTemp || (millis() - lastUpdate) > 500) {
        // Calculate temperature box dimensions
        int screenWidth = CoreS3.Display.width();
        int boxWidth = screenWidth - (MARGIN * 2);
        int boxHeight = TEMP_BOX_HEIGHT;
        int boxY = HEADER_HEIGHT + MARGIN;
        
        // Get sprite
        LGFX_Sprite* tempSprite = SpriteManager::getSprite();
        
        // Clear sprite
        tempSprite->fillSprite(DisplayConfig::COLOR_BACKGROUND);
        tempSprite->drawRoundRect(0, 0, boxWidth, boxHeight, 8, DisplayConfig::COLOR_TEXT);
        
        // Format temperature string with whole numbers
        char tempStr[16];
        float displayTemp;
        
        // The sensor returns Celsius * 100, convert appropriately
        displayTemp = temp / 100.0f;
        
        // Format with whole numbers and unit
        if (settings.useCelsius) {
            sprintf(tempStr, "%.0f°C", displayTemp);
        } else {
            displayTemp = (displayTemp * 9.0f / 5.0f) + 32.0f;
            sprintf(tempStr, "%.0f°F", displayTemp);
        }
        
        // Set text properties
        tempSprite->setTextSize(4);
        tempSprite->setTextDatum(middle_center);
        
        // Choose color based on temperature in Fahrenheit for consistent behavior
        float tempF = displayTemp * 9.0f / 5.0f + 32.0f;
        uint32_t tempColor;
        uint32_t ledColor = 0; // Default LED color (off)
        String statusMsg;
        
        // Updated temperature thresholds with status messages
        if (tempF < 300) {
            tempColor = DisplayConfig::COLOR_WARNING;
            ledColor = 0x000000; // LED off
            statusMsg = "Temperature out of range";
        }
        else if (tempF <= 500) {
            tempColor = DisplayConfig::COLOR_ICE_BLUE;
            ledColor = 0x87CEFA; // Blue
            statusMsg = "Too Cold";
        }
        else if (tempF <= 640) {
            tempColor = DisplayConfig::COLOR_NEON_GREEN;
            ledColor = 0x39FF14; // Green
            statusMsg = "Perfect";
        }
        else if (tempF <= 800) {
            tempColor = DisplayConfig::COLOR_LAVA;
            ledColor = 0xFF4500; // Red
            statusMsg = "Way too Hot";
        }
        else {
            tempColor = DisplayConfig::COLOR_WARNING;
            ledColor = 0x000000; // LED off
            statusMsg = "Temperature out of range";
        }
        
        // Update NCIR2 LED color
        ncir2.setLEDColor(ledColor);
        
        // Update status display
        updateStatusDisplay(statusMsg.c_str(), tempColor);
        
        tempSprite->setTextColor(tempColor);
        tempSprite->drawString(tempStr, boxWidth/2, boxHeight/2);
        
        // Push sprite to display
        tempSprite->pushSprite(MARGIN, boxY);
        
        lastTemp = temp;
        lastUpdate = millis();
    }
}

void playTouchSound(bool success) {
    if (!settings.soundEnabled) return;
    
    // Initialize speaker
    CoreS3.Speaker.begin();
    CoreS3.Speaker.setVolume(128);  // Set to 50% volume
    
    if (success) {
        CoreS3.Speaker.tone(1000, 50);  // 1kHz for 50ms
    } else {
        CoreS3.Speaker.tone(500, 100);  // 500Hz for 100ms
    }
    
    delay(50);  // Wait for sound to finish
    CoreS3.Speaker.end();  // Cleanup speaker
}

void drawInterface() {
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    
    // Draw header
    CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), HEADER_HEIGHT, DisplayConfig::COLOR_HEADER);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.drawString("Terp Meter", CoreS3.Display.width()/2, HEADER_HEIGHT/2);
    
    // Draw temperature display area
    updateTemperatureDisplay(state.currentTemp);
    
    // Draw buttons at the bottom
    Button monitorBtn = {MARGIN, CoreS3.Display.height() - 60, 100, 40, "Monitor", false, true};
    Button settingsBtn = {CoreS3.Display.width() - 100 - MARGIN, CoreS3.Display.height() - 60, 100, 40, "Settings", false, true};
    
    // Draw monitor button with highlight if active
    if (state.isMonitoring) {
        drawButton(monitorBtn, DisplayConfig::COLOR_SELECTED);  // Use highlight color when monitoring
        CoreS3.Display.setTextColor(DisplayConfig::COLOR_BACKGROUND);  // Invert text color
    } else {
        drawButton(monitorBtn, DisplayConfig::COLOR_BUTTON);
        CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    }
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.drawString(monitorBtn.label, monitorBtn.x + monitorBtn.w/2, monitorBtn.y + monitorBtn.h/2);
    
    // Draw settings button
    drawButton(settingsBtn, DisplayConfig::COLOR_BUTTON);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.drawString(settingsBtn.label, settingsBtn.x + settingsBtn.w/2, settingsBtn.y + settingsBtn.h/2);
    
    // Draw battery status
    drawBatteryStatus();
}

void toggleMonitoring() {
    state.isMonitoring = !state.isMonitoring;
    
    // Play sound effect when toggling monitoring
    if (settings.soundEnabled) {
        if (state.isMonitoring) {
            // Play ascending tones when turning on
            CoreS3.Speaker.tone(880, 100);  // A5
            delay(50);
            CoreS3.Speaker.tone(1047, 100); // C6
            delay(50);
            CoreS3.Speaker.tone(1319, 150); // E6
        } else {
            // Play descending tones when turning off
            CoreS3.Speaker.tone(1319, 100); // E6
            delay(50);
            CoreS3.Speaker.tone(1047, 100); // C6
            delay(50);
            CoreS3.Speaker.tone(880, 150);  // A5
        }
    }
    
    // Visual feedback animation
    Button monitorBtn = {MARGIN, CoreS3.Display.height() - 60, 100, 40, "Monitor", false, true};
    if (state.isMonitoring) {
        // Quick flash animation when turning on
        drawButton(monitorBtn, DisplayConfig::COLOR_GOOD);
        delay(100);
    } else {
        // Quick flash animation when turning off
        drawButton(monitorBtn, DisplayConfig::COLOR_DISABLED);
        delay(100);
    }
    
    drawInterface();
}

void loop() {
    unsigned long currentMillis = millis();
    
    // Update battery status periodically
    if (currentMillis - state.lastBatteryUpdate >= TimeConfig::BATTERY_UPDATE_INTERVAL) {
        drawBatteryStatus();
        state.lastBatteryUpdate = currentMillis;
    }
    
    // Handle touch input
    CoreS3.update();
    if (CoreS3.Touch.getCount()) {
        auto t = CoreS3.Touch.getDetail();
        if (t.wasPressed()) {
            state.lastActivityTime = currentMillis;
            
            if (state.inSettingsMenu) {
                handleSettingsMenu();
            } else {
                if (touchInButton(monitorBtn, t.x, t.y)) {
                    toggleMonitoring();
                } else if (touchInButton(settingsBtn, t.x, t.y)) {
                    enterSettingsMenu();
                }
            }
        }
    }
    
    // Update temperature display if monitoring
    if (state.isMonitoring && (currentMillis - state.lastTempUpdate >= TimeConfig::TEMP_UPDATE_INTERVAL)) {
        int16_t rawTemp = ncir2.getTempValue();
        state.currentTemp = filterAndCalibrateTemp(rawTemp);
        updateTemperatureDisplay(state.currentTemp);
        updateTemperatureTrend();
        handleTemperatureAlerts();
        state.lastTempUpdate = currentMillis;
        
        // Debug output for temperature calibration in Fahrenheit
        float rawTempF = (rawTemp / 100.0f * 9.0f / 5.0f) + 32.0f;
        float calibratedTempF = (state.currentTemp / 100.0f * 9.0f / 5.0f) + 32.0f;
        Serial.print("Raw Temp: ");
        Serial.print(rawTempF, 1);
        Serial.print("°F, Calibrated: ");
        Serial.print(calibratedTempF, 1);
        Serial.println("°F");
    }
    
    // Debug info update
    if (currentMillis - state.lastDebugUpdate >= TimeConfig::DEBUG_UPDATE_INTERVAL) {
        printDebugInfo();
        state.lastDebugUpdate = currentMillis;
    }
}

void enterSettingsMenu() {
    state.isMonitoring = false;  // Stop monitoring while in settings
    state.inSettingsMenu = true;
    state.settingsScrollPosition = 0;
    state.selectedMenuItem = -1;
    drawSettingsMenu();
}

void updateTemperatureTrend() {
    // Update temperature trend
    state.tempHistory[state.historyIndex] = state.currentTemp;
    state.historyIndex = (state.historyIndex + 1) % 5;
}

void checkBatteryWarning(unsigned long currentMillis) {
    if (currentMillis - state.lastBatteryUpdate >= TimeConfig::BATTERY_CHECK) {
        int bat_level = CoreS3.Power.getBatteryLevel();
        if (bat_level <= 5 && !state.lowBatteryWarningShown) {
            showLowBatteryWarning(bat_level);
            state.lowBatteryWarningShown = true;
        } else if (bat_level > 10) {
            state.lowBatteryWarningShown = false;
        }
        state.lastBatteryUpdate = currentMillis;
    }
}

void showLowBatteryWarning(int batteryLevel) {
    // Show low battery warning
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_WARNING);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Low Battery", CoreS3.Display.width()/2, CoreS3.Display.height()/2 - 20);
    char batStr[16];
    sprintf(batStr, "%d%%", batteryLevel);
    CoreS3.Display.drawString(batStr, CoreS3.Display.width()/2, CoreS3.Display.height()/2 + 20);
    delay(2000);
}

void handleTemperatureAlerts() {
    // Handle temperature alerts
    int displayTemp;
    displayTemp = state.currentTemp / 100;
    
    if (displayTemp < TempConfig::MIN_C || displayTemp > TempConfig::MAX_C) {
        updateStatusDisplay("Temperature Out of Range", DisplayConfig::COLOR_WARNING);
    } else if (abs(displayTemp - TempConfig::TARGET_C) > TempConfig::TOLERANCE_C) {
        updateStatusDisplay("Temperature Not at Target", DisplayConfig::COLOR_WARNING);
    } else {
        updateStatusDisplay("Temperature Stable", DisplayConfig::COLOR_GOOD);
    }
}

void updateStatusDisplay(const char* status, uint32_t color) {
    if (state.inSettingsMenu) return;  // Don't update status in settings menu
    
    int buttonY = CoreS3.Display.height() - BUTTON_HEIGHT - MARGIN;
    int statusBoxY = buttonY - STATUS_BOX_HEIGHT - MARGIN;
    
    // Clear status area with proper margins
    CoreS3.Display.fillRect(MARGIN + 5, statusBoxY + 5, 
                          CoreS3.Display.width() - (MARGIN * 2) - 10, 
                          STATUS_BOX_HEIGHT - 10, 
                          DisplayConfig::COLOR_BACKGROUND);
    
    // Draw new status with size based on length
    CoreS3.Display.setTextDatum(middle_center);
    int textSize = strlen(status) > 20 ? 2 : 3;  // Smaller text for longer messages
    CoreS3.Display.setTextSize(textSize);
    CoreS3.Display.setTextColor(color);
    CoreS3.Display.drawString(status, CoreS3.Display.width()/2, 
                            statusBoxY + (STATUS_BOX_HEIGHT/2));
    
    state.lastStatus = status;
}

void showUpdateScreen() {
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Updating...", CoreS3.Display.width()/2, CoreS3.Display.height()/2 - 40);
    
    // Progress bar
    int barWidth = CoreS3.Display.width() - (MARGIN * 4);
    int barHeight = 20;
    int barX = MARGIN * 2;
    int barY = CoreS3.Display.height()/2;
    
    // Draw progress bar outline
    CoreS3.Display.drawRect(barX, barY, barWidth, barHeight, DisplayConfig::COLOR_TEXT);
    
    // Animate progress bar
    for (int i = 0; i <= barWidth - 4; i++) {
        CoreS3.Display.fillRect(barX + 2, barY + 2, i, barHeight - 4, DisplayConfig::COLOR_GOOD);
        if (settings.soundEnabled && (i % 5 == 0)) {  // Play sound every 5 pixels
            int freq = map(i, 0, barWidth - 4, 500, 2000);  // Increasing frequency
            CoreS3.Speaker.tone(freq, 10);
        }
        delay(10);
    }
    
    if (settings.soundEnabled) {
        CoreS3.Speaker.tone(2000, 100);  // Completion sound
    }
    delay(500);
}

void showMessage(const char* title, const char* message, uint32_t color) {
    // Clear screen
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(color);
    
    // Show message
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString(title, CoreS3.Display.width()/2, CoreS3.Display.height()/2 - 20);
    CoreS3.Display.drawString(message, CoreS3.Display.width()/2, CoreS3.Display.height()/2 + 20);
    
    // Wait for touch
    delay(2000);
}

void showRestartConfirmation(float oldEmissivity, float newEmissivity) {
    // Clear screen
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    
    // Show restart confirmation
    CoreS3.Display.setTextDatum(top_center);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Emissivity Changed", CoreS3.Display.width()/2, 20);
    
    // Draw old and new emissivity values
    char oldEmisStr[16];
    char newEmisStr[16];
    sprintf(oldEmisStr, "%.2f", oldEmissivity);
    sprintf(newEmisStr, "%.2f", newEmissivity);
    CoreS3.Display.drawString("Old: " + String(oldEmisStr), CoreS3.Display.width()/2, 60);
    CoreS3.Display.drawString("New: " + String(newEmisStr), CoreS3.Display.width()/2, 100);
    
    // Draw restart button
    int buttonWidth = 150;
    int buttonHeight = 40;
    int buttonX = (CoreS3.Display.width() - buttonWidth) / 2;
    int buttonY = CoreS3.Display.height() - buttonHeight - 20;
    
    CoreS3.Display.fillRoundRect(buttonX, buttonY, buttonWidth, buttonHeight, 5, DisplayConfig::COLOR_BUTTON);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.drawString("Restart", buttonX + buttonWidth/2, buttonY + buttonHeight/2);
    
    // Wait for touch to restart
    while (!CoreS3.Touch.getDetail().wasPressed()) {
        delay(10);
    }
    
    // Restart the device
    ESP.restart();
}

void adjustEmissivity() {
    const float EMISSIVITY_MIN = 0.65f;
    const float EMISSIVITY_MAX = 1.00f;
    const float EMISSIVITY_STEP = 0.01f;
    
    float tempEmissivity = settings.emissivity;
    bool adjusting = true;
    
    // Save current state
    bool wasInSettingsMenu = state.inSettingsMenu;
    
    while (adjusting) {
        // Draw adjustment screen
        CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
        CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.setTextDatum(top_center);
        CoreS3.Display.drawString("Adjust Emissivity", CoreS3.Display.width()/2, MARGIN);
        
        // Create buttons
        int screenWidth = CoreS3.Display.width();
        int screenHeight = CoreS3.Display.height();
        Button upBtn = {screenWidth - 90, 60, 70, 50, "+", false, true};
        Button downBtn = {screenWidth - 90, 120, 70, 50, "-", false, true};
        Button doneBtn = {MARGIN, screenHeight - 60, 100, 40, "Done", false, true};
        
        // Display current value
        char emisStr[16];
        sprintf(emisStr, "%.2f", tempEmissivity);
        CoreS3.Display.setTextSize(3);
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.drawString(emisStr, (screenWidth - 90)/2, 115);
        
        // Draw buttons
        drawButton(upBtn, DisplayConfig::COLOR_BUTTON);
        drawButton(downBtn, DisplayConfig::COLOR_BUTTON);
        drawButton(doneBtn, DisplayConfig::COLOR_BUTTON);
        
        CoreS3.update();
        if (CoreS3.Touch.getCount()) {
            auto touched = CoreS3.Touch.getDetail();
            if (touched.wasPressed()) {
                if (touchInButton(upBtn, touched.x, touched.y)) {
                    if (tempEmissivity < EMISSIVITY_MAX) {
                        tempEmissivity += EMISSIVITY_STEP;
                        if (tempEmissivity > EMISSIVITY_MAX) tempEmissivity = EMISSIVITY_MAX;
                        playTouchSound(true);
                    }
                } else if (touchInButton(downBtn, touched.x, touched.y)) {
                    if (tempEmissivity > EMISSIVITY_MIN) {
                        tempEmissivity -= EMISSIVITY_STEP;
                        if (tempEmissivity < EMISSIVITY_MIN) tempEmissivity = EMISSIVITY_MIN;
                        playTouchSound(true);
                    }
                } else if (touchInButton(doneBtn, touched.x, touched.y)) {
                    playTouchSound(true);
                    adjusting = false;
                }
                delay(50);  // Debounce
            }
        }
        delay(10);
    }
    
    // Restore previous state
    state.inSettingsMenu = wasInSettingsMenu;
    
    // Update emissivity if changed
    if (tempEmissivity != settings.emissivity) {
        settings.emissivity = tempEmissivity;
        saveSettings();
    }
}

void toggleDisplayMode() {
    // Removed
}

void handleTouchInput() {
    static unsigned long lastTouch = 0;
    unsigned long currentTime = millis();
    
    if (currentTime - lastTouch < TimeConfig::TOUCH_DEBOUNCE) {
        return;
    }
    
    auto event = touchHandler.update();
    switch (event.type) {
        case TouchHandler::TouchEvent::Type::TAP:
            if (state.inSettingsMenu) {
                handleSettingsMenu();
            } else {
                if (touchInButton(monitorBtn, event.x, event.y)) {
                    toggleMonitoring();
                    playTouchSound(true);
                } else if (touchInButton(settingsBtn, event.x, event.y)) {
                    enterSettingsMenu();
                    playTouchSound(true);
                }
            }
            break;
            
        case TouchHandler::TouchEvent::Type::DOUBLE_TAP:
            if (!state.inSettingsMenu) {
                toggleDisplayMode();
                playTouchSound(true);
            }
            break;
            
        case TouchHandler::TouchEvent::Type::DRAGGING:
            if (state.inSettingsMenu) {
                // Handle settings menu scrolling
                static int scrollOffset = 0;
                scrollOffset += event.deltaY;
                // Update menu position based on scroll offset
                // ... (implement menu scrolling logic)
            }
            break;
    }
    
    lastTouch = currentTime;
}

void drawSettingsMenu() {
    const int ITEM_HEIGHT = 40;
    const int PADDING = 10;
    const int TOGGLE_WIDTH = 60;
    const int yStart = HEADER_HEIGHT + PADDING;
    
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.setTextDatum(top_left);
    
    // Draw header
    CoreS3.Display.drawString("Settings", PADDING, PADDING);
    
    int yPos = HEADER_HEIGHT + PADDING;
    int index = 0;
    
    // Temperature unit toggle
    CoreS3.Display.drawString("Temperature Unit", PADDING, yPos);
    drawToggleSwitch(CoreS3.Display.width() - PADDING - TOGGLE_WIDTH, yPos, settings.useCelsius);
    if (state.selectedMenuItem == index) {
        CoreS3.Display.drawRect(PADDING - 5, yPos - 5, 
            CoreS3.Display.width() - 2 * PADDING + 10, ITEM_HEIGHT + 10, 
            DisplayConfig::COLOR_HIGHLIGHT);
    }
    yPos += ITEM_HEIGHT + PADDING;
    index++;
    
    // Sound toggle
    CoreS3.Display.drawString("Sound", PADDING, yPos);
    drawToggleSwitch(CoreS3.Display.width() - PADDING - TOGGLE_WIDTH, yPos, settings.soundEnabled);
    if (state.selectedMenuItem == index) {
        CoreS3.Display.drawRect(PADDING - 5, yPos - 5, 
            CoreS3.Display.width() - 2 * PADDING + 10, ITEM_HEIGHT + 10, 
            DisplayConfig::COLOR_HIGHLIGHT);
    }
    yPos += ITEM_HEIGHT + PADDING;
    index++;
    
    // Brightness control
    CoreS3.Display.drawString("Brightness", PADDING, yPos);
    drawBrightnessButtons(CoreS3.Display.width() - PADDING - 150, yPos, settings.brightness);
    if (state.selectedMenuItem == index) {
        CoreS3.Display.drawRect(PADDING - 5, yPos - 5, 
            CoreS3.Display.width() - 2 * PADDING + 10, ITEM_HEIGHT + 10, 
            DisplayConfig::COLOR_HIGHLIGHT);
    }
    yPos += ITEM_HEIGHT + PADDING;
    index++;
    
    // Auto sleep toggle
    CoreS3.Display.drawString("Auto Sleep", PADDING, yPos);
    drawToggleSwitch(CoreS3.Display.width() - PADDING - TOGGLE_WIDTH, yPos, settings.autoSleep);
    if (state.selectedMenuItem == index) {
        CoreS3.Display.drawRect(PADDING - 5, yPos - 5, 
            CoreS3.Display.width() - 2 * PADDING + 10, ITEM_HEIGHT + 10, 
            DisplayConfig::COLOR_HIGHLIGHT);
    }
    yPos += ITEM_HEIGHT + PADDING;
    index++;
    
    // Emissivity setting
    char emissStr[32];
    sprintf(emissStr, "Emissivity: %.2f", settings.emissivity);
    CoreS3.Display.drawString(emissStr, PADDING, yPos);
    if (state.selectedMenuItem == index) {
        CoreS3.Display.drawRect(PADDING - 5, yPos - 5, 
            CoreS3.Display.width() - 2 * PADDING + 10, ITEM_HEIGHT + 10, 
            DisplayConfig::COLOR_HIGHLIGHT);
    }
    yPos += ITEM_HEIGHT + PADDING;
    index++;
    
    // Back button at the bottom
    Button backBtn = {PADDING, CoreS3.Display.height() - 60, 100, 40, "Back", false, true};
    drawButton(backBtn, DisplayConfig::COLOR_TEXT);
}

void handleSettingsMenu() {
    auto t = CoreS3.Touch.getDetail();
    if (!t.wasPressed()) return;  // Exit if no touch detected
    
    state.lastActivityTime = millis();
    
    // Calculate touch areas
    int itemHeight = 50;
    int startY = HEADER_HEIGHT + MARGIN;
    int visibleHeight = CoreS3.Display.height() - startY - MARGIN;
    int maxScroll = max(0, (MenuConfig::MENU_ITEMS * itemHeight) - visibleHeight);
    state.settingsScrollPosition = min(state.settingsScrollPosition, maxScroll);  // Add bounds check
    
    // Handle scrolling
    if (t.y < startY && state.settingsScrollPosition > 0) {
        playTouchSound(true);
        state.settingsScrollPosition = max(0, state.settingsScrollPosition - itemHeight);
        drawSettingsMenu();
        return;
    } else if (t.y > CoreS3.Display.height() - MARGIN && state.settingsScrollPosition < maxScroll) {
        playTouchSound(true);
        state.settingsScrollPosition = min(maxScroll, state.settingsScrollPosition + itemHeight);
        drawSettingsMenu();
        return;
    }
    
    // Calculate which item was touched
    int touchedItem = (t.y - startY + state.settingsScrollPosition) / itemHeight;
    if (touchedItem >= 0 && touchedItem < MenuConfig::MENU_ITEMS) {
        state.selectedMenuItem = touchedItem;
        
        // Handle brightness buttons
        if (touchedItem == 2) {  // Brightness
            int btnWidth = 60;
            int spacing = 10;
            int startX = CoreS3.Display.width() - 290;
            int btnIndex = (t.x - startX) / (btnWidth + spacing);
            
            if (btnIndex >= 0 && btnIndex < 4) {
                const int levels[4] = {64, 128, 192, 255};  // 25%, 50%, 75%, 100%
                settings.brightness = levels[btnIndex];
                CoreS3.Display.setBrightness(settings.brightness);
                playTouchSound(true);
                saveSettings();
                drawSettingsMenu();
                return;
            }
        }
        
        // Handle other menu items
        switch (touchedItem) {
            case 0:  // Temperature Unit
                settings.useCelsius = !settings.useCelsius;
                playTouchSound(true);
                saveSettings();
                break;
                
            case 1:  // Sound
                settings.soundEnabled = !settings.soundEnabled;
                playTouchSound(true);
                saveSettings();
                break;
                
            case 3:  // Auto Sleep
                settings.autoSleep = !settings.autoSleep;
                playTouchSound(true);
                saveSettings();
                break;
                
            case 4: {  // Emissivity
                playTouchSound(true);
                float oldEmissivity = settings.emissivity;
                adjustEmissivity();
                if (oldEmissivity != settings.emissivity) {
                    saveSettings();
                    showRestartConfirmation(oldEmissivity, settings.emissivity);
                }
                break;
            }
                
            case 5:  // Exit
                playTouchSound(true);
                exitSettingsMenu();
                return;
        }
        drawSettingsMenu();
    }
}

void exitSettingsMenu() {
    state.inSettingsMenu = false;
    state.selectedMenuItem = -1;
    state.settingsScrollPosition = 0;
    drawInterface();
}

void drawToggleSwitch(int x, int y, bool state) {
    const int width = 50;
    const int height = 24;
    const int knobSize = height - 4;
    
    // Draw switch background with gradient effect
    uint32_t baseColor = state ? DisplayConfig::COLOR_GOOD : DisplayConfig::COLOR_DISABLED;
    CoreS3.Display.fillRoundRect(x, y, width, height, height/2, baseColor);
    
    // Add highlight for 3D effect
    uint32_t highlightColor = state ? baseColor + 0x303030 : baseColor + 0x202020;
    CoreS3.Display.drawFastHLine(x + 2, y + 2, width - 4, highlightColor);
    
    // Draw knob with retro effect
    int knobX = state ? x + width - knobSize - 2 : x + 2;
    
    // Knob shadow
    CoreS3.Display.fillCircle(knobX + knobSize/2 + 1, y + height/2 + 1, knobSize/2, 
                             DisplayConfig::COLOR_BACKGROUND);
    
    // Main knob
    CoreS3.Display.fillCircle(knobX + knobSize/2, y + height/2, knobSize/2, 
                             DisplayConfig::COLOR_TEXT);
    
    // Knob highlight
    CoreS3.Display.drawCircle(knobX + knobSize/2, y + height/2, knobSize/2 - 2,
                             state ? DisplayConfig::COLOR_GOOD + 0x404040 : 
                                    DisplayConfig::COLOR_DISABLED + 0x404040);
}

// Function to filter and calibrate temperature readings
int16_t filterAndCalibrateTemp(int16_t rawTemp) {
    static const int MOVING_AVG_SIZE = 5;
    static int16_t readings[MOVING_AVG_SIZE];
    static int readIndex = 0;
    static float total = 0;
    
    // Remove the oldest reading
    total = total - readings[readIndex];
    // Add the new reading
    readings[readIndex] = rawTemp;
    total = total + readings[readIndex];
    readIndex = (readIndex + 1) % MOVING_AVG_SIZE;
    
    // Calculate average
    float avgTemp = total / MOVING_AVG_SIZE;
    
    // Apply calibration for high temperature range (around 500°F)
    // These values may need adjustment based on actual calibration measurements
    if (avgTemp > 45000) { // Above 450°F (in centidegrees)
        avgTemp = avgTemp * 1.05; // Adjust by 5% for high temperature compensation
    }
    
    return (int16_t)avgTemp;
}

// Temperature conversion functions
int16_t celsiusToFahrenheit(int16_t celsius) {
    // Input is raw temperature * 100
    // First convert to actual Celsius, then to Fahrenheit, then back to fixed point
    float actualCelsius = celsius / 100.0f;
    float fahrenheit = (actualCelsius * 9.0f / 5.0f) + 32.0f;
    return (int16_t)(fahrenheit * 100.0f);
}

int16_t fahrenheitToCelsius(int16_t fahrenheit) {
    // Input is raw temperature * 100
    // First convert to actual Fahrenheit, then to Celsius, then back to fixed point
    float actualFahrenheit = fahrenheit / 100.0f;
    float celsius = (actualFahrenheit - 32.0f) * 5.0f / 9.0f;
    return (int16_t)(celsius * 100.0f);
}

// Button interaction helper
bool touchInButton(Button &btn, int32_t touch_x, int32_t touch_y) {
    return (touch_x >= btn.x && touch_x <= btn.x + btn.w &&
            touch_y >= btn.y && touch_y <= btn.y + btn.h &&
            btn.enabled);
}

// Battery status display
void drawBatteryStatus() {
    int batteryLevel = CoreS3.Power.getBatteryLevel();
    bool isCharging = CoreS3.Power.isCharging();
    
    // Battery icon dimensions
    const int battX = CoreS3.Display.width() - 40;
    const int battY = 5;
    const int battW = 30;
    const int battH = 15;
    const int battTip = 3;
    
    // Draw battery outline
    CoreS3.Display.drawRect(battX, battY, battW, battH, DisplayConfig::COLOR_TEXT);
    CoreS3.Display.fillRect(battX + battW, battY + (battH - battTip) / 2, 2, battTip, DisplayConfig::COLOR_TEXT);
    
    // Calculate fill width based on battery level
    int fillWidth = (battW - 4) * batteryLevel / 100;
    
    // Choose color based on battery level and charging status
    uint32_t fillColor;
    if (isCharging) {
        fillColor = DisplayConfig::COLOR_CHARGING;
    } else if (batteryLevel > 60) {
        fillColor = DisplayConfig::COLOR_BATTERY_GOOD;
    } else if (batteryLevel > 20) {
        fillColor = DisplayConfig::COLOR_BATTERY_MED;
    } else {
        fillColor = DisplayConfig::COLOR_BATTERY_LOW;
    }
    
    // Fill battery icon
    if (fillWidth > 0) {
        CoreS3.Display.fillRect(battX + 2, battY + 2, fillWidth, battH - 4, fillColor);
    }
    
    // Draw percentage text
    char battText[5];
    sprintf(battText, "%d%%", batteryLevel);
    CoreS3.Display.setTextDatum(middle_right);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.drawString(battText, battX - 5, battY + battH/2);
    
    // Draw charging indicator if applicable
    if (isCharging) {
        CoreS3.Display.fillTriangle(
            battX - 15, battY + battH/2,
            battX - 10, battY + 2,
            battX - 5, battY + battH/2,
            DisplayConfig::COLOR_CHARGING
        );
    }
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    
    // Initialize M5 device
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    CoreS3.Display.setRotation(1);
    CoreS3.Display.setBrightness(settings.brightness);
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    
    // Initialize preferences
    preferences.begin("terp-meter", false);
    
    // Load settings first
    loadSettings();
    
    // Initialize I2C and NCIR sensor
    Wire.begin(2, 1);
    ncir2.begin();
    ncir2.setLEDColor(0);
    
    // Show initialization animation
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.drawString("Initializing", CoreS3.Display.width()/2, 40);
    
    // Progress bar parameters
    const int barWidth = 280;
    const int barHeight = 20;
    const int barX = (CoreS3.Display.width() - barWidth) / 2;
    const int barY = 100;
    const int segments = 10;
    const int segmentWidth = barWidth / segments;
    const int animationDelay = 100;
    
    // Draw progress bar outline
    CoreS3.Display.drawRect(barX - 2, barY - 2, barWidth + 4, barHeight + 4, DisplayConfig::COLOR_TEXT);
    
    // Animated progress bar with sliding effect
    for (int i = 0; i < segments; i++) {
        // Slide effect
        for (int j = 0; j <= segmentWidth; j++) {
            int x = barX + (i * segmentWidth);
            CoreS3.Display.fillRect(x, barY, j, barHeight, DisplayConfig::COLOR_GOOD);
            delay(5);
        }
        
        // Play a tone that increases in pitch
        if (settings.soundEnabled && (i % 5 == 0)) {  // Play sound every 5 pixels
            int frequency = 500 + (i * 100);  // Frequency increases with each segment
            CoreS3.Speaker.tone(frequency, 10);
        }
    }
    
    // Victory sound sequence
    if (settings.soundEnabled) {
        CoreS3.Speaker.tone(2000, 100);  // Completion sound
    }
    delay(500);
    
    state.isMonitoring = false;
    
    // Initialize temperature unit selection
    settings.useCelsius = selectTemperatureUnit();
    preferences.putBool("useCelsius", settings.useCelsius);  // Save the selection immediately
    
    // Initialize display interface
    drawInterface();
    ncir2.setLEDColor(0);
    drawBatteryStatus();
}