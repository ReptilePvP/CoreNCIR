// 1. Include statements
#include <Wire.h>
#include <M5GFX.h>
#include <M5CoreS3.h>
#include <M5Unified.h>
#include <M5UNIT_NCIR2.h>
#include <time.h>
#include <sys/time.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

// 2. Object initialization
M5UNIT_NCIR2 ncir2;
Preferences preferences;


namespace TempConfig {
    const float MIN_C = 0.0;      // Minimum valid temperature in Celsius
    const float MAX_C = 300.0;    // Maximum valid temperature in Celsius
    const float TARGET_C = 180.0; // Target temperature in Celsius
    const float TOLERANCE_C = 5.0; // Temperature tolerance range in Celsius
}
// Temperature and timing configuration
namespace TimeConfig {
    const unsigned long TEMP_UPDATE_INTERVAL = 100;    // Temperature update every 100ms
    const unsigned long HISTORY_UPDATE_INTERVAL = 1000; // History update every second
    const unsigned long MENU_UPDATE_INTERVAL = 100;    // Menu update every 100ms
    const unsigned long IDLE_UPDATE_INTERVAL = 1000;   // Idle screen update every second
    const unsigned long POWER_CHECK_INTERVAL = 5000;   // Power check every 5 seconds
    const unsigned long STATUS_MESSAGE_DURATION = 3000; // Status messages last 3 seconds
    const unsigned long BATTERY_CHECK = 5000;          // Battery check interval
}


// Display configuration
namespace DisplayConfig {
    // Retro-modern color scheme
    static const uint32_t COLOR_BACKGROUND = TFT_BLACK;  // Change from 0x000B1E
    static const uint32_t COLOR_TEXT = TFT_WHITE;        // Change from 0x00FF9C
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
const int TOUCH_THRESHOLD = 40;      // Minimum movement to register as a touch

// Speaker constants
const int BEEP_FREQUENCY = 1000;  // 1kHz tone
const int BEEP_DURATION = 100;    // 100ms beep

// Layout constants - adjusted TEMP_BOX_HEIGHT
const int HEADER_HEIGHT = 30;
const int TEMP_BOX_HEIGHT = 70;  // Reduced from 90 to 70
const int STATUS_BOX_HEIGHT = 60;
const int MARGIN = 10;
const int BUTTON_HEIGHT = 50;
const int BUTTON_SPACING = 10;

// Emissivity constants and variables
const float EMISSIVITY_MIN = 0.1;
const float EMISSIVITY_MAX = 1.0;
const float EMISSIVITY_STEP = 0.05;

// Menu configuration
namespace MenuConfig {
    const int MENU_ITEMS = 4;  // Now 4 items instead of 5
    const int ITEM_HEIGHT = 50; // Height of each menu item
    const int VISIBLE_ITEMS = 4; // How many items visible at once
    const int SCROLL_MARGIN = 10;
    
    const char* MENU_LABELS[MENU_ITEMS] = {
        "Temperature Unit",
        "Sound",
        "Brightness",
        "Emissivity"
    };
}


// Device settings
struct Settings {
    bool useCelsius = true;
    bool soundEnabled = true;
    int brightness = 128;  
    float emissivity = 0.87;  // Adjusted for quartz glass
    
    void load(Preferences& prefs) {
        useCelsius = prefs.getBool("useCelsius", true);
        soundEnabled = prefs.getBool("soundEnabled", true);
        brightness = prefs.getInt("brightness", 128);  // Default to 128 (50% brightness)
        brightness = constrain(brightness, 0, 255);    // Ensure valid range
        emissivity = prefs.getFloat("emissivity", 0.87);  // Default adjusted for quartz glass
    }
    
