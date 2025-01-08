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
        const uint32_t COLOR_BACKGROUND = 0x000B1E;  // Deep navy blue
        const uint32_t COLOR_TEXT = 0x00FF9C;        // Bright cyan/mint
        const uint32_t COLOR_PRIMARY = 0x0000FF;     // Blue
        const uint32_t COLOR_SUCCESS = 0x00FF00;     // Green
        const uint32_t COLOR_WARNING = 0xFF0000;     // Red
        const uint32_t COLOR_SECONDARY = 0x808080;   // Gray
        const uint32_t COLOR_BUTTON = 0x1B3358;      // Muted blue
        const uint32_t COLOR_BUTTON_ACTIVE = 0x3CD070; // Bright green
        const uint32_t COLOR_DISABLED = 0x666666;    // Disabled button color
        const uint32_t COLOR_SHADOW = 0x333333;      // Shadow color
        const uint32_t COLOR_HIGHLIGHT = 0xFFFFFF;   // Highlight color
        const uint32_t COLOR_TEXT_DISABLED = 0xAAAAAA;  // Disabled text color
    }
    
    namespace Temperature {
        const float MIN_VALID_TEMP = -20.0f;    // Minimum valid temperature in Celsius
        const float MAX_VALID_TEMP = 720.0f;    // Maximum valid temperature in Celsius
        const float DEFAULT_TARGET_C = 37.0f;   // Default target temperature
        const float DEFAULT_TOLERANCE_C = 0.5f;  // Default temperature tolerance
        const int UPDATE_INTERVAL_MS = 100;     // Temperature reading interval
        const int DISPLAY_UPDATE_MS = 250;      // Display update interval
        const float CHANGE_THRESHOLD = 1.0f;    // Minimum change to update display
    }
    
    namespace System {
        const int DEFAULT_BRIGHTNESS = 128;
        const float DEFAULT_EMISSIVITY = 0.95f;
        const unsigned long BATTERY_CHECK_INTERVAL_MS = 30000;  // 30 seconds
        const float MIN_EMISSIVITY = 0.1f;
        const float MAX_EMISSIVITY = 1.0f;
    }
}

// Forward declaration of draw3DButton
void draw3DButton(int x, int y, int w, int h, const char* label, bool enabled = true, bool pressed = false, uint32_t color = Config::Display::COLOR_BUTTON);

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
    enum Screen { MAIN, SETTINGS, EMISSIVITY } currentScreen = MAIN;
    bool useCelsius = true;
    float lastDisplayedTemp = -999.0f;  // Track last displayed temperature
    unsigned long lastDebugTime = 0;    // Track last debug print time

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
    
    void draw(uint32_t color = Config::Display::COLOR_BUTTON) {
        draw3DButton(x, y, width, height, label, true, pressed, color);
    }
    
    bool contains(int touchX, int touchY) {
        return (touchX >= x && touchX < x + width &&
                touchY >= y && touchY < y + height);
    }
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
void draw3DButton(int x, int y, int w, int h, const char* label, bool enabled, bool pressed, uint32_t color) {
    int r = 8;  // corner radius
    uint32_t buttonColor = enabled ? color : Config::Display::COLOR_DISABLED;
    uint32_t shadowColor = Config::Display::COLOR_SHADOW;
    uint32_t highlightColor = Config::Display::COLOR_HIGHLIGHT;
    
    if (pressed) {
        // Pressed state - darker with less 3D effect
        buttonColor = CoreS3.Display.color565(
            ((buttonColor >> 11) & 0x1F) * 0.7,
            ((buttonColor >> 5) & 0x3F) * 0.7,
            (buttonColor & 0x1F) * 0.7
        );
        y += 2;  // Shift down slightly when pressed
    }
    
    // Main button body
    CoreS3.Display.fillRoundRect(x, y, w, h, r, buttonColor);
    
    // 3D effects
    if (!pressed) {
        // Top highlight
        CoreS3.Display.drawLine(x+r, y, x+w-r, y, highlightColor);
        CoreS3.Display.drawLine(x+r, y+1, x+w-r, y+1, highlightColor);
        
        // Left highlight
        CoreS3.Display.drawLine(x, y+r, x, y+h-r, highlightColor);
        CoreS3.Display.drawLine(x+1, y+r, x+1, y+h-r, highlightColor);
        
        // Bottom shadow
        CoreS3.Display.drawLine(x+r, y+h-1, x+w-r, y+h-1, shadowColor);
        CoreS3.Display.drawLine(x+r, y+h-2, x+w-r, y+h-2, shadowColor);
        
        // Right shadow
        CoreS3.Display.drawLine(x+w-1, y+r, x+w-1, y+h-r, shadowColor);
        CoreS3.Display.drawLine(x+w-2, y+r, x+w-2, y+h-r, shadowColor);
    }
    
    // Draw the label
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextColor(enabled ? Config::Display::COLOR_TEXT : Config::Display::COLOR_TEXT_DISABLED);
    CoreS3.Display.drawString(label, x + w/2, y + h/2);
}

