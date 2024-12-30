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
    // All temperatures are stored in Celsius * 100
    static const int32_t MIN_C = 15000;     // 150°C
    static const int32_t MAX_C = 20000;     // 200°C
    static const int32_t TARGET_C = 17500;  // 175°C
    static const int32_t TOLERANCE_C = 500;  // 5°C
    static const int32_t MIN_VALID_TEMP = -7000;  // -70°C
    static const int32_t MAX_VALID_TEMP = 38000;  // 380°C
}

// Timing configuration (ms)
namespace TimeConfig {
    static const unsigned long TEMP_UPDATE = 250;
    static const unsigned long BATTERY_CHECK = 30000;
    static const unsigned long STATUS_UPDATE = 5000;
    static const unsigned long TOUCH_DEBOUNCE = 100;
    static const unsigned long BEEP_INTERVAL = 2000;
    static const unsigned long BATTERY_UPDATE_INTERVAL = 60000;
    static const unsigned long TEMP_UPDATE_INTERVAL = 250;
    static const unsigned long DEBUG_UPDATE_INTERVAL = 10000;
    static const unsigned long ACTIVITY_TIMEOUT = 300000;  // 5 minutes
    static const unsigned long BATTERY_CHECK_INTERVAL = 5000;  // 5 seconds
}

// Display configuration
namespace DisplayConfig {
    const uint32_t COLOR_BACKGROUND = 0x000B1E;  // Deep navy blue
    const uint32_t COLOR_TEXT = 0x00FF9C;        // Bright cyan/mint
    const uint32_t COLOR_HEADER = 0x1B3358;      // Muted blue
    const uint32_t COLOR_BUTTON = 0x1B3358;      // Muted blue
    const uint32_t COLOR_BUTTON_ACTIVE = 0x3CD070; // Bright green
    const uint32_t COLOR_SELECTED = 0x3CD070;    // Bright green
    const uint32_t COLOR_GOOD = 0x3CD070;        // Bright green
    const uint32_t COLOR_WARNING = 0xFFB627;     // Warm amber
    const uint32_t COLOR_ALERT = 0xFF0F7B;       // Neon pink
    const uint32_t COLOR_CHARGING = 0xFFFF00;    // Yellow
    const uint32_t COLOR_DISABLED = 0x424242;    // Dark gray
    const uint32_t COLOR_HOT = 0xFF0F7B;         // Neon pink (same as ALERT)
    const uint32_t COLOR_COLD = 0x00A6FB;        // Bright blue
    const uint32_t COLOR_BATTERY_LOW = 0xFF0000;   // Red
    const uint32_t COLOR_BATTERY_MED = 0xFFA500;   // Orange
    const uint32_t COLOR_BATTERY_GOOD = 0x00FF00;  // Green
    const uint32_t COLOR_ICE_BLUE = 0x87CEFA;    // Light ice blue
    const uint32_t COLOR_NEON_GREEN = 0x39FF14;  // Bright neon green
    const uint32_t COLOR_LAVA = 0xFF4500;        // Red-orange lava color
    const uint32_t COLOR_HIGHLIGHT = 0x6B6B6B;   // Button highlight color
}

// Layout constants
const int MARGIN = 10;
const int HEADER_HEIGHT = 30;
const int TEMP_BOX_HEIGHT = 70;  // Reduced from 90 to 70
const int STATUS_BOX_HEIGHT = 45;
const int BUTTON_HEIGHT = 50;
const int BUTTON_SPACING = 10;

// Touch and feedback constants
const int TOUCH_THRESHOLD = 10;      // Minimum movement to register as a touch

// Speaker constants
const int BEEP_FREQUENCY = 1000;  // 1kHz tone
const int BEEP_DURATION = 100;    // 100ms beep

// Emissivity constants and variables
const float EMISSIVITY_MIN = 0.1;
const float EMISSIVITY_MAX = 1.0;
const float EMISSIVITY_STEP = 0.05;

