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

// Emissivity constants and variables
const float EMISSIVITY_MIN = 0.1;
const float EMISSIVITY_MAX = 1.0;
const float EMISSIVITY_STEP = 0.05;
float currentEmissivity = 0.95;  // Default value

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
const int16_t TEMP_DISPLAY_THRESHOLD = 100;  // Only update display if temp changes by 1.0°C/F
const int TEMP_SAMPLES = 15;  // Increased sample size for better smoothing
const unsigned long TEMP_UPDATE_INTERVAL = 250;  // Reduced update frequency
const int DISPLAY_UPDATE_THRESHOLD = 1;  // Minimum change in temperature to trigger display update
const int DISPLAY_SMOOTHING_SAMPLES = 10; // Number of samples for display smoothing
unsigned long lastBeepTime = 0;  // Track last beep time

// Temperature trend tracking variables
float tempHistory[5] = {0};  // Array to store temperature history
int historyIndex = 0;        // Index for temperature history array

// Temperature smoothing variables
int16_t tempReadings[TEMP_SAMPLES];  // Array to store temperature readings
int tempIndex = 0;  // Index for current reading
unsigned long lastTempUpdate = 0;  // Last update time
float lastDisplayedTemp = 0;
float displaySmoothing[DISPLAY_SMOOTHING_SAMPLES] = {0};
int displaySmoothingIndex = 0;
float smoothedDisplayTemp = 0;
const float SMOOTHING_FACTOR = 0.3;  // Lower = smoother (try values between 0.05 and 0.2
extern const float SMOOTHING_FACTOR;  // If not already defined
bool useRawTemp = false;  // false = smoothed (averaged), true = raw


uint8_t low_alarm_duty, high_alarm_duty, duty;
int16_t low_alarm_temp, high_alarm_temp;

// Button structure
struct Button {
    int x, y, w, h;
    const char* label;
    bool pressed;
};

// Parameters: x position, y position, width, height, label, pressed state
Button monitorBtn = {10, 180, 145, 50, "Monitor", false};      // Bottom left
Button emissivityBtn = {165, 180, 145, 50, "Emissivity", false}; // Bottom right
Button displayModeBtn = {10, 10, 30, 30, "Mode", false};

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
    float tempF = (tempC * 9.0 / 5.0) + 32.0;

    String currentStatus;
    uint32_t statusColor;
    uint32_t ledColor = 0;  // Default LED color (off)

    // Determine status and LED color based on temperature
    if (!useCelsius) {  // Using Fahrenheit
        if (tempF < TEMP_MIN_F) {
            currentStatus = "Too Cold";
            statusColor = COLOR_COLD;
            ledColor = 0x0000FF;  // Blue
        } else if (tempF > TEMP_MAX_F) {
            currentStatus = "Too Hot!";
            statusColor = COLOR_HOT;
            ledColor = 0xFF0000;  // Red
        } else {
            currentStatus = "Good";
            statusColor = COLOR_GOOD;
            ledColor = 0x00FF00;  // Green
        }
    } else {  // Using Celsius
        if (tempC < TEMP_MIN_C) {
            currentStatus = "Too Cold";
            statusColor = COLOR_COLD;
            ledColor = 0x0000FF;  // Blue
        } else if (tempC > TEMP_MAX_C) {
            currentStatus = "Too Hot!";
            statusColor = COLOR_HOT;
            ledColor = 0xFF0000;  // Red
        } else {
            currentStatus = "Good";
            statusColor = COLOR_GOOD;
            ledColor = 0x00FF00;  // Green
        }
    }

    // Update LED only if monitoring is active
    if (isMonitoring) {
        ncir2.setLEDColor(ledColor);
    } else {
        ncir2.setLEDColor(0);  // LED off when not monitoring
    }

    // Update status display if changed
    if (lastStatus != currentStatus) {
        // Clear previous status area
        CoreS3.Display.fillRect(15, 130, 290, 35, COLOR_BACKGROUND);

        // Draw new status
        CoreS3.Display.setTextSize(3);
        CoreS3.Display.setTextColor(statusColor);
        CoreS3.Display.drawString(currentStatus, CoreS3.Display.width() / 2, 147);

        lastStatus = currentStatus;

        // Handle audio alerts if monitoring
        if (isMonitoring && currentStatus != "Good") {
            unsigned long currentTime = millis();
            if (currentTime - lastBeepTime >= BEEP_INTERVAL) {
                M5.Speaker.tone(BEEP_FREQUENCY, BEEP_DURATION);
                lastBeepTime = currentTime;
            }
        }
    }
}

