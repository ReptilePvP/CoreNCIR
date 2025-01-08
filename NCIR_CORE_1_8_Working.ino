#include <M5CoreS3.h>
#include <M5GFX.h>
#include <M5Unified.h>
#include <Wire.h>
#include <Preferences.h>

// Configuration namespace
namespace Config {
    namespace Display {
        const int HEADER_HEIGHT = 32;
        const int FOOTER_HEIGHT = 50;
        const int MARGIN = 10;
        const int BUTTON_HEIGHT = 40;
        
        // Colors
        const uint32_t COLOR_BACKGROUND = 0x000000;  // Black
        const uint32_t COLOR_TEXT = 0xFFFFFF;        // White
        const uint32_t COLOR_PRIMARY = 0x0000FF;     // Blue
        const uint32_t COLOR_SUCCESS = 0x00FF00;     // Green
        const uint32_t COLOR_WARNING = 0xFF0000;     // Red
        const uint32_t COLOR_SECONDARY = 0x808080;   // Gray
        const uint32_t COLOR_BUTTON = 0xCCCCCC;      // Button color
        const uint32_t COLOR_BUTTON_ACTIVE = 0xAAAAAA;  // Active button color
    }
    
    namespace Temperature {
        const float MIN_VALID_TEMP = -20.0f;    // Minimum valid temperature in Celsius
        const float MAX_VALID_TEMP = 200.0f;    // Maximum valid temperature in Celsius
        const float DEFAULT_TARGET_C = 37.0f;   // Default target temperature
        const float DEFAULT_TOLERANCE_C = 0.5f;  // Default temperature tolerance
        const int UPDATE_INTERVAL_MS = 100;     // Temperature reading interval
        const int DISPLAY_UPDATE_MS = 250;      // Display update interval
        const float CHANGE_THRESHOLD = 0.5f;    // Minimum change to update display
    }
    
    namespace System {
        const int DEFAULT_BRIGHTNESS = 128;
        const float DEFAULT_EMISSIVITY = 0.95f;
        const unsigned long BATTERY_CHECK_INTERVAL_MS = 30000;  // 30 seconds
        const float MIN_EMISSIVITY = 0.1f;
        const float MAX_EMISSIVITY = 1.0f;
    }
}

// Settings class
class Settings {
public:
    bool useCelsius = true;
    bool soundEnabled = true;
    int brightness = Config::System::DEFAULT_BRIGHTNESS;
    float emissivity = Config::System::DEFAULT_EMISSIVITY;
    float targetTemp = Config::Temperature::DEFAULT_TARGET_C;
    float tempTolerance = Config::Temperature::DEFAULT_TOLERANCE_C;
    
    void load() {
        preferences.begin("tempmeter", false);
        useCelsius = preferences.getBool("useCelsius", true);
        soundEnabled = preferences.getBool("soundEnabled", true);
        brightness = preferences.getInt("brightness", Config::System::DEFAULT_BRIGHTNESS);
        emissivity = preferences.getFloat("emissivity", Config::System::DEFAULT_EMISSIVITY);
        targetTemp = preferences.getFloat("targetTemp", Config::Temperature::DEFAULT_TARGET_C);
        tempTolerance = preferences.getFloat("tolerance", Config::Temperature::DEFAULT_TOLERANCE_C);
        preferences.end();
    }
    
    void save() {
        preferences.begin("tempmeter", false);
        preferences.putBool("useCelsius", useCelsius);
        preferences.putBool("soundEnabled", soundEnabled);
        preferences.putInt("brightness", brightness);
        preferences.putFloat("emissivity", emissivity);
        preferences.putFloat("targetTemp", targetTemp);
        preferences.putFloat("tolerance", tempTolerance);
        preferences.end();
    }
    
private:
    Preferences preferences;
};

// System state class
class SystemState {
public:
    bool isMonitoring = false;
    float currentTemp = 0.0f;
    float lastDisplayTemp = -999.0f;
    unsigned long lastTempUpdate = 0;
    unsigned long lastDisplayUpdate = 0;
    unsigned long lastBatteryCheck = 0;
    bool lowBatteryWarning = false;
    String statusMessage;
    uint32_t statusColor = Config::Display::COLOR_TEXT;
    enum Screen { MAIN, SETTINGS } currentScreen = MAIN;
    bool useCelsius = true;
    
    void updateStatus(const String& message, uint32_t color = Config::Display::COLOR_TEXT) {
        statusMessage = message;
        statusColor = color;
    }
};

