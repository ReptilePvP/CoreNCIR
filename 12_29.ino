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
    static const unsigned long TEMP_UPDATE = 250;
    static const unsigned long BATTERY_CHECK = 30000;
    static const unsigned long STATUS_UPDATE = 5000;
    static const unsigned long TOUCH_DEBOUNCE = 100;
    static const unsigned long BEEP_INTERVAL = 2000;
    static const unsigned long BATTERY_UPDATE_INTERVAL = 60000;
    static const unsigned long TEMP_UPDATE_INTERVAL = 250;
    static const unsigned long DEBUG_UPDATE_INTERVAL = 10000;
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
    float emissivity = 0.95;
    
    void load(Preferences& prefs) {
        useCelsius = prefs.getBool("useCelsius", true);
        soundEnabled = prefs.getBool("soundEnabled", true);
        autoSleep = prefs.getBool("autoSleep", true);
        brightness = prefs.getInt("brightness", 64);
        sleepTimeout = prefs.getInt("sleepTimeout", 5);
        emissivity = prefs.getFloat("emissivity", 0.95);
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
    float tempHistory[5] = {0};
    int historyIndex = 0;
    bool inSettingsMenu = false;
    int selectedMenuItem = -1;
    int settingsScrollPosition = 0;
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
static LGFX_Sprite* tempSprite = nullptr;

Settings settings;
DeviceState state;

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

void handleSettingsMenu() {
    auto t = CoreS3.Touch.getDetail();
    if (!t.wasPressed()) return;  // Exit if no touch detected
    
    state.lastActivityTime = millis();
    
    // Calculate touch areas
    int itemHeight = 50;
    int startY = HEADER_HEIGHT + MARGIN;
    int visibleHeight = CoreS3.Display.height() - startY - MARGIN;
    int maxScroll = max(0, (MenuConfig::MENU_ITEMS * itemHeight) - visibleHeight);
    
    // Handle scrolling
    if (t.y < startY && state.settingsScrollPosition > 0) {
        playTouchSound();
        state.settingsScrollPosition = max(0, state.settingsScrollPosition - itemHeight);
        drawSettingsMenu();
        return;
    } else if (t.y > CoreS3.Display.height() - MARGIN && state.settingsScrollPosition < maxScroll) {
        playTouchSound();
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
                playTouchSound();
                saveSettings();
                drawSettingsMenu();
                return;
            }
        }
        
        // Handle other menu items
        switch (touchedItem) {
            case 0:  // Temperature Unit
                settings.useCelsius = !settings.useCelsius;
                playTouchSound();
                saveSettings();
                break;
                
            case 1:  // Sound
                settings.soundEnabled = !settings.soundEnabled;
                playTouchSound();
                saveSettings();
                break;
                
            case 3:  // Auto Sleep
                settings.autoSleep = !settings.autoSleep;
                playTouchSound();
                saveSettings();
                break;
                
            case 4: {  // Emissivity
                playTouchSound();
                float oldEmissivity = settings.emissivity;
                adjustEmissivity();
                if (oldEmissivity != settings.emissivity) {
                    saveSettings();
                    showRestartConfirmation(oldEmissivity, settings.emissivity);
                }
                break;
            }
                
            case 5:  // Exit
                playTouchSound();
                exitSettingsMenu();
                return;
        }
        drawSettingsMenu();
    }
}

