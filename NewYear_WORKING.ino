#include <Wire.h>
#include <M5GFX.h>
#include <M5CoreS3.h>
#include <M5Unified.h>
#include <M5UNIT_NCIR2.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

// Initialize objects
M5UNIT_NCIR2 ncir2;
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
        
        // Layout constants
        const int HEADER_HEIGHT = 30;
        const int FOOTER_HEIGHT = 40;
        const int MARGIN = 10;
        const int BUTTON_HEIGHT = 50;
        const int STATUS_HEIGHT = 40;
    }
    
    // Temperature settings
    namespace Temperature {
        const float MIN_TEMP_C = 0.0;
        const float MAX_TEMP_C = 300.0;
        const float DEFAULT_TARGET_C = 180.0;
        const float DEFAULT_TOLERANCE_C = 5.0;
        const unsigned long UPDATE_INTERVAL_MS = 100;
        const int HISTORY_SIZE = 10;
    }
    
    // System settings
    namespace System {
        const unsigned long WATCHDOG_TIMEOUT_MS = 30000;
        const unsigned long BATTERY_CHECK_INTERVAL_MS = 5000;
        const unsigned long SLEEP_TIMEOUT_MS = 300000;  // 5 minutes
        const int DEFAULT_BRIGHTNESS = 128;
        const float DEFAULT_EMISSIVITY = 0.87;
    }
}

// Settings structure
struct Settings {
    bool useCelsius = true;
    bool soundEnabled = true;
    int brightness = Config::System::DEFAULT_BRIGHTNESS;
    float emissivity = Config::System::DEFAULT_EMISSIVITY;
    float targetTemp = Config::Temperature::DEFAULT_TARGET_C;
    float tempTolerance = Config::Temperature::DEFAULT_TOLERANCE_C;
    
    void load() {
        preferences.begin("terpmeter", false);
        useCelsius = preferences.getBool("useCelsius", true);
        soundEnabled = preferences.getBool("soundEnabled", true);
        brightness = preferences.getInt("brightness", Config::System::DEFAULT_BRIGHTNESS);
        emissivity = preferences.getFloat("emissivity", Config::System::DEFAULT_EMISSIVITY);
        targetTemp = preferences.getFloat("targetTemp", Config::Temperature::DEFAULT_TARGET_C);
        tempTolerance = preferences.getFloat("tolerance", Config::Temperature::DEFAULT_TOLERANCE_C);
        preferences.end();
    }
    
    void save() {
        preferences.begin("terpmeter", false);
        preferences.putBool("useCelsius", useCelsius);
        preferences.putBool("soundEnabled", soundEnabled);
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
    float currentTemp = 0.0f;
    float tempHistory[Config::Temperature::HISTORY_SIZE] = {0};
    int historyIndex = 0;
    unsigned long lastTempUpdate = 0;
    unsigned long lastBatteryCheck = 0;
    unsigned long lastActivity = 0;
    bool lowBatteryWarning = false;
    String statusMessage;
    uint32_t statusColor = Config::Display::COLOR_TEXT;
};

// Settings menu state
struct SettingsMenu {
    bool isOpen = false;
    int selectedItem = 0;
    const int numItems = 6;
    const char* menuItems[6] = {
        "Temperature Unit",
        "Sound",
        "Brightness",
        "Emissivity",
        "Target Temp",
        "Tolerance"
    };
};

SettingsMenu settingsMenu;

// Global instances
Settings settings;
SystemState state;

// UI Components
class Button {
public:
    int x, y, width, height;
    String label;
    bool enabled;
    bool pressed;
    
    Button(int _x, int _y, int _w, int _h, String _label, bool _enabled = true)
        : x(_x), y(_y), width(_w), height(_h), label(_label), enabled(_enabled), pressed(false) {}
    
    bool contains(int touch_x, int touch_y) {
        return enabled && 
               touch_x >= x && touch_x < x + width &&
               touch_y >= y && touch_y < y + height;
    }
    
