#include <ArduinoOTA.h>

#include <WiFi.h>
#include <Wire.h>
#include <M5GFX.h>
#include <M5CoreS3.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include "thingProperties.h"  // Include the thingProperties.h file

// Initialize objects
Preferences preferences;

// Configuration namespace
namespace Config {
    // Display settings
    namespace Display {
        const uint32_t COLOR_BACKGROUND = TFT_BLACK;
        const uint32_t COLOR_TEXT = TFT_WHITE;
        const uint32_t COLOR_PRIMARY = 0x3CD070;    // Main theme color (bright green)
        const uint32_t COLOR_SECONDARY = 0x1B3358;  // Secondary color (muted blue)
        const uint32_t COLOR_WARNING = 0xFFB627;    // Warning color (amber)
        const uint32_t COLOR_ERROR = 0xFF0F7B;      // Error color (pink)
        const uint32_t COLOR_SUCCESS = 0x3CD070;    // Success color (green)
        const uint32_t COLOR_ICE = 0x87CEFA;     // Light blue for too cold
        const uint32_t COLOR_READY = 0x00FF00;   // Neon green for ready
        const uint32_t COLOR_LAVA = 0xFF4500;    // Bright red-orange for way too hot
        const uint32_t COLOR_BUTTON = 0x4444FF;  // Light blue
        const uint32_t COLOR_BUTTON_ACTIVE = 0x00FF00;  // Green 
        
        // Layout constants
        const int HEADER_HEIGHT = 30;
        const int TEMP_BOX_HEIGHT = 70;
        const int FOOTER_HEIGHT = 40;
        const int STATUS_HEIGHT = 40;
        const int MARGIN = 10;
        const int BUTTON_HEIGHT = 50;
    }
    
    // Temperature settings
    namespace Temperature {
        const float MIN_TEMP_C = -20;   // Minimum valid temperature
        const float MAX_TEMP_C = 400;   // Maximum valid temperature
        const float DEFAULT_TARGET_C = 180.0;
        const float DEFAULT_TOLERANCE_C = 5.0;
        const unsigned long UPDATE_INTERVAL_MS = 500;  // Changed to 0.5 seconds for more live readings
        const float TEMP_THRESHOLD = 0.5;          // Minimum change to update display
        const int TEMP_DISPLAY_THRESHOLD = 100;    // For integer comparison (TEMP_THRESHOLD * 100) 
       
        // Temperature ranges in Celsius
        const int16_t TEMP_MIN_C = 304;     // 304°C (580°F)
        const int16_t TEMP_MAX_C = 371;     // 371°C (700°F)
        const int16_t TEMP_TARGET_C = 338;  // 338°C (640°F)
        const int16_t TEMP_TOLERANCE_C = 5;  // 5°C

        // Temperature ranges in Fahrenheit
        const int16_t TEMP_MIN_F = 580;     // 580°F
        const int16_t TEMP_MAX_F = 700;     // 700°F
        const int16_t TEMP_TARGET_F = 640;  // 640°F
        const int16_t TEMP_TOLERANCE_F = 9;  // 9°F
    }
    
    // System settings
    namespace System {
        const unsigned long WATCHDOG_TIMEOUT_MS = 30000;
        const unsigned long BATTERY_CHECK_INTERVAL_MS = 5000;
        const int DEFAULT_BRIGHTNESS = 128;
        const float DEFAULT_EMISSIVITY = 0.87;
    }
}

// Settings structure
struct Settings {
    bool useCelsius = true;
    bool soundEnabled = true;
    bool cloudEnabled = true;  // New setting for cloud connectivity
    int brightness = Config::System::DEFAULT_BRIGHTNESS;
    float emissivity = Config::System::DEFAULT_EMISSIVITY;
    float targetTemp = Config::Temperature::DEFAULT_TARGET_C;
    float tempTolerance = Config::Temperature::DEFAULT_TOLERANCE_C;
    
    void load() {
        preferences.begin("terpmeter", false);
        useCelsius = preferences.getBool("useCelsius", true);
        soundEnabled = preferences.getBool("soundEnabled", true);
        cloudEnabled = preferences.getBool("cloudEnabled", true);  // Load cloud setting
        brightness = preferences.getInt("brightness", Config::System::DEFAULT_BRIGHTNESS);
        if (brightness < 25) brightness = Config::System::DEFAULT_BRIGHTNESS;  // Prevent complete darkness
        emissivity = preferences.getFloat("emissivity", Config::System::DEFAULT_EMISSIVITY);
        targetTemp = preferences.getFloat("targetTemp", Config::Temperature::DEFAULT_TARGET_C);
        tempTolerance = preferences.getFloat("tolerance", Config::Temperature::DEFAULT_TOLERANCE_C);
        preferences.end();
    }
    
    void save() {
        preferences.begin("terpmeter", false);
        preferences.putBool("useCelsius", useCelsius);
        preferences.putBool("soundEnabled", soundEnabled);
        preferences.putBool("cloudEnabled", cloudEnabled);  // Save cloud setting
        preferences.putInt("brightness", brightness);
        preferences.putFloat("emissivity", emissivity);
        preferences.putFloat("targetTemp", targetTemp);
        preferences.putFloat("tolerance", tempTolerance);
        preferences.end();
    }
};