Preferences preferences;
Settings settings;
SystemState state;

// Button structure
struct Button {
    int x;
    int y;
    int width;
    int height;
    const char* label;
    bool pressed;
    bool isToggle;
};

// Helper function to check if touch is within button bounds
bool touchInButton(const Button& btn, int touchX, int touchY) {
    return (touchX >= btn.x && touchX < btn.x + btn.width &&
            touchY >= btn.y && touchY < btn.y + btn.height);
}

// Helper function to draw a button
void drawButton(const Button& btn, uint32_t color) {
    CoreS3.Display.fillRoundRect(btn.x, btn.y, btn.width, btn.height, 10, color);
    CoreS3.Display.drawRoundRect(btn.x, btn.y, btn.width, btn.height, 10, Config::Display::COLOR_TEXT);
}

// Helper function for touch sound
void playTouchSound(bool success) {
    if (settings.soundEnabled) {
        CoreS3.Speaker.tone(success ? 1000 : 500, success ? 50 : 100);
        delay(success ? 50 : 100);
    }
}

// Draw 3D button with highlight and shadow effects
void draw3DButton(int x, int y, int w, int h, const char* text, bool isActive, bool isPressed) {
    uint32_t baseColor = isActive ? Config::Display::COLOR_PRIMARY : Config::Display::COLOR_SECONDARY;
    uint32_t shadowColor = 0x2104;  // Dark shadow
    uint32_t highlightColor = 0xFFFF;  // Bright highlight
    
    if (isPressed) {
        // Draw pressed button (shifted down and right)
        CoreS3.Display.fillRoundRect(x + 2, y + 2, w, h, 10, shadowColor);
        CoreS3.Display.fillRoundRect(x, y, w, h, 10, baseColor);
    } else {
        // Draw 3D effect for unpressed button
        CoreS3.Display.fillRoundRect(x, y, w, h, 10, shadowColor);  // Shadow
        CoreS3.Display.fillRoundRect(x - 2, y - 2, w, h, 10, baseColor);  // Main button
        
        // Add highlight on top and left edges
        CoreS3.Display.drawLine(x - 2, y - 2, x - 2, y + h - 2, highlightColor);  // Left edge
        CoreS3.Display.drawLine(x - 2, y - 2, x + w - 2, y - 2, highlightColor);  // Top edge
    }
    
    // Draw text
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString(text, x + w/2 - (isPressed ? 0 : 2), y + h/2 - (isPressed ? 0 : 2));
}

