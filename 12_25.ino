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

// Button structure
struct Button {
    int x, y, w, h;
    const char* label;
    bool pressed;
};

// Parameters: x position, y position, width, height, label, pressed state
Button monitorBtn = {10, 180, 145, 50, "Monitor", false};      // Bottom left
Button emissivityBtn = {165, 180, 145, 50, "Emissivity", false}; // Bottom right


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
        // Similar logic for Celsius
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

    if (isMonitoring) {
        ncir2.setLEDColor(ledColor);
    }

    // Update status display if changed
    if (lastStatus != currentStatus) {
        updateStatusDisplay(currentStatus.c_str(), statusColor);
        lastStatus = currentStatus;

        // Audio alert for temperature reached
        if (isMonitoring && (currentStatus == "Perfect")) {
            M5.Speaker.tone(BEEP_FREQUENCY, BEEP_DURATION);
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
    float currentEmissivity = ncir2.getEmissivity();
    const float STEP_SIZE = 0.01;  // Explicit step size declaration
    const int boxWidth = 280;
    const int boxHeight = 120;
    const int boxX = (CoreS3.Display.width() - boxWidth) / 2;
    const int boxY = (CoreS3.Display.height() - boxHeight) / 2;
    bool adjusting = true;
    
    // Draw the adjustment box
    CoreS3.Display.fillRect(boxX, boxY, boxWidth, boxHeight, COLOR_BACKGROUND);
    CoreS3.Display.drawRect(boxX, boxY, boxWidth, boxHeight, COLOR_TEXT);
    
    // Title
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    CoreS3.Display.drawString("Adjust Emissivity", CoreS3.Display.width()/2, boxY + 25);
    
    // Draw buttons with step size indication
    CoreS3.Display.fillRect(boxX + 20, boxY + boxHeight - 50, 40, 40, COLOR_BUTTON);
    CoreS3.Display.drawString("-0.01", boxX + 40, boxY + boxHeight - 30);
    
    CoreS3.Display.fillRect(boxX + boxWidth - 60, boxY + boxHeight - 50, 40, 40, COLOR_BUTTON);
    CoreS3.Display.drawString("+0.01", boxX + boxWidth - 40, boxY + boxHeight - 30);

    while (adjusting) {
        // Clear the emissivity display area only
        CoreS3.Display.fillRect(boxX + 60, boxY + 40, boxWidth - 120, 40, COLOR_BACKGROUND);
        
        // Display current emissivity centered
        CoreS3.Display.setTextSize(3);
        char emissStr[10];
        sprintf(emissStr, "%.2f", currentEmissivity);
        CoreS3.Display.drawString(emissStr, CoreS3.Display.width()/2, boxY + 60);
        
        if (CoreS3.Touch.getCount()) {
            auto touched = CoreS3.Touch.getDetail();
            if (touched.wasPressed()) {
                if (touched.y >= boxY + boxHeight - 50 && touched.y <= boxY + boxHeight - 10) {
                    // Decrease button (-0.01)
                    if (touched.x >= boxX + 20 && touched.x <= boxX + 60) {
                        if (currentEmissivity > 0.1f) {
                            currentEmissivity -= STEP_SIZE;  // Decrease by 0.01
                            currentEmissivity = round(currentEmissivity * 100) / 100.0;  // Round to 2 decimal places
                            ncir2.setEmissivity(currentEmissivity);
                            CoreS3.Speaker.tone(1000, 50);
                        }
                    }
                    // Increase button (+0.01)
                    else if (touched.x >= boxX + boxWidth - 60 && touched.x <= boxX + boxWidth - 20) {
                        if (currentEmissivity < 1.0f) {
                            currentEmissivity += STEP_SIZE;  // Increase by 0.01
                            currentEmissivity = round(currentEmissivity * 100) / 100.0;  // Round to 2 decimal places
                            ncir2.setEmissivity(currentEmissivity);
                            CoreS3.Speaker.tone(1000, 50);
                        }
                    }
                }
                // Exit if touched outside the box
                else if (touched.y < boxY || touched.y > boxY + boxHeight ||
                         touched.x < boxX || touched.x > boxX + boxWidth) {
                    adjusting = false;
                }
                delay(100);  // Debounce
            }
        }
        delay(50);  // Prevent display refresh from being too fast
    }
    
    // Redraw main interface
    drawInterface();
    updateTemperatureDisplay(ncir2.getTempValue());
}
void updateTemperatureDisplay(int16_t temp) {
    static int lastDisplayedValue = -999;  // Track last displayed integer value
    
    // Convert raw temperature to final display value
    float currentTemp = temp / 100.0;
    int displayValue;

    if (useCelsius) {
        displayValue = (int)round(currentTemp);
    } else {
        displayValue = (int)round((currentTemp * 9.0/5.0) + 32.0);
    }

    // Update display on any change for more responsive readings
    if (displayValue != lastDisplayedValue) {
        // Clear previous temperature display area
        CoreS3.Display.fillRect(15, 40, 290, 70, COLOR_BACKGROUND);
        
        // Format and display new temperature
        char tempStr[32];
        sprintf(tempStr, "%d%c", displayValue, useCelsius ? 'C' : 'F');
        
        CoreS3.Display.setTextSize(5);
        CoreS3.Display.setTextColor(COLOR_TEXT);
        CoreS3.Display.drawString(tempStr, CoreS3.Display.width()/2, 75);
        
        lastDisplayedValue = displayValue;
    }
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
    CoreS3.Display.setBrightness(64);
    CoreS3.Display.setTextDatum(middle_center);
    
    // Initial screen
    CoreS3.Display.fillScreen(COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    
    // Initialize I2C and sensor
    int retryCount = 0;
    bool sensorInitialized = false;
    
    while (!sensorInitialized && retryCount < 3) {
        Wire.end();
        delay(100);
        Wire.begin(2, 1, 100000);
        delay(250);
        
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.drawString("Connecting to Sensor", CoreS3.Display.width()/2, 80);
        CoreS3.Display.drawString("Attempt " + String(retryCount + 1) + "/3", 
                                CoreS3.Display.width()/2, 120);
        
        if (ncir2.begin(&Wire, 2, 1, M5UNIT_NCIR2_DEFAULT_ADDR)) {
            sensorInitialized = true;
            ncir2.setEmissivity(0.95);
            ncir2.setConfig();
        } else {
            retryCount++;
            delay(500);
        }
    }
    
    if (!sensorInitialized) {
        CoreS3.Display.fillScreen(COLOR_BACKGROUND);
        CoreS3.Display.setTextColor(COLOR_HOT);
        CoreS3.Display.drawString("Sensor Init Failed!", CoreS3.Display.width()/2, CoreS3.Display.height()/2);
        while (1) {
            if (CoreS3.Power.isCharging()) ESP.restart();
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
    const int requiredStableReadings = 5;

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
    unsigned long currentMillis = millis();
    
    // Update temperature more frequently (every 100ms)
    if (currentMillis - lastUpdate >= 100) {
        // Get direct temperature reading
        currentTemp = ncir2.getTempValue();
        updateTemperatureDisplay(currentTemp);
        handleTemperatureAlerts();
        lastUpdate = currentMillis;
    }
    
    // Battery status update (every 2 seconds)
    if (currentMillis - lastBatteryUpdate >= 2000) {
        drawBatteryStatus();
        lastBatteryUpdate = currentMillis;
    }

    // Handle touch input
    if (CoreS3.Touch.getCount()) {
        auto touched = CoreS3.Touch.getDetail();
        if (touched.wasPressed()) {
            if (touchInButton(monitorBtn, touched.x, touched.y)) {
                isMonitoring = !isMonitoring;
                drawButton(monitorBtn, isMonitoring ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON);
                if (!isMonitoring) {
                    ncir2.setLEDColor(0);
                    CoreS3.Speaker.stop();
                } else {
                    handleTemperatureAlerts();
                }
            }
            else if (touchInButton(emissivityBtn, touched.x, touched.y)) {
                adjustEmissivity();
            }
            delay(100);  // Debounce
        }
    }
    
    delay(10);  // Small delay to prevent CPU overload
}