// System state structure
struct SystemState {
    bool isMonitoring = false;
    int32_t currentTemp = 0;  // Changed from float to int32_t
    static int32_t lastDisplayTemp;  // Remove initialization here
    unsigned long lastTempUpdate = 0;
    unsigned long lastSerialUpdate = 0;
    unsigned long lastBatteryCheck = 0;
    bool lowBatteryWarning = false;
    String statusMessage;
    uint32_t statusColor = Config::Display::COLOR_TEXT;
    enum Screen { MAIN, SETTINGS } currentScreen = MAIN;
    bool useCelsius = true;  // Move useCelsius here
};
// Settings menu state
struct SettingsMenu {
    bool isOpen = false;
    int selectedItem = 0;
    const int numItems = 4;  // Increased from 3 to 4
    const char* menuItems[4] = {  // Changed array size from 3 to 4
        "Sound",
        "Brightness",
        "Emissivity",
        "Cloud"
    };
};

int32_t SystemState::lastDisplayTemp = -999;  // Initialize the static member



// Global instances
Settings settings;
SystemState state;
SettingsMenu settingsMenu;  

// Forward declarations
void playSound(bool success);
void draw3DButton(int x, int y, int w, int h, const char* text, bool isSelected, bool isPressed);
void handleBrightnessButtons(int touchX, int touchY);
void drawBrightnessControls();
bool selectTemperatureUnit();
void handleSettingsTouch(int x, int y);
void updateStatus(const String& message, uint32_t color);
void drawToggleSwitch(int x, int y, bool state);
void drawTemperature();  // Add the new function declaration

// UI Components
class Button {
public:
    int x, y, width, height;
    String label;
    bool enabled;
    bool pressed;
    bool isToggle;
    bool toggleState;
    
    Button(int _x, int _y, int _w, int _h, String _label, bool _enabled = true, bool _isToggle = false)
        : x(_x), y(_y), width(_w), height(_h), label(_label), enabled(_enabled), 
          pressed(false), isToggle(_isToggle), toggleState(false) {}
    
    bool contains(int touch_x, int touch_y) {
        return enabled && 
               touch_x >= x && touch_x < x + width &&
               touch_y >= y && touch_y < y + height;
    }
    
    void draw(uint32_t color = Config::Display::COLOR_SECONDARY) {
        if (!enabled) {
            color = Config::Display::COLOR_SECONDARY;
        } else if (isToggle && toggleState) {
            color = Config::Display::COLOR_PRIMARY;
        }
        
        // Draw button background with pressed effect
        if (pressed) {
            // Draw darker background when pressed
            uint32_t pressedColor = (color == Config::Display::COLOR_PRIMARY) ? 
                                  Config::Display::COLOR_READY : 
                                  Config::Display::COLOR_WARNING;
            CoreS3.Display.fillRoundRect(x, y, width, height, 8, pressedColor);
            
            // Draw slightly offset text for 3D effect
            CoreS3.Display.setTextSize(2);  // Increased size for main menu buttons
            CoreS3.Display.setTextDatum(middle_center);
            CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
            CoreS3.Display.drawString(label.c_str(), x + width/2 + 1, y + height/2 + 1);
        } else {
            // Normal button drawing
            CoreS3.Display.fillRoundRect(x, y, width, height, 8, color);
            
            // Draw button border
            CoreS3.Display.drawRoundRect(x, y, width, height, 8, Config::Display::COLOR_TEXT);
            
            // Draw label
            CoreS3.Display.setTextSize(2);  // Increased size for main menu buttons
            CoreS3.Display.setTextDatum(middle_center);
            CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
            CoreS3.Display.drawString(label.c_str(), x + width/2, y + height/2);
        }
    }
    
    void setPressed(bool isPressed) {
        if (pressed != isPressed) {
            pressed = isPressed;
            if (pressed && settings.soundEnabled) {
                playSound(true);
            }
            draw(isToggle && toggleState ? Config::Display::COLOR_PRIMARY : Config::Display::COLOR_SECONDARY);
        }
    }
    
    void setToggleState(bool state) {
        if (toggleState != state) {
            toggleState = state;
            draw();
        }
    }
};

// Function declarations
void updateDisplay();
void handleTouch();
bool checkBattery();
void checkTemperature();
void drawHeader();
void drawFooter();
void drawMainDisplay();
void drawSettingsMenu();
void adjustEmissivity();
void checkStack();
bool isLowMemory();
void printMemoryInfo();
void debugLog(const char* function, const char* message);
void printDebugInfo();
bool checkBattery();
void updateTemperatureDisplay(int32_t temp);
int16_t celsiusToFahrenheit(int16_t celsius);
int16_t fahrenheitToCelsius(int16_t fahrenheit);