// Temperature unit selection function
bool selectTemperatureUnit() {
    Serial.println("Starting temperature unit selection");
    
    // Clear screen
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    
    // Draw header with gradient effect
    for (int i = 0; i < Config::Display::HEADER_HEIGHT; i++) {
        uint32_t gradientColor = CoreS3.Display.color565(
            map(i, 0, Config::Display::HEADER_HEIGHT, 0x3C, 0x1B),
            map(i, 0, Config::Display::HEADER_HEIGHT, 0xD0, 0x33),
            map(i, 0, Config::Display::HEADER_HEIGHT, 0x70, 0x58)
        );
        CoreS3.Display.drawFastHLine(0, i, CoreS3.Display.width(), gradientColor);
    }
    
    // Draw title with shadow effect
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.setTextColor(Config::Display::COLOR_SECONDARY);  // Shadow
    CoreS3.Display.drawString("Select Temperature Unit", CoreS3.Display.width()/2 + 2, Config::Display::HEADER_HEIGHT/2 + 2);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);  // Main text
    CoreS3.Display.drawString("Select Temperature Unit", CoreS3.Display.width()/2, Config::Display::HEADER_HEIGHT/2);
    
    // Calculate button positions
    int buttonWidth = 120;
    int buttonHeight = 120;
    int spacing = 60;
    int startY = Config::Display::HEADER_HEIGHT + 50;
    
    // Calculate center positions
    int celsiusX = (CoreS3.Display.width() - (2 * buttonWidth + spacing)) / 2;
    int fahrenheitX = celsiusX + buttonWidth + spacing;
    
    // Variables for button states
    bool celsiusPressed = false;
    bool fahrenheitPressed = false;
    bool celsiusActive = settings.useCelsius;
    bool fahrenheitActive = !settings.useCelsius;
    
    // Draw initial buttons
    draw3DButton(celsiusX, startY, buttonWidth, buttonHeight, "°C", celsiusActive, false);
    draw3DButton(fahrenheitX, startY, buttonWidth, buttonHeight, "°F", fahrenheitActive, false);
    
    // Draw labels with shadow effect
    CoreS3.Display.setTextSize(2);
    int labelY = startY + buttonHeight + 20;
    
    // Celsius label
    CoreS3.Display.setTextColor(Config::Display::COLOR_SECONDARY);
    CoreS3.Display.drawString("Celsius", celsiusX + buttonWidth/2 + 1, labelY + 1);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString("Celsius", celsiusX + buttonWidth/2, labelY);
    
    // Fahrenheit label
    CoreS3.Display.setTextColor(Config::Display::COLOR_SECONDARY);
    CoreS3.Display.drawString("Fahrenheit", fahrenheitX + buttonWidth/2 + 1, labelY + 1);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString("Fahrenheit", fahrenheitX + buttonWidth/2, labelY);
    
    // Handle touch input
    unsigned long startTime = millis();
    bool selectionMade = false;
    
    while (!selectionMade && (millis() - startTime < 30000)) {
        CoreS3.update();
        
        if (CoreS3.Touch.getCount()) {
            auto t = CoreS3.Touch.getDetail();
            
            if (t.wasPressed()) {
                // Check Celsius button
                if (t.x >= celsiusX && t.x < celsiusX + buttonWidth &&
                    t.y >= startY && t.y < startY + buttonHeight) {
                    
                    // Visual feedback
                    draw3DButton(celsiusX, startY, buttonWidth, buttonHeight, "°C", true, true);
                    
                    // Play sound
                    CoreS3.Speaker.tone(1000, 50);
                    delay(50);
                    CoreS3.Speaker.tone(1200, 50);
                    
                    settings.useCelsius = true;
                    state.useCelsius = true;
                    selectionMade = true;
                }
                // Check Fahrenheit button
                else if (t.x >= fahrenheitX && t.x < fahrenheitX + buttonWidth &&
                         t.y >= startY && t.y < startY + buttonHeight) {
                    
                    // Visual feedback
                    draw3DButton(fahrenheitX, startY, buttonWidth, buttonHeight, "°F", true, true);
                    
                    // Play sound
                    CoreS3.Speaker.tone(800, 50);
                    delay(50);
                    CoreS3.Speaker.tone(1000, 50);
                    
                    settings.useCelsius = false;
                    state.useCelsius = false;
                    selectionMade = true;
                }
            }
        }
        delay(10);  // Small delay to prevent tight loop
    }
    
    if (selectionMade) {
        // Quick success animation
        CoreS3.Display.fillScreen(Config::Display::COLOR_SUCCESS);
        delay(50);
        CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
        settings.save();
        return true;
    }
    
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    return false;
}

// Temperature sensor functions
float readTemperature() {
    Wire.beginTransmission(0x5A);
    Wire.write(0x07);  // Object temperature register
    Wire.endTransmission(false);
    
    Wire.requestFrom(0x5A, 2);
    if (Wire.available() >= 2) {
        uint16_t result = Wire.read();
        result |= Wire.read() << 8;
        
        // Convert to Celsius
        float tempC = result * 0.02f - 273.15f;
        
        // Debug output
        Serial.println("\n[Debug Temperature]");
        Serial.printf("Raw Temperature: %.2f\n", result);
        Serial.printf("Celsius Temperature: %.2f°C\n", tempC);
        Serial.printf("Fahrenheit Temperature: %.2f°F\n", (tempC * 9.0f / 5.0f) + 32.0f);
        Serial.printf("Display Temperature: %d°%c\n", (int)round(state.useCelsius ? tempC : (tempC * 9.0f / 5.0f) + 32.0f), 
                     state.useCelsius ? 'C' : 'F');
        Serial.println("-------------------");
        
        return tempC;
    }
    return -999.0f;
}

bool isValidTemperature(float temp) {
    return temp > Config::Temperature::MIN_VALID_TEMP && 
           temp < Config::Temperature::MAX_VALID_TEMP;
}

float celsiusToFahrenheit(float celsius) {
    return (celsius * 9.0f / 5.0f) + 32.0f;
}