void updateStatusDisplay(const char* status, uint32_t color) {
    // Clear previous status
    CoreS3.Display.fillRect(15, 130, 290, 35, COLOR_BACKGROUND);
    
    // Display status
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.setTextColor(color);
    CoreS3.Display.drawString(status, CoreS3.Display.width()/2, 147);  // Centered in status box
}

void adjustEmissivity() {
    float originalEmissivity = currentEmissivity;
    bool valueChanged = false;

    CoreS3.Display.fillScreen(COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(COLOR_TEXT);

    // Create buttons with landscape-optimized positions
    Button upBtn = {220, 60, 80, 60, "+", false};      // Right side, upper
    Button downBtn = {220, 140, 80, 60, "-", false};   // Right side, lower
    Button doneBtn = {10, 180, 100, 50, "Done", false}; // Bottom left

    // Draw title
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Adjust Emissivity", 120, 30); // Centered

    // Create a larger value display box
    int valueBoxWidth = 160;
    int valueBoxHeight = 80;
    int valueBoxX = 30;  // Left side
    int valueBoxY = 80;  // Center vertically
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
    while (adjusting) {
        // Clear only the value display area
        CoreS3.Display.fillRect(valueBoxX + 5, valueBoxY + 5, valueBoxWidth - 10, valueBoxHeight - 10, COLOR_BACKGROUND);

        // Display current value
        char emisStr[16];
        sprintf(emisStr, "%.2f", currentEmissivity);
        CoreS3.Display.setTextSize(3);
        CoreS3.Display.drawString(emisStr, CoreS3.Display.width()/2, valueBoxY + valueBoxHeight/2);

        CoreS3.update();
        if (CoreS3.Touch.getCount()) {
            auto touched = CoreS3.Touch.getDetail();
            if (touched.wasPressed()) {
                if (touchInButton(upBtn, touched.x, touched.y)) {
                    if (currentEmissivity < EMISSIVITY_MAX) {
                        currentEmissivity += EMISSIVITY_STEP;
                        if (currentEmissivity > EMISSIVITY_MAX) currentEmissivity = EMISSIVITY_MAX;
                        valueChanged = (currentEmissivity != originalEmissivity);
                    }
                } else if (touchInButton(downBtn, touched.x, touched.y)) {
                    if (currentEmissivity > EMISSIVITY_MIN) {
                        currentEmissivity -= EMISSIVITY_STEP;
                        if (currentEmissivity < EMISSIVITY_MIN) currentEmissivity = EMISSIVITY_MIN;
                        valueChanged = (currentEmissivity != originalEmissivity);
                    }
                } else if (touchInButton(doneBtn, touched.x, touched.y)) {
                    adjusting = false;
                }
                delay(200);  // Debounce
            }
        }
    }

    // If emissivity was changed, show confirmation screen
    if (valueChanged) {
        CoreS3.Display.fillScreen(COLOR_BACKGROUND);
        CoreS3.Display.setTextColor(COLOR_TEXT);
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.drawString("Emissivity Changed", CoreS3.Display.width()/2, 60);
        CoreS3.Display.drawString("Restart Required", CoreS3.Display.width()/2, 100);

        // Show old and new values
        char oldStr[32], newStr[32];
        sprintf(oldStr, "Old: %.2f", originalEmissivity);
        sprintf(newStr, "New: %.2f", currentEmissivity);
        CoreS3.Display.drawString(oldStr, CoreS3.Display.width()/2, 140);
        CoreS3.Display.drawString(newStr, CoreS3.Display.width()/2, 170);

        // Create confirm/cancel buttons
        Button confirmBtn = {10, 220, 145, 50, "Restart", false};
        Button cancelBtn = {165, 220, 145, 50, "Cancel", false};

        CoreS3.Display.fillRoundRect(confirmBtn.x, confirmBtn.y, confirmBtn.w, confirmBtn.h, 8, COLOR_GOOD);
        CoreS3.Display.fillRoundRect(cancelBtn.x, cancelBtn.y, cancelBtn.w, cancelBtn.h, 8, COLOR_HOT);

        CoreS3.Display.setTextColor(COLOR_TEXT);
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.drawString("Restart", confirmBtn.x + confirmBtn.w/2, confirmBtn.y + confirmBtn.h/2);
        CoreS3.Display.drawString("Cancel", cancelBtn.x + cancelBtn.w/2, cancelBtn.y + cancelBtn.h/2);

        // Wait for user choice
        while (true) {
            CoreS3.update();
            if (CoreS3.Touch.getCount()) {
                auto touched = CoreS3.Touch.getDetail();
                if (touched.wasPressed()) {
                    if (touchInButton(confirmBtn, touched.x, touched.y)) {
                        // Save new emissivity and restart
                        ncir2.setEmissivity(currentEmissivity);
                        delay(500);
                        ESP.restart();  // Restart the device
                    } else if (touchInButton(cancelBtn, touched.x, touched.y)) {
                        // Revert to original value
                        currentEmissivity = originalEmissivity;
                        break;
                    }
                }
            }
            delay(10);
        }
    }

    // Return to main interface
    drawInterface();
    currentTemp = getSmoothedTemperature();
    updateTemperatureDisplay(currentTemp);
    handleTemperatureAlerts();
    lastDisplayTemp = -999;
    lastStatus = "";
}

void updateTemperatureDisplay(int16_t temp) {
    static float previousTemp = 0;
    static bool firstReading = true;
    float currentTemp = temp / 100.0;
    
    // Initialize on first reading
    if (firstReading) {
        previousTemp = currentTemp;
        smoothedDisplayTemp = currentTemp;
        lastDisplayedTemp = currentTemp;
        firstReading = false;
    }

    // Calculate display temperature based on mode
    float displayTemp;
    if (useRawTemp) {
        displayTemp = currentTemp;  // Use raw temperature
    } else {
        // Increased smoothing (reduced from 0.1 to 0.05 for more stability)
        const float DISPLAY_SMOOTHING = 0.05;
        smoothedDisplayTemp = (DISPLAY_SMOOTHING * currentTemp) + 
                            ((1.0 - DISPLAY_SMOOTHING) * previousTemp);
        displayTemp = smoothedDisplayTemp;
    }
    
    // Increased threshold to prevent minor fluctuations (from 0.2 to 0.5)
    if (useRawTemp || abs(displayTemp - lastDisplayedTemp) >= 0.5) {
        // Only update display if the rounded value has changed
        int currentRounded = (int)round(displayTemp);
        int lastRounded = (int)round(lastDisplayedTemp);
        
        if (currentRounded != lastRounded || firstReading) {
            // Clear previous temperature display area
            CoreS3.Display.fillRect(15, 40, 290, 70, COLOR_BACKGROUND);
            
            // Format temperature string with NO decimal places
            char tempStr[32];
            if (useCelsius) {
                sprintf(tempStr, "%dC", currentRounded);
            } else {
                float tempF = (displayTemp * 9.0 / 5.0) + 32.0;
                sprintf(tempStr, "%dF", (int)round(tempF));
            }
            
            CoreS3.Display.setTextSize(5);
            CoreS3.Display.setTextColor(COLOR_TEXT);
            CoreS3.Display.drawString(tempStr, CoreS3.Display.width() / 2, 75);
            
            lastDisplayedTemp = displayTemp;
        }
        previousTemp = displayTemp;
    }
}

int16_t getSmoothedTemperature() {
    // Get raw temperature
    int16_t rawTemp = ncir2.getTempValue();
    
    // Add to rolling average with more weight on recent readings
    tempReadings[tempIndex] = rawTemp;
    tempIndex = (tempIndex + 1) % TEMP_SAMPLES;
    
    // Calculate weighted average
    long sum = 0;
    int weights = 0;
    for(int i = 0; i < TEMP_SAMPLES; i++) {
        int weight = i + 1;  // More recent readings get higher weight
        sum += tempReadings[(tempIndex - i + TEMP_SAMPLES) % TEMP_SAMPLES] * weight;
        weights += weight;
    }
    
    return sum / weights;
}

bool touchInButton(Button& btn, int32_t touch_x, int32_t touch_y) {
    return (touch_x >= btn.x && touch_x <= btn.x + btn.w &&
            touch_y >= btn.y && touch_y <= btn.y + btn.h);
}

int16_t celsiusToFahrenheit(int16_t celsius) {
    return (celsius * 9 / 5) + 3200;
}

int16_t fahrenheitToCelsius(int16_t fahrenheit) {
    return ((fahrenheit - 3200) * 5 / 9);
}

void drawButton(Button &btn, uint32_t color) {
    // Draw button background
    CoreS3.Display.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 10, color);
    
    // Draw button border for better visibility
    CoreS3.Display.drawRoundRect(btn.x, btn.y, btn.w, btn.h, 10, COLOR_TEXT);
    
    // Draw button text
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    CoreS3.Display.drawString(btn.label, btn.x + (btn.w/2), btn.y + (btn.h/2));
}