int16_t fahrenheitToCelsius(int16_t fahrenheit) {
    return (fahrenheit - 32) * 5 / 9;
}
int16_t celsiusToFahrenheit(int16_t celsius) {
    return (celsius * 9 / 5) + 32;
}
void updateTemperatureDisplay(int32_t temp) {
    // Clear temperature area with padding
    int tempBoxY = Config::Display::HEADER_HEIGHT + Config::Display::MARGIN;
    CoreS3.Display.fillRect(Config::Display::MARGIN + 5, tempBoxY + 5, 
                          CoreS3.Display.width() - (Config::Display::MARGIN * 2) - 10, 
                          Config::Display::TEMP_BOX_HEIGHT - 10, Config::Display::COLOR_BACKGROUND);
    
    // Convert and format temperature
    float displayTemp = temp / 100.0;
    char tempStr[32];
    
    if (state.useCelsius) {  // Use state.useCelsius instead of global
        sprintf(tempStr, "%dC", (int)round(displayTemp));
    } else {
        float tempF = (displayTemp * 9.0/5.0) + 32.0;
        sprintf(tempStr, "%dF", (int)round(tempF));
    }
    
    // Display temperature with larger text
    CoreS3.Display.setTextSize(5);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString(tempStr, CoreS3.Display.width()/2, tempBoxY + (Config::Display::TEMP_BOX_HEIGHT/2));
}
void checkTemperature() {
    float temp = readTemperature();
    if (isValidTemperature(temp)) {
        state.currentTemp = temp;
    }
}
void checkStack() {
    char stack;
    debugLog("Stack", String("Stack pointer: " + String((uint32_t)&stack, HEX)).c_str());
}
bool isLowMemory() {
    return ESP.getFreeHeap() < 10000; // Adjust threshold as needed
}
void printMemoryInfo() {
    debugLog("Memory", String("Free Heap: " + String(ESP.getFreeHeap()) + " bytes").c_str());
    debugLog("Memory", String("Largest Free Block: " + String(ESP.getMaxAllocHeap()) + " bytes").c_str());
}
void debugLog(const char* function, const char* message) {
    Serial.printf("[DEBUG] %s: %s\n", function, message);
    Serial.flush(); // Ensure the message is sent before any potential crash
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
bool isValidTemperature(float temp) {
    // Check if temperature is within reasonable bounds (-20°C to 200°C)
    return temp > -2000 && temp < 20000;  // Values are in centidegrees
}
void setup() {
    Serial.begin(115200);
    
    // Initialize Core S3 with configuration
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    
    // Initialize power management
    CoreS3.Power.begin();
    CoreS3.Power.setChargeCurrent(900);
    CoreS3.Power.setExtOutput(true);  // Set to false when using Grove port
    delay(500);  // Give power time to stabilize
    
    esp_task_wdt_init(Config::System::WATCHDOG_TIMEOUT_MS / 1000, true);
    esp_task_wdt_add(NULL);
    debugLog("setup", "Watchdog initialized with 10s timeout");
    
    // Load settings first
    settings.load();
    state.useCelsius = settings.useCelsius;  // Sync temperature unit setting to state
    
    // Initialize cloud connection if enabled
    if (settings.cloudEnabled) {
        WiFi.begin("Wack House", "justice69");
        ArduinoCloud.begin(ArduinoIoTPreferredConnection);
    }
    
    // Initialize display settings
    CoreS3.Display.setRotation(1);
    CoreS3.Display.setBrightness(settings.brightness);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);

    // Initialize I2C
    Wire.begin(2, 1, 400000);
    delay(100);  // Wait for I2C to stabilize
    
    // Show temperature unit selection on first boot
    if (!preferences.getBool("unitSelected", false)) {
        CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
        settings.useCelsius = selectTemperatureUnit();
        preferences.begin("terpmeter", false);
        preferences.putBool("unitSelected", true);
        preferences.end();
        settings.save();
    }
    
    // Initialize display for normal operation
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.setRotation(1);
    CoreS3.Display.setBrightness(settings.brightness);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.setTextSize(1);
    
    // Draw initial display
    updateDisplay();

    // Initialize Arduino IoT Cloud
    initProperties();
    ArduinoCloud.begin(ArduinoIoTPreferredConnection);
    setDebugMessageLevel(4);
    ArduinoCloud.printDebugInfo();
}
void handleCloudConnectivity() {
    static bool lastCloudState = true;
    
    // If cloud state has changed
    if (lastCloudState != settings.cloudEnabled) {
        if (settings.cloudEnabled) {
            // Reconnect to Arduino Cloud
            WiFi.begin("Wack House", "justice69");
            updateStatus("Cloud Connecting...", Config::Display::COLOR_WARNING);
            ArduinoCloud.begin(ArduinoIoTPreferredConnection);
            updateStatus("Cloud Connected", Config::Display::COLOR_SUCCESS);
        } else {
            // Disconnect from Arduino Cloud
            WiFi.disconnect(true);
            updateStatus("Cloud Disabled", Config::Display::COLOR_WARNING);
        }
        lastCloudState = settings.cloudEnabled;
    }
}
void loop() {
    static unsigned long lastDebugTime = 0;
    static unsigned long lastUpdate = 0;
    
    // Update M5Stack core
    CoreS3.update();
    esp_task_wdt_reset();
    
    // Handle cloud connectivity
    handleCloudConnectivity();
    if (settings.cloudEnabled) {
        ArduinoCloud.update();
    }
    
    unsigned long currentMillis = millis();
    
    // Add new temperature update logic
    if (currentMillis - lastUpdate >= 100) {  // Check every 100ms
        float newTemp = readTemperature();  // Now returns centidegrees
        int32_t newTempInt = (int32_t)newTemp;  // Convert to int32_t
        
        if (abs(newTempInt - state.lastDisplayTemp) >= (Config::Temperature::TEMP_THRESHOLD * 100)) {
            state.currentTemp = newTempInt;  // Store raw Celsius value in centidegrees
            updateTemperatureDisplay(state.currentTemp);  // This function handles the conversion if needed
            state.lastDisplayTemp = newTempInt;
            
            // Update Arduino Cloud if enabled
            if (settings.cloudEnabled) {
                float tempToSend = newTempInt / 100.0;  // Convert to actual temperature
                if (!state.useCelsius) {
                    tempToSend = (tempToSend * 9.0/5.0) + 32.0;  // Convert to Fahrenheit if needed
                }
                temperature = tempToSend;
            }
        }
        lastUpdate = currentMillis;
    }
    
    // Check battery status at regular intervals
    if (currentMillis - state.lastBatteryCheck >= Config::System::BATTERY_CHECK_INTERVAL_MS) {
        bool batteryStatusChanged = checkBattery();
        if (batteryStatusChanged) {
            updateDisplay();
        }
        debugLog("loop", "Updating battery status");
        state.lastBatteryCheck = currentMillis;
    }

    // Handle touch events
    if (CoreS3.Touch.getCount()) {
        handleTouch();
        updateDisplay();
    }
    
    delay(10);  // Short delay to prevent overwhelming the processor
}
void drawMainDisplay() {
    int contentY = Config::Display::HEADER_HEIGHT + Config::Display::MARGIN;
    int contentHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT - 2*Config::Display::MARGIN;
    
    // Clear the main display area except for temperature
    if (state.isMonitoring) {
        // Only clear the bottom half when monitoring
        CoreS3.Display.fillRect(
            Config::Display::MARGIN,
            contentY + contentHeight/2,
            CoreS3.Display.width() - 2*Config::Display::MARGIN,
            contentHeight/2,
            Config::Display::COLOR_BACKGROUND
        );
    }
    
    // Draw monitoring section if active
    if (state.isMonitoring) {
        // Draw box around monitoring area
        CoreS3.Display.drawRoundRect(
            Config::Display::MARGIN,
            contentY + contentHeight/2,
            CoreS3.Display.width() - 2*Config::Display::MARGIN,
            contentHeight/2,
            8,
            Config::Display::COLOR_TEXT
        );
        
        // Add monitoring info here
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
        
        // Draw target temperature
        char targetStr[20];
        float targetTemp = settings.useCelsius ? settings.targetTemp : (settings.targetTemp * 9.0/5.0) + 32.0;
        sprintf(targetStr, "Target: %d%c", (int)round(targetTemp), settings.useCelsius ? 'C' : 'F');
        CoreS3.Display.drawString(targetStr, CoreS3.Display.width()/2, contentY + contentHeight*3/4);
    }
}
void drawFooter() {
    int footerY = CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT;
    
    // Draw footer background
    CoreS3.Display.fillRect(0, footerY, CoreS3.Display.width(), Config::Display::FOOTER_HEIGHT, 
                          Config::Display::COLOR_SECONDARY);
    
    // Calculate button dimensions
    int buttonWidth = (CoreS3.Display.width() - 3 * Config::Display::MARGIN) / 2;
    int buttonY = footerY + (Config::Display::FOOTER_HEIGHT - Config::Display::BUTTON_HEIGHT) / 2;
    
    // Draw Monitor/Stop button
    Button monitorBtn(Config::Display::MARGIN, 
                     buttonY,
                     buttonWidth, 
                     Config::Display::BUTTON_HEIGHT - 4,
                     state.isMonitoring ? "Stop" : "Monitor",
                     true);
    monitorBtn.toggleState = state.isMonitoring;
    monitorBtn.draw();
    
    // Draw Settings/Back button
    Button settingsBtn(CoreS3.Display.width() - buttonWidth - Config::Display::MARGIN,
                      buttonY,
                      buttonWidth,
                      Config::Display::BUTTON_HEIGHT - 4,
                      settingsMenu.isOpen ? "Back" : "Settings");
    settingsBtn.draw();
}