// Temperature unit selection function
bool selectTemperatureUnit() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.setTextDatum(top_center);  // Set text alignment to center
    CoreS3.Display.setTextSize(3);
    CoreS3.Display.drawString("Select Unit", CoreS3.Display.width()/2, 40);
    
    // Landscape-optimized button positions with enabled field
    Button celsiusBtn = {20, 80, 130, 100, "C", false, true};    // Left side
    Button fahrenheitBtn = {170, 80, 130, 100, "F", false, true}; // Right side
    
    // Initial button draw
    drawButton(celsiusBtn, Config::Display::COLOR_BUTTON);
    drawButton(fahrenheitBtn, Config::Display::COLOR_BUTTON);
    
    // Draw large C and F text with 3D effect
    CoreS3.Display.setTextSize(4);  // Larger text for C/F

    // Draw C with 3D effect
    CoreS3.Display.setTextColor(0x666666);  // Darker shadow color
    CoreS3.Display.drawString("C", celsiusBtn.x + celsiusBtn.width/2 + 4, celsiusBtn.y + celsiusBtn.height/2 + 4 - 10);  // Bigger offset
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);    // Main text color
    CoreS3.Display.drawString("C", celsiusBtn.x + celsiusBtn.width/2, celsiusBtn.y + celsiusBtn.height/2 - 10);
 
    // Draw F with 3D effect
    CoreS3.Display.setTextColor(0x666666);  // Darker shadow color
    CoreS3.Display.drawString("F", fahrenheitBtn.x + fahrenheitBtn.width/2 + 4, fahrenheitBtn.y + fahrenheitBtn.height/2 + 4 - 10);  // Bigger offset
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);    // Main text color
    CoreS3.Display.drawString("F", fahrenheitBtn.x + fahrenheitBtn.width/2, fahrenheitBtn.y + fahrenheitBtn.height/2-10);
    
    // Add descriptive text below buttons
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.drawString("Celsius", celsiusBtn.x + celsiusBtn.width/2, celsiusBtn.y + celsiusBtn.height + 20);
    CoreS3.Display.drawString("Fahrenheit", fahrenheitBtn.x + fahrenheitBtn.width/2, fahrenheitBtn.y + fahrenheitBtn.height + 20);
    
    while (true) {
        CoreS3.update();
        if (CoreS3.Touch.getCount()) {
            auto touched = CoreS3.Touch.getDetail();
            if (touched.wasPressed()) {
                if (touchInButton(celsiusBtn, touched.x, touched.y)) {
                    // Highlight button
                    drawButton(celsiusBtn, Config::Display::COLOR_SUCCESS);
                    CoreS3.Display.setTextSize(4);
                    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
                    CoreS3.Display.drawString("C", celsiusBtn.x + celsiusBtn.width/2, celsiusBtn.y + celsiusBtn.height/2);
                    if (settings.soundEnabled) {
                        CoreS3.Speaker.tone(1200, 50);
                        delay(50);
                        CoreS3.Speaker.tone(1400, 50);
                    }
                    settings.useCelsius = true;  // Set to Celsius
                    state.useCelsius = true;
                    delay(150);  // Visual feedback
                    settings.save();
                    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);  // Clear screen
                    return true;
                }
                else if (touchInButton(fahrenheitBtn, touched.x, touched.y)) {
                    // Highlight button
                    drawButton(fahrenheitBtn, Config::Display::COLOR_SUCCESS);
                    CoreS3.Display.setTextSize(4);
                    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
                    CoreS3.Display.drawString("F", fahrenheitBtn.x + fahrenheitBtn.width/2, fahrenheitBtn.y + fahrenheitBtn.height/2);
                    if (settings.soundEnabled) {
                        CoreS3.Speaker.tone(1200, 50);
                        delay(50);
                        CoreS3.Speaker.tone(1400, 50);
                    }
                    settings.useCelsius = false;  // Set to Fahrenheit
                    state.useCelsius = false;
                    delay(150);  // Visual feedback
                    settings.save();
                    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);  // Clear screen
                    return false;
                }
            }
        }
        delay(10);
    }
}

