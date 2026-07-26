#include <HX711.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h> 
#include <WiFi.h>
#include "ThingSpeak.h"

// ============================================================
// HARDWARE PIN DEFINITIONS
// ============================================================
#define HX711_DT 4
#define HX711_SCK 5
#define PIN_LED_GREEN 32
#define PIN_LED_BLUE 14
#define PIN_LED_RED 33
#define PIN_SERVO 18
#define PIN_BTN_NEXT 25
#define PIN_BTN_ADJUST 26
#define PIN_BTN_RESET 27

// ============================================================
// SYSTEM CONFIGURATION
// ============================================================
float calibrationFactor = 435.5;
float emptyWeightGrams = 30.0;
float fullWeightGrams = 1500.0;
int lowThresholdPct = 40;
int criticalThresholdPct = 15;
int bedID = 1;

enum Status { STATUS_NORMAL, STATUS_LOW, STATUS_CRITICAL };
Status currentStatus = STATUS_NORMAL;
bool clampEngaged = false;

enum MenuScreen { HOME, SET_LOW, SET_CRITICAL, SET_BED };
MenuScreen currentScreen = HOME;

// ============================================================
// NETWORK CONFIGURATION
// ============================================================
const char* ssid = "AT";           
const char* password = "123456789";                  
unsigned long myChannelNumber = 3425728;
const char * myWriteAPIKey = "IC4S3U23USJGA0WE";

unsigned long lastUpload = 0;
const unsigned long UPLOAD_MS = 20000;

// ============================================================
// BUFFERING SYSTEM
// ============================================================
#define BUFFER_SIZE 100

struct DataPoint {
    int percent;
    int bedID;
    float weight;
    unsigned long timestamp;
    bool sent;
};

DataPoint dataBuffer[BUFFER_SIZE];
int bufferHead = 0;
int bufferTail = 0;
int bufferCount = 0;

unsigned long lastBufferSave = 0;
const unsigned long BUFFER_SAVE_INTERVAL = 60000;

// ============================================================
// EEPROM STORAGE
// ============================================================
#include <EEPROM.h>
#define EEPROM_SIZE 4096
#define EEPROM_MAGIC 0xDEADBEEF
#define EEPROM_HEADER_OFFSET 0
#define EEPROM_BUFFER_OFFSET 20

// ============================================================
// HARDWARE OBJECTS
// ============================================================
HX711 scale;
LiquidCrystal_I2C lcd(0x27, 16, 2); 
Servo clampServo;
WiFiClient client;

byte barBlock[8] = {0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F,0x1F};

unsigned long lastBtnNext = 0;
unsigned long lastBtnAdjust = 0;
const unsigned long DEBOUNCE_MS = 250;

const int SERVO_OPEN_ANGLE = 90;
const int SERVO_CLOSED_ANGLE = 0;

float currentWeightGrams = 0;
int currentPercent = 100;

unsigned long lastReconnectAttempt = 0;
const unsigned long RECONNECT_INTERVAL = 30000;
bool wifiConnected = false;

// ============================================================
// FUNCTION DECLARATIONS (to avoid compilation errors)
// ============================================================
void printBufferStatus();
void addReadingToBuffer(int percent, int bedID, float weight);
void processBufferUpload();
bool uploadSingleReading(DataPoint* dp);
void uploadLatestReading();
void saveBufferToEEPROM();
void restoreBufferFromEEPROM();
void writeEEPROMHeader(int count);
int readEEPROMHeader();
void connectToWiFi();
void attemptWiFiReconnection();
void readWeight();
void updateStatus();
void updateOutputs();
void updateLCD();
void printBar(int percent);
void handleButtons();