void updateDisplay() {
    static bool lastSettingsState = false;
    static float lastTemp = 0;
    static String lastStatus = "";
    static uint32_t lastStatusColor = 0;
    static bool lastMonitorState = false;
    
    bool fullRedraw = false;
    
    // Check if we need a full redraw
    if (lastSettingsState != settingsMenu.isOpen || 
        lastMonitorState != state.isMonitoring) {
        fullRedraw = true;
        lastSettingsState = settingsMenu.isOpen;
        lastMonitorState = state.isMonitoring;
    }
    
    // Draw header and footer
    drawHeader();
    drawFooter();
    
    // Update main content
    if (settingsMenu.isOpen) {
        if (fullRedraw) {
            drawSettingsMenu();
        }
    } else {
        // Only redraw temperature if it changed significantly or full redraw needed
        if (fullRedraw || abs(lastTemp - state.currentTemp) >= 0.5) {
            drawMainDisplay();
            lastTemp = state.currentTemp;
        }
    }
    
    // Update status if changed
    if (fullRedraw || lastStatus != state.statusMessage || lastStatusColor != state.statusColor) {
        updateStatus(state.statusMessage, state.statusColor);
        lastStatus = state.statusMessage;
        lastStatusColor = state.statusColor;
    }
}
void handleTouch() {
    auto touch = CoreS3.Touch.getDetail();
    if (!touch.wasPressed()) return;
    
    // Handle footer buttons
    int footerY = CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT;
    if (touch.y >= footerY) {
        int buttonWidth = (CoreS3.Display.width() - 3 * Config::Display::MARGIN) / 2;
        
        // Monitor button (left)
        if (!settingsMenu.isOpen && 
            touch.x >= Config::Display::MARGIN && 
            touch.x < Config::Display::MARGIN + buttonWidth) {
            
            state.isMonitoring = !state.isMonitoring;
            if (settings.soundEnabled) playSound(true);
            updateStatus(state.isMonitoring ? "Monitoring" : "Stopped", 
                        state.isMonitoring ? Config::Display::COLOR_SUCCESS : Config::Display::COLOR_WARNING);
        }
        // Settings/Back button (right)
        else if (touch.x >= CoreS3.Display.width() - buttonWidth - Config::Display::MARGIN && 
                 touch.x < CoreS3.Display.width() - Config::Display::MARGIN) {
            
            if (settings.soundEnabled) playSound(true);
            settingsMenu.isOpen = !settingsMenu.isOpen;
            state.currentScreen = settingsMenu.isOpen ? SystemState::SETTINGS : SystemState::MAIN;
        }
    }
    // Handle settings menu touches
    else if (settingsMenu.isOpen) {
        handleSettingsTouch(touch.x, touch.y);
    }
}
void updateStatus(const String& message, uint32_t color) {
    // Only update if message or color has changed
    if (message != state.statusMessage || color != state.statusColor) {
        state.statusMessage = message;
        state.statusColor = color;
        
        // Update status display area
        int statusY = CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT - Config::Display::STATUS_HEIGHT;
        
        // Draw status background
        CoreS3.Display.fillRect(Config::Display::MARGIN, statusY,
                              CoreS3.Display.width() - 2 * Config::Display::MARGIN,
                              Config::Display::STATUS_HEIGHT,
                              Config::Display::COLOR_SECONDARY);
                              
        // Draw status highlight
        CoreS3.Display.fillRect(Config::Display::MARGIN + 4, statusY + 4,
                              CoreS3.Display.width() - 2 * Config::Display::MARGIN - 8,
                              Config::Display::STATUS_HEIGHT - 8,
                              Config::Display::COLOR_BACKGROUND);
        
        // Draw status text
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextSize(1);
        CoreS3.Display.setTextColor(color);
        CoreS3.Display.drawString(message.c_str(), 
                                CoreS3.Display.width() / 2, 
                                statusY + Config::Display::STATUS_HEIGHT / 2);
    }
}
float readTemperature() {
    uint16_t result;
    float temperature;
    
    Wire.beginTransmission(0x5A);  // MLX90614 I2C address
    Wire.write(0x07);  // RAM register for object temperature (not ambient)
    Wire.endTransmission(false);
    
    Wire.requestFrom(0x5A, 2);  // Request 2 bytes from the sensor
    if (Wire.available() >= 2) {
        result = Wire.read();        // Read low byte
        result |= Wire.read() << 8;  // Read high byte
        
        temperature = result * 0.02 - 273.15;  // Convert to Celsius
        
        // Debug output
        Serial.print("Raw result: ");
        Serial.print(result);
        Serial.print(", Temperature: ");
        Serial.println(temperature);
        
        if (isValidTemperature(temperature)) {
            return temperature * 100;  // Convert to centidegrees for system compatibility
        }
    }
    
    return -999.0;  // Return error value if reading fails
}
void playSound(bool success) {
    if (!settings.soundEnabled) return;
    
    CoreS3.Speaker.tone(success ? 1000 : 500, success ? 50 : 100);
    delay(success ? 50 : 100);
}
void drawHeader() {
    // Draw header background
    CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), Config::Display::HEADER_HEIGHT, Config::Display::COLOR_SECONDARY);
    
    // Draw title
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.setTextSize(2);  // Increased text size for better visibility
    CoreS3.Display.drawString("TerpMeter Pro", CoreS3.Display.width() / 2, Config::Display::HEADER_HEIGHT / 2);
    
    // Draw battery indicator
    int batteryLevel = CoreS3.Power.getBatteryLevel();
    bool isCharging = CoreS3.Power.isCharging();
    
    // Battery icon dimensions and position
    const int battW = 40;
    const int battH = 20;
    const int battX = CoreS3.Display.width() - battW - 10;  // 10 pixels from right edge
    const int battY = (Config::Display::HEADER_HEIGHT - battH) / 2;  // Centered vertically
    
    // Draw battery outline
    CoreS3.Display.drawRect(battX, battY, battW, battH, Config::Display::COLOR_TEXT);
    
    // Draw battery tip
    const int tipW = 4;
    const int tipH = 10;
    CoreS3.Display.fillRect(battX + battW, battY + (battH - tipH) / 2, tipW, tipH, Config::Display::COLOR_TEXT);
    
    // Draw battery level
    uint32_t battColor;
    if (batteryLevel <= 20) {
        battColor = Config::Display::COLOR_ERROR;  // Red when low
    } else if (batteryLevel <= 50) {
        battColor = Config::Display::COLOR_WARNING;  // Yellow when medium
    } else {
        battColor = Config::Display::COLOR_SUCCESS;  // Green when high
    }
    
    // Calculate fill width based on battery level
    int fillW = ((battW - 4) * batteryLevel) / 100;
    if (fillW > 0) {
        CoreS3.Display.fillRect(battX + 2, battY + 2, fillW, battH - 4, battColor);
    }
    
    // Draw charging indicator if charging
    if (isCharging) {
        CoreS3.Display.setTextSize(1);
        CoreS3.Display.setTextDatum(middle_right);
        CoreS3.Display.drawString("+", battX - 4, battY + battH/2);
    }
}
void drawSettingsMenu() {
    // Clear the content area first
    int contentY = Config::Display::HEADER_HEIGHT;
    int contentHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT;
    CoreS3.Display.fillRect(0, contentY, CoreS3.Display.width(), contentHeight, Config::Display::COLOR_BACKGROUND);
    
    // Draw title
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString("Settings", CoreS3.Display.width()/2, contentY + 20);
    
    // Draw brightness controls first
    drawBrightnessControls();
    
    // Draw other settings below brightness controls
    int menuY = contentY + 140;  // Start below brightness controls
    int itemHeight = 50;
    int toggleX = CoreS3.Display.width() - 60 - Config::Display::MARGIN;
    
    // Sound toggle
    CoreS3.Display.setTextDatum(middle_left);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString("Sound", Config::Display::MARGIN * 2, menuY + itemHeight/2);
    drawToggleSwitch(toggleX, menuY + (itemHeight - 30)/2, settings.soundEnabled);
    
    // Emissivity setting
    menuY += itemHeight;
    CoreS3.Display.drawString("Emissivity", Config::Display::MARGIN * 2, menuY + itemHeight/2);
    CoreS3.Display.setTextDatum(middle_right);
    CoreS3.Display.drawString(String(settings.emissivity, 2), toggleX + 50, menuY + itemHeight/2);
    
    // Cloud toggle
    menuY += itemHeight;
    CoreS3.Display.setTextDatum(middle_left);
    CoreS3.Display.drawString("Cloud", Config::Display::MARGIN * 2, menuY + itemHeight/2);
    drawToggleSwitch(toggleX, menuY + (itemHeight - 30)/2, settings.cloudEnabled);
}
void handleSettingsTouch(int x, int y) {
    int menuY = Config::Display::HEADER_HEIGHT + 140;  // Start below brightness controls
    int itemHeight = 50;
    int toggleX = CoreS3.Display.width() - 60 - Config::Display::MARGIN;
    
    // Check if touch is within menu area
    if (x >= Config::Display::MARGIN && 
        x <= CoreS3.Display.width() - Config::Display::MARGIN &&
        y >= menuY && y <= menuY + 3 * itemHeight) {
        
        // Calculate which item was touched
        int touchedItem = (y - menuY) / itemHeight;
        if (touchedItem >= 0 && touchedItem < 3) {
            settingsMenu.selectedItem = touchedItem;
            
            // Handle item selection
            switch (touchedItem) {
                case 0: // Sound
                    settings.soundEnabled = !settings.soundEnabled;
                    if (settings.soundEnabled) playSound(true);
                    break;
                case 1: // Emissivity
                    adjustEmissivity();
                    break;
                case 2: // Cloud
                    settings.cloudEnabled = !settings.cloudEnabled;
                    if (settings.cloudEnabled) playSound(true);
                    break;
            }
            
            settings.save();
            if (settings.soundEnabled) playSound(true);
            
            // Clear and redraw the settings menu
            int contentY = Config::Display::HEADER_HEIGHT;
            int contentHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT;
            CoreS3.Display.fillRect(0, contentY, CoreS3.Display.width(), contentHeight, Config::Display::COLOR_BACKGROUND);
            drawSettingsMenu();
        }
    }
    // Check for back button touch
    if (y > CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT) {
        settingsMenu.isOpen = false;
        settings.save();  // Save settings before leaving
        // Force a complete screen refresh
        CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), CoreS3.Display.height(), Config::Display::COLOR_BACKGROUND);
        updateDisplay();  // This will now clear the screen completely
        return;
    }

    // Only process menu items if settings menu is open
    if (!settingsMenu.isOpen) return;
}
void draw3DButton(int x, int y, int w, int h, const char* text, bool isSelected, bool isPressed) {
    // Button colors
    uint32_t baseColor = isSelected ? Config::Display::COLOR_PRIMARY : Config::Display::COLOR_SECONDARY;
    uint32_t shadowColor = CoreS3.Display.color565(0, 0, 0);  // Black for shadow
    uint32_t highlightColor = CoreS3.Display.color565(255, 255, 255);  // White for highlight
    
    // Adjust position if pressed
    if (isPressed) {
        x += 2;
        y += 2;
    }
    
    // Draw main button body
    CoreS3.Display.fillRoundRect(x, y, w, h, 5, baseColor);
    
    if (!isPressed) {
        // Draw 3D effect (highlight on top-left, shadow on bottom-right)
        CoreS3.Display.drawLine(x, y + h - 1, x + w - 1, y + h - 1, shadowColor);  // Bottom
        CoreS3.Display.drawLine(x + w - 1, y, x + w - 1, y + h - 1, shadowColor);  // Right
        CoreS3.Display.drawLine(x, y, x + w - 1, y, highlightColor);  // Top
        CoreS3.Display.drawLine(x, y, x, y + h - 1, highlightColor);  // Left
    }
    
    // Draw text
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(isSelected ? Config::Display::COLOR_BACKGROUND : Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString(text, x + w/2, y + h/2);
}
void handleBrightnessButtons(int touchX, int touchY) {
    static bool buttonPressed = false;
    static int pressedButton = -1;
    
    // Button dimensions and layout
    const int buttonWidth = 70;
    const int buttonHeight = 40;
    const int buttonSpacing = 10;
    const int startX = (CoreS3.Display.width() - (4 * buttonWidth + 3 * buttonSpacing)) / 2;
    const int startY = Config::Display::HEADER_HEIGHT + 60;
    
    // Define brightness levels
    const int brightnessLevels[] = {25, 50, 75, 100};
    
    // Check which button was pressed
    for (int i = 0; i < 4; i++) {
        int btnX = startX + i * (buttonWidth + buttonSpacing);
        int btnY = startY;
        
        if (touchX >= btnX && touchX < btnX + buttonWidth &&
            touchY >= btnY && touchY < btnY + buttonHeight) {
            
            // Play click sound if enabled
            if (settings.soundEnabled) {
                CoreS3.Speaker.tone(800, 50);  // 800Hz for 50ms
            }
            
            // Set brightness
            int brightness = (brightnessLevels[i] * 255) / 100;
            settings.brightness = brightness;
            CoreS3.Display.setBrightness(brightness);
            settings.save();
            
            // Visual feedback
            pressedButton = i;
            buttonPressed = true;
            return;
        }
    }
    
    buttonPressed = false;
    pressedButton = -1;
}
void drawBrightnessControls() {
    // Button dimensions and layout
    const int buttonWidth = 70;
    const int buttonHeight = 40;
    const int buttonSpacing = 10;
    const int startX = (CoreS3.Display.width() - (4 * buttonWidth + 3 * buttonSpacing)) / 2;
    const int startY = Config::Display::HEADER_HEIGHT + 60;
    
    // Draw "Brightness:" label
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString("Brightness:", CoreS3.Display.width()/2, startY - 20);
    
    // Define brightness levels
    const int brightnessLevels[] = {25, 50, 75, 100};
    
    // Draw buttons
    for (int i = 0; i < 4; i++) {
        int btnX = startX + i * (buttonWidth + buttonSpacing);
        char btnText[8];
        snprintf(btnText, sizeof(btnText), "%d%%", brightnessLevels[i]);
        
        // Calculate if this button represents the current brightness
        int currentBrightness = (settings.brightness * 100) / 255;
        bool isSelected = (currentBrightness > brightnessLevels[i] - 13 && 
                         currentBrightness <= brightnessLevels[i] + 12);
        
        draw3DButton(btnX, startY, buttonWidth, buttonHeight, btnText, isSelected, false);
    }
}
bool selectTemperatureUnit() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.setTextSize(3);
    
    // Draw title
    CoreS3.Display.drawString("Select Temperature Unit", CoreS3.Display.width()/2, 40);
    
    // Draw buttons
    int buttonWidth = 120;
    int buttonHeight = 60;
    int spacing = 40;
    int startY = CoreS3.Display.height()/2 - buttonHeight/2;
    
    // Draw Celsius button
    int celsiusX = CoreS3.Display.width()/2 - buttonWidth - spacing/2;
    CoreS3.Display.fillRoundRect(celsiusX, startY, buttonWidth, buttonHeight, 10, Config::Display::COLOR_SECONDARY);
    CoreS3.Display.drawString("°C", celsiusX + buttonWidth/2, startY + buttonHeight/2);
    
    // Draw Fahrenheit button
    int fahrenheitX = CoreS3.Display.width()/2 + spacing/2;
    CoreS3.Display.fillRoundRect(fahrenheitX, startY, buttonWidth, buttonHeight, 10, Config::Display::COLOR_SECONDARY);
    CoreS3.Display.drawString("°F", fahrenheitX + buttonWidth/2, startY + buttonHeight/2);
    
    // Wait for touch
    while (true) {
        CoreS3.update();
        if (CoreS3.Touch.getCount()) {
            auto touch = CoreS3.Touch.getDetail();
            if (touch.wasPressed()) {
                int x = touch.x;
                int y = touch.y;
                
                if (y >= startY && y <= startY + buttonHeight) {
                    if (x >= celsiusX && x <= celsiusX + buttonWidth) {
                        return true;
                    }
                    if (x >= fahrenheitX && x <= fahrenheitX + buttonWidth) {
                        return false;
                    }
                }
            }
        }
        delay(10);
    }
}
void adjustEmissivity() {
    const int buttonWidth = 80;
    const int buttonHeight = 40;
    const int buttonSpacing = 20;
    
    Button increaseBtn(CoreS3.Display.width() / 2 - buttonWidth - buttonSpacing,
                      CoreS3.Display.height() / 2,
                      buttonWidth, buttonHeight, "+");
    
    Button decreaseBtn(CoreS3.Display.width() / 2 + buttonSpacing,
                      CoreS3.Display.height() / 2,
                      buttonWidth, buttonHeight, "-");
    
    Button doneBtn(CoreS3.Display.width() / 2 - buttonWidth / 2,
                  CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT - buttonHeight - 20,
                  buttonWidth, buttonHeight, "Done");
    
    bool adjusting = true;
    while (adjusting) {
        CoreS3.update();
        
        // Clear display
        CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
        
        // Draw title
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.setTextDatum(top_center);
        CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
        CoreS3.Display.drawString("Adjust Emissivity", CoreS3.Display.width() / 2, 20);
        
        // Draw current value
        String emisStr = "E=" + String(settings.emissivity, 2);
        CoreS3.Display.setTextSize(3);
        CoreS3.Display.drawString(emisStr.c_str(), CoreS3.Display.width() / 2, 80);
        
        // Draw buttons
        increaseBtn.draw();
        decreaseBtn.draw();
        doneBtn.draw();
        
        if (CoreS3.Touch.getCount()) {
            auto touch = CoreS3.Touch.getDetail();
            if (touch.wasPressed()) {
                if (increaseBtn.contains(touch.x, touch.y)) {
                    increaseBtn.setPressed(true);
                    settings.emissivity = min(settings.emissivity + 0.01f, 1.0f);
                    settings.save();
                } else if (decreaseBtn.contains(touch.x, touch.y)) {
                    decreaseBtn.setPressed(true);
                    settings.emissivity = max(settings.emissivity - 0.01f, 0.1f);
                    settings.save();
                } else if (doneBtn.contains(touch.x, touch.y)) {
                    doneBtn.setPressed(true);
                    adjusting = false;
                }
            } else if (touch.wasReleased()) {
                increaseBtn.setPressed(false);
                decreaseBtn.setPressed(false);
                doneBtn.setPressed(false);
            }
        }
        delay(10);
    }
    
    // Redraw main display when done
    drawMainDisplay();
}
void drawToggleSwitch(int x, int y, bool state) {
    const int width = 50;
    const int height = 30;
    const int radius = height / 2;
    const int knobRadius = (height - 4) / 2;
    
    // Draw background
    uint32_t bgColor = state ? Config::Display::COLOR_SUCCESS : Config::Display::COLOR_SECONDARY;
    CoreS3.Display.fillRoundRect(x, y, width, height, radius, bgColor);
    
    // Draw knob
    int knobX = state ? x + width - 2 - knobRadius*2 : x + 2;
    CoreS3.Display.fillCircle(knobX + knobRadius, y + height/2, knobRadius, Config::Display::COLOR_BACKGROUND);
}
bool checkBattery() {
    int batteryLevel = CoreS3.Power.getBatteryLevel();
    bool isCharging = CoreS3.Power.isCharging();
    bool statusChanged = false;
    
    if (batteryLevel <= 10 && !isCharging) {
        if (!state.lowBatteryWarning) {
            updateStatus("Low Battery!", Config::Display::COLOR_WARNING);
            state.lowBatteryWarning = true;
            statusChanged = true;
        }
    } else if (state.lowBatteryWarning) {
        state.lowBatteryWarning = false;
        statusChanged = true;
    }
    
    return statusChanged;
}