    void draw(uint32_t color = Config::Display::COLOR_SECONDARY) {
        if (!enabled) color = Config::Display::COLOR_SECONDARY;
        
        // Draw button background
        CoreS3.Display.fillRoundRect(x, y, width, height, 8, color);
        
        // Draw button border
        CoreS3.Display.drawRoundRect(x, y, width, height, 8, Config::Display::COLOR_TEXT);
        
        // Draw label
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
        CoreS3.Display.drawString(label.c_str(), x + width/2, y + height/2);
    }
};

// Forward declarations of helper functions
void updateDisplay();
void handleTouch();
void checkTemperature();
void checkBattery();
void updateStatus(const String& message, uint32_t color);
void enterSleep();
void wakeUp();
float readTemperature();
void playSound(bool success);
void drawHeader();
void drawFooter();
void drawMainDisplay();
void drawSettingsMenu();
void handleSettingsTouch(int x, int y);
void drawTemperature();

bool isValidTemperature(float temp) {
    // Check if temperature is within reasonable bounds (-20°C to 200°C)
    return temp > -2000 && temp < 20000;  // Values are in centidegrees
}

// Main setup function
void setup() {
    Serial.begin(115200);
    auto cfg = M5.config();
    cfg.clear_display = true;
    CoreS3.begin(cfg);
    
    // Initialize power management
    CoreS3.Power.begin();
    CoreS3.Power.setChargeCurrent(900);
    delay(500);
    
    // Initialize speaker
    CoreS3.Speaker.begin();
    CoreS3.Speaker.setVolume(128);

    // Initialize display
    CoreS3.Display.begin();
    CoreS3.Display.setRotation(1);
    CoreS3.Display.setBrightness(128);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    
    // Initialize I2C and sensor
    int retryCount = 0;
    bool sensorInitialized = false;
    bool validReadingObtained = false;
    
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Initializing Sensor", CoreS3.Display.width()/2, 40);
    
    while (!sensorInitialized && retryCount < 5) {  // Increased max retries
        Wire.end();
        delay(250);  // Increased delay
        Wire.begin(2, 1, 100000);
        delay(1000);  // Longer delay after Wire.begin
        
        CoreS3.Display.drawString("Connecting to Sensor", CoreS3.Display.width()/2, 80);
        CoreS3.Display.drawString("Attempt " + String(retryCount + 1) + "/5", 
                                CoreS3.Display.width()/2, 120);
        
        if (ncir2.begin(&Wire, 2, 1, M5UNIT_NCIR2_DEFAULT_ADDR)) {
            // Try to get valid readings
            int validReadingCount = 0;
            const int requiredValidReadings = 3;  // Need 3 valid readings to proceed
            
            CoreS3.Display.drawString("Checking Sensor...", CoreS3.Display.width()/2, 160);
            
            ncir2.setEmissivity(0.95);
            ncir2.setConfig();
            delay(500);  // Wait after config
            
            for (int i = 0; i < 10 && validReadingCount < requiredValidReadings; i++) {
                float temp = ncir2.getTempValue();
                Serial.printf("Init reading %d: %.2f\n", i, temp);
                
                if (isValidTemperature(temp)) {
                    validReadingCount++;
                    CoreS3.Display.drawString("Valid Reading " + String(validReadingCount) + "/" + String(requiredValidReadings), 
                                           CoreS3.Display.width()/2, 200);
                } else {
                    validReadingCount = 0;  // Reset if we get an invalid reading
                }
                delay(200);
            }
            
            if (validReadingCount >= requiredValidReadings) {
                sensorInitialized = true;
                validReadingObtained = true;
                CoreS3.Display.drawString("Sensor Ready!", CoreS3.Display.width()/2, 240);
                delay(1000);
            } else {
                Serial.println("Failed to get valid readings");
            }
        }
        
        if (!sensorInitialized) {
            retryCount++;
            delay(500);
        }
    }
    
    if (!sensorInitialized || !validReadingObtained) {
        CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
        CoreS3.Display.setTextColor(Config::Display::COLOR_ERROR);
        CoreS3.Display.drawString("Sensor Init Failed!", CoreS3.Display.width()/2, CoreS3.Display.height()/2);
        while (1) {
            if (CoreS3.Power.isCharging()) ESP.restart();
            delay(100);
        }
    }
    
    // Load settings and configure sensor
    settings.load();
    
    // Initialize watchdog
    esp_task_wdt_init(Config::System::WATCHDOG_TIMEOUT_MS / 1000, true);
    esp_task_wdt_add(NULL);
    
    // Initial display update
    updateDisplay();
}

// Main loop function
void loop() {
    esp_task_wdt_reset();
    
    CoreS3.update();  // Update CoreS3 state
    
    unsigned long currentMillis = millis();
    
    // Check for sleep condition
    if (currentMillis - state.lastActivity > Config::System::SLEEP_TIMEOUT_MS) {
        enterSleep();
    }
    
    // Handle touch input
    handleTouch();
    
    // Update temperature reading more frequently
    if (currentMillis - state.lastTempUpdate >= Config::Temperature::UPDATE_INTERVAL_MS) {
        checkTemperature();
        state.lastTempUpdate = currentMillis;
    }
    
    // Check battery status
    if (currentMillis - state.lastBatteryCheck >= Config::System::BATTERY_CHECK_INTERVAL_MS) {
        checkBattery();
        state.lastBatteryCheck = currentMillis;
    }
    
    delay(10);  // Small delay to prevent overwhelming the processor
}

void updateDisplay() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    drawHeader();
    