// ============================================================
// SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n\n=== SMART IV FLUID MONITORING SYSTEM ===");
    Serial.println("Version: 2.0 - Widget Optimized");
    
    EEPROM.begin(EEPROM_SIZE);
    
    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_BLUE, OUTPUT);
    pinMode(PIN_LED_RED, OUTPUT);
    pinMode(PIN_BTN_NEXT, INPUT_PULLUP);
    pinMode(PIN_BTN_ADJUST, INPUT_PULLUP);
    pinMode(PIN_BTN_RESET, INPUT_PULLUP);

    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_BLUE, LOW);
    digitalWrite(PIN_LED_RED, LOW);

    Wire.begin(21, 22); 
    lcd.init();
    lcd.backlight();
    lcd.createChar(0, barBlock);
    lcd.clear();
    
    lcd.setCursor(0, 0);
    lcd.print("IV Bed Monitor");
    lcd.setCursor(0, 1);
    lcd.print("Loading...");

    restoreBufferFromEEPROM();

    scale.begin(HX711_DT, HX711_SCK);
    if (scale.wait_ready_timeout(1000)) {
        scale.set_scale(calibrationFactor);
        scale.tare(); 
        Serial.println("Scale initialized");
    }

    clampServo.attach(PIN_SERVO);
    clampServo.write(SERVO_OPEN_ANGLE);
    clampEngaged = false;

    connectToWiFi();
    ThingSpeak.begin(client);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("System Ready");
    lcd.setCursor(0, 1);
    lcd.print(wifiConnected ? "WiFi: Connected" : "WiFi: OFFLINE");
    delay(1000);
    
    Serial.println("=== System Ready ===");
    printBufferStatus();
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
    handleButtons();
    readWeight();
    updateStatus();
    updateOutputs();
    updateLCD();
    
    // Always add current reading to buffer
    addReadingToBuffer(currentPercent, bedID, currentWeightGrams);
    
    // Upload if WiFi is available
    if (wifiConnected) {
        // Upload latest reading immediately (for real-time display)
        uploadLatestReading();
        // Then process any backlog
        processBufferUpload();
    } else {
        attemptWiFiReconnection();
    }
    
    if (millis() - lastBufferSave >= BUFFER_SAVE_INTERVAL) {
        lastBufferSave = millis();
        saveBufferToEEPROM();
        printBufferStatus();
    }

    delay(100);
}

// ============================================================
// UPLOAD FUNCTIONS - FOR THINGSPEAK WIDGETS
// ============================================================

bool uploadSingleReading(DataPoint* dp) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("✗ WiFi not connected!");
        return false;
    }
    
    Serial.println("\n=== UPLOADING TO THINGSPEAK ===");
    Serial.println("Field 1 (Percent): " + String(dp->percent) + "% ← GAUGE & LAMP Widgets");
    Serial.println("Field 3 (Bed ID): " + String(dp->bedID) + " ← Bed Identification");
    Serial.println("Field 4 (Weight): " + String(dp->weight) + "g ← NUMERIC DISPLAY Widget");
    
    // IMPORTANT: Field 2 is REMOVED - Lamp and Gauge both use Field 1
    ThingSpeak.setField(1, dp->percent);    // Gauge AND Lamp Indicator (both use this)
    ThingSpeak.setField(3, dp->bedID);      // Bed ID (optional)
    ThingSpeak.setField(4, dp->weight);     // Numeric display (Weight in grams)
    
    int code = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    
    if (code == 200) {
        Serial.println("✓ SUCCESS! Data uploaded.");
        Serial.println("  Gauge/Lamp: " + String(dp->percent) + "%");
        Serial.println("  Weight: " + String(dp->weight) + "g");
        return true;
    } else {
        Serial.println("✗ FAILED! Error: " + String(code));
        if (code == 401) Serial.println("  → Check Write API Key!");
        if (code == 404) Serial.println("  → Check Channel ID!");
        if (code == 429) Serial.println("  → Rate limited! Wait longer.");
        return false;
    }
}

void uploadLatestReading() {
    // For immediate updates - uses current values
    if (WiFi.status() != WL_CONNECTED) return;
    
    Serial.println("\n--- UPLOADING LATEST READING ---");
    
    ThingSpeak.setField(1, currentPercent);    // Gauge & Lamp
    ThingSpeak.setField(3, bedID);             // Bed ID
    ThingSpeak.setField(4, currentWeightGrams); // Weight
    
    int code = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    
    if (code == 200) {
        Serial.println("✓ Latest data uploaded successfully!");
        Serial.println("  Percent: " + String(currentPercent) + "%");
        Serial.println("  Weight: " + String(currentWeightGrams) + "g");
    } else {
        Serial.println("✗ Latest upload failed: " + String(code));
        // If latest upload fails, add to buffer for retry
        addReadingToBuffer(currentPercent, bedID, currentWeightGrams);
    }
}