// Display functions
void drawHeader() {
    CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), Config::Display::HEADER_HEIGHT, 
                          Config::Display::COLOR_PRIMARY);
    
    // Draw battery status
    float batteryLevel = CoreS3.Power.getBatteryLevel();
    String batteryStr = String(batteryLevel, 0) + "%";
    
    CoreS3.Display.setTextDatum(middle_right);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString(batteryStr, CoreS3.Display.width() - Config::Display::MARGIN, 
                            Config::Display::HEADER_HEIGHT/2);
    
    // Draw title
    CoreS3.Display.setTextDatum(middle_left);
    CoreS3.Display.drawString("NCIR1 Temp Meter", Config::Display::MARGIN, 
                            Config::Display::HEADER_HEIGHT/2);
}

void drawTemperature() {
    int contentY = Config::Display::HEADER_HEIGHT + Config::Display::MARGIN;
    int contentHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - 
                       Config::Display::FOOTER_HEIGHT - 2*Config::Display::MARGIN;
    
    if (state.isMonitoring) {
        contentHeight /= 2;
    }
    
    // Clear temperature area
    CoreS3.Display.fillRect(Config::Display::MARGIN, contentY, 
                          CoreS3.Display.width() - 2*Config::Display::MARGIN,
                          contentHeight, Config::Display::COLOR_BACKGROUND);
    
    // Draw temperature box
    CoreS3.Display.drawRoundRect(Config::Display::MARGIN, contentY, 
                               CoreS3.Display.width() - 2*Config::Display::MARGIN,
                               contentHeight, 8, Config::Display::COLOR_TEXT);
    
    // Format temperature
    char tempStr[10];
    float displayTemp = state.useCelsius ? state.currentTemp : 
                       celsiusToFahrenheit(state.currentTemp);
    sprintf(tempStr, "%d°%c", (int)round(displayTemp), 
            state.useCelsius ? 'C' : 'F');
    
    // Set color based on monitoring state
    uint32_t tempColor;
    if (state.isMonitoring) {
        float diff = abs(state.currentTemp - settings.targetTemp);
        tempColor = (diff <= settings.tempTolerance) ? 
                   Config::Display::COLOR_SUCCESS : 
                   Config::Display::COLOR_WARNING;
    } else {
        tempColor = Config::Display::COLOR_TEXT;
    }
    
    // Draw temperature
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(4);
    CoreS3.Display.setTextColor(tempColor);
    CoreS3.Display.drawString(tempStr, CoreS3.Display.width()/2, 
                            contentY + contentHeight/3);
    
    // Draw emissivity
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    char emissStr[20];
    sprintf(emissStr, "E=%.2f", settings.emissivity);
    CoreS3.Display.drawString(emissStr, CoreS3.Display.width()/2, 
                            contentY + 2*contentHeight/3);
    
    // Draw monitoring info if active
    if (state.isMonitoring) {
        CoreS3.Display.setTextSize(2);
        char targetStr[20];
        float targetTemp = state.useCelsius ? settings.targetTemp : 
                          celsiusToFahrenheit(settings.targetTemp);
        sprintf(targetStr, "Target: %d°%c", (int)round(targetTemp), 
                state.useCelsius ? 'C' : 'F');
        CoreS3.Display.drawString(targetStr, CoreS3.Display.width()/2, 
                                contentY + contentHeight + contentHeight/2);
    }
}

void drawFooter() {
    int footerY = CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT;
    CoreS3.Display.fillRect(0, footerY, CoreS3.Display.width(), 
                          Config::Display::FOOTER_HEIGHT, 
                          Config::Display::COLOR_SECONDARY);
    
    int buttonWidth = (CoreS3.Display.width() - 3*Config::Display::MARGIN) / 2;
    int buttonY = footerY + (Config::Display::FOOTER_HEIGHT - 
                            Config::Display::BUTTON_HEIGHT) / 2;
    
    // Draw buttons
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextDatum(middle_center);
    
    // Monitor/Stop button
    uint32_t monitorColor = state.isMonitoring ? 
                           Config::Display::COLOR_WARNING : 
                           Config::Display::COLOR_SUCCESS;
    CoreS3.Display.fillRoundRect(Config::Display::MARGIN, buttonY, 
                               buttonWidth, Config::Display::BUTTON_HEIGHT, 
                               8, monitorColor);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString(state.isMonitoring ? "Stop" : "Monitor", 
                            Config::Display::MARGIN + buttonWidth/2, 
                            buttonY + Config::Display::BUTTON_HEIGHT/2);
    
    // Settings button
    CoreS3.Display.fillRoundRect(CoreS3.Display.width() - buttonWidth - 
                               Config::Display::MARGIN, buttonY, 
                               buttonWidth, Config::Display::BUTTON_HEIGHT, 
                               8, Config::Display::COLOR_PRIMARY);
    CoreS3.Display.drawString("Settings", 
                            CoreS3.Display.width() - Config::Display::MARGIN - 
                            buttonWidth/2, buttonY + Config::Display::BUTTON_HEIGHT/2);
}