    if (settingsMenu.isOpen) {
        drawSettingsMenu();
    } else {
        drawMainDisplay();
    }
    
    drawFooter();
}

void handleTouch() {
    if (CoreS3.Touch.getCount()) {
        auto t = CoreS3.Touch.getDetail();
        if (t.wasPressed()) {
            Serial.println("Touch detected!"); // Debug output
            Serial.printf("Touch coordinates: x=%d, y=%d\n", t.x, t.y); // Debug coordinates
            
            state.lastActivity = millis();
            
            int x = t.x;
            int y = t.y;
            
            // Check footer buttons (always active)
            int footerY = CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT;
            if (y >= footerY) {
                int buttonWidth = (CoreS3.Display.width() - 3*Config::Display::MARGIN) / 2;
                
                Serial.println("Footer area touched"); // Debug output
                
                // Monitor button (only when not in settings)
                if (!settingsMenu.isOpen && 
                    x >= Config::Display::MARGIN && 
                    x < Config::Display::MARGIN + buttonWidth) {
                    Serial.println("Monitor button pressed"); // Debug output
                    state.isMonitoring = !state.isMonitoring;
                    if (settings.soundEnabled) playSound(true);
                    
                    // Reset temperature history when starting monitoring
                    if (state.isMonitoring) {
                        for (int i = 0; i < Config::Temperature::HISTORY_SIZE; i++) {
                            state.tempHistory[i] = 0;
                        }
                        state.historyIndex = 0;
                    }
                }
                // Settings/Back button (always active)
                else if (x >= CoreS3.Display.width() - buttonWidth - Config::Display::MARGIN && 
                         x < CoreS3.Display.width() - Config::Display::MARGIN) {
                    Serial.println("Settings/Back button pressed"); // Debug output
                    settingsMenu.isOpen = !settingsMenu.isOpen;
                    if (settings.soundEnabled) playSound(true);
                }
            }
            // Handle settings menu touches only when in settings
            else if (settingsMenu.isOpen) {
                handleSettingsTouch(x, y);
            }
            
            // Update display
            updateDisplay();
        }
    }
}

