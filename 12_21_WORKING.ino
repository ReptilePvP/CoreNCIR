// 1. Include statements
#include <Wire.h>
#include <M5GFX.h>
#include <M5CoreS3.h>
#include <M5Unified.h>
#include <M5UNIT_NCIR2.h>

// 2. Object initialization
M5UNIT_NCIR2 ncir2;

// 3. Constants

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

// Temperature smoothing variables
int16_t tempReadings[TEMP_SAMPLES];  // Array to store temperature readings
int tempIndex = 0;  // Index for current reading
unsigned long lastTempUpdate = 0;  // Last update time
float lastDisplayedTemp = 0;
float displaySmoothing[DISPLAY_SMOOTHING_SAMPLES] = {0};
int displaySmoothingIndex = 0;


uint8_t low_alarm_duty, high_alarm_duty, duty;
int16_t low_alarm_temp, high_alarm_temp;

// Button structure
struct Button {
    int x, y, w, h;
    const char* label;
    bool pressed;
};


// Monitor button definition
Button monitorBtn = {10, 240, 150, 40, "Monitor", false};

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

void updateTemperatureDisplay(int16_t temp) {
    // Convert raw value to actual temperature
    float actualTemp = temp / 100.0;
    
    // Convert to Fahrenheit if needed
    if (!useCelsius) {
        actualTemp = (actualTemp * 9.0/5.0) + 32.0;
    }
    
    // Add to smoothing array
    displaySmoothing[displaySmoothingIndex] = actualTemp;
    displaySmoothingIndex = (displaySmoothingIndex + 1) % DISPLAY_SMOOTHING_SAMPLES;
    
    // Calculate smoothed temperature
    float smoothedTemp = 0;
    for (int i = 0; i < DISPLAY_SMOOTHING_SAMPLES; i++) {
        smoothedTemp += displaySmoothing[i];
    }
    smoothedTemp /= DISPLAY_SMOOTHING_SAMPLES;
    
    // Only update display if change is significant
    if (abs(smoothedTemp - lastDisplayedTemp) >= DISPLAY_UPDATE_THRESHOLD) {
        // Clear previous temperature (only the number area)
        CoreS3.Display.fillRect(20, 110, CoreS3.Display.width()-40, 40, COLOR_BACKGROUND);
        
        // Display temperature as whole number
        char tempStr[16];
        sprintf(tempStr, "%d%c", (int)round(smoothedTemp), useCelsius ? 'C' : 'F');
        
        CoreS3.Display.setTextSize(3);
        CoreS3.Display.setTextColor(COLOR_TEXT);
        CoreS3.Display.drawString(tempStr, CoreS3.Display.width()/2, 130);
        
        lastDisplayedTemp = smoothedTemp;
    }
}

 // Update the getSmoothedTemperature function
int16_t getSmoothedTemperature() {
    // Get raw temperature directly
    int16_t rawTemp = ncir2.getTempValue();
    
    // Debug output
    Serial.print("Raw Temp: ");
    Serial.print(rawTemp);
    
    // Add to rolling average
    tempReadings[tempIndex] = rawTemp;
    tempIndex = (tempIndex + 1) % TEMP_SAMPLES;
    
    // Calculate average
    long sum = 0;
    for(int i = 0; i < TEMP_SAMPLES; i++) {
        sum += tempReadings[i];
    }
    
    return sum / TEMP_SAMPLES;
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
    CoreS3.Display.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 8, color);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString(btn.label, btn.x + btn.w/2, btn.y + btn.h/2);
}

void drawBatteryStatus() {
    bool bat_ischarging = CoreS3.Power.isCharging();
    int bat_level = CoreS3.Power.getBatteryLevel();
    
    // Always draw on first run or when changes occur
    if (lastBatLevel == -1 || lastBatLevel != bat_level || lastChargeState != bat_ischarging) {
        // Position calculation (to the right of Monitor button)
        int batteryX = monitorBtn.x + monitorBtn.w + 20; // 20 pixels gap from Monitor button
        int batteryY = monitorBtn.y + 10; // Align with Monitor button vertically
        
        // Clear the battery area first
        CoreS3.Display.fillRect(batteryX, batteryY, 60, 35, COLOR_BACKGROUND);
        
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
        
        // Show percentage if charging or low battery
        if (bat_ischarging || bat_level <= 20) {
            CoreS3.Display.setTextSize(1);
            CoreS3.Display.setTextColor(COLOR_TEXT);
            CoreS3.Display.drawString(String(bat_level) + "%", batteryX, batteryY + 25);
        }
        
        // Update last known values
        lastBatLevel = bat_level;
        lastChargeState = bat_ischarging;
    }
}

