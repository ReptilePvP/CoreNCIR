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

// Speaker constants
const int BEEP_FREQUENCY = 1000;  // 1kHz tone
const int BEEP_DURATION = 100;    // 100ms beep
const int BEEP_INTERVAL = 2000;   // 2 seconds between beeps


// Display colors
const uint32_t COLOR_COLD = 0x0000FF;    // Blue
const uint32_t COLOR_GOOD = 0x00FF00;    // Green
const uint32_t COLOR_HOT = 0xFF0000;     // Red
const uint32_t COLOR_BUTTON = 0x4444FF;  // Light blue
const uint32_t COLOR_BUTTON_ACTIVE = 0x00FF00;  // Green
const uint32_t COLOR_BACKGROUND = 0x000000;    // Black
const uint32_t COLOR_TEXT = 0xFFFFFF;    // White
const uint32_t COLOR_BATTERY_LOW = 0xFF0000;   // Red
const uint32_t COLOR_BATTERY_MED = 0xFFA500;   // Orange
const uint32_t COLOR_BATTERY_GOOD = 0x00FF00;  // Green
const uint32_t COLOR_CHARGING = 0xFFFF00;      // Yellow

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
float currentEmissivity = 0.95;  // Default value


// Touch and feedback constants
const int TOUCH_DEBOUNCE = 10;             // Reduced from 25ms to 10ms
const int AUDIO_FEEDBACK_DURATION = 15;     // Shorter audio feedback
const float TEMP_THRESHOLD = 0.5;          // More sensitive temperature updates
const int BUTTON_HIGHLIGHT = 0x6B6B6B;  // Color for button press feedback

// 4. Global variables
int16_t currentTemp = 0;
bool isMonitoring = false;
bool useCelsius = true;
static int16_t lastDisplayTemp = -999;
static String lastStatus = "";
static int lastBatLevel = -1;
static bool lastChargeState = false;
const float TEMP_OFFSET = 0.0;  // Adjust this if readings are consistently off
const float TEMP_SCALE = 1.0;   // Adjust this if readings are proportionally off
const int16_t TEMP_DISPLAY_THRESHOLD = 100;  // Only update display if temp changes by 1.0°C/Fg
const unsigned long TEMP_UPDATE_INTERVAL = 250;  // Reduced update frequency
const int DISPLAY_UPDATE_THRESHOLD = 1;  // Minimum change in temperature to trigger display update
const int DISPLAY_SMOOTHING_SAMPLES = 10; // Number of samples for display smoothing
unsigned long lastBeepTime = 0;  // Track last beep time


// Temperature trend tracking variables
float tempHistory[5] = {0};  // Array to store temperature history
int historyIndex = 0;        // Index for temperature history array

uint8_t low_alarm_duty, high_alarm_duty, duty;
int16_t low_alarm_temp, high_alarm_temp;

// First, update the button definitions with correct positions
struct Button {
    int x, y, w, h;
    const char* label;
    bool pressed;
    bool highlighted;  // Add this line
};
// Parameters: x position, y position, width, height, label, pressed state
// Update button positions to match where they're actually drawn
Button monitorBtn = {10, 180, 145, 50, "Monitor", false, false};      // Added false for highlighted
Button emissivityBtn = {165, 180, 145, 50, "Emissivity", false, false}; // Added false for highlighted

// 5. Function declarations
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
void updateStatusDisplay();
void showLowBatteryWarning(int batteryLevel);
void handleTouchInput();
void toggleDisplayMode();
void toggleMonitoring();
void handleAutoSleep(unsigned long currentMillis, unsigned long lastActivityTime);
void updateTemperatureTrend();
void checkBatteryWarning(unsigned long currentMillis);