void drawSettingsMenu() {
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    
    // Draw header
    CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), HEADER_HEIGHT, DisplayConfig::COLOR_HEADER);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.drawString("Settings", CoreS3.Display.width()/2, HEADER_HEIGHT/2);
    
    // Calculate visible area
    int startY = HEADER_HEIGHT + MARGIN;
    int visibleHeight = CoreS3.Display.height() - startY - MARGIN;
    int maxScroll = max(0, (MenuConfig::MENU_ITEMS * 50) - visibleHeight);
    
    // Draw menu items
    for (int i = 0; i < MenuConfig::MENU_ITEMS; i++) {
        int itemY = startY + (i * 50) - state.settingsScrollPosition;
        
        // Skip if item is not visible
        if (itemY + 50 < startY || itemY > CoreS3.Display.height() - MARGIN) continue;
        
        // Highlight selected item
        if (i == state.selectedMenuItem) {
            CoreS3.Display.fillRect(0, itemY, CoreS3.Display.width(), 50, DisplayConfig::COLOR_SELECTED);
        }
        
        // Draw menu item text and controls
        CoreS3.Display.setTextDatum(middle_left);
        CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
        CoreS3.Display.drawString(MenuConfig::MENU_LABELS[i], MARGIN, itemY + 25);
        
        switch(i) {
            case 0:  // Temperature Unit
                drawToggleSwitch(CoreS3.Display.width() - 70 - MARGIN, itemY + 10, settings.useCelsius);
                break;
                
            case 1:  // Sound
                drawToggleSwitch(CoreS3.Display.width() - 70 - MARGIN, itemY + 10, settings.soundEnabled);
                break;
                
            case 2:  // Brightness
                drawBrightnessButtons(CoreS3.Display.width() - 290, itemY + 5, settings.brightness);
                break;
                
            case 3:  // Auto Sleep
                drawToggleSwitch(CoreS3.Display.width() - 70 - MARGIN, itemY + 10, settings.autoSleep);
                break;
                
            case 4:  // Emissivity
                char emisStr[8];
                sprintf(emisStr, "%.2f", settings.emissivity);
                CoreS3.Display.setTextDatum(middle_right);
                CoreS3.Display.drawString(emisStr, CoreS3.Display.width() - MARGIN, itemY + 25);
                break;
        }
    }
    
    // Draw scroll indicators if needed
    if (state.settingsScrollPosition > 0) {
        CoreS3.Display.fillTriangle(
            CoreS3.Display.width()/2 - 10, startY + 10,
            CoreS3.Display.width()/2 + 10, startY + 10,
            CoreS3.Display.width()/2, startY,
            DisplayConfig::COLOR_TEXT
        );
    }
    if (state.settingsScrollPosition < maxScroll) {
        int y = CoreS3.Display.height() - MARGIN - 10;
        CoreS3.Display.fillTriangle(
            CoreS3.Display.width()/2 - 10, y,
            CoreS3.Display.width()/2 + 10, y,
            CoreS3.Display.width()/2, y + 10,
            DisplayConfig::COLOR_TEXT
        );
    }
}

void updateTemperatureDisplay(int16_t temp) {
    static int16_t lastTemp = -999;
    static uint32_t lastUpdate = 0;
    static LGFX_Sprite* tempSprite = nullptr;
    
    // Update only if temperature changed or it's been more than 500ms
    if (temp != lastTemp || (millis() - lastUpdate) > 500) {
        // Calculate temperature box dimensions
        int screenWidth = CoreS3.Display.width();
        int boxWidth = screenWidth - (MARGIN * 2);
        int boxHeight = 80;
        int boxY = HEADER_HEIGHT + MARGIN;
        
        // Create sprite if it doesn't exist
        if (!tempSprite) {
            tempSprite = new LGFX_Sprite(&CoreS3.Display);
            tempSprite->createSprite(boxWidth, boxHeight);
        }
        
        // Clear sprite
        tempSprite->fillSprite(DisplayConfig::COLOR_BACKGROUND);
        tempSprite->drawRoundRect(0, 0, boxWidth, boxHeight, 8, DisplayConfig::COLOR_TEXT);
        
        // Format temperature string with one decimal place
        char tempStr[16];
        float displayTemp;
        
        if (settings.useCelsius) {
            displayTemp = temp / 100.0f;  // Convert from fixed point to float
        } else {
            displayTemp = celsiusToFahrenheit(temp) / 100.0f;  // Convert to Fahrenheit and to float
        }
        
        // Format with whole numbers and unit
        sprintf(tempStr, "%.0f%c", displayTemp, settings.useCelsius ? 'C' : 'F');
        
        // Set text properties
        tempSprite->setTextSize(4);
        tempSprite->setTextDatum(middle_center);
        
        // Choose color based on temperature in Celsius
        float tempC = settings.useCelsius ? displayTemp : (displayTemp - 32.0f) * 5.0f / 9.0f;
        uint32_t tempColor;
        if (tempC < 20) tempColor = DisplayConfig::COLOR_COLD;
        else if (tempC > 35) tempColor = DisplayConfig::COLOR_HOT;
        else tempColor = DisplayConfig::COLOR_GOOD;
        
        tempSprite->setTextColor(tempColor);
        tempSprite->drawString(tempStr, boxWidth/2, boxHeight/2);
        
        // Push sprite to display
        tempSprite->pushSprite(MARGIN, boxY);
        
        lastTemp = temp;
        lastUpdate = millis();
    }
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
    
    // Draw battery status
    drawBatteryStatus();
    
    // Update status display if not monitoring
    if (!state.isMonitoring) {
        updateStatusDisplay("Ready", DisplayConfig::COLOR_TEXT);
    }
}