void drawInterface() {
    CoreS3.Display.fillScreen(COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    
    // Draw header
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.drawString("DopeMeter v2", CoreS3.Display.width()/2, 30);
    
    // Draw target temperature
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Target:", 70, 80);
    CoreS3.Display.drawString(useCelsius ? "304C" : "580F", 200, 80);
    
    // Draw boxes
    CoreS3.Display.drawRoundRect(10, 100, CoreS3.Display.width()-20, 60, 8, COLOR_TEXT);
    CoreS3.Display.drawRoundRect(10, 170, CoreS3.Display.width()-20, 60, 8, COLOR_TEXT);
    
    // Draw monitor button
    drawButton(monitorBtn, isMonitoring ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON);
    
    // Force initial battery status draw
    lastBatLevel = -1;  // Force redraw
    lastChargeState = !CoreS3.Power.isCharging();  // Force redraw
    drawBatteryStatus();  // Draw battery status
}

void handleTemperatureAlerts() {
    // Get current temperature in Fahrenheit
    float displayTemp = currentTemp / 100.0;
    if (!useCelsius) {
        displayTemp = (displayTemp * 9.0/5.0) + 32.0;
    } else {
        // If in Celsius, convert to Fahrenheit for status checks
        displayTemp = (displayTemp * 9.0/5.0) + 32.0;
    }
    
    String currentStatus;
    uint32_t statusColor;
    unsigned long currentMillis = millis();
    
    // Determine status based on Fahrenheit temperature
    if (displayTemp <= 550) {
        currentStatus = "Ready on standby!";
        statusColor = COLOR_COLD;
        if (isMonitoring) {
            ncir2.setLEDColor(0x0000FF);  // Blue
        }
    } else if (displayTemp <= 640) {
        currentStatus = "Ready to smoke!";
        statusColor = COLOR_GOOD;
        if (isMonitoring) {
            ncir2.setLEDColor(0x00FF00);  // Green
            
            // Sound alarm when in optimal range and monitoring
            if (currentMillis - lastBeepTime >= BEEP_INTERVAL) {
                CoreS3.Speaker.setVolume(128);  // Set volume (0-255)
                CoreS3.Speaker.tone(BEEP_FREQUENCY, BEEP_DURATION);
                lastBeepTime = currentMillis;
            }
        }
    } else {
        currentStatus = "WAY TO HOT";
        statusColor = COLOR_HOT;
        if (isMonitoring) {
            ncir2.setLEDColor(0xFF0000);  // Red
        }
    }
    
    // Update status if changed
    if (lastStatus != currentStatus) {
        // Clear previous status area
        CoreS3.Display.fillRect(20, 180, CoreS3.Display.width()-40, 40, COLOR_BACKGROUND);
        
        // Draw new status
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.setTextColor(statusColor);
        CoreS3.Display.drawString(currentStatus, CoreS3.Display.width()/2, 200);
        lastStatus = currentStatus;
    }
}


bool selectTemperatureUnit() {
    CoreS3.Display.fillScreen(COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.drawString("Select Unit", CoreS3.Display.width()/2, 40);
    
    // Adjusted x position from 40 to 20 to move buttons even more to the left
    Button celsiusBtn = {20, 80, 200, 70, "C", false};
    Button fahrenheitBtn = {20, 160, 200, 70, "F", false};
    
    CoreS3.Display.fillRoundRect(celsiusBtn.x, celsiusBtn.y, celsiusBtn.w, celsiusBtn.h, 8, COLOR_BUTTON);
    CoreS3.Display.fillRoundRect(fahrenheitBtn.x, fahrenheitBtn.y, fahrenheitBtn.w, fahrenheitBtn.h, 8, COLOR_BUTTON);
    
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.setTextColor(COLOR_TEXT);
    CoreS3.Display.drawString("C", celsiusBtn.x + celsiusBtn.w/2, celsiusBtn.y + celsiusBtn.h/2);
    CoreS3.Display.drawString("F", fahrenheitBtn.x + fahrenheitBtn.w/2, fahrenheitBtn.y + fahrenheitBtn.h/2);
    
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
    lastBatLevel = batteryLevel;
    lastChargeState = isCharging;
    
    if (batteryLevel < 20 && !isCharging) {
        CoreS3.Display.fillScreen(COLOR_BACKGROUND);
        CoreS3.Display.setTextColor(COLOR_HOT);
        CoreS3.Display.drawString("Low Battery!", CoreS3.Display.width()/2, CoreS3.Display.height()/2);
        CoreS3.Display.drawString(String(batteryLevel) + "%", CoreS3.Display.width()/2, CoreS3.Display.height()/2 + 30);
        delay(2000);
    }
    
    // Initialize display with reduced brightness to save power
    CoreS3.Display.begin();
    CoreS3.Display.setRotation(0);
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
}

// Update the loop function's timing
void loop() {
    CoreS3.update();
    drawBatteryStatus();
    
    static unsigned long lastUpdate = 0;
    unsigned long currentMillis = millis();
    
    // Update at a stable rate (every 100ms)
    if (currentMillis - lastUpdate >= 100) {
        // Get temperature
        currentTemp = getSmoothedTemperature();
        
        // Update display and status
        updateTemperatureDisplay(currentTemp);
        handleTemperatureAlerts();
        
        lastUpdate = currentMillis;
    }
    
    // Handle touch input
    if (CoreS3.Touch.getCount()) {
        auto touched = CoreS3.Touch.getDetail();
        if (touched.wasPressed()) {
            if (touchInButton(monitorBtn, touched.x, touched.y)) {
                isMonitoring = !isMonitoring;
                
                // Update button appearance
                drawButton(monitorBtn, isMonitoring ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON);
                
                if (!isMonitoring) {
                    ncir2.setLEDColor(0);  // Turn off LED
                    CoreS3.Speaker.stop();  // Stop any ongoing beep
                    // Or alternatively:
                    // CoreS3.Speaker.tone(0, 0);  // Stop sound by setting frequency to 0
                } else {
                    handleTemperatureAlerts();  // Immediately update LED state
                }
                
                delay(200);  // Debounce
            }
        }
    }
    
    delay(10);
}