// Menu configuration
namespace MenuConfig {
    const int MENU_ITEMS = 4;  // Reduced from 5 to 4
    const char* MENU_LABELS[] = {
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
    int brightness = 64;
    double emissivity = 0.95;  // Changed to double to match NCIR2 library

    void load(Preferences& prefs) {
        useCelsius = prefs.getBool("useCelsius", true);
        soundEnabled = prefs.getBool("soundEnabled", true);
        brightness = prefs.getInt("brightness", 64);
        emissivity = prefs.getDouble("emissivity", 0.95);
        // Apply emissivity to sensor
        ncir2.setEmissivity(emissivity);
    }

    void save(Preferences& prefs) {
        prefs.putBool("useCelsius", useCelsius);
        prefs.putBool("soundEnabled", soundEnabled);
        prefs.putInt("brightness", brightness);
        prefs.putDouble("emissivity", emissivity);
        // Apply emissivity to sensor
        ncir2.setEmissivity(emissivity);
    }
};

// Device state management
struct DeviceState {
    int32_t currentTemp = 0;  // Changed from int16_t to int32_t to handle higher temperatures
    bool isMonitoring = false;
    int32_t lastDisplayTemp = -999;  // Changed from int16_t to int32_t
    String lastStatus = "";
    int lastBatLevel = -1;
    bool lastChargeState = false;
    unsigned long lastBeepTime = 0;
    unsigned long lastTempUpdate = 0;
    unsigned long lastBatteryUpdate = 0;
    unsigned long lastActivityTime = 0;
    unsigned long lastDebugUpdate = 0;
    bool lowBatteryWarningShown = false;
    float tempHistory[5] = {0};
    int historyIndex = 0;
    bool inSettingsMenu = false;
    int selectedMenuItem = -1;
    int settingsScrollPosition = 0;
    unsigned long settingsExitTime = 0;  // Track when we exited settings menu
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

// Function declarations
void drawButton(Button &btn, uint32_t color);
void drawInterface();
void updateDisplay();
void handleTemperatureAlerts();
void drawBatteryStatus();
bool touchInButton(Button &btn, int32_t touch_x, int32_t touch_y);
int32_t celsiusToFahrenheit(int32_t celsius);
int32_t fahrenheitToCelsius(int32_t fahrenheit);
void updateTemperatureDisplay(int32_t temp);
void adjustEmissivity();
void updateStatusDisplay(const char* status, uint32_t color);
void showLowBatteryWarning(int batteryLevel);
void handleTouchInput();
void toggleMonitoring();
void drawSettingsMenu();
void handleSettingsMenu();
void saveSettings();
void loadSettings();
void enterEmissivityMode();
void updateStatusMessage();
void checkForErrors();
void wakeUp();
void drawToggleSwitch(int x, int y, bool state);
void showRestartConfirmation(float oldEmissivity, float newEmissivity);
void exitSettingsMenu();
void playTouchSound(bool success = true);
void drawBrightnessButtons(int x, int y, int currentBrightness);
void resetSettingsMenuState();
void validateSettingsScroll();
bool isTemperatureValid(float temp);
bool debounceTouch();

// Button declarations
Button monitorBtn = {0, 0, 0, 0, "Monitor", false, true};
Button settingsBtn = {0, 0, 0, 0, "Settings", false, true};  // Changed from emissivityBtn
static LGFX_Sprite* tempSprite = nullptr;

Settings settings;
DeviceState state;

bool debounceTouch() {
    static unsigned long lastTouchTime = 0;
    const unsigned long debounceDelay = 250; // 250ms debounce
    
    unsigned long currentTime = millis();
    if (currentTime - lastTouchTime >= debounceDelay) {
        lastTouchTime = currentTime;
        return true;
    }
    return false;
}

bool touchInButton(Button &btn, int32_t touch_x, int32_t touch_y) {
    // Add some padding to make buttons easier to press
    const int padding = 5;
    return (touch_x >= btn.x - padding && 
            touch_x <= btn.x + btn.w + padding && 
            touch_y >= btn.y - padding && 
            touch_y <= btn.y + btn.h + padding);
}

bool isTemperatureValid(float temp) {
    // Convert to int32_t (keeping 2 decimal places)
    int32_t tempInt = temp * 100;
    return (tempInt >= TempConfig::MIN_VALID_TEMP && tempInt <= TempConfig::MAX_VALID_TEMP);
}

void validateSettingsScroll() {
    int itemHeight = 50;
    int startY = HEADER_HEIGHT + MARGIN;
    int visibleHeight = CoreS3.Display.height() - startY - MARGIN;
    int totalMenuHeight = MenuConfig::MENU_ITEMS * itemHeight;
    int maxScroll = max(0, totalMenuHeight - visibleHeight);
    
    // Ensure scroll position stays within valid bounds
    state.settingsScrollPosition = constrain(state.settingsScrollPosition, 0, maxScroll);
}

void drawButton(Button &btn, uint32_t color) {
    // Draw button background
    CoreS3.Display.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 8, btn.enabled ? color : DisplayConfig::COLOR_DISABLED);
    