void checkBatteryWarning(unsigned long currentMillis) {
    static unsigned long lastWarning = 0;
    const unsigned long WARNING_INTERVAL = 60000;  // 1 minute
    
    if (CoreS3.Power.getBatteryLevel() < 10 && !CoreS3.Power.isCharging()) {
        if (currentMillis - lastWarning >= WARNING_INTERVAL) {
            showLowBatteryWarning(CoreS3.Power.getBatteryLevel());
            lastWarning = currentMillis;
        }
    }
}
void updateTemperatureTrend() {
    float currentTempF = (currentTemp / 100.0 * 9.0/5.0) + 32.0;
    tempHistory[historyIndex] = currentTempF;
    historyIndex = (historyIndex + 1) % 5;
}
void handleAutoSleep(unsigned long currentMillis, unsigned long lastActivityTime) {
    const unsigned long SLEEP_TIMEOUT = 300000;  // 5 minutes
    static bool isAsleep = false;
    
    if (!isMonitoring) {  // Only sleep if not monitoring
        if (currentMillis - lastActivityTime > SLEEP_TIMEOUT) {
            if (!isAsleep) {
                CoreS3.Display.setBrightness(0);
                isAsleep = true;
            }
        } else if (isAsleep) {
            CoreS3.Display.setBrightness(64);
            drawInterface();
            isAsleep = false;
        }
    }
}
void toggleMonitoring() {
    isMonitoring = !isMonitoring;
    drawButton(monitorBtn, isMonitoring ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON);
    if (!isMonitoring) {
        ncir2.setLEDColor(0);
        CoreS3.Speaker.stop();
    } else {
        handleTemperatureAlerts();
    }
}
void showLowBatteryWarning(int batteryLevel) {
    // Save current display area
    CoreS3.Display.fillRect(0, 35, 320, 80, COLOR_BACKGROUND);
    
    // Show warning text
    CoreS3.Display.setTextColor(COLOR_HOT);
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.drawString("LOW BATTERY!", CoreS3.Display.width()/2, 60);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString(String(batteryLevel) + "% Remaining", CoreS3.Display.width()/2, 90);
    
    // Add visual indicator
    int warningBoxWidth = 280;
    int warningBoxHeight = 4;
    int warningBoxX = (320 - warningBoxWidth) / 2;
    CoreS3.Display.fillRect(warningBoxX, 110, warningBoxWidth, warningBoxHeight, COLOR_HOT);
    
    delay(2000);  // Show warning for 2 seconds
    drawInterface();  // Redraw main interface
}
void handleTemperatureAlerts() {
    float tempC = currentTemp / 100.0;
    float tempF = (tempC * 9.0/5.0) + 32.0;
    
    String currentStatus;
    uint32_t statusColor;
    uint32_t ledColor = 0;

    // Determine temperature status
    if (!useCelsius) {
        if (tempF < TEMP_MIN_F) {
            currentStatus = "Too Cold";
            statusColor = COLOR_COLD;
            ledColor = 0x0000FF;
        } else if (tempF > TEMP_MAX_F) {
            currentStatus = "Too Hot!";
            statusColor = COLOR_HOT;
            ledColor = 0xFF0000;
        } else if (abs(tempF - TEMP_TARGET_F) <= TEMP_TOLERANCE_F) {
            currentStatus = "Perfect";
            statusColor = COLOR_GOOD;
            ledColor = 0x00FF00;
        } else {
            currentStatus = "Ready";
            statusColor = COLOR_GOOD;
            ledColor = 0x00FF00;
        }
    } else {
        // Celsius logic
        if (tempC < TEMP_MIN_C) {
            currentStatus = "Too Cold";
            statusColor = COLOR_COLD;
            ledColor = 0x0000FF;
        } else if (tempC > TEMP_MAX_C) {
            currentStatus = "Too Hot!";
            statusColor = COLOR_HOT;
            ledColor = 0xFF0000;
        } else if (abs(tempC - TEMP_TARGET_C) <= TEMP_TOLERANCE_C) {
            currentStatus = "Perfect";
            statusColor = COLOR_GOOD;
            ledColor = 0x00FF00;
        } else {
            currentStatus = "Ready";
            statusColor = COLOR_GOOD;
            ledColor = 0x00FF00;
        }
    }

    // Update LED if monitoring
    if (isMonitoring) {
        ncir2.setLEDColor(ledColor);
    }

    // Update status display if changed
    if (lastStatus != currentStatus) {
        updateStatusDisplay(currentStatus.c_str(), statusColor);
        lastStatus = currentStatus;

        // Audio alert for perfect temperature
        if (isMonitoring && (currentStatus == "Perfect")) {
            CoreS3.Speaker.tone(BEEP_FREQUENCY, BEEP_DURATION);
        }
    }
}
void updateStatusDisplay(const char* status, uint32_t color) {
    int buttonY = CoreS3.Display.height() - BUTTON_HEIGHT - MARGIN;
    int statusBoxY = buttonY - STATUS_BOX_HEIGHT - MARGIN;
    
    // Clear status area with padding
    CoreS3.Display.fillRect(MARGIN + 5, statusBoxY + 5, 
                          CoreS3.Display.width() - (MARGIN * 2) - 10, 
                          STATUS_BOX_HEIGHT - 10, COLOR_BACKGROUND);
    
    // Display status
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.setTextColor(color);
    CoreS3.Display.drawString(status, CoreS3.Display.width()/2, statusBoxY + (STATUS_BOX_HEIGHT/2));
}
void adjustEmissivity() {
    // Define constants for emissivity limits and step
    const float EMISSIVITY_MIN = 0.65f;
    const float EMISSIVITY_MAX = 1.00f;
    const float EMISSIVITY_STEP = 0.01f;

    float originalEmissivity = currentEmissivity;
    float tempEmissivity = currentEmissivity;
    
    // Ensure starting value is within limits
    if (tempEmissivity < EMISSIVITY_MIN) tempEmissivity = EMISSIVITY_MIN;
    if (tempEmissivity > EMISSIVITY_MAX) tempEmissivity = EMISSIVITY_MAX;
    
    bool valueChanged = false;

    CoreS3.Display.fillScreen(COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(COLOR_TEXT);

    // Create buttons with landscape-optimized positions
    Button upBtn = {220, 60, 80, 60, "+", false, false};      // Added highlighted state
    Button downBtn = {220, 140, 80, 60, "-", false, false};   // Added highlighted state
    Button doneBtn = {10, 180, 100, 50, "Done", false, false}; // Added highlighted state

    // Draw title
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Adjust Emissivity", 120, 30);

    // Create a larger value display box
    int valueBoxWidth = 160;
    int valueBoxHeight = 80;
    int valueBoxX = 20;  // Moved left
    int valueBoxY = 80;
    CoreS3.Display.drawRoundRect(valueBoxX, valueBoxY, valueBoxWidth, valueBoxHeight, 8, COLOR_TEXT);

    // Draw static buttons
    CoreS3.Display.fillRoundRect(upBtn.x, upBtn.y, upBtn.w, upBtn.h, 8, COLOR_BUTTON);
    CoreS3.Display.fillRoundRect(downBtn.x, downBtn.y, downBtn.w, downBtn.h, 8, COLOR_BUTTON);
    CoreS3.Display.fillRoundRect(doneBtn.x, doneBtn.y, doneBtn.w, doneBtn.h, 8, COLOR_BUTTON);

    // Draw button labels
    CoreS3.Display.setTextColor(COLOR_TEXT);
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.drawString("+", upBtn.x + upBtn.w/2, upBtn.y + upBtn.h/2);
    CoreS3.Display.drawString("-", downBtn.x + downBtn.w/2, downBtn.y + downBtn.h/2);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Done", doneBtn.x + doneBtn.w/2, doneBtn.y + doneBtn.h/2);

    bool adjusting = true;
    float lastDrawnValue = -1;  // Track last drawn value to prevent flicker

    while (adjusting) {
        // Only update display if value changed
        if (tempEmissivity != lastDrawnValue) {
            // Clear only the value display area
            CoreS3.Display.fillRect(valueBoxX + 5, valueBoxY + 5, valueBoxWidth - 10, valueBoxHeight - 10, COLOR_BACKGROUND);

            // Display current value
            char emisStr[16];
            sprintf(emisStr, "%.2f", tempEmissivity);
            CoreS3.Display.setTextSize(3);
            CoreS3.Display.drawString(emisStr, valueBoxX + (valueBoxWidth/2), valueBoxY + valueBoxHeight/2);
            
            lastDrawnValue = tempEmissivity;
        }

        CoreS3.update();
        if (CoreS3.Touch.getCount()) {
            auto touched = CoreS3.Touch.getDetail();
            if (touched.wasPressed()) {
                if (touchInButton(upBtn, touched.x, touched.y)) {
                    if (tempEmissivity < EMISSIVITY_MAX) {
                        tempEmissivity += EMISSIVITY_STEP;
                        if (tempEmissivity > EMISSIVITY_MAX) tempEmissivity = EMISSIVITY_MAX;
                        valueChanged = (tempEmissivity != originalEmissivity);
                        CoreS3.Speaker.tone(1000, AUDIO_FEEDBACK_DURATION);
                    }
                } else if (touchInButton(downBtn, touched.x, touched.y)) {
                    if (tempEmissivity > EMISSIVITY_MIN) {
                        tempEmissivity -= EMISSIVITY_STEP;
                        if (tempEmissivity < EMISSIVITY_MIN) tempEmissivity = EMISSIVITY_MIN;
                        valueChanged = (tempEmissivity != originalEmissivity);
                        CoreS3.Speaker.tone(1000, AUDIO_FEEDBACK_DURATION);
                    }
                } else if (touchInButton(doneBtn, touched.x, touched.y)) {
                    adjusting = false;
                    CoreS3.Speaker.tone(1000, AUDIO_FEEDBACK_DURATION);
                }
                delay(TOUCH_DEBOUNCE);  // Shorter debounce
            }
        } else {
            // Reset button highlights when no touch is detected
            if (upBtn.highlighted || downBtn.highlighted || doneBtn.highlighted) {
                upBtn.highlighted = false;
                downBtn.highlighted = false;
                doneBtn.highlighted = false;
                drawButton(upBtn, COLOR_BUTTON);
                drawButton(downBtn, COLOR_BUTTON);
                drawButton(doneBtn, COLOR_BUTTON);
            }
        }
    }
 // If emissivity was changed, show confirmation screen
    if (valueChanged) {
        CoreS3.Display.fillScreen(COLOR_BACKGROUND);
        CoreS3.Display.setTextColor(COLOR_TEXT);
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.drawString("Emissivity Changed", CoreS3.Display.width()/2, 40);  // Changed from 60
        CoreS3.Display.drawString("Restart Required", CoreS3.Display.width()/2, 80);    // Changed from 100

        // Show old and new values
        char oldStr[32], newStr[32];
        sprintf(oldStr, "Old: %.2f", originalEmissivity);
        sprintf(newStr, "New: %.2f", tempEmissivity);
        CoreS3.Display.drawString(oldStr, CoreS3.Display.width()/2, 120);              // Changed from 140
        CoreS3.Display.drawString(newStr, CoreS3.Display.width()/2, 150);              // Changed from 170

        // Create confirm/cancel buttons - moved up
        Button confirmBtn = {10, 190, 145, 50, "Restart", false};                      // Changed from 220
        Button cancelBtn = {165, 190, 145, 50, "Cancel", false};                       // Changed from 220

        CoreS3.Display.fillRoundRect(confirmBtn.x, confirmBtn.y, confirmBtn.w, confirmBtn.h, 8, COLOR_GOOD);
        CoreS3.Display.fillRoundRect(cancelBtn.x, cancelBtn.y, cancelBtn.w, cancelBtn.h, 8, COLOR_HOT);

        CoreS3.Display.setTextColor(COLOR_TEXT);
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.drawString("Restart", confirmBtn.x + confirmBtn.w/2, confirmBtn.y + confirmBtn.h/2);
        CoreS3.Display.drawString("Cancel", cancelBtn.x + cancelBtn.w/2, cancelBtn.y + cancelBtn.h/2);

        // Wait for user choice
        bool waiting = true;
        while (waiting) {
            CoreS3.update();
            if (CoreS3.Touch.getCount()) {
                auto touched = CoreS3.Touch.getDetail();
                if (touched.wasPressed()) {
                    if (touchInButton(confirmBtn, touched.x, touched.y)) {
                        // Save new emissivity and restart
                        currentEmissivity = tempEmissivity;  // Update global variable
                        ncir2.setEmissivity(currentEmissivity);  // Set the new emissivity on sensor
                        CoreS3.Speaker.tone(1000, 50);
                        delay(500);
                        ESP.restart();
                    } else if (touchInButton(cancelBtn, touched.x, touched.y)) {
                        CoreS3.Speaker.tone(1000, 50);
                        waiting = false;
                    }
                }
            }
            delay(10);
        }
    }

    // Return to main interface
    drawInterface();
    updateTemperatureDisplay(currentTemp);
    handleTemperatureAlerts();
    lastDisplayTemp = -999;
    lastStatus = "";
}
void updateTemperatureDisplay(int16_t temp) {
    // Clear temperature area with padding
    int tempBoxY = HEADER_HEIGHT + MARGIN;
    CoreS3.Display.fillRect(MARGIN + 5, tempBoxY + 5, 
                          CoreS3.Display.width() - (MARGIN * 2) - 10, 
                          TEMP_BOX_HEIGHT - 10, COLOR_BACKGROUND);
    
    // Convert and format temperature
    float displayTemp = temp / 100.0;
    char tempStr[32];
    
    if (useCelsius) {
        sprintf(tempStr, "%dC", (int)round(displayTemp));
    } else {
        float tempF = (displayTemp * 9.0/5.0) + 32.0;
        sprintf(tempStr, "%dF", (int)round(tempF));
    }
    
    // Display temperature with larger text
    CoreS3.Display.setTextSize(5);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    CoreS3.Display.drawString(tempStr, CoreS3.Display.width()/2, tempBoxY + (TEMP_BOX_HEIGHT/2));
}
bool touchInButton(Button &btn, int32_t touch_x, int32_t touch_y) {
    bool hit = (touch_x >= btn.x && touch_x <= (btn.x + btn.w) &&
                touch_y >= btn.y && touch_y <= (btn.y + btn.h));
    
    if (hit) {
        // Provide immediate visual feedback
        btn.highlighted = true;
        drawButton(btn, BUTTON_HIGHLIGHT);
    }
    
    return hit;
}
int16_t celsiusToFahrenheit(int16_t celsius) {
    return (celsius * 9 / 5) + 3200;
}
int16_t fahrenheitToCelsius(int16_t fahrenheit) {
    return ((fahrenheit - 3200) * 5 / 9);
}
void drawButton(Button &btn, uint32_t color) {
    // Draw button with rounded corners
    CoreS3.Display.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 10, color);
    CoreS3.Display.drawRoundRect(btn.x, btn.y, btn.w, btn.h, 10, COLOR_TEXT);
    
    // Draw button text
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    int yOffset = btn.highlighted ? 1 : 0;
    CoreS3.Display.drawString(btn.label, 
                            btn.x + (btn.w/2), 
                            btn.y + (btn.h/2) + yOffset);
}
void drawBatteryStatus() {
    // Battery indicator position in header
    int batteryX = CoreS3.Display.width() - 45;
    int batteryY = 5;
    
    // Clear battery area
    CoreS3.Display.fillRect(batteryX - 12, batteryY, 50, 20, COLOR_BACKGROUND);
    
    // Get battery status
    bool bat_ischarging = CoreS3.Power.isCharging();
    int bat_level = CoreS3.Power.getBatteryLevel();
    
    // Ensure battery level is within bounds
    bat_level = constrain(bat_level, 0, 100);
    
    // Draw battery outline with rounded corners
    CoreS3.Display.drawRoundRect(batteryX, batteryY, 35, 18, 3, COLOR_TEXT);
    CoreS3.Display.fillRoundRect(batteryX + 35, batteryY + 4, 3, 10, 1, COLOR_TEXT);
    
    // Calculate fill width
    int fillWidth = ((bat_level * 31) / 100);
    fillWidth = constrain(fillWidth, 0, 31);
    
    // Determine battery color
    uint32_t batteryColor;
    if (bat_ischarging) {
        batteryColor = COLOR_CHARGING;
    } else if (bat_level <= 20) {
        batteryColor = COLOR_BATTERY_LOW;
    } else if (bat_level <= 50) {
        batteryColor = COLOR_BATTERY_MED;
    } else {
        batteryColor = COLOR_BATTERY_GOOD;
    }
    
    // Fill battery indicator with rounded corners
    if (fillWidth > 0) {
        CoreS3.Display.fillRoundRect(batteryX + 2, batteryY + 2, fillWidth, 14, 2, batteryColor);
    }
    
    // Draw charging indicator
    if (bat_ischarging) {
        CoreS3.Display.setTextSize(1);
        CoreS3.Display.setTextColor(COLOR_TEXT);
        CoreS3.Display.drawString("⚡", batteryX - 12, batteryY + 9);
    }
}
void drawInterface() {
    // Clear screen with background color
    CoreS3.Display.fillScreen(COLOR_BACKGROUND);
    
    // Calculate layout dimensions
    int screenWidth = CoreS3.Display.width();
    int screenHeight = CoreS3.Display.height();
    int contentWidth = screenWidth - (MARGIN * 2);
    
    // Draw header area with title and battery
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    CoreS3.Display.drawString("Terp Timer", MARGIN + 70, HEADER_HEIGHT/2);

    // Temperature display box - now shorter
    int tempBoxY = HEADER_HEIGHT + MARGIN;
    CoreS3.Display.drawRoundRect(MARGIN, tempBoxY, contentWidth, TEMP_BOX_HEIGHT, 8, COLOR_TEXT);
    
    // Status box - Added extra margin from temperature box
    int buttonY = screenHeight - BUTTON_HEIGHT - MARGIN;
    int statusBoxY = buttonY - STATUS_BOX_HEIGHT - MARGIN;
    CoreS3.Display.drawRoundRect(MARGIN, statusBoxY, contentWidth, STATUS_BOX_HEIGHT, 8, COLOR_TEXT);

    // Update button positions
    int buttonWidth = (contentWidth - BUTTON_SPACING) / 2;
    
    // Update button struct positions
    monitorBtn = {MARGIN, buttonY, buttonWidth, BUTTON_HEIGHT, "Monitor", false, false};
    emissivityBtn = {MARGIN + buttonWidth + BUTTON_SPACING, buttonY, buttonWidth, BUTTON_HEIGHT, "Emissivity", false, false};

    // Draw buttons
    drawButton(monitorBtn, isMonitoring ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON);
    drawButton(emissivityBtn, COLOR_BUTTON);

    // Draw battery status last
    drawBatteryStatus();
}
bool selectTemperatureUnit() {
    CoreS3.Display.fillScreen(COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.drawString("Select Unit", CoreS3.Display.width()/2, 40);
    
    // Landscape-optimized button positions
    Button celsiusBtn = {20, 80, 130, 100, "C", false};    // Left side
    Button fahrenheitBtn = {170, 80, 130, 100, "F", false}; // Right side
    
    CoreS3.Display.fillRoundRect(celsiusBtn.x, celsiusBtn.y, celsiusBtn.w, celsiusBtn.h, 8, COLOR_BUTTON);
    CoreS3.Display.fillRoundRect(fahrenheitBtn.x, fahrenheitBtn.y, fahrenheitBtn.w, fahrenheitBtn.h, 8, COLOR_BUTTON);
    
    CoreS3.Display.setTextSize(4);  // Larger text for temperature units
    CoreS3.Display.setTextColor(COLOR_TEXT);
    CoreS3.Display.drawString("C", celsiusBtn.x + celsiusBtn.w/2, celsiusBtn.y + celsiusBtn.h/2);
    CoreS3.Display.drawString("F", fahrenheitBtn.x + fahrenheitBtn.w/2, fahrenheitBtn.y + fahrenheitBtn.h/2);
    
    // Add descriptive text below buttons
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Celsius", celsiusBtn.x + celsiusBtn.w/2, celsiusBtn.y + celsiusBtn.h + 20);
    CoreS3.Display.drawString("Fahrenheit", fahrenheitBtn.x + fahrenheitBtn.w/2, fahrenheitBtn.y + fahrenheitBtn.h + 20);
    
    while (true) {
        CoreS3.update();
        auto touched = CoreS3.Touch.getDetail();
        if (touched.wasPressed()) {
            if (touchInButton(celsiusBtn, touched.x, touched.y)) {
                return true;
            }
            if (touchInButton(fahrenheitBtn, touched.x, touched.y)) {
                return false;
            }
        }
        delay(10);
    }
}
void setup() {
    Serial.begin(115200);
    auto cfg = M5.config();
    cfg.clear_display = true;    
    CoreS3.begin(cfg);
    
    // Initialize power management first
    CoreS3.Power.begin();
    CoreS3.Power.setChargeCurrent(900);
    delay(500);  // Give power system time to stabilize

    // Initialize display and speaker
    CoreS3.Display.begin();
    CoreS3.Display.setRotation(1);
    CoreS3.Display.setBrightness(64);
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Speaker.begin();
    CoreS3.Speaker.setVolume(128);
    
    // Clear display and show initialization message
    CoreS3.Display.fillScreen(COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Initializing Sensor", CoreS3.Display.width()/2, 80);
    
    // Robust sensor initialization with multiple retries
    int retryCount = 0;
    bool sensorInitialized = false;
    const int MAX_RETRIES = 5;  // Increased from 3 to 5 retries
    
    while (!sensorInitialized && retryCount < MAX_RETRIES) {
        // Complete I2C reset
        Wire.end();
        delay(100);  // Increased delay for better stability
        
        // Initialize I2C with lower speed for stability
        Wire.begin(2, 1, 50000);  // Reduced from 100000 to 50000 Hz
        delay(250);
        
        CoreS3.Display.fillRect(0, 120, CoreS3.Display.width(), 30, COLOR_BACKGROUND);
        CoreS3.Display.drawString("Attempt " + String(retryCount + 1) + "/" + String(MAX_RETRIES), 
                                CoreS3.Display.width()/2, 120);
        
        if (ncir2.begin(&Wire, 2, 1, M5UNIT_NCIR2_DEFAULT_ADDR)) {
            // Additional verification step
            delay(100);
            int16_t testTemp = ncir2.getTempValue();
            if (testTemp != 0 && testTemp > -1000 && testTemp < 10000) {  // Basic sanity check
                sensorInitialized = true;
                ncir2.setEmissivity(currentEmissivity);
                
                // Reset sensor configuration
                ncir2.setConfig();
                delay(100);  // Allow config to settle
                
                // Double-check configuration
                if (ncir2.getTempValue() != 0) {
                    break;  // Successfully initialized
                }
            }
        }
        
        retryCount++;
        delay(500);  // Increased delay between retries
    }
    
    if (!sensorInitialized) {
        // Show error and instructions
        CoreS3.Display.fillScreen(COLOR_BACKGROUND);
        CoreS3.Display.setTextColor(COLOR_HOT);
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.drawString("Sensor Init Failed!", CoreS3.Display.width()/2, 60);
        CoreS3.Display.drawString("Please try:", CoreS3.Display.width()/2, 100);
        CoreS3.Display.drawString("1. Unplug sensor", CoreS3.Display.width()/2, 130);
        CoreS3.Display.drawString("2. Wait 10 seconds", CoreS3.Display.width()/2, 160);
        CoreS3.Display.drawString("3. Reconnect sensor", CoreS3.Display.width()/2, 190);
        
        // Wait for reset or charging
        while (1) {
            if (CoreS3.Power.isCharging()) {
                delay(1000);  // Wait a second before restart
                ESP.restart();
            }
            delay(100);
        }
    }

    // Warm-up sequence with progress bar
    CoreS3.Display.fillScreen(COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    
    // Title - larger and clear
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.drawString("Sensor Warming Up", CoreS3.Display.width()/2, 40);
    
    // Progress bar parameters
    const int barWidth = 280;
    const int barHeight = 30;
    const int barX = (CoreS3.Display.width() - barWidth) / 2;
    const int barY = 100;
    const int warmupTime = 10000; // 10 seconds warmup
    const int updateInterval = 100; // Update every 100ms
    const int totalSteps = warmupTime / updateInterval;
    
    // Draw progress bar outline
    CoreS3.Display.drawRect(barX, barY, barWidth, barHeight, COLOR_TEXT);
    
    // Define fixed text positions
    const int stabilityY = 150;  // Stability text position
    const int temperatureY = 190;  // Temperature text position
    const int percentY = 230;  // Percentage text position
    
    // Temperature stabilization variables
    float tempSum = 0;
    int readings = 0;
    float lastTemp = 0;
    float stabilityThreshold = 0.5; // °C
    int stableReadings = 0;
    const int requiredStableReadings = 100;

    // Pre-clear text areas once
    CoreS3.Display.fillRect(0, stabilityY - 15, CoreS3.Display.width(), 30, COLOR_BACKGROUND);
    CoreS3.Display.fillRect(0, temperatureY - 15, CoreS3.Display.width(), 30, COLOR_BACKGROUND);
    CoreS3.Display.fillRect(0, percentY - 15, CoreS3.Display.width(), 30, COLOR_BACKGROUND);

    // Warm-up loop
    for (int i = 0; i < totalSteps; i++) {
        // Update progress bar
        int progress = (i * barWidth) / totalSteps;
        CoreS3.Display.fillRect(barX + 1, barY + 1, progress, barHeight - 2, COLOR_GOOD);
        
        // Get temperature reading
        float currentTemp = ncir2.getTempValue() / 100.0;
        tempSum += currentTemp;
        readings++;

        // Clear specific text areas with minimum flicker
        CoreS3.Display.fillRect(0, stabilityY - 15, CoreS3.Display.width(), 30, COLOR_BACKGROUND);
        CoreS3.Display.fillRect(0, temperatureY - 15, CoreS3.Display.width(), 30, COLOR_BACKGROUND);
        CoreS3.Display.fillRect(0, percentY - 15, CoreS3.Display.width(), 30, COLOR_BACKGROUND);

        // Check temperature stability
        if (readings > 1) {
            if (abs(currentTemp - lastTemp) < stabilityThreshold) {
                stableReadings++;
                CoreS3.Display.setTextSize(2);
                String stabilityText = "Stabilizing: " + String(stableReadings) + "/" + String(requiredStableReadings);
                CoreS3.Display.drawString(stabilityText, CoreS3.Display.width()/2, stabilityY);
            } else {
                stableReadings = 0;
            }
        }
        lastTemp = currentTemp;
        
        // Progress percentage
        int percent = ((i + 1) * 100) / totalSteps;
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.drawString(String(percent) + "%", CoreS3.Display.width()/2, percentY);
        
        delay(updateInterval);
    }

    // Success indication with victory sound
    CoreS3.Display.fillRect(0, stabilityY - 15, CoreS3.Display.width(), 30, COLOR_BACKGROUND);
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.drawString("Sensor Ready!", CoreS3.Display.width()/2, stabilityY);

    // Victory sound sequence
    CoreS3.Speaker.setVolume(128);
    CoreS3.Speaker.tone(784, 100);  // G5
    delay(100);
    CoreS3.Speaker.tone(988, 100);  // B5
    delay(100);
    CoreS3.Speaker.tone(1319, 200); // E6
    delay(200);
    CoreS3.Speaker.tone(1047, 400); // C6
    delay(400);

    delay(500);

    // Initialize temperature unit selection
    useCelsius = selectTemperatureUnit();
    isMonitoring = false;
    
    // Initialize display interface
    drawInterface();
    ncir2.setLEDColor(0);
    drawBatteryStatus();
}
void loop() {
    CoreS3.update();
    
    static unsigned long lastUpdate = 0;
    static unsigned long lastBatteryUpdate = 0;
    static unsigned long lastTouch = 0;
    static bool touchHandled = false;
    unsigned long currentMillis = millis();
    
    // Update temperature and status
    if (currentMillis - lastUpdate >= 100) {  // Every 100ms
        // Get new temperature reading
        int16_t newTemp = ncir2.getTempValue();
        
        // Only update if temperature changed significantly
        if (abs(newTemp - lastDisplayTemp) >= (TEMP_THRESHOLD * 100)) {
            currentTemp = newTemp;
            updateTemperatureDisplay(currentTemp);
            lastDisplayTemp = newTemp;
            
            if (isMonitoring) {
                handleTemperatureAlerts();
            }
        }
        
        lastUpdate = currentMillis;
    }
    
    // Update battery status
    if (currentMillis - lastBatteryUpdate >= 2000) {  // Every 2 seconds
        lastBatLevel = -1;
        lastChargeState = !CoreS3.Power.isCharging();
        drawBatteryStatus();
        lastBatteryUpdate = currentMillis;
    }

    // Improved touch handling with better responsiveness
    if (CoreS3.Touch.getCount()) {
        auto touched = CoreS3.Touch.getDetail();
        if (touched.wasPressed() && !touchHandled) {
            if (currentMillis - lastTouch >= TOUCH_DEBOUNCE) {
                bool buttonPressed = false;
                
                // Monitor button
                if (touchInButton(monitorBtn, touched.x, touched.y)) {
                    toggleMonitoring();
                    CoreS3.Speaker.tone(1000, AUDIO_FEEDBACK_DURATION);
                    buttonPressed = true;
                }
                // Emissivity button
                else if (touchInButton(emissivityBtn, touched.x, touched.y)) {
                    adjustEmissivity();
                    CoreS3.Speaker.tone(1000, AUDIO_FEEDBACK_DURATION);
                    buttonPressed = true;
                }
                
                if (buttonPressed) {
                    touchHandled = true;
                    lastTouch = currentMillis;
                }
            }
        }
    } else {
        // Reset button states when no touch is detected
        if (monitorBtn.highlighted || emissivityBtn.highlighted) {
            monitorBtn.highlighted = false;
            emissivityBtn.highlighted = false;
            drawButton(monitorBtn, isMonitoring ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON);
            drawButton(emissivityBtn, COLOR_BUTTON);
        }
        touchHandled = false;
    }

    // Handle LED color based on monitoring state
    if (isMonitoring) {
        handleTemperatureAlerts();
    } else {
        ncir2.setLEDColor(0);  // Turn off LED when not monitoring
    }
    
    // Check for low battery warning
    static unsigned long lastBatteryWarning = 0;
    if (CoreS3.Power.getBatteryLevel() <= 10 && !CoreS3.Power.isCharging()) {
        if (currentMillis - lastBatteryWarning >= 60000) {  // Show warning every minute
            showLowBatteryWarning(CoreS3.Power.getBatteryLevel());
            lastBatteryWarning = currentMillis;
        }
    }
}