void updateDisplay() {
    drawHeader();
    drawTemperature();
    drawFooter();
    
    // Draw status if present
    if (state.statusMessage.length() > 0) {
        int statusY = CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT - 30;
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.setTextColor(state.statusColor);
        CoreS3.Display.drawString(state.statusMessage, 
                                CoreS3.Display.width()/2, statusY);
    }
}

// Touch handling
void handleTouch() {
    auto touch = CoreS3.Touch.getDetail();
    if (!touch.wasPressed()) return;
    
    int footerY = CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT;
    if (touch.y >= footerY) {
        int buttonWidth = (CoreS3.Display.width() - 3*Config::Display::MARGIN) / 2;
        
        // Monitor button
        if (touch.x >= Config::Display::MARGIN && 
            touch.x < Config::Display::MARGIN + buttonWidth) {
            state.isMonitoring = !state.isMonitoring;
            if (settings.soundEnabled) {
                CoreS3.Speaker.tone(2000, 50);
            }
            state.updateStatus(state.isMonitoring ? "Monitoring..." : "", 
                             Config::Display::COLOR_SUCCESS);
        }
        // Settings button
        else if (touch.x >= CoreS3.Display.width() - buttonWidth - Config::Display::MARGIN) {
            // TODO: Implement settings menu
            if (settings.soundEnabled) {
                CoreS3.Speaker.tone(1500, 50);
            }
        }
        updateDisplay();
    }
}

void setup() {
    Serial.begin(115200);
    
    // Initialize Core S3 with configuration
    auto cfg = M5.config();
    CoreS3.begin(cfg);
    
    // Initialize power management
    CoreS3.Power.begin();
    CoreS3.Power.setChargeCurrent(900);
    CoreS3.Power.setExtOutput(true);
    delay(500);  // Give power time to stabilize
    
    // Initialize display settings first
    CoreS3.Display.setRotation(1);
    CoreS3.Display.setBrightness(128); // Set initial brightness
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    
    // Initialize settings
    settings.load();  
    Serial.println("Settings loaded");

    // Initialize temperature unit selection
    if (!selectTemperatureUnit()) {
        Serial.println("Temperature unit selection timed out");
        settings.useCelsius = true; // Default to Celsius if no selection
    }
        
    // Initialize I2C for NCIR1
    Wire.begin(2, 1);  // SDA = GPIO2, SCL = GPIO1
    delay(100);  // Let I2C stabilize
    
    // Initial display
    updateDisplay();
    state.updateStatus("Ready", Config::Display::COLOR_SUCCESS);
}

void loop() {
    // Update M5Stack state
    CoreS3.update();
    
    unsigned long currentMillis = millis();
    
    // Update temperature reading
    if (currentMillis - state.lastTempUpdate >= Config::Temperature::UPDATE_INTERVAL_MS) {
        float temp = readTemperature();
        if (isValidTemperature(temp)) {
            state.currentTemp = temp;
            
            // Update display if changed enough
            if (abs(state.currentTemp - state.lastDisplayTemp) >= 
                Config::Temperature::CHANGE_THRESHOLD) {
                if (currentMillis - state.lastDisplayUpdate >= 
                    Config::Temperature::DISPLAY_UPDATE_MS) {
                    state.lastDisplayTemp = state.currentTemp;
                    updateDisplay();
                    state.lastDisplayUpdate = currentMillis;
                }
            }
        }
        state.lastTempUpdate = currentMillis;
    }
    
    // Check battery
    if (currentMillis - state.lastBatteryCheck >= 
        Config::System::BATTERY_CHECK_INTERVAL_MS) {
        float batteryLevel = CoreS3.Power.getBatteryLevel();
        if (batteryLevel < 15.0f && !state.lowBatteryWarning) {
            state.lowBatteryWarning = true;
            state.updateStatus("Low Battery!", Config::Display::COLOR_WARNING);
        }
        state.lastBatteryCheck = currentMillis;
        updateDisplay();
    }
    
    // Handle touch events
    if (CoreS3.Touch.getCount()) {
        handleTouch();
    }
}