bool selectTemperatureUnit() {
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    CoreS3.Display.setTextDatum(top_center);  // Set text alignment to center
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.drawString("Select Unit", CoreS3.Display.width()/2, 40);
    
    // Landscape-optimized button positions with enabled field
    Button celsiusBtn = {20, 80, 130, 100, "C", false, true};    // Left side
    Button fahrenheitBtn = {170, 80, 130, 100, "F", false, true}; // Right side
    
    // Initial button draw
    drawButton(celsiusBtn, DisplayConfig::COLOR_BUTTON);
    drawButton(fahrenheitBtn, DisplayConfig::COLOR_BUTTON);
    
    // Draw large C and F text
    CoreS3.Display.setTextSize(4);  // Larger text for C/F
    CoreS3.Display.drawString("C", celsiusBtn.x + celsiusBtn.w/2, celsiusBtn.y + celsiusBtn.h/2);
    CoreS3.Display.drawString("F", fahrenheitBtn.x + fahrenheitBtn.w/2, fahrenheitBtn.y + fahrenheitBtn.h/2);
    
    // Add descriptive text below buttons
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Celsius", celsiusBtn.x + celsiusBtn.w/2, celsiusBtn.y + celsiusBtn.h + 20);
    CoreS3.Display.drawString("Fahrenheit", fahrenheitBtn.x + fahrenheitBtn.w/2, fahrenheitBtn.y + fahrenheitBtn.h + 20);
    
    while (true) {
        CoreS3.update();
        if (CoreS3.Touch.getCount()) {
            auto touched = CoreS3.Touch.getDetail();
            if (touched.wasPressed()) {
                if (touchInButton(celsiusBtn, touched.x, touched.y)) {
                    // Highlight button
                    drawButton(celsiusBtn, DisplayConfig::COLOR_BUTTON_ACTIVE);
                    CoreS3.Display.setTextSize(4);
                    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
                    CoreS3.Display.drawString("C", celsiusBtn.x + celsiusBtn.w/2, celsiusBtn.y + celsiusBtn.h/2);
                    if (settings.soundEnabled) {
                        CoreS3.Speaker.tone(1200, 50);
                        delay(50);
                        CoreS3.Speaker.tone(1400, 50);
                    }
                    delay(150);  // Visual feedback
                    return true;
                }
                if (touchInButton(fahrenheitBtn, touched.x, touched.y)) {
                    // Highlight button
                    drawButton(fahrenheitBtn, DisplayConfig::COLOR_BUTTON_ACTIVE);
                    CoreS3.Display.setTextSize(4);
                    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
                    CoreS3.Display.drawString("F", fahrenheitBtn.x + fahrenheitBtn.w/2, fahrenheitBtn.y + fahrenheitBtn.h/2);
                    if (settings.soundEnabled) {
                        CoreS3.Speaker.tone(1200, 50);
                        delay(50);
                        CoreS3.Speaker.tone(1400, 50);
                    }
                    delay(150);  // Visual feedback
                    return false;
                }
            }
        }
        delay(10);
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
        if (settings.soundEnabled) {
            int frequency = 500 + (i * 100);  // Frequency increases with each segment
            CoreS3.Speaker.tone(frequency, 50);
        }
    }
    
    // Victory sound sequence
    if (settings.soundEnabled) {
        CoreS3.Speaker.setVolume(128);
        CoreS3.Speaker.tone(784, 100);  // G5
        delay(100);
        CoreS3.Speaker.tone(988, 100);  // B5
        delay(100);
        CoreS3.Speaker.tone(1319, 200); // E6
        delay(200);
        CoreS3.Speaker.tone(1047, 400); // C6
        delay(400);
    }
    
    delay(500);
    
    // Initialize temperature unit selection
    settings.useCelsius = selectTemperatureUnit();
    preferences.putBool("useCelsius", settings.useCelsius);  // Save the selection immediately
    
    state.isMonitoring = false;
    
    // Initialize display interface
    drawInterface();
    ncir2.setLEDColor(0);
    drawBatteryStatus();
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
        state.currentTemp = ncir2.getTempValue();
        updateTemperatureDisplay(state.currentTemp);
        updateTemperatureTrend();
        handleTemperatureAlerts();
        state.lastTempUpdate = currentMillis;
    }
    
    // Handle auto sleep
    if (settings.autoSleep) {
        handleAutoSleep(currentMillis, state.lastActivityTime);
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

void toggleMonitoring() {
    state.isMonitoring = !state.isMonitoring;
    if (state.isMonitoring) {
        handleTemperatureAlerts();
    } else {
        updateStatusDisplay("Ready", DisplayConfig::COLOR_TEXT);
    }
}

void handleAutoSleep(unsigned long currentMillis, unsigned long lastActivityTime) {
    if (currentMillis - lastActivityTime >= (settings.sleepTimeout * 60 * 1000)) {
        enterSleepMode();
    }
}

void updateTemperatureTrend() {
    // Update temperature trend
    state.tempHistory[state.historyIndex] = state.currentTemp;
    state.historyIndex = (state.historyIndex + 1) % 5;
}

void checkBatteryWarning(unsigned long currentMillis) {
    if (currentMillis - state.lastBatteryUpdate >= TimeConfig::BATTERY_CHECK) {
        int bat_level = CoreS3.Power.getBatteryLevel();
        if (bat_level <= 20 && !state.lowBatteryWarningShown) {
            showLowBatteryWarning(bat_level);
            state.lowBatteryWarningShown = true;
        } else if (bat_level > 20) {
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

void showWiFiSetup() {
    // Clear screen
    CoreS3.Display.fillScreen(DisplayConfig::COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(DisplayConfig::COLOR_TEXT);
    
    // Show WiFi status
    CoreS3.Display.setTextDatum(top_center);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("WiFi Setup", CoreS3.Display.width()/2, 20);
    
    // Draw back button
    int buttonWidth = 100;
    int buttonHeight = 40;
    int buttonX = (CoreS3.Display.width() - buttonWidth) / 2;
    int buttonY = CoreS3.Display.height() - buttonHeight - 20;
    
    CoreS3.Display.fillRoundRect(buttonX, buttonY, buttonWidth, buttonHeight, 5, DisplayConfig::COLOR_BUTTON);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.drawString("Back", buttonX + buttonWidth/2, buttonY + buttonHeight/2);
    
    // Wait for touch to return
    while (!CoreS3.Touch.getDetail().wasPressed()) {
        delay(10);
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
                        playTouchSound();
                    }
                } else if (touchInButton(downBtn, touched.x, touched.y)) {
                    if (tempEmissivity > EMISSIVITY_MIN) {
                        tempEmissivity -= EMISSIVITY_STEP;
                        if (tempEmissivity < EMISSIVITY_MIN) tempEmissivity = EMISSIVITY_MIN;
                        playTouchSound();
                    }
                } else if (touchInButton(doneBtn, touched.x, touched.y)) {
                    playTouchSound();
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

void handleTouchInput() {
    auto t = CoreS3.Touch.getDetail();
    if (t.wasPressed()) {
        state.lastActivityTime = millis();
        
        if (state.inSettingsMenu) {
            handleSettingsMenu();
        } else {
            if (touchInButton(monitorBtn, t.x, t.y)) {
                playTouchSound();
                toggleMonitoring();
            } else if (touchInButton(settingsBtn, t.x, t.y)) {
                playTouchSound();
                enterSettingsMenu();
            }
        }
    }
}

void playTouchSound(bool success) {
    if (!settings.soundEnabled) return;
    
    if (success) {
        CoreS3.Speaker.begin();
        CoreS3.Speaker.setVolume(255);
        CoreS3.Speaker.tone(880, 40);  // Higher pitch for positive feedback
        delay(50);
        CoreS3.Speaker.tone(1760, 20); // Even higher pitch for confirmation
        CoreS3.Speaker.end();
    } else {
        CoreS3.Speaker.begin();
        CoreS3.Speaker.setVolume(255);
        CoreS3.Speaker.tone(220, 100); // Lower pitch for negative feedback
        CoreS3.Speaker.end();
    }
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

void exitSettingsMenu() {
    state.inSettingsMenu = false;
    state.selectedMenuItem = -1;
    state.settingsScrollPosition = 0;
    drawInterface();
}