    // Draw button text
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(btn.enabled ? DisplayConfig::COLOR_TEXT : 
        ((DisplayConfig::COLOR_DISABLED & 0xFCFCFC) | 0x404040));
    CoreS3.Display.drawString(btn.label, btn.x + btn.w/2, btn.y + btn.h/2);
}

void resetSettingsMenuState() {
    state.settingsScrollPosition = 0;
    state.selectedMenuItem = -1;
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
    int32_t testTemp = ncir2.getTempValue();
    if (testTemp == 0 || testTemp < -1000 || testTemp > 10000) {
        // Sensor communication error
        updateStatusDisplay("Sensor Error", DisplayConfig::COLOR_HOT);
    }
}

void updateStatusMessage() {
    // This is already handled by updateStatusDisplay()
    handleTemperatureAlerts();
}

void enterEmissivityMode() {
    // This function already exists as adjustEmissivity()
    adjustEmissivity();
}

void exitSettingsMenu() {
    state.inSettingsMenu = false;
    resetSettingsMenuState();
    unsigned long now = millis();
    state.lastActivityTime = now;
    state.settingsExitTime = now;
    
    // Reset monitoring state if it was on
    if (state.isMonitoring) {
        state.isMonitoring = !state.isMonitoring;
    }
    
    // Clear screen and redraw full interface
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    drawInterface();
    drawBatteryStatus();
    updateTemperatureDisplay(state.currentTemp);  // This will also update the status
}

void loadSettings() {
    settings.load(preferences);
    // Apply loaded settings
    CoreS3.Display.setBrightness(settings.brightness);
}

void saveSettings() {
    settings.save(preferences);
}

void handleTouchInput() {
    if (!CoreS3.Touch.getCount()) return;  // No touch detected
    
    auto t = CoreS3.Touch.getDetail();
    if (!t.wasPressed()) return;  // Not a press event
    
    state.lastActivityTime = millis();
    
    if (state.inSettingsMenu) {
        handleSettingsMenu();
    } else {
        if (touchInButton(monitorBtn, t.x, t.y)) {
            playTouchSound(true);
            toggleMonitoring();
        } else if (touchInButton(settingsBtn, t.x, t.y)) {
            playTouchSound(true);
            enterSettingsMenu();
        }
    }
}

void updateTemperatureDisplay(int32_t temp) {
    // Only update if temperature has changed
    if (temp == state.lastDisplayTemp && !state.inSettingsMenu) {
        return;
    }
    
    state.lastDisplayTemp = temp;
    
    // Clear temperature display area
    int tempBoxY = HEADER_HEIGHT + MARGIN;
    CoreS3.Display.fillRect(MARGIN + 1, tempBoxY + 1, 
                              CoreS3.Display.width() - (2 * MARGIN) - 2, 
                              TEMP_BOX_HEIGHT - 2, 
                              DisplayConfig::COLOR_BACKGROUND);
    
    // Format temperature string
    char tempStr[32];
    char unitStr[2] = {settings.useCelsius ? 'C' : 'F', '\0'};
    float displayTemp;
    uint32_t tempColor;
    
    if (settings.useCelsius) {
        displayTemp = temp / 100.0f;  // Convert back to float
        if (displayTemp < TempConfig::MIN_C / 100.0f) {
            tempColor = DisplayConfig::COLOR_COLD;
            ncir2.setLEDColor(DisplayConfig::COLOR_COLD);
            updateStatusDisplay("Too Cold", DisplayConfig::COLOR_COLD);
        } else if (displayTemp > TempConfig::MAX_C / 100.0f) {
            tempColor = DisplayConfig::COLOR_HOT;
            ncir2.setLEDColor(DisplayConfig::COLOR_HOT);
            updateStatusDisplay("Too Hot", DisplayConfig::COLOR_HOT);
        } else if (abs(displayTemp * 100 - TempConfig::TARGET_C) <= TempConfig::TOLERANCE_C) {
            tempColor = DisplayConfig::COLOR_GOOD;
            ncir2.setLEDColor(DisplayConfig::COLOR_GOOD);
            updateStatusDisplay("Perfect", DisplayConfig::COLOR_GOOD);
        } else {
            tempColor = DisplayConfig::COLOR_WARNING;
            ncir2.setLEDColor(DisplayConfig::COLOR_WARNING);
            updateStatusDisplay("Getting Close", DisplayConfig::COLOR_WARNING);
        }
    } else {
        // Convert to Fahrenheit for display
        int32_t tempF = celsiusToFahrenheit(temp);
        displayTemp = tempF / 100.0f;
        
        if (displayTemp < 300.0f) {  // Below 300°F
            tempColor = DisplayConfig::COLOR_COLD;
            ncir2.setLEDColor(DisplayConfig::COLOR_COLD);
            updateStatusDisplay("Too Cold", DisplayConfig::COLOR_COLD);
        } else if (displayTemp > 640.0f) {  // Above 640°F
            tempColor = DisplayConfig::COLOR_HOT;
            ncir2.setLEDColor(DisplayConfig::COLOR_HOT);
            updateStatusDisplay("Too Hot", DisplayConfig::COLOR_HOT);
        } else if (displayTemp >= 500.0f && displayTemp <= 520.0f) {  // Perfect range
            tempColor = DisplayConfig::COLOR_GOOD;
            ncir2.setLEDColor(DisplayConfig::COLOR_GOOD);
            updateStatusDisplay("Perfect", DisplayConfig::COLOR_GOOD);
        } else {
            tempColor = DisplayConfig::COLOR_WARNING;
            ncir2.setLEDColor(DisplayConfig::COLOR_WARNING);
            updateStatusDisplay("Getting Close", DisplayConfig::COLOR_WARNING);
        }
    }
    
    // Format temperature with 1 decimal place
    sprintf(tempStr, "%.1f°%s", displayTemp, unitStr);
    
    // Draw temperature
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(tempColor);
    CoreS3.Display.setTextSize(4);
    CoreS3.Display.drawString(tempStr, CoreS3.Display.width()/2, HEADER_HEIGHT + MARGIN + (TEMP_BOX_HEIGHT/2));
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
    // Draw interface layout
    int screenWidth = CoreS3.Display.width();
    int contentWidth = screenWidth - (2 * MARGIN);
    int buttonY = CoreS3.Display.height() - BUTTON_HEIGHT - MARGIN;
    
    // Draw background
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    
    // Temperature display box
    int tempBoxHeight = 80;
    int tempBoxY = HEADER_HEIGHT + MARGIN;
    CoreS3.Display.drawRoundRect(MARGIN, tempBoxY, contentWidth, tempBoxHeight, 8, DisplayConfig::COLOR_TEXT);
    
    // Status box
    int statusBoxY = buttonY - STATUS_BOX_HEIGHT - MARGIN;
    CoreS3.Display.drawRoundRect(MARGIN, statusBoxY, contentWidth, STATUS_BOX_HEIGHT, 8, DisplayConfig::COLOR_TEXT);
    
    // Update button dimensions
    int buttonWidth = (contentWidth - BUTTON_SPACING) / 2;
    monitorBtn = {MARGIN, buttonY, buttonWidth, BUTTON_HEIGHT, "Monitor", false, true};
    settingsBtn = {MARGIN + buttonWidth + BUTTON_SPACING, buttonY, buttonWidth, BUTTON_HEIGHT, "Settings", false, true};
    
    // Draw buttons with modern style
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(2);  // Smaller text size for buttons
    drawButton(monitorBtn, state.isMonitoring ? DisplayConfig::COLOR_BUTTON_ACTIVE : DisplayConfig::COLOR_BUTTON);
    drawButton(settingsBtn, DisplayConfig::COLOR_BUTTON);
    
    // Draw current emissivity value in top-left
    char emisStr[32];
    sprintf(emisStr, "ε: %.2f", settings.emissivity);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.setTextDatum(middle_left);
    CoreS3.Display.drawString(emisStr, MARGIN + 5, MARGIN + 15);
    
    // Update status display if not monitoring
    if (!state.isMonitoring) {
        updateStatusDisplay("Ready", DisplayConfig::COLOR_TEXT);
    }
}

void setup() {
    // Initialize M5Stack
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    
    // Initialize display
    CoreS3.Display.setRotation(1);  // Landscape mode
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.setTextDatum(middle_center);
    
    // Initialize serial for debugging
    Serial.begin(115200);
    
    // Initialize preferences
    preferences.begin("terpmeter", false);
    
    // Load settings
    loadSettings();
    
    // Set initial brightness
    CoreS3.Display.setBrightness(settings.brightness);
    
    // Initialize I2C for NCIR2
    Wire.begin(2, 1);  // SDA = GPIO2, SCL = GPIO1
    
    // Initialize NCIR2
    if (!ncir2.begin(&Wire)) {
        Serial.println("Failed to initialize NCIR2!");
        showMessage("Error", "Sensor Init Failed", DisplayConfig::COLOR_ALERT);
        delay(3000);
    }
    
    // Set initial emissivity
    ncir2.setEmissivity(settings.emissivity);
    
    // Calculate button positions
    int screenWidth = CoreS3.Display.width();
    int screenHeight = CoreS3.Display.height();
    int buttonWidth = (screenWidth - (3 * MARGIN)) / 2;
    int buttonY = screenHeight - BUTTON_HEIGHT - MARGIN;
    
    // Initialize monitor button
    monitorBtn.x = MARGIN;
    monitorBtn.y = buttonY;
    monitorBtn.w = buttonWidth;
    monitorBtn.h = BUTTON_HEIGHT;
    monitorBtn.enabled = true;
    
    // Initialize settings button
    settingsBtn.x = screenWidth - buttonWidth - MARGIN;
    settingsBtn.y = buttonY;
    settingsBtn.w = buttonWidth;
    settingsBtn.h = BUTTON_HEIGHT;
    settingsBtn.enabled = true;
    
    // Initialize state
    state = DeviceState();  // Reset all state variables
    state.lastActivityTime = millis();
    
    // Draw initial interface
    drawInterface();
    drawBatteryStatus();
    updateStatusDisplay("Ready", DisplayConfig::COLOR_TEXT);
    
    // Initial temperature reading
    float temp = ncir2.getTempValue();
    if (isTemperatureValid(temp)) {
        state.currentTemp = temp * 100;
        updateTemperatureDisplay(state.currentTemp);
    }
    
    // Enable speaker
    CoreS3.Speaker.begin();
    CoreS3.Speaker.setVolume(64);  // 25% volume
    
    // Play startup sound
    playTouchSound(true);
}

void loop() {
    unsigned long currentMillis = millis();
    M5.update();
    
    // Update battery status every 5 seconds
    if (currentMillis - state.lastBatteryUpdate >= TimeConfig::BATTERY_CHECK_INTERVAL) {
        drawBatteryStatus();
        state.lastBatteryUpdate = currentMillis;
    }
    
    // Handle touch input
    handleTouchInput();
    
    // Update temperature if monitoring
    if (state.isMonitoring) {
        if (currentMillis - state.lastTempUpdate >= TimeConfig::TEMP_UPDATE_INTERVAL) {
            float temp = ncir2.getTempValue();
            if (isTemperatureValid(temp)) {
                state.currentTemp = temp * 100;  // Convert to int32_t (keeping 2 decimal places)
                updateTemperatureDisplay(state.currentTemp);
                state.tempHistory[state.historyIndex] = state.currentTemp;
                state.historyIndex = (state.historyIndex + 1) % 5;
                handleTemperatureAlerts();
            }
            state.lastTempUpdate = currentMillis;
        }
    }
    
    // Handle settings menu if active
    if (state.inSettingsMenu) {
        handleSettingsMenu();
    }
    
    // Print debug info if enabled
    if (currentMillis - state.lastDebugUpdate >= TimeConfig::DEBUG_UPDATE_INTERVAL) {
        printDebugInfo();
        state.lastDebugUpdate = currentMillis;
    }
}

void enterSettingsMenu() {
    state.isMonitoring = false;
    state.inSettingsMenu = true;
    state.settingsScrollPosition = 0;
    state.selectedMenuItem = -1;
    drawSettingsMenu();
    delay(100); // Small delay to prevent immediate touch registration
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
    int32_t displayTemp;
    if (settings.useCelsius) {
        displayTemp = state.currentTemp / 100;
    } else {
        displayTemp = celsiusToFahrenheit(state.currentTemp) / 100;
    }
    
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
    
    // Only update if status has changed
    if (state.lastStatus != status) {
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
    float lastEmissivity = tempEmissivity - 1.0f;  // Force initial draw
    bool needsRedraw = true;
    
    // Save current state
    bool wasInSettingsMenu = state.inSettingsMenu;
    
    // Initial screen setup
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
    
    // Draw static buttons
    drawButton(upBtn, DisplayConfig::COLOR_BUTTON);
    drawButton(downBtn, DisplayConfig::COLOR_BUTTON);
    drawButton(doneBtn, DisplayConfig::COLOR_BUTTON);
    
    while (adjusting) {
        // Only redraw if value changed
        if (tempEmissivity != lastEmissivity) {
            // Clear previous value area
            CoreS3.Display.fillRect(0, 95, (screenWidth - 90), 40, DisplayConfig::COLOR_BACKGROUND);
            
            // Display current value
            char emisStr[16];
            sprintf(emisStr, "%.2f", tempEmissivity);
            CoreS3.Display.setTextSize(3);
            CoreS3.Display.setTextDatum(middle_center);
            CoreS3.Display.drawString(emisStr, (screenWidth - 90)/2, 115);
            
            lastEmissivity = tempEmissivity;
        }
        
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
    settings.useCelsius = !settings.useCelsius;
    saveSettings();
    updateTemperatureDisplay(state.currentTemp);
}

void drawSettingsMenu() {
    // Clear the screen first
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    
    // Draw header
    CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), HEADER_HEIGHT, DisplayConfig::COLOR_HEADER);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Settings", CoreS3.Display.width()/2, HEADER_HEIGHT/2);
    
    // Calculate menu boundaries
    int startY = HEADER_HEIGHT + MARGIN;
    int itemHeight = 50;
    int visibleHeight = CoreS3.Display.height() - startY - MARGIN;
    
    // Draw visible menu items
    for (int i = 0; i < MenuConfig::MENU_ITEMS; i++) {
        int itemY = startY + (i * itemHeight) - state.settingsScrollPosition;
        
        // Skip if item is not visible
        if (itemY + itemHeight < startY || itemY > CoreS3.Display.height() - MARGIN) {
            continue;
        }
        
        // Draw menu item background
        if (i == state.selectedMenuItem) {
            CoreS3.Display.fillRect(0, itemY, CoreS3.Display.width(), itemHeight, DisplayConfig::COLOR_SELECTED);
        }
        
        // Draw menu item text
        CoreS3.Display.setTextDatum(middle_left);
        CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
        CoreS3.Display.drawString(MenuConfig::MENU_LABELS[i], MARGIN, itemY + itemHeight/2);
        
        // Draw controls for each item
        switch(i) {
            case 0:  // Temperature Unit
                drawToggleSwitch(CoreS3.Display.width() - 70 - MARGIN, itemY + 13, settings.useCelsius);
                break;
                
            case 1:  // Sound
                drawToggleSwitch(CoreS3.Display.width() - 70 - MARGIN, itemY + 13, settings.soundEnabled);
                break;
                
            case 2:  // Brightness
                if (itemY >= startY && itemY + itemHeight <= CoreS3.Display.height() - MARGIN) {
                    drawBrightnessButtons(CoreS3.Display.width() - 290, itemY + 5, settings.brightness);
                }
                break;
                
            case 3:  // Emissivity
                char emisStr[8];
                sprintf(emisStr, "%.2f", settings.emissivity);
                CoreS3.Display.setTextDatum(middle_right);
                CoreS3.Display.drawString(emisStr, CoreS3.Display.width() - MARGIN, itemY + itemHeight/2);
                break;
        }
    }
}

void handleSettingsMenu() {
    if (!CoreS3.Touch.getCount()) return;
    
    auto t = CoreS3.Touch.getDetail();
    if (!t.wasPressed()) return;
    
    // Update activity time for ANY touch in settings
    unsigned long now = millis();
    state.lastActivityTime = now;
    
    // Calculate menu boundaries
    int itemHeight = 50;
    int startY = HEADER_HEIGHT + MARGIN;
    int visibleHeight = CoreS3.Display.height() - startY - MARGIN;
    int totalMenuHeight = MenuConfig::MENU_ITEMS * itemHeight;
    int maxScroll = max(0, totalMenuHeight - visibleHeight);
    
    // Declare all variables needed for cases before the switch
    int btnWidth = 60;
    int spacing = 10;
    int startX = CoreS3.Display.width() - 290;
    int btnIndex = 0;
    float oldEmissivity = 0.0f;
    
    // Handle scrolling
    if (t.y < startY) {
        if (state.settingsScrollPosition > 0) {
            state.settingsScrollPosition = max(0, state.settingsScrollPosition - itemHeight);
            playTouchSound(true);
            drawSettingsMenu();
        }
        return;
    }
    
    if (t.y > CoreS3.Display.height() - MARGIN) {
        if (state.settingsScrollPosition < maxScroll) {
            state.settingsScrollPosition = min(maxScroll, state.settingsScrollPosition + itemHeight);
            playTouchSound(true);
            drawSettingsMenu();
        }
        return;
    }
    
    // Calculate touched item
    int touchedItem = (t.y - startY + state.settingsScrollPosition) / itemHeight;
    
    // Validate touched item
    if (touchedItem >= 0 && touchedItem < MenuConfig::MENU_ITEMS) {
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
                
            case 2:  // Brightness
                btnIndex = (t.x - startX) / (btnWidth + spacing);
                if (btnIndex >= 0 && btnIndex < 4) {
                    const int levels[4] = {64, 128, 192, 255};
                    settings.brightness = levels[btnIndex];
                    CoreS3.Display.setBrightness(settings.brightness);
                    playTouchSound(true);
                    saveSettings();
                }
                break;
                
            case 3:  // Emissivity
                oldEmissivity = settings.emissivity;
                playTouchSound(true);
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

void drawToggleSwitch(int x, int y, bool state) {
    const int width = 60;
    const int height = 24;
    const int knobSize = height - 4;
    const int knobX = state ? x + width - knobSize - 2 : x + 2;
    
    // Draw background
    uint32_t baseColor = state ? DisplayConfig::COLOR_GOOD : DisplayConfig::COLOR_BUTTON;
    CoreS3.Display.fillRoundRect(x, y, width, height, height/2, baseColor);
    
    // Draw knob with 3D effect
    uint32_t knobColor = state ? DisplayConfig::COLOR_TEXT : DisplayConfig::COLOR_DISABLED;
    CoreS3.Display.fillCircle(knobX + knobSize/2, y + height/2, knobSize/2, knobColor);
}

// Temperature conversion functions
int32_t celsiusToFahrenheit(int32_t celsius) {
    // celsius is in C * 100, result will be in F * 100
    return ((celsius * 9) / 5) + 3200;  // (C * 9/5 + 32) * 100
}

int32_t fahrenheitToCelsius(int32_t fahrenheit) {
    // fahrenheit is in F * 100, result will be in C * 100
    return ((fahrenheit - 3200) * 5) / 9;  // (F - 32) * 5/9 * 100
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
    // Draw battery tip
    CoreS3.Display.fillRect(battX + battW, battY + (battH - battTip) / 2,
                           battTip, battTip, DisplayConfig::COLOR_TEXT);
    
    // Calculate fill width based on battery level
    int fillWidth = (battW - 4) * batteryLevel / 100;
    
    // Choose color based on battery level or charging status
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

void toggleMonitoring() {
    state.isMonitoring = !state.isMonitoring;
    
    if (state.isMonitoring) {
        updateStatusDisplay("Monitoring...", DisplayConfig::COLOR_GOOD);
        monitorBtn.label = "Stop";
    } else {
        updateStatusDisplay("Ready", DisplayConfig::COLOR_TEXT);
        monitorBtn.label = "Monitor";
        // Turn off LED when not monitoring
        ncir2.setLEDColor(0);
    }
    
    // Redraw the monitor button with new label
    drawButton(monitorBtn, state.isMonitoring ? DisplayConfig::COLOR_BUTTON_ACTIVE : DisplayConfig::COLOR_BUTTON);
}