void drawBatteryStatus() {
    bool bat_ischarging = CoreS3.Power.isCharging();
    int bat_level = CoreS3.Power.getBatteryLevel();
    
    // Remove this condition to ensure battery always draws
    // if (lastBatLevel == -1 || lastBatLevel != bat_level || lastChargeState != bat_ischarging) {
    
    // Position battery in top right corner
    int batteryX = CoreS3.Display.width() - 60;  // 60 pixels from right edge
    int batteryY = 5;  // 5 pixels from top
    
    // Clear the battery area
    CoreS3.Display.fillRect(batteryX - 10, batteryY, 70, 35, COLOR_BACKGROUND);
    
    // Draw battery outline
    CoreS3.Display.drawRect(batteryX, batteryY, 40, 20, COLOR_TEXT);
    CoreS3.Display.fillRect(batteryX + 40, batteryY + 5, 4, 10, COLOR_TEXT);
    
    // Calculate fill width (36 pixels is the maximum fillable width)
    int fillWidth = (bat_level * 36) / 100;
    
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
    
    // Fill battery indicator
    if (fillWidth > 0) {
        CoreS3.Display.fillRect(batteryX + 2, batteryY + 2, fillWidth, 16, batteryColor);
    }
    
    // Draw charging icon if charging
    if (bat_ischarging) {
        CoreS3.Display.setTextSize(1);
        CoreS3.Display.setTextColor(COLOR_TEXT);
        CoreS3.Display.drawString("⚡", batteryX - 10, batteryY + 10);
    }
    
    // Always show percentage
    CoreS3.Display.setTextSize(1);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    CoreS3.Display.drawString(String(bat_level) + "%", batteryX, batteryY + 25);
    
    // Update last known values
    lastBatLevel = bat_level;
    lastChargeState = bat_ischarging;
    // }  // Remove the closing brace of the condition
}