void adjustEmissivity() {
    // Define constants for emissivity limits and step
    const float EMISSIVITY_MIN = 0.65f;
    const float EMISSIVITY_MAX = 1.00f;
    const float EMISSIVITY_STEP = 0.01f;

    float originalEmissivity = settings.emissivity;
    float tempEmissivity = settings.emissivity;
    
    // Ensure starting value is within limits
    if (tempEmissivity < EMISSIVITY_MIN) tempEmissivity = EMISSIVITY_MIN;
    if (tempEmissivity > EMISSIVITY_MAX) tempEmissivity = EMISSIVITY_MAX;
    
    bool valueChanged = false;

    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    
    // Draw header with gradient
    for (int i = 0; i < Config::Display::HEADER_HEIGHT; i++) {
        uint32_t gradientColor = CoreS3.Display.color565(
            map(i, 0, Config::Display::HEADER_HEIGHT, 0x3C, 0x1B),
            map(i, 0, Config::Display::HEADER_HEIGHT, 0xD0, 0x33),
            map(i, 0, Config::Display::HEADER_HEIGHT, 0x70, 0x58)
        );
        CoreS3.Display.drawFastHLine(0, i, CoreS3.Display.width(), gradientColor);
    }

    // Create buttons with landscape-optimized positions
    Button upBtn = {220, 60, 80, 60, "+", true, false};
    Button downBtn = {220, 140, 80, 60, "-", true, false};
    Button doneBtn = {10, 180, 100, 50, "Back", true, false};

    // Draw title
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString("Adjust Emissivity", CoreS3.Display.width()/2, Config::Display::HEADER_HEIGHT/2);

    // Create a larger value display box
    int valueBoxWidth = 160;
    int valueBoxHeight = 80;
    int valueBoxX = 20;
    int valueBoxY = 80;
    CoreS3.Display.drawRoundRect(valueBoxX, valueBoxY, valueBoxWidth, valueBoxHeight, 8, Config::Display::COLOR_TEXT);

    // Draw static buttons
    upBtn.draw();
    downBtn.draw();
    doneBtn.draw();

    bool adjusting = true;
    float lastDrawnValue = -1;

    while (adjusting) {
        // Only update display if value changed
        if (tempEmissivity != lastDrawnValue) {
            // Clear only the value display area
            CoreS3.Display.fillRect(valueBoxX + 5, valueBoxY + 5, valueBoxWidth - 10, valueBoxHeight - 10, Config::Display::COLOR_BACKGROUND);

            // Display current value
            char emisStr[16];
            sprintf(emisStr, "%.2f", tempEmissivity);
            CoreS3.Display.setTextSize(3);
            CoreS3.Display.drawString(emisStr, valueBoxX + (valueBoxWidth / 2), valueBoxY + valueBoxHeight / 2);
            
            lastDrawnValue = tempEmissivity;
        }

        CoreS3.update();
        if (CoreS3.Touch.getCount()) {
            auto touched = CoreS3.Touch.getDetail();
            if (touched.wasPressed()) {
                if (upBtn.contains(touched.x, touched.y)) {
                    if (tempEmissivity < EMISSIVITY_MAX) {
                        tempEmissivity += EMISSIVITY_STEP;
                        if (tempEmissivity > EMISSIVITY_MAX) tempEmissivity = EMISSIVITY_MAX;
                        valueChanged = (tempEmissivity != originalEmissivity);
                        playTouchSound(true);
                    }
                } else if (downBtn.contains(touched.x, touched.y)) {
                    if (tempEmissivity > EMISSIVITY_MIN) {
                        tempEmissivity -= EMISSIVITY_STEP;
                        if (tempEmissivity < EMISSIVITY_MIN) tempEmissivity = EMISSIVITY_MIN;
                        valueChanged = (tempEmissivity != originalEmissivity);
                        playTouchSound(true);
                    }
                } else if (doneBtn.contains(touched.x, touched.y)) {
                    adjusting = false;
                    playTouchSound(true);
                }
                delay(10);
            }
        }
    }

    // If emissivity was changed, show confirmation screen
    if (valueChanged) {
        CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
        CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.drawString("Emissivity Changed", CoreS3.Display.width() / 2, 40);
        CoreS3.Display.drawString("Restart Required", CoreS3.Display.width() / 2, 80);

        // Show old and new values
        char oldStr[32], newStr[32];
        sprintf(oldStr, "Old: %.2f", originalEmissivity);
        sprintf(newStr, "New: %.2f", tempEmissivity);
        CoreS3.Display.drawString(oldStr, CoreS3.Display.width() / 2, 120);
        CoreS3.Display.drawString(newStr, CoreS3.Display.width() / 2, 150);

        // Create confirm/cancel buttons
        Button confirmBtn = {10, 190, 145, 50, "Restart", true, false};
        Button cancelBtn = {165, 190, 145, 50, "Cancel", true, false};

        confirmBtn.draw(Config::Display::COLOR_SUCCESS);
        cancelBtn.draw(Config::Display::COLOR_WARNING);

        // Wait for user choice
        bool waiting = true;
        while (waiting) {
            CoreS3.update();
            if (CoreS3.Touch.getCount()) {
                auto touched = CoreS3.Touch.getDetail();
                if (touched.wasPressed()) {
                    if (confirmBtn.contains(touched.x, touched.y)) {
                        // Save new emissivity and restart
                        settings.emissivity = tempEmissivity;
                        settings.save();
                        playTouchSound(true);
                        delay(500);
                        ESP.restart();
                    } else if (cancelBtn.contains(touched.x, touched.y)) {
                        playTouchSound(true);
                        waiting = false;
                    }
                }
            }
            delay(10);
        }
    }

    // Return to settings screen
    state.currentScreen = SystemState::SETTINGS;
    drawSettingsScreen();
}

