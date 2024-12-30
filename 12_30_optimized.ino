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

// Button struct definition
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
            auto [x, y] = CoreS3.Touch.getDetail();
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

void checkForErrors() {
    // Check sensor connection
    if (!ncir2.isConnected()) {
        ErrorHandling::setError(ErrorHandling::ErrorCode::SENSOR_INIT_FAILED, "Temperature sensor not connected");
        return;
    }
    
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

// ... Rest of your existing functions (drawButton, updateDisplay, etc.) ...