void checkTemperature() {
    float temp = readTemperature();
    if (temp >= Config::Temperature::MIN_TEMP_C && temp <= Config::Temperature::MAX_TEMP_C) {
        // Only update if temperature has changed significantly (1.0°C threshold)
        if (abs(temp - state.currentTemp) > 1.0 || state.isMonitoring) {
            state.currentTemp = temp;
            
            if (state.isMonitoring) {
                state.tempHistory[state.historyIndex] = temp;
                state.historyIndex = (state.historyIndex + 1) % Config::Temperature::HISTORY_SIZE;
                
                float diff = abs(temp - settings.targetTemp);
                String newStatus;
                uint32_t newColor;
                
                if (diff > settings.tempTolerance) {
                    newStatus = "Temperature Out of Range!";
                    newColor = Config::Display::COLOR_WARNING;
                    if (settings.soundEnabled && 
                        (state.statusMessage != newStatus || state.statusColor != newColor)) {
                        playSound(false);
                    }
                } else {
                    newStatus = "Monitoring";
                    newColor = Config::Display::COLOR_SUCCESS;
                }
                
                updateStatus(newStatus, newColor);
            }
            
            // Only update display if temperature changed or monitoring
            drawTemperature();
        }
    } else {
        updateStatus("Invalid Temperature!", Config::Display::COLOR_ERROR);
    }
}

void checkBattery() {
    int batteryLevel = CoreS3.Power.getBatteryLevel();
    bool isCharging = CoreS3.Power.isCharging();
    
    if (batteryLevel <= 10 && !isCharging) {
        if (!state.lowBatteryWarning) {
            updateStatus("Low Battery!", Config::Display::COLOR_WARNING);
            state.lowBatteryWarning = true;
        }
    } else {
        state.lowBatteryWarning = false;
    }
}

void updateStatus(const String& message, uint32_t color) {
    // Only update if message or color has changed
    if (message != state.statusMessage || color != state.statusColor) {
        state.statusMessage = message;
        state.statusColor = color;
        
        // Update status display area
        int statusY = CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT - Config::Display::STATUS_HEIGHT;
        CoreS3.Display.fillRect(0, statusY, CoreS3.Display.width(), Config::Display::STATUS_HEIGHT, Config::Display::COLOR_BACKGROUND);
        CoreS3.Display.setTextColor(color);
        CoreS3.Display.drawString(message.c_str(), CoreS3.Display.width()/2, statusY + Config::Display::STATUS_HEIGHT/2);
    }
}

float readTemperature() {
    float rawTemp = ncir2.getTempValue();
    float tempC = rawTemp / 100.0;  // Convert to Celsius
    float tempF = (tempC * 9.0/5.0) + 32.0;  // Convert to Fahrenheit
    
    // Debug output
    Serial.println("Temperature Readings:");
    Serial.printf("  Raw: %.2f\n", rawTemp);
    Serial.printf("  Celsius: %.2f°C\n", tempC);
    Serial.printf("  Fahrenheit: %.2f°F\n", tempF);
    Serial.printf("  Display Value: %d°F\n", (int)round(tempF));
    Serial.println();
    
    return tempC;  // Return Celsius for internal use
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
    CoreS3.Display.drawString("TerpMeter Pro", CoreS3.Display.width()/2, Config::Display::HEADER_HEIGHT/2);
    
    // Draw battery indicator
    int batteryLevel = CoreS3.Power.getBatteryLevel();
    bool isCharging = CoreS3.Power.isCharging();
    
    // Battery icon dimensions
    const int battW = 30;
    const int battH = 15;
    const int battX = CoreS3.Display.width() - battW - Config::Display::MARGIN;
    const int battY = (Config::Display::HEADER_HEIGHT - battH)/2;
    
    // Draw battery outline
    CoreS3.Display.drawRect(battX, battY, battW, battH, Config::Display::COLOR_TEXT);
    CoreS3.Display.fillRect(battX + battW, battY + 4, 2, 7, Config::Display::COLOR_TEXT);
    
    // Fill battery based on level
    uint32_t battColor = batteryLevel > 50 ? Config::Display::COLOR_SUCCESS :
                        batteryLevel > 20 ? Config::Display::COLOR_WARNING :
                        Config::Display::COLOR_ERROR;
    
    if (isCharging) battColor = Config::Display::COLOR_SUCCESS;
    
    int fillW = (battW - 4) * batteryLevel / 100;
    CoreS3.Display.fillRect(battX + 2, battY + 2, fillW, battH - 4, battColor);
}