    void save(Preferences& prefs) {
        prefs.putBool("useCelsius", useCelsius);
        prefs.putBool("soundEnabled", soundEnabled);
        prefs.putInt("brightness", brightness);
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
    bool isRecovering = false;  // Flag for recovery from black screen
    unsigned long lastStateChange = 0;  // Track when states change
    bool isProcessingSettings = false;  // New flag to track settings processing
};

// Update your Button struct definition
struct Button {
    int x;
    int y;
    int width;    // Using width instead of w
    int height;   // Using height instead of h
    const char* label;
    bool pressed;
    bool enabled;
};

// Function declarations
void drawButton(Button &btn, uint32_t color);
void drawInterface();
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
void toggleMonitoring();
void updateTemperatureTrend();
void checkBatteryWarning(unsigned long currentMillis);
void drawSettingsMenu();
void handleSettingsMenu();
void saveSettings();
void loadSettings();
void enterSleepMode();
void checkForErrors();
void wakeUp();
void drawToggleSwitch(int x, int y, bool state);
void showRestartConfirmation(float oldEmissivity, float newEmissivity);
void exitSettingsMenu();
void playTouchSound(bool success = true);
void drawBrightnessButtons(int x, int y, int currentBrightness);
bool isSettingsMenuVisible();
void debugLog(const char* function, const char* message);
void printMemoryInfo();
bool isLowMemory();
void checkStack();
void drawScrollIndicator(bool isUpArrow, int y);
bool isButtonPressed(const Button& btn, int touchX, int touchY);
// Function declarations (add this with your other declarations)
void showStartupAnimation();
void debugLogf(const char* function, const char* format, ...);
// Button declarations
Button monitorBtn = {0, 0, 0, 0, "Monitor", false, true};
Button settingsBtn = {0, 0, 0, 0, "Settings", false, true};  // Changed from emissivityBtn
static LGFX_Sprite* tempSprite = nullptr;

Settings settings;
DeviceState state;

void debugLogf(const char* function, const char* format, ...) {
    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    debugLog(function, buffer);
}
void showStartupAnimation() {
    esp_task_wdt_reset();
    
    // Clear screen and set initial text properties
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    
    // Draw title
    CoreS3.Display.drawString("TerpMeter", CoreS3.Display.width()/2, 40);
    
   // Progress bar parameters
    const int barWidth = 200;
    const int barHeight = 20;
    const int barX = (CoreS3.Display.width() - barWidth) / 2;
    const int barY = 100;
    
    // Draw progress bar outline
    CoreS3.Display.drawRect(barX - 2, barY - 2, barWidth + 4, barHeight + 4, DisplayConfig::COLOR_TEXT);
    
    // Animate progress bar
    for (int i = 0; i < barWidth; i += 4) {
        CoreS3.Display.fillRect(barX, barY, i, barHeight, DisplayConfig::COLOR_GOOD);
        
        // Reset watchdog every 20 pixels
        if (i % 20 == 0) {
            esp_task_wdt_reset();
        }
        
        // Optional: Play startup sound
        if (settings.soundEnabled && i % 40 == 0) {
            CoreS3.Speaker.tone(500 + i, 50);
        }
        
        delay(10);
    }
    
    // Victory sound
    if (settings.soundEnabled) {
        CoreS3.Speaker.tone(1000, 100);
        delay(100);
        CoreS3.Speaker.tone(1500, 100);
    }
    
    delay(500);
    esp_task_wdt_reset();
}
bool isButtonPressed(const Button& btn, int touchX, int touchY) {
    return (touchX >= btn.x && touchX <= (btn.x + btn.width) &&
            touchY >= btn.y && touchY <= (btn.y + btn.height));
}
void drawScrollIndicator(bool isUpArrow, int y) {
    const int arrowWidth = 20;
    const int arrowHeight = 10;
    const int x = CoreS3.Display.width() / 2 - arrowWidth / 2;
    
    // Set color for the arrow
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    
    if (isUpArrow) {
        // Draw up arrow
        for (int i = 0; i < arrowHeight; i++) {
            CoreS3.Display.drawLine(x + i, y + arrowHeight - i, 
                                  x + arrowWidth - i, y + arrowHeight - i, 
                                  DisplayConfig::COLOR_TEXT);
        }
    } else {
        // Draw down arrow
        for (int i = 0; i < arrowHeight; i++) {
            CoreS3.Display.drawLine(x + i, y + i, 
                                  x + arrowWidth - i, y + i, 
                                  DisplayConfig::COLOR_TEXT);
        }
    }
}
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
void checkStack() {
    char stack;
    debugLog("Stack", String("Stack pointer: " + String((uint32_t)&stack, HEX)).c_str());
}

bool isLowMemory() {
    return ESP.getFreeHeap() < 10000; // Adjust threshold as needed
}
void printMemoryInfo() {
    debugLogf("Memory", "Free Heap: %d bytes", ESP.getFreeHeap());
    debugLogf("Memory", "Largest Free Block: %d bytes", ESP.getMaxAllocHeap());
}
// Debug logging utility functions
void debugLog(const char* function, const char* message) {
    Serial.printf("[DEBUG] %s: %s\n", function, message);
    Serial.flush(); // Ensure the message is sent before any potential crash
}
void drawButton(Button &btn, uint32_t color) {
    // Draw button background
    CoreS3.Display.fillRoundRect(btn.x, btn.y, 
                               btn.width, btn.height, 
                               8, color);
    
    // Add highlight effect at the top
    uint32_t highlightColor = (color == DisplayConfig::COLOR_BUTTON_ACTIVE) ? 
                             DisplayConfig::COLOR_HIGHLIGHT : 
                             DisplayConfig::COLOR_ICE_BLUE;
    
    // Draw highlight gradient (top portion of button)
    for(int i = 0; i < 8; i++) {
        uint8_t alpha = 255 - (i * 32);  // Fade out from top to bottom
        uint32_t currentHighlight = CoreS3.Display.color565(
            ((highlightColor >> 16) & 0xFF) * alpha / 255,
            ((highlightColor >> 8) & 0xFF) * alpha / 255,
            (highlightColor & 0xFF) * alpha / 255
        );
        CoreS3.Display.drawRoundRect(btn.x + 2, btn.y + i, 
                                   btn.width - 4, btn.height - (i * 2), 
                                   8, currentHighlight);
    }
    
    // Draw button border
    CoreS3.Display.drawRoundRect(btn.x, btn.y, 
                               btn.width, btn.height, 
                               8, DisplayConfig::COLOR_TEXT);
    
    // Draw button text
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.drawString(btn.label, 
                            btn.x + (btn.width / 2), 
                            btn.y + (btn.height / 2));
}

void drawBrightnessButtons(int x, int y, int currentBrightness) {
    const int btnWidth = 60;
    const int btnHeight = 40;
    const int spacing = 10;
    const int levels[4] = {64, 128, 192, 255};  // Keep these values as they're already in 0-255 range
    
    for (int i = 0; i < 4; i++) {
        int btnX = x + (btnWidth + spacing) * i;
        bool isSelected = (currentBrightness >= levels[i] - 32 && 
                         currentBrightness <= levels[i] + 32);
        
        // Draw button
        CoreS3.Display.fillRoundRect(btnX, y, btnWidth, btnHeight, 5, 
            isSelected ? DisplayConfig::COLOR_BUTTON_ACTIVE : DisplayConfig::COLOR_BUTTON);
        
        // Draw percentage text
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
        char label[5];
        sprintf(label, "%d%%", (levels[i] * 100) / 255);  // Convert to percentage for display
        CoreS3.Display.drawString(label, btnX + btnWidth/2, y + btnHeight/2);
    }
}

// Modify wakeUp function to properly reset state
void wakeUp() {
    // Reset states
    state.inSettingsMenu = false;
    state.isMonitoring = false;
    
    // Wake up display
    CoreS3.Display.wakeup();
    CoreS3.Display.setBrightness(settings.brightness);  // Already in 0-255 range
    delay(100);  // Increased delay for stability
    
    // Reset activity timer
    state.lastActivityTime = millis();
    
    // Redraw main interface
    drawInterface();
}

void printDebugInfo() {
    Serial.println("=== Debug Info ===");
    Serial.print("Temperature: "); Serial.println(state.currentTemp / 100.0);
    Serial.print("Battery Level: "); Serial.print(CoreS3.Power.getBatteryLevel()); Serial.println("%");
    Serial.print("Charging: "); Serial.println(CoreS3.Power.isCharging() ? "Yes" : "No");
    Serial.print("Monitoring: "); Serial.println(state.isMonitoring ? "Yes" : "No");
    Serial.print("Temperature Unit: "); Serial.println(settings.useCelsius ? "Celsius" : "Fahrenheit");
    Serial.print("Emissivity: "); Serial.println(settings.emissivity);
    Serial.println("================");
}

void checkForErrors() {
    // Add basic error checking
    if (CoreS3.Power.getBatteryLevel() <= 5) {
        showLowBatteryWarning(CoreS3.Power.getBatteryLevel());
    }
    
    // Add sensor communication check
    int16_t testTemp = ncir2.getTempValue();
    if (testTemp == 0 || testTemp < -1000 || testTemp > 10000) {
        // Sensor communication error
        updateStatusDisplay("Sensor Error", DisplayConfig::COLOR_HOT);
    }
}
void enterSleepMode() {
    // Prevent sleep mode during settings operations
    if (state.isProcessingSettings || state.inSettingsMenu) {
        debugLog("enterSleepMode", "Prevented sleep during settings operation");
        state.lastActivityTime = millis();  // Reset activity timer
        return;
    }
    
    debugLog("enterSleepMode", "Entering sleep mode");
    esp_task_wdt_reset();
    
    // Save current state before sleeping
    ncir2.setLEDColor(0);  // Turn off LED
    CoreS3.Display.setBrightness(0);  // Turn off display
    CoreS3.Display.sleep();  // Put display to sleep
    
    esp_task_wdt_reset();
    
    // Wait for touch to wake up
    while (true) {
        esp_task_wdt_reset();  // Keep resetting watchdog while in sleep
        CoreS3.update();
        auto touch = CoreS3.Touch.getDetail();
        
        if (touch.wasPressed()) {
            wakeUp();
            break;
        }
        delay(100);
    }
}
void loadSettings() {
    settings.load(preferences);
    // Ensure brightness is within valid range
    settings.brightness = constrain(settings.brightness, 0, 255);
    CoreS3.Display.setBrightness(settings.brightness); // Now using correct 0-255 range
}

void saveSettings() {
    settings.save(preferences);
}
void handleSettingsMenu() {
    esp_task_wdt_reset();
    debugLog("handleSettingsMenu", "Starting");
    
    auto t = CoreS3.Touch.getDetail();
    if (!t.wasPressed()) return;
    
    // Log touch coordinates
    char touchInfo[64];
    sprintf(touchInfo, "Touch detected at x=%d, y=%d", t.x, t.y);
    debugLog("handleSettingsMenu", touchInfo);
    
    state.lastActivityTime = millis();
    
    // Calculate touch areas with bounds checking
    int itemHeight = MenuConfig::ITEM_HEIGHT;
    int startY = HEADER_HEIGHT + MARGIN;
    int visibleHeight = CoreS3.Display.height() - startY - MARGIN;
    int maxScroll = max(0, (MenuConfig::MENU_ITEMS * itemHeight) - visibleHeight);
    
    // Log scroll position
    char scrollInfo[64];
    sprintf(scrollInfo, "ScrollPos=%d, MaxScroll=%d", state.settingsScrollPosition, maxScroll);
    debugLog("handleSettingsMenu", scrollInfo);
    
    // Ensure scroll position stays within bounds
    state.settingsScrollPosition = constrain(state.settingsScrollPosition, 0, maxScroll);
    
    // Handle header touch (exit settings)
    if (t.y < HEADER_HEIGHT) {
        debugLog("handleSettingsMenu", "Header touched - exiting settings");
        playTouchSound(true);
        exitSettingsMenu();
        return;
    }
    
    try {
        // Handle scroll gestures with wider touch areas
        const int SCROLL_AREA_WIDTH = 40; // Wide touch area for scrolling
        
        // Handle scroll up (left side)
        if (t.x < SCROLL_AREA_WIDTH && t.y > HEADER_HEIGHT) {
            if (state.settingsScrollPosition > 0) {
                debugLog("handleSettingsMenu", "Scrolling up");
                playTouchSound(true);
                state.settingsScrollPosition -= itemHeight;
                state.settingsScrollPosition = max(0, state.settingsScrollPosition);
                drawSettingsMenu();
                return;
            }
        } 
        // Handle scroll down (right side)
        else if (t.x > (CoreS3.Display.width() - SCROLL_AREA_WIDTH) && t.y > HEADER_HEIGHT) {
            if (state.settingsScrollPosition < maxScroll) {
                debugLog("handleSettingsMenu", "Scrolling down");
                playTouchSound(true);
                state.settingsScrollPosition += itemHeight;
                state.settingsScrollPosition = min(maxScroll, state.settingsScrollPosition);
                drawSettingsMenu();
                return;
            }
        }
        
        // Calculate which item was touched
        int touchedItem = (t.y - startY + state.settingsScrollPosition) / itemHeight;
        touchedItem = constrain(touchedItem, -1, MenuConfig::MENU_ITEMS - 1);
        
        char itemInfo[64];
        sprintf(itemInfo, "Touched item index: %d", touchedItem);
        debugLog("handleSettingsMenu", itemInfo);
        
        if (touchedItem >= 0 && touchedItem < MenuConfig::MENU_ITEMS) {
            state.selectedMenuItem = touchedItem;
            
            // Handle brightness control
            if (touchedItem == 2) { // Brightness
                int btnWidth = 60;
                int spacing = 10;
                int startX = CoreS3.Display.width() - 290;
                
                // Calculate which brightness button was pressed
                if (t.x >= startX && t.x <= startX + 4 * (btnWidth + spacing)) {
                    int btnIndex = (t.x - startX) / (btnWidth + spacing);
                    if (btnIndex >= 0 && btnIndex < 4) {
                        const int levels[4] = {64, 128, 192, 255}; // 25%, 50%, 75%, 100%
                        settings.brightness = levels[btnIndex];
                        CoreS3.Display.setBrightness(settings.brightness);
                        playTouchSound(true);
                        saveSettings();
                        drawSettingsMenu();
                        return;
                    }
                }
            }
            
            // Handle toggle switches and other controls
            int rightMarginStart = CoreS3.Display.width() - 90;
            if (t.x > rightMarginStart) {
                switch(touchedItem) {
                    case 0:  // Temperature Unit
                        debugLog("handleSettingsMenu", "Toggling temperature unit");
                        settings.useCelsius = !settings.useCelsius;
                        playTouchSound(true);
                        saveSettings();
                        break;
                        
                    case 1:  // Sound
                        debugLog("handleSettingsMenu", "Toggling sound");
                        settings.soundEnabled = !settings.soundEnabled;
                        playTouchSound(settings.soundEnabled);
                        saveSettings();
                        break;
                        
                    case 3:  // Emissivity
                        debugLog("handleSettingsMenu", "Adjusting emissivity");
                        float oldEmissivity = settings.emissivity;
                        adjustEmissivity();
                        if (oldEmissivity != settings.emissivity) {
                            saveSettings();
                            showRestartConfirmation(oldEmissivity, settings.emissivity);
                        }
                        break;
                }
                drawSettingsMenu();
            }
        }
        
    } catch (const std::exception& e) {
        char errorMsg[128];
        sprintf(errorMsg, "Error in handleSettingsMenu: %s", e.what());
        debugLog("handleSettingsMenu", errorMsg);
        // Attempt to recover from error
        state.inSettingsMenu = false;
        drawInterface();
    }
    
    // Print memory info after processing
    printMemoryInfo();
    
    // Reset watchdog timer before returning
    esp_task_wdt_reset();
    
    // Add small debounce delay
    delay(50);
}
void updateTemperatureDisplay(int16_t temp) {
    esp_task_wdt_reset();
    
    int screenWidth = CoreS3.Display.width();
    int contentWidth = screenWidth - (2 * MARGIN);
    int tempBoxHeight = 80;
    int tempBoxY = HEADER_HEIGHT + MARGIN;

    // Clear temperature display area
    CoreS3.Display.fillRect(MARGIN + 1, tempBoxY + 1, 
                          contentWidth - 2, 
                          tempBoxHeight - 2, 
                          DisplayConfig::COLOR_BACKGROUND);

    // Format temperature as whole number
    char tempStr[8];
    if (settings.useCelsius) {
        sprintf(tempStr, "%dC", temp/100);
    } else {
        // Convert to Fahrenheit while maintaining precision
        int16_t fTemp = (temp * 9 + 250) / 5 + 3200;
        sprintf(tempStr, "%dF", fTemp/100);
    }

    // Display temperature
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.drawString(tempStr, 
                            screenWidth / 2, 
                            tempBoxY + (tempBoxHeight / 2));

    CoreS3.Display.drawRoundRect(MARGIN, tempBoxY, 
                               contentWidth, tempBoxHeight, 
                               8, DisplayConfig::COLOR_TEXT);

    esp_task_wdt_reset();
}
void playTouchSound(bool success) {
    if (!settings.soundEnabled) return;
    
    // Initialize speaker just for this sound
    CoreS3.Speaker.begin();
    CoreS3.Speaker.setVolume(128);
    
    if (success) {
        CoreS3.Speaker.tone(1000, 50);  // 1kHz for 50ms
    } else {
        CoreS3.Speaker.tone(500, 100);  // 500Hz for 100ms
    }
    
    delay(50);  // Wait for sound to finish
    CoreS3.Speaker.end();  // Clean up speaker
}
void drawInterface() {
    esp_task_wdt_reset();
    
    // Calculate dimensions
    int screenWidth = CoreS3.Display.width();
    int screenHeight = CoreS3.Display.height();
    int contentWidth = screenWidth - (2 * MARGIN);
    int buttonY = screenHeight - BUTTON_HEIGHT - MARGIN;
    
    // Draw background
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    
    // Draw header
    CoreS3.Display.fillRect(0, 0, screenWidth, HEADER_HEIGHT, DisplayConfig::COLOR_HEADER);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.drawString("TerpMeter", screenWidth / 2, HEADER_HEIGHT / 2);
    
    // Temperature display box
    int tempBoxHeight = 80;
    int tempBoxY = HEADER_HEIGHT + MARGIN;
    CoreS3.Display.drawRoundRect(MARGIN, tempBoxY, contentWidth, tempBoxHeight, 8, DisplayConfig::COLOR_TEXT);
    
    // Draw current temperature if monitoring
    if (state.isMonitoring) {
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextSize(3);
        char tempStr[10];
        sprintf(tempStr, "%.1f%c", state.lastDisplayTemp, settings.useCelsius ? 'C' : 'F');
        CoreS3.Display.drawString(tempStr, screenWidth / 2, tempBoxY + (tempBoxHeight / 2));
    } else {
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.drawString("Ready", screenWidth / 2, tempBoxY + (tempBoxHeight / 2));
    }
    
    // Status box
    int statusBoxY = buttonY - STATUS_BOX_HEIGHT - MARGIN;
    CoreS3.Display.drawRoundRect(MARGIN, statusBoxY, contentWidth, STATUS_BOX_HEIGHT, 8, DisplayConfig::COLOR_TEXT);
    
    // Draw current emissivity value
    char emisStr[32];
    sprintf(emisStr, "E: %.2f", settings.emissivity);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.setTextDatum(middle_left);
    CoreS3.Display.setTextSize(1);
    CoreS3.Display.drawString(emisStr, MARGIN + 5, MARGIN + 15);
    
    // Draw battery status
    drawBatteryStatus();
    
    // Calculate button dimensions
    int buttonWidth = (contentWidth - BUTTON_SPACING) / 2;
    
    // Update button structures
    monitorBtn = {
        MARGIN,
        buttonY,
        buttonWidth,
        BUTTON_HEIGHT,
        "Monitor",
        false,
        true
    };
    
    settingsBtn = {
        MARGIN + buttonWidth + BUTTON_SPACING,
        buttonY,
        buttonWidth,
        BUTTON_HEIGHT,
        "Settings",
        false,
        true
    };
    
    // Draw buttons
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(2);
    
    // Draw monitor button with active state if monitoring
    uint32_t monitorBtnColor = state.isMonitoring ? 
        DisplayConfig::COLOR_BUTTON_ACTIVE : 
        DisplayConfig::COLOR_BUTTON;
    drawButton(monitorBtn, monitorBtnColor);
    
    // Draw settings button
    drawButton(settingsBtn, DisplayConfig::COLOR_BUTTON);
    
    // Update status display if not monitoring
    if (!state.isMonitoring) {
        updateStatusDisplay("Ready", DisplayConfig::COLOR_TEXT);
    }
    
    esp_task_wdt_reset();
}
bool selectTemperatureUnit() {
    esp_task_wdt_reset();
    Serial.println("Starting temperature unit selection");
    
    // Clear screen and draw header
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), HEADER_HEIGHT, DisplayConfig::COLOR_HEADER);
    