void drawSettingsScreen() {
    CoreS3.Display.fillScreen(Config::Display::COLOR_BACKGROUND);
    
    // Draw header
    for (int i = 0; i < Config::Display::HEADER_HEIGHT; i++) {
        uint32_t gradientColor = CoreS3.Display.color565(
            map(i, 0, Config::Display::HEADER_HEIGHT, 0x3C, 0x1B),
            map(i, 0, Config::Display::HEADER_HEIGHT, 0xD0, 0x33),
            map(i, 0, Config::Display::HEADER_HEIGHT, 0x70, 0x58)
        );
        CoreS3.Display.drawFastHLine(0, i, CoreS3.Display.width(), gradientColor);
    }
    
    // Draw title
    CoreS3.Display.setTextDatum(middle_center);
    CoreS3.Display.setTextSize(2);
    CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
    CoreS3.Display.drawString("Settings", CoreS3.Display.width()/2, Config::Display::HEADER_HEIGHT/2);
    
    // Calculate positions
    int startY = Config::Display::HEADER_HEIGHT + 20;
    int buttonHeight = 50;
    int spacing = 20;
    int buttonWidth = CoreS3.Display.width() - (2 * Config::Display::MARGIN);
    
    // Draw settings buttons
    // Sound toggle
    draw3DButton(Config::Display::MARGIN, startY, buttonWidth, buttonHeight, 
                 settings.soundEnabled ? "Sound: ON" : "Sound: OFF",
                 true, settings.soundEnabled);
    
    // Brightness control
    char brightnessText[20];
    snprintf(brightnessText, sizeof(brightnessText), "Brightness: %d%%", 
             (settings.brightness * 100) / 255);
    draw3DButton(Config::Display::MARGIN, startY + (buttonHeight + spacing), 
                 buttonWidth, buttonHeight, brightnessText, true, false);
    
    // Emissivity control
    char emissivityText[20];
    snprintf(emissivityText, sizeof(emissivityText), "Emissivity: %.2f", 
             settings.emissivity);
    draw3DButton(Config::Display::MARGIN, startY + 2*(buttonHeight + spacing),
                 buttonWidth, buttonHeight, emissivityText, true, false);
    
    // Back button
    draw3DButton(Config::Display::MARGIN, CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT,
                 buttonWidth, buttonHeight, "Back to Main", true, false);
}