void processBufferUpload() {
    if (bufferCount == 0 || WiFi.status() != WL_CONNECTED) return;
    
    int uploaded = 0;
    int failed = 0;
    
    Serial.println("\n--- PROCESSING BUFFER (" + String(bufferCount) + " readings) ---");
    
    for (int i = 0; i < bufferCount; i++) {
        int idx = (bufferTail + i) % BUFFER_SIZE;
        DataPoint* dp = &dataBuffer[idx];
        
        if (!dp->sent) {
            if (uploadSingleReading(dp)) {
                dp->sent = true;
                uploaded++;
                bufferTail = (bufferTail + 1) % BUFFER_SIZE;
                bufferCount--;
                delay(100);  // Small delay between uploads
            } else {
                failed++;
                break;  // Stop if upload fails
            }
        }
    }
    
    if (uploaded > 0 || failed > 0) {
        Serial.println("Uploaded: " + String(uploaded) + ", Failed: " + String(failed) + ", Remaining: " + String(bufferCount));
        saveBufferToEEPROM();
    }
}

// ============================================================
// BUFFER MANAGEMENT
// ============================================================

void addReadingToBuffer(int percent, int bedID, float weight) {
    if (bufferCount >= BUFFER_SIZE) {
        // Buffer full - overwrite oldest
        bufferTail = (bufferTail + 1) % BUFFER_SIZE;
        bufferCount--;
        Serial.println("WARNING: Buffer full - overwriting oldest");
    }
    
    dataBuffer[bufferHead].percent = percent;
    dataBuffer[bufferHead].bedID = bedID;
    dataBuffer[bufferHead].weight = weight;
    dataBuffer[bufferHead].timestamp = millis();
    dataBuffer[bufferHead].sent = false;
    
    bufferHead = (bufferHead + 1) % BUFFER_SIZE;
    bufferCount++;
}

void saveBufferToEEPROM() {
    writeEEPROMHeader(bufferCount);
    
    int addr = EEPROM_BUFFER_OFFSET;
    for (int i = 0; i < bufferCount; i++) {
        int idx = (bufferTail + i) % BUFFER_SIZE;
        DataPoint* dp = &dataBuffer[idx];
        
        EEPROM.writeInt(addr, dp->percent); addr += 4;
        EEPROM.writeInt(addr, dp->bedID); addr += 4;
        EEPROM.writeFloat(addr, dp->weight); addr += 4;
        EEPROM.writeInt(addr, (int)dp->timestamp); addr += 4;
        EEPROM.writeByte(addr, dp->sent ? 1 : 0); addr += 1;
    }
    
    EEPROM.commit();
}

void restoreBufferFromEEPROM() {
    int count = readEEPROMHeader();
    
    if (count <= 0 || count > BUFFER_SIZE) {
        Serial.println("No valid buffer in EEPROM");
        bufferCount = 0;
        bufferHead = 0;
        bufferTail = 0;
        return;
    }
    
    Serial.println("Restoring " + String(count) + " readings from EEPROM");
    
    int addr = EEPROM_BUFFER_OFFSET;
    for (int i = 0; i < count; i++) {
        DataPoint* dp = &dataBuffer[i];
        
        dp->percent = EEPROM.readInt(addr); addr += 4;
        dp->bedID = EEPROM.readInt(addr); addr += 4;
        dp->weight = EEPROM.readFloat(addr); addr += 4;
        dp->timestamp = (unsigned long)EEPROM.readInt(addr); addr += 4;
        dp->sent = EEPROM.readByte(addr) == 1; addr += 1;
    }
    
    bufferCount = count;
    bufferHead = count % BUFFER_SIZE;
    bufferTail = 0;
}