    // Draw title
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.drawString("Select Temperature Unit", CoreS3.Display.width() / 2, HEADER_HEIGHT / 2);
    
    // Calculate button positions
    int buttonWidth = 100;
    int buttonHeight = 100;
    int spacing = 40;
    int startY = HEADER_HEIGHT + 50;
    
    // Create buttons
    Button celsiusBtn = {
        (CoreS3.Display.width() - (2 * buttonWidth + spacing)) / 2,
        startY,
        buttonWidth,
        buttonHeight,
        "C",
        false,
        true
    };
    
    Button fahrenheitBtn = {
        celsiusBtn.x + buttonWidth + spacing,
        startY,
        buttonWidth,
        buttonHeight,
        "F",
        false,
        true
    };

    Serial.printf("Celsius button: x=%d, y=%d, w=%d, h=%d\n", 
                 celsiusBtn.x, celsiusBtn.y, celsiusBtn.width, celsiusBtn.height);
    Serial.printf("Fahrenheit button: x=%d, y=%d, w=%d, h=%d\n", 
                 fahrenheitBtn.x, fahrenheitBtn.y, fahrenheitBtn.width, fahrenheitBtn.height);
    
    // Draw buttons
    drawButton(celsiusBtn, settings.useCelsius ? DisplayConfig::COLOR_BUTTON_ACTIVE : DisplayConfig::COLOR_BUTTON);
    drawButton(fahrenheitBtn, !settings.useCelsius ? DisplayConfig::COLOR_BUTTON_ACTIVE : DisplayConfig::COLOR_BUTTON);
    