void handleSettingsTouch(int touchX, int touchY) {
    int startY = Config::Display::HEADER_HEIGHT + 20;
    int buttonHeight = 50;
    int spacing = 20;
    int buttonWidth = CoreS3.Display.width() - (2 * Config::Display::MARGIN);
    int backButtonY = CoreS3.Display.height() - Config::Display::FOOTER_HEIGHT;
    
    // Check back button first since it's at the bottom
    if (touchY >= backButtonY && touchY < backButtonY + buttonHeight &&
        touchX >= Config::Display::MARGIN && touchX < Config::Display::MARGIN + buttonWidth) {
        // Back button
        if (settings.soundEnabled) {
            CoreS3.Speaker.tone(800, 50);
        }
        state.currentScreen = SystemState::MAIN;
        updateDisplay();
        return;
    }
    
    // Now check other buttons only if we're not in the back button area
    if (touchY < backButtonY) {
        // Create button areas for better touch detection
        Button soundBtn = {Config::Display::MARGIN, startY, buttonWidth, buttonHeight, 
                          settings.soundEnabled ? "Sound: ON" : "Sound: OFF", settings.soundEnabled, true};
        
        Button brightnessBtn = {Config::Display::MARGIN, startY + (buttonHeight + spacing),
                               buttonWidth, buttonHeight, "Brightness", false, false};
        
        Button emissivityBtn = {Config::Display::MARGIN, startY + 2*(buttonHeight + spacing),
                               buttonWidth, buttonHeight, "Emissivity", false, false};
        
        if (soundBtn.contains(touchX, touchY)) {
            // Sound toggle
            settings.soundEnabled = !settings.soundEnabled;
            if (settings.soundEnabled) {
                CoreS3.Speaker.tone(1000, 50);
            }
            settings.save();
            drawSettingsScreen();
        }
        else if (brightnessBtn.contains(touchX, touchY)) {
            // Brightness control
            int currentPercent = (settings.brightness * 100) / 255;
            int newPercent = ((currentPercent + 25) > 100) ? 25 : (currentPercent + 25);
            settings.brightness = (newPercent * 255) / 100;
            CoreS3.Display.setBrightness(settings.brightness);
            if (settings.soundEnabled) {
                CoreS3.Speaker.tone(1500, 50);
            }
            settings.save();
            drawSettingsScreen();
        }
        else if (emissivityBtn.contains(touchX, touchY)) {
            // Emissivity adjustment
            if (settings.soundEnabled) {
                CoreS3.Speaker.tone(1800, 50);
            }
            state.currentScreen = SystemState::EMISSIVITY;
            adjustEmissivity();
        }
    }
}