void writeEEPROMHeader(int count) {
    EEPROM.writeInt(EEPROM_HEADER_OFFSET, EEPROM_MAGIC);
    EEPROM.writeInt(EEPROM_HEADER_OFFSET + 4, count);
    EEPROM.writeInt(EEPROM_HEADER_OFFSET + 8, bufferHead);
    EEPROM.writeInt(EEPROM_HEADER_OFFSET + 12, bufferTail);
    EEPROM.commit();
}

int readEEPROMHeader() {
    int magic = EEPROM.readInt(EEPROM_HEADER_OFFSET);
    if (magic != EEPROM_MAGIC) return -1;
    
    int count = EEPROM.readInt(EEPROM_HEADER_OFFSET + 4);
    bufferHead = EEPROM.readInt(EEPROM_HEADER_OFFSET + 8);
    bufferTail = EEPROM.readInt(EEPROM_HEADER_OFFSET + 12);
    return count;
}

// ============================================================
// WIFI MANAGEMENT
// ============================================================

void connectToWiFi() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");
    lcd.setCursor(0, 1);
    lcd.print(ssid);
    
    WiFi.begin(ssid, password);
    
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
        digitalWrite(PIN_LED_GREEN, HIGH);
        delay(250);
        digitalWrite(PIN_LED_GREEN, LOW);
        delay(250);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("\n✓ WiFi connected! IP: " + WiFi.localIP().toString());
    } else {
        wifiConnected = false;
        Serial.println("\n✗ WiFi connection failed! Operating offline.");
    }
}

void attemptWiFiReconnection() {
    if (wifiConnected) return;
    
    unsigned long now = millis();
    if (now - lastReconnectAttempt < RECONNECT_INTERVAL) return;
    lastReconnectAttempt = now;
    
    Serial.println("Attempting WiFi reconnection...");
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("✓ WiFi reconnected!");
        processBufferUpload();
        return;
    }
    
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(100);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("✓ WiFi reconnected!");
        processBufferUpload();
    } else {
        wifiConnected = false;
        Serial.println("✗ WiFi reconnection failed");
    }
}

// ============================================================
// SENSOR FUNCTIONS
// ============================================================

void readWeight() {
    if (scale.wait_ready_timeout(80)) {
        currentWeightGrams = scale.get_units(5);
        if (currentWeightGrams < 0) currentWeightGrams = 0;

        currentPercent = (int)round(
            ((currentWeightGrams - emptyWeightGrams) / (fullWeightGrams - emptyWeightGrams)) * 100.0
        );
        currentPercent = constrain(currentPercent, 0, 100);
    }
}

void updateStatus() {
    if (currentPercent <= criticalThresholdPct) {
        currentStatus = STATUS_CRITICAL;
    } else if (currentPercent <= lowThresholdPct) {
        currentStatus = STATUS_LOW;
    } else {
        currentStatus = STATUS_NORMAL;
    }
}

// ============================================================
// OUTPUT CONTROL FUNCTIONS
// ============================================================

void updateOutputs() {
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_BLUE, LOW);
    digitalWrite(PIN_LED_RED, LOW);

    if (currentStatus == STATUS_NORMAL) {
        digitalWrite(PIN_LED_GREEN, HIGH);
    } else if (currentStatus == STATUS_LOW) {
        digitalWrite(PIN_LED_BLUE, HIGH);
        if (bufferCount > 0 && (millis() / 500) % 2 == 0) {
            digitalWrite(PIN_LED_BLUE, LOW);
        }
    } else if (currentStatus == STATUS_CRITICAL) {
        digitalWrite(PIN_LED_RED, HIGH);
        if (bufferCount > 0 && (millis() / 500) % 2 == 0) {
            digitalWrite(PIN_LED_RED, LOW);
        }
    }

    // Servo control
    if (currentStatus == STATUS_LOW || currentStatus == STATUS_CRITICAL) {
        if (!clampEngaged) {
            clampServo.write(SERVO_CLOSED_ANGLE); 
            clampEngaged = true;
            Serial.println("[Servo] CLAMPED");
        }
    } else {
        if (clampEngaged && (currentPercent > (lowThresholdPct + 3))) {
            clampServo.write(SERVO_OPEN_ANGLE);  
            clampEngaged = false;
            Serial.println("[Servo] OPENED");
        }
    }
}