void drawTemperature() {
    int contentY = Config::Display::HEADER_HEIGHT + Config::Display::MARGIN;
    int contentHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT - 2*Config::Display::MARGIN;
    
    if (state.isMonitoring) {
        contentHeight = contentHeight / 2;
    }
    
    // Clear only the temperature display area
    CoreS3.Display.fillRect(
        Config::Display::MARGIN, 
        contentY, 
        CoreS3.Display.width() - 2*Config::Display::MARGIN,
        contentHeight,
        Config::Display::COLOR_BACKGROUND
    );
    
    // Draw the box around temperature area
    CoreS3.Display.drawRoundRect(
        Config::Display::MARGIN, 
        contentY, 
        CoreS3.Display.width() - 2*Config::Display::MARGIN,
        contentHeight,
        8,
        Config::Display::COLOR_TEXT
    );
    
    // Draw current temperature
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(4);
    
    float displayTemp;
    char tempStr[10];
    if (settings.useCelsius) {
        displayTemp = state.currentTemp;
        sprintf(tempStr, "%d°C", (int)round(displayTemp));
    } else {
        displayTemp = (state.currentTemp * 9.0/5.0) + 32.0;
        sprintf(tempStr, "%d°F", (int)round(displayTemp));
    }
    
    uint32_t tempColor = state.isMonitoring ? 
        (abs(state.currentTemp - settings.targetTemp) <= settings.tempTolerance ? 
            Config::Display::COLOR_SUCCESS : Config::Display::COLOR_WARNING) :
        Config::Display::COLOR_TEXT;
    
    CoreS3.Display.setTextColor(tempColor);
    CoreS3.Display.drawString(tempStr, CoreS3.Display.width()/2, contentY + contentHeight/3);
    
    // Draw emissivity below temperature
    CoreS3.Display.setTextSize(2);
    char emissStr[20];
    sprintf(emissStr, "E=%.2f", settings.emissivity);
    CoreS3.Display.drawString(emissStr, CoreS3.Display.width()/2, contentY + contentHeight*2/3);
}