void drawInterface() {
    // Clear screen with background color
    CoreS3.Display.fillScreen(COLOR_BACKGROUND);

    // Draw the mode toggle button
    drawButton(displayModeBtn, useRawTemp ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON);
    // Button changes color based on mode: active (green) for raw, normal for smoothed
    
    // Show RAW or AVG text below button
    CoreS3.Display.setTextSize(1);
    CoreS3.Display.drawString(useRawTemp ? "RAW" : "AVG", 
                            displayModeBtn.x + (displayModeBtn.w/2), 
                            displayModeBtn.y + displayModeBtn.h + 5);

    // Draw header area
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(COLOR_TEXT);

    // Title on left side of header
    CoreS3.Display.drawString("Terp Timer", 80, 15);

    // Draw main temperature box (larger and centered)
    CoreS3.Display.drawRect(10, 35, 300, 80, COLOR_TEXT);  // Made taller

    // Draw status box (below temperature)
    CoreS3.Display.drawRect(10, 125, 300, 45, COLOR_TEXT);  // Adjusted height

    // Draw bottom buttons with better spacing
    drawButton(monitorBtn, isMonitoring ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON);
    drawButton(emissivityBtn, COLOR_BUTTON);

    // Draw battery status in top right
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
    auto cfg = M5.config();
    cfg.clear_display = true;    // Ensure display is cleared
    CoreS3.begin(cfg);
    
    preferences.begin("terpTimer", false);
    useRawTemp = preferences.getBool("rawMode", false);  // Load saved mode

    // When mode changes
    preferences.putBool("rawMode", useRawTemp);  // Save mode
    
    // Initialize power management first
    CoreS3.Power.begin();
    CoreS3.Power.setChargeCurrent(900);
    delay(500);  // Allow power to stabilize

      // Initialize speaker
    CoreS3.Speaker.begin();
    CoreS3.Speaker.setVolume(128);  // Set initial volume (0-255)
    
    // Check battery level and show warning if low
    int batteryLevel = CoreS3.Power.getBatteryLevel();
    bool isCharging = CoreS3.Power.isCharging();
    
    // Initialize the last known values for drawBatteryStatus
    lastBatLevel = -1;  // Force first draw
    lastChargeState = false;
    
    if (batteryLevel < 20 && !isCharging) {
        CoreS3.Display.fillScreen(COLOR_BACKGROUND);
        CoreS3.Display.setTextColor(COLOR_HOT);
        CoreS3.Display.drawString("Low Battery!", CoreS3.Display.width()/2, CoreS3.Display.height()/2);
        CoreS3.Display.drawString(String(batteryLevel) + "%", CoreS3.Display.width()/2, CoreS3.Display.height()/2 + 30);
        delay(2000);
    }
    
    // Initialize display with reduced brightness to save power
    CoreS3.Display.begin();
    CoreS3.Display.setRotation(1);
    CoreS3.Display.setBrightness(64);  // Reduced brightness
    CoreS3.Display.setTextDatum(middle_center);
    
    // Initialize I2C with retry mechanism
    int retryCount = 0;
    bool sensorInitialized = false;
    
    while (!sensorInitialized && retryCount < 3) {
        // Power cycle I2C
        Wire.end();
        delay(100);
        Wire.begin(2, 1, 100000); // Use lower I2C frequency
        delay(250);
        
        CoreS3.Display.fillScreen(COLOR_BACKGROUND);
        CoreS3.Display.setTextColor(COLOR_TEXT);
        CoreS3.Display.drawString("Initializing Sensor...", CoreS3.Display.width()/2, CoreS3.Display.height()/2);
        CoreS3.Display.drawString("Attempt " + String(retryCount + 1) + "/3", CoreS3.Display.width()/2, CoreS3.Display.height()/2 + 30);
        
        if (ncir2.begin(&Wire, 2, 1, M5UNIT_NCIR2_DEFAULT_ADDR)) {
            sensorInitialized = true;
        } else {
            retryCount++;
            delay(500);
        }
    }
    
    if (!sensorInitialized) {
        CoreS3.Display.fillScreen(COLOR_BACKGROUND);
        CoreS3.Display.setTextColor(COLOR_HOT);
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.drawString("NCIR2 Init Failed!", CoreS3.Display.width()/2, CoreS3.Display.height()/2);
        CoreS3.Display.setTextSize(1);
        CoreS3.Display.drawString("Battery: " + String(batteryLevel) + "%", CoreS3.Display.width()/2, CoreS3.Display.height()/2 + 30);
        CoreS3.Display.drawString("Please check connections", CoreS3.Display.width()/2, CoreS3.Display.height()/2 + 50);
        while (1) {
            if (CoreS3.Power.isCharging()) {
                ESP.restart();  // Restart if USB is connected
            }
            delay(100);
        }
    }
    
    // Configure NCIR2 sensor with minimal settings first
    ncir2.setEmissivity(0.95);
    delay(100);
    
    // Take initial readings to stabilize
    for(int i = 0; i < TEMP_SAMPLES; i++) {
        tempReadings[i] = ncir2.getTempValue();
        delay(100);
    }
    ncir2.setConfig();  // Disable all alarms
       
    
    // Warm-up sequence with progress indication
    CoreS3.Display.fillScreen(COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Sensor Warming Up", CoreS3.Display.width()/2, CoreS3.Display.height()/2 - 20);
    
    // Progress bar setup
    int barWidth = 200;
    int barHeight = 20;
    int barX = (CoreS3.Display.width() - barWidth) / 2;
    int barY = CoreS3.Display.height()/2 + 20;
    
    // Initialize temperature readings array and take initial readings with validation
    currentTemp = ncir2.getTempValue();
    for(int i = 0; i < TEMP_SAMPLES; i++) {
        tempReadings[i] = currentTemp;
        
        // Show progress
        int progress = (i * barWidth) / TEMP_SAMPLES;
        CoreS3.Display.drawRect(barX, barY, barWidth, barHeight, COLOR_TEXT);
        CoreS3.Display.fillRect(barX, barY, progress, barHeight, COLOR_GOOD);
        
        // Take multiple readings to ensure stability
        bool readingValid = false;
        for(int j = 0; j < 5; j++) {
            int16_t temp = ncir2.getTempValue();
            if (temp != 0 && temp != -1) {
                currentTemp = temp;
                tempReadings[i] = temp;
                readingValid = true;
                break;
            }
            delay(100);
        }
        
        if (!readingValid) {
            CoreS3.Display.fillScreen(COLOR_BACKGROUND);
            CoreS3.Display.setTextColor(COLOR_HOT);
            CoreS3.Display.drawString("Sensor Reading Error", CoreS3.Display.width()/2, CoreS3.Display.height()/2);
            delay(2000);
            break;
        }
        
        delay(100);
    }
    
    // Initialize remaining system state
    lastDisplayTemp = currentTemp;
    tempIndex = 0;
    lastTempUpdate = millis();
    
    // Initialize temperature unit selection
    useCelsius = selectTemperatureUnit();
    isMonitoring = false;
    
    // Initialize display interface
    drawInterface();
    ncir2.setLEDColor(0);  // Ensure LED is off at startup
    drawBatteryStatus();
}

void loop() {
    CoreS3.update();
    
    static unsigned long lastUpdate = 0;
    static unsigned long lastActivityTime = millis();
    static unsigned long lastTrendUpdate = 0;
    unsigned long currentMillis = millis();
    
    // Update at a stable rate (every 500ms)
    if (currentMillis - lastUpdate >= 100) {
        // Get temperature
        currentTemp = getSmoothedTemperature();
        
        // Update display and status
        updateTemperatureDisplay(currentTemp);
        handleTemperatureAlerts();
        
        // Only update battery status every 1 second
        static unsigned long lastBatteryUpdate = 0;
        if (currentMillis - lastBatteryUpdate >= 1000) {
            drawBatteryStatus();
            lastBatteryUpdate = currentMillis;
        }
        
        lastUpdate = currentMillis;
    }
    
    // Handle touch input for buttons
    if (CoreS3.Touch.getCount()) {
        auto touched = CoreS3.Touch.getDetail();
        if (touched.wasPressed()) {
            // Display Mode Button
            if (touchInButton(displayModeBtn, touched.x, touched.y)) {
                // Toggle display mode
                useRawTemp = !useRawTemp;
                
                // Redraw the button with new state
                drawButton(displayModeBtn, useRawTemp ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON);
                
                // Update the indicator text
                CoreS3.Display.fillRect(displayModeBtn.x, 
                                      displayModeBtn.y + displayModeBtn.h + 2, 
                                      displayModeBtn.w, 10, COLOR_BACKGROUND);
                CoreS3.Display.setTextSize(1);
                CoreS3.Display.setTextColor(COLOR_TEXT);
                CoreS3.Display.drawString(useRawTemp ? "RAW" : "AVG", 
                                        displayModeBtn.x + (displayModeBtn.w/2), 
                                        displayModeBtn.y + displayModeBtn.h + 5);
                
                // Force immediate temperature update
                lastDisplayedTemp = -999;
                
                // Save mode preference if using preferences
                preferences.putBool("rawMode", useRawTemp);
                
                delay(100);  // Debounce
            }
            // Monitor Button
            else if (touchInButton(monitorBtn, touched.x, touched.y)) {
                // Toggle monitoring state
                isMonitoring = !isMonitoring;
                // Redraw the button with new state immediately
                drawButton(monitorBtn, isMonitoring ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON);              
                // Update LED state based on monitoring
                if (!isMonitoring) {
                    ncir2.setLEDColor(0);  // Turn off LED
                    CoreS3.Speaker.stop();  // Stop any ongoing beep
                } else {
                    // Immediately update LED based on current temperature
                    handleTemperatureAlerts();
                }
                delay(100);  // Short delay for button feedback
            }
            // Emissivity Button
            else if (touchInButton(emissivityBtn, touched.x, touched.y)) {
                adjustEmissivity();
            }
            
            // Reset activity timer on any button press
            lastActivityTime = currentMillis;
        }
    }
    
    // Auto-sleep feature
    const unsigned long SLEEP_TIMEOUT = 300000;  // 5 minutes
    if (currentMillis - lastActivityTime > SLEEP_TIMEOUT && !isMonitoring) {
        // Enter sleep mode
        CoreS3.Display.setBrightness(0);
        // Wake up on touch
        if (CoreS3.Touch.getCount()) {
            lastActivityTime = millis();
            CoreS3.Display.setBrightness(64);
            drawInterface();
        }
    }
    
    // Calculate and store temperature trend
    const unsigned long TREND_UPDATE_INTERVAL = 5000;  // Update trend every 5 seconds
    if (currentMillis - lastTrendUpdate >= TREND_UPDATE_INTERVAL) {
        // Store temperature history
        float currentTempF = (currentTemp / 100.0 * 9.0/5.0) + 32.0;
        tempHistory[historyIndex] = currentTempF;
        historyIndex = (historyIndex + 1) % 5;
        lastTrendUpdate = currentMillis;
    }
    
    // Power management and low battery warning
    if (CoreS3.Power.getBatteryLevel() < 10 && !CoreS3.Power.isCharging()) {
        static unsigned long lastWarning = 0;
        if (currentMillis - lastWarning >= 60000) {  // Show warning every minute
            CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), 30, COLOR_HOT);
            CoreS3.Display.setTextColor(COLOR_TEXT);
            CoreS3.Display.setTextSize(1);
            CoreS3.Display.drawString("LOW BATTERY!", CoreS3.Display.width()/2, 15);
            lastWarning = currentMillis;
        }
    }
    
    delay(10);  // Small delay to prevent CPU overload
}