// ============================================================
// LCD DISPLAY FUNCTIONS
// ============================================================

void updateLCD() {
    lcd.clear();
    
    switch (currentScreen) {
        case HOME: {
            lcd.setCursor(0, 0);
            lcd.print("B");
            lcd.print(bedID);
            lcd.print(" ");
            
            switch (currentStatus) {
                case STATUS_NORMAL:   lcd.print("NRM"); break;
                case STATUS_LOW:      lcd.print("LOW"); break;
                case STATUS_CRITICAL: lcd.print("CRT"); break;
            }
            
            lcd.print(" ");
            lcd.print((int)currentWeightGrams);
            lcd.print("g");
            
            if (bufferCount > 0) {
                lcd.setCursor(14, 0);
                lcd.print("B");
                lcd.print(bufferCount);
            }
            
            lcd.setCursor(0, 1);
            lcd.print(currentPercent);
            lcd.print("% ");
            printBar(currentPercent);
            
            lcd.setCursor(14, 1);
            lcd.print(wifiConnected ? "W" : "X");
            break;
        }
        case SET_LOW:
            lcd.setCursor(0, 0);
            lcd.print("Set LOW thresh  ");
            lcd.setCursor(0, 1);
            lcd.print(lowThresholdPct);
            lcd.print("%               ");
            break;
        case SET_CRITICAL:
            lcd.setCursor(0, 0);
            lcd.print("Set CRIT thresh ");
            lcd.setCursor(0, 1);
            lcd.print(criticalThresholdPct);
            lcd.print("%               ");
            break;
        case SET_BED:
            lcd.setCursor(0, 0);
            lcd.print("Set Bed ID      ");
            lcd.setCursor(0, 1);
            lcd.print(bedID);
            lcd.print("                ");
            break;
    }
}

void printBar(int percent) {
    int filled = map(percent, 0, 100, 0, 10);
    for (int i = 0; i < 10; i++) {
        lcd.write(i < filled ? byte(0) : byte(' '));
    }
}

// ============================================================
// BUTTON HANDLING
// ============================================================

void handleButtons() {
    unsigned long now = millis();

    if (digitalRead(PIN_BTN_NEXT) == LOW && now - lastBtnNext > DEBOUNCE_MS) {
        lastBtnNext = now;
        currentScreen = (MenuScreen)((currentScreen + 1) % 4);
        lcd.clear(); 
    }

    if (digitalRead(PIN_BTN_ADJUST) == LOW && now - lastBtnAdjust > DEBOUNCE_MS) {
        lastBtnAdjust = now;
        switch (currentScreen) {
            case SET_LOW:
                lowThresholdPct += 5;
                if (lowThresholdPct > 90) lowThresholdPct = 5;
                break;
            case SET_CRITICAL:
                criticalThresholdPct += 5;
                if (criticalThresholdPct > 90) criticalThresholdPct = 5;
                break;
            case SET_BED:
                bedID++;
                if (bedID > 20) bedID = 1;
                break;
            default:
                break;
        }
    }
}

// ============================================================
// STATUS REPORTING FUNCTIONS
// ============================================================

void printBufferStatus() {
    Serial.println("--- Buffer Status ---");
    Serial.println("Count: " + String(bufferCount) + "/" + String(BUFFER_SIZE));
    Serial.println("WiFi: " + String(wifiConnected ? "Connected" : "Disconnected"));
    if (bufferCount > 0) {
        Serial.println("Oldest: " + String(dataBuffer[bufferTail].percent) + "%");
        int lastIdx = (bufferHead - 1 + BUFFER_SIZE) % BUFFER_SIZE;
        Serial.println("Newest: " + String(dataBuffer[lastIdx].percent) + "%");
    }
    Serial.println("-------------------");
}