void updateDisplay() {
    // Clear the entire content area (between header and footer)
    int contentY = Config::Display::HEADER_HEIGHT;
    int contentHeight = CoreS3.Display.height() - Config::Display::HEADER_HEIGHT - Config::Display::FOOTER_HEIGHT;
    
    // Actually clear the content area
    CoreS3.Display.fillRect(0, contentY, CoreS3.Display.width(), contentHeight, Config::Display::COLOR_BACKGROUND);
    
    drawHeader();
    
    if (state.currentScreen == SystemState::MAIN) {
        drawTemperature();  // This will also draw the status box
    } else if (state.currentScreen == SystemState::SETTINGS) {
        drawSettingsScreen();
    } else if (state.currentScreen == SystemState::EMISSIVITY) {
        adjustEmissivity();
    }
    
    drawFooter();
}

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
    float currentTemp = readTemperature();
    
    // Only update if temperature changed by 1.0 degrees or more
    if (abs(currentTemp - state.lastDisplayedTemp) < 1.0f && state.lastDisplayedTemp != -999.0f) {
        return;
    }
    
    // Calculate temperature box dimensions and position
    int boxWidth = 200;
    int boxHeight = 100;
    int boxX = (CoreS3.Display.width() - boxWidth) / 2;
    int boxY = (CoreS3.Display.height() - boxHeight) / 3;  // Keep temperature box in upper third
    
    // Clear only the temperature box area
    CoreS3.Display.fillRect(boxX, boxY, boxWidth, boxHeight, Config::Display::COLOR_BACKGROUND);
    
    // Draw box border with rounded corners
    CoreS3.Display.drawRoundRect(boxX, boxY, boxWidth, boxHeight, 10, Config::Display::COLOR_TEXT);
    
    if (isValidTemperature(currentTemp)) {
        // Format temperature string
        char tempStr[10];
        float displayTemp = state.useCelsius ? currentTemp : celsiusToFahrenheit(currentTemp);
        sprintf(tempStr, "%d°%c", (int)round(displayTemp), state.useCelsius ? 'C' : 'F');
        
        // Draw temperature text
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextSize(4);
        CoreS3.Display.setTextColor(Config::Display::COLOR_TEXT);
        CoreS3.Display.drawString(tempStr, CoreS3.Display.width()/2, boxY + boxHeight/2);
        
        // Update last displayed temperature
        state.lastDisplayedTemp = currentTemp;
    } else {
        // Draw error message
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.setTextColor(Config::Display::COLOR_WARNING);
        CoreS3.Display.drawString("Error", CoreS3.Display.width()/2, boxY + boxHeight/2);
    }
    
    // Always draw status box after temperature
    drawStatusBox();
}
void drawStatusBox() {
    // Calculate status box dimensions and position
    int boxWidth = 200;
    int boxHeight = 60;
    int boxX = (CoreS3.Display.width() - boxWidth) / 2;
    int boxY = (CoreS3.Display.height() - boxHeight) / 2 + 30;  // Position below temperature box
    
    // Clear only the status box area
    CoreS3.Display.fillRect(boxX, boxY, boxWidth, boxHeight, Config::Display::COLOR_BACKGROUND);
    
    // Draw box border with rounded corners
    CoreS3.Display.drawRoundRect(boxX, boxY, boxWidth, boxHeight, 10, Config::Display::COLOR_TEXT);
    
    // Draw status message if any
    if (state.statusMessage.length() > 0) {
        CoreS3.Display.setTextDatum(middle_center);
        CoreS3.Display.setTextSize(2);
        CoreS3.Display.setTextColor(state.statusColor);
        CoreS3.Display.drawString(state.statusMessage.c_str(), CoreS3.Display.width()/2, boxY + boxHeight/2);
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

void handleTouch() {
    auto touch = CoreS3.Touch.getDetail();
    if (!touch.wasPressed()) return;
    
    if (state.currentScreen == SystemState::SETTINGS) {
        handleSettingsTouch(touch.x, touch.y);
        return;
    }
    
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
            updateDisplay();  // Update display to show monitoring state change
        }
        // Settings button
        else if (touch.x >= CoreS3.Display.width() - buttonWidth - Config::Display::MARGIN) {
            state.currentScreen = SystemState::SETTINGS;
            if (settings.soundEnabled) {
                CoreS3.Speaker.tone(1500, 50);
            }
            drawSettingsScreen();
        }
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
    
    // Clear any previous status
    state.updateStatus("");  // Ensure status starts empty
    
    // Initial display
    updateDisplay();
    state.updateStatus("Ready", Config::Display::COLOR_SUCCESS);
    drawStatusBox();  // Draw initial status box
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
                    if (state.currentScreen == SystemState::MAIN) {
                        drawTemperature();  // Will also update status box
                        state.lastDisplayUpdate = currentMillis;
                    }
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
            drawStatusBox();  // Update status box immediately
        }
        state.lastBatteryCheck = currentMillis;
    }
    
    // Handle touch events
    if (CoreS3.Touch.getCount()) {
        handleTouch();
    }
    
    // Small delay to prevent too frequent updates
    delay(50);
}

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
        
        // Debug output every 1 second
        unsigned long currentTime = millis();
        if (currentTime - state.lastDebugTime >= 1000) {  // 1000ms = 1 second
            Serial.println("\n[Debug Temperature]");
            Serial.printf("Celsius Temperature: %.2f°C\n", tempC);
            Serial.printf("Fahrenheit Temperature: %.2f°F\n", (tempC * 9.0f / 5.0f) + 32.0f);
            Serial.printf("Display Temperature: %d°%c\n", 
                         (int)round(state.useCelsius ? tempC : (tempC * 9.0f / 5.0f) + 32.0f), 
                         state.useCelsius ? 'C' : 'F');
            Serial.println("-------------------");
            state.lastDebugTime = currentTime;  // Update last debug time
        }
        
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