void drawFooter() {
    int footerY = CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT;
    
    // Draw footer background
    CoreS3.Display.fillRect(0, footerY, CoreS3.Display.width(), Config::Display::FOOTER_HEIGHT, Config::Display::COLOR_SECONDARY);
    
    // Create and draw buttons
    int buttonWidth = (CoreS3.Display.width() - 3*Config::Display::MARGIN) / 2;
    
    // Only show Monitor button when not in settings
    if (!settingsMenu.isOpen) {
        Button monitorBtn(Config::Display::MARGIN, 
                        footerY + (Config::Display::FOOTER_HEIGHT - Config::Display::BUTTON_HEIGHT)/2,
                        buttonWidth, 
                        Config::Display::BUTTON_HEIGHT - 4,
                        state.isMonitoring ? "Stop" : "Monitor");
        monitorBtn.draw(state.isMonitoring ? Config::Display::COLOR_WARNING : Config::Display::COLOR_PRIMARY);
    }
    
    // Settings/Back button
    Button settingsBtn(CoreS3.Display.width() - buttonWidth - Config::Display::MARGIN,
                      footerY + (Config::Display::FOOTER_HEIGHT - Config::Display::BUTTON_HEIGHT)/2,
                      buttonWidth,
                      Config::Display::BUTTON_HEIGHT - 4,
                      settingsMenu.isOpen ? "Back" : "Settings");
    settingsBtn.draw();
}

void drawTemperature() {
    int contentY = Config::Display::HEADER_HEIGHT + Config::Display::MARGIN;
    int contentHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT - 2*Config::Display::MARGIN;
    
    // Adjust height when monitoring is active to make room for graph
    if (state.isMonitoring) {
        contentHeight = contentHeight / 2;  // Use only top half for temperature display
    }
    
    // Clear temperature display area only
    CoreS3.Display.fillRect(
        Config::Display::MARGIN + 1, 
        contentY + 1, 
        CoreS3.Display.width() - 2*Config::Display::MARGIN - 2, 
        contentHeight - 2, 
        Config::Display::COLOR_BACKGROUND
    );
    
    // Draw current temperature
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(3);
    
    char tempStr[10];
    float tempF = (state.currentTemp * 9.0/5.0) + 32.0;
    sprintf(tempStr, "%d°F", (int)round(tempF));
    
    uint32_t tempColor = state.isMonitoring ? 
        (abs(state.currentTemp - settings.targetTemp) <= settings.tempTolerance ? 
            Config::Display::COLOR_SUCCESS : Config::Display::COLOR_WARNING) :
        Config::Display::COLOR_TEXT;
    
    CoreS3.Display.setTextColor(tempColor);
    CoreS3.Display.drawString(tempStr, CoreS3.Display.width()/2, contentY + contentHeight/2);
}