    // Draw labels with larger text size
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(3);  // Increased text size
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    
    // Draw unit symbols
    CoreS3.Display.drawString("C", celsiusBtn.x + celsiusBtn.width/2, celsiusBtn.y + celsiusBtn.height/2);
    CoreS3.Display.drawString("F", fahrenheitBtn.x + fahrenheitBtn.width/2, fahrenheitBtn.y + fahrenheitBtn.height/2);
    
    // Draw unit names
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Celsius", celsiusBtn.x + celsiusBtn.width/2, celsiusBtn.y + celsiusBtn.height + 20);
    CoreS3.Display.drawString("Fahrenheit", fahrenheitBtn.x + fahrenheitBtn.width/2, fahrenheitBtn.y + fahrenheitBtn.height + 20);
    
    // Handle touch input
    unsigned long startTime = millis();
    bool selectionMade = false;
    
    while (!selectionMade && (millis() - startTime < 30000)) {  // 30 second timeout
        esp_task_wdt_reset();
        CoreS3.update();  // Update M5Stack state
        
        if (CoreS3.Touch.getCount()) {
            auto t = CoreS3.Touch.getDetail();
            Serial.printf("Touch detected: x=%d, y=%d\n", t.x, t.y);
            
            if (t.wasPressed()) {
                Serial.println("Touch was pressed");
                
                if (touchInButton(celsiusBtn, t.x, t.y)) {
                    Serial.println("Celsius button pressed");
                    // Highlight selected button
                    drawButton(celsiusBtn, DisplayConfig::COLOR_BUTTON_ACTIVE);
                    CoreS3.Display.setTextSize(3);
                    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
                    CoreS3.Display.drawString("C", celsiusBtn.x + celsiusBtn.width/2, celsiusBtn.y + celsiusBtn.height/2);
                    
                    // Play sound and update settings
                    playTouchSound(true);
                    settings.useCelsius = true;
                    selectionMade = true;
                    
                    // Add visual feedback delay
                    delay(200);
                }
                else if (touchInButton(fahrenheitBtn, t.x, t.y)) {
                    Serial.println("Fahrenheit button pressed");
                    // Highlight selected button
                    drawButton(fahrenheitBtn, DisplayConfig::COLOR_BUTTON_ACTIVE);
                    CoreS3.Display.setTextSize(3);
                    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
                    CoreS3.Display.drawString("F", fahrenheitBtn.x + fahrenheitBtn.width/2, fahrenheitBtn.y + fahrenheitBtn.height/2);
                    
                    // Play sound and update settings
                    playTouchSound(true);
                    settings.useCelsius = false;
                    selectionMade = true;
                    
                    // Add visual feedback delay
                    delay(200);
                }
            }
        }
        
        delay(10);  // Small delay to prevent tight loop
    }
    