void drawMainDisplay() {
    int contentY = Config::Display::HEADER_HEIGHT + Config::Display::MARGIN;
    int contentHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT - 2*Config::Display::MARGIN;
    
    // Draw temperature display box (only drawn once)
    if (state.isMonitoring) {
        // Draw smaller box for temperature in top half
        CoreS3.Display.drawRoundRect(Config::Display::MARGIN, contentY, 
                                   CoreS3.Display.width() - 2*Config::Display::MARGIN, 
                                   contentHeight/2, 8, Config::Display::COLOR_TEXT);
        
        // Draw box for graph in bottom half
        CoreS3.Display.drawRoundRect(Config::Display::MARGIN, contentY + contentHeight/2 + Config::Display::MARGIN, 
                                   CoreS3.Display.width() - 2*Config::Display::MARGIN, 
                                   contentHeight/2 - Config::Display::MARGIN, 8, Config::Display::COLOR_TEXT);
    } else {
        // Full size box when not monitoring
        CoreS3.Display.drawRoundRect(Config::Display::MARGIN, contentY, 
                                   CoreS3.Display.width() - 2*Config::Display::MARGIN, 
                                   contentHeight, 8, Config::Display::COLOR_TEXT);
    }
    
    // Draw temperature
    drawTemperature();
    
    // Draw temperature history graph if monitoring
    if (state.isMonitoring) {
        const int graphH = (contentHeight/2) - 2*Config::Display::MARGIN;
        const int graphY = contentY + contentHeight/2 + 2*Config::Display::MARGIN;
        const int graphW = CoreS3.Display.width() - 4*Config::Display::MARGIN;
        const int graphX = 2*Config::Display::MARGIN;
        
        // Clear graph area first
        CoreS3.Display.fillRect(graphX - 5, graphY - 5, 
                              graphW + 10, graphH + 10, 
                              Config::Display::COLOR_BACKGROUND);
        
        // Draw graph axes
        CoreS3.Display.drawLine(graphX, graphY, graphX, graphY + graphH, Config::Display::COLOR_TEXT);
        CoreS3.Display.drawLine(graphX, graphY + graphH, graphX + graphW, graphY + graphH, Config::Display::COLOR_TEXT);
        
        // Draw target temperature line
        int targetY = graphY + graphH - (settings.targetTemp - (settings.targetTemp - settings.tempTolerance * 2)) * graphH / (settings.tempTolerance * 4);
        CoreS3.Display.drawLine(graphX, targetY, graphX + graphW, targetY, Config::Display::COLOR_SUCCESS);
        
        // Plot temperature history
        float minTemp = settings.targetTemp - settings.tempTolerance * 2;
        float maxTemp = settings.targetTemp + settings.tempTolerance * 2;
        float tempRange = maxTemp - minTemp;
        
        // Draw temperature points and lines
        for (int i = 1; i < Config::Temperature::HISTORY_SIZE; i++) {
            if (state.tempHistory[i] == 0 && state.tempHistory[i-1] == 0) continue;
            
            int x1 = graphX + (i-1) * graphW / (Config::Temperature::HISTORY_SIZE-1);
            int x2 = graphX + i * graphW / (Config::Temperature::HISTORY_SIZE-1);
            int y1 = graphY + graphH - ((state.tempHistory[i-1] - minTemp) * graphH / tempRange);
            int y2 = graphY + graphH - ((state.tempHistory[i] - minTemp) * graphH / tempRange);
            
            if (state.tempHistory[i] != 0 && state.tempHistory[i-1] != 0) {
                CoreS3.Display.drawLine(x1, y1, x2, y2, Config::Display::COLOR_PRIMARY);
            }
            
            // Draw points
            if (state.tempHistory[i-1] != 0) {
                CoreS3.Display.fillCircle(x1, y1, 2, Config::Display::COLOR_PRIMARY);
            }
            if (state.tempHistory[i] != 0) {
                CoreS3.Display.fillCircle(x2, y2, 2, Config::Display::COLOR_PRIMARY);
            }
        }
        
        // Draw min/max labels
        CoreS3.Display.setTextSize(1);
        CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
        char tempStr[10];
        sprintf(tempStr, "%.1f°F", maxTemp * 9.0/5.0 + 32);
        CoreS3.Display.drawString(tempStr, graphX - 5, graphY);
        sprintf(tempStr, "%.1f°F", minTemp * 9.0/5.0 + 32);
        CoreS3.Display.drawString(tempStr, graphX - 5, graphY + graphH);
    }
}