    // Save settings if a selection was made
    if (selectionMade) {
        Serial.println("Temperature unit selected: " + String(settings.useCelsius ? "Celsius" : "Fahrenheit"));
        saveSettings();
        // Clear screen before returning
        CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
        return true;
    }
    
    // If no selection was made (timeout)
    Serial.println("Temperature unit selection timed out");
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    return false;
}
void enterSettingsMenu() {
    state.isMonitoring = false;  // Stop monitoring while in settings
    state.inSettingsMenu = true;
    state.settingsScrollPosition = 0;
    state.selectedMenuItem = -1;
    drawSettingsMenu();
}
void toggleMonitoring() {
    state.isMonitoring = !state.isMonitoring;
    if (state.isMonitoring) {
        handleTemperatureAlerts();
    } else {
        updateStatusDisplay("Ready", DisplayConfig::COLOR_TEXT);
    }
}
void updateTemperatureTrend() {
    // Update temperature trend
    state.tempHistory[state.historyIndex] = state.currentTemp;
    state.historyIndex = (state.historyIndex + 1) % 5;
}
void checkBatteryWarning(unsigned long currentMillis) {
    if (currentMillis - state.lastBatteryUpdate >= TimeConfig::BATTERY_CHECK) {
        float voltage = CoreS3.Power.getBatteryVoltage();
        if (voltage < 3.3 && !CoreS3.Power.isCharging()) {
            updateStatusDisplay("Low Battery", DisplayConfig::COLOR_WARNING);
            setCpuFrequencyMhz(80);  // Reduce CPU frequency to save power
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
    float displayTemp = state.currentTemp / 100.0f;  // Convert to actual temperature
    
    if (displayTemp < TempConfig::MIN_C || displayTemp > TempConfig::MAX_C) {
        updateStatusDisplay("Temperature Out of Range", DisplayConfig::COLOR_WARNING);
    } else if (abs(displayTemp - TempConfig::TARGET_C) > TempConfig::TOLERANCE_C) {
        updateStatusDisplay("Temperature Warning", DisplayConfig::COLOR_WARNING);
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

void handleTouchInput() {
    if (CoreS3.Touch.getCount()) {
        auto t = CoreS3.Touch.getDetail();
        if (t.wasPressed()) {
            // If display is off or in sleep mode
            if (CoreS3.Display.getBrightness() == 0) {
                // Reset states
                state.inSettingsMenu = false;
                state.isMonitoring = false;
                
                // Wake up display
                CoreS3.Display.wakeup();
                CoreS3.Display.setBrightness(settings.brightness);
                delay(50);  // Allow display to stabilize
                
                // Redraw main interface
                drawInterface();
                return;
            }

            state.lastActivityTime = millis();
            
            if (state.inSettingsMenu) {
                handleSettingsMenu();
            } else {
                bool buttonPressed = false;
                
                // Check monitor button
                if (touchInButton(monitorBtn, t.x, t.y)) {
                    // Visual feedback - highlight button
                    uint32_t originalColor = monitorBtn.pressed ? 
                        DisplayConfig::COLOR_BUTTON_ACTIVE : 
                        DisplayConfig::COLOR_BUTTON;
                    
                    drawButton(monitorBtn, DisplayConfig::COLOR_SELECTED);
                    delay(100);  // Brief highlight
                    drawButton(monitorBtn, originalColor);
                    
                    toggleMonitoring();
                    buttonPressed = true;
                }
                // Check settings button
                else if (touchInButton(settingsBtn, t.x, t.y)) {
                    // Visual feedback - highlight button
                    drawButton(settingsBtn, DisplayConfig::COLOR_SELECTED);
                    delay(100);  // Brief highlight
                    drawButton(settingsBtn, DisplayConfig::COLOR_BUTTON);
                    
                    enterSettingsMenu();
                    buttonPressed = true;
                }
                
                // Play sound based on whether a button was pressed
                playTouchSound(buttonPressed);
            }
        }
    }
}
bool isSettingsMenuVisible() {
    // Check current display brightness
    if (CoreS3.Display.getBrightness() == 0) {
        return false;
    }
    
    // Check menu state
    if (!state.inSettingsMenu) {
        return false;
    }
    
    return true;
}

void drawSettingsMenu() {
    esp_task_wdt_reset();
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    debugLog("drawSettingsMenu", "Starting menu draw");
    
    // Clear screen
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    
    // Draw header
    CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), HEADER_HEIGHT, DisplayConfig::COLOR_HEADER);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.drawString("Settings", CoreS3.Display.width() / 2, HEADER_HEIGHT / 2);
    
    // Calculate visible area
    int startY = HEADER_HEIGHT + MARGIN;
    int visibleHeight = CoreS3.Display.height() - startY - MARGIN;
    int maxScroll = max(0, (MenuConfig::MENU_ITEMS * MenuConfig::ITEM_HEIGHT) - visibleHeight);
    
    // Constrain scroll position
    state.settingsScrollPosition = constrain(state.settingsScrollPosition, 0, maxScroll);
    
    // Draw menu items
    for (int i = 0; i < MenuConfig::MENU_ITEMS; i++) {
        int itemY = startY + (i * MenuConfig::ITEM_HEIGHT) - state.settingsScrollPosition;
        
        // Only draw items that are visible
        if (itemY + MenuConfig::ITEM_HEIGHT > HEADER_HEIGHT && itemY < CoreS3.Display.height()) {
            // Draw item background
            if (i == state.selectedMenuItem) {
                CoreS3.Display.fillRect(0, itemY, CoreS3.Display.width(), MenuConfig::ITEM_HEIGHT, 
                    DisplayConfig::COLOR_SELECTED);
            }
            
            // Draw item label
            CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
            CoreS3.Display.setTextDatum(middle_left);
            CoreS3.Display.drawString(MenuConfig::MENU_LABELS[i], MARGIN, itemY + MenuConfig::ITEM_HEIGHT/2);
            
            // Draw controls based on item type
            int rightMargin = CoreS3.Display.width() - MARGIN;
            switch(i) {
                case 0: // Temperature Unit
                    drawToggleSwitch(rightMargin - 60, itemY + 10, settings.useCelsius);
                    break;
                    
                case 1: // Sound
                    drawToggleSwitch(rightMargin - 60, itemY + 10, settings.soundEnabled);
                    break;
                    
                case 2: // Brightness
                    drawBrightnessButtons(rightMargin - 290, itemY + 5, settings.brightness);
                    break;
                    
                case 3: // Emissivity
                    CoreS3.Display.setTextDatum(middle_right);
                    char emissStr[8];
                    sprintf(emissStr, "%.2f", settings.emissivity);
                    CoreS3.Display.drawString(emissStr, rightMargin - 10, itemY + MenuConfig::ITEM_HEIGHT/2);
                    break;
            }
        }
    }
    
    // Draw scroll indicators if needed
    if (state.settingsScrollPosition > 0) {
        drawScrollIndicator(true, 5);  // Up arrow
    }
    if (state.settingsScrollPosition < maxScroll) {
        drawScrollIndicator(false, CoreS3.Display.height() - 15);  // Down arrow
    }
    
    esp_task_wdt_reset();
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
    bool touched = (touch_x >= btn.x && 
                   touch_x <= (btn.x + btn.width) &&
                   touch_y >= btn.y && 
                   touch_y <= (btn.y + btn.height) &&
                   btn.enabled);
    
    if (touched) {
        Serial.printf("Button touched: x=%d, y=%d, btn.x=%d, btn.y=%d, btn.width=%d, btn.height=%d\n",
                     touch_x, touch_y, btn.x, btn.y, btn.width, btn.height);
    }
    
    return touched;
}
// Battery status display
void drawBatteryStatus() {
    static bool lastChargingState = false;
    static unsigned long lastVoltageCheck = 0;
    
    int batteryLevel = CoreS3.Power.getBatteryLevel();
    bool isCharging = CoreS3.Power.isCharging();
    float voltage = CoreS3.Power.getBatteryVoltage();
    
    // Check if power source changed
    if (lastChargingState != isCharging) {
        debugLogf("Power", "Source changed: %s, Voltage: %.2fV", isCharging ? "USB" : "Battery", voltage);
        lastChargingState = isCharging;
        
        // Re-initialize components on power source change
        debugLog("Power", "Reinitializing components...");
        Wire.end();
        delay(50);
        Wire.begin(2, 1);
        delay(100);
        
        // Reinitialize sensor
        if (!ncir2.begin(&Wire, 2, 1, M5UNIT_NCIR2_DEFAULT_ADDR)) {
            debugLog("Power", "Failed to reinitialize sensor after power change");
            updateStatusDisplay("Sensor Error", DisplayConfig::COLOR_WARNING);
        } else {
            debugLog("Power", "Sensor reinitialized successfully");
        }

        // Reset touch if needed
        CoreS3.Touch.begin(&CoreS3.Display);
        delay(50);
        debugLog("Power", "Touch reinitialized");
    }

    // Monitor voltage every second
    if (millis() - lastVoltageCheck >= 1000) {
        lastVoltageCheck = millis();
        debugLogf("Power", "Status - Voltage: %.2fV, Level: %d%%, Charging: %s", 
                 voltage, batteryLevel, isCharging ? "Yes" : "No");
                     
        if (voltage < 3.3 && !isCharging) {
            debugLog("Power", "Low voltage detected, reducing CPU frequency");
            updateStatusDisplay("Low Battery", DisplayConfig::COLOR_WARNING);
            setCpuFrequencyMhz(80);
        }
    }

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
    
    // Draw charging indicator
    if (isCharging) {
        CoreS3.Display.fillTriangle(
            battX - 15, battY + battH/2,
            battX - 10, battY + 2,
            battX - 5, battY + battH/2,
            DisplayConfig::COLOR_CHARGING
        );
    }
}
void exitSettingsMenu() {
    state.isProcessingSettings = false;  // Ensure processing flag is cleared
    state.inSettingsMenu = false;
    state.selectedMenuItem = -1;
    state.settingsScrollPosition = 0;
    state.lastActivityTime = millis();  // Reset activity timer
    drawInterface();
}
void setup() {
    // Start Serial first and add delay for stability
    Serial.begin(115200);
    delay(1000);
    debugLog("Setup", "\nStarting TerpMeter initialization...");

    // Initialize M5 device with explicit configuration that disables I2S
    auto cfg = M5.config();
    cfg.internal_spk = false;  // Disable speaker initialization
    cfg.internal_mic = false;  // Disable microphone initialization
    cfg.internal_imu = false;  // Disable IMU if not needed
    cfg.internal_rtc = false;  // Disable RTC if not needed
    cfg.clear_display = true;
    cfg.output_power = true;
    
    CoreS3.begin(cfg);
    CoreS3.Power.begin();
    CoreS3.Power.setChargeCurrent(900);  // Set to 300mA for stability
    setCpuFrequencyMhz(80);  // Set CPU to lower frequency for stability
    
    // Only initialize speaker when needed for sounds
    if (settings.soundEnabled) {
        CoreS3.Speaker.begin();
        CoreS3.Speaker.setVolume(128);
    }
    
    
    debugLogf("Setup", "Power initialized - Charging: %s, Battery: %d%%, Voltage: %.2fV", 
              CoreS3.Power.isCharging() ? "Yes" : "No",
              CoreS3.Power.getBatteryLevel(),
              CoreS3.Power.getBatteryVoltage());

    // Initialize display with explicit settings
    CoreS3.Display.begin();
    CoreS3.Display.setRotation(1);
    CoreS3.Display.setBrightness(128); // Start with 50% brightness
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    debugLog("Setup", "Display initialized");

    // Initialize touch with display
    CoreS3.Touch.begin(&CoreS3.Display); // Pass the display object to touch initialization
    debugLog("Setup", "Touch initialized");


    // Initialize preferences
    if (!preferences.begin("terp-meter", false)) {
        debugLog("Setup", "Failed to initialize preferences");
    } else {
        debugLog("Setup", "Preferences initialized");
    }

    // Load settings
    loadSettings();
    debugLogf("Setup", "Settings loaded - Celsius: %s, Sound: %s, Brightness: %d, Emissivity: %.2f",
              settings.useCelsius ? "Yes" : "No",
              settings.soundEnabled ? "Yes" : "No",
              settings.brightness,
              settings.emissivity);

    // Initialize I2C for NCIR sensor
    Wire.begin(2, 1);
    delay(100);
    if (!ncir2.begin(&Wire, 2, 1, M5UNIT_NCIR2_DEFAULT_ADDR)) {
        debugLog("Setup", "Failed to initialize NCIR sensor!");
        CoreS3.Display.println("NCIR Sensor Error");
    } else {
        debugLog("Setup", "NCIR sensor initialized");
    }
    ncir2.setLEDColor(0);

    // Initialize watchdog timer
    esp_task_wdt_init(10, false);  // 10 second timeout, don't panic on timeout
    esp_task_wdt_add(NULL);  // Add current task to watchdog
    debugLog("Setup", "Watchdog initialized with 10s timeout");

    // Show startup animation
    showStartupAnimation();
    debugLog("Setup", "Startup animation completed");
    
    // Initialize temperature unit selection
    if (!selectTemperatureUnit()) {
        debugLog("Setup", "Temperature unit selection timed out");
        settings.useCelsius = true; // Default to Celsius if no selection
    } else {
        debugLogf("Setup", "Temperature unit selected: %s", 
                 settings.useCelsius ? "Celsius" : "Fahrenheit");
    }
    
    // Save settings
    saveSettings();
    debugLog("Setup", "Settings saved");

    // Initialize display interface
    drawInterface();
    debugLog("Setup", "Initial interface drawn");

    // Print final memory status
    printMemoryInfo();
    debugLog("Setup", "Setup complete!");

    // Reset watchdog timer before exiting setup
    esp_task_wdt_reset();
}
void loop() {
    // Reset watchdog
    esp_task_wdt_reset();

    // Get current time
    unsigned long currentMillis = millis();

    // Process touch input with debounce
    if (CoreS3.Touch.getCount() > 0) {
        static unsigned long lastButtonPress = 0;
        if (currentMillis - lastButtonPress >= 200) { // 200ms debounce
            handleTouchInput();
            lastButtonPress = currentMillis;
        }
    }

    // Main monitoring loop
    if (state.isMonitoring) {
        if (currentMillis - state.lastTempUpdate >= 100) { // 100ms update interval
            // Get raw temperature and filter it
            int16_t rawTemp = ncir2.getTempValue();
            state.currentTemp = filterAndCalibrateTemp(rawTemp);

            // Debug logging
            debugLogf("Temperature", "Raw: %d, Filtered: %d", 
                     rawTemp, state.currentTemp);

            // Update the display
            drawInterface();
            state.lastTempUpdate = currentMillis;
        }
    }

    // Settings menu handling
    if (state.inSettingsMenu) {
        drawSettingsMenu();
    }

    // Battery monitoring and power management
    if (currentMillis - state.lastBatteryUpdate >= 5000) { // 5 second interval
        // Get battery status
        float voltage = CoreS3.Power.getBatteryVoltage();
        int level = CoreS3.Power.getBatteryLevel();
        bool charging = CoreS3.Power.isCharging();

        // Debug logging
        debugLogf("Battery", "Voltage: %.2fV, Level: %d%%, Charging: %s",
                 voltage, level, charging ? "Yes" : "No");

        // Low battery handling
        if (voltage < 3.3 && !charging) {
            updateStatusDisplay("Low Battery", TFT_RED);
            if (CoreS3.Display.getBrightness() > 64) {
                CoreS3.Display.setBrightness(64); // Reduce brightness
            }
            setCpuFrequencyMhz(80); // Reduce CPU frequency
        }

        // Update battery indicator
        drawBatteryStatus();
        state.lastBatteryUpdate = currentMillis;
    }

    // Memory monitoring (every 30 seconds)
    static unsigned long lastMemCheck = 0;
    if (currentMillis - lastMemCheck >= 30000) {
        debugLogf("Memory", "Free heap: %d bytes", ESP.getFreeHeap());
        lastMemCheck = currentMillis;
    }

    // Small delay to prevent watchdog triggers
    delay(10);
    esp_task_wdt_reset();
}