void drawSettingsMenu() {
    int menuY = Config::Display::HEADER_HEIGHT + Config::Display::MARGIN;
    int menuHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT - 2*Config::Display::MARGIN;
    int itemHeight = menuHeight / settingsMenu.numItems;
    
    // Draw menu background
    CoreS3.Display.fillRect(Config::Display::MARGIN, menuY, 
                          CoreS3.Display.width() - 2*Config::Display::MARGIN, 
                          menuHeight, Config::Display::COLOR_SECONDARY);
    
    // Draw menu items
    CoreS3.Display.setTextDatum(middle_left);
    CoreS3.Display.setTextSize(1);
    
    for (int i = 0; i < settingsMenu.numItems; i++) {
        int itemY = menuY + i * itemHeight;
        bool isSelected = (i == settingsMenu.selectedItem);
        
        // Draw selection highlight
        if (isSelected) {
            CoreS3.Display.fillRect(Config::Display::MARGIN, itemY, 
                                  CoreS3.Display.width() - 2*Config::Display::MARGIN, 
                                  itemHeight, Config::Display::COLOR_PRIMARY);
        }
        
        // Draw item text
        CoreS3.Display.setTextColor(isSelected ? Config::Display::COLOR_BACKGROUND : Config::Display::COLOR_TEXT);
        CoreS3.Display.drawString(settingsMenu.menuItems[i], 
                                Config::Display::MARGIN * 2, 
                                itemY + itemHeight/2);
        
        // Draw current value
        String value;
        switch(i) {
            case 0: value = settings.useCelsius ? "Celsius" : "Fahrenheit"; break;
            case 1: value = settings.soundEnabled ? "On" : "Off"; break;
            case 2: value = String(settings.brightness); break;
            case 3: value = String(settings.emissivity, 2); break;
            case 4: value = String(settings.targetTemp, 1) + (settings.useCelsius ? "°C" : "°F"); break;
            case 5: value = String(settings.tempTolerance, 1) + (settings.useCelsius ? "°C" : "°F"); break;
        }
        
        CoreS3.Display.drawString(value, 
                                CoreS3.Display.width() - Config::Display::MARGIN * 3, 
                                itemY + itemHeight/2);
    }
}

void handleSettingsTouch(int x, int y) {
    int menuY = Config::Display::HEADER_HEIGHT + Config::Display::MARGIN;
    int menuHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT - 2*Config::Display::MARGIN;
    int itemHeight = menuHeight / settingsMenu.numItems;
    
    // Check if touch is within menu area
    if (x >= Config::Display::MARGIN && 
        x <= CoreS3.Display.width() - Config::Display::MARGIN &&
        y >= menuY && y <= menuY + menuHeight) {
        
        // Calculate which item was touched
        int touchedItem = (y - menuY) / itemHeight;
        if (touchedItem >= 0 && touchedItem < settingsMenu.numItems) {
            settingsMenu.selectedItem = touchedItem;
            
            // Handle item selection
            switch(touchedItem) {
                case 0: // Temperature Unit
                    settings.useCelsius = !settings.useCelsius;
                    break;
                case 1: // Sound
                    settings.soundEnabled = !settings.soundEnabled;
                    if (settings.soundEnabled) playSound(true);
                    break;
                case 2: // Brightness
                    settings.brightness = (settings.brightness + 64) % 256;
                    CoreS3.Display.setBrightness(settings.brightness);
                    break;
                case 3: // Emissivity
                    settings.emissivity += 0.01f;
                    if (settings.emissivity > 1.0f) settings.emissivity = 0.80f;
                    ncir2.setEmissivity(settings.emissivity);
                    break;
                case 4: // Target Temperature
                    settings.targetTemp += settings.useCelsius ? 5.0f : 10.0f;
                    if (settings.targetTemp > Config::Temperature::MAX_TEMP_C)
                        settings.targetTemp = Config::Temperature::MIN_TEMP_C;
                    break;
                case 5: // Tolerance
                    settings.tempTolerance += settings.useCelsius ? 0.5f : 1.0f;
                    if (settings.tempTolerance > 20.0f) settings.tempTolerance = 1.0f;
                    break;
            }
            
            settings.save();
            if (settings.soundEnabled) playSound(true);
        }
    }
}

void enterSleep() {
    // Save any necessary state
    settings.save();
    
    // Turn off display
    CoreS3.Display.setBrightness(0);
    CoreS3.Display.sleep();
    
    // Enter light sleep
    esp_sleep_enable_touchpad_wakeup();
    esp_light_sleep_start();
    
    // When woken up
    wakeUp();
}

void wakeUp() {
    // Restore display
    CoreS3.Display.wakeup();
    CoreS3.Display.setBrightness(settings.brightness);
    
    // Reset activity timer
    state.lastActivity = millis();
    
    // Update display
    updateDisplay();
}
